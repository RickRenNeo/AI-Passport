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

static uint8_t s_voice_buf[ECARD_VOICE_MAX_BUFFER];
static size_t s_voice_buf_len = 0;
static ecard_voice_state_t s_state = ECARD_VOICE_IDLE;
static int16_t s_encoder_predicted = 0;
static int8_t s_encoder_index = 0;
static size_t s_samples_recorded = 0;
static bool s_has_low_nibble = false;
static uint8_t s_saved_low_nibble = 0;

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
    step >>= 1;
    if (diff >= step) {
        nibble |= 2;
        diff -= step;
        delta += step;
    }
    step >>= 1;
    if (diff >= step) {
        nibble |= 1;
        delta += step;
    }

    if (nibble & 8) {
        *predicted -= delta;
        if (*predicted < -32768) *predicted = -32768;
    } else {
        *predicted += delta;
        if (*predicted > 32767) *predicted = 32767;
    }

    *index += k_index_table[nibble];
    if (*index < 0) *index = 0;
    else if (*index > 88) *index = 88;

    return nibble;
}

int16_t ecard_adpcm_decode_sample(uint8_t nibble, int16_t *predicted, int8_t *index) {
    int32_t step = k_step_table[*index];
    int32_t delta = step >> 3;

    if (nibble & 4) delta += step;
    if (nibble & 2) delta += (step >> 1);
    if (nibble & 1) delta += (step >> 2);

    if (nibble & 8) {
        *predicted -= delta;
        if (*predicted < -32768) *predicted = -32768;
    } else {
        *predicted += delta;
        if (*predicted > 32767) *predicted = 32767;
    }

    *index += k_index_table[nibble & 0x0F];
    if (*index < 0) *index = 0;
    else if (*index > 88) *index = 88;

    return *predicted;
}

void ecard_voice_init(void) {
    s_state = ECARD_VOICE_IDLE;
    s_voice_buf_len = 0;
    s_samples_recorded = 0;
    s_has_low_nibble = false;
    s_encoder_predicted = 0;
    s_encoder_index = 0;
}

bool ecard_voice_start_recording(void) {
    if (s_state != ECARD_VOICE_IDLE) return false;
    s_voice_buf_len = 0;
    s_samples_recorded = 0;
    s_has_low_nibble = false;
    s_encoder_predicted = 0;
    s_encoder_index = 0;
    s_state = ECARD_VOICE_RECORDING;
    return true;
}

bool ecard_voice_write_pcm(const int16_t *pcm_samples,
                           size_t sample_count,
                           uint8_t *out_remaining_sec,
                           uint8_t *out_amplitude) {
    if (s_state != ECARD_VOICE_RECORDING || !pcm_samples) return false;

    int64_t sum_amp = 0;

    for (size_t i = 0; i < sample_count; ++i) {
        if (s_voice_buf_len >= ECARD_VOICE_MAX_BUFFER) {
            // 已达 30 秒上限
            s_state = ECARD_VOICE_IDLE;
            if (out_remaining_sec) *out_remaining_sec = 0;
            if (out_amplitude) *out_amplitude = 0;
            return false;
        }

        int16_t s = pcm_samples[i];
        sum_amp += abs(s);

        uint8_t nibble = ecard_adpcm_encode_sample(s, &s_encoder_predicted, &s_encoder_index);

        if (!s_has_low_nibble) {
            s_saved_low_nibble = nibble & 0x0F;
            s_has_low_nibble = true;
        } else {
            uint8_t byte = s_saved_low_nibble | ((nibble & 0x0F) << 4);
            s_voice_buf[s_voice_buf_len++] = byte;
            s_has_low_nibble = false;
        }

        s_samples_recorded++;
    }

    uint32_t elapsed_sec = (uint32_t)(s_samples_recorded / ECARD_VOICE_SAMPLE_RATE);
    uint8_t rem = 0;
    if (elapsed_sec < ECARD_VOICE_MAX_SEC) {
        rem = (uint8_t)(ECARD_VOICE_MAX_SEC - elapsed_sec);
    }

    if (out_remaining_sec) *out_remaining_sec = rem;

    if (out_amplitude && sample_count > 0) {
        int avg = (int)(sum_amp / (int64_t)sample_count);
        // 映射 0..32767 -> 0..100
        int amp_pct = (avg * 100) / 16384;
        if (amp_pct > 100) amp_pct = 100;
        *out_amplitude = (uint8_t)amp_pct;
    }

    return (rem > 0);
}

bool ecard_voice_stop_recording(const uint8_t **out_adpcm_data, size_t *out_data_len, uint8_t *out_duration_sec) {
    if (s_state != ECARD_VOICE_RECORDING && s_state != ECARD_VOICE_IDLE) {
        return false;
    }

    // 处理最后一个奇数 nibble
    if (s_has_low_nibble && s_voice_buf_len < ECARD_VOICE_MAX_BUFFER) {
        s_voice_buf[s_voice_buf_len++] = s_saved_low_nibble;
        s_has_low_nibble = false;
    }

    uint8_t dur = (uint8_t)(s_samples_recorded / ECARD_VOICE_SAMPLE_RATE);
    if (dur == 0 && s_samples_recorded > (ECARD_VOICE_SAMPLE_RATE / 2)) {
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
