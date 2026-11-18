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
#include "ecard_cat_room.h"
#include "ecard_netlink.h"
#include "ecard_flashcard.h"
#include "ecard_lan_http.h"
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
static uint16_t s_play_progress_ms = 0;
static uint8_t s_play_total_sec = 0;
static bool s_has_polled_current_recess = false;
static uint8_t s_last_recess_minute = 0xFF;
static uint16_t s_sos_remaining_ms = 0;
static ecard_ui_page_t s_page_before_sos = ECARD_UI_PAGE_HOME;
// 播放来源：本机刚录完的试听（有声）或列表里的留言
static bool s_play_has_audio = false;
static uint32_t s_clip_message_id = 0;
static bool s_play_clears_unread = false;
// 正在播放的留言 ID：停止时只把这一条标记为已读。
static uint32_t s_play_message_id = 0;
// 设备页“长按 UP 重试连接”的进行态与超时。
static bool s_net_retrying = false;
// 小智只在答题讲解、古诗/单词朗读页常驻；番茄钟采用按需连接。
static bool s_ai_session = false;
static bool s_ai_listen = false;
// 多轮免按键续聊状态：首轮长按 UP 触发后，每次 TTS 播毕自动开启下一轮免持聆听。
static bool s_ai_multiturn = false;
static bool s_ai_auto_followup = false;
static bool s_ai_turn_waiting_tts = false;
static bool s_ai_turn_saw_tts = false;
static uint16_t s_ai_turn_wait_ms = 0;
static uint16_t s_ai_followup_delay_ms = 0;
// 长按 UP 触发聆听后吞掉松手时可能附带的 UP CLICK，避免误翻选项。
static bool s_ai_swallow_up_click = false;
// 上一次会话是否正在后台收尾（TLS 关闭 + 找回录音缓冲）。
static bool s_ai_stopping = false;
static volatile bool s_ai_stop_done = false;
// 会话异常重建：stop 完成后立即重开一次。
static bool s_ai_reopen = false;
static uint16_t s_ai_listen_ms = 0;
// 番茄钟按需小智会话状态（就绪/完成后宽限 20 秒自动释放，恢复局域网 HTTP）
#define ECARD_POMO_AI_GRACE_MS 20000
#define ECARD_POMO_AI_CONNECT_TIMEOUT_MS 15000
static bool s_ai_prepping = false;
static volatile bool s_ai_prep_done = false;
static volatile bool s_ai_prep_cancel = false;
static volatile bool s_ai_resuming = false;
static bool s_pomo_ai_owned = false;
static bool s_pomo_ai_held = false;
static bool s_pomo_ai_pending_listen = false;
static bool s_pomo_ai_used_listen = false;
static bool s_pomo_ai_ready_notified = false;
static bool s_pomo_ai_paused_timer = false;
static uint16_t s_pomo_ai_grace_ms = 0;
static uint16_t s_pomo_ai_connect_ms = 0;
static uint16_t s_net_retry_ms = 0;
// 录音浮层按 100ms 刷新一次，避免 20ms 音频块把 LVGL 刷屏
static uint8_t s_rec_ui_div = 0;
static bool s_rec_finish_pending = false;
static ecard_app_record_full_cb_t s_record_full_cb = NULL;

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
    msg.sender_role = ECARD_SENDER_PRIMARY_PARENT;
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
    s_play_progress_ms = 0;
    s_play_total_sec = msg->duration_sec ? msg->duration_sec : 1;
    s_play_message_id = msg->id;
    s_play_clears_unread = (msg->direction == ECARD_MESSAGE_RECEIVED && msg->unread);

    s_play_has_audio = false;
    if (msg->id != 0 && msg->id == s_clip_message_id && ecard_voice_has_clip()) {
        s_play_has_audio = ecard_voice_playback_start();
    }
    if (!s_play_has_audio && msg->has_audio) {
        char audio_path[ECARD_MESSAGES_PATH_MAX];
        if (ecard_messages_audio_path(msg->id, audio_path, sizeof(audio_path))) {
            s_play_has_audio = ecard_voice_playback_start_file(audio_path, msg->audio_is_8k);
        }
    }
    ecard_ui_set_playing_message(msg);
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
#define ECARD_AI_FOLLOWUP_SETTLE_MS 350
#define ECARD_AI_REPLY_TIMEOUT_MS 10000

static void clear_pomo_ai_paused_timer(void) {
    s_pomo_ai_paused_timer = false;
    ecard_ui_set_pomodoro_ai_paused(false);
}

static bool pomo_ai_voice_interacting(void) {
    return s_pomo_ai_pending_listen || s_ai_listen || s_ai_multiturn ||
           s_ai_turn_waiting_tts || ecard_xiaozhi_is_speaking();
}

static void sync_pomodoro_ai_pause(void) {
    if (ecard_ui_get_page() != ECARD_UI_PAGE_POMODORO ||
        ecard_pomodoro_phase() != ECARD_POMODORO_FOCUS) {
        if (s_pomo_ai_paused_timer) {
            clear_pomo_ai_paused_timer();
        }
        return;
    }
    if (pomo_ai_voice_interacting()) {
        if (!s_pomo_ai_paused_timer && ecard_pomodoro_is_running()) {
            ecard_pomodoro_pause();
            s_pomo_ai_paused_timer = true;
            ecard_ui_set_pomodoro_ai_paused(true);
            ecard_ui_pomodoro_refresh();
        }
    } else if (s_pomo_ai_paused_timer) {
        clear_pomo_ai_paused_timer();
        ecard_pomodoro_resume();
        ecard_ui_pomodoro_refresh();
    }
}

static void ai_disarm_multiturn(void) {
    s_ai_multiturn = false;
    s_ai_auto_followup = false;
    s_ai_turn_waiting_tts = false;
    s_ai_turn_saw_tts = false;
    s_ai_turn_wait_ms = 0;
    s_ai_followup_delay_ms = 0;
}

