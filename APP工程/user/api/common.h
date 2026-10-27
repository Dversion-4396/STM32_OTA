#ifndef _COMMON_H
#define _COMMON_H
#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define RCC_APB2_AFIO_CLOCK_ENABLE (0x01)
/// gpio 口相关参数
#define RCC_APB2_GPIOA_CLOCK_ENABLE (0X01 << 2)
#define RCC_APB2_GPIOB_CLOCK_ENABLE (0X01 << 3)
#define RCC_APB2_GPIOC_CLOCK_ENABLE (0X01 << 4)
#define RCC_APB2_GPIOD_CLOCK_ENABLE (0X01 << 5)
#define RCC_APB2_GPIOE_CLOCK_ENABLE (0X01 << 6)
#define RCC_APB2_GPIOF_CLOCK_ENABLE (0X01 << 7)
#define RCC_APB2_GPIOG_CLOCK_ENABLE (0X01 << 8)
////i2c相关
#define I2C_SCL_PIN GPIO_Pin_6
#define I2C_SDA_PIN GPIO_Pin_7
#define I2C_GPIO_GROUP GPIOB
/// 定时器相关参数
#define RCC_APB1_TIM2_CLOCK_ENABLE (0x01)
#define RCC_APB1_TIM3_CLOCK_ENABLE (0x01 << 1)
#define RCC_APB1_TIM4_CLOCK_ENABLE (0x01 << 2)
#define RCC_APB1_TIM5_CLOCK_ENABLE (0x01 << 3)
#define RCC_APB1_TIM6_CLOCK_ENABLE (0x01 << 4)
#define RCC_APB1_TIM7_CLOCK_ENABLE (0x01 << 5)
/// 串口相关参数
#define RCC_APB1_USART3_CLOCK_ENABLE (0x01 << 18)
#define RCC_APB2_USART1_CLOCK_ENABLE (0X01 << 14)
#define RCC_APB1_USART2_CLOCK_ENABLE (0X01 << 17)
/// 输出模式相关参数
#define General_Open_Drain_Output_Speed_10MHz (0x05) // 0101
#define General_Open_Drain_Output_Speed_2MHz (0x06)  // 0110
#define General_Open_Drain_Output_Speed_50MHz (0x07) // 0111
#define General_Push_And_Pull_Speed_10MHz (0x01)
#define General_Push_And_Pull_Speed_2MHz (0x02)
#define General_Push_And_Pull_Speed_50MHz (0x03)
#define Alternate_Push_And_Pull_Speed_10MHz (0x09) // 1001
#define Alternate_Push_And_Pull_Speed_2MHz (0x0A)  // 1010
#define Alternate_Push_And_Pull_Speed_50MHz (0x0B) // 1011
#define Floating_Input_Mode (0x04)                 // 0100
#define Pull_Up_Or_Pull_Down_Input_Mode (0x08)

/// 一些宏函数
#define Led0_Toggle (GPIOB->ODR ^= (0x01 << 5))
#define Led1_Toggle (GPIOE->ODR ^= (0x01 << 5))
#define Beep_Toggle (GPIOB->ODR ^= (0x01 << 8))
#define Usart1_Off (USART1->CR1 &= ~(0x01 << 13))
#define Usart1_Open (USART1->CR1 |= (0x01 << 13))
#define Usart2_Off (USART2->CR1 &= ~(0x01 << 13))
#define Usart2_Open (USART2->CR1 |= (0x01 << 13))
#define Usart3_Off (USART3->CR1 &= ~(0x01 << 13))
#define Usart3_Open (USART3->CR1 |= (0x01 << 13))
#define Led_High(GPIO) (GPIO->ODR |= (0x01 << 5)) /* 灭 */
#define Led_Low(GPIO) (GPIO->ODR &= ~(0x01 << 5)) /* 亮 */
#define Beep_On (GPIOB->ODR |= (0x01 << 8))
#define Beep_Off (GPIOB->ODR &= ~(0x01 << 8))
#define Led_Red GPIOB   /* DS0，PB5，低电平点亮 */
#define Led_Green GPIOE /* DS1，PE5，低电平点亮 */
#define OLED_ADDR (0X3C)
#define EEPROM_AT24C02_ADDR (0X50)
#define RS485_GPIO_PIN GPIO_Pin_7
#define RS485_GPIO_GROUP GPIOD
typedef enum
{
    WRITE_FLAG,
    READ_FLAG
} I2C_RW_Flag_t;
/*
因为 I2C 数据传递不稳定，有可能需要多次尝试数据传递，设置最大尝试次数
*/
#define I2C_RETRY_COUNT (3)
/*
I2C 设备响应对应超时时间，时间为 10 ms
*/
#define I2C_TIMEOUT (10)


