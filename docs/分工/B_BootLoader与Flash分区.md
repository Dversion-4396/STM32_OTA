# 【成员 B】BootLoader + Flash 分区

> 项目：战神V1（F103ZET6）+ 移柯 L511-2 + 阿里云 OTA
> 基础工程：`E:\MY_project\STM32F103ZET6_Demo10DMA`（裸机版）
> 你的角色：**固件落地负责人**，决定下载的固件能不能真正跑起来
> 工期：13 天 | 难度：★★★★

---

# 目录

1. [你负责什么](#一你负责什么)
2. [你的基础：从裸机工程裁剪](#二你的基础从裸机工程裁剪)
3. [第 0 天：工程裁剪 + 分区设计](#三第-0-天工程裁剪--分区设计)
4. [第 1 周：Flash 驱动](#四第-1-周flash-驱动)
5. [第 2 周：菜单 + EEPROM](#五第-2-周菜单--eeprom)
6. [第 3 周：搬移 + 跳转](#六第-3-周搬移--跳转)
7. [第 4 周：加固 + 联调](#七第-4-周加固--联调)
8. [任务总表](#八任务总表)
9. [排错手册](#九排错手册)
10. [答辩准备](#十答辩准备)

---

# 一、你负责什么

**写 BootLoader：上电时判断要不要升级，要升级就从 W25Q128 搬固件到运行区，不要就直接跳转 APP。**

## 5 个核心功能

| 功能 | 作用 | 判断标准 |
|---|---|---|
| ① Flash 擦写 | 擦除 A 区、写入固件 | 擦后全 FF；写入读回一致 |
| ② 串口菜单 | 变砖后能救回来 | 4 个功能都能用 |
| ③ EEPROM 读写 | 存三元组和 OTA 标志 | 断电不丢 |
| ④ 固件搬移 | W25Q128 → A 区 | 搬完内容正确 |
| ⑤ 跳转 APP | 把控制权交给应用 | 能跑起来 |

## 为什么需要 BootLoader

**STM32 不能自己擦除自己正在运行的 Flash。**

如果应用自己擦 Flash，擦到一半自己就没了 → 变砖。

所以需要一段独立的程序（BootLoader）住在 Flash 最前面，它负责：
- 判断有没有新固件
- 擦除应用区
- 搬移固件
- 跳转

---

# 二、你的基础：从裸机工程裁剪

## 2.1 不要从零建工程

**直接从 `Demo10DMA` 复制一份**，然后删掉不需要的。

这样串口、I2C、EEPROM、延时的驱动全都有，省 3-4 天。

## 2.2 保留什么

| 文件 | 用途 |
|---|---|
| `user/api/usart.c/h` | 串口（调试 + 菜单） |
| `user/api/i2c.c/h` | I2C 底层 |
| `user/api/eeprom.c/h` | 24C02 读写 |
| `user/api/delay.c/h` | 延时 |
| `user/api/led.c/h` | LED 指示 |
| `user/api/common.h` | 公共宏 |
| `user/stm32f10x_it.c/h` | 中断 |
| `libraries/` | 标准库 |

## 2.3 删掉什么

| 文件 | 大小 | 为什么删 |
|---|---|---|
| `oled.c/h` | 9.7KB | BootLoader 不需要显示 |
| `oled_font.c` | **836KB** | 字库，太大 |
| `gif_frames.c` | **1852KB** | GIF 数据，太大 |
| `dht11.c/h` | 7.6KB | 传感器 |
| `adc.c/h` | 4.4KB | 传感器 |
| `tim3_pwm.c/h` | 2.8KB | PWM |
| `tim6.c/h` | 0.6KB | 定时 |
| `modbus.c/h` | 6.1KB | 通信协议 |
| `mqtt.c/h` | 6.1KB | BootLoader 不联网 |

**删完 bin 应该能控制在 20KB 以内**，32KB 分区绰绰有余。

> ⚠️ **别删 `systick.c`**，`delay.c` 可能依赖它。

## 2.4 新增什么

| 文件 | 内容 | 约多少行 |
|---|---|---|
| `stmflash.c/h` | Flash 擦除、半字写入、读、判空 | 150 |
| `boot.c` | 主流程：分支判断、菜单、搬移、跳转 | 250 |

---

# 三、第 0 天：工程裁剪 + 分区设计

## 3.1 裁剪步骤

**Step 1：复制工程**
```
把 E:\MY_project\STM32F103ZET6_Demo10DMA
复制到 E:\MY_project\STM32F103ZET6_BootLoader
```

**Step 2：从工程里移除文件**

Keil 左侧 Project 窗口 → 右键要删的文件 → Remove File

**Step 3：改 IROM1**

Options for Target → Target 标签：

| 项 | 改成 |
|---|---|
| IROM1 Start | `0x08000000` |
| IROM1 Size | **`0x8000`**（32KB） |

> 原工程是 `0x80000`（全片 512KB），**必须改**。
> 不改的话链接器不会报错，代码超 32KB 就会覆盖 A 区。

**Step 4：改 VECT_TAB_OFFSET**

打开 `libraries/CMSIS/.../system_stm32f10x.c`，找到：

```c
#define VECT_TAB_OFFSET  0x0
```

**BootLoader 保持 `0x0`**（因为它就在 Flash 起始）。

**Step 5：编译验证**

编译，检查：
- [ ] 0 Error
- [ ] bin 大小 < 32768 字节
- [ ] 没有警告（或只有无害警告）

## 3.2 Flash 分区设计

### 芯片参数

| 项 | 值 |
|---|---|
| Flash 容量 | 512 KB |
| 页大小 | **2 KB** |
| 总页数 | **256** |

### 分区方案

```
0x08000000 ┌──────────────────────────────┐
           │   BootLoader  (16页 = 32KB)  │  ← 你写的
0x08008000 ├──────────────────────────────┤  ← APP 起始
           │   应用 A 区 (240页 = 480KB)  │  ← 别人写的
0x08080000 └──────────────────────────────┘
```

### 宏定义（写进 `main.h`）

```c
/* ==================== Flash 分区 ==================== */
#define FLASH_BASE_ADDR       0x08000000
#define FLASH_PAGE_SIZE       2048
#define FLASH_TOTAL_PAGES     256

/* BootLoader 占 16 页 = 32KB */
#define FLASH_BOOT_PAGE_NUM   16
#define FLASH_BOOT_SIZE       (FLASH_BOOT_PAGE_NUM * FLASH_PAGE_SIZE)   /* 0x8000 */
#define FLASH_BOOT_ADDR       FLASH_BASE_ADDR

/* A 区从第 16 页开始，占 240 页 = 480KB */
#define FLASH_A_ADDR          (FLASH_BASE_ADDR + FLASH_BOOT_SIZE)       /* 0x08008000 */
#define FLASH_A_START_PAGE    16
#define FLASH_A_PAGE_NUM      240
#define FLASH_A_SIZE          (FLASH_A_PAGE_NUM * FLASH_PAGE_SIZE)      /* 480KB */
```

### ⚠️ 三处必须对齐（否则全组返工）

| 位置 | BootLoader | APP |
|---|---|---|
| Keil → Target → IROM1 | `0x08000000` / `0x8000` | `0x08008000` / `0x78000` |
| `system_stm32f10x.c` 的 `VECT_TAB_OFFSET` | **`0x0`** | **`0x8000`** |
| `main.h` 分区宏 | 一致 | 一致 |

> **`VECT_TAB_OFFSET` 最容易漏。**
> APP 不改的话，中断会跳到 BootLoader 的向量表 → 跑飞。
> 现象像"下载到一半卡死"，实际是中断炸了。这是移植失败第一大原因。

---

# 四、第 1 周：Flash 驱动

## 4.1 核心难点：F103 是半字编程

### G0 和 F103 的区别

| 项目 | G070（原例程） | **F103（你的）** |
|---|---|---|
| 编程单位 | **8 字节双字** | **2 字节半字** |
| 编程 API | `HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, data)` | `FLASH_ProgramHalfWord(addr, data)` |
| 擦除单位 | 2KB 页 | 2KB 页（一样） |
| 擦除 API | `HAL_FLASHEx_Erase(&init, &err)` | `FLASH_ErasePage(addr)` |
| 解锁 | `HAL_FLASH_Unlock()` | `FLASH_Unlock()` |

**原代码传 `uint64_t*`，你必须改成 `uint8_t*` 并按半字拼装。**

直接照搬会写坏数据。

## 4.2 要实现的 5 个函数

```c
u8 Flash_ErasePage(u32 addr);                    /* 擦一页 */
u8 Flash_EraseArea(u32 start, u16 pages);        /* 擦连续多页 */
u8 Flash_Write(u32 addr, u8 *buf, u32 len);      /* 写数据 */
void Flash_Read(u32 addr, u8 *buf, u32 len);     /* 读数据 */
u8 Flash_IsBlank(u32 addr, u32 len);             /* 判断是否全 FF */
```

## 4.3 Flash 擦除

```c
/**
 * 擦除一页 Flash（2KB）
 * @param addr  页起始地址（必须 2KB 对齐）
 * @return 0=成功  1=地址未对齐  2=擦除失败
 */
u8 Flash_ErasePage(u32 addr)
{
    FLASH_Status st;

    /* 检查地址是否 2KB 对齐 */
    if(addr % FLASH_PAGE_SIZE) return 1;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    st = FLASH_ErasePage(addr);

    FLASH_Lock();

    return (st == FLASH_COMPLETE) ? 0 : 2;
}

/**
 * 擦除 A 区全部（240 页）
 * @return 0=成功
 */
u8 Flash_EraseApp(void)
{
    u16 i;

    for(i = 0; i < FLASH_A_PAGE_NUM; i++){
        if(Flash_ErasePage(FLASH_A_ADDR + i * FLASH_PAGE_SIZE)){
            return 1;
        }
    }
    return 0;
}
```

## 4.4 Flash 写入（关键）

```c
/**
 * 写 Flash —— F103 每次只能写 2 字节
 * @param addr  目标地址（必须 2 字节对齐）
 * @param buf   数据缓冲区
 * @param len   字节数（必须为偶数）
 * @return 0=成功  1=地址未对齐  2=长度非偶数  3=写失败
 */
u8 Flash_Write(u32 addr, u8 *buf, u32 len)
{
    u32 i;
    u16 halfword;

    if(addr % 2) return 1;      /* 地址必须 2 字节对齐 */
    if(len  % 2) return 2;      /* 长度必须偶数 */

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    for(i = 0; i < len; i += 2){
        /* 小端拼装：低字节在前 */
        halfword = (u16)buf[i] | ((u16)buf[i + 1] << 8);

        if(FLASH_ProgramHalfWord(addr + i, halfword) != FLASH_COMPLETE){
            FLASH_Lock();
            return 3;
        }
    }

    FLASH_Lock();
    return 0;
}
```

### ⚠️ 三个关键点

**1. 地址必须 2 字节对齐**

`0x08008000` 是对齐的，但 `0x08008001` 不是。

**2. 长度必须偶数**

如果固件长度是奇数，最后一个字节要补 `0xFF`。

**3. 小端拼装**

```c
halfword = (u16)buf[i] | ((u16)buf[i + 1] << 8);    /* ✅ 正确 */
halfword = (u16)buf[i + 1] | ((u16)buf[i] << 8);    /* ❌ 高低字节反了 */
```

## 4.5 读和判空

```c
/**
 * 读 Flash（直接读内存，不需要特殊函数）
 */
void Flash_Read(u32 addr, u8 *buf, u32 len)
{
    memcpy(buf, (void*)addr, len);
}

/**
 * 判断 Flash 是否为空（全 FF）
 * @return 1=全FF  0=有数据
 */
u8 Flash_IsBlank(u32 addr, u32 len)
{
    u32 i;

    for(i = 0; i < len; i++){
        if(*(volatile u8*)(addr + i) != 0xFF) return 0;
    }
    return 1;
}
```

## 4.6 自测（Day 2-3）

**写个测试函数，用 A 区地址测试。**

```c
void Flash_Test(void)
{
    u8  wbuf[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    u8  rbuf[16];
    u32 test_addr = FLASH_A_ADDR;   /* ⚠️ 用 A 区，不要用 0x08000000 */

    Usart1_SendString("\r\n===== Flash 测试 =====\r\n");

    /* 1. 擦除 */
    if(Flash_ErasePage(test_addr)){
        Usart1_SendString("擦除失败\r\n");
        return;
    }
    Usart1_SendString("擦除成功\r\n");

    /* 2. 验证擦除后全 FF */
    if(Flash_IsBlank(test_addr, 16)){
        Usart1_SendString("擦除后全 FF ✓\r\n");
    }else{
        Usart1_SendString("擦除后不是全 FF ✗\r\n");
    }

    /* 3. 写入 */
    if(Flash_Write(test_addr, wbuf, 16)){
        Usart1_SendString("写入失败\r\n");
        return;
    }
    Usart1_SendString("写入成功\r\n");

    /* 4. 读回比对 */
    Flash_Read(test_addr, rbuf, 16);
    if(memcmp(wbuf, rbuf, 16) == 0){
        Usart1_SendString("读回一致 ✓\r\n");
    }else{
        Usart1_SendString("读回不一致 ✗\r\n");
    }

    Usart1_SendString("======================\r\n\r\n");
}
```

**期望输出**：
```
===== Flash 测试 =====
擦除成功
擦除后全 FF ✓
写入成功
读回一致 ✓
======================
```

> ⚠️ **测试地址必须是 A 区**（`0x08008000`）。
> 如果用 `0x08000000`，会把 BootLoader 自己擦了，板子就废了。

---

# 五、第 2 周：菜单 + EEPROM

## 5.1 串口救砖菜单（Day 4-5，2 天）

### 菜单设计

```
========================================
     战神V1 BootLoader  v1.0
     Build: Oct 27 2026 09:00:00
========================================

[1] A区擦除
[2] A区下载（Xmodem）
[3] 设置阿里云三元组证书信息
[4] 查询阿里云三元组证书信息
```

### 主流程

```c
int main(void)
{
    /* 初始化 */
    SystemInit();
    Delay_Init();
    Usart1_Init(115200);
    I2C_GPIO_Init();
    EEPROM_ReadInfo();          /* 读三元组 */

    /* 打印 banner */
    Usart1_SendString("\r\n\r\n");
    Usart1_SendString("========================================\r\n");
    Usart1_SendString("     战神V1 BootLoader  v1.0\r\n");
    Usart1_SendString("========================================\r\n");

    /* 分支判断 */
    BootLoader_Branch();

    /* 进入菜单主循环 */
    BootLoader_Info();

    while(1){
        /* 处理串口命令 */
        if(usart1_value.flag){
            BootLoader_Event(usart1_value.data, usart1_value.count);
            usart1_value.flag  = 0;
            usart1_value.count = 0;
        }

        /* 处理 OTA 搬移事件 */
        ActiveEvent();
    }
}
```

### 分支判断（核心逻辑）

```c
/**
 * BootLoader 分支走向
 * 三种情况：
 *   1. 2秒内输入 'w' → 进菜单
 *   2. EEPROM 有 OTA 标志 → 搬移固件
 *   3. 其他 → 跳转 APP
 */
void BootLoader_Branch(void)
{
    u16 i;

    /* 1. 等 2 秒看有没有 'w' */
    Usart1_SendString("\r\n2秒内输入 'w' 进入 BootLoader\r\n");

    for(i = 0; i < 20; i++){
        if(usart1_value.data[0] == 'w'){
            Usart1_SendString("进入 BootLoader\r\n");
            return;
        }
        delay_ms(100);
    }

    /* 2. 检查 OTA 标志 */
    if(AliInfoCB.OTA_flag == BOOT_STA_O){
        Usart1_SendString("检测到 OTA 标志，开始升级\r\n");
        Usart1_SendString("固件大小：");
        Usart1_SendNumber(AliInfoCB.OTA_firelen);
        Usart1_SendString("\r\n");

        BootEventFlag |= OTA_MODEM;    /* 置事件，由 ActiveEvent 处理 */
        return;
    }

    /* 3. 跳转 APP */
    Usart1_SendString("跳转到 A 区\r\n");
    Load_APP(FLASH_A_ADDR);
}
```

### 命令处理

```c
void BootLoader_Event(u8 *data, u16 len)
{
    /* ---------- 无事件：处理菜单命令 ---------- */
    if(BootEventFlag == 0){
        /* [1] A 区擦除 */
        if(len == 1 && data[0] == '1'){
            Usart1_SendString("准备擦除 A 区...\r\n");
            if(Flash_EraseApp() == 0){
                Usart1_SendString("擦除成功\r\n");
            }else{
                Usart1_SendString("擦除失败\r\n");
            }
            BootLoader_Info();
        }
        /* [2] A 区下载（Xmodem） */
        else if(len == 1 && data[0] == '2'){
            Usart1_SendString("准备通过 Xmodem 下载\r\n");
            /* 见 5.2 */
        }
        /* [3] 设置三元组 */
        else if(len == 1 && data[0] == '3'){
            BootEventFlag |= ALIYUN_CER;
            Usart1_SendString("请输入三元组，格式：\r\n");
            Usart1_SendString("/ProductKey/DeviceName/DeviceSecret/版本号/\r\n");
        }
        /* [4] 查询三元组 */
        else if(len == 1 && data[0] == '4'){
            Printf_AliCer();
            BootLoader_Info();
        }
    }
    /* ---------- 设置三元组事件 ---------- */
    else if(BootEventFlag & ALIYUN_CER){
        if(AliCer_Inspect(data) == 0){
            Usart1_SendString("三元组设置成功\r\n");
            AliInfoCB.valid_cer = VALID_ALICER_DATA;
            EEPROM_SaveInfo();
            Printf_AliCer();
            BootEventFlag &= ~ALIYUN_CER;
            BootLoader_Info();
        }else{
            Usart1_SendString("格式错误，重新输入\r\n");
        }
    }
}
```

## 5.2 Xmodem 下载（可选，但建议做）

**为什么建议做**：这是你的救命稻草。如果 APP 刷坏了，这是唯一能救回来的方式。

**Xmodem 协议要点**：

| 常量 | 值 | 含义 |
|---|---|---|
| SOH | `0x01` | 数据包起始 |
| EOT | `0x04` | 传输结束 |
| ACK | `0x06` | 应答 |
| NAK | `0x15` | 非应答 |
| CAN | `0x18` | 取消 |

**数据包格式**（133 字节）：
```
[SOH][包号][~包号][128字节数据][CRC高][CRC低]
```

**流程**：
1. BootLoader 发 `NAK` 或 `C`，请求第一包
2. 上位机发第 1 包
3. BootLoader 校验包号、CRC，正确则发 `ACK`
4. 循环，直到收到 `EOT`
5. 发 `ACK`，传输完成

**测试方法**：用 SecureCRT 或 Xshell 的"发送文件 → Xmodem"功能。

> 如果时间紧，可以先不做，用 ST-Link 直接烧 APP 代替。
> 但答辩时这是个加分项（"我们做了变砖恢复机制"）。

## 5.3 EEPROM 读写（Day 6-7，1 天）

### 你已有的

`user/api/eeprom.c` 已有：
```c
u8 EEPROM_WriteByte(u8 eeprom_addr, u8 reg_addr, u8 data);
u8 EEPROM_ReadByte(u8 eeprom_addr, u8 reg_addr);
```

`common.h` 里有 `EEPROM_AT24C02_ADDR (0x50)`。

### 结构体定义

```c
/* ==================== 三元组长度 ==================== */
#define PRODUCTKEY_LEN        11
#define DEVICENAME_LEN        32
#define DEVICESECRET_LEN      32
#define VERSION_LEN           23
#define VALID_ALICER_DATA     0xDD

/* ==================== EEPROM 结构体 ==================== */
typedef struct{
    u8  valid_cer;                             /* 0xDD = 有效 */
    u8  ProductKeyBuff[PRODUCTKEY_LEN+1];      /* 12 字节 */
    u8  DeviceNameBuff[DEVICENAME_LEN+1];      /* 33 字节 */
    u8  DeviceSecretBuff[DEVICESECRET_LEN+1];  /* 33 字节 */
    u8  Version_ABuff[VERSION_LEN+1];          /* 24 字节 */
    u32 OTA_firelen;                           /* 固件大小 */
    u32 OTA_flag;                              /* 0x5AA5C33C = 需要OTA */
} AliyunInfo_CB;
```

### 结构体读写（包一层）

```c
AliyunInfo_CB AliInfoCB;

/**
 * 从 EEPROM 读取整个结构体
 */
void EEPROM_ReadInfo(void)
{
    u8 *p = (u8*)&AliInfoCB;
    u16 i;

    memset(&AliInfoCB, 0, sizeof(AliInfoCB));

    for(i = 0; i < sizeof(AliInfoCB); i++){
        p[i] = EEPROM_ReadByte(EEPROM_AT24C02_ADDR, i);
    }

    if(AliInfoCB.valid_cer != VALID_ALICER_DATA){
        Usart1_SendString("证书信息无效，请输入 '3' 重新设置\r\n");
    }
}

/**
 * 保存整个结构体到 EEPROM
 */
u8 EEPROM_SaveInfo(void)
{
    u8 *p = (u8*)&AliInfoCB;
    u16 i;

    for(i = 0; i < sizeof(AliInfoCB); i++){
        if(EEPROM_WriteByte(EEPROM_AT24C02_ADDR, i, p[i]) != I2C_ACK){
            return 1;
        }
        delay_ms(5);    /* ⚠️ AT24C02 每写一字节要等 5ms */
    }
    return 0;
}
```

### ⚠️ AT24C02 的关键限制

| 限制 | 值 | 说明 |
|---|---|---|
| 容量 | 256 字节 | 地址 0~255 |
| **写周期** | **5ms** | 每写一字节必须等 5ms |
| 页大小 | 8 字节 | 跨页要特殊处理 |

**你的结构体多大**：
```
1 + 12 + 33 + 33 + 24 + 4 + 4 = 111 字节
```
**111 < 256，装得下。** 但每写一字节等 5ms，写 111 字节要 555ms，**接近 0.6 秒**。

> 如果觉得慢，可以用"页写"模式（一次写 8 字节），但要处理跨页。

## 5.4 三元组校验

**格式**：`/ProductKey/DeviceName/DeviceSecret/版本号/`

**示例**：
```
/grfdYViGNwS/test_001/43e992a58ce66c1a855c68f13c9a9ae4/app_2023112855511/
```

**长度硬校验**（错一个字节就拒绝）：

| 字段 | 要求 | 宏 |
|---|---|---|
| ProductKey | **必须正好 11** | `PRODUCTKEY_LEN` |
| DeviceName | ≤ 32 | `DEVICENAME_LEN` |
| DeviceSecret | **必须正好 32** | `DEVICESECRET_LEN` |
| 版本号 | **必须正好 23** | `VERSION_LEN` |

**解析函数**：

```c
/**
 * 校验并保存三元组
 * @param data  收到的字符串
 * @return 0=成功  1~4=各字段长度错  5=格式错
 */
u8 AliCer_Inspect(u8 *data)
{
    /* 用 sscanf 按 "/xxx/xxx/xxx/xxx/" 格式提取 */
    if(sscanf((char*)data, "/%[^/]/%[^/]/%[^/]/%[^/]/",
              AliInfoCB.ProductKeyBuff,
              AliInfoCB.DeviceNameBuff,
              AliInfoCB.DeviceSecretBuff,
              AliInfoCB.Version_ABuff) != 4){
        return 5;
    }

    if(strlen((char*)AliInfoCB.ProductKeyBuff)   != PRODUCTKEY_LEN)   return 1;
    if(strlen((char*)AliInfoCB.DeviceNameBuff)   >  DEVICENAME_LEN)   return 2;
    if(strlen((char*)AliInfoCB.DeviceSecretBuff) != DEVICESECRET_LEN) return 3;
    if(strlen((char*)AliInfoCB.Version_ABuff)    != VERSION_LEN)      return 4;

    return 0;
}
```

---

# 六、第 3 周：搬移 + 跳转

## 6.1 固件搬移（Day 8-10，2 天）

```c
/**
 * 从 W25Q128 搬移固件到 A 区
 * @param fw_len  固件长度（不含 28 字节尾缀）
 * @return 0=成功  1=超范围  2=写失败
 */
u8 Boot_CopyFirmware(u32 fw_len)
{
    u32 i, remain;
    u8  buf[FLASH_PAGE_SIZE];       /* 2KB 缓冲 */
    u16 page;

    Usart1_SendString("开始搬移，共 ");
    Usart1_SendNumber(fw_len);
    Usart1_SendString(" 字节\r\n");

    /* 1. 检查范围 */
    if(fw_len > FLASH_A_SIZE){
        Usart1_SendString("固件超过 A 区容量\r\n");
        return 1;
    }

    /* 2. 按 2KB 页搬 */
    page = 0;
    for(i = 0; i < fw_len; i += FLASH_PAGE_SIZE){
        remain = fw_len - i;
        if(remain > FLASH_PAGE_SIZE) remain = FLASH_PAGE_SIZE;

        /* 从 W25Q128 读一页 */
        W25Q128_Read(i, buf, remain);

        /* 补齐到偶数（F103 半字编程要求） */
        if(remain % 2) buf[remain] = 0xFF;

        /* 写入 A 区 */
        if(Flash_Write(FLASH_A_ADDR + i, buf, (remain + 1) & ~1)){
            Usart1_SendString("写入失败\r\n");
            return 2;
        }

        /* 进度 */
        Usart1_SendString("搬移进度：");
        Usart1_SendNumber(i + remain);
        Usart1_SendString(" / ");
        Usart1_SendNumber(fw_len);
        Usart1_SendString("\r\n");
    }

    Usart1_SendString("搬移完成\r\n");
    return 0;
}
```

> ⚠️ **注意 `(remain + 1) & ~1`**：把奇数长度补齐成偶数。

## 6.2 跳转 APP（Day 11-12，1.5 天）

**这是最容易出错的地方，每一步都要检查。**

```c
typedef void (*pFunction)(void);

/**
 * 跳转到 APP
 * @param app_addr  APP 起始地址（0x08008000）
 */
void Load_APP(u32 app_addr)
{
    pFunction jump;
    u32 stack_top, reset_vec;

    Usart1_SendString("准备跳转到 APP：0x");
    Usart1_SendHex(app_addr);
    Usart1_SendString("\r\n");

    /* ========== 检查 1：栈顶地址是否合法 ========== */
    stack_top = *(volatile u32*)app_addr;
    /* SRAM 范围：0x20000000 ~ 0x2000FFFF（64KB） */
    if((stack_top & 0x2FFE0000) != 0x20000000){
        Usart1_SendString("跳转失败：栈顶地址非法\r\n");
        return;
    }

    /* ========== 检查 2：复位向量是否在 A 区 ========== */
    reset_vec = *(volatile u32*)(app_addr + 4);
    if(reset_vec < app_addr || reset_vec > 0x08080000){
        Usart1_SendString("跳转失败：复位向量非法\r\n");
        return;
    }

    Usart1_SendString("栈顶：0x");
    Usart1_SendHex(stack_top);
    Usart1_SendString("  复位向量：0x");
    Usart1_SendHex(reset_vec);
    Usart1_SendString("\r\n");

    /* ========== 跳转前清理 ========== */
    __disable_irq();                    /* 关所有中断 */
    SysTick->CTRL = 0;                  /* 关 SysTick */

    /* 反初始化用到的外设，避免和 APP 冲突 */
    USART_DeInit(USART1);
    USART_DeInit(USART3);

    /* ========== 设置向量表 ========== */
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, app_addr - FLASH_BASE_ADDR);

    /* ========== 设置栈顶 ========== */
    __set_MSP(stack_top);

    /* ========== 跳转 ========== */
    jump = (pFunction)reset_vec;
    jump();

    /* 正常不会走到这里 */
    Usart1_SendString("跳转失败\r\n");
}
```

### 为什么每一步都要检查

| 检查 | 不检查的后果 |
|---|---|
| 栈顶地址 | 跳到空 Flash（全 FF）→ HardFault |
| 复位向量 | 可能跳到乱地址 → HardFault |
| 关中断 | BootLoader 中断触发 → 跳到错误向量表 → 跑飞 |
| 设向量表 | APP 的中断全跳到 BootLoader → 跑飞 |
| 外设 DeInit | APP 里重新初始化冲突 → 行为异常 |

### 一个容易忽略的细节

**`NVIC_SetVectorTable` 的第二个参数是"偏移量"，不是绝对地址。**

```c
NVIC_SetVectorTable(NVIC_VectTab_FLASH, app_addr - FLASH_BASE_ADDR);   /* ✅ */
NVIC_SetVectorTable(NVIC_VectTab_FLASH, app_addr);                     /* ❌ 会算错 */
```

因为标准库内部是：
```c
SCB->VTOR = NVIC_VectTab | (Offset & 0x1FFFFF80);
```

`NVIC_VectTab_FLASH` 已经是 `0x08000000`，所以 Offset 只需传偏移 `0x8000`。

---

# 七、第 4 周：加固 + 联调

## 7.1 固件校验（Day 13，1.5 天，加分项）

### 为什么原例程没做这个

原例程的逻辑：只要 EEPROM 有 OTA 标志就搬，**不校验数据**。

**风险**：如果 W25Q128 里数据损坏（下载中断、Flash 坏块），照样搬进 A 区，
搬完重启 → APP 是坏的 → 又重启 → **可能变砖**。

**你加上校验，就是工程上的改进，答辩能讲。**

### 28 字节尾缀格式

| 偏移 | 长度 | 内容 | 说明 |
|---|---|---|---|
| +0 | 4 | `CD EF CD EF` | ⚠️ **十六进制字节，不是 ASCII 字符串** |
| +4 | 4 | 原始 bin 长度 | 小端 |
| +8 | 16 | 原始 bin 的 MD5 | 16 字节裸摘要 |
| +24 | 4 | `FF FF FF FF` | 结尾标记 |

### 校验流程

```
1. 从 (总长度 - 28) 读 4 字节魔数
   → 比对 0xEFCDEFCD（小端）

2. 从 (总长度 - 24) 读 4 字节长度
   → 比对 (总长度 - 28)

3. 从 (总长度 - 20) 读 16 字节 MD5

4. 逐块读固件（每块 256 字节），算 MD5

5. 比对
   → 不一致：不搬移，继续用旧固件（避免变砖）
```

### 实现要点

```c
/**
 * 校验 W25Q128 里的固件
 * @param fw_total_len  总长度（含 28 字节尾缀）
 * @param out_real_len  输出：真实固件长度（不含尾缀）
 * @return 0=通过  其他=失败
 */
u8 Boot_VerifyFirmware(u32 fw_total_len, u32 *out_real_len)
{
    u32 magic, len_in_footer;
    u8  md5_in_flash[16];
    u8  md5_calc[16];
    u8  buf[256];
    u32 i, real_len;

    if(fw_total_len <= 28){
        Usart1_SendString("固件太小，没有尾缀\r\n");
        return 1;
    }

    /* 1. 读魔数 */
    W25Q128_Read(fw_total_len - 28, (u8*)&magic, 4);
    if(magic != 0xEFCDEFCD){        /* 小端：CD EF CD EF */
        Usart1_SendString("尾缀魔数错误\r\n");
        return 2;
    }

    /* 2. 读长度 */
    W25Q128_Read(fw_total_len - 24, (u8*)&len_in_footer, 4);
    real_len = fw_total_len - 28;
    if(len_in_footer != real_len){
        Usart1_SendString("长度不匹配\r\n");
        return 3;
    }

    /* 3. 读 MD5 */
    W25Q128_Read(fw_total_len - 20, md5_in_flash, 16);

    /* 4. 算 MD5 */
    MD5_Init();
    for(i = 0; i < real_len; i += 256){
        u32 n = (real_len - i >= 256) ? 256 : (real_len - i);
        W25Q128_Read(i, buf, n);
        MD5_Update(buf, n);
    }
    MD5_Final(md5_calc);

    /* 5. 比对 */
    if(memcmp(md5_in_flash, md5_calc, 16) != 0){
        Usart1_SendString("MD5 校验失败\r\n");
        return 4;
    }

    *out_real_len = real_len;
    Usart1_SendString("固件校验通过\r\n");
    return 0;
}
```

> MD5 源码从 `36号工程\HMAC\scr\utils_md5.c` 复制，纯 C 不用改。

## 7.2 完整 OTA 流程

```c
void ActiveEvent(void)
{
    u32 real_len;

    if(!(BootEventFlag & OTA_MODEM)) return;
    BootEventFlag &= ~OTA_MODEM;

    Usart1_SendString("\r\n========== OTA 升级 ==========\r\n");

    /* 1. 校验固件 */
    if(Boot_VerifyFirmware(AliInfoCB.OTA_firelen, &real_len)){
        Usart1_SendString("校验失败，取消升级\r\n");
        AliInfoCB.OTA_flag = 0;
        EEPROM_SaveInfo();
        Load_APP(FLASH_A_ADDR);      /* 继续用旧固件 */
        return;
    }

    /* 2. 擦除 A 区 */
    Usart1_SendString("擦除 A 区...\r\n");
    if(Flash_EraseApp()){
        Usart1_SendString("擦除失败\r\n");
        NVIC_SystemReset();
    }

    /* 3. 搬移 */
    if(Boot_CopyFirmware(real_len)){
        Usart1_SendString("搬移失败\r\n");
        NVIC_SystemReset();
    }

    /* 4. 清标志 */
    AliInfoCB.OTA_flag = 0;
    EEPROM_SaveInfo();

    Usart1_SendString("升级完成，重启\r\n");
    delay_ms(500);
    NVIC_SystemReset();
}
```

## 7.3 异常测试

| 场景 | 怎么模拟 | 期望结果 |
|---|---|---|
| 固件损坏 | 手动改 W25Q128 几个字节 | MD5 失败，不搬移，继续用旧固件 |
| 尾缀错误 | 改魔数 | 校验失败，不搬移 |
| 固件超范围 | 改 `OTA_firelen` 超大 | 拒绝搬移 |
| 下载中断 | 下载一半断电 | 下次上电 OTA 标志还在，重试 |
| 变砖恢复 | 故意烧坏 APP | 进菜单 Xmodem 恢复 |

---

# 八、任务总表

| # | 任务 | 工期 | 完成标准 | 检查点 |
|---|---|---|---:|---|
| B1 | 裁剪工程 | 1 | 编译通过，bin < 32KB，IROM1 = `0x8000` | W0 |
| B2 | **Flash 擦写驱动** | 2 | 擦后全 FF；写入读回一致 | W1 |
| B3 | 串口救砖菜单 | 2 | 4 个功能都能用 | W2 |
| B4 | EEPROM 结构体读写 | 1 | 写入读回一致，断电不丢 | W2 |
| B5 | W25Q128 读取 | 1 | 能读出 A 区固件（等 D 的驱动） | W2 |
| B6 | 固件搬移 | 2 | A 区内容与原始 bin 一致 | W3 |
| B7 | 跳转逻辑 | 1.5 | 能跳到 APP 并运行 | W3 |
| B8 | 固件校验（加分项） | 1.5 | 损坏固件被拒绝 | W4 |
| B9 | 联调 | 1 | 端到端跑通 | W4 |
| | **合计** | **13** | | |

---

# 九、排错手册

## 9.1 Flash 问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 擦除失败 | 地址没 2KB 对齐 | 检查 `addr % 2048 == 0` |
| 写入后读回乱码 | 半字拼装高低字节反了 | 检查 `buf[i] \| (buf[i+1] << 8)` |
| 写入返回 3 | 写前没擦除 | Flash 只能 1→0，写前必须擦 |
| 程序跑飞 | 把自己擦了 | 检查测试地址不是 `0x08000000` |
| 擦除后不是全 FF | 页大小搞错 | F103ZET6 是 2KB/页 |

## 9.2 跳转问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 跳转后 HardFault | 栈顶非法（APP 没烧/烧错地址） | 检查 APP 是否烧到 `0x08008000` |
| 跳转后能跑但中断不响应 | APP 的 `VECT_TAB_OFFSET` 没改 | 改成 `0x8000` |
| 跳转后串口乱码 | 波特率没重新初始化 | APP 里重新 `Usart1_Init()` |
| 打印"栈顶非法" | APP 没烧，或烧到了 `0x08000000` | 重新烧到 `0x08008000` |
| 跳转后死机 | 没关中断 | 检查 `__disable_irq()` |

## 9.3 EEPROM 问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 读出来全 0xFF | 从没写过 | 菜单选 `3` 设置三元组 |
| 读出来全 0x00 | I2C 通信失败 | 检查 PB6/PB7 上拉（板上有 4.7K） |
| 写入后读回不对 | I2C 时序太快 / 没等 5ms | 加 `delay_ms(5)` |
| 结构体读出来乱码 | **BootLoader 和 APP 的 `main.h` 不一致** | 两边改成完全一样 |

## 9.4 W25Q128 问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 读 ID 不是 `0xEF4018` | SPI 没通 / CS 错 | 检查 PB12 是否被拉低 |
| 读出来全 FF | 从没写过（正常） | A 区下载后会写 |
| 读出来全 00 | SPI 模式错 | 检查 CPOL/CPHA（W25Q128 是 Mode 0 或 3） |

---

# 十、答辩准备

## 10.1 讲稿结构（3 分钟）

### 第 1 分钟：为什么需要 BootLoader

> "STM32 不能自己擦除自己正在运行的 Flash，所以需要一段独立的程序来做升级。
> 我们把 512KB Flash 分成两块：BootLoader 占 32KB，应用区占 480KB。"

### 第 2 分钟：升级流程

> "上电后 BootLoader 先检查 EEPROM 里的 OTA 标志。
> 如果有标志，说明有新固件待安装，就校验 W25Q128 里的固件，
> 通过后擦除应用区、搬移固件、重启。
> 如果没有标志，直接跳转到应用区。"

### 第 3 分钟：跳转的关键点

> "跳转有三个关键：设栈顶、设向量表、关中断。漏掉任何一个都会跑飞。
> 我们还加了固件 MD5 校验，原例程没做这个，数据坏了也照搬，会变砖。"

## 10.2 可能被问到的问题

**Q：为什么 BootLoader 要独立一个工程？**
> 因为它要操作 Flash，如果和应用在一个工程里，擦除时会把自己擦掉。

**Q：跳转为什么要设栈顶？**
> APP 的栈顶地址存在它的第一个字里，BootLoader 的栈顶不一样，必须重新设。

**Q：向量表为什么要重定位？**
> 中断向量表默认在 `0x08000000`，但 APP 在 `0x08008000`，不重定位的话中断会跳到 BootLoader 的向量表。

**Q：F103 和 G0 的 Flash 编程有什么不同？**
> G0 是 8 字节双字编程，F103 是 2 字节半字编程。移植时必须改，直接照搬会写坏数据。

**Q：升级失败会怎样？**
> 校验失败就不搬移，继续用旧固件，不会变砖。真变砖了还能进串口菜单用 Xmodem 救。

**Q：为什么 BootLoader 要 32KB？**
> 标准库 + 串口 + I2C + Flash 驱动编译后约 20KB，留 12KB 余量。
> 如果以后要加功能（比如 CAN 升级），还有空间。

---

# 十一、常用参考

| 内容 | 位置 |
|---|---|
| 标准库 | `E:\桌面\板子\STM开发资料\STM32F10x_StdPeriph_Lib_V3.5.0\` |
| 原例程 BootLoader | `E:\BaiduNetdiskDownload\新建文件夹\0-BootLoader区例程\` |
| MD5 源码 | `36号工程\HMAC\scr\utils_md5.c` |
| 板子引脚表 | `outputs/板子资料档案.md` |
| 你的基础工程 | `E:\MY_project\STM32F103ZET6_Demo10DMA` |

---

# 一句话记住

**让下载的固件能真正跑起来，而且跑坏了还能救回来。**
