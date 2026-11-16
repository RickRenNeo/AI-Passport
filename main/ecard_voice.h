#ifndef ECARD_VOICE_H
#define ECARD_VOICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECARD_VOICE_MAX_SEC       15   // 界面进度条上限
// 实际录音按需分配，最多 10 秒（40 KB）：超过会和小智 TLS/Opus 抢堆。
#define ECARD_VOICE_RECORD_SEC    10
// 采集、编码、回放全部统一 16 kHz，和 BSP 已验证的 I2S 时钟一致；
// 4-bit IMA-ADPCM 在 16 kHz 下是 64 kbps，频响 8 kHz。
#define ECARD_VOICE_SAMPLE_RATE   16000   // I2S 采集/回放采样率
// 本地存档降到 8 kHz：48 KB 内存即可录 10 秒以上，回放时再插值回 16 kHz。
#define ECARD_VOICE_STORE_RATE    8000
#define ECARD_VOICE_BYTES_PER_SEC 4000    // 8 kHz × 4-bit ADPCM
// 15 秒 × 8 KB/s = 120 KB。ESP32-C3 无 PSRAM，改为录音开始时从堆按需分配；
// 分配失败会逐级减半，实际容量以 ecard_voice_buffer_capacity() 为准。
#define ECARD_VOICE_MAX_BUFFER    (ECARD_VOICE_BYTES_PER_SEC * ECARD_VOICE_MAX_SEC)

typedef enum {
    ECARD_VOICE_IDLE = 0,
    ECARD_VOICE_RECORDING,
    ECARD_VOICE_PLAYING
} ecard_voice_state_t;

/**
 * @brief 初始化语音缓存与编码器状态，并释放上一次残留的录音缓冲
 */
void ecard_voice_init(void);

/**
 * @brief 开机预留录音缓冲（不受后续堆碎片影响）
 *
 * 建议在 app 初始化阶段、Wi-Fi 与界面启动之前调用。
 * 预留后再开始录音，容量固定为传入字节数。
 */
void ecard_voice_reserve(size_t bytes);

/** 释放预留缓冲（例如需要给 TLS/Opus 会话腾内存时）。 */
void ecard_voice_release_reserve(void);

/**
 * @brief 本次录音实际可用的 ADPCM 缓冲容量（字节）
 *
 * 堆不足时会降级到 ECARD_VOICE_MAX_BUFFER 的一半或四分之一，
 * 因此调用方不能用 ECARD_VOICE_MAX_BUFFER 假定实际时长。
 */
size_t ecard_voice_buffer_capacity(void);

/**
 * @brief 开始录制新留言（必要时优先复用空闲静态区并分段分配，规避连续大块堆碎片）
 * @return true 启动成功, false 处于忙碌状态或缓冲分配失败
 */
bool ecard_voice_start_recording(void);

/**
 * @brief 以指定单块上限分段申请录音缓冲（max_chunk_bytes=0 为默认策略，主要用于单测覆盖分段边界）
 */
bool ecard_voice_start_recording_chunked(size_t max_chunk_bytes);

/**
 * @brief 流式写入采集到的 16-bit 单声道 PCM 并转 ADPCM 存入缓存
 *
 * @param pcm_samples 输入 16 kHz PCM 数组
 * @param sample_count 输入样本数量
 * @param out_remaining_sec 输出当前剩余秒数 (0..30)
 * @param out_amplitude 输出当前音频平均振幅 (0..100)
 * @return true 写入成功, false 缓冲已满并自动停止
 */
bool ecard_voice_write_pcm(const int16_t *pcm_samples,
                           size_t sample_count,
                           uint8_t *out_remaining_sec,
                           uint8_t *out_amplitude);

/**
 * @brief 停止录制并封包，缓冲保留供本地回放或落盘
 *
 * @param out_adpcm_data 若缓冲连续则输出指针，分段分配时为 NULL（可配合 ecard_voice_save_to_file）
 * @param out_data_len 输出总字节数
 * @param out_duration_sec 输出实际录制时长 (秒)
 * @return true 录音有效且成功, false 无效或过短
 */
bool ecard_voice_stop_recording(const uint8_t **out_adpcm_data, size_t *out_data_len, uint8_t *out_duration_sec);

/**
 * @brief 将当前已录制的分段 ADPCM 数据顺序写入文件（无需连续大块拷贝）
 */
bool ecard_voice_save_to_file(const char *path, size_t *out_bytes);

/**
 * @brief 获取当前语音工作状态
 */
ecard_voice_state_t ecard_voice_get_state(void);

/**
 * @brief 重置录音缓存
 */
void ecard_voice_reset(void);

/**
 * @brief 是否存在一段可本地回放的录音
 */
bool ecard_voice_has_clip(void);

/**
 * @brief 开始回放最近一段录音
 * @return true 已进入回放状态, false 没有可用录音
 */
bool ecard_voice_playback_start(void);

/**
 * @brief 回放内置演示留言（网络留言没有音频负载时使用）
 * @return true 已进入回放状态
 */
bool ecard_voice_playback_start_demo(void);

/**
 * @brief 从本地持久化文件（如 SPIFFS 的 .adp）流式解码回放 ADPCM 语音
 *
 * @param path      ADPCM 文件路径
 * @param expand_8k true 表示 8 kHz 存档（回放时插值到 16 kHz），false 表示原生 16 kHz
 * @return true 成功打开并进入回放状态
 */
bool ecard_voice_playback_start_file(const char *path, bool expand_8k);

/** 内置演示留言的时长（秒），用于匹配留言卡片与播放进度。 */
uint8_t ecard_voice_demo_duration_sec(void);

/**
 * @brief 取出回放 PCM（16 kHz 单声道，一个 ADPCM 半字节对应一个样本）
 *
 * @param pcm_out 输出缓冲
 * @param max_samples 最多写入的 16 kHz 样本数
 * @return 实际写入样本数；0 表示回放结束
 */
size_t ecard_voice_playback_read(int16_t *pcm_out, size_t max_samples);

/**
 * @brief 终止回放（不清除录音缓冲）
 */
void ecard_voice_playback_stop(void);

/**
 * @brief 释放录音缓冲；下次录音会自动重新分配
 */
void ecard_voice_clip_release(void);

/**
 * @brief 本次录音的峰值电平（0..32767），用于判断麦克风增益是否削顶
 */
uint16_t ecard_voice_peak_level(void);

/**
 * @brief 单个 IMA-ADPCM 样本编码辅助函数
 */
uint8_t ecard_adpcm_encode_sample(int16_t sample, int16_t *predicted, int8_t *index);

/**
 * @brief 单个 IMA-ADPCM 样本解码辅助函数
 */
int16_t ecard_adpcm_decode_sample(uint8_t nibble, int16_t *predicted, int8_t *index);

#ifdef __cplusplus
}
#endif

#endif // ECARD_VOICE_H
