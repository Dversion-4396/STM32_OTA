/*=====================================================================
 * 文件：cat1.c
 * 说明：L511-2 4G 模块驱动实现
 * 负责：A（CAT1_ParseOTA 由 C 负责）
 *
 * ★完成顺序建议★：
 *   第 1 步：CAT1_GPIO_Init
 *   第 2 步：CAT1_PowerOn（能开机，NET 灯闪）
 *   第 3 步：CAT1_SendAT（能发 AT 收 OK）← 到这里就算通了
 *   第 4 步：CAT1_ActiveGPRS（能拿到 IP）
 *   第 5 步：CAT1_HttpInit + CAT1_HttpRead
 *   第 6 步：CAT1_OTA（完整流程）
 *====================================================================*/
#include "cat1.h"
#include "usart.h"
#include "delay.h"
#include "spi.h"

/*=====================================================================
 * 函数：CAT1_GPIO_Init
 * 负责：A
 *
 * 实现要点：
 *   1. 开时钟：RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOF, ENABLE)
 *   2. PF0（PWRKEY）：GPIO_Mode_Out_OD（开漏）★不是推挽★
 *      ← 模块内部有 1.8V 上拉，开漏只拉低不放高，避免 3.3V 倒灌
 *   3. PF1（RESET）：GPIO_Mode_Out_PP
 *   4. PF2/PF3：GPIO_Mode_IPU（上拉输入）
 *   5. 初始：PWRKEY 拉高（释放），RESET 拉高
 *====================================================================*/
void CAT1_GPIO_Init(void)
{
    /* TODO【A】：实现 */
}

/*=====================================================================
 * 函数：CAT1_PowerOn
 * 负责：A
 *
 * 实现要点：
 *   1. PWRKEY 拉低
 *   2. delay_ms(1500)       规格书要求 >1 秒
 *   3. PWRKEY 拉高（释放）
 *   4. delay_ms(3000)       等模块启动
 *   5. 循环 timeout 次：
 *        delay_ms(100)
 *        如果 NET_STA == 0，返回 0
 *   6. 超时返回 1
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
    /* TODO【A】：实现 */
    return 1;
}

/*=====================================================================
 * 函数：CAT1_ActiveGPRS
 * 负责：A
 *
 * 实现要点：连续发 4 条指令，任何一条失败就返回对应错误码
 *
 *   if(CAT1_SendAT("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", "OK", 3000)) return 1;
 *   if(CAT1_SendAT("AT+SAPBR=3,1,\"APN\",\"\"", "OK", 3000))         return 2;
 *   if(CAT1_SendAT("AT+SAPBR=1,1", "OK", 20000))                     return 3;
 *   if(CAT1_SendAT("AT+SAPBR=2,1", "+SAPBR: 1,1", 5000))             return 4;
 *   return 0;
 *
 * ★注意字符串里的引号要转义：\"★
 *====================================================================*/
u8 CAT1_ActiveGPRS(void)
{
    /* TODO【A】：实现 */
    return 1;
}

/*=====================================================================
 * 函数：CAT1_HttpInit
 * 负责：A
 *
 * 实现要点：
 *   char cmd[600];
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
 * 云端推送报文：
 *   {"data":{"size":31236,"sign":"xxx","version":"app_2023112855511",
 *   "signMethod":"Md5","url":"https://xxx.aliyuncs.com/fw.bin","md5":"abc"},
 *   "id":123,"message":"success"}
 *
 * 实现要点：
 *   1. ptr = strstr(json, "\"data\":");
 *      if(ptr == NULL) return 1;
 *   2. sscanf(ptr,
 *        "\"data\":{\"size\":%lu,\"sign\":\"%[^\"]\",\"version\":\"%[^\"]\","
 *        "\"signMethod\":\"Md5\",\"url\":\"https://%[^\"]\",\"md5\":\"%[^\"]\"},"
 *        "\"id\":%d,\"message\":\"success\"}",
 *        &AliInfoCB.OTA_firelen, sign_buff, AliInfoCB.Version_ABuff,
 *        URL_ABuff, md5_buff, &iddata);
 *   3. 返回值 != 6 就 return 2
 *   4. return 0
 *
 * ★坑：格式串必须和云端报文完全一致（含空格逗号）★
 *       解析失败先把原始报文打印出来逐字符对比
 *====================================================================*/
u8 CAT1_ParseOTA(char *json)
{
    /* TODO【C】：实现 */
    return 1;
}
