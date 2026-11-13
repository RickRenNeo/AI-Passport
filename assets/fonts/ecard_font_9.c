/*******************************************************************************
 * Size: 9 px
 * Bpp: 2
 * Opts: --font /Users/neo/Anti/AI-Passport/assets/fonts/.cache/NotoSansCJKsc-Regular.otf --range 0x20-0x7E --symbols  --size 9 --bpp 2 --format lvgl --no-compress --lv-font-name ecard_font_9 --lv-include lvgl.h --output /Users/neo/Anti/AI-Passport/assets/fonts/ecard_font_9.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef ECARD_FONT_9
#define ECARD_FONT_9 1
#endif

#if ECARD_FONT_9

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x30, 0xc2, 0x8, 0x10, 0x3, 0x0,

    /* U+0022 "\"" */
    0x25, 0x25, 0x10,

    /* U+0023 "#" */
    0x11, 0x4, 0x83, 0xb4, 0x88, 0x7b, 0x48, 0x41,
    0x10,

    /* U+0024 "$" */
    0x4, 0x7, 0x43, 0x10, 0xc0, 0x1d, 0x0, 0xc0,
    0x24, 0xa8, 0x8, 0x0,

    /* U+0025 "%" */
    0x28, 0x20, 0x84, 0x10, 0x85, 0x80, 0x49, 0x58,
    0x26, 0x22, 0x1, 0x22, 0x8, 0x19,

    /* U+0026 "&" */
    0x1a, 0x2, 0x20, 0x29, 0x2, 0xc2, 0x56, 0x59,
    0x2c, 0x3a, 0x60,

    /* U+0027 "'" */
    0x22, 0x10,

    /* U+0028 "(" */
    0x0, 0x12, 0x8, 0x20, 0x82, 0x8, 0x10, 0x10,

    /* U+0029 ")" */
    0x1, 0x42, 0x8, 0x20, 0x82, 0x8, 0x11, 0x0,

    /* U+002A "*" */
    0x4, 0x3c, 0x24,

    /* U+002B "+" */
    0x4, 0x2, 0x6, 0xe4, 0x20, 0x8, 0x0,

    /* U+002C "," */
    0x3, 0x20,

    /* U+002D "-" */
    0x64,

    /* U+002E "." */
    0x2,

    /* U+002F "/" */
    0x4, 0x8, 0x8, 0x14, 0x20, 0x20, 0x50, 0x80,
    0x40,

    /* U+0030 "0" */
    0x2a, 0x8, 0x85, 0x16, 0x46, 0x51, 0x48, 0x82,
    0xa0,

    /* U+0031 "1" */
    0x2c, 0x3, 0x0, 0x80, 0x20, 0x8, 0x2, 0x2,
    0xe4,

    /* U+0032 "2" */
    0x2a, 0x0, 0x80, 0x20, 0xc, 0x9, 0x9, 0x7,
    0xa4,

    /* U+0033 "3" */
    0x2a, 0x0, 0x80, 0x30, 0x74, 0x2, 0x0, 0x96,
    0xa0,

    /* U+0034 "4" */
    0x7, 0x2, 0x82, 0x31, 0x4c, 0xab, 0x40, 0xc0,
    0x30,

    /* U+0035 "5" */
    0x3a, 0x8, 0x3, 0xa0, 0x9, 0x1, 0x40, 0x96,
    0xe0,

    /* U+0036 "6" */
    0x1a, 0xc, 0x5, 0x1, 0xa8, 0x51, 0x48, 0x61,
    0xa0,

    /* U+0037 "7" */
    0x6b, 0x40, 0x80, 0x50, 0x20, 0xc, 0x2, 0x0,
    0x80,

    /* U+0038 "8" */
    0x2a, 0x8, 0x43, 0x20, 0xb8, 0x52, 0x64, 0x62,
    0xa0,

    /* U+0039 "9" */
    0x29, 0x14, 0x88, 0x15, 0x49, 0x2a, 0x40, 0x82,
    0x90,

    /* U+003A ":" */
    0x20, 0x0, 0x20,

    /* U+003B ";" */
    0x20, 0x0, 0x32, 0x0,

    /* U+003C "<" */
    0x0, 0x1, 0x9a, 0x0, 0x64, 0x0, 0x40,

    /* U+003D "=" */
    0x6a, 0x40, 0x6, 0xa4,

    /* U+003E ">" */
    0x0, 0x19, 0x0, 0x28, 0x64, 0x40, 0x0,

    /* U+003F "?" */
    0x29, 0x2, 0x5, 0x8, 0x14, 0x0, 0x14,

    /* U+0040 "@" */
    0x6, 0xa4, 0x14, 0x1, 0x21, 0xa2, 0x55, 0x22,
    0x44, 0x22, 0x52, 0x64, 0x20, 0x0, 0x6, 0x90,

    /* U+0041 "A" */
    0xd, 0x0, 0xa0, 0x22, 0x3, 0x14, 0x3a, 0x85,
    0xc, 0xc0, 0x90,

    /* U+0042 "B" */
    0x3a, 0x42, 0xc, 0x20, 0xc3, 0xb8, 0x20, 0x92,
    0x9, 0x3a, 0x80,

    /* U+0043 "C" */
    0xa, 0x83, 0x0, 0x60, 0x5, 0x0, 0x60, 0x3,
    0x0, 0xa, 0x80,

    /* U+0044 "D" */
    0x3a, 0x42, 0x8, 0x20, 0x62, 0x2, 0x20, 0x62,
    0xc, 0x3a, 0x40,

    /* U+0045 "E" */
    0x3a, 0x48, 0x2, 0x0, 0xe8, 0x20, 0x8, 0x3,
    0xa8,

    /* U+0046 "F" */
    0x3a, 0x48, 0x2, 0x0, 0xe8, 0x20, 0x8, 0x2,
    0x0,

    /* U+0047 "G" */
    0xa, 0x83, 0x0, 0x60, 0x5, 0x19, 0x60, 0x23,
    0x6, 0xa, 0xc0,

    /* U+0048 "H" */
    0x20, 0x22, 0x2, 0x20, 0x23, 0xae, 0x20, 0x22,
    0x2, 0x20, 0x20,

    /* U+0049 "I" */
    0x22, 0x22, 0x22, 0x20,

    /* U+004A "J" */
    0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x2d,

    /* U+004B "K" */
    0x20, 0xc2, 0x24, 0x26, 0x3, 0xf0, 0x32, 0x42,
    0xc, 0x20, 0x60,

    /* U+004C "L" */
    0x20, 0x8, 0x2, 0x0, 0x80, 0x20, 0x8, 0x3,
    0xa4,

    /* U+004D "M" */
    0x30, 0x34, 0xd0, 0xd2, 0x86, 0x4a, 0x25, 0x26,
    0x94, 0x8c, 0x52, 0x1, 0x40,

    /* U+004E "N" */
    0x30, 0x23, 0x82, 0x28, 0x22, 0x22, 0x22, 0x62,
    0xe, 0x20, 0xa0,

    /* U+004F "O" */
    0xa, 0x80, 0xc1, 0x86, 0x3, 0x14, 0x8, 0x60,
    0x30, 0xc1, 0x80, 0xa8, 0x0,

    /* U+0050 "P" */
    0x3b, 0x42, 0xc, 0x20, 0x82, 0xc, 0x3a, 0x42,
    0x0, 0x20, 0x0,

    /* U+0051 "Q" */
    0x1a, 0x80, 0xc0, 0x85, 0x3, 0x14, 0xc, 0x20,
    0x30, 0xa6, 0x40, 0x34, 0x0, 0x28,

    /* U+0052 "R" */
    0x3a, 0x82, 0x8, 0x20, 0xc3, 0xb4, 0x22, 0x2,
    0x8, 0x20, 0x80,

    /* U+0053 "S" */
    0x2e, 0x48, 0x3, 0x40, 0x2c, 0x1, 0xc0, 0x32,
    0xb4,

    /* U+0054 "T" */
    0xae, 0x80, 0x80, 0x8, 0x0, 0x80, 0x8, 0x0,
    0x80, 0x8, 0x0,

    /* U+0055 "U" */
    0x20, 0x22, 0x2, 0x20, 0x22, 0x2, 0x20, 0x23,
    0x9, 0x1e, 0x80,

    /* U+0056 "V" */
    0xc0, 0xc9, 0x8, 0x21, 0x43, 0x20, 0x22, 0x1,
    0xa0, 0xc, 0x0,

    /* U+0057 "W" */
    0x82, 0x42, 0x52, 0x85, 0x62, 0x88, 0x21, 0x8c,
    0x34, 0x58, 0x28, 0x28, 0x18, 0x34,

    /* U+0058 "X" */
    0x50, 0x83, 0x20, 0x1a, 0x0, 0xc0, 0x26, 0x2,
    0x20, 0x90, 0x80,

    /* U+0059 "Y" */
    0x30, 0x61, 0x88, 0xc, 0x80, 0x70, 0x3, 0x0,
    0x30, 0x3, 0x0,

    /* U+005A "Z" */
    0x2a, 0xc0, 0x90, 0x20, 0x20, 0x18, 0xc, 0x7,
    0xa8,

    /* U+005B "[" */
    0x34, 0x82, 0x8, 0x20, 0x82, 0x8, 0x24,

    /* U+005C "\\" */
    0x80, 0x40, 0x20, 0x20, 0x10, 0x4, 0x8, 0x4,
    0x0,

    /* U+005D "]" */
    0x70, 0x82, 0x8, 0x20, 0x82, 0x8, 0x60,

    /* U+005E "^" */
    0xc, 0x5, 0x42, 0x20, 0x88,

    /* U+005F "_" */
    0x6a, 0x40,

    /* U+0060 "`" */
    0x40, 0x80, 0x0,

    /* U+0061 "a" */
    0x2a, 0x0, 0x82, 0xa5, 0x49, 0x2a, 0x40,

    /* U+0062 "b" */
    0x20, 0x2, 0x0, 0x3b, 0x42, 0xc, 0x20, 0xc2,
    0xc, 0x3b, 0x40,

    /* U+0063 "c" */
    0x2a, 0x18, 0x5, 0x1, 0x80, 0x2a, 0x0,

    /* U+0064 "d" */
    0x0, 0x80, 0x22, 0xa9, 0x82, 0x50, 0x98, 0x22,
    0xe8,

    /* U+0065 "e" */
    0x2a, 0x18, 0x56, 0xa5, 0x80, 0x2a, 0x0,

    /* U+0066 "f" */
    0x18, 0x30, 0x78, 0x30, 0x30, 0x30, 0x30,

    /* U+0067 "g" */
    0x2b, 0x98, 0x83, 0x90, 0x80, 0x3a, 0x54, 0x32,
    0xa4,

    /* U+0068 "h" */
    0x20, 0x8, 0x3, 0xb4, 0xc2, 0x20, 0x88, 0x22,
    0x8,

    /* U+0069 "i" */
    0x20, 0x22, 0x22, 0x20,

    /* U+006A "j" */
    0x8, 0x0, 0x82, 0x8, 0x20, 0x82, 0x24,

    /* U+006B "k" */
    0x20, 0x8, 0x2, 0x24, 0x94, 0x3d, 0xc, 0xc2,
    0x18,

    /* U+006C "l" */
    0x20, 0x82, 0x8, 0x20, 0x83, 0x0,

    /* U+006D "m" */
    0x2b, 0x6c, 0x31, 0x85, 0x20, 0x86, 0x20, 0x82,
    0x20, 0x82,

    /* U+006E "n" */
    0x2b, 0x4c, 0x22, 0x8, 0x82, 0x20, 0x80,

    /* U+006F "o" */
    0x2a, 0x18, 0x35, 0xd, 0x83, 0x2a, 0x0,

    /* U+0070 "p" */
    0x2b, 0x42, 0xc, 0x20, 0xc2, 0xc, 0x3b, 0x42,
    0x0, 0x20, 0x0,

    /* U+0071 "q" */
    0x2a, 0x98, 0x25, 0x9, 0x82, 0x2e, 0x80, 0x20,
    0x8,

    /* U+0072 "r" */
    0x29, 0x30, 0x20, 0x20, 0x20,

    /* U+0073 "s" */
    0x29, 0x60, 0x18, 0x2, 0x69,

    /* U+0074 "t" */
    0x10, 0x30, 0xb8, 0x30, 0x30, 0x30, 0x28,

    /* U+0075 "u" */
    0x20, 0x88, 0x22, 0x8, 0x86, 0x29, 0x80,

    /* U+0076 "v" */
    0x81, 0x54, 0x82, 0x20, 0x94, 0x1c, 0x0,

    /* U+0077 "w" */
    0x83, 0xd, 0x49, 0x22, 0x49, 0x4a, 0x28, 0x28,
    0x70,

    /* U+0078 "x" */
    0x53, 0xa, 0x41, 0xc0, 0x94, 0x93, 0x0,

    /* U+0079 "y" */
    0x81, 0x58, 0x83, 0x20, 0x64, 0xc, 0x2, 0x6,
    0x0,

    /* U+007A "z" */
    0x6b, 0x2, 0x1, 0x80, 0xc0, 0xba, 0x0,

    /* U+007B "{" */
    0x24, 0x82, 0x8, 0x60, 0x82, 0x8, 0x14,

    /* U+007C "|" */
    0x11, 0x11, 0x11, 0x11, 0x11,

    /* U+007D "}" */
    0x60, 0x82, 0x8, 0x18, 0x82, 0x8, 0x50,

    /* U+007E "~" */
    0x24, 0x1, 0x80
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 32, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 47, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 6, .adv_w = 68, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 9, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 18, .adv_w = 80, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 30, .adv_w = 133, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 44, .adv_w = 98, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 55, .adv_w = 40, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 57, .adv_w = 49, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 65, .adv_w = 49, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 73, .adv_w = 67, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 76, .adv_w = 80, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 83, .adv_w = 40, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 85, .adv_w = 50, .box_w = 3, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 86, .adv_w = 40, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 87, .adv_w = 56, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 96, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 105, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 114, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 123, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 132, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 141, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 150, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 159, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 168, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 177, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 186, .adv_w = 40, .box_w = 2, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 189, .adv_w = 40, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 193, .adv_w = 80, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 200, .adv_w = 80, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 204, .adv_w = 80, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 211, .adv_w = 68, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 218, .adv_w = 136, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 234, .adv_w = 88, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 245, .adv_w = 95, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 256, .adv_w = 92, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 267, .adv_w = 99, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 278, .adv_w = 85, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 287, .adv_w = 79, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 296, .adv_w = 99, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 307, .adv_w = 105, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 318, .adv_w = 42, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 322, .adv_w = 77, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 329, .adv_w = 93, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 340, .adv_w = 78, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 349, .adv_w = 117, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 362, .adv_w = 104, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 373, .adv_w = 107, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 386, .adv_w = 91, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 397, .adv_w = 107, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 411, .adv_w = 91, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 422, .adv_w = 86, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 431, .adv_w = 86, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 442, .adv_w = 104, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 453, .adv_w = 83, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 464, .adv_w = 126, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 478, .adv_w = 83, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 489, .adv_w = 76, .box_w = 6, .box_h = 7, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 500, .adv_w = 87, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 509, .adv_w = 49, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 516, .adv_w = 56, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 525, .adv_w = 49, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 532, .adv_w = 80, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 537, .adv_w = 80, .box_w = 5, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 539, .adv_w = 87, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 542, .adv_w = 81, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 549, .adv_w = 89, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 560, .adv_w = 73, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 567, .adv_w = 89, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 576, .adv_w = 80, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 583, .adv_w = 47, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 590, .adv_w = 81, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 599, .adv_w = 87, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 608, .adv_w = 40, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 612, .adv_w = 40, .box_w = 3, .box_h = 9, .ofs_x = -1, .ofs_y = -2},
    {.bitmap_index = 619, .adv_w = 79, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 628, .adv_w = 41, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 634, .adv_w = 133, .box_w = 8, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 644, .adv_w = 88, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 651, .adv_w = 87, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 658, .adv_w = 89, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 669, .adv_w = 89, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 678, .adv_w = 56, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 683, .adv_w = 67, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 688, .adv_w = 54, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 695, .adv_w = 87, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 702, .adv_w = 75, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 709, .adv_w = 115, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 718, .adv_w = 72, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 725, .adv_w = 75, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 734, .adv_w = 68, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 741, .adv_w = 49, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 748, .adv_w = 39, .box_w = 2, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 753, .adv_w = 49, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 760, .adv_w = 80, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = 2}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Map glyph_ids to kern left classes*/
