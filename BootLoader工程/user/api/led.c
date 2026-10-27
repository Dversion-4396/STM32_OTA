#include "led.h"
#include "delay.h"
void Led_Init(void)
{
    /*
     *对其相应接口进行时钟赋能
     */
    RCC->APB2ENR |= RCC_APB2_GPIOB_CLOCK_ENABLE | RCC_APB2_GPIOE_CLOCK_ENABLE;
    /// 初始化并设置通用推挽输出模式
    GPIOB->CRL &= ~(0x0f << 20);
    GPIOB->CRL |= (General_Push_And_Pull_Speed_2MHz << 20);
    GPIOE->CRL &= ~(0x0f << 20);
    GPIOE->CRL |= (General_Push_And_Pull_Speed_2MHz << 20);
    /// 设置默认高电平 —— ★ 本板 LED 是低电平点亮，所以一开始是"灭"
    Led_High(GPIOB);
    Led_High(GPIOE);
    
}
void Led_Breath(u32 Brightness)
{
    /* 低电平点亮：Brightness 越大，灯"亮"的占空比越小，所以这里越暗 */
    for (u32 i = 0; i < 256; i++)
    {
        i < Brightness ? Led_Low(GPIOB):Led_High(GPIOB);
        delay_us(1);
    }
}
