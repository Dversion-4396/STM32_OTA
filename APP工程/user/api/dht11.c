#include "dht11.h"
static GPIO_InitTypeDef GPIO_InitStructure = {0};
void dht11_init(DHG11_NUM num)
{
    switch (num)
    {
    case DHT11_1:
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOE, ENABLE);
        GPIO_InitStructure.GPIO_Pin = DHT11_1_GPIO_PIN;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(DHT11_1_GPIO, &GPIO_InitStructure);
        GPIO_SetBits(DHT11_1_GPIO, DHT11_1_GPIO_PIN);
        break;

    case DHT11_2:
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
        GPIO_InitStructure.GPIO_Pin = DHT11_2_GPIO_PIN;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(DHT11_2_GPIO, &GPIO_InitStructure);
        GPIO_SetBits(DHT11_2_GPIO, DHT11_2_GPIO_PIN);
        break;

    case DHT11_3:
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
        GPIO_InitStructure.GPIO_Pin = DHT11_3_GPIO_PIN;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(DHT11_3_GPIO, &GPIO_InitStructure);
        GPIO_SetBits(DHT11_3_GPIO, DHT11_3_GPIO_PIN);
        break;

    case DHT11_4:
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
        GPIO_InitStructure.GPIO_Pin = DHT11_4_GPIO_PIN;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(DHT11_4_GPIO, &GPIO_InitStructure);
        GPIO_SetBits(DHT11_4_GPIO, DHT11_4_GPIO_PIN);
        break;
    }
}

void dht11_gpio_input_mode(DHG11_NUM num)
{

    switch (num)
    {
    case DHT11_1:
        GPIO_InitStructure.GPIO_Pin = DHT11_1_GPIO_PIN;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(DHT11_1_GPIO, &GPIO_InitStructure);
        break;
    case DHT11_2:
        GPIO_InitStructure.GPIO_Pin = DHT11_2_GPIO_PIN;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(DHT11_2_GPIO, &GPIO_InitStructure);
        break;
    case DHT11_3:
        GPIO_InitStructure.GPIO_Pin = DHT11_3_GPIO_PIN;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(DHT11_3_GPIO, &GPIO_InitStructure);
        break;
    case DHT11_4:
        GPIO_InitStructure.GPIO_Pin = DHT11_4_GPIO_PIN;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
        GPIO_Init(DHT11_4_GPIO, &GPIO_InitStructure);
        break;
    }
}

void dht11_gpio_output_mode(DHG11_NUM num)
{
    switch (num)
    {
    case DHT11_1:
        GPIO_InitStructure.GPIO_Pin = DHT11_1_GPIO_PIN;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(DHT11_1_GPIO, &GPIO_InitStructure);
        GPIO_SetBits(DHT11_1_GPIO, DHT11_1_GPIO_PIN);
        break;
    case DHT11_2:
        GPIO_InitStructure.GPIO_Pin = DHT11_2_GPIO_PIN;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(DHT11_2_GPIO, &GPIO_InitStructure);
        GPIO_SetBits(DHT11_2_GPIO, DHT11_2_GPIO_PIN);
        break;
    case DHT11_3:
        GPIO_InitStructure.GPIO_Pin = DHT11_3_GPIO_PIN;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(DHT11_3_GPIO, &GPIO_InitStructure);
        GPIO_SetBits(DHT11_3_GPIO, DHT11_3_GPIO_PIN);
        break;
    case DHT11_4:
        GPIO_InitStructure.GPIO_Pin = DHT11_4_GPIO_PIN;
        GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
        GPIO_Init(DHT11_4_GPIO, &GPIO_InitStructure);
        GPIO_SetBits(DHT11_4_GPIO, DHT11_4_GPIO_PIN);
        break;
    
    }
}

static void dht11_gpio_low(DHG11_NUM num)
{
    switch (num)
    {
    case DHT11_1:
        GPIO_ResetBits(DHT11_1_GPIO, DHT11_1_GPIO_PIN);
        break;
    case DHT11_2:
        GPIO_ResetBits(DHT11_2_GPIO, DHT11_2_GPIO_PIN);
        break;
    case DHT11_3:
        GPIO_ResetBits(DHT11_3_GPIO, DHT11_3_GPIO_PIN);
        break;
    case DHT11_4:
        GPIO_ResetBits(DHT11_4_GPIO, DHT11_4_GPIO_PIN);
        break;
    default:
        break;
    }
}

