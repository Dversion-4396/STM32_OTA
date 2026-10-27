# DX-CT511/DX-CT511N AT 指令手册（提取）

> 来源：`E:\桌面\大夏龙雀科技-资料下载\产品技术手册（PDF）\4G+定位模块\DX-CT511\DX-CT511&CT511N_串口UART_应用指导.pdf`
> 提取日期：2026-10-27
> 模块型号：LYNQ_L511C_2C（大夏龙雀 DX-CT511 方案）

---

## ⚠️ 最重要的一条

**HTTP 指令前缀是 `AT\` 而不是 `AT+`！**

| 错误写法（阿里云/Air724 风格） | 正确写法（本模块） |
|---|---|
| `AT+HTTPINIT` | `AT\$HTTPOPEN` |
| `AT+HTTPPARA` | `AT\$HTTPPARA` |
| `AT+HTTPACTION` | `AT\$HTTPACTION` |
| `AT+HTTPREAD` | **不需要**（数据主动推送） |
| `AT+HTTPTERM` | `AT\$HTTPCLOSE` |

---

## 一、实测验证通过的完整流程（2026-10-27）

```
① AT+QICSGP=1,1,"CMNET","","",1     → OK      配置 APN
② AT\$HTTPOPEN?                      → \    查状态
③ AT\$HTTPOPEN                       → OK      开启服务
④ AT\$HTTPPARA=http://httpbin.org/get,80  → OK  配置URL
⑤ AT\$HTTPACTION=0                   → 见下方响应   发GET
⑥ AT\$HTTPCLOSE                      → OK      关闭服务
```

### 第 ⑤ 步的实际响应（实测原文）

```
AT\$HTTPACTION=0

\$HTTPRECV:DATA,230
HTTP/1.1 200 OK
Date: Tue, 27 Oct 2026 12:32:56 GMT
Content-Type: application/json
Content-Length: 199
Connection: keep-alive
Server: gunicorn/19.9.0
Access-Control-Allow-Origin: *
Access-Control-Allow-Credentials: true

