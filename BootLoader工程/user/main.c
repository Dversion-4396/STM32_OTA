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

//串口波特率
//★ 2026-11-03 改成 115200 ★
//  原来 BootLoader 是 9600、APP 是 9600，两边不一致的话
//  跳转前后串口助手要来回切波特率，很麻烦。
//  现在两个工程 common.h 的 USART1_BRR 都统一成 115200 了。
#define  USART1_BT 115200
#define  USART2_BT 9600

/*==================== 全局变量定义 ====================*/

//巴法云参数 + OTA 状态（EEPROM 的 0x00~0x62 就存这个结构体）
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
 *   1. 串口（先让调试口通，后面才能打印日志）
 *   2. I2C + EEPROM（读巴法云参数和 App 信息）
 *   3. SPI + W25Q128（OTA 固件存在这里）
 *   4. BootLoader_Branch（5 秒等待 / OTA 事件 / 直接跳 APP）
 *   5. 打印菜单
 *   6. 进入菜单主循环
 *====================================================================*/
int main(void)
{
    /* ---- ① 串口：先通调试口 ---- */
    Usart1_Init(USART1_BT);
    Usart2_Init(USART2_BT);

    Usart1_SendString("\r\n\r\n");
    Usart1_SendString("==========================================\r\n");
    Usart1_SendString("     战神V1 BootLoader  v3.0\r\n");
    Usart1_SendString("     分区：Boot 32KB / APP 480KB\r\n");
    Usart1_SendString("==========================================\r\n");

    /* ---- ② I2C + EEPROM ---- */
    I2C_GPIO_Init();

    if (EEPROM_Init() != 0)
    {
        Usart1_SendString("[1] EEPROM 初始化失败\r\n");
    }
    else
    {
        /* 读巴法云参数 + OTA 标志（99 字节）
         * 注意：这个函数读失败时会把 BemfaInfoCB 清零，
         *       OTA_flag 变 0 ⇒ 不会误判"有待升级事件"，是安全的 */
        if (EEPROM_ReadInfo() != 0)
        {
            /* 参数无效（首次上电）时，顺手把 App 信息的出厂默认值也写进去，
             * 这样菜单 [3] 至少能显示 1.0.0 而不是一片空白 */
            (void)EEPROM_LoadAppInfo(&EEPROM_Data);
            EEPROM_Data.app_size = 0;
            EEPROM_Data.ver_num  = 10000;      /* 1.0.0 */
            (void)EEPROM_SaveAppInfo(&EEPROM_Data);
            Usart1_SendString("[1] EEPROM 参数无效，已写入默认值\r\n");
        }
        else
        {
            Usart1_SendString("[1] EEPROM 参数有效\r\n");
        }

        /* 把当前 App 信息读出来打印一次，方便对照 */
        (void)EEPROM_LoadAppInfo(&EEPROM_Data);
        Usart1_SendString("[1] 当前版本名 = ");
        Usart1_SendString(EEPROM_Data.ver);
        Usart1_SendString("\r\n");
    }

    /* ---- ③ SPI + W25Q128 ----
     * ★ 必须调 W25Q128_Init ★
     *   只调 SPI2_Init 只是把 SPI 外设配好，
     *   W25Q128 芯片本身还要发"释放掉电模式"等初始化命令才能读写。
     *   少了这一步，后面读 ID 全是 0x000000，
     *   OTA 搬运时会以为"W25Q128 里没有固件"。 */
    SPI2_Init();

    if (W25Q128_Init() != 0)
    {
        Usart1_SendString("[2] W25Q128 初始化失败\r\n");
    }
    else
    {
        char idbuf[48];
        sprintf(idbuf, "[2] W25Q128 正常，ID = 0x%06lX\r\n",
                (unsigned long)W25Q128_ReadID());
        Usart1_SendString(idbuf);
    }

    /* ---- ④ 分支判断：进菜单 / 有 OTA 事件 / 直接跳 APP ---- */
    BootLoader_Branch();

    /* ---- ⑤ 进到这里说明要留在 BootLoader，打印菜单 ---- */
    BootLoader_Info();

    /* ---- ⑥ 菜单主循环 ---- */
    while (1)
    {
        /* (1) 串口收到一个字节（IDLE 中断置的 flag） */
        if (usart1_value.flag == 1)
        {
            /* (2) 交给菜单处理 */
            BootLoader_Event(usart1_value.data, usart1_value.count);

            /* (3) 清标志，准备收下一条命令 */
            usart1_value.flag  = 0;
            usart1_value.count = 0;
        }

        /* (4) 处理 OTA 事件
         *     ★ 这一句原来被删掉了 ★
         *     删掉的后果：BootLoader_Branch 里发现 OTA_flag 时
         *     置了 BootEventFlag |= OTA_MODEM 然后 return，
         *     但没人调 ActiveEvent，于是程序就停在那儿一动不动，
         *     既不升级也不跳 APP，表现为"开机卡死"。
         *     ActiveEvent 内部会自己判断"有没有 OTA 事件"，
         *     没有就直接 return，所以放主循环里每轮调是安全的。 */
        ActiveEvent();
    }
}
