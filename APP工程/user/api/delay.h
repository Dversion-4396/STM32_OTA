#ifndef _DELAY_H
#define _DELAY_H
#include "common.h"

/* 开机以来的毫秒数，由 SysTick 中断（1ms）维护。
   volatile 必须加：主循环在读，中断在写。 */
extern volatile u32 g_sys_ms;

/**
 * @brief 配置 SysTick 为 1ms 中断，并清零 g_sys_ms。开机调用一次。
 *        之后 delay_ms / delay_us / Get_Tick 都可用。
 */
void Delay_Init(void);

/**
 * @brief 延时，单位微秒。基于 SysTick 计数值，与主频严格对应。
 */
void delay_us(u32 us);

/**
 * @brief 延时，单位毫秒。基于 g_sys_ms 递增，与中断节拍严格对应。
 *        ★依赖中断打开★ 在 __disable_irq() 里调用会死等。
 */
void delay_ms(u32 ms);

/**
 * @brief 读取当前毫秒时基。心跳、超时判断都用它代替"数主循环圈数"。
 *        无符号减法天然处理 48.6 天回绕：if (Get_Tick() - t0 >= 30000)
 */
u32 Get_Tick(void);

#endif
