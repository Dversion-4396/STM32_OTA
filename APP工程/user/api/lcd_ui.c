/*
	lcd_ui.c —— 手机式界面（128x160 ST7735S，65K 色）
	==================================================================
	布局见 lcd_ui.h 顶部注释。

	★ 三个刷新节拍（都用主循环轮次计数，因为本工程 delay 是纯空转没有 ms 计时）:
	    动图区   每 8 轮  ≈ 80ms   （主循环每轮 delay_ms(10)）
	    列表/状态 每 50 轮 ≈ 500ms
	    状态栏    随列表一起
	  这样最坏情况一帧只重绘 8 根柱子里高度变了的几个像素段。

	★ 为什么不用 SysTick:
	  全工程没有 SysTick_Config，stm32f10x_it.c 还没参与编译
	  （uvprojx 里 IncludeInBuild=0），所以没有可靠毫秒基准。
	  用轮次估算的误差在 10% 以内，对动画和状态刷新完全够用。
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
#define C_APPNAME      0x07E0        /* APP 名：绿 */
#define C_LABEL        0x7D7C        /* "APP" 标签：浅灰 */
#define C_ITEM_FG      0xC618        /* 列表未选中：浅灰 */
#define C_ITEM_BG      0x0000
#define C_SEL_FG       0xFFFF        /* 列表选中：白字 */
#define C_SEL_BG       0x001F        /* 列表选中：蓝底 */
#define C_SIG_OFF      0x2945        /* 信号格熄灭：暗灰 */
#define C_SIG_GPRS     0x07E0        /* 信号格 4G：绿 */
#define C_SIG_MQTT     0x07E0        /* 信号格 MQTT：绿 */
#define C_SIG_OTA      0xF800        /* 信号格 OTA 中：红 */

/*=====================================================================
 * 动图：8 根音柱，16 帧循环
 * 每帧 8 个高度值，范围 6..54（柱底 y=UI_VU_BASE=105，向上生长，不超出 y=36..105）
 *====================================================================*/
static const u8 s_vu[16][8] = {
    { 30, 47, 54, 47, 30, 13,  6, 13 },
    { 39, 52, 52, 39, 21,  8,  8, 21 },
    { 47, 54, 47, 30, 13,  6, 13, 30 },
    { 52, 52, 39, 21,  8,  8, 21, 39 },
    { 54, 47, 30, 13,  6, 13, 30, 47 },
    { 52, 39, 21,  8,  8, 21, 39, 52 },
    { 47, 30, 13,  6, 13, 30, 47, 54 },
    { 39, 21,  8,  8, 21, 39, 52, 52 },
    { 30, 13,  6, 13, 30, 47, 54, 47 },
    { 21,  8,  8, 21, 39, 52, 52, 39 },
    { 13,  6, 13, 30, 47, 54, 47, 30 },
    {  8,  8, 21, 39, 52, 52, 39, 21 },
    {  6, 13, 30, 47, 54, 47, 30, 13 },
    {  8, 21, 39, 52, 52, 39, 21,  8 },
    { 13, 30, 47, 54, 47, 30, 13,  6 },
    { 21, 39, 52, 52, 39, 21,  8,  8 },
};

/*=====================================================================
 * 全局状态
 *====================================================================*/
UI_Status g_ui_status;

static char s_app_name[UI_APP_MAX][13];   /* 每个 APP 名字最长 12 字符 + 结尾 0 */
static u8   s_app_count   = 1;
static u8   s_app_cur     = 0;

/* 缓存：记录上次画上去的值，用来判断要不要重绘 */
static u8  s_c_sig       = 0xFF;          /* 信号格状态 */
static u8  s_c_cur       = 0xFF;          /* 当前 APP 序号 */
static u8  s_c_count     = 0xFF;          /* 列表数量 */
static char s_c_name[13] = { 0 };         /* 上次画的 APP 名 */
static char s_c_list[UI_APP_MAX][13];     /* 上次画的列表内容 */

/* 节拍 */
static u16 s_tick_anim = 0;
static u16 s_tick_ui   = 0;
static u8  s_vu_frame  = 0;
static u8  s_vu_prev[8] = { 0 };          /* 上一帧各柱高度，用于只重绘差异 */

/*=====================================================================
 * 内部工具
 *====================================================================*/

/* 清一行（16px 高），用指定背景色 */
static void ui_clear_row(u16 y, u16 bg)
{
    LCD_Fill(0, y, LCD_W - 1, (u16)(y + UI_FONT_H - 1), bg);
}

/* 画一条分隔线 */
static void ui_sep(u16 y, u16 h)
{
    LCD_Fill(0, y, LCD_W - 1, (u16)(y + h - 1), C_SEP);
}

/* 柱高 → 颜色（自下而上：绿 → 青 → 黄 → 橙 → 红） */
static u16 ui_vu_color(u8 h)
{
    if (h < 12)      return 0x07E0;   /* 绿 */
    else if (h < 24) return 0x07FF;   /* 青 */
    else if (h < 36) return 0xFFE0;   /* 黄 */
    else if (h < 48) return 0xFD20;   /* 橙 */
    else             return 0xF800;   /* 红 */
}

/*=====================================================================
 * 状态栏右上角：4 格信号
 * 都没连=全灭 / 只有4G=3格 / MQTT通=4格 / OTA中=闪红
 *====================================================================*/
