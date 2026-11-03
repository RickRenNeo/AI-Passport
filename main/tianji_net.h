// main/tianji_net.h — 《天机·六爻》网络连接、oMLX 大模型与语音解卦网关接口
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
    TIANJI_NET_STATE_INFERENCING,    // 正在向服务端发送请求并等待处理
    TIANJI_NET_STATE_SUCCESS,        // 处理成功完成
    TIANJI_NET_STATE_INFER_ERROR,    // 处理出错（超时、网络错误等）
} tianji_net_state_t;

// 语音交互业务状态
typedef enum {
    TIANJI_VOICE_STATE_IDLE = 0,
    TIANJI_VOICE_STATE_RECORDING,    // 正在录音倾听
    TIANJI_VOICE_STATE_UPLOADING,    // 录音结束，正在上传与等待神思审度
    TIANJI_VOICE_STATE_SUCCESS,      // 解卦成功
    TIANJI_VOICE_STATE_NEED_MORE,    // 意图不足/需要更明确的求测事项
    TIANJI_VOICE_STATE_PLAYING,      // 正在播放宗师语音
    TIANJI_VOICE_STATE_ERROR,        // 语音交互异常
} tianji_voice_state_t;

// 异步回调函数类型
typedef void (*tianji_net_callback_t)(tianji_net_state_t state, const char *message);
typedef void (*tianji_voice_callback_t)(tianji_voice_state_t state, const char *message);

// 初始化网络模块底座（NVS 与 netif）
esp_err_t tianji_net_init(void);

// 开启或关闭联机模式（长按 OK 键触发）
void tianji_net_set_enabled(bool enable);

// 获取当前是否处于联机模式
bool tianji_net_is_enabled(void);

// 获取当前 Wi-Fi 是否已成功获取 IP 连接就绪
bool tianji_net_is_connected(void);

// 获取当前网络状态
tianji_net_state_t tianji_net_get_state(void);

// 获取顶部状态栏显示的中文标签（如“离线”、“已联机”、“连接中”、“未联网”）
const char *tianji_net_get_status_label(void);

// 发起原生 oMLX AI 深度解卦请求（文本模式备用）
esp_err_t tianji_net_request_ai(const tianji_session_t *session, tianji_net_callback_t cb);

// 获取已生成的 AI 解断结果文本
const char *tianji_net_get_ai_result(void);

// ============================================================================
// 语音解卦网关接口 (SenseVoice + MiniCPM5 + EdgeTTS)
// ============================================================================

// 启动语音解卦流程（录音采集 -> WAV 上传 -> 流式播放）
esp_err_t tianji_voice_start(const tianji_session_t *session, tianji_voice_callback_t cb);

// 用户手动结束说话（再次按下确定键）
void tianji_voice_stop_recording(void);

// 停止宗师语音播放（静音打断，用户按确定键或下键）
void tianji_voice_stop_playing(void);

// 查询语音系统状态
tianji_voice_state_t tianji_voice_get_state(void);
bool tianji_voice_is_recording(void);
bool tianji_voice_is_playing(void);

// 获取当前录音音量等级 (0..100)
int tianji_voice_get_current_volume(void);

// 获取服务端返回的文本批断与音频下载地址
const char *tianji_voice_get_text_reply(void);
const char *tianji_voice_get_audio_url(void);

#ifdef __cplusplus
}
#endif

