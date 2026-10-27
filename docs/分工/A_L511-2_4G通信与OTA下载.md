# 【成员 A】L511-2 4G 通信 + HTTP OTA 下载

> 项目：战神V1（STM32F103ZET6）+ 移柯 L511-2 + 阿里云 OTA
> 基础工程：`E:\MY_project\STM32F103ZET6_Demo10DMA`（裸机版）
> 你的角色：**核心模块负责人**，答辩的得分点
> 工期：17 天 | 难度：★★★★★

---

# 目录

1. [你负责什么](#一你负责什么)
2. [你的基础：哪些已有、哪些要写](#二你的基础哪些已有哪些要写)
3. [第 0 天：准备工作](#三第-0-天准备工作)
4. [第 1 周：把模块摸透](#四第-1-周把模块摸透)
5. [第 2 周：写 4G 驱动](#五第-2-周写-4g-驱动)
6. [第 3 周：HTTP 下载](#六第-3-周http-下载)
7. [第 4 周：联调](#七第-4-周联调)
8. [任务总表](#八任务总表)
9. [排错手册](#九排错手册)
10. [答辩准备](#十答辩准备)

---

# 一、你负责什么

**让 STM32 通过 L511-2 模块，用 HTTP 从阿里云下载固件，并写进 W25Q128。**

拆成 4 个阶段：

| 阶段 | 目标 | 判断标准 |
|---|---|---|
| ① 联网 | 模块能开机、注册网络、拿到 IP | `AT+SAPBR=2,1` 返回 IP 地址 |
| ② 控制 | MCU 能发 AT 指令并收到回复 | `AT` 能收到 `OK` |
| ③ 下载 | 能 HTTP 分片下载完整文件 | 下载的数据和原文件一致 |
| ④ 存储 | 写进 W25Q128 且校验一致 | ST-Link 读出与原始 bin 逐字节相同 |

**最终交付**：一段代码，调用 `CAT1_OTA(url, len)` 就能把固件下到 W25Q128。

---

# 二、你的基础：哪些已有、哪些要写

## 2.1 已有的（直接用，不要重写）

| 模块 | 文件 | 你能用的函数 |
|---|---|---|
| **USART3 驱动** | `user/api/usart.c` | `Usart3_Init(brr)` / `Usart3_SendByte()` / `Usart3_SendBuffer()` / `Usart3_SendString()` |
| **接收缓冲** | `user/api/usart.h` | `usart3_value.data[]` / `.count` / `.flag` |
| **延时** | `user/api/delay.c` | `delay_ms(ms)` |
| **LED** | `user/api/led.c` | `Led_Low(Led_Red)` / `Led_High()` / `Led0_Toggle` |
| **公共宏** | `user/api/common.h` | `RCC_APB2_GPIOF_CLOCK_ENABLE` 等 |

## 2.2 关键确认：USART3 引脚正好对

你的 `usart.c` 第 495-499 行：

```c
GPIOB->CRH |= (Alternate_Push_And_Pull_Speed_2MHz << 8);   // PB10 = TX
GPIOB->CRH |= (Floating_Input_Mode << 12);                 // PB11 = RX
```

**PB10=TX / PB11=RX，正好是 4G 模块要接的，不用改。**

而且带 **RXNE + IDLE 双中断**：
- RXNE：收到一个字节
- IDLE：总线空闲，说明"这一帧收完了"——**正好适合 AT 指令解析**

## 2.3 要自己写的

| 文件 | 内容 | 说明 |
|---|---|---|
| `cat1.h` | 引脚定义、函数声明 | 新建 |
| `cat1.c` | GPIO 初始化、开机时序、AT 收发、GPRS、HTTP 下载 | 新建，约 400 行 |

## 2.4 要改的（隐患）

| 位置 | 现状 | 改成 | 为什么 |
|---|---|---|---|
| `usart.h` | `USART3_BUFFER_SIZE (256)` | **`(1024)`** | HTTP 一次返回 274 字节，256 会截断 |
| `usart.c` 的 `USART3_IRQHandler` | 中断里处理 PWM/蜂鸣器/LED | **只收数据置 flag** | 4G 数据会误触发这些逻辑 |

---

# 三、第 0 天：准备工作

## 3.1 硬件检查清单

| 检查项 | 怎么检查 | 期望 |
|---|---|---|
| 4G 模块形态 | 看外观 | 带底板、有排针、有 SIM 卡座 |
| 天线 | 看 IPEX 座 | 已接天线 |
| SIM 卡 | 插卡 | Nano-SIM，物联网卡或手机卡 |
| 电源 | 万用表量 | 独立 3.8V/2A 供电 |
| 电平转换 | 待测（见 3.2） | 1.8V 则需要 TXB0108 |
| P5 跳线 | 看板子 | **必须拔掉** |

## 3.2 第一步：量 TXD 电压（决定要不要电平转换）

**这是最重要的一步，做错会烧模块。**

**操作**：
1. 只给 4G 模块供电，**不接单片机**
2. 让它开机（PWRKEY 拉低 1 秒以上）
3. 万用表打**直流电压档（20V 档）**
4. 黑表笔接模块 **GND**，红表笔点模块 **TXD 排针**

**判断表**：

| 测到电压 | 结论 | 你要做什么 |
|---|---|---|
| **≈1.8V** | 模块是 1.8V 电平 | ⚠️ **必须买 TXB0108 电平转换模块** |
| **≈3.3V** | 底板已内置电平转换 | ✅ 可以直连 PB10/PB11 |
| ≈0V | 没开机 / 测错脚 | 先确认 PWR 灯亮 |
| 乱跳 | 万用表接触不良 | 重新测 |

**原理**：串口 TXD 空闲时一直是高电平，所以空载量它的电压就是它的电平域。

> **把测到的值记在实验报告里**，答辩可能问到。

## 3.3 第二步：USB-TTL 单独测模块

**在接单片机之前，先用电脑确认模块是好的。** 这一步能省你几天。

**需要**：1.8V 电平的 USB-TTL（CH340 有 1.8V/3.3V 跳线的型号）

**接线**：
```
USB-TTL(1.8V)      L511 底板
TXD      ────────► RXD
RXD      ◄──────── TXD
GND      ───────── GND
（模块 VBAT 独立供电，不要从 USB-TTL 取电）
```

**串口助手设置**：
- 波特率 **115200**
- 数据位 8，停止位 1，无校验
- **勾选"发送新行"**（很重要，不勾 AT 指令不生效）

**开机方法**：
- 底板上有按键 → 长按 2 秒
- 没有按键 → 杜邦线把 PWRKEY 短接 GND 1-2 秒

**开机成功的标志**：
1. 发 `AT` 收到 `OK`
2. **NET 灯**开始闪：
   - 64ms 亮 / 3000ms 灭 = **已注册网络**（成功）
   - 64ms 亮 / 800ms 灭 = 正在找网（等一下）
   - 常灭 = 没开机

## 3.4 第三步：逐条验证 AT 指令（截图存证）

**按顺序发，每条确认 OK 再下一条。**

### 第 1 组：基础检查（3GPP 标准，肯定通用）

```
AT                    → OK
ATI                   → 厂商/型号/版本信息
AT+CPIN?              → +CPIN: READY
AT+CSQ                → +CSQ: 25,99
AT+CREG?              → +CREG: 0,1
AT+CGATT?             → +CGATT: 1
```

**怎么判断**：

| 指令 | 正常返回 | 异常返回 | 异常怎么办 |
|---|---|---|---|
| `AT` | `OK` | 无回复 | TX/RX 接反 / 波特率错 / 没开机 |
| `AT+CPIN?` | `+CPIN: READY` | `+CME ERROR: 10` | SIM 卡没插好 |
| `AT+CSQ` | `+CSQ: 20,99`（第一个数 >10） | `+CSQ: 99,99` | 没信号，接天线 |
| `AT+CREG?` | `+CREG: 0,1` 或 `0,5` | `+CREG: 0,2` | 正在找网，等 10-30 秒 |
| `AT+CGATT?` | `+CGATT: 1` | `+CGATT: 0` | 没附着，等或发 `AT+CGATT=1` |

### 第 2 组：GPRS 承载（关键验证）

```
AT+SAPBR=3,1,"CONTYPE","GPRS"     → OK
AT+SAPBR=3,1,"APN",""             → OK
AT+SAPBR=1,1                      → OK
AT+SAPBR=2,1                      → +SAPBR: 1,1,"10.x.x.x"
```

**最后一条返回 IP 地址就成功了。**

**APN 填什么**（如果留空不行）：
- 移动：`CMNET` 或 `CMIOT`
- 联通：`3GNET`
- 电信：`CTNET`

### 第 3 组：HTTP（最关键验证）

**前提**：先准备一个能下载的小文件（几 KB），放在某个 HTTP 服务器上。

```
AT+HTTPINIT                              → OK
AT+HTTPPARA="CID",1                      → OK
AT+HTTPPARA="URL","http://xxx/test.bin"  → OK
AT+HTTPACTION=0                          → OK
   （等几秒，模块主动上报）
   +HTTPACTION: 0,200,1024               （0=GET, 200=成功, 1024=长度）
AT+HTTPREAD=0,256                        → +HTTPREAD: 256
                                            <256 字节二进制数据>
AT+HTTPTERM                              → OK
```

**返回码含义**：

| 返回 | 含义 | 怎么办 |
|---|---|---|
| `+HTTPACTION: 0,200,1024` | 成功，文件 1024 字节 | ✅ |
| `+HTTPACTION: 0,404,0` | URL 错了 | 用浏览器先确认能下 |
| `+HTTPACTION: 0,601,0` | 网络未就绪 | 检查 SAPBR 是否激活 |
| `+HTTPACTION: 0,200,0` | 文件为空 | 确认文件真实存在 |
| `ERROR` | 指令不支持 | 🚨 **门禁 1 失败** |

> ## 🚨 门禁 1（第一周必须通过）
> **如果 `AT+SAPBR=1,1` 或 `AT+HTTPINIT` 返回 ERROR，立刻通知全组！**
>
> 说明移柯改了 AT 指令集，HTTP 方案不可行，要改走 MQTT 分片路线（工作量大增）。
> 这是全项目最大风险点。

## 3.5 第四步：截图存档

**把每一组的正常回复都截图，整理成一个文档。**

后面写代码出问题时，对比截图能快速判断：
- 是模块问题？（截图里明明是好的）
- 还是代码问题？（指令发错了）

---

# 四、第 1 周：把模块摸透

## Day 1-2：USB-TTL 阶段（不碰单片机）

| 任务 | 具体做什么 | 完成标准 |
|---|---|---|
| 量 TXD 电压 | 见 3.2 | 记录数值 |
| 模块开机 | PWRKEY 拉低 1 秒 | `AT` 收到 OK，NET 灯闪 |
| 基础 AT 指令 | 6 条逐条跑 | 全部正常，截图 |
| **SAPBR 验证** | 4 条指令 | 拿到 IP 地址 |
| **HTTP 验证** | 7 条指令 | 能读出文件内容 |
| 下载小文件 | 完整读一遍 | 内容正确 |

> **这 2 天最关键。** 模块本身有问题的话，越早发现越好。

## Day 3-5：接线 + 单片机点亮

### 接线表

**电源（先接）**：
```
5V/2A 适配器
    │
    ├── DC-DC 降压 → 3.8V / ≥2A
    │        │
    │        ├── 100μF 电解 + 100nF 陶瓷 → 模块 VBAT
    │        └── 模块 GND ──┬── 开发板 GND（必须共地！）
    │                       └── 适配器 GND
```

**串口（3.3V 情况）**：
```
模块 TXD ────── PB11 (USART3_RX)
模块 RXD ────── PB10 (USART3_TX)
模块 GND ────── GND
```

**串口（1.8V 情况，需要 TXB0108）**：
```
模块 TXD ── TXB0108 A侧 ── B侧 ── PB11
模块 RXD ── TXB0108 A侧 ── B侧 ── PB10
TXB0108: VCCA=1.8V(接模块VDD_EXT)  VCCB=3.3V  GND=共地  OE=VCCB
```

**控制脚**：

| 模块引脚 | 战神V1 | GPIO 模式 |
|---|---|---|
| PWRKEY | **PF0** | 开漏输出（`GPIO_Mode_Out_OD`） |
| RESET | **PF1** | 推挽输出 |
| NET_STATUS | **PF2** | 上拉输入（`GPIO_Mode_IPU`） |
| STATUS | **PF3** | 上拉输入 |

> **PWRKEY 为什么用开漏**：模块内部有 1.8V 上拉，开漏输出只拉低不放高，避免 3.3V 倒灌。

**为什么用 PF0~PF3**：战神V1 引脚表标注这几个"未接任何外设，完全独立"。
不用 PC0~PC3 是因为它们接了 OV 摄像头数据线。

### 接线检查清单

- [ ] P5 跳线帽已拔
- [ ] 模块独立供电，**与开发板共地**
- [ ] 电源有 100μF 电容
- [ ] 天线接好
- [ ] SIM 卡插好
- [ ] TX/RX **交叉**接（模块 TX → MCU RX）
- [ ] 控制脚按上表接

### Day 3-5 任务

| 任务 | 具体做什么 | 完成标准 |
|---|---|---|
| GPIO 初始化 | 写 `CAT1_GPIO_Init()`，PF0~PF3 | 万用表量电平正确 |
| 开机时序 | PWRKEY 拉低 1.5s → 释放 → 等 NET_STA 变低 | 串口打印"网络已注册" |
| AT 收发框架 | 基于 `usart3_value.flag` 写 `CAT1_SendAT()` | `CAT1_SendAT("AT","OK",1000)` 返回 0 |

---

# 五、第 2 周：写 4G 驱动

## 5.1 先修隐患（Day 6，0.5 天）

### 隐患 1：缓冲改大

`user/api/usart.h`：

```c
/* 改前 */
#define USART3_BUFFER_SIZE (256)

/* 改后 */
#define USART3_BUFFER_SIZE (1024)
```

**为什么**：HTTP 下载一次返回：
```
+HTTPREAD: 256\r\n    16 字节
<256 字节二进制>       256 字节
\r\n                  2 字节
─────────────────────────────
总计                 274 字节 > 256
```

**不改的后果**：数据截断，下载的固件和原始 bin 对不上，而且**看不出哪错了**——非常难查。

### 隐患 2：中断去业务逻辑

`user/api/usart.c` 的 `USART3_IRQHandler`，现在是这样：

```c
/* 现状：中断里处理 PWM、蜂鸣器、LED */
if (strstr(usart3_value.data + 3, "\"PWM\":0")) TIM3_CH4_PB1_PWM_Set_CCR4(1500);
if (strstr(usart3_value.data + 3, "\"Beep\":true")) Beep_On;
if (strstr(usart3_value.data + 3, "\"Led_Red\":true")) Led_Low(Led_Red);
```

**问题**：这些是给 WiFi 模块用的。4G 的 AT 指令数据里如果碰巧含 `"PWM"` 字样，会误触发。

**改成**：

```c
void USART3_IRQHandler(void)
{
    /* 1. RXNE：收一个字节 */
    if (USART3->SR & (0x01 << 5)){
        if(usart3_value.count < USART3_BUFFER_SIZE){
            usart3_value.data[usart3_value.count++] = USART3->DR;
        }else{
            USART3->DR;     /* 缓冲满，读掉丢弃 */
        }
    }

    /* 2. IDLE：一帧收完 */
    if (USART3->SR & (0x01 << 4)){
        USART3->DR;             /* 清标志 */
        usart3_value.flag = 1;  /* 只置标志，业务逻辑移到主循环 */
    }
}
```

**业务逻辑移到主循环**：

```c
while(1){
    if(usart3_value.flag){
        /* 在这里处理业务 */
        U3_EventDispatch(usart3_value.data, usart3_value.count);
        usart3_value.flag = 0;
        usart3_value.count = 0;
    }
}
```

## 5.2 AT 收发框架（Day 6-8，2 天）

### 核心设计

你的串口是**标志位模式**：收到一帧数据后 `usart3_value.flag = 1`。

AT 收发要利用这个标志：

```c
/**
 * 发送 AT 指令并等待期望回复
 * @param cmd         指令（不含 \r\n，函数自动加）
 * @param expect      期望关键字，如 "OK"
 * @param timeout_ms  超时（毫秒）
 * @return 0=收到期望回复  1=超时
 */
u8 CAT1_SendAT(char *cmd, char *expect, u16 timeout_ms)
{
    u16 i;

    /* 1. 清标志和计数 */
    usart3_value.flag  = 0;
    usart3_value.count = 0;

    /* 2. 发送指令（注意加 \r\n） */
    Usart3_SendString(cmd);
    Usart3_SendString("\r\n");

    /* 3. 循环等待 */
    for(i = 0; i < timeout_ms; i++){
        delay_ms(1);

        if(usart3_value.flag){
            /* 收到一帧，检查是否含期望关键字 */
            if(strstr((char*)usart3_value.data, expect)){
                return 0;       /* 成功 */
            }

            /* 不是期望的，清掉继续等下一帧 */
            usart3_value.flag  = 0;
            usart3_value.count = 0;
        }
    }

    return 1;   /* 超时 */
}
```

### 三个注意点

**注意 1：`strstr` 需要 `char*`**

`usart3_value.data` 是 `u8` 数组，必须强制转换：
```c
strstr((char*)usart3_value.data, expect)   /* ✅ */
strstr(usart3_value.data, expect)          /* ❌ 编译警告 */
```

**注意 2：发指令必须加 `\r\n`**

AT 指令以回车换行结尾，不发的话模块不响应。

**注意 3：每条指令前要清缓冲**

否则上一条的残留数据会干扰判断。

### 验证方法

```c
/* 测试代码 */
void CAT1_Test(void){
    Usart1_SendString("\r\n===== AT 测试 =====\r\n");

    if(CAT1_SendAT("AT", "OK", 1000) == 0){
        Usart1_SendString("AT -> OK\r\n");
    }else{
        Usart1_SendString("AT -> 超时\r\n");
    }

    if(CAT1_SendAT("AT+CPIN?", "READY", 2000) == 0){
        Usart1_SendString("SIM 卡正常\r\n");
    }else{
        Usart1_SendString("SIM 卡异常\r\n");
    }
}
```

**期望输出**：
```
===== AT 测试 =====
AT -> OK
SIM 卡正常
```

## 5.3 GPRS 承载封装（Day 9-10，1 天）

把 4 条 SAPBR 指令封装成一个函数，返回 0 表示成功：

```c
/**
 * 激活 GPRS 承载（拿到 IP）
 * @return 0=成功  其他=失败步骤号
 */
u8 CAT1_ActiveGPRS(void)
{
    Usart1_SendString("[4G] 激活 GPRS...\r\n");

    /* 1. 设置承载类型 */
    if(CAT1_SendAT("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", "OK", 3000)){
        return 1;
    }

    /* 2. 设置 APN（留空自动获取） */
    if(CAT1_SendAT("AT+SAPBR=3,1,\"APN\",\"\"", "OK", 3000)){
        return 2;
    }

    /* 3. 激活承载（这条要等久一点，10-20 秒） */
    if(CAT1_SendAT("AT+SAPBR=1,1", "OK", 20000)){
        return 3;
    }

    /* 4. 查询状态（应返回 +SAPBR: 1,1,"IP"） */
    if(CAT1_SendAT("AT+SAPBR=2,1", "+SAPBR: 1,1", 5000)){
        return 4;
    }

    Usart1_SendString("[4G] GPRS 激活成功\r\n");
    return 0;
}
```

**调试技巧**：每一步都 `printf` 出来，卡在哪一步一眼看出。

## 5.4 开机时序（Day 5，1 天）

L511-2 规格书：**PWRKEY 拉低超过 1 秒然后释放**。

```c
/**
 * 模块开机
 * @param timeout  等待注册网络的超时（100ms 为单位）
 * @return 0=成功  1=超时
 */
u8 CAT1_PowerOn(u16 timeout)
{
    Usart1_SendString("[4G] 开始开机...\r\n");

    /* 1. 拉低 PWRKEY 1.5 秒（规格书要求 >1s） */
    GPIO_ResetBits(GPIOF, GPIO_Pin_0);
    delay_ms(1500);
    GPIO_SetBits(GPIOF, GPIO_Pin_0);     /* 释放 */

    /* 2. 等模块启动 */
    delay_ms(3000);

    /* 3. 等网络注册（NET_STA 变低） */
    Usart1_SendString("[4G] 等待网络注册");
    while(timeout--){
        delay_ms(100);
        Usart1_SendString(".");

        if(GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_2) == 0){
            Usart1_SendString("\r\n[4G] 网络已注册\r\n");
            return 0;
        }
    }

    Usart1_SendString("\r\n[4G] 注册超时\r\n");
    return 1;
}
```

---

# 六、第 3 周：HTTP 下载

## 6.1 HTTP 初始化（Day 11-13，3 天）

```c
/**
 * 初始化 HTTP 并设置 URL
 * @param url  完整下载地址（http:// 开头）
 * @return 0=成功  其他=失败步骤号
 */
u8 CAT1_HttpInit(char *url)
{
    char cmd[600];

    /* 1. 初始化 HTTP */
    if(CAT1_SendAT("AT+HTTPINIT", "OK", 5000)) return 1;

    /* 2. 设置承载 ID */
    if(CAT1_SendAT("AT+HTTPPARA=\"CID\",1", "OK", 3000)) return 2;

    /* 3. 设置 URL */
    sprintf(cmd, "AT+HTTPPARA=\"URL\",\"%s\"", url);
    if(CAT1_SendAT(cmd, "OK", 5000)) return 3;

    /* 4. 发起 GET */
    if(CAT1_SendAT("AT+HTTPACTION=0", "OK", 5000)) return 4;

    /* 5. 等服务器响应（最多 30 秒） */
    {
        u16 t = 300;
        while(t--){
            delay_ms(100);

            if(strstr((char*)usart3_value.data, "+HTTPACTION: 0,200")){
                return 0;       /* 成功 */
            }
            if(strstr((char*)usart3_value.data, "+HTTPACTION: 0,")){
                return 5;       /* 服务器返回错误 */
            }
        }
    }

    return 6;   /* 超时 */
}
```

## 6.2 分片读取（核心）

```c
/**
 * 读取固件的一段
 * @param offset  文件偏移
 * @param len     读取长度（≤256）
 * @param outbuf  输出缓冲区（至少 len 字节）
 * @return 0=成功
 */
u8 CAT1_HttpRead(u32 offset, u16 len, u8 *outbuf)
{
    char cmd[64];
    char expect[32];
    u8 *ptr;

    sprintf(cmd, "AT+HTTPREAD=%lu,%u", offset, len);
    sprintf(expect, "+HTTPREAD: %u", len);

    if(CAT1_SendAT(cmd, expect, 5000)) return 1;

    /* 定位数据起点 */
    ptr = (u8*)strstr((char*)usart3_value.data, expect);
    if(ptr == NULL) return 2;

    ptr += strlen(expect);
    while(*ptr == '\r' || *ptr == '\n') ptr++;   /* 跳过换行 */

    /* 定长拷贝（二进制数据，不能用 strcpy！） */
    memcpy(outbuf, ptr, len);

    return 0;
}
```

### ⚠️ 关键：不能用 strcpy

HTTPREAD 返回的是**二进制数据**，里面有 `0x00` 字节。

```c
memcpy(outbuf, ptr, len);    /* ✅ 正确：定长拷贝 */
strcpy(outbuf, ptr);         /* ❌ 错误：遇到 0x00 就停 */
```

## 6.3 写 W25Q128（Day 14-15，1.5 天）

```c
/**
 * OTA 下载主流程
 * @param url     固件下载地址
 * @param fw_len  固件总长度
 * @return 0=成功
 */
u8 CAT1_OTA(char *url, u32 fw_len)
{
    u32 offset;
    u16 len, page = 0;
    u8  buf[256];

    Usart1_SendString("\r\n===== 开始 OTA 下载 =====\r\n");
    Usart1_SendString("[4G] 固件大小：");
    Usart1_SendNumber(fw_len);
    Usart1_SendString(" 字节\r\n");

    /* 1. 开机 + 联网 */
    if(CAT1_PowerOn(600)) return 1;
    if(CAT1_ActiveGPRS()) return 2;

    /* 2. 初始化 HTTP */
    if(CAT1_HttpInit(url)) return 3;

    /* 3. 擦除 W25Q128 第 0 块（64KB） */
    Usart1_SendString("[4G] 擦除 W25Q128...\r\n");
    W25Q128_EraseBlock(0);

    /* 4. 分片下载 */
    Usart1_SendString("[4G] 开始下载\r\n");
    for(offset = 0; offset < fw_len; offset += 256){
        len = (fw_len - offset >= 256) ? 256 : (fw_len - offset);

        if(CAT1_HttpRead(offset, len, buf)){
            Usart1_SendString("[4G] 下载失败\r\n");
            CAT1_SendAT("AT+HTTPTERM", "OK", 3000);
            return 4;
        }

        /* 写入 W25Q128 一页 */
        W25Q128_WritePage(page++, buf);

        /* 进度 */
        if((offset % 4096) == 0){
            Usart1_SendString("[4G] 已下载 ");
            Usart1_SendNumber(offset);
            Usart1_SendString(" / ");
            Usart1_SendNumber(fw_len);
            Usart1_SendString("\r\n");
        }
    }

    /* 5. 终止 HTTP */
    CAT1_SendAT("AT+HTTPTERM", "OK", 3000);

    Usart1_SendString("[4G] 下载完成\r\n");
    return 0;
}
```

## 6.4 验证：ST-Link 比对（关键）

下载完成后，**必须验证数据正确**：

1. 用 ST-Link + STM32CubeProgrammer 连接板子
2. 读 W25Q128 的内容（通过 ST-Link 的 SPI 读，或写个测试函数把数据读到内存再打印）
3. 与原始 bin 逐字节比对

**更简单的方法**：写个函数把 W25Q128 前 N 字节通过串口打印成 hex，和原始 bin 的 hex 对比。

```c
void CAT1_DumpFlash(u32 len)
{
    u32 i;
    u8 buf[16];
    u8 j;

    for(i = 0; i < len; i += 16){
        W25Q128_Read(i, buf, 16);
        Usart1_SendString("0x");
        /* 打印地址 */
        for(j = 0; j < 16; j++){
            /* 打印 hex */
            Usart1_SendHexByte(buf[j]);
            Usart1_SendString(" ");
        }
        Usart1_SendString("\r\n");
    }
}
```

---

# 七、第 4 周：联调

## 7.1 联调任务

| 任务 | 具体做什么 | 完成标准 |
|---|---|---|
| 集成到主循环 | 把 `CAT1_OTA()` 接入 `main.c` | 不影响其他功能 |
| 处理异常 | 超时、断线、数据错位 | 失败能正确返回 |
| 连续测试 | 至少 5 次完整升级 | 成功率 100% |
| 配合 D | 解决集成冲突 | 工程能编译运行 |

## 7.2 主循环集成

```c
int main(void)
{
    /* 初始化 */
    Usart1_Init(115200);
    Usart3_Init(115200);
    CAT1_GPIO_Init();
    W25Q128_Init();

    Usart1_SendString("\r\n===== 4G OTA 客户端 =====\r\n");

    while(1){
        /* 1. 处理 4G 接收 */
        if(usart3_value.flag){
            U3_EventDispatch(usart3_value.data, usart3_value.count);
            usart3_value.flag  = 0;
            usart3_value.count = 0;
        }

        /* 2. 处理 OTA 事件（由 MQTT 收到推送触发） */
        if(SysEventFlag & OTA_EVENT){
            SysEventFlag &= ~OTA_EVENT;
            CAT1_OTA(URL_ABuff, AliInfoCB.OTA_firelen);
        }
    }
}
```

---

# 八、任务总表

| # | 任务 | 工期 | 完成标准 | 检查点 |
|---|---|---|---:|---|
| A1 | 修串口隐患 | 0.5 | 缓冲 1024；中断只收数据 | W0 |
| A2 | 确认模块硬件 | 2 | TXD 电压已测；USB-TTL 测通 AT | W1 |
| A3 | **验证 AT 指令** | 1 | **门禁 1：SAPBR + HTTP 都返回 OK** | W1 |
| A4 | 4G GPIO 初始化 | 0.5 | PF0~PF3 电平正确 | W2 |
| A5 | 开机时序 | 1 | MCU 能开机并注册网络 | W2 |
| A6 | AT 收发框架 | 2 | `CAT1_SendAT()` 可用 | W2 |
| A7 | GPRS 承载封装 | 1 | 能拿到 IP | W2 |
| A8 | HTTP 下载 | 3 | 能下载完整固件 | W3 |
| A9 | 写 W25Q128 | 1.5 | **ST-Link 读出与原始 bin 一致** | W3 |
| A10 | OTA 报文解析 | 1 | 能提取 size/version/url/md5 | W3 |
| A11 | 主循环集成 | 1 | 不影响其他功能 | W4 |
| A12 | 联调排错 | 3 | 连续 5 次成功 | W4 |
| | **合计** | **17** | | |

---

# 九、排错手册

## 9.1 模块无响应

| 现象 | 排查顺序 |
|---|---|
| 发 AT 完全无回复 | ① TX/RX 接反 ② 波特率错（试 115200/9600）③ 模块没开机（看 PWR 灯）④ 电平不匹配 |
| 收到乱码 | ① 波特率错 ② 电平不匹配（1.8V 直连 3.3V）③ 没共地 |
| AT 有回复但偶尔丢字符 | ① 波特率误差 ② 中断优先级 ③ 接收缓冲太小 |
| 有时通有时不通 | **电源不足**，加 100μF 电容，改独立供电 |

## 9.2 网络注册问题

| 现象 | 原因 | 解决 |
|---|---|---|
| `+CME ERROR: 10` | SIM 卡没插好 | 重插，注意正反面 |
| `+CSQ: 99,99` | 无信号 | 接天线，靠近窗户 |
| `+CREG: 0,2` 一直不变 | ① 卡欠费 ② 模块频段不匹配（海外版） | 换卡；确认模块是 L511C |
| `+CGATT: 0` | 没附着网络 | 等一会儿；`AT+CGATT=1` 手动附着 |
| `AT+SAPBR=1,1` 一直失败 | APN 错 / 卡欠费 | 试 `CMNET`/`3GNET`/`CTNET` |

## 9.3 HTTP 问题

| 现象 | 原因 | 解决 |
|---|---|---|
| `AT+HTTPINIT` → ERROR | 上次没终止 | 先 `AT+HTTPTERM` 再 INIT |
| `+HTTPACTION: 0,601,0` | 网络未就绪 | 检查 SAPBR |
| `+HTTPACTION: 0,404,0` | URL 错 | 浏览器先确认能下 |
| `+HTTPACTION: 0,200,0` | 文件为空 | 换直链 |
| `AT+HTTPREAD` 超时 | 数据量大 / 信号差 | 分片改 128 |
| **下载的数据和原始 bin 不一致** | ① 缓冲截断（256→1024）② 分片偏移算错 ③ 最后一片处理错 | 打印每片偏移核对 |

## 9.4 电源问题（最隐蔽）

| 现象 | 说明 |
|---|---|
| 模块反复重启 | 供电不足，NET 灯反复亮灭 |
| AT 有时通有时不通 | 电压跌落，加电容 |
| 发射瞬间开发板复位 | 从开发板取电了，改独立供电 |

**排查方法**：万用表量模块 VBAT，发射瞬间掉到 3.0V 以下就是电源问题。

## 9.5 编译问题

| 现象 | 原因 | 解决 |
|---|---|---|
| `strstr` 警告 | `u8*` 传给了 `char*` | 强制转换 `(char*)` |
| `sprintf` 未定义 | 没包含 `<stdio.h>` | 加 `#include <stdio.h>` |
| 程序卡死 | 中断里死循环 | 检查 `while` 等待有没有超时 |

---

# 十、答辩准备

## 10.1 讲稿结构（4 分钟）

### 第 1 分钟：整体方案

> "我们做的是基于 4G 的远程固件升级。设备通过 L511-2 模块连接阿里云，
> 云端推送新固件地址，设备用 HTTP 下载固件存到外部 Flash，
> 然后重启由 BootLoader 搬到运行区。"

### 第 2 分钟：4G 联网流程

> "联网分三步：SIM 卡识别 → 网络注册 → GPRS 承载激活。
> 代码里对应 AT+CPIN、AT+CREG、AT+SAPBR 三组指令。
> 关键是要等 NET_STATUS 引脚变低，才说明真正注册成功。"

### 第 3 分钟：HTTP 分片下载

> "模块的 HTTP 是一次 GET 到模块内部缓存，再用 HTTPREAD 分片读出来。
> 我们每片读 256 字节，因为模块串口缓冲有限，片太大会丢数据。
> 读出来立即写进 W25Q128，这样内存占用只要 256 字节。"

### 第 4 分钟：难点与解决

> "最大的难点是模块是 1.8V 电平，直连 STM32 会烧 IO，我们加了电平转换。
> 另外电源必须独立供电，模块发射瞬间要 1.2A，从开发板取电会导致反复重启。"

## 10.2 建议准备的素材

| 素材 | 用途 |
|---|---|
| AT 指令时序图 | 讲联网流程 |
| 串口输出录屏 | 展示真实下载过程 |
| 下载进度截图 | 证明固件真的下来了 |
| W25Q128 比对截图 | 证明数据正确 |

## 10.3 可能被问到的问题

**Q：为什么用 HTTP 不用 MQTT 下载？**
> HTTP 实现简单，模块自带协议栈；MQTT 分片需要自己处理应答，代码复杂度高很多。

**Q：为什么分片 256 字节？**
> 模块串口接收缓冲有限，实测 256 稳定；512 偶尔丢数据。而且 256 正好是 W25Q128 一页的大小，写入方便。

**Q：下载失败怎么办？**
> 有超时重试；如果彻底失败就重启重来。固件先存到 W25Q128，不会影响正在运行的 APP。

**Q：怎么保证下载的固件是对的？**
> 固件尾部有 28 字节校验信息，包含长度和 MD5。BootLoader 搬移前会校验。

**Q：模块是 1.8V 为什么不能直连？**
> STM32 输出 3.3V 打进模块的 1.8V IO 会过压，可能击穿；反过来模块的 1.8V 输出达不到 STM32 的高电平门限，读不稳定。所以要加电平转换芯片。

---

# 十一、常用参考

| 内容 | 位置 |
|---|---|
| 标准库 | `E:\桌面\板子\STM开发资料\STM32F10x_StdPeriph_Lib_V3.5.0\` |
| 原例程 cat1.c | `E:\BaiduNetdiskDownload\新建文件夹\36-MQTT协议...\HARDWARE\Scr\cat1.c` |
| L511 规格书要点 | `outputs/_l511_ctx.txt` |
| 板子引脚表 | `outputs/板子资料档案.md` |
| 你的工程 | `E:\MY_project\STM32F103ZET6_Demo10DMA` |

---

# 一句话记住

**让板子能联网、能下载、能存对。这三件事做到了，你的部分就完成了。**
