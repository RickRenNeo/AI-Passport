#ifndef ECARD_BARCODE_H
#define ECARD_BARCODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECARD_BARCODE_MAX_MODULES 256

/**
 * @brief 编码 ASCII 文本为 Code128 模式 B 的模块条码数据 (0 = 空白, 1 = 黑色线条)
 *
 * @param text 输入文本 (通常是学工号，如 "2026110508")
 * @param modules 输出缓冲区，每个元素表示一个 module (0 或 1)
 * @param max_modules 缓冲区大小
 * @param out_count 实际输出的 module 总数量 (含起始符、字符数据、校验码、终止符及静区)
 * @return true 编码成功, false 文本过长或包含不支持字符
 */
bool ecard_barcode_encode_code128(const char *text, uint8_t *modules, size_t max_modules, size_t *out_count);

/**
 * @brief 自动选择最优 Code128 子集编码学工号
 *
 * 偶数位纯数字使用 Code128C (每两个数字一个符号，条码更短更易扫描)；
 * 其他情况回退到 Code128B。输出格式与 ecard_barcode_encode_code128 一致。
 *
 * @param text 输入文本 (通常是学工号)
 * @param modules 输出模块缓冲区 (0 = 空白, 1 = 黑色线条)
 * @param max_modules 缓冲区大小
 * @param out_count 实际输出的 module 总数量 (含静区)
 * @return true 编码成功, false 文本过长或包含不支持字符
 */
bool ecard_barcode_encode_code128_auto(const char *text, uint8_t *modules, size_t max_modules, size_t *out_count);

/**
 * @brief 将条码渲染为旋转90度的纵向 RGB565 位图 (用于工牌左侧区域显示)
 *
 * @param text 输入学工号
 * @param dst_rgb565 输出 RGB565 目标像素缓冲区
 * @param width 目标宽度 (如 80)
 * @param height 目标高度 (如 175)
 * @param bar_color 条码颜色 (如 0x0000 黑色)
 * @param bg_color 背景颜色 (如 0xFFFF 白色)
 * @return true 成功, false 失败
 */
bool ecard_barcode_render_vertical_rgb565(const char *text,
                                         uint16_t *dst_rgb565,
                                         int width,
                                         int height,
                                         uint16_t bar_color,
                                         uint16_t bg_color);

#ifdef __cplusplus
}
#endif

#endif // ECARD_BARCODE_H
