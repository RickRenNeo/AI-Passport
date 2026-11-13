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

    // 平均误差应当在一个合理的 ADPCM 动态量化噪声范围内 (如 < 800)
    int32_t avg_error = total_error / 160;
    assert(avg_error < 800);
}

static void test_recording_stream_and_stop(void) {
    ecard_voice_init();
    assert(ecard_voice_get_state() == ECARD_VOICE_IDLE);

    bool started = ecard_voice_start_recording();
    assert(started);
    assert(ecard_voice_get_state() == ECARD_VOICE_RECORDING);
    // 实际录音上限 10 秒 = 40 KB（再长会和小智 TLS/Opus 抢堆）
    assert(ecard_voice_buffer_capacity() ==
           ECARD_VOICE_RECORD_SEC * ECARD_VOICE_BYTES_PER_SEC);

    // 模拟录入 2 秒音频 (2 * 16000 = 32000 个样本)
    int16_t pcm_block[1600];
    for (int i = 0; i < 1600; ++i) {
        pcm_block[i] = (int16_t)(2000 + (i % 40) * 50);
    }

    uint8_t rem_sec = 0;
    uint8_t amp = 0;

    // 写入 20 个 1600 样本块 (正好 2 秒)
    for (int b = 0; b < 20; ++b) {
        bool ok = ecard_voice_write_pcm(pcm_block, 1600, &rem_sec, &amp);
        assert(ok);
    }
    assert(rem_sec == ECARD_VOICE_RECORD_SEC - 2);
    assert(amp > 0);

    // 停止录音
    const uint8_t *data = NULL;
    size_t len = 0;
    uint8_t duration = 0;
    bool stopped = ecard_voice_stop_recording(&data, &len, &duration);
    assert(stopped);
    assert(duration == 2);
    assert(ecard_voice_peak_level() >= 2000);
    // 8kHz 4-bit 编码: 2 秒 = 2 * 4000 字节
    assert(len == 2 * ECARD_VOICE_BYTES_PER_SEC);
    assert(data != NULL);
    assert(ecard_voice_has_clip());
}

static void test_recording_max_buffer_cutoff(void) {
    ecard_voice_init();
    assert(ecard_voice_start_recording());
    const size_t capacity = ecard_voice_buffer_capacity();
    assert(capacity == ECARD_VOICE_RECORD_SEC * ECARD_VOICE_BYTES_PER_SEC);

    int16_t pcm_block[1600];
    for (int i = 0; i < 1600; ++i) pcm_block[i] = 100;

    uint8_t rem = 0;
    uint8_t amp = 0;

    // 连续写入直到录满 10 秒上限
    bool still_recording = true;
    for (int b = 0; b < 400 && still_recording; ++b) {
        still_recording = ecard_voice_write_pcm(pcm_block, 1600, &rem, &amp);
    }
    assert(!still_recording);

    // 再次写入应被拒绝
    bool more = ecard_voice_write_pcm(pcm_block, 1600, &rem, &amp);
    assert(!more);

    const uint8_t *data = NULL;
    size_t len = 0;
    uint8_t dur = 0;
    ecard_voice_stop_recording(&data, &len, &dur);
    assert(len == capacity);
    assert(dur == ECARD_VOICE_RECORD_SEC);
}

static void test_playback_round_trip(void) {
    ecard_voice_init();
    assert(ecard_voice_start_recording());

    int16_t pcm_block[320];
    uint8_t rem = 0;
    uint8_t amp = 0;
    for (int b = 0; b < 50; ++b) { // 1 秒
        for (int i = 0; i < 320; ++i) {
            pcm_block[i] = (int16_t)(4000.0 * sin(2.0 * 3.1415926 * 300.0 * (b * 320 + i) / 16000.0));
        }
        assert(ecard_voice_write_pcm(pcm_block, 320, &rem, &amp));
    }

    const uint8_t *data = NULL;
    size_t len = 0;
    uint8_t dur = 0;
    assert(ecard_voice_stop_recording(&data, &len, &dur));
    assert(dur == 1);
    assert(len == ECARD_VOICE_BYTES_PER_SEC);

    assert(ecard_voice_playback_start());
    assert(ecard_voice_get_state() == ECARD_VOICE_PLAYING);

    int16_t out[160];
    size_t total = 0;
    int peak = 0;
    for (;;) {
        size_t n = ecard_voice_playback_read(out, 160);
        if (n == 0) break;
        for (size_t i = 0; i < n; ++i) {
            int v = abs((int)out[i]);
            if (v > peak) peak = v;
        }
        total += n;
        assert(total <= 4 * len + 8);
    }

    // 8 kHz 存档每个半字节对应一个样本，回放时每个样本重复一次 → 4 倍字节数
    assert(total == 4 * len);
    // 解码后的波形必须真的有幅度，而不是全零
    assert(peak > 500);
    assert(ecard_voice_get_state() == ECARD_VOICE_IDLE);

    // 缓冲释放后不能再回放
    ecard_voice_clip_release();
    assert(!ecard_voice_has_clip());
    assert(!ecard_voice_playback_start());
}

static void test_playback_stop_and_silence_guard(void) {
    ecard_voice_init();
    // 没有录音时不能进入回放
    assert(!ecard_voice_playback_start());

    int16_t out[16];
    assert(ecard_voice_playback_read(out, 16) == 0);

    assert(ecard_voice_start_recording());
    int16_t block[2] = {0, 0};
    uint8_t rem = 0;
    uint8_t amp = 0;
    assert(ecard_voice_write_pcm(block, 2, &rem, &amp));
    ecard_voice_stop_recording(NULL, NULL, NULL);

    assert(ecard_voice_playback_start());
    ecard_voice_playback_stop();
    assert(ecard_voice_get_state() == ECARD_VOICE_IDLE);
    assert(ecard_voice_playback_read(out, 16) == 0);
}

static void test_demo_voice_playback(void) {
    ecard_voice_init();
    ecard_voice_clip_release();

    // 网络留言没有音频负载时用内置演示语音，必须有真实时长与幅度
    assert(ecard_voice_demo_duration_sec() > 0);
    assert(ecard_voice_playback_start_demo());
    assert(ecard_voice_get_state() == ECARD_VOICE_PLAYING);

    int16_t out[160];
    size_t total = 0;
    int peak = 0;
    for (;;) {
        size_t n = ecard_voice_playback_read(out, 160);
        if (n == 0) break;
        for (size_t i = 0; i < n; ++i) {
            int v = abs((int)out[i]);
            if (v > peak) peak = v;
        }
        total += n;
        assert(total <= (size_t)(ecard_voice_demo_duration_sec() + 1) * ECARD_VOICE_SAMPLE_RATE);
    }
    assert(total > ECARD_VOICE_SAMPLE_RATE);
    assert(peak > 500);
    assert(ecard_voice_get_state() == ECARD_VOICE_IDLE);
}

int main(void) {
    printf("Running ecard_voice tests...\n");
    test_adpcm_encode_decode_sine_wave();
    test_recording_stream_and_stop();
    test_recording_max_buffer_cutoff();
    test_playback_round_trip();
    test_playback_stop_and_silence_guard();
    test_demo_voice_playback();
    printf("ecard_voice tests passed!\n");
    return 0;
}
