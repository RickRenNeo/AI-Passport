#include "ecard_app.h"
#include "ecard_flashcard.h"
#include "ecard_messages.h"
#include "ecard_pomodoro.h"
#include "ecard_profile.h"
#include "ecard_setup.h"
#include "ecard_quiz.h"
#include "ecard_ui.h"
#include "ecard_voice.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static void clear_unread_messages(void) {
    const ecard_message_t *msg;
    while ((msg = ecard_messages_latest_unread()) != NULL) {
        ecard_messages_mark_read(msg->id);
    }
}

static void test_ecard_app_recording_lifecycle(void) {
    ecard_app_init();
    clear_unread_messages();
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_HOME);

    // 1. 首页长按 OK 开始录音
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_RECORDING);

    // 注入两帧真实麦克风 PCM（16kHz，20ms/帧）
    int16_t frame[320];
    for (int i = 0; i < 320; ++i) frame[i] = (int16_t)(1500 + (i % 20) * 30);
    ecard_app_feed_mic_pcm(frame, 320);
    ecard_app_feed_mic_pcm(frame, 320);
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);

    // 2. 短按 OK 提前结束录音并上传，随后进入本地试听
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);

    // 试听由真实 PCM 渲染驱动，取空后自动回到空闲
    int16_t out[160];
    int guard = 0;
    while (ecard_app_render_playback_pcm(out, 160) > 0 && guard++ < 1000) {
    }
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);

    // 试听结束后必须继续保留这段录音：学生还要在留言列表里重听，
    // 不能再退回内置 7 秒示例语音。
    assert(ecard_voice_has_clip());
    assert(ecard_voice_playback_start());
    ecard_voice_playback_stop();
}

static void test_ecard_app_message_play_and_mutual_exclusion(void) {
    ecard_app_init();
    // 演示数据里有 2 条未读（妈妈 + 王老师）
    assert(ecard_messages_unread_count() == 2);

    // 首页短按 OK 直接收听最新一条未读
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_PLAYING);

    // 推进时钟至播完（演示语音约 7 秒，留 1 秒容错）
    for (int s = 0; s < 10; ++s) {
        ecard_app_tick(10, 0, 1000);
    }

    // 播放完毕，自动标记已读，退出模态
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
    assert(ecard_messages_unread_count() == 1);

    // 再按一次：播放剩余的未读留言，并清空未读
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);
    for (int s = 0; s < 10; ++s) {
        ecard_app_tick(10, 0, 1000);
    }
    assert(ecard_messages_unread_count() == 0);
}

static void test_ecard_app_playback_stop_marks_read(void) {
    ecard_app_init();
    assert(ecard_messages_unread_count() == 2);

    // 校园服务端未接入：首页按 UP 会查询，但不能凭空多出一条未读
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_messages_unread_count() == 2);

    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);

    // 播放中再按 OK 提前停止，也应标记已读并清除模态
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
    assert(ecard_messages_unread_count() == 1);

    // 再查一次也不能复活已读留言
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_messages_unread_count() == 1);
}


static void test_ecard_app_message_list_selection(void) {
    ecard_app_init();
    clear_unread_messages();

    // 首页 -> 菜单 -> 语音留言
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MESSAGES);
    assert(ecard_ui_messages_selection() == 0);

    // DOWN 选中第二条（发出的留言）
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_ui_messages_selection() == 1);
    const ecard_message_t *sel = ecard_messages_recent(1);
    assert(sel && sel->direction == ECARD_MESSAGE_SENT);

    // OK 播放；已读/发出的留言都可以重复收听
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
}

static void test_ecard_app_sos_confirm_and_release(void) {
    ecard_app_init();

    // 长按 DOWN 先进入 3 秒确认，不立即发送
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_LONG);
    assert(ecard_app_get_state() == ECARD_APP_STATE_SOS_PENDING);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_SOS_PENDING);

    // 松开 DOWN 取消
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_RELEASE);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_HOME);
}

static void test_ecard_app_sos_sent(void) {
    ecard_app_init();
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_LONG);

    // 保持按住 3 秒后发送
    ecard_app_tick(10, 0, 1000);
    assert(ecard_app_get_state() == ECARD_APP_STATE_SOS_PENDING);
    ecard_app_tick(10, 0, 1000);
    assert(ecard_app_get_state() == ECARD_APP_STATE_SOS_PENDING);
    ecard_app_tick(10, 0, 1000);
    assert(ecard_app_get_state() == ECARD_APP_STATE_SOS_SENT);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_SOS_SENT);

    // 短按 OK 确认返回首页
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_HOME);
}

