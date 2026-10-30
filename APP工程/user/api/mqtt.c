/*=====================================================================
 * 文件：mqtt.c
 * 说明：MQTT 协议实现（巴法云 bemfa）
 * 负责：C
 *
 *=====================================================================
 * ★ 平台变更说明（2026-10-27）★
 *
 * 从阿里云 IoT 改为巴法云（bemfa）。这个文件最大的变化是：
 * 【不需要加密库了】
 *
 *   阿里云需要：utils_sha1.c + utils_md5.c + utils_hmac.c（共 797 行）
 *   巴法云需要：无
 *
 * 原因：
 *   阿里云的 MQTT 密码要用 HMAC-SHA1 签名算出来：
 *     Password = HMAC-SHA1(clientId + deviceName + productKey, deviceSecret)
 *   巴法云的密码就是 UID 本身：
 *     Password = UID
 *
 * 所以 utils_sha1.c 和 utils_hmac.c 完全不用移植。
 * （如果要做固件 MD5 校验，只需 utils_md5.c 一个，约 200 行）
 *
 *=====================================================================
 * ★ 移植来源 ★
 *
 *   MQTT 协议栈从 36 号工程移植（338 行）：
 *   E:\BaiduNetdiskDownload\新建文件夹\36-MQTT协议...\HARDWARE\Scr\mqtt.c
 *
 *   移植时只需改 3 个宏：
 *     #define CAT1_printf     Usart3_SendString
 *     #define CAT1_RX_BUF     usart3_value.data
 *     #define CAT1_RxCounter  usart3_value.count
 *=====================================================================*/
#include "mqtt.h"
#include "usart.h"
#include "delay.h"

/*==================== 全局变量 ====================*/
MQTT_CB Bemfa_mqtt;

/*=====================================================================
 * 函数：MQTT_Init
 * 负责：C
 *
 * ★ 巴法云版本（比阿里云简单得多）★
 *
 * 实现要点：
 *   1. ClientID：任意唯一字符串
 *      建议带上设备标识，避免多设备用同一个 ClientID 互相踢下线
 *      sprintf(Bemfa_mqtt.ClientID, "stm32_%s", BemfaInfoCB.Version_ABuff);
 *
 *   2. Username：巴法云 UID（32位私钥）
 *      sprintf(Bemfa_mqtt.Username, "%s", BemfaInfoCB.UID_Buff);
 *
 *   3. Password：也是 UID（和 Username 完全一样）
 *      sprintf(Bemfa_mqtt.Passward, "%s", BemfaInfoCB.UID_Buff);
 *      ← ★这就是省掉加密库的关键：不用算 HMAC-SHA1 了★
 *
 *   4. ServerIP 和端口：
 *      sprintf(Bemfa_mqtt.ServerIP, "bemfa.com");
 *      Bemfa_mqtt.ServerPort = 9501;      ← TCP 明文；TLS 用 9503
 *
 *   5. 构建订阅 Topic（巴法云 Topic 自己定）：
 *      Stopic_Buff[0] = "ota{uid}"        ← ★OTA 命令下发，核心★
 *      Stopic_Buff[1] = "stm32{uid}"      ← 通用消息（调试用）
 *      拼接方法：
 *        sprintf(Bemfa_mqtt.Stopic_Buff[0], "ota%s", BemfaInfoCB.UID_Buff);
 *        sprintf(Bemfa_mqtt.Stopic_Buff[1], "stm32%s", BemfaInfoCB.UID_Buff);
 *
 * ★★★ 注意 ★★★
 *   服务器地址、端口、Topic 格式这三样【必须】去 bemfa.com 官网确认，
 *   上面写的是凭经验的值，参数不对会连不上。
 *====================================================================*/
