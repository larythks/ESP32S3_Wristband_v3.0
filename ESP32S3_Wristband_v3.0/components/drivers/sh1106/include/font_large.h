/**
 * @file font_large.h
 * @brief 16x24 大数字字模定义（0-9 + ':'）
 */

#ifndef FONT_LARGE_H
#define FONT_LARGE_H

#include <stdint.h>

#define FONT_LARGE_WIDTH 16
#define FONT_LARGE_HEIGHT 24
#define FONT_LARGE_CHAR_SPACING 1

// 7 段码位定义
#define LARGE_SEG_A (1 << 0)
#define LARGE_SEG_B (1 << 1)
#define LARGE_SEG_C (1 << 2)
#define LARGE_SEG_D (1 << 3)
#define LARGE_SEG_E (1 << 4)
#define LARGE_SEG_F (1 << 5)
#define LARGE_SEG_G (1 << 6)

// 0-9 的 7 段编码
static const uint8_t font_large_digit_segments[10] = {
    LARGE_SEG_A | LARGE_SEG_B | LARGE_SEG_C | LARGE_SEG_D | LARGE_SEG_E | LARGE_SEG_F,             // 0
    LARGE_SEG_B | LARGE_SEG_C,                                                                       // 1
    LARGE_SEG_A | LARGE_SEG_B | LARGE_SEG_D | LARGE_SEG_E | LARGE_SEG_G,                             // 2
    LARGE_SEG_A | LARGE_SEG_B | LARGE_SEG_C | LARGE_SEG_D | LARGE_SEG_G,                             // 3
    LARGE_SEG_B | LARGE_SEG_C | LARGE_SEG_F | LARGE_SEG_G,                                           // 4
    LARGE_SEG_A | LARGE_SEG_C | LARGE_SEG_D | LARGE_SEG_F | LARGE_SEG_G,                             // 5
    LARGE_SEG_A | LARGE_SEG_C | LARGE_SEG_D | LARGE_SEG_E | LARGE_SEG_F | LARGE_SEG_G,               // 6
    LARGE_SEG_A | LARGE_SEG_B | LARGE_SEG_C,                                                         // 7
    LARGE_SEG_A | LARGE_SEG_B | LARGE_SEG_C | LARGE_SEG_D | LARGE_SEG_E | LARGE_SEG_F | LARGE_SEG_G, // 8
    LARGE_SEG_A | LARGE_SEG_B | LARGE_SEG_C | LARGE_SEG_D | LARGE_SEG_F | LARGE_SEG_G                // 9
};

#endif // FONT_LARGE_H
