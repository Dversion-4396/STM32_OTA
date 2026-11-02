
#include "common.h"
#include "usart.h"
#include "delay.h"
#include "i2c.h"
#include "eeprom.h"
#include "spi.h"
#include "cat1.h"
#include "mqtt.h"
#define TEST_URL "http://httpbin.org/get" // TODO [A] 测试阶段用
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
/*=====================================================================
 * 函数：MQTT_CheckPacket
 * 功能：检查 data 处是不是一个完整的 MQTT 报文
 * 参数：data    - 数据起始位置
 *       len     - 从 data 开始的可用字节数
 *       pkt_len - 输出：这个报文的总长度（固定头 + 剩余长度）
 * 返回：1 = 是合法 MQTT 报文，0 = 不是
 * 负责：D
 *
 *=====================================================================
 * ★ 为什么需要这个函数（2026-11-02 踩坑记录）★
 *
 *   之前用"扫描第一个字节的高 4 位"来判断 MQTT 报文，结果：
 *     '+' 的 ASCII 是 0x2B，0x2B & 0xF0 == 0x20
 *     → 被误判成 CONNACK
 *     → 然后 data[3] 读到 'P'(0x50)=80
 *     → 打印"连接被拒，返回码 = 80"
 *
 *   所以必须验证"剩余长度"是否和实际数据量对得上，
 *   不能只看第一个字节。
 *====================================================================*/
static u8 MQTT_CheckPacket(u8 *data, u16 len, u16 *pkt_len)
{
    u16 pos = 1;
    u32 rem = 0;
    u32 mul = 1;
    u8 b;
    u8 type;
    u8 cnt = 0;

    if (len < 2)
        return 0;

    /*---------- ① 报文类型检查 ----------*/
    type = data[0] & 0xF0;
    if (type != 0x20 && type != 0x30 && type != 0x40 &&
        type != 0x90 && type != 0xD0)
    {
        return 0;
    }

    /*---------- ② 解析剩余长度（变长编码，最多 4 字节）----------*/
    do
    {
        if (pos >= len)
            return 0;
        if (cnt >= 4)
            return 0; /* 超过 4 字节 = 非法 */
        b = data[pos++];
        rem += (u32)(b & 0x7F) * mul;
        mul *= 128;
        cnt++;
    } while (b & 0x80);

    if (rem > 0xFFFF)
        return 0;

    /*---------- ③ 报文总长度必须 <= 可用数据量 ----------*/
    *pkt_len = (u16)(pos + rem);
    if (*pkt_len > len)
        return 0;

    return 1;
}

/*=====================================================================
 * 函数：U3_EventDispatch
 * 功能：USART3（4G）收到数据后的事件分发
 * 参数：data - 收到的数据；len - 长度
 * 返回：无
 * 负责：D
 *
 *=====================================================================
 * ★ 4G 模块收到 TCP 数据的实际格式（2026-11-02 实测）★
 *
 *   \r\n
 *   +CIPRXGET:SUCCESS,0,0,10,\r\n     ← 头，最后那个数字 = 数据长度
 *   30 08 00 03 31 32 33 33 33 33     ← 正好 10 字节 MQTT 报文
 *   \r\n
 *
 *   对应巴法云发 "333"：
 *     30        PUBLISH
 *     08        剩余长度 8
 *     00 03     主题长度 3
 *     31 32 33  主题 "123"
 *     33 33 33  载荷 "333"    ← 要的
 *
 *=====================================================================
 * ★ 处理方式 ★
 *
 *   1. 先找 "+CIPRXGET:"，解析头里第 4 个字段（长度）
 *   2. 数据从头的 '\n' 之后开始，长度用解析出来的数字
 *   3. 这样能精确切出 MQTT 报文，不会把后面的 \r\n 算进去
 *====================================================================*/
