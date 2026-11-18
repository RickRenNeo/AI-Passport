#!/usr/bin/env python3
"""Generate main/ecard_cards.c with LZSS-packed Grade 1-6 curriculum seeds.

Reads tools/ecard_curriculum_data.py, validates all Xiaozhi prompt length and
UTF-8 buffer contracts, packs each grade into a deterministic .pak image,
compresses each image with byte-aligned LZSS (4096-byte window, 3..18 match),
and emits main/ecard_cards.c with SPIFFS <=3-grade .pak retention + on-demand
single-deck loader.

Run from the repository root:

    python3 tools/generate_ecard_cards.py
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
REPO_ROOT = TOOLS_DIR.parent
sys.path.insert(0, str(TOOLS_DIR))

from ecard_curriculum_data import POEM_PACKS, WORD_UNITS  # noqa: E402

CARDS_OUT = REPO_ROOT / "main" / "ecard_cards.c"

GRADE_COUNT = 6
PAK_MAGIC = b"ECPK"
PAK_VERSION = 1
PAK_HEADER_FMT = "<4sBBBBHH"  # magic, ver, grade, poem_decks, word_decks, poem_total, word_total
PAK_DIR_FMT = "<BBHIHH48s52s"  # kind, count, rsv, offset, len, pad, title[48], subtitle[52]
assert struct.calcsize(PAK_HEADER_FMT) == 12
assert struct.calcsize(PAK_DIR_FMT) == 112

LZSS_WINDOW = 4096
LZSS_MIN_MATCH = 3
LZSS_MAX_MATCH = 18


def validate_curriculum() -> None:
    for g in range(1, GRADE_COUNT + 1):
        assert g in POEM_PACKS, f"missing POEM_PACKS[{g}]"
        assert g in WORD_UNITS, f"missing WORD_UNITS[{g}]"
        p_packs = POEM_PACKS[g]
        w_units = WORD_UNITS[g]
        assert 2 <= len(p_packs) <= 4, f"grade {g} poem pack count {len(p_packs)}"
        assert len(w_units) == 12, f"grade {g} word unit count {len(w_units)} != 12"

        for d_idx, (title, subtitle, cards) in enumerate(p_packs):
            assert 0 < len(title.encode("utf-8")) < 48, f"G{g} P{d_idx} title too long: {title}"
            assert 0 < len(subtitle.encode("utf-8")) < 52, f"G{g} P{d_idx} subtitle too long: {subtitle}"
            assert 5 <= len(cards) <= 24, f"G{g} P{d_idx} bad card count: {len(cards)}"
            for c_idx, (front, back, meta, explain) in enumerate(cards):
                assert front and back and meta and explain, f"G{g} P{d_idx}#{c_idx} empty field"
                pair = f"{front}|{back}"
                assert len(pair) <= 16, (
                    f"G{g} P{d_idx}#{c_idx} poem prompt > 16 chars ({len(pair)}): {pair}"
                )

        for d_idx, (title, subtitle, cards) in enumerate(w_units):
            assert 0 < len(title.encode("utf-8")) < 48, f"G{g} W{d_idx} title too long: {title}"
            assert 0 < len(subtitle.encode("utf-8")) < 52, f"G{g} W{d_idx} subtitle too long: {subtitle}"
            assert 8 <= len(cards) <= 24, f"G{g} W{d_idx} bad card count: {len(cards)}"
            for c_idx, (front, back, example, example_cn) in enumerate(cards):
                assert front and back and example and example_cn, f"G{g} W{d_idx}#{c_idx} empty"
                assert front.isascii(), f"G{g} W{d_idx}#{c_idx} non-ascii word: {front}"
                assert len(f"读:{front}") <= 16, f"G{g} W{d_idx}#{c_idx} word too long: {front}"
                assert example.isascii(), f"G{g} W{d_idx}#{c_idx} non-ascii example: {example}"
                assert len(example.encode("ascii")) <= 30, (
                    f"G{g} W{d_idx}#{c_idx} example > 30 bytes ({len(example)}): {example}"
                )


def pack_padded_utf8(text: str, size: int) -> bytes:
    raw = text.encode("utf-8")
    assert len(raw) < size
    return raw + b"\x00" * (size - len(raw))


def build_grade_pak(grade: int) -> tuple[bytes, int, int, int, list[int]]:
    p_packs = POEM_PACKS[grade]
    w_units = WORD_UNITS[grade]
    total_decks = len(p_packs) + len(w_units)
    data_cursor = 12 + total_decks * 112

    dir_entries: list[bytes] = []
    deck_blobs: list[bytes] = []
    poem_total = 0
    word_counts: list[int] = []

    for title, subtitle, cards in p_packs:
        chunks: list[bytes] = []
        for front, back, meta, explain in cards:
            for field in (front, back, meta, explain):
                chunks.append(field.encode("utf-8") + b"\x00")
        blob = b"".join(chunks)
        assert len(blob) <= 3000, f"G{grade} poem blob too large: {len(blob)}"
        dir_entries.append(
            struct.pack(
                PAK_DIR_FMT,
                0,
                len(cards),
                0,
                data_cursor,
                len(blob),
                0,
                pack_padded_utf8(title, 48),
                pack_padded_utf8(subtitle, 52),
            )
        )
        deck_blobs.append(blob)
        data_cursor += len(blob)
        poem_total += len(cards)

    for title, subtitle, cards in w_units:
        chunks = []
        for front, back, example, example_cn in cards:
            for field in (front, back, example, example_cn):
                chunks.append(field.encode("utf-8") + b"\x00")
        blob = b"".join(chunks)
        assert len(blob) <= 3000, f"G{grade} word blob too large: {len(blob)}"
        dir_entries.append(
            struct.pack(
                PAK_DIR_FMT,
                1,
                len(cards),
                0,
                data_cursor,
                len(blob),
                0,
                pack_padded_utf8(title, 48),
                pack_padded_utf8(subtitle, 52),
            )
        )
        deck_blobs.append(blob)
        data_cursor += len(blob)
        word_counts.append(len(cards))

    word_total = sum(word_counts)
    header = struct.pack(
        PAK_HEADER_FMT,
        PAK_MAGIC,
        PAK_VERSION,
        grade,
        len(p_packs),
        len(w_units),
        poem_total,
        word_total,
    )
    return header + b"".join(dir_entries) + b"".join(deck_blobs), len(p_packs), poem_total, word_total, word_counts


def lzss_compress(data: bytes) -> bytes:
    out = bytearray()
    n = len(data)
    pos = 0
    while pos < n:
        flag_pos = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if pos >= n:
                break
            win_start = max(0, pos - LZSS_WINDOW)
            best_len = 0
            best_dist = 0
            max_len = min(LZSS_MAX_MATCH, n - pos)
            if max_len >= LZSS_MIN_MATCH and pos > win_start:
                needle = data[pos : pos + LZSS_MIN_MATCH]
                search_pos = data.rfind(needle, win_start, pos + LZSS_MIN_MATCH - 1)
                checks = 0
                while search_pos != -1 and checks < 48:
                    checks += 1
                    dist = pos - search_pos
                    mlen = LZSS_MIN_MATCH
                    while mlen < max_len and data[search_pos + mlen] == data[pos + mlen]:
                        mlen += 1
                    if mlen > best_len:
                        best_len = mlen
                        best_dist = dist
                        if best_len == max_len:
                            break
                    if search_pos == win_start:
                        break
                    search_pos = data.rfind(needle, win_start, search_pos + LZSS_MIN_MATCH - 1)
            if best_len >= LZSS_MIN_MATCH:
                dm1 = best_dist - 1
                out.append(dm1 & 0xFF)
                out.append((((dm1 >> 8) & 0x0F) << 4) | ((best_len - LZSS_MIN_MATCH) & 0x0F))
                pos += best_len
            else:
                flags |= 1 << bit
                out.append(data[pos])
                pos += 1
        out[flag_pos] = flags
    return bytes(out)


def lzss_decompress_verify(comp: bytes, raw_len: int) -> bytes:
    out = bytearray()
    src = 0
    while len(out) < raw_len and src < len(comp):
        flags = comp[src]
        src += 1
        for bit in range(8):
            if len(out) >= raw_len:
                break
            if flags & (1 << bit):
                out.append(comp[src])
                src += 1
            else:
                b0 = comp[src]
                b1 = comp[src + 1]
                src += 2
                dist = (((b1 >> 4) & 0x0F) << 8 | b0) + 1
                length = (b1 & 0x0F) + LZSS_MIN_MATCH
                for _ in range(length):
                    out.append(out[-dist])
                    if len(out) >= raw_len:
                        break
    return bytes(out)


def format_byte_array(name: str, data: bytes) -> str:
    lines = [f"static const uint8_t {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i : i + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


C_RUNTIME_TAIL = r"""
#define ECARD_PAK_HEADER_BYTES 12u
#define ECARD_PAK_DIR_BYTES    112u
#define ECARD_LZSS_WINDOW      4096u
#define ECARD_DECK_BLOB_MAX    3072u

