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

/*==================== 全局变量定义（★只能在这里定义一次★）====================*/
AliyunInfo_CB AliInfoCB;
u32            SysEventFlag = 0;
u8             URL_ABuff[OTA_URL_LEN+1];
u32            ConnectTimer = 0;
u32            PingTimer = 0;

/*=====================================================================
 * 函数：U3_EventDispatch
 * 功能：USART3（4G）收到数据后的事件分发
 * 参数：data - 数据；len - 长度
 * 返回：无
 * 负责：D
 *
 * 实现要点：
 *   4G 回来的数据分两种：
 *     ① MQTT 报文（首字节 0x20/0x30/0x40/0x90/0xD0）→ U2PassiveEvent
 *     ② AT 应答（文本）→ 由 CAT1_SendAT 内部处理，这里可打印调试
 *====================================================================*/
void U3_EventDispatch(u8 *data, u16 len)
{
    /* TODO【D】：实现
     *
     * if(len > 0 && (data[0]==0x20 || data[0]==0x30 || data[0]==0x40 ||
     *                data[0]==0x90 || data[0]==0xD0)){
     *     U2PassiveEvent(data, len);
     *     return;
     * }
     */
}

/*=====================================================================
 * 函数：ActiveEvent
 * 功能：主动事件处理（主循环循环调用）
 * 参数：无
 * 返回：无
 * 负责：A + C 各自往这里加
 *====================================================================*/
void ActiveEvent(void)
{
    /* TODO【A】：处理 OTA 下载事件
     *
     * if(SysEventFlag & OTA_EVENT){
     *     SysEventFlag &= ~OTA_EVENT;
     *     if(CAT1_OTA(URL_ABuff, AliInfoCB.OTA_firelen) == 0){
     *         AliInfoCB.OTA_flag = BOOT_STA_O;
     *         EEPROM_SaveInfo();
     *         NVIC_SystemReset();
     *     }else{
     *         NVIC_SystemReset();
     *     }
     * }
     */

    /* TODO【C】：处理连接服务器事件
     *
     * if(SysEventFlag & CONNECT_START){
     *     SysEventFlag &= ~CONNECT_START;
     *     MQTT_ConnectAliyun();
     * }
     */
}

/*=====================================================================
 * 函数：main
 * 功能：程序入口
 * 参数：无
 * 返回：int（不会返回）
 * 负责：D
 *====================================================================*/
int main(void)
{
    /* ---------- 1. 向量表重定位（★必须★）---------- */
    /* TODO【D】：NVIC_SetVectorTable(NVIC_VectTab_FLASH, 0x8000);
     *            漏了中断会跳到 BootLoader 的向量表，跑飞 */

    /* ---------- 2. 串口初始化 ---------- */
    /* TODO【D】：Usart1_Init(115200);    调试串口 PA9/PA10
     *            Usart3_Init(115200);    4G 模块 PB10/PB11
     *            然后打印 banner 确认串口通 */

    /* ---------- 3. 存储初始化 ---------- */
    /* TODO【D】：I2C_GPIO_Init();       24C02
     *            EEPROM_ReadInfo();     读三元组
     *            SPI2_Init();           W25Q128
     *            W25Q128_Init();        检查 ID（0xEF4018）
     *            ID 不对就死循环报错 */

    /* ---------- 4. 4G 模块初始化 ---------- */
    /* TODO【A】：CAT1_GPIO_Init();
     * TODO【C】：MQTT_Init(); */

    /* ---------- 5. 主循环 ---------- */
    while(1)
    {
        /* 5.1 处理 USART3 收到的数据 */
        if(usart3_value.flag)
        {
            /* TODO【D】：U3_EventDispatch(usart3_value.data, usart3_value.count);
             *            usart3_value.flag = 0;
             *            usart3_value.count = 0; */
        }

        /* 5.2 主动事件 */
        ActiveEvent();
    }
}
