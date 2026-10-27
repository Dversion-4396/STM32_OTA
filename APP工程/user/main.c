/*=====================================================================
 * 文件：main.c
 * 说明：APP 主程序（OTA 客户端）
 * 负责：D（主循环整合），A/C 各自往 ActiveEvent 里加逻辑
 *
 * ★初始化顺序很重要，不要随便调★
 *====================================================================*/
#include "common.h"
#include "usart.h"
#include "delay.h"
#include "i2c.h"
#include "eeprom.h"
#include "spi.h"
#include "cat1.h"
#include "mqtt.h"

/*=====================================================================
 * 全局变量定义（★只能在这里定义一次，其他文件用 extern 引用★）
 *
 * 为什么集中在这里定义：
 *   C 语言里全局变量只能定义一次，其他地方只能 extern 声明。
 *   放在 main.c 最直观，谁都能看到有哪些全局状态。
 *===================================================================*/
BemfaInfo_CB BemfaInfoCB;      /* 巴法云参数 + OTA 状态 */
u32 SysEventFlag = 0;          /* 系统事件标志（位掩码） */
u8 URL_ABuff[OTA_URL_LEN + 1]; /* OTA 下载地址缓冲区 */
u32 ConnectTimer = 0;          /* 连上服务器后的计时（秒） */
u32 PingTimer = 0;             /* 上次发 PING 的时间（秒） */

/*=====================================================================
 * 函数：main
 * 功能：程序入口
 * 参数：无
 * 返回：int（不会返回）
 * 负责：D
 *====================================================================*/
