#ifndef _EEPROM_H
#define _EEPROM_H

#include "common.h"
#include "i2c.h"


/**
 * @brief 写入一个字节数据到 EEPROM AT24C02 存储芯片中，
 *
 * @param eeprom_addr EEPROM AT24C02 存储芯片 I2C 设备地址
 * @param reg_addr    目标写入到存储芯片寄存器地址
 * @param data        目标写入的 1 字节数据内容
 * @return 对应当前从机应答信息。
 */
u8 EEPROM_WriteByte(u8 eeprom_addr, u8 reg_addr, u8 data);

/**
 * @brief 从 EEPROM AT24C02 存储芯片中读取一个字节数据
 *
 * @param eeprom_addr EEPROM AT24C02 存储芯片 I2C 设备地址
 * @param reg_addr    目标读取数据的寄存器地址
 * @return 对应读取到的数据内容
 */
u8 EEPROM_ReadByte(u8 eeprom_addr, u8 reg_addr);

#endif