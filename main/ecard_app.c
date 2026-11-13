#include "ecard_app.h"
#if defined(ESP_PLATFORM)
#include <esp_log.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#else
#define ESP_LOGI(tag, fmt, ...) do { (void)(tag); } while (0)
#define ESP_LOGW(tag, fmt, ...) do { (void)(tag); } while (0)
#endif
#include "ecard_ai_prompt.h"
#include "ecard_cards.h"
#include "ecard_netlink.h"
#include "ecard_flashcard.h"
#include "ecard_messages.h"
#include "ecard_network.h"
#include "ecard_pomodoro.h"
#include "ecard_portal.h"
#include "ecard_setup.h"
#include "ecard_profile.h"
#include "ecard_quiz.h"
#include "ecard_ui.h"
#include "ecard_voice.h"
#include "ecard_xiaozhi.h"
#include <stdio.h>
#include <string.h>

// 长按 DOWN 的确认窗口。BSP 在 500ms 就产生 LONG 事件，因此这里再做 3 秒
// 倒计时，让用户有机会松手取消，避免误触报警。
#define ECARD_SOS_CONFIRM_MS 3000

static ecard_app_state_t s_app_state = ECARD_APP_STATE_IDLE;
static uint8_t s_play_progress_sec = 0;
static uint8_t s_play_total_sec = 0;
static bool s_has_polled_current_recess = false;
static uint8_t s_last_recess_minute = 0xFF;
static uint16_t s_sos_remaining_ms = 0;
static ecard_ui_page_t s_page_before_sos = ECARD_UI_PAGE_HOME;
// 播放来源：本机刚录完的试听（有声）或列表里的留言（无网络音频时用演示语音）
static bool s_play_has_audio = false;
// 当前录音缓冲属于哪一条已发送留言：只有它回放时用真实录音，
// 网络留言仍用内置演示语音。
static uint32_t s_clip_message_id = 0;
static bool s_play_clears_unread = false;
// 正在播放的留言 ID：停止时只把这一条标记为已读。
static uint32_t s_play_message_id = 0;
// 设备页“长按 UP 重试连接”的进行态与超时。
static bool s_net_retrying = false;
// 小智只在答题讲解、古诗/单词朗读页启用；首页录音与留言不触发。
static bool s_ai_session = false;
static bool s_ai_listen = false;
// 本轮追问是否由“长按 UP”启动：只有这种录音才在松开 UP 时自动发送。
static bool s_ai_long_listen = false;
// 追问结束（松手或满 10 秒）后吞掉随之而来的 UP CLICK，避免误翻选项。
static bool s_ai_swallow_up_click = false;
// 上一次会话是否正在后台收尾（TLS 关闭 + 找回录音缓冲）。
static bool s_ai_stopping = false;
// 会话异常重建：stop 完成后立即重开一次。
static bool s_ai_reopen = false;
static uint16_t s_ai_listen_ms = 0;
static uint16_t s_net_retry_ms = 0;
// 录音浮层按 100ms 刷新一次，避免 20ms 音频块把 LVGL 刷屏
static uint8_t s_rec_ui_div = 0;

#define ECARD_REC_UI_DIV 5
#define ECARD_NET_RETRY_TIMEOUT_MS 10000

static void sync_unread_messages(void) {
    bool has_new = false;
    char sender[32] = {0};
    uint8_t dur = 0;
    uint32_t message_id = 0;
    if (!ecard_network_poll_recess_messages(&has_new, sender, &dur, &message_id) ||
        !has_new) {
        return;
    }

    ecard_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.id = message_id;
    msg.direction = ECARD_MESSAGE_RECEIVED;
    msg.duration_sec = dur;
    msg.unread = true;
    snprintf(msg.peer, sizeof(msg.peer), "%s", sender[0] ? sender : "家长");
    // 列表按 ID 去重：已读的同一条留言再次轮询到时不会重新变回未读。
    ecard_messages_add(&msg, 0);
    ecard_ui_refresh_content();
}

static void start_playback(const ecard_message_t *msg) {
    if (!msg) return;

    s_play_progress_sec = 0;
    s_play_total_sec = msg->duration_sec ? msg->duration_sec : 1;
    s_play_message_id = msg->id;
    s_play_clears_unread = (msg->direction == ECARD_MESSAGE_RECEIVED && msg->unread);

    // 只有“刚录的那条”留言能用真实录音；网络下发的留言没有音频负载，
    // 用内置演示语音顶上，避免“无声倒计时”。
    s_play_has_audio = (msg->id == s_clip_message_id) && ecard_voice_playback_start();
    if (!s_play_has_audio) {
        s_play_has_audio = ecard_voice_playback_start_demo();
        if (s_play_has_audio) {
            s_play_total_sec = ecard_voice_demo_duration_sec();
        }
    }
    s_app_state = ECARD_APP_STATE_PLAYING;
    ecard_ui_show_playing_modal(0, s_play_total_sec);
}