int main(void)
{
    u8 ret;
    u16 i;

    /*---------- 1. 初始化调试串口 USART1 ----------*/
    Usart1_Init(USART1_BRR);
    Usart1_SendString("\r\n\r\n=== STM32 启动 ===\r\n");

    /*---------- 2. 初始化 4G 串口 USART3 ----------*/
    Usart3_Init(USART3_BRR);
    Usart1_SendString("USART3 初始化完成\r\n");

    /*---------- 3. 等 4G 模块启动 ----------*/
    /* 模块上电后要 10~15 秒才能响应 AT，太早发没反应 */
    Usart1_SendString("等待 4G 模块启动5秒...\r\n");
    delay_ms(5000);

    /*---------- 4. 测试 AT ----------*/
    Usart1_SendString("发送 AT ...\r\n");
    ret = CAT1_SendAT("AT", "OK", 3000);

    if (ret == 0)
    {
        Usart1_SendString("AT 成功\r\n");
        /* 打印模块返回的原始数据 */
        Usart1_SendString("模块返回: [");
        Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
        Usart1_SendString("]\r\n");
    }
    else
    {
        Usart1_SendString("AT 失败\r\n");
        Usart1_SendString("收到字节数: ");
        /* 手动打印数字 */
        {
            char num[8];
            sprintf(num, "%u", usart3_value.count);
            Usart1_SendString(num);
        }
        Usart1_SendString("\r\n");
        Usart1_SendString("原始数据: [");
        Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
        Usart1_SendString("]\r\n");
        Usart1_SendString("flag=");
        {
            char num[4];
            sprintf(num, "%u", usart3_value.flag);
            Usart1_SendString(num);
        }
        Usart1_SendString("\r\n");
    }
    /*---------- 5. 激活 GPRS（内部已有详细打印）----------*/
    ret = CAT1_ActiveGPRS();
    if (ret != 0)
    {
        Usart1_SendString("[GPRS] 失败，步骤=");
        {
            char num[4];
            sprintf(num, "%u", ret);
            Usart1_SendString(num);
        }
        Usart1_SendString("\r\n");
    }
    /*=================================================================
     * HTTP 功能诊断（A 负责）
     *
     * 目的：确认这块 L511-2 到底支不支持 HTTP
     *       决定后面走 HTTP 方案还是 TCP 直连方案
     *
     * 诊断顺序（每步都要看返回）：
     *   ① AT+CGATT?        确认 GPRS 已附着
     *   ② AT+CGACT?        确认 PDP 已激活
     *   ③ AT+CGPADDR=1     确认有 IP
     *   ④ AT+HTTPTERM      清残留（返回 ERROR 正常）
     *   ⑤ AT+HTTPINIT      核心测试
     *   ⑥ AT+HTTPINIT=?    如果 ⑤ 失败，看支持哪些子命令
     *   ⑦ AT+CIPSTART=?    如果 HTTP 不支持，看 TCP 支持哪些参数
     *=================================================================*/

    Usart1_SendString("\r\n");
    Usart1_SendString("========================================\r\n");
    Usart1_SendString("  HTTP 功能诊断\r\n");
    Usart1_SendString("========================================\r\n");

    /*---------- ① 确认 GPRS 附着 ----------*/
    Usart1_SendString("[1] AT+CGATT?\r\n");
    ret = CAT1_SendAT("AT+CGATT?", "OK", 3000);
    Usart1_SendString("    返回: [");
    Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
    Usart1_SendString("]\r\n\r\n");

    /*---------- ② 确认 PDP 激活 ----------*/
    Usart1_SendString("[2] AT+CGACT?\r\n");
    ret = CAT1_SendAT("AT+CGACT?", "OK", 3000);
    Usart1_SendString("    返回: [");
    Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
    Usart1_SendString("]\r\n\r\n");

    /*---------- ③ 确认有 IP ----------*/
    Usart1_SendString("[3] AT+CGPADDR=1\r\n");
    ret = CAT1_SendAT("AT+CGPADDR=1", "OK", 5000);
    Usart1_SendString("    返回: [");
    Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
    Usart1_SendString("]\r\n\r\n");

    /*---------- ④ 清残留（返回 ERROR 正常）----------*/
    Usart1_SendString("[4] AT+HTTPTERM（清残留，ERROR 正常）\r\n");
    CAT1_SendAT("AT+HTTPTERM", "OK", 3000);
    Usart1_SendString("    返回: [");
    Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
    Usart1_SendString("]\r\n\r\n");
    delay_ms(500);

    /*---------- ⑤ 核心测试：HTTPINIT ----------*/
    Usart1_SendString("[5] AT+HTTPINIT  ★核心测试★\r\n");
    ret = CAT1_SendAT("AT+HTTPINIT", "OK", 5000);
    Usart1_SendString("    返回: [");
    Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
    Usart1_SendString("]\r\n");

    if (ret == 0)
    {
        Usart1_SendString("    ★★★ HTTPINIT 成功！HTTP 方案可行 ★★★\r\n");
        Usart1_SendString("    下一步：写 CAT1_HttpInit / CAT1_HttpRead\r\n");
        /* 测试完记得 TERM 掉，否则会占用资源 */
        CAT1_SendAT("AT+HTTPTERM", "OK", 3000);
    }
    else
    {
        Usart1_SendString("    ★ HTTPINIT 失败\r\n");
        Usart1_SendString("    继续诊断看模块支持什么...\r\n\r\n");

        /*---------- ⑥ 查 HTTP 支持哪些子命令 ----------*/
        Usart1_SendString("[6] AT+HTTPINIT=?（查支持哪些 HTTP 子命令）\r\n");
        ret = CAT1_SendAT("AT+HTTPINIT=?", "OK", 3000);
        Usart1_SendString("    返回: [");
        Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
        Usart1_SendString("]\r\n");
        Usart1_SendString("    如果这里也是 ERROR，说明模块根本不支持 HTTP\r\n\r\n");

        /*---------- ⑦ 查 TCP 支持哪些参数 ----------*/
        Usart1_SendString("[7] AT+CIPSTART=?（查 TCP 支持）\r\n");
        ret = CAT1_SendAT("AT+CIPSTART=?", "OK", 3000);
        Usart1_SendString("    返回: [");
        Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
        Usart1_SendString("]\r\n");
        Usart1_SendString("    如果这里 OK，说明 TCP 可用，改走 TCP 直连方案\r\n\r\n");
    }

    /*---------- ⑧ 看模块型号和固件版本 ----------*/
    Usart1_SendString("[8] ATI（模块型号）\r\n");
    CAT1_SendAT("ATI", "OK", 3000);
    Usart1_SendString("    返回: [");
    Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
    Usart1_SendString("]\r\n\r\n");

    Usart1_SendString("[9] AT+CGMR（固件版本）\r\n");
    CAT1_SendAT("AT+CGMR", "OK", 3000);
    Usart1_SendString("    返回: [");
    Usart1_SendBuffer((char *)usart3_value.data, usart3_value.count);
    Usart1_SendString("]\r\n\r\n");

    Usart1_SendString("========================================\r\n");
    Usart1_SendString("  诊断结束\r\n");
    Usart1_SendString("========================================\r\n");
    /*---------- 6. 主循环 ----------*/
    while (1)
    {
        delay_ms(1000);
    }
}
