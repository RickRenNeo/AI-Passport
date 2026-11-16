#include "ecard_qr.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t version;
    uint8_t size;
    uint8_t total_data;
    uint8_t num_blocks;
    uint8_t block_data;
    uint8_t block_ecc;
    uint8_t max_bytes;
} qr_spec_t;

static const qr_spec_t k_specs[6] = {
    {1, 21,  19, 1, 19,  7,  17},
    {2, 25,  34, 1, 34, 10,  32},
    {3, 29,  55, 1, 55, 15,  53},
    {4, 33,  80, 1, 80, 20,  78},
    {5, 37, 108, 1, 108, 26, 106},
    {6, 41, 136, 2, 68, 18, 134},
};

static uint8_t gf_mul(uint8_t x, uint8_t y) {
    uint8_t r = 0;
    for (int i = 0; i < 8; ++i) {
        if (y & 1u) r ^= x;
        const bool hi = (x & 0x80u) != 0;
        x <<= 1;
        if (hi) x ^= 0x1Du; /* x^8 + x^4 + x^3 + x^2 + 1 */
        y >>= 1;
    }
    return r;
}

static void rs_compute_ecc(const uint8_t *data, size_t data_len,
                           uint8_t *ecc, size_t ecc_len) {
    uint8_t gen[32];
    memset(gen, 0, sizeof(gen));
    gen[0] = 1;
    uint8_t root = 1;
    for (size_t i = 0; i < ecc_len; ++i) {
        for (size_t j = i + 1; j > 0; --j) {
            gen[j] = gen[j - 1] ^ gf_mul(gen[j], root);
        }
        gen[0] = gf_mul(gen[0], root);
        root = gf_mul(root, 2);
    }

    memset(ecc, 0, ecc_len);
    for (size_t i = 0; i < data_len; ++i) {
        const uint8_t factor = data[i] ^ ecc[0];
        memmove(&ecc[0], &ecc[1], ecc_len - 1);
        ecc[ecc_len - 1] = 0;
        for (size_t j = 0; j < ecc_len; ++j) {
            ecc[j] ^= gf_mul(gen[ecc_len - 1 - j], factor);
        }
    }
}

static inline uint8_t m_get(const uint8_t *buf, int size, int x, int y) {
    const size_t idx = (size_t)y * (size_t)size + (size_t)x;
    return (uint8_t)((buf[idx >> 3] >> (idx & 7u)) & 1u);
}

static inline void m_set(uint8_t *buf, int size, int x, int y, uint8_t val) {
    const size_t idx = (size_t)y * (size_t)size + (size_t)x;
    const uint8_t mask = (uint8_t)(1u << (idx & 7u));
    if (val) {
        buf[idx >> 3] |= mask;
    } else {
        buf[idx >> 3] &= (uint8_t)~mask;
    }
}

static void set_func(uint8_t *mods, uint8_t *is_func, int size,
                     int x, int y, uint8_t dark) {
    if (x < 0 || y < 0 || x >= size || y >= size) return;
    m_set(mods, size, x, y, dark ? 1u : 0u);
    m_set(is_func, size, x, y, 1u);
}

static void draw_finder(uint8_t *mods, uint8_t *is_func, int size, int x0, int y0) {
    for (int dy = -1; dy <= 7; ++dy) {
        for (int dx = -1; dx <= 7; ++dx) {
            const int x = x0 + dx;
            const int y = y0 + dy;
            if (x < 0 || y < 0 || x >= size || y >= size) continue;
            uint8_t dark = 0;
            if (dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6) {
                const bool border = (dx == 0 || dx == 6 || dy == 0 || dy == 6);
                const bool center = (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4);
                dark = (border || center) ? 1u : 0u;
            }
            set_func(mods, is_func, size, x, y, dark);
        }
    }
}

static void draw_alignment(uint8_t *mods, uint8_t *is_func, int size, int cx, int cy) {
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int dist = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);
            set_func(mods, is_func, size, cx + dx, cy + dy, (dist != 1) ? 1u : 0u);
        }
    }
}

