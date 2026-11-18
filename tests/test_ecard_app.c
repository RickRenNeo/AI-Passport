#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#include "ecard_app.h"
#include "ecard_cat_room.h"
#include "ecard_flashcard.h"
#include "ecard_messages.h"
#include "ecard_pomodoro.h"
#include "ecard_profile.h"
#include "ecard_setup.h"
#include "ecard_quiz.h"
#include "ecard_ui.h"
#include "ecard_voice.h"
#include "ecard_xiaozhi.h"
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

    // 首页短按 OK 直接查看/收听最新一条未读；无音频负载时按消息自身时长展示转写，不回退合成占位音
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_PLAYING);
    assert(ecard_voice_get_state() == ECARD_VOICE_IDLE);

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
    ecard_cards_reset_curriculum();
    ecard_app_init();
    clear_unread_messages();

    // 默认出厂档案为“三年级(2)班”，设备保留 [2, 3, 4] 三个年级，默认光标停在三年级（槽位 1）
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK); // QUIZ
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK); // POEM
    assert(ecard_ui_menu_selection() == ECARD_MENU_POEM);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POEM_PICK);
    assert(ecard_ui_pick_step() == ECARD_PICK_STEP_GRADE);
    assert(ecard_ui_pick_selected_grade() == 3);

    // 第 1 步按 OK 进入三年级古诗古文分组列表，再选第 3 组（古文组）按 OK 进入闪卡
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POEM_PICK);
    assert(ecard_ui_pick_step() == ECARD_PICK_STEP_DECK);
    assert(ecard_ui_deck_selection() == 0);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_ui_deck_selection() == 2);

    uint8_t expected_count = 0;
    (void)ecard_cards_load_deck(ECARD_CARD_POEM, 3, 2, &expected_count);
    assert(expected_count > 0);

    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POEM);
    assert(ecard_flashcard_total() == expected_count);
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
    assert(ecard_flashcard_remaining() == expected_count - 1);

    // 长按 OK 从闪卡返回分组列表（保持第 2 步），再长按返回选年级（第 1 步），再长按回主菜单
    const unsigned reset_poem_exit = ecard_stub_reset_playback_calls;
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_stub_reset_playback_calls == reset_poem_exit + 1);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POEM_PICK);
    assert(ecard_ui_pick_step() == ECARD_PICK_STEP_DECK);
    assert(ecard_ui_deck_selection() == 2);

    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POEM_PICK);
    assert(ecard_ui_pick_step() == ECARD_PICK_STEP_GRADE);

    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
}

static void test_ecard_app_word_deck_flow(void) {
    ecard_cards_reset_curriculum();
    // 模拟家长把学生年级配置为六年级（设备保留 [4, 5, 6] 年级）
    assert(ecard_cards_set_curriculum(6, NULL, 0));
    ecard_app_init();
    assert(ecard_cards_set_curriculum(6, NULL, 0));
    clear_unread_messages();

    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    /* 菜单顺序：语音留言 → 随堂答题 → 古诗 → 英语单词（“我的证件”已移除）。 */
    for (int i = 0; i < 3; ++i) {
        ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    }
    assert(ecard_ui_menu_selection() == ECARD_MENU_WORD);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_WORD_PICK);
    assert(ecard_ui_pick_step() == ECARD_PICK_STEP_GRADE);
    // 当前年级为 6，默认停在 [4, 5, 6] 的槽位 2（六年级）
    assert(ecard_ui_grade_selection() == 2);
    assert(ecard_ui_pick_selected_grade() == 6);

    // 按 OK 进入六年级 12 个英语单元列表，选到第 5 个单元（index = 4，上册 Unit 5）
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_pick_step() == ECARD_PICK_STEP_DECK);
    for (int i = 0; i < 4; ++i) {
        ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    }
    assert(ecard_ui_deck_selection() == 4);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_WORD);
    assert(ecard_flashcard_total() == 10);
    assert(ecard_flashcard_audio_index() == ecard_cards_word_deck_offset(6, 4));

    // 翻面只换文字，保持在同一张卡的发音下标
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_flashcard_is_flipped());
    assert(ecard_flashcard_audio_index() == ecard_cards_word_deck_offset(6, 4));

    // 记认识后自动抽下一张，切换时必须中止上一张卡的朗读和例句追问
    extern unsigned ecard_stub_reset_playback_calls;
    const unsigned reset_word_mark = ecard_stub_reset_playback_calls;
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_stub_reset_playback_calls == reset_word_mark + 1);
    assert(ecard_flashcard_audio_index() == ecard_cards_word_deck_offset(6, 4) + 1);

    // 长按 OK 退出闪卡页回到单元列表，再连按两次长按 OK 退回菜单
    const unsigned reset_word_exit = ecard_stub_reset_playback_calls;
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_stub_reset_playback_calls == reset_word_exit + 1);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_WORD_PICK);
    assert(ecard_ui_pick_step() == ECARD_PICK_STEP_DECK);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_pick_step() == ECARD_PICK_STEP_GRADE);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
    ecard_cards_reset_curriculum();
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