static void test_ecard_app_menu_navigation(void) {
    ecard_app_init();
    clear_unread_messages();
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_HOME);

    // 无未读时首页 OK 进入菜单
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
    assert(ecard_ui_menu_selection() == ECARD_MENU_VOICE);

    // DOWN 移动选中项（“我的证件”入口已移除，第二项即随堂答题）
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_ui_menu_selection() == ECARD_MENU_QUIZ);

    // 进入答题页（OK 在答题页是提交答案），长按 OK 返回菜单
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_QUIZ);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);

    // 长按 OK 从菜单回首页
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_HOME);

    // 菜单中的长按 OK 不得启动录音
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_HOME);
}

static void test_ecard_app_recess_polling(void) {
    ecard_app_init();
    const int before = ecard_messages_recent_count();
    assert(before == 5);

    // 09:30 上课时段不触发同步
    ecard_app_tick(9, 30, 1000);
    assert(ecard_messages_recent_count() == before);

    // 09:40 跨入大课间：会发起同步，但服务端未接入，不应凭空多出留言
    ecard_app_tick(9, 40, 1000);
    assert(ecard_messages_recent_count() == before);
    assert(ecard_messages_unread_count() == 2);
}

static void test_ecard_app_quiz_flow(void) {
    ecard_app_init();
    clear_unread_messages();

    // 首页 -> 菜单 -> 随堂答题
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK); // QUIZ
    assert(ecard_ui_menu_selection() == ECARD_MENU_QUIZ);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_QUIZ);
    assert(ecard_quiz_index() == 0);
    assert(ecard_quiz_selected() == 0);
    assert(!ecard_quiz_is_answered());

    // DOWN 选中 B（第一题正确答案是 A），提交后答错
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_quiz_selected() == 1);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_quiz_is_answered());
    assert(ecard_quiz_score() == 0);

    // 已作答后 UP/DOWN 不再改变选项
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_quiz_selected() == 1);

    // 再按 OK 进入下一题：必须先停掉上一题的讲解语音。
    extern unsigned ecard_stub_reset_playback_calls;
    const unsigned reset_before = ecard_stub_reset_playback_calls;
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_stub_reset_playback_calls == reset_before + 1);
    assert(ecard_quiz_index() == 1);
    assert(!ecard_quiz_is_answered());

    // 长按 OK 退出答题页回到菜单，也要中止上一题语音
    const unsigned reset_exit_quiz = ecard_stub_reset_playback_calls;
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_stub_reset_playback_calls == reset_exit_quiz + 1);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
}

static void test_ecard_app_poem_deck_flow(void) {
    ecard_app_init();
    clear_unread_messages();

    // 首页 -> 菜单 -> 古诗接句（第 3 项）-> 年级选择
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK); // QUIZ
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK); // POEM
    assert(ecard_ui_menu_selection() == ECARD_MENU_POEM);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POEM_PICK);
    assert(ecard_ui_grade_selection() == 0);

    // 选到三年级再进入
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_ui_grade_selection() == 2);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POEM);
    assert(ecard_flashcard_total() == 10);
    assert(ecard_flashcard_current() != NULL);

    // 未翻面时 UP/DOWN 不记录
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(!ecard_flashcard_is_flipped());

    // OK 翻面，DOWN 记认识：切下一张卡前必须停止上一张卡的朗读
    extern unsigned ecard_stub_reset_playback_calls;
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_flashcard_is_flipped());
    const unsigned reset_poem_mark = ecard_stub_reset_playback_calls;
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_stub_reset_playback_calls == reset_poem_mark + 1);
    assert(ecard_flashcard_remaining() == 9);

    // 长按 OK 退出到菜单
    const unsigned reset_poem_exit = ecard_stub_reset_playback_calls;
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_stub_reset_playback_calls == reset_poem_exit + 1);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
}

