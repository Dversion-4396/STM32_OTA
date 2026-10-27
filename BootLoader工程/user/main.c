/*=====================================================================
 * 文件：main.c
 * 说明：BootLoader 主程序
 * 负责：B
 *====================================================================*/
#include "common.h"
#include "boot.h"
#include "usart.h"
#include "delay.h"
#include "i2c.h"
#include "eeprom.h"
#include "spi.h"
#include "stmflash.h"

/*==================== 全局变量定义 ====================*/
AliyunInfo_CB AliInfoCB;
u32            SysEventFlag = 0;
u8             URL_ABuff[OTA_URL_LEN+1];
u32            ConnectTimer = 0;
u32            PingTimer = 0;

/*=====================================================================
 * 函数：main
 * 功能：程序入口
 * 参数：无
 * 返回：int（不会返回）
 * 负责：B
 *
 * 初始化顺序：
 *   1. 串口（先让调试口通）
 *   2. I2C + EEPROM（读三元组）
 *   3. SPI + W25Q128
 *   4. 打印 banner
 *   5. BootLoader_Branch（分支判断）
 *   6. 进入菜单主循环
 *====================================================================*/
int main(void)
{
    /* ---------- 1. 串口 ---------- */
    /* TODO【B】：Usart1_Init(115200); */

    /* ---------- 2. I2C + EEPROM ---------- */
    /* TODO【B】：I2C_GPIO_Init();
     *            EEPROM_ReadInfo(); */

    /* ---------- 3. SPI + W25Q128 ---------- */
    /* TODO【B】：SPI2_Init();
     *            W25Q128_Init(); */

    /* ---------- 4. 打印 banner ---------- */
    /* TODO【B】：
     * Usart1_SendString("\r\n\r\n");
     * Usart1_SendString("========================================\r\n");
     * Usart1_SendString("     战神V1 BootLoader  v1.0\r\n");
     * Usart1_SendString("========================================\r\n");
     */

    /* ---------- 5. 分支判断 ---------- */
    /* TODO【B】：BootLoader_Branch(); */

    /* ---------- 6. 打印菜单 ---------- */
    /* TODO【B】：BootLoader_Info(); */

    /* ---------- 7. 主循环 ---------- */
    while(1)
    {
        if(usart1_value.flag)
        {
            /* TODO【B】：BootLoader_Event(usart1_value.data, usart1_value.count);
             *            usart1_value.flag = 0;
             *            usart1_value.count = 0; */
        }

        ActiveEvent();
    }
}
