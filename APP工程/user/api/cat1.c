/*=====================================================================
 * 文件：cat1.c
 * 说明：L511-2 4G 模块驱动实现
 * 负责：A（CAT1_ParseOTA 由 C 负责）
 *
 *=====================================================================
 * ★★★ 当前进度（2026-10-27）★★★
 *
 *   ✅ CAT1_SendAT      已实现，实测通过
 *   ✅ CAT1_ActiveGPRS  已实现，实测通过（拿到 IP）
 *   ⏳ CAT1_PowerOn      待写（很简单，只等模块启动）
 *   ⏳ CAT1_HttpInit     待写（★HTTP 方案待定，见下方硬件结论★）
 *   ⏳ CAT1_HttpRead     待写（同上）
 *   ⏳ CAT1_OTA          待写
 *   ⏳ CAT1_ParseOTA     待写（C 负责）
 *
 *=====================================================================
 * ★★★ 硬件实测结论（2026-10-27，最关键）★★★
 *
 * 模块型号：LYNQ_L511C_2C
 * 固件版本：L511C_2Cv02.01b07.00（2024/06/07）
 *
 * 【支持的指令】实测通过：
 *   AT                        → OK
 *   ATI                       → LYNQ_L511C_2C
 *   AT+CGMR                   → L511C_2Cv02.01b07.00
 *   AT+ICCID                  → +ICCID: 898600501626fa367139（移动号段）
 *   AT+CPIN?                  → +CPIN: READY
 *   AT+CSQ                    → +CSQ: 24,99（信号极好）
 *   AT+CREG?                  → +CREG: 0,1（已注册）
 *   AT+CGATT?                 → +CGATT: 1（已附着）
 *   AT+CGACT?                 → +CGACT: 1,1（PDP 已激活）
 *   AT+CGDCONT=1,"IP","CMNET" → OK
 *   AT+CGACT=1,1              → OK
 *   AT+CGPADDR=1              → +CGPADDR: 1,"10.224.56.26"（有 IP）
 *
 * 【不支持的指令】实测返回 ERROR：
 *   AT+SAPBR=3,1,"Contype","GPRS"  → ERROR
 *   AT+SAPBR=3,1,"APN","CMNET"     → ERROR
 *   AT+SAPBR=1,1                   → ERROR
 *   AT+HTTPINIT                    → ERROR（用错前缀，见下方）
 *   AT+HTTPINIT=?                  → ERROR
 *   AT+HTTPTERM                    → ERROR
 *   AT+CIPSTART=?                  → ERROR
 *
 *=====================================================================
 * ★★★ HTTP 已解决！★★★（2026-10-27 20:32 实测通过）
 *
 * 根本原因：HTTP 指令前缀是 【AT$】 不是 【AT+】
 *
 *   错误写法（阿里云 Air724 风格）    正确写法（本模块）
 *   ------------------------------------------------
 *   AT+HTTPINIT                       AT$HTTPOPEN
 *   AT+HTTPPARA="CID",1               （不需要）
 *   AT+HTTPPARA="URL","..."           AT$HTTPPARA=<url>,<port>
 *   AT+HTTPACTION=0                   AT$HTTPACTION=0
 *   AT+HTTPREAD=<offset>,<len>        （不需要！数据主动推送）
 *   AT+HTTPTERM                       AT$HTTPCLOSE
 *
 * 官方手册：
 *   E:\桌面\大夏龙雀科技-资料下载\产品技术手册（PDF）\4G+定位模块\
 *   DX-CT511\DX-CT511&CT511N_串口UART_应用指导.pdf
 *   （第 5.6 节 HTTP 相关指令，第 3.1.2.4 节 HTTP 示例）
 *   已提取到 docs/CT511_AT指令手册_提取.md
 *
 *=====================================================================
 * 实测通过的完整流程：
 *
 *   ① AT+QICSGP=1,1,"CMNET","","",1   → OK      配置 APN
 *   ② AT$HTTPOPEN                     → OK      开启 HTTP 服务
 *   ③ AT$HTTPPARA=http://xxx/fw.bin,80 → OK     配置 URL
 *   ④ AT$HTTPACTION=0                 → 数据推送  发 GET
 *   ⑤ 循环接收 $HTTPRECV:DATA,<len> + 数据
 *   ⑥ AT$HTTPCLOSE                    → OK      关闭服务
 *
 *=====================================================================
 * ★ 关键理解：数据是【主动推送】的，不需要 HTTPREAD ★
 *
 * 实测 AT$HTTPACTION=0 的响应：
 *
 *   AT$HTTPACTION=0
 *   $HTTPRECV:DATA,230              ← 第一段：HTTP 响应头，230 字节
 *   HTTP/1.1 200 OK
 *   Date: Tue, 27 Oct 2026 12:32:56 GMT
 *   Content-Type: application/json
 *   Content-Length: 199             ← 响应体长度
 *   Connection: keep-alive
 *   ...
 *   $HTTPRECV:DATA,199              ← 第二段：响应体，199 字节
 *   {"args": {}, ...}
 *   OK
 *
 * 所以：
 *   1. 响应头和响应体【分成两段】推送
 *   2. $HTTPRECV:DATA,<len> 里的 <len> 是【该段的字节数】
 *   3. 不需要 AT+HTTPREAD，不需要算偏移量
 *   4. 下载固件时：跳过响应头，只取响应体写 Flash
 *
 *=====================================================================
 * 三个实测踩过的坑
 *
 * 坑 1：AT 指令必须以 \r\n 结尾
 *       只发 "AT" 不加回车换行，模块只回显不执行。
 *       串口助手里要勾"发送新行"，或 HEX 发 41 54 0D 0A。
 *
 * 坑 2：模块只在开机时读 SIM 卡
 *       先上电、后插卡 → 一直报 +CME ERROR: 10（SIM not inserted），
 *       即使卡后来插好了也不认。
 *       正确顺序：断电 → 插卡 → 上电 → 等 15 秒。
 *
 * 坑 3：PWRKEY 是"开关机切换"，不是"开机键"
 *       拉低 1.5 秒释放 = 开机；再拉低 1.5 秒释放 = 关机。
 *       本工程用的是成品底板（上电即工作），不需要控 PWRKEY。
 *
 *=====================================================================
 * 接 STM32 的接线（战神V1）
 *
 *   4G 模块        战神V1           说明
 *   --------------------------------------------
 *   VIN      ->    5V              ★必须 5V，峰值 1.2A★
 *   GND      ->    GND             ★必须共地★
 *   TX       ->    PB11            USART3_RX，交叉
 *   RX       ->    PB10            USART3_TX，交叉
 *   EN       ->    悬空            （或接 3.3V）
 *   DTR      ->    不接            ★会导致模块复位★
 *
 *   接之前必做：
 *     1. 拔掉板子上的 P5 跳线帽（否则 PB10/PB11 被板载 RS232/WiFi 占用）
 *     2. K1 拨动开关拨到 TXD 侧（否则 PB11 接的是 JOYPAD_LAT）
 *
 *   供电提醒：
 *     模块发射瞬间要 1.2A，从开发板 5V 取电必须并联 470uF 电解电容，
 *     否则会把 STM32 的电压拉低导致复位。
 *=====================================================================*/