static void draw_function_patterns(uint8_t *mods, uint8_t *is_func,
                                   const qr_spec_t *spec, size_t packed_bytes) {
    const int size = spec->size;
    memset(mods, 0, packed_bytes);
    memset(is_func, 0, packed_bytes);

    draw_finder(mods, is_func, size, 0, 0);
    draw_finder(mods, is_func, size, size - 7, 0);
    draw_finder(mods, is_func, size, 0, size - 7);

    /* 时序线 */
    for (int i = 8; i < size - 8; ++i) {
        const uint8_t dark = (i % 2 == 0) ? 1u : 0u;
        set_func(mods, is_func, size, i, 6, dark);
        set_func(mods, is_func, size, 6, i, dark);
    }

    /* Version 2..6 仅右下角存在一个校正图形 */
    if (spec->version >= 2) {
        draw_alignment(mods, is_func, size, size - 7, size - 7);
    }

    /* 先占位格式信息区（占位后统一写入具体 mask 的格式位） */
    for (int i = 0; i <= 8; ++i) {
        if (i != 6) {
            set_func(mods, is_func, size, 8, i, 0);
            set_func(mods, is_func, size, i, 8, 0);
        }
    }
    for (int i = 0; i < 8; ++i) {
        set_func(mods, is_func, size, size - 1 - i, 8, 0);
        if (i < 7) {
            set_func(mods, is_func, size, 8, size - 1 - i, 0);
        }
    }
    /* 固定暗点 (8, 4*V + 9) == (8, size - 8) */
    set_func(mods, is_func, size, 8, size - 8, 1);
}

static void draw_format_bits(uint8_t *mods, int size, int mask) {
    /* ECC-L = 01b */
    const uint32_t data = (1u << 3) | (uint32_t)(mask & 7);
    uint32_t rem = data << 10;
    for (int i = 4; i >= 0; --i) {
        if (rem & (1u << (i + 10))) {
            rem ^= (0x537u << i);
        }
    }
    const uint32_t bits = ((data << 10) | rem) ^ 0x5412u;

    /* 副本 1：左上定标符周围 */
    for (int i = 0; i <= 5; ++i) {
        m_set(mods, size, 8, i, (uint8_t)((bits >> i) & 1u));
    }
    m_set(mods, size, 8, 7, (uint8_t)((bits >> 6) & 1u));
    m_set(mods, size, 8, 8, (uint8_t)((bits >> 7) & 1u));
    m_set(mods, size, 7, 8, (uint8_t)((bits >> 8) & 1u));
    for (int i = 9; i <= 14; ++i) {
        m_set(mods, size, 14 - i, 8, (uint8_t)((bits >> i) & 1u));
    }

    /* 副本 2：左下 + 右上 */
    for (int i = 0; i < 8; ++i) {
        m_set(mods, size, size - 1 - i, 8, (uint8_t)((bits >> i) & 1u));
    }
    for (int i = 8; i <= 14; ++i) {
        m_set(mods, size, 8, size - 15 + i, (uint8_t)((bits >> i) & 1u));
    }
}

static bool mask_cond(int mask, int x, int y) {
    switch (mask) {
    case 0: return ((x + y) % 2) == 0;
    case 1: return (y % 2) == 0;
    case 2: return (x % 3) == 0;
    case 3: return ((x + y) % 3) == 0;
    case 4: return (((y / 2) + (x / 3)) % 2) == 0;
    case 5: return (((x * y) % 2) + ((x * y) % 3)) == 0;
    case 6: return ((((x * y) % 2) + ((x * y) % 3)) % 2) == 0;
    default: return ((((x + y) % 2) + ((x * y) % 3)) % 2) == 0;
    }
}

static void place_codewords(uint8_t *mods, const uint8_t *is_func, int size,
                            const uint8_t *codewords, size_t cw_len) {
    size_t bit_idx = 0;
    const size_t total_bits = cw_len * 8u;
    int dir = -1;
    int y = size - 1;
    for (int right = size - 1; right >= 1; right -= 2) {
        if (right == 6) right = 5; /* 跳过第 6 列垂直时序线 */
        for (int step = 0; step < size; ++step) {
            for (int dx = 0; dx < 2; ++dx) {
                const int x = right - dx;
                if (!m_get(is_func, size, x, y)) {
                    uint8_t bit = 0;
                    if (bit_idx < total_bits) {
                        const uint8_t byte = codewords[bit_idx >> 3];
                        bit = (uint8_t)((byte >> (7u - (bit_idx & 7u))) & 1u);
                    }
                    m_set(mods, size, x, y, bit);
                    ++bit_idx;
                }
            }
            if (step + 1 < size) {
                y += dir;
            }
        }
        dir = -dir;
    }
}