static void stop_playback(void) {
    ecard_voice_playback_stop();
    if (s_play_clears_unread && s_play_message_id) {
        ecard_messages_mark_read(s_play_message_id);
    }
    s_play_has_audio = false;
    s_play_clears_unread = false;
    s_play_message_id = 0;
    // 不要释放录音缓冲：学生还要在留言列表里重听刚才的录音。
    // 下次录音开始时 ecard_voice_start_recording() 会自动清空重录。
    ecard_ui_hide_modal();
    ecard_ui_refresh_content();
    s_app_state = ECARD_APP_STATE_IDLE;
}


#define ECARD_AI_LISTEN_MAX_MS 10000

static bool ai_page_active(ecard_ui_page_t page) {
    // 番茄钟也算 AI 页：专注期间长按 UP 可以随时问小智，多轮对话不掉线。
    return page == ECARD_UI_PAGE_QUIZ || page == ECARD_UI_PAGE_POEM ||
           page == ECARD_UI_PAGE_WORD || page == ECARD_UI_PAGE_POEM_PICK ||
           page == ECARD_UI_PAGE_WORD_PICK || page == ECARD_UI_PAGE_POMODORO;
}

static void ai_session_begin(void) {
    // 上一次会话正在后台收尾（TLS 关闭 + 找回录音缓冲），此时不要抢跑。
    if (s_ai_stopping) return;
    if (ecard_portal_is_active()) {
        ecard_portal_stop();
    }
    if (!s_ai_session) {
        ESP_LOGI("ecard_ai", "建立小智会话");
        // TLS + Opus 约需 40 KB：把上一次录音占着的 40 KB 缓冲让出来，
        // 空闲堆足够时握手后才收得到服务端 hello。
        ecard_voice_clip_release();
        s_clip_message_id = 0;
        s_ai_session = true;
    }
    // 连接失败后再次进入 AI 入口时允许重试；已在连接中则直接返回。
    ecard_xiaozhi_session_start();
}

#if defined(ESP_PLATFORM)
// 关闭 TLS/WebSocket 会阻塞几百毫秒；绝不能放在 LVGL 锁里，
// 否则按键要等 500 ms 才拿到锁（实测连续丢 4~5 个按键）。
static void ai_stop_task(void *arg) {
    (void)arg;
    ecard_xiaozhi_session_stop();
    // 录音缓冲不再常驻：下次录音时按当时空闲堆直接分配（正常都是 10 秒）。
    s_ai_stopping = false;
    // 会话异常重建路径：学生仍留在 AI 页面，立刻重连，不用等下一次按键。
    if (s_ai_reopen && ai_page_active(ecard_ui_get_page())) {
        s_ai_reopen = false;
        ai_session_begin();
    }
    vTaskDelete(NULL);
}
#endif

static void ai_session_end(void) {
    if (!s_ai_session) return;
    s_ai_listen = false;
    s_ai_long_listen = false;
    s_ai_swallow_up_click = false;
    s_ai_session = false;
#if defined(ESP_PLATFORM)
    if (!s_ai_stopping) {
        s_ai_stopping = true;
        if (xTaskCreate(ai_stop_task, "ai_stop", 4096, NULL, 3, NULL) == pdPASS) {
            return;
        }
        s_ai_stopping = false;
    } else {
        return;
    }
#endif
    ecard_xiaozhi_session_stop();
}

/* 小智文本入口的长度上限与提示词构造统一放在 ecard_ai_prompt.c，
 * 那里的规则由真机 + 官方服务端实测得出，并有主机测试逐卡校验。 */

static void ai_ask_quiz(void) {
    const ecard_quiz_question_t *q = ecard_quiz_current();
    if (!q) return;
    ESP_LOGI("ecard_ai", "触发讲题：%s", q->prompt);
    ai_session_begin();
    char prompt[96];
    ecard_ai_build_quiz_prompt(q, prompt, sizeof(prompt));
    ESP_LOGI("ecard_ai", "小智提问(%u字): %s", (unsigned)ecard_ai_utf8_chars(prompt), prompt);
    ecard_xiaozhi_ask(prompt);
}