static char s_pak_dir[160];
static uint8_t s_current_grade = 3;
static uint8_t s_active_grades[ECARD_MAX_ACTIVE_GRADES] = {2, 3, 4};
static uint8_t s_active_grade_count = 3;

static char s_loaded_blob[ECARD_DECK_BLOB_MAX];
static ecard_card_t s_loaded_cards[ECARD_MAX_CARDS_PER_DECK];

static uint16_t rd_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool lzss_extract_slice(const uint8_t *comp, size_t comp_len,
                               size_t raw_total,
                               size_t slice_off, size_t slice_len,
                               uint8_t *out_slice) {
    if (!comp || !out_slice || slice_off + slice_len > raw_total) {
        return false;
    }
    if (slice_len == 0) {
        return true;
    }
    uint8_t *ring = (uint8_t *)malloc(ECARD_LZSS_WINDOW);
    if (!ring) {
        return false;
    }
    const size_t target_end = slice_off + slice_len;
    size_t out_pos = 0;
    size_t src = 0;
    while (out_pos < target_end && src < comp_len) {
        const uint8_t flags = comp[src++];
        for (uint8_t bit = 0; bit < 8 && out_pos < target_end; ++bit) {
            if (flags & (uint8_t)(1u << bit)) {
                if (src >= comp_len) {
                    free(ring);
                    return false;
                }
                const uint8_t v = comp[src++];
                ring[out_pos & (ECARD_LZSS_WINDOW - 1u)] = v;
                if (out_pos >= slice_off) {
                    out_slice[out_pos - slice_off] = v;
                }
                ++out_pos;
            } else {
                if (src + 1 >= comp_len) {
                    free(ring);
                    return false;
                }
                const uint8_t b0 = comp[src++];
                const uint8_t b1 = comp[src++];
                const size_t dist = ((((size_t)(b1 >> 4) & 0x0Fu) << 8) | (size_t)b0) + 1u;
                const size_t len = ((size_t)(b1 & 0x0Fu)) + 3u;
                if (dist > out_pos) {
                    free(ring);
                    return false;
                }
                for (size_t k = 0; k < len && out_pos < target_end; ++k) {
                    const uint8_t v = ring[(out_pos - dist) & (ECARD_LZSS_WINDOW - 1u)];
                    ring[out_pos & (ECARD_LZSS_WINDOW - 1u)] = v;
                    if (out_pos >= slice_off) {
                        out_slice[out_pos - slice_off] = v;
                    }
                    ++out_pos;
                }
            }
        }
    }
    free(ring);
    return out_pos >= target_end;
}