static int penalty_runs(const uint8_t *mods, int size) {
    int score = 0;
    for (int r = 0; r < size; ++r) {
        int run_h = 0;
        uint8_t prev_h = 2;
        int run_v = 0;
        uint8_t prev_v = 2;
        for (int c = 0; c < size; ++c) {
            const uint8_t h = m_get(mods, size, c, r);
            if (h == prev_h) {
                ++run_h;
                if (run_h == 5) score += 3;
                else if (run_h > 5) score += 1;
            } else {
                prev_h = h;
                run_h = 1;
            }

            const uint8_t v = m_get(mods, size, r, c);
            if (v == prev_v) {
                ++run_v;
                if (run_v == 5) score += 3;
                else if (run_v > 5) score += 1;
            } else {
                prev_v = v;
                run_v = 1;
            }
        }
    }
    return score;
}

static int compute_penalty(const uint8_t *mods, int size) {
    int score = penalty_runs(mods, size);

    /* N2: 2x2 同色方块 */
    for (int y = 0; y < size - 1; ++y) {
        for (int x = 0; x < size - 1; ++x) {
            const uint8_t c = m_get(mods, size, x, y);
            if (m_get(mods, size, x + 1, y) == c &&
                m_get(mods, size, x, y + 1) == c &&
                m_get(mods, size, x + 1, y + 1) == c) {
                score += 3;
            }
        }
    }

    /* N3: 1:1:3:1:1 finder-like 模式 */
    for (int r = 0; r < size; ++r) {
        for (int c = 0; c <= size - 11; ++c) {
            const bool mh1 =
                m_get(mods, size, c, r) == 1 && m_get(mods, size, c + 1, r) == 0 &&
                m_get(mods, size, c + 2, r) == 1 && m_get(mods, size, c + 3, r) == 1 &&
                m_get(mods, size, c + 4, r) == 1 && m_get(mods, size, c + 5, r) == 0 &&
                m_get(mods, size, c + 6, r) == 1 && m_get(mods, size, c + 7, r) == 0 &&
                m_get(mods, size, c + 8, r) == 0 && m_get(mods, size, c + 9, r) == 0 &&
                m_get(mods, size, c + 10, r) == 0;
            const bool mh2 =
                m_get(mods, size, c, r) == 0 && m_get(mods, size, c + 1, r) == 0 &&
                m_get(mods, size, c + 2, r) == 0 && m_get(mods, size, c + 3, r) == 0 &&
                m_get(mods, size, c + 4, r) == 1 && m_get(mods, size, c + 5, r) == 0 &&
                m_get(mods, size, c + 6, r) == 1 && m_get(mods, size, c + 7, r) == 1 &&
                m_get(mods, size, c + 8, r) == 1 && m_get(mods, size, c + 9, r) == 0 &&
                m_get(mods, size, c + 10, r) == 1;
            if (mh1 || mh2) score += 40;

            const bool mv1 =
                m_get(mods, size, r, c) == 1 && m_get(mods, size, r, c + 1) == 0 &&
                m_get(mods, size, r, c + 2) == 1 && m_get(mods, size, r, c + 3) == 1 &&
                m_get(mods, size, r, c + 4) == 1 && m_get(mods, size, r, c + 5) == 0 &&
                m_get(mods, size, r, c + 6) == 1 && m_get(mods, size, r, c + 7) == 0 &&
                m_get(mods, size, r, c + 8) == 0 && m_get(mods, size, r, c + 9) == 0 &&
                m_get(mods, size, r, c + 10) == 0;
            const bool mv2 =
                m_get(mods, size, r, c) == 0 && m_get(mods, size, r, c + 1) == 0 &&
                m_get(mods, size, r, c + 2) == 0 && m_get(mods, size, r, c + 3) == 0 &&
                m_get(mods, size, r, c + 4) == 1 && m_get(mods, size, r, c + 5) == 0 &&
                m_get(mods, size, r, c + 6) == 1 && m_get(mods, size, r, c + 7) == 1 &&
                m_get(mods, size, r, c + 8) == 1 && m_get(mods, size, r, c + 9) == 0 &&
                m_get(mods, size, r, c + 10) == 1;
            if (mv1 || mv2) score += 40;
        }
    }

    /* N4: 黑白比例偏差 */
    int dark = 0;
    const int total = size * size;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            dark += m_get(mods, size, x, y);
        }
    }
    const int k = abs(dark * 20 - total * 10) / total;
    score += k * 10;
    return score;
}

typedef struct {
    uint8_t *buf;
    size_t bit_pos;
} bit_writer_t;

static void bw_put(bit_writer_t *bw, uint32_t val, int bits) {
    for (int i = bits - 1; i >= 0; --i) {
        if ((val >> i) & 1u) {
            bw->buf[bw->bit_pos >> 3] |= (uint8_t)(1u << (7u - (bw->bit_pos & 7u)));
        }
        ++bw->bit_pos;
    }
}

