#ifndef _LCD_UI_H
#define _LCD_UI_H

#include "common.h"

/*
	LCD 界面层 —— 手机式布局
	==================================================================
	屏幕: 128 x 160 竖屏, ST7735S, 65K 色(RGB565), 16 号字(8x16 像素)

	布局（每行 16 字符 = 128 像素）:
	┌────────────────────────┐
	│STM32 OTA          ▁▃▅█│ y=  0  状态栏（深蓝底白字 + 右上信号格）
	├────────────────────────┤ y= 16  分隔线 2px
	│APP  OTA                │ y= 18  当前运行的 APP
	├────────────────────────┤ y= 34  分隔线 2px
	│                        │
	│    ▁ ▃ ▅ █ ▆ ▄ ▂ ▁     │ y= 36  动图区 70px（8 根音柱，高低起伏）
	│                        │
	├────────────────────────┤ y=106  分隔线 4px
	│> OTA                   │ y=110  APP 列表第 1 行（选中=蓝底白字）
	│  APP2                  │ y=126  APP 列表第 2 行
	│  APP3                  │ y=142  APP 列表第 3 行
	├────────────────────────┤ y=158  分隔线 2px
	└────────────────────────┘ 正好铺满 160

	★ 状态栏右上角信号格（4 格，越高越亮）:
	    都没连上        → 4 格全灭（暗灰）
	    4G 上网成功     → 亮 3 格
	    MQTT 连上巴法云 → 亮 4 格（全绿）
	    OTA 下载中      → 4 格闪红
	  这样一眼就能看出"卡在哪一步"：全灭=没网，3格=有网没连云，4格=全通。

	★ 动图区:
	  8 根音柱，高度按正弦表起伏，颜色按高度自下而上绿→青→黄→红。
	  只重绘高度变化的那几个像素，不做整块重画。

	★ 刷新策略（关键, 避免拖慢主循环）:
	  全屏填充 128x160 = 40960 次 SPI 写, 实测约 150ms。
	  所以:
	    1. 开机只做一次全屏清 + 画静态框架
	    2. 动图区每约 80ms 只重绘【高度有变化的柱段】
	    3. 状态栏/APP 名/列表每约 500ms 检查一次, 内容没变就一个字节都不发
*/

#define UI_APP_MAX      3           /* 列表区最多显示几行 */
#define UI_FONT_H       16
#define UI_FONT_W       8

/*---------- 各区域 y 坐标（改这里就能整体调整布局）----------*/
#define UI_Y_STAT       0
#define UI_Y_SEP1       16
#define UI_Y_APPNAME    18
#define UI_Y_SEP2       34
#define UI_Y_ANIM       36
#define UI_ANIM_H       70
#define UI_Y_SEP3       106
#define UI_Y_LIST       110
#define UI_ROW_STEP     16
#define UI_Y_SEP4       158

/*---------- 音柱参数 ---------*/
#define UI_VU_COUNT     8
#define UI_VU_W         14
#define UI_VU_GAP       2
#define UI_VU_X0        1
#define UI_VU_BASE      105         /* 柱底 y（= UI_Y_ANIM + UI_ANIM_H - 1） */

/* ============================================================
   运行状态 —— 由 main.c 在初始化各步骤后设置
   ============================================================ */
typedef struct
{
    u8  gprs_ok;      /* 4G 激活上网成功 = 1 */
    u8  flash_ok;     /* W25Q128 初始化成功 = 1 */
    u8  eeprom_ok;    /* EEPROM 参数有效 = 1 */
    u8  mqtt_ok;      /* MQTT 已连上巴法云 = 1 */
    u8  ota_busy;     /* OTA 下载中 = 1 */
    u32 rx_pkts;      /* 收到的 MQTT 报文数（保留，供调试） */
} UI_Status;

extern UI_Status g_ui_status;

/*
	初始化界面: 清屏 + 画静态框架。在 LCD_Init() 之后调用一次。
	★ 全屏清屏约 150ms, 只在开机做这一次。
*/
void LCD_UI_Init(void);

/*
	主循环里每轮调用一次, 内部自带节流。
	返回: 1 = 本次有更新 / 0 = 没到时间或没变化
*/
u8 LCD_UI_Update(void);

/* 强制全量刷新（忽略缓存, 调试用） */
void LCD_UI_Refresh_All(void);

/* ============================================================
   APP 列表
   ============================================================ */
void LCD_UI_SetAppCount(u8 count);              /* 列表里有几个 APP（最多 3） */
void LCD_UI_SetAppName(u8 idx, const char *name); /* 设置第 idx 个 APP 的名字 */
void LCD_UI_SetCurrentApp(u8 idx);              /* 当前运行第几个 */
u8   LCD_UI_GetCurrentApp(void);
void LCD_UI_NextApp(void);                      /* 切到下一个（以后接按键用） */

/*
	往屏幕追加日志。
	★ 布局改成手机式后【屏幕上没有日志区了】，这个函数保留只为
	  main.c 里的调用不用改，实际什么都不做，内容走串口。
*/
void LCD_Log(const char *s);

#endif
