#ifndef ECARD_APP_H
#define ECARD_APP_H

#include "bsp_button.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ECARD_APP_STATE_IDLE = 0,
    ECARD_APP_STATE_RECORDING,
    ECARD_APP_STATE_PLAYING,
    ECARD_APP_STATE_SOS
} ecard_app_state_t;

/**
 * @brief 初始化 eCard 电子学生证核心业务引擎
 */
void ecard_app_init(void);

/**
 * @brief 按键事件处理入口
 *
 * @param btn 按键类型 (UP, DOWN, OK)
 * @param ev 事件类型 (PRESS, RELEASE, CLICK, LONG_PRESS, DOUBLE_CLICK)
 */
void ecard_app_handle_key(bsp_btn_t btn, bsp_btn_ev_t ev);

/**
 * @brief 周期性心跳驱动 (建议每 1 秒调用一次)
 *
 * @param current_hour 当前小时 (0..23)
 * @param current_minute 当前分钟 (0..59)
 * @param delta_ms 距离上次调用的毫秒数 (如 1000)
 */
void ecard_app_tick(uint8_t current_hour, uint8_t current_minute, uint32_t delta_ms);

/**
 * @brief 获取当前 eCard 应用状态
 */
ecard_app_state_t ecard_app_get_state(void);

/**
 * @brief 模拟/注入录音流帧 (在无真实外设或单测环境下驱动倒计时)
 */
void ecard_app_simulate_audio_tick(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_APP_H
