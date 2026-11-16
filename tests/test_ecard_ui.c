#include "ecard_profile.h"
#include "ecard_cards.h"
#include "ecard_ui.h"
#include <assert.h>
#include <stdio.h>

static void test_idle_timeout_2min(void) {
    ecard_ui_reset_navigation();
    ecard_ui_feed_activity();

    // 步进 60 秒 (60,000 ms) - 不应超时
    bool timed_out = ecard_ui_check_idle_timeout(60000);
    assert(!timed_out);

    // 步进 59 秒 - 累计 119 秒 - 不应超时
    timed_out = ecard_ui_check_idle_timeout(59000);
    assert(!timed_out);

    // 步进 2 秒 - 累计 121 秒 (超过 120 秒) - 触发超时
    timed_out = ecard_ui_check_idle_timeout(2000);
    assert(timed_out);

    // 此时已经处于 dimmed 状态，再次步进不应重复触发边缘
    timed_out = ecard_ui_check_idle_timeout(5000);
    assert(!timed_out);

    // 触发按键活跃喂狗
    ecard_ui_feed_activity();

    // 再次步进 10 秒 - 计时已被重置
    timed_out = ecard_ui_check_idle_timeout(10000);
    assert(!timed_out);
}

static void test_idle_timeout_paused_by_modal(void) {
    ecard_ui_reset_navigation();
    ecard_ui_show_recording_modal(30, 40);

    // 模态层存在时不得熄屏，且计时被清零
    bool timed_out = ecard_ui_check_idle_timeout(130000);
    assert(!timed_out);

    ecard_ui_hide_modal();
    timed_out = ecard_ui_check_idle_timeout(60000);
    assert(!timed_out);
}

static void test_modal_state_machine(void) {
    ecard_ui_reset_navigation();
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);

    // 录音模态 (30 秒倒计时)
    ecard_ui_show_recording_modal(30, 45);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_RECORDING);
    ecard_ui_show_recording_modal(15, 60);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_RECORDING);
    ecard_ui_hide_modal();
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);

    // 播放模态
    ecard_ui_show_playing_modal(5, 12);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_PLAYING);
    ecard_ui_hide_modal();
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);

    // SOS 确认与已发送
    ecard_ui_show_sos_pending(3);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_SOS_PENDING);
    ecard_ui_hide_modal();
    ecard_ui_show_sos_sent();
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_SOS_SENT);
    ecard_ui_hide_modal();
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
}

static void test_page_navigation(void) {
    ecard_ui_reset_navigation();
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_HOME);
    assert(ecard_ui_menu_selection() == 0);

    ecard_ui_show_page(ECARD_UI_PAGE_MENU);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MENU);

    ecard_ui_menu_move(1);
    assert(ecard_ui_menu_selection() == ECARD_MENU_QUIZ);
    ecard_ui_menu_move(1);
    assert(ecard_ui_menu_selection() == ECARD_MENU_POEM);
    ecard_ui_menu_move(1);
    assert(ecard_ui_menu_selection() == ECARD_MENU_WORD);
    ecard_ui_menu_move(1);
    assert(ecard_ui_menu_selection() == ECARD_MENU_POMODORO);
    ecard_ui_menu_move(1);
    assert(ecard_ui_menu_selection() == ECARD_MENU_DEVICE);
    ecard_ui_menu_move(1);
    assert(ecard_ui_menu_selection() == ECARD_MENU_VOICE); // 循环
    ecard_ui_menu_move(-1);
    assert(ecard_ui_menu_selection() == ECARD_MENU_DEVICE);

    ecard_ui_show_page(ECARD_UI_PAGE_MESSAGES);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_MESSAGES);
    ecard_ui_show_page(ECARD_UI_PAGE_QUIZ);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_QUIZ);
    ecard_ui_show_page(ECARD_UI_PAGE_POEM_PICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POEM_PICK);
    ecard_ui_show_page(ECARD_UI_PAGE_POEM);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POEM);
    ecard_ui_show_page(ECARD_UI_PAGE_WORD_PICK);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_WORD_PICK);
    ecard_ui_show_page(ECARD_UI_PAGE_WORD);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_WORD);
    ecard_ui_show_page(ECARD_UI_PAGE_POMODORO);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_POMODORO);
    ecard_ui_show_page(ECARD_UI_PAGE_DEVICE);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_DEVICE);

    // 模态层存在时页面切换被拒绝
    // 年级选择：循环移动与设置显示年级
    ecard_ui_grade_move(1);
    assert(ecard_ui_grade_selection() == 1);
    ecard_ui_grade_move(1);
    assert(ecard_ui_grade_selection() == 2);
    ecard_ui_grade_move(-3);
    assert(ecard_ui_grade_selection() == ECARD_GRADE_COUNT - 1);
    ecard_ui_set_deck_grade(6);
    ecard_ui_set_deck_grade(0);
    ecard_ui_grade_move(1);
    assert(ecard_ui_grade_selection() == 0);

    ecard_ui_show_sos_pending(3);
    ecard_ui_show_page(ECARD_UI_PAGE_HOME);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_DEVICE);
    ecard_ui_hide_modal();

    ecard_ui_reset_navigation();
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_HOME);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
}

int main(void) {
    printf("Running ecard_ui tests...\n");
    test_idle_timeout_2min();
    test_idle_timeout_paused_by_modal();
    test_modal_state_machine();
    test_page_navigation();
    printf("ecard_ui tests passed!\n");
    return 0;
}