static void ai_ask_poem(void) {
    const ecard_card_t *c = ecard_flashcard_current();
    if (!c) return;
    ESP_LOGI("ecard_ai", "触发读诗：%s", c->front);
    ai_session_begin();
    // 卡片上的上下句本身就是最好的上下文：小智能据此判断诗题、作者，
    // 以及“超过四句时该读哪四句”。两句合计不超过 15 字（上限 16 字）。
    char prompt[96];
    ecard_ai_build_poem_prompt(c, prompt, sizeof(prompt));
    ESP_LOGI("ecard_ai", "小智提问(%u字): %s", (unsigned)ecard_ai_utf8_chars(prompt), prompt);
    ecard_xiaozhi_ask(prompt);
}

static void ai_ask_word(void) {
    const ecard_card_t *c = ecard_flashcard_current();
    if (!c) return;
    ESP_LOGI("ecard_ai", "触发读词：%s", c->front);
    ai_session_begin();
    // 分两轮保证都不超限：先"读:单词,词义"（<=16 字），播完自动追问
    // 卡片原句例句（纯英文 <=30 字节）。构造规则见 ecard_ai_prompt.c。
    char first[96];
    char example[64];
    ecard_ai_build_word_prompts(c, first, sizeof(first), example, sizeof(example));
    if (example[0]) {
        ESP_LOGI("ecard_ai", "小智提问(%u字): %s，随后追问例句：%s",
                 (unsigned)ecard_ai_utf8_chars(first), first, example);
        ecard_xiaozhi_ask_with_follow(first, example);
        return;
    }
    ESP_LOGI("ecard_ai", "小智提问(%u字): %s", (unsigned)ecard_ai_utf8_chars(first), first);
    ecard_xiaozhi_ask(first);
}

static bool ai_start_listen(void) {
    if (!ecard_netlink_is_connected()) {
        ESP_LOGW("ecard_ai", "追问：设备未联网");
        ecard_ui_set_hint("没有联网：请先连上 Wi-Fi 再长按 UP");
        return false;
    }
    // 必须每次都走一遍：会话可能已被服务端关掉或卡在重连，
    // 只有重新调用 session_start 才能触发重建，而不是永远显示“连接中”。
    ai_session_begin();
    if (s_ai_stopping) {
        ecard_ui_set_hint("小智正在重连，请稍候再长按 UP");
        return false;
    }
    if (!ecard_xiaozhi_session_ready()) {
        ESP_LOGW("ecard_ai", "追问：会话尚未就绪");
        ecard_ui_set_hint("小智连接中，连上后再长按 UP");
        return false;
    }

    // 先停掉上一轮回答，避免麦克风把扬声器声录进去。
    ecard_xiaozhi_reset_playback();
    ecard_xiaozhi_reset_mic();
    if (!ecard_xiaozhi_listen_start()) {
        ESP_LOGW("ecard_ai", "追问：小智无法开始采音");
        ecard_ui_set_hint("小智暂时听不到，请稍后再按 UP");
        s_app_state = ECARD_APP_STATE_IDLE;
        return false;
    }
    s_ai_listen = true;
    s_ai_long_listen = false;
    s_ai_swallow_up_click = false;
    s_ai_listen_ms = 0;
    s_app_state = ECARD_APP_STATE_RECORDING;
    ecard_ui_show_ai_listen_modal(10, 0);
    ESP_LOGI("ecard_ai", "开始追问录音，松开 UP 发送");
    return true;
}

static void ai_finish_listen(void) {
    if (!s_ai_listen) return;
    ecard_xiaozhi_listen_stop();
    const bool had_audio = ecard_xiaozhi_listen_had_audio();
    s_ai_listen = false;
    s_ai_long_listen = false;
    // 满 10 秒自动停止时学生可能还按着 UP：记录一次待吞掉的 CLICK。
    s_ai_swallow_up_click = true;
    ecard_ui_hide_modal();
    s_app_state = ECARD_APP_STATE_IDLE;
    if (had_audio) {
        ecard_ui_set_hint("已发送，等小智回答…");
    } else {
        ecard_ui_set_hint("没听到声音，请再长按 UP 说一次");
    }
    ESP_LOGI("ecard_ai", "追问录音结束，音频帧%s", had_audio ? "已上传" : "为空");
}

bool ecard_app_ai_listening(void) {
    return s_ai_listen;
}

