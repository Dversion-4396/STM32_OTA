/*=====================================================================
 * 文件：delay.c
 * 说明：标准时基延时（SysTick 1ms 中断）
 *
 * ★ 为什么要重写 ★
 *   原来 delay_us 是 100 个 __NOP() 的忙循环，延时长度取决于
 *   编译器优化级别和主频，实测 delay_ms(10) 实际是 15~20ms。
 *   后果有两个：
 *     ① 主循环周期不准 → MQTT 心跳"数圈数"计时跟着漂
 *     ② 4G 等待、备份间隔这些时序全靠猜
 *
 * ★ 现在的做法 ★
 *   SysTick 每 1ms 中断一次，中断里只给 g_sys_ms 加 1。
 *     delay_ms  → 等 g_sys_ms 走够 n
 *     delay_us  → 读 SysTick->VAL 倒计数，72 个 tick = 1us
 *     Get_Tick  → 直接返回 g_sys_ms
 *
 *   延时精度只跟 SystemCoreClock 有关，与优化级别无关。
 *
 * ★ 注意 ★
 *   本文件的 SysTick_Handler 写在 stm32f10x_it.c 里（那边本来就是
 *   空的弱定义），这里只放 g_sys_ms 的定义和三个延时函数。
 *====================================================================*/
#include "delay.h"

/* SysTick 中断里自增；主循环只读，所以是 volatile */
volatile u32 g_sys_ms = 0;

/*=====================================================================
 * ★ SysTick 中断服务函数 ★
 *
 * 【为什么写在这个文件，而不是 stm32f10x_it.c】
 *   本工程的 Keil 配置里，stm32f10x_it.c 的 IncludeInBuild = 0，
 *   也就是【不参与编译】。如果把这个函数写在那边，链接器会去用
 *   启动文件 startup_stm32f10x_hd.s 里的弱定义：
 *       SysTick_Handler  PROC
 *                        B   .        ← 原地死循环
 *   一旦 Delay_Init() 打开 SysTick，1ms 后 CPU 就卡死在那儿，
 *   现象是上电打印完 "=== STM32 OTA 启动 ===" 就再也不动了。
 *
 *   放进 delay.c 就没有这个问题：delay.c 一定参与编译，
 *   强定义会覆盖启动文件的弱定义。
 *
 *   ★以后如果谁把 stm32f10x_it.c 打开了，必须先把这里的删掉，
 *     否则两个强定义会报 multiply defined。★
 *
 * 【中断里只做一件事】
 *   给 g_sys_ms 加 1。不要在这里调串口发送、MQTT、Flash 读写，
 *   1ms 一次的节奏做任何耗时操作都会把主循环拖垮。
 *====================================================================*/
void SysTick_Handler(void)
{
    g_sys_ms++;
}

/*=====================================================================
 * 函数：Delay_Init
 * 功能：把 SysTick 配成 1ms 周期中断，并清零时基
 * 说明：SysTick_Config 内部会
 *         ① 设 LOAD = 72000-1（72MHz 下正好 1ms）
 *         ② 清 VAL
 *         ③ 设优先级为最低（0xF0），不会抢串口中断
 *         ④ 开 SysTick 中断并使能计数器
 *       反过来说：只要 SystemCoreClock 不是 72000000，延时会同比例偏。
 *       怀疑时钟不对时，开机打印一下 SystemCoreClock 就能确认。
 *====================================================================*/
void Delay_Init(void)
{
    g_sys_ms = 0;
    SysTick_Config(SystemCoreClock / 1000);
}

/*=====================================================================
 * 函数：Get_Tick
 * 功能：返回开机以来的毫秒数（约 48.6 天回绕一次）
 * 用法：u32 t0 = Get_Tick();  ...  if (Get_Tick() - t0 >= 30000) { 30 秒到了 }
 *       无符号减法自动处理回绕，不要写成 if (Get_Tick() > t0 + 30000)
 *====================================================================*/
u32 Get_Tick(void)
{
    return g_sys_ms;
}

/*=====================================================================
 * 函数：delay_ms
 * 功能：毫秒级延时，基于 1ms 中断节拍
 * 说明：用 (g_sys_ms - start) 而不是比较大小，跨回绕也正确
 *====================================================================*/
void delay_ms(u32 ms)
{
    u32 start;

    if (ms == 0)
    {
        return;
    }

    start = g_sys_ms;
    while ((u32)(g_sys_ms - start) < ms)
    {
        /* 空转，中断在后台推进 g_sys_ms */
    }
}

/*=====================================================================
 * 函数：delay_us
 * 功能：微秒级延时
 *
 * 说明：SysTick 的计数器 VAL 是【向下计数】的，LOAD → 0 → 重装。
 *       所以已过时间 = start - now（没跨过 0）或者
 *                    start + (LOAD+1) - now（跨过了 0）
 *
 *       超过 1ms 的部分先交给 delay_ms，剩下的不足 1ms 再精算，
 *       这样 us 传很大的值也不会因为 ticks 超过 LOAD 而死等。
 *====================================================================*/
void delay_us(u32 us)
{
    u32 start;
    u32 ticks;
    u32 elapsed;

    if (us == 0)
    {
        return;
    }

    if (us >= 1000)
    {
        delay_ms(us / 1000);
        us %= 1000;
        if (us == 0)
        {
            return;
        }
    }

    /* 72MHz 下 72 个 tick = 1us，所以最多 999*72 = 71928 < 72000，不会超一圈 */
    ticks = us * (SystemCoreClock / 1000000UL);
    start = SysTick->VAL;

    do
    {
        u32 now = SysTick->VAL;

        if (now <= start)
        {
            elapsed = start - now;
        }
        else
        {
            elapsed = start + (SysTick->LOAD + 1UL) - now;
        }
    } while (elapsed < ticks);
}
