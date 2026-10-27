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
    // ClientID
    sprintf(Bemfa_mqtt.ClientID,"client");
    // username 
    sprintf(Bemfa_mqtt.Username,USERNAME_KEY);
    // password 
    sprintf(Bemfa_mqtt.Passward,USERNAME_KEY);
    // Server IP 
    sprintf(Bemfa_mqtt.ServerIP,SERVER_IP);
    // Serrver Port
    Bemfa_mqtt.ServerPort = SERVER_PORT;
    // 订阅 Topic 
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
    // 1. 初始化设备
	Usart1_Init(USART1_BRR);
	Usart3_Init(USART3_BRR);
	
	/*
	进入 WiFi 模块操作，需要发送 AT 指令
	*/
	
	// 1. 发送 "AT\r\n" 到 WiFi 模块
	Usart3_SendString("AT\r\n");
	/*
	在每一条 AT 指令之间，需要预留一定的延时，满足 WiFi 模块对 AT 指令处理
	和响应所需必要时间。通常情况下时间 2 ~ 5 秒
	*/
	delay_ms(2000);
	
	// 2. 设置当前 WiFi 模块对应的工作模式为 STA 模式
	/*
	AT+CWMODE 设置当前 WiFi 模块对应工作模式
		1. AT+CWMODE 已淘汰，但是可以使用
		2. AT+CWMODE_CUE 设置当前 WiFi 工作模式，不储存到 Flash 下次启动
			无记录，需要重新配置
		3. AT+CWMODE_DEF 设置当前 WiFi 工作模式，存储到 Flash 下次启动直
			接使用当前配置
	*/
	Usart3_SendString("AT+CWMODE_CUR=1\r\n");
	delay_ms(2000);
	
	/*
	3. 扫描当前可用 AP 热点信息
	*/
	Usart3_SendString("AT+CWLAP\r\n");
	delay_ms(3000);
	
	/*
	4. 连接目标热点，需要提供
		1. 热点对应 SSID
		2. 热点对应 密码
		3. 【重点】当前 AP 热点必须是 2.4 GHz 频段。
	要求提供的 AT 指令为
		AT+CWJAP="你的WiFi名称","你的WiFi密码"
		要求 WiFi 名称和对应密码必须在 AT 指令中是字符串形式，
		因为本身 AT 指令就是字符串形式，需要提供转移字符进行处理
	*/
	char at_cmd[64] = "";
	sprintf(at_cmd,"AT+CWJAP=\"%s\",\"%s\"\r\n",AP_SSID,AP_PSK);
	Usart3_SendBuffer(at_cmd,strlen(at_cmd));
	
	delay_ms(10000);
	
	/*
	5. 发送 AT + CIFSR 到 WiFi 模块查看当前连接网络情况
	*/
	Usart3_SendString("AT+CIFSR\r\n");
	delay_ms(3000);

    memset(at_cmd,0,64);
	sprintf(at_cmd,"AT+CIPSTART=\"TCP\",\"%s\",%d\r\n",MQTT_SERVER_IP_ADDR,MQTT_SERVER_PORT);
	Usart3_SendBuffer(at_cmd,strlen(at_cmd));
	delay_ms(2000);

    Usart3_SendString("AT+CIPMODE=1\r\n");
	delay_ms(2000);
	
	Usart3_SendString("AT+CIPSEND\r\n");
	delay_ms(2000);
	
	/*
	需要在透传模式开启工作之后，发送 MQTT 数据包，此时 WiFi 模块会直接将所有的数据内容
	直接发送到——连接的 TCP 远端服务。
	*/
    MQTT_ConectPack();
	
	delay_ms(2000);
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
    int temp;              //计算报文剩余长度时，使用的临时变量              
	int Remaining_len;     //保存报文剩余长度字节 
	
	Bemfa_mqtt.MessageID = 0;  //报文标识符清零，CONNECT报文虽然不需要添加报文标识符，但是CONNECT报文是第一个发送的报文，在此清零报文标识符，为后续报文做准备
	Bemfa_mqtt.Fixed_len = 1;                                                                                                        //CONNECT报文，固定报头长度暂定为1
	Bemfa_mqtt.Variable_len = 10;                                                                                                    //CONNECT报文，可变报头长度=10
	Bemfa_mqtt.Payload_len = 2 + strlen(Bemfa_mqtt.ClientID) + 2 + strlen(Bemfa_mqtt.Username) + 2 + strlen(Bemfa_mqtt.Passward); //CONNECT报文，计算负载长度      
	Remaining_len = Bemfa_mqtt.Variable_len + Bemfa_mqtt.Payload_len;                                                               //剩余长度=可变报头长度+负载长度
	
	Bemfa_mqtt.Pack_buff[0]=0x10;                                   //CONNECT报文 固定报头第1个字节 ：0x10	
	do{                                                              //循环处理固定报头中的剩余长度字节，字节量根据剩余字节的真实长度变化
		temp = Remaining_len%128;                                    //剩余长度取余128
		Remaining_len = Remaining_len/128;                           //剩余长度取整128
		if(Remaining_len>0) temp |= 0x80;                            //如果Remaining_len大于等于128了 按协议要求位7置位                   				     
		Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len] = temp;         //剩余长度字节记录一个数据
		Bemfa_mqtt.Fixed_len++;	                                 //固定报头总长度+1    
	}while(Remaining_len>0);                                         //如果Remaining_len>0的话，再次进入循环
	
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+0]=0x00;             //CONNECT报文，可变报头第1个字节 ：固定0x00	            
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+1]=0x04;             //CONNECT报文，可变报头第2个字节 ：固定0x04
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+2]=0x4D;	         //CONNECT报文，可变报头第3个字节 ：固定0x4D
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+3]=0x51;	         //CONNECT报文，可变报头第4个字节 ：固定0x51
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+4]=0x54;	         //CONNECT报文，可变报头第5个字节 ：固定0x54
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+5]=0x54;	         //CONNECT报文，可变报头第6个字节 ：固定0x54
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+6]=0x04;	         //CONNECT报文，可变报头第7个字节 ：固定0x04
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+7]=0xC2;	         //CONNECT报文，可变报头第8个字节 ：使能用户名和密码校验，不使用遗嘱功能，不保留会话功能
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+8]=0x00; 	         //CONNECT报文，可变报头第9个字节 ：保活时间高字节 0x00
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+9]=0x64;	         //CONNECT报文，可变报头第10个字节：保活时间高字节 0x64   最终值=100s
	
	/*     CLIENT_ID      */
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+10] = strlen(Bemfa_mqtt.ClientID)/256;                			  		                                           //客户端ID长度高字节
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+11] = strlen(Bemfa_mqtt.ClientID)%256;               			  		                                           //客户端ID长度低字节
	memcpy(&Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+12],Bemfa_mqtt.ClientID,strlen(Bemfa_mqtt.ClientID));                                                    //复制过来客户端ID字串	
	/*     用户名        */ 
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+12+strlen(Bemfa_mqtt.ClientID)] = strlen(Bemfa_mqtt.Username)/256; 	                                               //用户名长度高字节
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+13+strlen(Bemfa_mqtt.ClientID)] = strlen(Bemfa_mqtt.Username)%256; 		                                           //用户名长度低字节
	memcpy(&Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+14+strlen(Bemfa_mqtt.ClientID)],Bemfa_mqtt.Username,strlen(Bemfa_mqtt.Username));                           //复制过来用户名字串	
	/*      密码        */
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+14+strlen(Bemfa_mqtt.ClientID)+strlen(Bemfa_mqtt.Username)] = strlen(Bemfa_mqtt.Passward)/256;	                        //密码长度高字节
	Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+15+strlen(Bemfa_mqtt.ClientID)+strlen(Bemfa_mqtt.Username)] = strlen(Bemfa_mqtt.Passward)%256;	                        //密码长度低字节
	memcpy(&Bemfa_mqtt.Pack_buff[Bemfa_mqtt.Fixed_len+16+strlen(Bemfa_mqtt.ClientID)+strlen(Bemfa_mqtt.Username)],Bemfa_mqtt.Passward,strlen(Bemfa_mqtt.Passward));   //复制过来密码字串
   
	// TxDataBuf_Deal(Bemfa_mqtt.Pack_buff, Bemfa_mqtt.Fixed_len + Bemfa_mqtt.Variable_len + Bemfa_mqtt.Payload_len); //加入发送数据缓冲区
    Usart3_SendBuffer(Bemfa_mqtt.Pack_buff,Bemfa_mqtt.Fixed_len + Bemfa_mqtt.Variable_len + Bemfa_mqtt.Payload_len);
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
