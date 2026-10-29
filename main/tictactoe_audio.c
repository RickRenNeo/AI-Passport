// main/tictactoe_audio.c — Sound effects synthesis for Tic-Tac-Toe
#include "tictactoe_audio.h"
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

static const char *TAG = "ttt_audio";
static QueueHandle_t s_sfx_queue = NULL;
static bool s_audio_ready = false;

static void play_tone(int freq_hz, int duration_ms, int volume_scale) {
    if (!s_audio_ready || freq_hz <= 0 || duration_ms <= 0) return;

    int total_samples = (SAMPLE_RATE_HZ * duration_ms) / 1000;
    int16_t buffer[CHUNK_SAMPLES];
    float phase = 0.0f;
    float phase_step = (2.0f * (float)M_PI * (float)freq_hz) / (float)SAMPLE_RATE_HZ;
    float amplitude = (float)volume_scale * 16000.0f; // max int16 is 32767

    int generated = 0;
    while (generated < total_samples) {
        int chunk = total_samples - generated;
        if (chunk > CHUNK_SAMPLES) chunk = CHUNK_SAMPLES;

        for (int i = 0; i < chunk; i++) {
            // Apply slight trapezoidal envelope to avoid clicks
            float env = 1.0f;
            int current_sample = generated + i;
            if (current_sample < 80) {
                env = (float)current_sample / 80.0f;
            } else if (current_sample > total_samples - 80) {
                env = (float)(total_samples - current_sample) / 80.0f;
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
    ttt_sfx_t sfx;

    while (1) {
        if (xQueueReceive(s_sfx_queue, &sfx, portMAX_DELAY) == pdTRUE) {
            switch (sfx) {
            case TTT_SFX_MOVE:
                play_tone(880, 25, 1);
                break;
            case TTT_SFX_PLACE:
                play_tone(1175, 60, 1);
                break;
            case TTT_SFX_INVALID:
                play_tone(220, 120, 1);
                break;
            case TTT_SFX_WIN:
                play_tone(523, 70, 1);  // C5
                play_tone(659, 70, 1);  // E5
                play_tone(784, 70, 1);  // G5
                play_tone(1046, 140, 1); // C6
                break;
            case TTT_SFX_LOSE:
                play_tone(392, 90, 1);  // G4
                play_tone(330, 90, 1);  // E4
                play_tone(262, 140, 1); // C4
                break;
            case TTT_SFX_DRAW:
                play_tone(659, 60, 1);
                play_tone(784, 80, 1);
                break;
            default:
                break;
            }
        }
    }
}

void ttt_audio_init(void) {
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "Audio init skipped or failed, audio feedback disabled");
        return;
    }

    if (bsp_audio_set_format(SAMPLE_RATE_HZ, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set audio format");
        return;
    }

    bsp_audio_set_volume(80);

    s_sfx_queue = xQueueCreate(AUDIO_QUEUE_LEN, sizeof(ttt_sfx_t));
    if (!s_sfx_queue) {
        ESP_LOGE(TAG, "Failed to create sfx queue");
        return;
    }

    s_audio_ready = true;
    xTaskCreate(audio_worker_task, "ttt_audio", 2048, NULL, 4, NULL);
    ESP_LOGI(TAG, "Audio worker task started");
}

void ttt_audio_play(ttt_sfx_t sfx) {
    if (!s_audio_ready || !s_sfx_queue) return;
    xQueueSend(s_sfx_queue, &sfx, 0);
}
