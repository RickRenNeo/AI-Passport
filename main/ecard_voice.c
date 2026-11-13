#include "ecard_voice.h"
#include <stdlib.h>
#include <string.h>

// IMA-ADPCM 标准步长表
static const int16_t k_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

// IMA-ADPCM 步长索引调整表
static const int8_t k_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static uint8_t *s_voice_buf = NULL;
static size_t s_voice_capacity = 0;
// 预留缓冲：开机时一次性分配，避免 Wi-Fi/界面运行后堆碎片把录音时长压到几秒。
static bool s_buf_reserved;
static size_t s_voice_buf_len = 0;
static ecard_voice_state_t s_state = ECARD_VOICE_IDLE;

static size_t s_samples_encoded = 0;
static int32_t s_peak = 0;

static int16_t s_encoder_predicted = 0;
static int8_t s_encoder_index = 0;
static bool s_has_low_nibble = false;
static uint8_t s_saved_low_nibble = 0;

// 回放游标：一个 ADPCM 半字节解码为一个 16 kHz 样本
static bool s_play_active = false;
static const uint8_t *s_play_src = NULL;
static size_t s_play_pos = 0;
static size_t s_play_len = 0;
static uint8_t s_play_byte = 0;
static bool s_play_byte_ready = false;
static bool s_play_high_nibble = false;
static bool s_play_expand_8k = false;
static int16_t s_decoder_predicted = 0;
static int8_t s_decoder_index = 0;

// 由 tools/generate_ecard_demo_voice.py 生成，见 assets/sounds/。
extern const uint8_t ecard_demo_voice_adpcm[];
extern const size_t ecard_demo_voice_adpcm_len;
extern const uint8_t ecard_demo_voice_duration_sec;

uint8_t ecard_adpcm_encode_sample(int16_t sample, int16_t *predicted, int8_t *index) {
    int32_t diff = (int32_t)sample - (int32_t)(*predicted);
    uint8_t nibble = 0;

    if (diff < 0) {
        nibble = 8;
        diff = -diff;
    }

    int32_t step = k_step_table[*index];
    int32_t delta = step >> 3;
    if (diff >= step) {
        nibble |= 4;
        diff -= step;
        delta += step;
    }
    if (diff >= (step >> 1)) {
        nibble |= 2;
        diff -= (step >> 1);
        delta += (step >> 1);
    }
    if (diff >= (step >> 2)) {
        nibble |= 1;
        delta += (step >> 2);
    }

    int32_t pred = *predicted;
    pred += (nibble & 8) ? -delta : delta;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    *predicted = (int16_t)pred;

    *index += k_index_table[nibble];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;

    return nibble;
}

int16_t ecard_adpcm_decode_sample(uint8_t nibble, int16_t *predicted, int8_t *index) {
    int32_t step = k_step_table[*index];
    int32_t delta = step >> 3;
    if (nibble & 4) delta += step;
    if (nibble & 2) delta += (step >> 1);
    if (nibble & 1) delta += (step >> 2);

    int32_t pred = *predicted;
    pred += (nibble & 8) ? -delta : delta;
    if (pred > 32767) pred = 32767;
    if (pred < -32768) pred = -32768;
    *predicted = (int16_t)pred;

    *index += k_index_table[nibble & 0x0F];
    if (*index < 0) *index = 0;
    if (*index > 88) *index = 88;

    return (int16_t)pred;
}

void ecard_voice_clip_release(void) {
    if (!s_buf_reserved) {
        free(s_voice_buf);
        s_voice_buf = NULL;
        s_voice_capacity = 0;
    }
    s_voice_buf_len = 0;
    s_play_active = false;
    s_play_src = NULL;
    s_play_pos = 0;
    s_play_len = 0;
}

void ecard_voice_init(void) {
    ecard_voice_clip_release();
    s_state = ECARD_VOICE_IDLE;
    s_samples_encoded = 0;
    s_peak = 0;
    s_has_low_nibble = false;
    s_encoder_predicted = 0;
    s_encoder_index = 0;
    s_play_byte_ready = false;
    s_play_high_nibble = false;
    s_decoder_predicted = 0;
    s_decoder_index = 0;
}

void ecard_voice_reserve(size_t bytes) {
    if (s_voice_buf || bytes == 0) return;
    s_voice_buf = malloc(bytes);
    if (s_voice_buf) {
        s_voice_capacity = bytes;
        s_buf_reserved = true;
        s_voice_buf_len = 0;
    }
}