void MQTT_Init(void)
{
    /*=================================================================
     * 功能：初始化 MQTT 连接参数（ClientID / 用户名 / 密码 / 服务器 / Topic）
     *
     * ★ 巴法云的关键规则（官方文档"MQTT协议接入"）★
     *
     *   方式一：把【私钥】直接当 ClientID，用户名/密码随便填甚至留空
     *           —— 不需要额外认证，最省事
     *
     *   方式二：如果 ClientID 不是私钥，服务器改用
     *           username=appID、password=secretKey 认证
     *
     *   ★ C 在测试工程里踩过这个坑 ★
     *     ClientID 写 "client"，触发方式二，但用户名填的是私钥不是 appID，
     *     服务器回 CONNACK 0x05（未授权）。
     *     改成方式一（ClientID = 私钥）就通了。
     *
     *=================================================================
     * 本实现：ClientID / Username / Password 全部填私钥（方式一）
     *================================================================*/
    sprintf(Bemfa_mqtt.ClientID,  USERNAME_KEY);
    sprintf(Bemfa_mqtt.Username,  USERNAME_KEY);
    sprintf(Bemfa_mqtt.Passward,  USERNAME_KEY);

    sprintf(Bemfa_mqtt.ServerIP,  SERVER_IP);
    Bemfa_mqtt.ServerPort = SERVER_PORT;

    /*---------- 订阅 Topic ----------
     * 巴法云控制台里创建的主题，直接用名字，不需要拼 UID 后缀。
     *
     * ★ 注意：这里的 "123" 和 "qwe" 是 C 测试时用的临时主题名 ★
     *   正式版应该从 EEPROM 的 BemfaInfoCB.Topic_Buff 读，
     *   或者按 OTA 需求拼成 "ota{uid}"。
     *----------------------------------------------------------------*/
    strcpy(Bemfa_mqtt.Stopic_Buff[0], TOPIC_1);
    strcpy(Bemfa_mqtt.Stopic_Buff[1], TOPIC_2);
    strcpy(Bemfa_mqtt.Stopic_Buff[2], "");
}

/*=====================================================================
 * 函数：MQTT_ConnectBemfa
 * 负责：C
 *
 * 功能：连接巴法云服务器
 * 返回：0 = 报文发出去了（不代表连上了，要等 CONNACK）
 *       1 = TCP 连接失败
 *
 * 实现要点：
 *   1. 用 AT+CIPSTART 建立 TCP 连接到 bemfa.com:9501
 *        CAT1_SendAT("AT+CIPSTART=\"TCP\",\"bemfa.com\",9501", "CONNECT OK", 10000);
 *
 *   2. 调 MQTT_ConectPack() 构建 CONNECT 报文到 Pack_buff
 *
 *   3. 用 AT+CIPSEND 发出
 *        sprintf(cmd, "AT+CIPSEND=%d", Fixed_len);
 *        CAT1_SendAT(cmd, ">", 3000);           ← 等模块回 '>' 提示符
 *        Usart3_SendBuffer((char*)Pack_buff, Fixed_len);
 *        CAT1_SendAT("", "SEND OK", 5000);      ← 等发送完成
 *
 *   4. 返回 0，然后等 U2PassiveEvent 收到 CONNACK（0x20）
 *
 * ★ 和阿里云的区别 ★
 *   阿里云要连 {pk}.iot-as-mqtt.cn-shanghai.aliyuncs.com:1883
 *   巴法云只要连 bemfa.com:9501，简单得多
 *====================================================================*/
