# 战神V1（F103ZET6）+ 移柯 L511-2 4G模块 —— OTA 复刻完整方案


> ## ⚠️⚠️⚠️ 重要提示：本文档部分内容已过期 ⚠️⚠️⚠️
>
> **2026-10-27 起，平台从「阿里云 IoT」改为「巴法云（bemfa）」。**
>
> 本文档里凡是提到**阿里云、三元组（ProductKey/DeviceName/DeviceSecret）、
> HMAC-SHA1 签名、阿里云 MQTT Topic** 的内容，**全部作废**。
>
> **最新方案请看 → [巴法云方案重构_分工与实施.md](巴法云方案重构_分工与实施.md)**
>
> 本文档中**仍然有效的部分**：
> - 硬件接线、引脚定义、跳线配置
> - 4G 模块 AT 指令
> - Flash 分区方案
> - BootLoader 逻辑
> - OTA 原理和常见错误

---
> 目标：把「超子说物联网 36 号工程」（G070 + Air724 + 阿里云 HTTP OTA）
> 移植到 **战神V1（STM32F103ZET6）+ 移柯 L511-2**，用 **标准库** 从头实现
> 编写日期：2026-10-26

---

# ⚠️ 第零部分：先看这三个致命问题

## 问题 1：L511 的串口是 1.8V 电平，直连会烧

**官方规格书原文**（涂鸦 L511E-Y6E Module Datasheet）：

| 引脚 | 信号 | 电压域 |
|---|---|---|
| 17 | MAIN_RXD | **1.8V** |
| 18 | MAIN_TXD | **1.8V** |
| 16 | NET_STATUS | **1.8V** |
| 7 | PWRKEY | **1.8~2.2V** |
| 15 | RESET_N | **1.28V** |

规格书还明确写：

> 「模块串口与 AP 端电平匹配时……TXB0108RGYR」
> 「MAIN_RXD 建议添加 10K 上拉电阻到 1.8V。MAIN_TXD 建议添加 10K 上拉电阻到 1.8V。」

**STM32F103 的 GPIO 是 3.3V 电平。**

**直接对接的后果**：
- MCU 的 3.3V TX 打进模块 RXD（1.8V 域）→ **可能击穿模块 IO**
- 模块的 1.8V TX 送给 MCU（3.3V 域）→ **MCU 识别不到高电平**（1.8V < 2.0V VIH 门限，勉强临界，不稳定）

**必须做电平转换。**

## 问题 2：PWRKEY 是 1.8~2.2V，不能直接接 3.3V GPIO

规格书写明 PWRKEY「模块内部有上拉，典型值为 1.8~2.2V」。

MCU 输出 3.3V 高电平打进这个引脚，会通过内部上拉倒灌电流。虽然通常不会立刻烧，但长期不可靠。

**解法**：PWRKEY 用**开漏输出**（`GPIO_Mode_Out_OD`），或者串一个分压电阻。

## 问题 3：模块是 LCC+LGA 贴片封装，不是插针

L511-2 是 **109-pin LCC+LGA**，尺寸 15.8×17.7×2.3mm。

**你手里如果是裸模块，没有转接板是接不了线的。**

必须先确认：**你买的是「模块 + 转接底板」还是裸模块？**

---

# 第一部分：先确认你手里到底是什么

## 1.1 三种可能，处理方式完全不同

| 情况 | 外观 | 怎么办 |
|---|---|---|
| **A. 模块 + 开发底板** | 一块 PCB 上有 L511 + SIM 卡座 + 天线座 + 排针 | ✅ 最好，直接看底板丝印找引脚 |
| **B. 裸模块（LCC 贴片）** | 小方块，109 个焊盘，无排针 | ⚠️ 需要买 **L511 转接板**，或自己画板 |
| **C. 已焊在别的板子上** | 需要看是什么板 | 看板子原理图 |

**如果不知道是哪种，拍照发我，或者告诉我有没有排针、有没有 SIM 卡座。**

## 1.2 确认模块是 L511 的哪个子型号

规格书列出的 L511-Y6 系列：

| 型号 | Flash | 频段 | 国内能用？ |
|---|---|---|---|
| **L511C-Y6** | 2MB | TDD B34/38/39/40/41 + FDD B1/3/5/8 | ✅ 全网通 |
| **L511C-Y6E** | 4MB | 同上 | ✅ 全网通 |
| L511C-Y6MF | 2MB | TDD B34/38/39/40/41 + FDD B3/8 | ⚠️ 频段少 |
| L511C-Y6M | 2MB | TDD B34/38/39/40/41 | ⚠️ 只有移动 |
| **L511E-Y6 / Y6E** | 2MB/4MB | TDD B38/40/8 + FDD B1/3/5/7/8/20/28 | ❌ **海外版，国内频段不全** |
| L511J-Y6 | — | TDD B39/41 + FDD B1/3/8/18/19/26/28 | ❌ 日本版 |

> **`L511-2` 这个写法不完整**。要看模块标签上的完整型号（如 `L511C-Y6`）。
> 如果买到 `L511E`（海外版），**国内可能注册不上网络**，这是硬伤。

**请在模块标签上找到完整型号告诉我。**

## 1.3 关键：AT 指令集

L511 用的是**移芯（EIGENCOMM）EC 平台**（证据：规格书出现 `AT+ECPMUCFG=1,2` 指令）。

**好消息**：Air724 也是移芯 EC618 平台，两者的基础 AT 指令高度相似：
- 3GPP 标准指令（`AT`、`AT+CPIN?`、`AT+CSQ`、`AT+CREG?`、`AT+CGATT?`）→ **完全一样**
- `AT+SAPBR`（GPRS 承载）→ **移芯平台通用，大概率一样**
- `AT+HTTPINIT` / `AT+HTTPPARA` / `AT+HTTPACTION` / `AT+HTTPREAD` → **移芯平台通用，大概率一样**

