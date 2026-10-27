#include "key.h"
#include "delay.h"
void Key_Init(void)
{
    RCC->APB2ENR |= RCC_APB2_GPIOA_CLOCK_ENABLE | RCC_APB2_GPIOE_CLOCK_ENABLE;
    ///对key_up初始化 对应pa0   其工作模式为下拉
    GPIOA->CRL &= ~(0x0f);
    GPIOA->CRL |= Pull_Up_Or_Pull_Down_Input_Mode;
    GPIOA->ODR &= ~(0x01);
    ///对key2 初始化 对应端口为pe2 其工作模式为上拉
    GPIOE->CRL &= ~(0x0f<<8);
    GPIOE->CRL |= (Pull_Up_Or_Pull_Down_Input_Mode<<8);
    GPIOE->ODR |= (0x01<<2);
     ///对key1 初始化 对应端口为pe3 其工作模式为上拉
    GPIOE->CRL &= ~(0x0f<<12);
    GPIOE->CRL |= (Pull_Up_Or_Pull_Down_Input_Mode<<12);
    GPIOE->ODR |= (0x01<<3);
     ///对key0 初始化 对应端口为pe4 其工作模式为上拉
    GPIOE->CRL &= ~(0x0f<<16);
    GPIOE->CRL |= (Pull_Up_Or_Pull_Down_Input_Mode<<16);
    GPIOE->ODR |= (0x01<<4);
}
Key_Value Key_Press(void)
{
    if (0x01&GPIOA->IDR)
    {
        delay_ms(20);
        while (0x01&GPIOA->IDR);
        return Key_Up_Press;
    }
    if (!((0X01<<2)&GPIOE->IDR))
    {
        delay_ms(20);
        while (!((0X01<<2)&GPIOE->IDR));
        return Key2_Press;
    }
    if (!((0X01<<3)&GPIOE->IDR))
    {
        delay_ms(20);
        while (!((0X01<<3)&GPIOE->IDR));
        return Key1_Press;
    }
    if (!((0X01<<4)&GPIOE->IDR))
    {
        delay_ms(20);
        while (!((0X01<<4)&GPIOE->IDR));
        return Key0_Press;
    }
    return Key_RELESE;
}
