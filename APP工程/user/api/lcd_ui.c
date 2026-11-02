#include "lcd_ui.h"
#include "lcd.h"
#include "lcd_init.h"

/*
	LCD 界面实现 —— 运行信息 + 日志
	==================================================================
	重写自 SmartHome_v21\MASTER\user\api\lcd_ui.c。
	原版强依赖 modbus.h / autolight.h / sr602.h / tick.h，无法直接移植，
	所以这里只保留"屏幕布局 + 增量刷新"的骨架，内容换成 OTA 工程的运行信息。

	★ 三个移植时必须改掉的地方（原版在本工程里编不过或跑不对）:
	  1. Delay_GetMs() 本工程没有 —— 本工程 delay 是纯空转，没有 ms 计数。
	     改用主循环轮次计数（主循环每轮 delay_ms(10)，50 轮 ≈ 500ms）。
	  2. LOG_Str() 本工程没有 —— 这里自己维护 3 行日志环形缓冲。
	  3. g_mqtt_connack / g_slave[] 等外部变量本工程没有 ——
	     换成 g_ui_status 结构体，由 main.c 填。

	★ 布局算术（改之前先算一遍，我第一版就踩了）:
	  屏幕 128 x 160。16 号字每行占 16 像素。
	    5 行信息   5 x 16 = 80   (y=0..79)
	    分隔线     2             (y=80..81)
	    日志标题   1 x 16 = 16   (y=82..97)
	    日志正文   3 x 16 = 48   (y=98..145)
	    合计      146            <= 160  ✅ 还剩 14 像素余量
	  若硬塞 4 行日志: 80 + 2 + 16 + 64 = 162 > 160 —— 最后一行会被切掉 2 像素。
	  所以日志只放 3 行。这是算出来的，不是随手定的。
*/

/* ============================================================
   常量与配色
   ============================================================ */
#define FONT_SIZE      16
#define CHAR_W         (FONT_SIZE / 2)          /* 8 像素宽 */
#define ROW_STEP       16                       /* 行高 */

#define C_BG           0x0000                   /* 背景: 黑 */
#define C_TITLE_BG     0x001F                   /* 标题栏: 蓝 */
#define C_TITLE_FG     0xFFFF                   /* 标题字: 白 */
#define C_LABEL        0x7D7C                   /* 标签: 浅蓝 */
#define C_OK           0x07E0                   /* 正常: 绿 */
#define C_FAIL         0xF800                   /* 异常: 红 */
#define C_VALUE        0xFFFF                   /* 数值: 白 */
#define C_SEP          0x8430                   /* 分隔线: 灰 */
#define C_LOG          0xC618                   /* 日志字: 浅灰 */

/* 各行 y 坐标（严格按上面的算术，不要随手改） */
#define Y_TITLE        0
#define Y_UPTIME       16
#define Y_PERIPH       32
#define Y_CLOUD        48
#define Y_EVENT        64
#define Y_SEP          80
#define Y_LOGTAG       82
#define Y_LOG1         98
#define Y_LOG2         114
#define Y_LOG3         130

/* ============================================================
   日志环形缓冲
   ============================================================ */
#define LOG_LINES      3
/*
	★ 为什么是 15 而不是 16:
	  日志行画成 ">" + 正文。">" 占 x=0~7, 正文从 x=8 开始。
	  若正文 16 个字符, 最后一个落在 x=128, 而 x+8=136 > LCD_W(128),
	  会被 LCD_ShowChar 的越界保护拦掉 —— 屏幕上永远少最后一个字。
	  改成 15 个字符正好铺满 x=8~127, 一个不多一个不少。
*/
#define LOG_COLS       15

static char s_log[LOG_LINES][LOG_COLS + 1];
static u8   s_log_head = 0;                     /* 下一个要写的位置 */
static u8   s_log_dirty = 0;                    /* 有变化, 需要重绘 */

/* ============================================================
   刷新节流 + 缓存
   ============================================================ */
static u16  s_tick = 0;                         /* 主循环轮次计数 */
static u32  s_uptime_ticks = 0;                 /* 节流后的更新次数, 用来算运行秒数 */
static u8   s_drawn_lines = 0;                  /* 已画过多少行日志 */

