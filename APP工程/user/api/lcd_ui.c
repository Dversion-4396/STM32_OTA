/*
	lcd_ui.c —— 手机式界面（240x320 ST7789V，65K 色）
	==================================================================
	布局见 lcd_ui.h 顶部注释。

	★ 三个刷新节拍（都用主循环轮次计数，因为本工程 delay 是纯空转没有 ms 计时）:
	    动图区    每 8 轮  ≈ 80ms   （主循环每轮 delay_ms(10)）
	    运行信息   每 50 轮 ≈ 500ms
	    日志区    脏了立刻刷，不等节拍
	  这样最坏情况一帧只重绘 8 根柱子里高度变了的几个像素段。

	★ 为什么不用 SysTick:
	  全工程没有 SysTick_Config，stm32f10x_it.c 还没参与编译
	  （uvprojx 里 IncludeInBuild=0），所以没有可靠毫秒基准。
	  用轮次估算的误差在 10% 以内，对动画和状态刷新完全够用。

	★ 为什么 240x320 反而更快了:
	  虽然全屏填充从 150ms 涨到 560ms，但那是【只在开机做一次】的。
	  平时每轮只重绘：
	    动图 8 根柱子中高度变了的几段，最多 8 x 28 x 17 像素 ≈ 3800 像素 ≈ 28ms
	    信息区 4 行 x 30 字符，且内容不变就不发
	  所以稳态开销和 128x160 时基本一样。
*/

#include "lcd_ui.h"
#include "lcd.h"
#include "lcd_init.h"
#include "lcd_img.h"

/*=====================================================================
 * 配色（RGB565）
 *====================================================================*/
#define C_STAT_BG      0x0010        /* 状态栏底色：深蓝 */
#define C_STAT_FG      0xFFFF        /* 状态栏文字：白 */
#define C_SEP          0x4208        /* 分隔线：深灰 */
#define C_BG           0x0000        /* 主背景：黑 */
#define C_APPNAME      0x07E0        /* APP 名：绿 */
#define C_LABEL        0x7D7C        /* 标签文字：浅灰 */
#define C_ITEM_FG      0xC618        /* 列表未选中：浅灰 */
#define C_ITEM_BG      0x0000
#define C_SEL_FG       0xFFFF        /* 列表选中：白字 */
#define C_SEL_BG       0x001F        /* 列表选中：蓝底 */
#define C_SIG_OFF      0x2945        /* 信号格熄灭：暗灰 */
#define C_SIG_GPRS     0x07E0        /* 信号格 4G：绿 */
#define C_SIG_MQTT     0x07E0        /* 信号格 MQTT：绿 */
#define C_SIG_OTA      0xF800        /* 信号格 OTA 中：红 */
#define C_OK           0x07E0        /* 状态 OK：绿 */
#define C_FAIL         0xF800        /* 状态失败：红 */
#define C_VAL          0xFFFF        /* 数值：白 */
#define C_LOG          0xC618        /* 日志文字：浅灰 */

/* 每个 APP 名字最长 20 字符（240px 能放 30 个字符，留足余量） */
#define UI_NAME_MAX     20

/*=====================================================================
 * 动图区：显示启动画面图片（240x92，16 色 + 4bpp，见 lcd_img.h）
 *
 * ★ 2026-11-02 换掉了原来的 8 根音柱。
 *   原来的音柱是 16 帧循环 + 只重绘高度变化段，看着像"流体"，用户觉得难看。
 *   现在改成一张静态图片，只在 LCD_UI_Init() 里画一次，主循环里完全不碰。
 *
 * ★ 为什么是 4bpp 调色板而不是 RGB565：
 *   本工程用 MDK-ARM Lite，链接器卡 32KB 镜像上限。
 *   去掉图片后固件已 20820 字节，只剩 11948 字节可用。
 *   240x112 RGB565 要 53760 字节，装不下；
 *   240x92 的 16 色 4bpp 只要 11040 + 32 字节。
 *
 * ★ 图片数据在 lcd_img.h 的 lcd_img_pal[] + lcd_img_data[]。
 *   想换图：改 tools\img2lcd.py 里的 SRC 路径重跑，别手改 lcd_img.h。
 *====================================================================*/

/*=====================================================================
 * 全局状态
 *====================================================================*/
UI_Status g_ui_status;

static char s_app_name[UI_APP_MAX][UI_NAME_MAX + 1];
static u8   s_app_count   = 1;
static u8   s_app_cur     = 0;

