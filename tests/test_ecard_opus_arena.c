#include "ecard_opus_arena.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *media_lib_module_malloc(const char *module, size_t size);
void *media_lib_module_calloc(const char *module, size_t num, size_t size);
void *media_lib_module_realloc(const char *module, void *ptr, size_t size);
void media_lib_free(void *ptr);

static void test_arena_capacity_and_idle(void) {
    assert(!ecard_opus_arena_busy());
    assert(ecard_opus_arena_size() == 25u * 1024u);
    assert(ecard_opus_arena_size() >= 24524u);

    // 未进入 begin() 时，大块分配走系统 malloc，后备区不被占用
    void *p = media_lib_module_malloc("enc", 24524);
    assert(p != NULL);
    assert(!ecard_opus_arena_busy());
    media_lib_free(p);
}

static void test_arena_takes_encoder_request(void) {
    ecard_opus_arena_begin();

    // 小于 12 KB 的分配走系统堆，不占用后备区
    void *small = media_lib_module_malloc("enc", 8192);
    assert(small != NULL);
    assert(!ecard_opus_arena_busy());
    media_lib_free(small);

    // 16 kHz / 60 ms / VOIP 编码器实测申请 24524 字节：由后备区接管
    void *enc = media_lib_module_malloc("enc", 24524);
    assert(enc != NULL);
    assert(ecard_opus_arena_busy());

    // 验证全量 24524 字节可安全读写，无崩溃
    memset(enc, 0x5A, 24524);
    for (size_t i = 0; i < 24524; ++i) {
        assert(((const uint8_t *)enc)[i] == 0x5A);
    }

    // 后备区只托管一块；使用期间的第二笔大分配回退到系统堆
    void *second = media_lib_module_malloc("enc", 16384);
    assert(second != NULL);
    assert(second != enc);
    assert(ecard_opus_arena_busy());
    media_lib_free(second);

    // 释放后备区
    media_lib_free(enc);
    assert(!ecard_opus_arena_busy());

    ecard_opus_arena_end();
}

static void test_arena_overflow_guard(void) {
    ecard_opus_arena_begin();
    assert(!ecard_opus_arena_busy());

    // 超过 ARENA_BYTES (25 KB) 的请求必须被硬性拒绝返回 NULL，
    // 绝不能返回一块装不下的缓冲区踩坏相邻的 LVGL 内存池。
    const size_t oversize = 26u * 1024u;
    void *huge = media_lib_module_malloc("enc", oversize);
    assert(huge == NULL);
    assert(!ecard_opus_arena_busy());

    ecard_opus_arena_end();
}

static void test_arena_calloc(void) {
    ecard_opus_arena_begin();

    void *zeroed = media_lib_module_calloc("enc", 1, 24524);
    assert(zeroed != NULL);
    assert(ecard_opus_arena_busy());

    // 验证静态区已被清零
    for (size_t i = 0; i < 24524; ++i) {
        assert(((const uint8_t *)zeroed)[i] == 0x00);
    }

    media_lib_free(zeroed);
    assert(!ecard_opus_arena_busy());

    ecard_opus_arena_end();
}

static void test_arena_decoder_encoder_alternation(void) {
    // 解码器约 18.5 KB、编码器 24524 B，互斥轮换复用同一块 25 KB 静态后备区
    ecard_opus_arena_begin();
    void *dec = media_lib_module_malloc("dec", 18500);
    ecard_opus_arena_end();
    assert(dec != NULL);
    assert(ecard_opus_arena_busy());
    memset(dec, 0xA5, 18500);

    media_lib_free(dec);
    assert(!ecard_opus_arena_busy());

    ecard_opus_arena_begin();
    void *enc = media_lib_module_malloc("enc", 24524);
    ecard_opus_arena_end();
    assert(enc == dec);
    assert(ecard_opus_arena_busy());
    memset(enc, 0x3C, 24524);

    media_lib_free(enc);
    assert(!ecard_opus_arena_busy());

    ecard_opus_arena_begin();
    void *dec2 = media_lib_module_malloc("dec", 18500);
    ecard_opus_arena_end();
    assert(dec2 == dec);
    assert(ecard_opus_arena_busy());
    media_lib_free(dec2);
    assert(!ecard_opus_arena_busy());
}

int main(void) {
    printf("Running ecard_opus_arena tests...\n");
    test_arena_capacity_and_idle();
    test_arena_takes_encoder_request();
    test_arena_overflow_guard();
    test_arena_calloc();
    test_arena_decoder_encoder_alternation();
    printf("ecard_opus_arena tests passed!\n");
    return 0;
}