static bool lzss_unpack_to_file(const uint8_t *comp, size_t comp_len,
                                size_t raw_total, const char *path) {
    uint8_t *ring = (uint8_t *)malloc(ECARD_LZSS_WINDOW);
    if (!ring) {
        return false;
    }
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(ring);
        return false;
    }
    uint8_t wbuf[256];
    size_t wlen = 0;
    size_t out_pos = 0;
    size_t src = 0;
    bool ok = true;

    while (out_pos < raw_total && src < comp_len && ok) {
        const uint8_t flags = comp[src++];
        for (uint8_t bit = 0; bit < 8 && out_pos < raw_total; ++bit) {
            if (flags & (uint8_t)(1u << bit)) {
                if (src >= comp_len) {
                    ok = false;
                    break;
                }
                const uint8_t v = comp[src++];
                ring[out_pos & (ECARD_LZSS_WINDOW - 1u)] = v;
                wbuf[wlen++] = v;
                if (wlen == sizeof(wbuf)) {
                    if (fwrite(wbuf, 1, wlen, fp) != wlen) {
                        ok = false;
                        break;
                    }
                    wlen = 0;
                }
                ++out_pos;
            } else {
                if (src + 1 >= comp_len) {
                    ok = false;
                    break;
                }
                const uint8_t b0 = comp[src++];
                const uint8_t b1 = comp[src++];
                const size_t dist = ((((size_t)(b1 >> 4) & 0x0Fu) << 8) | (size_t)b0) + 1u;
                const size_t len = ((size_t)(b1 & 0x0Fu)) + 3u;
                if (dist > out_pos) {
                    ok = false;
                    break;
                }
                for (size_t k = 0; k < len && out_pos < raw_total; ++k) {
                    const uint8_t v = ring[(out_pos - dist) & (ECARD_LZSS_WINDOW - 1u)];
                    ring[out_pos & (ECARD_LZSS_WINDOW - 1u)] = v;
                    wbuf[wlen++] = v;
                    if (wlen == sizeof(wbuf)) {
                        if (fwrite(wbuf, 1, wlen, fp) != wlen) {
                            ok = false;
                            break;
                        }
                        wlen = 0;
                    }
                    ++out_pos;
                }
                if (!ok) {
                    break;
                }
            }
        }
    }
    if (ok && out_pos != raw_total) {
        ok = false;
    }
    if (ok && wlen > 0 && fwrite(wbuf, 1, wlen, fp) != wlen) {
        ok = false;
    }
    if (fclose(fp) != 0) {
        ok = false;
    }
    free(ring);
    if (!ok) {
        (void)remove(path);
    }
    return ok;
}

