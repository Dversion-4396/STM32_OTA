/*
	lcd_ui.c —— 纯状态面板（240x320 ST7789V，65K 色）
	==================================================================
	布局见 lcd_ui.h 顶部注释。

	★ 2026-11-02 第二次改版（用户要求"把图片砍掉，只显示状态"）:
	  去掉了启动图片区（240x92 / 16 色 4bpp / 11072 字节）和 APP 列表区，
	  整屏改成状态面板：
	    版本号（2 倍大字）/ 上电时间 / 4G / MQTT / EEPROM / Flash
	    / APP 运行状态 / 事件标志 / 收包数 / 6 行日志
	  省下 11072 字节 Flash，MDK-ARM Lite 的 32KB 上限从"只剩 772 B"变成剩约 11.8KB。

	★ 两个刷新节拍（都用主循环轮次计数，因为本工程 delay 是纯空转没有 ms 计时）:
	    运行信息   每 50 轮 ≈ 500ms
	    日志区    脏了立刻刷，不等节拍
	  内容没变就一个字节都不发（全部走缓存判重）。

	★ 为什么不用 SysTick:
	  全工程没有 SysTick_Config，stm32f10x_it.c 还没参与编译
	  （uvprojx 里 IncludeInBuild=0），所以没有可靠毫秒基准。
	  用轮次估算的误差在 10% 以内，对状态刷新完全够用。

	★ 为什么 240x320 还能跑得动:
	  虽然全屏填充从 150ms 涨到 560ms，但那是【只在开机做一次】的。
	  平时每轮最多重绘：
	    运行信息区 4 行 x 30 字符，且内容不变就不发
	    日志 6 行，只在有新日志时刷
	  所以稳态开销很小。
*/

#include "lcd_ui.h"
#include "lcd.h"
#include "lcd_init.h"

/*=====================================================================
 * 配色（RGB565）
 *====================================================================*/
#define C_STAT_BG      0x0010        /* 状态栏底色：深蓝 */
#define C_STAT_FG      0xFFFF        /* 状态栏文字：白 */
#define C_SEP          0x4208        /* 分隔线：深灰 */
#define C_BG           0x0000        /* 主背景：黑 */
#define C_VER_BG       0x001F        /* 版本号框底色：蓝 */
#define C_VER_FG       0xFFFF        /* 版本号文字：白 */
#define C_LABEL        0x7D7C        /* 标签文字：浅灰 */
#define C_SIG_OFF      0x2945        /* 信号格熄灭：暗灰 */
#define C_SIG_GPRS     0x07E0        /* 信号格 4G：绿 */
#define C_SIG_MQTT     0x07E0        /* 信号格 MQTT：绿 */
#define C_SIG_OTA      0xF800        /* 信号格 OTA 中：红 */
#define C_OK           0x07E0        /* 状态 OK：绿 */
#define C_FAIL         0xF800        /* 状态失败：红 */
#define C_VAL          0xFFFF        /* 数值：白 */
#define C_LOG          0xC618        /* 日志文字：浅灰 */

/*=====================================================================
 * 全局状态
 *====================================================================*/
UI_Status g_ui_status;

/* 缓存：记录上次画上去的值，用来判断要不要重绘 */
static u8   s_c_sig       = 0xFF;          /* 信号格状态 */
static u8   s_c_gprs      = 0xFF;
static u8   s_c_flash     = 0xFF;
static u8   s_c_eeprom    = 0xFF;
static u8   s_c_mqtt      = 0xFF;
static u8   s_c_app       = 0xFF;          /* APP 运行状态 */
static u32  s_c_uptime    = 0xFFFFFFFF;
static u32  s_c_rx        = 0xFFFFFFFF;
static u32  s_c_event     = 0xFFFFFFFF;
static char s_c_ver[UI_VER_MAX] = { 0 };

/* 节拍 */
static u16 s_tick_ui      = 0;

/* 运行时长（单位：0.5 秒，因为每 50 轮 ≈ 500ms 加一次） */
static u32 s_uptime_half = 0;

/* 日志环形缓冲：写指针指向下一个要写的槽 */
static char s_log[UI_LOG_LINES][UI_LOG_COLS + 1];
static u8   s_log_head    = 0;
static u8   s_log_dirty   = 0;
static u8   s_ui_ready    = 0;             /* 静态框架画完了才允许 LCD_Log 刷屏 */

/*=====================================================================
 * 内部工具
 *====================================================================*/