static void test_ecard_app_word_deck_flow(void) {
    ecard_app_init();
    clear_unread_messages();

    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    /* 菜单顺序：语音留言 → 随堂答题 → 古诗 → 英语单词（“我的证件”已移除）。 */
    for (int i = 0; i < 3; ++i) {
        ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    }
    assert(ecard_ui_menu_selection() == ECARD_MENU_WORD);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_WORD_PICK);

    // 选到六年级
    for (int i = 0; i < 5; ++i) {
        ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    }
    assert(ecard_ui_grade_selection() == 5);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_WORD);
    assert(ecard_flashcard_total() == 47);
    assert(ecard_flashcard_audio_index() == ecard_cards_word_offset(6));

    // 翻面只换文字，不触发新的朗读（单测无法观测“是否重新开始”，
    // 因此只验证翻面后仍停留在同一张卡的发音下标）
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_flashcard_is_flipped());
    assert(ecard_flashcard_audio_index() == ecard_cards_word_offset(6));

    // 记认识后自动抽下一张，切换时必须中止上一张卡的朗读和例句追问
    extern unsigned ecard_stub_reset_playback_calls;
    const unsigned reset_word_mark = ecard_stub_reset_playback_calls;
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_stub_reset_playback_calls == reset_word_mark + 1);
    assert(ecard_flashcard_audio_index() == ecard_cards_word_offset(6) + 1);

    // 长按 OK 退出闪卡页
    const unsigned reset_word_exit = ecard_stub_reset_playback_calls;
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_stub_reset_playback_calls == reset_word_exit + 1);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
}

static void test_ecard_app_pomodoro_flow(void) {
    ecard_app_init();
    clear_unread_messages();

    // 首页 -> 菜单 -> 番茄钟（第 5 项）
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    for (int i = 0; i < 4; ++i) {
        ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    }
    assert(ecard_ui_menu_selection() == ECARD_MENU_POMODORO);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POMODORO);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_IDLE);

    // 未开始时 UP/DOWN 选时长：50 分钟为上限
    for (int i = 0; i < 10; ++i) {
        ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    }
    assert(ecard_pomodoro_focus_minutes() == ECARD_POMODORO_FOCUS_MAX_ALLOWED);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_pomodoro_focus_minutes() ==
           ECARD_POMODORO_FOCUS_MAX_ALLOWED - ECARD_POMODORO_FOCUS_STEP);

    // OK 开始专注
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_FOCUS);
    assert(ecard_pomodoro_is_running());

    // 再按 OK 暂停，第三次继续
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(!ecard_pomodoro_is_running());
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_pomodoro_is_running());

    // 心跳推进 1 分钟
    const uint32_t before = ecard_pomodoro_remaining_ms();
    ecard_app_tick(10, 0, 1000);
    assert(ecard_pomodoro_remaining_ms() == before - 1000);

    // DOWN 结束本轮
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_IDLE);
    assert(!ecard_pomodoro_is_running());

    // 长按 OK 退出
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
    ecard_pomodoro_reset();
}

static void test_ecard_app_reset_page(void) {
    ecard_app_init();
    clear_unread_messages();

    // 菜单 -> 设备信息（第 6 项）-> DOWN 进入重置确认页
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    for (int i = 0; i < 5; ++i) {
        ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    }
    assert(ecard_ui_menu_selection() == ECARD_MENU_DEVICE);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_DEVICE);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_RESET);

    // 长按 OK 返回菜单
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
}

static void test_ecard_app_menu_press_is_immediate(void) {
    ecard_app_init();
    clear_unread_messages();
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
    assert(ecard_ui_menu_selection() == ECARD_MENU_VOICE);

    // 真实按键顺序：PRESS 立即移动，随后的 CLICK 不能重复移动。
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_PRESS);
    assert(ecard_ui_menu_selection() == ECARD_MENU_QUIZ);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_ui_menu_selection() == ECARD_MENU_QUIZ);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_PRESS);
    assert(ecard_ui_menu_selection() == ECARD_MENU_VOICE);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_ui_menu_selection() == ECARD_MENU_VOICE);
}

static void test_ecard_app_ai_long_press_followup(void) {
    ecard_app_init();
    clear_unread_messages();

    // 首页 -> 菜单 -> 随堂答题（菜单第二项）
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_QUIZ);

    // 长按 UP 必须进入追问录音，而不是被答题页的无条件 return 吞掉。
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_RECORDING);

    // 录音中每秒刷新，不应提前结束。
    ecard_app_tick(9, 40, 1000);
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);

    // 松开 UP：发送并回到空闲。
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);

    // 松手后按键库可能补发一个 CLICK：必须被吞掉，不能翻动答题选项。
    const uint8_t selected = ecard_quiz_selected();
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_quiz_selected() == selected);
}

