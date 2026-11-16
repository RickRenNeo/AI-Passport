#include "ecard_voice.h"
#if defined(ESP_PLATFORM)
#include "ecard_opus_arena.h"
#endif
#include <stdio.h>
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

#define VOICE_MAX_CHUNKS 16

typedef struct {
    uint8_t *ptr;
    size_t cap;
    size_t len;
    bool from_heap;
    bool from_arena;
} voice_chunk_t;

static voice_chunk_t s_chunks[VOICE_MAX_CHUNKS];
static size_t s_chunk_count = 0;
static size_t s_write_chunk = 0;
static size_t s_voice_capacity = 0;
// 预留缓冲：开机时一次性分配，避免 Wi-Fi/界面运行后堆碎片把录音时长压到几秒。
static bool s_buf_reserved = false;
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
static size_t s_play_chunk = 0;
static size_t s_play_chunk_pos = 0;
static FILE *s_play_file = NULL;
static uint8_t s_play_file_buf[512];
static size_t s_play_file_pos = 0;
static size_t s_play_file_len = 0;
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
    if (s_play_file) {
        fclose(s_play_file);
        s_play_file = NULL;
    }
    s_play_file_pos = 0;
    s_play_file_len = 0;
    if (!s_buf_reserved) {
        for (size_t i = 0; i < s_chunk_count; ++i) {
            if (s_chunks[i].from_arena) {
#if defined(ESP_PLATFORM)
                ecard_opus_arena_return(s_chunks[i].ptr);
#endif
            } else if (s_chunks[i].from_heap) {
                free(s_chunks[i].ptr);
            }
        }
        memset(s_chunks, 0, sizeof(s_chunks));
        s_chunk_count = 0;
        s_voice_capacity = 0;
    } else {
        for (size_t i = 0; i < s_chunk_count; ++i) {
            s_chunks[i].len = 0;
        }
    }
    s_write_chunk = 0;
    s_voice_buf_len = 0;
    s_play_active = false;
    s_play_src = NULL;
    s_play_pos = 0;
    s_play_len = 0;
    s_play_chunk = 0;
    s_play_chunk_pos = 0;
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
    if (s_chunk_count > 0 || bytes == 0) return;
    uint8_t *buf = malloc(bytes);
    if (buf) {
        s_chunks[0].ptr = buf;
        s_chunks[0].cap = bytes;
        s_chunks[0].len = 0;
        s_chunks[0].from_heap = true;
        s_chunks[0].from_arena = false;
        s_chunk_count = 1;
        s_write_chunk = 0;
        s_voice_capacity = bytes;
        s_buf_reserved = true;
        s_voice_buf_len = 0;
    }
}

void ecard_voice_release_reserve(void) {
    if (!s_buf_reserved) return;
    s_buf_reserved = false;
    ecard_voice_clip_release();
}

size_t ecard_voice_buffer_capacity(void) {
    return s_voice_capacity;
}

uint16_t ecard_voice_peak_level(void) {
    return (uint16_t)(s_peak > 32767 ? 32767 : s_peak);
}

static bool append_heap_chunk(size_t bytes) {
    if (bytes == 0 || s_chunk_count >= VOICE_MAX_CHUNKS) return false;
    uint8_t *p = malloc(bytes);
    if (!p) return false;
    s_chunks[s_chunk_count].ptr = p;
    s_chunks[s_chunk_count].cap = bytes;
    s_chunks[s_chunk_count].len = 0;
    s_chunks[s_chunk_count].from_heap = true;
    s_chunks[s_chunk_count].from_arena = false;
    s_chunk_count++;
    s_voice_capacity += bytes;
    return true;
}