#include "cat1.h"

/*=====================================================================
 * 函数：CAT1_SendAT
 * 负责：A
 *
 * ★这是最核心的函数，其他都依赖它★
 *
 * 实现要点：
 *   1. 清标志：usart3_value.flag = 0; usart3_value.count = 0;
 *   2. Usart3_SendString(cmd);
 *      Usart3_SendString("\r\n");     ★必须加，不加模块不响应★
 *   3. 循环 timeout_ms 次：
 *        delay_ms(1)
 *        如果 usart3_value.flag == 1：
 *            if(strstr((char*)usart3_value.data, expect)) return 0;
 *            usart3_value.flag = 0; usart3_value.count = 0;
 *   4. 超时返回 1
 *
 * ★注意：usart3_value.data 是 u8 数组，strstr 需要 (char*) 强转★
 *====================================================================*/
u8 CAT1_SendAT(char *cmd, char *expect, u16 timeout_ms)
{
    usart3_value.flag = 0;
    usart3_value.count = 0;
    Usart3_SendString(cmd);
    Usart3_SendString("\r\n");
    while (timeout_ms--)
    {
        delay_ms(1);
        if (usart3_value.flag == 1)
        {
            if (usart3_value.count < USART3_BUFFER_SIZE)
            {
                usart3_value.data[usart3_value.count] = 0;
            }

            if (strstr((char *)usart3_value.data, expect) != NULL)
            {

                return 0;
            }
            usart3_value.flag = 0;
        }
    }
    return 1;
}
u8 CAT1_ActiveGPRS(void)
{
    u16 i;

    /*------------------------------------------------------------------
     * ★★★ 重要：CAT1_SendAT 的返回值约定 ★★★
     *   返回 0 = 收到期望关键字 = 成功
     *   返回 1 = 超时 = 失败
     *
     * 所以"失败才报错"要写成：  if (CAT1_SendAT(...))  { 报错 }
     * 千万不要写成：            if (!CAT1_SendAT(...)) { 报错 }  ← 反了！
     *------------------------------------------------------------------*/

    /*---------- 1. SIM 卡就绪 ----------*/
    if (CAT1_SendAT("AT+CPIN?", "READY", 3000))
    {
        Usart1_SendString("[GPRS] SIM卡未就绪\r\n");
        return 1;
    }
    Usart1_SendString("[GPRS] SIM卡就绪\r\n");

    /*---------- 2. 等网络注册（最多 60 秒）----------*/
    for (i = 0; i < 60; i++)
    {
        if (CAT1_SendAT("AT+CREG?", ",1", 2000) == 0)
            break; /* 本地注册 */
        if (CAT1_SendAT("AT+CREG?", ",5", 2000) == 0)
            break; /* 漫游注册 */
        delay_ms(1000);
    }
    if (i >= 60)
    {
        Usart1_SendString("[GPRS] 网络注册超时\r\n");
        return 2;
    }
    Usart1_SendString("[GPRS] 网络已注册\r\n");

    /*---------- 3. 设置 PDP 上下文 ----------*/
    if (CAT1_SendAT("AT+CGDCONT=1,\"IP\",\"CMNET\"", "OK", 3000))
    {
        Usart1_SendString("[GPRS] 设置APN失败\r\n");
        return 3;
    }

    /*---------- 4. 附着 GPRS ----------*/
    if (CAT1_SendAT("AT+CGATT=1", "OK", 10000))
    {
        Usart1_SendString("[GPRS] 附着失败\r\n");
        return 4;
    }

    /*---------- 5. 激活 PDP ----------*/
    if (CAT1_SendAT("AT+CGACT=1,1", "OK", 20000))
    {
        Usart1_SendString("[GPRS] 激活PDP失败\r\n");
        return 5;
    }

    /*---------- 6. 确认拿到 IP ----------*/
    if (CAT1_SendAT("AT+CGPADDR=1", "+CGPADDR:", 5000))
    {
        Usart1_SendString("[GPRS] 未拿到IP\r\n");
        return 6;
    }

    /*---------- 打印 IP ----------*/
    Usart1_SendString("[GPRS] IP: ");
    Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
    Usart1_SendString("\r\n");

    Usart1_SendString("[GPRS] 成功\r\n");
    return 0;
}

