/*=====================================================================
 * 文件：cat1.c
 * 说明：L511-2 4G 模块驱动实现
 * 负责：A（CAT1_ParseOTA 由 C 负责）
 *
 * ★完成顺序建议★：
 *   第 1 步：CAT1_GPIO_Init    ← 成品底板不需要，留空
 *   第 2 步：CAT1_PowerOn      ← 只等模块启动，不控 PWRKEY
 *   第 3 步：CAT1_SendAT       ← ★核心★ 已跑通
 *   第 4 步：CAT1_ActiveGPRS   ← ★核心★ 已跑通（拿到 IP 10.57.138.207）
 *   第 5 步：CAT1_HttpInit + CAT1_HttpRead
 *   第 6 步：CAT1_OTA（完整流程）
 *
 * ★★★ 当前进度（2026-10-27）★★★
 *   ✅ CAT1_SendAT      已实现并实测通过
 *   ✅ CAT1_ActiveGPRS  已实现并实测通过
 *     实测输出：
 *       [GPRS] SIM卡就绪
 *       [GPRS] 网络已注册
 *       [GPRS] IP: +CGPADDR: 1,"10.57.138.207"
 *       [GPRS] 成功
 *   ⏳ CAT1_PowerOn      待写（简单）
 *   ⏳ CAT1_HttpInit     待写（★先验证 AT+HTTPINIT 是否支持★）
 *   ⏳ CAT1_HttpRead     待写
 *   ⏳ CAT1_OTA          待写
 *
 *=====================================================================
 * 2026-10-27 实测记录（串口助手 + USB-TTL）
 *
 * 硬件：L511-2 成品模块板（LCC 焊死 + NANO SIM 卡座 + DC-DC + 天线座）
 * 接线：DX-PJ14 USB-TTL 的 GND/TX/RX 接模块，115200 8N1
 *
 * 实测通过：
 *   AT                        → OK
 *   AT+ICCID                  → +ICCID: 898600501626fa367139  ← 移动号段
 *   AT+CPIN?                  → +CPIN: READY
 *   AT+CSQ                    → +CSQ: 24,99                   ← 信号极好
 *   AT+CREG?                  → +CREG: 0,1                    ← 已注册
 *   AT+CGATT?                 → +CGATT: 1                     ← 已附着
 *   AT+CGDCONT=1,"IP","CMNET" → OK
 *   AT+CGACT=1,1              → OK
 *   AT+CGPADDR=1              → +CGPADDR: 1,"10.167.134.50"
 *
 * 实测失败：
 *   AT+SAPBR=3,1,"Contype","GPRS"  → ERROR
 *   AT+SAPBR=3,1,"APN","CMNET"     → ERROR
 *   AT+SAPBR=1,1                   → ERROR
 *   AT+HTTPINIT                    → ERROR  ★待解决★
 *   AT+HTTPTERM                    → ERROR（正常，没初始化过就会报错）
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
 *       所以 CAT1_PowerOn 必须先发 AT 探测，已开机就别再拉 PWRKEY。
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
 * 函数：CAT1_PowerOn
 * 负责：A
 *
 * ★★★ 本工程用的是成品底板，上电即工作，不需要 PWRKEY 时序 ★★★
 *
 * 实测（2026-10-27）：
 *   底板 VIN 接 5V，EN 悬空 → 模块自动启动，
 *   等约 10~15 秒后发 AT 就能收到 OK。
 *
 * 所以本函数只做一件事：【等模块启动完成】。
 *
 * ===== 实现要点 =====
 *   1. 先给模块一点启动时间：
 *        delay_ms(3000);
 *      ← 刚上电时模块内部还在初始化，太早发 AT 没响应
 *
 *   2. 循环 timeout 次（每次 100ms），发 AT 探测：
 *        for(i = 0; i < timeout; i++) {
 *            delay_ms(100);
 *            if (i % 10 == 0) {              // 每 1 秒试一次
 *                if (CAT1_SendAT("AT", "OK", 500) == 0)
 *                    return 0;                // 模块已响应
 *            }
 *        }
 *
 *   3. 超时返回 1
 *
 * ===== 参数说明 =====
 *   timeout - 单位 100ms，600 = 60 秒
 *             建议值：300（30秒）够用，冷启动慢的给 600
 *
 * ===== 排错 =====
 *   如果一直超时，按顺序查：
 *     1. 模块 VIN 有没有接 5V（不是 3.3V）
 *     2. GND 有没有和 STM32 共地
 *     3. TX/RX 有没有接反（模块 TX → PB11，模块 RX → PB10）
 *     4. 板子 P5 跳线帽有没有拔掉
 *     5. K1 拨动开关有没有拨到 TXD 侧
 *
 * ===== 如果以后换成裸模块 =====
 *   在 delay_ms(3000) 之前加开机时序：
 *     PWRKEY_LOW();
 *     delay_ms(1500);        // 规格书要求 >1 秒
 *     PWRKEY_HIGH();
 *     delay_ms(3000);
 *   并且要注意：PWRKEY 是"开关机切换"，已开机再拉会关机，
 *   所以裸模块方案要先发 AT 探测，活着就别拉。
 *====================================================================*/