u8 MQTT_ConnectBemfa(void)
{
    /*=================================================================
     * 功能：连接巴法云 MQTT 服务器
     *
     * 返回：0 = 成功（CONNECT 报文已发出）
     *       1 = 配置 APN 失败
     *       2 = 激活 PDP 失败
     *       3 = TCP 连接失败
     *
     *=================================================================
     * ★★★ 重要说明 ★★★
     *
     * 这个函数原来是从 C 的 WiFi 测试工程复制过来的，
     * 里面全是 ESP8266 的指令（AT+CWMODE / AT+CWJAP / AT+CIPMODE）。
     *
     * 本工程用的是【移柯 L511C-2C 4G 模块】，完全不认那些 WiFi 指令。
     * 所以 WiFi 版本的代码已经用 #if 0 屏蔽掉了（见下方），
     * 保留是为了参考 MQTT 报文发送的时序。
     *
     * 现在的实现按 CT511 手册（docs/CT511_AT指令手册_提取.md）走 4G 流程。
     *
     *=================================================================
     * ★ 待确认 ★
     *
     * CT511 的 AT+CIPOPEN 具体行为还没实测：
     *   - 是透传模式（发了数据直接进 TCP）
     *   - 还是 AT 模式（还要 AT+CIPSEND 指定长度）
     *
     * 如果是透传模式，本函数末尾直接发 CONNECT 报文即可；
     * 如果是 AT 模式，要在 MQTT_ConectPack 之前加 AT+CIPSEND。
     *
     * ★ 请 C 先做这个测试 ★
     *   AT+CIPOPEN=0,"TCP","bemfa.com",9501
     *   看返回是 OK + +CIPOPEN: 0,0（成功）还是 ERROR
     *================================================================*/

#if 0
    /*------------------------------------------------------------------
     * 【已废弃】WiFi 版本（ESP8266）
     *
     * 这段是从 C 的 WiFi 测试工程（E:\桌面\STM32F103ZET6_MQTTceshi）
     * 复制过来的原始代码，在 WiFi 模块上实测能跑通。
     *
     * 但本工程用的是 4G 模块，这些指令全部无效，所以屏蔽掉。
     * 保留作为参考：可以看 MQTT 报文发送的时序安排。
     *------------------------------------------------------------------*/
    {
        char at_cmd[64] = "";

        Usart1_Init(USART1_BRR);
        Usart3_Init(USART3_BRR);

        Usart3_SendString("AT\r\n");
        delay_ms(2000);

        Usart3_SendString("AT+CWMODE_CUR=1\r\n");       /* WiFi STA 模式 */
        delay_ms(2000);

        Usart3_SendString("AT+CWLAP\r\n");              /* 扫描热点 */
        delay_ms(3000);

        sprintf(at_cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", AP_SSID, AP_PSK);
        Usart3_SendBuffer(at_cmd, strlen(at_cmd));      /* 连 WiFi */
        delay_ms(10000);

        Usart3_SendString("AT+CIFSR\r\n");              /* 查 IP */
        delay_ms(3000);

        memset(at_cmd, 0, 64);
        sprintf(at_cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n",
                MQTT_SERVER_IP_ADDR, MQTT_SERVER_PORT);
        Usart3_SendBuffer(at_cmd, strlen(at_cmd));
        delay_ms(2000);

        Usart3_SendString("AT+CIPMODE=1\r\n");          /* 透传模式 */
        delay_ms(2000);

        Usart3_SendString("AT+CIPSEND\r\n");            /* 进透传 */
        delay_ms(2000);

        MQTT_ConectPack();

        delay_ms(2000);
        return 1;
    }
#endif

    /*==================================================================
     * 【待实现】4G 版本的连接流程
     *
     * ★★★ 为什么这里是空的 ★★★
     *
     * 下面 #if 0 里的 WiFi 版本是 C 在 ESP8266 上实测跑通的，
     * 但 4G 模块（移柯 L511C-2C）的 TCP 连接指令还没验证过。
     *
     * 按 docs/CT511_AT指令手册_提取.md 推测流程是：
     *   ① AT+QICSGP=1,1,"CMNET","","",1    配置 APN   ← 已实测 OK
     *   ② AT+QIACT=1                       激活 PDP   ← 已实测 OK
     *   ③ AT+CIPOPEN=0,"TCP","bemfa.com",9501   建 TCP  ← ★未验证★
     *   ④ 发 CONNECT 报文（透传 or 需要 CIPSEND，未知）
     *   ⑤ 订阅 Topic
     *
     * ★ 需要 C 先做这个测试再写代码 ★
     *
     *   AT+CIPOPEN=0,"TCP","bemfa.com",9501
     *
     *   看返回是：
     *     OK + +CIPOPEN: 0,0   → 成功，可以继续
     *     ERROR                → 指令不对，要换别的
     *
     *   同时要确认：CIPOPEN 之后是透传模式（数据直接进 TCP），
     *   还是 AT 模式（还要 AT+CIPSEND=<len> 指定长度）。
     *
     * 测通之后再按实际结果填这个函数。
     *==================================================================*/

    /* TODO【C】：等 AT+CIPOPEN 实测通过后实现 */
    
    char cmd[128] = "";
    Usart1_SendString("[MQTT]配置APN\r\n");
    /* CAT1_SendAT 返回值：0 = 收到期望关键字（成功），1 = 超时（失败）
     * 所以"失败才报错"要写成 != 0，注意别写成 == 0 */
    if(CAT1_SendAT("AT+QICSGP=1,1,\"CMNET\",\"\",1", "OK", 10000) != 0)
    {
        Usart1_SendString("[MQTT] 配置APN失败\r\n");
        return 9;
    }
    Usart1_SendString("[MQTT] 激活PDP\r\n");
    Usart3_SendString("AT+QIACT=1\r\n");
    delay_ms(10000);
    if(CAT1_SendAT("AT+QIACT=1", "OK", 10000) != 0)
    {
        Usart1_SendString("[MQTT] 激活PDP失败\r\n");
        return 9;
    }
    Usart1_SendString("[MQTT] 建立TCP连接\r\n");
    /* expect 只能填"一段"能被 strstr 找到的字符串，多行响应请挑最有代表性的一行
     * ★ 该返回值暂不判断，等实测确认 CIPOPEN 的响应格式后再加 ★ */
    CAT1_SendAT("AT+CIPOPEN=0,\"TCP\",\"bemfa.com\",9501", "+CIPOPEN: 0,0", 10000);
    MQTT_ConectPack();
    delay_ms(2000);
    Usart1_SendString("[MQTT] 发送CONNECT报文\r\n");
    MQTT_Subscribe(Bemfa_mqtt.Stopic_Buff,2,0);
    sprintf(cmd, "AT+CIPSEND=%d", Bemfa_mqtt.Fixed_len);
    return 0;
}

/*=====================================================================
 * 函数：MQTT_ConectPack
 * 负责：C（从 36 号工程移植）
 *
 * 移植说明：直接从 36 号工程 mqtt.c 复制这个函数，
 *          把 u2_printf 改成 Usart3_SendString
 *=====================================================================*/
void MQTT_ConectPack(void)
{
    /*=================================================================
     * 功能：组装并发送 MQTT CONNECT 报文
     *
     * 来源：从 36 号工程移植（C 在 WiFi 测试工程里实测跑通）
     *
     * CONNECT 报文结构：
     *   固定头：0x10 + 剩余长度
     *   可变头：00 04 4D 51 54 54 04 C2 00 64
     *           协议名 MQTT / 协议级别 4 / 连接标志 C2 / KeepAlive 100s
     *   载荷：  ClientID + Username + Password
     *================================================================*/
    int temp;          // 计算报文剩余长度时，使用的临时变量
    int Remaining_len; // 保存报文剩余长度字节

    Bemfa_mqtt.MessageID = 0;                                                                                                     // 报文标识符清零，CONNECT报文虽然不需要添加报文标识符，但是CONNECT报文是第一个发送的报文，在此清零报文标识符，为后续报文做准备
    Bemfa_mqtt.Fixed_len = 1;                                                                                                     // CONNECT报文，固定报头长度暂定为1
    Bemfa_mqtt.Variable_len = 10;                                                                                                 // CONNECT报文，可变报头长度=10
    Bemfa_mqtt.Payload_len = 2 + strlen(Bemfa_mqtt.ClientID) + 2 + strlen(Bemfa_mqtt.Username) + 2 + strlen(Bemfa_mqtt.Passward); // CONNECT报文，计算负载长度
    Remaining_len = Bemfa_mqtt.Variable_len + Bemfa_mqtt.Payload_len;                                                             // 剩余长度=可变报头长度+负载长度

    Bemfa_mqtt.Pack_buff[0] = 0x10; // CONNECT报文 固定报头第1个字节 ：0x10
    do
    {                                        // 循环处理固定报头中的剩余长度字节，字节量根据剩余字节的真实长度变化
        temp = Remaining_len % 128;          // 剩余长度取余128
        Remaining_len = Remaining_len / 128; // 剩余长度取整128
        if (Remaining_len > 0)
            temp |= 0x80;                                  // 如果Remaining_len大于等于128了 按协议要求位7置位
        Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len] = temp; // 剩余长度字节记录一个数据
        Bemfa_mqtt.Fixed_len++;                            // 固定报头总长度+1
    } while (Remaining_len > 0); // 如果Remaining_len>0的话，再次进入循环

    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 0] = 0x00; // CONNECT报文，可变报头第1个字节 ：固定0x00
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 1] = 0x04; // CONNECT报文，可变报头第2个字节 ：固定0x04
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 2] = 0x4D; // CONNECT报文，可变报头第3个字节 ：固定0x4D
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 3] = 0x51; // CONNECT报文，可变报头第4个字节 ：固定0x51
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 4] = 0x54; // CONNECT报文，可变报头第5个字节 ：固定0x54
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 5] = 0x54; // CONNECT报文，可变报头第6个字节 ：固定0x54
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 6] = 0x04; // CONNECT报文，可变报头第7个字节 ：固定0x04
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 7] = 0xC2; // CONNECT报文，可变报头第8个字节 ：使能用户名和密码校验，不使用遗嘱功能，不保留会话功能
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 8] = 0x00; // CONNECT报文，可变报头第9个字节 ：保活时间高字节 0x00
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 9] = 0x64; // CONNECT报文，可变报头第10个字节：保活时间高字节 0x64   最终值=100s

    /*     CLIENT_ID      */
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 10] = strlen(Bemfa_mqtt.ClientID) / 256;                        // 客户端ID长度高字节
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 11] = strlen(Bemfa_mqtt.ClientID) % 256;                        // 客户端ID长度低字节
    memcpy(&Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 12], Bemfa_mqtt.ClientID, strlen(Bemfa_mqtt.ClientID)); // 复制过来客户端ID字串
    /*     用户名        */
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 12 + strlen(Bemfa_mqtt.ClientID)] = strlen(Bemfa_mqtt.Username) / 256;                        // 用户名长度高字节
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 13 + strlen(Bemfa_mqtt.ClientID)] = strlen(Bemfa_mqtt.Username) % 256;                        // 用户名长度低字节
    memcpy(&Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 14 + strlen(Bemfa_mqtt.ClientID)], Bemfa_mqtt.Username, strlen(Bemfa_mqtt.Username)); // 复制过来用户名字串
    /*      密码        */
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 14 + strlen(Bemfa_mqtt.ClientID) + strlen(Bemfa_mqtt.Username)] = strlen(Bemfa_mqtt.Passward) / 256;                        // 密码长度高字节
    Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 15 + strlen(Bemfa_mqtt.ClientID) + strlen(Bemfa_mqtt.Username)] = strlen(Bemfa_mqtt.Passward) % 256;                        // 密码长度低字节
    memcpy(&Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len + 16 + strlen(Bemfa_mqtt.ClientID) + strlen(Bemfa_mqtt.Username)], Bemfa_mqtt.Passward, strlen(Bemfa_mqtt.Passward)); // 复制过来密码字串

    // TxDataBuf_Deal(Bemfa_mqtt.Pack_buff, Bemfa_mqtt.Fixed_len + Bemfa_mqtt.Variable_len + Bemfa_mqtt.Payload_len); //加入发送数据缓冲区
    Usart3_SendBuffer(Bemfa_mqtt.Pack_buff, Bemfa_mqtt.Fixed_len + Bemfa_mqtt.Variable_len + Bemfa_mqtt.Payload_len);
    //// DDDDD
}

