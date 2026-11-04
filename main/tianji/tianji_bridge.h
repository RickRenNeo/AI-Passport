// main/tianji/tianji_bridge.h — 天机·六爻与小智 AI 底座交互桥接
#pragma once

#include "tianji_logic.h"
#include "tianji_ui.h"
#include "tianji_net.h"

#ifdef __cplusplus
extern "C" {
#endif

// 初始化天机·六爻桥接层（注册小智协议事件并创建天机 UI）
void tianji_bridge_init(void);

// 按键输入事件分发到六爻状态机
void tianji_bridge_handle_key(tianji_key_action_t action);

// 电池电量更新
void tianji_bridge_update_battery(int soc);

// 网络状态更新
void tianji_bridge_set_net_state(tianji_net_state_t state);

// 协议层回调注入：收到 STT 识别文本
void tianji_bridge_on_stt_text(const char *text);

// 协议层回调注入：收到 TTS 播报句子
void tianji_bridge_on_tts_sentence(const char *text);

// 协议层回调注入：TTS 开始播放
void tianji_bridge_on_tts_start(void);

// 协议层回调注入：TTS 播放停止
void tianji_bridge_on_tts_stop(void);

#ifdef __cplusplus
}
#endif