**但这是推断，不是保证。** 移柯可能对 AT 做了裁剪或改动。

**必须验证**：拿到模块后，用 USB-TTL（**1.8V 电平的**）手工发指令测试。
移柯官网指令手册在 `https://www.lynq-iot.com/support/download` 的「指令手册」栏目（需登录）。

---

# 第二部分：硬件方案（含电平转换电路）

## 2.1 需要额外买的东西

| 器件 | 型号 | 数量 | 价格 | 用途 |
|---|---|---|---|---|
| **电平转换模块** | TXB0108 / TXS0108E 模块（8 通道双向） | 1 | ~8元 | 4G 串口 + 控制脚电平转换 |
| **L511 转接板** | 搜「L511 转接板 LCC 转 DIP」 | 1（若裸模块） | ~20元 | 把贴片转成排针 |
| **USB-TTL（1.8V）** | CH340 带 1.8V/3.3V 跳线 | 1 | ~15元 | 单独测试模块 |
| **5V 电源** | 5V 2A 适配器 | 1 | ~15元 | 模块独立供电 |
| **DC-DC 降压** | 5V→3.8V 2A（LM2596 或 MP1584 模块） | 1 | ~8元 | 给模块 3.8V |
| 电解电容 | 100μF / 330μF | 各 1 | ~2元 | 电源滤波 |

> **为什么不用 5V 直接供**：规格书写 VBAT 范围 3.3~4.5V，**推荐 3.8V**，且要求电源能提供 **1.2A**。
> 5V 超范围会烧模块。

## 2.2 电源方案（关键）

规格书原文：

> 「VBAT 为模块的主电源，其电压输入范围是 3.3V 到 4.5V，推荐电压为 3.8V。
> 在网络较差的环境下，天线会以最大功率发射，为了保证电源的稳定，
> **模块必须选择至少能够提供 1.2A 电流能力的电源**。
> 靠近模块的 VBAT 引脚建议使用低 ESR 的 **100uF 滤波电容**，
> 同时建议 VBAT 加至少 3 个（**100nF、33pF、10pF**）片层多层陶瓷电容。」

**推荐电路**：

```
5V/2A 适配器
    │
    ├── DC-DC 降压模块（输出 3.8V，≥2A）
    │        │
    │        ├── 100μF 电解 + 100nF + 33pF + 10pF → 模块 VBAT
    │        └── 模块 GND ──┬── 开发板 GND（必须共地）
    │                       └── 适配器 GND
    │
    └── 开发板 5V（可选，或用 USB 供电）
```

⚠️ **绝对不要从开发板的 3.3V 排针给模块供电**。3.3V LDO 通常只有 500mA，模块发射瞬间要 1.2A，会直接拉垮开发板导致复位。

## 2.3 接线表（最终版）

### 主串口（USART3，115200）

| L511 引脚 | 信号 | 电平转换 | 战神V1 |
|---|---|---|---|
| 18 | MAIN_TXD（1.8V 输出） | → A 侧 | **B 侧 → PB11（USART3_RX）** |
| 17 | MAIN_RXD（1.8V 输入） | ← A 侧 | **B 侧 ← PB10（USART3_TX）** |
| 1/10/95 | GND | — | GND |

> TXB0108 接线：A 侧 VCCA=1.8V（接模块 VDD_EXT），B 侧 VCCB=3.3V（接开发板 3.3V），OE 接 VCCB。

### 控制脚

| L511 引脚 | 信号 | 电平域 | 战神V1 | 接法 |
|---|---|---|---|---|
| 7 | PWRKEY | 1.8~2.2V | **PF0** | 经电平转换，或开漏 + 分压 |
| 15 | RESET_N | 1.28V | **PF1** | 经电平转换（或分压 3.3→1.3V） |
| 16 | NET_STATUS | 1.8V | **PF2** | 经电平转换（模块→MCU 单向） |
| 25 | STATUS | 1.8V | **PF3** | 经电平转换（可选） |

**如果不想用 8 通道转换芯片**，控制脚可以用电阻分压（简单但不太可靠）：

```
MCU 3.3V ── 10K ──┬── 模块 PWRKEY
                   │
                  4.7K
                   │
                  GND
（分压后约 1.05V，偏低；改 10K + 3.3K 分压约 0.82V，太低）
实际建议：10K + 4.7K → 3.3*4.7/(10+4.7) = 1.05V
```

> **更简单可靠的做法**：PWRKEY 用 **NPN 三极管**（如 S8050）做开漏驱动：
> ```
> MCU PF0 ── 1K ── 三极管 B
>                  三极管 E → GND
>                  三极管 C → 模块 PWRKEY
>                  模块 PWRKEY 内部已有 1.8V 上拉
> ```
> MCU 输出高 → 三极管导通 → PWRKEY 被拉低 → 开机。
> **这是最稳的，强烈推荐。**

### SIM 卡与天线

| 项 | 说明 |
|---|---|
| SIM 卡 | 模块底板一般自带卡座；裸模块需要自己接（USIM1: 11/12/13/14/95） |
| 天线 | 模块底板一般带 IPEX 座；需要配 4G 天线（注意频段） |

## 2.4 完整接线示意

```
                    ┌──────────────────┐
   5V/2A ──DC-DC──► │ VBAT  (3.8V)     │
        (3.8V)      │                  │
                    │  L511-2 模块      │
   开发板3.3V ──────│ VDD_EXT          │
                    │                  │
   PB10 (USART3_TX)─┤◄─ TXB0108 ───────┤ MAIN_RXD (1.8V)
   PB11 (USART3_RX)─┤── TXB0108 ──────►│ MAIN_TXD (1.8V)
                    │                  │
   PF0 ──1K── NPN ──┤◄─────────────────┤ PWRKEY
   PF1 ────────────┤── TXB0108 ──────►│ RESET_N
   PF2 ────────────┤◄─ TXB0108 ───────┤ NET_STATUS
                    │                  │
   GND ─────────────┤ GND              │
                    └──────────────────┘
```