static bool ai_page_active(ecard_ui_page_t page) {
    return page == ECARD_UI_PAGE_QUIZ || page == ECARD_UI_PAGE_POEM ||
           page == ECARD_UI_PAGE_WORD || page == ECARD_UI_PAGE_POEM_PICK ||
           page == ECARD_UI_PAGE_WORD_PICK;
}

#if defined(ESP_PLATFORM)
// 关闭/重启局域网 HTTP 涉及 httpd_stop/lwIP 套接字等待，必须在 LVGL 锁外运行，
// 否则按一次按键会让专注时钟和屏幕刷新卡死数秒。
static void ai_prep_task(void *arg) {
    (void)arg;
    ecard_lan_http_set_ai_busy(true);
    if (!s_ai_prep_cancel) {
        ecard_xiaozhi_session_start();
    }
    s_ai_prep_done = true;
    vTaskDelete(NULL);
}

static void ai_resume_task(void *arg) {
    (void)arg;
    ecard_lan_http_set_ai_busy(false);
    s_ai_resuming = false;
    vTaskDelete(NULL);
}

// 关闭 TLS/WebSocket 会阻塞几百毫秒；绝不能放在 LVGL 锁里，
// 否则按键要等 500 ms 才拿到锁（实测连续丢 4~5 个按键）。
static void ai_stop_task(void *arg) {
    (void)arg;
    ecard_xiaozhi_session_stop();
    s_ai_stop_done = true;
    vTaskDelete(NULL);
}
#endif

static void ai_resume_lan_http_async(void) {
#if defined(ESP_PLATFORM)
    if (!s_ai_resuming) {
        s_ai_resuming = true;
        if (xTaskCreate(ai_resume_task, "ai_http_on", 4096, NULL, 3, NULL) == pdPASS) {
            return;
        }
        s_ai_resuming = false;
    } else {
        return;
    }
#endif
    ecard_lan_http_set_ai_busy(false);
}

static void ai_session_begin(void) {
    // 上一次会话正在后台收尾（TLS 关闭 + 找回录音缓冲），登记 reopen，收尾完立即启动。
    if (s_ai_stopping) {
        s_ai_reopen = true;
        return;
    }
    if (ecard_portal_is_active()) {
        ecard_portal_stop();
    }
    if (!s_ai_session) {
        ESP_LOGI("ecard_ai", "建立小智会话");
        // TLS + Opus 约需 40 KB：把上一次录音占着的 40 KB 缓冲让出来，
        // 空闲堆足够时握手后才收到服务端 hello。
        ecard_voice_clip_release();
        s_clip_message_id = 0;
        s_ai_session = true;
    }
#if defined(ESP_PLATFORM)
    if (s_ai_prepping) {
        s_ai_prep_cancel = false;
        return;
    }
    if (ecard_lan_http_is_running() || s_ai_resuming) {
        s_ai_prep_cancel = false;
        s_ai_prep_done = false;
        s_ai_prepping = true;
        if (xTaskCreate(ai_prep_task, "ai_http_off", 5120, NULL, 3, NULL) == pdPASS) {
            return;
        }
        s_ai_prepping = false;
        s_ai_prep_done = false;
    }
#endif
    ecard_lan_http_set_ai_busy(true);
    // 连接失败后再次进入 AI 入口时允许重试；已在连接中则直接返回。
    ecard_xiaozhi_session_start();
}

static void ai_session_end(void) {
    ai_disarm_multiturn();
    if (!s_ai_session) {
        if (!s_ai_stopping && !s_ai_prepping) {
            ai_resume_lan_http_async();
        }
        return;
    }
    s_ai_listen = false;
    s_ai_swallow_up_click = false;
    s_ai_session = false;
    if (s_ai_prepping) {
        // ai_prep_task 还在关停局域网 HTTP：登记取消标记，由 tick 发现 !s_ai_session 后再收尾。
        s_ai_prep_cancel = true;
        return;
    }
#if defined(ESP_PLATFORM)
    if (!s_ai_stopping) {
        s_ai_stop_done = false;
        s_ai_stopping = true;
        if (xTaskCreate(ai_stop_task, "ai_stop", 5120, NULL, 3, NULL) == pdPASS) {
            return;
        }
        s_ai_stopping = false;
        s_ai_stop_done = false;
    } else {
        return;
    }
#endif
    ecard_xiaozhi_session_stop();
    ecard_lan_http_set_ai_busy(false);
}

static void ai_cancel_listen(const char *hint) {
    if (s_ai_listen) {
        ecard_xiaozhi_listen_cancel();
        s_ai_listen = false;
        ecard_ui_hide_modal();
        s_app_state = ECARD_APP_STATE_IDLE;
    }
    s_pomo_ai_held = false;
    s_pomo_ai_grace_ms = 0;
    s_pomo_ai_used_listen = true;
    ai_disarm_multiturn();
    if (hint && hint[0]) {
        ecard_ui_set_hint(hint);
    }
}

static void leave_pomodoro_page(void) {
    clear_pomo_ai_paused_timer();
    ecard_pomodoro_stop();
    (void)ecard_pomodoro_take_completed_focus_minutes();
    s_pomo_ai_held = false;
    s_pomo_ai_pending_listen = false;
    s_pomo_ai_used_listen = false;
    s_pomo_ai_ready_notified = false;
    s_pomo_ai_grace_ms = 0;
    s_pomo_ai_connect_ms = 0;
    s_ai_reopen = false;
    ecard_ui_clear_pomodoro_hint();
    if (s_ai_listen) {
        ai_cancel_listen(NULL);
    } else {
        ai_disarm_multiturn();
    }
    if (s_pomo_ai_owned || s_ai_session) {
        ecard_xiaozhi_reset_playback();
        s_pomo_ai_owned = false;
        ai_session_end();
    }
}

/* 小智文本入口的长度上限与提示词构造统一放在 ecard_ai_prompt.c，
 * 那里的规则由真机 + 官方服务端实测得出，并有主机测试逐卡校验。 */