/*=====================================================================
 * 函数：MQTT_Subscribe
 * 负责：C（从 36 号工程移植）
 *=====================================================================*/
void MQTT_Subscribe(char topics[][TOPIC_SIZE], int num, int qos)
{
    /*=================================================================
     * 功能：组装并发送 SUBSCRIBE 报文（订阅多个 Topic）
     *
     * 来源：从 C 的 MQTT 测试工程移植（E:\桌面\STM32F103ZET6_MQTTceshi）
     *       实测在巴法云上能正常订阅
     *
     * MQTT SUBSCRIBE 报文结构：
     *   固定头：0x82 + 剩余长度
     *           高4位 1000 = SUBSCRIBE（必须是 0x82，低4位固定 0010）
     *   可变头：报文标识符（2 字节，大端）
     *   载荷：  每个 Topic 三部分
     *             Topic 长度（2 字节大端）
     *             Topic 内容
     *             QoS（1 字节）
     *
     * ★ 为什么用 static 数组 ★
     *   512 字节放栈上会吃掉一大块 RAM（本工程只有 64KB），
     *   而且这个函数不会重入（裸机单线程），用 static 放 .bss 最省。
     *
     * ★ 限制：剩余长度只支持单字节（报文 < 128 字节）★
     *   当前 2 个短 Topic 够用。如果以后 Topic 变长要改多字节编码。
     *================================================================*/
    static u8 pack[MQTT_PACKAGE_SIZE];
    u16 pos = 0;
    u16 remaining = 2; /* 报文标识符占 2 字节 */
    u16 tlen;
    int i;

    if (num <= 0)
        return;

    /* 算总长度：每个 Topic = 2(长度) + 内容 + 1(QoS) */
    for (i = 0; i < num; i++)
    {
        remaining += 2 + strlen(topics[i]) + 1;
    }

    /* 本实现只处理单字节剩余长度 */
    if (remaining > 127)
        return;
    if ((2 + remaining) > MQTT_PACKAGE_SIZE)
        return;

    /* 固定头 */
    pack[pos++] = 0x82;
    pack[pos++] = (u8)remaining;

    /* 报文标识符（随便给一个，SUBACK 会原样返回） */
    pack[pos++] = 0x00;
    pack[pos++] = 0x0B; /* = 11 */

    /* 载荷：逐个 Topic */
    for (i = 0; i < num; i++)
    {
        tlen = strlen(topics[i]);
        pack[pos++] = tlen >> 8;     /* 长度高字节 */
        pack[pos++] = tlen & 0x00FF; /* 长度低字节 */
        memcpy(pack + pos, topics[i], tlen);
        pos += tlen;
        pack[pos++] = (u8)qos;
    }

    Usart3_SendBuffer(pack, pos);
}

