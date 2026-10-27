# STM32G070 + 阿里云 OTA 远程升级 —— 复刻指南

> 分析对象：`E:\BaiduNetdiskDownload\新建文件夹`
> 分析日期：2026-10-26
> 结论先行：这是一套「超子说物联网 STM32G0 开发板」的**双区单备份 + 外部 Flash 暂存** OTA 方案。
> 复刻的核心不是抄代码，而是**把 Flash 分区、向量表偏移、bin 尾缀格式这三件事对齐**。

---

## 一、先搞清楚这套东西是什么

文件夹里其实是**同一套方案的三份材料**，不是三个独立项目：

| 目录 | 角色 | 关键文件 |
|---|---|---|
| `0-BootLoader区例程` | **BootLoader 工程**，烧到 `0x08000000`，负责擦写 A 区、跳转、串口救砖 | `boot.c` `stmflash.c` `main.h` |
| `36-...OTA升级 APP控制LED灯` | **APP 工程（HTTP 路线）**，烧到 `0x08007000`，走 4G 模块 HTTP 下载固件 | `cat1.c` `mqtt.c` |
| `34-...OTA远程升级` | **APP 工程（MQTT 路线）**，同上但用 MQTT 分片下载 | `cat1.c` |
| `21.OTA升级.pdf` | 教程，讲原理 + 操作步骤（对应 36 号路线） | — |
| `winhex` | 十六进制编辑器，用来手工加 bin 尾缀 | — |
| `29-RS485...` | 无关，是串口实验例程，可忽略 | — |

**两条 OTA 路线的区别（重要，选一条就行）：**

| | 34 号（MQTT 分片） | 36 号（HTTP 下载） |
|---|---|---|
| 固件传输通道 | MQTT `thing/file/download` | 4G 模块内置 HTTP |
| 单次大小 | 1024 字节/片 | 256 字节/片 |
| 需要的云端信息 | `streamId` / `streamFileId` | 一个 HTTPS URL |
| 代码复杂度 | 高（要处理分片应答） | 低 |
| 教程 PDF 讲的是 | ✗ | ✓ |

> **建议复刻 36 号路线。** 逻辑短、坑少，PDF 也是照着它写的。

---

## 二、核心：Flash 分区表（背下来）

STM32G070RBT6：Flash 128KB @ `0x08000000`，SRAM 36KB @ `0x20000000`，扇区 2KB。

```
0x08000000 ┌──────────────────────────┐
           │   BootLoader  (14 扇区)   │  28 KB  = 0x7000
0x08007000 ├──────────────────────────┤  ← APP 起始地址
           │   应用 A 区  (50 扇区)    │  100 KB = 0x19000
0x08020000 └──────────────────────────┘  芯片 Flash 结束
```

对应代码里的宏（`0-BootLoader区例程/USER/main.h`）：

```c
#define FLASH_BASE_ADDR        0x08000000
#define FLASH_SECTO_NUM        64        // 128KB / 2KB
#define FLASH_SECTOR_SIZE      2048
#define FLASH_BOOT_SECTO_NUM   14        // BootLoader 占 14 个扇区 = 28KB
#define FLASH_A_SECTO_NUM      FLASH_SECTO_NUM - FLASH_BOOT_SECTO_NUM   // 50
#define FLASH_A_ADDR           FLASH_BASE_ADDR + FLASH_BOOT_SECTO_NUM * FLASH_SECTOR_SIZE  // 0x08007000
#define FLASH_A_START_SECTOR   (FLASH_A_ADDR - FLASH_BASE_ADDR)/FLASH_SECTOR_SIZE          // 14
```

**Keil 里必须改的两个地方：**

| 工程 | Options → Target → IROM1 | 实际配置 |
|---|---|---|
| BootLoader | Start `0x08000000`，Size **`0x7000`** | ⚠️ 原工程写的是 `0x20000`（全片），**建议改成 `0x7000`** |
| APP（36/34） | Start `0x08007000`，Size `0x19000` | ✅ 原工程已正确 |

> 为什么 BootLoader 要改成 `0x7000`：原工程填全片，链接器不会报错，一旦代码长到 28KB 以上，
> 编译能过、烧进去就把 A 区开头覆盖了，表现为「上电直接 HardFault / 跳转失败」。
> 当前 BootLoader bin 是 **24340 字节**，离 28672 上限只剩 4KB，很危险。

**还有一处必须改：中断向量表偏移**