static void build_pak_path(uint8_t grade, char *out, size_t out_size) {
    snprintf(out, out_size, "%s/cur_g%u.pak", s_pak_dir, (unsigned)grade);
}

static bool pak_file_valid(const char *path, uint8_t grade, size_t expected_bytes) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    uint8_t hdr[ECARD_PAK_HEADER_BYTES];
    if (fread(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        fclose(fp);
        return false;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }
    const long sz = ftell(fp);
    fclose(fp);
    if (sz < 0 || (size_t)sz != expected_bytes) {
        return false;
    }
    return memcmp(hdr, "ECPK", 4) == 0 && hdr[4] == 1 && hdr[5] == grade;
}

static bool sync_mounted_paks(void) {
    if (s_pak_dir[0] == '\0') {
        return true;
    }
    bool all_ok = true;
    for (uint8_t g = 1; g <= ECARD_GRADE_COUNT; ++g) {
        char path[200];
        build_pak_path(g, path, sizeof(path));
        if (ecard_cards_is_grade_active(g)) {
            const ecard_grade_seed_t *seed = &s_grade_seeds[g - 1];
            if (!pak_file_valid(path, g, seed->raw_len)) {
                if (!lzss_unpack_to_file(seed->comp, seed->comp_len, seed->raw_len, path)) {
                    all_ok = false;
                }
            }
        } else {
            (void)remove(path);
        }
    }
    return all_ok;
}

static bool read_grade_slice(uint8_t grade, size_t off, size_t len, uint8_t *out) {
    if (grade < 1 || grade > ECARD_GRADE_COUNT || !out) {
        return false;
    }
    const ecard_grade_seed_t *seed = &s_grade_seeds[grade - 1];
    if (off + len > seed->raw_len) {
        return false;
    }
    if (s_pak_dir[0] != '\0') {
        if (!ecard_cards_is_grade_active(grade)) {
            return false;
        }
        char path[200];
        build_pak_path(grade, path, sizeof(path));
        FILE *fp = fopen(path, "rb");
        if (!fp) {
            if (!sync_mounted_paks()) {
                return false;
            }
            fp = fopen(path, "rb");
            if (!fp) {
                return false;
            }
        }
        bool ok = (fseek(fp, (long)off, SEEK_SET) == 0) &&
                  (fread(out, 1, len, fp) == len);
        fclose(fp);
        return ok;
    }
    return lzss_extract_slice(seed->comp, seed->comp_len, seed->raw_len, off, len, out);
}

void ecard_cards_reset_curriculum(void) {
    s_pak_dir[0] = '\0';
    s_current_grade = 3;
    s_active_grades[0] = 2;
    s_active_grades[1] = 3;
    s_active_grades[2] = 4;
    s_active_grade_count = 3;
}

