#include "led.h"
#include "delay.h"

/*
	LED 驱动实现
	==================================================================
	硬件见 led.h 顶部说明。

	★ 用寄存器操作而不是标准库 GPIO_Init:
	  因为 common.h 里已经有寄存器宏风格（Led0_Toggle / Led_High / Led_Low），
	  而这两个脚在 common.h 里的宏都写死了 bit5 —— PB5 和 PE5 刚好都是 bit5，
	  所以 Led0_Toggle / Led1_Toggle 都能直接用。
	  但为了本文件自洽、不依赖调用者先开时钟，这里还是自己配一遍。

	★ 为什么 LED0 用 GPIOB bit5、LED1 用 GPIOE bit5 是巧合:
	  PB5 和 PE5 位号相同，所以 common.h 的宏 `(0x01 << 5)` 两个都能用。
	  这是本板的设计，不要以为是笔误。
*/

/* 非阻塞节流用：靠主循环轮次计数，不依赖 SysTick
 * （本工程的 delay 是纯空转，没有 ms 计数器，见 delay.c）
 */
static u16 s_hb_cnt   = 0;   /* 心跳计数 */
static u8  s_err_on   = 0;   /* 错误模式 */
static u8  s_ota_busy = 0;   /* OTA 下载中 */

void Led_Init(void)
{
    /* 1. 开 GPIOB（红灯）和 GPIOE（绿灯）的时钟 */
    RCC->APB2ENR |= RCC_APB2_GPIOB_CLOCK_ENABLE | RCC_APB2_GPIOE_CLOCK_ENABLE;

    /* 2. PB5 配置成推挽输出 50MHz
     *    CRL 管 Pin0~Pin7，每个引脚 4 位；Pin5 对应 bit20~23
     *    0x03 = 推挽输出 50MHz
     */
    GPIOB->CRL &= ~(0x0fUL << 20);
    GPIOB->CRL |=  (General_Push_And_Pull_Speed_50MHz << 20);

    /* 3. PE5 同样配置（GPIOE 的 CRL 也是管 Pin0~Pin7） */
    GPIOE->CRL &= ~(0x0fUL << 20);
    GPIOE->CRL |=  (General_Push_And_Pull_Speed_50MHz << 20);

    /* 4. 初始状态：两灯都灭（低电平点亮，所以置高 = 灭）
     *    ★ 必须先灭再开，否则上电会看到灯闪一下
     */
    GPIOB->BSRR = (0x01UL << 5);
    GPIOE->BSRR = (0x01UL << 5);

    s_hb_cnt   = 0;
    s_err_on   = 0;
    s_ota_busy = 0;
}

void Led_On(u8 which)
{
    if (which == LED_RED)
    {
        GPIOB->BRR = (0x01UL << 5);   /* 置低 = 亮 */
    }
    else
    {
        GPIOE->BRR = (0x01UL << 5);
    }
}

void Led_Off(u8 which)
{
    if (which == LED_RED)
    {
        GPIOB->BSRR = (0x01UL << 5);  /* 置高 = 灭 */
    }
    else
    {
        GPIOE->BSRR = (0x01UL << 5);
    }
}

void Led_Toggle(u8 which)
{
    if (which == LED_RED)
    {
        GPIOB->ODR ^= (0x01UL << 5);
    }
    else
    {
        GPIOE->ODR ^= (0x01UL << 5);
    }
}

/*
	主循环心跳
	------------------------------------------------------------------
	★ 节流方式：用主循环轮次计数，不用 SysTick。
	  主循环末尾有 delay_ms(10)，所以 1 轮 ≈ 10ms + 处理时间。
	  50 轮 ≈ 500ms，够接近 1Hz 了。
	  这不是精确计时 —— 但如果主循环卡住，灯一定停，这正是我们要的。
*/
void Led_Heartbeat(void)
{
    s_hb_cnt++;

    /* OTA 下载中：红灯常亮，不参与心跳 */
    if (s_ota_busy)
    {
        Led_On(LED_RED);
        return;
    }

    /* 错误模式：红灯 5Hz 快闪（每 5 轮翻转 ≈ 50ms 亮 / 50ms 灭） */
    if (s_err_on)
    {
        if ((s_hb_cnt % 5) == 0)
        {
            Led_Toggle(LED_RED);
        }
        return;
    }

    /* 正常模式：绿灯 1Hz 慢闪（每 50 轮翻转 ≈ 500ms） */
    if ((s_hb_cnt % 50) == 0)
    {
        Led_Toggle(LED_GREEN);
    }
}

void Led_Error(u8 on)
{
    s_err_on = on ? 1 : 0;

    if (s_err_on)
    {
        /* 进错误态时把红灯点亮，让用户立刻看到 */
        Led_On(LED_RED);
    }
    else
    {
        Led_Off(LED_RED);
        s_hb_cnt = 0;   /* 重置计数，避免立刻触发一次翻转 */
    }
}

void Led_OtaBusy(u8 on)
{
    s_ota_busy = on ? 1 : 0;

    if (s_ota_busy)
    {
        Led_On(LED_RED);
    }
    else
    {
        Led_Off(LED_RED);
        s_hb_cnt = 0;
    }
}