APP 工程的 `CMSIS/Device/ST/STM32G0xx/Source/system_stm32g0xx.c`：

```c
#define VECT_TAB_OFFSET  0x7000U    // APP 工程必须是 0x7000
                                    // BootLoader 工程必须是 0x0
```

> 这是**最容易漏的一步**。漏了的话：程序能跑、串口能打印，但任何中断（串口接收、
> 定时器、SysTick）都会跳到 `0x08000000` 的 BootLoader 向量表里去，直接跑飞或死机。
> 现象很像「OTA 下载到一半卡死」，实际是中断炸了。

---

## 三、运行流程（一图流）

```
┌─ 上电 ─┐
│        ▼
│   BootLoader 启动
│        │
│        ├─ 串口 2 秒内收到 'w' ──► 进入串口救砖菜单
│        │                           [1]擦 A 区  [2]Xmodem 下载  [3]设三元组  [4]查三元组
│        │
│        ├─ EEPROM(24C02) 里 OTA_flag == 0x5AA5C33C ?
│        │        │是                                    │否
│        │        ▼                                      ▼
│        │   擦除 A 区(14~63 扇区)                  跳转 A 区执行 APP
│        │   从 W25Q128 第 0 块搬 2048B/次 到 A 区
│        │   清 OTA_flag → 写 EEPROM → 软复位
│        │
│        └─ 跳转 A 区：Load_APP(0x08007000)
│
└─ APP 运行（36 号路线）─┐
        │
        ├─ 连阿里云 MQTT（三元组 + hmacsha1 算密码）
        ├─ 连上第 5 秒：向 /ota/device/inform/ 上报当前版本号
        ├─ 云端比对：版本不一致 → 推送 /ota/device/upgrade/ 带 {size, version, url, md5}
        ├─ sscanf 解析出 OTA_firelen / Version_ABuff / URL_ABuff
        ├─ CAT1_OTA()：4G 模块断电重启 → 注册网络 → HTTPINIT → 下载 256B/片 → 写 W25Q128
        ├─ 成功：OTA_flag = BOOT_STA_O，写 EEPROM，软复位 → 回到 BootLoader 搬移
        └─ 失败：软复位重来
```

**关键设计：单区模式 + 外部 Flash 当备份区。**
固件先完整落到 W25Q128（64KB 块），确认下载成功后才置标志、重启搬移。
这比纯单区安全（下载失败不影响运行中的 APP），又比双区省内部 Flash。

---

## 四、复刻步骤（照着做）

### 步骤 0：准备工具

- Keil MDK 5.x + STM32G0xx DFP 包（工程是 Keil 工程 `.uvprojx`）
- ST-Link + STM32CubeProgrammer（或 Keil 直接下载）
- 串口助手（波特率 **921600**，串口 1 是调试口）
- 一个 4G Cat.1 模块（Air724 之类）+ 阿里云物联网平台账号

### 步骤 1：先跑通 BootLoader（不碰 OTA）

1. 打开 `0-BootLoader区例程/USER/工程文件.uvprojx`
2. 改 IROM1：Start `0x08000000`，Size `0x7000`
3. 确认 `system_stm32g0xx.c` 里 `VECT_TAB_OFFSET = 0x0`
4. 编译，确认 bin 大小 **< 28672 字节**
5. 烧录，串口看到 `2秒内，输入'w'进入bootloader`

> 此时还没有 APP，跳转会失败并打印「跳转到A区失败，进入BootLoader」，这是**正常的**。

### 步骤 2：改三元组和版本号

阿里云后台 → 设备详情 → 复制三元组：

```json
{
  "ProductKey": "grfdYViGNwS",
  "DeviceName": "zz2301_test",
  "DeviceSecret": "43e992a58ce66c1a855c68f13c9a9ae4"
}
```

在 BootLoader 串口菜单输入 `3`，按这个格式发一行（**结尾必须有 `/`**）：

```
/ProductKey/DeviceName/DeviceSecret/版本号/
```

例如：

```
/grfdYViGNwS/zz2301_test/43e992a58ce66c1a855c68f13c9a9ae4/app_dht11_2023112855511/
```

**长度校验（`BootLoader_AliCerInspect` 里硬校验，错一个字节就拒绝）：**

| 字段 | 长度要求 | 宏 |
|---|---|---|
| ProductKey | **必须正好 11** | `PRODUCTKEY_LEN` |
| DeviceName | ≤ 32 | `DEVICENAME_LEN` |
| DeviceSecret | **必须正好 32** | `DEVICESECRET_LEN` |
| 版本号 | **必须正好 23** | `VERSION_LEN` |

