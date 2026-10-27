#ifndef _DHT11_H
#define _DHT11_H
#include "common.h"
#include "delay.h"
#define DHT11_1_GPIO GPIOE
#define DHT11_2_GPIO GPIOD
#define DHT11_3_GPIO GPIOD
#define DHT11_4_GPIO GPIOD
#define DHT11_1_GPIO_PIN GPIO_Pin_0
#define DHT11_2_GPIO_PIN GPIO_Pin_11
#define DHT11_3_GPIO_PIN GPIO_Pin_12
#define DHT11_4_GPIO_PIN GPIO_Pin_13

#define DHT11TIMEOUT (100)
typedef struct
{
    u8 huml_mantissa;
    u8 huml_fraction;
    u8 temp_mantissa;
    u8 temp_fraction;
} DHT11_Data_TypeDef;

typedef enum
{
    DHT11_STATUS_SUCCES,
    DHT11_STATUS_FAILED,
} DHT11_STATUS;
typedef enum
{
    GPIO_LOW,
    GPIO_HIGH,
} GPIO_STATUS;
typedef enum
{
    DHT11_1,
    DHT11_2,
    DHT11_3,
    DHT11_4,
} DHG11_NUM;
/// @brief 初始化函数
/// @param num 选择对应的dht11编号
void dht11_init(DHG11_NUM num);
/// @brief 控制dht11为输入模式同时设置为无拉模式
void dht11_gpio_input_mode(DHG11_NUM num);
/// @brief 控制dht11为输出模式同时设置为上拉模式
/// @param
void dht11_gpio_output_mode(DHG11_NUM num);
/// @brief mcu向dht111发送起始信号
void dht11_start_signal(DHG11_NUM num);
/// @brief 读取相应信号
/// @return 相应成功返回DHT11_STATUS_SUCCES,失败返回DHT11_STATUS_FAILED,
DHT11_STATUS dht11_response(DHG11_NUM num);

GPIO_STATUS dht11_gpio_input_value(DHG11_NUM num);
u8 dht11_read_bit(DHG11_NUM num);
u8 dht11_read_byte(DHG11_NUM num);
u8 dht11_read_data(DHT11_Data_TypeDef *DHT11_Data_Struct,DHG11_NUM num);
#endif