/* 清一行（UI_FONT_H 高），用指定背景色 */
static void ui_clear_row(u16 y, u16 bg)
{
    LCD_Fill(0, y, LCD_W - 1, (u16)(y + UI_FONT_H - 1), bg);
}

/* 画一条分隔线 */
static void ui_sep(u16 y, u16 h)
{
    LCD_Fill(0, y, LCD_W - 1, (u16)(y + h - 1), C_SEP);
}

/* 十进制位数（0 算 1 位） */
static u8 ui_digits(u32 n)
{
    u8 d = 1;
    while (n >= 10)
    {
        n /= 10;
        d++;
    }
    return d;
}

/*=====================================================================
 * 状态栏右上角：4 格信号
 * 都没连=全灭 / 只有4G=3格 / MQTT通=4格 / OTA中=闪红
 *
 * 横向核算：w=8, gap=2 → 每格占 10px，4 格 = 38px
 *           x0=200 → 最后一格 230..237，屏宽 240，留 2px 右边距 ✓
 * 纵向核算：y_base=21，格子高度 5/10/15/20 → 最矮从 y=17 起，最高从 y=2 起
 *           状态栏 0..23，全部在内 ✓
 *====================================================================*/
static void ui_draw_signal(u8 state)
{
    u8 i;
    u16 color;
    u16 x0     = 200;
    u16 y_base = 21;
    u16 w = 8, hstep = 5, gap = 2;

    for (i = 0; i < 4; i++)
    {
        u16 h = (u16)((i + 1) * hstep);        /* 第 i 格高度 5/10/15/20 */
        u16 x = (u16)(x0 + i * (w + gap));
        u16 y = (u16)(y_base - h + 1);

        if (state == 0)      color = C_SIG_OFF;   /* 全灭 */
        else if (state == 1) color = (i < 3) ? C_SIG_GPRS : C_SIG_OFF;  /* 亮 3 格 */
        else if (state == 2) color = C_SIG_MQTT;  /* 4 格全亮 */
        else                 color = C_SIG_OTA;   /* 闪红 */

        LCD_Fill(x, y, (u16)(x + w - 1), y_base, color);
    }
}

/*=====================================================================
 * 状态栏
 *====================================================================*/
static void ui_draw_statusbar(u8 sig)
{
    LCD_Fill(0, UI_Y_STAT, LCD_W - 1, (u16)(UI_Y_STAT + UI_STAT_H - 1), C_STAT_BG);
    LCD_ShowString(4, (u16)(UI_Y_STAT + 4), (const u8 *)"STM32 OTA",
                   C_STAT_FG, C_STAT_BG, UI_FONT_H, 0);
    ui_draw_signal(sig);
}

/*=====================================================================
 * 版本号框（2 倍大字）
 *
 * 横向核算：2 倍大字每字符 16px。
 *           "VER " 4 字符 = 64px（x=2..65），剩下给版本号的起点 x=66，
 *           240 - 66 = 174px，174 / 16 = 10.875
 *           ⇒ **最多 10 个字符**（66 + 10*16 = 226 <= 240 ✓，
 *             11 个会到 242 越界，被 LCD_ShowCharBig 的越界保护整字丢弃）
 *           Version_ABuff 最长 23 字符，超出部分截断
 *           （正常版本号如 "1.0.0" 只有 5 个字符）。
 *
 * 纵向核算：框 26..59（34px），字 27..58（32px），上下各留 1px ✓
 *====================================================================*/
static void ui_draw_version(void)
{
    char buf[UI_VER_SHOW_MAX + 1];
    u8   i;

    /* 框底色 + 上下 1px 边线（用状态栏同色，看起来像个"卡片"） */
    LCD_Fill(0, UI_Y_VERBOX, LCD_W - 1, (u16)(UI_Y_VERBOX + UI_VERBOX_H - 1), C_VER_BG);

    LCD_ShowStringBig(2, UI_Y_VER, (const u8 *)"VER ", C_VER_FG, C_VER_BG);

    /* 拷版本号，过滤非 ASCII 并截断到 UI_VER_SHOW_MAX 字符 */
    for (i = 0; i < UI_VER_SHOW_MAX && g_ui_status.version[i] != 0; i++)
    {
        char c = g_ui_status.version[i];
        if (c < 0x20 || c > 0x7E) c = '.';
        buf[i] = c;
    }
    buf[i] = 0;

    if (i == 0)
    {
        LCD_ShowStringBig(66, UI_Y_VER, (const u8 *)"--", C_VER_FG, C_VER_BG);
    }
    else
    {
        LCD_ShowStringBig(66, UI_Y_VER, (const u8 *)buf, C_VER_FG, C_VER_BG);
    }
}