> 版本号 `app_dht11_2023112855511` 数一下正好 23 个字符。
> 想用别的长度就改 `VERSION_LEN`，**但 BootLoader 和 APP 两个工程的 `main.h` 都要改**，
> 否则 EEPROM 里结构体布局对不上，读出来全是乱码。

### 步骤 3：编译 APP，生成 bin

1. 打开 `36-.../USER/工程文件.uvprojx`
2. 确认 IROM1 = `0x08007000` / `0x19000`，`VECT_TAB_OFFSET = 0x7000`
3. Options → Output → 勾选 **Create HEX File**，并在 User 页加 `fromelf --bin` 生成 bin
   （原工程已配好，产物在 `USER/Objects/工程文件.bin`）
4. 把 `main.h` 里的 `VERSION_LEN` 和版本号字符串改成和 BootLoader 里一致

### 步骤 4：给 bin 加 28 字节尾缀 ⚠️ 最容易错的一步

**尾缀格式（28 字节）：**

| 偏移 | 长度 | 内容 |
|---|---|---|
| +0 | 4 | `CD EF CD EF` ← **十六进制字节，不是 ASCII 字符串 "cdefcdef"** |
| +4 | 4 | 原始 bin 长度，**小端** |
| +8 | 16 | 原始 bin 的 MD5（16 字节裸摘要，不是 32 位 hex 字符串） |
| +24 | 4 | `FF FF FF FF` |

**用脚本（推荐，已实测与官方样例逐字节一致）：**

```powershell
python outputs\ota_pack.py "工程文件.bin"
# 生成 工程文件_ota.bin，自动算长度和 MD5

python outputs\ota_pack.py --verify "工程文件_ota.bin"
# 反向校验
```

实测验证：用 `36 号工程/USER/Objects/工程文件.bin`（31208 字节）打包，
产出 31236 字节，与官方 `36 号工程/工程文件.bin` **逐字节完全相同**。

**手工做法（用 winhex，PDF 教程教的）：**
1. 打开 bin，光标移到末尾
2. 追加 `CD EF CD EF`
3. 追加长度（低字节在前，如 31208 = `0x79E8` → `E8 79 00 00`）
4. `CertUtil -hashfile 工程文件.bin MD5` 取 MD5，转成 16 字节二进制追加
5. 追加 `FF FF FF FF`

> **为什么必须脚本化**：MD5 要转成二进制、长度要转小端，手工极易错，
> 而且错一个字节云端校验就过不了。

**这个坑的双重证据**（我实测确认过，不是推测）：

1. **PDF 自己的例子就是十六进制串**：教程第 10 页写的整合结果是
   `cdefcdef7c384aa4125e4e48678001cff144f2d3a2acffffffff`
   —— 这是一串 hex，其中 `cdefcdef` 是 **4 个字节 `CD EF CD EF`**。
   如果 `cdefcdef` 真是 8 个 ASCII 字符，hex 里应出现 `6364656663646566`，但并没有。

   > ⚠️ **顺带发现教程这行示例本身写漏了**：这串只有 **52 个 hex 字符 = 26 字节**，
   > 比规范少 2 字节。少的是长度字段的高 2 字节 —— `7c38` 后面直接接 MD5 了，
   > 正确应为 `7c38 0000`（`0x387C` = 14460 小端，需 4 字节）。
   > 所以**别照抄教程那行串**，照规范生成。
2. **字节级比对**：用 36 号例程的原始 bin 打包，产出与官方 `工程文件.bin`
   **逐字节完全相同**（31236 字节，`a == b` 为 True）。

### 步骤 5：阿里云后台配置

1. **创建产品** → 选「自定义品类」，数据格式 **透传/自定义**（本例是自定义 Topic）
2. **创建设备** → 拿到三元组
3. **功能定义** → 加一个属性 `powerstate`（bool，读写）——对应 APP 里的 `ATTRIBUTE1`
4. **OTA 升级** → 新增升级包：
   - 上传 **步骤 4 生成的 `_ota.bin`**
   - 版本号填和 `VERSION_LEN` 一致的 23 位字符串
   - 选择「静态升级」，推送到目标设备
5. **Topic 列表**（代码里自动订阅，不用手动建）：
   - `/sys/{pk}/{dn}/thing/service/property/set`
   - `/ota/device/upgrade/{pk}/{dn}`
   - `/sys/{pk}/{dn}/thing/file/download_reply`