static void feed_test_pcm_ms(int16_t level, uint32_t duration_ms) {
    int16_t chunk[320];
    for (int i = 0; i < 320; ++i) {
        chunk[i] = (i & 1) ? level : (int16_t)(-level);
    }
    const uint32_t chunks = duration_ms / 20u;
    for (uint32_t c = 0; c < chunks; ++c) {
        ecard_xiaozhi_feed_mic_pcm(chunk, 320);
    }
}

static void test_ecard_app_ai_long_press_followup(void) {
    extern bool ecard_stub_ai_ready;
    extern bool ecard_stub_ai_speaking;
    extern unsigned ecard_stub_listen_start_calls;
    extern unsigned ecard_stub_listen_stop_calls;
    extern unsigned ecard_stub_listen_cancel_calls;

    ecard_stub_ai_ready = true;
    ecard_stub_ai_speaking = false;
    ecard_stub_listen_start_calls = 0;
    ecard_stub_listen_stop_calls = 0;
    ecard_stub_listen_cancel_calls = 0;

    ecard_app_init();
    clear_unread_messages();

    // 首页 -> 菜单 -> 随堂答题（菜单第二项）
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_QUIZ);

    // 1. 讲解未播完时（barge-in）长按 UP 1~2 秒触发后松开：立即打断播报并进入免持聆听，倒计时从 10 秒开始
    ecard_stub_ai_speaking = true;
    const uint8_t selected = ecard_quiz_selected();
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    assert(!ecard_xiaozhi_is_speaking());
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);
    assert(ecard_app_ai_listening());
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_RECORDING);
    assert(ecard_ui_modal_remaining_sec() == 10);
    assert(ecard_stub_listen_start_calls == 1);

    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);
    assert(ecard_app_ai_listening());
    assert(ecard_quiz_selected() == selected);

    // 1b. 4x 数字增益后的环境底噪（~320）不得误触发人声检测
    feed_test_pcm_ms(320, 600);
    assert(!ecard_xiaozhi_mic_voice_detected());

    // 2. 学生说话（400ms 有效语音）后停顿 1.5 秒：VAD 自动提交，无需再按键
    feed_test_pcm_ms(1800, 400);
    assert(ecard_xiaozhi_mic_voice_detected());
    ecard_app_tick(9, 40, 600);
    assert(ecard_app_ai_listening());

    feed_test_pcm_ms(320, 1520);
    assert(ecard_xiaozhi_mic_vad_complete());
    ecard_app_tick(9, 40, 200);
    assert(!ecard_app_ai_listening());
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
    assert(ecard_stub_listen_stop_calls == 1);

    // 3. 多轮对话第 2 轮：小智 TTS 播完 + 400ms 静置后自动开启下一轮聆听，
    //    倒计时必须统一从 10 秒单调递减，过 2 秒或开口说话后绝不跳回 9/8/7
    ecard_stub_ai_speaking = true;
    ecard_app_tick(9, 40, 1000);
    assert(!ecard_app_ai_listening());
    ecard_stub_ai_speaking = false;
    ecard_app_tick(9, 40, 200);
    assert(!ecard_app_ai_listening());
    ecard_app_tick(9, 40, 200);
    assert(ecard_app_ai_listening());
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);
    assert(ecard_stub_listen_start_calls == 2);
    assert(ecard_ui_modal_remaining_sec() == 10);

    uint8_t prev_sec = ecard_ui_modal_remaining_sec();
    for (int i = 0; i < 10; ++i) { // 经过 2 秒底噪：10 -> 9 -> 8
        feed_test_pcm_ms(320, 200);
        ecard_app_tick(9, 40, 200);
        const uint8_t cur_sec = ecard_ui_modal_remaining_sec();
        assert(cur_sec <= prev_sec);
        prev_sec = cur_sec;
    }
    assert(prev_sec == 8);
    // 第 2 秒后开口说话：倒计时继续从 8 往下走，绝不向上跳变
    feed_test_pcm_ms(1600, 400);
    assert(ecard_xiaozhi_mic_voice_detected());
    ecard_app_tick(9, 40, 400);
    assert(ecard_ui_modal_remaining_sec() <= prev_sec);
    assert(ecard_ui_modal_remaining_sec() == 8);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(!ecard_app_ai_listening());
    assert(ecard_stub_listen_stop_calls == 2);

    // 4. 多轮对话第 3 轮：TTS 播完再次自动开启聆听（10s 单调倒数）；若 10 秒内无人说话，静默取消并退出多轮
    ecard_stub_ai_speaking = true;
    ecard_app_tick(9, 40, 600);
    ecard_stub_ai_speaking = false;
    ecard_app_tick(9, 40, 400);
    assert(ecard_app_ai_listening());
    assert(ecard_stub_listen_start_calls == 3);
    assert(ecard_ui_modal_remaining_sec() == 10);

    prev_sec = ecard_ui_modal_remaining_sec();
    for (int i = 0; i < 49; ++i) {
        feed_test_pcm_ms(320, 200);
        ecard_app_tick(9, 40, 200);
        assert(ecard_app_ai_listening());
        const uint8_t cur_sec = ecard_ui_modal_remaining_sec();
        assert(cur_sec <= prev_sec);
        prev_sec = cur_sec;
    }
    feed_test_pcm_ms(320, 200);
    ecard_app_tick(9, 40, 200);
    assert(!ecard_app_ai_listening());
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    assert(ecard_stub_listen_cancel_calls == 1);
    assert(ecard_stub_listen_stop_calls == 2);

    // 5. 主动按 DOWN 取消聆听，以及满 10 秒硬上限自动提交
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    assert(ecard_app_ai_listening());
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(!ecard_app_ai_listening());
    assert(ecard_stub_listen_cancel_calls == 2);

    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    feed_test_pcm_ms(40, 200);
    feed_test_pcm_ms(1500, 400);
    for (int i = 0; i < 9; ++i) {
        feed_test_pcm_ms(1500, 1000);
        ecard_app_tick(9, 40, 1000);
        assert(ecard_app_ai_listening());
    }
    ecard_app_tick(9, 40, 1000);
    assert(!ecard_app_ai_listening());
    assert(ecard_stub_listen_stop_calls == 3);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
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

    // 首页点击 OK 播放最新未读：200ms 心跳下不能提前按每次 tick 累加 1 秒
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);
    for (int i = 0; i < 15; ++i) {
        ecard_app_tick(9, 40, 200);
        assert(ecard_app_get_state() == ECARD_APP_STATE_PLAYING);
    }
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

