/*******************************************************************************
 * Size: 16 px
 * Bpp: 1
 * Opts: --font Silkscreen-Regular.ttf --symbols  ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789%+-:./ --size 16 --format lvgl --bpp 1 --no-compress --lv-include lvgl.h --lv-font-name pixel_silk_16 --output pixel_silk_16.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef PIXEL_SILK_16
#define PIXEL_SILK_16 1
#endif

#if PIXEL_SILK_16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0025 "%" */
    0xf3, 0x3c, 0xcf, 0x33, 0xcc, 0xc, 0x3, 0x3,
    0x3c, 0xcf, 0x33, 0xcc, 0xf0,

    /* U+002B "+" */
    0xc, 0x3, 0x0, 0xc0, 0x30, 0xff, 0xff, 0xf0,
    0xc0, 0x30, 0xc, 0x3, 0x0,

    /* U+002D "-" */
    0xff, 0xf0,

    /* U+002E "." */
    0xf0,

    /* U+002F "/" */
    0xc, 0x30, 0xc3, 0x30, 0xcc, 0x30, 0xc3, 0x0,

    /* U+0030 "0" */
    0x3c, 0x3c, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0031 "1" */
    0xf3, 0xc3, 0xc, 0x30, 0xc3, 0xc, 0xff, 0xf0,

    /* U+0032 "2" */
    0xfc, 0xfc, 0x3, 0x3, 0x3c, 0x3c, 0xc0, 0xc0,
    0xff, 0xff,

    /* U+0033 "3" */
    0xfc, 0xfc, 0x3, 0x3, 0x3c, 0x3c, 0x3, 0x3,
    0xfc, 0xfc,

    /* U+0034 "4" */
    0xcc, 0xcc, 0xcc, 0xcc, 0xff, 0xff, 0xc, 0xc,
    0xc, 0xc,

    /* U+0035 "5" */
    0xff, 0xff, 0xc0, 0xc0, 0xfc, 0xfc, 0x3, 0x3,
    0xfc, 0xfc,

    /* U+0036 "6" */
    0x3c, 0x3c, 0xc0, 0xc0, 0xfc, 0xfc, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0037 "7" */
    0xff, 0xff, 0x3, 0x3, 0xc, 0xc, 0x30, 0x30,
    0x30, 0x30,

    /* U+0038 "8" */
    0x3c, 0x3c, 0xc3, 0xc3, 0x3c, 0x3c, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0039 "9" */
    0x3c, 0x3c, 0xc3, 0xc3, 0x3f, 0x3f, 0x3, 0x3,
    0x3c, 0x3c,

    /* U+003A ":" */
    0xf0, 0xf0,

    /* U+0041 "A" */
    0x3c, 0x3c, 0xc3, 0xc3, 0xff, 0xff, 0xc3, 0xc3,
    0xc3, 0xc3,

    /* U+0042 "B" */
    0xfc, 0xfc, 0xc3, 0xc3, 0xff, 0xff, 0xc3, 0xc3,
    0xfc, 0xfc,

    /* U+0043 "C" */
    0x3c, 0x3c, 0xc3, 0xc3, 0xc0, 0xc0, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0044 "D" */
    0xfc, 0xfc, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xfc, 0xfc,

    /* U+0045 "E" */
    0xff, 0xfc, 0x30, 0xff, 0xfc, 0x30, 0xff, 0xf0,

    /* U+0046 "F" */
    0xff, 0xfc, 0x30, 0xff, 0xfc, 0x30, 0xc3, 0x0,

    /* U+0047 "G" */
    0x3f, 0x3f, 0xc0, 0xc0, 0xcf, 0xcf, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0048 "H" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xff, 0xc3, 0xc3,
    0xc3, 0xc3,

    /* U+0049 "I" */
    0xff, 0xff, 0xf0,

    /* U+004A "J" */
    0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+004B "K" */
    0xc3, 0xc3, 0xcc, 0xcc, 0xf0, 0xf0, 0xcc, 0xcc,
    0xc3, 0xc3,

    /* U+004C "L" */
    0xc3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xff, 0xf0,

    /* U+004D "M" */
    0xc0, 0xf0, 0x3f, 0x3f, 0xcf, 0xcc, 0xf3, 0x3c,
    0xf, 0x3, 0xc0, 0xf0, 0x30,

    /* U+004E "N" */
    0xc0, 0xf0, 0x3f, 0xf, 0xc3, 0xcc, 0xf3, 0x3c,
    0x3f, 0xf, 0xc0, 0xf0, 0x30,

    /* U+004F "O" */
    0x3c, 0x3c, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0050 "P" */
    0xfc, 0xfc, 0xc3, 0xc3, 0xfc, 0xfc, 0xc0, 0xc0,
    0xc0, 0xc0,

    /* U+0051 "Q" */
    0x3c, 0x3c, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0x3c, 0x3c, 0x3, 0x3,

    /* U+0052 "R" */
    0xfc, 0xfc, 0xc3, 0xc3, 0xfc, 0xfc, 0xcc, 0xcc,
    0xc3, 0xc3,

    /* U+0053 "S" */
    0x3f, 0x3f, 0xc0, 0xc0, 0x3c, 0x3c, 0x3, 0x3,
    0xfc, 0xfc,

    /* U+0054 "T" */
    0xff, 0xf3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xc0,

    /* U+0055 "U" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0056 "V" */
    0xc0, 0xf0, 0x3c, 0xf, 0x3, 0x33, 0xc, 0xc3,
    0x30, 0xcc, 0xc, 0x3, 0x0,

    /* U+0057 "W" */
    0xc0, 0xf0, 0x3c, 0xcf, 0x33, 0xcc, 0xf3, 0x3c,
    0xcf, 0x33, 0x33, 0xc, 0xc0,

    /* U+0058 "X" */
    0xc0, 0xf0, 0x33, 0x30, 0xcc, 0xc, 0x3, 0x3,
    0x30, 0xcc, 0xc0, 0xf0, 0x30,

    /* U+0059 "Y" */
    0xc0, 0xf0, 0x33, 0x30, 0xcc, 0xc, 0x3, 0x0,
    0xc0, 0x30, 0xc, 0x3, 0x0,

    /* U+005A "Z" */
    0xff, 0xf0, 0xc3, 0x30, 0xcc, 0x30, 0xff, 0xf0,

    /* U+0061 "a" */
    0x3c, 0x3c, 0xc3, 0xc3, 0xff, 0xff, 0xc3, 0xc3,
    0xc3, 0xc3,

    /* U+0062 "b" */
    0xfc, 0xfc, 0xc3, 0xc3, 0xff, 0xff, 0xc3, 0xc3,
    0xfc, 0xfc,

    /* U+0063 "c" */
    0x3c, 0x3c, 0xc3, 0xc3, 0xc0, 0xc0, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0064 "d" */
    0xfc, 0xfc, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xfc, 0xfc,

    /* U+0065 "e" */
    0xff, 0xfc, 0x30, 0xff, 0xfc, 0x30, 0xff, 0xf0,

    /* U+0066 "f" */
    0xff, 0xfc, 0x30, 0xff, 0xfc, 0x30, 0xc3, 0x0,

    /* U+0067 "g" */
    0x3f, 0x3f, 0xc0, 0xc0, 0xcf, 0xcf, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0068 "h" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xff, 0xc3, 0xc3,
    0xc3, 0xc3,

    /* U+0069 "i" */
    0xff, 0xff, 0xf0,

    /* U+006A "j" */
    0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+006B "k" */
    0xc3, 0xc3, 0xcc, 0xcc, 0xf0, 0xf0, 0xcc, 0xcc,
    0xc3, 0xc3,

    /* U+006C "l" */
    0xc3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xff, 0xf0,

    /* U+006D "m" */
    0xc0, 0xf0, 0x3f, 0x3f, 0xcf, 0xcc, 0xf3, 0x3c,
    0xf, 0x3, 0xc0, 0xf0, 0x30,

    /* U+006E "n" */
    0xc0, 0xf0, 0x3f, 0xf, 0xc3, 0xcc, 0xf3, 0x3c,
    0x3f, 0xf, 0xc0, 0xf0, 0x30,

    /* U+006F "o" */
    0x3c, 0x3c, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0070 "p" */
    0xfc, 0xfc, 0xc3, 0xc3, 0xfc, 0xfc, 0xc0, 0xc0,
    0xc0, 0xc0,

    /* U+0071 "q" */
    0x3c, 0x3c, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0x3c, 0x3c, 0x3, 0x3,

    /* U+0072 "r" */
    0xfc, 0xfc, 0xc3, 0xc3, 0xfc, 0xfc, 0xcc, 0xcc,
    0xc3, 0xc3,

    /* U+0073 "s" */
    0x3f, 0x3f, 0xc0, 0xc0, 0x3c, 0x3c, 0x3, 0x3,
    0xfc, 0xfc,

    /* U+0074 "t" */
    0xff, 0xf3, 0xc, 0x30, 0xc3, 0xc, 0x30, 0xc0,

    /* U+0075 "u" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0x3c, 0x3c,

    /* U+0076 "v" */
    0xc0, 0xf0, 0x3c, 0xf, 0x3, 0x33, 0xc, 0xc3,
    0x30, 0xcc, 0xc, 0x3, 0x0,

    /* U+0077 "w" */
    0xc0, 0xf0, 0x3c, 0xcf, 0x33, 0xcc, 0xf3, 0x3c,
    0xcf, 0x33, 0x33, 0xc, 0xc0,

    /* U+0078 "x" */
    0xc0, 0xf0, 0x33, 0x30, 0xcc, 0xc, 0x3, 0x3,
    0x30, 0xcc, 0xc0, 0xf0, 0x30,

    /* U+0079 "y" */
    0xc0, 0xf0, 0x33, 0x30, 0xcc, 0xc, 0x3, 0x0,
    0xc0, 0x30, 0xc, 0x3, 0x0,

    /* U+007A "z" */
    0xff, 0xf0, 0xc3, 0x30, 0xcc, 0x30, 0xff, 0xf0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 128, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 14, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 27, .adv_w = 160, .box_w = 6, .box_h = 2, .ofs_x = 2, .ofs_y = 4},
    {.bitmap_index = 29, .adv_w = 96, .box_w = 2, .box_h = 2, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 30, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 38, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 48, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 56, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 66, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 76, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 86, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 96, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 106, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 116, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 126, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 136, .adv_w = 96, .box_w = 2, .box_h = 6, .ofs_x = 2, .ofs_y = 2},
    {.bitmap_index = 138, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 148, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 158, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 168, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 178, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 186, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 194, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 204, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 214, .adv_w = 96, .box_w = 2, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 217, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 227, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 237, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 245, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 258, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 271, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 281, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 291, .adv_w = 192, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 303, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 313, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 323, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 331, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 341, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 354, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 367, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 380, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 393, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 401, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 411, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 421, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 431, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 441, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 449, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 457, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 467, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 477, .adv_w = 96, .box_w = 2, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 480, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 490, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 500, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 508, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 521, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 534, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 544, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 554, .adv_w = 192, .box_w = 8, .box_h = 12, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 566, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 576, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 586, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 594, .adv_w = 192, .box_w = 8, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 604, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 617, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 630, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 643, .adv_w = 224, .box_w = 10, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 656, .adv_w = 160, .box_w = 6, .box_h = 10, .ofs_x = 2, .ofs_y = 0}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x5, 0xb
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 12, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 3, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    },
    {
        .range_start = 45, .range_length = 14, .glyph_id_start = 4,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 65, .range_length = 26, .glyph_id_start = 18,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 97, .range_length = 26, .glyph_id_start = 44,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
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
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 4,
    .bpp = 1,
    .kern_classes = 0,
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
const lv_font_t pixel_silk_16 = {
#else
lv_font_t pixel_silk_16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 12,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -2,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if PIXEL_SILK_16*/

