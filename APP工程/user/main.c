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
u32 ConnectTimer = 0;          /* 连上服务器后的计时（秒） */
u32 PingTimer = 0;             /* 上次发 PING 的时间（秒） */

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

    off = U3_SkipTcpPrefix(data, len);
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

        if (CAT1_OTA(URL_ABuff, BemfaInfoCB.OTA_firelen) == 0)
        {
            // 将事件处理标志位清零，后续跳转到 APP 区域
            BemfaInfoCB.OTA_flag = BOOT_STA_O;
            EEPROM_SaveInfo();
        }

        // 如果失败则复位到 Bootloder 区域 等待下一次 OTA 事件
        NVIC_SystemReset();
    }

    // 如果收到的指令是 MQTT 连接事件
    if (SysEventFlag & CONNECT_START)
    {
        SysEventFlag &= ~CONNECT_START;
        MQTT_ConnectBemfa();
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

    /*---------- 1. 初始化调试串口（必须最先）----------*/
    Usart1_Init(USART1_BRR);
    Usart1_SendString("\r\n\r\n");
    Usart1_SendString("=== STM32 OTA 启动 ===\r\n");

    /*---------- 2. 初始化 4G 模块串口 ----------*/
    Usart3_Init(USART3_BRR);
    Usart1_SendString("USART3 初始化完成\r\n");

    /*---------- 3. 等 4G 模块启动 ----------*/
    /* 模块上电后要 10~15 秒才能响应 AT，太早发没反应 */
    Usart1_SendString("等待 4G 模块启动...\r\n");
    delay_ms(10000);

    /*---------- 4. 激活 GPRS ----------*/
    /* 内部已有详细打印，失败会打印步骤号 */
    if (CAT1_ActiveGPRS() != 0)
    {
        Usart1_SendString("GPRS 激活失败，检查 SIM 卡和信号\r\n");
    }


    /*---------- 5. 各模块初始化 ----------*/
    /* TODO【D】：EEPROM 和 SPI Flash 初始化 */

    // 初始化 I2C 和 SPI

    I2C_GPIO_Init();
    SPI2_Init();
    if (EEPROM_ReadInfo() != 0)
    {
        Usart1_SendString("EEPROM 读取失败\r\n");
    }
    if (W25Q128_Init() != 0)
    {
        Usart1_SendString("W25Q128 初始化失败\r\n");
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
    if (MQTT_ConnectBemfa() == 0)
    {
        Usart1_SendString("连接成功\r\n");
    }
    else
    {
        Usart1_SendString("连接失败\r\n");
    }
#endif

    /*---------- 6. 主循环 ----------*/
    Usart1_SendString("进入 APP \r\n");

    while (1)
    {
        /*=================================================================
         * 处理 4G 收到的数据
         *
         * 两版都保留：
         *   【版本 A】D 的原始版本（被注释掉，框架版）
         *   【版本 B】云端提交的版本（#if 0 包住，测试用）
         *   正式版要选一个启用，现在两个都关着，等联调时再定
         *================================================================*/

        /*---------- 【版本 A】D 的框架版（暂时注释）----------*/

        if (usart3_value.flag)
        {
            usart3_value.flag = 0;
            U3_EventDispatch(usart3_value.data, usart3_value.count);
            usart3_value.count = 0;
        }

        /*---------- 【版本 B】云端提交版（#if 0 包住）----------
         * 和版本 A 的差别：
         *   多了 count 清零，避免下次收数据时追加到旧数据后面
         *   但少了注释说明，风格不统一
         *----------------------------------------------------------------*/
#if 0
        if (usart3_value.flag)
        {
            usart3_value.flag = 0;
            U3_EventDispatch(usart3_value.data, usart3_value.count);
            usart3_value.count = 0;
        }
#endif

        /* 处理主动事件（OTA 下载 / MQTT 连接等） */
        ActiveEvent();

        /* TODO【C】：MQTT 心跳保活 */
        if (SysEventFlag & CONNECT_EVENT)
        {
            /* 连上后上报一次版本号（OTA 流程第一步） */
            if (VersionSent == 0)
            {
                VersionSent = 1;
                CAT1_PropertyVersion();
            }

            ConnectTimer++;
            if (ConnectTimer - PingTimer >= 3000)
            {
                PingTimer = ConnectTimer;
                MQTT_PingREQ();
            }
        }

        delay_ms(10);
    }
}
