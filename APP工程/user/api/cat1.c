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
 *       -- 4. 发送 GET 请求 --
 *       if (CAT1_SendAT("AT$HTTPACTION=0", "OK", 5000))
 *           return 4;
 *
 *       return 0;
 *   }
 *====================================================================*/
/*=====================================================================
 * 函数：CAT1_HttpClose
 * 功能：关闭 HTTP 服务（安全版，不管成败都往下走）
 * 负责：A
 *
 * ★ 为什么必须关 ★
 *   AT$HTTPOPEN 是"全局"状态，不关的话：
 *     下次 AT$HTTPOPEN 会返回 ERROR 301（服务已开启）
 *     TCP 会话也被占着，AT+CIPOPEN 会 FAIL
 *   每次 OTA 结束后（无论成败）都必须关。
 *====================================================================*/
void CAT1_HttpClose(void)
{
    Usart1_SendString("[HTTP] 关闭服务\r\n");
    CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
    delay_ms(500);
}

u8 CAT1_HttpInit(char *url)
{
    char cmd[URL_LEN];

    /*---------- ① 配置 APN ----------*/
    if (CAT1_SendAT("AT+QICSGP=1,1,\"CMNET\",\"\",\"\",1", "OK", 10000))
    {
        Usart1_SendString("[HTTP] APN配置失败\r\n");
        return 1;
    }

    /*---------- ② 开启 HTTP 服务 ----------
     * ★ 如果返回失败，可能是上次没关（ERROR 301 服务已开启）
     *   先关一次，再重开
     *------------------------------------------------*/
    if (CAT1_SendAT("AT$HTTPOPEN", "OK", 10000))
    {
        Usart1_SendString("[HTTP] 首次开启失败，先关闭再重试\r\n");
        CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
        delay_ms(1000);

        if (CAT1_SendAT("AT$HTTPOPEN", "OK", 10000))
        {
            Usart1_SendString("[HTTP] ★服务开启失败\r\n");
            return 2;
        }
    }

    /*---------- ③ 配置 URL ----------*/
    sprintf(cmd, "AT$HTTPPARA=%s,80", url);
    if (CAT1_SendAT(cmd, "OK", 8000))
    {
        Usart1_SendString("[HTTP] URL 配置失败\r\n");
        CAT1_HttpClose();
        return 3;
    }

    /*---------- ④ 发送 GET 请求 ----------
     * ★ 先清空接收缓冲，避免 AT 响应残留在里面
     *------------------------------------------------*/
    __disable_irq();
    usart3_value.count = 0;
    usart3_value.flag = 0;
    __enable_irq();

    if (CAT1_SendAT("AT$HTTPACTION=0", "OK", 5000))
    {
        Usart1_SendString("[HTTP] GET 请求发送失败\r\n");
        CAT1_HttpClose();
        return 4;
    }

    Usart1_SendString("[HTTP] 连接成功\r\n");
    return 0;
}

/*=====================================================================
 * 诊断辅助：打印缓冲区的头尾，看清模块实际推了什么
 * 只在调试期用，正式版删掉
 *====================================================================*/
static void CAT1_HttpDbgDump(u8 *data, u16 len)
{
    u16 i;
    u16 show;
    char hex[8];

    Usart1_SendString("[DBG] count=");
    sprintf(hex, "%u", len);
    Usart1_SendString(hex);

    Usart1_SendString("  头24字节: ");
    show = (len < 24) ? len : 24;
    for (i = 0; i < show; i++)
    {
        sprintf(hex, "%02X ", data[i]);
        Usart1_SendString(hex);
    }

    Usart1_SendString("\r\n[DBG] 尾24字节: ");
    if (len > 24)
    {
        for (i = len - 24; i < len; i++)
        {
            sprintf(hex, "%02X ", data[i]);
            Usart1_SendString(hex);
        }
    }
    Usart1_SendString("\r\n");
}

