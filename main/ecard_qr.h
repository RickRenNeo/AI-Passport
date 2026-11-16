#ifndef ECARD_QR_H
#define ECARD_QR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 支持 QR Version 1..6 (最大 41x41 模块，ECC-L Byte 模式最多 134 字节) */
#define ECARD_QR_MAX_SIZE 41
#define ECARD_QR_PACKED_BYTES (((ECARD_QR_MAX_SIZE * ECARD_QR_MAX_SIZE) + 7) / 8)

/**
 * @brief 将 UTF-8/ASCII 文本编码为位压缩 QR 矩阵（1 bit/模块，行优先 MSB 在低位或索引顺序）。
 */
bool ecard_qr_encode_packed(const char *text, uint8_t *packed, size_t packed_cap,
                            int *out_size);

static inline bool ecard_qr_bit(const uint8_t *packed, int size, int r, int c) {
    if (!packed || r < 0 || c < 0 || r >= size || c >= size) return false;
    const size_t idx = (size_t)r * (size_t)size + (size_t)c;
    return ((packed[idx >> 3] >> (idx & 7u)) & 1u) != 0;
}

/**
 * @brief 将 UTF-8/ASCII 文本编码为 QR 矩阵（Byte 模式，ECC-L）。
 *
 * @param text        输入字符串（1..134 字节）
 * @param modules     输出模块数组（1=黑，0=白），行优先存储 size * size 字节
 * @param max_modules modules 缓冲区容量（至少 size * size）
 * @param out_size    输出矩阵边长（21/25/29/33/37/41）
 * @return true 编码成功；false 参数非法或超长
 */
bool ecard_qr_encode(const char *text, uint8_t *modules, size_t max_modules,
                     int *out_size);

#ifdef __cplusplus
}
#endif

#endif // ECARD_QR_H