/*=====================================================================
 * 函数：CAT1_HttpInit
 * 负责：A
 *
 * 功能：开启 HTTP 服务、配置 URL、发起 GET 请求
 * 参数：url - 完整 URL（含 http:// 前缀），如 "http://xxx.com/fw.bin"
 * 返回：0 = 成功（已发出请求，数据会推送过来）
 *       1 = 配置 APN 失败
 *       2 = 开启 HTTP 服务失败
 *       3 = 配置 URL 失败
 *       4 = 发送请求失败
 *
 *=====================================================================
 * ★ 正确指令（2026-10-27 实测通过）★
 *
 * ★★★ 前缀是 AT$ 不是 AT+ ★★★
 *
 *   1. 配置 APN
 *        CAT1_SendAT("AT+QICSGP=1,1,\"CMNET\",\"\",\"\",1", "OK", 10000);
 *        失败 return 1;
 *
 *   2. 开启 HTTP 服务
 *        CAT1_SendAT("AT$HTTPOPEN", "OK", 10000);
 *        失败 return 2;
 *
 *   3. 配置 URL（★注意：url 参数要带 http:// 前缀，端口单独传★）
 *        sprintf(cmd, "AT$HTTPPARA=%s,80", url);
 *        CAT1_SendAT(cmd, "OK", 8000);
 *        失败 return 3;
 *
 *   4. 发送 GET 请求
 *        CAT1_SendAT("AT$HTTPACTION=0", "OK", 5000);
 *        失败 return 4;
 *
 * ★ 第 4 步注意 ★
 *   AT$HTTPACTION=0 会立刻回 OK（表示指令收到了），
 *   然后数据【异步推送】过来，形如：
 *     $HTTPRECV:DATA,230
 *     <230 字节响应头>
 *     $HTTPRECV:DATA,199
 *     <199 字节响应体>
 *     OK
 *
 *   所以第 4 步只要确认回了 OK 就行，
 *   真正的数据接收由 CAT1_HttpRead 或 OTA 主循环处理。
 *
 *=====================================================================
 * ★ 常见错误码（看模块返回的 ERROR xxx）★
 *
 *   202 网络子系统不可用 → 先 AT$HTTPCLOSE 再 AT$HTTPOPEN
 *   300 HTTP 服务未开启 → 先 AT$HTTPOPEN
 *   301 HTTP 服务已开启 → 不用重复开
 *   302 URL 解析失败    → 检查 URL 格式（必须有 http://）
 *   303 DNS 错误        → 域名解析失败，换 IP 试试
 *   307 URL 未设置      → 先 AT$HTTPPARA
 *   315 连接服务器失败  → 检查 URL 和网络
 *
 *=====================================================================
 * ★ 实现示例 ★
 *
 *   u8 CAT1_HttpInit(char *url)
 *   {
 *       char cmd[600];      /* URL 最长 255，加指令前缀共 600 够用 *\/
 *
 *       /* 1. 配置 APN *\/
 *       if (CAT1_SendAT("AT+QICSGP=1,1,\"CMNET\",\"\",\"\",1", "OK", 10000))
 *           return 1;
 *
 *       /* 2. 开启 HTTP 服务 *\/
 *       if (CAT1_SendAT("AT$HTTPOPEN", "OK", 10000))
 *           return 2;
 *
 *       /* 3. 配置 URL *\/
 *       sprintf(cmd, "AT$HTTPPARA=%s,80", url);
 *       if (CAT1_SendAT(cmd, "OK", 8000))
 *           return 3;
 *
 *       /* 4. 发送 GET 请求 *\/
 *       if (CAT1_SendAT("AT$HTTPACTION=0", "OK", 5000))
 *           return 4;
 *
 *       return 0;
 *   }
 *====================================================================*/
