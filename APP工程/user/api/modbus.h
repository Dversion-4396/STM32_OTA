#ifndef _MODBUS_H
#define _MODBUS_H
#include "common.h"
#include "usart.h"
#define LOCAL_ADDR (0X01)

//// 波特率
#define RS485_BRR (9600)
//// 485单一字节传递所需时间
#define BYTE_INTERVAL_TIME (1000000 / (RS485_BRR / 10))

#define USING_T3_5 3.5


#ifdef USING_T3_5
/// 条件编译
/*
    若使用 3.5 个字节间隔时间，则定义 USING_T3_5
*/
#define DATA_FRAME_TIME_INTERVAL (BYTE_INTERVAL_TIME * 3 + (BYTE_INTERVAL_TIME >> 1))
#endif
/*
    若使用 4 个字节间隔时间，则定义 USING_T4_0
*/
#ifdef USING_T4_0
#define DATA_FRAME_TIME_INTERVAL (BYTE_INTERVAL_TIME * 4)
#endif
/// @brief  MOSBus 数据接受控制定时使用Tim7 实现
void Modbus_Tim_Config(void);
/// @brief 发送 Modbus 03 功能码来进行对多个寄存器数据的提取操作
/// @param device_addr 从机设备地址
/// @param reg_addr 从机寄存器起始地址
/// @param reg_count 期望读取的寄存器个数
void Modbus_Send_03_Cmd(u8 device_addr, u16 reg_addr, u16 reg_count);

/// @brief
/// @param buffer
/// @param count
/// @return
u8 Modbus_Analysis(u8 *buffer, u16 count);
/**
 * @brief Modbus CRC-16 计算函数
 *
 * @param data   待计算的数据指针
 * @param length 数据长度（字节数）
 * @return CRC校验值（16位）
 */
uint16_t Modbus_CRC16(const uint8_t *data, size_t length);
void Modbus_SendData_03(u8 device_addr, u16 reg_count, u16 *data);
u8 Modbus_Cmd_Analysis(u8 *buffer, u16 count);
extern volatile u8 usart2_frame_flag;
#endif