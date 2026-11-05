#include "ecard_profile.h"
#include "ecard_ui.h"
#include <assert.h>
#include <stdio.h>

static void test_idle_timeout_2min(void) {
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

static void test_modal_state_machine(void) {
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);

    // 触发录音模态 (30秒倒计时)
    ecard_ui_show_recording_modal(30, 45);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_RECORDING);

    // 更新倒计时
    ecard_ui_show_recording_modal(15, 60);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_RECORDING);

    // 关闭模态
    ecard_ui_hide_modal();
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);

    // 触发播放模态
    ecard_ui_show_playing_modal(5, 12);
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_PLAYING);

    ecard_ui_hide_modal();
    assert(ecard_ui_get_modal() == ECARD_UI_MODAL_NONE);
}

int main(void) {
    printf("Running ecard_ui tests...\n");
    test_idle_timeout_2min();
    test_modal_state_machine();
    printf("ecard_ui tests passed!\n");
    return 0;
}
