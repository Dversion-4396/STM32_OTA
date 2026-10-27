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
BemfaInfo_CB   BemfaInfoCB;                     /* 巴法云参数 + OTA 状态 */
u32            SysEventFlag = 0;                /* 系统事件标志（位掩码） */
u8             URL_ABuff[OTA_URL_LEN+1];        /* OTA 下载地址缓冲区 */
u32            ConnectTimer = 0;                /* 连上服务器后的计时（秒） */
u32            PingTimer = 0;                   /* 上次发 PING 的时间（秒） */

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

    /*---------- 6. 主循环 ----------*/
    while (1)
    {
        delay_ms(1000);
    }
}
