#include "oled.h"

// 显存缓冲区：8 页 x 128 列 = 1024 字节
// 每字节管竖直 8 个像素，bit0 是最上面一行
u8 OLED_GRAM[8][128];

/**
 * 发送一条命令
 * 帧格式：START -> 地址 -> 0x00 -> 命令 -> STOP
 * 成功返回 0，失败返回 1
 */
u8 OLED_WriteCmd(u8 Cmd)
{
    u8 retry = I2C_RETRY_COUNT;
    u8 success_flag = 0;

    while (retry--)
    {
        I2C_Start();

        if (I2C_SendByte(OLED_ADDR << 1 | WRITE_FLAG))
        {
            I2C_Stop();
            continue;
        }
        if (I2C_SendByte(OLED_CTRL_CMD))
        {
            I2C_Stop();
            continue;
        }
        if (I2C_SendByte(Cmd))
        {
            I2C_Stop();
            continue;
        }

        I2C_Stop();
        success_flag = 1;
        break;
    }

    return success_flag ? 0 : 1;
}

/**
 * 初始化 OLED
 * 按 OLED_INIT_SEQ 顺序发送 26 条命令，顺序不可打乱
 */
void OLED_Init(void)
{
    static const u8 init_seq[] = {OLED_INIT_SEQ};
    u16 i;

    // 等待模块上电稳定
    delay_ms(100);

    for (i = 0; i < sizeof(init_seq); i++)
    {
        OLED_WriteCmd(init_seq[i]);
    }
}

/**
 * 发送一个字节显存数据
 * 帧格式：START -> 地址 -> 0x40 -> 数据 -> STOP
 * 成功返回 0，失败返回 1
 */
u8 OLED_WriteData(u8 data)
{
    u8 retry = I2C_RETRY_COUNT;
    u8 success_flag = 0;

    while (retry--)
    {
        I2C_Start();

        if (I2C_SendByte(OLED_ADDR << 1 | WRITE_FLAG))
        {
            I2C_Stop();
            continue;
        }
        if (I2C_SendByte(OLED_CTRL_DATA))
        {
            I2C_Stop();
            continue;
        }
        if (I2C_SendByte(data))
        {
            I2C_Stop();
            continue;
        }

        I2C_Stop();
        success_flag = 1;
        break;
    }

    return success_flag ? 0 : 1;
}

/**
 * 连续发送 N 字节显存数据
 * 控制字节 0x40 只发一次，之后连续发数据，显存列地址自动递增
 * 成功返回 0，失败返回 1
 */
u8 OLED_WriteDataBuf(const u8 *buf, u16 len)
{
    u8 retry = I2C_RETRY_COUNT;

    while (retry--)
    {
        u8 fail_flag = 0;
        u16 temp_len = len;
        u16 pos = 0;

        I2C_Start();

        if (I2C_SendByte(OLED_ADDR << 1 | WRITE_FLAG))
        {
            I2C_Stop();
            continue;
        }
        if (I2C_SendByte(OLED_CTRL_DATA))
        {
            I2C_Stop();
            continue;
        }

        while (temp_len--)
        {
            if (I2C_SendByte(buf[pos++]))
            {
                fail_flag = 1;
                break;
            }
        }

        I2C_Stop();

        if (fail_flag)
        {
            continue;
        }
        return 0;
    }

    return 1;
}

/**
 * 清屏：只清显存缓冲区，屏幕要调 OLED_Refresh 才会变
 */
void OLED_Clear(void)
{
    memset(OLED_GRAM, 0, OLED_GRAM_SIZE);
}

/**
 * 设置一个像素
 * 坐标换算：页号 = y / 8，列号 = x，位号 = y % 8
 * 必须用 |= 和 &=~，直接赋值会擦掉同列另外 7 个像素
 *
 * @param x     列坐标 0 ~ 127
 * @param y     行坐标 0 ~ 63
 * @param color 1 点亮，0 熄灭
 */
void OLED_SetPixel(u8 x, u8 y, u8 color)
{
    u8 page;
    u8 bit;

    // 越界保护
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT)
    {
        return;
    }

    page = y / 8;
    bit = y % 8;

    if (color)
    {
        OLED_GRAM[page][x] |= (1 << bit);
    }
    else
    {
        OLED_GRAM[page][x] &= ~(1 << bit);
    }
}