u8 CAT1_HttpInit(char *url)
{
    char cmd[URL_LEN];
    if (CAT1_SendAT("AT+QICSGP=1,1,\"CMNET\",\"\",\"\",1", "OK", 10000))
    {
        Usart1_SendString("APN配置失败\r\n");
        return 1;
    }
    if (CAT1_SendAT("AT$HTTPOPEN", "OK", 10000))
    {
        Usart1_SendString("[HTTP] 服务开启失败\r\n");
        return 2;
    }
    sprintf(cmd, "AT$HTTPPARA=%s,80", url);
    if (CAT1_SendAT(cmd, "OK", 8000))
    {
        Usart1_SendString("[URL ]配置失败\r\n");
        return 3;
    }
    if (CAT1_SendAT("AT$HTTPACTION=0", "OK", 5000))
    {
        Usart1_SendString("[GET] 请求发送失败\r\n");
        return 4;
    }
    Usart1_SendString("[HTTP]连接成功\r\n");
    return 0;
}

/*=====================================================================
 * 函数：CAT1_HttpRead
 * 负责：A
 *
 * 功能：从收到的数据里提取一段 $HTTPRECV:DATA 的载荷
 * 参数：data   - 收到的一段数据（usart3_value.data）
 *       len    - 这段数据的长度（usart3_value.count）
 *       outbuf - 输出缓冲区（提取出的数据放这里）
 *       maxlen - outbuf 的最大长度
 * 返回：0 = 提取到数据，*outlen 是实际提取的字节数
 *       1 = 这段数据不是 $HTTPRECV:DATA 格式
 *       2 = outbuf 不够大
 *
 *=====================================================================
 * ★★★ 这个函数和原来的设计完全不同 ★★★
 *
 *   原设计（阿里云 Air724）：
 *     AT+HTTPREAD=<offset>,<len>   ← 要自己指定偏移量去读
 *
 *   本模块：
 *     数据主动推送，格式是：
 *       $HTTPRECV:DATA,<len>\r\n<len 字节数据>
 *
 *   所以这个函数【不是发指令读数据】，
 *   而是【从已收到的数据里把载荷提取出来】。
 *
 *=====================================================================
 * ★ 数据格式解析 ★
 *
 * 收到的原始数据示例：
 *
 *   \r\n$HTTPRECV:DATA,199\r\n{"args": {}, ...}\r\nOK\r\n
 *       ↑                 ↑ ↑                  ↑
 *     头部标记          长度  真正的数据        结束
 *
 * 解析步骤：
 *   1. strstr 找到 "$HTTPRECV:DATA,"
 *   2. 从它后面 atoi 出长度
 *   3. 找 '\n' 跳过换行
 *   4. 从那里开始 memcpy 出 <len> 字节
 *
 *=====================================================================
 * ★ 实现示例 ★
 *
 *   u8 CAT1_HttpRead(u8 *data, u16 len, u8 *outbuf, u16 maxlen, u16 *outlen)
 *   {
 *       char *p;
 *       u16  dlen;
 *       u16  avail;
 *
 *       if (data == NULL || outbuf == NULL || outlen == NULL) return 1;
 *
 *       /* 1. 确保字符串以 \0 结尾，才能用 strstr *\/
 *       if (len >= USART3_BUFFER_SIZE) return 1;
 *       data[len] = 0;
 *
 *       /* 2. 找头部标记 *\/
 *       p = strstr((char *)data, "$HTTPRECV:DATA,");
 *       if (p == NULL) return 1;
 *
 *       /* 3. 解析长度 *\/
 *       p += strlen("$HTTPRECV:DATA,");
 *       dlen = (u16)atoi(p);
 *       if (dlen == 0) { *outlen = 0; return 0; }   /* 空段，正常 *\/
 *
 *       /* 4. 跳过长度后面的 \r\n *\/
 *       p = strchr(p, '\n');
 *       if (p == NULL) return 1;
 *       p++;
 *
 *       /* 5. 检查 outbuf 够不够 *\/
 *       if (dlen > maxlen) return 2;
 *
 *       /* 6. 检查剩余数据够不够 dlen 字节 *\/
 *       avail = (u16)(len - (u16)(p - (char *)data));
 *       if (avail < dlen) dlen = avail;   /* 不够就只取有的 *\/
 *
 *       /* 7. ★定长拷贝，必须用 memcpy★ *\/
 *       /* 固件是二进制，里面有 0x00，用 strcpy 会被截断 *\/
 *       memcpy(outbuf, p, dlen);
 *       *outlen = dlen;
 *
 *       return 0;
 *   }
 *
 *=====================================================================
 * ★ 三个注意点 ★
 *
 *   1. 必须用 memcpy 不能用 strcpy
 *      固件二进制里有 0x00，strcpy 遇到就停
 *
 *   2. 长度要双重检查
 *      ① dlen 不能超过 outbuf 大小
 *      ② 剩余数据不能少于 dlen（可能一段数据被中断拆开）
 *
 *   3. 响应头和响应体要分开处理
 *      第一段 $HTTPRECV:DATA,230 是 HTTP 响应头
 *      第二段 $HTTPRECV:DATA,199 是真正的固件数据
 *      下载时要判断：这段是头还是体
 *
 *====================================================================*/
