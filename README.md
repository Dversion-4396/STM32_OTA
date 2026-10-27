# STM32 OTA 远程升级项目

> 战神V1（STM32F103ZET6）+ 移柯 L511-2（4G Cat.1）+ 阿里云物联网平台
> 基于标准库的裸机 OTA 方案

---

## 项目简介

通过 4G 模块从阿里云下载固件，写入外部 Flash（W25Q128），
再由 BootLoader 搬移到内部 Flash 运行区，实现远程升级。

### 核心流程

```
① APP 运行 → 连阿里云 MQTT → 上报版本号
② 云端比对发现新版本 → 推送 OTA 通知
③ APP 用 4G 模块 HTTP 下载固件 → 存 W25Q128
④ 置 OTA 标志 → 重启
⑤ BootLoader 检查标志 → 搬移固件到 A 区 → 重启
⑥ 新固件运行
```

---

## 硬件平台

| 项目 | 型号 | 说明 |
|---|---|---|
| 主控 | STM32F103ZET6 | 512KB Flash / 64KB RAM，LQFP144 |
| 开发板 | 战神V1（郑州精畅） | |
| 4G 模块 | 移柯 L511-2 | Cat.1，USART3 连接 |
| 外部 Flash | W25Q128 | 16MB，SPI2 连接 |
| EEPROM | AT24C02 | 256B，I2C1 连接 |

### Flash 分区

```
0x08000000 ┌──────────────────────────────┐
           │   BootLoader  (16页 = 32KB)  │
0x08008000 ├──────────────────────────────┤  ← APP 起始
           │   应用 A 区 (240页 = 480KB)  │
0x08080000 └──────────────────────────────┘
```

### 引脚分配

| 功能 | 引脚 | 说明 |
|---|---|---|
| 4G TX/RX | PB10 / PB11 | USART3，**需拔 P5 跳线** |
| 4G PWRKEY | PF0 | 开漏输出 |
| 4G RESET | PF1 | 推挽输出 |
| 4G NET_STA | PF2 | 上拉输入 |
| W25Q128 CS | PB12 | **不能当普通 IO** |
| W25Q128 SCK/MISO/MOSI | PB13 / PB14 / PB15 | SPI2 |
| 24C02 SCL/SDA | PB6 / PB7 | I2C（软件模拟） |
| 调试串口 | PA9 / PA10 | USART1（CH340） |
| LED0（红） | PB5 | 低电平点亮 |
| LED1（绿） | PE5 | 低电平点亮 |

---

## 目录结构

```
STM32_OTA/
├── APP工程/                    ← 烧到 0x08008000
│   ├── libraries/              标准库（CMSIS + StdPeriph）
│   ├── project/                Keil 工程
│   │   └── STM32F103ZET6_Project.uvprojx
│   └── user/
│       ├── main.c              主程序
│       ├── stm32f10x_it.c      中断服务
│       └── api/
│           ├── common.h        ★公共定义（两工程必须一致）
│           ├── spi.c/h         SPI2 + W25Q128
│           ├── cat1.c/h        L511-2 4G 模块
│           ├── mqtt.c/h        MQTT 协议
│           ├── usart.c/h       串口驱动
│           ├── i2c.c/h         I2C 驱动
│           ├── eeprom.c/h      24C02 驱动
│           └── ...
│
├── BootLoader工程/             ← 烧到 0x08000000
│   ├── libraries/              标准库
│   ├── project/                Keil 工程
│   └── user/
│       ├── main.c
│       └── api/
│           ├── common.h        ★和 APP 完全一致
│           ├── spi.c/h         SPI2 + W25Q128
│           ├── stmflash.c/h    内部 Flash 读写
│           ├── boot.c/h        BootLoader 主逻辑
│           └── ...
│
├── docs/                       文档
├── tools/                      工具脚本
├── 框架使用说明.md              ★先看这个
└── .gitignore
```

---

## 📌 第一次来？先看这个

👉 **[docs/开工手册.md](docs/开工手册.md)** —— 936 行，从零到跑通的完整流程

包含：
- 第一天要做的五件事
- 怎么配 Git、怎么克隆、怎么切分支
- 怎么验证硬件（量电压、测 AT 指令）
- 怎么写你的第一个函数
- 每天的固定动作
- 卡住了怎么办
- 四个人的独立路线
- 常见问题速查

---
## 快速开始

### 1. 环境要求

- **Keil MDK 5.x**（需 STM32F1xx DFP 包）
- **ST-Link** + STM32CubeProgrammer（烧录）
- **串口助手**（SSCOM / XCOM）
- **Python 3**（仅升级包工具需要）

