/*******************************************************************************
 * Size: 9 px
 * Bpp: 2
 * Opts: --font /Users/neo/orca/workspaces/AI-Passport/neo-10/assets/fonts/.cache/NotoSansCJKsc-Regular.otf --range 0x20-0x7E --symbols  --size 9 --bpp 2 --format lvgl --lv-font-name ecard_font_9 --lv-include lvgl.h --output /Users/neo/orca/workspaces/AI-Passport/neo-10/assets/fonts/ecard_font_9.c
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
    0x30, 0xe4, 0x3b, 0x2, 0xc,

    /* U+0022 "\"" */
    0x25, 0x7, 0x6a,

    /* U+0023 "#" */
    0x11, 0xd, 0x82, 0x8a, 0x19, 0x74, 0xba, 0x89,
    0x84,

    /* U+0024 "$" */
    0x2, 0x13, 0x20, 0xb0, 0xe4, 0x16, 0x81, 0xe0,
    0xca, 0x28, 0x24, 0x40,

    /* U+0025 "%" */
    0x28, 0x8, 0x14, 0xc1, 0x87, 0x32, 0xc, 0xcd,
    0x86, 0xf3, 0xd0, 0x9c, 0x3d, 0x27, 0x60,

    /* U+0026 "&" */
    0x1a, 0x5, 0xc1, 0xd6, 0x19, 0x44, 0xf4, 0x7c,
    0x24, 0xd4, 0x50,

    /* U+0027 "'" */
    0x20, 0xb0,

    /* U+0028 "(" */
    0xe, 0x49, 0xf, 0xfe, 0x26, 0x5, 0x0,

    /* U+0029 ")" */
    0x9, 0x43, 0x87, 0xff, 0x17, 0x14, 0x0,

    /* U+002A "*" */
    0x2, 0xe, 0x3, 0x0,

    /* U+002B "+" */
    0x2, 0x1b, 0x3, 0x32, 0xa6, 0x43, 0x80,

    /* U+002C "," */
    0xb, 0x12,

    /* U+002D "-" */
    0x64,

    /* U+002E "." */
    0xa,

    /* U+002F "/" */
    0x2, 0x16, 0x1e, 0x70, 0x68, 0x73, 0x83, 0x41,
    0x84,

    /* U+0030 "0" */
    0x2a, 0xa, 0x3, 0x9b, 0x85, 0xe0, 0xb7, 0x34,
    0x10, 0x0,

    /* U+0031 "1" */
    0x2c, 0x28, 0x39, 0xf, 0xfe, 0x34, 0xc8,

    /* U+0032 "2" */
    0x2a, 0x5, 0x3, 0xfc, 0x85, 0x40, 0x5a, 0x2b,
    0x90,

    /* U+0033 "3" */
    0x2a, 0x5, 0x3, 0x90, 0x3c, 0x7, 0xc1, 0xca,
    0xa0, 0x80,

    /* U+0034 "4" */
    0x3, 0x85, 0xa0, 0xa2, 0x38, 0x5e, 0x86, 0xa1,
    0xe,

    /* U+0035 "5" */
    0x3a, 0x1, 0xa0, 0x1a, 0x3, 0x84, 0x2c, 0x36,
    0x36, 0x20,

    /* U+0036 "6" */
    0x1a, 0x2, 0xa1, 0x83, 0x74, 0x7, 0x6a, 0xc1,
    0x9d, 0xc0,

    /* U+0037 "7" */
    0x69, 0xaa, 0x8a, 0x3, 0x85, 0xa1, 0x21, 0x90,
    0xf8,

    /* U+0038 "8" */
    0x2a, 0xa, 0xc0, 0x98, 0x1c, 0xf, 0x87, 0x7,
    0xae, 0xe0,

    /* U+0039 "9" */
    0x28, 0x8f, 0x66, 0x9b, 0xa6, 0x3c, 0x15, 0x4,
    0xa6, 0x0,

    /* U+003A ":" */
    0x22, 0xe, 0x80,

    /* U+003B ";" */
    0x22, 0xe, 0xc4, 0x80,

    /* U+003C "<" */
    0xf, 0x99, 0xa3, 0x37, 0x20, 0x65, 0x0,

    /* U+003D "=" */
    0x6a, 0x55, 0x4a, 0xa9,

    /* U+003E ">" */
    0xe, 0x64, 0xc, 0xd0, 0x6e, 0x54, 0x80,

    /* U+003F "?" */
    0x28, 0x94, 0xc0, 0xe0, 0xd1, 0xc0, 0xa0, 0x28,

    /* U+0040 "@" */
    0x3, 0x52, 0x4, 0xa9, 0x46, 0xaa, 0x1b, 0xa4,
    0x19, 0x10, 0xe5, 0x48, 0xce, 0x4c, 0x82, 0x68,
    0x80,

    /* U+0041 "A" */
    0x6, 0x86, 0x70, 0xa8, 0x19, 0x34, 0x2b, 0xc3,
    0x52, 0x48, 0x14,

    /* U+0042 "B" */
    0x3a, 0x20, 0x6a, 0xf, 0xcd, 0xa0, 0x6c, 0x43,
    0xe6, 0x82,

    /* U+0043 "C" */
    0x5, 0x40, 0xea, 0x14, 0x1d, 0x87, 0x61, 0x94,
    0x1d, 0xd4, 0x0,

    /* U+0044 "D" */
    0x3a, 0x20, 0x69, 0x87, 0x70, 0x64, 0x39, 0xe,
    0xa0, 0xd4, 0x0,

    /* U+0045 "E" */
    0x3a, 0x23, 0x44, 0x3c, 0xd0, 0xd, 0x3, 0xe6,
    0xa0,

    /* U+0046 "F" */
    0x3a, 0x23, 0x44, 0x3c, 0xd0, 0xd, 0x3, 0xfe,

    /* U+0047 "G" */
    0x5, 0x40, 0xea, 0x14, 0x1d, 0x8c, 0x98, 0xda,
    0x80, 0x83, 0xac,

    /* U+0048 "H" */
    0x20, 0xa0, 0xff, 0xe0, 0xb4, 0xc0, 0xd3, 0xf,
    0xfe, 0x8,

    /* U+0049 "I" */
    0x20, 0xff, 0xe0, 0x0,

    /* U+004A "J" */
    0xb, 0xf, 0xfe, 0x4d, 0xc0,

    /* U+004B "K" */
    0x20, 0x61, 0xa8, 0x12, 0x20, 0x64, 0x36, 0xa0,
    0x25, 0x3, 0xa8,

    /* U+004C "L" */
    0x20, 0xff, 0xe8, 0xb4, 0x40,

    /* U+004D "M" */
    0x30, 0xb4, 0x8, 0x73, 0x8a, 0xe, 0xf0, 0x5c,
    0x1c, 0xa8, 0x3b, 0x8,

    /* U+004E "N" */
    0x30, 0xa0, 0x41, 0x90, 0xf5, 0x3, 0xc8, 0x6a,
    0x7, 0x20,

    /* U+004F "O" */
    0x5, 0x41, 0x74, 0xe1, 0x40, 0x50, 0x30, 0x90,
    0x61, 0x22, 0x80, 0xa0, 0x74, 0xe0,

    /* U+0050 "P" */
    0x3b, 0x40, 0xdc, 0x1c, 0x87, 0x20, 0x6a, 0x3,
    0x44, 0x3e,

    /* U+0051 "Q" */
    0x1a, 0x82, 0xae, 0x18, 0x24, 0x3f, 0x38, 0x7d,
    0x34, 0x5, 0x5a, 0x1b, 0xd0,

    /* U+0052 "R" */
    0x3a, 0x80, 0xd0, 0x3c, 0x81, 0xb8, 0xc, 0xa0,
    0xd4, 0xf, 0x80,

    /* U+0053 "S" */
    0x2e, 0x41, 0xc8, 0xa0, 0xbe, 0xa, 0x98, 0x48,
    0x29, 0xc0,

    /* U+0054 "T" */
    0xa7, 0x42, 0x8d, 0x3, 0xff, 0xa6,

    /* U+0055 "U" */
    0x20, 0xa0, 0xff, 0xe6, 0xa0, 0xb2, 0xe1,

    /* U+0056 "V" */
    0xc2, 0xc5, 0x1, 0x2c, 0x70, 0x26, 0x81, 0xe,
    0xe0, 0xca, 0x80,

    /* U+0057 "W" */
    0x81, 0x20, 0xb4, 0x18, 0xe6, 0x1b, 0x50, 0x30,
    0x20, 0x5b, 0x50, 0x1c, 0x70, 0xb0, 0x9c,

    /* U+0058 "X" */
    0x50, 0x21, 0x8a, 0x2, 0x81, 0x95, 0x5, 0x50,
    0x64, 0x2b, 0x28, 0x0,

    /* U+0059 "Y" */
    0x30, 0x31, 0x43, 0x85, 0x7, 0x5c, 0x12, 0x1f,
    0xfc, 0x30,

    /* U+005A "Z" */
    0x2a, 0xca, 0x10, 0x64, 0x14, 0x2, 0x1a, 0x80,
    0x4a, 0x80,

    /* U+005B "[" */
    0x34, 0x50, 0x7f, 0xf2, 0x10,

    /* U+005C "\\" */
    0x82, 0xc2, 0x60, 0xfb, 0x9, 0x41, 0x61, 0x61,
    0x20,

    /* U+005D "]" */
    0x71, 0x41, 0xff, 0xc7, 0x40,

    /* U+005E "^" */
    0x6, 0x13, 0x20, 0xdc, 0x3c,

    /* U+005F "_" */
    0x6a, 0x40,

    /* U+0060 "`" */
    0x40, 0xc0, 0x80,

    /* U+0061 "a" */
    0x2a, 0x5, 0x2, 0xa0, 0xae, 0x3, 0xc0,

    /* U+0062 "b" */
    0x20, 0xff, 0x9b, 0x40, 0xdc, 0x1f, 0xfc, 0x26,
    0xe0,

    /* U+0063 "c" */
    0x2a, 0x12, 0x80, 0xc3, 0x61, 0x25, 0x0,

    /* U+0064 "d" */
    0xd, 0x7, 0xaa, 0x12, 0x80, 0xc3, 0x61, 0x27,
    0x0,

    /* U+0065 "e" */
    0x2a, 0x12, 0xd0, 0x58, 0x54, 0x51, 0x40,

    /* U+0066 "f" */
    0x18, 0x14, 0x12, 0x12, 0xf, 0xfe, 0x8,

    /* U+0067 "g" */
    0x29, 0xc9, 0x32, 0xac, 0xc, 0x81, 0xa2, 0xab,
    0x3d, 0x40,

    /* U+0068 "h" */
    0x20, 0xfe, 0x6d, 0x5, 0xe1, 0xf, 0xfe, 0x8,

    /* U+0069 "i" */
    0x22, 0x20, 0xfc,

    /* U+006A "j" */
    0x4, 0x8, 0x10, 0x7f, 0xf1, 0x2c,

    /* U+006B "k" */
    0x20, 0xff, 0xa4, 0xe, 0x8c, 0x1b, 0x80, 0x94,

    /* U+006C "l" */
    0x20, 0xff, 0xe2, 0xa0,

    /* U+006D "m" */
    0x29, 0xb6, 0x6, 0x9d, 0x11, 0x10, 0xb0, 0xf2,
    0x1f, 0xc0,

    /* U+006E "n" */
    0x29, 0xa3, 0x78, 0x43, 0xff, 0x82,

    /* U+006F "o" */
    0x2a, 0x12, 0x99, 0x86, 0xc2, 0x4a, 0x60,

    /* U+0070 "p" */
    0x29, 0xa1, 0x5c, 0x1f, 0xfc, 0x26, 0xe0, 0x36,
    0x87, 0xc0,

    /* U+0071 "q" */
    0x2b, 0x25, 0x1, 0x86, 0xc2, 0x4e, 0x5, 0xc1,
    0xe0,

    /* U+0072 "r" */
    0x28, 0x8c, 0x88, 0x7f, 0x80,

    /* U+0073 "s" */
    0x28, 0xa2, 0x57, 0x1, 0xa3, 0x4c,

    /* U+0074 "t" */
    0x10, 0xa0, 0x44, 0x44, 0x1f, 0xe6, 0x0,

    /* U+0075 "u" */
    0x20, 0x41, 0xff, 0xc1, 0x42, 0x80,

    /* U+0076 "v" */
    0x80, 0xad, 0x35, 0x61, 0xce, 0xe, 0x40,

    /* U+0077 "w" */
    0x81, 0x83, 0xc8, 0xa1, 0x5b, 0x6e, 0xc, 0x1a,
    0x1b, 0x40,

    /* U+0078 "x" */
    0x51, 0x8f, 0x40, 0x6a, 0x7, 0x25, 0xb0,

    /* U+0079 "y" */
    0x80, 0xae, 0x35, 0x83, 0x53, 0x2, 0xc1, 0x20,
    0x68, 0x0,

    /* U+007A "z" */
    0x69, 0x8c, 0x60, 0x43, 0x50, 0x11, 0x40,

    /* U+007B "{" */
    0x24, 0x8, 0x79, 0x2, 0x1f, 0xda,

    /* U+007C "|" */
    0x10, 0xff, 0xe3, 0x0,

    /* U+007D "}" */
    0x61, 0xf, 0xee, 0x38, 0x3c, 0xe0,

    /* U+007E "~" */
    0x24, 0x28, 0x80
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 32, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 47, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 5, .adv_w = 68, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 8, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 17, .adv_w = 80, .box_w = 5, .box_h = 9, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 29, .adv_w = 133, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 44, .adv_w = 98, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 55, .adv_w = 40, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 57, .adv_w = 49, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 64, .adv_w = 49, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 71, .adv_w = 67, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 75, .adv_w = 80, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 82, .adv_w = 40, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 84, .adv_w = 50, .box_w = 3, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 85, .adv_w = 40, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 86, .adv_w = 56, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 95, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 105, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 112, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 121, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 131, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 140, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 150, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 160, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 169, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 179, .adv_w = 80, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 189, .adv_w = 40, .box_w = 2, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 192, .adv_w = 40, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 196, .adv_w = 80, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 203, .adv_w = 80, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 207, .adv_w = 80, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 214, .adv_w = 68, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 222, .adv_w = 136, .box_w = 8, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 239, .adv_w = 88, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 250, .adv_w = 95, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 260, .adv_w = 92, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 271, .adv_w = 99, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 282, .adv_w = 85, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 291, .adv_w = 79, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 299, .adv_w = 99, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 310, .adv_w = 105, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 320, .adv_w = 42, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 324, .adv_w = 77, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 329, .adv_w = 93, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 340, .adv_w = 78, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 345, .adv_w = 117, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 357, .adv_w = 104, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 367, .adv_w = 107, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 381, .adv_w = 91, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 391, .adv_w = 107, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 404, .adv_w = 91, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 415, .adv_w = 86, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 425, .adv_w = 86, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 431, .adv_w = 104, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 438, .adv_w = 83, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 449, .adv_w = 126, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 464, .adv_w = 83, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 476, .adv_w = 76, .box_w = 6, .box_h = 7, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 486, .adv_w = 87, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 496, .adv_w = 49, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 501, .adv_w = 56, .box_w = 4, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 510, .adv_w = 49, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 515, .adv_w = 80, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 520, .adv_w = 80, .box_w = 5, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 522, .adv_w = 87, .box_w = 3, .box_h = 3, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 525, .adv_w = 81, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 532, .adv_w = 89, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 541, .adv_w = 73, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 548, .adv_w = 89, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 557, .adv_w = 80, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 564, .adv_w = 47, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 571, .adv_w = 81, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 581, .adv_w = 87, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 589, .adv_w = 40, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 592, .adv_w = 40, .box_w = 3, .box_h = 9, .ofs_x = -1, .ofs_y = -2},
    {.bitmap_index = 598, .adv_w = 79, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 606, .adv_w = 41, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 610, .adv_w = 133, .box_w = 8, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 620, .adv_w = 88, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 626, .adv_w = 87, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 633, .adv_w = 89, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 643, .adv_w = 89, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 652, .adv_w = 56, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 657, .adv_w = 67, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 663, .adv_w = 54, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 670, .adv_w = 87, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 676, .adv_w = 75, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 683, .adv_w = 115, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 693, .adv_w = 72, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 700, .adv_w = 75, .box_w = 5, .box_h = 7, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 710, .adv_w = 68, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 717, .adv_w = 49, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 723, .adv_w = 39, .box_w = 2, .box_h = 10, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 727, .adv_w = 49, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 733, .adv_w = 80, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = 2}
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
    .bitmap_format = 1,
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