---

# 第三部分：IO 分配表（最终版）

## 3.1 4G 模块占用的引脚

| 功能 | 战神V1 引脚 | 引脚号 | 方向 | 备注 |
|---|---|---|---|---|
| **4G_TX** | **PB10** | 69 | AF_PP | USART3_TX，需拔 P5 跳线 |
| **4G_RX** | **PB11** | 70 | IN_FLOATING | USART3_RX，需拔 P5 跳线 |
| **4G_PWRKEY** | **PF0** | 10 | Out_OD 或经 NPN | 完全独立 |
| **4G_RESET** | **PF1** | 11 | Out_PP | 完全独立 |
| **4G_NET_STA** | **PF2** | 12 | IPU | 完全独立 |
| **4G_STATUS** | **PF3** | 13 | IPU | 完全独立（可选） |

**为什么用 USART3**：
- USART1（PA9/PA10）是 CH340 调试口，必须保留
- USART2（PA2/PA3）接 RS485/RS232，且要 PD7 控方向，麻烦
- **USART3（PB10/PB11）** 的 P5 跳线本来就是给 WIFI/蓝牙模块预留的，拔掉跳线后完全独立

**操作**：拔掉板子上 **P5 的跳线帽**。

**为什么用 PF0~PF3**：引脚表明确标注「未接任何外设，完全独立」。
不用 PC0~PC3（原例程用的）是因为它们接了 OV 摄像头数据线。

## 3.2 保留的板载外设

| 器件 | 引脚 | 用途 | 负责 |
|---|---|---|---|
| USART1 | PA9/PA10 | 调试串口（CH340，P8） | D |
| W25Q128 | PB12(CS)/PB13/PB14/PB15 | SPI2，暂存固件 | D |
| AT24C02 | PB6(SCL)/PB7(SDA) | I2C1，存三元组+OTA标志 | D |
| LED0（红） | PB5 | 状态指示 | D |
| LED1（绿） | PE5 | 状态指示 | D |
| KEY0/1/2 | PE4/PE3/PE2 | 进入 BootLoader | D |
| RTC 晶振 | PC14/PC15 | 32.768K | B |

## 3.3 绝对不能动的引脚

| 引脚 | 原因 |
|---|---|
| PA13/PA14 | SWD 调试口 |
| PC14/PC15 | RTC 晶振 |
| PB12 | W25Q128 片选，悬空会让 SPI2 报废 |
| PB6/PB7 | 24C02 的 I2C，有 4.7K 上拉 |
| PA9/PA10 | 调试串口 |

---

# 第四部分：Flash 分区（F103 版）

## 4.1 芯片对比

| 项目 | G070（原例程） | **F103ZET6（你的板子）** |
|---|---|---|
| Flash | 128 KB | **512 KB** |
| Flash 页大小 | 2 KB | **2 KB（一样！）** |
| RAM | 36 KB | **64 KB** |
| 页数 | 64 | **256** |

**页大小相同是关键**：原例程按 2KB 擦除的逻辑可以直接沿用。

## 4.2 推荐分区

```
0x08000000 ┌──────────────────────────────┐
           │   BootLoader  (16 页 = 32KB) │
0x08008000 ├──────────────────────────────┤  ← A 区起始
           │   应用 A 区 (240 页 = 480KB) │
0x08080000 └──────────────────────────────┘
```

## 4.3 宏定义

```c
#define FLASH_BASE_ADDR       0x08000000
#define FLASH_PAGE_SIZE       2048
#define FLASH_TOTAL_PAGES     256
#define FLASH_BOOT_PAGE_NUM   16
#define FLASH_BOOT_SIZE       (FLASH_BOOT_PAGE_NUM * FLASH_PAGE_SIZE)   /* 0x8000 */
#define FLASH_A_ADDR          (FLASH_BASE_ADDR + FLASH_BOOT_SIZE)       /* 0x08008000 */
#define FLASH_A_START_PAGE    16
#define FLASH_A_PAGE_NUM      240
#define FLASH_A_SIZE          (FLASH_A_PAGE_NUM * FLASH_PAGE_SIZE)      /* 480KB */
```

## 4.4 与原例程的差异

| 项 | G070 原例程 | F103 新方案 |
|---|---|---|
| BootLoader 大小 | 14 页 = 28KB | **16 页 = 32KB** |
| A 区起始 | `0x08007000` | **`0x08008000`** |
| **VECT_TAB_OFFSET** | `0x7000` | **`0x8000`** ⚠️必须改 |
| Keil IROM1（APP） | `0x8007000` / `0x19000` | **`0x8008000` / `0x78000`** |

> 想省事也可以继续用 `0x7000`。但推荐 `0x8000`，因为标准库版 BootLoader 通常比 HAL 版大。

---

# 第五部分：L511-2 从零配置（保姆级）

## 5.1 第一步：先别接单片机，用 USB-TTL 单独测试

**这一步至关重要**。先确认模块是好的、AT 指令能用，再接单片机。

**准备**：
- **1.8V 电平的 USB-TTL**（CH340 有 1.8V/3.3V 跳线的型号）
- 杜邦线
- 串口助手（SSCOM / XCOM）
- SIM 卡（物联网卡或手机卡）
- 4G 天线
- 3.8V 电源（DC-DC 或稳压模块）

