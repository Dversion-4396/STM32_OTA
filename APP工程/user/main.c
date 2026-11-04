/*=====================================================================
 * 文件：main.c
 * 说明：APP 主程序（OTA 客户端）
 * 负责：D（主循环整合）
 *
 *=====================================================================
 * ★ 当前阶段（2026-10-27）★
 *
 * 这是【框架版】，只保留初始化骨架和主循环。
 * 各人的业务逻辑写到自己的函数里，由 ActiveEvent 调用。
 *
 * 已完成并实测通过（A）：
 *   CAT1_SendAT       AT 指令收发
 *   CAT1_ActiveGPRS   GPRS 激活，拿到 IP
 *
 * 待实现：
 *   A：CAT1_HttpInit / CAT1_HttpRead / CAT1_OTA
 *   C：MQTT 连接巴法云 / CAT1_ParseOTA
 *   D：SPI Flash 驱动 / 本文件集成
 *   B：BootLoader 搬移（在 BootLoader 工程里）
 *
 *=====================================================================
 * ★ 硬件实测结论（2026-10-27）★
 *
 * 模块型号：LYNQ_L511C_2C
 * 固件版本：L511C_2Cv02.01b07.00（2024/06/07）
 *
 * 支持的 AT 指令（实测通过）：
 *   AT / ATI / AT+CGMR / AT+CPIN? / AT+CSQ / AT+CREG?
 *   AT+CGATT / AT+CGACT / AT+CGDCONT / AT+CGPADDR
 *
 * 【不支持】的 AT 指令：
 *   AT+HTTPINIT      → ERROR
 *   AT+HTTPINIT=?    → ERROR
 *
 * ★ 这意味着标准 HTTP OTA 方案走不通 ★
 *   下一步要验证 TCP（AT+CIPSTART）是否可用：
 *     能用 → 改走 TCP 直连，手工拼 HTTP 报文
 *     不能用 → 只能本地升级（Xmodem）或换模块
 *
 *=====================================================================
 * ★ 初始化顺序（不要随便调）★
 *
 *   1. USART1（调试口，9600）  ← 必须最先，后面才能打印
 *   2. USART3（4G 模块，115200）
 *   3. 等 4G 模块启动（10~15 秒）
 *   4. 激活 GPRS
 *   5. 各模块初始化（EEPROM / SPI Flash / MQTT）
 *   6. 进入主循环
 *====================================================================*/

#include "common.h"
#include "usart.h"
#include "delay.h"
#include "i2c.h"
#include "eeprom.h"
#include "spi.h"
#include "cat1.h"
#include "mqtt.h"
#include "led.h"
#include "lcd_init.h"
#include "lcd.h"
#include "lcd_ui.h"
/*=====================================================================
 * 全局变量定义（★只能在这里定义一次，其他文件用 extern 引用★）
 *
 * 为什么集中在这里定义：
 *   C 语言里全局变量只能定义一次，其他地方只能 extern 声明。
 *   放在 main.c 最直观，谁都能看到有哪些全局状态。
 *===================================================================*/
BemfaInfo_CB BemfaInfoCB;      /* 巴法云参数 + OTA 状态 */
u32 SysEventFlag = 0;          /* 系统事件标志（位掩码） */
u8 URL_ABuff[OTA_URL_LEN + 1]; /* OTA 下载地址缓冲区 */
u32 ConnectTimer = 0;          /* 主循环圈数（已停用，心跳改用毫秒时基） */
u32 PingTimer = 0;             /* ★单位已改为毫秒★ 上次发 PING 的时刻 */

/* ★2026-11-03 加：心跳间隔。改这一个数就行，别再去改主循环里的比较值。
   巴法云建议 30~60 秒；超过 60 秒容易被判超时踢下线。 */
#define PING_INTERVAL_MS   30000u

/* ★2026-11-04 加：本包心跳的发出时刻，用来判断 PINGRESP 有没有超时 */
static u32 s_ping_sent_ms = 0;

// 剥离函数
static u16 U3_SkipTcpPrefix(u8 *data, u16 len)
{
    u16 pos = 0;
    while (pos < len && (data[pos] == '\r' || data[pos] == '\n'))
    {
        pos++;
    }

    while (pos < len && data[pos] == '+')
    {
        u16 p = pos;
        u16 after_nl = 0xFFFF;
        u16 after_colon = 0xFFFF;

        // 找 换行 或 冒号
        while (p < len)
        {
            if (data[p] == '\n' && after_nl == 0xFFFF)
            {
                after_nl = p + 1;
            }
            if (data[p] == ':' && after_colon == 0xFFFF)
            {
                after_colon = p + 1;
            }

            p++;
        }

        if (after_nl != 0xFFFF && after_colon != 0xFFFF)
        {
            pos = (after_nl > after_colon) ? after_nl : after_colon;
        }
        else if (after_nl != 0xFFFF)
        {
            pos = after_nl;
        }
        else if (after_colon != 0xFFFF)
        {
            pos = after_colon;
        }
        else
        {
            // 没有找到需要剥离的内容，返回原始长度
            return len;
        }

        while (pos < len && (data[pos] == '\r' || data[pos] == '\n'))
        {
            pos++;
        }
    }

    return pos;
}

