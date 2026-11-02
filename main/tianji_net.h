// main/tianji_net.h — 《天机·六爻》网络连接与 oMLX 大模型请求驱动
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "tianji_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    TIANJI_NET_STATE_DISABLED = 0,   // 离线模式（WiFi栈关闭，省电省内存）
    TIANJI_NET_STATE_CONNECTING,     // 正在尝试连接手机热点
    TIANJI_NET_STATE_CONNECTED,      // 已连接热点并取得 IP
    TIANJI_NET_STATE_FAILED,         // 热点连接失败/无信号
    TIANJI_NET_STATE_INFERENCING,    // 正在向 oMLX (MiniCPM 5) 发送请求并等待推理
    TIANJI_NET_STATE_SUCCESS,        // 大模型推理成功完成
    TIANJI_NET_STATE_INFER_ERROR,    // 大模型请求出错（超时、401鉴权失败或网络不可达）
} tianji_net_state_t;

// 异步回调函数类型
typedef void (*tianji_net_callback_t)(tianji_net_state_t state, const char *message);

// 初始化网络模块底座（NVS 与 netif）
esp_err_t tianji_net_init(void);

// 开启或关闭联机模式（长按 OK 键触发）
void tianji_net_set_enabled(bool enable);

// 获取当前是否处于联机模式
bool tianji_net_is_enabled(void);

// 获取当前网络状态
tianji_net_state_t tianji_net_get_state(void);

// 获取顶部状态栏显示的中文标签（如“离线”、“已联机”、“连接中”、“未联网”）
const char *tianji_net_get_status_label(void);

// 发起 oMLX AI 深度解卦请求
esp_err_t tianji_net_request_ai(const tianji_session_t *session, tianji_net_callback_t cb);

// 获取已生成的 AI 解断结果文本
const char *tianji_net_get_ai_result(void);

#ifdef __cplusplus
}
#endif