u8 CAT1_HttpRead(u8 *outbuf, u16 maxlen, u16 *outlen)
{
    char *p;
    u16 dlen;
    u16 avail;
    char num[16];
    /// 防止传入空指针
    if (outbuf == NULL || outlen == NULL)
    {
        Usart1_SendString("传入缓存区为空\r\n");
        return 1;
    }
    /// 清楚输出长度 防止外侧收到脏数据
    *outlen = 0;

    /*---------- 诊断：每次进来先看缓冲区状态 ----------*/
    CAT1_HttpDbgDump(usart3_value.data, usart3_value.count);

    if (usart3_value.count >= USART3_BUFFER_SIZE)
    {
        Usart1_SendString("[DBG] 缓冲满(溢出)\r\n");
        return 1;
    }
    /// 给数据末尾加 \0
    usart3_value.data[usart3_value.count] = 0;
    /// 此时p在 $HTTPRECV:DATA,的第一位
    p = strstr((char *)usart3_value.data, "$HTTPRECV:DATA,");
    if (NULL == p)
    {
        Usart1_SendString("[DBG] 没找到 $HTTPRECV:DATA,\r\n");
        return 1;
    }
    // 将p 指向支付串的末尾
    p += strlen("$HTTPRECV:DATA,");
    dlen = (u16)atoi(p);

    /*---------- 诊断：模块声明的长度 ----------*/
    Usart1_SendString("[DBG] 模块声明 dlen=");
    sprintf(num, "%u", dlen);
    Usart1_SendString(num);
    Usart1_SendString("  maxlen=");
    sprintf(num, "%u", maxlen);
    Usart1_SendString(num);
    Usart1_SendString("\r\n");

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
        Usart1_SendString("[DBG] dlen 大于 outbuf 容量\r\n");
        return 2;
    }
    u16 offset; /* p 距离缓冲区开头多少字节 */

    /* 第一步：算 p 的偏移量 */
    offset = (u16)(p - (char *)usart3_value.data);

    /* 第二步：算 p 后面还剩多少 */
    avail = usart3_value.count - offset;

    /*---------- 诊断：偏移和可用量 ----------*/
    Usart1_SendString("[DBG] offset=");
    sprintf(num, "%u", offset);
    Usart1_SendString(num);
    Usart1_SendString("  avail=");
    sprintf(num, "%u", avail);
    Usart1_SendString(num);
    Usart1_SendString("\r\n");

    /* 第三步：检查 */
    if (avail < dlen)
    {
        dlen = avail;
        Usart1_SendString("[DBG] ★数据不完整，被截断★\r\n");
    }
    memcpy(outbuf, p, dlen);
    *outlen = dlen;

    Usart1_SendString("[DBG] 实际返回 ");
    sprintf(num, "%u", dlen);
    Usart1_SendString(num);
    Usart1_SendString(" 字节\r\n");

    return 0;
}

/*=====================================================================
 * 函数：OTA_ParseChunk
 * 功能：从一段 "$HTTPRECV:DATA,<len>\r\n<data>" 数据里，把 HTTP 正文抠出来
 *       并写到 W25Q128
 * 负责：A
 *
 *=====================================================================
 * ★ 为什么单独写这个函数（2026-11-02 排查记录）★
 *
 *   原来解析器的 bug：
 *     它假设 "$HTTPRECV:DATA,<len>\r\n<data>" 完整地出现在缓冲区里，
 *     然后在 data 里独立找 "\r\n\r\n" 判断 HTTP 头结束。
 *
 *   但实测发现 "\r\n\r\n" 会被段边界切开：
 *     段A 结尾: "...\r\n"
 *     段B 开头: "\r\n<正文>"
 *     → 每段独立找永远找不到 → 整段被当头部丢掉 → 只解析出 824 字节
 *
 *   修复思路：
 *     把"是否找到头结束"做成跨段状态（got_hdr 由调用者保存），
 *     并且用一个"连续匹配计数"跟踪 "\r\n\r\n"，
 *     这样即使被切开也能正确识别。
 *=====================================================================
 * 参数：
 *   buf     - 数据区起始（$HTTPRECV:DATA, 后面那串数字之后、\r\n 之后）
 *   len     - 数据区长度
 *   got_hdr - 输入输出：是否已经跳过 HTTP 响应头
 *   addr    - 输入输出：W25Q128 写地址
 *   total   - 输入输出：累计写入字节数
 *   hdr_cnt - 输入输出：匹配 "\r\n\r\n" 的进度（0~4）
 * 返回：无
 *====================================================================*/
