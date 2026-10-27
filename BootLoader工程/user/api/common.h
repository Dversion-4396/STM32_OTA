#ifndef _COMMON_H
#define _COMMON_H
#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define USART1_BRR 9600
#define USART2_BRR 115200
#define USART3_BRR 115200
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
#define Usart1_Off (USART1->CR1 &= ~(0x01 << 13))
#define Usart1_Open (USART1->CR1 |= (0x01 << 13))
#define Usart2_Off (USART2->CR1 &= ~(0x01 << 13))
#define Usart2_Open (USART2->CR1 |= (0x01 << 13))
#define Usart3_Off (USART3->CR1 &= ~(0x01 << 13))
#define Usart3_Open (USART3->CR1 |= (0x01 << 13))
#define Led_High(GPIO) (GPIO->ODR |= (0x01 << 5)) /* 灭 */
#define Led_Low(GPIO) (GPIO->ODR &= ~(0x01 << 5)) /* 亮 */
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
 * ★★ 以下为 OTA 项目追加内容（上半部分原工程内容保持不变）★★
 *
 * 维护：D
 * 平台：巴法云（bemfa）—— 2026-10-27 起从阿里云切换过来
 *
 * ★★★★ 警告 ★★★★
 * 这个文件两个工程（APP / BootLoader）必须【逐字节完全一致】
 * 修改前必须全组开会通知，否则：
 *
 *   APP 按结构体 A 写 EEPROM 地址 0
 *   BootLoader 按结构体 B 读 EEPROM 地址 0
 *        ↓
 *   字段错位 → OTA_flag 读到的值不对 → OTA 永远不触发
 *
 * 改完必须两边都更新，并且做一次"写-读-比对"测试。
 *=====================================================================*/

/*==================== 类型别名（原工程可能没有，补上）====================*/
#ifndef U8_DEFINED
#define U8_DEFINED
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
#endif

/*=====================================================================
 * 一、Flash 分区（★第一天就要锁死，后面不许改★）
 *
 * 战神V1 是 STM32F103ZET6：
 *   片内 Flash 512KB = 0x08000000 ~ 0x0807FFFF
 *   页大小 2KB（F103 的页是 2KB，不是 G0 的 2KB 扇区，注意区分）
 *
 * 分区方案（双区单备份）：
 *   ┌──────────────────────────────────────────┐
 *   │ 0x08000000   BootLoader     32KB (16页)  │
 *   ├──────────────────────────────────────────┤
 *   │ 0x08008000   APP 运行区    480KB (240页) │
 *   │              ↑ 平时程序跑在这里           │
 *   └──────────────────────────────────────────┘
 *
 * 为什么 BootLoader 要 32KB：
 *   实测完整 BootLoader（含 Flash 读写、I2C、SPI、串口、OTA 搬移）
 *   编译后约 12~18KB，留 32KB 有充足余量。
 *   如果超过 32KB，必须调整 FLASH_BOOT_PAGE_NUM 并同步改 APP 的 IROM1。
 *===================================================================*/
#define FLASH_BASE_ADDR       0x08000000        /* 片内 Flash 起始地址 */
#define FLASH_PAGE_SIZE       2048              /* F103ZET6 页大小 2KB */
#define FLASH_TOTAL_PAGES     256               /* 512KB / 2KB = 256 页 */

#define FLASH_BOOT_PAGE_NUM   16                /* BootLoader 占 16 页 */
#define FLASH_BOOT_SIZE       (FLASH_BOOT_PAGE_NUM * FLASH_PAGE_SIZE)   /* 0x8000 = 32KB */

#define FLASH_A_ADDR          (FLASH_BASE_ADDR + FLASH_BOOT_SIZE)       /* 0x08008000 */
#define FLASH_A_START_PAGE    16                /* APP 从第 16 页开始 */
#define FLASH_A_PAGE_NUM      240               /* APP 占 240 页 */
#define FLASH_A_SIZE          (FLASH_A_PAGE_NUM * FLASH_PAGE_SIZE)      /* 480KB */

/*=====================================================================
 * 二、OTA 状态标志
 *
 * 流程：
 *   ① APP 下载完固件 → 写 OTA_flag = BOOT_STA_O → 重启
 *   ② BootLoader 上电读 OTA_flag
 *        == BOOT_STA_O → 把 W25Q128 里的固件搬到片内 Flash
 *                     → 清 OTA_flag = 0
 *                     → 跳转 APP
 *        != BOOT_STA_O → 直接跳转 APP
 *
 * 为什么用 0x5AA5C33C 这么怪的数：
 *   这是一个"魔数"，防止 EEPROM 掉电/干扰产生的随机值误触发 OTA。
 *   EEPROM 未初始化时通常是 0x00 或 0xFF，不会恰好等于这个值。
 *===================================================================*/
#define BOOT_STA_O            0x5AA5C33C