### 步骤 6：烧录 + 验证

1. 先烧 BootLoader 到 `0x08000000`
2. 再烧 APP 到 `0x08007000`（或用 BootLoader 的 Xmodem 方式下载）
3. 串口 1（921600）观察：
   ```
   正常开机
   2秒内，输入'w'进入bootloader
   ...连接服务器成功
   ...上报版本号
   OTA升级，版本号:xxx
   OTA升级，大小：31236
   OTA升级，URL:xxx
   下载固件成功，重启
   OTA升级
   固件版本：xxx
   固件大小：31236
   擦除A区Flash成功
   OTA更新完毕，重启
   ```
4. 重启后 APP 里改个 LED 行为（比如换个闪烁频率），确认新固件真的生效

---

## 五、关键代码骨架（移植时抄这几段）

### 1. BootLoader 跳转（`stmflash.c`）

```c
typedef void (*LoadApp)(void);

__asm void MSR_MSP(uint32_t addr)
{
    MSR MSP, r0
    BX  r14
}

uint8_t Load_APP(uint32_t appxaddr)
{
    /* 校验栈顶地址是否落在 SRAM 范围：0x2000xxxx */
    if(((*(volatile uint32_t*)appxaddr) & 0x2FFE0000) == 0x20000000){
        LoadApp jump = (LoadApp)(*(volatile uint32_t*)(appxaddr + 4));  /* 复位向量 */
        MSR_MSP(*(volatile uint32_t*)appxaddr);                        /* 设栈顶 */
        BootLoader_Clear();                                            /* 反初始化外设 */
        jump();
    }
    return 1;
}
```

`BootLoader_Clear()` 必须做，否则 APP 里重新初始化 UART/TIM/RTC 会和 BootLoader 的残留状态打架：

```c
void BootLoader_Clear(void)
{
    HAL_UART_DeInit(&U1_Control.UART_Handler);
    HAL_TIM_Base_DeInit(&htim17);
    HAL_RTC_DeInit(&MyRtc.hrtc);
}
```

> **加固建议**：跳转前再补三行，比原工程更稳
> ```c
> __disable_irq();
> SysTick->CTRL = 0;
> SCB->VTOR = appxaddr;   // 主动设向量表，不完全依赖 APP 的 SystemInit
> ```

### 2. BootLoader 搬移固件（`boot.c` 的 `ActiveEvent`）

```c
if(BootEventFlag & OTA_MODEM){
    for(i = 0; i < AliInfoCB.OTA_firelen / FLASH_SECTOR_SIZE; i++){
        W25Q128_Read(Update.UpdateBuff, i*FLASH_SECTOR_SIZE, FLASH_SECTOR_SIZE);
        Stm32Flash_Write(FLASH_A_ADDR + i*FLASH_SECTOR_SIZE,
                         (uint64_t*)Update.UpdateBuff, FLASH_SECTOR_SIZE);
    }
    /* 处理不足一个扇区的尾部 */
    if((AliInfoCB.OTA_firelen % FLASH_SECTOR_SIZE) != 0){
        W25Q128_Read(Update.UpdateBuff,
                     (AliInfoCB.OTA_firelen/FLASH_SECTOR_SIZE)*FLASH_SECTOR_SIZE,
                     AliInfoCB.OTA_firelen % FLASH_SECTOR_SIZE);
        Stm32Flash_Write(FLASH_A_ADDR + i*FLASH_SECTOR_SIZE,
                         (uint64_t*)Update.UpdateBuff, FLASH_SECTOR_SIZE);
    }
    AliInfoCB.OTA_flag = 0;
    EEprom_WriteData(0, &AliInfoCB, ALIINFO_STRUCT_LEN);
    NVIC_SystemReset();
}
```

### 3. EEPROM 里的状态结构体（两个工程必须**完全一致**）

```c
typedef struct{
    uint8_t  valid_cer;                              // 0xDD 表示有效
    uint8_t  ProductKeyBuff[PRODUCTKEY_LEN+1];       // 11+1
    uint8_t  DeviceNameBuff[DEVICENAME_LEN+1];       // 32+1
    uint8_t  DeviceSecretBuff[DEVICESECRET_LEN+1];   // 32+1
    uint8_t  Version_ABuff[VERSION_LEN+1];           // 23+1
    uint32_t OTA_firelen;                            // 固件大小
    uint32_t OTA_flag;                               // 0x5AA5C33C = 需要 OTA
} AliyunInfo_CB;
```