/* 上次显示的内容缓存 —— 内容没变就不重绘, 省 SPI 时间 */
static u8   s_c_gprs   = 0xFF;
static u8   s_c_flash  = 0xFF;
static u8   s_c_eeprom = 0xFF;
static u8   s_c_mqtt   = 0xFF;
static u32  s_c_event  = 0xFFFFFFFF;
static u32  s_c_uptime = 0xFFFFFFFF;
static u32  s_c_rx     = 0xFFFFFFFF;

UI_Status g_ui_status;

/* ============================================================
   内部小工具
   ============================================================ */

/* 清一行（16 像素高） */
static void ui_clear_row(u16 y, u16 bg)
{
    LCD_Fill(0, y, LCD_W - 1, (u16)(y + FONT_SIZE - 1), bg);
}

/* 数一个 u32 有多少位十进制（0 算 1 位） */
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

/*
	画 "4G:OK" 这种「标签 + 状态」片段, 返回画完后的 x
	★ 返回 x 而不是写死坐标: 第一版我写死 "7*CHAR_W", 结果 "4G:" 只有 3 个字符,
	  状态又只有 2 个字符, 中间空出一大截, 两个字段之间还会粘在一起。
	  让函数返回下一个可用 x, 排版就不会错。
*/
static u16 ui_show_flag(u16 x, u16 y, const char *label, u8 ok)
{
    LCD_ShowString(x, y, (const u8 *)label, C_LABEL, C_BG, FONT_SIZE, 0);
    x = (u16)(x + strlen(label) * CHAR_W);

    if (ok)
    {
        LCD_ShowString(x, y, (const u8 *)"OK", C_OK, C_BG, FONT_SIZE, 0);
    }
    else
    {
        LCD_ShowString(x, y, (const u8 *)"--", C_FAIL, C_BG, FONT_SIZE, 0);
    }
    return (u16)(x + 2 * CHAR_W);
}

/* 画一行日志（bufidx 是 s_log 的下标） */
static void ui_show_log_line(u8 row, u8 bufidx)
{
    u16 y = (u16)(Y_LOG1 + row * ROW_STEP);

    ui_clear_row(y, C_BG);
    LCD_ShowString(0, y, (const u8 *)">", C_LOG, C_BG, FONT_SIZE, 0);
    LCD_ShowString(CHAR_W, y, (const u8 *)s_log[bufidx], C_LOG, C_BG, FONT_SIZE, 0);
}

/* ============================================================
   对外接口
   ============================================================ */

void LCD_UI_Init(void)
{
    u8 i;

    /* 1. 全屏清一次（约 150ms, 只在开机做） */
    LCD_Fill(0, 0, LCD_W - 1, LCD_H - 1, C_BG);

    /* 2. 标题栏 */
    LCD_Fill(0, Y_TITLE, LCD_W - 1, (u16)(Y_TITLE + FONT_SIZE - 1), C_TITLE_BG);
    /*
        ★ 14 个字符 x 8 像素 = 112 像素, 从 x=8 开始正好居中 (128-112)/2 = 8。
          第一版写成 x=2 + 16 个字符 = 130 > 128, 最后一个 '0' 会被越界保护丢掉,
          屏幕上显示成 "STM32 OTA   v1." —— 少一位版本号。
    */
    LCD_ShowString(8, Y_TITLE, (const u8 *)"STM32 OTA v1.0",
                   C_TITLE_FG, C_TITLE_BG, FONT_SIZE, 0);

    /* 3. 分隔线（用 2 像素高的灰条代替画线函数, 省代码） */
    LCD_Fill(0, Y_SEP, LCD_W - 1, (u16)(Y_SEP + 1), C_SEP);

    /* 4. 日志区标题 */
    LCD_ShowString(0, Y_LOGTAG, (const u8 *)"[LOG]", C_TITLE_FG, C_BG, FONT_SIZE, 0);

    /* 5. 清日志缓冲与缓存 */
    for (i = 0; i < LOG_LINES; i++)
    {
        s_log[i][0] = '\0';
    }
    s_log_head    = 0;
    s_log_dirty   = 0;
    s_drawn_lines = 0;
    s_tick        = 0;
    s_uptime_ticks = 0;

    s_c_gprs   = 0xFF;
    s_c_flash  = 0xFF;
    s_c_eeprom = 0xFF;
    s_c_mqtt   = 0xFF;
    s_c_event  = 0xFFFFFFFF;
    s_c_uptime = 0xFFFFFFFF;
    s_c_rx     = 0xFFFFFFFF;
}

