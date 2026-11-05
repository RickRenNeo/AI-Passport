#ifndef ECARD_UI_H
#define ECARD_UI_H

#include "ecard_profile.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ECARD_UI_MODAL_NONE = 0,
    ECARD_UI_MODAL_RECORDING, // 正在录制留言 (最长30秒倒计时)
    ECARD_UI_MODAL_PLAYING,   // 正在播放未读留言
    ECARD_UI_MODAL_SOS        // SOS 报警状态
} ecard_ui_modal_t;

/**
 * @brief 初始化并创建 eCard 主界面 (240x320 竖屏)
 * @note 必须在持有 bsp_lvgl_lock() 条件下调用
 */
void ecard_ui_create(void);

/**
 * @brief 销毁 eCard 主界面对象
 */
void ecard_ui_destroy(void);

/**
 * @brief 刷新顶栏状态信息 (时间、电池百分比、WiFi状态)
 *
 * @param time_str 时间字符串 (如 "10:15")
 * @param battery_pct 电池电量 (0..100)
 * @param is_charging 是否充电中
 * @param wifi_connected WiFi是否已连通
 */
void ecard_ui_update_status(const char *time_str, uint8_t battery_pct, bool is_charging, bool wifi_connected);

/**
 * @brief 刷新下方互斥卡片状态 (有新消息显示高亮卡片，无消息显示姓名班级胶囊)
 */
void ecard_ui_refresh_content(void);

/**
 * @brief 打开或更新录音模态弹窗
 *
 * @param remaining_sec 倒计时剩余秒数 (0..30)
 * @param audio_amplitude 实时音频振幅 (0..100 用于麦克风音浪动效)
 */
void ecard_ui_show_recording_modal(uint8_t remaining_sec, uint8_t audio_amplitude);

/**
 * @brief 打开或更新播放模态弹窗
 *
 * @param current_sec 当前播放进度秒数
 * @param total_sec 总时长秒数
 */
void ecard_ui_show_playing_modal(uint8_t current_sec, uint8_t total_sec);

/**
 * @brief 关闭当前活动的模态弹窗
 */
void ecard_ui_hide_modal(void);

/**
 * @brief 获取当前模态状态
 */
ecard_ui_modal_t ecard_ui_get_modal(void);

/**
 * @brief 喂狗重置 2 分钟无操作计时 (按键触发唤醒与重置)
 */
void ecard_ui_feed_activity(void);

/**
 * @brief 检查是否超过 2 分钟无操作需要熄屏/降低背光
 * @param delta_ms 步进毫秒
 * @return true 表示达到 2 分钟应进入浅休眠
 */
bool ecard_ui_check_idle_timeout(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif // ECARD_UI_H