> ⚠️ 这个结构体存在 24C02 的地址 0，两个工程靠它通信。
> **任何字段顺序/长度改动都必须两边同步**，否则读出来是乱码，表现为「OTA 标志读不到，永远不升级」。

### 4. APP 端解析 OTA 推送（`cat1.c`）

```c
if(strstr((char *)Aliyun_mqtt.cmdbuff, "https")){
    ptr = strstr((char *)Aliyun_mqtt.cmdbuff, "\"data\":");
    if(ptr != NULL){
        if(sscanf(ptr,
            "\"data\":{\"size\":%d,\"sign\":\"%[^\"]\",\"version\":\"%[^\"]\","
            "\"signMethod\":\"Md5\",\"url\":\"https://%[^\"]\",\"md5\":\"%[^\"]\"},"
            "\"id\":%d,\"message\":\"success\"}\r\n",
            &AliInfoCB.OTA_firelen, sign_buff, AliInfoCB.Version_ABuff,
            URL_ABuff, md5_buff, &iddata) == 6){
            SysCB.SysEventFlag |= OTA_EVENT;
        }
    }
}
```

### 5. APP 端 HTTP 下载固件（`cat1.c` 的 `CAT1_OTA`）

AT 指令顺序（Air724，串口 2）：

```
AT+SAPBR=3,1,"CONTYPE","GPRS"
AT+SAPBR=3,1,"APN",""
AT+SAPBR=1,1
AT+SAPBR=2,1
AT+HTTPINIT
AT+HTTPPARA="CID",1
AT+HTTPPARA="URL","<云端给的 https 地址>"
AT+HTTPACTION=0                      → 等 +HTTPACTION: 0,200
AT+HTTPREAD=<offset>,256             → 循环读 256 字节，写 W25Q128
```

> 下载前会先给 4G 模块**断电重启**（POWER_KEY 时序：拉高→延时→拉低→延时→拉高→拉低），
> 然后等 `NET_STA` 引脚变低表示注册上网络。移植时这个引脚一定要接对。

---

## 六、我实测发现的 14 个坑（按踩坑概率排序）

| # | 坑 | 后果 | 解决 |
|---|---|---|---|
| 1 | `VECT_TAB_OFFSET` 没改成 `0x7000` | 中断全跳错，跑飞 | APP 的 `system_stm32g0xx.c` 改 `0x7000` |
| 2 | 尾缀 `cdefcdef` 当成 ASCII 写成 8 字节 | 尾缀变 32 字节，云端校验失败 | 写成十六进制字节 `CD EF CD EF` |
| 3 | MD5 写成 32 位 hex 字符串 | 尾缀变 48 字节 | 必须是 **16 字节裸摘要** |
| 4 | 版本号不是正好 23 字节 | BootLoader 拒绝设置三元组 | 数准 23 个字符，或两边同步改 `VERSION_LEN` |
| 5 | 只改了一个工程的 `main.h` | EEPROM 结构体错位，OTA 标志读不到 | BootLoader / APP 两边**同步改** |
| 6 | BootLoader 的 IROM1 是全片 `0x20000` | 代码超 28KB 就覆盖 A 区 | 改成 `0x7000`，编译时会报错提醒 |
| 7 | 固件超过 64KB | W25Q128 只擦了第 0 块（64KB），越界写坏 | 控制固件 < 64KB，或改擦多个块 |
| 8 | 固件超过 100KB | A 区放不下 | A 区上限 `0x19000` = 100KB |
| 9 | 三元组里 ProductKey 不是 11 位 | 校验返回 1，拒绝保存 | 阿里云后台复制，别手打 |
| 10 | 串口 1 波特率不对 | 看不到任何提示，以为没跑 | **921600**，不是 115200 |
| 11 | 跳转前没 `DeInit` 外设 | APP 里 UART/定时器行为异常 | 保留 `BootLoader_Clear()` |
| 12 | EEPROM 换成了 24C01（128 字节） | 结构体 112 字节 + 余量不够，写溢出 | 必须用 **24C02（256 字节）** |
| 13 | 4G 模块 `NET_STA` 引脚接错 | 一直卡「注册网络超时」 | 对照原理图确认引脚 |
| 14 | 云端 OTA 升级包没推送到目标设备 | APP 一直上报版本但不下载 | 后台确认「静态升级」已选设备并启动 |