void ecard_voice_release_reserve(void) {
    if (!s_buf_reserved) return;
    free(s_voice_buf);
    s_voice_buf = NULL;
    s_voice_capacity = 0;
    s_voice_buf_len = 0;
    s_buf_reserved = false;
    s_play_active = false;
    s_play_src = NULL;
}

size_t ecard_voice_buffer_capacity(void) {
    return s_voice_capacity;
}

uint16_t ecard_voice_peak_level(void) {
    return (uint16_t)(s_peak > 32767 ? 32767 : s_peak);
}

bool ecard_voice_start_recording(void) {
    if (s_state != ECARD_VOICE_IDLE) return false;

    if (s_buf_reserved && s_voice_buf) {
        // 已预留：固定 10 秒可用，不再受运行时堆碎片影响。
        s_voice_buf_len = 0;
        s_samples_encoded = 0;
        s_peak = 0;
        s_has_low_nibble = false;
        s_encoder_predicted = 0;
        s_encoder_index = 0;
        s_state = ECARD_VOICE_RECORDING;
        return true;
    }

    ecard_voice_clip_release();

    // C3 的堆最大连续块通常小于总空闲量（实测约 112 KB），
    // 因此按整秒从 15 秒往下试，第一次成功即为本次可用时长。
    for (uint8_t sec = ECARD_VOICE_RECORD_SEC; sec >= 1; --sec) {
        size_t want = (size_t)sec * ECARD_VOICE_BYTES_PER_SEC;
        s_voice_buf = malloc(want);
        if (s_voice_buf) {
            s_voice_capacity = want;
            break;
        }
    }
    if (!s_voice_buf) return false;

    s_voice_buf_len = 0;
    s_samples_encoded = 0;
    s_peak = 0;
    s_has_low_nibble = false;
    s_encoder_predicted = 0;
    s_encoder_index = 0;
    s_state = ECARD_VOICE_RECORDING;
    return true;
}

// 把 8 kHz 样本编码成半个字节，写入缓存；缓存已满返回 false
static bool encode_sample(int16_t sample) {
    if (s_voice_buf_len >= s_voice_capacity && !s_has_low_nibble) {
        return false;
    }

    uint8_t nibble = ecard_adpcm_encode_sample(sample, &s_encoder_predicted, &s_encoder_index);

    if (!s_has_low_nibble) {
        s_saved_low_nibble = nibble & 0x0F;
        s_has_low_nibble = true;
    } else {
        if (s_voice_buf_len >= s_voice_capacity) return false;
        s_voice_buf[s_voice_buf_len++] = s_saved_low_nibble | ((nibble & 0x0F) << 4);
        s_has_low_nibble = false;
    }

    s_samples_encoded++;
    return true;
}

bool ecard_voice_write_pcm(const int16_t *pcm_samples,
                           size_t sample_count,
                           uint8_t *out_remaining_sec,
                           uint8_t *out_amplitude) {
    if (s_state != ECARD_VOICE_RECORDING || !pcm_samples) return false;

    int64_t sum_amp = 0;
    bool full = false;

    // 输入是 16 kHz：两两取均值降到 8 kHz 再编码，存储量减半。
    size_t i = 0;
    for (; i + 1 < sample_count; i += 2) {
        const int16_t pair = (int16_t)(((int32_t)pcm_samples[i] + pcm_samples[i + 1]) / 2);
        int v = abs(pair);
        sum_amp += v;
        if (v > s_peak) s_peak = v;
        if (!encode_sample(pair)) {
            full = true;
            break;
        }
    }
    if (!full && i < sample_count) {
        int16_t s = pcm_samples[i];
        int v = abs(s);
        sum_amp += v;
        if (v > s_peak) s_peak = v;
        if (!encode_sample(s)) full = true;
    }

    const size_t max_sec = s_voice_capacity / ECARD_VOICE_BYTES_PER_SEC;
    uint32_t elapsed_sec = (uint32_t)(s_samples_encoded / ECARD_VOICE_STORE_RATE);
    uint8_t rem = 0;
    if (elapsed_sec < max_sec) {
        rem = (uint8_t)(max_sec - elapsed_sec);
    }

    if (out_remaining_sec) *out_remaining_sec = rem;

    if (out_amplitude && sample_count > 0) {
        int avg = (int)(sum_amp / (int64_t)sample_count);
        int amp_pct = (avg * 100) / 16384;
        if (amp_pct > 100) amp_pct = 100;
        *out_amplitude = (uint8_t)amp_pct;
    }

    return !full && rem > 0;
}

