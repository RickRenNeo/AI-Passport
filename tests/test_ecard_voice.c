#include "ecard_voice.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void test_adpcm_encode_decode_sine_wave(void) {
    // 生成一段 16kHz 正弦波测试 ADPCM 保真度
    int16_t orig_samples[160];
    for (int i = 0; i < 160; ++i) {
        orig_samples[i] = (int16_t)(10000.0 * sin(2.0 * 3.1415926 * 440.0 * i / 16000.0));
    }

    int16_t enc_pred = 0;
    int8_t enc_idx = 0;
    int16_t dec_pred = 0;
    int8_t dec_idx = 0;

    int32_t total_error = 0;
    for (int i = 0; i < 160; ++i) {
        uint8_t nibble = ecard_adpcm_encode_sample(orig_samples[i], &enc_pred, &enc_idx);
        int16_t decoded = ecard_adpcm_decode_sample(nibble, &dec_pred, &dec_idx);
        total_error += abs((int32_t)orig_samples[i] - (int32_t)decoded);
    }

    // 平均误差应当在一个合理的 ADPCM 动态量化噪声范围内 (如 < 500)
    int32_t avg_error = total_error / 160;
    assert(avg_error < 800);
}

static void test_recording_stream_and_stop(void) {
    ecard_voice_init();
    assert(ecard_voice_get_state() == ECARD_VOICE_IDLE);

    bool started = ecard_voice_start_recording();
    assert(started);
    assert(ecard_voice_get_state() == ECARD_VOICE_RECORDING);

    // 模拟录入 2 秒音频 (2 * 16000 = 32000 个样本)
    int16_t pcm_block[1600];
    for (int i = 0; i < 1600; ++i) {
        pcm_block[i] = 2000;
    }

    uint8_t rem_sec = 0;
    uint8_t amp = 0;

    // 写入 20 个 1600 样本块 (正好 2 秒)
    for (int b = 0; b < 20; ++b) {
        bool ok = ecard_voice_write_pcm(pcm_block, 1600, &rem_sec, &amp);
        assert(ok);
    }
    assert(rem_sec == 28); // 30 - 2 = 28秒剩余
    assert(amp > 0);

    // 停止录音
    const uint8_t *data = NULL;
    size_t len = 0;
    uint8_t duration = 0;
    bool stopped = ecard_voice_stop_recording(&data, &len, &duration);
    assert(stopped);
    assert(duration == 2);
    // 2 秒 ADPCM 对应 2 * 8000 = 16000 字节
    assert(len == 16000);
    assert(data != NULL);
}

static void test_recording_max_30s_cutoff(void) {
    ecard_voice_init();
    ecard_voice_start_recording();

    int16_t pcm_1sec[16000];
    for (int i = 0; i < 16000; ++i) pcm_1sec[i] = 100;

    uint8_t rem = 0;
    uint8_t amp = 0;

    // 连续写入 30 秒
    for (int s = 0; s < 30; ++s) {
        ecard_voice_write_pcm(pcm_1sec, 16000, &rem, &amp);
    }
    assert(rem == 0);

    // 再次写入应被截断
    bool more = ecard_voice_write_pcm(pcm_1sec, 16000, &rem, &amp);
    assert(!more);

    const uint8_t *data = NULL;
    size_t len = 0;
    uint8_t dur = 0;
    ecard_voice_stop_recording(&data, &len, &dur);
    assert(dur == 30);
    assert(len == ECARD_VOICE_MAX_BUFFER); // 240,000 字节
}

int main(void) {
    printf("Running ecard_voice tests...\n");
    test_adpcm_encode_decode_sine_wave();
    test_recording_stream_and_stop();
    test_recording_max_30s_cutoff();
    printf("ecard_voice tests passed!\n");
    return 0;
}
