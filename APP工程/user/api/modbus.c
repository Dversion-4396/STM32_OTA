#include "modbus.h"
// #define MASTER
extern USART_Data usart2_value;
void Modbus_Tim_Config(void)
{
    ////首先对Tim7定时器进行时钟使能
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM7, ENABLE);

    /// 由于部分操作再定时器开启时无法执行,则是能后先关闭定时器
    TIM_Cmd(TIM7, DISABLE);

    /* 此为结构体内部成员变量
        /// PSC CNT计数器预分频值
        uint16_t TIM_Prescaler;
        /// CNT 计数器计数模式
        uint16_t TIM_CounterMode;
        /// ARR 自动重装载寄存器值
        uint16_t TIM_Period;
        /// 当前系统时钟给当前定时器的分频倍速
        uint16_t TIM_ClockDivision;
        /// 定时器是否重复
        uint8_t TIM_RepetitionCounter;
    */
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure = {0};
    TIM_TimeBaseInitStructure.TIM_Prescaler = 72 - 1; /// 由于芯片为72MHz 则PSC为72时 整体频率为  1MHz
    TIM_TimeBaseInitStructure.TIM_Period = DATA_FRAME_TIME_INTERVAL - 1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up; /// 向上计数模式
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;     /// 时钟分频为1
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;            /// 不重复计数
    TIM_TimeBaseInit(TIM7, &TIM_TimeBaseInitStructure);
    TIM_SelectOnePulseMode(TIM7, TIM_OPMode_Single); /// 单次计数模式
    /// 配置定时器当前中断

    TIM_ITConfig(TIM7, TIM_IT_Update, ENABLE);
    NVIC_InitTypeDef NVIC_InitStructure = {0};
    NVIC_InitStructure.NVIC_IRQChannel = TIM7_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);
    /// 关闭当前定时
    TIM_SetCounter(TIM7, 0);
}
volatile u8 usart2_frame_flag = 0;
void TIM7_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM7, TIM_IT_Update))
    {
        /// 清除标志位
        TIM7->SR = 0;
        /// 关闭定时器
        TIM_Cmd(TIM7, DISABLE);
        TIM_SetCounter(TIM7, 0);
        usart2_frame_flag = 1;
    }
}

/// @brief 发送03功能码对设备进行多个寄存器数据提取操作
/// @param device_addr  从机设备地址
/// @param reg_addr 从机对应寄存器起始地址
/// @param reg_count 寄存器数量

