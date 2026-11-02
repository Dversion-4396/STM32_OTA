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
    sprintf(Bemfa_mqtt.ClientID, USERNAME_KEY);
    sprintf(Bemfa_mqtt.Username, USERNAME_KEY);
    sprintf(Bemfa_mqtt.Passward, USERNAME_KEY);

    sprintf(Bemfa_mqtt.ServerIP, SERVER_IP);
    Bemfa_mqtt.ServerPort = SERVER_PORT;

    /*---------- 订阅 Topic ----------
     * 巴法云控制台里创建的主题，直接用名字，不需要拼 UID 后缀。
     *
     *=================================================================
     * ★ 为什么只订阅 1 个（2026-11-02 修正）★
     *
     *   之前订阅了 2 个（"123" 和 "abc"），是 C 在 WiFi 测试工程里
     *   验证"多主题订阅"时留下的。
     *
     *   但巴法云规则：主题必须先创建才能订阅。
     *   控制台里只建了 "123"，没建 "abc"，
     *   所以 SUBACK 返回：
     *     90 04 00 0B 00 80
     *              └┬┘ └┬┘
     *               │   └─ "abc" = 0x80（失败）
     *               └───── "123" = 0x00（成功）
     *
     *   OTA 只需要一个 Topic 收升级命令，订阅 1 个就够了。
     *
     *=================================================================
     * ★ 正式版应该怎么改 ★
     *
     *   现在用的是固定 Topic "123"（测试用）。
     *   正式版应该按 OTA 需求拼成 "ota{uid}"，
     *   这样不同设备有自己的 Topic，互不干扰：
     *
     *     sprintf(Bemfa_mqtt.Stopic_Buff[0], "ota%s", BemfaInfoCB.UID_Buff);
     *
     *   但要注意：BemfaInfoCB.UID_Buff 需要先从 EEPROM 读出来，
     *   且 valid_flag == 0xDD 才有效。
     *----------------------------------------------------------------*/
    strcpy(Bemfa_mqtt.Stopic_Buff[0], TOPIC_1);      /* "123" */
    strcpy(Bemfa_mqtt.Stopic_Buff[1], "");
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

    u8 ret;
    char cmd[128];

    /*---------- ① 配置 APN ----------
     * 手册格式：AT+QICSGP=1,1,"","",""
     * 三个空字符串 = APN/用户名/密码，留空用 SIM 卡默认
     *------------------------------------------------*/
    Usart1_SendString("[MQTT] 1.配置APN\r\n");
    ret = CAT1_SendAT("AT+QICSGP=1,1,\"\",\"\",\"\"", "OK", 10000);
    if (ret != 0)
    {
        Usart1_SendString("[MQTT] APN配置失败\r\n");
        Usart1_SendString("[MQTT] 响应: [");
        Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
        Usart1_SendString("]\r\n");
        return 1;
    }

    Usart1_SendString("[MQTT] 2.开启移动网络\r\n");

    usart3_value.flag = 0;
    usart3_value.count = 0;

    Usart3_SendString("AT+NETOPEN\r\n");
    delay_ms(3000);

    /* 打印响应，但不判断成败（902 = 已激活，也是好事） */
    Usart1_SendString("[MQTT] NETOPEN 响应: [");
    Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
    Usart1_SendString("]\r\n");

    /*---------- ③ 建立 TCP 连接 ----------
     * 手册：C. 建立连接会话：AT+CIPOPEN=0,"TCP","ip",port
     * 成功响应：+CIPOPEN: SUCCESS,0
     *
     *=================================================================
     * ★ 为什么失败也要继续（2026-11-02 实测）★
     *
     *   实测现象：
     *     [MQTT] 3.建立TCP连接
     *     [MQTT] TCP连接失败
     *     响应: [OK +CIPOPEN: FAIL,0]
     *     ...但后面能正常收到 PUBLISH 数据！
     *
     *   原因：
     *     上次 HTTP 下载结束后，TCP 会话 0 没有完全释放。
     *     再发 AT+CIPOPEN=0 会因为"会话已存在"返回 FAIL,0。
     *     但旧会话本身是活的，数据能收发。
     *
     *   所以 FAIL 有两种含义：
     *     ① 真的连不上（网络问题）
     *     ② 会话已存在（旧连接还活着）
     *
     *   处理策略：
     *     FAIL 时不立即报错，而是先 AT+CIPCLOSE=0 关掉旧的，
     *     再重试一次。如果还是 FAIL，才认定真失败。
     *================================================================*/
    Usart1_SendString("[MQTT] 3.建立TCP连接\r\n");
    sprintf(cmd, "AT+CIPOPEN=0,\"TCP\",\"%s\",%d",
            Bemfa_mqtt.ServerIP, Bemfa_mqtt.ServerPort);

    ret = CAT1_SendAT(cmd, "CIPOPEN: SUCCESS", 20000);
    if (ret != 0)
    {
        Usart1_SendString("[MQTT] 首次建连返回非 SUCCESS，检查是否残留会话\r\n");
        Usart1_SendString("[MQTT] 响应: [");
        Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
        Usart1_SendString("]\r\n");

        /* 先关掉可能残留的会话 */
        Usart1_SendString("[MQTT] 关闭残留会话 0...\r\n");
        CAT1_SendAT("AT+CIPCLOSE=0", "OK", 5000);
        delay_ms(1000);

        /* 重试一次 */
        Usart1_SendString("[MQTT] 重试建立 TCP 连接...\r\n");
        ret = CAT1_SendAT(cmd, "CIPOPEN: SUCCESS", 20000);
        if (ret != 0)
        {
            /* 第二次还是失败，可能是"会话已存在"但能通，
             * 不直接放弃，继续往下走发 CONNECT 报文试试 */
            Usart1_SendString("[MQTT] ★重试仍失败，继续尝试发 MQTT 报文\r\n");
            Usart1_SendString("[MQTT] 响应: [");
            Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
            Usart1_SendString("]\r\n");
            /* ★ 不 return，继续往下走 ★ */
        }
        else
        {
            Usart1_SendString("[MQTT] TCP已连接（重试成功）\r\n");
        }
    }
    else
    {
        Usart1_SendString("[MQTT] TCP已连接\r\n");
    }

    /*---------- ④ 发 CONNECT 报文（用 CIPSEND 包裹）----------*/
    Usart1_SendString("[MQTT] 4.发送CONNECT报文\r\n");
    MQTT_ConectPack(); /* 只组包，不发 */
    ret = CAT1_SendMQTTPacket(Bemfa_mqtt.Pack_buff,
                              Bemfa_mqtt.Fixed_len + Bemfa_mqtt.Variable_len + Bemfa_mqtt.Payload_len);
    if (ret != 0)
    {
        Usart1_SendString("[MQTT] CONNECT报文发送失败\r\n");
        return 4;
    }

    /* 等 CONNACK（U2PassiveEvent 会处理，这里只等一会儿） */
    delay_ms(2000);

    /*---------- ⑤ 订阅 Topic ----------
     * ★ 只订阅 1 个 ★
     *   OTA 只需要一个 Topic 收升级命令。
     *   之前订阅 2 个（"123" + "abc"），但控制台只建了 "123"，
     *   导致 SUBACK 里 "abc" 返回 0x80（失败），容易被误判。
     *------------------------------------------------*/
    Usart1_SendString("[MQTT] 5.订阅主题\r\n");
    MQTT_Subscribe(Bemfa_mqtt.Stopic_Buff, 1, 0); /* 只订阅 1 个 */

    delay_ms(1000);

    Usart1_SendString("[MQTT] 连接流程完成\r\n");
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
    // Usart3_SendBuffer(Bemfa_mqtt.Pack_buff, Bemfa_mqtt.Fixed_len + Bemfa_mqtt.Variable_len + Bemfa_mqtt.Payload_len);
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

    CAT1_SendMQTTPacket(pack, pos);
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

    CAT1_SendMQTTPacket(pack, pos);
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

    CAT1_SendMQTTPacket(pack, 2);
}