bool ecard_voice_start_recording_chunked(size_t max_chunk_bytes) {
    if (s_state != ECARD_VOICE_IDLE) return false;

    if (s_buf_reserved && s_chunk_count == 1 && s_chunks[0].ptr) {
        s_chunks[0].len = 0;
        s_write_chunk = 0;
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

    const size_t want = (size_t)ECARD_VOICE_RECORD_SEC * ECARD_VOICE_BYTES_PER_SEC;

    if (max_chunk_bytes == 0) {
#if defined(ESP_PLATFORM)
        // 首页录音不与小智会话并发：借用闲置的 25 KB 静态后备区，仅需从堆再补 ~14.4 KB。
        uint8_t *arena_ptr = NULL;
        size_t arena_bytes = 0;
        if (ecard_opus_arena_borrow(&arena_ptr, &arena_bytes) && arena_ptr && arena_bytes > 0) {
            const size_t cap = (arena_bytes < want) ? arena_bytes : want;
            s_chunks[0].ptr = arena_ptr;
            s_chunks[0].cap = cap;
            s_chunks[0].len = 0;
            s_chunks[0].from_heap = false;
            s_chunks[0].from_arena = true;
            s_chunk_count = 1;
            s_voice_capacity = cap;
        }
#endif
        if (s_chunk_count == 0) {
            // 宿主单测或后备区占满时先尝试整块连续申请，保持出参指针连续
            (void)append_heap_chunk(want);
        }

        // 若未达 40 KB，则按小块分片从堆补足，避免连续大块碎片限制
        static const size_t k_steps[] = { 8000, 4000, 2000 };
        while (s_voice_capacity < want && s_chunk_count < VOICE_MAX_CHUNKS) {
            const size_t rem = want - s_voice_capacity;
            bool got = false;
            if (rem <= 8000) {
                got = append_heap_chunk(rem);
            }
            for (size_t i = 0; !got && i < sizeof(k_steps) / sizeof(k_steps[0]); ++i) {
                const size_t step = (rem < k_steps[i]) ? rem : k_steps[i];
                if (step >= 1000 && append_heap_chunk(step)) {
                    got = true;
                }
            }
            if (!got) break;
        }
    } else {
        while (s_voice_capacity < want && s_chunk_count < VOICE_MAX_CHUNKS) {
            const size_t rem = want - s_voice_capacity;
            const size_t step = (rem < max_chunk_bytes) ? rem : max_chunk_bytes;
            if (!append_heap_chunk(step)) break;
        }
    }

    if (s_voice_capacity < ECARD_VOICE_BYTES_PER_SEC || s_chunk_count == 0) {
        ecard_voice_clip_release();
        return false;
    }

    s_write_chunk = 0;
    s_voice_buf_len = 0;
    s_samples_encoded = 0;
    s_peak = 0;
    s_has_low_nibble = false;
    s_encoder_predicted = 0;
    s_encoder_index = 0;
    s_state = ECARD_VOICE_RECORDING;
    return true;
}

bool ecard_voice_start_recording(void) {
    return ecard_voice_start_recording_chunked(0);
}

static bool write_packed_byte(uint8_t byte) {
    while (s_write_chunk < s_chunk_count &&
           s_chunks[s_write_chunk].len >= s_chunks[s_write_chunk].cap) {
        s_write_chunk++;
    }
    if (s_write_chunk >= s_chunk_count) return false;
    s_chunks[s_write_chunk].ptr[s_chunks[s_write_chunk].len++] = byte;
    s_voice_buf_len++;
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
        if (!write_packed_byte((uint8_t)(s_saved_low_nibble | ((nibble & 0x0F) << 4)))) {
            return false;
        }
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
        if (write_packed_byte(s_saved_low_nibble)) {
            s_has_low_nibble = false;
        }
    }

    uint8_t dur = (uint8_t)(s_samples_encoded / ECARD_VOICE_STORE_RATE);
    if (dur == 0 && s_samples_encoded > (ECARD_VOICE_STORE_RATE / 2)) {
        dur = 1;
    }

    s_state = ECARD_VOICE_IDLE;

    if (out_adpcm_data) {
        *out_adpcm_data = (s_chunk_count == 1) ? s_chunks[0].ptr : NULL;
    }
    if (out_data_len) *out_data_len = s_voice_buf_len;
    if (out_duration_sec) *out_duration_sec = dur;

    return (s_voice_buf_len > 0);
}