/*=====================================================================
 * 上电时间（秒）
 *====================================================================*/
static void ui_draw_uptime(u32 sec)
{
    u16 y = UI_Y_UPTIME;
    u16 x;

    ui_clear_row(y, C_BG);
    LCD_ShowString(0, y, (const u8 *)"UP:", C_LABEL, C_BG, UI_FONT_H, 0);
    x = (u16)(3 * UI_FONT_W);
    LCD_ShowU32(x, y, sec, C_VAL, C_BG, UI_FONT_H);
    x = (u16)(x + ui_digits(sec) * UI_FONT_W);
    LCD_ShowString(x, y, (const u8 *)"s", C_VAL, C_BG, UI_FONT_H, 0);
}

/*=====================================================================
 * 设备状态（4 行：4G / MQTT / EEP / FLSH）
 *
 * 横向核算：标签统一 5 字符宽（"4G   " / "MQTT " / "EEP  " / "FLSH "），
 *           所以 x=5*8=40 处画 [ OK ] 一定对齐，不会因为标签长短粘连。
 *====================================================================*/
#define UI_DEV_LABEL_X   0
#define UI_DEV_VAL_X     40

static void ui_draw_dev_row(u16 y, const char *label, u8 ok)
{
    ui_clear_row(y, C_BG);

    LCD_ShowString(UI_DEV_LABEL_X, y, (const u8 *)label, C_LABEL, C_BG, UI_FONT_H, 0);
    LCD_ShowString(UI_DEV_VAL_X, y, (const u8 *)"[", C_LABEL, C_BG, UI_FONT_H, 0);

    if (ok)
        LCD_ShowString((u16)(UI_DEV_VAL_X + UI_FONT_W), y,
                       (const u8 *)" OK ", C_OK, C_BG, UI_FONT_H, 0);
    else
        LCD_ShowString((u16)(UI_DEV_VAL_X + UI_FONT_W), y,
                       (const u8 *)" -- ", C_FAIL, C_BG, UI_FONT_H, 0);

    LCD_ShowString((u16)(UI_DEV_VAL_X + 5 * UI_FONT_W), y,
                   (const u8 *)"]", C_LABEL, C_BG, UI_FONT_H, 0);
}

static void ui_draw_dev(void)
{
    u16 y = UI_Y_DEV;

    ui_draw_dev_row(y,                    "4G   ", g_ui_status.gprs_ok);
    ui_draw_dev_row((u16)(y + UI_DEV_STEP),  "MQTT ", g_ui_status.mqtt_ok);
    ui_draw_dev_row((u16)(y + 2 * UI_DEV_STEP), "EEP  ", g_ui_status.eeprom_ok);
    ui_draw_dev_row((u16)(y + 3 * UI_DEV_STEP), "FLSH ", g_ui_status.flash_ok);
}

/*=====================================================================
 * 运行状态（3 行：APP / 事件标志 / 收包数）
 *====================================================================*/
#define UI_RUN_VAL_X     40

static void ui_draw_run_app(void)
{
    u16 y = UI_Y_RUN;

    ui_clear_row(y, C_BG);
    LCD_ShowString(0, y, (const u8 *)"APP  ", C_LABEL, C_BG, UI_FONT_H, 0);

    if (g_ui_status.ota_busy)
        LCD_ShowString(UI_RUN_VAL_X, y, (const u8 *)"OTA UPDATING",
                       C_SIG_OTA, C_BG, UI_FONT_H, 0);
    else if (g_ui_status.app_running)
        LCD_ShowString(UI_RUN_VAL_X, y, (const u8 *)"RUNNING",
                       C_OK, C_BG, UI_FONT_H, 0);
    else
        LCD_ShowString(UI_RUN_VAL_X, y, (const u8 *)"BOOTING",
                       C_FAIL, C_BG, UI_FONT_H, 0);
}

static void ui_draw_run_event(u32 ev)
{
    u16 y = (u16)(UI_Y_RUN + UI_RUN_STEP);

    ui_clear_row(y, C_BG);
    LCD_ShowString(0, y, (const u8 *)"EV   ", C_LABEL, C_BG, UI_FONT_H, 0);
    LCD_ShowHex8(UI_RUN_VAL_X, y, ev, C_VAL, C_BG, UI_FONT_H);
}