/*=====================================================================
 * ★★ 以下为 OTA 项目追加内容（原工程内容保持不变）★★
 * 维护：D
 *
 * ★★ 警告：这个文件两个工程（APP / BootLoader）必须完全一致 ★★
 * ★★ 修改前必须全组开会通知，否则 EEPROM 结构体错位，OTA 永远不触发 ★★
 *=====================================================================*/

/*==================== 类型别名（原工程可能没有，补上）====================*/
#ifndef U8_DEFINED
#define U8_DEFINED
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
#endif

/*==================== Flash 分区（★第一天锁死★）====================*/
#define FLASH_BASE_ADDR       0x08000000
#define FLASH_PAGE_SIZE       2048              /* F103ZET6 页大小 2KB */
#define FLASH_TOTAL_PAGES     256               /* 512KB / 2KB */

#define FLASH_BOOT_PAGE_NUM   16                /* BootLoader 占 16 页 */
#define FLASH_BOOT_SIZE       (FLASH_BOOT_PAGE_NUM * FLASH_PAGE_SIZE)   /* 0x8000 = 32KB */

#define FLASH_A_ADDR          (FLASH_BASE_ADDR + FLASH_BOOT_SIZE)       /* 0x08008000 */
#define FLASH_A_START_PAGE    16
#define FLASH_A_PAGE_NUM      240
#define FLASH_A_SIZE          (FLASH_A_PAGE_NUM * FLASH_PAGE_SIZE)      /* 480KB */

/*==================== OTA 状态标志 ====================*/
/* EEPROM 里 OTA_flag == 这个值，表示有新固件待搬移 */
#define BOOT_STA_O            0x5AA5C33C

/*==================== 三元组长度（★硬校验，错一个字节就拒绝★）====================*/
#define PRODUCTKEY_LEN        11                /* ProductKey 必须正好 11 位 */
#define DEVICENAME_LEN        32                /* DeviceName ≤ 32 位 */
#define DEVICESECRET_LEN      32                /* DeviceSecret 必须正好 32 位 */
#define VERSION_LEN           23                /* 版本号必须正好 23 位 */

#define VALID_ALICER_DATA     0xDD              /* valid_cer == 这个值 = 三元组有效 */
#define OTA_URL_LEN           512               /* OTA 下载地址缓冲区长度 */

/*==================== EEPROM 结构体（★两个工程必须完全一致★）====================*/
typedef struct{
    u8  valid_cer;                              /* 0xDD = 有效 */
    u8  ProductKeyBuff[PRODUCTKEY_LEN+1];       /* 12 字节 */
    u8  DeviceNameBuff[DEVICENAME_LEN+1];       /* 33 字节 */
    u8  DeviceSecretBuff[DEVICESECRET_LEN+1];   /* 33 字节 */
    u8  Version_ABuff[VERSION_LEN+1];           /* 24 字节 */
    u32 OTA_firelen;                            /* 固件大小（含 28 字节尾缀） */
    u32 OTA_flag;                               /* 0x5AA5C33C = 需要 OTA */
} AliyunInfo_CB;

#define ALIINFO_STRUCT_LEN    sizeof(AliyunInfo_CB)     /* 共 111 字节 */

/*==================== 系统事件标志 ====================*/
#define CONNECT_START   0x80000000      /* 启动连接服务器 */
#define CONNECT_CMD     0x40000000      /* 连接命令已发，等结果 */
#define CONNECT_EVENT   0x20000000      /* 已连接上服务器 */
#define PING_SENT       0x10000000      /* PING 已发送 */
#define PING_CHECK      0x08000000      /* PING 检查中 */
#define OTA_EVENT       0x04000000      /* ★收到 OTA 推送，需要下载★ */

/*==================== 全局变量（★只在 main.c 里定义，其他文件用 extern★）====================*/
extern AliyunInfo_CB AliInfoCB;                 /* 三元组 + OTA 状态 */
extern u32            SysEventFlag;             /* 系统事件标志 */
extern u8             URL_ABuff[OTA_URL_LEN+1]; /* OTA 下载地址 */
extern u32            ConnectTimer;             /* 连上服务器后的计时（秒） */
extern u32            PingTimer;                /* 上次发 PING 的时间 */

#endif /* _COMMON_H */
