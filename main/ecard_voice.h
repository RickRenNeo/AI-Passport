#ifndef ECARD_VOICE_H
#define ECARD_VOICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECARD_VOICE_MAX_SEC       30   // 单次录音最长 30 秒
#define ECARD_VOICE_SAMPLE_RATE   16000// 16kHz
#define ECARD_VOICE_BYTES_PER_SEC 8000 // 16000 * 4-bit = 8000 bytes/s
#define ECARD_VOICE_MAX_BUFFER    (ECARD_VOICE_MAX_SEC * ECARD_VOICE_BYTES_PER_SEC) // 240,000 bytes (240KB)

typedef enum {
    ECARD_VOICE_IDLE = 0,
    ECARD_VOICE_RECORDING,
    ECARD_VOICE_PLAYING
} ecard_voice_state_t;

/**
 * @brief 初始化语音缓存与编码器状态
 */
void ecard_voice_init(void);

/**
 * @brief 开始录制新留言
 * @return true 启动成功, false 处于忙碌状态
 */
bool ecard_voice_start_recording(void);

/**
 * @brief 流式写入采集到的 16-bit 单声道 PCM 样本并转为 ADPCM 存入缓存
 *
 * @param pcm_samples 输入 16-bit PCM 数组
 * @param sample_count 样本数量
 * @param out_remaining_sec 输出当前剩余秒数 (0..30)
 * @param out_amplitude 输出当前音频平均振幅 (0..100)
 * @return true 写入成功, false 已达到 30 秒上限已自动停止
 */
bool ecard_voice_write_pcm(const int16_t *pcm_samples,
                           size_t sample_count,
                           uint8_t *out_remaining_sec,
                           uint8_t *out_amplitude);

/**
 * @brief 停止录制并封包
 *
 * @param out_adpcm_data 输出 ADPCM 数据缓冲区指针
 * @param out_data_len 输出总字节数
 * @param out_duration_sec 输出实际录制时长 (秒)
 * @return true 录音有效且成功, false 无效或过短
 */
bool ecard_voice_stop_recording(const uint8_t **out_adpcm_data, size_t *out_data_len, uint8_t *out_duration_sec);

/**
 * @brief 获取当前语音工作状态
 */
ecard_voice_state_t ecard_voice_get_state(void);

/**
 * @brief 重置录音缓存
 */
void ecard_voice_reset(void);

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