uint8_t ecard_cards_parse_grade_from_class(const char *class_name) {
    if (!class_name || class_name[0] == '\0') {
        return 0;
    }
    static const struct {
        const char *cn;
        uint8_t grade;
    } k_cn_map[] = {
        {"一", 1}, {"二", 2}, {"三", 3}, {"四", 4}, {"五", 5}, {"六", 6},
    };
    for (const char *p = class_name; *p != '\0'; ++p) {
        if (*p >= '1' && *p <= '6') {
            return (uint8_t)(*p - '0');
        }
        for (size_t i = 0; i < sizeof(k_cn_map) / sizeof(k_cn_map[0]); ++i) {
            if (strncmp(p, k_cn_map[i].cn, 3) == 0) {
                return k_cn_map[i].grade;
            }
        }
    }
    return 0;
}

void ecard_cards_default_grades_for(uint8_t current_grade,
                                    uint8_t out_grades[ECARD_MAX_ACTIVE_GRADES],
                                    uint8_t *out_count) {
    if (!out_grades) {
        if (out_count) *out_count = 0;
        return;
    }
    uint8_t g = (current_grade >= 1 && current_grade <= ECARD_GRADE_COUNT) ? current_grade : 3;
    uint8_t start = 1;
    if (g <= 2) {
        start = 1;
    } else if (g >= 5) {
        start = 4;
    } else {
        start = (uint8_t)(g - 1);
    }
    out_grades[0] = start;
    out_grades[1] = (uint8_t)(start + 1);
    out_grades[2] = (uint8_t)(start + 2);
    if (out_count) {
        *out_count = 3;
    }
}

bool ecard_cards_set_curriculum(uint8_t current_grade,
                                const uint8_t *grades,
                                uint8_t grade_count) {
    if (current_grade < 1 || current_grade > ECARD_GRADE_COUNT) {
        return false;
    }
    uint8_t next_grades[ECARD_MAX_ACTIVE_GRADES] = {0};
    uint8_t next_count = 0;

    if (!grades || grade_count == 0) {
        ecard_cards_default_grades_for(current_grade, next_grades, &next_count);
    } else {
        if (grade_count > ECARD_MAX_ACTIVE_GRADES) {
            return false;
        }
        bool seen[ECARD_GRADE_COUNT + 1] = {false};
        for (uint8_t i = 0; i < grade_count; ++i) {
            const uint8_t g = grades[i];
            if (g < 1 || g > ECARD_GRADE_COUNT) {
                return false;
            }
            seen[g] = true;
        }
        for (uint8_t g = 1; g <= ECARD_GRADE_COUNT; ++g) {
            if (seen[g]) {
                if (next_count >= ECARD_MAX_ACTIVE_GRADES) {
                    return false;
                }
                next_grades[next_count++] = g;
            }
        }
        if (next_count == 0) {
            return false;
        }
    }

    s_current_grade = current_grade;
    s_active_grade_count = next_count;
    for (uint8_t i = 0; i < ECARD_MAX_ACTIVE_GRADES; ++i) {
        s_active_grades[i] = (i < next_count) ? next_grades[i] : 0;
    }
    return sync_mounted_paks();
}

bool ecard_cards_mount_dir(const char *dir_path) {
    if (!dir_path || dir_path[0] == '\0') {
        s_pak_dir[0] = '\0';
        return true;
    }
    const size_t len = strlen(dir_path);
    if (len + 20 >= sizeof(s_pak_dir)) {
        return false;
    }
    memcpy(s_pak_dir, dir_path, len + 1);
    while (s_pak_dir[0] != '\0' && s_pak_dir[strlen(s_pak_dir) - 1] == '/') {
        s_pak_dir[strlen(s_pak_dir) - 1] = '\0';
    }
    return sync_mounted_paks();
}

uint8_t ecard_cards_current_grade(void) {
    return s_current_grade;
}

uint8_t ecard_cards_active_grades(uint8_t out_grades[ECARD_MAX_ACTIVE_GRADES]) {
    if (out_grades) {
        for (uint8_t i = 0; i < s_active_grade_count; ++i) {
            out_grades[i] = s_active_grades[i];
        }
    }
    return s_active_grade_count;
}