\$HTTPRECV:DATA,199
{"args": {}, "headers": {"Host": "httpbin.org"}, "origin": "223.104.110.56", "url": "http://httpbin.org/get"}
OK
```

### ★ 关键理解：数据是主动推送的

- `\$HTTPRECV:DATA,<len>` 是**分段推送**的
- 响应头和响应体**分成两段**（230 字节 + 199 字节）
- `<len>` 是**该段的字节数**
- **不需要 `AT+HTTPREAD`，不需要偏移量，数据直接来**

**对 OTA 下载非常友好**：收到 `\$HTTPRECV:DATA,<len>` 后，
把后面 `<len>` 字节直接写入 W25Q128 即可。

---

## 二、HTTP 指令详解（手册第 5.6 节原文）

HTTP 相关指令
5.6.1. 开启HTTP 服务
功能
指令
响应
说明
查询
AT$HTTPOPEN?
$HTTPOPEN:<stat>
OK
<stat>：状态

----- PAGE 30 -----
DX-CT511/DX-CT511N 4G 串口应用指导
深圳大夏龙雀科技有限公司
www.szdx-smart.com
- 30 -
0: 未开启
1: 已开启
开启服务
AT$HTTPOPEN
OK
5.6.2. 关闭HTTP 服务
功能
指令
响应
说明
查询
AT$HTTPCLOSE?
$HTTPCLOSE:<stat>
OK
<stat>: 状态
0: 未关闭
1: 已关闭
关闭服务
AT$HTTPCLOSE
OK
5.6.3. 配置HTTP 的URL 信息
功能
指令
响应
说明
查询
AT$HTTPPARA?
Host : "<host>"
URI : "<uri>"
Port : <port>
Cert : <cert>
<url>：URL
范围：0-255 字节
<port>：URL 端口号
默认：80 (HTTP)
设置
AT$HTTPPARA=
<url>,<port>
OK
<host>：主机域名/IP
<uri>：URI
<cert>：证书(默认：0 无证书)
5.6.4. 发送HTTP 请求
功能
响应
说明
发送请求
AT$HTTPACTION=
<request>
$HTTPRECV:
DATA,<len>
......
<request>：请求类型
0: GET
1: POST (大数据量)
2: HEAD
3: POST（小数据量）
<len>：响应的数据长度
备注：
1. 请求成功后，模块返回HTTP 响应头或HTML 文本等信息
2. 发送POST 请求，需要设置请求头和请求体，请求头需包含Content-Length 字段

----- PAGE 31 -----
DX-CT511/DX-CT511N 4G 串口应用指导
深圳大夏龙雀科技有限公司
www.szdx-smart.com
- 31 -
3. <request>=1 时，请求体数据需要在该指令发送后提交
4. <request>=3 时，请求体数据需要在该指令发送前提交
5. 返回ERROR 202 时，需先发送AT$HTTPCLOSE，再发送AT$HTTPOPEN，重启HTTP 服务
5.6.5. 设置请求头字段
功能
指令
响应
说明
查询
AT$HTTPRQH?
List:{<key>:<value>}
OK
<key>：请求头字段的键
范围：0-50 字节
设置请求头字段
AT$HTTPRQH=
<key>,<value>
OK
<value>：请求头字段的值
范围：0-255 字节
备注：
1. <key>和<value>参数，若存在特殊字符，需要加上引号
2. 该指令需要在发送HTTP 请求前，即指令AT$HTTPACTION 前发送
5.6.6. 设置请求体数据
功能
指令
响应
说明
设置小数据量
AT$HTTPDATAEX=
<short_data_len>,<data>
OK
<short_data_len>：数据长度
范围：0 - 500
<data>：数据内容
设置大数据量
AT$HTTPDATA=
<long_data_len>
>>
<long_data_len>：数据长度
范围：0 - 1024 字节
备注：
AT$HTTPDATAEX 指令说明：
1. 该指令只能在发送小数据量的POST 请求，即AT$HTTPACTION=3 时使用
2. <short_data_len>长度需要与请求头字段Content-Length 的一致
AT$HTTPDATA 指令说明：
1. 该指令只能在发送大数据量的POST 请求，即AT$HTTPACTION=1 时使用
2. 该指令响应提示符>>后即可输入数据，数据长度不足则会等待输入
3. 每次设置完请求体数据后，需要发送AT$HTTPSEND，提交请求体数据
4. 该指令可多次发送，结束请求体数据提交时，会把多次设置的请求体数据整合到一起提交

----- PAGE 32 -----
DX-CT511/DX-CT511N 4G 串口应用指导
深圳大夏龙雀科技有限公司
www.szdx-smart.com
- 32 -
5. 结束请求体数据提交，需要发送AT$HTTPDATA=0 和AT$HTTPSEND
6. <long_data_len>之和的长度需要与请求头字段Content-Length 的一致
5.6.7. 提交请求体数据
功能
指令
响应
说明
提交请求体数据
AT$HTTPSEND
OK
5.6.8. HTTP 错误码
200：子系统已建立并可用
201：子系统建立正在进行中
202：网络子系统不可用
203：PPP 正在关闭
204：已存在网络子系统资源
205：物理链路进入休眠状态
300：HTTP 服务未开启
301：HTTP 服务已开启
302：URL 解析失败
303：DNS 错误
304：操作错误
305：请求超时
306：文件下载中
307：URL 未设置
308：请求头字段数量超过限制
309：请求头字段错误，如POST 请求未设置"Content-Length"
310：响应头异常
311：正在发送POST 数据
312：POST 请求未启动，仅适用于$HTTPACTION=1
313："Content-Length"的值与内容长度不一致
314：请求失败，需关闭套接字
315：连接服务器失败
316：EFS 空间不足
317：EFS 操作失败
350：未知HTTP 错误


---

## 三、其他有用的指令（从 CLAC 列表 + 手册提取）

### 网络配置
```
AT+QICSGP=1,1,"CMNET","","",1    配置 APN（移远风格）
AT+QIACT=1                        激活 PDP
AT+QIACT?                         查询（返回 \$MYNETACT:0,1,"IP"）
AT+NETOPEN                        开网络（中兴微风格）
AT+NETCLOSE                       关网络
AT+CGPADDR=1                      查 IP
AT+CGATT?                         GPRS 附着状态
AT+CGACT?                         PDP 激活状态
```

### 信号与状态
```
AT+CSQ                            信号质量
AT+CREG?                          网络注册
AT+CEREG?                         LTE 注册
AT+CPIN?                          SIM 卡状态
AT+ICCID                          SIM 卡号
AT+CPSI                           系统信息
```

### MQTT（巴法云方案可能需要）
```
AT+MCONFIG                        MQTT 配置
AT+MCONNECT                       MQTT 连接
AT+MPUB                           MQTT 发布
AT+MPUBEX                         MQTT 发布（扩展）
AT+MSUB                           MQTT 订阅
AT+MUNSUB                         MQTT 取消订阅
AT+MQTTSTATU                      MQTT 状态
AT+MDISCONNECT                    MQTT 断开
AT+MIPCLOSE                       释放 MQTT 资源
```

### TCP Socket（备选方案）
```
AT+CIPMODE=0/1                    0=AT指令模式 1=透传模式
AT+CIPOPEN                        建立连接
AT+CIPSEND                        发送数据
AT+CIPCLOSE                       关闭连接
ATO                               退出透传模式
```

### FOTA（模块自身固件升级，不是我们的 OTA）
```
AT+FOTA
AT\$MYFOTA
AT+MLHFOTA
```

---

## 四、HTTP 错误码对照表

| 码 | 含义 | 处理 |
|---|---|---|
| 200 | 子系统已建立并可用 | 正常 |
| 201 | 子系统建立进行中 | 等待 |
| **202** | **网络子系统不可用** | **先 `AT\$HTTPCLOSE` 再 `AT\$HTTPOPEN`** |
| 203 | PPP 正在关闭 | 等待 |
| 204 | 已存在网络子系统资源 | 先关闭再重开 |
| 205 | 物理链路进入休眠 | 唤醒 |
| 300 | HTTP 服务未开启 | 先 `AT\$HTTPOPEN` |
| 301 | HTTP 服务已开启 | 无需重复开启 |
| 302 | URL 解析失败 | 检查 URL 格式 |
| 303 | DNS 错误 | 检查域名/换 IP |
| 304 | 操作错误 | — |
| 305 | 请求超时 | 重试 |
| 306 | 文件下载中 | 等待 |
| 307 | URL 未设置 | 先 `AT\$HTTPPARA` |
| 308 | 请求头字段数量超限 | — |
| 309 | 请求头字段错误 | 检查 Content-Length |
| 310 | 响应头异常 | — |
| 311 | 正在发送 POST 数据 | 等待 |
| 312 | POST 请求未启动 | 仅 `ACTION=1` |
| 313 | Content-Length 不匹配 | 检查长度 |
| 314 | 请求失败，需关闭套接字 | `AT\$HTTPCLOSE` |
| 315 | 连接服务器失败 | 检查网络/URL |
| 316 | EFS 空间不足 | — |
| 317 | EFS 操作失败 | — |
| 350 | 未知 HTTP 错误 | — |

---

## 五、对 A 的工作的影响

### 原来以为要做的（基于阿里云 Air724 方案）
```
AT+HTTPINIT
AT+HTTPPARA="CID",1
AT+HTTPPARA="URL","..."
AT+HTTPACTION=0
等 +HTTPACTION: 0,200,<len>
AT+HTTPREAD=<offset>,256      ← 需要循环，带偏移量
AT+HTTPTERM
```

### 实际要做的（本模块）
```
AT+QICSGP=1,1,"CMNET","","",1
AT\$HTTPOPEN
AT\$HTTPPARA=<url>,<port>
AT\$HTTPACTION=0
循环接收 \$HTTPRECV:DATA,<len> + 数据    ← 数据主动推送，无需偏移量
AT\$HTTPCLOSE
```

**★ 比原方案简单得多：不需要 HTTPREAD，不需要算偏移量 ★**

### 实现要点

1. **`AT\$HTTPACTION=0` 之后不能再用 `CAT1_SendAT` 等 "OK"**
   因为响应是分多段推送的，中间夹着 `\$HTTPRECV:DATA,<len>` 和数据，
   必须自己写接收循环，解析 `\$HTTPRECV:DATA,` 后面的长度。

2. **解析方法**
   ``c
   /* 收到一段数据后 */
   p = strstr((char *)usart3_value.data, "\$HTTPRECV:DATA,");
   if (p != NULL)
   {
       p += strlen("\$HTTPRECV:DATA,");
       len = atoi(p);              /* 这段的长度 */
       p = strchr(p, '\n');        /* 跳过换行 */
       if (p) { p++; /* p 现在指向真正的数据起始位置 */ }
       /* 把 p 开始的 len 字节写到 W25Q128 */
   }
   ``

3. **响应头和响应体分开**
   第一段 `\$HTTPRECV:DATA,230` 是 HTTP 响应头（含 Content-Length）
   第二段 `\$HTTPRECV:DATA,199` 是响应体（真正的数据）
   **下载固件时：跳过响应头，只取响应体**

4. **判断下载完成**
   根据 HTTP 响应头里的 `Content-Length`，或者看有没有收到最后的 `OK`。

---

## 六、手册原始位置

```
E:\桌面\大夏龙雀科技-资料下载\产品技术手册（PDF）\4G+定位模块\
  DX-CT511\
    DX-CT511&CT511N_串口UART_应用指导.pdf      ← AT 指令手册（38页）
    DX-CT511&DX-CT511N-4G模块技术手册.pdf      ← 硬件规格（43页）
```

**其他型号手册也在同目录**（CT01/CT02/CT11/CT12/GP10/GP21/GP22/GP23/GP24）。

---

**文档结束**