**接线**：
```
USB-TTL(1.8V)      L511
TXD      ────────► MAIN_RXD (17)
RXD      ◄──────── MAIN_TXD (18)
GND      ───────── GND
（模块 VBAT 独立 3.8V 供电）
```

**串口参数**：**115200, 8, N, 1**（规格书：默认波特率 115200）

**测试步骤**：

1. 插 SIM 卡、接天线、给模块上电（3.8V）
2. **开机**：拉低 PWRKEY 超过 **1 秒**然后释放
   - 裸模块：用杜邦线把 PWRKEY 短暂接 GND
   - 有按键：长按 2 秒
3. 发送 `AT`（串口助手勾「发送新行」）
4. 期望：`OK`

**开机成功的标志**：
- `AT` 能收到 `OK`
- NET_STATUS 灯：64ms 亮 / 3000ms 灭 = **已注册网络**；64ms 亮 / 800ms 灭 = 正在找网

> 规格书的 NET_STATUS 状态定义：
> | LED 状态 | 模块状态 |
> |---|---|
> | 熄灭 | Power off |
> | 64ms 亮 / 800ms 灭 | Shut down network（找网中） |
> | **64ms 亮 / 3000ms 灭** | **Registered network（已注册）** |

## 5.2 第二步：逐条测试 AT 指令

### 第 1 组：基础检查（3GPP 标准，肯定通用）

```
AT                    → OK
ATI                   → 厂商/型号/版本
AT+CPIN?              → +CPIN: READY           （SIM 正常）
AT+CSQ                → +CSQ: 25,99            （信号 >10 可用）
AT+CREG?              → +CREG: 0,1             （已注册）
AT+CGATT?             → +CGATT: 1              （附着成功）
```

### 第 2 组：GPRS 承载（移芯平台通用，大概率一样）

```
AT+SAPBR=3,1,"CONTYPE","GPRS"     → OK
AT+SAPBR=3,1,"APN",""             → OK
AT+SAPBR=1,1                      → OK
AT+SAPBR=2,1                      → +SAPBR: 1,1,"10.x.x.x"
```

### 第 3 组：HTTP（OTA 核心）

```
AT+HTTPINIT                       → OK
AT+HTTPPARA="CID",1               → OK
AT+HTTPPARA="URL","http://xxx/test.bin"   → OK
AT+HTTPACTION=0                   → OK
   （等几秒）
   +HTTPACTION: 0,200,1024        （0=GET, 200=成功, 1024=长度）
AT+HTTPREAD=0,256                 → +HTTPREAD: 256
                                     <256 字节数据>
AT+HTTPTERM                       → OK
```

> **如果 `AT+SAPBR` 或 `AT+HTTP*` 报 `ERROR`**：说明移柯改了指令集。
> 这时要去官网下载指令手册（需登录），或者联系 FAE。
> **这是本项目最大的技术风险，必须先验证。**

## 5.3 常见问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 发 AT 无回复 | TX/RX 接反 / 电平不对 / 没开机 | 检查线序、用 1.8V TTL、手动开机 |
| 收到乱码 | 波特率错 或 电平不匹配 | 确认 115200 + 1.8V |
| `+CME ERROR: 10` | SIM 卡没插好 | 重插 |
| `+CSQ: 99,99` | 天线没接/无信号 | 接天线，靠近窗户 |
| `AT+CGATT?` 返回 0 | 没附着网络 | 等一会儿，检查 APN |
| `AT+SAPBR=1,1` 一直 ERROR | APN 错 / 卡欠费 | 试 APN 填 `CMNET`/`3GNET`/`CTNET` |
| 注册不上网络 | **模块是海外版（L511E）** | 换 L511C 版本 |

---

# 第六部分：G0（HAL）→ F103（标准库）移植对照

## 6.1 GPIO

| G0 / HAL | F103 / 标准库 |
|---|---|
| `HAL_GPIO_WritePin(GPIOC, GPIO_PIN_3, GPIO_PIN_SET)` | `GPIO_SetBits(GPIOF, GPIO_Pin_0)` |
| `HAL_GPIO_WritePin(..., GPIO_PIN_RESET)` | `GPIO_ResetBits(GPIOF, GPIO_Pin_0)` |
| `HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1)` | `GPIO_ReadInputDataBit(GPIOF, GPIO_Pin_2)` |

```c
/* F103 标准库 GPIO 初始化 */
GPIO_InitTypeDef gpio;
RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOF, ENABLE);   /* 必须先开时钟！ */

gpio.GPIO_Pin   = GPIO_Pin_0;
gpio.GPIO_Mode  = GPIO_Mode_Out_OD;      /* 开漏，用于 PWRKEY */
gpio.GPIO_Speed = GPIO_Speed_50MHz;
GPIO_Init(GPIOF, &gpio);
```

## 6.2 Flash 编程（最关键差异）

| 项目 | G070 / HAL | **F103 / 标准库** |
|---|---|---|
| 编程单位 | **8 字节双字** | **2 字节半字** |
| 编程 API | `HAL_FLASH_Program(DOUBLEWORD,...)` | `FLASH_ProgramHalfWord(addr, data)` |
| 擦除 API | `HAL_FLASHEx_Erase(...)` | `FLASH_ErasePage(addr)` |

```c
/**
 * 写 Flash —— F103 每次只能写 2 字节
 * @param addr 地址（必须 2 字节对齐）
 * @param buf  数据
 * @param len  字节数（必须偶数）
 */
uint8_t F103_Flash_Write(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint32_t i;
    uint16_t hw;

    if(addr % 2) return 1;
    if(len  % 2) return 2;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    for(i = 0; i < len; i += 2){
        hw = (uint16_t)buf[i] | ((uint16_t)buf[i+1] << 8);
        if(FLASH_ProgramHalfWord(addr + i, hw) != FLASH_COMPLETE){
            FLASH_Lock();
            return 3;
        }
    }

    FLASH_Lock();
    return 0;
}
```