// 在二进制缓冲里找一段 ASCII 关键字（usart3_value.data 没有 '\0' 结尾，不能直接用 strstr）
static u8 U3_Contains(u8 *buf, u16 len, const char *key)
{
    u16 klen = (u16)strlen(key);
    u16 i;

    if (len < klen)
    {
        return 0;
    }
    for (i = 0; i + klen <= len; i++)
    {
        if (memcmp(buf + i, key, klen) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// 收到 4G 模块的报文，判断是什么事件
void U3_EventDispatch(u8 *data, u16 len)
{

    // 承接得出的 TCP 前缀长度
    u16 off;

    // 承接剥离后的数据
    u8 *pkt;

    // 剥离后的数据长度
    u16 pkt_len;

    if (data == NULL || len == 0)
    {
        return;
    }

    /*=================================================================
     * ★2026-11-04 加：先看模块有没有报告"连接已断开"★
     *
     * 为什么必须放在剥离前缀之前：
     *   模块上报的是 "\r\n+SERVER DISCONNECTED:0\r\n"，
     *   U3_SkipTcpPrefix 会把 "+...:" 连同冒号一起剥掉，
     *   剩下的 "0" 首字节不是 MQTT 类型，于是整条消息被丢掉 ——
     *   程序完全不知道已经掉线，还在往死链路发心跳。
     *
     * 这是"连一会儿就掉线、掉线后不恢复"的直接原因。
     *================================================================*/
    if (U3_Contains(data, len, "SERVER DISCONNECTED") ||
        U3_Contains(data, len, "CIPOPEN FAIL"))
    {
        if (SysEventFlag & CONNECT_EVENT)
        {
            Usart1_SendString("[MQTT] ★模块报告连接已断开，准备重连\r\n");
        }
        SysEventFlag &= ~CONNECT_EVENT;
        SysEventFlag &= ~PING_SENT;
        SysEventFlag |= CONNECT_START;
        /* 不 return：这一帧后面可能还带着最后一包 MQTT 数据 */
    }

    /*=================================================================
     * ★2026-11-04 改：优先按 "+CIPRXGET://u2026//r//n</u6570/u636e>" 取载荷★
     *
     * 实测（L511-2，固件 02.01b07）：
     *   模块开着回显，且 TCP 数据不是裸推，而是先来一行
     *     +CIPRXGET:SUCCESS,0,0,4,\r\n
     *   再跟 4 字节真数据。同一帧里还夹着上一条 AT 的回显
     *   （"OK\r\n\r\n+CIPSEND=0,114" 这种），
     *   所以原来“从帧首开始剥前缀”永远落在 'O' 或 ','上，
     *   真正的 CONNACK（0x20 0x02 0x01 0x00）连同整帧被丢。
     *   后果：CONNECT_EVENT 永远不置位 -> 心跳不发 -> 服务器超时踢人
     *        -> 重连 -> 又丢 …… 无限循环。
     *
     * 现在：先找帧里最后一个 "+CIPRXGET"，跳过它那一行的 \n，
     * 剩下的就是真数据；没有这个标记才退回原来的前缀剥离。
     *=================================================================*/
    {
        int last = -1;
        u16 i;

        for (i = 0; (u16)(i + 9) <= len; i++)
        {
            if (memcmp(&data[i], "+CIPRXGET", 9) == 0)
            {
                last = (int)i;
            }
        }

        if (last >= 0)
        {
            u16 k = (u16)last;

            while (k < len && data[k] != '\n')
            {
                k++;
            }
            if (k < len)
            {
                k++;                        /* 跳过 '\n' */
            }
            off = k;
        }
        else
        {
            off = U3_SkipTcpPrefix(data, len);
        }
    }

    if (off >= len)
    {
        Usart1_SendString("U3_EventDispatch: 剥离后没有数据\r\n");
        return;
    }
    
    pkt = data + off;
    pkt_len = len - off;
    /* 判断是不是 MQTT 报文（看第一个字节的高 4 位，低 4 位是标志位） */
    if ((pkt[0] & 0xF0) == 0x20||
        (pkt[0] & 0xF0) == 0x30||
        (pkt[0] & 0xF0) == 0x40||
        (pkt[0] & 0xF0) == 0x90||
        (pkt[0] & 0xF0) == 0xD0)
    {
        /* 屏幕上的 rx 计数：能持续增长说明 MQTT 收包链路是通的，
           一直不动就说明前缀没剥对或者根本没收到 —— 联调时一眼能看出来 */
        g_ui_status.rx_pkts++;
        U2PassiveEvent(pkt, pkt_len);
    }

    /* 其他情况（AT 应答）不处理，CAT1_SendAT 已经处理过了 */
}

// 处理云端下发的 OTA 事件
void ActiveEvent(void)
{
    // 如果收到的指令是 OTA 事件
    if (SysEventFlag & OTA_EVENT)
    {
        SysEventFlag &= ~OTA_EVENT;

        /* OTA 下载期间：红灯常亮 + 屏幕显示 OTA，别让人以为卡死了。
           下载最坏要几分钟，这期间主循环是阻塞的，
           LCD_UI_Update() 不会被调用 —— 所以先手动刷一次，
           把 "OTA busy" 这个状态留在屏幕上。

           ★2026-11-03 加：ota_state 是给屏幕上 "OTA" 那一行看的，
             UI_OTA_EVENT 会显示成绿色的 "EVENT"（收到通知、还没开始下）。
             进度百分比由 CAT1_OTA() 内部每 5% 回调 LCD_UI_SetOtaProgress() 更新，
             因为那段时间主循环不转，只能靠它自己刷。 */
        g_ui_status.ota_busy   = 1;
        g_ui_status.ota_state  = UI_OTA_EVENT;
        g_ui_status.ota_percent = 0;
        Led_OtaBusy(1);
        LCD_Log("OTA downloading");
        LCD_UI_Update();

        if (CAT1_OTA((char *)URL_ABuff, BemfaInfoCB.OTA_firelen) == 0)
        {
            /* ★2026-11-04 改：下载成功后才提交"已有待升级固件"★
                原来这个动作在 CAT1_ParseOTA（收到通知那一刻）就做了，
                可此时一个字节都还没下。下载失败时 BootLoader 仍会
                认为有固件而去搬残包。
                现在两步：
                 ① 收到通知时 new_len 先清 0（cat1.c 里已改）
                 ② 这里下载完、长度严格相等之后，才写回真实长度
               BootLoader 菜单 [4] 判据是 new_len > 0，因此半包永远不会被搬。 */
            u8 save_ok = 1;

            EEPROM_Data.new_len = BemfaInfoCB.OTA_firelen;
            if (EEPROM_SaveAppInfo(&EEPROM_Data) != 0)
            {
                save_ok = 0;
            }

            // 将事件处理标志位清零，后续跳转到 APP 区域
            BemfaInfoCB.OTA_flag = BOOT_STA_O;
            if (EEPROM_SaveInfo() != 0)
            {
                save_ok = 0;
            }

            if (save_ok)
            {
                g_ui_status.ota_state = UI_OTA_DONE;    /* 屏幕显示绿色 DONE */
                LCD_Log("OTA OK, rebooting");
            }
            else
            {
                /* 状态没存进去就别让 BootLoader 拿半截记录去搬：
                   new_len 清 0 再存一次，宁可这次不升级 */
                EEPROM_Data.new_len = 0;
                (void)EEPROM_SaveAppInfo(&EEPROM_Data);
                BemfaInfoCB.OTA_flag = 0;
                (void)EEPROM_SaveInfo();
                g_ui_status.ota_state = UI_OTA_FAIL;
                LCD_Log("OTA save FAIL");
            }
        }
        else
        {
            g_ui_status.ota_state = UI_OTA_FAIL;    /* 屏幕显示红色 FAIL */
            LCD_Log("OTA FAIL");
        }
        g_ui_status.ota_busy = 0;
        Led_OtaBusy(0);
        LCD_UI_Update();

        /* ★ 停 1 秒再重启：不然 DONE / FAIL 这一行刚画上去就被复位冲掉了，
             人根本看不见下载到底是成功还是失败。 */
        delay_ms(1000);

        // 如果失败则复位到 Bootloder 区域 等待下一次 OTA 事件
        NVIC_SystemReset();
    }

    // 如果收到的指令是 MQTT 连接事件
    if (SysEventFlag & CONNECT_START)
    {
        SysEventFlag &= ~CONNECT_START;
        LCD_Log("reconnect MQTT");
        MQTT_ConnectBemfa();
    }
}

// 具体实现功能区,如需添加或修改实现功能直接修改即可
//void My_App(void);

/*=====================================================================
 * 版本号的唯一真源：FW_VERSION_STR
 * 版本号的唯一写入点：AppInfo_CommitVersion()
 *
 * 为什么要这么收拢：
 *   版本号在系统里有 3 个出现位置，但只应该有 1 个“定义”：
 *     ① 镜像里编译进去的字符串        <- 唯一真源
 *     ② BemfaInfoCB.Version_ABuff     <- EEPROM 副本（MQTT 上报读它）
 *     ③ EEPROM_Data.ver / ver_num     <- EEPROM 副本（BootLoader 菜单 [3] 读它）
 *
 *   副本躲不掉：BootLoader 是独立编译的，读不到 APP 镜像里的常量，
 *   只能读 EEPROM。那就把“写”收成一个人：开机调一次，
 *   把两份副本一起刷成①的值。全工程不再有第二处写版本号。
 *
 * 为什么不能只用 EEPROM 一份（不要①）：
 *   那就是改版本必须手改 EEPROM、或靠 FORCE_REWRITE 硬刷，
 *   一旦忘了就出现“跑着新代码、嘴上说旧版本”。
 *
 * 为什么不能只用①（不要副本）：
 *   BootLoader 读不到它，菜单 [3] 和版本比较都没数据了。
 *=====================================================================*/
#define FW_VERSION_STR         "1.0.3"

/* 把镜像里的版本同步进 EEPROM 的两份缓存；不一致才写，一致就不动。 */
static void AppInfo_CommitVersion(void)
{
    u32 vnum;
    u8  changed = 0;

    /* 版本名 -> 版本号数值（1.0.3 -> 10003），供 BootLoader 比大小 */
    vnum = cat1_version_to_num(FW_VERSION_STR);
    if (vnum == 0)
    {
        Usart1_SendString("[VER] 版本名不是 X.Y.Z 格式，版本号按 10000 记\r\n");
        vnum = 10000;
    }

    /* ---- 副本 ②：巴法云参数区 ---- */
    if (strcmp((char *)BemfaInfoCB.Version_ABuff, FW_VERSION_STR) != 0)
    {
        memset(BemfaInfoCB.Version_ABuff, 0, sizeof(BemfaInfoCB.Version_ABuff));
        strcpy((char *)BemfaInfoCB.Version_ABuff, FW_VERSION_STR);

        if (EEPROM_SaveInfo() != 0)
        {
            Usart1_SendString("[VER] 写巴法云参数区失败\r\n");
        }
        else
        {
            changed = 1;
        }
    }

    /* ---- 副本 ③：App 信息区 ---- */
    (void)EEPROM_LoadAppInfo(&EEPROM_Data);
    if (strcmp(EEPROM_Data.ver, FW_VERSION_STR) != 0 || EEPROM_Data.ver_num != vnum)
    {
        memset(EEPROM_Data.ver, 0, sizeof(EEPROM_Data.ver));
        strncpy(EEPROM_Data.ver, FW_VERSION_STR, EEPROM_VER_SIZE - 1);
        EEPROM_Data.ver[EEPROM_VER_SIZE - 1] = '\0';
        EEPROM_Data.ver_num = vnum;

        if (EEPROM_SaveAppInfo(&EEPROM_Data) != 0)
        {
            Usart1_SendString("[VER] 写 App 信息区失败\r\n");
        }
        else
        {
            changed = 1;
        }
    }

    if (changed)
    {
        Usart1_SendString("[VER] EEPROM 版本缓存已同步为 ");
        Usart1_SendString(FW_VERSION_STR);
        Usart1_SendString("\r\n");
    }
}

/*=====================================================================
 * 函数：main
 * 功能：程序入口
 * 参数：无
 * 返回：int（不会返回）
 * 负责：D
 *====================================================================*/
int main(void)
{
    __enable_irq();
    static u8 VersionSent = 0; /* 版本号只上报一次 */

    /*---------- 0. 1ms 系统时基（必须在所有 delay 之前）----------*/
    /* 配置 SysTick 1ms 中断，之后 delay_ms/delay_us 才准。
       心跳计时也用 Get_Tick()，不再靠数主循环圈数。 */
    Delay_Init();

    /*---------- 1. 初始化调试串口（必须最先）----------*/
    Usart1_Init(USART1_BRR);
    Usart1_SendString("\r\n\r\n");
    Usart1_SendString("=== STM32 OTA 启动 ===\r\n");

    /*---------- 2. 初始化 4G 模块串口 ----------*/
    Usart3_Init(USART3_BRR);
    Usart1_SendString("USART3 初始化完成\r\n");

    /*---------- 3. 初始化 LED ----------*/
    /* ★ 放在最前面：后面任何一个初始化卡住，绿灯至少还在闪，
       能一眼看出"程序在跑，只是某一步没过"，而不是死机。
       绿灯 1Hz 慢闪 = 正常运行中 */
    Led_Init();

    /*---------- 4. 初始化 LCD ----------*/
    /*
        ★ 放在 4G 等待之前：LCD 初始化只要 ~440ms，
          而等 4G 模块要 10 秒 —— 这 10 秒正好用来把界面画好，
          用户上电就能看到屏幕亮起来，不用干等。

        ★ 如果没接屏，LCD_Init() 会一直卡在等 SPI 标志吗？
          不会。SPI 是主机模式，没有从机应答也能发出去，
          只是数据没人收。所以不接屏时程序照常往下走，
          最多多花 440ms。
    */
    LCD_Init();
    LCD_UI_Init();

    /*---------- 5. 等 4G 模块启动 ----------*/
    /* 模块上电后要 10~15 秒才能响应 AT，太早发没反应 */
    Usart1_SendString("等待 4G 模块启动...\r\n");
    LCD_Log("wait 4G 15s");
    /* ★2026-11-03 改：原来是 10000。delay 改成真时基后，
       10000 就是实打实 10 秒，而模块实测要 10~15 秒才响应 AT，不够。 */
    delay_ms(15000);

    /*---------- 6. 激活 GPRS ----------*/

    /* 内部已有详细打印，失败会打印步骤号 */
    if (CAT1_ActiveGPRS() != 0)
    {
        Usart1_SendString("GPRS 激活失败，检查 SIM 卡和信号\r\n");
        LCD_Log("GPRS FAIL");
    }
    else
    {
        g_ui_status.gprs_ok = 1;
        LCD_Log("GPRS OK");
    }


    /*---------- 7. 各模块初始化 ----------*/
    /* TODO【D】：EEPROM 和 SPI Flash 初始化 */

    // 初始化 I2C 和 SPI

    I2C_GPIO_Init();
    SPI2_Init();
    if (EEPROM_ReadInfo() != 0)
    {
        Usart1_SendString("EEPROM 读取失败\r\n");
        LCD_Log("EEPROM FAIL");
    }
    else
    {
        g_ui_status.eeprom_ok = 1;
        LCD_Log("EEPROM OK");
    }

    /*---------- 7.1 首次上电写默认参数 ----------*/
    /*
        ★ 为什么要这一段 ★

        EEPROM_ReadInfo() 只是"把 EEPROM 里的 99 字节读进 BemfaInfoCB"，
        它不会写。而原来唯一的写入点在 BootLoader 的 AliCer_Inspect() 里，
        那个函数现在没有任何调用点（菜单 [3] 改成了"查看当前状态"），
        所以新板子/擦过 EEPROM 的板子，读出来永远是：

            valid_flag != 0xDD  →  EEPROM_ReadInfo() 把整个结构体 memset 成 0
                                →  UID / Topic / Version 全是空串
                                →  MQTT_Init() 只能退回 mqtt.h 里的硬编码测试值
                                →  CAT1_PropertyVersion() 发出去的 payload 是 "ver:"

        这里补上：只要参数无效，就把下面这套默认值写进 EEPROM 一次。

        ★ 以后换版本号，就改下面 Version_ABuff 那一行 ★

        注意：改完源码重新烧录，EEPROM 里存的还是旧值（valid_flag 仍是 0xDD），
              这段不会重跑。想让它真的重写一次，两个办法：
                ① 临时把下面的 if 条件改成 if (1)，烧一次，写完再改回来
                ② 换一块没写过的 EEPROM / 手动擦掉 AT24C02 前 99 字节
    */
    /* ★★★ 2026-11-03：改 UID/Topic/版本号后，EEPROM 里存的还是旧值，
     *   valid_flag 仍是 0xDD，下面这段「写默认值」不会重跑。
     *
     *   所以加了这个强制开关：改成 1 编译烧录一次，开机就会无条件重写，
     *   写完立刻改回 0 再烧一次（否则每次开机都重写 EEPROM，白耗寿命）。
     *
     *   AT24C02 擦写寿命 100 万次，一次重写写 99+ 字节 ≈ 99 次，
     *   烧个几百次也到不了上限，但还是别长期开着。
     * ------------------------------------------------------------ */
#define FORCE_REWRITE_EEPROM   0

    if (FORCE_REWRITE_EEPROM || BemfaInfoCB.valid_flag != VALID_BEMFA_DATA)
    {
        Usart1_SendString("[EEPROM] 参数无效，写入默认值\r\n");

        memset(&BemfaInfoCB, 0, sizeof(BemfaInfoCB));
        BemfaInfoCB.valid_flag = VALID_BEMFA_DATA;                              /* 0xDD = 参数有效 */
        strcpy((char *)BemfaInfoCB.UID_Buff,     "536cd706bafe4b10a4ddb1f7570f9893"); /* 巴法云 UID，32 位 */
        strcpy((char *)BemfaInfoCB.Topic_Buff,   "qian1");                      /* 订阅的 Topic 名字 */
        /* ★ 版本号改这里，最长 23 字符
         *   2026-11-04：OTA 已跑通，1.0.2 定为基线。
         *   这个字符串必须和板子上实际运行的固件版本一致，
         *   否则每次开机都会把版本写回这个值，造成云端反复重推。
         *   下次升级就把这里改成 1.0.3，再重新打包。 */
        strcpy((char *)BemfaInfoCB.Version_ABuff, FW_VERSION_STR);
        BemfaInfoCB.OTA_firelen = 0;                                            /* 没有待搬运的固件 */
        BemfaInfoCB.OTA_flag    = 0;                                            /* 0x5AA5C33C 才是"待升级" */

        if (EEPROM_SaveInfo() != 0)
        {
            Usart1_SendString("[EEPROM] 默认值写入失败\r\n");
            LCD_Log("EEPROM wr FAIL");
        }
        else
        {
            Usart1_SendString("[EEPROM] 默认值写入完成\r\n");
            LCD_Log("EEPROM default");
        }
    }
    else
    {
        /* 参数有效，打印一下当前版本号，方便核对是不是想要的版本
         * （真正的同步在下面 AppInfo_CommitVersion() 里做） */
        Usart1_SendString("[EEPROM] 当前版本号: ");
        Usart1_SendBuffer((char *)BemfaInfoCB.Version_ABuff,
                          (u16)strlen((char *)BemfaInfoCB.Version_ABuff));
        Usart1_SendString("\r\n");
    }

    /* ★2026-11-04：版本号唯一写入点★
     *   不管上面走的是“写默认值”还是“参数有效”，
     *   这里都把两份缓存刷成镜像里的版本号，保证全局一致。 */
    AppInfo_CommitVersion();

    /*---------- 7.1.1 回显当前生效的巴法云参数 ----------*/
    /*
        ★ 为什么要打这一段 ★

        以前改完 UID/Topic 烧进去，串口里什么都看不出来 ——
        只能看到 "[EEPROM] 参数有效"，但没法确认里面到底是不是新值。
        而 EEPROM 有个坑：valid_flag 是 0xDD 时那段"写默认值"根本不跑，
        所以改了源码烧进去，实际用的还是旧值，却一点提示都没有。

        这段就是用来一眼核对：
          - 如果 UID 还是旧的 a0f83060...，说明 EEPROM 没被重写，
            要把上面的 FORCE_REWRITE_EEPROM 改成 1 再烧一次。
          - 如果 Topic 不是 qian1，同理。

        ★ 顺便说清 Topic 的用法 ★
          巴法云一个 Topic 只能收一路消息。本工程订阅了两个：
            Topic_Buff       → OTA 通知走这里（云端下发升级命令）
            Topic_Buff + "_msg" → 普通消息 + 版本上报走这里
          所以你在巴法云控制台往 qian1 发 OTA 命令，
          设备侧是订阅 qian1 收的；上报版本号则发到 qian1_msg。
    */
    Usart1_SendString("[MQTT] UID   = ");
    Usart1_SendString((char *)BemfaInfoCB.UID_Buff);
    Usart1_SendString("\r\n");
    Usart1_SendString("[MQTT] Topic = ");
    Usart1_SendString((char *)BemfaInfoCB.Topic_Buff);
    Usart1_SendString("  (OTA 收这个, 上报发 ");
    Usart1_SendString((char *)BemfaInfoCB.Topic_Buff);
    Usart1_SendString("_msg)\r\n");

    /*=================================================================
     * ★★ 7.2 把自己的版本信息写进 EEPROM 的 App 信息区 ★★
     *
     * 【为什么必须有这一段】
     *   BootLoader 的菜单 [3]（查看当前 APP）和 [4]（OTA 升级搬运）
     *   都要知道"当前 APP 是什么版本、多大"。
     *   但 BootLoader 自己不知道 —— 它是独立编译的，
     *   拿不到 APP 的编译信息。所以只能由 APP 启动时主动写进去。
     *
     * 【写什么】
     *   版本名  ：跟 BemfaInfoCB.Version_ABuff 保持同一个字符串，
     *             这样云端看到的版本和本地记录的一致，不会自相矛盾。
     *   版本号  ：由版本名换算（1.0.0 → 10000），供 BootLoader 比大小。
     *   大小    ：APP 镜像的真实字节数，见下面 APP_IMAGE_SIZE 的说明。
     *   App 名  ：默认 "STM32 OTA"，这里沿用 EEPROM 里的旧值。
     *   序列号  ：沿用 EEPROM 里的旧值（目前没用到，预留）。
     *
     * 【什么时候写】
     *   每次上电都写。为什么不做"只在版本变了才写"：
     *     AT24C02 擦写寿命 100 万次，每次上电写 7 次左右，
     *     一天上电 10 次的话能用 270 年，完全不用担心磨损。
     *     而"只在变了才写"需要一个额外的比较基准，多一处可能出错的地方。
     *
     * 【为什么先 Load 再改再 Save】
     *   直接构造一个全新结构体会把 app/sn 冲成空串。
     *   先读出来（读失败会自动填默认值），只改要改的字段，再写回去。
     *================================================================*/
    {
        /* ① 先读现有信息（读失败也会填默认值，不会留下空结构体） */
        (void)EEPROM_LoadAppInfo(&EEPROM_Data);

        /* ②③ 版本名 / 版本号：★不在这里写★
         *   版本号只有一个写入点：AppInfo_CommitVersion()（上面已调过）。
         *   这里只负责把"APP 实际占多大"写进去，避免两处各写一半、
         *   以后改版本时又得改两个地方。 */

        /* ④ 大小：APP 镜像的真实字节数
         *
         * ★ 这个值怎么来的（2026-11-03 改）★
         *   原来是在这里手填一个数字（20896），每次改代码都要
         *   翻 map 文件里的 Total ROM Size 再改一遍 —— 一忘就永远显示旧值。
         *
         *   现在改成让链接器自己算：
         *     ARMCC 链接时会生成一批 Image$$ 开头的符号，
         *     其中 Image$$ER_IROM1$$Limit 是"执行区 ER_IROM1 的结束地址"。
         *     拿它减去 APP 的起始地址，就是镜像占了多少字节。
         *
         *   这个数和 map 文件里的 Total ROM Size 是同一个，
         *   区别只在于：链接器自己算的，永远不会忘、也不会填错。
         *
         * ★ 为什么声明成 extern unsigned int ★
         *   Image$$ 符号本身没有类型，它只是个地址标记。
         *   取它的地址（&符号）就得到那个地址值。
         *   声明成 unsigned int 只是为了让 & 运算合法，
         *   类型不参与计算（我们只要地址）。
         *
         * ★ 注意 ★
         *   ER_IROM1 的 Limit 是"最后一段数据的末尾"，
         *   而 Total ROM Size 还额外算了 RW 段在 Flash 里的初值副本。
         *   两者可能差几十字节，用来显示"多大"足够了。 */
        {
            extern unsigned int Image$$ER_IROM1$$Limit;
            EEPROM_Data.app_size = (u32)(&Image$$ER_IROM1$$Limit) - (u32)FLASH_A_ADDR;
        }

        /* ⑤ 写回 EEPROM */
        if (EEPROM_SaveAppInfo(&EEPROM_Data) != 0)
        {
            Usart1_SendString("[EEPROM] ★App 信息写入失败\r\n");
            LCD_Log("AppInfo wr FAIL");
        }
        else
        {
            Usart1_SendString("[EEPROM] App 信息已写入：版本名=");
            Usart1_SendString(EEPROM_Data.ver);
            Usart1_SendString(" 版本号=");
            {
                char numbuf[16];
                sprintf(numbuf, "%lu", (unsigned long)EEPROM_Data.ver_num);
                Usart1_SendString(numbuf);
            }
            Usart1_SendString(" 大小=");
            {
                char numbuf[16];
                sprintf(numbuf, "%lu", (unsigned long)EEPROM_Data.app_size);
                Usart1_SendString(numbuf);
            }
            Usart1_SendString(" 字节\r\n");
            LCD_Log("AppInfo saved");
        }
    }

    /*=================================================================
     * 把 EEPROM 里的版本号同步给屏幕
     *
     * ★ 为什么单独拷一份，而不是让 lcd_ui.c 直接读 BemfaInfoCB：
     *   界面层不该知道巴法云结构体长什么样。
     *   而且 Version_ABuff 是 u8[]，UI_Status.version 是 char[]，类型对不上。
     *
     * ★ 为什么放在这里：
     *   上面 if/else 两条路（首次写默认值 / 参数有效）都走完了，
     *   此时 BemfaInfoCB 里一定已经是最终要显示的版本号。
     *
     * ★ 为什么要 LCD_UI_Refresh_All()：
     *   LCD_UI_Init() 早就跑过了（那时 version 还是空的，画的是 "--"），
     *   这里改完必须让界面下一轮重画，否则屏幕上永远停在 "--"。
     *================================================================*/
    {
        u8 vi;
        for (vi = 0; vi < (UI_VER_MAX - 1) && BemfaInfoCB.Version_ABuff[vi] != 0; vi++)
        {
            char vc = (char)BemfaInfoCB.Version_ABuff[vi];
            if (vc < 0x20 || vc > 0x7E) vc = '.';
            g_ui_status.version[vi] = vc;
        }
        g_ui_status.version[vi] = 0;

        /*=================================================================
         * 2026-11-03 加：屏幕上那一行 "VER  x.y.z"（当前运行的 APP 版本名）
         *
         * ★ 为什么不直接用上面刚填的 g_ui_status.version：
         *   version 是"要上报给云端的版本号"（BemfaInfoCB.Version_ABuff），
         *   app_ver 是"EEPROM App 信息区里记着的、BootLoader 菜单 [3] 也显示的那个"。
         *   两者来源不同 —— 一个来自巴法云参数区，一个来自 App 信息区。
         *   分开显示才能在屏幕上看出它们是否同步（不同步就是 bug）。
         *
         * ★ 为什么不用 EEPROM_Data.ver 原样拷：
         *   EEPROM 里可能是别人写进去的乱码，界面上显示控制字符会花屏，
         *   所以非可打印 ASCII 一律换成 '.'，和上面那段保持一致。
         *================================================================*/
        {
            u8 ai;
            for (ai = 0; ai < (UI_VER_MAX - 1) && EEPROM_Data.ver[ai] != 0; ai++)
            {
                char ac = EEPROM_Data.ver[ai];
                if (ac < 0x20 || ac > 0x7E) ac = '.';
                g_ui_status.app_ver[ai] = ac;
            }
            g_ui_status.app_ver[ai] = 0;
        }

        LCD_UI_Refresh_All();
    }

    if (W25Q128_Init() != 0)
    {
        Usart1_SendString("W25Q128 初始化失败\r\n");
        LCD_Log("FLASH FAIL");
    }
    else
    {
        g_ui_status.flash_ok = 1;
        LCD_Log("FLASH OK");
    }

    /*=================================================================
     * 【版本 B】云端提交的 MQTT 连接（#if 0 包住，先不启用）
     *
     * 为什么先关掉：
     *   MQTT_ConnectBemfa 里还有几个待确认的点：
     *     ① AT+QICSGP 的参数个数（云端版少了一个）
     *     ② AT+QIACT 被发了两遍
     *     ③ AT+CIPOPEN 的返回值没判断就往下走
     *     ④ 末尾 sprintf 出来的 cmd 没有发送
     *   等这些改好了再打开，否则连不上还会卡住启动流程
     *================================================================*/
#if 1
    /* 连接 MQTT 服务器 */
    MQTT_Init();
    LCD_Log("MQTT connect");
    if (MQTT_ConnectBemfa() == 0)
    {
        Usart1_SendString("连接成功\r\n");
        g_ui_status.mqtt_ok = 1;
        LCD_Log("MQTT OK");
    }
    else
    {
        Usart1_SendString("连接失败\r\n");
        LCD_Log("MQTT FAIL");
    }
#endif

    /*---------- 8. 主循环 ----------*/
    Usart1_SendString("进入 APP \r\n");
    LCD_Log("APP running");

    /* 进主循环前把"APP 运行中"置 1，屏幕 APP 那一行会从 BOOTING（红）变成 RUNNING（绿） */
    g_ui_status.app_running = 1;
    LCD_UI_Refresh_All();

    while (1)
    {
        /*=================================================================
         * 处理 4G 收到的数据
         *================================================================*/
        if (usart3_value.flag)
        {
            usart3_value.flag = 0;
            U3_EventDispatch(usart3_value.data, usart3_value.count);
            usart3_value.count = 0;
        }

        /* 处理主动事件（OTA 下载 / MQTT 连接等） */
        ActiveEvent();

        /* MQTT 心跳保活 */
        if (SysEventFlag & CONNECT_EVENT)
        {
            /* 连上后上报一次版本号（OTA 流程第一步） */
            if (VersionSent == 0)
            {
                VersionSent = 1;
                CAT1_PropertyVersion();
                PingTimer = Get_Tick();         /* ★从这一刻开始算心跳周期 */
                LCD_Log("ver reported");
            }

            /* ★2026-11-03 改：按毫秒时基判断，不再数主循环圈数★
               原来 ConnectTimer++ 是"每圈加 1"，主循环一转慢心跳就跟着长；
               现在看的是真实耗时，主循环跑多快都无所谓。
               无符号减法自动处理 48.6 天回绕，不会因为溢出而漏发。

               ★2026-11-04 加：等 PINGRESP，超时就重连★
               原来只管每 30 秒发一包，发没发出去、服务器回没回一概不管。
               链路已经断了的时候，CIPSEND 每次还会阻塞最多 5 秒，
               主循环跟着卡，看起来就是"掉线了还一直僵着"。
               现在：上一包没等到 PINGRESP 就不再发新的，超过 45 秒
               直接判定掉线，置 CONNECT_START 重连。 */
            if (SysEventFlag & PING_SENT)
            {
                if ((u32)(Get_Tick() - s_ping_sent_ms) > 45000u)
                {
                    Usart1_SendString("[MQTT] ★心跳 45 秒无应答，重连\r\n");
                    SysEventFlag &= ~CONNECT_EVENT;
                    SysEventFlag &= ~PING_SENT;
                    SysEventFlag |= CONNECT_START;
                    PingTimer = Get_Tick();
                    LCD_Log("ping timeout");
                }
            }
            else if ((u32)(Get_Tick() - PingTimer) >= PING_INTERVAL_MS)
            {
                PingTimer = Get_Tick();
                s_ping_sent_ms = PingTimer;
                SysEventFlag |= PING_SENT;      /* 标记"等应答"；收到 PINGRESP 时 mqtt.c 会清掉 */
                if (MQTT_PingREQ() != 0)
                {
                    /* 没发出去就别等了，直接当这一包丢了：清掉等待标志，
                       下一轮按周期重发；连续丢会被上面的 45 秒超时兜住。 */
                    SysEventFlag &= ~PING_SENT;
                    Usart1_SendString("[MQTT] ★心跳发送失败（CIPSEND 没拿到提示符）\r\n");
                }
            }
        }

        /*=================================================================
         * 界面与指示灯（放在最后，优先级最低）
         *
         * ★ 为什么放最后：
         *   这两件事都"不影响功能，只影响观感"。
         *   放前面的话，一旦屏幕或灯出问题，会挡住串口收包和心跳。
         *
         * ★ LCD_UI_Update 内部自带 500ms 节流：
         *   主循环每轮 delay_ms(10)，50 轮才真正刷一次屏。
         *   其余 49 轮它立刻返回，开销只有一次自增和比较。
         *================================================================*/
       // My_App();
        Led_Heartbeat();
        LCD_UI_Update();

        delay_ms(10);
    }
}

//void My_App()
//{
  //  Usart1_SendString("My_App() 运行中\r\n");
  //  Usart1_SendString("\r\n");
//}
