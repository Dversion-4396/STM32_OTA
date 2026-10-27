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
    /* TODO【C】：实现 */
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
    /* TODO【C】：实现 */
    return 1;
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
    /* TODO【C】：从 36 号工程移植 */
}

/*=====================================================================
 * 函数：MQTT_Subscribe
 * 负责：C（从 36 号工程移植）
 *=====================================================================*/
void MQTT_Subscribe(char topics[][TOPIC_SIZE], int num, int qos)
{
    /* TODO【C】：从 36 号工程移植 */
}

/*=====================================================================
 * 函数：MQTT_PublishQs0
 * 负责：C（从 36 号工程移植）
 *=====================================================================*/
void MQTT_PublishQs0(char *topic, char *data, int len)
{
    /* TODO【C】：从 36 号工程移植 */
}

/*=====================================================================
 * 函数：MQTT_PublishQs1
 * 负责：C（从 36 号工程移植）
 *=====================================================================*/
void MQTT_PublishQs1(char *topic, char *data, int len)
{
    /* TODO【C】：从 36 号工程移植 */
}

/*=====================================================================
 * 函数：MQTT_PingREQ
 * 负责：C（从 36 号工程移植）
 *=====================================================================*/
void MQTT_PingREQ(void)
{
    /* TODO【C】：从 36 号工程移植 */
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
}
