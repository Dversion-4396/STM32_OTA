#include "systick.h"
void SysTick_Init(void)
{
    /*
    SystemCoreClock Cortex-M3 内核程序中提供的用于描述当前
    MCU 时钟频率宏定义，并且当前宏定义可以根据不同底层芯片信号进行分别处理处理

    SystemCoreClock / 1000 使用 Cortex-M3 内核提供的初始化函数 SysTick_Config
    配置当前系统嘀嗒时间为 1 ms
    SysTick_Config 返回值 1 表示失败，0 表示成功，如果失败进入 while (1) 表示系统
    初始化失败。
    */
    if (SysTick_Config(SystemCoreClock / 1000))
    {
        while (1);
    }
}
uint32_t g_systick = 0;
void SysTick_Handler(void)
{
    // g_msTicks 执行累加操作
    g_systick += 1;
}
void Delay_Ms(uint32_t ms)
{
    uint32_t StartTick = g_systick;
    while ((g_systick - StartTick) < ms);
}