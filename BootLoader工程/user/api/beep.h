#ifndef _BEEP_H
#define  _BEEP_H
#include "common.h"
/// @brief 此为蜂鸣器初始化函数 PB8为蜂鸣器gpio口 收到高电平开始叫 通用推挽输出
void Beep_Init(void);
/// @brief 蜂鸣器开关控制函数
/// @param flag 值为1蜂鸣器开始工作 当0时候关闭
void Beep_Ctrl(u8 flag);
#endif