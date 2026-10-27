#ifndef _USART_H
#define _USART_H
#include "common.h"
#include "modbus.h"
#define USART3_BUFFER_SIZE (1024)   /* ★改为 1024：HTTP 下载一次返回约 274 字节★ */
#define USART1_BUFFER_SIZE (256)
#define USART2_BUFFER_SIZE (256)
typedef struct
{
  // u8 类型缓冲区数组，用于存放数据内容
  u8 data[USART3_BUFFER_SIZE];
  // data 数组中，有效字节个数
  u16 count;
  /*
  情况一: 当前数据尚未接收完成，flag = 0
  情况二: 当前数据接收完成，触发 IDLE 中断，flag = 1
  情况三: data 数据接收缓冲区已满，flag = 1
  */
  u8 flag;
} USART_Data;
extern USART_Data usart1_value;

extern USART_Data usart2_value;

extern USART_Data usart3_value;
/// @brief usart1初始化函数要求用户提供对应波特率
/** Tx=pa9 rx=pa10
 *  需要设置浮空输入模式
 * nrz数据格式为8n1
 */
/// @param brr 波特率需要用户根据实际来进行选择
void Usart1_Init(u32 brr);
/// 发送一个字节到接受端
void Usart1_SendByte(u8 byte);
/// @brief 模块指定字节个数发送到接收端
/// @param Buffer 发送数据空间对应地址
/// @param len 发送的字节数
void Usart1_SendBuffer(char *Buffer, u16 len);
/// @brief 向目标发送字符串
/// @param str 目标字符串内容
void Usart1_SendString(const char *str);
void Usart2_Init(u32 brr);
void Usart2_SendByte(u8 byte);

void Usart2_SendBuffer(u8 *Buffer, u16 len);

void Usart2_SendString(const char *str);
void Usart3_Init(u32 brr);
void Usart3_SendByte(u8 byte);

void Usart3_SendBuffer(char *Buffer, u16 len);

void Usart3_SendString(const char *str);
void RS485_Init(u16 brr);
void RS485_SendBuffer(u8 *data, u16 count);

#endif