/**
 * 刷新：把显存缓冲区一次发到屏幕
 * 水平寻址模式下设一次窗口即可连刷整屏
 */
void OLED_Refresh(void)
{
    // 设置列地址范围 0 ~ 127
    OLED_WriteCmd(OLED_CMD_SET_COL_ADDR);
    OLED_WriteCmd(OLED_COL_START);
    OLED_WriteCmd(OLED_COL_END);

    // 设置页地址范围 0 ~ 7
    OLED_WriteCmd(OLED_CMD_SET_PAGE_ADDR);
    OLED_WriteCmd(OLED_PAGE_START);
    OLED_WriteCmd(OLED_PAGE_END);

    // 一次性发送 1024 字节
    OLED_WriteDataBuf((u8 *)OLED_GRAM, OLED_GRAM_SIZE);
}


/**
 * 从 GBK 字节流解出一个汉字
 * GBK 一级汉字字节直接对应 GB2312 区位索引，不需要查表
 * 返回消耗的字节数，0 表示非法
 *
 * @param p   指向当前字节
 * @param idx 输出字模索引，0xFFFF 表示字库里没有
 */
#if OLED_USE_FONT
static u8 OLED_GbkDecode(const u8 *p, u16 *idx)
{
    u8 hi = p[0];
    u8 lo = p[1];

    // 前导字节 0x81 ~ 0xFE
    if (hi < 0x81 || hi > 0xFE)
    {
        return 0;
    }

    // 尾字节 0x40 ~ 0xFE，排除 0x7F
    if (lo < 0x40 || lo > 0xFE || lo == 0x7F)
    {
        return 0;
    }

    // 一级汉字 0xB0A1 ~ 0xD7F9，直接算索引
    if (hi >= 0xB0 && hi <= 0xD7 && lo >= 0xA1)
    {
        *idx = (u16)(hi - 0xB0) * 94 + (lo - 0xA1);
        return 2;
    }

    // 二级汉字和符号，字库里没有
    *idx = 0xFFFF;
    return 2;
}
#endif /* OLED_USE_FONT */

/**
 * 在指定位置画一个 ASCII 字符（8x16，占 2 页）
 */
#if OLED_USE_FONT
static void OLED_DrawAscii(u8 x, u8 page, char ch)
{
    u8 idx;
    u8 i;

    if (ch < ' ' || ch > '~')
    {
        return;
    }

    idx = ch - ' ';

    for (i = 0; i < 8; i++)
    {
        if (x + i < OLED_WIDTH)
        {
            OLED_GRAM[page][x + i] = OLED_F8x16[idx][i];
            if (page + 1 < OLED_PAGE_COUNT)
            {
                OLED_GRAM[page + 1][x + i] = OLED_F8x16[idx][8 + i];
            }
        }
    }
}
#endif /* OLED_USE_FONT */

/**
 * 在指定位置画一个汉字（16x16，占 2 页）
 *
 * @param x    起始列
 * @param page 起始页
 * @param idx  字模索引（GB2312 区位索引）
 * @return 1 成功，0 字库里没有
 */
#if OLED_USE_FONT
static u8 OLED_DrawCNByIdx(u8 x, u8 page, u16 idx)
{
    u8 i;

    if (idx >= OLED_CN_COUNT)
    {
        return 0;
    }

    for (i = 0; i < 16; i++)
    {
        if (x + i < OLED_WIDTH)
        {
            OLED_GRAM[page][x + i] = OLED_CN16x16[idx][i];
            if (page + 1 < OLED_PAGE_COUNT)
            {
                OLED_GRAM[page + 1][x + i] = OLED_CN16x16[idx][16 + i];
            }
        }
    }

    return 1;
}
#endif /* OLED_USE_FONT */

/**
 * 显示字符串，自动识别 ASCII 和中文
 * 源文件用 GBK 编码（Keil AC5 默认），直接写中文即可
 *
 * 用法：OLED_ShowString(0, 0, "I hava a 傻逼");
 *
 * ASCII 占 8 列宽，汉字占 16 列宽，都占 2 页高
 * 一行放不下自动换到下一行（page 加 2，因为字符占 2 页）
 *
 * @param x    起始列
 * @param page 起始页 0 ~ 6
 * @param str  字符串（GBK）
 */
