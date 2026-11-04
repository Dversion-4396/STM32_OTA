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
#include "lcd_ui.h" /* ★2026-11-03 加：下载进度要刷屏幕 */

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
                // 不清除会导致下个包进行累加导致出错
                usart3_value.flag = 0;
                usart3_value.count = 0;
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

    /*---------- 打印 IP ----------
     * ★2026-11-03 改：原来这里只打一句 "已拿到 IP: " 后面是空的，
     *   因为 CAT1_SendAT 匹配成功时会把 usart3_value.count 清 0，
     *   上层拿不到刚收到的那段文本。
     *
     *   这里单独再读一次，自己留一份缓冲区打印出来 ——
     *   IP 是排查 4G 问题最有用的一个数，必须能看见。
     *------------------------------------------------*/
    {
        // 拿到的 ip
        char ipbuf[64];
        u16 n = 0;
        u16 i2;

        usart3_value.flag = 0;
        usart3_value.count = 0;
        Usart3_SendString("AT+CGPADDR\r\n");

        /* 最多等 3 秒收全 */
        for (i2 = 0; i2 < 300; i2++)
        {
            delay_ms(10);
            if (usart3_value.flag == 1)
            {
                usart3_value.flag = 0;
                /* 再等 200ms 让剩下的字节到齐 */
                delay_ms(200);
                break;
            }
        }

        /* 从回复里抠出 +CGPADDR: 后面那段 */
        if (usart3_value.count > 0 && usart3_value.count < USART3_BUFFER_SIZE)
        {
            usart3_value.data[usart3_value.count] = 0;
            {
                char *paddr = strstr((char *)usart3_value.data, "+CGPADDR:");
                if (paddr != NULL)
                {
                    paddr += 9; /* 跳过 "+CGPADDR:" */
                    while (*paddr == ' ')
                    {
                        paddr++;
                    }
                    while (*paddr != 0 && *paddr != '\r' && *paddr != '\n' &&
                           n < (u16)(sizeof(ipbuf) - 1))
                    {
                        ipbuf[n++] = *paddr++;
                    }
                }
            }
        }
        ipbuf[n] = '\0';

        Usart1_SendString("[GPRS] 已拿到 IP: ");
        if (n > 0)
        {
            Usart1_SendString(ipbuf);
        }
        else
        {
            Usart1_SendString("(解析失败)");
        }
        Usart1_SendString("\r\n");

        usart3_value.count = 0;
    }

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
    /* ★2026-11-04 改：ACTION 不在这里发★
     *   CAT1_SendAT 匹配到 OK 时会把 usart3_value.count 清 0，
     *   而模块往往在同一帧里就把 HTTP 响应头推过来了 —— 一清就没了，
     *   结果只能收到正文，永远"第一段不是 HTTP 头"。
     *   改由 CAT1_OTA 直接发原始命令、不清缓冲。 */
    Usart1_SendString("[HTTP] 已就绪\r\n");
    return 0;
}

