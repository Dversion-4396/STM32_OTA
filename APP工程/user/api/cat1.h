/*=====================================================================
 * 文件：cat1.h
 * 说明：L511-2 4G 模块驱动
 * 负责：A
 *
 * 硬件连接（战神V1）：
 *   串口：USART3（PB10=TX / PB11=RX）★需拔掉板子上的 P5 跳线帽★
 *   PWRKEY   = PF0（开漏输出）
 *   RESET    = PF1（推挽输出）
 *   NET_STA  = PF2（上拉输入，低电平=已注册网络）
 *   STATUS   = PF3（上拉输入，可选）
 *
 * ★电源必须独立★：模块发射瞬间要 1.2A，从开发板取电会反复重启
 *====================================================================*/
#ifndef __CAT1_H
#define __CAT1_H

#include "common.h"
#define USART3_BRR (115200)
/*==================== 串口定义 ====================*/
#define CAT1_UART           USART3
#define CAT1_printf         Usart3_SendString
#define CAT1_RX_BUF         usart3_value.data
#define CAT1_RxCounter      usart3_value.count

/*==================== 控制脚定义（PF0~PF3）====================*/
#define CAT1_CTRL_PORT      GPIOF
#define CAT1_CTRL_CLK       RCC_APB2Periph_GPIOF

#define PWRKEY_PIN          GPIO_Pin_0
#define RESET_PIN           GPIO_Pin_1
#define NET_STA_PIN         GPIO_Pin_2
#define STATUS_PIN          GPIO_Pin_3

/*==================== 控制宏 ====================*/
#define PWRKEY_LOW()        GPIO_ResetBits(CAT1_CTRL_PORT, PWRKEY_PIN)
#define PWRKEY_HIGH()       GPIO_SetBits(CAT1_CTRL_PORT, PWRKEY_PIN)
#define RESET_LOW()         GPIO_ResetBits(CAT1_CTRL_PORT, RESET_PIN)
#define RESET_HIGH()        GPIO_SetBits(CAT1_CTRL_PORT, RESET_PIN)

#define NET_STA             GPIO_ReadInputDataBit(CAT1_CTRL_PORT, NET_STA_PIN)
#define PWR_STA             GPIO_ReadInputDataBit(CAT1_CTRL_PORT, STATUS_PIN)

/*==================== 函数声明 ====================*/

/*---------------------------------------------------------------------
 * 函数：CAT1_GPIO_Init
 * 功能：初始化 4 个控制脚
 * 参数：无
 * 返回：无
 * 负责：A
 *-------------------------------------------------------------------*/
void CAT1_GPIO_Init(void);

/*---------------------------------------------------------------------
 * 函数：CAT1_PowerOn
 * 功能：4G 模块开机
 * 参数：timeout - 等网络注册的超时（单位 100ms，600 = 60 秒）
 * 返回：0=成功  1=超时
 * 负责：A
 *
 * L511 规格书：PWRKEY 拉低超过 1 秒然后释放
 *-------------------------------------------------------------------*/
u8 CAT1_PowerOn(u16 timeout);

/*---------------------------------------------------------------------
 * 函数：CAT1_SendAT
 * 功能：发送 AT 指令并等待期望回复
 * 参数：cmd        - 指令（不含 \r\n，函数自动加）
 *       expect     - 期望关键字，如 "OK"
 *       timeout_ms - 超时（毫秒）
 * 返回：0=收到期望回复  1=超时
 * 负责：A
 *-------------------------------------------------------------------*/
u8 CAT1_SendAT(char *cmd, char *expect, u16 timeout_ms);

/*---------------------------------------------------------------------
 * 函数：CAT1_ActiveGPRS
 * 功能：激活 GPRS 承载（拿到 IP）
 * 参数：无
 * 返回：0=成功  1~4=失败步骤号
 * 负责：A
 *-------------------------------------------------------------------*/
u8 CAT1_ActiveGPRS(void);

/*---------------------------------------------------------------------
 * 函数：CAT1_HttpInit
 * 功能：初始化 HTTP 并设置 URL，发起 GET
 * 参数：url - 完整下载地址（http:// 开头）
 * 返回：0=成功  1~6=失败步骤号
 * 负责：A
 *-------------------------------------------------------------------*/
u8 CAT1_HttpInit(char *url);

/*---------------------------------------------------------------------
 * 函数：CAT1_HttpRead
 * 功能：读取固件的一段
 * 参数：offset - 文件偏移；len - 长度（建议 256）；outbuf - 输出缓冲
 * 返回：0=成功  1=超时  2=定位失败
 * 负责：A
 *
 * ★返回二进制数据，含 0x00，必须用 memcpy 不能用 strcpy★
 *-------------------------------------------------------------------*/
u8 CAT1_HttpRead(u32 offset, u16 len, u8 *outbuf);

/*---------------------------------------------------------------------
 * 函数：CAT1_OTA
 * 功能：OTA 下载主流程
 * 参数：url - 下载地址；fw_len - 固件总长度（含 28 字节尾缀）
 * 返回：0=成功
 * 负责：A
 *-------------------------------------------------------------------*/
u8 CAT1_OTA(char *url, u32 fw_len);

/*---------------------------------------------------------------------
 * 函数：CAT1_ParseOTA
 * 功能：解析云端 OTA 推送报文
 * 参数：json - 收到的 JSON
 * 返回：0=成功  1=找不到 data  2=格式不匹配
 * 负责：C
 *-------------------------------------------------------------------*/
u8 CAT1_ParseOTA(char *json);

#endif /* __CAT1_H */