/*=====================================================================
 * 函数：MQTT_PublishQs0
 * 负责：C（从 36 号工程移植）
 *=====================================================================*/
void MQTT_PublishQs0(char *topic, char *data, int len)
{
    /*=================================================================
     * 功能：组装并发送 PUBLISH 报文（QoS 0，最多一次）
     *
     * 来源：从 C 的 MQTT 测试工程移植
     *
     * MQTT PUBLISH 报文结构（QoS0 没有报文标识符）：
     *   固定头：0x30 + 剩余长度
     *           高4位 0011 = PUBLISH，低4位 0000 = QoS0
     *   可变头：Topic 长度（2 字节大端）+ Topic 内容
     *   载荷：  实际数据（原样塞进去）
     *
     * ★ QoS0 的特点 ★
     *   发出去就不管了，服务器不回 PUBACK。
     *   适合：传感器数据上报、心跳、版本上报
     *   不适合：OTA 命令下发（丢了就没了）
     *================================================================*/
    static u8 pack[MQTT_PACKAGE_SIZE];
    u16 pos = 0;
    u16 topic_len = (u16)strlen(topic);
    u16 remaining = 2 + topic_len + (u16)len;

    /* 本实现只处理单字节剩余长度 */
    if (remaining > 127)
        return;
    if ((2 + remaining) > MQTT_PACKAGE_SIZE)
        return;

    /* 固定头 */
    pack[pos++] = 0x30;
    pack[pos++] = (u8)remaining;

    /* 可变头：Topic */
    pack[pos++] = (u8)(topic_len >> 8);
    pack[pos++] = (u8)(topic_len & 0x00FF);
    memcpy(pack + pos, topic, topic_len);
    pos += topic_len;

    /* 载荷 */
    memcpy(pack + pos, data, len);
    pos += len;

    Usart3_SendBuffer(pack, pos);
}

