#include "ecard_barcode.h"
#include <string.h>

// Code128 条空宽度模式表 (索引 0..106)
// 每个符号占 11 个模块，用 6 个宽度的十进制数表示，例如 "212222" (条2 空1 条2 空2 条2 空2)
// 终止符 (106) 占 13 个模块，有 7 个条空。
static const char *const k_code128_patterns[107] = {
    "212222", "222122", "222221", "121223", "121322", "131222", "122213", "122312", "132212", "221213",
    "221312", "231212", "112232", "122132", "122231", "113222", "123122", "123221", "223211", "221132",
    "221231", "213212", "223112", "312131", "311222", "321122", "321221", "312212", "322112", "322211",
    "212123", "212321", "232121", "111323", "131123", "131321", "112313", "132113", "132311", "211313",
    "231113", "231311", "112133", "112331", "132131", "113123", "113321", "133121", "313121", "211331",
    "231131", "213113", "213311", "213131", "311123", "311321", "331121", "312113", "312311", "332111",
    "314111", "221411", "431111", "111224", "111422", "121124", "121421", "141122", "141221", "112214",
    "112412", "122114", "122411", "142112", "142211", "241211", "221114", "413111", "241112", "134111",
    "111242", "121142", "121241", "114212", "124112", "124211", "411212", "421112", "421211", "212141",
    "214121", "412121", "111143", "111341", "131141", "114113", "114311", "411113", "411311", "113141",
    "114131", "311141", "411131",
    "211412", // 103: Start A
    "211214", // 104: Start B
    "211232", // 105: Start C
    "2331112" // 106: Stop (7 元素, 13 模块)
};

static void append_pattern(const char *pat, uint8_t *modules, size_t *offset) {
    bool is_bar = true;
    for (size_t i = 0; pat[i] != '\0'; ++i) {
        int width = pat[i] - '0';
        for (int w = 0; w < width; ++w) {
            modules[(*offset)++] = is_bar ? 1 : 0;
        }
        is_bar = !is_bar;
    }
}

bool ecard_barcode_encode_code128(const char *text, uint8_t *modules, size_t max_modules, size_t *out_count) {
    if (!text || !modules || !out_count) return false;
    size_t len = strlen(text);
    if (len == 0 || len > 32) return false;

    // 检查字符集 (Code128B 支持 ASCII 32..126)
    for (size_t i = 0; i < len; ++i) {
        if ((unsigned char)text[i] < 32 || (unsigned char)text[i] > 126) {
            return false;
        }
    }

    // 计算所需模块数：
    // 起始符(11) + 每个字符(11 * len) + 校验码(11) + 终止符(13) + 静区两端(各10)
    size_t needed = 20 + 11 + (len * 11) + 11 + 13;
    if (needed > max_modules) return false;

    size_t offset = 0;

    // 起始静区 (10 个空白模块)
    for (int i = 0; i < 10; ++i) {
        modules[offset++] = 0;
    }

    // 起始符 B (代码值 104)
    append_pattern(k_code128_patterns[104], modules, &offset);

    // 校验和初始值
    uint32_t checksum = 104;

    // 编码每个字符
    for (size_t i = 0; i < len; ++i) {
        int code_val = text[i] - 32;
        append_pattern(k_code128_patterns[code_val], modules, &offset);
        checksum += (uint32_t)(i + 1) * (uint32_t)code_val;
    }

    // 校验符
    uint32_t check_val = checksum % 103;
    append_pattern(k_code128_patterns[check_val], modules, &offset);

    // 终止符
    append_pattern(k_code128_patterns[106], modules, &offset);

    // 结束静区 (10 个空白模块)
    for (int i = 0; i < 10; ++i) {
        modules[offset++] = 0;
    }

    *out_count = offset;
    return true;
}

bool ecard_barcode_render_vertical_rgb565(const char *text,
                                         uint16_t *dst_rgb565,
                                         int width,
                                         int height,
                                         uint16_t bar_color,
                                         uint16_t bg_color) {
    if (!text || !dst_rgb565 || width <= 0 || height <= 0) return false;

    uint8_t modules[ECARD_BARCODE_MAX_MODULES];
    size_t module_count = 0;
    if (!ecard_barcode_encode_code128(text, modules, sizeof(modules), &module_count)) {
        return false;
    }

    // 底色填充
    for (int i = 0; i < width * height; ++i) {
        dst_rgb565[i] = bg_color;
    }

    // 条码沿垂直方向（Y轴）展开，X轴为线条粗细/宽度
    // 模块沿 Y 轴等比缩放分布
    if ((int)module_count > height) {
        return false; // 高度不够容纳
    }

    // 计算 Y 轴上下居中偏移
    int y_start = (height - (int)module_count) / 2;
    if (y_start < 0) y_start = 0;

    // 条码线条左右两侧留出边距 (如各留 6px 留白)
    int margin_x = 4;
    int bar_w = width - (margin_x * 2);
    if (bar_w <= 0) bar_w = width;

    for (size_t m = 0; m < module_count; ++m) {
        int y = y_start + (int)m;
        if (y >= height) break;

        if (modules[m] == 1) { // 黑色线条
            uint16_t *row = &dst_rgb565[y * width];
            for (int x = margin_x; x < margin_x + bar_w && x < width; ++x) {
                row[x] = bar_color;
            }
        }
    }

    return true;
}