u8 CAT1_HttpRead(u8 *outbuf, u16 maxlen, u16 *outlen)
{
    char *p;

    // 数据长度
    u16 dlen;

    // 可用数据长度
    u16 avail;
    /// 防止传入空指针
    if (outbuf == NULL || outlen == NULL)
    {
        Usart1_SendString("传入缓存区为空\r\n");
        return 1;
    }

    /// 清除输出长度 防止外侧收到脏数据
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

    // 将 p 指向支付串的末尾
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
 * OTA 下载（流式接收版，2026-11-04 重写）
 *
 * ★为什么不用 CAT1_HttpRead 了★
 *   实测（2026-11-04 日志）：模块一次会连推 2000+ 字节，中间没有 IDLE 间隔。
 *   CAT1_HttpRead 的规则是"整段必须在缓冲里、且 count < 缓冲大小"，
 *   缓冲一满它就返回错误，调用方清空缓冲 ——
 *   连同 $HTTPRECV:DATA, 标记一起丢掉，之后全是二进制正文，
 *   永远"未找到标识符"，最后"只收到 0 字节"。
 *
 *   现在改成边收边解析：找到标记就解析长度，凑够就取走，不够就等着，
 *   已经收到的数据永远不主动丢弃。
 *====================================================================*/
#define OTA_TAIL_LEN        28UL
#define OTA_ERASE_BLOCKS    ((u8)((512UL * 1024UL) / 65536UL))   /* = 8 块 */
#define OTA_CACHE_SIZE      512U
#define OTA_HDR_MAX         1024U
#define URC_PREFIX          "HTTPRECV:DATA,"   /* 不含开头的 $ 或 +，两种都认 */
#define URC_PREFIX_LEN      14U

#define OTA_SEG_GOT         0
#define OTA_SEG_WAIT        1
#define OTA_SEG_ERR         2

static char s_ota_hdr[OTA_HDR_MAX + 1];
static u16  s_ota_hdr_len;
static u8   s_ota_cache[OTA_CACHE_SIZE];
static u16  s_ota_cache_len;
static u32  s_ota_addr;
static u16  s_ota_rxoff;

/* 把已消费的部分从串口缓冲里移走（关中断，防 ISR 同时 count++） */
static void ota_compact(void)
{
    u16 left;

    NVIC_DisableIRQ(USART3_IRQn);

    if (s_ota_rxoff > usart3_value.count)
    {
        s_ota_rxoff = usart3_value.count;
    }
    left = (u16)(usart3_value.count - s_ota_rxoff);
    if (s_ota_rxoff > 0 && left > 0)
    {
        memmove(usart3_value.data, &usart3_value.data[s_ota_rxoff], left);
    }
    usart3_value.count = left;
    s_ota_rxoff = 0;

    NVIC_EnableIRQ(USART3_IRQn);
}

/*---------------------------------------------------------------------
 * 从串口缓冲里取出一段：$HTTPRECV:DATA,<len>\r\n<payload>
 *   返回 OTA_SEG_GOT  ：取到，*outlen 是正文长度
 *        OTA_SEG_WAIT ：还没凑齐，等更多数据（不丢已收到的）
 *        OTA_SEG_ERR  ：长度异常或超过 out 容量
 *-------------------------------------------------------------------*/
static u8 ota_next_seg(u8 *out, u16 maxlen, u16 *outlen)
{
    u16 i;
    u16 len = usart3_value.count;
    u16 j;
    u32 v;
    u8  nl;
    u16 dlen;

    *outlen = 0;

    /* ① 找标记 */
    for (i = s_ota_rxoff; (u16)(i + URC_PREFIX_LEN) <= len; i++)
    {
        if (memcmp(&usart3_value.data[i], URC_PREFIX, URC_PREFIX_LEN) == 0)
        {
            break;
        }
    }
    if ((u16)(i + URC_PREFIX_LEN) > len)
    {
        /* 没找到：末尾可能是个被截断的标记，留着；前面的垃圾丢掉 */
        s_ota_rxoff = (len > URC_PREFIX_LEN) ? (u16)(len - URC_PREFIX_LEN + 1) : 0;
        ota_compact();
        return OTA_SEG_WAIT;
    }

    /* ② 解析段长，直到 '\n' */
    j  = (u16)(i + URC_PREFIX_LEN);
    v  = 0;
    nl = 0;
    while (j < len)
    {
        u8 c = usart3_value.data[j];

        if (c >= '0' && c <= '9')
        {
            v = v * 10UL + (u32)(c - '0');
            j++;
            continue;
        }
        if (c == '\n')
        {
            j++;
            nl = 1;
            break;
        }
        if (c == '\r' || c == ',')
        {
            j++;
            continue;
        }
        break;
    }
    if (nl == 0)
    {
        return OTA_SEG_WAIT;        /* 长度行还没到齐 */
    }
    if (v > 0xFFFFUL)
    {
        return OTA_SEG_ERR;
    }
    dlen = (u16)v;

    /* ③ 正文没到齐就等着 */
    if ((u16)(len - j) < dlen)
    {
        return OTA_SEG_WAIT;
    }
    if (dlen > maxlen)
    {
        return OTA_SEG_ERR;
    }

    /* ④ 取走，并把已消费的部分移出缓冲 */
    memcpy(out, &usart3_value.data[j], dlen);
    s_ota_rxoff = (u16)(j + dlen);
    *outlen     = dlen;
    ota_compact();
    return OTA_SEG_GOT;
}

/* 正文进缓存，攒满 512 整块写（512 是 2 个 Flash 页，天然页对齐） */
static u8 ota_body_push(const u8 *p, u16 n, u32 fw_len, u32 *got)
{
    if (*got + n > fw_len)
    {
        return 1;
    }
    while (n > 0)
    {
        u16 space = (u16)(OTA_CACHE_SIZE - s_ota_cache_len);
        u16 take  = (n < space) ? n : space;

        memcpy(&s_ota_cache[s_ota_cache_len], p, take);
        s_ota_cache_len = (u16)(s_ota_cache_len + take);
        p += take;
        n = (u16)(n - take);
        *got += take;

        if (s_ota_cache_len >= OTA_CACHE_SIZE)
        {
            if (W25Q128_Write(s_ota_addr, s_ota_cache, OTA_CACHE_SIZE) != 0)
            {
                return 2;
            }
            s_ota_addr += OTA_CACHE_SIZE;
            s_ota_cache_len = 0;
        }
    }
    return 0;
}

/* 把前 64 字节按十六进制打出来，便于看清模块到底发了什么 */
static void ota_dump(const u8 *p, u16 n)
{
    u16 i;
    char hx[8];

    if (n > 64)
    {
        n = 64;
    }
    for (i = 0; i < n; i++)
    {
        sprintf(hx, "%02X ", p[i]);
        Usart1_SendString(hx);
    }
    Usart1_SendString("\r\n");
}

/*=====================================================================
 * 函数：CAT1_OTA
 * 参数：url    - 下载地址（http:// 开头）
 *       fw_len - 固件总长（含 28 字节尾缀，来自云端通知）
 * 返回：0 成功 / 1 网络 / 2 HTTP / 3 Flash / 4 超时 / 5 入参
 *       6 响应头非法 / 7 大小不一致 / 8 段异常
 *====================================================================*/
u8 CAT1_OTA(char *url, u32 fw_len)
{
    static u8 buf[2100];            /* ★模块一段 2048 字节，缓冲必须比它大★ */
    u8  r;
    u16 dlen;
    u32 total = 0;
    u8  got_header = 0;
    u16 idle = 0;
    u16 stall = 0;
    u8  last_pct = 0xFF;
    u8  i;
    char *p;
    u32 clen;

    /* ① 入参 */
    if (url == NULL || url[0] == '\0')
    {
        Usart1_SendString("[OTA] URL 为空\r\n");
        return 5;
    }
    if (fw_len < OTA_TAIL_LEN || fw_len > FLASH_A_SIZE)
    {
        Usart1_SendString("[OTA] 固件大小非法\r\n");
        return 5;
    }

    /* ② 状态复位 */
    s_ota_hdr[0]    = '\0';
    s_ota_cache_len = 0;
    s_ota_addr      = 0;
    s_ota_rxoff     = 0;
    usart3_value.flag  = 0;
    usart3_value.count = 0;

    /* ③ 激活网络 */
    Usart1_SendString("[OTA] 网络激活...\r\n");
    if (CAT1_ActiveGPRS())
    {
        Usart1_SendString("[OTA] 网络激活失败\r\n");
        return 1;
    }

    /* ④ 固定擦除 512KB（必须在发 GET 之前，否则擦除期间数据会把缓冲写爆） */
    Usart1_SendString("[OTA] 擦除 W25Q128 512KB");
    LCD_UI_SetOtaProgress(0, fw_len);
    for (i = 0; i < OTA_ERASE_BLOCKS; i++)
    {
        W25Q128_EraseBlock((u8)i);
        Usart1_SendString(".");
    }
    Usart1_SendString(" 完成\r\n");

    /* ⑤ 发 GET */
    Usart1_SendString("[OTA] 发送 GET，开始接收...\r\n");
    if (CAT1_HttpInit(url))
    {
        Usart1_SendString("[OTA] HTTP 初始化失败\r\n");
        CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
        return 2;
    }

    /* ★2026-11-04 加：直接发 GET，不等 OK、不清缓冲★
       模块收到 ACTION 后会在同一帧里连着把响应头推过来，
       用 CAT1_SendAT 等 OK 的话，响应头会被一起清掉。 */
    usart3_value.flag  = 0;
    usart3_value.count = 0;
    Usart3_SendString("AT$HTTPACTION=0");
    Usart3_SendString("\r\n");

    /* ⑥ 边收边解析 */
    for (;;)
    {
        if (usart3_value.count == 0)
        {
            if (idle++ > 6000)              /* 6000 × 2ms = 12 秒没数据 */
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
            delay_ms(2);
            continue;
        }
        idle = 0;
        usart3_value.flag = 0;

        r = ota_next_seg(buf, (u16)sizeof(buf), &dlen);
        if (r == OTA_SEG_WAIT)
        {
            /* ★2026-11-04 加：连续 2 秒凑不出一个有效段，把缓冲打出来★
               否则"有数据但格式不对"会一直转下去，既不下崽也不超时。 */
            if (++stall > 2000)
            {
                Usart1_SendString("\r\n[OTA] 收不到 HTTPRECV:DATA, 标记，缓冲前 64 字节：\r\n");
                ota_dump(usart3_value.data, usart3_value.count);
                CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
                return 9;
            }
            delay_ms(1);                    /* 还没凑齐，等下一批 */
            continue;
        }
        stall = 0;
        if (r == OTA_SEG_ERR)
        {
            Usart1_SendString("\r\n[OTA] 数据段异常（长度超限）\r\n");
            CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
            return 8;
        }
        if (dlen == 0)
        {
            continue;
        }

        /* ---- 响应头：累积到 "\r\n\r\n" 再解析 ----
         * 不假设"第一段就是完整响应头"：实测模块可能在头前面多塞 \r\n
         * 甚至 OK，头也可能被切成两段。这里一路累积，找到结束标志才解析。 */
        if (got_header == 0)
        {
            if ((u16)(s_ota_hdr_len + dlen) > OTA_HDR_MAX)
            {
                Usart1_SendString("[OTA] 响应头超长，已收内容：\r\n");
                ota_dump((const u8 *)s_ota_hdr, s_ota_hdr_len);
                CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
                return 6;
            }
            memcpy(&s_ota_hdr[s_ota_hdr_len], buf, dlen);
            s_ota_hdr_len = (u16)(s_ota_hdr_len + dlen);
            s_ota_hdr[s_ota_hdr_len] = '\0';

            if (strstr(s_ota_hdr, "\r\n\r\n") == NULL)
            {
                continue;                   /* 头还没收完 */
            }

            /* 从 "HTTP/" 开始解析，前面有 
 或 OK 也不影响 */
            p = strstr(s_ota_hdr, "HTTP/");
            if (p == NULL)
            {
                Usart1_SendString("[OTA] 找不到 HTTP/，已收内容：\r\n");
                ota_dump((const u8 *)s_ota_hdr, s_ota_hdr_len);
                CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
                return 6;
            }

            {
                char *sp = strchr(p, ' ');
                if (sp == NULL || atoi(sp + 1) != 200)
                {
                    Usart1_SendString("[OTA] 服务器返回非 200，响应头：\r\n");
                    ota_dump((const u8 *)p, (u16)strlen(p));
                    CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
                    return 6;
                }
            }

            {
                char *cl = strstr(p, "Content-Length:");
                if (cl == NULL)
                {
                    cl = strstr(p, "content-length:");
                }
                if (cl == NULL)
                {
                    Usart1_SendString("[OTA] 响应没有 Content-Length，响应头：\r\n");
                    ota_dump((const u8 *)p, (u16)strlen(p));
                    CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
                    return 6;
                }
                clen = (u32)strtoul(cl + 15, NULL, 10);
            }

            if (clen != fw_len)
            {
                Usart1_SendString("[OTA] ★大小不一致：声明=");
                {
                    char num[16];
                    sprintf(num, "%lu", (unsigned long)fw_len);
                    Usart1_SendString(num);
                }
                Usart1_SendString(" 实际=");
                {
                    char num[16];
                    sprintf(num, "%lu", (unsigned long)clen);
                    Usart1_SendString(num);
                }
                Usart1_SendString("，中止\r\n");
                CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
                return 7;
            }

            got_header = 1;

            /* 头段后面如果已经跟了正文，接着写 */
            {
                char *e = strstr(s_ota_hdr, "\r\n\r\n");
                if (e != NULL)
                {
                    u16 head_end = (u16)((e + 4) - s_ota_hdr);

                    if (head_end < s_ota_hdr_len)
                    {
                        u16 body = (u16)(s_ota_hdr_len - head_end);

                        if (ota_body_push((const u8 *)&s_ota_hdr[head_end], body, fw_len, &total) != 0)
                        {
                            Usart1_SendString("\r\n[OTA] Flash 写入失败\r\n");
                            CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
                            return 3;
                        }
                    }
                }
            }

            Usart1_SendString("[OTA] 响应头 OK，大小一致\r\n");
            continue;
        }

        /* ---- 正文 ---- */
        r = ota_body_push(buf, dlen, fw_len, &total);
        if (r != 0)
        {
            Usart1_SendString("\r\n[OTA] Flash 写入失败或数据超长\r\n");
            CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
            return 3;
        }

        /* 进度刷屏（5% 节流） */
        {
            u8 now = (u8)((total >= fw_len) ? 100 : ((total * 100UL) / fw_len));

            if (last_pct == 0xFF || now >= 100 || (u8)(now - last_pct) >= 5)
            {
                last_pct = now;
                LCD_UI_SetOtaProgress(total, fw_len);
            }
        }

        if (total >= fw_len)
        {
            break;
        }
    }

    /* ⑦ 把缓存尾巴写掉 */
    if (s_ota_cache_len > 0)
    {
        if (W25Q128_Write(s_ota_addr, s_ota_cache, s_ota_cache_len) != 0)
        {
            Usart1_SendString("\r\n[OTA] Flash 写入失败\r\n");
            CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
            return 3;
        }
        s_ota_addr += s_ota_cache_len;
        s_ota_cache_len = 0;
    }

    /* ⑧ 长度必须严格相等 */
    if (total != fw_len)
    {
        Usart1_SendString("\r\n[OTA] 长度不符：收 ");
        {
            char num[16];
            sprintf(num, "%lu", (unsigned long)total);
            Usart1_SendString(num);
        }
        Usart1_SendString(" 字节\r\n");
        CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
        return 4;
    }

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
 * 函数：cat1_version_to_num
 * 负责：把 "1.0.1" 这种版本名字符串换算成数值
 *
 * 参数：ver_str - 版本名字符串，必须是"数字.数字.数字"三段
 * 返回：大版本*10000 + 小版本*100 + 修订号；返回 0 表示格式非法
 *
 * 例：1.0.0 → 10000    1.0.1 → 10001    1.2.0 → 10200
 *
 * ★ 为什么要把版本号变成数字 ★
 *   字符串比大小是字典序，会出错：
 *     "1.10.0" < "1.9.0"  （因为 '1' < '9'）
 *   但 1.10.0 明显比 1.9.0 新。变成数字 11000 vs 10900 就对了。
 *
 * ★ 为什么不用 sscanf ★
 *   armcc 的 sscanf 会拖进来一大坨库代码（约 2~3KB），
 *   而这个 MCU 的 APP 区虽然有 480KB，但启动区 BootLoader 只有 32KB，
 *   库代码能省则省。手写解析只要几十字节。
 *=====================================================================*/
u32 cat1_version_to_num(const char *ver_str)
{
    u32 parts[3]; /* 大版本 / 小版本 / 修订号 */
    u32 cur = 0;
    u8 idx = 0;
    u8 has_digit = 0;
    const char *p = ver_str;

    if (ver_str == NULL)
    {
        return 0;
    }

    parts[0] = 0;
    parts[1] = 0;
    parts[2] = 0;

    while (*p != '\0')
    {
        if (*p >= '0' && *p <= '9')
        {
            cur = cur * 10 + (u32)(*p - '0');
            has_digit = 1;

            /* 防溢出：版本号每段不该超过 9999 */
            if (cur > 9999)
            {
                return 0;
            }
        }
        else if (*p == '.')
        {
            if (has_digit == 0 || idx >= 2)
            {
                return 0; /* ".." 或 ".1.2.3" 这种非法 */
            }
            parts[idx] = cur;
            idx++;
            cur = 0;
            has_digit = 0;
        }
        else
        {
            return 0; /* 出现非数字非点的字符 ⇒ 非法 */
        }
        p++;
    }

    /* 最后一段也要有数字 */
    if (has_digit == 0 || idx != 2)
    {
        return 0; /* 必须正好三段 */
    }
    parts[2] = cur;

    /* 编码：大版本*10000 + 小版本*100 + 修订号 */
    return parts[0] * 10000UL + parts[1] * 100UL + parts[2];
}

/*=====================================================================
 * 函数：cat1_trim_tail
 * 功能：原地去掉字符串尾部的 空格 / \t / \r / \n
 *
 * ★为什么需要★
 *   巴法云控制台发出来的消息末尾经常带 "\r\n"，
 *   第三个字段会被解析成 "1.0.2\r"，cat1_version_to_num() 判定非法；
 *   URL 尾部多一个空格同样会让 AT$HTTPPARA 带上脏字符，取不到文件。
 *   与其要求人工每次都对，不如在这里统一清一遍。
 *====================================================================*/
static void cat1_trim_tail(char *s)
{
    u16 n;

    if (s == NULL)
    {
        return;
    }
    n = (u16)strlen(s);
    while (n > 0)
    {
        char c = s[n - 1];

        if (c == '\r' || c == '\n' || c == ' ' || c == '\t')
        {
            s[--n] = '\0';
        }
        else
        {
            break;
        }
    }
}


/*=====================================================================
 * 函数：CAT1_ParseOTA
 * 负责：解析云端下发的 OTA 通知
 *
 * 参数：json - 通知文本
 * 返回：0 = 解析成功  1 = 格式错  2 = URL 非法  3 = 大小非法  4 = 版本号非法
 *
 * ★ 报文格式（2026-11-03 改）★
 *
 *   旧格式：http://xxx/fw.bin,31236
 *   旧格式：http://xxx/fw.bin,31236,md5
 *   新格式：http://xxx/fw.bin,31236,1.0.1
 *   新格式：http://xxx/fw.bin,31236,1.0.1,md5
 *
 *   第 3 个字段从"MD5"改成了"版本号"。
 *   为什么必须加版本号：
 *     原来 BootLoader 只能比"当前版本号 vs 新版本号"，
 *     但新版本号根本没地方来 —— 通知里只有 URL 和大小。
 *     没有版本号就只能"有固件就刷"，同一个固件会被反复刷写，
 *     而且降级也拦不住。加上版本号之后才能真的做"只有更新才升级"。
 *
 *   ★ 怎么区分"第 3 个字段是 MD5 还是版本号" ★
 *     版本号一定是"数字.数字.数字"这种带点的形式（1.0.1）；
 *     MD5 是 32 位纯十六进制、不带点。
 *     所以判据就是"含不含 '.'"，含点当版本号，不含点当 MD5（兼容老格式）。
 *=====================================================================*/
u8 CAT1_ParseOTA(char *json)
{
    // 存放原始 URL、大小
    char *onedouhao;

    // 存放切割后的 URL、大小
    char *url;

    // 每次找到 ',' 后，size_str 指向下一个字符
    char *size_str;

    // 第 3 个字段（版本号 或 老格式的 MD5）
    char *third_str;

    // 第 4 个字段（MD5）
    char *md5_str;

    // 真实的 URL 长度
    u32 url_len;

    if (json == NULL)
    {
        return 1;
    }

    /* 报文格式：http://xxx/fw.bin,31236,版本号[,md5] */

    /* ① 切出 URL */
    onedouhao = strchr(json, ',');
    if (onedouhao == NULL)
    {
        return 1;
    }
    *onedouhao = '\0';
    url = json;
    size_str = onedouhao + 1;

    /* ② 切出大小，后面两个字段可有可无 */
    third_str = strchr(size_str, ',');
    if (third_str != NULL)
    {
        *third_str = '\0';
        third_str++;

        md5_str = strchr(third_str, ',');
        if (md5_str != NULL)
        {
            *md5_str = '\0';
            md5_str++; /* MD5 目前不参与校验，保留给后面用 */
        }
        (void)md5_str;
    }

    /*=================================================================
     * ★2026-11-04 加：去掉各字段尾部的空白 / 回车 / 换行★
     *
     * 为什么必须加：
     *   巴法云控制台发消息时，粘贴或回车很容易在末尾留下 "\r\n" 或空格。
     *   结果第三个字段变成 "1.0.2\r"，cat1_version_to_num() 一看 '\r'
     *   不是数字也不是点，直接 return 0，上层报"版本号格式非法"。
     *   URL 尾部多一个空格也会让 AT$HTTPPARA 带上垃圾字符。
     *=================================================================*/
    cat1_trim_tail(url);
    cat1_trim_tail(third_str);

    /* ③ ★URL 长度检查 + 拷进 URL_ABuff★
     *   原来这一步完全没做，URL_ABuff 永远是全 0 缓冲区，
     *   CAT1_OTA 拿着空字符串去发 AT$HTTPPARA，必然失败 */
    url_len = (u32)strlen(url);
    if (url_len == 0 || url_len > OTA_URL_LEN)
    {
        Usart1_SendString("[OTA] URL 长度非法\r\n");
        return 2;
    }
    strcpy((char *)URL_ABuff, url);

    /* ④ ★固件大小解析 + 范围检查★ */
    BemfaInfoCB.OTA_firelen = (u32)atoi(size_str);
    if (BemfaInfoCB.OTA_firelen == 0)
    {
        Usart1_SendString("[OTA] 固件大小为 0\r\n");
        return 3;
    }
    if (BemfaInfoCB.OTA_firelen > FLASH_A_SIZE)
    {
        Usart1_SendString("[OTA] 固件超过 480KB，拒绝\r\n");
        return 3;
    }

    /* ⑤ ★解析版本号，写进 EEPROM 的 App 信息区★
     *
     *   EEPROM_Data 是 App 信息区（0x70 之后），
     *   和 BemfaInfoCB（0x00~0x62）是两块独立区域，互不影响。
     *
     *   写进去之后，BootLoader 菜单 [3] 能看到"待升级版本号"，
     *   菜单 [4] 会拿它和当前版本号比大小，只有更新才搬。 */
    if (third_str != NULL && third_str[0] != '\0')
    {
        if (strchr(third_str, '.') != NULL)
        {
            /* 含点 ⇒ 是版本号，形如 "1.0.1" */
            u32 new_num;

            /* 先把字符串存下来（版本名，最多 7 字符 + '\0'） */
            memset(EEPROM_Data.new_ver, 0, sizeof(EEPROM_Data.new_ver));
            strncpy(EEPROM_Data.new_ver, third_str, EEPROM_VER_SIZE - 1);

            /* 再算数值版本号：大版本*10000 + 小版本*100 + 修订号 */
            new_num = cat1_version_to_num(third_str);
            if (new_num == 0)
            {
                Usart1_SendString("[OTA] 版本号格式非法\r\n");
                return 4;
            }
            EEPROM_Data.new_ver_num = new_num;

            Usart1_SendString("[OTA] 新版本名=");
            Usart1_SendString(EEPROM_Data.new_ver);
            Usart1_SendString(" 新版本号=");
            {
                char numbuf[16];
                sprintf(numbuf, "%lu", (unsigned long)new_num);
                Usart1_SendString(numbuf);
            }
            Usart1_SendString("\r\n");
        }
        else
        {
            /* 不含点 ⇒ 是老格式的 MD5，当作"没给版本号"处理。
             * 这时候 new_ver_num 保持 0，BootLoader 菜单 [4] 会拒绝升级
             * （因为它判 new_ver_num <= ver_num）。这是故意的：
             * 没有版本号就不该盲目刷，避免重复刷同一个固件。 */
            Usart1_SendString("[OTA] 通知里没带版本号，只下载不升级\r\n");
            EEPROM_Data.new_ver_num = 0;
            EEPROM_Data.new_ver[0] = '\0';
        }
    }
    else
    {
        /* 完全没给第 3 个字段 ⇒ 同老格式，只下载不升级 */
        Usart1_SendString("[OTA] 通知里没带版本号，只下载不升级\r\n");
        EEPROM_Data.new_ver_num = 0;
        EEPROM_Data.new_ver[0] = '\0';
    }

    /* ★2026-11-04 改：这里不再写 new_len★
       原来收到通知就把长度记进去，可此刻一个字节都还没下；
       BootLoader 菜单 [4] 拿 new_len > 0 判断"W25Q128 里有固件"，
       于是下载失败 / 被版本过滤时它会去搬一段残包。
       现在先清 0 作废旧记录，等 CAT1_OTA() 真正收全、长度严格相等后，
       由 main.c 的 ActiveEvent() 写回真实长度。 */
    EEPROM_Data.new_len = 0;

    if (EEPROM_SaveAppInfo(&EEPROM_Data) != 0)
    {
        Usart1_SendString("[OTA] ★版本信息写 EEPROM 失败\r\n");
    }

    Usart1_SendString("[OTA] URL=");
    Usart1_SendString((char *)URL_ABuff);
    Usart1_SendString("\r\n");

    return 0;
}

u8 CAT1_SendMQTTPacket(u8 *buf, u16 len)
{
    char cmd[64];
    u8 ret;

    /*---------- ① 告诉模块要发多少字节 ----------*/
    sprintf(cmd, "AT+CIPSEND=0,%d", len);

    /* 等模块回 ">" 提示符
     * 注意：提示符是单个字符 >，不是 "OK"
     * 超时给 5 秒，模块要准备 TCP 发送缓冲 */
    ret = CAT1_SendAT(cmd, ">", 5000);
    if (ret != 0)
    {
        Usart1_SendString("[CAT1] CIPSEND 未收到提示符\r\n");
        return 1;
    }

    /*---------- ② 发送原始数据 ----------
     * ★ 这里不能用 Usart3_SendString（它不能处理二进制 0x00）★
     *   必须用 SendBuffer 按长度发
     *------------------------------------------------*/
    Usart3_SendBuffer(buf, len);

    /*---------- ③ 等发送完成 ----------
     * 手册没给明确响应，常见是 +CIPSEND: <len> 或 SEND OK
     * 这里等 5 秒，只要收到任何响应就算成功
     *------------------------------------------------*/
    usart3_value.flag = 0;
    usart3_value.count = 0;
    u16 i;
    // 等待响应或超时
    for (i = 0; i < 50; i++)
    {
        delay_ms(100);
        if (usart3_value.flag)
        {
            break;
        }
    }

    /* ★2026-11-04：平时保持安静，只在真出错时打一行★
       （调试期间打印过完整回包，定位到了根因：
         模块把 TCP 数据包在 "+CIPRXGET:SUCCESS,0,0,N,\r\n" 里。） */
    if (usart3_value.count == 0)
    {
        Usart1_SendString("[CAT1] ★CIPSEND 没有任何回包\r\n");
    }
    else
    {
        u16 k;
        u8  err = 0;

        for (k = 0; (u16)(k + 5) <= usart3_value.count; k++)
        {
            if (memcmp(&usart3_value.data[k], "ERROR", 5) == 0)
            {
                err = 1;
                break;
            }
        }
        if (err)
        {
            Usart1_SendString("[CAT1] ★CIPSEND 回包里有 ERROR\r\n");
        }
    }

    return 0; /* 即使没等到响应也返回成功，避免误判 */
}