static void ui_draw_signal(u8 state)
{
    u8 i;
    u16 color;
    u16 x0 = 96;          /* 信号格区域起点 x */
    u16 y_base = 13;      /* 格子底部 y */
    u16 w = 6, hstep = 3, gap = 1;

    for (i = 0; i < 4; i++)
    {
        u16 h = (u16)((i + 1) * hstep);       /* 第 i 格高度 3/6/9/12 */
        u16 x = (u16)(x0 + i * (w + gap));
        u16 y = (u16)(y_base - h + 1);

        if (state == 0)                     color = C_SIG_OFF;   /* 全灭 */
        else if (state == 1)                color = (i < 3) ? C_SIG_GPRS : C_SIG_OFF;  /* 3 格 */
        else if (state == 2)                color = C_SIG_MQTT;  /* 4 格全亮 */
        else                                color = C_SIG_OTA;   /* 闪红 */

        LCD_Fill(x, y, (u16)(x + w - 1), y_base, color);
    }
}

/*=====================================================================
 * 动图区：只重绘高度发生变化的柱段
 *====================================================================*/
static void ui_draw_vu(u8 frame)
{
    u8 k;
    const u8 *cur = s_vu[frame & 0x0F];

    for (k = 0; k < UI_VU_COUNT; k++)
    {
        u8 h_new = cur[k];
        u8 h_old = s_vu_prev[k];
        u16 x = (u16)(UI_VU_X0 + k * (UI_VU_W + UI_VU_GAP));
        u16 y_new_top = (u16)(UI_VU_BASE - h_new + 1);
        u16 y_old_top = (u16)(UI_VU_BASE - h_old + 1);

        if (h_new == h_old)
            continue;                        /* 没变，不动它 */

        if (h_new > h_old)
        {
            /* 变高了：只画新长出来的那一段 */
            LCD_Fill(x, y_new_top, (u16)(x + UI_VU_W - 1),
                     (u16)(y_old_top - 1), ui_vu_color(h_new));
        }
        else
        {
            /* 变矮了：把顶部那一段擦回背景 */
            LCD_Fill(x, y_old_top, (u16)(x + UI_VU_W - 1),
                     (u16)(y_new_top - 1), C_BG);
        }

        s_vu_prev[k] = h_new;
    }
}

/*=====================================================================
 * 状态栏
 *====================================================================*/
static void ui_draw_statusbar(u8 sig)
{
    ui_clear_row(UI_Y_STAT, C_STAT_BG);
    LCD_ShowString(2, UI_Y_STAT, (const u8 *)"STM32 OTA",
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
 * APP 列表（底部 3 行）
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
            /* 空槽：清成背景，再画一条暗色占位线，避免看起来像"没画完" */
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

    /* 全屏清一次（约 150ms，只在开机做） */
    LCD_Fill(0, 0, LCD_W - 1, LCD_H - 1, C_BG);

    /* 静态框架 */
    ui_draw_statusbar(0);
    ui_sep(UI_Y_SEP1, 2);
    ui_draw_appname(s_app_cur);
    ui_sep(UI_Y_SEP2, 2);
    ui_sep(UI_Y_SEP3, 4);
    ui_draw_list();
    ui_sep(UI_Y_SEP4, 2);

    /* 动图区清干净 */
    LCD_Fill(0, UI_Y_ANIM, LCD_W - 1, (u16)(UI_Y_ANIM + UI_ANIM_H - 1), C_BG);
    for (i = 0; i < UI_VU_COUNT; i++)
        s_vu_prev[i] = 0;

    /* 缓存置"未知"，逼下一轮把会变的部分重画一遍 */
    s_c_sig   = 0xFF;
    s_c_cur   = 0xFF;
    s_c_count = 0xFF;
    s_c_name[0] = 0;

    s_tick_anim = 0;
    s_tick_ui   = 0;
    s_vu_frame  = 0;
}

/*=====================================================================
 * 主刷新（主循环每轮调）
 *====================================================================*/
u8 LCD_UI_Update(void)
{
    u8  dirty = 0;
    u8  sig   = ui_sig_state();

    /*---------- 动图：每 8 轮（约 80ms）推进一帧 ----------*/
    s_tick_anim++;
    if (s_tick_anim >= 8)
    {
        s_tick_anim = 0;
        s_vu_frame  = (u8)((s_vu_frame + 1) & 0x0F);
        ui_draw_vu(s_vu_frame);
        dirty = 1;
    }

    /*---------- 状态栏 / APP 名 / 列表：每 50 轮（约 500ms）----------*/
    s_tick_ui++;
    if (s_tick_ui < 50)
        return dirty;
    s_tick_ui = 0;

    /* 状态栏（信号格变化才重画） */
    if (sig != s_c_sig)
    {
        s_c_sig = sig;
        ui_draw_statusbar(sig);
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

    s_c_sig   = 0xFF;
    s_c_cur   = 0xFF;
    s_c_count = 0xFF;
    s_c_name[0] = 0;
    for (i = 0; i < UI_APP_MAX; i++)
        s_c_list[i][0] = 0;
    for (i = 0; i < UI_VU_COUNT; i++)
        s_vu_prev[i] = 0;
    s_tick_anim = 7;      /* 下一轮立刻推进动图 */
    s_tick_ui   = 49;     /* 下一轮立刻刷新状态 */
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

    /* 拷贝并截断到 12 字符，同时过滤非 ASCII（字模只到 0x7E） */
    for (i = 0; i < 12 && name[i] != 0; i++)
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
 * 日志：布局改成手机式后屏幕上没有日志区了，这里什么都不做。
 * 保留空实现是为了 main.c 里那一堆 LCD_Log() 调用不用改。
 *====================================================================*/
void LCD_Log(const char *s)
{
    (void)s;
}
