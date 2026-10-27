/*=====================================================================
 * 文件：boot.c
 * 说明：BootLoader 主逻辑实现
 * 负责：B
 *
 * ★完成顺序建议★：
 *   第 1 步：EEPROM_ReadInfo + AliCer_Inspect + Printf_AliCer
 *   第 2 步：BootLoader_Info（菜单能打印）
 *   第 3 步：BootLoader_Branch（三种分支能走）
 *   第 4 步：Load_APP（能跳转）← 基础功能通了
 *   第 5 步：Boot_CopyFirmware（能搬固件）
 *   第 6 步：Boot_VerifyFirmware（加分项）
 *====================================================================*/
#include "boot.h"
#include "stmflash.h"
#include "usart.h"
#include "delay.h"
#include "eeprom.h"
#include "spi.h"

u32 BootEventFlag = 0;

/*=====================================================================
 * 函数：BootLoader_Branch
 * 负责：B
 *
 * 实现要点：
 *   u16 i;
 *   1. 打印 "2秒内输入 'w' 进入 BootLoader"
 *   2. for(i = 0; i < 20; i++){
 *          if(usart1_value.data[0] == 'w') return;
 *          delay_ms(100);
 *      }
 *   3. if(AliInfoCB.OTA_flag == BOOT_STA_O){
 *          BootEventFlag |= OTA_MODEM;
 *          return;
 *      }
 *   4. Load_APP(FLASH_A_ADDR);
 *====================================================================*/
void BootLoader_Branch(void)
{
    /* TODO【B】：实现 */
}

/*=====================================================================
 * 函数：BootLoader_Info
 * 负责：B
 *
 * 实现要点：打印 4 行菜单
 *   [1] A区擦除
 *   [2] A区下载（Xmodem）
 *   [3] 设置阿里云三元组证书信息
 *   [4] 查询阿里云三元组证书信息
 *====================================================================*/
void BootLoader_Info(void)
{
    /* TODO【B】：实现 */
}

/*=====================================================================
 * 函数：BootLoader_Event
 * 负责：B
 *
 * 实现要点：
 *   if(BootEventFlag == 0){
 *       '1' → Flash_EraseApp(); BootLoader_Info();
 *       '2' → Xmodem（可选）
 *       '3' → BootEventFlag |= ALIYUN_CER; 打印格式提示
 *       '4' → Printf_AliCer(); BootLoader_Info();
 *   }
 *   else if(BootEventFlag & ALIYUN_CER){
 *       if(AliCer_Inspect(data) == 0){
 *           AliInfoCB.valid_cer = VALID_ALICER_DATA;
 *           EEPROM_SaveInfo();
 *           Printf_AliCer();
 *           BootEventFlag &= ~ALIYUN_CER;
 *           BootLoader_Info();
 *       }else 打印 "格式错误，重新输入"
 *   }
 *====================================================================*/
void BootLoader_Event(u8 *data, u16 len)
{
    /* TODO【B】：实现 */
}

/*=====================================================================
 * 函数：ActiveEvent
 * 负责：B
 *
 * 实现要点：
 *   u32 real_len;
 *   if(!(BootEventFlag & OTA_MODEM)) return;
 *   BootEventFlag &= ~OTA_MODEM;
 *
 *   1. 校验固件（如果做了 Boot_VerifyFirmware）
 *      失败：清 OTA_flag，存 EEPROM，跳回旧 APP（避免变砖）
 *   2. Flash_EraseApp()
 *   3. Boot_CopyFirmware(real_len)
 *   4. 清 OTA_flag，存 EEPROM
 *   5. NVIC_SystemReset()
 *====================================================================*/
void ActiveEvent(void)
{
    /* TODO【B】：实现 */
}

/*=====================================================================
 * 函数：Load_APP
 * 负责：B
 *
 * ★★ 最容易出错的地方，6 步一个不能少 ★★
 *
 *   typedef void (*pFunction)(void);
 *   pFunction jump;
 *   u32 stack_top, reset_vec;
 *
 *   1. stack_top = *(volatile u32*)app_addr;
 *      if((stack_top & 0x2FFE0000) != 0x20000000) return;
 *
 *   2. reset_vec = *(volatile u32*)(app_addr + 4);
 *      if(reset_vec < app_addr || reset_vec > 0x08080000) return;
 *
 *   3. __disable_irq();
 *      SysTick->CTRL = 0;
 *      USART_DeInit(USART1);
 *      USART_DeInit(USART3);
 *
 *   4. NVIC_SetVectorTable(NVIC_VectTab_FLASH, app_addr - FLASH_BASE_ADDR);
 *      ← ★第二参数是偏移量，不是绝对地址★
 *
 *   5. __set_MSP(stack_top);
 *
 *   6. jump = (pFunction)reset_vec;
 *      jump();
 *
 * ★漏掉任何一步的后果★：
 *   不检查栈顶   → 跳到空 Flash 会 HardFault
 *   不关中断     → BootLoader 中断触发，跳到错误向量表
 *   不设向量表   → APP 的中断全跑飞
 *====================================================================*/
