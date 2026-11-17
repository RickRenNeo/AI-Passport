#include "ecard_pomodoro.h"

static ecard_pomodoro_phase_t s_phase = ECARD_POMODORO_IDLE;
static bool s_running;
static bool s_paused;
static uint32_t s_remaining_ms;
static uint32_t s_phase_total_ms;
static uint8_t s_completed;
static uint8_t s_focus_min = ECARD_POMODORO_FOCUS_MIN;
static uint8_t s_pending_completed_focus_min;

static uint32_t phase_ms(ecard_pomodoro_phase_t phase) {
    switch (phase) {
    case ECARD_POMODORO_FOCUS:
        return (uint32_t)s_focus_min * 60u * 1000u;
    case ECARD_POMODORO_BREAK:
        return (uint32_t)ECARD_POMODORO_BREAK_MIN * 60u * 1000u;
    case ECARD_POMODORO_LONG_BREAK:
        return (uint32_t)ECARD_POMODORO_LONG_BREAK_MIN * 60u * 1000u;
    default:
        return 0;
    }
}

static void enter_phase(ecard_pomodoro_phase_t phase) {
    s_phase = phase;
    s_phase_total_ms = phase_ms(phase);
    s_remaining_ms = s_phase_total_ms;
    s_running = (phase == ECARD_POMODORO_FOCUS || phase == ECARD_POMODORO_BREAK ||
                 phase == ECARD_POMODORO_LONG_BREAK);
    s_paused = false;
}

void ecard_pomodoro_reset(void) {
    s_phase = ECARD_POMODORO_IDLE;
    s_running = false;
    s_paused = false;
    s_remaining_ms = 0;
    s_phase_total_ms = phase_ms(ECARD_POMODORO_FOCUS);
    s_completed = 0;
    s_pending_completed_focus_min = 0;
}

void ecard_pomodoro_start(void) {
    if (s_phase == ECARD_POMODORO_FOCUS || s_phase == ECARD_POMODORO_BREAK ||
        s_phase == ECARD_POMODORO_LONG_BREAK) {
        return;
    }
    enter_phase(ECARD_POMODORO_FOCUS);
}

void ecard_pomodoro_pause(void) {
    if (!s_running || s_paused) return;
    s_paused = true;
}

void ecard_pomodoro_resume(void) {
    if (!s_running || !s_paused) return;
    s_paused = false;
}

void ecard_pomodoro_stop(void) {
    s_running = false;
    s_paused = false;
    s_phase = ECARD_POMODORO_IDLE;
    s_remaining_ms = 0;
    s_phase_total_ms = phase_ms(ECARD_POMODORO_FOCUS);
    s_pending_completed_focus_min = 0;
}

void ecard_pomodoro_tick(uint32_t delta_ms) {
    if (!s_running || s_paused) return;

    for (;;) {
        if (s_remaining_ms == 0) {
            if (s_phase == ECARD_POMODORO_FOCUS) {
                ++s_completed;
                s_pending_completed_focus_min = s_focus_min;
                if (s_completed % ECARD_POMODORO_CYCLES_BEFORE_LONG == 0) {
                    enter_phase(ECARD_POMODORO_LONG_BREAK);
                } else {
                    enter_phase(ECARD_POMODORO_BREAK);
                }
                continue;
            }
            /* 休息结束：停在 DONE，等用户确认后再开始下一轮。 */
            s_phase = ECARD_POMODORO_DONE;
            s_running = false;
            s_paused = false;
            s_remaining_ms = 0;
            s_phase_total_ms = 0;
            return;
        }

        if (delta_ms == 0) return;

        const uint32_t step = (delta_ms < s_remaining_ms) ? delta_ms : s_remaining_ms;
        s_remaining_ms -= step;
        delta_ms -= step;
    }
}

ecard_pomodoro_phase_t ecard_pomodoro_phase(void) {
    return s_phase;
}

bool ecard_pomodoro_is_running(void) {
    return s_running && !s_paused;
}

uint32_t ecard_pomodoro_remaining_ms(void) {
    return s_remaining_ms;
}

uint32_t ecard_pomodoro_remaining_sec(void) {
    return (s_remaining_ms + 999u) / 1000u;
}

uint8_t ecard_pomodoro_focus_minutes(void) {
    return s_focus_min;
}

void ecard_pomodoro_set_focus_minutes(uint8_t minutes) {
    // 计时中改时长会让进度环和剩余时间失真，直接忽略。
    if (s_running) return;
    if (s_phase == ECARD_POMODORO_FOCUS || s_phase == ECARD_POMODORO_BREAK ||
        s_phase == ECARD_POMODORO_LONG_BREAK) {
        return;
    }
    if (minutes < ECARD_POMODORO_FOCUS_MIN_ALLOWED) {
        minutes = ECARD_POMODORO_FOCUS_MIN_ALLOWED;
    }
    if (minutes > ECARD_POMODORO_FOCUS_MAX_ALLOWED) {
        minutes = ECARD_POMODORO_FOCUS_MAX_ALLOWED;
    }
    /* 对齐到 5 分钟步长，避免出现 33 分钟这种奇怪值。 */
    minutes = (uint8_t)(((minutes + ECARD_POMODORO_FOCUS_STEP / 2) /
                         ECARD_POMODORO_FOCUS_STEP) * ECARD_POMODORO_FOCUS_STEP);
    if (minutes < ECARD_POMODORO_FOCUS_MIN_ALLOWED) {
        minutes = ECARD_POMODORO_FOCUS_MIN_ALLOWED;
    }
    s_focus_min = minutes;
    s_phase_total_ms = phase_ms(ECARD_POMODORO_FOCUS);
    s_remaining_ms = 0;
}

uint8_t ecard_pomodoro_completed(void) {
    return s_completed;
}

uint32_t ecard_pomodoro_phase_total_ms(void) {
    return s_phase_total_ms;
}

uint8_t ecard_pomodoro_take_completed_focus_minutes(void) {
    const uint8_t mins = s_pending_completed_focus_min;
    s_pending_completed_focus_min = 0;
    return mins;
}
