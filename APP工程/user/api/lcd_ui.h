#ifndef _LCD_UI_H
#define _LCD_UI_H

#include "common.h"

/*
	LCD 界面层 —— 纯状态面板（2.4 寸 ST7789V，240x320）
	==================================================================
	屏幕: 240 x 320 竖屏, ST7789V, 65K 色(RGB565), 16 号字(8x16 像素)
	      每行可放 30 个字符（240 / 8），共 20 行（320 / 16）。

	★ 2026-11-02 第二次改版（用户要求）:
	  砍掉启动图片和 APP 列表，整屏只显示"现在是什么状态"：
	    版本号 / 上电时间 / 4G / MQTT / EEPROM / Flash / APP 运行状态 / 事件标志 / 日志
	  好处：Flash 占用从 31996 B 降到约 20900 B（省掉 11072 B 图片），
	        MDK-ARM Lite 的 32KB 上限一下宽松了 11KB。

	布局（纵向铺满 320，末尾留 22px）:
	┌──────────────────────────────┐
	│STM32 OTA              ▁▃▅█  │ y=  0  状态栏 24px（深蓝底白字 + 右上信号格）
	├──────────────────────────────┤ y= 24  分隔线 2px
	│  VER 1.0.0                   │ y= 26  版本号框 34px（2 倍大字 16x32，蓝底白字）
	│                              │ y= 58  版本号框下沿
	├──────────────────────────────┤ y= 60  分隔线 2px
	│UP:123s                       │ y= 62  上电时间（秒）
	├──────────────────────────────┤ y= 80  分隔线 2px
	│4G   [ OK ]                   │ y= 82  4G 激活状态
	│MQTT [ -- ]                   │ y= 98  MQTT 连接状态
	│EEP  [ OK ]                   │ y=114  EEPROM 参数状态
	│FLSH [ OK ]                   │ y=130  W25Q128 状态
	├──────────────────────────────┤ y=146  分隔线 4px
	│APP  RUNNING                  │ y=150  APP 运行状态
	│VER  1.0.0                    │ y=166  正在运行的 APP 版本名
	│OTA  DN 45%                   │ y=182  OTA 事件标志（有 OTA 变绿）+ 下载进度
	│RX   42                       │ y=198  MQTT 收包数
	├──────────────────────────────┤ y=214  分隔线 4px
	│>wait 4G 10s                  │ y=218  日志第 1 行（最新在最后一行）
	│>GPRS FAIL                    │ y=234  日志第 2 行
	│>MQTT FAIL                    │ y=250  日志第 3 行
	│>ENTER APP                    │ y=266  日志第 4 行
	│>...                          │ y=282  日志第 5 行
	│>...                          │ y=298  日志第 6 行
	└──────────────────────────────┘ y=314  余 6px 留白（314..319）

	★ 状态栏右上角信号格（4 格，越高越亮）:
	    都没连上        → 4 格全灭（暗灰）
	    4G 上网成功     → 亮 3 格
	    MQTT 连上巴法云 → 亮 4 格（全绿）
	    OTA 下载中      → 4 格闪红
	  这样一眼就能看出"卡在哪一步"：全灭=没网，3格=有网没连云，4格=全通。

	★ 刷新策略（关键, 避免拖慢主循环）:
	  全屏填充 240x320 = 76800 像素 = 153600 次 SPI 写, 实测约 560ms。
	  所以:
	    1. 开机只做一次全屏清 + 画静态框架（这一次慢无所谓）
	    2. 状态行/运行信息每约 500ms 检查一次, 内容没变就一个字节都不发
	    3. 日志一来就立刻刷（不等 500ms），否则缓冲区会覆盖掉用户还没看到的内容
*/

#define UI_FONT_H       16
#define UI_FONT_W       8

#define UI_COLS         30          /* 每行字符数 = 240 / 8 */
#define UI_LOG_LINES    6           /* 日志区行数（砍掉图片后腾出的空间全给日志） */
#define UI_LOG_COLS     29          /* 日志正文列数（前面留 1 列画 ">"） */

#define UI_VER_MAX      24          /* 版本号最长字符数（EEPROM 里 Version_ABuff 是 24 字节） */

/* 版本号框里最多显示几个 2 倍大字：
   "VER " 占 x=2..65（4 个 16px 字），版本号从 x=66 起，
   240 - 66 = 174px，174 / 16 = 10.875 ⇒ 只能放 10 个。
   11 个会到 x=242 越界，被 LCD_ShowCharBig 的越界保护整字丢掉。 */
#define UI_VER_SHOW_MAX 10

/*---------- 各区域 y 坐标（改这里就能整体调整布局）----------
   纵向核算（屏高 320）：
     状态栏       0 ..  23   (24)
     分隔线1     24 ..  25   (2)
     版本号框    26 ..  59   (34，含 2 倍大字 32px + 上下各 1px)
     分隔线2     60 ..  61   (2)
     上电时间    62 ..  79   (18 = 16 字体 + 2 行距)
     分隔线3     80 ..  81   (2)
     4G 行       82 ..  97   (16)
     MQTT 行     98 .. 113   (16)
     EEP 行     114 .. 129   (16)
     FLSH 行    130 .. 145   (16)
     分隔线4    146 .. 149   (4)
     APP 运行   150 .. 165   (16)
     事件标志   166 .. 181   (16)
     收包数     182 .. 197   (16)
     分隔线5    198 .. 201   (4)
     日志 6 行  202 .. 297   (6 x 16)
     末尾剩余   298 .. 319   (22)
   ----------------------------------------------------------*/