void Load_APP(u32 app_addr)
{
    /* TODO【B】：实现 */
}

/*=====================================================================
 * 函数：Boot_VerifyFirmware
 * 负责：B（加分项）
 *
 * 28 字节尾缀格式（在固件末尾）：
 *   +0   4 字节  魔数 CD EF CD EF（小端读作 0xEFCDEFCD）
 *   +4   4 字节  原始 bin 长度（小端）
 *   +8  16 字节  MD5 裸摘要
 *   +24  4 字节  FF FF FF FF
 *
 * 实现要点：
 *   1. 读 (fw_total_len - 28) 处 4 字节，比对 0xEFCDEFCD
 *   2. 读 (fw_total_len - 24) 处 4 字节，比对 (fw_total_len - 28)
 *   3. 读 (fw_total_len - 20) 处 16 字节 MD5
 *   4. 逐块读固件算 MD5（源码从 36 号工程 utils_md5.c 复制）
 *   5. 比对
 *====================================================================*/
u8 Boot_VerifyFirmware(u32 fw_total_len, u32 *out_real_len)
{
    /* TODO【B】：实现 */
    return 1;
}

/*=====================================================================
 * 函数：Boot_CopyFirmware
 * 负责：B
 *
 * 实现要点：
 *   u32 i, remain;
 *   u8  buf[FLASH_PAGE_SIZE];
 *
 *   1. if(fw_len > FLASH_A_SIZE) return 1;
 *   2. for(i = 0; i < fw_len; i += FLASH_PAGE_SIZE){
 *          remain = fw_len - i;
 *          if(remain > FLASH_PAGE_SIZE) remain = FLASH_PAGE_SIZE;
 *          W25Q128_Read(i, buf, remain);
 *          if(remain % 2) buf[remain] = 0xFF;      补齐偶数
 *          if(Flash_Write(FLASH_A_ADDR + i, buf, (remain + 1) & ~1)) return 2;
 *          打印进度
 *      }
 *   3. return 0;
 *====================================================================*/
u8 Boot_CopyFirmware(u32 fw_len)
{
    /* TODO【B】：实现 */
    return 1;
}

/*=====================================================================
 * 函数：EEPROM_ReadInfo
 * 负责：B
 *
 * 实现要点：
 *   u8 *p = (u8*)&AliInfoCB;
 *   memset(&AliInfoCB, 0, sizeof(AliInfoCB));
 *   for(i = 0; i < sizeof(AliInfoCB); i++){
 *       p[i] = EEPROM_ReadByte(EEPROM_AT24C02_ADDR, i);
 *   }
 *====================================================================*/
void EEPROM_ReadInfo(void)
{
    /* TODO【B】：实现 */
}

/*=====================================================================
 * 函数：EEPROM_SaveInfo
 * 负责：B
 *
 * 实现要点：
 *   u8 *p = (u8*)&AliInfoCB;
 *   for(i = 0; i < sizeof(AliInfoCB); i++){
 *       if(EEPROM_WriteByte(EEPROM_AT24C02_ADDR, i, p[i]) != I2C_ACK) return 1;
 *       delay_ms(5);        ★AT24C02 每写一字节要等 5ms★
 *   }
 *   return 0;
 *====================================================================*/
u8 EEPROM_SaveInfo(void)
{
    /* TODO【B】：实现 */
    return 1;
}

/*=====================================================================
 * 函数：AliCer_Inspect
 * 负责：B
 *
 * 实现要点：
 *   if(sscanf((char*)data, "/%[^/]/%[^/]/%[^/]/%[^/]/",
 *             AliInfoCB.ProductKeyBuff,
 *             AliInfoCB.DeviceNameBuff,
 *             AliInfoCB.DeviceSecretBuff,
 *             AliInfoCB.Version_ABuff) != 4) return 5;
 *
 *   if(strlen((char*)AliInfoCB.ProductKeyBuff)   != PRODUCTKEY_LEN)   return 1;
 *   if(strlen((char*)AliInfoCB.DeviceNameBuff)   >  DEVICENAME_LEN)   return 2;
 *   if(strlen((char*)AliInfoCB.DeviceSecretBuff) != DEVICESECRET_LEN) return 3;
 *   if(strlen((char*)AliInfoCB.Version_ABuff)    != VERSION_LEN)      return 4;
 *   return 0;
 *====================================================================*/
u8 AliCer_Inspect(u8 *data)
{
    /* TODO【B】：实现 */
    return 5;
}

/*=====================================================================
 * 函数：Printf_AliCer
 * 负责：B
 *
 * 实现要点：打印 4 个字段
 *====================================================================*/
void Printf_AliCer(void)
{
    /* TODO【B】：实现 */
}
