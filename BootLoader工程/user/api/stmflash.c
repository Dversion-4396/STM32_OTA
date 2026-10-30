
#include "stmflash.h"
#include "usart.h"
/// @brief 擦除指定页
/// @param addr 页地址
/// @return 1：地址无效
/// @return 0 成功
/// @return 2：失败
u8 Flash_ErasePage(u32 addr)
{
    /// 定义 FLASH 状态变量
    FLASH_Status fl;
    /// 检查地址是否对齐
    if (addr % FLASH_PAGE_SIZE)
    {
        return 1;
    }
    /// 检查地址是否在有效范围内
    if (addr < FLASH_A_ADDR || addr >= FLASH_A_ADDR + FLASH_A_SIZE)
    {
        return 1;
    }
    /// 解锁 FLASH
    FLASH_Unlock();
    /// 清除标志位
    /// 1. EOP：擦除完成标志位
    /// 2. PGERR：页错误标志位
    /// 3. WRPRTERR：写保护错误标志位
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    fl = FLASH_ErasePage(addr);
    /// 锁 FLASH
    if (fl != FLASH_COMPLETE)
    {
        FLASH_Lock();
        return 2;
    }

    FLASH_Lock();
    return 0;
}

/// @brief 擦除整个 A 区（240 页 = 480KB）
/// @param 无
/// @return 0=成功  1=失败
u8 Flash_EraseApp(void)
{
    u16 i;
    for (i = 0; i < 240; i++)
    {
        if (Flash_ErasePage(FLASH_A_ADDR + i * FLASH_PAGE_SIZE) != 0)
        {
            return 1;
        }
    }
    return 0;
}

/// @brief 写 Flash（自动按半字拆分）
/// @param addr 目标地址（必须 2 字节对齐）
/// @param buf 数据
/// @param len 字节数（必须为偶数）
/// @return 0 成功
/// @return 1：地址未对齐
/// @return 2：长度非偶数
/// @return 3：写失败
u8 Flash_Write(u32 addr, u8 *buf, u32 len)
{
    u32 i;
    u16 halfword;
    if (addr < FLASH_A_ADDR || addr >= FLASH_A_ADDR + FLASH_A_SIZE)
    {
        return 1;
    }
    if (addr % 2)
    {
        return 1;
    }
    if (len % 2)
    {
        return 2;
    }
    /// 解锁 FLASH
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    for (i = 0; i < len; i += 2)
    {
        halfword = ((u16)buf[i + 1] << 8) | (u16)buf[i];
        if (FLASH_ProgramHalfWord(addr + i, halfword) != FLASH_COMPLETE)
        {
            FLASH_Lock();
            return 3;
        }
    }
    //  锁 FLASH
    FLASH_Lock();
    return 0;
}

/// @brief 读 Flash
/// @param addr 读取地址
/// @param buf 缓冲区
/// @param len 字节数
void Flash_Read(u32 addr, u8 *buf, u32 len)
{
    memcpy(buf, (void *)addr, len);
}

/// @brief 检查 Flash 是否为全 0xFF
/// @param addr 检查地址
/// @param len 字节数
/// @return 0 不是全 0xFF
/// @return 1 是全 0xFF
/// @note 实现要点：循环检查每个字节，不等于 0xFF 就返回 0
u8 Flash_IsBlank(u32 addr, u32 len)
{
    u32 i;
    u8 *buf = (u8 *)addr;
    for (i = 0; i < len; i++)
    {
        if (buf[i] != 0xFF)
        {
            return 0;
        }
    }

    return 1;
}

/// @brief Flash 自检
/// @param 无
/// @return 0 成功
/// @return 1 失败
/// @note 测试流程（串口打印）：
///   ===== Flash 测试 =====
///   擦除成功
///   擦除后全 FF
///   写入成功
///   读回一致
///   ======================
/// @note ★测试地址必须用 A 区（0x08008000），不能用 0x08000000★
/// @note 否则会把 BootLoader 自己擦了

u8 Flash_SelfTest(void)
{
    u8 i;
    u8 read_buf[16];
    u8 write_buf[16] = "Hello Flash";
    Usart1_SendString("===== Flash 测试 =====\r\n");
    Flash_ErasePage(FLASH_A_ADDR);
    if (Flash_IsBlank(FLASH_A_ADDR, FLASH_PAGE_SIZE) == 0)
    {
        Usart1_SendString("擦除失败\r\n");
        return 1;
    }
    Usart1_SendString("擦除成功\r\n");
    if (Flash_Write(FLASH_A_ADDR, write_buf, 256) != 0)
    {
        Usart1_SendString("写入失败\r\n");
        return 1;
    }
    Usart1_SendString("写入数据：");
    Usart1_SendBuffer(write_buf, 16);
    Usart1_SendString("\r\n");
    Usart1_SendString("写入成功\r\n");
    Flash_Read(FLASH_A_ADDR, read_buf, 16);
    for (i = 0; i < 16; i++)
    {
        if (read_buf[i] != write_buf[i])
        {
            Usart1_SendString("读回不一致\r\n");
            return 1;
        }
    }
    Usart1_SendString("读回数据：");
    Usart1_SendBuffer(read_buf, 16);
    Usart1_SendString("\r\n");
    Usart1_SendString("读回一致\r\n");
    Flash_ErasePage(FLASH_A_ADDR);
    if (Flash_IsBlank(FLASH_A_ADDR, FLASH_PAGE_SIZE) == 0)
    {
        Usart1_SendString("擦除失败\r\n");
        return 1;
    }
    Usart1_SendString("测试完成\r\n");
    return 0;
}