static void OTA_ParseChunk(u8 *buf, u32 len,
                           u8  *got_hdr,
                           u32 *addr,
                           u32 *total,
                           u8  *hdr_cnt)
{
    u32 i;
    u32 start = 0;
    u32 body_len;

    if (len == 0) return;

    /*---------- 如果还没跳过 HTTP 头，先找 "\r\n\r\n" ----------*/
    if (*got_hdr == 0)
    {
        for (i = 0; i < len; i++)
        {
            /* 用状态机匹配 "\r\n\r\n"，跨段也能正确工作 */
            if (buf[i] == '\r')
            {
                if (*hdr_cnt == 0 || *hdr_cnt == 2) (*hdr_cnt)++;
                else                                 *hdr_cnt = 1;
            }
            else if (buf[i] == '\n')
            {
                if (*hdr_cnt == 1 || *hdr_cnt == 3) (*hdr_cnt)++;
                else                                 *hdr_cnt = 0;
            }
            else
            {
                *hdr_cnt = 0;
            }

            /* 匹配到 4 个 = 找到 "\r\n\r\n" */
            if (*hdr_cnt >= 4)
            {
                *got_hdr = 1;
                *hdr_cnt = 0;
                start = i + 1;      /* 正文从下一个字节开始 */
                break;
            }
        }

        /* 整段都是头，没找到正文起点 */
        if (*got_hdr == 0) return;
    }

    /*---------- 剩下的都是正文，写 Flash ----------*/
    body_len = len - start;
    if (body_len == 0) return;

    if (W25Q128_Write(*addr, buf + start, (u16)body_len) != 0)
    {
        Usart1_SendString("\r\n[OTA] ★Flash 写入失败\r\n");
        return;
    }

    *addr  += body_len;
    *total += body_len;
}

/*=====================================================================
 * 函数：CAT1_OTAFetchRange
 * 功能：下载固件的一个区间（用 HTTP Range 头）
 * 负责：A
 *
 *=====================================================================
 * ★ 为什么要分段下载（2026-11-02 排查记录）★
 *
 *   实测发现：一次 AT$HTTPACTION=0 请求，模块只能推 7~13KB 就停了，
 *   而固件有 15KB。两次实测：
 *     第1次：目标 14880，只收到 7689 字节数据
 *     第2次：目标 14936，只收到 12888 字节数据
 *   差异是随机的，说明是模块内部缓冲/TCP 窗口限制，不是我们代码问题。
 *
 *   解决办法：用 HTTP Range 请求头分段下载。
 *     Range: bytes=0-8191      → 要前 8KB
 *     Range: bytes=8192-16383  → 要后 8KB
 *   每段都远小于模块的单次推送上限，所以能收全。
 *
 *=====================================================================
 * 参数：
 *   url      - 固件 URL
 *   range_lo - 起始字节（含）
 *   range_hi - 结束字节（含）
 *   got_hdr  - 输入输出：是否已跳过 HTTP 头（多段共用）
 *   addr     - 输入输出：W25Q128 写地址
 *   total    - 输入输出：累计写入字节数
 *   hdr_cnt  - 输入输出：头匹配进度（多段共用）
 * 返回：0=成功  1=HTTP 初始化失败  2=接收超时  3=无数据
 *====================================================================*/
