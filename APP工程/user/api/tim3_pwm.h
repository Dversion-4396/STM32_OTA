#ifndef _TIME3_PWM_H
#define _TIME3_PWM_H
#include "common.h"
void TIM3_CH2_PB5_PWM(u16 psc, u16 arr);
void TIM3_CH4_PB1_PWM(u16 psc, u16 arr);
void TIM3_CH2_PB5_PWM_Set_CCR2(u16 ccr2);
void TIM3_CH4_PB1_PWM_Set_CCR2(u16 ccr2);
#endif