void LCD_Log(const char *s)
{
    u8  i = 0;
    u8  n;

    if (s == NULL)
    {
        return;
    }

    /* 跳过开头的空格和 '>'（调用方常写成 "> xxx"） */
    while (*s == ' ' || *s == '>' || *s == '\t')
    {
        s++;
    }

    /* 拷贝, 遇到 \r \n \0 就停, 最多 LOG_COLS 字符 */
    while (s[i] != '\0' && s[i] != '\r' && s[i] != '\n' && i < LOG_COLS)
    {
        /*
            非 ASCII 一律换成 '.' —— 字模表只有 0x20~0x7E,
            直接传进去会被 LCD_ShowChar 换成空格, 屏幕上什么都看不到,
            换成 '.' 至少能看出"这里有个不能显示的字符"。
        */
        if ((u8)s[i] < 0x20 || (u8)s[i] > 0x7E)
        {
            s_log[s_log_head][i] = '.';
        }
        else
        {
            s_log[s_log_head][i] = s[i];
        }
        i++;
    }

    /* 用空格补齐, 这样重绘时不用再清行（虽然现在还是清了, 留着更保险） */
    n = i;
    while (n < LOG_COLS)
    {
        s_log[s_log_head][n++] = ' ';
    }
    s_log[s_log_head][LOG_COLS] = '\0';

    s_log_head = (u8)((s_log_head + 1) % LOG_LINES);
    if (s_drawn_lines < LOG_LINES)
    {
        s_drawn_lines++;
    }
    s_log_dirty = 1;

    /*
        ★ 立刻画一次。
          原因: 开机初始化阶段连着调好几次 LCD_Log（wait 4G / GPRS OK / EEPROM OK /
          FLASH OK / MQTT connect / MQTT OK / APP running = 7 条），但 LCD_UI_Update
          只在主循环里调。日志环形缓冲只有 3 格，等到主循环第一次 Update 时
          前 4 条已经被覆盖掉了 —— 用户看不到开机过程，只看到最后 3 条。
          在这里同步画一次，每条日志都能在屏幕上留痕。
          开销: 一行 15 字符 = 1920 次 SPI 写 ≈ 7ms, 可以接受。
    */
    (void)LCD_UI_Update();
}

