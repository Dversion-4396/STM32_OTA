#ifndef _KEY_H
#define _KEY_H
#include "common.h"
typedef enum
{
    Key_RELESE=-1,
    Key2_Press,
    Key1_Press,
    Key0_Press,
    Key_Up_Press,
}Key_Value;
/*
             @brief 初始化函数
*           key_up==PA0 对应下拉输入模式
*           key2  ==PE2 对应上拉输入模式
*           key1  ==PE3 对应上拉输入模式
*           key0  ==PE4 对应上拉输入模式
*/ 
void Key_Init(void);
/// @brief 用于反馈指定按钮是否被按下 
/// @return 返回类型对应key_value的类型
Key_Value Key_Press(void);
#endif