static void dht11_gpio_high(DHG11_NUM num)
{
    switch (num)
    {
    case DHT11_1:
        GPIO_SetBits(DHT11_1_GPIO, DHT11_1_GPIO_PIN);
        break;
    case DHT11_2:
        GPIO_SetBits(DHT11_2_GPIO, DHT11_2_GPIO_PIN);
        break;
    case DHT11_3:
        GPIO_SetBits(DHT11_3_GPIO, DHT11_3_GPIO_PIN);
        break;
    case DHT11_4:
        GPIO_SetBits(DHT11_4_GPIO, DHT11_4_GPIO_PIN);
        break;
    default:
        break;
    }
}

void dht11_start_signal(DHG11_NUM num)
{
    dht11_gpio_output_mode(num); /* 切推挽输出，函数内部已置高 */
    dht11_gpio_low(num);         /* 拉低，18ms 以上 */
    delay_ms(35);
    dht11_gpio_high(num); /* 释放为高，20~40us */
    delay_us(35);
    dht11_gpio_input_mode(num); /* 切输入，等 DHT11 响应 */
}
DHT11_STATUS dht11_response(DHG11_NUM num)
{

    u8 timeout = 0;

    while (GPIO_LOW == dht11_gpio_input_value(num))
    {
        delay_us(1);
        timeout += 1;
        if (100 == timeout)
        {
            return DHT11_STATUS_FAILED;
            /* code */
        }
    }
    timeout = 0;
    while (GPIO_HIGH == dht11_gpio_input_value(num))
    {
        delay_us(1);
        timeout += 1;
        if (100 == timeout)
        {
            return DHT11_STATUS_FAILED;
            /* code */
        }
    }
    return DHT11_STATUS_SUCCES;
}

GPIO_STATUS dht11_gpio_input_value(DHG11_NUM num)
{
    switch (num)
    {
    case DHT11_1:
        return GPIO_ReadInputDataBit(DHT11_1_GPIO, DHT11_1_GPIO_PIN) ? GPIO_HIGH : GPIO_LOW;
    case DHT11_2:
        return GPIO_ReadInputDataBit(DHT11_2_GPIO, DHT11_2_GPIO_PIN) ? GPIO_HIGH : GPIO_LOW;
    case DHT11_3:
        return GPIO_ReadInputDataBit(DHT11_3_GPIO, DHT11_3_GPIO_PIN) ? GPIO_HIGH : GPIO_LOW;
    case DHT11_4:
        return GPIO_ReadInputDataBit(DHT11_4_GPIO, DHT11_4_GPIO_PIN) ? GPIO_HIGH : GPIO_LOW;
    default:
        return GPIO_LOW;
    }
}

u8 dht11_read_bit(DHG11_NUM num)
{
    u8 timeout = 0;
    while (GPIO_LOW == dht11_gpio_input_value(num))
    {
        delay_us(1);
        timeout += 1;
        if (DHT11TIMEOUT == timeout)
        {
            return 0;
            /* code */
        }
    }
    delay_us(40);
    if (GPIO_LOW == dht11_gpio_input_value(num))
    {
        return 0;
        /* code */
    }
    else
    {
        timeout = 0;
        while (GPIO_HIGH == dht11_gpio_input_value(num))
        {
            delay_us(1);
            timeout += 1;
            if (DHT11TIMEOUT == timeout)
            {
                return 0;
            }
        }
        return 1;
    }
}

u8 dht11_read_byte(DHG11_NUM num)
{
    u8 byte = 0;
    for (u32 i = 0; i < 8; i++)
    {
        byte <<= 1;
        byte |= dht11_read_bit(num);
    }
    return byte;
}

u8 dht11_read_data(DHT11_Data_TypeDef *DHT11_Data_Struct, DHG11_NUM num)
{
    dht11_start_signal(num);
    if (dht11_response(num))
    {
        return DHT11_STATUS_FAILED;
    }
    u8 dht11_data[5] = {0};
    for (u32 i = 0; i < 5; i++)
    {
        dht11_data[i] = dht11_read_byte(num);
    }

    if (dht11_data[0] + dht11_data[1] + dht11_data[2] + dht11_data[3] == dht11_data[4])
    {
        DHT11_Data_Struct->huml_mantissa = dht11_data[0];
        DHT11_Data_Struct->huml_fraction = dht11_data[1];
        DHT11_Data_Struct->temp_mantissa = dht11_data[2];
        DHT11_Data_Struct->temp_fraction = dht11_data[3];
    }
    else
    {
        return DHT11_STATUS_FAILED;
    }
    return DHT11_STATUS_SUCCES;
}
