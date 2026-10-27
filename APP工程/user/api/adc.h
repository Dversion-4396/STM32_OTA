#ifndef _ADC_H
#define _ADC_H
#include "common.h"
extern u16 lsen_value;

/// @brief ADC 模块初始化函数 
void ADC3_Init(void);
void Get_lsen_value(void);
#endif