void U3_EventDispatch(u8 *data, u16 len)
{
    char *hdr;
    char *p;
    char *q;
    u16 data_start;
    u16 data_len;
    u16 i;
    u16 pkt_len;
    u8 field;

    if (data == NULL || len == 0)
        return;

    /*---------- 先补 \0，方便用 strstr / strchr ----------
     * 缓冲区 1024 字节，中断里已经做了越界保护，
     * 这里再判一次，防止 count == 1024 时写越界
     *------------------------------------------------*/
    if (len < USART3_BUFFER_SIZE)
    {
        data[len] = '\0';
    }

    /*=================================================================
     * 路径 1：有 +CIPRXGET 头（正常情况）
     *================================================================*/
    hdr = strstr((char *)data, "+CIPRXGET:");
    if (hdr != NULL)
    {
        /*---------- ① 找头结束的 '\n' ----------*/
        p = strchr(hdr, '\n');
        if (p == NULL)
        {
            return;
        }

        data_start = (u16)(p + 1 - (char *)data);

        /*---------- ② 解析数据长度 ----------
         * 头格式：+CIPRXGET:SUCCESS,<link>,<mode>,<len>,
         *   字段 0 = SUCCESS
         *   字段 1 = link（会话 ID）
         *   字段 2 = mode
         *   字段 3 = len（数据长度）  ← 要这个
         *------------------------------------------------*/
        data_len = 0;
        field = 0;
        q = hdr + 10; /* 跳过 "+CIPRXGET:" */

        while (*q != '\0' && *q != '\r' && *q != '\n')
        {
            if (*q == ',')
            {
                field++;
            }
            else if (field == 3 && *q >= '0' && *q <= '9')
            {
                data_len = (u16)atoi(q);
                break;
            }
            q++;
        }

        /*---------- ③ 越界保护 ----------*/
        if (data_start >= len)
            return;
        if (data_len == 0)
            return;
        if ((u32)data_start + (u32)data_len > (u32)len)
        {
            data_len = len - data_start;
        }

        /*---------- ④ 交给 MQTT 处理 ----------*/
        U2PassiveEvent(&data[data_start], data_len);
        return;
    }

    /*=================================================================
     * 路径 2：没有 CIPRXGET 头
     *   可能是模块直接推（少数情况），或者 AT 响应残留。
     *   扫描找合法 MQTT 报文（带完整校验，避免误判）
     *================================================================*/
    for (i = 0; i + 2 <= len && i < 64; i++)
    {
        if (MQTT_CheckPacket(&data[i], len - i, &pkt_len))
        {
            U2PassiveEvent(&data[i], pkt_len);
            return;
        }
    }

    /* 两条路径都没命中：不是 MQTT 数据（可能是 AT 响应残留），忽略 */
}
void ActiveEvent(void)
{

    if (SysEventFlag & OTA_EVENT)
    {
        SysEventFlag &= ~OTA_EVENT;

        if (CAT1_OTA(URL_ABuff, BemfaInfoCB.OTA_firelen) == 0)
        {
            BemfaInfoCB.OTA_flag = BOOT_STA_O;
            EEPROM_SaveInfo();
            delay_ms(2000);
            NVIC_SystemReset();
        }
        else
        {
            BemfaInfoCB.OTA_flag = 0;
            EEPROM_SaveInfo();
        }
    }

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
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
    GPIO_InitTypeDef gpio_initStruct = {0};
    gpio_initStruct.GPIO_Pin = GPIO_Pin_5;
    gpio_initStruct.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_initStruct.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOB, &gpio_initStruct);
    gpio_initStruct.GPIO_Pin = GPIO_Pin_5;
    gpio_initStruct.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_initStruct.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOE, &gpio_initStruct);
    
    GPIO_ResetBits(GPIOE,GPIO_Pin_5);
    GPIO_ResetBits(GPIOB,GPIO_Pin_5);

    /*---------- 1. 初始化调试串口（必须最先）----------*/
    Usart1_Init(USART1_BRR);
    Usart1_SendString("\r\n\r\n");
    Usart1_SendString("=== STM32 OTA 启动 ===\r\n");

    /*---------- 2. 初始化 4G 模块串口 ----------*/
    Usart3_Init(USART3_BRR);
    Usart1_SendString("USART3 初始化完成\r\n");

    delay_ms(1000);
    CAT1_SendAT("ATE0", "OK", 2000);
    Usart1_SendString("[INIT] 已关闭模块回显\r\n");
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
    //// 初始化各模块
    Usart1_SendString("各模块初始化");
    I2C_GPIO_Init();
    EEPROM_ReadInfo();

    /*=================================================================
     * ★★ 首次上电：初始化 EEPROM 参数（2026-11-02 修复 OTA 不生效）★★
     *
     * 【问题现象】
     *   APP 下载固件成功 → 置 OTA_flag = 0x5AA5C33C → 存 EEPROM → 重启
     *   BootLoader 上电却打印 "[EEPROM] 参数无效，需要重新配置"
     *   然后 "BootLoader 已退出跳转到APP" —— 根本没进 OTA 模式
     *
     * 【根因】
     *   BootLoader 的 EEPROM_ReadInfo() 里有这段：
     *     if (BemfaInfoCB.valid_flag != VALID_BEMFA_DATA)
     *     {
     *         memset(&BemfaInfoCB, 0, sizeof(BemfaInfoCB));   ← 全清零
     *     }
     *   而 APP 从来没设过 valid_flag，它一直是 0。
     *   所以 APP 辛苦写进去的 OTA_flag，被 BootLoader 读的时候清零了。
     *
     * 【修复】
     *   APP 首次上电时，把 valid_flag 设成 0xDD 并保存。
     *   以后上电走 else 分支，不会再清零。
     *
     * 【验证】
     *   修复后 BootLoader 应该打印 "[EEPROM] 参数有效"
     *   而不是 "[EEPROM] 参数无效，需要重新配置"
     *================================================================*/
    if (BemfaInfoCB.valid_flag != VALID_BEMFA_DATA)
    {
        Usart1_SendString("[INIT] 首次上电，初始化 EEPROM 参数\r\n");

        BemfaInfoCB.valid_flag = VALID_BEMFA_DATA;   /* 0xDD */

        /* 巴法云私钥（32 位）*/
        strcpy((char *)BemfaInfoCB.UID_Buff, USERNAME_KEY);

        /* 订阅的 Topic */
        strcpy((char *)BemfaInfoCB.Topic_Buff, TOPIC_1);

        /* 版本号（最长 23 字符）*/
        strncpy((char *)BemfaInfoCB.Version_ABuff, "v1.0.0", VERSION_LEN);
        BemfaInfoCB.Version_ABuff[VERSION_LEN] = '\0';

        /* OTA 状态清零 */
        BemfaInfoCB.OTA_firelen = 0;
        BemfaInfoCB.OTA_flag    = 0;

        if (EEPROM_SaveInfo() == 0)
        {
            Usart1_SendString("[INIT] ★EEPROM 参数已初始化\r\n");
        }
        else
        {
            Usart1_SendString("[INIT] ★EEPROM 写入失败\r\n");
        }
    }

    Usart1_SendString("EEPROM 读取完成\r\n");
    SPI2_Init();
    Usart1_SendString("SPI 初始化完成\r\n");
    if (W25Q128_Init() != 0)
    {
        Usart1_SendString("W25Q128 初始化失败\r\n");
    }
    else
    {
        Usart1_SendString("W25Q128 初始化成功\r\n");
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
    Usart1_SendString("进入主循环\r\n");

    while (1)
    {
        if (usart3_value.flag)
        {
            usart3_value.flag = 0;
            U3_EventDispatch(usart3_value.data, usart3_value.count);
            usart3_value.count = 0;
        }

        /* 处理主动事件（OTA 下载 / MQTT 连接等） */
        ActiveEvent();

        if (SysEventFlag & CONNECT_EVENT)
        {
            ConnectTimer++;
            if (ConnectTimer - PingTimer >= 5000)
            {
                PingTimer = ConnectTimer;
                MQTT_PingREQ();
            }
        }

        delay_ms(10);
    }
}