u8 CAT1_HttpRead(u8 *outbuf, u16 maxlen, u16 *outlen)
{
    char *p;
    u16 dlen;
    u16 avail;
    /// 防止传入空指针
    if (outbuf == NULL || outlen == NULL)
    {
        Usart1_SendString("传入缓存区为空\r\n");
        return 1;
    }
    /// 清楚输出长度 防止外侧收到脏数据
    *outlen = 0;
    if (usart3_value.count >= USART3_BUFFER_SIZE)
    {
        Usart1_SendString("接受超过最大长度\r\n");
        return 1;
    }
    /// 给数据末尾加 \0
    usart3_value.data[usart3_value.count] = 0;
    /// 此时p在 $HTTPRECV:DATA,的第一位
    p = strstr((char *)usart3_value.data, "$HTTPRECV:DATA,");
    if (NULL == p)
    {
        Usart1_SendString("未找到标识符\r\n");
        return 1;
    }
    // 将p 指向支付串的末尾
    p += strlen("$HTTPRECV:DATA,");
    dlen = (u16)atoi(p);
    if (dlen == 0)
    {
        *outlen = 0;
        return 0;
    }
    // 跳过后的\r\n
    p = strchr(p, '\n');
    if (NULL == p)
    {
        Usart1_SendString("未找到换行符\r\n");
        return 1;
        /* code */
    }
    // p++指向真正的数据位
    p++;

    if (dlen > maxlen)
    {
        Usart1_SendString("数据超出缓冲区\r\n");
        return 2;
    }
    u16 offset; /* p 距离缓冲区开头多少字节 */

    /* 第一步：算 p 的偏移量 */
    offset = (u16)(p - (char *)usart3_value.data);

    /* 第二步：算 p 后面还剩多少 */
    avail = usart3_value.count - offset;

    /* 第三步：检查 */
    if (avail < dlen)
    {
        dlen = avail;
    }
    memcpy(outbuf, p, dlen);
    *outlen = dlen;
    return 0;
}

