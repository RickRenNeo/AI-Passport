// main/ecard_opus_arena.c —— 给 Opus 编码器准备的一块静态连续内存
//
// 背景：官方 esp_audio_codec 通过弱符号 media_lib_module_malloc / calloc /
// realloc / media_lib_free 分配内部状态（反汇编确认：tag 参数被忽略，
// 分别转发到 malloc(size) / calloc(n, size) / realloc(ptr, size) / free(ptr)）。
// 在 ESP32-C3 上，TLS 握手与 Wi-Fi 缓冲把系统堆打碎后最大连续块只有 ~18 KB，
// 而 libopus 16 kHz 单声道编码器初始化实测需要 24524 字节的连续分配。
// 本文件用强符号覆盖这些弱符号：只在 begin()/end() 之间、且请求 ≥12 KB 时，
// 从静态后备区分出这一块；其它分配（包括 Opus 解码器的 9 KB 状态）仍走系统堆。

#include "ecard_opus_arena.h"

#include <esp_log.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "opus_arena";

/* 本次实测：16 kHz / 60 ms / VOIP / complexity=0 的 libopus 编码器会申请
 * 24524 字节。后备区设为 25 KB（25600 字节，留出 1076 字节余量），
 * 避免过多占用静态 DRAM 影响 TLS 握手内存；同时加硬性上限防越界。 */
#define ARENA_BYTES (25u * 1024u)
/* 只接管编码器那一块大分配；解码器（约 9 KB）仍走系统堆，避免互相抢。 */
#define ARENA_MIN_REQUEST (12u * 1024u)

static uint8_t s_arena[ARENA_BYTES] __attribute__((aligned(16)));

static bool s_active;       // 处于 esp_audio_enc_open 窗口内
static bool s_in_use;       // 后备区已被编码器拿走
static size_t s_used;       // 当前占用的字节数（单块，便于回收）
static void *s_owner;       // 后备区里那块内存的地址

static bool arena_contains(const void *ptr) {
    const uint8_t *p = (const uint8_t *)ptr;
    return p >= s_arena && p < s_arena + ARENA_BYTES;
}

void ecard_opus_arena_begin(void) {
    s_active = true;
}

void ecard_opus_arena_end(void) {
    s_active = false;
}

bool ecard_opus_arena_busy(void) {
    return s_in_use;
}

unsigned ecard_opus_arena_size(void) {
    return ARENA_BYTES;
}

/* ---- 覆盖 esp_audio_codec 的弱符号分配器 ------------------------------- */

void *media_lib_module_malloc(const char *module, size_t size);
void *media_lib_module_calloc(const char *module, size_t num, size_t size);
void *media_lib_module_realloc(const char *module, void *ptr, size_t size);
void media_lib_free(void *ptr);

void *media_lib_module_malloc(const char *module, size_t size) {
    (void)module;
    if (s_active && !s_in_use && size >= ARENA_MIN_REQUEST) {
        // 后备区是固定大小的静态数组：宁可直接失败，也不能返回一块装不下
        // 的内存，否则 libopus 会写穿到相邻的 LVGL 内存池并触发重启。
        if (size > ARENA_BYTES) {
            ESP_LOGW(TAG, "编码器申请 %u 字节超过后备区 %u，已拒绝",
                     (unsigned)size, (unsigned)ARENA_BYTES);
            return NULL;
        }
        /* 单块后备分配：编码器关闭后 free 会把整块还回来。 */
        s_owner = s_arena;
        s_used = size;
        s_in_use = true;
        ESP_LOGI(TAG, "编码器大块分配走后备区: %u 字节", (unsigned)size);
        return s_owner;
    }
    return malloc(size);
}

void *media_lib_module_calloc(const char *module, size_t num, size_t size) {
    const size_t total = num * size;
    void *ptr = media_lib_module_malloc(module, total);
    if (ptr) {
        /* 后备区是静态区，需要自己清零；malloc 出来的也必须清零。 */
        memset(ptr, 0, total);
    }
    return ptr;
}

void *media_lib_module_realloc(const char *module, void *ptr, size_t size) {
    if (!ptr) return media_lib_module_malloc(module, size);
    if (arena_contains(ptr)) {
        /* 后备区里的块无法原地扩展：搬到系统堆（后备区保持占用，
         * 等编码器整体关闭时再由 media_lib_free 释放）。 */
        void *fresh = malloc(size);
        if (fresh && s_used) {
            memcpy(fresh, ptr, s_used < size ? s_used : size);
        }
        return fresh;
    }
    return realloc(ptr, size);
}

void media_lib_free(void *ptr) {
    if (!ptr) return;
    if (arena_contains(ptr)) {
        if (ptr == s_owner) {
            s_in_use = false;
            s_owner = NULL;
            s_used = 0;
            ESP_LOGI(TAG, "编码器已释放后备区");
        }
        return; /* 静态区不需要 free */
    }
    free(ptr);
}
