#ifndef ECARD_VAD_H
#define ECARD_VAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 本地零依赖整数 PCM 端点检测器（VAD）与振幅估计。
 *
 * 用于小智 AI 松手免持提问与免按键多轮续聊：
 * 1. 过滤录音起步前 160 ms 的按键/扬声器尾音抖动；
 * 2. 以去直流平均绝对值（AC mean-abs）做双阈值迟滞判定，连续语音 >= 200 ms 置位 voice_detected；
 * 3. 已检测到人声后，尾部静音累计 >= 1500 ms 置位 end_of_speech，供应用层自动提交。
 */
#define ECARD_VAD_SETTLE_MS       160u
#define ECARD_VAD_MIN_SPEECH_MS   200u
#define ECARD_VAD_TAIL_SILENCE_MS 1500u
#define ECARD_VAD_SPEECH_ON_ABS   650u
#define ECARD_VAD_SPEECH_HOLD_ABS 420u

typedef struct {
    uint32_t total_ms;
    uint16_t speech_run_ms;
    uint16_t silence_ms;
    uint8_t amplitude;
    bool in_speech;
    bool voice_detected;
    bool end_of_speech;
} ecard_vad_t;

void ecard_vad_reset(ecard_vad_t *vad);

void ecard_vad_feed(ecard_vad_t *vad, const int16_t *pcm, size_t samples,
                    uint32_t sample_rate_hz);

#ifdef __cplusplus
}
#endif

#endif // ECARD_VAD_H