static void nav_to_menu_item(ecard_menu_item_t item) {
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
    for (int i = 0; i < (int)item; ++i) {
        ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    }
    assert(ecard_ui_menu_selection() == item);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
}

static void test_pomodoro_on_demand_ai_and_points_and_abort(void) {
    extern bool ecard_stub_ai_ready;
    extern bool ecard_stub_ai_speaking;
    extern bool ecard_stub_lan_ai_busy;
    extern unsigned ecard_stub_ai_session_start_calls;
    extern unsigned ecard_stub_ai_session_stop_calls;

    ecard_cat_room_reset_all();
    ecard_pomodoro_reset();
    ecard_pomodoro_set_focus_minutes(ECARD_POMODORO_FOCUS_MIN);
    ecard_app_init();
    clear_unread_messages();
    ecard_stub_ai_ready = true;
    ecard_stub_ai_speaking = false;
    ecard_stub_lan_ai_busy = false;

    const unsigned starts_before = ecard_stub_ai_session_start_calls;
    nav_to_menu_item(ECARD_MENU_POMODORO);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POMODORO);

    // 1. 进入番茄钟页面不得预连小智，LAN HTTP 保持可用
    assert(ecard_stub_ai_session_start_calls == starts_before);
    assert(!ecard_stub_lan_ai_busy);

    // 1b. 连接尚未就绪时按长按 UP 1~2s 后松手：连上后自动进入免持聆听，无需长按第二次；按 OK 发送或 DOWN 取消后保留 20s 宽限再释放 HTTP
    ecard_stub_ai_ready = false;
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    assert(ecard_stub_lan_ai_busy);
    assert(strcmp(ecard_ui_pomodoro_override_hint(), "正在连接小智…") == 0);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    assert(strcmp(ecard_ui_pomodoro_override_hint(), "正在连接小智…") == 0);
    ecard_ui_feed_activity();
    (void)ecard_ui_poll_idle_state(4000);
    assert(strcmp(ecard_ui_pomodoro_override_hint(), "正在连接小智…") == 0);
    ecard_stub_ai_ready = true;
    ecard_app_tick(10, 0, 1000);
    assert(ecard_app_ai_listening());
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(!ecard_app_ai_listening());
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    for (int i = 0; i < 19; ++i) {
        ecard_app_tick(10, 0, 1000);
    }
    assert(ecard_stub_lan_ai_busy);
    ecard_app_tick(10, 0, 1000);
    assert(!ecard_stub_lan_ai_busy);
    assert(ecard_ui_pomodoro_override_hint()[0] == '\0');

    // 2. 长按 UP 触发连接并免持录音 + 多轮续聊；说完并超时退出多轮后再过 20s 释放 HTTP
    ecard_stub_ai_ready = false;
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    assert(ecard_stub_lan_ai_busy);
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);

    ecard_stub_ai_ready = true;
    ecard_app_tick(10, 0, 1000);
    assert(ecard_app_ai_listening());
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);

    // 松开 UP 后仍在免持录音；按 OK 提交首轮提问
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    assert(ecard_app_ai_listening());
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(!ecard_app_ai_listening());
    assert(ecard_app_get_state() == ECARD_APP_STATE_IDLE);
    // 若硬件未补发 CLICK，下一次真实短按 UP (PRESS -> RELEASE -> CLICK) 必须正常 +5 分钟
    const uint8_t min_before = ecard_pomodoro_focus_minutes();
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_PRESS);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_pomodoro_focus_minutes() == min_before + ECARD_POMODORO_FOCUS_STEP);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_pomodoro_focus_minutes() == min_before);

    // 小智回答播完后自动开启第二轮免持聆听；按 DOWN 结束对话后满 20s 自动关闭会话并恢复 LAN HTTP
    ecard_stub_ai_speaking = true;
    ecard_app_tick(10, 0, 5000);
    assert(ecard_stub_lan_ai_busy);
    ecard_stub_ai_speaking = false;
    ecard_app_tick(10, 0, 400);
    assert(ecard_app_ai_listening());
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(!ecard_app_ai_listening());
    const unsigned stops_before = ecard_stub_ai_session_stop_calls;
    for (int i = 0; i < 20; ++i) {
        ecard_app_tick(10, 0, 1000);
    }
    assert(ecard_stub_ai_session_stop_calls == stops_before + 1);
    assert(!ecard_stub_lan_ai_busy);

    // 2b. 专注计时中：点按 UP 即可松手触发后台预连，就绪后常驻提示 20s，再长按 UP 秒开免持录音
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_FOCUS);
    ecard_stub_ai_ready = false;
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_PRESS);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_pomodoro_focus_minutes() == min_before);
    assert(ecard_stub_lan_ai_busy);
    assert(!ecard_app_ai_listening());
    assert(strcmp(ecard_ui_pomodoro_override_hint(), "正在连接小智…") == 0);
    ecard_ui_feed_activity();
    (void)ecard_ui_poll_idle_state(4000);
    assert(strcmp(ecard_ui_pomodoro_override_hint(), "正在连接小智…") == 0);
    ecard_stub_ai_ready = true;
    ecard_app_tick(10, 0, 1000);
    assert(strcmp(ecard_ui_pomodoro_override_hint(), "小智已就绪，长按 UP 说话") == 0);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    assert(ecard_app_ai_listening());
    assert(ecard_app_get_state() == ECARD_APP_STATE_RECORDING);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    assert(ecard_app_ai_listening());
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(!ecard_app_ai_listening());
    for (int i = 0; i < 20; ++i) {
        ecard_app_tick(10, 0, 1000);
    }
    assert(!ecard_stub_lan_ai_busy);
    assert(ecard_ui_pomodoro_override_hint()[0] == '\0');
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_IDLE);

    // 3. 完成一轮 25 分钟 FOCUS 后自动记入 +25 积分，并从暗屏状态自动唤醒
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_FOCUS);
    assert(ecard_ui_poll_idle_state(60000) == ECARD_UI_IDLE_DIM);
    assert(ecard_ui_is_dimmed());
    for (int s = 0; s < 25 * 60; ++s) {
        ecard_app_tick(10, s / 60, 1000);
    }
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_BREAK);
    assert(!ecard_ui_is_dimmed());
    assert(ecard_cat_room_pending_points() == 25);
    assert(ecard_cat_room_completed_sessions() == 1);

    // 3b. 完成 5 分钟 BREAK 进入 DONE 后，按 UP/DOWN 应退回 IDLE 并实时更新专注时长
    for (int s = 0; s < ECARD_POMODORO_BREAK_MIN * 60; ++s) {
        ecard_app_tick(10, 25 + s / 60, 1000);
    }
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_DONE);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_IDLE);
    assert(ecard_pomodoro_focus_minutes() == 30);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(ecard_pomodoro_focus_minutes() == 25);

    // 4. 在 FOCUS 期间按 OK 长按切出页面，立即中止且不得加分
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_FOCUS);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_IDLE);
    assert(ecard_cat_room_pending_points() == 25);

    // 5. 回到菜单重进，开始 FOCUS 并在剩 1 秒暂停后长按 OK 离开 / 长按 DOWN SOS，也必须 0 分中止
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POMODORO);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    for (int s = 0; s < 25 * 60 - 1; ++s) {
        ecard_app_tick(11, s / 60, 1000);
    }
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK); // pause at 00:01
    assert(!ecard_pomodoro_is_running());
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_LONG); // SOS cancels pomodoro
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_IDLE);
    assert(ecard_cat_room_pending_points() == 25);
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_RELEASE);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POMODORO);
    ecard_pomodoro_reset();
    ecard_cat_room_reset_all();
}

