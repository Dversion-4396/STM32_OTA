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
    u16 i;
    Usart1_SendString("2秒内输入 'w' 进入 BootLoader\r\n");
    for (i = 0; i < 20; i++)
    {

        if (usart1_value.flag && usart1_value.data[0] == 'w')
        {
            usart1_value.flag = 0;
            usart1_value.count = 0;
            Usart1_SendString("BootLoader 已进入\r\n");
            return;
        }
        delay_ms(100);
    }
    if (BemfaInfoCB.OTA_flag == BOOT_STA_O)
    {
        Usart1_SendString("BootLoader 已进入 OTA 模式\r\n");
        BootEventFlag |= OTA_MODEM;
        return;
    }
    Usart1_SendString("BootLoader 已退出跳转到APP\r\n");
    delay_ms(100);
    Load_APP(FLASH_A_ADDR);
}

/*=====================================================================
 * 函数：BootLoader_Info
 * 负责：B
 *
 * 实现要点：打印 4 行菜单
 *   [1] A区擦除
 *   [2] A区下载（Xmodem）

 *====================================================================*/
void BootLoader_Info(void)
{
    Usart1_SendString("\r\n");
    Usart1_SendString("========================================\r\n");
    Usart1_SendString("       BootLoader 菜单\r\n");
    Usart1_SendString("========================================\r\n");
    Usart1_SendString("[1] A区擦除\r\n");
    Usart1_SendString("[2] 跳转到 APP\r\n");
    Usart1_SendString("[3] 查看当前状态\r\n");
    Usart1_SendString("========================================\r\n");
    Usart1_SendString("请输入选项：");
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
    /* buf 提到函数顶部声明：
     *   C89 规定 case 标签后面不能直接跟变量声明，
     *   放这里最省事，也不用给每个 case 套花括号 */
    char buf[80];

    if (data == NULL || len == 0)
    {
        return;
    }

    /* 只读 data[0]，不用看任何 flag：
     *   usart1_value.flag 在 main.c 主循环里已经判断过了
     *   进到本函数就说明"确实收到数据了" */
    switch (data[0])
    {
    /*---------- [1] 擦除 APP 区 ----------
     * 危险操作：擦完 APP 就没了，只能重新烧录或走 OTA
     *------------------------------------------------*/
    case '1':
        Usart1_SendString("\r\n[擦除] 开始擦除 APP 区（约 3 秒）...\r\n");

        if (Flash_EraseApp() == 0)
        {
            Usart1_SendString("[擦除] ★擦除成功，APP 区已清空\r\n");
            Usart1_SendString("[擦除] 注意：此时 APP 已无法运行，请重新下载\r\n");
        }
        else
        {
            Usart1_SendString("[擦除] ★擦除失败\r\n");
        }

        BootLoader_Info();
        break;

    /*---------- [2] 跳转到 APP ----------
     * 调试时最有用：改完 APP 只烧 APP 区，不用重烧 BootLoader
     *------------------------------------------------*/
    case '2':
        Usart1_SendString("\r\n[跳转] 正在跳转到 APP...\r\n");
        delay_ms(100);
        Load_APP(FLASH_A_ADDR);

        /* 能执行到这里说明 Load_APP 内部检查失败后 return 了
         * （APP 区为空 / 栈顶非法 / 复位向量非法） */
        Usart1_SendString("[跳转] ★跳转失败（APP 区可能为空）\r\n");
        BootLoader_Info();
        break;

    /*---------- [3] 查看当前状态 ----------
     * 调试用：一眼看出 EEPROM、W25Q128、OTA 标志是否正常
     *------------------------------------------------*/
    case '3':
        Usart1_SendString("\r\n--- 当前状态 ---\r\n");

        sprintf(buf, "valid_flag  = 0x%02X  %s\r\n",
                BemfaInfoCB.valid_flag,
                (BemfaInfoCB.valid_flag == VALID_BEMFA_DATA) ? "(有效)" : "(未配置)");
        Usart1_SendString(buf);

        sprintf(buf, "OTA_flag    = 0x%08lX  %s\r\n",
                (unsigned long)BemfaInfoCB.OTA_flag,
                (BemfaInfoCB.OTA_flag == BOOT_STA_O) ? "(有待升级固件)" : "(无)");
        Usart1_SendString(buf);

        sprintf(buf, "OTA_firelen = %lu\r\n", (unsigned long)BemfaInfoCB.OTA_firelen);
        Usart1_SendString(buf);

        /* 版本号可能为空（全 0 或全 FF），先判断再打印，避免输出乱码 */
        if (BemfaInfoCB.Version_ABuff[0] != 0 && BemfaInfoCB.Version_ABuff[0] != 0xFF)
        {
            Usart1_SendString("Version     = ");
            Usart1_SendBuffer((char *)BemfaInfoCB.Version_ABuff,
                              strlen((char *)BemfaInfoCB.Version_ABuff));
            Usart1_SendString("\r\n");
        }
        else
        {
            Usart1_SendString("Version     = (未设置)\r\n");
        }

        /* W25Q128 的 JEDEC ID，正常应该是 0xEF4018（Winbond 16MB） */
        sprintf(buf, "W25Q128 ID  = 0x%08lX\r\n", (unsigned long)W25Q128_ReadID());
        Usart1_SendString(buf);

        Usart1_SendString("----------------\r\n");
        BootLoader_Info();
        break;

    /*---------- 其他输入：提示并重显菜单 ----------*/
    default:
        Usart1_SendString("输入错误，请重新输入\r\n");
        BootLoader_Info();
        break;
    }
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

    u32 real_len;
    if (!(BootEventFlag & OTA_MODEM))
    {
        return;
    }
    BootEventFlag &= ~OTA_MODEM;
    if (Boot_VerifyFirmware(BemfaInfoCB.OTA_firelen, &real_len) != 0)
    {
        Usart1_SendString("[BOOT] 校验失败\r\n");
        BemfaInfoCB.OTA_flag = 0;
        EEPROM_SaveInfo();
        Load_APP(FLASH_A_ADDR);
        return;
    }
    Usart1_SendString("[BOOT] 擦除APP区域\r\n");
    Flash_EraseApp();
    if (Boot_CopyFirmware(real_len) != 0)
    {
        Usart1_SendString("[BOOT] 搬运失败\r\n");
        BemfaInfoCB.OTA_flag = 0;
        EEPROM_SaveInfo();
        Load_APP(FLASH_A_ADDR);
        return;
    }
    Usart1_SendString("[BOOT] 搬运成功\r\n");
    BemfaInfoCB.OTA_flag = 0;
    EEPROM_SaveInfo();

    Usart1_SendString("[BOOT] 升级完成正在重置系统\r\n");
    /// 重置系统
    NVIC_SystemReset();
}