static void start_recording(void) {
    if (!ecard_voice_start_recording()) return;
    uint8_t max_sec = (uint8_t)(ecard_voice_buffer_capacity() / ECARD_VOICE_BYTES_PER_SEC);
    if (max_sec == 0) max_sec = 1;
    if (max_sec > ECARD_VOICE_RECORD_SEC) max_sec = ECARD_VOICE_RECORD_SEC;
    s_rec_ui_div = 0;
    s_app_state = ECARD_APP_STATE_RECORDING;
    ecard_ui_show_recording_modal(max_sec, 0);
}

static void finish_recording(void) {
    const uint8_t *adpcm_data = NULL;
    size_t len = 0;
    uint8_t dur = 0;
    if (ecard_voice_stop_recording(&adpcm_data, &len, &dur)) {
        ecard_network_upload_voice(adpcm_data, len, dur);
        // 发出的留言同样进入列表，方便之后回看与重听。
        ecard_message_t sent;
        memset(&sent, 0, sizeof(sent));
        sent.direction = ECARD_MESSAGE_SENT;
        sent.duration_sec = dur;
        snprintf(sent.peer, sizeof(sent.peer), "%s", "妈妈");
        s_clip_message_id = ecard_messages_add(&sent, 0);

        // 上传后立即本地试听，确认麦克风真的录到了声音。
        s_play_progress_sec = 0;
        s_play_total_sec = dur ? dur : 1;
        s_play_has_audio = ecard_voice_playback_start();
        s_play_clears_unread = false;
        s_app_state = ECARD_APP_STATE_PLAYING;
        ecard_ui_show_playing_modal(0, s_play_total_sec);
        return;
    }
    ecard_voice_clip_release();
    ecard_ui_hide_modal();
    s_app_state = ECARD_APP_STATE_IDLE;
}

static void begin_sos_pending(void) {
    if (s_app_state == ECARD_APP_STATE_RECORDING) {
        const uint8_t *adpcm = NULL;
        size_t len = 0;
        uint8_t dur = 0;
        (void)ecard_voice_stop_recording(&adpcm, &len, &dur);
        ecard_voice_clip_release();
    } else if (s_app_state == ECARD_APP_STATE_PLAYING) {
        ecard_voice_playback_stop();
        ecard_voice_clip_release();
        s_play_has_audio = false;
        s_play_clears_unread = false;
    }
    s_page_before_sos = ecard_ui_get_page();
    ecard_ui_hide_modal();
    s_app_state = ECARD_APP_STATE_SOS_PENDING;
    s_sos_remaining_ms = ECARD_SOS_CONFIRM_MS;
    ecard_ui_show_sos_pending(3);
}

static void cancel_sos_pending(void) {
    ecard_ui_hide_modal();
    s_app_state = ECARD_APP_STATE_IDLE;
    s_sos_remaining_ms = 0;
    ecard_ui_show_page(s_page_before_sos);
}

static void send_sos_alert(void) {
    ecard_ui_hide_modal();
    s_app_state = ECARD_APP_STATE_SOS_SENT;
    s_sos_remaining_ms = 0;
    const ecard_profile_t *p = ecard_profile_get();
    ecard_network_send_sos_alert(p->student_id, "校园紧急报警");
    ecard_ui_show_sos_sent();
}

static void leave_sos_sent(void) {
    ecard_ui_hide_modal();
    s_app_state = ECARD_APP_STATE_IDLE;
    ecard_ui_show_page(ECARD_UI_PAGE_HOME);
}

// 菜单 PRESS 已经移动过时，随后的 CLICK 只负责清标记，不能再移动一次。
static bool s_menu_press_moved = false;

