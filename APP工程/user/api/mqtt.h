/*=====================================================================
 * 文件：mqtt.h
 * 说明：MQTT 协议实现（巴法云 bemfa）
 * 负责：C
 *
 *=====================================================================
 * ★ 平台变更说明（2026-10-27）★
 *
 * 从阿里云 IoT 改为巴法云（bemfa），带来三个巨大简化：
 *
 *   ① 不需要 HMAC-SHA1 签名
 *      阿里云：Password = HMAC-SHA1(clientId + deviceName + productKey, deviceSecret)
 *      巴法云：Password = UID 本身
 *      → utils_sha1.c / utils_hmac.c 两个文件全部不需要移植
 *
 *   ② 不需要三元组
 *      阿里云：ProductKey(11位) + DeviceName + DeviceSecret(32位)
 *      巴法云：只要 UID(32位)
 *
 *   ③ Topic 自己定义
 *      阿里云：平台自动生成 /sys/{pk}/{dn}/thing/event/property/post
 *      巴法云：自己定，如 ota{uid}、stm32{uid}
 *
 *=====================================================================
 * ★ 巴法云 MQTT 连接参数 ★
 *
 *   服务器地址：bemfa.com
 *   端口：      9501（TCP 明文）/ 9503（TLS 加密）
 *   ClientID：  任意唯一字符串（建议带设备标识，避免多设备冲突）
 *   Username：  UID（32位私钥）
 *   Password：  UID（和 Username 一样）
 *   KeepAlive： 60 秒（必须小于巴法云的超时时间）
 *
 * ★★★ 注意 ★★★
 *   上面这些参数是凭经验写的，实际用之前【必须】去 bemfa.com 官网
 *   的「文档 → MQTT 接入」确认。参数不对会连不上。
 *
 *=====================================================================
 * ★ 移植来源 ★
 *
 *   MQTT 协议栈从 36 号工程移植（338 行）：
 *   E:\BaiduNetdiskDownload\新建文件夹\36-MQTT协议...\HARDWARE\Scr\mqtt.c
 *
 *   移植时只需改 3 个宏（在 mqtt.c 顶部）：
 *     #define CAT1_printf    Usart3_SendString
 *     #define CAT1_RX_BUF    usart3_value.data
 *     #define CAT1_RxCounter usart3_value.count
 *====================================================================*/
#ifndef __MQTT_H
#define __MQTT_H

#include "common.h"

/*==================== 缓冲区尺寸 ====================*/
#define TOPIC_NUM          3               /* 最多订阅几个 Topic */
#define TOPIC_SIZE         128             /* 单个 Topic 最大长度 */
#define MQTT_PACKAGE_SIZE  512             /* MQTT 报文缓冲区大小 */

/*=====================================================================
 * MQTT 会话结构体
 *
 * 说明：
 *   这个结构体保存一次 MQTT 会话需要的全部参数和缓冲区。
 *   全局只有一个实例（Bemfa_mqtt），在 mqtt.c 里定义。
 *===================================================================*/
typedef struct{
    /*---------- 连接参数（MQTT_Init 里填）----------*/
    char ClientID[128];                     /* 任意唯一字符串，如 "stm32_001" */
    char Username[128];                     /* 巴法云 UID（32位私钥） */
    char Passward[128];                     /* 巴法云 UID（和 Username 一样） */
    char ServerIP[128];                     /* "bemfa.com" */
    int  ServerPort;                        /* 9501（TCP）/ 9503（TLS） */

    /*---------- 报文缓冲区 ----------*/
    u8   Pack_buff[MQTT_PACKAGE_SIZE];      /* 组装 MQTT 报文用 */
    int  MessageID;                         /* 报文 ID，每条 QoS>0 的消息递增 */
    int  Fixed_len;                         /* 固定头长度 */
    int  Variable_len;                      /* 可变头长度 */
    int  Payload_len;                       /* 载荷长度 */

    /*---------- 订阅的 Topic 列表 ----------*/
    char Stopic_Buff[TOPIC_NUM][TOPIC_SIZE];

    /*---------- 收到的命令/数据 ----------*/
    char cmdbuff[3000];                     /* PUBLISH 报文里提取出的载荷 */
} MQTT_CB;

