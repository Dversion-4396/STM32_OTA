/*=====================================================================
 * 文件：stmflash.c
 * 说明：内部 Flash 读写实现
 * 负责：B
 *
 * ★完成顺序★：
 *   第 1 步：Flash_ErasePage（能擦除，读回全 FF）
 *   第 2 步：Flash_Write（能写入，读回一致）
 *   第 3 步：Flash_SelfTest（跑通就算完成）
 *====================================================================*/
#include "stmflash.h"
#include "usart.h"

/*=====================================================================
 * 函数：Flash_ErasePage
 * 负责：B
 *
 * 实现要点：
 *   1. if(addr % FLASH_PAGE_SIZE) return 1;
 *   2. FLASH_Unlock();
 *   3. FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
 *   4. st = FLASH_ErasePage(addr);
 *   5. FLASH_Lock();
 *   6. return (st == FLASH_COMPLETE) ? 0 : 2;
 *====================================================================*/
u8 Flash_ErasePage(u32 addr)
{
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

    FLASH_Status fl;
    /// 解锁 FLASH
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);

    return 0;
}

/*=====================================================================
 * 函数：Flash_EraseApp
 * 负责：B
 *
 * 实现要点：循环 240 次，从 FLASH_A_ADDR 开始逐页擦除
 *====================================================================*/
u8 Flash_EraseApp(void)
{
    /* TODO【B】：实现 */
    return 1;
}

/*=====================================================================
 * 函数：Flash_Write
 * 负责：B
 *
 * ★最容易写错的地方★
 *
 * 实现要点：
 *   1. if(addr % 2) return 1;       地址必须 2 字节对齐
 *   2. if(len  % 2) return 2;       长度必须偶数
 *   3. FLASH_Unlock(); FLASH_ClearFlag(...);
 *   4. for(i = 0; i < len; i += 2){
 *          halfword = (u16)buf[i] | ((u16)buf[i+1] << 8);      ← 小端！
 *          if(FLASH_ProgramHalfWord(addr + i, halfword) != FLASH_COMPLETE){
 *              FLASH_Lock(); return 3;
 *          }
 *      }
 *   5. FLASH_Lock(); return 0;
 *
 * ★坑：高低字节别拼反★
 *   正确：(u16)buf[i]   | ((u16)buf[i+1] << 8)
 *   错误：(u16)buf[i+1] | ((u16)buf[i]   << 8)
 *====================================================================*/
u8 Flash_Write(u32 addr, u8 *buf, u32 len)
{
    /* TODO【B】：实现 */
    return 1;
}

/*=====================================================================
 * 函数：Flash_Read
 * 负责：B
 *
 * 实现要点：memcpy(buf, (void*)addr, len);  就这么简单
 *====================================================================*/
void Flash_Read(u32 addr, u8 *buf, u32 len)
{
    /* TODO【B】：实现 */
}

/*=====================================================================
 * 函数：Flash_IsBlank
 * 负责：B
 *
 * 实现要点：循环检查每个字节，不等于 0xFF 就返回 0
 *====================================================================*/
u8 Flash_IsBlank(u32 addr, u32 len)
{
    /* TODO【B】：实现 */
    return 0;
}

/*=====================================================================
 * 函数：Flash_SelfTest
 * 负责：B
 *
 * 测试流程（串口打印）：
 *   ===== Flash 测试 =====
 *   擦除成功
 *   擦除后全 FF
 *   写入成功
 *   读回一致
 *   ======================
 *
 * ★测试地址用 FLASH_A_ADDR，不要用 0x08000000★
 *====================================================================*/
u8 Flash_SelfTest(void)
{
    /* TODO【B】：实现 */
    return 1;
}
