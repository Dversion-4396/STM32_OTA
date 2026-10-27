#ifndef __OLED_FONT_H
#define __OLED_FONT_H

#include "common.h"

/* ============================================================================
 *  字库开关
 *  0 = 不编译字库（Flash 小，烧录快，适合调试图形/动画）
 *  1 = 编译字库（约 900KB，支持 OLED_ShowString 显示中文）
 *
 *  改这里切换，不需要删代码
 * ========================================================================== */
#ifndef OLED_USE_FONT
#define OLED_USE_FONT   0
#endif

/* 汉字字库个数（GB2312 一级汉字） */
#define OLED_CN_COUNT 3755

#if OLED_USE_FONT

/* ASCII 8x16 字库，索引 = ch - 0x20 */
extern const u8 OLED_F8x16[96][16];

/* 16x16 汉字字模，GB2312 区位顺序 */
extern const u8 OLED_CN16x16[OLED_CN_COUNT][32];

#endif /* OLED_USE_FONT */

#endif