static u8 CAT1_OTAFetchRange(char *url, u32 range_lo, u32 range_hi,
                             u8 *got_hdr, u32 *addr, u32 *total, u8 *hdr_cnt)
{
    char cmd[URL_LEN + 64];
    u32  wait = 0;
    u32  last_count = 0;
    u32  idle_cnt = 0;
    u32  body_before = *total;   /* 本段开始前的正文累计 */
    u32  body_got;               /* 本段收到的正文字节数 */
    u32  body_need;              /* 本段应该收到多少正文 */
    u8   ret;

    /*=================================================================
     * ★ 关键：每次 Range 请求都要重置 HTTP 头跳过状态 ★
     *
     *   每个 Range 请求都是一个独立的 HTTP 事务，各有自己的响应头：
     *     HTTP/1.1 206 Partial Content\r\n
     *     Content-Range: bytes 8192-14667/14668\r\n
     *     ...
     *     \r\n
     *     <正文>
     *
     *   如果不重置，第 2 段就以为头已经跳过了，
     *   把第 2 段的 430 字节响应头当成固件写进 Flash。
     *
     *   实测现象（2026-11-02）：
     *     目标 14668，累计收到 15098
     *     15098 - 14668 = 430  ← 正好是 nginx 响应头大小
     *=================================================================*/
    *got_hdr = 0;
    *hdr_cnt = 0;

    body_need = range_hi - range_lo + 1;

    /*---------- ① 打开 HTTP 服务 ----------*/
    if (CAT1_SendAT("AT$HTTPOPEN", "OK", 10000))
    {
        /* 可能是上次没关，先关再重开 */
        CAT1_SendAT("AT$HTTPCLOSE", "OK", 5000);
        delay_ms(1000);
        if (CAT1_SendAT("AT$HTTPOPEN", "OK", 10000))
        {
            Usart1_SendString("[HTTP] 服务开启失败\r\n");
            return 1;
        }
    }

    /*---------- ② 配置 URL ----------*/
    sprintf(cmd, "AT$HTTPPARA=%s,80", url);
    if (CAT1_SendAT(cmd, "OK", 8000))
    {
        Usart1_SendString("[HTTP] URL 配置失败\r\n");
        CAT1_HttpClose();
        return 1;
    }

    /*---------- ③ 设置 Range 请求头 ----------
     * ★ 关键：告诉服务器只发这一段
     *   格式：Range: bytes=<起始>-<结束>
     *------------------------------------------------*/
    sprintf(cmd, "AT$HTTPRQH=Range,bytes=%lu-%lu",
            (unsigned long)range_lo, (unsigned long)range_hi);
    if (CAT1_SendAT(cmd, "OK", 8000))
    {
        Usart1_SendString("[HTTP] ★Range 头设置失败（服务器可能不支持分段）\r\n");
        CAT1_HttpClose();
        return 1;
    }

    /*---------- ④ 清空缓冲后发 GET ----------
     * ★ 关键修正（2026-11-02）★
     *
     *   手册 5.6.4 明确写了 AT$HTTPACTION=0 的响应：
     *     $HTTPRECV:DATA,<len>
     *     ......
     *
     *   它【不会】先回 "OK"！数据直接就来。
     *
     *   之前用 CAT1_SendAT("AT$HTTPACTION=0", "OK", 5000) 等 "OK"，
     *   必然等不到 → 5 秒后超时返回 1 → 报"GET 请求失败"。
     *
     *   所以这里改成"只发不等"，发完直接进入接收循环。
     *------------------------------------------------*/
    __disable_irq();
    usart3_value.count = 0;
    usart3_value.flag  = 0;
    __enable_irq();

    Usart3_SendString("AT$HTTPACTION=0\r\n");

    /* 给模块 500ms 启动请求（不判断响应）*/
    delay_ms(500);

    /*---------- ⑤ 接收这一段的数据 ----------
     * ★ 关键：收够本段需要的正文字节就停 ★
     *
     *   每段是独立的 HTTP 事务，服务器只会返回本段的数据。
     *   一旦正文收够 body_need 字节，就立即停止，不多等。
     *   这样即使模块继续推（比如连接没断），也不会多收。
     *------------------------------------------------*/
    {
        /* 缓冲上限：正文 + 头部余量 */
        u32 buf_limit = body_need + 1024;

        while (1)
        {
            delay_ms(10);
            wait++;

            if (usart3_value.count != last_count)
            {
                if (usart3_value.count - last_count >= 1024)
                {
                    Usart1_SendString(".");
                }
                last_count = usart3_value.count;
                idle_cnt = 0;
            }
            else
            {
                idle_cnt++;
            }

            /* 缓冲区快满了，必须停（防止溢出） */
            if (usart3_value.count >= (USART3_BUFFER_SIZE - 64))
                break;

            /* 收够了本段需要的量 + 头部余量 */
            if (usart3_value.count >= buf_limit)
                break;

            /* 空闲 3 秒 = 这段推完了 */
            if (idle_cnt > 300)
                break;

            /* 单段总超时 60 秒 */
            if (wait > 6000)
                break;
        }
    }

    /*---------- ⑥ 解析这段，把正文写进 Flash ----------*/
    {
        u8 *p   = usart3_value.data;
        u8 *end = usart3_value.data + usart3_value.count;
        u8 *tag = (u8 *)"$HTTPRECV:DATA,";
        u16 taglen = 15;

        while (p + taglen <= end)
        {
            u16 k;
            u8 *q = NULL;

            /* 找 "$HTTPRECV:DATA," */
            for (; p + taglen <= end; p++)
            {
                for (k = 0; k < taglen; k++)
                    if (p[k] != tag[k]) break;
                if (k == taglen)
                {
                    q = p + taglen;
                    break;
                }
            }
            if (q == NULL) break;

            /* 读长度 */
            {
                u32 seglen = 0;

                while (q < end && *q >= '0' && *q <= '9')
                    seglen = seglen * 10 + (*q++ - '0');

                /* 跳过 \r\n */
                while (q < end && (*q == '\r' || *q == '\n')) q++;

                /* ★ 关键修复：用 seglen 限制数据范围，防止越界 ★ */
                if (q + seglen > end)
                {
                    seglen = (u32)(end - q);
                }

                /* 空段（模块有时推 len=0）直接跳过 */
                if (seglen == 0)
                {
                    p = q;
                    continue;
                }

                /* 把这段交给解析器（它内部处理 HTTP 头跳过） */
                OTA_ParseChunk(q, seglen, got_hdr, addr, total, hdr_cnt);

                p = q + seglen;
            }
        }
    }

    /*---------- ⑦ 关闭 HTTP（准备下一段）----------*/
    CAT1_HttpClose();

    ret = 0;
    return ret;
}

