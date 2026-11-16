#ifndef ECARD_UI_H
#define ECARD_UI_H

#include "ecard_messages.h"
#include "ecard_profile.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief eCard 顶层页面
 *
 * 所有页面按需创建、切换时销毁旧页面，避免在 24KB 的 LVGL 内存池中同时驻留多套界面。
 */
typedef enum {
    ECARD_UI_PAGE_HOME = 0,  // 电子工牌首页
    ECARD_UI_PAGE_MENU,      // 快捷菜单
    ECARD_UI_PAGE_MESSAGES,  // 语音留言
    ECARD_UI_PAGE_QUIZ,      // 随堂答题
    ECARD_UI_PAGE_POEM_PICK, // 古诗：选择年级
    ECARD_UI_PAGE_WORD_PICK, // 单词：选择年级
    ECARD_UI_PAGE_POEM,      // 古诗接句闪卡
    ECARD_UI_PAGE_WORD,      // 英语单词闪卡
    ECARD_UI_PAGE_POMODORO,  // 番茄钟
    ECARD_UI_PAGE_SETUP,     // 首次配网引导
    ECARD_UI_PAGE_RESET,     // 设备重置确认
    ECARD_UI_PAGE_DEVICE,    // 设备信息
    ECARD_UI_PAGE_PAIR       // 家长扫码配对
} ecard_ui_page_t;

/**
 * @brief 覆盖在页面之上的全屏交互层
 */
typedef enum {
    ECARD_UI_MODAL_NONE = 0,
    ECARD_UI_MODAL_RECORDING,   // 正在录音
    ECARD_UI_MODAL_PLAYING,     // 正在播放留言
    ECARD_UI_MODAL_SOS_PENDING, // 紧急求助倒计时（可松开取消）
    ECARD_UI_MODAL_SOS_SENT     // 求助已发出
} ecard_ui_modal_t;

/**
 * @brief 快捷菜单条目
 */
typedef enum {
    ECARD_MENU_VOICE = 0,
    ECARD_MENU_QUIZ,
    ECARD_MENU_POEM,
    ECARD_MENU_WORD,
    ECARD_MENU_POMODORO,
    ECARD_MENU_DEVICE,
    ECARD_MENU_COUNT
} ecard_menu_item_t;

/**
 * @brief 创建首页。必须在持有 bsp_lvgl_lock() 的条件下调用。
 */
void ecard_ui_create(void);

/**
 * @brief 销毁当前页面与模态层。
 */
void ecard_ui_destroy(void);

/**
 * @brief 仅重置导航/模态/空闲计数等逻辑状态，不触摸 LVGL 对象。
 *
 * 供应用层重新初始化时使用；设备启动时应在 ecard_ui_create() 之前调用。
 */
void ecard_ui_reset_navigation(void);

/**
 * @brief 切换到指定页面。必须在持有 bsp_lvgl_lock() 的条件下调用。
 */
void ecard_ui_show_page(ecard_ui_page_t page);

/**
 * @brief 当前页面。
 */
ecard_ui_page_t ecard_ui_get_page(void);

/**
 * @brief 首次配网页按“等待热点 → 连接中 → 填写身份 → 完成”自动换页。
 *
 * 必须在持有 bsp_lvgl_lock() 的条件下调用；状态未变化时不做任何事。
 */
void ecard_ui_setup_refresh(void);

/**
 * @brief 在快捷菜单中移动选中项（delta 为 +1/-1，循环）。
 */
void ecard_ui_menu_move(int delta);

/**
 * @brief 当前快捷菜单选中项（0..ECARD_MENU_COUNT-1）。
 */
uint8_t ecard_ui_menu_selection(void);

/**
 * @brief 语音留言列表当前选中项（0 = 最新一条）。
 */
uint8_t ecard_ui_messages_selection(void);

/**
 * @brief 在语音留言列表中上下移动选中项（循环）。
 *
 * 必须在持有 bsp_lvgl_lock() 的条件下调用。
 */
void ecard_ui_messages_move(int delta);

/**
 * @brief 刷新顶栏时间、电量与网络状态。
 *
 * @param time_str    形如 "09:40" 的时间字符串，可为 NULL
 * @param battery_pct 0..100；传入负数表示电量不可用，界面显示 "--"
 * @param is_charging 是否正在充电
 * @param wifi_connected 校园网是否已连接
 */
void ecard_ui_update_status(const char *time_str, int battery_pct, bool is_charging, bool wifi_connected);

/**
 * @brief 状态栏小智 AI 指示灯：已连上会话时点为绿色，未连接为灰色。
 *
 * 必须在持有 bsp_lvgl_lock() 的条件下调用。
 */
void ecard_ui_set_ai_connected(bool connected);

/**
 * @brief 刷新未读留言/身份信息等动态内容。
 */
void ecard_ui_refresh_content(void);

/**
 * @brief 设备页“长按 UP 重试连接”状态：为 true 时提示正在重连。
 *
 * 必须在持有 bsp_lvgl_lock() 的条件下调用。
 */