static const uint8_t kern_left_class_mapping[] =
{
    0, 0, 0, 1, 0, 0, 0, 0,
    1, 2, 0, 0, 0, 3, 4, 3,
    5, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 6, 6, 0, 0, 0,
    0, 0, 7, 8, 9, 10, 11, 12,
    13, 0, 0, 14, 15, 16, 0, 0,
    10, 17, 10, 18, 19, 20, 21, 22,
    23, 24, 25, 26, 2, 27, 0, 0,
    0, 0, 28, 29, 30, 0, 31, 32,
    33, 34, 0, 0, 35, 36, 34, 34,
    29, 29, 37, 38, 39, 40, 37, 41,
    42, 43, 44, 45, 2, 0, 0, 0
};

/*Map glyph_ids to kern right classes*/
static const uint8_t kern_right_class_mapping[] =
{
    0, 0, 1, 2, 0, 0, 0, 0,
    2, 0, 3, 4, 0, 5, 6, 7,
    8, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 9, 10, 0, 0, 0,
    11, 0, 12, 0, 13, 0, 0, 0,
    13, 0, 0, 14, 0, 0, 0, 0,
    13, 0, 13, 0, 15, 16, 17, 18,
    19, 20, 21, 22, 0, 23, 3, 0,
    0, 0, 24, 0, 25, 25, 25, 26,
    27, 0, 28, 29, 0, 0, 30, 30,
    25, 30, 25, 30, 31, 32, 33, 34,
    35, 36, 37, 38, 0, 0, 3, 0
};

