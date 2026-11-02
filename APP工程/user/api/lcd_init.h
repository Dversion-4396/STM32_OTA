#ifndef _LCD_INIT_H
#define _LCD_INIT_H

#include "common.h"

/*
	2.4 寸 TFT (ST7789V) 驱动 —— 引脚定义与底层时序
	==================================================================
	屏幕型号: 10P-2D4TFT_V2.1
	  驱动 IC  : ST7789V
	  分辨率   : 240 x 320
	  接口     : 4 线 SPI (SCL/SDA/RES/DC/CS/BLK)
	  模块引脚 : 10 针

	★ 从 ST7735S 换成 ST7789V 改了什么（不只是改尺寸）:
	  1. 分辨率 128x160 -> 240x320，帧缓冲概念上大了 3.75 倍，
	     全屏填充从 40960 次 SPI 写涨到 153600 次，约 560ms。
	     所以 UI 层绝不能每帧全屏重画，必须只重绘变化区域。
	  2. 初始化序列完全不同。ST7789V 没有 ST7735S 的 B1/B2/B3/B4 帧率寄存器，
	     改成了 PORCTRL(B2)/FRCTRL2(C6)/PWCTRL1(D0)/GCTRL(E0)/GATECTRL(E4) 等。
	  3. 伽马校正寄存器从 16 个(E0/E1) 变成 14 个(E0/E1 各 7 参数)。
	  4. MADCTL(0x36) 的位含义一致，但取值要按新分辨率重算。
	  5. 像素格式 COLMOD(0x3A) = 0x55（16bit RGB565），
	     而 ST7735S 那份写的是 0x05 —— 两个芯片对 0x05 的解释不一样，
	     0x05 在 ST7789V 上是 16bit，但官方推荐 0x55，这里用 0x55。
	  6. ST7789V 有 GRAM 偏移（240x320 的显存是 240x320，但有些模组
	     可视区小于显存），这里按整屏无偏移处理，偏移量宏留空方便调整。

	★ 引脚分配（与 ST7735S 那份完全一致，不用改接线）:
	    SCL  -> PA5   (SPI1_SCK,  唯一选择)
	    SDA  -> PA7   (SPI1_MOSI, 唯一选择)
	    RES  -> PA1   (空闲, 拔 P10 跳线帽后完全独立)
	    DC   -> PE0   (手册: 未接任何外设, 完全独立)
	    CS   -> PE1   (同上)
	    BLK  -> PE6   (同上)

	  VDD -> 3.3V, GND -> GND

	★ 为什么不能照抄官方例程的 PA5/PA7/PA1/PA2/PA3/PA4:
	    战神V1 主机板上
	      PA2 = USART2_TX -> RS485 DI
	      PA3 = USART2_RX <- RS485 RO
	      PA4 = ESP8266 CH_PD (模块使能, 拉低就关断)
	    照抄会把 485 和 WiFi 一起废掉。

	★ 本工程与 SmartHome 的差异(移植时改的):
	  1. 去掉 log.h 依赖, 直接调 Usart1_SendString
	  2. 去掉 tick.h 依赖(SmartHome 用 Delay_GetMs 做节流, 本工程没有 ms 计数器)
	  3. 引脚宏全部改成寄存器风格, 与 common.h 统一
*/

/* ---- 屏幕尺寸 (竖屏 240x320) ---- */
#define USE_HORIZONTAL  1       /* 0/1 = 竖屏 240x320, 2/3 = 横屏 320x240 */

#if (USE_HORIZONTAL == 0) || (USE_HORIZONTAL == 1)
#define LCD_W 240
#define LCD_H 320
#else
#define LCD_W 320
#define LCD_H 240
#endif

/* ---- GRAM 偏移 ----
 * ST7789V 的显存固定 240x320，多数模组可视区就是整屏，偏移为 0。
 * 如果烧上去发现画面整体偏移若干像素（常见 0/1/2 像素错位或边缘有彩条），
 * 就改这两个值试：常见组合是 (0,0)、(0,80)、(80,0)。
 * 320x240 的模组（可视区 240x320 但显存 320x240 那种）通常需要 (0,0)。 */
#define LCD_X_OFFSET    0
#define LCD_Y_OFFSET    0

/* ---- 控制引脚 (★ 已避开 PA2/PA3/PA4) ---- */

/* RES 复位: PA1 */
#define LCD_RES_PORT    GPIOA
#define LCD_RES_PIN     GPIO_Pin_1
#define LCD_RES_Clr()   GPIO_ResetBits(LCD_RES_PORT, LCD_RES_PIN)
#define LCD_RES_Set()   GPIO_SetBits(LCD_RES_PORT, LCD_RES_PIN)

/* DC 数据/命令选择: PE0 */
#define LCD_DC_PORT     GPIOE
#define LCD_DC_PIN      GPIO_Pin_0
#define LCD_DC_Clr()    GPIO_ResetBits(LCD_DC_PORT, LCD_DC_PIN)
#define LCD_DC_Set()    GPIO_SetBits(LCD_DC_PORT, LCD_DC_PIN)

/* CS 片选: PE1 */
#define LCD_CS_PORT     GPIOE
#define LCD_CS_PIN      GPIO_Pin_1
#define LCD_CS_Clr()    GPIO_ResetBits(LCD_CS_PORT, LCD_CS_PIN)
#define LCD_CS_Set()    GPIO_SetBits(LCD_CS_PORT, LCD_CS_PIN)

/* BLK 背光: PE6 (高 = 亮) */
#define LCD_BLK_PORT    GPIOE
#define LCD_BLK_PIN     GPIO_Pin_6
#define LCD_BLK_Clr()   GPIO_ResetBits(LCD_BLK_PORT, LCD_BLK_PIN)
#define LCD_BLK_Set()   GPIO_SetBits(LCD_BLK_PORT, LCD_BLK_PIN)

/* ---- 底层接口 ---- */
void LCD_GPIO_Init(void);
void LCD_Init(void);
void LCD_Writ_Bus(u8 dat);
void LCD_WR_DATA8(u8 dat);
void LCD_WR_DATA(u16 dat);
void LCD_WR_REG(u8 dat);
void LCD_Address_Set(u16 x1, u16 y1, u16 x2, u16 y2);

#endif