/*=====================================================================
 * 函数：CAT1_OTA
 * 功能：OTA 固件下载主流程（分段下载版）
 * 负责：A
 *
 *=====================================================================
 * ★ 整体流程 ★
 *
 *   1. 激活 GPRS
 *   2. 按 8KB 一段，循环下载：
 *        Range: bytes=<lo>-<hi>
 *        → 收到数据 → 解析 → 写 W25Q128
 *   3. 全部下完 → 校验总长度
 *   4. 关闭 HTTP
 *
 * ★ 为什么用 8KB 一段 ★
 *   实测模块单次最多能推 12~13KB，
 *   8KB 留足余量，保证每段都能收全。
 *
 * ★ 缓冲区只需要装 8KB + 头部开销（约 9KB）★
 *   当前 USART3_BUFFER_SIZE = 16384，够用。
 *====================================================================*/
u8 CAT1_OTA(char *url, u32 fw_len)
{
    u32 addr     = 0;      /* W25Q128 写地址 */
    u32 total    = 0;      /* 累计写入字节数 */
    u32 lo;                /* 当前段起始 */
    u32 hi;                /* 当前段结束 */
    u16 block;             /* 要擦几个 64KB 块 */
    u16 i;
    u8  got_hdr  = 0;      /* 是否已跳过 HTTP 响应头 */
    u8  hdr_cnt  = 0;      /* "\r\n\r\n" 匹配进度 */
    u8  ret;
    u32 seg_idx  = 0;      /* 第几段（打印用） */
    u32 before   = 0;      /* 本段开始前的累计值 */

    /*---------- ① 长度检查 ----------*/
    block = (u16)((fw_len + 65535) / 65536);
    if (block == 0) block = 1;

    if (block > 256)
    {
        Usart1_SendString("[OTA] ★固件太大，超过 16MB\r\n");
        return 1;
    }

    if (fw_len < 28)
    {
        Usart1_SendString("[OTA] ★固件长度异常（小于 28 字节尾缀）\r\n");
        return 1;
    }

    /*---------- ② 激活网络 ----------*/
    Usart1_SendString("[OTA] 网络激活...\r\n");
    if (CAT1_ActiveGPRS())
    {
        Usart1_SendString("[OTA] 网络激活失败\r\n");
        return 1;
    }

    /*---------- ③ 擦除 Flash ----------*/
    Usart1_SendString("[OTA] 擦除 Flash");
    for (i = 0; i < block; i++)
    {
        W25Q128_EraseBlock((u8)i);
        Usart1_SendString(".");
    }
    Usart1_SendString("\r\n");

    /*---------- ④ 分段下载 ----------
     * 每段 8KB（8192 字节）
     *------------------------------------------------*/
    #define OTA_CHUNK_SIZE  8192

    Usart1_SendString("[OTA] 分段下载（每段 8KB）...\r\n");

    for (lo = 0; lo < fw_len; lo = hi + 1)
    {
        hi = lo + OTA_CHUNK_SIZE - 1;
        if (hi >= fw_len) hi = fw_len - 1;

        seg_idx++;

        Usart1_SendString("[OTA] 第 ");
        {
            char num[8];
            sprintf(num, "%lu", (unsigned long)seg_idx);
            Usart1_SendString(num);
        }
        Usart1_SendString(" 段: bytes=");
        {
            char num[16];
            sprintf(num, "%lu", (unsigned long)lo);
            Usart1_SendString(num);
        }
        Usart1_SendString("-");
        {
            char num[16];
            sprintf(num, "%lu", (unsigned long)hi);
            Usart1_SendString(num);
        }
        Usart1_SendString("  ");

        before = total;     /* 记录本段开始前的累计值 */
        ret = CAT1_OTAFetchRange(url, lo, hi,
                                 &got_hdr, &addr, &total, &hdr_cnt);

        Usart1_SendString("\r\n");

        if (ret != 0)
        {
            Usart1_SendString("[OTA] ★第 ");
            {
                char num[8];
                sprintf(num, "%lu", (unsigned long)seg_idx);
                Usart1_SendString(num);
            }
            Usart1_SendString(" 段下载失败，中止\r\n");
            CAT1_HttpClose();
            return 3;
        }
        /* 本段没收到任何新数据 = 服务器不支持 Range 或网络异常
         * ★ 必须用 before 比较，不能用 total==0 ★
         *   因为 total 是累计值，第2段之后永远大于0 */
        if (total == before)
        {
            Usart1_SendString("[OTA] ★本段没有收到数据\r\n");
            CAT1_HttpClose();
            return 4;
        }

        Usart1_SendString("  累计 ");
        {
            char num[16];
            sprintf(num, "%lu", (unsigned long)total);
            Usart1_SendString(num);
        }
        Usart1_SendString(" / ");
        {
            char num[16];
            sprintf(num, "%lu", (unsigned long)fw_len);
            Usart1_SendString(num);
        }
        Usart1_SendString(" 字节\r\n");

        /* 已收够，提前结束 */
        if (total >= fw_len) break;
    }

    /*---------- ⑤ 关闭 HTTP ----------*/
    CAT1_HttpClose();

    /*---------- ⑥ 结果判定 ----------*/
    if (total != fw_len)
    {
        Usart1_SendString("[OTA] ★★ 固件不完整：");
        {
            char num[16];
            sprintf(num, "%lu", (unsigned long)total);
            Usart1_SendString(num);
        }
        Usart1_SendString(" / ");
        {
            char num[16];
            sprintf(num, "%lu", (unsigned long)fw_len);
            Usart1_SendString(num);
        }
        Usart1_SendString(" 字节\r\n");
        return 4;
    }

    Usart1_SendString("[OTA] ★固件完整，大小校验通过\r\n");
    return 0;
}