static void test_device_to_pair_navigation(void) {
    ecard_cat_room_reset_all();
    ecard_app_init();
    clear_unread_messages();

    nav_to_menu_item(ECARD_MENU_DEVICE);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_DEVICE);
    assert(!ecard_cat_room_is_pairing_active());

    // UP click 进入家长扫码配对页
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_PAIR);
    assert(ecard_cat_room_is_pairing_active());

    // UP click 刷新配对码
    char old_code[16];
    char new_code[16];
    ecard_cat_room_get_pair_code(old_code, sizeof(old_code));
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    ecard_cat_room_get_pair_code(new_code, sizeof(new_code));
    assert(strcmp(old_code, new_code) != 0);

    // OK click 返回设备信息页；再次进入后 OK long 返回菜单，窗口自动关闭
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_DEVICE);
    assert(!ecard_cat_room_is_pairing_active());
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_CLICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_PAIR);
    assert(ecard_cat_room_is_pairing_active());
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);
    assert(!ecard_cat_room_is_pairing_active());
    ecard_cat_room_reset_all();
}

static void test_pomodoro_ai_connect_fail_resets_and_hints(void) {
    extern bool ecard_stub_ai_ready;
    extern bool ecard_stub_ai_connect_failed;
    extern bool ecard_stub_lan_ai_busy;
    extern unsigned ecard_stub_ai_session_stop_calls;

    ecard_cat_room_reset_all();
    ecard_pomodoro_reset();
    ecard_app_init();
    clear_unread_messages();
    ecard_stub_ai_ready = false;
    ecard_stub_ai_connect_failed = false;
    ecard_stub_lan_ai_busy = false;

    nav_to_menu_item(ECARD_MENU_POMODORO);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POMODORO);

    // 1. 按住 UP 等待连接期间服务端/TLS 立即报错：必须退出等待、恢复 LAN HTTP、提示连接失败
    const unsigned stops_before = ecard_stub_ai_session_stop_calls;
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    assert(ecard_stub_lan_ai_busy);
    ecard_stub_ai_connect_failed = true;
    ecard_app_tick(10, 0, 1000);
    assert(!ecard_stub_lan_ai_busy);
    assert(!ecard_app_ai_listening());
    assert(ecard_stub_ai_session_stop_calls == stops_before + 1);
    assert(strcmp(ecard_ui_pomodoro_override_hint(), "小智连接失败，请稍后再试") == 0);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    assert(!ecard_app_ai_listening());

    // 2. 持续按住 UP 但一直未就绪超过 15 秒超时：同样必须回收会话、恢复 LAN HTTP、给出失败提示
    ecard_stub_ai_connect_failed = false;
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    assert(ecard_stub_lan_ai_busy);
    for (int i = 0; i < 14; ++i) {
        ecard_app_tick(10, 0, 1000);
    }
    assert(ecard_stub_lan_ai_busy);
    ecard_app_tick(10, 0, 1000);
    assert(!ecard_stub_lan_ai_busy);
    assert(!ecard_app_ai_listening());
    assert(strcmp(ecard_ui_pomodoro_override_hint(), "小智连接失败，请稍后再试") == 0);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);

    // 3. 确保首页长按 OK 10 秒录音上限未被削弱
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_LONG);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_HOME);
    assert(ECARD_VOICE_RECORD_SEC == 10);
    assert(ECARD_VOICE_RECORD_SEC * ECARD_VOICE_BYTES_PER_SEC == 40000);
    ecard_pomodoro_reset();
    ecard_cat_room_reset_all();
}