/// @brief 加载 APP 到指定地址
/// @param app_addr APP 地址
void Load_APP(u32 app_addr)
{
    Usart1_SendString("LOAD APP START!!!\r\n");
    /// 重定向向量表地址 使用volatile 原因是这个地址可能会被改变
    u32 reset_vec = *(volatile u32 *)(app_addr + 4);
    /// 重定向栈顶地址 使用volatile 原因是这个地址可能会被改变
    u32 stack_top = *(volatile u32 *)app_addr;
    /// 检查重置向量是否有效
    if (reset_vec < app_addr || reset_vec > 0x08080000)
    {
        /// 打印错误信息
        Usart1_SendString("LOAD APP: 无效重置向量\r\n");
        return;
    }
    /// 检查栈顶是否有效
    if ((stack_top & 0x2FFE0000) != 0x20000000)
    {
        /// 打印错误信息
        Usart1_SendString("LOAD APP: 无效栈顶\r\n");
        return;
    }
    /*=================================================================
     * 【修复 2026-10-30】原来的顺序有两个问题：
     *
     *   问题 1：先 USART_DeInit 再打印 "LOAD APP DONE!!!"
     *           → 串口已经关了，这句永远发不出去
     *           → 现象：日志停在 "LOAD APP START!!!"，看着像卡死
     *
     *   问题 2："LOAD APP DONE!!!" 打印了两次（重复代码）
     *
     *   现在顺序：先打印 → 等发完 → 再关外设和中断 → 最后跳转
     *================================================================*/

    /* ① 先把这句话发出去（此时串口还开着） */
    Usart1_SendString("LOAD APP DONE!!!\r\n");

    /* ② 等 50ms，确保这句话的最后一个字节真的发完
     *    为什么必须等：
     *      Usart1_SendByte 只等到 TC=1 就返回，最后一个字节还在移位寄存器里
     *      如果马上 USART_DeInit，最后一个字节会被截断
     *    delay_ms 是 NOP 空循环，不依赖中断，这里可以安全调用 */
    delay_ms(50);

    /* ③ 关串口（APP 会自己重新初始化，这里不清干净会和 APP 冲突） */
    USART_DeInit(USART1);
    USART_DeInit(USART3);

    /* ④ 关全局中断（PRIMASK = 1）
     *    ★注意：APP 的 main() 开头必须调用 __enable_irq() 重新打开，
     *      否则 APP 里的串口接收中断、SysTick 全都不工作★ */
    __disable_irq();

    /* ⑤ 停 SysTick（BootLoader 的 SysTick 配置和 APP 不一样，必须先停） */
    SysTick->CTRL = 0;

    /* ⑥ 重设中断向量表
     *    第二个参数是【偏移量】不是绝对地址：
     *      app_addr - FLASH_BASE_ADDR = 0x08008000 - 0x08000000 = 0x8000 */
    NVIC_SetVectorTable(NVIC_VectTab_FLASH, app_addr - FLASH_BASE_ADDR);

    /* ⑦ 设置主栈指针为 APP 的栈顶
     *    之后所有函数调用都用 APP 自己的栈，不再用 BootLoader 的 */
    __set_MSP(stack_top);

    /* ⑧ 函数指针跳转到 APP 的 Reset_Handler
     *    jump() 之后不会返回（APP 进入自己的 main 死循环） */
    typedef void (*pFunction)(void);
    pFunction jump = (pFunction)reset_vec;
    jump();
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
    u8 buf[28];
    u32 real_len;
    if (fw_total_len < 28)
    {
        Usart1_SendString("BOOT VERIFY FIRMWARE: 无效固件长度\r\n");
        return 1;
    }
    if (out_real_len == NULL)
    {
        Usart1_SendString("BOOT VERIFY FIRMWARE: 无效输出指针\r\n");
        return 1;
    }
    W25Q128_Read(fw_total_len - 28, buf, 28);
    if (buf[0] != 0xCD || buf[1] != 0xEF || buf[2] != 0xCD || buf[3] != 0xEF)
    {
        Usart1_SendString("BOOT VERIFY FIRMWARE: 无效魔数\r\n");
        return 1;
    }
    real_len = (u32)buf[4] | (u32)(buf[5] << 8) | (u32)(buf[6] << 16) | (u32)(buf[7] << 24);
    if (real_len != fw_total_len - 28)
    {
        Usart1_SendString("BOOT VERIFY FIRMWARE: 无效原始长度\r\n");
        return 1;
    }
    if (buf[24] != 0xFF || buf[25] != 0xFF || buf[26] != 0xFF || buf[27] != 0xFF)
    {
        Usart1_SendString("BOOT VERIFY FIRMWARE: 无效校验和\r\n");
        return 1;
    }
    *out_real_len = real_len;
    Usart1_SendString("BOOT VERIFY FIRMWARE: 校验通过\r\n");
    return 0;
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
    u32 i;
    u32 real_len;
    Usart1_SendString("BOOT COPY FIRMWARE START !!!\r\n");
    static u8 buf[FLASH_PAGE_SIZE];
    if (fw_len > FLASH_A_SIZE)
    {
        Usart1_SendString("BOOT COPY FIRMWARE: 无效固件长度\r\n");
        return 1;
    }
    if (fw_len == 0)
    {
        Usart1_SendString("BOOT COPY FIRMWARE: 无效固件长度\r\n");
        return 1;
    }
    for (i = 0; i < fw_len; i += FLASH_PAGE_SIZE)
    {
        /* 算这次搬多少 */
        u32 remain = fw_len - i;
        if (remain > FLASH_PAGE_SIZE)
        {
            real_len = FLASH_PAGE_SIZE;
        }
        else
        {
            real_len = remain;
        }
        W25Q128_Read(i, buf, real_len);
        if (real_len % 2)
        {
            buf[real_len] = 0xFF;
            real_len++;
        }

        if (Flash_Write(FLASH_A_ADDR + i, buf, real_len))
        {
            return 2;
        }
        if (i % 2048 == 0)
        {
            Usart1_SendString(".");
        }
    }
    Usart1_SendString("\r\nBOOT COPY FIRMWARE DONE !!!\r\n");
    return 0;
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
    u8 i;
    u8 *p = (u8 *)&BemfaInfoCB;
    memset(&BemfaInfoCB, 0, sizeof(BemfaInfoCB));
    for (i = 0; i < sizeof(BemfaInfoCB); i++)
    {
        p[i] = EEPROM_ReadByte(EEPROM_AT24C02_ADDR, i);
        delay_ms(5); // 等待 5ms，确保读取完成
    }

    // 判断是否为首次启动
    if (BemfaInfoCB.valid_flag != VALID_BEMFA_DATA) /* 0xDD */
    {
        /* 参数无效：首次上电 / EEPROM 未配置 / 数据损坏 */
        memset(&BemfaInfoCB, 0, sizeof(BemfaInfoCB));
        BemfaInfoCB.valid_flag = 0; /* 保持无效状态 */

        Usart1_SendString("[EEPROM] 参数无效，需要重新配置\r\n");
        return;
    }
    else
    {
        Usart1_SendString("[EEPROM] 参数有效\r\n");
    }
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
    u8 i;
    u8 *p = (u8 *)&BemfaInfoCB;
    for (i = 0; i < sizeof(BemfaInfoCB); i++)
    {
        if (EEPROM_WriteByte(EEPROM_AT24C02_ADDR, i, p[i]) != I2C_ACK)
        {
            return 1;
        }
        delay_ms(5);
    }
    Usart1_SendString("EEPROM SAVE INFO DONE !!!\r\n");
    return 0;
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
    /* 临时：把 UID 和主题写进 EEPROM（只跑一次，写完注释掉） */
    memset(&BemfaInfoCB, 0, sizeof(BemfaInfoCB));
    BemfaInfoCB.valid_flag = VALID_BEMFA_DATA;
    strcpy((char *)BemfaInfoCB.UID_Buff, "a0f83060ae0a4750a09d1607611f3b32");
    strcpy((char *)BemfaInfoCB.Topic_Buff, "123"); 
    strcpy((char *)BemfaInfoCB.Version_ABuff, "1.0.0");
    BemfaInfoCB.OTA_firelen = 0;
    BemfaInfoCB.OTA_flag = 0;
    EEPROM_SaveInfo();

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
