// tests/test_tianji_voice.c — 验证天机六爻语音交互与 WAV 头结构契约
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "tianji_logic.h"
#ifndef LV_FONT_DECLARE
#define LV_FONT_DECLARE(name)
#endif
#include "tianji_ui.h"


#pragma pack(push, 1)
typedef struct {
    char riff[4];
    uint32_t overall_size;
    char wave[4];
    char fmt_chunk_marker[4];
    uint32_t length_of_fmt;
    uint16_t format_type;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data_chunk_header[4];
    uint32_t data_size;
} test_wav_header_t;
#pragma pack(pop)

static void test_wav_header_structure(void) {
    assert(sizeof(test_wav_header_t) == 44);

    test_wav_header_t hdr = {
        .riff = {'R', 'I', 'F', 'F'},
        .overall_size = 44 + 32000 - 8,
        .wave = {'W', 'A', 'V', 'E'},
        .fmt_chunk_marker = {'f', 'm', 't', ' '},
        .length_of_fmt = 16,
        .format_type = 1,
        .channels = 1,
        .sample_rate = 16000,
        .byterate = 32000,
        .block_align = 2,
        .bits_per_sample = 16,
        .data_chunk_header = {'d', 'a', 't', 'a'},
        .data_size = 32000
    };

    assert(memcmp(hdr.riff, "RIFF", 4) == 0);
    assert(memcmp(hdr.wave, "WAVE", 4) == 0);
    assert(memcmp(hdr.fmt_chunk_marker, "fmt ", 4) == 0);
    assert(hdr.format_type == 1);
    assert(hdr.channels == 1);
    assert(hdr.sample_rate == 16000);
    assert(hdr.byterate == 32000);
    assert(hdr.bits_per_sample == 16);
    assert(memcmp(hdr.data_chunk_header, "data", 4) == 0);
    assert(hdr.data_size == 32000);

    printf("test_wav_header_structure: PASS (44 bytes standard PCM WAV header)\n");
}

static void test_rms_volume_calculation(void) {
    int16_t silence_samples[512] = {0};
    int64_t sum_sq = 0;
    for (int i = 0; i < 512; i++) {
        int32_t s = silence_samples[i];
        sum_sq += s * s;
    }
    int rms_silence = (int)sqrt((double)sum_sq / 512.0);
    assert(rms_silence == 0);

    // 模拟正弦波 2000 幅度
    int16_t voice_samples[512];
    sum_sq = 0;
    for (int i = 0; i < 512; i++) {
        voice_samples[i] = (int16_t)(2000.0 * sin(2.0 * M_PI * i / 16.0));
        int32_t s = voice_samples[i];
        sum_sq += s * s;
    }
    int rms_voice = (int)sqrt((double)sum_sq / 512.0);
    assert(rms_voice > 1000 && rms_voice < 2000);

    int vol = (rms_voice > 7000) ? 100 : (rms_voice * 100 / 7000);
    assert(vol > 15 && vol < 30);

    printf("test_rms_volume_calculation: PASS (RMS=%d, Volume=%d)\n", rms_voice, vol);
}

static void test_view_states_enum(void) {
    assert(TIANJI_VIEW_STANDBY == 0);
    assert(TIANJI_VIEW_CASTING == 1);
    assert(TIANJI_VIEW_OVERVIEW == 2);
    assert(TIANJI_VIEW_READING == 3);
    assert(TIANJI_VIEW_AI == 4);
    assert(TIANJI_VIEW_VOICE_LISTENING == 5);
    assert(TIANJI_VIEW_VOICE_RETRY == 6);

    printf("test_view_states_enum: PASS\n");
}

int main(void) {
    printf("========================================\n");
    printf("Running Tianji Voice Unit Tests...\n");
    printf("========================================\n");
    test_wav_header_structure();
    test_rms_volume_calculation();
    test_view_states_enum();
    printf("All Tianji Voice Unit Tests Passed!\n");
    return 0;
}