static void test_pomodoro_focus_auto_pauses_during_ai_voice(void) {
    extern bool ecard_stub_ai_ready;
    extern bool ecard_stub_ai_speaking;
    extern bool ecard_stub_ai_connect_failed;

    ecard_stub_ai_ready = false;
    ecard_stub_ai_speaking = false;
    ecard_stub_ai_connect_failed = false;

    ecard_app_init();
    clear_unread_messages();
    ecard_pomodoro_reset();
    ecard_pomodoro_set_focus_minutes(ECARD_POMODORO_FOCUS_MIN);
    nav_to_menu_item(ECARD_MENU_POMODORO);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POMODORO);

    // 启动专注倒计时并跑 5 秒
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_FOCUS);
    assert(ecard_pomodoro_is_running());
    ecard_app_tick(10, 0, 5000);
    const uint32_t frozen_ms = ecard_pomodoro_remaining_ms();
    assert(frozen_ms == (uint32_t)ECARD_POMODORO_FOCUS_MIN * 60000u - 5000u);

    // 1. 长按 UP 触发小智连接 + 语音交互：专注倒计时立即自动暂停，连接/聆听/回答/多轮续聊全程不扣秒
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    assert(!ecard_pomodoro_is_running());
    assert(ecard_app_pomo_ai_paused());

    ecard_app_tick(10, 0, 2000); // 连接等待期不扣秒
    assert(ecard_pomodoro_remaining_ms() == frozen_ms);

    ecard_stub_ai_ready = true;
    ecard_app_tick(10, 0, 200); // 自动进入第 1 轮聆听
    assert(ecard_app_ai_listening());
    assert(!ecard_pomodoro_is_running());
    assert(ecard_pomodoro_remaining_ms() == frozen_ms);

    feed_test_pcm_ms(1800, 600);
    ecard_app_tick(10, 0, 600);
    feed_test_pcm_ms(100, 1520);
    ecard_app_tick(10, 0, 200); // VAD 自动提交，进入等待回答期
    assert(!ecard_app_ai_listening());
    assert(!ecard_pomodoro_is_running());
    assert(ecard_pomodoro_remaining_ms() == frozen_ms);

    ecard_stub_ai_speaking = true;
    ecard_app_tick(10, 0, 3000); // 小智语音播报期不扣秒
    assert(!ecard_pomodoro_is_running());
    assert(ecard_pomodoro_remaining_ms() == frozen_ms);

    ecard_stub_ai_speaking = false;
    ecard_app_tick(10, 0, 400); // 自动进入第 2 轮续聊聆听，仍保持暂停
    assert(ecard_app_ai_listening());
    assert(!ecard_pomodoro_is_running());
    assert(ecard_pomodoro_remaining_ms() == frozen_ms);

    // 2. 学生按 DOWN 结束语音对话：专注倒计时立即自动恢复读秒！
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(!ecard_app_ai_listening());
    assert(!ecard_app_pomo_ai_paused());
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_FOCUS);
    assert(ecard_pomodoro_is_running());
    ecard_app_tick(10, 0, 2000);
    assert(ecard_pomodoro_remaining_ms() == frozen_ms - 2000u);

    // 3. 小智正在说话时按 DOWN：仅结束语音交流并恢复专注读秒，绝不误清空本轮专注！
    const uint32_t before_barge = ecard_pomodoro_remaining_ms();
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    feed_test_pcm_ms(1800, 600);
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    ecard_stub_ai_speaking = true;
    ecard_app_tick(10, 0, 1000);
    assert(!ecard_pomodoro_is_running());
    assert(ecard_pomodoro_remaining_ms() == before_barge);

    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(!ecard_xiaozhi_is_speaking());
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_FOCUS);
    assert(ecard_pomodoro_is_running());
    ecard_app_tick(10, 0, 1000);
    assert(ecard_pomodoro_remaining_ms() == before_barge - 1000u);

    // 4. 若学生原本已手动按 OK 暂停专注，与小智交流结束后保持手动暂停，不擅自启动计时
    ecard_app_handle_key(BSP_BTN_OK, BSP_BTN_CLICK);
    assert(!ecard_pomodoro_is_running());
    assert(!ecard_app_pomo_ai_paused());
    const uint32_t manual_paused_ms = ecard_pomodoro_remaining_ms();

    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_LONG);
    ecard_app_handle_key(BSP_BTN_UP, BSP_BTN_RELEASE);
    assert(ecard_app_ai_listening());
    ecard_app_handle_key(BSP_BTN_DOWN, BSP_BTN_CLICK);
    assert(!ecard_app_ai_listening());
    assert(!ecard_pomodoro_is_running());
    ecard_app_tick(10, 0, 2000);
    assert(ecard_pomodoro_remaining_ms() == manual_paused_ms);

    ecard_pomodoro_reset();
    ecard_cat_room_reset_all();
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
    test_pomodoro_on_demand_ai_and_points_and_abort();
    test_device_to_pair_navigation();
    test_pomodoro_ai_connect_fail_resets_and_hints();
    test_pomodoro_focus_auto_pauses_during_ai_voice();
    printf("ecard_app tests passed!\n");
    return 0;
}