/*=====================================================================
 * 函数：CAT1_OTA
 * 负责：A
 *
 * 功能：完整下载固件到 W25Q128
 * 参数：url    - 固件下载地址
 *       fw_len - 固件总长度（含 28 字节尾缀）
 * 返回：0 = 成功
 *       2 = GPRS 激活失败
 *       3 = HTTP 初始化失败
 *       4 = 下载过程中出错
 *       5 = 擦 Flash 失败
 *
 *=====================================================================
 * ★ 完整流程（基于实测通过的指令）★
 *
 *   1. 激活 GPRS
 *        if (CAT1_ActiveGPRS()) return 2;
 *
 *   2. 开启 HTTP 并配置 URL
 *        if (CAT1_HttpInit(url)) return 3;
 *
 *   3. 擦 W25Q128（按固件大小算要擦几个 64KB 块）
 *        blocks = (fw_len + 65535) / 65536;
 *        for (b = 0; b < blocks; b++) W25Q128_EraseBlock(b);
 *
 *   4. ★循环接收数据，解析 $HTTPRECV:DATA，写 Flash★
 *        这是核心，难点在于：
 *          - 数据是异步推送的，不是一次给完
 *          - 每段前面有 $HTTPRECV:DATA,<len> 标记
 *          - 第一段是响应头，要跳过
 *          - 后面的段才是固件数据
 *
 *   5. 关闭 HTTP 服务
 *        CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
 *
 *   6. 校验（可选）
 *        读回 W25Q128 前 28 字节尾缀，检查魔数 CD EF CD EF
 *
 *=====================================================================
 * ★ 第 4 步的实现要点（最关键）★
 *
 *   u8 CAT1_OTA(char *url, u32 fw_len)
 *   {
 *       u8  buf[256];
 *       u16 dlen;
 *       u16 page = 0;
 *       u32 total = 0;          /* 已收到多少字节 *\/
 *       u8  got_header = 0;     /* 是否已跳过响应头 *\/
 *       u16 seg;
 *
 *       /* ... 步骤 1-3 ... *\/
 *
 *       /* 发请求 *\/
 *       Usart3_SendString("AT$HTTPACTION=0\r\n");
 *
 *       /* 循环接收（超时要够长，固件可能几 MB）*\/#
 *       for (seg = 0; seg < 60000; seg++)
 *       {
 *           delay_ms(10);
 *
 *           if (usart3_value.flag == 0) continue;
 *
 *           usart3_value.flag = 0;
 *
 *           /* 解析这段 *\/
 *           if (CAT1_HttpRead(usart3_value.data, usart3_value.count,
 *                             buf, sizeof(buf), &dlen) == 0)
 *           {
 *               if (!got_header)
 *               {
 *                   /* 第一段是 HTTP 响应头，跳过 *\/
 *                   /* 但要从里面读 Content-Length 确认大小 *\/
 *                   got_header = 1;
 *               }
 *               else if (dlen > 0)
 *               {
 *                   /* 这是固件数据，写 Flash *\/
 *                   /* 注意：一段可能不是 256 的整数倍，要拼页 *\/
 *                   W25Q128_WritePage(page++, buf);
 *                   total += dlen;
 *
 *                   /* 打印进度 *\/
 *                   if ((page % 16) == 0) Usart1_SendString(".");
 *
 *                   if (total >= fw_len) break;   /* 收完了 *\/
 *               }
 *           }
 *
 *           usart3_value.count = 0;
 *       }
 *
 *       CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
 *       return 0;
 *   }
 *
 *=====================================================================
 * ★ 五个坑 ★
 *
 *   坑 1：一段数据可能不是 256 字节的整数倍
 *        W25Q128 页写必须 256 字节对齐
 *        需要自己维护一个 256 字节的拼包缓冲
 *
 *   坑 2：响应头和响应体要分开
 *        第一段 $HTTPRECV:DATA,230 是 HTTP 头
 *        别把 "HTTP/1.1 200 OK" 写进 Flash
 *
 *   坑 3：超时要够长
 *        下载 100KB 可能要 30 秒以上
 *        循环次数按最坏情况算
 *
 *   坑 4：中途出错要能重试
 *        网络抖动会导致某段丢失
 *        可以记录已下载位置，重新请求（但本模块不支持 Range 请求，
 *        所以只能整个重下）
 *
 *   坑 5：Flash 要先擦再写
 *        W25Q128 擦除最小单位是 4KB 扇区或 64KB 块
 *        必须先擦后写，否则数据是错的
 *
 *====================================================================*/