void U2PassiveEvent(u8 *data, u16 len)
{
    u8 type;

    /*---------- 参数检查 ----------*/
    if (data == NULL || len < 2)
    {
        return;
    }

    /* 取报文类型（第一个字节的高 4 位） */
    type = data[0] & 0xF0;

    switch (type)
    {
    /*=================================================================
     * CONNACK：连接确认
     *
     * 报文结构（4 字节）：
     *   0x20      固定头
     *   0x02      剩余长度
     *   0x00      连接标志
     *   XX        返回码（0 = 成功）
     *================================================================*/
    case 0x20:
        if (len >= 4)
        {
            if (data[3] == 0x00)
            {
                Usart1_SendString("[MQTT] ★连接成功★\r\n");
                SysEventFlag |= CONNECT_EVENT;
            }
            else
            {
                Usart1_SendString("[MQTT] ★连接被拒，返回码 = ");
                {
                    char num[4];
                    sprintf(num, "%u", data[3]);
                    Usart1_SendString(num);
                }
                Usart1_SendString("\r\n");

                /* 返回码含义：
                 *   1 = 协议版本不支持
                 *   2 = ClientID 被拒
                 *   4 = 用户名密码错
                 *   5 = 未授权（最常见） */
            }
        }
        break;

    /*=================================================================
     * PUBLISH：收到服务器发来的消息
     *
     * ★ 这里是 OTA 的入口 ★
     *================================================================*/
    case 0x30:
        Usart1_SendString("[MQTT] 收到 PUBLISH\r\n");

        /* 提取载荷到 Bemfa_mqtt.cmdbuff */
        MQTT_DealPushdata_Qs0(data, len);

        /* 有载荷才处理 */
        if (strlen(Bemfa_mqtt.cmdbuff) > 0)
        {
            Usart1_SendString("[MQTT] 载荷: ");
            Usart1_SendString(Bemfa_mqtt.cmdbuff);
            Usart1_SendString("\r\n");

            /*---------------------------------------------------------
             * 尝试按 OTA 命令格式解析
             *
             * 格式：URL,大小,MD5
             *   例：http://xxx.com/fw.bin,31236,abc123
             *
             * ★ 用返回值判断，不用 strcmp 比对固定字符串 ★
             *   返回 0 = 解析成功（是 OTA 命令）
             *   返回 1/2 = 格式不对（不是 OTA 命令，忽略）
             *--------------------------------------------------------*/
            if (CAT1_ParseOTA(Bemfa_mqtt.cmdbuff) == 0)
            {
                Usart1_SendString("[MQTT] ★这是 OTA 命令★\r\n");
                SysEventFlag |= OTA_EVENT;
            }
            else
            {
                Usart1_SendString("[MQTT] 不是 OTA 命令，忽略\r\n");
            }
        }
        break;

    /*=================================================================
     * SUBACK：订阅确认
     *
     * 报文结构：
     *   0x90          固定头
     *   <len>         剩余长度
     *   <msgid_hi>    报文标识符高字节
     *   <msgid_lo>    报文标识符低字节
     *   <return_code> 订阅返回码
     *
     * 返回码：0x00/0x01/0x02 = 成功，0x80 = 失败
     *
     * ★ 只看最后一个字节，不要遍历整个报文 ★
     *================================================================*/
    case 0x90:
        /*=================================================================
         * SUBACK：订阅确认
         *
         * 报文结构：
         *   0x90  <len>  <msgid_hi> <msgid_lo>  <code1> [<code2> ...]
         *
         * 实测（订阅 "123" 和 "abc" 两个 Topic）：
         *   90 04 00 0B 00 80
         *              └┬┘ └┬┘
         *               │   └─ "abc" 的返回码 = 0x80（失败）
         *               └───── "123" 的返回码 = 0x00（成功）
         *
         * ★ 必须逐个检查，不能只看最后一个 ★
         *   之前的 bug：只看 data[len-1]，
         *   结果 "123" 成功、"abc" 失败 → 误判成整体失败
         *================================================================*/
        if (len < 5)
        {
            Usart1_SendString("[MQTT] SUBACK 格式错误\r\n");
        }
        else
        {
            u16 k;
            u8  ok_count  = 0;
            u8  fail_count = 0;

            /* 返回码从第 4 字节开始（跳过 0x90、len、msgid 2 字节）*/
            for (k = 4; k < len; k++)
            {
                if (data[k] == 0x80)
                {
                    fail_count++;
                }
                else
                {
                    ok_count++;
                }
            }

            Usart1_SendString("[MQTT] 订阅结果: 成功 ");
            {
                char num[4];
                sprintf(num, "%u", ok_count);
                Usart1_SendString(num);
            }
            Usart1_SendString(" 个，失败 ");
            {
                char num[4];
                sprintf(num, "%u", fail_count);
                Usart1_SendString(num);
            }
            Usart1_SendString(" 个\r\n");

            /* ★ 只要有一个成功，就认为连接可用 ★
             *   因为我们只需要一个 Topic 收 OTA 命令 */
            if (ok_count > 0)
            {
                Usart1_SendString("[MQTT] ★订阅成功\r\n");
                SysEventFlag |= CONNECT_EVENT;
            }
            else
            {
                Usart1_SendString("[MQTT] ★订阅全部失败（服务器拒绝）\r\n");
            }
        }
        break;

    /*=================================================================
     * PUBACK：发布确认（QoS1 才有）
     *
     * 我们全用 QoS0，服务器不会发这个
     *================================================================*/
    case 0x40:
        break;

    /*=================================================================
     * PINGRESP：心跳响应
     *
     * ★ 必须用 &= ~，不能用 &= ★
     *   &= PING_SENT 会清掉其他所有标志位
     *================================================================*/
    case 0xD0:
        SysEventFlag &= ~PING_SENT;
        Usart1_SendString("[MQTT] PINGRESP\r\n");
        break;

    /*=================================================================
     * 其他：不认识的报文
     *================================================================*/
    default:
        Usart1_SendString("[MQTT] 未知报文类型: 0x");
        {
            char hex[4];
            sprintf(hex, "%02X", data[0]);
            Usart1_SendString(hex);
        }
        Usart1_SendString("\r\n");
        break;
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
 * 功能：从 PUBLISH 报文里提取载荷，存到 Bemfa_mqtt.cmdbuff
 * 参数：data - 原始报文（usart3_value.data）
 *       len  - 报文长度（usart3_value.count）
 * 负责：C
 *
 *=====================================================================
 * ★ PUBLISH 报文结构 ★
 *
 *   固定头：
 *     字节 0:   0x30          类型 PUBLISH
 *     字节 1~4: 剩余长度       变长编码（1~4 字节）
 *
 *   可变头：
 *     主题长度（2 字节，大端）
 *     主题内容
 *     [报文标识符 2 字节]      QoS > 0 才有
 *
 *   载荷：
 *     剩下所有字节
 *
 *=====================================================================
 * ★ 变长编码怎么读 ★
 *
 *   每个字节：
 *     最高位 = 1  →  后面还有字节，继续读
 *     最高位 = 0  →  这是最后一个字节，停止
 *
 *   本函数不需要算具体数值，只要知道"读到哪"，
 *   载荷长度直接用 len - pos 算。
 *====================================================================*/

void MQTT_DealPushdata_Qs0(u8 *data, u16 len)
{
    u16 pos = 0;
    u16 topic_len;
    u16 payload_len;
    u32 remaining_len = 0; /* ★新增：从报文里读出的剩余长度★ */
    u32 multiplier = 1;
    u8 qos;
    u8 byte;
    u16 packet_end; /* ★新增：报文结束位置★ */

    if (data == NULL || len < 2)
    {
        return;
    }

    memset(Bemfa_mqtt.cmdbuff, 0, sizeof(Bemfa_mqtt.cmdbuff));

    /*---------- ① 跳过固定头，同时算出剩余长度 ----------*/
    pos = 1; /* 跳过报文类型字节 */

    do
    {
        if (pos >= len)
            return;
        byte = data[pos];
        pos++;

        /* 累加剩余长度（变长编码） */
        remaining_len += (u32)(byte & 0x7F) * multiplier;
        multiplier *= 128;

    } while (byte & 0x80);

    /* ★ 报文总长度 = 固定头长度(pos) + 剩余长度 ★ */
    packet_end = (u16)(pos + remaining_len);

    /* 防止越界：如果算出来的报文结尾超过实际收到的长度，就用实际长度 */
    if (packet_end > len)
    {
        packet_end = len;
    }

    /*---------- ② 读主题长度（2 字节，大端）----------*/
    if (pos + 2 > packet_end)
        return;
    topic_len = ((u16)data[pos] << 8) | data[pos + 1];
    pos += 2;

    /*---------- ③ 跳过主题内容 ----------*/
    if (pos + topic_len > packet_end)
        return;
    pos += topic_len;

    /*---------- ④ QoS > 0 时跳过 2 字节报文标识符 ----------*/
    qos = (data[0] >> 1) & 0x03;
    if (qos > 0)
    {
        if (pos + 2 > packet_end)
            return;
        pos += 2;
    }

    /*---------- ⑤ 载荷长度 = 报文结尾 - 当前位置 ----------
     * ★ 这里用 packet_end，不是 len ★
     *   因为 len 可能包含后面的 \r\n
     *------------------------------------------------*/
    payload_len = packet_end - pos;
    if (payload_len == 0)
        return;

    if (payload_len >= sizeof(Bemfa_mqtt.cmdbuff))
    {
        payload_len = sizeof(Bemfa_mqtt.cmdbuff) - 1;
    }

    memcpy(Bemfa_mqtt.cmdbuff, &data[pos], payload_len);
    Bemfa_mqtt.cmdbuff[payload_len] = '\0';
}