void ecard_ui_set_network_retrying(bool retrying);

/**
 * @brief 按 ecard_quiz 当前状态重绘答题页（题号、选项高亮、判题反馈与结算）。
 *
 * 必须在持有 bsp_lvgl_lock() 的条件下调用。
 */
void ecard_ui_quiz_refresh(void);

/**
 * @brief 按 ecard_flashcard 当前状态重绘闪卡页（卡面、释义、进度与结算）。
 *
 * 必须在持有 bsp_lvgl_lock() 的条件下调用。
 */
void ecard_ui_flashcard_refresh(void);

/**
 * @brief 设置年级选择页的当前高亮项（0..ECARD_GRADE_COUNT-1）。
 */
uint8_t ecard_ui_grade_selection(void);

/** 在年级选择页上下移动（delta 为 +1/-1，循环）。 */
void ecard_ui_grade_move(int delta);

/** 闪卡页显示用：进入卡组前记录本次要展示的年级（1..6）。 */
void ecard_ui_set_deck_grade(uint8_t grade);

/**
 * @brief 按 ecard_pomodoro 当前状态重绘番茄钟页面。
 *
 * 必须在持有 bsp_lvgl_lock() 的条件下调用。
 */
void ecard_ui_pomodoro_refresh(void);

/**
 * @brief 刷新家长扫码配对页（配对码、绑定状态与局域网 IP）。
 *
 * 必须在持有 bsp_lvgl_lock() 的条件下调用。
 */
void ecard_ui_pair_refresh(void);

/**
 * @brief 显示或更新录音界面。
 *
 * @param remaining_sec 剩余秒数（0..30）
 * @param audio_amplitude 实时振幅（0..100），用于驱动波形
 */
void ecard_ui_show_recording_modal(uint8_t remaining_sec, uint8_t audio_amplitude);

/**
 * @brief 显示或更新小智追问录音界面（松开 UP 发送）。
 *
 * 与普通留言录音复用同一套浮层，但文案明确提示“小智聆听中”，
 * 让学生知道长按 UP 已经生效，而不是没有反应。
 *
 * @param remaining_sec 剩余秒数（0..10）
 * @param audio_amplitude 实时振幅（0..100），无采样时传 0
 */
void ecard_ui_show_ai_listen_modal(uint8_t remaining_sec, uint8_t audio_amplitude);

/**
 * @brief 修改当前页面底部提示；页面没有提示条时忽略。
 */
void ecard_ui_set_hint(const char *text);
void ecard_ui_set_hint_persistent(const char *text);
void ecard_ui_clear_pomodoro_hint(void);
const char *ecard_ui_pomodoro_override_hint(void);

/**
 * @brief 设置当前播放弹窗展示的留言元数据（发送方、角色与语音转写文本）。
 */
void ecard_ui_set_playing_message(const ecard_message_t *msg);

/**
 * @brief 获取当前正在播放弹窗绑定的留言元数据（无数据时返回 NULL）。
 */
const ecard_message_t *ecard_ui_playing_message(void);

/**
 * @brief 显示或更新播放界面。
 */
void ecard_ui_show_playing_modal(uint8_t current_sec, uint8_t total_sec);

/**
 * @brief 显示或更新紧急求助倒计时。
 *
 * @param seconds_left 剩余秒数（0..3）
 */
void ecard_ui_show_sos_pending(uint8_t seconds_left);

/**
 * @brief 切换为“求助已发出”界面。
 */
void ecard_ui_show_sos_sent(void);

/**
 * @brief 关闭当前模态层。
 */
void ecard_ui_hide_modal(void);

/**
 * @brief 当前模态层类型。
 */
ecard_ui_modal_t ecard_ui_get_modal(void);

/**
 * @brief 喂狗重置无操作计时（按键触发唤醒与重置）。
 */
void ecard_ui_feed_activity(void);

typedef enum {
    ECARD_UI_IDLE_NONE = 0,
    ECARD_UI_IDLE_DIM, /* 番茄钟专注/休息中 1 分钟无操作：暗屏省电（不熄屏） */
    ECARD_UI_IDLE_OFF  /* 其他页面/状态 2 分钟无操作：熄屏 */
} ecard_ui_idle_action_t;

/**
 * @brief 轮询两段式无操作电源动作，仅在状态跃迁边沿触发一次。
 */
ecard_ui_idle_action_t ecard_ui_poll_idle_state(uint32_t delta_ms);

/**
 * @brief 当前是否处于 1 分钟暗屏省电状态。
 */
bool ecard_ui_is_dimmed(void);

/**
 * @brief 当前是否处于 2 分钟无操作熄屏状态。
 */
bool ecard_ui_is_screen_off(void);

/**
 * @brief 兼容旧接口：等价于 ecard_ui_poll_idle_state(delta_ms) == ECARD_UI_IDLE_OFF。
 */
bool ecard_ui_check_idle_timeout(uint32_t delta_ms);

#ifdef __cplusplus
}
#endif

#endif // ECARD_UI_H
