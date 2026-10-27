#include "tim3_pwm.h"
void TIM3_CH2_PB5_PWM(u16 psc, u16 arr)
{
    /// 1.rcc 时钟使能gpiob afio tim3定时器
    RCC->APB2ENR |= RCC_APB2_GPIOB_CLOCK_ENABLE | RCC_APB2_AFIO_CLOCK_ENABLE;
    RCC->APB1ENR |= RCC_APB1_TIM3_CLOCK_ENABLE;
    // /// 2.修改pb5引脚配置
    // GPIOB->CRL &= ~(0x0f << 20);
    // GPIOB->CRL |= (Alternate_Push_And_Pull_Speed_2MHz << 20);
    /// pb1
    GPIOB->CRL &= ~(0x0f << 4);
    GPIOB->CRL |= (Alternate_Push_And_Pull_Speed_2MHz << 4);
    /// 3.关闭定时器
    TIM3->CR1 &= ~(0x01);
    /// 4.afio模块对当前tim3io引脚进行重映射方式 对应寄存器位MAPR 的10和11位
    AFIO->MAPR &= ~(0x03 << 10);
    AFIO->MAPR |= (0x02 << 10);
    /// tim3 ch2配置

    TIM3->PSC = psc - 1;
    TIM3->ARR = arr - 1;
    TIM3->CCMR1 &= ~(0x03 << 8);  // cc2s配置位00明确为输出模式
    TIM3->CCMR1 |= (0x01 << 10);  // oc2fe配置为1为快速输出模式
    TIM3->CCMR1 &= ~(0x01 << 11); // OC2PE 配置为 0，关闭 ARR 预装载，随时可修改
    TIM3->CCMR1 &= ~(0x07 << 12); // 把12到14位清零
    TIM3->CCMR1 |= (0x06 << 12);  /// 将其配置为110位向上输出pwm模式
    TIM3->CCMR1 &= ~(0x01 << 15); /// 关闭外部触发
    TIM3->CCER |= (0x01 << 5);    /// 设置位低电平有效
    TIM3->CCER |= (0x01 << 4);    /// 开启输出
    /// 启动定时器

    TIM3->CR1 &= ~(0x01 << 3); /// OPM设为零周期性定时器
    TIM3->CR1 &= ~(0x01 << 4); /// dir 设位零crt向上计数
    TIM3->CR1 &= ~(0x01 << 7); /// arr缓冲关闭
    TIM3->CR1 |= (0x01);       // 开启
}
void TIM3_CH4_PB1_PWM(u16 psc, u16 arr)
{
    /// 1.rcc 时钟使能gpiob afio tim3定时器
    RCC->APB2ENR |= RCC_APB2_GPIOB_CLOCK_ENABLE | RCC_APB2_AFIO_CLOCK_ENABLE;
    RCC->APB1ENR |= RCC_APB1_TIM3_CLOCK_ENABLE;
    // /// 2.修改pb5引脚配置
    /// pb1
    GPIOB->CRL &= ~(0x0f << 4);
    GPIOB->CRL |= (Alternate_Push_And_Pull_Speed_2MHz << 4);
    /// 3.关闭定时器
    TIM3->CR1 &= ~(0x01);
    /// 4.afio模块对当前tim3io引脚进行重映射方式 对应寄存器位MAPR 的10和11位
    AFIO->MAPR &= ~(0x03 << 10);
    AFIO->MAPR |= (0x02 << 10);
    /// tim3 ch2配置

    TIM3->PSC = psc - 1;
    TIM3->ARR = arr - 1;
    TIM3->CCMR2 &= ~(0x03 << 8);  // cc4s配置位00明确为输出模式
    TIM3->CCMR2 |= (0x01 << 10);  // oc4fe配置为1为快速输出模式
    TIM3->CCMR2 &= ~(0x01 << 11); // OC4PE 配置为 0，关闭 ARR 预装载，随时可修改
    TIM3->CCMR2 &= ~(0x07 << 12); // 把12到14位清零
    TIM3->CCMR2 |= (0x06 << 12);  /// 将其配置为110位向上输出pwm模式
    TIM3->CCMR2 &= ~(0x01 << 15); /// 关闭外部触发
    TIM3->CCER &= ~(0x01 << 13);    /// 设置位高电平有效
    TIM3->CCER |= (0x01 << 12);    /// 开启输出
    /// 启动定时器

    TIM3->CR1 &= ~(0x01 << 3); /// OPM设为零周期性定时器
    TIM3->CR1 &= ~(0x01 << 4); /// dir 设位零crt向上计数
    TIM3->CR1 &= ~(0x01 << 7); /// arr缓冲关闭
    TIM3->CR1 |= (0x01);       // 开启
}

void TIM3_CH2_PB5_PWM_Set_CCR2(u16 ccr2)
{
    TIM3->CCR2 = ccr2;
}
void TIM3_CH4_PB1_PWM_Set_CCR4(u16 ccr4)
{
    TIM3->CCR4 = ccr4;
}
