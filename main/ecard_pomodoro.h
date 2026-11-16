#ifndef ECARD_POMODORO_H
#define ECARD_POMODORO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 默认专注时长（分钟）与休息时长（分钟）。 */
#define ECARD_POMODORO_FOCUS_MIN 25
/** 专注时长可选范围与步长：5..50 分钟，每次 ±5。 */
#define ECARD_POMODORO_FOCUS_MIN_ALLOWED 5
#define ECARD_POMODORO_FOCUS_MAX_ALLOWED 50
#define ECARD_POMODORO_FOCUS_STEP 5
#define ECARD_POMODORO_BREAK_MIN 5
#define ECARD_POMODORO_LONG_BREAK_MIN 15
#define ECARD_POMODORO_CYCLES_BEFORE_LONG 4

typedef enum {
    ECARD_POMODORO_IDLE = 0,  // 未开始
    ECARD_POMODORO_FOCUS,     // 专注中
    ECARD_POMODORO_BREAK,     // 短休息
    ECARD_POMODORO_LONG_BREAK,// 长休息
    ECARD_POMODORO_DONE       // 休息结束，等待开始下一轮
} ecard_pomodoro_phase_t;

/** 回到未开始状态，清空完成的专注轮数。 */
void ecard_pomodoro_reset(void);

/** 开始专注（从未开始或休息结束后调用）；已在计时时无动作。 */
void ecard_pomodoro_start(void);

/** 暂停当前倒计时。 */
void ecard_pomodoro_pause(void);

/** 继续已暂停的倒计时。 */
void ecard_pomodoro_resume(void);

/** 中止本轮，回到未开始状态（保留已完成轮数）。 */
void ecard_pomodoro_stop(void);

/**
 * @brief 推进时钟。
 *
 * 专注结束自动进入休息，休息结束停在 DONE，等用户按开始。
 */
void ecard_pomodoro_tick(uint32_t delta_ms);

ecard_pomodoro_phase_t ecard_pomodoro_phase(void);

bool ecard_pomodoro_is_running(void);

/** 当前阶段剩余毫秒数。 */
uint32_t ecard_pomodoro_remaining_ms(void);

/** 当前阶段剩余秒数（向上取整，用于界面倒计时）。 */
uint32_t ecard_pomodoro_remaining_sec(void);

/** 当前选择的专注时长（分钟）。 */
uint8_t ecard_pomodoro_focus_minutes(void);

/** 设置专注时长：自动夹到 5..50 分钟；计时中调用会被忽略。 */
void ecard_pomodoro_set_focus_minutes(uint8_t minutes);

/** 已完成的专注轮数。 */
uint8_t ecard_pomodoro_completed(void);

/** 当前阶段总时长（毫秒），用于进度环。 */
uint32_t ecard_pomodoro_phase_total_ms(void);

/** 取出并清零最近一次自然完成 FOCUS 轮次的分钟数；未完成或被中止返回 0。 */
uint8_t ecard_pomodoro_take_completed_focus_minutes(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_POMODORO_H