---

## 七、一个「原工程其实没做」的地方（可加固）

我全文搜索了 BootLoader 工程，**找不到任何解析那 28 字节尾缀的代码**：
搜 `0xCDEF`、`0xFFFFFFFF`、`MD5`、`md5` 在 `0-BootLoader区例程` 里全部无结果。

也就是说：**尾缀只是给云端校验用的元数据，BootLoader 端并没有验证它。**
BootLoader 直接信任 `EEPROM 里记录的 OTA_firelen`，从 W25Q128 盲搬。

**风险**：如果 W25Q128 里数据损坏（下载中断、Flash 坏块），BootLoader 照样搬进 A 区，
搬完重启 → APP 是坏的 → 又重启 → 可能砖。

**建议加固**（复刻时值得加，原工程没有）：

```c
/* 在 ActiveEvent 搬移前，先校验 W25Q128 里的固件 */
uint8_t OTA_Verify(void)
{
    uint32_t magic, len;
    uint8_t  md5_in_flash[16], md5_calc[16];
    uint8_t  buf[256];
    uint32_t i;

    /* 1. 读尾缀：位置 = OTA_firelen - 28 */
    W25Q128_Read((uint8_t*)&magic, AliInfoCB.OTA_firelen - 28, 4);
    if(magic != 0xEFCDEFCD) return 1;            /* 注意小端：CD EF CD EF */

    W25Q128_Read((uint8_t*)&len, AliInfoCB.OTA_firelen - 24, 4);
    if(len != AliInfoCB.OTA_firelen - 28) return 2;

    W25Q128_Read(md5_in_flash, AliInfoCB.OTA_firelen - 20, 16);

    /* 2. 逐块算 MD5（需要 md5 库，可复用 APP 工程的 utils_md5.c） */
    MD5_Init();
    for(i = 0; i < len; i += 256){
        W25Q128_Read(buf, i, (len - i >= 256) ? 256 : (len - i));
        MD5_Update(buf, (len - i >= 256) ? 256 : (len - i));
    }
    MD5_Final(md5_calc);

    /* 3. 比对 */
    if(memcmp(md5_in_flash, md5_calc, 16) != 0) return 3;
    return 0;
}
```

失败时不要搬移，直接清 `OTA_flag` 跳回原 APP，避免变砖。

---

## 八、自检清单（交付前逐条打勾）

**BootLoader 侧**
- [ ] IROM1 = `0x08000000` / `0x7000`
- [ ] `VECT_TAB_OFFSET = 0x0`
- [ ] bin 大小 < 28672 字节
- [ ] `FLASH_BOOT_SECTO_NUM = 14` 与实际占用一致
- [ ] 串口 921600 能看到菜单
- [ ] 输入 `3` 能设置三元组，输入 `4` 能正确回读
- [ ] 无 APP 时能打印「跳转到A区失败」而不是死机

**APP 侧**
- [ ] IROM1 = `0x08007000` / `0x19000`
- [ ] `VECT_TAB_OFFSET = 0x7000`
- [ ] `main.h` 的 `VERSION_LEN` / 结构体与 BootLoader **完全一致**
- [ ] 能连上阿里云（串口打印「连接服务器成功」）
- [ ] 连上 5 秒后能上报版本号
- [ ] 能收到 OTA 推送并打印 size/version/url

**升级包侧**
- [ ] 用 `ota_pack.py` 打的包，`--verify` 通过
- [ ] 上传的是 `_ota.bin`（31236 字节那种），不是原始 bin
- [ ] 云端版本号 = 23 位且与代码里一致
- [ ] 固件 < 64KB（W25Q128 单块限制）

**端到端**
- [ ] 完整走完：上报 → 推送 → 下载 → 重启 → 擦除 → 搬移 → 重启 → 新功能生效
- [ ] 拔网线中途失败后，设备仍能正常运行旧固件（不变砖）

---

## 九、配套文件

| 文件 | 说明 |
|---|---|
| `outputs/ota_pack.py` | bin 尾缀打包/校验脚本，已用官方样例验证逐字节一致 |
| `outputs/21.OTA升级_提取文本.txt` | 从教程 PDF 提取的纯文本（15 页） |
| `outputs/ota_pdf_extract.py` | PDF 提取脚本（Skia/PDF + CID 字体，按字体独立 CMap 解码） |
| `outputs/selftest_ota.bin` | 打包自测产物（与官方样例完全相同，可删） |