static void handle_idle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    const ecard_ui_page_t page = ecard_ui_get_page();

    // 菜单导航在“按下”瞬间响应。按住超过 500 ms 时按键库只会发 LONG、
    // 不再发 CLICK，之前用户按住稍久就觉得菜单卡了两秒。
    if (page == ECARD_UI_PAGE_MENU &&
        (btn == BSP_BTN_UP || btn == BSP_BTN_DOWN)) {
        if (ev == BSP_BTN_PRESS) {
            ecard_ui_menu_move(btn == BSP_BTN_UP ? -1 : 1);
            s_menu_press_moved = true;
            return;
        }
        if (ev == BSP_BTN_CLICK) {
            if (s_menu_press_moved) {
                s_menu_press_moved = false;
                return;
            }
            ecard_ui_menu_move(btn == BSP_BTN_UP ? -1 : 1);
            return;
        }
    }

    // AI 页面：长按 UP 开始追问，松开 UP 发送。必须放在各页面的
    // 无条件 return 之前，否则答题/古诗/单词页会把 LONG 事件吞掉。
    if (ai_page_active(page) && btn == BSP_BTN_UP) {
        if (ev == BSP_BTN_LONG) {
            if (!s_ai_listen && ai_start_listen()) {
                s_ai_long_listen = true;
            }
            return;
        }
        if (ev == BSP_BTN_RELEASE && s_ai_long_listen) {
            ai_finish_listen();
            return;
        }
        if (ev == BSP_BTN_CLICK && s_ai_swallow_up_click) {
            // 追问录音结束后松手仍可能补发一个 CLICK，这里吞掉，避免误翻选项。
            s_ai_swallow_up_click = false;
            return;
        }
    }

    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        if (page == ECARD_UI_PAGE_HOME) {
            start_recording();
        } else if (page == ECARD_UI_PAGE_MENU) {
            ecard_ui_show_page(ECARD_UI_PAGE_HOME);
        } else {
            if (ai_page_active(page)) {
                ecard_xiaozhi_reset_playback();
            }
            ecard_ui_show_page(ECARD_UI_PAGE_MENU);
        }
        return;
    }

    /* 答题页：UP/DOWN 换选项，OK 提交或进入下一题，长按 OK 已在上方退出菜单。 */
    if (page == ECARD_UI_PAGE_QUIZ) {
        if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) {
            if (!ecard_quiz_is_answered() && !ecard_quiz_is_finished()) {
                ecard_quiz_move(-1);
                ecard_ui_quiz_refresh();
            }
        } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
            if (!ecard_quiz_is_answered() && !ecard_quiz_is_finished()) {
                ecard_quiz_move(1);
                ecard_ui_quiz_refresh();
            }
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            if (ecard_quiz_is_finished()) {
                ecard_xiaozhi_reset_playback();
                ecard_ui_show_page(ECARD_UI_PAGE_MENU);
            } else if (ecard_quiz_is_answered()) {
                // 切到下一题就结束上一题的讲解：继续播会让新题和学生听到的
                // 内容对不上，而且旧音频会压住下一题的提问。
                ecard_xiaozhi_reset_playback();
                ecard_quiz_advance();
                ecard_ui_quiz_refresh();
            } else {
                ecard_quiz_submit();
                ecard_ui_quiz_refresh();
                ai_ask_quiz();
            }
        }
        return;
    }

    /* 首次配网与设备重置在 ecard_app_handle_key() 顶部已提前处理，
       这里不再重复，避免长按 OK 被通用“返回菜单”逻辑截走。 */

    /* 番茄钟：未开始时 UP/DOWN 选专注时长（5..50 分钟）；
       计时中 OK 暂停/继续、DOWN 结束。长按 UP 追问已在上面统一处理。 */
    if (page == ECARD_UI_PAGE_POMODORO) {
        const ecard_pomodoro_phase_t phase = ecard_pomodoro_phase();
        const bool idle = (phase == ECARD_POMODORO_IDLE || phase == ECARD_POMODORO_DONE);
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            if (idle) {
                ecard_pomodoro_start();
            } else if (ecard_pomodoro_is_running()) {
                ecard_pomodoro_pause();
            } else {
                ecard_pomodoro_resume();
            }
            ecard_ui_pomodoro_refresh();
        } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
            if (idle) {
                ecard_pomodoro_set_focus_minutes(
                    (uint8_t)(ecard_pomodoro_focus_minutes() - ECARD_POMODORO_FOCUS_STEP));
            } else {
                ecard_pomodoro_stop();
            }
            ecard_ui_pomodoro_refresh();
        } else if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK && idle) {
            ecard_pomodoro_set_focus_minutes(
                (uint8_t)(ecard_pomodoro_focus_minutes() + ECARD_POMODORO_FOCUS_STEP));
            ecard_ui_pomodoro_refresh();
        }
        return;
    }

    /* 年级选择页：UP/DOWN 换年级，OK 载入该年级卡组并进入闪卡页。 */
    if (page == ECARD_UI_PAGE_POEM_PICK || page == ECARD_UI_PAGE_WORD_PICK) {
        const bool poem = (page == ECARD_UI_PAGE_POEM_PICK);
        if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) {
            ecard_ui_grade_move(-1);
        } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
            ecard_ui_grade_move(1);
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            const uint8_t grade = (uint8_t)(ecard_ui_grade_selection() + 1);
            uint8_t count = 0;
            const ecard_card_t *cards = poem ? ecard_cards_poems(grade, &count)
                                             : ecard_cards_words(grade, &count);
            if (cards && count > 0) {
                ecard_ui_set_deck_grade(grade);
                ecard_flashcard_load(cards, count,
                                     poem ? 0 : ecard_cards_word_offset(grade));
                ecard_ui_show_page(poem ? ECARD_UI_PAGE_POEM : ECARD_UI_PAGE_WORD);
            }
        }
        return;
    }

    /* 闪卡页：OK 翻面；翻面后 UP 记不认识、DOWN 记认识。 */
    if (page == ECARD_UI_PAGE_POEM || page == ECARD_UI_PAGE_WORD) {
        if (ecard_flashcard_is_finished()) {
            if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
                ecard_xiaozhi_reset_playback();
                ecard_ui_show_page(ECARD_UI_PAGE_MENU);
            }
            return;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            if (!ecard_flashcard_is_flipped()) {
                ecard_flashcard_flip();
                ecard_ui_flashcard_refresh();
                if (page == ECARD_UI_PAGE_POEM) {
                    ai_ask_poem();
                } else {
                    ai_ask_word();
                }
            }
        } else if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) {
            if (ecard_flashcard_is_flipped()) {
                // 切到下一张卡前先停掉上一张卡的朗读和例句追问，避免新旧音频续播/串台。
                ecard_xiaozhi_reset_playback();
                ecard_flashcard_mark(false);
                ecard_ui_flashcard_refresh();
            }
        } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
            if (ecard_flashcard_is_flipped()) {
                ecard_xiaozhi_reset_playback();
                ecard_flashcard_mark(true);
                ecard_ui_flashcard_refresh();
            }
        }
        return;
    }

    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        switch (page) {
        case ECARD_UI_PAGE_HOME: {
            const ecard_message_t *latest = ecard_messages_latest_unread();
            if (latest) {
                start_playback(latest);
            } else {
                ecard_ui_show_page(ECARD_UI_PAGE_MENU);
            }
            break;
        }
        case ECARD_UI_PAGE_MENU:
            switch ((ecard_menu_item_t)ecard_ui_menu_selection()) {
            case ECARD_MENU_VOICE:
                ecard_ui_show_page(ECARD_UI_PAGE_MESSAGES);
                break;
            case ECARD_MENU_QUIZ:
                ecard_quiz_reset();
                ecard_ui_show_page(ECARD_UI_PAGE_QUIZ);
                // 学生读题时先把 OTA/TLS 连好，提交答案后能更快出声。
                ai_session_begin();
                break;
            case ECARD_MENU_POEM:
                ecard_ui_show_page(ECARD_UI_PAGE_POEM_PICK);
                ai_session_begin();
                break;
            case ECARD_MENU_WORD:
                ecard_ui_show_page(ECARD_UI_PAGE_WORD_PICK);
                ai_session_begin();
                break;
            case ECARD_MENU_POMODORO:
                ecard_ui_show_page(ECARD_UI_PAGE_POMODORO);
                // 专注期间要能随时追问：进页就把会话连好，状态栏绿点即为就绪。
                ai_session_begin();
                break;
            case ECARD_MENU_DEVICE:
            default:
                ecard_ui_show_page(ECARD_UI_PAGE_DEVICE);
                break;
            }
            break;
        case ECARD_UI_PAGE_MESSAGES: {
            const ecard_message_t *selected =
                ecard_messages_recent((int)ecard_ui_messages_selection());
            if (selected) {
                start_playback(selected);
            } else {
                ecard_ui_show_page(ECARD_UI_PAGE_MENU);
            }
            break;
        }
        case ECARD_UI_PAGE_DEVICE:
        default:
            ecard_ui_show_page(ECARD_UI_PAGE_MENU);
            break;
        }
        return;
    }

    if (btn == BSP_BTN_UP && ev == BSP_BTN_LONG && page == ECARD_UI_PAGE_MESSAGES) {
        // 留言页长按 UP：手动同步一次，短按留给列表选择。
        sync_unread_messages();
        return;
    }

    if (btn == BSP_BTN_UP && ev == BSP_BTN_LONG && page == ECARD_UI_PAGE_DEVICE) {
        // 设备页：长按 UP 立即重试联网，并先在提示条上显示“正在重新连接”。
        s_net_retrying = true;
        s_net_retry_ms = 0;
        ecard_ui_set_network_retrying(true);
        ecard_netlink_reconnect();
        return;
    }

    if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) {
        if (page == ECARD_UI_PAGE_MESSAGES) {
            ecard_ui_messages_move(-1);
        } else if (page == ECARD_UI_PAGE_HOME) {
            sync_unread_messages();
        }
        return;
    }

    if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
        if (page == ECARD_UI_PAGE_MESSAGES) {
            ecard_ui_messages_move(1);
        } else if (page == ECARD_UI_PAGE_DEVICE) {
            ecard_ui_show_page(ECARD_UI_PAGE_RESET);
        }
    }
}