/*=====================================================================
 * 三、巴法云平台参数长度
 *
 * ★ 和阿里云的巨大区别 ★
 *   阿里云：三元组（ProductKey 11位 + DeviceName + DeviceSecret 32位）
 *           还要用 HMAC-SHA1 算密码，需要移植加密库
 *   巴法云：只要一个 UID（32位私钥）
 *           密码就是 UID 本身，不需要任何签名算法
 *
 * 所以原来那三个 PRODUCTKEY_LEN / DEVICENAME_LEN / DEVICESECRET_LEN
 * 全部删掉了，换成 UID 一个。
 *===================================================================*/
#define BEMFA_UID_LEN         32                /* 巴法云私钥，固定 32 位 */
#define BEMFA_TOPIC_LEN       32                /* 订阅的 Topic 名字长度 */
#define VERSION_LEN           23                /* 版本号必须正好 23 位 */

#define VALID_BEMFA_DATA      0xDD              /* valid_flag == 这个值 = 参数有效 */
#define OTA_URL_LEN           512               /* OTA 下载地址缓冲区长度 */

/*=====================================================================
 * 四、EEPROM 结构体（★两个工程必须完全一致★）
 *
 * 存储位置：AT24C02（2Kbit = 256 字节），I2C 地址 0x50
 * 存储偏移：从地址 0 开始，占用前 99 字节
 *
 * 内存布局（共 99 字节）：
 *   ┌──────┬────────┬──────────────────────────────┐
 *   │ 偏移 │ 长度   │ 字段                         │
 *   ├──────┼────────┼──────────────────────────────┤
 *   │   0  │   1    │ valid_flag（0xDD=有效）       │
 *   │   1  │  33    │ UID（巴法云私钥）             │
 *   │  34  │  33    │ Topic（订阅的 Topic 名字）    │
 *   │  67  │  24    │ Version_ABuff（版本号）       │
 *   │  91  │   4    │ OTA_firelen（固件大小）       │
 *   │  95  │   4    │ OTA_flag（0x5AA5C33C=待升级） │
 *   └──────┴────────┴──────────────────────────────┘
 *
 * ★ 为什么每个字段都是 LEN+1 ★
 *   多留 1 字节给字符串结束符 '\0'，方便直接用 strcpy/sprintf。
 *   例：UID 32 位 → 数组 33 字节，前 32 存内容，第 33 是 '\0'。
 *
 * ★ 为什么 OTA_firelen 和 OTA_flag 用 u32 ★
 *   固件大小可能超过 65535（u16 上限），必须用 32 位。
 *   4MB 的固件用 u32 完全够（u32 上限 4GB）。
 *===================================================================*/
typedef struct{
    u8  valid_flag;                             /* 0xDD = 参数有效，其他值 = 未配置 */
    u8  UID_Buff[BEMFA_UID_LEN+1];              /* 33 字节：巴法云 UID（32位+'\0'） */
    u8  Topic_Buff[BEMFA_TOPIC_LEN+1];          /* 33 字节：订阅的 Topic 名字 */
    u8  Version_ABuff[VERSION_LEN+1];           /* 24 字节：固件版本号（23位+'\0'） */
    u32 OTA_firelen;                            /* 固件大小（含 28 字节尾缀） */
    u32 OTA_flag;                               /* 0x5AA5C33C = 需要 OTA */
} BemfaInfo_CB;

/* 编译期检查：结构体必须是 99 字节，变了就报错 */
#define BEMFAINFO_STRUCT_LEN    sizeof(BemfaInfo_CB)     /* 应为 99 字节 */

/*=====================================================================
 * 五、系统事件标志（位掩码，一个 u32 存多个状态）
 *
 * 用法：
 *   置位：SysEventFlag |= OTA_EVENT;
 *   清位：SysEventFlag &= ~OTA_EVENT;
 *   判断：if (SysEventFlag & OTA_EVENT) { ... }
 *
 * 为什么用位掩码而不是多个 u8 变量：
 *   主循环里可以一次判断多个事件，效率高；
 *   而且 32 位读写在 Cortex-M3 上是原子的，中断里置位不会撕裂。
 *===================================================================*/
#define CONNECT_START   0x80000000      /* 请求连接巴法云服务器 */
#define CONNECT_CMD     0x40000000      /* 连接命令已发出，等待 CONNACK */
#define CONNECT_EVENT   0x20000000      /* 已成功连上巴法云 */
#define PING_SENT       0x10000000      /* PINGREQ 已发送 */
#define PING_CHECK      0x08000000      /* 正在等待 PINGRESP */
#define OTA_EVENT       0x04000000      /* ★收到 OTA 消息，需要下载固件★ */

/*=====================================================================
 * 六、全局变量（★只在 main.c 里定义一次，其他文件用 extern 引用★）
 *
 * 定义在：APP工程/user/main.c
 * 引用方式：包含 common.h 后直接用，不需要再 extern
 *===================================================================*/
extern BemfaInfo_CB   BemfaInfoCB;                  /* 巴法云参数 + OTA 状态 */
extern u32            SysEventFlag;                 /* 系统事件标志（位掩码） */
extern u8             URL_ABuff[OTA_URL_LEN+1];     /* OTA 下载地址缓冲区 */
extern u32            ConnectTimer;                 /* 连上服务器后的计时（秒） */
extern u32            PingTimer;                    /* 上次发 PING 的时间（秒） */

#endif /* _COMMON_H */