extern MQTT_CB Bemfa_mqtt;

/*==================== 函数声明 ====================*/

/*---------------------------------------------------------------------
 * 函数：MQTT_Init
 * 功能：初始化 MQTT 参数（ClientID / Username / Password / Topic）
 * 参数：无
 * 返回：无
 * 负责：C
 *
 * ★ 实现要点（比阿里云简单得多）★
 *   1. ClientID = 任意唯一字符串
 *        例：sprintf(Bemfa_mqtt.ClientID, "stm32_%s", BemfaInfoCB.Version_ABuff);
 *   2. Username = UID
 *        例：sprintf(Bemfa_mqtt.Username, "%s", BemfaInfoCB.UID_Buff);
 *   3. Password = UID（和 Username 一样）
 *        例：sprintf(Bemfa_mqtt.Passward, "%s", BemfaInfoCB.UID_Buff);
 *   4. ServerIP = "bemfa.com"
 *        例：sprintf(Bemfa_mqtt.ServerIP, "bemfa.com");
 *   5. ServerPort = 9501
 *   6. 构建订阅 Topic：
 *        Topic[0] = "ota{uid}"    ← ★OTA 命令下发，核心★
 *        Topic[1] = "stm32{uid}"  ← 通用消息（调试用）
 *
 * ★ 对比阿里云 ★
 *   阿里云要算 HMAC-SHA1，还要拼 4 段字符串，容易出错。
 *   巴法云就是 3 个 strcpy，不会错。
 *-------------------------------------------------------------------*/
void MQTT_Init(void);

/*---------------------------------------------------------------------
 * 函数：MQTT_ConnectBemfa
 * 功能：连接巴法云服务器
 * 参数：无
 * 返回：0=发送成功（不代表连上了，要等 CONNACK）
 * 负责：C
 *
 * 实现要点：
 *   1. 用 AT+CIPSTART 建立 TCP 连接到 bemfa.com:9501
 *   2. 等 CONNECT OK
 *   3. 调 MQTT_ConectPack() 构建 CONNECT 报文
 *   4. 通过 AT+CIPSEND 发出
 *   5. 等模块回 SEND OK
 *
 * ★ 注意：函数返回 0 只表示"报文发出去了"
 *   真正的连接成功要看 U2PassiveEvent 收到的 CONNACK（0x20）
 *-------------------------------------------------------------------*/
u8 MQTT_ConnectBemfa(void);

/*---------------------------------------------------------------------
 * 函数：MQTT_ConectPack
 * 功能：构建 CONNECT 报文
 * 参数：无
 * 返回：无
 * 负责：C（从 36 号工程移植）
 *
 * MQTT CONNECT 报文结构：
 *   固定头：0x10 + 剩余长度
 *   可变头：协议名 "MQTT" + 协议级别 4 + 连接标志 + KeepAlive 60
 *   载荷：ClientID + Username + Password
 *-------------------------------------------------------------------*/
void MQTT_ConectPack(void);

/*---------------------------------------------------------------------
 * 函数：MQTT_Subscribe
 * 功能：构建 SUBSCRIBE 报文
 * 参数：topics - Topic 数组；num - 个数；qos - 服务质量
 * 返回：无
 * 负责：C（从 36 号工程移植）
 *
 * ★ 巴法云订阅注意 ★
 *   Topic 名字要加 UID 后缀，如 "ota{uid}"
 *   具体格式去官网确认
 *-------------------------------------------------------------------*/
void MQTT_Subscribe(char topics[][TOPIC_SIZE], int num, int qos);

/*---------------------------------------------------------------------
 * 函数：MQTT_PublishQs0
 * 功能：发布 QoS 0（最多一次，不保证送达）
 * 参数：topic - 主题；data - 数据；len - 长度
 * 返回：无
 * 负责：C（从 36 号工程移植）
 *-------------------------------------------------------------------*/
void MQTT_PublishQs0(char *topic, char *data, int len);

/*---------------------------------------------------------------------
 * 函数：MQTT_PublishQs1
 * 功能：发布 QoS 1（至少一次，有 PUBACK 确认）
 * 参数：topic - 主题；data - 数据；len - 长度
 * 返回：无
 * 负责：C（从 36 号工程移植）
 *-------------------------------------------------------------------*/