void ecard_app_init(void) {
    ecard_profile_init_default();
    ecard_network_init();
    ecard_messages_init();
    ecard_voice_init();
    // 录音缓冲不再开机常驻：常驻 40 KB 会把 AI 会话（TLS/Opus）挤到只剩
    // 约 10 KB，解码器打不开、hello 也收不到。改为录音时按空闲堆分配。
    ecard_ui_reset_navigation();
    ecard_ui_feed_activity();
    s_app_state = ECARD_APP_STATE_IDLE;
    s_play_progress_sec = 0;
    s_play_total_sec = 0;
    s_has_polled_current_recess = false;
    s_last_recess_minute = 0xFF;
    s_sos_remaining_ms = 0;
    s_page_before_sos = ECARD_UI_PAGE_HOME;
    s_play_has_audio = false;
    s_play_clears_unread = false;
    s_play_message_id = 0;
    s_clip_message_id = 0;
    s_net_retrying = false;
    s_net_retry_ms = 0;
    s_ai_session = false;
    s_ai_listen = false;
    s_ai_long_listen = false;
    s_ai_swallow_up_click = false;
    s_ai_stopping = false;
    s_ai_reopen = false;
    s_menu_press_moved = false;
    s_ai_listen_ms = 0;
    s_rec_ui_div = 0;
}