bool ecard_voice_stop_recording(const uint8_t **out_adpcm_data, size_t *out_data_len, uint8_t *out_duration_sec) {
    if (s_state != ECARD_VOICE_RECORDING && s_state != ECARD_VOICE_IDLE) {
        return false;
    }

    // 末尾落单的 nibble 补成一个字节，保证回放长度与采样数一致
    if (s_has_low_nibble && s_voice_buf_len < s_voice_capacity) {
        s_voice_buf[s_voice_buf_len++] = s_saved_low_nibble;
        s_has_low_nibble = false;
    }

    uint8_t dur = (uint8_t)(s_samples_encoded / ECARD_VOICE_STORE_RATE);
    if (dur == 0 && s_samples_encoded > (ECARD_VOICE_STORE_RATE / 2)) {
        dur = 1;
    }

    s_state = ECARD_VOICE_IDLE;

    if (out_adpcm_data) *out_adpcm_data = s_voice_buf;
    if (out_data_len) *out_data_len = s_voice_buf_len;
    if (out_duration_sec) *out_duration_sec = dur;

    return (s_voice_buf_len > 0);
}

ecard_voice_state_t ecard_voice_get_state(void) {
    return s_state;
}

void ecard_voice_reset(void) {
    ecard_voice_init();
}

bool ecard_voice_has_clip(void) {
    return s_voice_buf != NULL && s_voice_buf_len > 0;
}

bool ecard_voice_playback_start(void) {
    if (!ecard_voice_has_clip()) return false;

    s_play_src = s_voice_buf;
    s_play_pos = 0;
    s_play_len = s_voice_buf_len;
    s_play_byte_ready = false;
    s_play_high_nibble = false;
    s_decoder_predicted = 0;
    s_decoder_index = 0;
    s_play_active = true;
    s_play_expand_8k = true; // 本机录音按 8 kHz 存档，回放需插值回 16 kHz
    s_state = ECARD_VOICE_PLAYING;
    return true;
}

bool ecard_voice_playback_start_demo(void) {
    if (ecard_demo_voice_adpcm_len == 0) return false;

    s_play_src = ecard_demo_voice_adpcm;
    s_play_pos = 0;
    s_play_len = ecard_demo_voice_adpcm_len;
    s_play_byte_ready = false;
    s_play_high_nibble = false;
    s_decoder_predicted = 0;
    s_decoder_index = 0;
    s_play_active = true;
    s_play_expand_8k = false; // 演示语音本身是 16 kHz
    s_state = ECARD_VOICE_PLAYING;
    return true;
}

uint8_t ecard_voice_demo_duration_sec(void) {
    return ecard_demo_voice_duration_sec;
}

size_t ecard_voice_playback_read(int16_t *pcm_out, size_t max_samples) {
    if (!s_play_active || !pcm_out || max_samples == 0) return 0;

    size_t written = 0;
    while (written < max_samples) {
        if (!s_play_byte_ready) {
            if (s_play_pos >= s_play_len || !s_play_src) break;
            s_play_byte = s_play_src[s_play_pos++];
            s_play_byte_ready = true;
            s_play_high_nibble = false;
        }

        uint8_t nibble = s_play_high_nibble ? (uint8_t)((s_play_byte >> 4) & 0x0F)
                                            : (uint8_t)(s_play_byte & 0x0F);
        if (s_play_high_nibble) {
            s_play_byte_ready = false;
        } else {
            s_play_high_nibble = true;
        }

        const int16_t sample =
            ecard_adpcm_decode_sample(nibble, &s_decoder_predicted, &s_decoder_index);
        pcm_out[written++] = sample;
        if (s_play_expand_8k && written < max_samples) {
            // 8 kHz 存档回放成 16 kHz：每个样本重复一次即可。
            pcm_out[written++] = sample;
        }
    }

    if (written == 0) {
        s_play_active = false;
        s_state = ECARD_VOICE_IDLE;
    }
    return written;
}

void ecard_voice_playback_stop(void) {
    s_play_active = false;
    s_play_byte_ready = false;
    s_play_src = NULL;
    if (s_state == ECARD_VOICE_PLAYING) {
        s_state = ECARD_VOICE_IDLE;
    }
}