static void test_ecard_app_playing_transcript_metadata(void) {
    ecard_app_init();
    clear_unread_messages();

    ecard_message_t incoming;
    memset(&incoming, 0, sizeof(incoming));
    incoming.direction = ECARD_MESSAGE_RECEIVED;
    incoming.sender_role = ECARD_SENDER_TEACHER;
    incoming.duration_sec = 4;
    incoming.unread = true;
    snprintf(incoming.peer, sizeof(incoming.peer), "%s", "李老师");
    snprintf(incoming.text, sizeof(incoming.text), "%s", "下午三点操场集合");
    assert(ecard_messages_add(&incoming, 0) != 0);

    // 首页点击 OK 播放最新未读：播放弹窗必须绑定发送方与转写文字
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);
    const ecard_message_t *playing = ecard_ui_playing_message();
    assert(playing != NULL);
    assert(playing->sender_role == ECARD_SENDER_TEACHER);
    assert(strcmp(playing->peer, "李老师") == 0);
    assert(strcmp(playing->text, "下午三点操场集合") == 0);

    // 停止播放后清除弹窗绑定元数据
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_playing_message() == NULL);
}

static unsigned g_record_full_calls = 0;

static void on_test_record_full(void) {
    g_record_full_calls++;
}

static void test_ecard_app_record_full_async_handoff(void) {
    char dir[128];
    snprintf(dir, sizeof(dir), "/tmp/ecard_app_rec_XXXXXX");
    char *mk = mkdtemp(dir);
    assert(mk != NULL);
    assert(ecard_messages_mount_dir(dir));

    ecard_app_init();
    clear_unread_messages();
    g_record_full_calls = 0;
    ecard_app_set_record_full_cb(on_test_record_full);

    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);

    int16_t frame[320];
    for (int i = 0; i < 320; ++i) {
        frame[i] = (int16_t)(1200 + (i % 30) * 40);
    }
    for (int b = 0; b < 505; ++b) {
        ecard_app_feed_mic_pcm(frame, 320);
    }

    // 录满 10 秒后应仅触发一次异步收尾回调，绝不能在 audio 回调里直接写 SPIFFS
    assert(g_record_full_calls == 1);
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);

    uint8_t dur = 0;
    size_t bytes = 0;
    assert(ecard_app_begin_save_recording(&dur, &bytes));
    assert(dur == ECARD_VOICE_RECORD_SEC);
    assert(bytes == (size_t)ECARD_VOICE_RECORD_SEC * ECARD_VOICE_BYTES_PER_SEC);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);

    // 落盘后必须立即释放 RAM 录音分段缓冲，再启动基于 SPIFFS 文件的本地试听
    const uint32_t msg_id = ecard_app_persist_recorded_clip(dur);
    assert(msg_id != 0);
    assert(!ecard_voice_has_clip());

    ecard_app_start_recorded_playback(msg_id, dur);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_PLAYING);

    int16_t out[320];
    size_t total = 0;
    while (ecard_app_render_playback_pcm(out, 320) > 0) {
        total += 320;
        assert(total <= 4 * bytes + 320);
    }
    assert(total >= 4 * bytes);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);

    ecard_app_set_record_full_cb(NULL);
    ecard_messages_mount_dir(NULL);
    char rm_cmd[192];
    snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", dir);
    (void)system(rm_cmd);
}

int main(void) {
    printf("Running ecard_app tests...\n");
    test_ecard_app_recording_lifecycle();
    test_ecard_app_message_play_and_mutual_exclusion();
    test_ecard_app_playback_stop_marks_read();
    test_ecard_app_message_list_selection();
    test_ecard_app_sos_confirm_and_release();
    test_ecard_app_sos_sent();
    test_ecard_app_menu_navigation();
    test_ecard_app_menu_press_is_immediate();
    test_ecard_app_recess_polling();
    test_ecard_app_quiz_flow();
    test_ecard_app_poem_deck_flow();
    test_ecard_app_word_deck_flow();
    test_ecard_app_pomodoro_flow();
    test_ecard_app_reset_page();
    test_ecard_app_ai_long_press_followup();
    test_ecard_app_playing_transcript_metadata();
    test_ecard_app_record_full_async_handoff();
    printf("ecard_app tests passed!\n");
    return 0;
}
