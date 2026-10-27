#ifndef _LED_H
#define _LED_H

#define Led0_Toggle GPIOB->ODR ^= (0x01 << 5)
#define Led1_Toggle GPIOE->ODR ^= (0x01 << 5)
#include "common.h"
/// @brief 对led初始化    pb5对应ds0 led0 pe5对应ds1 led1
void Led_Init(void);

void Led_Breath(u32 Brightness);

#endif