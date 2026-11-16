#include "ecard_pomodoro.h"
#include <assert.h>
#include <stdio.h>

static uint32_t focus_ms(void) { return ECARD_POMODORO_FOCUS_MIN * 60u * 1000u; }
static uint32_t selected_focus_ms(void) {
    return (uint32_t)ecard_pomodoro_focus_minutes() * 60u * 1000u;
}
static uint32_t break_ms(void) { return ECARD_POMODORO_BREAK_MIN * 60u * 1000u; }
static uint32_t long_break_ms(void) { return ECARD_POMODORO_LONG_BREAK_MIN * 60u * 1000u; }

static void test_initial_state(void) {
    ecard_pomodoro_reset();
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_IDLE);
    assert(!ecard_pomodoro_is_running());
    assert(ecard_pomodoro_completed() == 0);
    assert(ecard_pomodoro_remaining_ms() == 0);
    assert(ecard_pomodoro_phase_total_ms() == focus_ms());
}

static void test_start_and_countdown(void) {
    ecard_pomodoro_reset();
    ecard_pomodoro_start();
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_FOCUS);
    assert(ecard_pomodoro_is_running());
    assert(ecard_pomodoro_remaining_ms() == selected_focus_ms());
    assert(ecard_pomodoro_remaining_sec() == (uint32_t)ECARD_POMODORO_FOCUS_MIN * 60u);

    ecard_pomodoro_tick(60000);
    assert(ecard_pomodoro_remaining_sec() == (uint32_t)ECARD_POMODORO_FOCUS_MIN * 60u - 60u);

    // 重复开始不重置计时
    ecard_pomodoro_start();
    assert(ecard_pomodoro_remaining_sec() == (uint32_t)ECARD_POMODORO_FOCUS_MIN * 60u - 60u);
}

static void test_pause_resume(void) {
    ecard_pomodoro_reset();
    ecard_pomodoro_start();
    ecard_pomodoro_tick(30000);
    ecard_pomodoro_pause();
    assert(!ecard_pomodoro_is_running());
    const uint32_t frozen = ecard_pomodoro_remaining_ms();
    ecard_pomodoro_tick(60000);
    assert(ecard_pomodoro_remaining_ms() == frozen);

    ecard_pomodoro_resume();
    assert(ecard_pomodoro_is_running());
    ecard_pomodoro_tick(1000);
    assert(ecard_pomodoro_remaining_ms() == frozen - 1000);
}

static void test_focus_duration_selection(void) {
    ecard_pomodoro_reset();
    /* 默认 25 分钟，可调到 5..50，并按 5 分钟对齐。 */
    assert(ecard_pomodoro_focus_minutes() == ECARD_POMODORO_FOCUS_MIN);
    ecard_pomodoro_set_focus_minutes(50);
    assert(ecard_pomodoro_focus_minutes() == 50);
    assert(ecard_pomodoro_phase_total_ms() == (uint32_t)50 * 60u * 1000u);
    ecard_pomodoro_set_focus_minutes(60);
    assert(ecard_pomodoro_focus_minutes() == ECARD_POMODORO_FOCUS_MAX_ALLOWED);
    ecard_pomodoro_set_focus_minutes(1);
    assert(ecard_pomodoro_focus_minutes() == ECARD_POMODORO_FOCUS_MIN_ALLOWED);
    ecard_pomodoro_set_focus_minutes(33);
    assert(ecard_pomodoro_focus_minutes() == 35);

    /* 选好的时长立刻用于下一轮计时。 */
    ecard_pomodoro_set_focus_minutes(40);
    ecard_pomodoro_start();
    assert(ecard_pomodoro_remaining_ms() == (uint32_t)40 * 60u * 1000u);
    /* 计时中改时长无效，避免进度环与剩余时间错乱。 */
    ecard_pomodoro_set_focus_minutes(10);
    assert(ecard_pomodoro_focus_minutes() == 40);
    ecard_pomodoro_stop();
    assert(ecard_pomodoro_focus_minutes() == 40);
    /* 选择会保留到下一轮；测试收尾恢复默认，避免影响后续用例的假设。 */
    ecard_pomodoro_set_focus_minutes(ECARD_POMODORO_FOCUS_MIN);
    assert(ecard_pomodoro_focus_minutes() == ECARD_POMODORO_FOCUS_MIN);
}

static void test_focus_to_break(void) {
    ecard_pomodoro_reset();
    ecard_pomodoro_start();
    ecard_pomodoro_tick(focus_ms());
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_BREAK);
    assert(ecard_pomodoro_completed() == 1);
    assert(ecard_pomodoro_remaining_ms() == break_ms());
    assert(ecard_pomodoro_phase_total_ms() == break_ms());

    ecard_pomodoro_tick(break_ms());
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_DONE);
    assert(!ecard_pomodoro_is_running());
    assert(ecard_pomodoro_remaining_ms() == 0);
}

static void test_long_break_after_four(void) {
    ecard_pomodoro_reset();
    for (int round = 1; round <= 4; ++round) {
        ecard_pomodoro_start();
        assert(ecard_pomodoro_phase() == ECARD_POMODORO_FOCUS);
        ecard_pomodoro_tick(focus_ms());
        assert(ecard_pomodoro_completed() == round);
        if (round < 4) {
            assert(ecard_pomodoro_phase() == ECARD_POMODORO_BREAK);
            ecard_pomodoro_tick(break_ms());
            assert(ecard_pomodoro_phase() == ECARD_POMODORO_DONE);
        } else {
            assert(ecard_pomodoro_phase() == ECARD_POMODORO_LONG_BREAK);
            assert(ecard_pomodoro_remaining_ms() == long_break_ms());
        }
    }
}

static void test_overshoot_in_one_tick(void) {
    ecard_pomodoro_reset();
    ecard_pomodoro_start();
    // 一次 tick 越过专注结束点，应立刻进入休息并继续消耗剩余时间
    ecard_pomodoro_tick(focus_ms() + 120000);
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_BREAK);
    assert(ecard_pomodoro_completed() == 1);
    assert(ecard_pomodoro_remaining_ms() == break_ms() - 120000);
}

static void test_stop_keeps_completed(void) {
    ecard_pomodoro_reset();
    ecard_pomodoro_start();
    ecard_pomodoro_tick(focus_ms());
    ecard_pomodoro_tick(1000);
    assert(ecard_pomodoro_completed() == 1);
    ecard_pomodoro_stop();
    assert(ecard_pomodoro_phase() == ECARD_POMODORO_IDLE);
    assert(!ecard_pomodoro_is_running());
    assert(ecard_pomodoro_completed() == 1);
}

int main(void) {
    printf("Running ecard_pomodoro tests...\n");
    test_initial_state();
    test_start_and_countdown();
    test_focus_duration_selection();
    test_pause_resume();
    test_focus_to_break();
    test_long_break_after_four();
    test_overshoot_in_one_tick();
    test_stop_keeps_completed();
    printf("ecard_pomodoro tests passed!\n");
    return 0;
}
