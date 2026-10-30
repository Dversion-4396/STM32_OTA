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

//巴法云参数 + OTA 状态
BemfaInfo_CB BemfaInfoCB;
u32 SysEventFlag = 0;
u8 URL_ABuff[OTA_URL_LEN + 1];
u32 ConnectTimer = 0;
u32 PingTimer = 0;

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
    u8 ret;
    char buf[80]; /* ★ sprintf 用的缓冲区 */

    /*========== 1. 串口 ==========*/
    Usart1_Init(USART1_BRR); /* 9600，和串口助手一致 */
    Usart1_SendString("\r\n\r\n");
    Usart1_SendString("==========================================\r\n");
    Usart1_SendString("     战神V1 BootLoader  v1.0\r\n");
    Usart1_SendString("==========================================\r\n");

    /*========== 2. I2C + EEPROM ==========*/
    I2C_GPIO_Init();
    Usart1_SendString("[1] I2C 初始化完成\r\n");

    EEPROM_ReadInfo();
    Usart1_SendString("[2] EEPROM 读取完成\r\n");

    /*========== 3. SPI + W25Q128 ==========*/
    SPI2_Init();
    ret = W25Q128_Init();
    if (ret == 0)
        Usart1_SendString("[3] W25Q128 初始化成功\r\n");
    else
        Usart1_SendString("[3] ★W25Q128 初始化失败\r\n");

    BootLoader_Branch();

    BootLoader_Info();
    /*========== 5. 主循环 ==========*/
    Usart1_SendString("进入主循环\r\n");
    while (1)
    {
        if (usart1_value.flag)
        {
            BootLoader_Event(usart1_value.data, usart1_value.count);
            usart1_value.flag = 0;
            usart1_value.count = 0;
        }

        ActiveEvent();
    }
}