static void ai_ask_quiz(void) {
    const ecard_quiz_question_t *q = ecard_quiz_current();
    if (!q) return;
    ai_disarm_multiturn();
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
    ai_disarm_multiturn();
    ESP_LOGI("ecard_ai", "触发读诗：%s", c->front);
    ai_session_begin();
    char prompt[96];
    ecard_ai_build_poem_prompt(c, prompt, sizeof(prompt));
    ESP_LOGI("ecard_ai", "小智提问(%u字): %s", (unsigned)ecard_ai_utf8_chars(prompt), prompt);
    ecard_xiaozhi_ask(prompt);
}

static void ai_ask_word(void) {
    const ecard_card_t *c = ecard_flashcard_current();
    if (!c) return;
    ai_disarm_multiturn();
    ESP_LOGI("ecard_ai", "触发读词：%s", c->front);
    ai_session_begin();
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

static bool ai_start_listen_ex(bool is_followup) {
    if (!ecard_netlink_is_connected()) {
        ESP_LOGW("ecard_ai", "追问：设备未联网");
        if (!is_followup) {
            ecard_ui_set_hint("没有联网：请先连上 Wi-Fi 再长按 UP");
        }
        ai_disarm_multiturn();
        return false;
    }
    ai_session_begin();
    if (s_ai_stopping) {
        if (!is_followup) {
            ecard_ui_set_hint("小智正在重连，请稍候再长按 UP");
        }
        ai_disarm_multiturn();
        return false;
    }
    if (!ecard_xiaozhi_session_ready()) {
        ESP_LOGW("ecard_ai", "追问：会话尚未就绪");
        if (!is_followup) {
            ecard_ui_set_hint("小智连接中，连上后再长按 UP");
        }
        ai_disarm_multiturn();
        return false;
    }

    // 先停掉上一轮回答，避免麦克风把扬声器声录进去。
    ecard_xiaozhi_reset_playback();
    ecard_xiaozhi_reset_mic();
    if (!ecard_xiaozhi_listen_start()) {
        ESP_LOGW("ecard_ai", "追问：小智无法开始采音");
        if (!is_followup) {
            ecard_ui_set_hint("小智暂时听不到，请稍后再按 UP");
        }
        ai_disarm_multiturn();
        s_app_state = ECARD_APP_STATE_IDLE;
        return false;
    }
    s_ai_listen = true;
    s_ai_auto_followup = is_followup;
    s_ai_multiturn = true;
    s_ai_turn_waiting_tts = false;
    s_ai_turn_saw_tts = false;
    s_ai_turn_wait_ms = 0;
    s_ai_followup_delay_ms = 0;
    if (!is_followup) {
        s_ai_swallow_up_click = true;
    }
    s_ai_listen_ms = 0;
    s_app_state = ECARD_APP_STATE_RECORDING;
    const uint8_t init_sec = (uint8_t)(ECARD_AI_LISTEN_MAX_MS / 1000u);
    ecard_ui_show_ai_listen_modal(init_sec, 0);
    ESP_LOGI("ecard_ai", "开始%s聆听（松手即可说话，最长 10 秒）",
             is_followup ? "多轮续聊" : "首轮免持");
    return true;
}

static bool ai_start_listen(void) {
    return ai_start_listen_ex(false);
}

static void ai_finish_listen(void) {
    if (!s_ai_listen) return;
    ecard_xiaozhi_listen_stop();
    const bool had_audio = ecard_xiaozhi_listen_had_audio();
    s_ai_listen = false;
    s_ai_auto_followup = false;
    s_pomo_ai_held = false;
    s_pomo_ai_grace_ms = 0;
    s_pomo_ai_used_listen = true;
    ecard_ui_hide_modal();
    s_app_state = ECARD_APP_STATE_IDLE;
    if (had_audio) {
        if (s_ai_multiturn) {
            s_ai_turn_waiting_tts = true;
            s_ai_turn_saw_tts = false;
            s_ai_turn_wait_ms = 0;
            s_ai_followup_delay_ms = 0;
        }
        ecard_ui_set_hint("已发送，等小智回答…");
    } else {
        ai_disarm_multiturn();
        ecard_ui_set_hint("没听到声音，请再长按 UP 说一次");
    }
    ESP_LOGI("ecard_ai", "追问录音结束，音频帧%s", had_audio ? "已上传" : "为空");
}

bool ecard_app_ai_listening(void) {
    return s_ai_listen;
}

bool ecard_app_pomo_ai_paused(void) {
    return s_pomo_ai_paused_timer;
}

void ecard_app_set_record_full_cb(ecard_app_record_full_cb_t cb) {
    s_record_full_cb = cb;
}

static void start_recording(void) {
    if (!ecard_voice_start_recording()) return;
    uint8_t max_sec = (uint8_t)(ecard_voice_buffer_capacity() / ECARD_VOICE_BYTES_PER_SEC);
    if (max_sec == 0) max_sec = 1;
    if (max_sec > ECARD_VOICE_RECORD_SEC) max_sec = ECARD_VOICE_RECORD_SEC;
    s_rec_ui_div = 0;
    s_rec_finish_pending = false;
    s_app_state = ECARD_APP_STATE_RECORDING;
    ecard_ui_show_recording_modal(max_sec, 0);
}

bool ecard_app_begin_save_recording(uint8_t *out_duration_sec, size_t *out_bytes) {
    if (s_app_state != ECARD_APP_STATE_RECORDING || s_ai_listen) {
        return false;
    }
    s_rec_finish_pending = false;
    const uint8_t *adpcm_data = NULL;
    size_t len = 0;
    uint8_t dur = 0;
    if (!ecard_voice_stop_recording(&adpcm_data, &len, &dur)) {
        ecard_voice_clip_release();
        ecard_ui_hide_modal();
        s_app_state = ECARD_APP_STATE_IDLE;
        return false;
    }
    // 立刻切回 IDLE 停止继续采音，随后退出 LVGL 锁再执行 SPIFFS 写盘。
    s_app_state = ECARD_APP_STATE_IDLE;
    if (out_duration_sec) *out_duration_sec = dur;
    if (out_bytes) *out_bytes = len;
    return true;
}

uint32_t ecard_app_persist_recorded_clip(uint8_t duration_sec) {
    ecard_message_t sent;
    memset(&sent, 0, sizeof(sent));
    sent.direction = ECARD_MESSAGE_SENT;
    sent.sender_role = ECARD_SENDER_STUDENT;
    sent.duration_sec = duration_sec;
    sent.audio_is_8k = (ECARD_VOICE_STORE_RATE == 8000);
    sent.pending_upload = true;
    const ecard_setup_t *cfg = ecard_setup_get();
    if (cfg && cfg->phone[0]) {
        snprintf(sent.sender_phone, sizeof(sent.sender_phone), "%s", cfg->phone);
    }
    snprintf(sent.peer, sizeof(sent.peer), "%s", "家长");

    uint32_t saved_id = 0;
    char rec_tmp[80];
    size_t saved_bytes = 0;
    if (ecard_messages_rec_temp_audio_path(rec_tmp, sizeof(rec_tmp))) {
        if (ecard_voice_save_to_file(rec_tmp, &saved_bytes) && saved_bytes > 0) {
            saved_id = ecard_messages_adopt_audio_file(&sent, 0, rec_tmp, saved_bytes);
        }
        if (saved_id != 0) {
            // 已落盘至 /spiffs/msg_<id>.adp：立即归还借用的静态区和堆分段，
            // 留足 ~40 KB 堆空间给文件试听与 HTTP 上传。
            ecard_voice_clip_release();
            ecard_network_request_upload(saved_id);
        }
    } else {
        // 未挂载持久化目录（如宿主单测）：保留内存录音以便随后试听。
        saved_id = ecard_messages_add(&sent, 0);
        if (saved_id != 0) {
            ecard_network_request_upload(saved_id);
        }
    }
    return saved_id;
}

void ecard_app_start_recorded_playback(uint32_t local_msg_id, uint8_t duration_sec) {
    s_clip_message_id = local_msg_id;
    s_play_progress_sec = 0;
    s_play_progress_ms = 0;
    s_play_total_sec = duration_sec ? duration_sec : 1;
    s_play_clears_unread = false;
    s_play_message_id = local_msg_id;
    s_play_has_audio = false;

    ecard_message_t snapshot;
    if (local_msg_id != 0 && ecard_messages_copy_by_id(local_msg_id, &snapshot)) {
        if (snapshot.has_audio) {
            char audio_path[ECARD_MESSAGES_PATH_MAX];
            if (ecard_messages_audio_path(local_msg_id, audio_path, sizeof(audio_path))) {
                s_play_has_audio = ecard_voice_playback_start_file(audio_path, snapshot.audio_is_8k);
            }
        } else if (ecard_voice_has_clip()) {
            s_play_has_audio = ecard_voice_playback_start();
        }
        ecard_ui_set_playing_message(&snapshot);
    } else {
        if (ecard_voice_has_clip()) {
            s_play_has_audio = ecard_voice_playback_start();
        }
        memset(&snapshot, 0, sizeof(snapshot));
        snapshot.direction = ECARD_MESSAGE_SENT;
        snapshot.sender_role = ECARD_SENDER_STUDENT;
        snapshot.duration_sec = duration_sec;
        snprintf(snapshot.peer, sizeof(snapshot.peer), "%s", "家长");
        ecard_ui_set_playing_message(&snapshot);
    }

    if (s_play_has_audio) {
        s_app_state = ECARD_APP_STATE_PLAYING;
        ecard_ui_show_playing_modal(0, s_play_total_sec);
    } else {
        ecard_voice_clip_release();
        ecard_ui_hide_modal();
        s_app_state = ECARD_APP_STATE_IDLE;
    }
}

static void finish_recording(void) {
    uint8_t dur = 0;
    size_t len = 0;
    if (!ecard_app_begin_save_recording(&dur, &len)) {
        return;
    }
    const uint32_t saved_id = ecard_app_persist_recorded_clip(dur);
    ecard_app_start_recorded_playback(saved_id, dur);
}

static void begin_sos_pending(void) {
    s_rec_finish_pending = false;
    if (s_ai_listen) {
        ai_cancel_listen(NULL);
    } else if (s_app_state == ECARD_APP_STATE_RECORDING) {
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
    ai_disarm_multiturn();
    s_page_before_sos = ecard_ui_get_page();
    if (s_page_before_sos == ECARD_UI_PAGE_POMODORO) {
        leave_pomodoro_page();
    }
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

    // 番茄钟页：按需连接小智，避免常态占用约 40 KB 堆而阻塞局域网 HTTP。
    // 长按 UP 1~2 秒触发后即可松手：已就绪则直接开启免持聆听 + 多轮续聊；
    // 未就绪则后台连接并在就绪瞬间自动开启免持聆听，无需长按第二次。
    if (page == ECARD_UI_PAGE_POMODORO && btn == BSP_BTN_UP) {
        const ecard_pomodoro_phase_t phase = ecard_pomodoro_phase();
        const bool idle = (phase == ECARD_POMODORO_IDLE || phase == ECARD_POMODORO_DONE);
        if (ev == BSP_BTN_LONG) {
            s_pomo_ai_held = true;
            s_pomo_ai_grace_ms = 0;
            if (!ecard_netlink_is_connected()) {
                ecard_ui_set_hint("没有联网：请先连上 Wi-Fi 再按 UP");
                s_pomo_ai_held = false;
                s_pomo_ai_pending_listen = false;
                return;
            }
            if (!s_pomo_ai_owned || !s_ai_session) {
                s_pomo_ai_connect_ms = 0;
                s_pomo_ai_used_listen = false;
                s_pomo_ai_ready_notified = false;
            }
            s_pomo_ai_owned = true;
            s_ai_swallow_up_click = true;
            ai_session_begin();
            if (!s_ai_stopping && !s_ai_prepping && ecard_xiaozhi_session_ready()) {
                if (!s_ai_listen && ai_start_listen()) {
                    s_pomo_ai_used_listen = true;
                }
            } else {
                s_pomo_ai_pending_listen = true;
                ecard_ui_set_hint_persistent("正在连接小智…");
            }
            return;
        }
        if (ev == BSP_BTN_RELEASE) {
            s_pomo_ai_held = false;
            if (s_pomo_ai_pending_listen) {
                ecard_ui_set_hint_persistent("正在连接小智…");
                return;
            }
        }
        if (ev == BSP_BTN_CLICK) {
            if (s_ai_swallow_up_click) {
                s_ai_swallow_up_click = false;
                return;
            }
            if (!idle) {
                s_pomo_ai_held = false;
                s_pomo_ai_pending_listen = false;
                s_pomo_ai_grace_ms = 0;
                if (!ecard_netlink_is_connected()) {
                    ecard_ui_set_hint("没有联网：请先连上 Wi-Fi 再按 UP");
                    return;
                }
                if (!s_pomo_ai_owned || !s_ai_session) {
                    s_pomo_ai_connect_ms = 0;
                    s_pomo_ai_used_listen = false;
                    s_pomo_ai_ready_notified = false;
                }
                s_pomo_ai_owned = true;
                ai_session_begin();
                if (!s_ai_stopping && !s_ai_prepping && ecard_xiaozhi_session_ready()) {
                    ecard_ui_set_hint_persistent("小智已就绪，长按 UP 说话");
                    s_pomo_ai_ready_notified = true;
                } else {
                    s_pomo_ai_ready_notified = false;
                    ecard_ui_set_hint_persistent("正在连接小智…");
                }
                return;
            }
        }
    }

    // AI 页面：长按 UP 1~2 秒触发免持聆听 + 多轮续聊，松开 UP 不中断录音。
    if (ai_page_active(page) && btn == BSP_BTN_UP) {
        if (ev == BSP_BTN_LONG) {
            if (!s_ai_listen) {
                (void)ai_start_listen();
            }
            return;
        }
        if (ev == BSP_BTN_RELEASE) {
            return;
        }
        if (ev == BSP_BTN_CLICK && s_ai_swallow_up_click) {
            // 长按松手后按键库可能补发一个 CLICK，这里吞掉，避免误翻选项。
            s_ai_swallow_up_click = false;
            return;
        }
    }

    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
        if (page == ECARD_UI_PAGE_HOME) {
            start_recording();
        } else if (page == ECARD_UI_PAGE_MENU) {
            ecard_ui_show_page(ECARD_UI_PAGE_HOME);
        } else if (page == ECARD_UI_PAGE_POEM || page == ECARD_UI_PAGE_WORD) {
            ai_disarm_multiturn();
            ecard_xiaozhi_reset_playback();
            ecard_ui_show_page(page == ECARD_UI_PAGE_POEM
                                   ? ECARD_UI_PAGE_POEM_PICK
                                   : ECARD_UI_PAGE_WORD_PICK);
        } else if ((page == ECARD_UI_PAGE_POEM_PICK || page == ECARD_UI_PAGE_WORD_PICK) &&
                   ecard_ui_pick_step() == ECARD_PICK_STEP_DECK) {
            ai_disarm_multiturn();
            ecard_xiaozhi_reset_playback();
            ecard_ui_pick_set_step(ECARD_PICK_STEP_GRADE);
        } else {
            ai_disarm_multiturn();
            if (page == ECARD_UI_PAGE_POMODORO) {
                leave_pomodoro_page();
            } else if (ai_page_active(page)) {
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
            ai_disarm_multiturn();
            if (ecard_quiz_is_finished()) {
                ecard_xiaozhi_reset_playback();
                ecard_ui_show_page(ECARD_UI_PAGE_MENU);
            } else if (ecard_quiz_is_answered()) {
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
                clear_pomo_ai_paused_timer();
                ecard_pomodoro_start();
            } else if (s_pomo_ai_paused_timer) {
                s_pomo_ai_pending_listen = false;
                ai_disarm_multiturn();
                ecard_xiaozhi_reset_playback();
                clear_pomo_ai_paused_timer();
                ecard_pomodoro_resume();
            } else if (ecard_pomodoro_is_running()) {
                ecard_pomodoro_pause();
            } else {
                ecard_pomodoro_resume();
            }
            ecard_ui_pomodoro_refresh();
        } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
            if (s_pomo_ai_pending_listen ||
                (!idle && (s_ai_multiturn || s_ai_turn_waiting_tts ||
                           ecard_xiaozhi_is_speaking()))) {
                s_pomo_ai_pending_listen = false;
                ai_disarm_multiturn();
                ecard_xiaozhi_reset_playback();
                ecard_ui_clear_pomodoro_hint();
                if (!idle) {
                    ecard_ui_set_hint("已结束语音对话");
                }
                sync_pomodoro_ai_pause();
                ecard_ui_pomodoro_refresh();
                return;
            }
            if (idle) {
                if (phase == ECARD_POMODORO_DONE) ecard_pomodoro_stop();
                ecard_pomodoro_set_focus_minutes(
                    (uint8_t)(ecard_pomodoro_focus_minutes() - ECARD_POMODORO_FOCUS_STEP));
            } else {
                clear_pomo_ai_paused_timer();
                ecard_pomodoro_stop();
                if (s_pomo_ai_owned && !s_ai_listen && !ecard_xiaozhi_is_speaking()) {
                    s_pomo_ai_owned = false;
                    s_pomo_ai_held = false;
                    s_pomo_ai_pending_listen = false;
                    s_pomo_ai_grace_ms = 0;
                    s_pomo_ai_connect_ms = 0;
                    ai_session_end();
                    ecard_ui_clear_pomodoro_hint();
                }
            }
            ecard_ui_pomodoro_refresh();
        } else if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK && idle) {
            if (phase == ECARD_POMODORO_DONE) ecard_pomodoro_stop();
            ecard_pomodoro_set_focus_minutes(
                (uint8_t)(ecard_pomodoro_focus_minutes() + ECARD_POMODORO_FOCUS_STEP));
            ecard_ui_pomodoro_refresh();
        }
        return;
    }

    /* 二级题库选择页：第 1 步在设备保留的 <=3 个年级中选年级，第 2 步选分组/单元并进入闪卡。 */
    if (page == ECARD_UI_PAGE_POEM_PICK || page == ECARD_UI_PAGE_WORD_PICK) {
        const bool poem = (page == ECARD_UI_PAGE_POEM_PICK);
        const ecard_card_kind_t kind = poem ? ECARD_CARD_POEM : ECARD_CARD_WORD;
        if (ecard_ui_pick_step() == ECARD_PICK_STEP_GRADE) {
            if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) {
                ecard_ui_grade_move(-1);
            } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
                ecard_ui_grade_move(1);
            } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
                ecard_ui_pick_set_step(ECARD_PICK_STEP_DECK);
            }
        } else {
            if (btn == BSP_BTN_UP && ev == BSP_BTN_CLICK) {
                ecard_ui_deck_move(-1);
            } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
                ecard_ui_deck_move(1);
            } else if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
                ai_disarm_multiturn();
                const uint8_t grade = ecard_ui_pick_selected_grade();
                const uint8_t deck_idx = ecard_ui_deck_selection();
                ecard_deck_info_t info;
                memset(&info, 0, sizeof(info));
                (void)ecard_cards_deck_info(kind, grade, deck_idx, &info);
                uint8_t count = 0;
                const ecard_card_t *cards =
                    ecard_cards_load_deck(kind, grade, deck_idx, &count);
                if (cards && count > 0) {
                    ecard_ui_set_deck_meta(grade, info.title);
                    ecard_flashcard_load(
                        cards, count,
                        poem ? 0 : ecard_cards_word_deck_offset(grade, deck_idx));
                    ecard_ui_show_page(poem ? ECARD_UI_PAGE_POEM : ECARD_UI_PAGE_WORD);
                }
            }
        }
        return;
    }

    /* 闪卡页：OK 翻面；翻面后 UP 记不认识、DOWN 记认识。 */
    if (page == ECARD_UI_PAGE_POEM || page == ECARD_UI_PAGE_WORD) {
        if (ecard_flashcard_is_finished()) {
            if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
                ai_disarm_multiturn();
                ecard_xiaozhi_reset_playback();
                ecard_ui_show_page(page == ECARD_UI_PAGE_POEM
                                       ? ECARD_UI_PAGE_POEM_PICK
                                       : ECARD_UI_PAGE_WORD_PICK);
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
                ai_disarm_multiturn();
                ecard_xiaozhi_reset_playback();
                ecard_flashcard_mark(false);
                ecard_ui_flashcard_refresh();
            }
        } else if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
            if (ecard_flashcard_is_flipped()) {
                ai_disarm_multiturn();
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
            ecard_message_t latest;
            if (ecard_messages_copy_latest_unread(&latest)) {
                start_playback(&latest);
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
                break;
            case ECARD_MENU_DEVICE:
            default:
                ecard_ui_show_page(ECARD_UI_PAGE_DEVICE);
                break;
            }
            break;
        case ECARD_UI_PAGE_MESSAGES: {
            ecard_message_t selected;
            if (ecard_messages_copy_recent((int)ecard_ui_messages_selection(), &selected)) {
                start_playback(&selected);
            } else {
                ecard_ui_show_page(ECARD_UI_PAGE_MENU);
            }
            break;
        }
        case ECARD_UI_PAGE_PAIR:
            ecard_ui_show_page(ECARD_UI_PAGE_DEVICE);
            break;
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
        } else if (page == ECARD_UI_PAGE_DEVICE) {
            ecard_ui_show_page(ECARD_UI_PAGE_PAIR);
        } else if (page == ECARD_UI_PAGE_PAIR) {
            ecard_cat_room_rotate_pair_code();
            ecard_ui_pair_refresh();
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
    ecard_cat_room_init();
    ecard_network_init();
    ecard_messages_init();
    ecard_voice_init();
    ecard_ui_reset_navigation();
    ecard_ui_feed_activity();
    s_app_state = ECARD_APP_STATE_IDLE;
    s_play_progress_sec = 0;
    s_play_progress_ms = 0;
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
    ai_disarm_multiturn();
    s_ai_swallow_up_click = false;
    s_ai_stopping = false;
    s_ai_stop_done = false;
    s_ai_prepping = false;
    s_ai_prep_done = false;
    s_ai_prep_cancel = false;
    s_ai_resuming = false;
    s_ai_reopen = false;
    s_menu_press_moved = false;
    s_ai_listen_ms = 0;
    s_pomo_ai_owned = false;
    s_pomo_ai_held = false;
    s_pomo_ai_pending_listen = false;
    s_pomo_ai_used_listen = false;
    s_pomo_ai_ready_notified = false;
    clear_pomo_ai_paused_timer();
    s_pomo_ai_grace_ms = 0;
    s_pomo_ai_connect_ms = 0;
    s_rec_ui_div = 0;
    s_rec_finish_pending = false;
}