static void ui_draw_run_rx(void)
{
    u16 y = (u16)(UI_Y_RUN + 2 * UI_RUN_STEP);

    ui_clear_row(y, C_BG);
    LCD_ShowString(0, y, (const u8 *)"RX   ", C_LABEL, C_BG, UI_FONT_H, 0);
    LCD_ShowU32(UI_RUN_VAL_X, y, g_ui_status.rx_pkts, C_VAL, C_BG, UI_FONT_H);
}

/*=====================================================================
 * 日志区（6 行，最新一条在最下面，像控制台）
 *====================================================================*/
static void ui_draw_log(void)
{
    u8 r;

    for (r = 0; r < UI_LOG_LINES; r++)
    {
        u16 y = (u16)(UI_Y_LOG + r * UI_LOG_STEP);
        u8  idx = (u8)((s_log_head + r) % UI_LOG_LINES);
        const char *txt = s_log[idx];

        ui_clear_row(y, C_BG);

        /*
           ★ 空行不画 ">"：
             刚开机时日志只有一两条，其余 4~5 行是空的，
             如果每行都挂一个 ">"，看起来像"这行有内容但内容丢了"。
             判据用 txt[0] != 0，因为 LCD_Log 填的是完整 29 字符行
             （不足补空格），空槽的 txt[0] 是 0。
        */
        if (txt[0] != 0)
        {
            LCD_ShowString(0, y, (const u8 *)">", C_LOG, C_BG, UI_FONT_H, 0);
            LCD_ShowString(UI_FONT_W, y, (const u8 *)txt,
                           C_LOG, C_BG, UI_FONT_H, 0);
        }
    }
}

/*=====================================================================
 * 判断信号格该显示哪种状态
 *   0 = 全灭（没网没云）
 *   1 = 4G 通（3 格）
 *   2 = MQTT 通（4 格）
 *   3 = OTA 下载中（闪红）
 *====================================================================*/
static u8 ui_sig_state(void)
{
    if (g_ui_status.ota_busy) return 3;
    if (g_ui_status.mqtt_ok)  return 2;
    if (g_ui_status.gprs_ok)  return 1;
    return 0;
}

/*=====================================================================
 * 初始化
 *====================================================================*/
void LCD_UI_Init(void)
{
    u8 i;

    s_ui_ready = 0;

    /* 日志区清空 */
    for (i = 0; i < UI_LOG_LINES; i++)
        s_log[i][0] = 0;
    s_log_head  = 0;
    s_log_dirty = 0;

    /* 全屏清一次（约 560ms，只在开机做） */
    LCD_Fill(0, 0, LCD_W - 1, LCD_H - 1, C_BG);

    /* 静态框架 */
    ui_draw_statusbar(0);
    ui_sep(UI_Y_SEP1, 2);
    ui_draw_version();
    ui_sep(UI_Y_SEP2, 2);
    ui_draw_uptime(0);
    ui_sep(UI_Y_SEP3, 2);
    ui_draw_dev();
    ui_sep(UI_Y_SEP4, 4);
    ui_draw_run_app();
    ui_draw_run_event(0);
    ui_draw_run_rx();
    ui_sep(UI_Y_SEP5, 4);
    ui_draw_log();

    /* ★ 全画完了才开显示：LCD_Init() 里是 DISPOFF 状态，
       这里开显示，用户第一眼看到的就是完整界面，不会闪花屏 */
    LCD_Display_On();

    /* 缓存置"未知"，逼下一轮把会变的部分重画一遍 */
    s_c_sig     = 0xFF;
    s_c_gprs    = 0xFF;
    s_c_flash   = 0xFF;
    s_c_eeprom  = 0xFF;
    s_c_mqtt    = 0xFF;
    s_c_app     = 0xFF;
    s_c_uptime  = 0xFFFFFFFF;
    s_c_rx      = 0xFFFFFFFF;
    s_c_event   = 0xFFFFFFFF;
    s_c_ver[0]  = 0;

    s_tick_ui     = 0;
    s_uptime_half = 0;

    s_ui_ready = 1;
}

/*=====================================================================
 * 主刷新（主循环每轮调）
 *====================================================================*/
