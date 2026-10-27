/* GIF 动画帧数据 */
#ifndef __GIF_FRAMES_H
#define __GIF_FRAMES_H

#include "common.h"
#include <stdint.h>   /* 保证 uint8_t 可用 */

/* 帧数：换动画且帧数变了才改这里 */
#define GIF_FRAME_COUNT     300

/* 每帧字节数：128 x 64 / 8 = 1024，固定不用改 */
#define GIF_FRAME_SIZE      1024

/* 帧指针表：gif_frames[i] 指向第 i 帧的 1024 字节数据 */
extern const uint8_t *const gif_frames[GIF_FRAME_COUNT];

#endif
