#include "eeprom.h"
#include "usart.h"

u8 EEPROM_WriteByte(u8 eeprom_addr, u8 reg_addr, u8 data)
{
    u8 retry = I2C_RETRY_COUNT;
    u8 timeout = I2C_TIMEOUT;
    while (retry--)
    {
        /*
        数据帧组成
                1. i2c初始信号 i2c_start
                2. i2c_SendByte 发送设备地址和读写标志位 同时处理ack应答
                3. i2c_SendByte 目标写入寄存器地址同时处理ack应答
                4, i2c_SendByte 写入到存储及的一字节数据 同时处理ack应答
                5. i2c 停止信号 i2c_stop
        */
        /// 1.初始信号
        I2C_Start();

        /// 2.发送设备地址同时处理ack应答
        ////
        if (I2C_SendByte(eeprom_addr << 1 | WRITE_FLAG))
        {
            /*
                 若应答失败直接停止后续操作同时跳出本次循环
            */
            I2C_Stop();
            continue;
        }

        // 3. I2C_SendByte 目标写入寄存器地址，同时处理 ACK 应答
        if (I2C_SendByte(reg_addr))
        {
            I2C_Stop();
            continue;
        }

        // 4. I2C_SendByte 写入到存储器的 1 字节数据，同时处理 ACK 应答
        if (I2C_SendByte(data))
        {
            I2C_Stop();
            continue;
        }
        // 5. 停止
        I2C_Stop();
        /*
           以上操作是将数据按照 I2C 协议帧格式要求写入到 EEPROM AT24C02 芯片中。
           【EEPROM AT24C02 进入冥想模式】
           存储芯片需要一定的时间来处理刚刚收到的数据内容，大概时间在 5ms 左右
           在当前时间段内容，EEPROM AT24C02 芯片不会响应外部任何操作。
           当前处理完成之后，EEPROM AT24C02 芯片才会响应外部其他操作。
           可以利用当前特征判断数据是否有效写入到存储芯片中。
       */
        while (timeout--)
        {

            /*
                 利用 I2C 设备寻址操作，要求对应芯片进行应答
            */
            I2C_Start();

            /*
            发送设备地址 + 读写标志位到 I2C 从机设备
            如果设备应答为 NACK(1) 表示当前设备在处理数据过程中
            如果设备应答为 ACK(0) 表示数据已完成处理。
            */
            if (!I2C_SendByte(eeprom_addr << 1 | WRITE_FLAG))
            {
                I2C_Stop();
                return I2C_ACK;
            }

            /*
            I2C 停止信号，准备进入下一次操作
            */
            I2C_Stop();
            delay_ms(1);
        }
        timeout=I2C_TIMEOUT;
    }
    return I2C_NACK;
}

u8 EEPROM_ReadByte(u8 eeprom_addr, u8 reg_addr)
{
    u8 retry = I2C_RETRY_COUNT;
    u8 data = 0;
    while (retry--)
    {
        /*
        数据帧组成
            1. I2C 起始信号，I2C_Start
            2. I2Cw_SendByte 发送设备地址 + 写入标志，同时处理 ACK 应答
            3. I2C_SendByte 目标读取数据寄存器地址，同时处理 ACK 应答
            4. I2C 起始信号，I2C_Start
            5. I2C_SendByte 发送设备地址 + 读取标志，同时处理 ACK 应答
            6. I2C_ReadByte 读取从机发送的数据内容，同时主机通过 ACK 应答告知
                    从机是否继续发送。
            7. I2C 停止信号，I2C_Stop
        */

        // 1. I2C 起始信号，I2C_Start
        I2C_Start();

        // 2. I2C_SendByte 发送设备地址 + 写入标志，同时处理 ACK 应答
        if (I2C_SendByte(eeprom_addr << 1 | WRITE_FLAG))
        {
            I2C_Stop();
            continue;
        }

        // 3. I2C_SendByte 目标读取数据寄存器地址，同时处理 ACK 应答
        if (I2C_SendByte(reg_addr))
        {
            I2C_Stop();
            continue;
        }

        // 4. I2C 起始信号，I2C_Start
        I2C_Start();

        // 5. I2C_SendByte 发送设备地址 + 读取标志，同时处理 ACK 应答
        if (I2C_SendByte(eeprom_addr << 1 | READ_FLAG))
        {
            I2C_Stop();
            continue;
        }

        /*
        6. I2C_ReadByte 读取从机发送的数据内容，同时主机通过 ACK 应答告知
            从机是否继续发送。
        */
        data = I2C_ReadByte(I2C_NACK);

        // 7. I2C 停止信号，I2C_Stop
        I2C_Stop();

        return data;
    }
    return 0;
    
}

/*
    从 AT24C02 读出整个 BemfaInfoCB（99 字节）
 */
u8 EEPROM_ReadInfo(void)
{
    u8 i;
    u8 *p = (u8 *)&BemfaInfoCB;
    memset(&BemfaInfoCB, 0, sizeof(BemfaInfoCB));
    for (i = 0; i < sizeof(BemfaInfoCB); i++)
    {
        p[i] = EEPROM_ReadByte(EEPROM_AT24C02_ADDR, i);
        delay_ms(5);
    }

    /* 判断是否为首次启动 */
    if (BemfaInfoCB.valid_flag != VALID_BEMFA_DATA) /* 0xDD */
    {
        memset(&BemfaInfoCB, 0, sizeof(BemfaInfoCB));
        BemfaInfoCB.valid_flag = 0;
        Usart1_SendString("[EEPROM] 参数无效，需要重新配置\r\n");
        return 1;
    }

    Usart1_SendString("[EEPROM] 参数有效\r\n");
    return 0;
}

/*
  把整个 BemfaInfoCB（99 字节）写进 AT24C02
*/
u8 EEPROM_SaveInfo(void)
{
    u8 i;
    u8 *p = (u8 *)&BemfaInfoCB;
    for (i = 0; i < sizeof(BemfaInfoCB); i++)
    {
        if (EEPROM_WriteByte(EEPROM_AT24C02_ADDR, i, p[i]) != I2C_ACK)
        {
            return 1;
        }
        delay_ms(5);
    }
    Usart1_SendString("EEPROM SAVE INFO DONE !!!\r\n");
    return 0;
}