u8 CAT1_OTA(char *url, u32 fw_len)
{
    u8 buf[256];
    u16 dlen;      /// 本次提取了多少字节
    u32 total = 0; /// 累计已下载字节数
    u32 addr = 0;  /// W25Q128地址
    u16 seg;
    u8 got_header = 0; /// 标记“是否已经跳过 HTTP 响应头”。
    u16 block;
    u16 i;

    /* ① 算要擦几块（★用 65536，u16）★ */
    block = (u16)((fw_len + 65535) / 65536);
    if (block == 0)
        block = 1;

    /* ② 激活网络 */
    Usart1_SendString("[OTA] 网络激活...\r\n");
    if (CAT1_ActiveGPRS())
    {
        Usart1_SendString("[OTA] 网络激活失败\r\n");
        return 1;
    }

    /* ③ HTTP 初始化 */
    Usart1_SendString("[OTA] 开始下载...\r\n");
    if (CAT1_HttpInit(url))
    {
        Usart1_SendString("[OTA] HTTP 初始化失败\r\n");
        return 2;
    }

    /* ④ 擦 Flash */
    Usart1_SendString("[OTA] 擦除 Flash");
    for (i = 0; i < block; i++)
    {
        W25Q128_EraseBlock((u8)i);
        Usart1_SendString(".");
    }
    Usart1_SendString("\r\n");

    /* ⑤ 接收循环 */
    Usart1_SendString("[OTA] 接收数据...\r\n");
    for (seg = 0; seg < 60000; seg++)
    {
        delay_ms(20);

        if (usart3_value.flag == 0)
            continue;
        usart3_value.flag = 0;

        if (CAT1_HttpRead(buf, sizeof(buf), &dlen) != 0)
        {
            usart3_value.count = 0;
            continue;
        }

        if (dlen == 0)
        {
            usart3_value.count = 0;
            continue;
        }

        /* ★跳过响应头（可能多段）★ */
        if (got_header == 0 && strncmp((char *)buf, "HTTP/", 5) == 0)
        {
            got_header = 1;
            Usart1_SendString("[OTA] 响应头 ");
            {
                char num[8];
                sprintf(num, "%u", dlen);
                Usart1_SendString(num);
            }
            Usart1_SendString(" 字节\r\n");

            usart3_value.count = 0;
            continue;
        }

        /* ★写 Flash（函数调用！）★ */
        if (W25Q128_Write(addr, buf, dlen) != 0)
        {
            Usart1_SendString("\r\n[OTA] Flash 写入失败\r\n");
            CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
            return 3;
        }

        addr += dlen;
        total += dlen;

        /* 进度点 */
        if ((total % 4096) < dlen)
        {
            Usart1_SendString(".");
        }

        usart3_value.count = 0;

        /* 收完了吗 */
        if (total >= fw_len)
            break;
    }

    /* ⑥ 检查是否收完 */
    if (total < fw_len)
    {
        Usart1_SendString("\r\n[OTA] 超时，只收到 ");
        {
            char num[16];
            sprintf(num, "%lu", (unsigned long)total);
            Usart1_SendString(num);
        }
        Usart1_SendString(" 字节\r\n");
        CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
        return 4;
    }

    /* ⑦ 关闭 HTTP */
    CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);

    Usart1_SendString("\r\n[OTA] 下载完成，共 ");
    {
        char num[16];
        sprintf(num, "%lu", (unsigned long)total);
        Usart1_SendString(num);
    }
    Usart1_SendString(" 字节\r\n");

    return 0;
}

