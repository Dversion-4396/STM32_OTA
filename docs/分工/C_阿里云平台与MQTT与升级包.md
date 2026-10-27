# 【成员 C】MQTT 改造 + 阿里云 + 升级包

> 项目：战神V1（F103ZET6）+ 移柯 L511-2 + 阿里云 OTA
> 基础工程：`E:\MY_project\STM32F103ZET6_Demo10DMA`（裸机版）
> 你的角色：**云端对接负责人**，决定设备能不能"上网报到"并"收到升级通知"
> 工期：13 天 | 难度：★★★★（比原来重，原因见下）

---

# 目录

1. [你负责什么](#一你负责什么)
2. [你现在的 MQTT 不够用](#二你现在的-mqtt-不够用)
3. [第 0 周：阿里云后台配置](#三第-0-周阿里云后台配置)
4. [第 1 周：移植加密库](#四第-1-周移植加密库)
5. [第 2 周：移植 MQTT](#五第-2-周移植-mqtt)
6. [第 3 周：版本上报 + OTA 解析](#六第-3-周版本上报--ota-解析)
7. [第 3-4 周：升级包制作](#七第-3-4-周升级包制作)
8. [任务总表](#八任务总表)
9. [排错手册](#九排错手册)
10. [答辩准备](#十答辩准备)

---

# 一、你负责什么

**让设备连上阿里云、上报版本号、接收 OTA 通知，并制作符合格式的升级包。**

## 4 个阶段

| 阶段 | 目标 | 判断标准 |
|---|---|---|
| ① 平台 | 阿里云建产品、建设备 | 拿到三元组 |
| ② 连接 | MQTT 连上阿里云 | 串口打印"连接成功" |
| ③ 通信 | 上报版本、接收下发 | 云端能看到版本；能收到 OTA 推送 |
| ④ 升级包 | 制作符合格式的 bin | `--verify` 通过，云端校验成功 |

---

# 二、你现在的 MQTT 不够用

## 2.1 现有 MQTT 只有 3 个发送函数

你的 `user/api/mqtt.c`（6.1KB）只有：

```c
void MQTT_Connect_Package_Send(const char *client,
                               const char *username,
                               const char *password,
                               u16 keep_alive_time);

void MQTT_Publish_Package_Send(const char *publish_topic,
                               const char *payload);

void MQTT_Subscribe_Package_Send(const char *subscribe_topic);
```

**它只会"发"，不会"收"。**

## 2.2 缺的东西（OTA 必须的）

| 缺失 | 为什么需要 | 后果 |
|---|---|---|
| ❌ 报文接收解析 | 收不到云端下发 | 不知道要升级 |
| ❌ CONNACK 处理 | 不知道连没连上 | 连不上也不知道原因 |
| ❌ SUBACK 处理 | 不知道订阅成功没 | 收不到消息也不知道为什么 |
| ❌ PING 保活 | 连接会超时断 | 几十秒就掉线 |
| ❌ OTA 推送解析 | 解析不出 URL 和大小 | 无法下载 |
| ❌ HMAC-SHA1 | 阿里云鉴权必须用 | 连不上 |

## 2.3 解决方案：移植 36 号工程的 MQTT

**36 号工程的 `mqtt.c`（338 行）是完整的**，直接移植替换。

**位置**：
```
E:\BaiduNetdiskDownload\新建文件夹\36-MQTT协议  阿里云物联网平台 OTA升级 APP控制LED灯\HARDWARE\Scr\mqtt.c
```

## 2.4 移植时要改什么

**只需改串口调用**（3 处宏定义）：

```c
/* ===== 36 号工程（G0 / HAL）===== */
#define CAT1_printf    u2_printf
#define CAT1_RX_BUF    U2_RxBuff
#define CAT1_RxCounter U2_Control.U_RxCounter

/* ===== 你的（F103 / 裸机）===== */
#define CAT1_printf    Usart3_SendString
#define CAT1_RX_BUF    usart3_value.data
#define CAT1_RxCounter usart3_value.count
```

**其余逻辑全部不用改**（报文拼装、解析都是纯 C）。

---

# 三、第 0 周：阿里云后台配置

**这一周不依赖硬件，可以立刻开工。**

## 3.1 建产品

### 操作步骤

1. 登录 [阿里云物联网平台](https://iot.console.aliyun.com/)
2. 左侧菜单 → **设备管理 → 产品** → **创建产品**
3. 填写：

| 字段 | 填什么 | 为什么 |
|---|---|---|
| 产品名称 | `战神V1_OTA测试` | 随便起 |
| 品类 | **自定义品类** | 不要选标准品类 |
| 节点类型 | **直连设备** | 设备直接连阿里云 |
| 连网方式 | **蜂窝（2G/3G/4G/5G）** | 用 4G |
| 数据格式 | **透传/自定义** | ⚠️ 关键，见下 |
| 认证方式 | **设备密钥** | 用三元组 |

### ⚠️ 数据格式为什么必须选"透传/自定义"

| 选项 | 区别 |
|---|---|
| **透传/自定义** | 平台原样转发你的 JSON，不解析 |
| ICA 标准 | 平台会按 Alink 协议解析，多一层封装 |

选错了报文格式对不上，会多花几天排查。

## 3.2 建设备，拿三元组

### 操作步骤

1. 产品详情页 → **设备** → **添加设备**
2. DeviceName 填 `test_001`
3. 创建后会弹出三元组

### 三元组格式

```json
{
  "ProductKey":   "grfdYViGNwS",
  "DeviceName":   "test_001",
  "DeviceSecret": "43e992a58ce66c1a855c68f13c9a9ae4"
}
```

### ⚠️ 三个注意事项

**1. DeviceSecret 只显示一次**

弹窗关掉就看不到了。**立刻复制保存。**

如果丢了：设备详情页可以"重置"（但会生成新的）。

**2. 长度必须符合要求**

| 字段 | 长度要求 | 代码里的宏 |
|---|---|---|
| ProductKey | **正好 11 位** | `PRODUCTKEY_LEN` |
| DeviceName | ≤ 32 位 | `DEVICENAME_LEN` |
| DeviceSecret | **正好 32 位** | `DEVICESECRET_LEN` |

代码里是**硬校验**，错一个字节就拒绝保存。

**3. 立刻发给 A 和 B**

他们要用这个写进 EEPROM。

## 3.3 功能定义

### 操作步骤

1. 产品详情 → **功能定义** → **编辑草稿**
2. 点击 **添加自定义功能**
3. 填写：

| 字段 | 值 |
|---|---|
| 功能类型 | **属性** |
| 功能名称 | 开关 |
| 标识符 | **`powerstate`** |
| 数据类型 | **bool** |
| 读写类型 | **读写** |

4. **发布上线**（不发布不生效）

### ⚠️ 标识符必须完全一致

代码里是：
```c
#define ATTRIBUTE1  "powerstate"
```

大小写、拼写都必须完全一样。

## 3.4 Topic 列表（了解即可）

阿里云会自动生成，**你不需要手动创建**：

| 用途 | Topic 格式 | 方向 | QoS |
|---|---|---|---|
| 属性下发 | `/sys/{pk}/{dn}/thing/service/property/set` | 云端→设备 | 0 |
| **OTA 通知** | `/ota/device/upgrade/{pk}/{dn}` | 云端→设备 | 0 |
| 属性上报 | `/sys/{pk}/{dn}/thing/event/property/post` | 设备→云端 | 1 |
| **版本上报** | `/ota/device/inform/{pk}/{dn}` | 设备→云端 | 1 |

其中 `{pk}` = ProductKey，`{dn}` = DeviceName。

**示例**（假设 pk=`grfdYViGNwS`, dn=`test_001`）：
```
/sys/grfdYViGNwS/test_001/thing/service/property/set
/ota/device/upgrade/grfdYViGNwS/test_001
/sys/grfdYViGNwS/test_001/thing/event/property/post
/ota/device/inform/grfdYViGNwS/test_001
```

---

# 四、第 1 周：移植加密库

## 4.1 三个文件，零改动

| 文件 | 行数 | 作用 |
|---|---|---|
| `utils_sha1.c` | 326 | SHA1 算法 |
| `utils_md5.c` | 300 | MD5 算法 |
| `utils_hmac.c` | 171 | HMAC 封装 |

**位置**：
```
E:\BaiduNetdiskDownload\新建文件夹\36-MQTT协议...\HMAC\scr\
E:\BaiduNetdiskDownload\新建文件夹\36-MQTT协议...\HMAC\inc\
```

## 4.2 为什么零改动

这三个是**纯 C 算法**，只依赖 `<string.h>`，不依赖任何硬件。

直接复制到 `user/api/` 下，加到工程里就能编译。

## 4.3 接口说明

```c
/* utils_sha1.h */
void utils_sha1(const unsigned char *input, unsigned int ilen, unsigned char output[20]);

/* utils_md5.h */
void utils_md5(const unsigned char *input, unsigned int ilen, unsigned char output[16]);

/* utils_hmac.h */
void utils_hmac_sha1(const char *msg, int msg_len,
                     char *output, const char *key, int key_len);
```

## 4.4 验证方法

**写个测试，用已知数据验证。**

```c
void Crypto_Test(void)
{
    char output[64];
    char *msg = "clientIdtest_001deviceNametest_001productKeygrfdYViGNwS";
    char *key = "43e992a58ce66c1a855c68f13c9a9ae4";

    utils_hmac_sha1(msg, strlen(msg), output, key, strlen(key));

    Usart1_SendString("HMAC-SHA1 结果：\r\n");
    Usart1_SendString(output);
    Usart1_SendString("\r\n");
}
```

**期望输出**：40 位十六进制字符串（SHA1 是 160 位 = 20 字节 = 40 个 hex 字符）

**验证方法**：把同样的 msg 和 key 输入在线 HMAC-SHA1 工具，比对结果。

> 如果结果不对，检查 `utils_hmac_sha1` 的参数顺序（msg 和 key 别传反）。

---

# 五、第 2 周：移植 MQTT

## 5.1 阿里云 MQTT 鉴权原理（面试常问）

**阿里云不用用户名密码，用三元组算签名。**

### 计算步骤

```
① 拼明文：
   clientId{DeviceName}deviceName{DeviceName}productKey{ProductKey}

   例如：
   clientIdtest_001deviceNametest_001productKeygrfdYViGNwS

② 用 DeviceSecret 做密钥，对明文做 HMAC-SHA1

③ 结果转 16 进制字符串 = MQTT 密码
```

### 连接参数

| 参数 | 值 |
|---|---|
| ClientID | `{DeviceName}\|securemode=3,signmethod=hmacsha1\|` |
| Username | `{DeviceName}&{ProductKey}` |
| Password | HMAC-SHA1 算出来的 |
| 服务器 | `{ProductKey}.iot-as-mqtt.cn-shanghai.aliyuncs.com` |
| 端口 | **1883**（非加密） |

**示例**：
```
ClientID  = test_001|securemode=3,signmethod=hmacsha1|
Username  = test_001&grfdYViGNwS
Password  = <算出来的 40 位 hex>
Server    = grfdYViGNwS.iot-as-mqtt.cn-shanghai.aliyuncs.com
Port      = 1883
```

## 5.2 MQTT 报文结构

### CONNECT（设备→云端）

```
固定报头：0x10 + 剩余长度
可变报头：
  0x00 0x04 'M' 'Q' 'T' 'T'    协议名
  0x04                          协议级别（MQTT 3.1.1）
  0xC2                          连接标志（用户名+密码+清理会话）
  0x00 0x3C                     保活时间 60 秒
有效载荷：
  ClientID + Username + Password
```

### SUBSCRIBE（设备→云端）

```
固定报头：0x82 + 剩余长度
可变报头：报文标识符（2 字节）
有效载荷：
  Topic1 + QoS1 + Topic2 + QoS1 + ...
```

### PUBLISH（设备→云端）

```
固定报头：0x30（QoS0）或 0x32（QoS1）+ 剩余长度
可变报头：Topic + 报文标识符（QoS1 才有）
有效载荷：JSON 数据
```

> **好消息**：`mqtt.c` 里这些都写好了，你主要理解 + 改串口调用。

## 5.3 必须处理的 5 种响应

| 报文 | 首字节 | 含义 | 你要做什么 |
|---|---|---|---|
| **CONNACK** | `0x20` | 连接结果 | `data[3]==0` 成功，否则打印错误码 |
| **SUBACK** | `0x90` | 订阅结果 | 检查每个 Topic 的结果字节 |
| **PUBACK** | `0x40` | QoS1 发布确认 | 打印成功 |
| **PINGRESP** | `0xD0` | 心跳回复 | 清 PING 标志 |
| **PUBLISH** | `0x30` | 云端下发 | **解析命令 / OTA 推送** |

### CONNACK 返回码

| 返回码 | 含义 | 怎么办 |
|---|---|---|
| `0x00` | 连接成功 | ✅ |
| `0x01` | 协议版本不支持 | 检查 MQTT 版本 |
| `0x02` | ClientID 格式错 | 检查 `\|securemode=3...\|` |
| `0x03` | 服务端不可用 | 稍后重试 |
| `0x04` | 用户名或密码错 | **检查 HMAC-SHA1 计算** |
| `0x05` | 未授权 | 设备被禁用，后台检查 |

### SUBACK 返回码

| 返回码 | 含义 |
|---|---|
| `0x00` | 成功（QoS0） |
| `0x01` | 成功（QoS1） |
| `0x80` | **失败**（Topic 无权限） |

## 5.4 移植步骤

### Step 1：复制 mqtt.c

从 36 号工程复制 `mqtt.c` 和 `mqtt.h` 到 `user/api/`。

### Step 2：改头文件引用

36 号工程的 `mqtt.c` 开头是：

```c
#include "stm32g0xx_hal.h"
#include "main.h"
#include "mqtt.h"
#include "usart.h"
```

改成：

```c
#include "common.h"
#include "usart.h"
#include "mqtt.h"
#include "utils_hmac.h"
```

### Step 3：改串口宏

找到 `mqtt.c` 里用到串口的地方，改成你的：

```c
/* 发送 */
#define CAT1_printf     Usart3_SendString

/* 接收缓冲 */
#define CAT1_RX_BUF     usart3_value.data
```

### Step 4：改数据类型

36 号工程用的是 `uint8_t` / `uint16_t` / `uint32_t`，
你的工程用 `u8` / `u16` / `u32`（在 `common.h` 里定义）。

**可以不改**（标准库头文件里也有 `uint8_t`），也可以统一改。建议统一，风格一致。

### Step 5：编译，解决报错

常见报错：

| 报错 | 原因 | 解决 |
|---|---|---|
| `stm32g0xx_hal.h: No such file` | 没改头文件 | 改成 `common.h` |
| `u2_printf 未定义` | 没改宏 | 改成 `Usart3_SendString` |
| `U2_RxBuff 未定义` | 没改宏 | 改成 `usart3_value.data` |
| 重复定义 | 和现有 mqtt.c 冲突 | 删掉旧的 |

## 5.5 连接流程

```c
/**
 * MQTT 连接阿里云
 * @return 0=成功
 */
u8 MQTT_ConnectAliyun(void)
{
    char temp[256];

    /* 1. 构建 ClientID */
    sprintf(Aliyun_mqtt.ClientID, "%s|securemode=3,signmethod=hmacsha1|",
            AliInfoCB.DeviceNameBuff);

    /* 2. 构建 Username */
    sprintf(Aliyun_mqtt.Username, "%s&%s",
            AliInfoCB.DeviceNameBuff, AliInfoCB.ProductKeyBuff);

    /* 3. 算密码 */
    memset(temp, 0, sizeof(temp));
    sprintf(temp, "clientId%sdeviceName%sproductKey%s",
            AliInfoCB.DeviceNameBuff,
            AliInfoCB.DeviceNameBuff,
            AliInfoCB.ProductKeyBuff);

    utils_hmac_sha1(temp, strlen(temp),
                    Aliyun_mqtt.Passward,
                    (char*)AliInfoCB.DeviceSecretBuff,
                    DEVICESECRET_LEN);

    /* 4. 构建服务器地址 */
    sprintf(Aliyun_mqtt.ServerIP, "%s.iot-as-mqtt.cn-shanghai.aliyuncs.com",
            AliInfoCB.ProductKeyBuff);
    Aliyun_mqtt.ServerPort = 1883;

    /* 5. 发 CONNECT 报文 */
    MQTT_ConectPack();

    return 0;
}
```

> ⚠️ **注意 `utils_hmac_sha1` 的参数顺序**：
> `(明文, 明文长度, 输出缓冲, 密钥, 密钥长度)`

## 5.6 订阅 Topic

```c
/**
 * 订阅 3 个 Topic
 */
void MQTT_SubscribeAll(void)
{
    /* 1. 属性下发 */
    sprintf(Aliyun_mqtt.Stopic_Buff[0],
            "/sys/%s/%s/thing/service/property/set",
            AliInfoCB.ProductKeyBuff, AliInfoCB.DeviceNameBuff);

    /* 2. OTA 升级通知 */
    sprintf(Aliyun_mqtt.Stopic_Buff[1],
            "/ota/device/upgrade/%s/%s",
            AliInfoCB.ProductKeyBuff, AliInfoCB.DeviceNameBuff);

    /* 3. 文件下载回复（36 号路线用不上，但保留） */
    sprintf(Aliyun_mqtt.Stopic_Buff[2],
            "/sys/%s/%s/thing/file/download_reply",
            AliInfoCB.ProductKeyBuff, AliInfoCB.DeviceNameBuff);

    /* 发 SUBSCRIBE 报文 */
    MQTT_Subscribe(Aliyun_mqtt.Stopic_Buff, 3, 1);
}
```

---

# 六、第 3 周：版本上报 + OTA 解析

## 6.1 版本上报

### 什么时候发

**连上服务器后 5 秒**（给模块一点时间稳定）。

用定时器计数：

```c
/* 定时器中断里（1 秒一次） */
void TIM_IRQHandler(void)
{
    SysCB.SystemTimer++;

    if(SysCB.SysEventFlag & CONNECT_EVENT){
        SysCB.ConnectTimer++;

        /* 连上 5 秒后上报版本 */
        if(SysCB.ConnectTimer == 5){
            CAT1_PropertyVersion();
        }

        /* 每 60 秒发 PING */
        if((SysCB.ConnectTimer - SysCB.PingTimer) >= 60){
            SysCB.PingTimer = SysCB.ConnectTimer;
            MQTT_PingREQ();
        }
    }
}
```

### 报文格式

**Topic**：`/ota/device/inform/{pk}/{dn}`

**Payload**：
```json
{"id": 1,"params":{"version":"app_2023112855511"}}
```

**代码**：

```c
void CAT1_PropertyVersion(void)
{
    char topic[128], data[128];

    sprintf(topic, "/ota/device/inform/%s/%s",
            AliInfoCB.ProductKeyBuff, AliInfoCB.DeviceNameBuff);

    sprintf(data, "{\"id\": 1,\"params\":{\"version\":\"%s\"}}",
            AliInfoCB.Version_ABuff);

    MQTT_PublishQs1(topic, data, strlen(data));
}
```

> ⚠️ **版本号必须正好 23 个字符**，这是 BootLoader 的硬校验。

## 6.2 OTA 推送解析

### 云端推送的报文

```json
{
  "data": {
    "size": 31236,
    "sign": "xxx",
    "version": "app_2023112855511",
    "signMethod": "Md5",
    "url": "https://xxx.oss-cn-shanghai.aliyuncs.com/fw.bin",
    "md5": "abc123..."
  },
  "id": 123,
  "message": "success"
}
```

### 怎么识别是 OTA 推送

搜索 `"https"` 或 `"streamId"`：

```c
if(strstr(Aliyun_mqtt.cmdbuff, "https")){
    /* 是 OTA 推送 */
}
```

### 解析代码

```c
/**
 * 解析 OTA 推送
 * @param json  收到的 JSON
 * @return 0=成功
 */
u8 CAT1_ParseOTA(char *json)
{
    char *ptr;
    char sign_buff[128], md5_buff[128];
    int iddata;

    ptr = strstr(json, "\"data\":");
    if(ptr == NULL) return 1;

    if(sscanf(ptr,
        "\"data\":{\"size\":%lu,\"sign\":\"%[^\"]\",\"version\":\"%[^\"]\","
        "\"signMethod\":\"Md5\",\"url\":\"https://%[^\"]\",\"md5\":\"%[^\"]\"},"
        "\"id\":%d,\"message\":\"success\"}",
        &AliInfoCB.OTA_firelen,
        sign_buff,
        AliInfoCB.Version_ABuff,
        URL_ABuff,
        md5_buff,
        &iddata) != 6){
        return 2;
    }

    return 0;
}
```

### ⚠️ sscanf 的坑

**1. 格式串必须完全一致**

包括空格、逗号、引号。云端报文格式变了，解析就失败。

**2. 用 `%[^"]` 提取字符串**

遇到引号停止，正好适合 JSON 字符串字段。

**3. 解析失败怎么办**

先把收到的原始报文 `printf` 出来，逐字符对比。

```c
Usart1_SendString("收到的原始报文：\r\n");
Usart1_SendString((char*)Aliyun_mqtt.cmdbuff);
Usart1_SendString("\r\n");
```

## 6.3 属性下发处理

云端下发开关命令：

```json
{"id":"123","version":"1.0","params":{"powerstate":1},"method":"thing.service.property.set"}
```

**处理**：

```c
if(strstr(Aliyun_mqtt.cmdbuff, ATTRIBUTE1)){
    if(strstr(Aliyun_mqtt.cmdbuff, "\"powerstate\":1")){
        /* 开灯 */
        Led_Low(Led_Red);
        /* 回报状态 */
        MQTT_PublishQs1(property_post_topic,
                        "{\"params\":{\"powerstate\":1}}", 26);
    }
    else if(strstr(Aliyun_mqtt.cmdbuff, "\"powerstate\":0")){
        /* 关灯 */
        Led_High(Led_Red);
        MQTT_PublishQs1(property_post_topic,
                        "{\"params\":{\"powerstate\":0}}", 26);
    }
}
```

---

# 七、第 3-4 周：升级包制作

## 7.1 为什么需要 28 字节尾缀

阿里云要求升级包带校验信息，BootLoader 用它验证固件完整性。

**没有尾缀的后果**：
- 云端校验失败，无法上传
- 即使上传了，BootLoader 无法校验，损坏的固件也会被烧进去

## 7.2 28 字节格式详解

| 偏移 | 长度 | 内容 | 说明 |
|---|---|---|---|
| +0 | 4 | `CD EF CD EF` | ⚠️ **十六进制字节，不是 ASCII 字符串 "cdefcdef"** |
| +4 | 4 | 原始 bin 长度 | **小端**存储 |
| +8 | 16 | 原始 bin 的 MD5 | **16 字节裸摘要**，不是 32 位 hex 字符串 |
| +24 | 4 | `FF FF FF FF` | 结尾标记 |

### 举例说明

假设 `工程文件.bin` 是 31208 字节，MD5 是 `c56fa07156c405eec04d261b50656323`：

```
尾缀 = CD EF CD EF                          (4 字节魔数)
     + E8 79 00 00                          (31208 = 0x79E8，小端)
     + C5 6F A0 71 56 C4 05 EE              (MD5 前 8 字节)
       C0 4D 26 1B 50 65 63 23              (MD5 后 8 字节)
     + FF FF FF FF                          (4 字节结尾)
     ────────────────────────────
     共 28 字节
```

**最终文件** = 31208 + 28 = **31236 字节**

## 7.3 用脚本生成（强烈推荐）

### 命令

```powershell
# 打包
python E:\桌面\deepseek\玩\outputs\ota_pack.py "工程文件.bin"

# 输出：
# [pack] 工程文件.bin
#        原始长度 : 31208  (0x79E8)
#        MD5      : c56fa07156c405eec04d261b50656323
#        尾缀     : cdefcdefe8790000c56fa07156c405eec04d261b50656323ffffffff
#        输出     : 工程文件_ota.bin  (31236 字节)

# 校验
python E:\桌面\deepseek\玩\outputs\ota_pack.py --verify "工程文件_ota.bin"
```

### ✅ 可靠性说明

**这个脚本用官方样例验证过**：
- 输入：36 号工程的原始 bin（31208 字节）
- 输出：31236 字节
- 与官方 `工程文件.bin` **逐字节完全相同**

## 7.4 两个致命错误

### 错误 1：把 `cdefcdef` 当 ASCII 字符串

```
❌ 错误：写成 8 个 ASCII 字符 "c","d","e","f","c","d","e","f"
        → 尾缀变成 32 字节
        → 格式全错，云端校验失败

✅ 正确：写成 4 个十六进制字节 CD EF CD EF
```

**为什么会搞错**：教程 PDF 里写的是 `cdefcdef`，看起来像 ASCII 字符，实际是十六进制表示。

**证据**：PDF 里给的示例串 `cdefcdef7c38...` 是 hex 字符串，`cdefcdef` 占 8 个 hex 字符 = 4 字节。

### 错误 2：MD5 写成 32 位 hex 字符串

```
❌ 错误：把 "c56fa071..." 这 32 个字符直接拼进去
        → 尾缀变成 4 + 4 + 32 + 4 = 44 字节

✅ 正确：转成 16 字节二进制
```

## 7.5 上传阿里云

### 操作步骤

1. 阿里云后台 → **监控运维 → OTA 升级**
2. 选择产品 → **添加升级包**
3. 填写：

| 字段 | 填什么 | 说明 |
|---|---|---|
| 升级包类型 | **整包** | 不差分 |
| 升级包名称 | `fw_v1.1` | 随便起 |
| **升级包版本号** | **23 位，与代码完全一致** | ⚠️ 关键 |
| 上传文件 | **`_ota.bin`** | 加过尾缀的 |
| 签名算法 | MD5 | |
| 升级包描述 | 随便 | |

4. 点击 **验证**（阿里云会自动校验格式）

### 创建升级任务

1. 升级包列表 → 找到刚上传的包 → **创建升级任务**
2. 填写：

| 字段 | 填什么 |
|---|---|
| 升级方式 | **静态升级** |
| 待升级设备 | 勾选你的设备 |
| 升级范围 | 全量 |

3. 点击 **确认**

### ⚠️ 版本号必须比当前新

平台是**比对版本号**的，不是比对文件内容。

如果推的包版本号和设备当前一样，平台不会推。

**所以每次升级都要改版本号**，而且：
- BootLoader 的 `main.h` 里 `VERSION_LEN` 相关
- APP 的 `main.h` 里版本号字符串
- 阿里云后台的升级包版本号

**三处必须一致。**

## 7.6 需要准备的截图（答辩用）

| 截图 | 证明什么 |
|---|---|
| 产品创建成功 | 后台配置 |
| 三元组弹窗 | 拿到凭证 |
| 设备在线状态 | MQTT 连通 |
| 版本号显示 | 上报成功 |
| OTA 升级包列表 | 升级包制作 |
| 升级任务详情 | 推送成功 |
| 升级前后版本对比 | 升级生效 |

---

# 八、任务总表

| # | 任务 | 工期 | 完成标准 | 检查点 |
|---|---|---|---:|---|
| C1 | 阿里云建产品 | 0.5 | 创建成功，数据格式选透传 | W0 |
| C2 | 建设备拿三元组 | 0.5 | 记录完整，发给 A 和 B | W0 |
| C3 | 功能定义 | 0.5 | `powerstate` 已发布上线 | W0 |
| C4 | 移植加密库 | 0.5 | 能编译，HMAC 结果正确 | W1 |
| C5 | **移植 MQTT** | 3 | 能编译，报文格式正确 | W2 |
| C6 | 连接鉴权 | 2 | 串口打印"连接成功" | W2 |
| C7 | 订阅 Topic | 1 | 3 个 Topic 都订阅成功 | W2 |
| C8 | 版本上报 | 1 | 云端显示正确版本 | W3 |
| C9 | OTA 推送解析 | 1.5 | 能正确打印 4 个字段 | W3 |
| C10 | 升级包制作 | 0.5 | `--verify` 通过 | W3 |
| C11 | 配置 OTA 推送 | 1 | 后台显示推送成功 | W3 |
| C12 | 联调 | 1 | 全链路走通 | W4 |
| | **合计** | **13** | | |

---

# 九、排错手册

## 9.1 连接失败

| 现象 | 原因 | 解决 |
|---|---|---|
| 一直连不上（无 CONNACK） | ① 域名解析失败 ② 网络不通 ③ 端口错 | 检查 ProductKey；确认 GPRS 已激活；端口 1883 |
| CONNACK 返回 `0x04` | 用户名密码错 | **检查 HMAC-SHA1 计算**：明文拼接顺序、key 是否传对 |
| CONNACK 返回 `0x05` | 未授权 | 设备被禁用，后台检查 |
| CONNACK 返回 `0x02` | ClientID 格式错 | 检查 `\|securemode=3,signmethod=hmacsha1\|` |
| 连上就断 | 保活超时 | 检查 PING 定时器（60 秒） |
| 连上几分钟后断 | 没发 PING | 加 `MQTT_PingREQ()` |

## 9.2 订阅失败

| 现象 | 原因 | 解决 |
|---|---|---|
| SUBACK 返回 `0x80` | Topic 无权限 | 检查 Topic 格式，`{pk}` `{dn}` 是否替换对 |
| 订阅成功但收不到消息 | 云端没推 | 后台检查是否创建了升级任务 |
| 收到乱码 | 报文解析错 | 打印原始字节，检查剩余长度计算 |

## 9.3 版本上报问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 云端看不到版本 | 上报 Topic 错 | 检查 `/ota/device/inform/` |
| 版本号乱码 | 长度不对 | 必须 23 位，且是字符串 |
| 一直不升级 | 版本号没变 | 云端比对版本，相同就不推 |
| 上报了但设备离线 | 连接断了 | 检查 PING 保活 |

## 9.4 升级包问题

| 现象 | 原因 | 解决 |
|---|---|---|
| 后台校验失败 | 尾缀格式错 | 用 `ota_pack.py` 重新生成 |
| 后台说版本号不合法 | 不是 23 位 | 检查长度 |
| 推送了但设备没反应 | 设备离线 / Topic 错 | 检查设备在线状态 |
| 设备收到但不下载 | 解析失败 | 打印原始报文，检查 sscanf 格式串 |

## 9.5 加密库问题

| 现象 | 原因 | 解决 |
|---|---|---|
| HMAC 结果不对 | 参数顺序错 | `(msg, msg_len, out, key, key_len)` |
| 编译报错 | 缺头文件 | 加 `#include "utils_hmac.h"` |
| 结果长度不对 | 输出缓冲太小 | 至少 41 字节（40 + '\0'） |

---

# 十、答辩准备

## 10.1 讲稿结构（3 分钟）

### 第 1 分钟：平台配置

> "我们在阿里云物联网平台创建了产品和设备，拿到三元组。
> 数据格式选了透传/自定义，这样平台不会额外解析我们的报文。"

### 第 2 分钟：MQTT 鉴权

> "阿里云不用用户名密码，而是用三元组算签名。
> 把 ClientID、DeviceName、ProductKey 拼成明文，
> 用 DeviceSecret 做密钥做 HMAC-SHA1，结果就是 MQTT 的密码。
> 这样密钥不需要在网络上传输，很安全。"

### 第 3 分钟：OTA 流程与升级包

> "设备连上后上报版本号，云端比对发现新版本就推送到 upgrade Topic。
> 我们解析出 URL 和固件大小，交给 4G 模块下载。
> 升级包要加 28 字节尾缀，包含长度和 MD5，供 BootLoader 校验。"

## 10.2 可能被问到的问题

**Q：为什么用 MQTT 不用 HTTP？**
> MQTT 是长连接，云端可以主动推送，实时性好；HTTP 要设备轮询，浪费流量。

**Q：HMAC-SHA1 和普通 SHA1 有什么区别？**
> HMAC 加了密钥，同样的明文不同密钥得到不同结果，所以能用来鉴权。
> 而且它防重放：即使截获了密文，没有密钥也算不出新的。

**Q：为什么升级包要加 28 字节尾缀？**
> 阿里云要求带校验信息。BootLoader 用它验证固件完整性，防止下载损坏的固件被烧进去。

**Q：版本号为什么必须 23 位？**
> 这是 BootLoader 里的硬编码结构体长度。太短会读到脏数据，太长会溢出。
> 两边必须一致。

**Q：升级包被篡改会怎样？**
> MD5 校验会失败，BootLoader 拒绝搬移，设备继续用旧固件。

**Q：QoS 0 和 QoS 1 有什么区别？**
> QoS 0 是"最多一次"，发出去不管；QoS 1 是"至少一次"，有 PUBACK 确认。
> 属性上报用 QoS 1（重要），OTA 通知云端用 QoS 0。

**Q：为什么 DeviceSecret 不能泄露？**
> 它是算 MQTT 密码的密钥。泄露了别人就能冒充你的设备连上云端。

---

# 十一、常用参考

| 内容 | 位置 |
|---|---|
| 加密库（直接复制） | `36号工程\HMAC\scr\` 和 `\inc\` |
| MQTT 源码 | `36号工程\HARDWARE\Scr\mqtt.c` |
| OTA 解析参考 | `36号工程\HARDWARE\Scr\cat1.c`（`U2PassiveEvent` 函数） |
| 升级包工具 | `outputs/ota_pack.py` |
| 你的基础工程 | `E:\MY_project\STM32F103ZET6_Demo10DMA` |

---

# 一句话记住

**让设备能被云端认出来、能报版本、能收到升级通知，并做出正确的升级包。**