u8 CAT1_ParseOTA(char *json)
{
    char *p;
    char *url;
    char *size_str;
    char *md5_str;
    u32 fw_size;

    /*---------- 0. 参数检查 ----------*/
    if (json == NULL)
    {
        return 1;
    }

    /*---------- 1. 找第一个逗号，前面是 URL ----------*/
    p = strchr(json, ',');
    if (p == NULL)
    {
        Usart1_SendString("[OTA] 格式错误：找不到逗号\r\n");
        return 1;
    }
    *p = '\0';
    url = json;
    size_str = p + 1;

    /*---------- 2. 找第二个逗号，后面是 MD5（可选）----------*/
    p = strchr(size_str, ',');
    if (p != NULL)
    {
        *p = '\0';
        md5_str = p + 1;
    }
    else
    {
        md5_str = NULL; /* 没有 MD5 也可以，不算错 */
    }

    /*---------- 3. 检查 URL 长度并保存 ----------*/
    if (strlen(url) == 0)
    {
        Usart1_SendString("[OTA] 格式错误：URL 为空\r\n");
        return 1;
    }

    if (strlen(url) > OTA_URL_LEN) /* ★ 防溢出 ★ */
    {
        Usart1_SendString("[OTA] URL 太长，装不下\r\n");
        return 2;
    }

    strcpy((char *)URL_ABuff, url); /* ★ 关键：保存 URL ★ */

    /*---------- 4. 解析固件大小 ----------*/
    fw_size = (u32)atoi(size_str);

    /*---------- 5. 大小合理性检查 ----------*/
    if (fw_size == 0)
    {
        Usart1_SendString("[OTA] 固件大小为 0，不对\r\n");
        return 2;
    }

    if (fw_size > FLASH_A_SIZE) /* 480KB */
    {
        Usart1_SendString("[OTA] 固件太大，超过 480KB\r\n");
        return 2;
    }

    BemfaInfoCB.OTA_firelen = fw_size;

    /*---------- 6. 打印解析结果 ----------*/
    Usart1_SendString("[OTA] ★收到升级命令★\r\n");
    Usart1_SendString("[OTA] URL  : ");
    Usart1_SendString((char *)URL_ABuff);
    Usart1_SendString("\r\n");

    Usart1_SendString("[OTA] 大小 : ");
    {
        char num[16];
        sprintf(num, "%lu", (unsigned long)fw_size);
        Usart1_SendString(num);
    }
    Usart1_SendString(" 字节\r\n");

    if (md5_str != NULL)
    {
        Usart1_SendString("[OTA] MD5  : ");
        Usart1_SendString(md5_str);
        Usart1_SendString("\r\n");
    }

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
    ret = CAT1_SendAT("", "CIPSEND", 5000);

    /* ★ 注意：CAT1_SendAT("") 会多发一个空指令的 \r\n
     *   如果实测发现多余，改成下面这种"只等不收"的写法：
     *
     *   usart3_value.flag = 0;
     *   usart3_value.count = 0;
     *   for (i = 0; i < 50; i++) {
     *       delay_ms(100);
     *       if (usart3_value.flag) break;
     *   }
     */

    return 0; /* 即使没等到响应也返回成功，避免误判 */
}