bool ecard_cards_is_grade_active(uint8_t grade) {
    for (uint8_t i = 0; i < s_active_grade_count; ++i) {
        if (s_active_grades[i] == grade) {
            return true;
        }
    }
    return false;
}

uint8_t ecard_cards_deck_count(ecard_card_kind_t kind, uint8_t grade) {
    if (grade < 1 || grade > ECARD_GRADE_COUNT) {
        return 0;
    }
    const ecard_grade_seed_t *seed = &s_grade_seeds[grade - 1];
    return (kind == ECARD_CARD_POEM) ? seed->poem_decks : seed->word_decks;
}

uint16_t ecard_cards_grade_card_total(ecard_card_kind_t kind, uint8_t grade) {
    if (grade < 1 || grade > ECARD_GRADE_COUNT) {
        return 0;
    }
    const ecard_grade_seed_t *seed = &s_grade_seeds[grade - 1];
    return (kind == ECARD_CARD_POEM) ? seed->poem_total : seed->word_total;
}

static bool resolve_dir_index(ecard_card_kind_t kind, uint8_t grade,
                              uint8_t deck_index, size_t *out_dir_off) {
    if (grade < 1 || grade > ECARD_GRADE_COUNT || !out_dir_off) {
        return false;
    }
    const ecard_grade_seed_t *seed = &s_grade_seeds[grade - 1];
    size_t idx = 0;
    if (kind == ECARD_CARD_POEM) {
        if (deck_index >= seed->poem_decks) return false;
        idx = deck_index;
    } else if (kind == ECARD_CARD_WORD) {
        if (deck_index >= seed->word_decks) return false;
        idx = (size_t)seed->poem_decks + (size_t)deck_index;
    } else {
        return false;
    }
    *out_dir_off = ECARD_PAK_HEADER_BYTES + idx * ECARD_PAK_DIR_BYTES;
    return true;
}

bool ecard_cards_deck_info(ecard_card_kind_t kind, uint8_t grade,
                           uint8_t deck_index, ecard_deck_info_t *out_info) {
    if (!out_info) return false;
    memset(out_info, 0, sizeof(*out_info));

    size_t dir_off = 0;
    if (!resolve_dir_index(kind, grade, deck_index, &dir_off)) {
        return false;
    }
    uint8_t entry[ECARD_PAK_DIR_BYTES];
    if (!read_grade_slice(grade, dir_off, sizeof(entry), entry)) {
        return false;
    }
    out_info->card_count = entry[1];
    memcpy(out_info->title, &entry[12], 47);
    out_info->title[47] = '\0';
    memcpy(out_info->subtitle, &entry[60], 51);
    out_info->subtitle[51] = '\0';
    return out_info->card_count > 0;
}

const ecard_card_t *ecard_cards_load_deck(ecard_card_kind_t kind, uint8_t grade,
                                          uint8_t deck_index, uint8_t *out_count) {
    if (out_count) *out_count = 0;
    size_t dir_off = 0;
    if (!resolve_dir_index(kind, grade, deck_index, &dir_off)) {
        return NULL;
    }
    uint8_t entry[ECARD_PAK_DIR_BYTES];
    if (!read_grade_slice(grade, dir_off, sizeof(entry), entry)) {
        return NULL;
    }
    const uint8_t count = entry[1];
    const uint32_t data_off = rd_le32(&entry[4]);
    const uint16_t data_len = rd_le16(&entry[8]);
    if (count == 0 || count > ECARD_MAX_CARDS_PER_DECK ||
        data_len == 0 || data_len > ECARD_DECK_BLOB_MAX) {
        return NULL;
    }
    if (!read_grade_slice(grade, data_off, data_len, (uint8_t *)s_loaded_blob)) {
        return NULL;
    }
    if (s_loaded_blob[data_len - 1] != '\0') {
        return NULL;
    }

    size_t pos = 0;
    memset(s_loaded_cards, 0, sizeof(s_loaded_cards));
    for (uint8_t i = 0; i < count; ++i) {
        const char *fields[4] = {NULL, NULL, NULL, NULL};
        for (int f = 0; f < 4; ++f) {
            if (pos >= data_len) {
                return NULL;
            }
            fields[f] = &s_loaded_blob[pos];
            pos += strlen(fields[f]) + 1u;
        }
        s_loaded_cards[i].front = fields[0];
        s_loaded_cards[i].back = fields[1];
        if (kind == ECARD_CARD_POEM) {
            s_loaded_cards[i].meta = fields[2];
            s_loaded_cards[i].explain = fields[3];
        } else {
            s_loaded_cards[i].example = fields[2];
            s_loaded_cards[i].example_cn = fields[3];
        }
    }
    if (out_count) {
        *out_count = count;
    }
    return s_loaded_cards;
}