/*=====================================================================
 * 函数：CAT1_ParseOTA
 * 负责：C
 *
 * 功能：解析巴法云下发的 OTA 命令
 * 参数：json - 收到的消息内容
 * 返回：0 = 解析成功
 *       1 = 格式不对（找不到逗号分隔）
 *       2 = 数据不合法（大小或 MD5 长度不对）
 *
 *=====================================================================
 * ★ 巴法云报文格式（★和阿里云完全不同★）
 *
 *   阿里云：平台推固定 JSON，格式不能改
 *     {"data":{"size":31236,"sign":"xxx","version":"...",
 *      "signMethod":"Md5","url":"https://xxx/fw.bin","md5":"abc"},
 *      "id":123,"message":"success"}
 *
 *   巴法云：你在电脑/手机上往 Topic 发什么就是什么，格式自己定
 *     推荐用纯文本逗号分隔（最简单，好解析）：
 *       http://xxx.com/fw.bin,31236,abc123def456...
 *              ↑ URL          ↑ 大小  ↑ MD5（可选）
 *
 *   为什么用逗号分隔：
 *     1. 解析只要 strchr + atoi，十几行搞定
 *     2. 不用写 JSON 解析器
 *     3. URL 里正常不会出现逗号，不会有歧义
 *
 *=====================================================================
 * ★ 实现要点 ★
 *
 *   u8 CAT1_ParseOTA(char *msg)
 *   {
 *       char *p;
 *       char *url;
 *       char *size_str;
 *       char *md5_str;
 *
 *       /* 1. 找第一个逗号，前面是 URL *\/
 *       p = strchr(msg, ',');
 *       if (p == NULL) return 1;          /* 没有逗号，格式不对 *\/
 *       *p = '\0';                        /* 把逗号改成字符串结束符 *\/
 *       url = msg;
 *       size_str = p + 1;                 /* 逗号后面是大小 *\/
 *
 *       /* 2. 找第二个逗号，前面是大小，后面是 MD5 *\/
 *       p = strchr(size_str, ',');
 *       if (p != NULL)
 *       {
 *           *p = '\0';
 *           md5_str = p + 1;
 *       }
 *       else
 *       {
 *           md5_str = NULL;               /* 没有 MD5，跳过校验 *\/
 *       }
 *
 *       /* 3. 保存 URL 和大小 *\/
 *       if (strlen(url) > OTA_URL_LEN) return 2;
 *       strcpy((char *)URL_ABuff, url);
 *       BemfaInfoCB.OTA_firelen = atoi(size_str);
 *
 *       /* 4. 校验大小合理性 *\/
 *       if (BemfaInfoCB.OTA_firelen == 0 ||
 *           BemfaInfoCB.OTA_firelen > FLASH_A_SIZE) return 2;
 *
 *       /* 5. 打印调试 *\/
 *       Usart1_SendString("[OTA] URL: ");
 *       Usart1_SendString((char *)URL_ABuff);
 *       Usart1_SendString("\r\n");
 *
 *       return 0;
 *   }
 *
 *=====================================================================
 * ★ 三个注意点 ★
 *
 *   注意 1：atoi 返回 int，固件大小要转 u32
 *           atoi 上限是 2^31-1（约 2GB），固件一般 < 4MB，够用
 *           但赋值时要强转：BemfaInfoCB.OTA_firelen = (u32)atoi(size_str);
 *
 *   注意 2：strchr 会修改原字符串（把逗号改成 '\0'）
 *           如果原消息后面还要用，先 strcpy 一份副本再解析
 *
 *   注意 3：URL 长度必须检查
 *           URL_ABuff 只有 513 字节，超了会溢出
 *           用 strlen 检查后再 strcpy
 *
 *=====================================================================
 * ★ 云端怎么发这条消息 ★
 *
 *   方式 1：巴法云网页控制台
 *     登录 cloud.bemfa.com → 找到你的 Topic → 发送消息
 *     内容填：http://xxx.com/fw.bin,31236,abc123...
 *
 *   方式 2：MQTT 客户端软件（MQTTX）
 *     连接 bemfa.com:9501，Username/Password 都填 UID
 *     往 Topic "ota{uid}" 发布消息
 *
 *   方式 3：手机 APP（巴法云官方有）
 *====================================================================*/
u8 CAT1_ParseOTA(char *json)
{
    /* TODO【C】：实现 */
    return 1;
}