u8 LCD_UI_Update(void)
{
    u8 dirty = 0;
    u8 sig   = ui_sig_state();

    /*---------- 日志：脏了立刻刷，不受 500ms 节流限制 ----------
      ★ 只有 6 行缓冲，如果等 500ms 才刷，开机那十几条日志
        前面的会被直接覆盖掉，用户根本看不到。 */
    if (s_log_dirty)
    {
        s_log_dirty = 0;
        ui_draw_log();
        dirty = 1;
    }

    /*---------- 其余信息：每 50 轮（约 500ms）----------*/
    s_tick_ui++;
    if (s_tick_ui < 50)
        return dirty;
    s_tick_ui = 0;

    /* 运行时长（每 50 轮 = 500ms 记半秒） */
    s_uptime_half++;
    if (s_uptime_half / 2 != s_c_uptime)
    {
        s_c_uptime = s_uptime_half / 2;
        ui_draw_uptime(s_c_uptime);
        dirty = 1;
    }

    /* 状态栏（信号格变化才重画） */
    if (sig != s_c_sig)
    {
        s_c_sig = sig;
        ui_draw_statusbar(sig);
        dirty = 1;
    }

    /* 版本号（变了才重画，2 倍大字一屏要 512 次 SPI 写 x 字符数，别乱刷） */
    if (strcmp(g_ui_status.version, s_c_ver) != 0)
    {
        strcpy(s_c_ver, g_ui_status.version);
        ui_draw_version();
        dirty = 1;
    }

    /* 设备状态（4G / MQTT / EEP / FLSH 任一变化才重画整块） */
    if (g_ui_status.gprs_ok   != s_c_gprs  ||
        g_ui_status.mqtt_ok   != s_c_mqtt  ||
        g_ui_status.eeprom_ok != s_c_eeprom ||
        g_ui_status.flash_ok  != s_c_flash)
    {
        s_c_gprs   = g_ui_status.gprs_ok;
        s_c_mqtt   = g_ui_status.mqtt_ok;
        s_c_eeprom = g_ui_status.eeprom_ok;
        s_c_flash  = g_ui_status.flash_ok;
        ui_draw_dev();
        dirty = 1;
    }

    /* APP 运行状态（运行中 / OTA 中 / 还在启动） */
    {
        u8 app_state = g_ui_status.ota_busy ? 2 : (g_ui_status.app_running ? 1 : 0);
        if (app_state != s_c_app)
        {
            s_c_app = app_state;
            ui_draw_run_app();
            dirty = 1;
        }
    }

    /* 事件标志 */
    if (SysEventFlag != s_c_event)
    {
        s_c_event = SysEventFlag;
        ui_draw_run_event(s_c_event);
        dirty = 1;
    }

    /* 收包数 */
    if (g_ui_status.rx_pkts != s_c_rx)
    {
        s_c_rx = g_ui_status.rx_pkts;
        ui_draw_run_rx();
        dirty = 1;
    }

    return dirty;
}

/*=====================================================================
 * 强制全量刷新
 *====================================================================*/
void LCD_UI_Refresh_All(void)
{
    s_c_sig     = 0xFF;
    s_c_gprs    = 0xFF;
    s_c_flash   = 0xFF;
    s_c_eeprom  = 0xFF;
    s_c_mqtt    = 0xFF;
    s_c_app     = 0xFF;
    s_c_uptime  = 0xFFFFFFFF;
    s_c_rx      = 0xFFFFFFFF;
    s_c_event   = 0xFFFFFFFF;
    s_c_ver[0]  = 0;

    s_tick_ui = 49;     /* 下一轮立刻刷新状态 */
}

/*=====================================================================
 * 日志
 *====================================================================*/
void LCD_Log(const char *s)
{
    u8   i;
    char *dst;

    if (s == NULL) return;

    /* 跳过前导空格和 '>'（调用处习惯写成 "> xxx" 这种） */
    while (*s == ' ' || *s == '>') s++;

    dst = s_log[s_log_head];

    for (i = 0; i < UI_LOG_COLS && s[i] != 0 && s[i] != '\r' && s[i] != '\n'; i++)
    {
        char c = s[i];
        if (c < 0x20 || c > 0x7E) c = '.';   /* 中文字模没有，替换成点 */
        dst[i] = c;
    }
    /* 补空格到整行，避免残留上一条的尾巴 */
    for (; i < UI_LOG_COLS; i++)
        dst[i] = ' ';
    dst[UI_LOG_COLS] = 0;

    s_log_head = (u8)((s_log_head + 1) % UI_LOG_LINES);
    s_log_dirty = 1;

    /*
        ★ 立刻刷一次，不等主循环的 500ms 节拍。
          开机初始化阶段连着打十几条日志，6 行缓冲会被迅速覆盖，
          必须每条一进来就上屏，用户才能看到完整的启动过程。
          开销：一行 30 字符 = 480 像素 = 960 次 SPI 写 ≈ 7ms，可以接受。
          s_ui_ready 保护：静态框架还没画完时不刷，否则会画到半成品上。
    */
    if (s_ui_ready)
    {
        (void)LCD_UI_Update();
    }
}
