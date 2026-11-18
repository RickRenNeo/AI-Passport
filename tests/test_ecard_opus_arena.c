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

static void test_arena_borrow_and_mutex_with_codec(void) {
    assert(!ecard_opus_arena_busy());

    uint8_t *buf = NULL;
    size_t bytes = 0;
    assert(ecard_opus_arena_borrow(&buf, &bytes));
    assert(buf != NULL);
    assert(bytes == ecard_opus_arena_size());
    assert(ecard_opus_arena_busy());

    // 二次借用必须被拒绝；归还前全部字节可安全读写
    uint8_t *buf2 = NULL;
    size_t bytes2 = 0;
    assert(!ecard_opus_arena_borrow(&buf2, &bytes2));
    memset(buf, 0x7E, bytes);

    // 借出期间即使进入 encoder open 窗口，编码器也必须退回系统堆而不能踩坏录音缓冲
    ecard_opus_arena_begin();
    void *fallback = media_lib_module_malloc("enc", 24524);
    ecard_opus_arena_end();
    assert(fallback != NULL);
    assert(fallback != buf);
    for (size_t i = 0; i < bytes; ++i) {
        assert(buf[i] == 0x7E);
    }
    media_lib_free(fallback);

    // 归还后重新空闲
    ecard_opus_arena_return(buf);
    assert(!ecard_opus_arena_busy());

    // Opus 占用期间不允许外部借出，释放后可再次借出
    ecard_opus_arena_begin();
    assert(!ecard_opus_arena_borrow(&buf2, &bytes2));
    void *enc = media_lib_module_malloc("enc", 24524);
    ecard_opus_arena_end();
    assert(enc != NULL);
    assert(!ecard_opus_arena_borrow(&buf2, &bytes2));
    media_lib_free(enc);

    assert(ecard_opus_arena_borrow(&buf2, &bytes2));
    assert(buf2 == buf);
    ecard_opus_arena_return(buf2);
    assert(!ecard_opus_arena_busy());
}

static void test_arena_owns_and_portal_fallback_safety(void) {
    assert(!ecard_opus_arena_owns(NULL));

    // 模拟 esp_opus_dec_open：先 8 字节句柄走系统堆，再 17864 字节解码状态进后备区
    ecard_opus_arena_begin();
    void *dec_wrap = media_lib_module_calloc("OPUS_DEC", 1, 8);
    void *dec_state = media_lib_module_malloc("OPUS_DEC", 17864);
    ecard_opus_arena_end();
    assert(dec_wrap != NULL && !ecard_opus_arena_owns(dec_wrap));
    assert(dec_state != NULL && ecard_opus_arena_owns(dec_state));
    assert(ecard_opus_arena_busy());

    // 后备区忙碌时配网门户回退 malloc，归还非后备区指针不得破坏正在工作的解码器
    void *heap_html = calloc(1, 4096);
    assert(heap_html != NULL && !ecard_opus_arena_owns(heap_html));
    ecard_opus_arena_return(heap_html);
    assert(ecard_opus_arena_busy());
    free(heap_html);

    media_lib_free(dec_state);
    media_lib_free(dec_wrap);
    assert(!ecard_opus_arena_busy());

    // 解码器关闭后，配网门户可直接零堆开销借用后备区存放 4 KB HTML 页面
    uint8_t *arena_html = NULL;
    size_t cap = 0;
    assert(ecard_opus_arena_borrow(&arena_html, &cap));
    assert(cap >= 4096u && ecard_opus_arena_owns(arena_html));
    ecard_opus_arena_return(arena_html);
    assert(!ecard_opus_arena_busy());
}

int main(void) {
    printf("Running ecard_opus_arena tests...\n");
    test_arena_capacity_and_idle();
    test_arena_takes_encoder_request();
    test_arena_overflow_guard();
    test_arena_calloc();
    test_arena_decoder_encoder_alternation();
    test_arena_borrow_and_mutex_with_codec();
    test_arena_owns_and_portal_fallback_safety();
    printf("ecard_opus_arena tests passed!\n");
    return 0;
}