void MQTT_PublishQs1(char *topic, char *data, int len);

/*---------------------------------------------------------------------
 * 函数：MQTT_PingREQ
 * 功能：发 PING 保活包
 * 参数：无
 * 返回：无
 * 负责：C（从 36 号工程移植）
 *
 * 说明：
 *   每 30~60 秒发一次，不发会被服务器踢下线。
 *   发送后置 PING_SENT 标志，收到 PINGRESP 清标志。
 *   连续两次没收到 PINGRESP 说明掉线了，要重连。
 *-------------------------------------------------------------------*/
void MQTT_PingREQ(void);

/*---------------------------------------------------------------------
 * 函数：U2PassiveEvent
 * 功能：处理 4G 收到的 MQTT 报文（被动事件）
 * 参数：data - 数据；len - 长度
 * 返回：无
 * 负责：C
 *
 * 要处理 5 种报文（看首字节）：
 *   0x20 CONNACK   → 连接确认，data[3]==0 表示成功
 *   0x90 SUBACK    → 订阅确认，检查每个 Topic 的结果
 *   0x40 PUBACK    → 发布确认（QoS 1 才回）
 *   0xD0 PINGRESP  → 心跳响应，清 PING 标志
 *   0x30 PUBLISH   → ★核心★ 收到消息（可能是 OTA 命令）
 *
 * ★ PUBLISH 处理流程 ★
 *   1. 调 MQTT_DealPushdata_Qs0() 提取载荷到 cmdbuff
 *   2. 如果 Topic 是 "ota{uid}" → 调 CAT1_ParseOTA() 解析
 *   3. 解析成功 → 置 OTA_EVENT 标志 → 主循环触发下载
 *-------------------------------------------------------------------*/
void U2PassiveEvent(u8 *data, u16 len);

/*---------------------------------------------------------------------
 * 函数：CAT1_PropertyVersion
 * 功能：上报固件版本号
 * 参数：无
 * 返回：无
 * 负责：C
 *
 * ★ 巴法云简化版 ★
 *   阿里云要发到固定 Topic /ota/device/inform/{pk}/{dn}，还要 JSON 格式。
 *   巴法云只要往自己的 Topic 发个版本字符串就行。
 *
 * 实现建议：
 *   1. 往 "stm32{uid}" 发 BemfaInfoCB.Version_ABuff
 *   2. 格式自定，比如 "ver:app_2023112855511"
 *-------------------------------------------------------------------*/
void CAT1_PropertyVersion(void);

/*---------------------------------------------------------------------
 * 函数：CAT1_PropertyPost
 * 功能：上报属性/数据
 * 参数：postdata - 要上报的内容（字符串）
 * 返回：无
 * 负责：C
 *
 * ★ 巴法云简化版 ★
 *   阿里云：{"params":{"powerstate":1}} 这种 JSON 格式
 *   巴法云：发什么内容都行，看你自己约定
 *-------------------------------------------------------------------*/
void CAT1_PropertyPost(char *postdata);

/*---------------------------------------------------------------------
 * 函数：MQTT_DealPushdata_Qs0
 * 功能：从 PUBLISH 报文里提取载荷到 cmdbuff
 * 参数：data - 数据；len - 长度
 * 返回：无
 * 负责：C（从 36 号工程移植）
 *
 * PUBLISH 报文结构：
 *   固定头：0x30 + 剩余长度
 *   可变头：Topic 长度(2字节) + Topic + 报文ID(2字节，QoS>0才有)
 *   载荷：实际数据（就是 cmdbuff 要装的）
 *
 * ★ 提取方法 ★
 *   1. 跳过固定头（1 字节类型 + 1~4 字节剩余长度）
 *   2. 读 Topic 长度（2 字节大端）
 *   3. 跳过 Topic
 *   4. 如果是 QoS>0，再跳过 2 字节报文 ID
 *   5. 剩下的就是载荷，memcpy 到 cmdbuff
 *-------------------------------------------------------------------*/
void MQTT_DealPushdata_Qs0(u8 *data, u16 len);

#endif /* __MQTT_H */
