#ifndef ECARD_APP_H
#define ECARD_APP_H

#include "bsp_button.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ECARD_APP_STATE_IDLE = 0,
    ECARD_APP_STATE_RECORDING,
    ECARD_APP_STATE_PLAYING,
    ECARD_APP_STATE_SOS_PENDING, // DOWN 长按后的 3 秒确认窗口
    ECARD_APP_STATE_SOS_SENT     // 求助已发送，等待用户确认返回首页
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
 * @brief 注入一帧真实麦克风 PCM（16 kHz 单声道，建议 20ms = 320 样本）
 *
 * 由设备层音频任务在持有 bsp_lvgl_lock() 时调用；录音状态外调用会被忽略。
 */
void ecard_app_feed_mic_pcm(const int16_t *pcm, size_t samples);

/**
 * @brief 取出回放 PCM（16 kHz 单声道），驱动扬声器
 *
 * 由设备层音频任务在持有 bsp_lvgl_lock() 时调用。
 * @return 实际样本数；0 表示当前没有可回放音频或回放已结束
 */
size_t ecard_app_render_playback_pcm(int16_t *out, size_t max_samples);

/** 当前是否处于“小智追问”录音（仅编码上传，不写本地缓冲）。 */
bool ecard_app_ai_listening(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_APP_H
