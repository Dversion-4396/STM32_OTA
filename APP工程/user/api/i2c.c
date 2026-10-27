#include "i2c.h"
void I2C_GPIO_Init(void)
{
    ////对gpiob进行使能
    RCC->APB2ENR |= RCC_APB2_GPIOB_CLOCK_ENABLE;
    /// 配置pb6 与pb7
    GPIOB->CRL &= ~(0x0f << 24);
    GPIOB->CRL |= (General_Open_Drain_Output_Speed_2MHz << 24);
    GPIOB->CRL &= ~(0x0f << 28);
    GPIOB->CRL |= (General_Open_Drain_Output_Speed_2MHz << 28);

    /// 3. 利用odr寄存器控制当前gpio引脚输出高电平
    I2C_SCL_PIN_HIGH;
    I2C_SDA_PIN_HIGH;
}
void SCl_Set(u8 flag)
{
    if (flag)
    {
        I2C_SCL_PIN_HIGH;
    }
    else
    {
        I2C_SCL_PIN_LOW;
    }
}

void SDA_Set(u8 flag)
{
    if (flag)
    {
        I2C_SDA_PIN_HIGH;
    }
    else
    {
        I2C_SDA_PIN_LOW;
    }
}

void I2C_Delay(void)
{
    delay_us(I2C_DELAY_TIME);
}

void SDA_Output_Mode(void)
{
    GPIOB->CRL &= ~(0x0F << 28);
    GPIOB->CRL |= General_Open_Drain_Output_Speed_2MHz << 28;
}

void SDA_Input_Mode(void)
{
    GPIOB->CRL &= ~(0x0F << 28);
    GPIOB->CRL |= Floating_Input_Mode << 28;
}

void I2C_Start(void)
{
    ////必须保证sda为输出模式
    SDA_Output_Mode();
    /// 让两条线保持在稳定高电平状态
    SCl_Set(1);
    SDA_Set(1);
    I2C_Delay();
    /// i2c 起始信号让数据线完成跳变
    SDA_Set(0);
    I2C_Delay();
    /// 让时钟线进入无效时钟周期
    SCl_Set(0);
    I2C_Delay();
}

void I2C_Stop(void)
{
    SDA_Output_Mode();
    /// 目前处于应答结束状态要求两条线都为低电平
    SCl_Set(0);
    SDA_Set(0);
    I2C_Delay();
    /// 拉高时钟线进入有效周期
    SCl_Set(1);
    I2C_Delay();
    /// 控制时钟线由低到高完成跳变
    SDA_Set(1);
    I2C_Delay();
    ////结束时两条线都为高电平对应i2c空闲
}

I2C_ACK_t I2C_ReadBit(void)
{
    /// 根据idr引脚状态来进行判断如果 为1 对应返回NACK 如果为0对应返回ACK
    return GPIOB->IDR & (0x01 << 7) ? I2C_NACK : I2C_ACK;
}

u8 I2C_SendByte(u8 data)
{
    /// 在最开始sda必须为输出模式
    SDA_Output_Mode();

    I2C_ACK_t ack = I2C_ACK;
    for (u8 i = 0; i < 8; i++)
    {
        SCl_Set(0);
        I2C_Delay();

        /// 共八位数据 从高位开始传输
        /*  若
            data>>> 1011 0000
            0x80>>> 1000 0000
            则高位为1将数据线变为高电平发送即可

            若
            data>>> 0011 0000
            0x80>>> 1000 0000
            则高位为0将数据先变为低电平发送即可
        */
        if (data & 0x80)
        {
            SDA_Set(1);
            /* code */
        }
        else
        {
            SDA_Set(0);
        }
        /// 随后将scl拉高进入有效电平周期
        SCl_Set(1);
        I2C_Delay();
        /// 发送完成一位数据以后将数据整体左移
        data <<= 1;
    }
    /// 以上操作将八位数据发送到从机 此时需要处理从机应答数据
    ///  发送完成之后 scl处于高电平状态 需要将其拉低
    SCl_Set(0);
    I2C_Delay();

    /// 转换当前sda为输入模式以便接受从机数据
    SDA_Input_Mode();
    I2C_Delay();

    /// 转换完成后把时钟线拉高进入有效电平周期告知从机现在可以进行数据发送
    SCl_Set(1);
    I2C_Delay();

    /// 读取ack数据
    ack = I2C_ReadBit();
    /// 最后进行拉低操作以保证周期完整
    SCl_Set(0);
    I2C_Delay();
    return ack;
}

u8 I2C_ReadByte(I2C_ACK_t ack)
{
    /// 保证sda为输入模式以便接受从机数据
    SDA_Input_Mode();
    u8 data = 0;
    for (u8 i = 0; i < 8; i++)
    {
        /// 首先将时钟线拉低进入无效电平
        SCl_Set(0);
        I2C_Delay();

        /// 将其进行拉高告知从机可以进行数据发送
        SCl_Set(1);
        I2C_Delay();

        /// 把用于存储数据的data在每次接受数据之前都左移一位而低位将存放新的数据
        data <<= 1;
        /*
       通过 I2C_ReadBit() 读取到数据为 1，表示当前从机发送数据为 1
       将数据通过 |= 存储到 data 变量中，如果读取到 0，if 大括号内容
       不再执行
       */
        if (I2C_ReadBit())
        {
            data |= 1;
        }
    }
    ///此时 scl为高电平状态将其拉低以保证完整性
    SCl_Set(0);
    I2C_Delay();

    //主机需要发送信息到从机需要将其调整为 输出模式
    SDA_Output_Mode();

    ///根据用户提供的ack调整sda电平变化
    SDA_Set(ack);
    I2C_Delay();

    ///将scl时钟线拉高 让当前主机应答ack到从机
    SCl_Set(1);
    I2C_Delay();


    ///同上
    SCl_Set(0);
    I2C_Delay();
    return data;
}
