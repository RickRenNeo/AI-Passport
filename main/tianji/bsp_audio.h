// main/tianji/bsp_audio.h — Audio Codec 兼容层
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_audio_write(const void *data, size_t size);
esp_err_t bsp_audio_set_volume(uint8_t volume);

#ifdef __cplusplus
}
#endif