void ecard_app_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev) {
    // 任何按键动作均重置 2 分钟无操作计时，并唤醒屏幕
    ecard_ui_feed_activity();
    if (btn == BSP_BTN_UP && ev == BSP_BTN_PRESS) {
        s_ai_swallow_up_click = false;
    }

    // 首次配网是强制流程：在完成 Wi-Fi 与身份之前，不允许长按 OK 逃到菜单，
    // 也不允许长按 DOWN 触发求助。
    const ecard_ui_page_t current_page = ecard_ui_get_page();
    if (current_page == ECARD_UI_PAGE_SETUP) {
        if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
            (void)ecard_portal_start();
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
            ecard_messages_clear_all();
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
        if (s_ai_listen) {
            // 松开 UP（及紧随其后的残留 CLICK）不结束聆听：保持免持录音。
            if (btn == BSP_BTN_UP &&
                (ev == BSP_BTN_RELEASE || ev == BSP_BTN_CLICK)) {
                s_ai_swallow_up_click = false;
                return;
            }
            if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
                ai_finish_listen();
                sync_pomodoro_ai_pause();
                return;
            }
            if (btn == BSP_BTN_DOWN && ev == BSP_BTN_CLICK) {
                ai_cancel_listen("已结束语音对话");
                sync_pomodoro_ai_pause();
                return;
            }
            if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {
                ai_cancel_listen(NULL);
                if (current_page == ECARD_UI_PAGE_POMODORO) {
                    leave_pomodoro_page();
                } else if (ai_page_active(current_page)) {
                    ecard_xiaozhi_reset_playback();
                }
                ecard_ui_show_page(ECARD_UI_PAGE_MENU);
                return;
            }
            return;
        }
        if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
            finish_recording();
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
    sync_pomodoro_ai_pause();
}