void ecard_app_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // 任何按键动作均重置 2 分钟无操作计时，并唤醒屏幕
    ecard_ui_feed_activity();

    // 首次配网是强制流程：在完成 Wi-Fi 与身份之前，不允许长按 OK 逃到菜单，
    // 也不允许长按 DOWN 触发求助。
    const ecard_ui_page_t current_page = ecard_ui_get_page();
    if (current_page == ECARD_UI_PAGE_SETUP) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            (void)ecard_portal_start();
            // 已联网时不要为了“重试”把好连接断掉；只有未联网才重连。
            if (!ecard_netlink_is_connected()) {
                ecard_netlink_reconnect();
            }
            ecard_ui_show_page(ECARD_UI_PAGE_SETUP);
        }
        return;
    }
    if (current_page == ECARD_UI_PAGE_RESET) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            ecard_setup_clear();
#if defined(ESP_PLATFORM)
            esp_restart();
#endif
            ecard_ui_show_page(ECARD_UI_PAGE_SETUP);
        } else if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            ecard_ui_show_page(ECARD_UI_PAGE_MENU);
        }
        return;
    }

    if (s_app_state == ECARD_APP_STATE_SOS_PENDING) {
        if (btn == BSP_BTN_DOWN && ev == BSP_BTN_RELEASE) {
            cancel_sos_pending();
        }
        return;
    }

    if (s_app_state == ECARD_APP_STATE_SOS_SENT) {
        if (btn == BSP_BTN_OK && (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG)) {
            leave_sos_sent();
        }
        return;
    }

    // 长按 DOWN 在任意界面进入 3 秒求助确认；确认期间必须保持按住。
    if (btn == BSP_BTN_DOWN && ev == BSP_BTN_LONG) {
        begin_sos_pending();
        return;
    }

    if (s_app_state == ECARD_APP_STATE_RECORDING) {
        // 长按 UP 的追问：松开即发送，不需要再按 OK。
        if (s_ai_listen && s_ai_long_listen && btn == BSP_BTN_UP &&
            ev == BSP_BTN_RELEASE) {
            ai_finish_listen();
            return;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            if (s_ai_listen) {
                ai_finish_listen();
            } else {
                finish_recording();
            }
        }
        return;
    }

    if (s_app_state == ECARD_APP_STATE_PLAYING) {
        if (btn == BSP_BTN_OK && (ev == BSP_BTN_CLICK || ev == BSP_BTN_LONG)) {
            stop_playback();
        }
        return;
    }

    handle_idle_key(btn, ev);
}

