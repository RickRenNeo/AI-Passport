#include "ecard_vad.h"
#include <string.h>

void ecard_vad_reset(ecard_vad_t *vad) {
    if (!vad) return;
    memset(vad, 0, sizeof(*vad));
}

void ecard_vad_feed(ecard_vad_t *vad, const int16_t *pcm, size_t samples,
                    uint32_t sample_rate_hz) {
    if (!vad || !pcm || samples == 0 || sample_rate_hz == 0) return;

    int64_t sum = 0;
    for (size_t i = 0; i < samples; ++i) {
        sum += pcm[i];
    }
    const int32_t dc = (int32_t)(sum / (int64_t)samples);

    uint64_t abs_sum = 0;
    for (size_t i = 0; i < samples; ++i) {
        int32_t diff = (int32_t)pcm[i] - dc;
        if (diff < 0) diff = -diff;
        abs_sum += (uint32_t)diff;
    }
    const uint32_t mean_abs = (uint32_t)(abs_sum / samples);

    uint32_t amp = (mean_abs * 100u) / 3200u;
    if (amp > 100u) amp = 100u;
    vad->amplitude = (uint8_t)amp;

    uint32_t chunk_ms = (uint32_t)((samples * 1000u + sample_rate_hz / 2u) / sample_rate_hz);
    if (chunk_ms == 0) chunk_ms = 1u;
    vad->total_ms += chunk_ms;

    // 跳过起始极短按键释放/扬声器残响窗口，避免误把机械咔嗒声计入人声
    if (vad->total_ms <= ECARD_VAD_SETTLE_MS) {
        return;
    }

    const uint32_t thresh = vad->in_speech ? ECARD_VAD_SPEECH_HOLD_ABS
                                           : ECARD_VAD_SPEECH_ON_ABS;
    if (mean_abs >= thresh) {
        vad->in_speech = true;
        vad->silence_ms = 0;
        if (vad->speech_run_ms < 60000u) {
            vad->speech_run_ms = (uint16_t)(vad->speech_run_ms + chunk_ms);
        }
        if (vad->speech_run_ms >= ECARD_VAD_MIN_SPEECH_MS) {
            vad->voice_detected = true;
            vad->end_of_speech = false;
        }
    } else {
        vad->in_speech = false;
        if (!vad->voice_detected) {
            vad->speech_run_ms = 0;
        } else if (vad->silence_ms < 60000u) {
            vad->silence_ms = (uint16_t)(vad->silence_ms + chunk_ms);
            if (vad->silence_ms >= ECARD_VAD_TAIL_SILENCE_MS) {
                vad->end_of_speech = true;
            }
        }
    }
}
