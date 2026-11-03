// main/tianji_audio.c — 天机·六爻: 仪式音效系统实现
#include "tianji_audio.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include <stdint.h>

#define SAMPLE_RATE_HZ   16000
#define AUDIO_QUEUE_LEN  8
#define CHUNK_SAMPLES    128

static const char *TAG = "tianji_audio";
static QueueHandle_t s_sfx_queue = NULL;
static bool s_audio_ready = false;

// 基础单音发生器
static void play_tone(int freq_hz, int duration_ms, float volume_scale) {
    if (!s_audio_ready || freq_hz <= 0 || duration_ms <= 0) return;

    int total_samples = (SAMPLE_RATE_HZ * duration_ms) / 1000;
    int16_t buffer[CHUNK_SAMPLES];
    float phase = 0.0f;
    float phase_step = (2.0f * (float)M_PI * (float)freq_hz) / (float)SAMPLE_RATE_HZ;
    float amplitude = volume_scale * 16000.0f;

    int generated = 0;
    while (generated < total_samples) {
        int chunk = total_samples - generated;
        if (chunk > CHUNK_SAMPLES) chunk = CHUNK_SAMPLES;

        for (int i = 0; i < chunk; i++) {
            float env = 1.0f;
            int current_sample = generated + i;
            if (current_sample < 40) {
                env = (float)current_sample / 40.0f;
            } else if (current_sample > total_samples - 40) {
                env = (float)(total_samples - current_sample) / 40.0f;
            }

            buffer[i] = (int16_t)(sinf(phase) * amplitude * env);
            phase += phase_step;
            if (phase >= 2.0f * (float)M_PI) {
                phase -= 2.0f * (float)M_PI;
            }
        }

        bsp_audio_write(buffer, chunk * sizeof(int16_t));
        generated += chunk;
    }
}

// 铜钱掷落拟真合成音 (模拟 3 枚铜钱碰撞回响)
static void play_coin_rattle(void) {
    if (!s_audio_ready) return;
    // 快速多重清脆金属撞击模拟
    play_tone(1960, 20, 0.8f);
    vTaskDelay(pdMS_TO_TICKS(15));
    play_tone(2480, 25, 0.9f);
    vTaskDelay(pdMS_TO_TICKS(10));
    play_tone(1760, 30, 0.7f);
    vTaskDelay(pdMS_TO_TICKS(12));
    play_tone(2200, 35, 0.6f);
}

// 爻象生成清脆点亮音
static void play_line_reveal(void) {
    play_tone(880, 40, 0.8f);
    play_tone(1320, 60, 0.9f);
}

// 六爻成卦通明磬声/科技定卦声
static void play_hex_complete(void) {
    play_tone(523, 60, 0.8f);
    play_tone(659, 60, 0.85f);
    play_tone(784, 80, 0.9f);
    play_tone(1046, 140, 1.0f);
}

static void audio_worker_task(void *arg) {
    (void)arg;
    tianji_sfx_t sfx;

    while (1) {
        if (xQueueReceive(s_sfx_queue, &sfx, portMAX_DELAY) == pdTRUE) {
            switch (sfx) {
            case TIANJI_SFX_START:
                play_tone(440, 50, 0.7f);
                play_tone(660, 80, 0.85f);
                break;
            case TIANJI_SFX_TOSS:
                play_coin_rattle();
                break;
            case TIANJI_SFX_LINE_REVEAL:
                play_line_reveal();
                break;
            case TIANJI_SFX_HEX_COMPLETE:
                play_hex_complete();
                break;
            case TIANJI_SFX_PAGE_FLIP:
                play_tone(1175, 18, 0.6f);
                break;
            default:
                break;
            }
        }
    }
}

void tianji_audio_init(void) {
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "Audio hardware init skipped or failed; running silently");
        return;
    }

    if (bsp_audio_set_format(SAMPLE_RATE_HZ, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "Audio format set failed");
        return;
    }

    bsp_audio_set_volume(85);

    s_sfx_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(tianji_sfx_t));
    if (!s_sfx_queue) {
        ESP_LOGE(TAG, "Failed to create audio queue");
        return;
    }

    s_audio_ready = true;
    xTaskCreate(audio_worker_task, "tianji_audio", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "Tianji audio worker task initialized successfully");
}

void tianji_audio_play(tianji_sfx_t sfx) {
    if (!s_audio_ready || !s_sfx_queue) return;
    xQueueSend(s_sfx_queue, &sfx, 0);
}