/*=====================================================================
 * 函数：MQTT_PublishQs1
 * 负责：C（从 36 号工程移植）
 *=====================================================================*/
void MQTT_PublishQs1(char *topic, char *data, int len)
{
    /*=================================================================
     * 功能：组装并发送 PUBLISH 报文（QoS 1，至少一次）
     *
     * ★ 待实现 ★
     *
     * C 的测试工程里这个函数也是空的（只在 #if 0 里有个空壳），
     * 所以没有实测过的代码可以搬。
     *
     * 实现时的参考（MQTT 3.1.1 协议）：
     *   固定头：0x32 + 剩余长度
     *           高4位 0011 = PUBLISH，低4位 0010 = QoS1
     *   可变头：Topic 长度（2 字节）+ Topic + 报文标识符（2 字节）
     *   载荷：  实际数据
     *
     * QoS1 和 QoS0 的区别就是多了 2 字节报文标识符，
     * 固定头从 0x30 变成 0x32。
     * 可以参考 MQTT_PublishQs0 的实现来写。
     *
     * ★ 当前项目用 QoS0 就够了 ★
     *   巴法云下发 OTA 命令用 QoS0 也能收到，
     *   所以这个函数可以先空着。
     *================================================================*/
    /* TODO【C】：需要时参考 MQTT_PublishQs0 实现 */

    /* 临时屏蔽未使用参数警告 */
    (void)topic;
    (void)data;
    (void)len;
}