u8 CAT1_PowerOn(u16 timeout)
{
    /* TODO【A】：实现 */

    return 1;
}

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
        if (CAT1_SendAT("AT+CREG?", ",1", 2000) == 0) break;   /* 本地注册 */
        if (CAT1_SendAT("AT+CREG?", ",5", 2000) == 0) break;   /* 漫游注册 */
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
 * 实现要点：
 *   char cmd[600];        ← ★600 字节，阿里云 OSS 签名 URL 可能很长★
 *
 *   1. CAT1_SendAT("AT+HTTPINIT", "OK", 5000)                → 失败 return 1
 *   2. CAT1_SendAT("AT+HTTPPARA=\"CID\",1", "OK", 3000)      → 失败 return 2
 *   3. sprintf(cmd, "AT+HTTPPARA=\"URL\",\"%s\"", url);
 *      CAT1_SendAT(cmd, "OK", 5000)                          → 失败 return 3
 *   4. CAT1_SendAT("AT+HTTPACTION=0", "OK", 5000)            → 失败 return 4
 *   5. 等响应（最多 30 秒）：
 *        循环 300 次，每次 delay_ms(100)
 *        收到 "+HTTPACTION: 0,200" → return 0
 *        收到 "+HTTPACTION: 0,"   → return 5
 *   6. return 6
 *
 * ★★ 两个实测坑 ★★
 *
 * 坑 1：AT+HTTPTERM 返回 ERROR 是正常的
 *       HTTP 服务没初始化过时，TERM 一定报错，不能拿它判断成败。
 *       所以本函数开头可以"先 TERM 再 INIT"，TERM 的返回值忽略。
 *
 * 坑 2：AT+HTTPACTION=0 的响应是【两段】
 *       发送后立即回：OK                        ← 指令收到了
 *       等几秒后回：+HTTPACTION: 0,200,12345    ← 下载完成
 *       所以第 4 步 SendAT 找到 OK 就返回了，但下载还没完，
 *       必须第 5 步单独再等第二段。很多人这里出错。
 *
 * ★★ 如果 AT+HTTPINIT 一直返回 ERROR（实测还没验证通过）★★
 *   说明这块 L511-2 的 HTTP 功能可能没开或实现不同。排查顺序：
 *     ① 先发 AT+HTTPTERM 再发 AT+HTTPINIT（清残留状态）
 *     ② 确认 AT+CGACT=1,1 已成功、AT+CGPADDR=1 有 IP
 *     ③ 发 AT+HTTPINIT=? 看模块支持哪些 HTTP 子命令
 *   如果确实不支持 HTTP，改走 TCP 直连方案：
 *     AT+CIPSTART="TCP","服务器",80
 *     AT+CIPSEND=长度  然后手工拼 HTTP 报文
 *   工作量更大但一定能通，因为 TCP 是所有模块的基础功能。
 *====================================================================*/
u8 CAT1_HttpInit(char *url)
{
    /* TODO【A】：实现 */

    return 1;
}

/*=====================================================================
 * 函数：CAT1_HttpRead
 * 负责：A
 *
 * ★两个大坑★
 *
 * 实现要点：
 *   1. sprintf(cmd, "AT+HTTPREAD=%lu,%u", offset, len);
 *      sprintf(expect, "+HTTPREAD: %u", len);
 *   2. if(CAT1_SendAT(cmd, expect, 5000)) return 1;
 *   3. ptr = (u8*)strstr((char*)usart3_value.data, expect);
 *      if(ptr == NULL) return 2;
 *      ptr += strlen(expect);
 *      while(*ptr == '\r' || *ptr == '\n') ptr++;
 *   4. memcpy(outbuf, ptr, len);
 *
 * ★坑 1：必须用 memcpy 定长拷贝，不能用 strcpy（二进制含 0x00 会截断）★
 * ★坑 2：返回约 274 字节，接收缓冲必须 ≥1024（已改好）★
 *====================================================================*/
u8 CAT1_HttpRead(u32 offset, u16 len, u8 *outbuf)
{
    /* TODO【A】：实现 */
    return 1;
}

/*=====================================================================
 * 函数：CAT1_OTA
 * 负责：A
 *
 * 实现要点：
 *   u32 offset; u16 len, page = 0; u8 buf[256];
 *
 *   1. if(CAT1_PowerOn(600)) return 1;
 *   2. if(CAT1_ActiveGPRS()) return 2;
 *   3. if(CAT1_HttpInit(url)) return 3;
 *   4. W25Q128_EraseBlock(0);          擦第 0 块（64KB）
 *   5. for(offset = 0; offset < fw_len; offset += 256){
 *          len = (fw_len - offset >= 256) ? 256 : (fw_len - offset);
 *          if(CAT1_HttpRead(offset, len, buf)) return 4;
 *          W25Q128_WritePage(page++, buf);
 *          每 4KB 打印一次进度
 *      }
 *   6. CAT1_SendAT("AT+HTTPTERM", "OK", 3000);
 *   7. return 0;
 *
 * ★固件必须 < 64KB（W25Q128 只擦了第 0 块）★
 *====================================================================*/
u8 CAT1_OTA(char *url, u32 fw_len)
{
    /* TODO【A】：实现 */
    return 1;
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
