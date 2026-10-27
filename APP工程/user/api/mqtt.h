/*=====================================================================
 * 文件：mqtt.h
 * 说明：MQTT 协议（阿里云）
 * 负责：C
 *
 * ★你现有的 mqtt.c 只有 3 个发送函数，不够用★
 *   需要移植 36 号工程的完整版（338 行）：
 *   E:\BaiduNetdiskDownload\新建文件夹\36-MQTT协议...\HARDWARE\Scr\mqtt.c
 *
 * 移植时只需改 3 个宏：
 *   #define CAT1_printf    Usart3_SendString
 *   #define CAT1_RX_BUF    usart3_value.data
 *   #define CAT1_RxCounter usart3_value.count
 *====================================================================*/
#ifndef __MQTT_H
#define __MQTT_H

#include "common.h"

#define TOPIC_NUM          3
#define TOPIC_SIZE         128
#define MQTT_PACKAGE_SIZE  512

/*==================== 阿里云属性标识符 ====================*/
/* 必须和阿里云后台"功能定义"里的标识符完全一致 */
#define ATTRIBUTE1      "powerstate"

/*==================== MQTT 结构体 ====================*/
typedef struct{
    char ClientID[128];                     /* test_001|securemode=3,signmethod=hmacsha1| */
    char Username[128];                     /* test_001&grfdYViGNwS */
    char Passward[128];                     /* HMAC-SHA1 算出来的 40 位 hex */
    char ServerIP[128];                     /* {pk}.iot-as-mqtt.cn-shanghai.aliyuncs.com */
    int  ServerPort;                        /* 1883 */

    u8   Pack_buff[MQTT_PACKAGE_SIZE];
    int  MessageID;
    int  Fixed_len;
    int  Variable_len;
    int  Payload_len;

    char Stopic_Buff[TOPIC_NUM][TOPIC_SIZE];
    char cmdbuff[3000];
} MQTT_CB;

extern MQTT_CB Aliyun_mqtt;

/*==================== 函数声明 ====================*/

/*---------------------------------------------------------------------
 * 函数：MQTT_Init
 * 功能：初始化 MQTT 参数（ClientID / Username / Password / Topic）
 * 参数：无
 * 返回：无
 * 负责：C
 *
 * 内部要做：
 *   1. ClientID = "{DeviceName}|securemode=3,signmethod=hmacsha1|"
 *   2. Username = "{DeviceName}&{ProductKey}"
 *   3. 拼明文 "clientId{dn}deviceName{dn}productKey{pk}"
 *   4. utils_hmac_sha1(明文, 长度, Password, DeviceSecret, 32)
 *   5. ServerIP = "{pk}.iot-as-mqtt.cn-shanghai.aliyuncs.com"
 *   6. 构建 3 个订阅 Topic
 *-------------------------------------------------------------------*/
void MQTT_Init(void);

/*---------------------------------------------------------------------
 * 函数：MQTT_ConnectAliyun
 * 功能：连接阿里云
 * 参数：无
 * 返回：0=发送成功
 * 负责：C
 *-------------------------------------------------------------------*/
u8 MQTT_ConnectAliyun(void);

/*---------------------------------------------------------------------
 * 函数：MQTT_ConectPack
 * 功能：构建 CONNECT 报文
 * 参数：无
 * 返回：无
 * 负责：C（从 36 号工程移植）
 *-------------------------------------------------------------------*/
void MQTT_ConectPack(void);

/*---------------------------------------------------------------------
 * 函数：MQTT_Subscribe
 * 功能：构建 SUBSCRIBE 报文
 * 参数：topics - Topic 数组；num - 个数；qos - 服务质量
 * 返回：无
 * 负责：C（从 36 号工程移植）
 *-------------------------------------------------------------------*/
void MQTT_Subscribe(char topics[][TOPIC_SIZE], int num, int qos);

/*---------------------------------------------------------------------
 * 函数：MQTT_PublishQs0
 * 功能：发布 QoS 0（最多一次）
 * 参数：topic - 主题；data - 数据；len - 长度
 * 返回：无
 * 负责：C（从 36 号工程移植）
 *-------------------------------------------------------------------*/
void MQTT_PublishQs0(char *topic, char *data, int len);

/*---------------------------------------------------------------------
 * 函数：MQTT_PublishQs1
 * 功能：发布 QoS 1（至少一次，有 PUBACK）
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
 * 说明：每 60 秒发一次，不发会掉线
 *-------------------------------------------------------------------*/
void MQTT_PingREQ(void);

/*---------------------------------------------------------------------
 * 函数：U2PassiveEvent
 * 功能：处理 4G 收到的 MQTT 报文
 * 参数：data - 数据；len - 长度
 * 返回：无
 * 负责：C
 *
 * 要处理 5 种报文：
 *   0x20 CONNACK  → data[3]==0 成功，否则打印错误码
 *   0x90 SUBACK   → 检查每个 Topic 的结果
 *   0x40 PUBACK   → 打印成功
 *   0xD0 PINGRESP → 清 PING 标志
 *   0x30 PUBLISH  → ★解析命令 / OTA 推送★
 *-------------------------------------------------------------------*/
void U2PassiveEvent(u8 *data, u16 len);

/*---------------------------------------------------------------------
 * 函数：CAT1_PropertyVersion
 * 功能：上报固件版本号
 * 参数：无
 * 返回：无
 * 负责：C
 *
 * Topic: /ota/device/inform/{pk}/{dn}
 * 数据:  {"id": 1,"params":{"version":"app_2023112855511"}}
 * ★版本号必须正好 23 个字符★
 *-------------------------------------------------------------------*/
void CAT1_PropertyVersion(void);

/*---------------------------------------------------------------------
 * 函数：CAT1_PropertyPost
 * 功能：上报属性
 * 参数：postdata - JSON，如 {"params":{"powerstate":1}}
 * 返回：无
 * 负责：C
 *-------------------------------------------------------------------*/
void CAT1_PropertyPost(char *postdata);


/*---------------------------------------------------------------------
 * 函数：MQTT_DealPushdata_Qs0
 * 功能：从 PUBLISH 报文里提取 JSON 载荷到 cmdbuff
 * 参数：data - 数据；len - 长度
 * 返回：无
 * 负责：C（从 36 号工程移植）
 *-------------------------------------------------------------------*/
void MQTT_DealPushdata_Qs0(u8 *data, u16 len);

#endif /* __MQTT_H */