/*Kern values between classes*/
static const int8_t kern_class_values[] =
{
    0, 0, 0, 0, -18, 0, -18, 0,
    0, 0, 0, -9, 0, -15, -2, 0,
    0, 0, 0, -2, 0, 0, 0, 0,
    -5, 0, 0, 0, 0, 0, -3, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -3, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 13, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -15, 0, -22,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -16, -3, -11, -5, 0,
    -15, 0, 0, 0, -2, 0, 0, 0,
    4, 0, 0, -7, 0, -5, -4, 0,
    -3, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -3,
    -3, -8, 0, -3, -2, -4, -11, -3,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -4, 0, -1, 0, -2, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -6, -2, -13, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -4,
    -5, 0, -2, 4, 4, 0, 0, 1,
    -3, 0, 0, 0, 0, 0, 0, 0,
    0, -8, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -4, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -9, 0, -15,
    0, 0, 0, 0, 0, 0, -4, -1,
    -2, 0, 0, -9, -2, -2, 0, 1,
    -2, -1, -6, 4, 0, -2, 0, 0,
    0, 0, 4, -2, -1, -1, -1, -1,
    -1, 0, 0, 0, 0, -5, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -3,
    -2, -4, 0, -1, -1, -1, -2, -1,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, -2, -2, -2, -2, 0,
    0, 0, 0, 0, 0, -4, 0, 0,
    0, 0, 0, 0, -5, -2, -4, -3,
    -2, -1, -1, -1, -1, -2, 0, 0,
    0, 0, -3, 0, 0, 0, 0, -4,
    -2, -2, -2, 0, -2, 0, 0, 0,
    0, -5, 0, 0, 0, -3, 0, 0,
    0, -2, 0, -6, 0, -4, 0, -2,
    -1, -3, -3, -3, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, -1, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    0, 0, 0, -4, 0, -2, 0, -5,
    -2, 0, 0, 0, 0, 0, -12, 0,
    -12, -11, 0, 0, 0, -6, -2, -22,
    -3, 0, 0, 1, 1, -4, 0, -5,
    0, -5, -2, 0, -4, 0, 0, -3,
    -3, -2, -3, -3, -3, -4, -3, -5,
    0, 0, 0, -4, 0, 0, 0, 0,
    0, 0, 0, -1, 0, 0, 0, -3,
    0, -2, -1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -4, 0, -4, 0, 0, 0,
    0, 0, 0, -6, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -3, 0, -6,
    0, -5, 0, 0, 0, 0, -1, -2,
    -3, 0, -1, -3, -2, -2, -2, 0,
    -3, 0, 0, 0, -1, 0, 0, 0,
    -2, 0, 0, -5, -2, -3, -3, -3,
    -3, -2, 0, -14, 0, -24, 0, -9,
    0, 0, 0, 0, -5, 0, -4, 0,
    -4, -19, -5, -12, -9, 0, -12, 0,
    -13, 0, -2, -2, -1, 0, 0, 0,
    0, -3, -2, -6, -5, 0, -6, 0,
    0, 0, 0, 0, -18, -5, -18, -12,
    0, 0, 0, -8, 0, -24, -2, -4,
    0, 0, 0, -4, -2, -13, 0, -7,
    -4, 0, -5, 0, 0, 0, -2, 0,
    0, 0, 0, -2, 0, -3, 0, 0,
    0, -2, 0, -5, 0, 0, 0, 0,
    0, -1, 0, -3, -2, -2, 0, 1,
    1, -1, 0, -2, 0, -1, -2, 0,
    -1, 0, 0, 0, 0, 0, 0, 0,
    0, -1, 0, -1, 0, 0, 0, -3,
    0, 2, 0, 0, 0, 0, 0, 0,
    0, -2, -2, -3, 0, 0, 0, 0,
    -2, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -4, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -17, -12,
    -17, -14, -3, -3, 0, -6, -4, -20,
    -6, 0, 0, 0, 0, -3, -2, -9,
    0, -12, -11, -3, -12, 0, 0, -7,
    -10, -3, -7, -5, -5, -6, -5, -12,
    0, 0, 0, 0, -3, 0, -3, -5,
    0, 0, 0, -3, 0, -8, -2, 0,
    0, -1, 0, -2, -2, 0, 0, -1,
    0, 0, -2, 0, 0, 0, -1, 0,
    0, 0, 0, -1, 0, 0, 0, 0,
    0, 0, -11, -3, -11, -7, 0, 0,
    0, -2, -2, -12, -2, 0, -2, 2,
    0, 0, 0, -3, 0, -4, -2, 0,
    -3, 0, 0, -3, -2, 0, -5, -1,
    -1, -2, -1, -4, 0, 0, 0, 0,
    -5, -2, -5, -5, 0, 0, 0, 0,
    -1, -11, -1, 0, 0, 0, 0, 0,
    0, -1, 0, -3, 0, 0, -2, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, -2, 0, -2, 0, -4,
    0, 0, 0, 0, 0, 0, -3, 0,
    -2, -3, -2, 0, 0, 0, 0, 0,
    0, -2, -1, -3, 0, 0, 0, 0,
    0, -3, -2, -3, -2, -2, -3, -2,
    0, 0, 0, 0, -15, -11, -15, -11,
    -4, -4, -1, -2, -2, -16, -3, -2,
    -2, 0, 0, 0, 0, -4, 0, -11,
    -7, 0, -10, 0, 0, -6, -7, -4,
    -5, -2, -4, -5, -2, -8, 0, 0,
    0, 0, 0, -5, 0, 0, 0, 0,
    0, -1, -3, -5, -5, 0, -2, -1,
    -1, 0, -2, -3, 0, -3, -3, -3,
    -2, 0, 0, 0, 0, -2, -4, -3,
    -3, -4, -3, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -14, -5, -8, -5, 0,
    -12, 0, 0, 0, 0, 0, 5, 0,
    12, 0, 0, 0, 0, -3, -2, 0,
    2, 0, 0, 0, 0, -9, 0, 0,
    0, 0, 0, 0, -2, 0, 0, 0,
    0, -4, 0, -3, -1, 0, -4, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, -2, 0, 0, 0, 0, 0, 0,
    0, -5, 0, -4, -2, 1, -2, 0,
    0, 0, -2, 0, 0, 0, 0, -9,
    0, -3, 0, -1, -7, 0, -4, -2,
    0, 0, 0, 0, 0, 0, 0, -3,
    0, -1, -1, -3, -1, -1, 0, 0,
    0, 0, 0, -3, 0, 0, 0, 0,
    0, 0, 0, 0, 0, -3, 0, -2,
    0, 0, -4, 0, 0, -2, -3, 0,
    -2, 0, 0, 0, 0, -2, 0, 1,
    1, 1, 1, 0, 0, 0, 0, -5,
    0, 2, 0, 0, 0, 0, -1, 0,
    0, -3, -3, -4, 0, -3, -2, 0,
    -4, 0, -3, -2, 0, 0, -2, 0,
    0, 0, 0, -2, 0, 1, 1, -1,
    1, 0, 2, 6, 8, 0, -8, -2,
    -8, -2, 0, 0, 4, 0, 0, 0,
    0, 7, 0, 11, 7, 5, 10, 0,
    10, -3, -2, 0, -2, 0, -2, 0,
    -1, 0, 0, 2, 0, -1, 0, -2,
    0, 0, 2, -5, 0, 0, 0, 8,
    0, 0, -6, 0, 0, 0, 0, -4,
    0, 0, 0, 0, -2, 0, 0, -3,
    -2, 0, 0, 0, 6, 0, 0, 0,
    0, -1, -1, 0, 2, -2, 0, 0,
    0, -5, 0, 0, 0, 0, 0, 0,
    -1, 0, 0, 0, 0, -4, 0, -2,
    0, 0, -3, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -3,
    2, -7, 2, 0, 2, 2, -2, 0,
    0, 0, 0, -6, 0, 0, 0, 0,
    -2, 0, 0, -2, -3, 0, -2, 0,
    -2, 0, 0, -3, -2, 0, 0, -1,
    0, -1, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -4, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -3,
    0, -2, 0, 0, -5, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, -9, -4, -9, -5, 4, 4,
    0, -2, 0, -9, 0, 0, 0, 0,
    0, 0, 0, -2, 2, -4, -2, 0,
    -2, 0, 0, 0, -1, 0, 0, 4,
    3, 0, 4, -1, 0, 0, 0, -8,
    0, 2, 0, 0, 0, 0, -2, 0,
    0, 0, 0, -4, 0, -2, 0, 0,
    -3, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, -3, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, -4,
    1, 2, 2, 2, -4, 0, 0, 0,
    0, -2, 0, 0, 0, 0, -1, 0,
    0, -3, -2, 0, -2, 0, 0, 0,
    -2, -3, 0, 0, 0, -3, 0, 0,
    0, 0, 0, -2, -5, -1, -5, -3,
    0, 0, 0, -2, 0, -6, 0, -3,
    0, -1, 0, 0, -2, -2, 0, -3,
    -1, 0, 0, 0, -2, 0, 0, 0,
    0, 0, 0, 0, 0, -4, 0, 0,
    0, -2, -6, 0, -6, -1, 0, 0,
    0, -1, 0, -5, 0, -4, 0, -1,
    0, -2, -4, 0, 0, -2, -1, 0,
    0, 0, -2, 0, 0, 0, 0, 0,
    0, 0, 0, -3, -2, 0, 0, -4,
    1, -2, -1, 0, 0, 1, 0, 0,
    -2, 0, -1, -5, 0, -2, 0, -2,
    -5, 0, 0, -2, -3, 0, 0, 0,
    0, 0, 0, -4, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, -5, 0,
    -5, -2, 0, 0, 0, 0, 0, -6,
    0, -3, 0, -1, 0, -1, -1, 0,
    0, -3, -1, 0, 0, 0, -2, 0,
    0, 0, 0, 0, 0, -2, 0, -4,
    0, 0, 0, 0, 0, -3, 0, 0,
    0, 0, 0, 0, 0, 0, 0, -4,
    0, 0, 0, 0, -5, 0, 0, -4,
    -2, 0, -1, 0, 0, 0, 0, 0,
    -2, -1, 0, 0, -1, 0
};


/*Collect the kern class' data in one place*/
static const lv_font_fmt_txt_kern_classes_t kern_classes =
{
    .class_pair_values   = kern_class_values,
    .left_class_mapping  = kern_left_class_mapping,
    .right_class_mapping = kern_right_class_mapping,
    .left_class_cnt      = 45,
    .right_class_cnt     = 38,
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_classes,
    .kern_scale = 16,
    .cmap_num = 1,
    .bpp = 2,
    .kern_classes = 1,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ecard_font_9 = {
#else
lv_font_t ecard_font_9 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 10,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if ECARD_FONT_9*/