/*=====================================================================
 * 函数：MQTT_PingREQ
 * 负责：C（从 36 号工程移植）
 *=====================================================================*/
void MQTT_PingREQ(void)
{
    /*=================================================================
     * 功能：发送 PINGREQ 心跳包（2 字节，最简单的一条 MQTT 报文）
     *
     * 报文结构：
     *   0xC0  固定头（高4位 1100 = PINGREQ）
     *   0x00  剩余长度 = 0（没有可变头和载荷）
     *
     * ★ 为什么必须发 ★
     *   CONNECT 报文里声明了 KeepAlive = 100 秒。
     *   如果 100 秒内没有任何报文交互，服务器会认为客户端掉线并断开。
     *   所以要定期发 PINGREQ，服务器回 PINGRESP，保持连接活着。
     *
     * ★ 发送频率建议 ★
     *   KeepAlive 的一半，即 50 秒左右发一次。
     *================================================================*/
    static u8 pack[2] = {0xC0, 0x00};

    Usart3_SendBuffer(pack, 2);
}

/*=====================================================================
 * 函数：U2PassiveEvent
 * 负责：C
 *
 * 功能：处理 4G 收到的 MQTT 报文（被动事件）
 * 参数：data - 收到的数据；len - 长度
 *
 * 要处理 5 种报文（看首字节）：
 *
 *   0x20 CONNACK  连接确认
 *        data[3] == 0  → 连接成功，置 CONNECT_EVENT 标志
 *        data[3] != 0  → 失败，打印错误码
 *                        1=协议版本不对 2=ClientID被拒 4=用户名密码错 5=未授权
 *
 *   0x90 SUBACK   订阅确认
 *        逐个检查返回码，0x00=成功，0x80=失败
 *
 *   0x40 PUBACK   发布确认（QoS 1 才有）
 *        打印一下就行，不用特殊处理
 *
 *   0xD0 PINGRESP 心跳响应
 *        清 PING_SENT 标志，说明连接正常
 *
 *   0x30 PUBLISH  ★核心★ 收到消息
 *        处理流程：
 *          ① 调 MQTT_DealPushdata_Qs0() 把载荷提取到 cmdbuff
 *          ② 判断 Topic 是不是 "ota{uid}"
 *          ③ 是的话调 CAT1_ParseOTA(cmdbuff) 解析 URL/大小/MD5
 *          ④ 解析成功 → SysEventFlag |= OTA_EVENT
 *          ⑤ 主循环的 ActiveEvent() 看到这个标志就触发下载
 *
 * ★ 巴法云和阿里云的区别 ★
 *   阿里云 OTA 是平台推固定的 JSON 格式
 *   巴法云是你在电脑/手机上往 Topic 发一条消息，格式自己定
 *====================================================================*/