> ⚠️ 原代码传 `uint64_t*`，F103 要改成 `uint8_t*` 并按半字拼装。**直接照搬会写坏数据。**

## 6.3 中断向量表重定位（最容易漏）

```c
/* system_stm32f10x.c —— APP 工程 */
#define VECT_TAB_OFFSET  0x8000

/* 或在 main() 开头手动设（双保险） */
int main(void)
{
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x8000);
    ...
}
```

> **漏了的现象**：能跑、能打印，但一有中断就跳到 BootLoader 的向量表 → 跑飞。
> 看起来像「下载到一半卡死」，实际是中断炸了。**移植失败第一大原因。**

## 6.4 串口（USART3）

```c
void USART3_Init(uint32_t baud)
{
    GPIO_InitTypeDef gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    /* PB10 = TX，复用推挽 */
    gpio.GPIO_Pin   = GPIO_Pin_10;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    /* PB11 = RX，浮空输入 */
    gpio.GPIO_Pin  = GPIO_Pin_11;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOB, &gpio);

    usart.USART_BaudRate            = baud;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &usart);

    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);
    USART_Cmd(USART3, ENABLE);
}
```

## 6.5 可直接复用（零改动）

| 文件 | 行数 | 说明 |
|---|---|---|
| `utils_sha1.c` | 326 | 纯 C |
| `utils_md5.c` | 300 | 纯 C |
| `utils_hmac.c` | 171 | 纯 C |
| `mqtt.c` | 338 | 纯逻辑 |
| `w25q128.c` | 180 | 只需改 SPI 调用 |
| `24c02.c` | 140 | 可保留软件 I2C |

**约 1200 行几乎不用改。**

---

# 第七部分：四人完整分工表

## 7.1 总览

| 成员 | 模块 | 核心文件 | 难度 | 交付物 |
|---|---|---|---|---|
| **A（你）** | **L511-2 通信 + HTTP OTA 下载** | `cat1.c` | ⭐⭐⭐⭐⭐ | 能下载固件写 W25Q128 |
| **B** | **BootLoader + Flash** | `boot.c` `stmflash.c` | ⭐⭐⭐⭐ | 能搬移固件并跳转 |
| **C** | **阿里云 + MQTT + 升级包** | `mqtt.c` + 加密库 | ⭐⭐⭐ | 设备上线、收到 OTA 推送 |
| **D** | **驱动层 + 板级适配 + 集成** | `usart.c` `spi.c` `iic.c` `delay.c` | ⭐⭐⭐⭐ | 驱动全通、集成能跑 |

## 7.2 A（你）—— L511-2 + HTTP OTA

### 任务清单

| # | 任务 | 内容 | 耗时 |
|---|---|---|---|
| A0 | **确认模块形态** | 是裸模块还是带底板？完整型号是 L511C 还是 L511E？ | 0.5 天 |
| A1 | **买电平转换等配件** | TXB0108 + DC-DC 3.8V + 5V2A 电源 | 1 天 |
| A2 | **USB-TTL 单独测模块** | 1.8V TTL，验证 AT 指令可用 | 2 天 |
| A3 | **验证 SAPBR/HTTP 指令** | ⚠️ 最关键，确认移柯没改指令 | 1 天 |
| A4 | **硬件接线** | 电平转换 + 电源 + 控制脚 | 1 天 |
| A5 | **GPIO 驱动** | PF0~PF3 初始化（标准库） | 0.5 天 |
| A6 | **开机时序** | PWRKEY 拉低 1s+ 释放，等 NET_STATUS | 1 天 |
| A7 | **AT 收发框架** | 发送 + 等回复 + 超时重试 | 2 天 |
| A8 | **GPRS 承载封装** | SAPBR 4 条指令 | 1 天 |
| A9 | **HTTP 下载** | HTTPINIT/PARA/ACTION/READ 全流程 | 3 天 |
| A10 | **分片写 W25Q128** | 256 字节/片 | 1.5 天 |
| A11 | **OTA 报文解析** | sscanf 提取 size/version/url/md5 | 1 天 |
| A12 | **联调排错** | 配合 D | 3 天 |

### 关键代码骨架

**引脚定义（`cat1.h`）**：

```c
#ifndef __CAT1_H
#define __CAT1_H

/* 串口：USART3，需拔 P5 跳线 */
#define CAT1_UART           USART3

/* 控制脚：PF0~PF3（完全独立） */
#define PWRKEY_PIN          GPIO_Pin_0      /* PF0 */
#define RESET_PIN           GPIO_Pin_1      /* PF1 */
#define NET_STA_PIN         GPIO_Pin_2      /* PF2 */
#define STATUS_PIN          GPIO_Pin_3      /* PF3 */
#define CAT1_CTRL_PORT      GPIOF

/* PWRKEY 用开漏：输出 0 = 拉低（开机），输出 1 = 释放 */
#define PWRKEY_LOW()        GPIO_ResetBits(CAT1_CTRL_PORT, PWRKEY_PIN)
#define PWRKEY_HIGH()       GPIO_SetBits(CAT1_CTRL_PORT, PWRKEY_PIN)
#define RESET_LOW()         GPIO_ResetBits(CAT1_CTRL_PORT, RESET_PIN)
#define RESET_HIGH()        GPIO_SetBits(CAT1_CTRL_PORT, RESET_PIN)

/* 读状态 */
#define NET_STA             GPIO_ReadInputDataBit(CAT1_CTRL_PORT, NET_STA_PIN)

/* 串口重定向 */
#define CAT1_printf         u3_printf
#define CAT1_RX_BUF         U3_RxBuff

void CAT1_GPIO_Init(void);
uint8_t CAT1_PowerOn(uint16_t timeout);
uint8_t CAT1_HttpRead(uint32_t offset, uint16_t len, uint8_t *outbuf);
char CAT1_OTA(uint16_t timeout);

#endif
```

