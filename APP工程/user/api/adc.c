#include "adc.h"
u16 lsen_value = 0;
/*
【DMA 补充】
    将 ADC 模块中对应的 DR 数据直接利用 DMA 搬运到 lsen_value 变量中。
    【片上外设数据】==> 【MCU 内存】

【整体开发流程】
    1. DMA 模块时钟使能，当前搬运数据对应的片上外设模块对应 ADC3
        根据 DMA 模块通道片上外设映射关系，所需的 DMA 通道为
            DMA2_Channel5
    【需要时钟使能 DMA 模块】所在时钟分析为 AHB
    2. DMA 模块配置初始化
    3. DMA 触发方式配置
    4. 【????????】
*/
/**
 * @brief ADC3 模块初始化，需要完成
 *      1. 时钟使能 ADC3 模块和 GPIOF 模块
 *      2. PF8 配置为【模拟输入模式】
 *      3. ADC 相关配置
 */
void ADC3_Init(void)
{
    
}
void Get_lsen_value(void)
{
    
}