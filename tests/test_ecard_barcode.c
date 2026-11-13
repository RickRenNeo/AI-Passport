#include "ecard_barcode.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_encode_code128_basic(void) {
    uint8_t modules[ECARD_BARCODE_MAX_MODULES];
    size_t count = 0;

    // 正常测试学号
    bool ok = ecard_barcode_encode_code128("2026110508", modules, sizeof(modules), &count);
    assert(ok);
    // 起始静区(10) + 起始B(11) + 10字符*11 + 校验(11) + 终止(13) + 结束静区(10) = 165
    assert(count == 165);

    // 检查起始静区为全 0
    for (int i = 0; i < 10; ++i) {
        assert(modules[i] == 0);
    }
    // 起始符 104 = "211214" -> 2条 1空 1条 2空 1条 4空
    assert(modules[10] == 1 && modules[11] == 1); // 2 条
    assert(modules[12] == 0);                     // 1 空
    assert(modules[13] == 1);                     // 1 条
    assert(modules[14] == 0 && modules[15] == 0); // 2 空

    // 非法字符测试
    ok = ecard_barcode_encode_code128("测试中文", modules, sizeof(modules), &count);
    assert(!ok);

    // 空文本测试
    ok = ecard_barcode_encode_code128("", modules, sizeof(modules), &count);
    assert(!ok);

    // NULL 测试
    assert(!ecard_barcode_encode_code128(NULL, modules, sizeof(modules), &count));
}

static void test_encode_code128_auto(void) {
    uint8_t modules[ECARD_BARCODE_MAX_MODULES];
    size_t count = 0;
    size_t count_b = 0;

    // 偶数位纯数字 -> Code128C: 20 + 11 + 5*11 + 11 + 13 = 110
    bool ok = ecard_barcode_encode_code128_auto("2026110508", modules, sizeof(modules), &count);
    assert(ok);
    assert(count == 110);

    // 起始符 C (105) 的 "211232" 模式
    assert(modules[10] == 1 && modules[11] == 1);
    assert(modules[12] == 0);
    assert(modules[13] == 1);
    assert(modules[14] == 0 && modules[15] == 0);
    assert(modules[16] == 1 && modules[17] == 1 && modules[18] == 1);
    assert(modules[19] == 0 && modules[20] == 0);

    // 与 Code128B 相比明显更短
    ok = ecard_barcode_encode_code128("2026110508", modules, sizeof(modules), &count_b);
    assert(ok && count_b == 165);
    assert(count < count_b);

    // 奇数位数字回退到 Code128B
    ok = ecard_barcode_encode_code128_auto("202611050", modules, sizeof(modules), &count);
    assert(ok && count == 154);

    // 含字母回退到 Code128B
    ok = ecard_barcode_encode_code128_auto("A2026", modules, sizeof(modules), &count);
    assert(ok && count == 20 + 11 + 5 * 11 + 11 + 13);

    // 边界与错误输入
    assert(!ecard_barcode_encode_code128_auto("", modules, sizeof(modules), &count));
    assert(!ecard_barcode_encode_code128_auto(NULL, modules, sizeof(modules), &count));
    assert(!ecard_barcode_encode_code128_auto("2026110508", modules, 100, &count));
    assert(!ecard_barcode_encode_code128_auto("1234567890123456789012345678901234",
                                              modules, sizeof(modules), &count));
}

static void test_render_vertical_rgb565(void) {
    uint16_t buffer[80 * 175];
    memset(buffer, 0, sizeof(buffer));

    bool ok = ecard_barcode_render_vertical_rgb565("2026110508", buffer, 80, 175, 0x0000, 0xFFFF);
    assert(ok);

    // 检查四周背景是否为白色 (0xFFFF)
    assert(buffer[0] == 0xFFFF);
    assert(buffer[79] == 0xFFFF);

    // 检查是否有黑色条码线 (0x0000)
    int black_pixel_count = 0;
    for (int i = 0; i < 80 * 175; ++i) {
        if (buffer[i] == 0x0000) {
            black_pixel_count++;
        }
    }
    assert(black_pixel_count > 0);
}

int main(void) {
    printf("Running ecard_barcode tests...\n");
    test_encode_code128_basic();
    test_encode_code128_auto();
    test_render_vertical_rgb565();
    printf("ecard_barcode tests passed!\n");
    return 0;
}