bool ecard_voice_save_to_file(const char *path, size_t *out_bytes) {
    if (!path || !path[0] || s_voice_buf_len == 0 || s_chunk_count == 0) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;

    size_t total = 0;
    bool ok = true;
    for (size_t i = 0; i < s_chunk_count; ++i) {
        if (s_chunks[i].len == 0) continue;
        if (fwrite(s_chunks[i].ptr, 1, s_chunks[i].len, fp) != s_chunks[i].len) {
            ok = false;
            break;
        }
        total += s_chunks[i].len;
    }
    if (fflush(fp) != 0) ok = false;
    if (fclose(fp) != 0) ok = false;

    if (!ok || total != s_voice_buf_len) {
        (void)remove(path);
        return false;
    }
    if (out_bytes) *out_bytes = total;
    return true;
}

ecard_voice_state_t ecard_voice_get_state(void) {
    return s_state;
}

void ecard_voice_reset(void) {
    ecard_voice_init();
}

bool ecard_voice_has_clip(void) {
    return s_chunk_count > 0 && s_voice_buf_len > 0;
}

static void close_play_file(void) {
    if (s_play_file) {
        fclose(s_play_file);
        s_play_file = NULL;
    }
    s_play_file_pos = 0;
    s_play_file_len = 0;
}

bool ecard_voice_playback_start(void) {
    if (!ecard_voice_has_clip()) return false;

    close_play_file();
    s_play_src = NULL;
    s_play_pos = 0;
    s_play_len = 0;
    s_play_chunk = 0;
    s_play_chunk_pos = 0;
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

    close_play_file();
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

bool ecard_voice_playback_start_file(const char *path, bool expand_8k) {
    if (!path || !path[0]) return false;

    close_play_file();
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    s_play_file = fp;
    s_play_file_pos = 0;
    s_play_file_len = 0;
    s_play_src = NULL;
    s_play_pos = 0;
    s_play_len = 0;
    s_play_byte_ready = false;
    s_play_high_nibble = false;
    s_decoder_predicted = 0;
    s_decoder_index = 0;
    s_play_active = true;
    s_play_expand_8k = expand_8k;
    s_state = ECARD_VOICE_PLAYING;
    return true;
}

uint8_t ecard_voice_demo_duration_sec(void) {
    return ecard_demo_voice_duration_sec;
}

static bool next_play_byte(uint8_t *out) {
    if (s_play_file) {
        if (s_play_file_pos >= s_play_file_len) {
            s_play_file_len = fread(s_play_file_buf, 1, sizeof(s_play_file_buf), s_play_file);
            s_play_file_pos = 0;
            if (s_play_file_len == 0) {
                return false;
            }
        }
        *out = s_play_file_buf[s_play_file_pos++];
        return true;
    }
    if (s_play_src) {
        if (s_play_pos >= s_play_len) return false;
        *out = s_play_src[s_play_pos++];
        return true;
    }
    while (s_play_chunk < s_chunk_count) {
        if (s_play_chunk_pos < s_chunks[s_play_chunk].len) {
            *out = s_chunks[s_play_chunk].ptr[s_play_chunk_pos++];
            return true;
        }
        s_play_chunk++;
        s_play_chunk_pos = 0;
    }
    return false;
}

size_t ecard_voice_playback_read(int16_t *pcm_out, size_t max_samples) {
    if (!s_play_active || !pcm_out || max_samples == 0) return 0;

    size_t written = 0;
    while (written < max_samples) {
        if (!s_play_byte_ready) {
            if (!next_play_byte(&s_play_byte)) break;
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
        close_play_file();
        s_play_active = false;
        s_state = ECARD_VOICE_IDLE;
    }
    return written;
}

void ecard_voice_playback_stop(void) {
    close_play_file();
    s_play_active = false;
    s_play_byte_ready = false;
    s_play_src = NULL;
    if (s_state == ECARD_VOICE_PLAYING) {
        s_state = ECARD_VOICE_IDLE;
    }
}
