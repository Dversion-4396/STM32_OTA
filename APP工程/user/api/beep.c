#include "beep.h"
void Beep_Init(void)
{
    RCC->APB2ENR |=RCC_APB2_GPIOB_CLOCK_ENABLE;
    GPIOB->CRH &=~(0x0f);
    GPIOB->CRH |= (General_Push_And_Pull_Speed_2MHz);
}