bool ecard_qr_encode_packed(const char *text, uint8_t *packed, size_t packed_cap,
                            int *out_size) {
    if (!text || !text[0] || !packed) return false;
    const size_t len = strlen(text);

    const qr_spec_t *spec = NULL;
    for (size_t i = 0; i < sizeof(k_specs) / sizeof(k_specs[0]); ++i) {
        if (len <= k_specs[i].max_bytes) {
            spec = &k_specs[i];
            break;
        }
    }
    if (!spec) return false;

    const int size = spec->size;
    const size_t total_mods = (size_t)size * (size_t)size;
    const size_t need_bytes = (total_mods + 7u) / 8u;
    if (packed_cap < need_bytes) return false;

    /* 1. 编码 Byte 模式数据码字 */
    uint8_t data_cw[136];
    memset(data_cw, 0, sizeof(data_cw));
    bit_writer_t bw = { data_cw, 0 };
    bw_put(&bw, 0x4u, 4); /* Byte mode */
    bw_put(&bw, (uint32_t)len, 8);
    for (size_t i = 0; i < len; ++i) {
        bw_put(&bw, (uint8_t)text[i], 8);
    }
    const size_t data_cap_bits = (size_t)spec->total_data * 8u;
    const size_t rem_bits = data_cap_bits - bw.bit_pos;
    const int term = (rem_bits >= 4u) ? 4 : (int)rem_bits;
    if (term > 0) bw_put(&bw, 0, term);
    while ((bw.bit_pos & 7u) != 0) bw_put(&bw, 0, 1);

    size_t cw_idx = bw.bit_pos >> 3;
    const uint8_t pads[2] = { 0xECu, 0x11u };
    int pad_i = 0;
    while (cw_idx < spec->total_data) {
        data_cw[cw_idx++] = pads[pad_i];
        pad_i ^= 1;
    }

    /* 2. 计算每块的 Reed-Solomon 纠错码并交织 */
    uint8_t ecc_blocks[2][32];
    for (int b = 0; b < spec->num_blocks; ++b) {
        rs_compute_ecc(&data_cw[b * spec->block_data], spec->block_data,
                       ecc_blocks[b], spec->block_ecc);
    }

    uint8_t codewords[172];
    size_t out_cw = 0;
    for (int i = 0; i < spec->block_data; ++i) {
        for (int b = 0; b < spec->num_blocks; ++b) {
            codewords[out_cw++] = data_cw[b * spec->block_data + i];
        }
    }
    for (int i = 0; i < spec->block_ecc; ++i) {
        for (int b = 0; b < spec->num_blocks; ++b) {
            codewords[out_cw++] = ecc_blocks[b][i];
        }
    }

    /* 3. 位压缩构造功能区与码字，选取惩罚分最低的掩码（总栈占用 ~1 KB） */
    uint8_t base[ECARD_QR_PACKED_BYTES];
    uint8_t is_func[ECARD_QR_PACKED_BYTES];
    uint8_t candidate[ECARD_QR_PACKED_BYTES];

    draw_function_patterns(base, is_func, spec, need_bytes);
    place_codewords(base, is_func, size, codewords, out_cw);

    int best_score = 0x7FFFFFFF;
    for (int mask = 0; mask < 8; ++mask) {
        memcpy(candidate, base, need_bytes);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                if (!m_get(is_func, size, x, y) && mask_cond(mask, x, y)) {
                    m_set(candidate, size, x, y, m_get(candidate, size, x, y) ^ 1u);
                }
            }
        }
        draw_format_bits(candidate, size, mask);
        const int score = compute_penalty(candidate, size);
        if (score < best_score) {
            best_score = score;
            memcpy(packed, candidate, need_bytes);
        }
    }

    if (out_size) *out_size = size;
    return true;
}

bool ecard_qr_encode(const char *text, uint8_t *modules, size_t max_modules,
                     int *out_size) {
    if (!modules) return false;
    uint8_t packed[ECARD_QR_PACKED_BYTES];
    int size = 0;
    if (!ecard_qr_encode_packed(text, packed, sizeof(packed), &size)) {
        return false;
    }
    const size_t total = (size_t)size * (size_t)size;
    if (max_modules < total) return false;
    for (int r = 0; r < size; ++r) {
        for (int c = 0; c < size; ++c) {
            modules[r * size + c] = ecard_qr_bit(packed, size, r, c) ? 1u : 0u;
        }
    }
    if (out_size) *out_size = size;
    return true;
}