void ecard_app_tick(uint8_t current_hour, uint8_t current_minute, uint32_t delta_ms) {
    // 留言列表按“距现在多久”排序与过滤，按实际经过毫秒推进年龄。
    ecard_messages_tick(delta_ms);

    // 后台 HTTP 同步完成（新留言落盘或本机语音 ASR 转写回写）后，安全在 LVGL 锁内刷新界面。
    if (ecard_network_consume_sync_dirty()) {
        if (s_app_state == ECARD_APP_STATE_PLAYING && s_play_message_id != 0) {
            ecard_message_t updated;
            if (ecard_messages_copy_by_id(s_play_message_id, &updated)) {
                ecard_ui_set_playing_message(&updated);
            }
        }
        ecard_ui_refresh_content();
    }

    const ecard_ui_page_t cur_page = ecard_ui_get_page();
    if (s_ai_prepping && s_ai_prep_done) {
        const bool canceled = s_ai_prep_cancel;
        s_ai_prep_done = false;
        s_ai_prepping = false;
        s_ai_prep_cancel = false;
        if (!canceled && s_ai_session &&
            (ai_page_active(cur_page) || (cur_page == ECARD_UI_PAGE_POMODORO && s_pomo_ai_owned))) {
            ecard_xiaozhi_session_start();
        } else if (ecard_xiaozhi_is_busy()) {
            s_ai_session = true;
            ai_session_end();
        } else {
            s_ai_session = false;
            s_pomo_ai_owned = false;
            ai_resume_lan_http_async();
        }
    }
    if (s_ai_stopping && s_ai_stop_done) {
        s_ai_stop_done = false;
        s_ai_stopping = false;
        if (s_ai_reopen &&
            (ai_page_active(cur_page) || (cur_page == ECARD_UI_PAGE_POMODORO && s_pomo_ai_owned))) {
            s_ai_reopen = false;
            ai_session_begin();
        } else {
            s_ai_reopen = false;
            ai_resume_lan_http_async();
        }
    }

    // 免按键多轮续聊推进：本轮语音提问发走后，等小智 TTS 播完并静置 350ms 自动开启下一轮聆听
    bool started_followup = false;
    if (!s_ai_listen && s_ai_multiturn && s_ai_turn_waiting_tts) {
        const bool on_ai_scene = ai_page_active(cur_page) ||
                                 (cur_page == ECARD_UI_PAGE_POMODORO && s_pomo_ai_owned);
        if (!on_ai_scene || !s_ai_session || s_ai_stopping) {
            ai_disarm_multiturn();
        } else if (ecard_xiaozhi_is_speaking()) {
            s_ai_turn_saw_tts = true;
            s_ai_turn_wait_ms = 0;
            s_ai_followup_delay_ms = 0;
        } else if (s_ai_turn_saw_tts) {
            s_ai_followup_delay_ms = (uint16_t)(s_ai_followup_delay_ms + delta_ms);
            if (s_ai_followup_delay_ms >= ECARD_AI_FOLLOWUP_SETTLE_MS) {
                s_ai_turn_waiting_tts = false;
                s_ai_turn_saw_tts = false;
                s_ai_followup_delay_ms = 0;
                started_followup = ai_start_listen_ex(true);
            }
        } else {
            s_ai_turn_wait_ms = (uint16_t)(s_ai_turn_wait_ms + delta_ms);
            if (s_ai_turn_wait_ms >= ECARD_AI_REPLY_TIMEOUT_MS) {
                ai_disarm_multiturn();
            }
        }
    }

    if (s_ai_listen) {
        if (!started_followup) {
            s_ai_listen_ms = (uint16_t)(s_ai_listen_ms + delta_ms);
        }
        const bool voice = ecard_xiaozhi_mic_voice_detected();
        const bool vad_done = ecard_xiaozhi_mic_vad_complete();
        const uint8_t amp = ecard_xiaozhi_mic_amplitude();
        if (vad_done) {
            ai_finish_listen();
        } else if (s_ai_listen_ms >= ECARD_AI_LISTEN_MAX_MS) {
            if (s_ai_auto_followup && !voice) {
                ai_cancel_listen("对话已收起，长按 UP 可继续问");
            } else if (ecard_xiaozhi_mic_active() && !voice) {
                ai_cancel_listen("没听到声音，请长按 UP 再试");
            } else {
                ai_finish_listen();
            }
        } else {
            const uint16_t left = (uint16_t)(ECARD_AI_LISTEN_MAX_MS - s_ai_listen_ms);
            uint8_t sec = (uint8_t)((left + 999) / 1000);
            if (sec < 1) sec = 1;
            ecard_ui_show_ai_listen_modal(sec, amp);
        }
    } else if (cur_page == ECARD_UI_PAGE_POMODORO && s_pomo_ai_owned) {
        if (s_pomo_ai_pending_listen) {
            if (!s_ai_stopping && !s_ai_prepping && ecard_xiaozhi_session_ready()) {
                s_pomo_ai_pending_listen = false;
                s_pomo_ai_connect_ms = 0;
                if (ai_start_listen()) {
                    s_pomo_ai_used_listen = true;
                    s_pomo_ai_grace_ms = 0;
                }
            } else if (!s_ai_stopping) {
                s_pomo_ai_connect_ms = (uint16_t)(s_pomo_ai_connect_ms + delta_ms);
                if ((!s_ai_prepping && (ecard_xiaozhi_connect_failed() || ecard_xiaozhi_restart_pending())) ||
                    s_pomo_ai_connect_ms >= ECARD_POMO_AI_CONNECT_TIMEOUT_MS) {
                    s_pomo_ai_pending_listen = false;
                    s_pomo_ai_held = false;
                    s_pomo_ai_owned = false;
                    s_pomo_ai_grace_ms = 0;
                    s_pomo_ai_connect_ms = 0;
                    ai_session_end();
                    ecard_ui_pomodoro_refresh();
                    ecard_ui_set_hint("小智连接失败，请稍后再试");
                }
            }
        } else if (s_ai_session) {
            if (ecard_xiaozhi_restart_pending() && !s_ai_stopping) {
                s_ai_reopen = false;
                s_pomo_ai_owned = false;
                s_pomo_ai_grace_ms = 0;
                s_pomo_ai_connect_ms = 0;
                ai_session_end();
                ecard_ui_pomodoro_refresh();
                ecard_ui_set_hint("小智连接失败，请稍后再试");
            } else if (s_pomo_ai_held || s_ai_multiturn || ecard_xiaozhi_is_speaking()) {
                s_pomo_ai_grace_ms = 0;
                s_pomo_ai_connect_ms = 0;
            } else if (!s_ai_stopping && !s_ai_prepping && ecard_xiaozhi_session_ready()) {
                s_pomo_ai_connect_ms = 0;
                if (!s_pomo_ai_used_listen && !s_pomo_ai_ready_notified) {
                    s_pomo_ai_ready_notified = true;
                    s_pomo_ai_grace_ms = 0;
                    ecard_ui_set_hint_persistent("小智已就绪，长按 UP 说话");
                } else {
                    s_pomo_ai_grace_ms = (uint16_t)(s_pomo_ai_grace_ms + delta_ms);
                }
                if (s_pomo_ai_grace_ms >= ECARD_POMO_AI_GRACE_MS) {
                    s_pomo_ai_owned = false;
                    s_pomo_ai_grace_ms = 0;
                    ai_session_end();
                    ecard_ui_clear_pomodoro_hint();
                }
            } else if (!s_ai_stopping) {
                s_pomo_ai_connect_ms = (uint16_t)(s_pomo_ai_connect_ms + delta_ms);
                if ((!s_ai_prepping && ecard_xiaozhi_connect_failed()) ||
                    s_pomo_ai_connect_ms >= ECARD_POMO_AI_CONNECT_TIMEOUT_MS) {
                    s_pomo_ai_owned = false;
                    s_pomo_ai_grace_ms = 0;
                    s_pomo_ai_connect_ms = 0;
                    ai_session_end();
                    ecard_ui_pomodoro_refresh();
                    ecard_ui_set_hint("小智连接失败，请稍后再试");
                }
            }
        } else if (!s_ai_stopping) {
            s_pomo_ai_owned = false;
            s_pomo_ai_connect_ms = 0;
        }
    } else if (s_ai_session && !ai_page_active(cur_page)) {
        s_pomo_ai_owned = false;
        s_pomo_ai_connect_ms = 0;
        ai_session_end();
    } else if (s_ai_session && !s_ai_listen &&
               ecard_xiaozhi_restart_pending() && !s_ai_stopping) {
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

    // 番茄钟仅在当前停留于番茄钟页时计时；与小智语音交互期间自动暂停专注倒计时
    sync_pomodoro_ai_pause();
    if (ecard_ui_get_page() == ECARD_UI_PAGE_POMODORO) {
        const ecard_pomodoro_phase_t prev_phase = ecard_pomodoro_phase();
        ecard_pomodoro_tick(delta_ms);
        const uint8_t done_min = ecard_pomodoro_take_completed_focus_minutes();
        if (done_min > 0) {
            (void)ecard_cat_room_award_focus_minutes(done_min);
        }
        if (ecard_pomodoro_phase() != prev_phase) {
            ecard_ui_feed_activity();
        }
        ecard_ui_pomodoro_refresh();
    }

    // 3. 正在播放中的倒计时流转（按实际毫秒累计整秒，兼容 200ms 与 1000ms 心跳）
    if (s_app_state == ECARD_APP_STATE_PLAYING) {
        s_play_progress_ms = (uint16_t)(s_play_progress_ms + delta_ms);
        while (s_play_progress_ms >= 1000) {
            s_play_progress_ms = (uint16_t)(s_play_progress_ms - 1000);
            s_play_progress_sec++;
        }
        ecard_ui_show_playing_modal(s_play_progress_sec, s_play_total_sec);
        const uint8_t grace = s_play_has_audio ? 1 : 0;
        if (s_play_progress_sec >= (uint8_t)(s_play_total_sec + grace)) {
            stop_playback();
        }
    }
}

void ecard_app_feed_mic_pcm(const int16_t *pcm, size_t samples) {
    if (s_app_state != ECARD_APP_STATE_RECORDING || s_rec_finish_pending || !pcm || samples == 0) {
        return;
    }

    uint8_t rem_sec = 0;
    uint8_t amp = 0;
    if (!ecard_voice_write_pcm(pcm, samples, &rem_sec, &amp)) {
        // 录满上限：若已注册异步回调，交由 input_task 在 LVGL 锁外落盘，避免压爆 audio_task 栈
        if (s_record_full_cb) {
            s_rec_finish_pending = true;
            s_record_full_cb();
        } else {
            finish_recording();
        }
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