**GPIO 初始化**：

```c
void CAT1_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOF, ENABLE);

    /* PF0 = PWRKEY：开漏输出（模块内部有 1.8V 上拉） */
    gpio.GPIO_Pin   = PWRKEY_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(CAT1_CTRL_PORT, &gpio);

    /* PF1 = RESET：推挽输出 */
    gpio.GPIO_Pin  = RESET_PIN;
    gpio.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(CAT1_CTRL_PORT, &gpio);

    /* PF2/PF3 = 状态输入：上拉 */
    gpio.GPIO_Pin  = NET_STA_PIN | STATUS_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(CAT1_CTRL_PORT, &gpio);

    /* 初始状态：PWRKEY 释放（高阻），RESET 高 */
    PWRKEY_HIGH();
    RESET_HIGH();
}
```

**开机（按 L511 规格书时序）**：

```c
/**
 * L511-2 开机
 * 规格书：拉低 PWRKEY 超过 1s 然后释放
 * @return 0=成功  1=超时
 */
uint8_t CAT1_PowerOn(uint16_t timeout)
{
    /* 1. 拉低 PWRKEY 1.5 秒（规格书要求 >1s） */
    PWRKEY_LOW();
    Delay_ms(1500);
    PWRKEY_HIGH();

    /* 2. 等模块启动 */
    Delay_ms(3000);

    /* 3. 等网络注册（NET_STATUS 64ms亮/3000ms灭） */
    while(timeout--){
        Delay_ms(100);
        if(NET_STA == 0) return 0;
    }
    return 1;
}
```

**AT 指令发送**：

```c
/**
 * 发送 AT 指令并等待期望回复
 * @param cmd        指令（不含 \r\n）
 * @param expect     期望关键字，如 "OK"
 * @param timeout_ms 超时
 * @return 0=收到  1=超时
 */
uint8_t AT_SendCmd(char *cmd, char *expect, uint16_t timeout_ms)
{
    U3_RxBuff_Clear();
    u3_printf("%s\r\n", cmd);

    while(timeout_ms--){
        Delay_ms(1);
        if(strstr((char*)U3_RxBuff, expect) != NULL)
            return 0;
    }
    return 1;
}
```

**HTTP 分片读取**：

```c
uint8_t CAT1_HttpRead(uint32_t offset, uint16_t len, uint8_t *outbuf)
{
    char cmd[64], expect[32];
    char *ptr;

    sprintf(cmd, "AT+HTTPREAD=%lu,%u", offset, len);
    sprintf(expect, "+HTTPREAD: %u", len);

    if(AT_SendCmd(cmd, expect, 3000)) return 1;

    ptr = strstr((char*)U3_RxBuff, expect);
    if(ptr == NULL) return 2;
    ptr += strlen(expect);
    while(*ptr == '\r' || *ptr == '\n') ptr++;

    memcpy(outbuf, ptr, len);
    return 0;
}
```

### A 的学习路径（4G 零基础）

| 阶段 | 做什么 | 时间 |
|---|---|---|
| D1 | 读 L511 规格书（重点：引脚定义、开机时序、电气特性） | 1 天 |
| D2 | 确认模块形态 + 买配件 | 1 天 |
| D3-4 | USB-TTL 连模块，5.2 节指令逐条跑通，**截图存证** | 2 天 |
| D5 | **重点验证 SAPBR/HTTP 指令**（决定方案可行性） | 1 天 |
| D6-7 | 接线到战神V1，只做「串口能发 AT、能收 OK」 | 2 天 |
| D8-9 | AT 指令封装成函数 | 2 天 |
| D10-12 | HTTP 下载全流程，先下小文件 | 3 天 |
| D13-14 | 接 W25Q128，边下边存，ST-Link 读出来比对 | 2 天 |

> **强烈建议**：先在电脑上用串口助手把所有 AT 指令手工发一遍，正常回复全部截图。
> 后面写代码时有对照，出问题能快速判断是模块问题还是代码问题。

---

## 7.3 B —— BootLoader + Flash

### 任务清单

| # | 任务 | 内容 | 耗时 |
|---|---|---|---|
| B1 | 建标准库 BootLoader 工程 | IROM1 = `0x8000000` / `0x8000` | 1 天 |
| B2 | Flash 驱动 | `FLASH_ErasePage` / `FLASH_ProgramHalfWord` 封装 | 2 天 |
| B3 | 串口救砖菜单 | `[1]擦A区 [2]Xmodem [3]设三元组 [4]查三元组` | 2 天 |
| B4 | EEPROM 读写 | 24C02 存三元组和 OTA 标志 | 1.5 天 |
| B5 | W25Q128 读取 | SPI2 | 1.5 天 |
| B6 | 固件搬移 | W25Q128 → A 区，按页搬 | 2 天 |
| B7 | 跳转逻辑 | 设栈顶 + 设向量表 + 跳转 | 1.5 天 |
| B8 | **加固件校验** | 28 字节尾缀 + MD5（原例程没做） | 2 天 |
| B9 | 联调 | 配合 D | 2 天 |

### 跳转代码（标准库）

