#ifndef ECARD_XIAOZHI_H
#define ECARD_XIAOZHI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 官方 OTA/激活地址（小智官方流程）。 */
#define ECARD_XIAOZHI_OTA_URL "https://api.tenclass.net/xiaozhi/ota/"
#define ECARD_XIAOZHI_POLL_MS (30u * 1000u)

/** 初始化模块（不再自动联网；连接由 session_start 触发）。 */
void ecard_xiaozhi_start(void);

/** 按需建立小智会话：OTA 激活 + WebSocket。只允许明确 AI 入口调用。 */
void ecard_xiaozhi_session_start(void);

/** 结束小智会话并释放 TLS / Opus 内存。 */
void ecard_xiaozhi_session_stop(void);

/** 是否已拿到 WebSocket 地址与 token（激活完成）。 */
bool ecard_xiaozhi_is_ready(void);

/** 待用户在小智平台输入的激活码；未激活返回空串。 */
const char *ecard_xiaozhi_activation_code(void);

/** 当前 WebSocket 地址；未激活返回空串。 */
const char *ecard_xiaozhi_websocket_url(void);

/** WebSocket 是否已连接（hello 已发出）。 */
bool ecard_xiaozhi_ws_connected(void);

/** 是否已收到服务端 hello 的 session_id，可以安全开始 listen/detect。 */
bool ecard_xiaozhi_session_ready(void);

/** hello 看门狗请求重启会话；返回 true 后调用方应 stop + start。 */
bool ecard_xiaozhi_restart_pending(void);

/**
 * @brief 向小智提一个文本问题（答题讲解 / 古诗单词朗读）。
 *
 * 会话未就绪时先缓存，收到服务端 hello 后自动发出。
 */
void ecard_xiaozhi_ask(const char *text);

/**
 * @brief 提一个问题，并附带服务端拒绝长文本时自动重试的短提示词。
 *
 * 官方服务端对含中文的 detect 文本限制 16 个字符；纯英文可以稍长，
 * 但不同句子仍可能被判定为长文本。rich 被拒绝时自动改发 fallback。
 */
void ecard_xiaozhi_ask_with_fallback(const char *text, const char *fallback);

/**
 * @brief 提一个问题，并在本轮回答播完后自动追问第二句。
 *
 * 单词卡用它保证“单词+词义”和“例句”都送到小智：服务端对 detect 文本
 * 有长度上限（实测 30 字符内稳定，36 字符被判定为长文本），一次塞不下
 * 卡片全部内容，只能在两轮里分别发送。
 */
void ecard_xiaozhi_ask_with_follow(const char *text, const char *follow);

/** 开始一轮手动聆听（ASR 采音前调用）。失败返回 false。 */
bool ecard_xiaozhi_listen_start(void);

/** 本轮聆听是否至少上传过一帧音频；用于区分“已发送”和“没听到”。 */
bool ecard_xiaozhi_listen_had_audio(void);

/** 结束本轮聆听（采音结束后调用，服务端开始识别）。 */
void ecard_xiaozhi_listen_stop(void);

/** 发送一帧 Opus 音频（type 0 二进制帧）。 */
void ecard_xiaozhi_send_audio(const uint8_t *opus, size_t len);

/** 最近一次服务端识别文本；没有返回空串。 */
const char *ecard_xiaozhi_last_stt(void);

/** 最近一次 LLM 回复（流式块已拼接）；没有返回空串。 */
const char *ecard_xiaozhi_last_llm(void);

/** 最近一次事件类型（hello/stt/llm/tts/...）。 */
const char *ecard_xiaozhi_last_event(void);

/** 最近一次 TTS 状态（start/sentence_start/stop）。 */
const char *ecard_xiaozhi_last_tts_state(void);

/** 本轮 TTS 已收到的音频字节数（Opus 负载）。 */
int ecard_xiaozhi_tts_bytes(void);

/** 麦克风 PCM（16 kHz 单声道）流式送入 Opus 编码并上传（ASR 采音期间每帧调用）。 */
void ecard_xiaozhi_feed_mic_pcm(const int16_t *pcm, size_t samples);

/** 清空未凑满一帧的麦克风残留样本（开始新一轮录音时调用）。 */
void ecard_xiaozhi_reset_mic(void);

/** 服务端 TTS 是否正在播放或还有缓存音频。 */
bool ecard_xiaozhi_is_speaking(void);

/**
 * @brief 立即停止本地 TTS 播放、清空 PCM 缓冲，并请求服务端中止本轮回答。
 *
 * 学生切换题目或长按 UP 追问时调用：既避免旧回答继续出声，也避免下一轮
 * 提问时服务端仍把上一段 TTS 补完。
 */
void ecard_xiaozhi_reset_playback(void);

/** 取出服务端 TTS 解码后的 16 kHz PCM；0 表示当前没有可播放音频。 */
size_t ecard_xiaozhi_read_pcm(int16_t *out, size_t max_samples);

/** AI 会话期间限频打印音频/内存统计（应用 tick 每秒调用即可）。 */
void ecard_xiaozhi_log_stats(void);

#ifdef __cplusplus
}
#endif

#endif // ECARD_XIAOZHI_H
