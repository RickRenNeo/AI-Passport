#ifndef ECARD_OPUS_ARENA_H
#define ECARD_OPUS_ARENA_H

/*
 * esp_audio_codec 内部的 media_lib_* 分配器是弱符号，默认直接落到 malloc。
 * ESP32-C3（无 PSRAM）在跑完 Wi-Fi + TLS + Opus 解码后，系统堆的最大连续块
 * 只剩 18 KB 左右，而真机实测 libopus 编码器要一次性拿到 24524 字节，长按 UP
 * 必然 OPUS_ALLOC_FAIL(-7)。这里提供一块 25 KB 静态后备区，只在“打开编码器”
 * 这一瞬间接管 ≥12 KB 的分配，其余分配仍然走系统堆；超过后备区容量的请求
 * 会直接返回失败，绝不返回一块装不下的内存。
 *
 * 用法：ecard_opus_arena_begin() → esp_audio_enc_open() → ecard_opus_arena_end()。
 * 编码器关闭时会 free 掉这块内存，后备区自动回到“空闲”状态，可反复复用。
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 开始接管编码器的大块分配（只覆盖 esp_audio_enc_open 这一段）。 */
void ecard_opus_arena_begin(void);

/** 结束接管：之后的大块分配重新走系统堆。 */
void ecard_opus_arena_end(void);

/** 后备区是否正被编码器或外部借用占用（用于日志与诊断）。 */
bool ecard_opus_arena_busy(void);

/** 后备区容量（字节）。 */
unsigned ecard_opus_arena_size(void);

/**
 * @brief 在 Opus 编解码空闲时借用 25 KB 静态后备区（供首页留言录音复用，避免堆碎片）。
 *
 * @return true 借用成功；false 后备区正忙或处于编码器打开窗口
 */
bool ecard_opus_arena_borrow(uint8_t **out_buf, size_t *out_bytes);

/** 归还由 ecard_opus_arena_borrow() 借出的静态后备区。 */
void ecard_opus_arena_return(const void *buf);

#ifdef __cplusplus
}
#endif

#endif // ECARD_OPUS_ARENA_H