uint16_t ecard_cards_word_deck_offset(uint8_t grade, uint8_t deck_index) {
    if (grade < 1 || grade > ECARD_GRADE_COUNT) {
        return 0;
    }
    uint16_t offset = 0;
    for (uint8_t g = 1; g < grade; ++g) {
        offset = (uint16_t)(offset + s_grade_seeds[g - 1].word_total);
    }
    const ecard_grade_seed_t *seed = &s_grade_seeds[grade - 1];
    const uint8_t cap = (deck_index < seed->word_decks) ? deck_index : seed->word_decks;
    for (uint8_t d = 0; d < cap; ++d) {
        offset = (uint16_t)(offset + seed->word_unit_counts[d]);
    }
    return offset;
}

const ecard_card_t *ecard_cards_poems(uint8_t grade, uint8_t *count) {
    return ecard_cards_load_deck(ECARD_CARD_POEM, grade, 0, count);
}

const ecard_card_t *ecard_cards_words(uint8_t grade, uint8_t *count) {
    return ecard_cards_load_deck(ECARD_CARD_WORD, grade, 0, count);
}

uint16_t ecard_cards_word_offset(uint8_t grade) {
    return ecard_cards_word_deck_offset(grade, 0);
}

uint16_t ecard_cards_word_total(void) {
    uint16_t total = 0;
    for (uint8_t g = 0; g < ECARD_GRADE_COUNT; ++g) {
        total = (uint16_t)(total + s_grade_seeds[g].word_total);
    }
    return total;
}
"""


def main() -> int:
    validate_curriculum()

    blocks: list[str] = [
        "/* Auto-generated by tools/generate_ecard_cards.py; do not edit by hand. */",
        '#include "ecard_cards.h"',
        "#include <stdio.h>",
        "#include <stdlib.h>",
        "#include <string.h>",
        "",
    ]

    seed_rows: list[str] = []
    raw_sum = 0
    comp_sum = 0

    for g in range(1, GRADE_COUNT + 1):
        pak, p_decks, p_total, w_total, w_counts = build_grade_pak(g)
        comp = lzss_compress(pak)
        assert lzss_decompress_verify(comp, len(pak)) == pak, f"LZSS verify failed for grade {g}"
        raw_sum += len(pak)
        comp_sum += len(comp)
        blocks.append(format_byte_array(f"s_grade_lzss_{g}", comp))
        blocks.append("")
        counts_str = ", ".join(str(x) for x in w_counts)
        seed_rows.append(
            f"    {{ s_grade_lzss_{g}, {len(comp)}u, {len(pak)}u, "
            f"{p_decks}u, {len(w_counts)}u, {p_total}u, {w_total}u, {{ {counts_str} }} }},"
        )

    blocks.append("typedef struct {")
    blocks.append("    const uint8_t *comp;")
    blocks.append("    uint32_t comp_len;")
    blocks.append("    uint32_t raw_len;")
    blocks.append("    uint8_t poem_decks;")
    blocks.append("    uint8_t word_decks;")
    blocks.append("    uint16_t poem_total;")
    blocks.append("    uint16_t word_total;")
    blocks.append("    uint8_t word_unit_counts[12];")
    blocks.append("} ecard_grade_seed_t;")
    blocks.append("")
    blocks.append("static const ecard_grade_seed_t s_grade_seeds[ECARD_GRADE_COUNT] = {")
    blocks.extend(seed_rows)
    blocks.append("};")
    blocks.append(C_RUNTIME_TAIL.lstrip("\n"))

    CARDS_OUT.write_text("\n".join(blocks), encoding="utf-8")
    ratio = (comp_sum * 100.0 / raw_sum) if raw_sum else 0.0
    print(
        f"Generated {CARDS_OUT.relative_to(REPO_ROOT)}: "
        f"raw={raw_sum} B -> lzss={comp_sum} B ({ratio:.1f}%)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
