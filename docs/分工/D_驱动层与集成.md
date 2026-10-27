# 【成员 D】补 SPI 驱动 + 集成 + 测试

> 项目：战神V1（F103ZET6）+ 移柯 L511-2 + 阿里云 OTA
> 基础工程：`E:\MY_project\STM32F103ZET6_Demo10DMA`（裸机版）
> 你的角色：**关键路径 + 集成负责人**
> 工期：11 天（比原来省 6.5 天）| 难度：★★★

---

# 目录

1. [你负责什么](#一你负责什么)
2. [为什么你轻松了](#二为什么你轻松了)
3. [第 0 周：SPI 驱动（最关键）](#三第-0-周spi-驱动最关键)
4. [第 1 周：验证 + 建工程](#四第-1-周验证--建工程)
5. [第 2-3 周：集成](#五第-2-3-周集成)
6. [第 4 周：测试 + 文档](#六第-4-周测试--文档)
7. [任务总表](#七任务总表)
8. [排错手册](#八排错手册)
9. [答辩准备](#九答辩准备)

---

# 一、你负责什么

**补上唯一的缺口（SPI 驱动），然后把所有人的代码集成到一起，保证能跑、能测。**

## 3 个阶段

| 阶段 | 目标 | 判断标准 |
|---|---|---|
| ① SPI | 写 SPI2 + W25Q128 驱动 | 读 ID = `0xEF4018` |
| ② 集成 | 合并 A/B/C 的代码 | 能编译、能运行 |
| ③ 测试 | 6 类场景 | 全部通过 |

---

# 二、为什么你轻松了

## 2.1 你已经有的（不用重写）

| 模块 | 文件 | 你能用的函数 |
|---|---|---|
| **USART1/2/3** | `user/api/usart.c` | `Usart1_Init()` / `Usart3_Init()` / `Usart*_SendString()` |
| **接收缓冲** | `user/api/usart.h` | `usart1_value` / `usart3_value`（data + count + flag） |
| **I2C** | `user/api/i2c.c` | `I2C_GPIO_Init()` / `I2C_Start()` / `I2C_SendByte()` / `I2C_ReadByte()` |
| **EEPROM** | `user/api/eeprom.c` | `EEPROM_WriteByte()` / `EEPROM_ReadByte()` |
| **LED** | `user/api/led.c` | `Led_Low()` / `Led_High()` / `Led0_Toggle` |
| **按键** | `user/api/key.c` | 按键扫描 |
| **蜂鸣器** | `user/api/beep.c` | `Beep_On` / `Beep_Off` |
| **延时** | `user/api/delay.c` | `delay_ms()` / `delay_us()` |
| **公共宏** | `user/api/common.h` | 时钟使能、GPIO 模式等 |

**省了大约 6.5 天。**

## 2.2 你唯一要补的：SPI

**两个工程都没有 SPI 驱动**，而 OTA 必须用 SPI 访问 W25Q128。

**但这是最简单的任务**，因为标准库有现成例程：

```
E:\桌面\板子\STM开发资料\STM32F10x_StdPeriph_Lib_V3.5.0\
    Project\STM32F10x_StdPeriph_Examples\SPI\SPI_FLASH\
```

里面有完整的 `main.c`，改引脚就能用。

---

# 三、第 0 周：SPI 驱动（最关键）

## 3.1 硬件连接（战神V1）

| 信号 | 引脚 | 说明 |
|---|---|---|
| **CS** | **PB12** | 片选 |
| SCK | PB13 | 时钟 |
| MISO | PB14 | 主入从出 |
| MOSI | PB15 | 主出从入 |

### ⚠️ 三个硬件注意事项

**1. PB12 不能当普通 IO**

它接 W25Q128 片选。**悬空或误拉低会让 W25Q128 一直占线，SPI2 报废。**

**2. SPI2 被两个器件共用**

- W25Q128（CS = PB12）
- NRF24L01 无线模块（CS = PG7，在 P6 座）

本项目不用 NRF，但要注意**不要同时操作**。

**3. 上拉电阻**

板子上 PB12 可能没有上拉。如果读 ID 不稳定，在 PB12 加个 10K 上拉到 3.3V。

## 3.2 芯片参数（W25Q128）

| 参数 | 值 |
|---|---|
| 容量 | 16 MB（128 Mbit） |
| 页大小 | **256 字节** |
| 扇区大小 | **4 KB** |
| 块大小 | **64 KB** |
| JEDEC ID | **`0xEF4018`**（Winbond） |
| SPI 模式 | Mode 0 或 Mode 3 |

## 3.3 要实现的 7 个函数

```c
/* 初始化 */
void SPI2_Init(void);
u8   W25Q128_Init(void);                    /* 检查 ID，0=成功 */

/* 基础操作 */
u32  W25Q128_ReadID(void);                  /* 应返回 0xEF4018 */
void W25Q128_WaitBusy(void);                /* 轮询 BUSY 位 */
void W25Q128_WriteEnable(void);             /* 每次写/擦前必发 */

/* 读写擦 */
void W25Q128_EraseSector(u16 sector);       /* 擦 4KB */
void W25Q128_EraseBlock(u8 block);          /* 擦 64KB */
void W25Q128_WritePage(u16 page, u8 *buf);  /* 写 256B，不跨页 */
void W25Q128_Read(u32 addr, u8 *buf, u32 len);   /* 读任意长度 */
```

## 3.4 指令表（W25Q128 数据手册）

| 指令 | 代码 | 功能 |
|---|---|---|
| Write Enable | `0x06` | 写使能（每次写/擦前必发） |
| Read Status | `0x05` | 读状态寄存器（bit0 = BUSY） |
| Read Data | `0x03` | 读数据 |
| Page Program | `0x02` | 写一页（256B） |
| Sector Erase | `0x20` | 擦扇区（4KB） |
| Block Erase | `0xD8` | 擦块（64KB） |
| JEDEC ID | `0x9F` | 读 ID |

## 3.5 初始化要点

### GPIO 配置

```c
/* 1. 开时钟 */
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);   /* ⚠️ SPI2 在 APB1 */

/* 2. PB13/14/15 = 复用推挽 */
gpio.GPIO_Pin   = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
gpio.GPIO_Speed = GPIO_Speed_50MHz;
GPIO_Init(GPIOB, &gpio);

/* 3. PB12 = CS，推挽输出，默认拉高 */
gpio.GPIO_Pin  = GPIO_Pin_12;
gpio.GPIO_Mode = GPIO_Mode_Out_PP;
GPIO_Init(GPIOB, &gpio);
GPIO_SetBits(GPIOB, GPIO_Pin_12);
```

### SPI 参数

```c
spi.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
spi.SPI_Mode              = SPI_Mode_Master;
spi.SPI_DataSize          = SPI_DataSize_8b;
spi.SPI_CPOL              = SPI_CPOL_High;              /* Mode 3 */
spi.SPI_CPHA              = SPI_CPHA_2Edge;             /* Mode 3 */
spi.SPI_NSS               = SPI_NSS_Soft;               /* 软件 NSS */
spi.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;    /* 36M/4 = 9MHz */
spi.SPI_FirstBit          = SPI_FirstBit_MSB;
spi.SPI_CRCPolynomial     = 7;
SPI_Init(SPI2, &spi);
SPI_Cmd(SPI2, ENABLE);
```

### ⚠️ 时钟频率怎么算

**SPI2 挂在 APB1 上，APB1 = 36MHz**（不是 72MHz）。

```
36MHz / 4 = 9MHz
```

W25Q128 最高支持 104MHz，9MHz 完全够用，也稳定。

**如果想快**：`SPI_BaudRatePrescaler_2` → 18MHz，也可以。

## 3.6 核心函数实现要点

### 收发一个字节

```c
u8 SPI2_ReadWriteByte(u8 data)
{
    /* 等发送缓冲空 */
    while(SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET);

    /* 发送 */
    SPI_I2S_SendData(SPI2, data);

    /* 等接收缓冲非空 */
    while(SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE) == RESET);

    /* 读回 */
    return SPI_I2S_ReceiveData(SPI2);
}
```

### 读 ID

```c
u32 W25Q128_ReadID(void)
{
    u32 id = 0;

    W25Q_CS_LOW();
    SPI2_ReadWriteByte(0x9F);                    /* JEDEC ID 指令 */
    id  = (u32)SPI2_ReadWriteByte(0xFF) << 16;   /* Manufacturer */
    id |= (u32)SPI2_ReadWriteByte(0xFF) << 8;    /* Memory Type */
    id |= (u32)SPI2_ReadWriteByte(0xFF);         /* Capacity */
    W25Q_CS_HIGH();

    return id;
}
```

### 等待空闲

```c
void W25Q128_WaitBusy(void)
{
    u8 status;

    W25Q_CS_LOW();
    SPI2_ReadWriteByte(0x05);        /* 读状态寄存器 */

    do{
        status = SPI2_ReadWriteByte(0xFF);
    }while((status & 0x01) == 0x01); /* bit0 = BUSY */

    W25Q_CS_HIGH();
}
```

### 擦除扇区

```c
void W25Q128_EraseSector(u16 sector)
{
    u32 addr = (u32)sector * 4096;

    W25Q128_WriteEnable();           /* ⚠️ 必须先写使能 */

    W25Q_CS_LOW();
    SPI2_ReadWriteByte(0x20);        /* Sector Erase */
    SPI2_ReadWriteByte(addr >> 16);  /* 24 位地址 */
    SPI2_ReadWriteByte(addr >> 8);
    SPI2_ReadWriteByte(addr);
    W25Q_CS_HIGH();

    W25Q128_WaitBusy();              /* ⚠️ 必须等擦完 */
}
```

### 写一页

```c
void W25Q128_WritePage(u16 page, u8 *buf)
{
    u16 i;
    u32 addr = (u32)page * 256;

    W25Q128_WriteEnable();

    W25Q_CS_LOW();
    SPI2_ReadWriteByte(0x02);        /* Page Program */
    SPI2_ReadWriteByte(addr >> 16);
    SPI2_ReadWriteByte(addr >> 8);
    SPI2_ReadWriteByte(addr);

    for(i = 0; i < 256; i++){
        SPI2_ReadWriteByte(buf[i]);
    }

    W25Q_CS_HIGH();
    W25Q128_WaitBusy();
}
```

### 读数据

```c
void W25Q128_Read(u32 addr, u8 *buf, u32 len)
{
    u32 i;

    W25Q_CS_LOW();
    SPI2_ReadWriteByte(0x03);        /* Read Data */
    SPI2_ReadWriteByte(addr >> 16);
    SPI2_ReadWriteByte(addr >> 8);
    SPI2_ReadWriteByte(addr);

    for(i = 0; i < len; i++){
        buf[i] = SPI2_ReadWriteByte(0xFF);   /* 发 0xFF 是占位 */
    }

    W25Q_CS_HIGH();
}
```

## 3.7 ⚠️ 跨扇区写入的陷阱（重要）

### 问题描述

W25Q128 擦除最小单位是 **4KB 扇区**。

如果写数据跨越扇区边界：

```
假设从 addr=4000 开始写 200 字节：
  4000 在扇区 0（0~4095）
  4000+200 = 4200 在扇区 1（4096~8191）
  → 跨越了两个扇区
```

**错误做法**：每写一段就擦当前扇区

```
第 1 次：写 4000~4095（96 字节），擦扇区 0
第 2 次：写 4096~4200（104 字节），擦扇区 1
        ↑ 这一步没问题，但如果反过来先擦扇区 1 再写扇区 0，就会把刚写的擦掉
```

### 正确做法（推荐）

**一次性擦除整个目标范围，然后再写。**

```c
/**
 * 写数据（调用前必须已擦除目标区域）
 */
u8 W25Q128_Write(u32 addr, u8 *buf, u32 len)
{
    u32 offset = 0;
    u32 page_addr, page_offset, write_len;
    u8  page_buf[256];
    u16 i;

    while(offset < len){
        page_addr   = addr + offset;
        page_offset = page_addr % 256;
        page_addr   = page_addr - page_offset;      /* 页对齐 */

        write_len = 256 - page_offset;
        if(write_len > (len - offset)){
            write_len = len - offset;
        }

        if(write_len < 256){
            /* 不满一页：先读整页，改数据，再写回 */
            W25Q128_Read(page_addr, page_buf, 256);

            for(i = 0; i < write_len; i++){
                page_buf[page_offset + i] = buf[offset + i];
            }

            W25Q128_WritePage(page_addr / 256, page_buf);
        }else{
            /* 整页写 */
            W25Q128_WritePage(page_addr / 256, &buf[offset]);
        }

        offset += write_len;
    }

    return 0;
}
```

> **注意**：这个函数假设**目标区域已经擦除**。
> 调用前先 `W25Q128_EraseBlock()` 或 `W25Q128_EraseSector()`。

## 3.8 自测函数

```c
/**
 * 自测：读 ID + 写读校验
 * @return 0=全部通过
 */
u8 W25Q128_SelfTest(void)
{
    u32 id;
    u8  wbuf[16] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,
                    0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,0x00};
    u8  rbuf[16];
    u8  i;

    Usart1_SendString("\r\n===== W25Q128 自测 =====\r\n");

    /* 1. 读 ID */
    id = W25Q128_ReadID();
    Usart1_SendString("JEDEC ID：0x");
    Usart1_SendHex(id);
    Usart1_SendString("\r\n");

    if(id != 0xEF4018){
        Usart1_SendString("ID 错误！应为 0xEF4018\r\n");
        return 1;
    }
    Usart1_SendString("ID 正确 ✓\r\n");

    /* 2. 擦除扇区 0 */
    W25Q128_EraseSector(0);
    Usart1_SendString("擦除完成\r\n");

    /* 3. 验证擦除后全 FF */
    W25Q128_Read(0, rbuf, 16);
    for(i = 0; i < 16; i++){
        if(rbuf[i] != 0xFF){
            Usart1_SendString("擦除后不是全 FF ✗\r\n");
            return 2;
        }
    }
    Usart1_SendString("擦除后全 FF ✓\r\n");

    /* 4. 写入 */
    W25Q128_Write(0, wbuf, 16);
    Usart1_SendString("写入完成\r\n");

    /* 5. 读回比对 */
    W25Q128_Read(0, rbuf, 16);
    for(i = 0; i < 16; i++){
        if(rbuf[i] != wbuf[i]){
            Usart1_SendString("读回不一致 ✗\r\n");
            return 3;
        }
    }
    Usart1_SendString("读回一致 ✓\r\n");

    /* 6. 清理 */
    W25Q128_EraseSector(0);

    Usart1_SendString("自测全部通过 ✓✓✓\r\n");
    Usart1_SendString("========================\r\n\r\n");

    return 0;
}
```

**期望输出**：
```
===== W25Q128 自测 =====
JEDEC ID：0xEF4018
ID 正确 ✓
擦除完成
擦除后全 FF ✓
写入完成
读回一致 ✓
自测全部通过 ✓✓✓
========================
```

---

# 四、第 1 周：验证 + 建工程

## 4.1 修串口隐患（Day 3，0.5 天）

### 隐患 1：缓冲改大

`user/api/usart.h`：

```c
/* 改前 */
#define USART3_BUFFER_SIZE (256)

/* 改后 */
#define USART3_BUFFER_SIZE (1024)
```

**为什么**：HTTP 下载一次返回约 274 字节（16 字节头 + 256 数据 + 2 字节尾），256 会截断。

### 隐患 2：中断去业务逻辑

`user/api/usart.c` 的 `USART3_IRQHandler`，现在里面有 PWM、蜂鸣器、LED 处理。

**改成只收数据置 flag**：

```c
void USART3_IRQHandler(void)
{
    /* RXNE：收一个字节 */
    if (USART3->SR & (0x01 << 5)){
        if(usart3_value.count < USART3_BUFFER_SIZE){
            usart3_value.data[usart3_value.count++] = USART3->DR;
        }else{
            USART3->DR;
        }
    }

    /* IDLE：一帧收完 */
    if (USART3->SR & (0x01 << 4)){
        USART3->DR;
        usart3_value.flag = 1;      /* 只置标志 */
    }
}
```

> 业务逻辑移到主循环处理。

## 4.2 建 APP 工程（Day 4，1 天）

### 步骤

**Step 1：复制工程**
```
把 E:\MY_project\STM32F103ZET6_Demo10DMA
复制到 E:\MY_project\STM32F103ZET6_OTA_APP
```

**Step 2：改 IROM1**

Options for Target → Target 标签：

| 项 | 改成 |
|---|---|
| IROM1 Start | `0x08008000` |
| IROM1 Size | **`0x78000`**（480KB） |

**Step 3：改 VECT_TAB_OFFSET**

打开 `libraries/CMSIS/.../system_stm32f10x.c`：

```c
/* 改前 */
#define VECT_TAB_OFFSET  0x0

/* 改后 */
#define VECT_TAB_OFFSET  0x8000
```

**Step 4：双保险（推荐）**

在 `main()` 开头也设一次：

```c
int main(void)
{
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x8000);   /* ★关键★ */
    ...
}
```

**Step 5：编译验证**

- [ ] 0 Error
- [ ] bin 大小 < 480KB
- [ ] 烧到 `0x08008000` 能跑

> ⚠️ **验证方法**：先烧 BootLoader（`0x08000000`），再烧 APP（`0x08008000`），
> 上电看串口有没有 APP 的输出。如果跑飞，多半是向量表没改。

---

# 五、第 2-3 周：集成

## 5.1 集成前的准备

### 收集文件

从 A 和 C 那里拿到：

| 来源 | 文件 |
|---|---|
| A | `cat1.c` `cat1.h` |
| C | `mqtt.c` `mqtt.h` `utils_sha1.c/h` `utils_md5.c/h` `utils_hmac.c/h` |

### 加进工程

Keil → 右键 Source Group → Add Existing Files to Group

## 5.2 统一 common.h（最容易冲突）

### 冲突点

| 冲突 | 原因 |
|---|---|
| 宏重复定义 | A 和 C 各自加了自己需要的宏 |
| 类型定义重复 | `u8` / `u16` / `u32` 可能重复 |
| 结构体重复 | `AliyunInfo_CB` 可能多处定义 |

### 解决方案

**`common.h` 只保留一份**，包含所有公共定义：

```c
#ifndef _COMMON_H
#define _COMMON_H

#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ==================== 类型定义 ==================== */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* ==================== 全局宏 ==================== */
/* （保留原有的时钟使能、GPIO 模式等） */

/* ==================== Flash 分区 ==================== */
#define FLASH_BASE_ADDR       0x08000000
#define FLASH_PAGE_SIZE       2048
#define FLASH_TOTAL_PAGES     256
#define FLASH_BOOT_PAGE_NUM   16
#define FLASH_BOOT_SIZE       (FLASH_BOOT_PAGE_NUM * FLASH_PAGE_SIZE)
#define FLASH_A_ADDR          (FLASH_BASE_ADDR + FLASH_BOOT_SIZE)
#define FLASH_A_START_PAGE    16
#define FLASH_A_PAGE_NUM      240
#define FLASH_A_SIZE          (FLASH_A_PAGE_NUM * FLASH_PAGE_SIZE)

/* ==================== 三元组 ==================== */
#define PRODUCTKEY_LEN        11
#define DEVICENAME_LEN        32
#define DEVICESECRET_LEN      32
#define VERSION_LEN           23
#define VALID_ALICER_DATA     0xDD
#define OTA_URL_LEN           512

/* ==================== OTA 状态 ==================== */
#define BOOT_STA_O            0x5AA5C33C

/* ==================== EEPROM 结构体 ==================== */
typedef struct{
    u8  valid_cer;
    u8  ProductKeyBuff[PRODUCTKEY_LEN+1];
    u8  DeviceNameBuff[DEVICENAME_LEN+1];
    u8  DeviceSecretBuff[DEVICESECRET_LEN+1];
    u8  Version_ABuff[VERSION_LEN+1];
    u32 OTA_firelen;
    u32 OTA_flag;
} AliyunInfo_CB;

/* ==================== 事件标志 ==================== */
#define CONNECT_START   0x80000000
#define CONNECT_CMD     0x40000000
#define CONNECT_EVENT   0x20000000
#define PING_SENT       0x10000000
#define PING_CHECK      0x08000000
#define OTA_EVENT       0x04000000

/* ==================== 全局变量（只在 main.c 定义）==================== */
extern AliyunInfo_CB AliInfoCB;
extern u32           SysEventFlag;
extern u8            URL_ABuff[OTA_URL_LEN+1];

#endif
```

> ⚠️ **`extern` 和定义的区别**：
> - 头文件里写 `extern`（声明）
> - `.c` 文件里写实际定义（分配内存）
>
> 如果头文件里直接定义，多个 .c 包含它就会"重复定义"报错。

## 5.3 主循环整合

```c
/* ==================== 全局变量定义 ==================== */
AliyunInfo_CB AliInfoCB;
u32           SysEventFlag = 0;
u8            URL_ABuff[OTA_URL_LEN+1];

int main(void)
{
    /* ========== 1. 向量表重定位（★必须★）========== */
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x8000);

    /* ========== 2. 硬件初始化 ========== */
    Usart1_Init(115200);        /* 调试串口 */
    Usart3_Init(115200);        /* 4G 模块 */
    I2C_GPIO_Init();            /* 24C02 */
    SPI2_Init();                /* W25Q128 */
    Led_Init();

    Usart1_SendString("\r\n\r\n");
    Usart1_SendString("========================================\r\n");
    Usart1_SendString("   战神V1 OTA 客户端  v1.0\r\n");
    Usart1_SendString("========================================\r\n");

    /* ========== 3. 读 EEPROM ========== */
    EEPROM_ReadInfo();

    /* ========== 4. 检查 W25Q128 ========== */
    if(W25Q128_Init() != 0){
        Usart1_SendString("W25Q128 初始化失败！\r\n");
        while(1);   /* 停在这里 */
    }
    Usart1_SendString("W25Q128 就绪\r\n");

    /* ========== 5. 4G 初始化 ========== */
    CAT1_GPIO_Init();
    MQTT_Init();

    /* ========== 6. 主循环 ========== */
    while(1){
        /* --- 6.1 处理 USART3（4G）接收 --- */
        if(usart3_value.flag){
            U3_EventDispatch(usart3_value.data, usart3_value.count);
            usart3_value.flag  = 0;
            usart3_value.count = 0;
        }

        /* --- 6.2 处理主动事件 --- */
        ActiveEvent();

        /* --- 6.3 喂狗（如果开了看门狗）--- */
    }
}
```

## 5.4 事件分发函数

**这是集成的关键**：把 4G 收到的数据分发给 A（AT 应答）或 C（MQTT 报文）。

```c
/**
 * USART3 事件分发
 * @param data  收到的数据
 * @param len   长度
 */
void U3_EventDispatch(u8 *data, u16 len)
{
    /* ---------- 1. MQTT 报文（以 0x20/0x30/0x40/0x90/0xD0 开头）---------- */
    if(len > 0 && (data[0] == 0x20 || data[0] == 0x30 ||
                   data[0] == 0x40 || data[0] == 0x90 ||
                   data[0] == 0xD0)){
        U2PassiveEvent(data, len);      /* 交给 C 的 MQTT 处理 */
        return;
    }

    /* ---------- 2. AT 指令应答（文本）---------- */
    /* 这些由 A 的 CAT1_SendAT() 内部处理，这里不用管 */
    /* 但如果是不认识的文本，打印出来调试 */
    if(data[0] >= 0x20 && data[0] < 0x7F){
        Usart1_SendString("[4G] ");
        Usart1_SendBuffer((char*)data, len);
        Usart1_SendString("\r\n");
    }
}
```

> **MQTT 报文首字节说明**：
> - `0x20` = CONNACK
> - `0x30` = PUBLISH
> - `0x40` = PUBACK
> - `0x90` = SUBACK
> - `0xD0` = PINGRESP

## 5.5 集成时的 6 个常见冲突

| # | 冲突 | 现象 | 解决 |
|---|---|---|---|
| 1 | `common.h` 重复定义 | 编译报 "redefinition" | 只保留一份 |
| 2 | 变量重复定义 | 链接报 "multiply defined" | 头文件用 `extern`，.c 里定义 |
| 3 | 函数名重复 | 链接报错 | 改名（如 `delay_ms` → `bsp_delay_ms`） |
| 4 | 中断优先级冲突 | 运行时行为异常 | 统一规划 |
| 5 | 缓冲大小不一致 | 数据截断 | 统一用最大值（1024） |
| 6 | 数据类型不一致 | 编译警告 | 统一用 `u8`/`u16`/`u32` |

## 5.6 中断优先级规划

| 中断 | 抢占优先级 | 子优先级 | 理由 |
|---|---|---|---|
| USART1（调试） | 1 | 0 | 最高，方便调试 |
| USART3（4G） | 2 | 0 | 数据量大，要快 |
| TIM（定时） | 3 | 0 | 1 秒一次，不急 |
| SysTick | 15 | 0 | 最低 |

**设置方法**（在 `main()` 开头调一次）：

```c
NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);   /* 2 位抢占，2 位子优先级 */
```

---

# 六、第 4 周：测试 + 文档

## 6.1 测试用例（6 类）

| 编号 | 场景 | 操作步骤 | 期望结果 | 判定 |
|---|---|---|---|---|
| **T1** | 正常升级 | 完整走一遍：上报→推送→下载→搬移→重启 | 新功能生效 | ☐ |
| **T2** | 断网中断 | 下载到一半拔天线 | 下载失败，重启，仍运行旧固件 | ☐ |
| **T3** | 断电恢复 | 下载到一半断电 | 重新上电，OTA 标志还在，可重试 | ☐ |
| **T4** | 固件损坏 | 手动改 W25Q128 里几个字节 | MD5 校验失败，不搬移，不变砖 | ☐ |
| **T5** | 变砖恢复 | 故意烧坏 APP | 进串口菜单，Xmodem 恢复 | ☐ |
| **T6** | 重复升级 | 连续升级 5 次 | 每次都成功 | ☐ |

### 每个用例要记录

| 项目 | 内容 |
|---|---|
| 测试日期 | |
| 测试人 | |
| 前置条件 | |
| 操作步骤 | |
| 实际结果 | |
| 是否通过 | |
| 备注 | |

## 6.2 集成文档要写什么

### 1. 开发环境

- Keil 版本
- 标准库版本（3.5.0）
- 需要的 pack
- 编译器设置（MicroLIB 等）

### 2. 工程结构

```
STM32F103ZET6_OTA_APP/
├── libraries/          标准库
├── user/
│   ├── main.c          主程序
│   ├── api/            驱动层
│   │   ├── usart.c     串口
│   │   ├── i2c.c       I2C
│   │   ├── eeprom.c    24C02
│   │   ├── spi.c       SPI2 + W25Q128
│   │   ├── cat1.c      4G 模块
│   │   ├── mqtt.c      MQTT
│   │   └── utils_*.c   加密库
│   └── stm32f10x_it.c  中断
└── project/            Keil 工程
```

### 3. 编译步骤

1. 打开 `project/*.uvprojx`
2. 确认 IROM1 = `0x08008000` / `0x78000`
3. Build（F7）

### 4. 烧录步骤

| 固件 | 烧录地址 | 方式 |
|---|---|---|
| BootLoader | `0x08000000` | ST-Link |
| APP | `0x08008000` | ST-Link 或 OTA |

### 5. 调试方法

- 串口 1（921600 或 115200）看打印
- 用 `Usart1_SendString()` 打日志
- 关键节点都打印

### 6. 常见问题

（把排错手册的内容整理进来）

---

# 七、任务总表

| # | 任务 | 工期 | 完成标准 | 检查点 |
|---|---|---|---:|---|
| D1 | **写 SPI2 + W25Q128 驱动** | 2 | 读 ID = `0xEF4018` | W0 |
| D2 | W25Q128 自测 | 0.5 | 擦写读全部通过 | W0 |
| D3 | 修串口隐患 | 0.5 | 缓冲 1024；中断只收数据 | W0 |
| D4 | 建 APP 工程 | 1 | IROM1 = `0x8008000`；向量表 = `0x8000` | W1 |
| D5 | 集成 A 的代码 | 1.5 | 4G 功能可用 | W2 |
| D6 | 集成 C 的代码 | 1.5 | MQTT 功能可用 | W2 |
| D7 | 主循环整合 | 1.5 | 各功能并行不冲突 | W3 |
| D8 | 测试用例 | 2 | 6 类场景全部通过 | W4 |
| D9 | 文档 | 1 | 新人能照着跑起来 | W4 |
| | **合计** | **11** | | |

---

# 八、排错手册

## 8.1 SPI 问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 读 ID 返回 `0x000000` | ① MISO 没接 ② SPI 没使能 ③ CS 没拉低 | 逐项检查 |
| 读 ID 返回 `0xFFFFFF` | MISO 一直被拉高 | 检查 PB14 是否短路到 3.3V |
| 读 ID 返回 `0xEF4018` 但偶尔错 | 时序太快 / 没等 BUSY | 降速到 `Prescaler_8`；检查 `WaitBusy` |
| 写入后读回全 FF | 没发 WriteEnable | 每次写/擦前必须发 `0x06` |
| 写入后读回乱码 | 跨页写了 | 一页最多 256 字节，不能跨页 |
| 擦除后不是全 FF | 地址没对齐 / 擦错扇区 | 检查扇区号计算 |

## 8.2 集成问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 编译报 "redefinition" | 头文件里定义了变量 | 改成 `extern`，.c 里定义 |
| 链接报 "multiply defined" | 多个 .c 定义了同名变量 | 只在一个 .c 定义 |
| 链接报 "undefined reference" | 忘了加 .c 到工程 | Add Existing Files |
| 程序大小超 480KB | 加太多东西 | 检查是否误加了不用的库（如 GIF 数据） |
| 运行时死机 | 栈溢出 | 加大 `startup` 里的 `Stack_Size` |
| 一收数据就死机 | **向量表没重定位** | `NVIC_SetVectorTable(..., 0x8000)` |

## 8.3 串口问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 完全没有输出 | ① 没开时钟 ② 引脚模式错 | 检查 `RCC_APB2PeriphClockCmd` |
| 输出乱码 | 波特率不匹配 | 检查 72MHz 配置 |
| 只能发不能收 | RX 没配浮空输入 | 改 `Floating_Input_Mode` |
| 接收丢数据 | 缓冲太小 | 改 1024 |
| 卡在 `while(TXE==RESET)` | 串口没使能 | `USART_Cmd(ENABLE)` |

## 8.4 跳转/启动问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 烧了 APP 但不运行 | 向量表没改 | `VECT_TAB_OFFSET = 0x8000` |
| 上电直接 HardFault | BootLoader 跳转失败 | 检查 APP 是否烧到 `0x08008000` |
| APP 能跑但串口乱码 | 波特率没重新初始化 | APP 里重新 `Usart1_Init()` |
| 一有中断就死机 | 向量表没重定位 | 双保险：`NVIC_SetVectorTable` |

---

# 九、答辩准备

## 9.1 讲稿结构（2 分钟）

### 第 1 分钟：驱动层

> "我负责底层驱动和集成。核心是三个串口和两个存储：
> USART1 接 CH340 做调试，USART3 接 4G 模块，
> SPI2 接 W25Q128 存固件，I2C 接 24C02 存三元组。"

### 第 2 分钟：集成与测试

> "集成最大的挑战是接口对齐。我们第一天就锁死了 `common.h` 里的分区宏和结构体。
> 测试覆盖了 6 类场景，包括断网、断电、固件损坏和变砖恢复。"

## 9.2 可能被问到的问题

**Q：为什么用 SPI2 不用 SPI1？**
> SPI1（PA5/6/7）被板载 VS1053 音频芯片占用；SPI2 接 W25Q128，是板子设计好的。

**Q：PB12 为什么不能当普通 IO？**
> 它接 W25Q128 片选，悬空会让芯片一直占线，SPI 总线报废。

**Q：SPI 时钟为什么是 9MHz？**
> SPI2 挂在 APB1（36MHz），4 分频就是 9MHz。W25Q128 支持到 104MHz，9MHz 稳定够用。

**Q：W25Q128 的跨扇区写入怎么处理？**
> 擦除最小单位是 4KB，跨扇区时先擦整个目标范围再写，或者读-改-写。

**Q：集成时最大的问题是什么？**
> 接口不一致。比如结构体字段顺序不同，读出来就是乱码。所以第一天就锁死了。

**Q：向量表为什么要重定位？**
> APP 在 `0x08008000`，但中断向量表默认在 `0x08000000`。不重定位的话，中断会跳到 BootLoader 的向量表。

**Q：为什么用软件 NSS 不用硬件 NSS？**
> 硬件 NSS 在多主设备时容易冲突；软件 NSS 可以精确控制片选时机，更灵活。

---

# 十、常用参考

| 内容 | 位置 |
|---|---|
| **SPI_FLASH 例程** | `E:\桌面\板子\STM开发资料\STM32F10x_StdPeriph_Lib_V3.5.0\Project\STM32F10x_StdPeriph_Examples\SPI\SPI_FLASH` |
| 标准库 | `E:\桌面\板子\STM开发资料\STM32F10x_StdPeriph_Lib_V3.5.0\Libraries\` |
| W25Q128 数据手册 | `E:\桌面\板子\STM开发资料\W25Q128.pdf` |
| 板子引脚表 | `outputs/板子资料档案.md` |
| 你的基础工程 | `E:\MY_project\STM32F103ZET6_Demo10DMA` |

---

# 十一、余量时间怎么用

**你省了 6.5 天，建议这样分配：**

| 用途 | 时间 | 为什么 |
|---|---|---|
| 帮 B 调试 BootLoader | 2 天 | BootLoader 出问题最难查，你帮他能显著降风险 |
| 完善测试用例 | 1.5 天 | 6 类场景要仔细设计 |
| 写集成文档 | 1 天 | 新人能照着跑 |
| 准备答辩演示 | 1.5 天 | 录屏、截图 |
| 缓冲 | 0.5 天 | 应急 |

> **你是关键路径，帮别人就是在帮自己。** BootLoader 出问题，整个项目就卡住了。

---

# 一句话记住

**先花 2 天把 SPI 驱动写出来（A 和 B 都等着用），剩下的时间做集成、测试和帮别人。**