```c
typedef void (*pFunction)(void);

void Boot_JumpToApp(uint32_t app_addr)
{
    pFunction jump;
    uint32_t stack_top;

    /* 1. 检查栈顶（必须在 SRAM 0x20000000~0x2000FFFF） */
    stack_top = *(volatile uint32_t*)app_addr;
    if((stack_top & 0x2FFE0000) != 0x20000000) return;

    /* 2. 检查复位向量是否在 A 区 */
    jump = (pFunction)(*(volatile uint32_t*)(app_addr + 4));
    if((uint32_t)jump < app_addr || (uint32_t)jump > 0x08080000) return;

    /* 3. 关中断、关外设 */
    __disable_irq();
    SysTick->CTRL = 0;

    /* 4. 设向量表 */
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, app_addr - FLASH_BASE_ADDR);

    /* 5. 设栈顶 */
    __set_MSP(stack_top);

    /* 6. 跳转 */
    jump();
}
```

### 答辩讲

为什么要 BootLoader、Flash 分区怎么算、F103 半字编程 vs G0 双字、跳转前为什么关中断、变砖怎么救

---

## 7.4 C —— 阿里云 + MQTT + 升级包

### 任务清单

| # | 任务 | 内容 | 耗时 |
|---|---|---|---|
| C1 | 阿里云建产品 | 自定义品类，数据格式「透传/自定义」 | 0.5 天 |
| C2 | 建测试设备 | 拿三元组 | 0.5 天 |
| C3 | 功能定义 | 属性 `powerstate`（bool，读写） | 0.5 天 |
| C4 | MQTT 连接 | CONNECT + HMAC-SHA1 算密码 | 2 天 |
| C5 | Topic 订阅 | 3 个 Topic | 1 天 |
| C6 | 属性下发解析 | 控制 LED | 1.5 天 |
| C7 | 版本上报 | 连上 5 秒后上报 | 1 天 |
| C8 | 升级包制作 | `ota_pack.py` 加 28 字节尾缀 | 0.5 天 |
| C9 | 配置 OTA | 上传升级包，静态升级 | 1 天 |
| C10 | 联调 | 配合 A | 2 天 |

### 三元组格式

```
/ProductKey/DeviceName/DeviceSecret/版本号/
```

**长度硬校验**：

| 字段 | 要求 |
|---|---|
| ProductKey | **正好 11** |
| DeviceName | ≤ 32 |
| DeviceSecret | **正好 32** |
| 版本号 | **正好 23** |

### 升级包

```powershell
python outputs\ota_pack.py "工程文件.bin"
# 输出 工程文件_ota.bin（原始 + 28 字节）
python outputs\ota_pack.py --verify "工程文件_ota.bin"
```

**28 字节尾缀**：

| 偏移 | 长度 | 内容 |
|---|---|---|
| +0 | 4 | `CD EF CD EF`（**十六进制字节，不是 ASCII**） |
| +4 | 4 | 原始 bin 长度，小端 |
| +8 | 16 | 原始 bin MD5（裸摘要） |
| +24 | 4 | `FF FF FF FF` |

---

## 7.5 D —— 驱动层 + 板级适配 + 集成

### 任务清单

| # | 任务 | 内容 | 耗时 |
|---|---|---|---|
| D1 | 建标准库 APP 工程 | IROM1 = `0x8008000` / `0x78000` | 1 天 |
| D2 | 时钟配置 | 8MHz HSE × 9 = 72MHz | 1 天 |
| D3 | **USART1 调试串口** | PA9/PA10，115200，printf 重定向 | 1 天 |
| D4 | **USART3 给 4G** | PB10/PB11，中断 + 环形缓冲 | 1.5 天 |
| D5 | SPI2 + W25Q128 | PB12(CS)/PB13/14/15 | 2 天 |
| D6 | I2C1 + 24C02 | PB6/PB7 | 2 天 |
| D7 | 延时函数 | SysTick 微秒/毫秒 | 0.5 天 |
| D8 | LED + 按键 | PB5/PE5，PE2/3/4 | 1 天 |
| D9 | **向量表重定位** | `VECT_TAB_OFFSET = 0x8000` | 0.5 天 |
| D10 | 集成联调 | 合并 A/B/C 代码 | 4 天 |
| D11 | 测试用例 | 正常/断网/断电/变砖 | 2 天 |
| D12 | 维护 main.h | 分区宏、结构体、事件标志 | 持续 |

### 公共头文件（`main.h`）—— 第一天必须锁死

```c
#ifndef __MAIN_H
#define __MAIN_H

/* ---------- Flash 分区 ---------- */
#define FLASH_BASE_ADDR       0x08000000
#define FLASH_PAGE_SIZE       2048
#define FLASH_TOTAL_PAGES     256
#define FLASH_BOOT_PAGE_NUM   16
#define FLASH_BOOT_SIZE       (FLASH_BOOT_PAGE_NUM * FLASH_PAGE_SIZE)
#define FLASH_A_ADDR          (FLASH_BASE_ADDR + FLASH_BOOT_SIZE)
#define FLASH_A_START_PAGE    16
#define FLASH_A_PAGE_NUM      240
#define FLASH_A_SIZE          (FLASH_A_PAGE_NUM * FLASH_PAGE_SIZE)

/* ---------- OTA 状态 ---------- */
#define BOOT_STA_O            0x5AA5C33C

/* ---------- 三元组长度 ---------- */
#define PRODUCTKEY_LEN        11
#define DEVICENAME_LEN        32
#define DEVICESECRET_LEN      32
#define VERSION_LEN           23

/* ---------- EEPROM 结构体（两个工程必须完全一致！）---------- */
typedef struct{
    uint8_t  valid_cer;                             /* 0xDD = 有效 */
    uint8_t  ProductKeyBuff[PRODUCTKEY_LEN+1];
    uint8_t  DeviceNameBuff[DEVICENAME_LEN+1];
    uint8_t  DeviceSecretBuff[DEVICESECRET_LEN+1];
    uint8_t  Version_ABuff[VERSION_LEN+1];
    uint32_t OTA_firelen;
    uint32_t OTA_flag;
} AliyunInfo_CB;
#define ALIINFO_STRUCT_LEN  sizeof(AliyunInfo_CB)

/* ---------- 事件标志 ---------- */
#define CONNECT_START   0x80000000
#define CONNECT_CMD     0x40000000
#define CONNECT_EVENT   0x20000000
#define PING_SENT       0x10000000
#define PING_CHECK      0x08000000
#define OTA_EVENT       0x04000000

#endif
```