void Modbus_Send_03_Cmd(u8 device_addr, u16 reg_addr, u16 reg_count)
{
    u8 modbus_03_cmd[8] = {0};
    u8 pos = 0;
    /*
    Modbus 03 功能码问询帧对应的字节数固定位 8 个字节
        地址域 1 字节
        功能码 1 字节
        寄存器地址 2 字节
        寄存器数量 2 字节
        CRC 校验 2 字节
    */
    // 1. 设备地址===>地址域 1 字节
    modbus_03_cmd[pos++] = device_addr;
    // 2. 03功能码===>功能码 1 字节
    modbus_03_cmd[pos++] = 0x03;
    // 3 寄存器地址===>寄存器地址 2 字节
    modbus_03_cmd[pos++] = reg_addr >> 8;     // 高字节
    modbus_03_cmd[pos++] = reg_addr & 0x00FF; // 低字节
    // 4.寄存器数量===>寄存器数量 2 字节
    modbus_03_cmd[pos++] = reg_count >> 8;     // 高字节
    modbus_03_cmd[pos++] = reg_count & 0x00FF; // 低字节
    // 5. 寄存器CRC 数据
    uint16_t crc = Modbus_CRC16(modbus_03_cmd, pos);
    modbus_03_cmd[pos++] = crc & 0x00FF; // 低字节
    modbus_03_cmd[pos++] = crc >> 8;     // 高字节
    // 6. 利用 RS485 模块将数据发送给从机
    Usart1_SendBuffer(modbus_03_cmd, pos);
    RS485_SendBuffer(modbus_03_cmd, pos);
}
void Modbus_SendData_03(u8 device_addr, u16 reg_count, u16 *data)
{
    u8 data_len = reg_count * 2;
    u8 modbus_03_cmd[16] = {0};
    u8 pos = 0;
    /*
    Modbus 03 功能码问询帧对应的字节数固定位 8 个字节
        地址域 1 字节
        功能码 1 字节
        寄存器地址 2 字节
        寄存器数量 2 字节
        CRC 校验 2 字节
    */
    // 1. 设备地址===>地址域 1 字节
    modbus_03_cmd[pos++] = device_addr;
    // 2. 03功能码===>功能码 1 字节
    modbus_03_cmd[pos++] = 0x03;
    // 3 数据字节数
    modbus_03_cmd[pos++] = data_len;
    // 4 存入数据
    for (u8 i = 0; i < reg_count; i++)
    {

        modbus_03_cmd[pos++] = data[i] >> 8;     // 高字节
        modbus_03_cmd[pos++] = data[i] & 0x00FF; // 低字节
    }
    // 5. 寄存器CRC 数据
    uint16_t crc = Modbus_CRC16(modbus_03_cmd, pos);
    modbus_03_cmd[pos++] = crc & 0x00FF; // 低字节
    modbus_03_cmd[pos++] = crc >> 8;     // 高字节
    // 6. 利用 RS485 模块将数据发送给主机
    RS485_SendBuffer(modbus_03_cmd, pos);
    Usart1_SendBuffer(modbus_03_cmd, pos);
}
/*
01 03 0A 01 C4 01 05 02 32 00 23 00 3C 4A 87
*/
/*
数据解析函数是针对于不同的功能码信息处理
*/

u8 Modbus_Analysis(u8 *buffer, u16 count)
{
    if (count < 5)
    {
        Usart1_SendString("Modbus_Analysis: count < 5\r\n");
        return 0;
    }

    u16 crc_calc = Modbus_CRC16(buffer, count - 2);
    u16 crc_recv = buffer[count - 2] | (buffer[count - 1] << 8);
    if (crc_calc != crc_recv)
    {
        Usart1_SendString("Modbus_Analysis: crc_calc != crc_recv\r\n");
        return 0;
    }
    if (buffer[0] != LOCAL_ADDR)
    {
        Usart1_SendString("Modbus_Analysis: buffer[0] != LOCAL_ADDR\r\n");
        return 0;
    }
    if (buffer[1] != 0x03)
    {
        Usart1_SendString("Modbus_Analysis: EXCEPTION from slave\r\n");
        return 0;
    }
    if (count == 8)
    {
        Usart1_SendString("已收到主机问讯帧\r\n");
        return 2;
    }
    else if (buffer[2] + 5 != count)
    {
        Usart1_SendString("Modbus_Analysis: buffer[2] + 5 != count\r\n");
        return 0;
    }

    u8 data_len = buffer[2] / 2;
    char dht11_data[64] = {0};
    u16 temp_data[2] = 0;
    for (u8 i = 0; i < data_len; i++)
    {

        temp_data[i] = (buffer[3 + i * 2] << 8 | buffer[4 + i * 2]);
    }
    sprintf(dht11_data, "DHT11_data humi:%d.%d\r\nDHT11_data temp:%d.%d\r\n",
            temp_data[0] / 10, temp_data[0] % 10,
            temp_data[1] / 10, temp_data[1] % 10);
    Usart1_SendString(dht11_data);
    Usart2_SendString(dht11_data);
    return 1;
}

uint16_t Modbus_CRC16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF; // 初始值
    size_t i, j;

    for (i = 0; i < length; i++)
    {
        crc ^= data[i]; // 与当前字节异或

        for (j = 0; j < 8; j++)
        {
            if (crc & 0x0001)
            { // 检查最低位
                crc >>= 1;
                crc ^= 0xA001; // 多项式0x8005的逆序
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc; // 返回CRC值（无需取反）
}
