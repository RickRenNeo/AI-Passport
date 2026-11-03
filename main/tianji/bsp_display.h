// main/tianji/bsp_display.h — LVGL Port Lock 兼容层
#pragma once

#include "esp_lvgl_port.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline bool bsp_lvgl_lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

static inline void bsp_lvgl_unlock(void) {
    lvgl_port_unlock();
}

#ifdef __cplusplus
}
#endif