void U2PassiveEvent(u8 *data, u16 len)
{
    /* TODO【C】：实现 */
    if(data[0] == 0x20)
    {
        if(data[3] == 0)
        {
            SysEventFlag |= CONNECT_EVENT;
            Usart1_SendString("连接成功\r\n");
        }
        else
        {
            switch (data[3])
            {
            case 1:
                Usart1_SendString("协议版本不对\r\n");
                break;
            case 2:
                Usart1_SendString("ClientID被拒\r\n");
                break;
            case 4:
                Usart1_SendString("用户名密码错\r\n");
                break;
            case 5:
                Usart1_SendString("未授权\r\n");
                break;
            default:
                Usart1_SendString("未知错误\r\n");
                break;
            }
        }
    }
    else if(data[0] == 0x90)
    {
        u8 i = 0;
        for(i;i<len;i++)
        {
            if(data[i] != 0x00)
            {
                Usart1_SendString("订阅失败\r\n");
                break;
            }
        }
        if(i == len)
        {
            Usart1_SendString("订阅成功\r\n");
        }
    }
    /// 发布确认 (QoS 1 才有)，咱订阅主题全部使用Qos0，对我们没用，不处理
    else if(data[0] == 0x40)
    {
        ;
    }
    else if(data[0] == 0xD0)
    {
        SysEventFlag &= PING_SENT;
    }
    else if(data[0] == 0x30)
    {
        MQTT_DealPushdata_Qs0(data, len);
        if(strcmp(Bemfa_mqtt.cmdbuff, "123") == 0)
        {
            CAT1_ParseOTA(Bemfa_mqtt.cmdbuff);
            SysEventFlag |= OTA_EVENT;
        }
    }
}

/*=====================================================================
 * 函数：CAT1_PropertyVersion
 * 负责：C
 *
 * 功能：上报固件版本号
 *
 * ★ 巴法云简化版 ★
 *   阿里云要发到固定 Topic：/ota/device/inform/{pk}/{dn}
 *   而且必须是 JSON：{"id":1,"params":{"version":"app_2023112855511"}}
 *   版本号还必须正好 23 个字符
 *
 *   巴法云只要往自己的 Topic 发个字符串就行：
 *     MQTT_PublishQs0("stm32{uid}", "ver:app_2023112855511", len);
 *
 * 实现建议：
 *   1. sprintf(buf, "ver:%s", BemfaInfoCB.Version_ABuff);
 *   2. MQTT_PublishQs0(Bemfa_mqtt.Stopic_Buff[1], buf, strlen(buf));
 *====================================================================*/
void CAT1_PropertyVersion(void)
{
    /* TODO【C】：实现 */
}

/*=====================================================================
 * 函数：CAT1_PropertyPost
 * 负责：C
 *
 * 功能：上报属性/数据到云端
 * 参数：postdata - 要上报的字符串内容
 *
 * ★ 巴法云简化版 ★
 *   阿里云要求严格 JSON：{"params":{"powerstate":1}}
 *   巴法云发什么内容都行，格式自己约定
 *
 * 实现建议：
 *   MQTT_PublishQs0(Bemfa_mqtt.Stopic_Buff[1], postdata, strlen(postdata));
 *
 * 用途举例：
 *   上报当前固件版本、上报下载进度、上报心跳状态等
 *====================================================================*/
void CAT1_PropertyPost(char *postdata)
{
    /* TODO【C】：实现 */
}

/*=====================================================================
 * 函数：MQTT_DealPushdata_Qs0
 * 负责：C（从 36 号工程移植）
 *
 * 功能：从 PUBLISH 报文里提取出 JSON 载荷，存到 Bemfa_mqtt.cmdbuff
 *=====================================================================*/
void MQTT_DealPushdata_Qs0(u8 *data, u16 len)
{
    /* TODO【C】：从 36 号工程移植 */
    u16 topiclen = data[2] << 8 | data[3];
    memcpy(Bemfa_mqtt.cmdbuff, data + 4 + topiclen, len - 4 - topiclen);
}  