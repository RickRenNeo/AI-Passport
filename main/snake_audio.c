#include "snake_audio.h"
#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <math.h>
#include <stdint.h>

#define SAMPLE_RATE_HZ 16000
#define AUDIO_QUEUE_LEN 4
#define CHUNK_SAMPLES 128

static const char *TAG = "snake_audio";
static QueueHandle_t s_sfx_queue = NULL;
static bool s_audio_ready = false;

static void play_tone(int freq_hz, int duration_ms, int volume_scale) {
    if (!s_audio_ready || freq_hz <= 0 || duration_ms <= 0) return;

    int total_samples = (SAMPLE_RATE_HZ * duration_ms) / 1000;
    int16_t buffer[CHUNK_SAMPLES];
    float phase = 0.0f;
    float phase_step = (2.0f * (float)M_PI * (float)freq_hz) / (float)SAMPLE_RATE_HZ;
    float amplitude = (float)volume_scale * 16000.0f;

    int generated = 0;
    while (generated < total_samples) {
        int chunk = total_samples - generated;
        if (chunk > CHUNK_SAMPLES) chunk = CHUNK_SAMPLES;

        for (int i = 0; i < chunk; i++) {
            float env = 1.0f;
            int current_sample = generated + i;
            if (current_sample < 60) {
                env = (float)current_sample / 60.0f;
            } else if (current_sample > total_samples - 60) {
                env = (float)(total_samples - current_sample) / 60.0f;
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

static void audio_worker_task(void *arg) {
    (void)arg;
    snake_sfx_t sfx;

    while (1) {
        if (xQueueReceive(s_sfx_queue, &sfx, portMAX_DELAY) == pdTRUE) {
            switch (sfx) {
            case SNAKE_SFX_START:
                play_tone(523, 50, 1);
                play_tone(659, 50, 1);
                play_tone(784, 80, 1);
                break;
            case SNAKE_SFX_TURN:
                play_tone(880, 15, 1);
                break;
            case SNAKE_SFX_EAT:
                play_tone(587, 30, 1);
                play_tone(880, 45, 1);
                break;
            case SNAKE_SFX_BONUS:
                play_tone(659, 40, 1);
                play_tone(784, 40, 1);
                play_tone(1046, 90, 1);
                break;
            case SNAKE_SFX_DIE:
                play_tone(440, 70, 1);
                play_tone(220, 80, 1);
                play_tone(110, 140, 1);
                break;
            case SNAKE_SFX_PAUSE:
                play_tone(440, 30, 1);
                play_tone(330, 30, 1);
                break;
            default:
                break;
            }
        }
    }
}

void snake_audio_init(void) {
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "Audio init skipped or failed; running silently");
        return;
    }

    if (bsp_audio_set_format(SAMPLE_RATE_HZ, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "Audio set format failed");
        return;
    }

    bsp_audio_set_volume(80);

    s_sfx_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(snake_sfx_t));
    if (!s_sfx_queue) {
        ESP_LOGE(TAG, "Failed to create audio queue");
        return;
    }

    s_audio_ready = true;
    xTaskCreate(audio_worker_task, "snake_audio", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "Snake audio worker ready");
}

void snake_audio_play(snake_sfx_t sfx) {
    if (!s_audio_ready || !s_sfx_queue) return;
    xQueueSend(s_sfx_queue, &sfx, 0);
}
