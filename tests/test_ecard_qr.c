#include "ecard_qr.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool has_finder_at(const uint8_t *m, int size, int x0, int y0) {
    for (int dy = 0; dy < 7; ++dy) {
        for (int dx = 0; dx < 7; ++dx) {
            const bool border = (dx == 0 || dx == 6 || dy == 0 || dy == 6);
            const bool center = (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4);
            const uint8_t expect = (border || center) ? 1 : 0;
            if (m[(y0 + dy) * size + (x0 + dx)] != expect) {
                return false;
            }
        }
    }
    return true;
}

static void test_qr_encode_pairing_payload(void) {
    uint8_t modules[ECARD_QR_MAX_SIZE * ECARD_QR_MAX_SIZE];
    int size = 0;
    const char *payload =
        "{\"v\":1,\"id\":\"AABBCCDDEEFF\",\"code\":\"AB12CD34\","
        "\"ip\":\"192.168.123.123\",\"port\":80}";
    assert(strlen(payload) <= 90);

    bool ok = ecard_qr_encode(payload, modules, sizeof(modules), &size);
    assert(ok);
    assert(size >= 29 && size <= ECARD_QR_MAX_SIZE);
    assert((size - 17) % 4 == 0);

    /* 三个角 7x7 定位符 */
    assert(has_finder_at(modules, size, 0, 0));
    assert(has_finder_at(modules, size, size - 7, 0));
    assert(has_finder_at(modules, size, 0, size - 7));

    /* 左下角暗点 (8, 4*V+9) = (8, size - 8) */
    assert(modules[(size - 8) * size + 8] == 1);

    /* 同一输入应确定性生成完全一致的矩阵 */
    uint8_t modules2[ECARD_QR_MAX_SIZE * ECARD_QR_MAX_SIZE];
    int size2 = 0;
    assert(ecard_qr_encode(payload, modules2, sizeof(modules2), &size2));
    assert(size2 == size);
    assert(memcmp(modules, modules2, (size_t)(size * size)) == 0);
}

static void test_qr_encode_small_and_bounds(void) {
    uint8_t modules[ECARD_QR_MAX_SIZE * ECARD_QR_MAX_SIZE];
    int size = 0;

    assert(ecard_qr_encode("hello", modules, sizeof(modules), &size));
    assert(size == 21); /* Version 1 */
    assert(has_finder_at(modules, size, 0, 0));

    assert(!ecard_qr_encode(NULL, modules, sizeof(modules), &size));
    assert(!ecard_qr_encode("", modules, sizeof(modules), &size));
    assert(!ecard_qr_encode("hello", modules, 100, &size)); /* 太小放不下 21x21 */

    char too_long[160];
    memset(too_long, 'A', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    assert(!ecard_qr_encode(too_long, modules, sizeof(modules), &size));
}

static void test_qr_encode_packed_equivalence(void) {
    const char *payload =
        "{\"v\":1,\"id\":\"AABBCCDDEEFF\",\"code\":\"AB12CD34\","
        "\"ip\":\"192.168.123.123\",\"port\":80}";
    uint8_t modules[ECARD_QR_MAX_SIZE * ECARD_QR_MAX_SIZE];
    uint8_t packed[ECARD_QR_PACKED_BYTES];
    int size_unpacked = 0;
    int size_packed = 0;

    assert(ecard_qr_encode(payload, modules, sizeof(modules), &size_unpacked));
    assert(ecard_qr_encode_packed(payload, packed, sizeof(packed), &size_packed));
    assert(size_packed == size_unpacked);

    for (int r = 0; r < size_packed; ++r) {
        for (int c = 0; c < size_packed; ++c) {
            bool b = ecard_qr_bit(packed, size_packed, r, c);
            assert(b == (modules[r * size_unpacked + c] != 0));
        }
    }

    assert(!ecard_qr_bit(packed, size_packed, -1, 0));
    assert(!ecard_qr_bit(packed, size_packed, size_packed, 0));
    assert(!ecard_qr_encode_packed("hello", packed, 10, &size_packed));
    assert(!ecard_qr_encode_packed(NULL, packed, sizeof(packed), &size_packed));
}

int main(void) {
    printf("Running ecard_qr tests...\n");
    test_qr_encode_pairing_payload();
    test_qr_encode_small_and_bounds();
    test_qr_encode_packed_equivalence();
    printf("ecard_qr tests passed!\n");
    return 0;
}