void ecard_app_tick(uint8_t current_hour, uint8_t current_minute, uint32_t delta_ms) {
    // 留言列表按“距现在多久”排序与过滤，每秒推进一次年龄。
    ecard_messages_tick(delta_ms);

    // 小智追问录音 10 秒上限；离开 AI 页面立刻关闭会话释放内存。
    if (s_ai_listen) {
        s_ai_listen_ms = (uint16_t)(s_ai_listen_ms + delta_ms);
        if (s_ai_listen_ms >= ECARD_AI_LISTEN_MAX_MS) {
            ai_finish_listen();
        } else {
            const uint16_t left = (uint16_t)(ECARD_AI_LISTEN_MAX_MS - s_ai_listen_ms);
            uint8_t sec = (uint8_t)((left + 999) / 1000);
            if (sec < 1) sec = 1;
            // 计时每秒刷新，学生能看到录音仍在进行，松手就会发送。
            ecard_ui_show_ai_listen_modal(sec, 0);
        }
    } else if (s_ai_session && !ai_page_active(ecard_ui_get_page())) {
        ai_session_end();
    } else if (s_ai_session && !s_ai_listen &&
               ecard_xiaozhi_restart_pending() && !s_ai_stopping) {
        // hello 看门狗判死（服务端不回 hello）：整会话重建。
        // 停止是异步的（ai_stop_task），s_ai_session 归零后下一轮 tick
        // 会因“不在 AI 页面/重进 AI 页面”自然重新 start。
        ESP_LOGW("ecard_ai", "小智会话异常，自动重建");
        s_ai_reopen = true;
        ai_session_end();
    }

    // 0. 设备页“长按 UP 重试”的进行态：连上或超时后收起提示
    if (s_net_retrying) {
        if (ecard_netlink_is_connected()) {
            s_net_retrying = false;
            ecard_ui_set_network_retrying(false);
        } else if (s_net_retry_ms >= ECARD_NET_RETRY_TIMEOUT_MS) {
            s_net_retrying = false;
            ecard_ui_set_network_retrying(false);
        } else {
            s_net_retry_ms = (uint16_t)(s_net_retry_ms + delta_ms);
        }
    }

    // 1. SOS 确认倒计时
    if (s_app_state == ECARD_APP_STATE_SOS_PENDING) {
        if (delta_ms >= s_sos_remaining_ms) {
            send_sos_alert();
        } else {
            s_sos_remaining_ms = (uint16_t)(s_sos_remaining_ms - delta_ms);
            uint8_t seconds_left = (uint8_t)((s_sos_remaining_ms + 999) / 1000);
            if (seconds_left < 1) seconds_left = 1;
            ecard_ui_show_sos_pending(seconds_left);
        }
        return;
    }

    // 2. 课间时段自动同步检查
    const bool is_recess = ecard_network_is_recess_time(current_hour, current_minute);
    if (is_recess) {
        if (!s_has_polled_current_recess || s_last_recess_minute != current_minute) {
            s_has_polled_current_recess = true;
            s_last_recess_minute = current_minute;
            sync_unread_messages();
        }
    } else {
        s_has_polled_current_recess = false;
    }

    // 番茄钟计时与界面刷新
    ecard_pomodoro_tick(delta_ms);
    if (ecard_ui_get_page() == ECARD_UI_PAGE_POMODORO) {
        ecard_ui_pomodoro_refresh();
    }

    // 3. 正在播放中的倒计时流转
    if (s_app_state == ECARD_APP_STATE_PLAYING) {
        s_play_progress_sec++;
        ecard_ui_show_playing_modal(s_play_progress_sec, s_play_total_sec);
        // 有声回放通常由 PCM 渲染结束驱动停止；多留 1 秒容错，
        // 防止音频任务卡住时模态永久停在“播放中”。
        const uint8_t grace = s_play_has_audio ? 1 : 0;
        if (s_play_progress_sec >= (uint8_t)(s_play_total_sec + grace)) {
            stop_playback();
        }
    }
}

void ecard_app_feed_mic_pcm(const int16_t *pcm, size_t samples) {
    if (s_app_state != ECARD_APP_STATE_RECORDING || !pcm || samples == 0) return;

    uint8_t rem_sec = 0;
    uint8_t amp = 0;
    if (!ecard_voice_write_pcm(pcm, samples, &rem_sec, &amp)) {
        // 录满上限：自动封包并进入本地试听
        finish_recording();
        return;
    }

    if (++s_rec_ui_div >= ECARD_REC_UI_DIV) {
        s_rec_ui_div = 0;
        ecard_ui_show_recording_modal(rem_sec, amp);
    }
}

size_t ecard_app_render_playback_pcm(int16_t *out, size_t max_samples) {
    if (s_app_state != ECARD_APP_STATE_PLAYING || !s_play_has_audio || !out) return 0;

    size_t n = ecard_voice_playback_read(out, max_samples);
    if (n == 0) {
        stop_playback();
    }
    return n;
}

ecard_app_state_t ecard_app_get_state(void) {
    return s_app_state;
}