> ⚠️ 这个结构体存在 24C02 地址 0，BootLoader 和 APP 靠它通信。
> **任何字段改动必须两边同步**，否则「OTA 标志读不到，永远不升级」。

---

# 第八部分：4 周时间线

| 周 | A（你·L511） | B（BootLoader） | C（云端/MQTT） | D（驱动/集成） |
|---|---|---|---|---|
| **W1** | 确认模块型号；买配件；USB-TTL 测通；**验证 SAPBR/HTTP** | 建工程；Flash 擦写 | 建产品/设备；拿三元组 | **USART1+USART3+延时+时钟** |
| **W2** | 接线（电平转换+电源）；GPIO+开机+AT 封装 | 串口菜单 + EEPROM | MQTT 连接 + 上报版本 | **SPI2+W25Q128；I2C+24C02** |
| **W3** | HTTP 下载全流程；分片写 W25Q128 | 固件搬移 + 跳转 + 校验 | 升级包 + 配置 OTA | **集成联调** |
| **W4** | 配合排查 | MD5 加固 | 全链路验证 + 截图 | 测试报告 |

**关键节点**：
- **W1 中**：A 必须验证 HTTP 指令可用，否则整个方案要调整
- **W1 末**：D 的串口必须通
- **W3 末**：完整走通一次 OTA
- **W4 末**：异常测试 + 文档 + 答辩

---

# 第九部分：风险与预案

| 风险 | 概率 | 影响 | 预案 |
|---|---|---|---|
| **L511 串口 1.8V 直连烧模块** | **高** | 模块报废 | **必须用 TXB0108 电平转换** |
| **买的是裸模块没有底板** | **高** | 接不了线 | 买 L511 转接板 |
| **模块是 L511E 海外版** | 中 | 国内注册不上网 | 换 L511C 版本 |
| **移柯改了 AT 指令** | **中** | 方案要调整 | **W1 就验证 SAPBR/HTTP** |
| **电源不足 1.2A** | **高** | 反复重启 | 独立 3.8V/2A + 100μF 电容 |
| **向量表没改** | **高** | 中断跑飞 | `VECT_TAB_OFFSET = 0x8000` |
| **Flash 半字编程写错** | **高** | 固件损坏 | 地址/长度都要偶数 |
| 结构体不一致 | **高** | OTA 标志读不到 | 第一天锁死 |
| P5 跳线没拔 | 中 | 串口被干扰 | 接线前先拔 |

---

# 第十部分：交付前自检清单

## 硬件
- [ ] 确认模块是 **L511C**（国内版），不是 L511E
- [ ] 有转接底板 / SIM 卡座 / 天线座
- [ ] **电平转换模块已接**（TXB0108）
- [ ] 电源 3.8V / ≥1.2A，有 100μF 电容
- [ ] 与开发板共地
- [ ] P5 跳线已拔

## 模块测试（USB-TTL 阶段）
- [ ] 发 `AT` 收到 `OK`
- [ ] `AT+CPIN?` → READY
- [ ] `AT+CSQ` → 信号 >10
- [ ] `AT+CREG?` → 0,1
- [ ] `AT+CGATT?` → 1
- [ ] `AT+SAPBR=1,1` → OK ⚠️关键
- [ ] `AT+HTTPINIT` → OK ⚠️关键
- [ ] 完整下载一个小文件成功

## BootLoader
- [ ] IROM1 = `0x8000000` / `0x8000`
- [ ] `VECT_TAB_OFFSET = 0x0`
- [ ] bin < 32768 字节
- [ ] 串口菜单正常
- [ ] 三元组能设能读
- [ ] Flash 擦写正常
- [ ] 能跳转 APP

## APP
- [ ] IROM1 = `0x8008000` / `0x78000`
- [ ] `VECT_TAB_OFFSET = 0x8000`
- [ ] `main.h` 与 BootLoader 一致
- [ ] USART1 调试输出正常
- [ ] USART3 能发 AT 收 OK
- [ ] W25Q128 / 24C02 读写正常
- [ ] 能连阿里云

## 全流程
- [ ] 版本上报成功
- [ ] 收到 OTA 推送
- [ ] HTTP 下载完整
- [ ] 固件写入 W25Q128
- [ ] 重启后搬移成功
- [ ] 新固件生效
- [ ] 断网中途失败不变砖

---

# 第十一部分：配套资料

| 文件 | 用途 |
|---|---|
| `outputs/ota_pack.py` | 升级包打包工具（C 用） |
| `outputs/OTA复刻指南.md` | G0 原方案细节（14 个坑） |
| `outputs/战神V1_4G_OTA_完整方案.md` | 本文档 |
| `outputs/板子资料档案.md` | 战神V1 完整资料 |
| `outputs/_l511_ctx.txt` | L511 规格书关键段落（原文） |
| `outputs/_战神V1引脚表_全量.txt` | 引脚表原始数据 |

---

# 附：现在你最该做的三件事

1. **看模块标签，告诉我完整型号**（`L511C-Y6`？`L511E-Y6`？还是别的）
2. **告诉我模块是裸贴片还是带底板的**（有没有排针、SIM 卡座）
3. **买电平转换模块**（TXB0108/TXS0108E）和 **3.8V 电源**——这两个不买，接上去就是烧模块

**这三件事确认后，我可以给你更精确的接线图和代码。**
