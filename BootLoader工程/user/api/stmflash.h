
#ifndef __STMFLASH_H
#define __STMFLASH_H

#include "common.h"

/// @brief 擦除一页（2KB）
/// @param addr 页起始地址（必须 2KB 对齐）
/// @return 0=成功  1=地址未对齐  2=擦除失败
u8 Flash_ErasePage(u32 addr);


/// @brief 擦除整个 A 区（240 页 = 480KB）
/// @param 无
/// @return 0=成功  1=失败
u8 Flash_EraseApp(void);

/// @brief 写 Flash（自动按半字拆分）
/// @param addr 目标地址（必须 2 字节对齐）
/// @param buf 数据
/// @param len 字节数（必须为偶数）
/// @return 0=成功  1=地址未对齐  2=长度非偶数  3=写失败
/// @note 负责：B
/// @note 说明：搬移固件前必须先调用
/// @note 说明：Flash 可以直接当内存写，用 memcpy 即可
/// @note 说明：★小端拼装：halfword = buf[i] | (buf[i+1] << 8)★
u8 Flash_Write(u32 addr, u8 *buf, u32 len);

/*---------------------------------------------------------------------
 * 函数：Flash_Read
 * 功能：读 Flash
 * 参数：addr - 地址；buf - 缓冲区；len - 长度
 * 返回：无
 * 负责：B
 * 说明：Flash 可以直接当内存读，用 memcpy 即可
 *-------------------------------------------------------------------*/
void Flash_Read(u32 addr, u8 *buf, u32 len);

/// @brief 检查 Flash 是否为全 0xFF
/// @param addr 检查地址
/// @param len 字节数
/// @return 0 不是全 0xFF
/// @return 1 是全 0xFF
/// @note 实现要点：循环检查每个字节，不等于 0xFF 就返回 0
u8 Flash_IsBlank(u32 addr, u32 len);

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
u8 Flash_SelfTest(void);

#endif /* __STMFLASH_H */