/* 缓存：记录上次画上去的值，用来判断要不要重绘 */
static u8   s_c_sig       = 0xFF;          /* 信号格状态 */
static u8   s_c_cur       = 0xFF;          /* 当前 APP 序号 */
static u8   s_c_count     = 0xFF;          /* 列表数量 */
static char s_c_name[UI_NAME_MAX + 1] = { 0 };
static char s_c_list[UI_APP_MAX][UI_NAME_MAX + 1];
static u8   s_c_gprs      = 0xFF;
static u8   s_c_flash     = 0xFF;
static u8   s_c_eeprom    = 0xFF;
static u8   s_c_mqtt      = 0xFF;
static u32  s_c_uptime    = 0xFFFFFFFF;
static u32  s_c_rx        = 0xFFFFFFFF;
static u32  s_c_event     = 0xFFFFFFFF;

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
 * 动图区：画启动画面图片（静态，只画一次）
 *====================================================================*/
static void ui_draw_pic(void)
{
    LCD_ShowImage16(0, UI_Y_ANIM, LCD_IMG_W, LCD_IMG_H, lcd_img_pal, lcd_img_data);
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
 * 当前 APP 名（第二行）
 *====================================================================*/
static void ui_draw_appname(u8 idx)
{
    ui_clear_row(UI_Y_APPNAME, C_BG);
    LCD_ShowString(0, UI_Y_APPNAME, (const u8 *)"APP",
                   C_LABEL, C_BG, UI_FONT_H, 0);
    LCD_ShowString(4 * UI_FONT_W, UI_Y_APPNAME,
                   (const u8 *)s_app_name[idx], C_APPNAME, C_BG, UI_FONT_H, 0);
}

/*=====================================================================
 * 运行信息区（4 行）
 *   y=160  UP:123s
 *   y=176  4G:OK  FLSH:OK  EEP:OK
 *   y=192  MQTT:OK  RX:42
 *   y=208  EV:20000000
 *====================================================================*/

/* 画一个 "标签 + OK/--" 组合，返回下一个可用 x（尾部留 1 字符空隙）
   ★ 不能写死坐标：标签长短不一（"4G:" 3 字符、"FLSH:" 5 字符），
     写死会导致两段粘连或空一大截。 */
static u16 ui_show_flag(u16 x, u16 y, const char *label, u8 ok)
{
    LCD_ShowString(x, y, (const u8 *)label, C_LABEL, C_BG, UI_FONT_H, 0);
    x = (u16)(x + (u16)strlen(label) * UI_FONT_W);

    if (ok)
    {
        LCD_ShowString(x, y, (const u8 *)"OK", C_OK, C_BG, UI_FONT_H, 0);
    }
    else
    {
        LCD_ShowString(x, y, (const u8 *)"--", C_FAIL, C_BG, UI_FONT_H, 0);
    }
    return (u16)(x + 2 * UI_FONT_W + UI_FONT_W);
}

static void ui_draw_uptime(u32 sec)
{
    u16 y = UI_Y_INFO;
    u16 x;

    ui_clear_row(y, C_BG);
    LCD_ShowString(0, y, (const u8 *)"UP:", C_LABEL, C_BG, UI_FONT_H, 0);
    x = (u16)(3 * UI_FONT_W);
    LCD_ShowU32(x, y, sec, C_VAL, C_BG, UI_FONT_H);
    x = (u16)(x + ui_digits(sec) * UI_FONT_W);
    LCD_ShowString(x, y, (const u8 *)"s", C_VAL, C_BG, UI_FONT_H, 0);
}

static void ui_draw_periph(void)
{
    u16 y = UI_Y_INFO + UI_FONT_H;
    u16 x = 0;

    ui_clear_row(y, C_BG);
    x = ui_show_flag(x, y, "4G:",   g_ui_status.gprs_ok);
    x = ui_show_flag(x, y, "FLSH:", g_ui_status.flash_ok);
    x = ui_show_flag(x, y, "EEP:",  g_ui_status.eeprom_ok);
    (void)x;
}

static void ui_draw_cloud(void)
{
    u16 y = UI_Y_INFO + 2 * UI_FONT_H;
    u16 x = 0;

    ui_clear_row(y, C_BG);
    x = ui_show_flag(x, y, "MQTT:", g_ui_status.mqtt_ok);

    LCD_ShowString(x, y, (const u8 *)"RX:", C_LABEL, C_BG, UI_FONT_H, 0);
    x = (u16)(x + 3 * UI_FONT_W);
    LCD_ShowU32(x, y, g_ui_status.rx_pkts, C_VAL, C_BG, UI_FONT_H);
}

static void ui_draw_event(u32 ev)
{
    u16 y = UI_Y_INFO + 3 * UI_FONT_H;

    ui_clear_row(y, C_BG);
    LCD_ShowString(0, y, (const u8 *)"EV:", C_LABEL, C_BG, UI_FONT_H, 0);
    LCD_ShowHex8((u16)(3 * UI_FONT_W), y, ev, C_VAL, C_BG, UI_FONT_H);
}

/*=====================================================================
 * APP 列表（3 行）
 *====================================================================*/
static void ui_draw_list(void)
{
    u8 i;

    for (i = 0; i < UI_APP_MAX; i++)
    {
        u16 y = (u16)(UI_Y_LIST + i * UI_ROW_STEP);
        u16 fg, bg;

        if (i >= s_app_count)
        {
            /* 空槽：清成背景，再画一条暗色占位，避免看起来像"没画完" */
            ui_clear_row(y, C_BG);
            LCD_ShowString(UI_FONT_W, y, (const u8 *)"---",
                           C_SIG_OFF, C_BG, UI_FONT_H, 0);
            s_c_list[i][0] = 0;
            continue;
        }

        if (i == s_app_cur)
        {
            fg = C_SEL_FG;  bg = C_SEL_BG;      /* 选中：蓝底白字 */
        }
        else
        {
            fg = C_ITEM_FG; bg = C_ITEM_BG;     /* 未选中：黑底浅灰 */
        }

        ui_clear_row(y, bg);
        LCD_ShowString(UI_FONT_W, y, (const u8 *)s_app_name[i],
                       fg, bg, UI_FONT_H, 0);

        /* 选中项左侧画一个 ">" 标记 */
        if (i == s_app_cur)
            LCD_ShowString(0, y, (const u8 *)">", fg, bg, UI_FONT_H, 0);
    }
}

/*=====================================================================
 * 日志区（2 行，最新一条在最下面，像控制台）
 *====================================================================*/
static void ui_draw_log(void)
{
    u8 r;

    for (r = 0; r < UI_LOG_LINES; r++)
    {
        u16 y = (u16)(UI_Y_LOG + r * UI_LOG_STEP);
        u8  idx = (u8)((s_log_head + r) % UI_LOG_LINES);

        ui_clear_row(y, C_BG);
        LCD_ShowString(0, y, (const u8 *)">", C_LOG, C_BG, UI_FONT_H, 0);
        LCD_ShowString(UI_FONT_W, y, (const u8 *)s_log[idx],
                       C_LOG, C_BG, UI_FONT_H, 0);
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

    /* 默认 APP 列表 */
    s_app_count = 1;
    s_app_cur   = 0;
    for (i = 0; i < UI_APP_MAX; i++)
    {
        s_app_name[i][0] = 0;
        s_c_list[i][0]   = 0;
    }
    /* 第 0 个默认叫 OTA（本工程就是 OTA 程序） */
    s_app_name[0][0] = 'O'; s_app_name[0][1] = 'T';
    s_app_name[0][2] = 'A'; s_app_name[0][3] = 0;

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
    ui_draw_appname(s_app_cur);
    ui_sep(UI_Y_SEP2, 2);
    ui_sep(UI_Y_SEP3, 4);

    /* 动图区：画启动画面图片（约 200ms） */
    ui_draw_pic();

    ui_draw_uptime(0);
    ui_draw_periph();
    ui_draw_cloud();
    ui_draw_event(0);
    ui_sep(UI_Y_SEP4, 4);
    ui_draw_list();
    ui_sep(UI_Y_SEP5, 4);
    ui_draw_log();

    /* ★ 全画完了才开显示：LCD_Init() 里是 DISPOFF 状态，
       这里开显示，用户第一眼看到的就是完整界面，不会闪花屏 */
    LCD_Display_On();

    /* 缓存置"未知"，逼下一轮把会变的部分重画一遍 */
    s_c_sig    = 0xFF;
    s_c_cur    = 0xFF;
    s_c_count  = 0xFF;
    s_c_name[0] = 0;
    s_c_gprs   = 0xFF;
    s_c_flash  = 0xFF;
    s_c_eeprom = 0xFF;
    s_c_mqtt   = 0xFF;
    s_c_uptime = 0xFFFFFFFF;
    s_c_rx     = 0xFFFFFFFF;
    s_c_event  = 0xFFFFFFFF;

    s_tick_ui      = 0;
    s_uptime_half  = 0;

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
      ★ 只有 2 行缓冲，如果等 500ms 才刷，开机那十几条日志
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

    /* 外设状态（4G / Flash / EEPROM 任一变化才重画整行） */
    if (g_ui_status.gprs_ok   != s_c_gprs  ||
        g_ui_status.flash_ok  != s_c_flash ||
        g_ui_status.eeprom_ok != s_c_eeprom)
    {
        s_c_gprs   = g_ui_status.gprs_ok;
        s_c_flash  = g_ui_status.flash_ok;
        s_c_eeprom = g_ui_status.eeprom_ok;
        ui_draw_periph();
        dirty = 1;
    }

    /* 云连接 + 收包数 */
    if (g_ui_status.mqtt_ok != s_c_mqtt || g_ui_status.rx_pkts != s_c_rx)
    {
        s_c_mqtt = g_ui_status.mqtt_ok;
        s_c_rx   = g_ui_status.rx_pkts;
        ui_draw_cloud();
        dirty = 1;
    }

    /* 事件标志 */
    if (SysEventFlag != s_c_event)
    {
        s_c_event = SysEventFlag;
        ui_draw_event(s_c_event);
        dirty = 1;
    }

    /* 当前 APP 名（换 APP 才重画） */
    if (s_app_cur != s_c_cur ||
        strcmp(s_app_name[s_app_cur], s_c_name) != 0)
    {
        s_c_cur = s_app_cur;
        strcpy(s_c_name, s_app_name[s_app_cur]);
        ui_draw_appname(s_app_cur);
        dirty = 1;
    }

    /* 列表（数量或任一名字变化才重画） */
    {
        u8 need = 0;
        if (s_app_count != s_c_count) need = 1;
        else
        {
            u8 i;
            for (i = 0; i < UI_APP_MAX; i++)
            {
                if (strcmp(s_app_name[i], s_c_list[i]) != 0) { need = 1; break; }
            }
        }
        if (need)
        {
            u8 i;
            s_c_count = s_app_count;
            for (i = 0; i < UI_APP_MAX; i++)
                strcpy(s_c_list[i], s_app_name[i]);
            ui_draw_list();
            dirty = 1;
        }
    }

    return dirty;
}

/*=====================================================================
 * 强制全量刷新
 *====================================================================*/
void LCD_UI_Refresh_All(void)
{
    u8 i;

    s_c_sig    = 0xFF;
    s_c_cur    = 0xFF;
    s_c_count  = 0xFF;
    s_c_name[0] = 0;
    s_c_gprs   = 0xFF;
    s_c_flash  = 0xFF;
    s_c_eeprom = 0xFF;
    s_c_mqtt   = 0xFF;
    s_c_uptime = 0xFFFFFFFF;
    s_c_rx     = 0xFFFFFFFF;
    s_c_event  = 0xFFFFFFFF;

    for (i = 0; i < UI_APP_MAX; i++)
        s_c_list[i][0] = 0;

    s_tick_ui = 49;     /* 下一轮立刻刷新状态 */
}

/*=====================================================================
 * APP 列表接口
 *====================================================================*/
void LCD_UI_SetAppCount(u8 count)
{
    if (count > UI_APP_MAX) count = UI_APP_MAX;
    if (count == 0)         count = 1;
    s_app_count = count;
}

void LCD_UI_SetAppName(u8 idx, const char *name)
{
    u8 i;

    if (idx >= UI_APP_MAX || name == NULL) return;

    /* 拷贝并截断到 UI_NAME_MAX 字符，同时过滤非 ASCII（字模只到 0x7E） */
    for (i = 0; i < UI_NAME_MAX && name[i] != 0; i++)
    {
        char c = name[i];
        if (c < 0x20 || c > 0x7E) c = '.';
        s_app_name[idx][i] = c;
    }
    s_app_name[idx][i] = 0;
}

void LCD_UI_SetCurrentApp(u8 idx)
{
    if (idx >= s_app_count) return;
    s_app_cur = idx;
}

u8 LCD_UI_GetCurrentApp(void)
{
    return s_app_cur;
}

void LCD_UI_NextApp(void)
{
    if (s_app_count <= 1) return;
    s_app_cur++;
    if (s_app_cur >= s_app_count) s_app_cur = 0;
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
          开机初始化阶段连着打十几条日志，2 行缓冲会被迅速覆盖，
          必须每条一进来就上屏，用户才能看到完整的启动过程。
          开销：一行 30 字符 = 480 像素 = 960 次 SPI 写 ≈ 7ms，可以接受。
          s_ui_ready 保护：静态框架还没画完时不刷，否则会画到半成品上。
    */
    if (s_ui_ready)
    {
        (void)LCD_UI_Update();
    }
}
