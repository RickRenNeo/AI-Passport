// main/tianji/bsp_audio.h — Audio Codec 兼容层
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline esp_err_t bsp_audio_init(void) {
    return ESP_OK;
}

static inline esp_err_t bsp_audio_set_format(uint32_t rate, uint8_t bits, uint8_t ch) {
    (void)rate;
    (void)bits;
    (void)ch;
    return ESP_OK;
}

esp_err_t bsp_audio_write(const void *data, size_t size);
esp_err_t bsp_audio_set_volume(uint8_t volume);

#ifdef __cplusplus
}
#endif
