#include "ecard_profile.h"
#include "ecard_cards.h"
#include "ecard_cat_room.h"
#include "ecard_pomodoro.h"
#include "ecard_ui.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_idle_timeout_2min(void) {
    ecard_pomodoro_reset();
    ecard_ui_reset_navigation();
    ecard_ui_feed_activity();

    // 步进 60 秒 (60,000 ms) - 非专注页不应超时
    bool timed_out = ecard_ui_check_idle_timeout(60000);
    assert(!timed_out);
    assert(!ecard_ui_is_screen_off());

    // 步进 59 秒 - 累计 119 秒 - 不应超时
    timed_out = ecard_ui_check_idle_timeout(59000);
    assert(!timed_out);

    // 步进 2 秒 - 累计 121 秒 (超过 120 秒) - 触发熄屏超时
    timed_out = ecard_ui_check_idle_timeout(2000);
    assert(timed_out);
    assert(ecard_ui_is_screen_off());

    // 此时已经处于熄屏状态，再次步进不应重复触发边缘
    timed_out = ecard_ui_check_idle_timeout(5000);
    assert(!timed_out);
    assert(ecard_ui_is_screen_off());

    // 触发按键活跃喂狗
    ecard_ui_feed_activity();
    assert(!ecard_ui_is_screen_off());

    // 再次步进 10 秒 - 计时已被重置
    timed_out = ecard_ui_check_idle_timeout(10000);
    assert(!timed_out);
}

static void test_two_stage_power_policy(void) {
    ecard_pomodoro_reset();
    ecard_ui_reset_navigation();
    ecard_ui_show_page(ECARD_UI_PAGE_POMODORO);

    // 1. 番茄钟处于 IDLE（未开始）：1 分钟不暗屏，2 分钟熄屏
    assert(ecard_ui_poll_idle_state(60000) == ECARD_UI_IDLE_NONE);
    assert(!ecard_ui_is_screen_off());
    assert(ecard_ui_poll_idle_state(60000) == ECARD_UI_IDLE_OFF);
    assert(ecard_ui_is_screen_off());
    assert(ecard_ui_poll_idle_state(30000) == ECARD_UI_IDLE_NONE);

    // 喂狗恢复亮屏
    ecard_ui_feed_activity();
    assert(!ecard_ui_is_screen_off());

    // 2. 番茄钟专注中：1 分钟后暗屏省电（DIM），之后不熄屏（OFF）
    ecard_pomodoro_start();
    assert(ecard_ui_poll_idle_state(59000) == ECARD_UI_IDLE_NONE);
    assert(!ecard_ui_is_dimmed());
    assert(ecard_ui_poll_idle_state(1000) == ECARD_UI_IDLE_DIM);
    assert(ecard_ui_is_dimmed());
    assert(!ecard_ui_is_screen_off());
    assert(ecard_ui_poll_idle_state(120000) == ECARD_UI_IDLE_NONE);
    assert(ecard_ui_is_dimmed());
    assert(!ecard_ui_is_screen_off());

    // 2b. 暂停番茄钟后：恢复 2 分钟无操作自动熄屏规则
    ecard_pomodoro_pause();
    ecard_ui_feed_activity();
    assert(!ecard_ui_is_dimmed());
    assert(ecard_ui_poll_idle_state(60000) == ECARD_UI_IDLE_NONE);
    assert(ecard_ui_poll_idle_state(60000) == ECARD_UI_IDLE_OFF);
    assert(ecard_ui_is_screen_off());

    // 3. 切出番茄钟页后再经过 2 分钟应熄屏
    ecard_pomodoro_stop();
    ecard_ui_show_page(ECARD_UI_PAGE_HOME);
    ecard_ui_feed_activity();
    assert(ecard_ui_poll_idle_state(60000) == ECARD_UI_IDLE_NONE);
    assert(ecard_ui_poll_idle_state(60000) == ECARD_UI_IDLE_OFF);
    assert(ecard_ui_is_screen_off());
}

static void test_idle_timeout_paused_by_modal(void) {
    ecard_pomodoro_reset();
    ecard_ui_reset_navigation();
    ecard_ui_show_recording_modal(30, 40);

    // 模态层存在时不得熄屏，且计时被清零
    bool timed_out = ecard_ui_check_idle_timeout(130000);
    assert(!timed_out);
    assert(!ecard_ui_is_screen_off());

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
    assert(!ecard_cat_room_is_pairing_active());

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
    assert(!ecard_cat_room_is_pairing_active());
    ecard_ui_show_page(ECARD_UI_PAGE_PAIR);
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_PAIR);
    assert(ecard_cat_room_is_pairing_active());

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
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_PAIR);
    assert(ecard_cat_room_is_pairing_active());
    ecard_ui_hide_modal();

    ecard_ui_reset_navigation();
    assert(!ecard_cat_room_is_bound());
    assert(!ecard_cat_room_is_pairing_active());
    assert(ecard_ui_get_page() == ECARD_UI_PAGE_HOME);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
}

static void test_pomodoro_points_and_redeem_copy(void) {
    ecard_cat_room_reset_all();
    char pts[32];
    ecard_ui_format_pomodoro_points(pts, sizeof(pts));
    assert(strcmp(pts, "可用积分：0") == 0);

    (void)ecard_cat_room_award_focus_minutes(25);
    ecard_ui_format_pomodoro_points(pts, sizeof(pts));
    assert(strcmp(pts, "可用积分：25") == 0);

    for (int i = 0; i < 200; ++i) {
        (void)ecard_cat_room_award_focus_minutes(50);
    }
    ecard_ui_format_pomodoro_points(pts, sizeof(pts));
    assert(strcmp(pts, "可用积分：9999") == 0);

    const char *rem = ecard_ui_pomodoro_redeem_reminder();
    assert(rem != NULL);
    assert(strcmp(rem, "攒够积分找家长\n小程序里兑奖励") == 0);
    ecard_cat_room_reset_all();
}

int main(void) {
    printf("Running ecard_ui tests...\n");
    test_idle_timeout_2min();
    test_two_stage_power_policy();
    test_idle_timeout_paused_by_modal();
    test_modal_state_machine();
    test_page_navigation();
    test_pomodoro_points_and_redeem_copy();
    printf("ecard_ui tests passed!\n");
    return 0;
}