u8 LCD_UI_Update(void)
{
    u8  changed = 0;
    u32 uptime;
    u8  i;

    /* ---- 节流: 主循环每轮 delay_ms(10), 50 轮 ≈ 500ms ---- */
    /*
        ★ 有日志要画的时候不节流 —— 否则开机初始化阶段（LCD_Log 连发好几条，
          但 LCD_UI_Update 总共只被调几次，凑不够 50）日志一条都不会显示，
          要等主循环转 50 圈才出现。用户会觉得"屏是黑的"。
          现在改成: 日志脏了就立刻画，周期性的那几行照样按 50 轮节流。
    */
    s_tick++;
    if (s_tick < 50 && s_log_dirty == 0)
    {
        return 0;
    }
    if (s_tick >= 50)
    {
        s_tick = 0;
        s_uptime_ticks++;
    }

    /*
        运行秒数: 每 500ms 加 1, 所以 2 次 tick = 1 秒。
        这个值是估算 —— 主循环里若有阻塞操作（比如 OTA 下载、AT 指令超时）
        会偏慢, 但作为"程序跑了多久"的粗略指示足够。
    */
    uptime = (u32)(s_uptime_ticks / 2);

    /* ---- 1. 运行时长行 "UP:123s" ---- */
    if (uptime != s_c_uptime)
    {
        u8 d;

        s_c_uptime = uptime;
        ui_clear_row(Y_UPTIME, C_BG);
        LCD_ShowString(0, Y_UPTIME, (const u8 *)"UP:", C_LABEL, C_BG, FONT_SIZE, 0);
        LCD_ShowU32((u16)(3 * CHAR_W), Y_UPTIME, uptime, C_VALUE, C_BG, FONT_SIZE);

        /* 's' 紧跟在数字后面 —— 数字位数会变, 不能写死坐标 */
        d = ui_digits(uptime);
        LCD_ShowString((u16)((3 + d) * CHAR_W), Y_UPTIME,
                       (const u8 *)"s", C_LABEL, C_BG, FONT_SIZE, 0);
        changed = 1;
    }

    /* ---- 2. 外设状态行 "4G:OK FLSH:OK" ---- */
    if (g_ui_status.gprs_ok != s_c_gprs || g_ui_status.flash_ok != s_c_flash)
    {
        u16 x;

        s_c_gprs  = g_ui_status.gprs_ok;
        s_c_flash = g_ui_status.flash_ok;

        ui_clear_row(Y_PERIPH, C_BG);
        x = ui_show_flag(0, Y_PERIPH, "4G:", g_ui_status.gprs_ok);
        /* 中间留 1 个字符空隙, 否则会连成 "4G:OKFLSH:OK" */
        ui_show_flag((u16)(x + CHAR_W), Y_PERIPH, "FLSH:", g_ui_status.flash_ok);
        changed = 1;
    }

    /* ---- 3. 云端状态行 "MQTT:OK EEP:OK" ---- */
    if (g_ui_status.mqtt_ok != s_c_mqtt || g_ui_status.eeprom_ok != s_c_eeprom)
    {
        u16 x;

        s_c_mqtt   = g_ui_status.mqtt_ok;
        s_c_eeprom = g_ui_status.eeprom_ok;

        ui_clear_row(Y_CLOUD, C_BG);
        x = ui_show_flag(0, Y_CLOUD, "MQTT:", g_ui_status.mqtt_ok);
        ui_show_flag((u16)(x + CHAR_W), Y_CLOUD, "EEP:", g_ui_status.eeprom_ok);
        changed = 1;
    }

    /* ---- 4. 事件标志行 "EV:20000000 R123" ---- */
    /*
        ★ 右边那截是收到的 MQTT 报文计数（R + 最多 3 位数字）。
          横向核算:
            "EV:" + 8 位十六进制 = 11 字符 = x=0~87
            空隙 1 字符               x=88~95
            "R"                       x=96~103
            数字最多 3 位              x=104~127
          正好铺满 128 像素。所以计数超过 999 就钳在 999
          （够用来判断"收包链路通不通", 不需要精确值）。
          第一版写成 "R:" 从 x=88 开始, 结果 "EV:04000000R:999" 中间没有空隙,
          和十六进制数粘在一起 —— 所以这里把冒号去掉, 靠位置分隔。
    */
    if (SysEventFlag != s_c_event || g_ui_status.rx_pkts != s_c_rx)
    {
        u32 rx = g_ui_status.rx_pkts;

        s_c_event = SysEventFlag;
        s_c_rx    = g_ui_status.rx_pkts;

        if (rx > 999)
        {
            rx = 999;
        }

        ui_clear_row(Y_EVENT, C_BG);
        LCD_ShowString(0, Y_EVENT, (const u8 *)"EV:", C_LABEL, C_BG, FONT_SIZE, 0);
        LCD_ShowHex8((u16)(3 * CHAR_W), Y_EVENT, SysEventFlag, C_VALUE, C_BG, FONT_SIZE);
        LCD_ShowString((u16)(12 * CHAR_W), Y_EVENT, (const u8 *)"R", C_LABEL, C_BG, FONT_SIZE, 0);
        LCD_ShowU32((u16)(13 * CHAR_W), Y_EVENT, rx, C_VALUE, C_BG, FONT_SIZE);
        changed = 1;
    }

    /* ---- 5. 日志区 ---- */
    if (s_log_dirty)
    {
        s_log_dirty = 0;

        /*
            s_log_head 指向"下一个要写的位置", 写满之后它同时也是最旧的一行。
            没写满时从 0 开始顺序显示。
        */
        if (s_drawn_lines < LOG_LINES)
        {
            for (i = 0; i < s_drawn_lines; i++)
            {
                ui_show_log_line(i, i);
            }
        }
        else
        {
            for (i = 0; i < LOG_LINES; i++)
            {
                ui_show_log_line(i, (u8)((s_log_head + i) % LOG_LINES));
            }
        }
        changed = 1;
    }

    return changed;
}

void LCD_UI_Refresh_All(void)
{
    /* 把缓存全部置成"不可能的值", 逼下一轮 Update 全部重绘 */
    s_c_gprs    = 0xFF;
    s_c_flash   = 0xFF;
    s_c_eeprom  = 0xFF;
    s_c_mqtt    = 0xFF;
    s_c_event   = 0xFFFFFFFF;
    s_c_uptime  = 0xFFFFFFFF;
    s_c_rx      = 0xFFFFFFFF;
    s_log_dirty = 1;
    s_tick      = 49;            /* 下一轮立刻到时间 */
}