### 2. 打开工程

```
APP工程/project/STM32F103ZET6_Project.uvprojx
BootLoader工程/project/STM32F103ZET6_Project.uvprojx
```

### 3. 编译

直接 Build（F7）。

> 当前框架里有很多函数体是空的（标记 `TODO【X】`），
> 编译会通过，但功能不完整。详见 `框架使用说明.md`。

### 4. 烧录

| 固件 | 地址 | 方式 |
|---|---|---|
| BootLoader | `0x08000000` | ST-Link |
| APP | `0x08008000` | ST-Link 或 OTA |

---

## 关键配置（不要改）

| 项目 | APP 工程 | BootLoader 工程 |
|---|---|---|
| **IROM1 Start** | `0x08008000` | `0x08000000` |
| **IROM1 Size** | `0x78000` | `0x8000` |
| **VECT_TAB_OFFSET** | **`0x8000`** | **`0x0`** |

> ⚠️ `VECT_TAB_OFFSET` 是最容易漏的配置。
> APP 不改的话，中断会跳到 BootLoader 的向量表 → 程序跑飞。

---

## 分工

| 成员 | 负责 | 主要文件 |
|---|---|---|
| **A** | 4G 通信 + HTTP OTA 下载 | `cat1.c` |
| **B** | BootLoader + Flash 分区 | `boot.c` `stmflash.c` |
| **C** | MQTT + 阿里云 + 升级包 | `mqtt.c` |
| **D** | SPI 驱动 + 集成 + 测试 | `spi.c` `main.c` |

详细任务见 `框架使用说明.md` 和各人文档。

---

## 开发流程

### 第一次使用

```bash
git clone https://github.com/Dversion-4396/STM32_OTA.git
cd STM32_OTA
```

### 日常开发

```bash
# 1. 拉取最新代码（每天开始工作前）
git pull

# 2. 写你的代码（只改你负责的文件）

# 3. 提交
git add .
git commit -m "feat(A): 实现 CAT1_SendAT 函数"

# 4. 推送
git push
```

### 提交信息规范

```
feat(A): 新增 xxx 功能
fix(B): 修复 xxx 问题
docs: 更新文档
refactor(D): 重构 xxx
test: 添加测试
```

---

## 常见问题

### Q: 编译报 "redefinition" 错误？

A: 变量在头文件里定义了。改成 `extern` 声明，在 `.c` 里定义。

### Q: 烧录后程序不运行？

A: 检查 `VECT_TAB_OFFSET` 是否配成 `0x8000`（APP 工程）。

### Q: 两个工程能同时打开吗？

A: 可以。但注意不要同时改 `common.h`，改完要同步到两个工程。

### Q: 4G 模块没反应？

A: 按顺序检查：① 电源是否独立（要 1.2A）② P5 跳线是否拔掉 ③ 波特率是否 115200 ④ 电平是否匹配（1.8V 要转换）。

---

## 参考资料

- `框架使用说明.md` —— 框架结构和各人任务
- `docs/` —— 技术方案、接线图、AT 指令手册
- 阿里云物联网平台：https://iot.console.aliyun.com/

---

## 许可

本项目仅用于学习和研究。

---

## 🔗 仓库信息

**地址**：https://github.com/Dversion-4396/STM32_OTA

**分支**：

| 分支 | 用途 | 谁用 |
|---|---|---|
| `main` | 主分支（稳定版） | 全员 |
| `dev-A` | A 的开发分支 | A（4G） |
| `dev-B` | B 的开发分支 | B（BootLoader） |
| `dev-C` | C 的开发分支 | C（MQTT） |
| `dev-D` | D 的开发分支 | D（SPI） |

---

## 🚀 组员快速上手（三步）

### 第一步：克隆

```bash
git clone https://github.com/Dversion-4396/STM32_OTA.git
cd STM32_OTA
```

### 第二步：切到自己的分支

```bash
# A 就切 dev-A，B 就切 dev-B，以此类推
git checkout dev-A
```

### 第三步：改代码 → 一键提交

双击运行 **`一键提交.bat`**，按提示输入提交说明即可。

或者手动：

```bash
git add -A
git commit -m "feat(A): 实现 CAT1_SendAT"
git pull --rebase
git push
```

---

## 📖 详细协作说明

见 [docs/团队协作指南.md](docs/团队协作指南.md)

包含：
- Git 安装配置
- 日常开发流程
- 提交信息规范
- 冲突解决
- 常见问题

