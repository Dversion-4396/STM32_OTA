#ifndef _LCD_UI_H
#define _LCD_UI_H

#include "common.h"

/*
	LCD 界面层 —— 手机式布局（2.4 寸 ST7789V，240x320）
	==================================================================
	屏幕: 240 x 320 竖屏, ST7789V, 65K 色(RGB565), 16 号字(8x16 像素)
	      每行可放 30 个字符（240 / 8），共 20 行（320 / 16）。

	布局（纵向铺满 320，末尾留 8px）:
	┌──────────────────────────────┐
	│STM32 OTA              ▁▃▅█  │ y=  0  状态栏 24px（深蓝底白字 + 右上信号格）
	├──────────────────────────────┤ y= 24  分隔线 2px
	│APP  OTA                      │ y= 26  当前运行的 APP
	├──────────────────────────────┤ y= 42  分隔线 2px
	│                              │
	│                              │
	│    ▁  ▃  ▅  █  ▆  ▄  ▂  ▁   │ y= 44  动图区 112px（8 根音柱，高低起伏）
	│                              │
	│                              │
	├──────────────────────────────┤ y=156  分隔线 4px
	│UP:123s                       │ y=160  运行时长
	│4G:OK FLSH:OK EEP:OK          │ y=176  外设状态
	│MQTT:OK RX:42                 │ y=192  云连接 + 收包数
	│EV:20000000                   │ y=208  事件标志（十六进制）
	├──────────────────────────────┤ y=224  分隔线 4px
	│> OTA                         │ y=228  APP 列表第 1 行（选中=蓝底白字）
	│  APP2                        │ y=244  APP 列表第 2 行
	│  APP3                        │ y=260  APP 列表第 3 行
	├──────────────────────────────┤ y=276  分隔线 4px
	│>最新一条日志                 │ y=280  日志区第 1 行
	│>上一条日志                   │ y=296  日志区第 2 行
	└──────────────────────────────┘ y=312  余 8px 留白

	★ 状态栏右上角信号格（4 格，越高越亮）:
	    都没连上        → 4 格全灭（暗灰）
	    4G 上网成功     → 亮 3 格
	    MQTT 连上巴法云 → 亮 4 格（全绿）
	    OTA 下载中      → 4 格闪红
	  这样一眼就能看出"卡在哪一步"：全灭=没网，3格=有网没连云，4格=全通。

	★ 动图区:
	  8 根音柱，高度按正弦表起伏，颜色按高度自下而上绿→青→黄→橙→红。
	  只重绘高度变化的那几个像素段，不做整块重画。

	★ 刷新策略（关键, 避免拖慢主循环）:
	  全屏填充 240x320 = 76800 像素 = 153600 次 SPI 写, 实测约 560ms。
	  所以:
	    1. 开机只做一次全屏清 + 画静态框架（这一次慢无所谓）
	    2. 动图区每约 80ms 只重绘【高度有变化的柱段】
	    3. 运行信息/APP 名/列表每约 500ms 检查一次, 内容没变就一个字节都不发
	    4. 日志一来就立刻刷（不等 500ms），否则缓冲区会覆盖掉用户还没看到的内容
*/

#define UI_APP_MAX      3           /* 列表区最多显示几行 */
#define UI_FONT_H       16
#define UI_FONT_W       8

#define UI_COLS         30          /* 每行字符数 = 240 / 8 */
#define UI_LOG_LINES    2           /* 日志区行数 */
#define UI_LOG_COLS     29          /* 日志正文列数（前面留 1 列画 ">"） */

/*---------- 各区域 y 坐标（改这里就能整体调整布局）----------*/
#define UI_Y_STAT       0
#define UI_STAT_H       24          /* 状态栏高度（比字体高，显得像个手机状态栏） */
#define UI_Y_SEP1       24
#define UI_Y_APPNAME    26
#define UI_Y_SEP2       42
#define UI_Y_ANIM       44
#define UI_ANIM_H       112
#define UI_Y_SEP3       156
#define UI_Y_INFO       160         /* 运行信息 4 行 */
#define UI_Y_SEP4       224
#define UI_Y_LIST       228
#define UI_ROW_STEP     16
#define UI_Y_SEP5       276
#define UI_Y_LOG        280         /* 日志区 2 行 */
#define UI_LOG_STEP     16

/*---------- 音柱参数 ---------*/
#define UI_VU_COUNT     8
#define UI_VU_W         28
#define UI_VU_GAP       2
#define UI_VU_X0        1
#define UI_VU_BASE      (UI_Y_ANIM + UI_ANIM_H - 1)   /* 柱底 y = 155 */

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
    u32 rx_pkts;      /* 收到的 MQTT 报文数 */
} UI_Status;

extern UI_Status g_ui_status;

/*
	初始化界面: 清屏 + 画静态框架。在 LCD_Init() 之后调用一次。
	★ 全屏清屏约 560ms, 只在开机做这一次。
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
void LCD_UI_SetAppCount(u8 count);                /* 列表里有几个 APP（最多 3） */
void LCD_UI_SetAppName(u8 idx, const char *name); /* 设置第 idx 个 APP 的名字 */
void LCD_UI_SetCurrentApp(u8 idx);                /* 当前运行第几个 */
u8   LCD_UI_GetCurrentApp(void);
void LCD_UI_NextApp(void);                        /* 切到下一个（以后接按键用） */

/*
	往屏幕日志区追加一条日志（屏幕底部 2 行，最新在下）。
	★ 只接受 ASCII 0x20~0x7E，中文会被替换成 '.'（字模表只有 ASCII）。
	★ 每条自动截断到 UI_LOG_COLS 个字符。
	★ 调用后立刻刷屏，不等 500ms 节拍。
*/
void LCD_Log(const char *s);

#endif
