// main/tianji_ui.h — 天机·六爻: 极简现代科技风 UI 界面系统
#pragma once

#include "lvgl.h"
#include "tianji_logic.h"
#include "tianji_net.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 字体声明 (支持完整常用汉字及64卦经文)
LV_FONT_DECLARE(tianji_font_16);

// UI 当前所处页面状态
typedef enum {
    TIANJI_VIEW_STANDBY = 0,    // 待命 / 问事准备页
    TIANJI_VIEW_CASTING,        // 逐次掷爻推演页
    TIANJI_VIEW_OVERVIEW,       // 双盘全息对比总览页
    TIANJI_VIEW_READING,        // 离线断语与经文详情页
    TIANJI_VIEW_AI,             // 联网 AI 推演页（文字呈现与宗师语音播放）
    TIANJI_VIEW_VOICE_LISTENING,// 【天机倾听中...】语音录制与上传审度页
    TIANJI_VIEW_VOICE_RETRY,    // 【意念未明】意图不足引导提示页
} tianji_view_t;

// 按键输入动作枚举 (供主控调度输入)
typedef enum {
    TIANJI_KEY_UP_CLICK = 0,
    TIANJI_KEY_DOWN_CLICK,
    TIANJI_KEY_OK_CLICK,
    TIANJI_KEY_OK_LONG,
} tianji_key_action_t;

// 初始化 UI 系统 (必须在 bsp_lvgl_init 之后调用)
void tianji_ui_init(void);

// 销毁并释放 UI 资源
void tianji_ui_destroy(void);

// 处理实体按键事件 (必须在持有 bsp_lvgl_lock 的环境下调用)
void tianji_ui_handle_key(tianji_key_action_t action);

// 更新电池电量显示 (0..100%, -1 为不可用)
void tianji_ui_update_battery(int soc);

// 更新网络状态显示
void tianji_ui_update_net_state(tianji_net_state_t state);

// 获取当前 UI 状态
tianji_view_t tianji_ui_get_current_view(void);

#ifdef __cplusplus
}
#endif
