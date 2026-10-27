#include "tim6.h"
#include "usart.h"
#include "delay.h"
#define MQTT_PUBLISH_TOPIC "attributes"
void Tim6_Init(u16 psc, u16 arr)
{
    RCC->APB1ENR |= RCC_APB1_TIM6_CLOCK_ENABLE;
    TIM6->CR1 &= ~(0x01);

    TIM6->PSC = psc - 1;

    TIM6->ARR = arr - 1;

    TIM6->CR1 &= ~(0x01 << 7); // ARR 缓冲关闭
#if 1
    TIM6->CR1 &= ~(0x01 << 3); // OPM 为 0，周期性定时器
#else
    TIM6->CR1 |= (0x01 << 3); // OPM 为 1，一次性定时器
#endif
    TIM6->DIER |= (0x01);
    NVIC_SetPriority(TIM6_IRQn, 2);
    NVIC_EnableIRQ(TIM6_IRQn);
    TIM6->CR1 |= (0x01);
}
void TIM6_IRQHandler(void)
{
    // 清除中断标志位
    TIM6->SR = 0;
}