#if OLED_USE_FONT
void OLED_ShowString(u8 x, u8 page, const char *str)
{
    const u8 *p = (const u8 *)str;
    u8 used;
    u8 width;
    u16 idx;

    while (*p != '\0')
    {
        // 回车：回到行首
        if (*p == '\r')
        {
            x = 0;
            p++;
            continue;
        }

        // 换行：下一行行首（字符占 2 页，所以 page 加 2）
        if (*p == '\n')
        {
            x = 0;
            page += 2;
            p++;
            continue;
        }

        // ---------- 解析当前字符 ----------
        if (*p < 0x80)
        {
            // ASCII，占 8 列
            idx = 0xFFFF;
            used = 1;
            width = 8;
        }
        else
        {
            // GBK 汉字，占 16 列
            used = OLED_GbkDecode(p, &idx);
            if (used == 0)
            {
                p++;
                continue;
            }
            width = 16;
        }

        // ---------- 排版 ----------
        // 当前行放不下，换到下一行
        if (x + width-16 > OLED_WIDTH)
        {
            x = 0;
            // page += 2;
        }

        // 页号越界，屏幕写满
        if (page + 1 >= OLED_PAGE_COUNT)
        {
            return;
        }

        // ---------- 绘制 ----------
        if (width == 8)
        {
            OLED_DrawAscii(x, page, (char)(*p));
        }
        else
        {
            if (!OLED_DrawCNByIdx(x, page, idx))
            {
                // 字库里没有，用 ? 占位
                OLED_DrawAscii(x, page, '?');
            }
        }

        x += width;
        p += used;
    }
}
#endif /* OLED_USE_FONT */

void OLED_DrawBitmap(const u8 *bmp)
{
    u8 page, x, y, xb, bit;
    u8 b;

    if (bmp == 0)
    {
        return;
    }

    for (y = 0; y < OLED_HEIGHT; y++)
    {
        page = y / 8;
        bit = y % 8;

        for (xb = 0; xb < 16; xb++)
        {
            b = bmp[y * 16 + xb];

            for (x = 0; x < 8; x++)
            {
                u8 px = xb * 8 + x;

                if (px >= OLED_WIDTH)
                {
                    break;
                }

                if ((b >> (7 - x)) & 1)
                {
                    OLED_GRAM[page][px] |= (1 << bit);
                }
                else
                {
                    OLED_GRAM[page][px] &= ~(1 << bit);
                }
            }
        }
    }
}

/* 播放 GIF 动画（阻塞式，一直循环） */
void OLED_PlayGif(const u8 *const *frames, u16 frame_cnt, u16 frame_ms)
{
    u16 i;

    if (frames == 0 || frame_cnt == 0)
    {
        return;
    }

    while (1)
    {
        for (i = 0; i < frame_cnt; i++)
        {
            if (frames[i] == 0)
            {
                continue;
            }

            OLED_Clear();
            OLED_DrawBitmap(frames[i]);
            OLED_Refresh();
            delay_ms(frame_ms);
        }
    }
}

/* 播放 GIF 动画（单步，非阻塞，主循环里反复调用） */
void OLED_PlayGifStep(const u8 *const *frames, u16 frame_cnt, u16 *frame)
{
    if (frames == 0 || frame_cnt == 0 || frame == 0)
    {
        return;
    }

    if (*frame >= frame_cnt)
    {
        *frame = 0;
    }

    if (frames[*frame] != 0)
    {
        OLED_Clear();
        OLED_DrawBitmap(frames[*frame]);
        OLED_Refresh();
    }

    (*frame)++;
    if (*frame >= frame_cnt)
    {
        *frame = 0;
    }
}

/* 播放 GIF 动画（只播一遍） */
void OLED_PlayGifOnce(const u8 *const *frames, u16 frame_cnt, u16 frame_ms)
{
    u16 i;

    if (frames == 0 || frame_cnt == 0)
    {
        return;
    }

    for (i = 0; i < frame_cnt; i++)
    {
        if (frames[i] == 0)
        {
            continue;
        }

        OLED_Clear();
        OLED_DrawBitmap(frames[i]);
        OLED_Refresh();
        delay_ms(frame_ms);
    }
}