#define UI_Y_STAT       0
#define UI_STAT_H       24          /* 状态栏高度（比字体高，显得像个手机状态栏） */
#define UI_Y_SEP1       24

#define UI_Y_VERBOX     26          /* 版本号框（2 倍大字 16x32） */
#define UI_VERBOX_H     34          /* 32 + 上下各 1px 边 */
#define UI_Y_VER        27          /* 字实际起点（框内留 1px） */

#define UI_Y_SEP2       60
#define UI_Y_UPTIME     62
#define UI_Y_SEP3       80

#define UI_Y_DEV        82          /* 4G 行起点，4 行各 16px */
#define UI_DEV_STEP     16

#define UI_Y_SEP4       146
#define UI_Y_RUN        150         /* APP 运行状态 / APP 版本名 / OTA 标志 / 收包数，4 行各 16px */
#define UI_RUN_STEP     16

#define UI_Y_SEP5       214
#define UI_Y_LOG        218         /* 日志区 6 行 */
#define UI_LOG_STEP     16

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
    u8  app_running;  /* APP 主循环已在跑 = 1（进主循环前置 1） */
    u32 rx_pkts;      /* 收到的 MQTT 报文数 */
    char version[UI_VER_MAX];  /* 当前版本号（从 EEPROM 读，main.c 负责填） */

    /* ---------- 2026-11-03 新增：APP 版本名 / OTA 事件标志 / 下载进度 ---------- */

    /*
       OTA 事件标志（决定屏幕上 OTA 那一行显示什么）
         UI_OTA_IDLE    没有 OTA 事件        → 白色 "OTA  IDLE"
         UI_OTA_EVENT   收到升级通知，待下载  → 绿色 "OTA  EVENT"（用户要求：有 OTA 就变绿）
         UI_OTA_DOWN    正在下载             → 绿色 "OTA  DN 45%"
         UI_OTA_DONE    下载完成，等重启升级  → 绿色 "OTA  DONE"
         UI_OTA_FAIL    下载失败             → 红色 "OTA  FAIL"
       ★ 这个字段和 SysEventFlag 里的 OTA_EVENT 位是两回事：
         SysEventFlag 是"有事要处理"的队列位，处理完就清掉；
         这个字段是"给人看的状态"，会一直留着，直到下一次状态变化。
    */
    u8  ota_state;
    u8  ota_percent;  /* 下载进度 0~100 */

    /*
       正在运行的 APP 版本名（从 EEPROM 的 App 信息区读，main.c 负责填）
       ★ 和上面的 version 有什么区别：
         version  = 巴法云参数里的版本号（BemfaInfoCB.Version_ABuff），上报云端用；
         app_ver  = EEPROM 独立 App 信息区里的版本名（EEPROM_Data.ver），
                    BootLoader 菜单 [3] 显示的也是它，OTA 升级成功后由 BootLoader 改写。
         两者正常情况下相同，但来源不同，所以分开存，出问题时一眼能看出哪个没同步。
    */
    char app_ver[UI_VER_MAX];
} UI_Status;

/* OTA 状态取值（给 g_ui_status.ota_state 用） */
#define UI_OTA_IDLE     0
#define UI_OTA_EVENT    1
#define UI_OTA_DOWN     2
#define UI_OTA_DONE     3
#define UI_OTA_FAIL     4

extern UI_Status g_ui_status;

/*
	初始化界面: 清屏 + 画静态框架。在 LCD_Init() 之后调用一次。
	★ 全屏清屏约 560ms, 只在开机做这一次。
	★ LCD_Init() 里是 DISPOFF，这里全画完才调 LCD_Display_On()。
*/
void LCD_UI_Init(void);

/*
	主循环里每轮调用一次, 内部自带节流。
	返回: 1 = 本次有更新 / 0 = 没到时间或没变化
*/
u8 LCD_UI_Update(void);

/* 强制全量刷新（忽略缓存, 调试用） */
void LCD_UI_Refresh_All(void);

/*
	往屏幕日志区追加一条日志（屏幕底部 6 行，最新在最下面一行）。
	★ 只接受 ASCII 0x20~0x7E，中文会被替换成 '.'（字模表只有 ASCII）。
	★ 每条自动截断到 UI_LOG_COLS 个字符。
	★ 调用后立刻刷屏，不等 500ms 节拍。
*/
void LCD_Log(const char *s);

/*
	OTA 下载进度（★ 给 cat1.c 的阻塞下载循环用）
	------------------------------------------------------------------
	done / total：已收字节数 / 总字节数，内部算百分比。

	★ 为什么必须单独开一个函数：
	  CAT1_OTA() 是阻塞的，下载期间主循环不转，LCD_UI_Update() 不会被调，
	  屏幕上的进度就永远不动。所以只能由下载循环自己回调刷屏。

	★ 内部已经做了"值没变就不刷"的判断，但调用方仍需节流：
	  一次重画 ≈ 45ms，每收 256 字节就刷一次会明显拖慢下载。
	  cat1.c 里按 5% 一档调用。
*/
void LCD_UI_SetOtaProgress(u32 done, u32 total);

#endif
