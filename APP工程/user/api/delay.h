#ifndef _DELAY_H
#define _DELAY_H
#include "common.h"
/**
 * @brief 延时控制函数，控制时间单位为 us 微秒
 *
 * @param us 用户提供的控制延时的微秒数
 */
void delay_us(u32 us);
/**
 * @brief 延时控制函数，控制时间单位为 ms 毫秒
 *
 * @param ms 用户提供的控制延时的毫秒数
 */
void delay_ms(u32 ms); 
#endif