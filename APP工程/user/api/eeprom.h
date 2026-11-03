#ifndef _EEPROM_H
#define _EEPROM_H

#include "common.h"
#include "i2c.h"


/**
 * @brief 写入一个字节数据到 EEPROM AT24C02 存储芯片中，
 *
 * @param eeprom_addr EEPROM AT24C02 存储芯片 I2C 设备地址
 * @param reg_addr    目标写入到存储芯片寄存器地址
 * @param data        目标写入的 1 字节数据内容
 * @return 对应当前从机应答信息。I2C_ACK(0)=成功，I2C_NACK(1)=失败
 */
u8 EEPROM_WriteByte(u8 eeprom_addr, u8 reg_addr, u8 data);

/**
 * @brief 从 EEPROM AT24C02 存储芯片中读取一个字节数据
 *
 * @param eeprom_addr EEPROM AT24C02 存储芯片 I2C 设备地址
 * @param reg_addr    目标读取数据的寄存器地址
 * @return 对应读取到的数据内容
 */
u8 EEPROM_ReadByte(u8 eeprom_addr, u8 reg_addr);

/**
 * @brief 从 EEPROM 读出整个 BemfaInfoCB 结构体（99 字节，占 0x00~0x62）
 * @return 0 = 参数有效，1 = 参数无效（首次上电 / 数据损坏）
 */
u8 EEPROM_ReadInfo(void);

/**
 * @brief 把整个 BemfaInfoCB 结构体（99 字节）写进 EEPROM
 * @return 0 = 成功，1 = 失败
 */
u8 EEPROM_SaveInfo(void);


/*=====================================================================
 * ★★ 以下为 App 信息区（2026-11-03 新增）★★
 *   这一段在 APP 工程和 BootLoader 工程里【必须逐字节完全一致】。
 *   APP 启动时把自己的版本名/版本号/大小写进来，
 *   BootLoader 的菜单 [3] 和 [4] 再读出来显示、做版本对比。
 *
 *   地址分配（跟 BemfaInfo_CB 占用的 0x00~0x62 完全错开）：
 *
 *   片内地址          长度   内容
 *   ---------------------------------------------------------------
 *   0x00 ~ 0x62       99     BemfaInfo_CB（巴法云参数 + OTA 标志）
 *   0x63 ~ 0x6F       13     空闲
 *   0x70 ~ 0x77        8     版本名字符串 "1.0.0\0"
 *   0x78 ~ 0x87       16     App 名字符串
 *   0x88 ~ 0x97       16     序列号字符串
 *   0x98 ~ 0x9B        4     APP 镜像大小（u32，字节）
 *   0x9C ~ 0x9F        4     当前版本号数值（u32，1.0.0 -> 10000）
 *   0xA0 ~ 0xA3        4     待升级版本号数值（u32，云端下发的）
 *   0xA4 ~ 0xA7        4     待升级固件长度（u32，字节）
 *   0xA8 ~ 0xFF       88     空闲
 *   ---------------------------------------------------------------
 *===================================================================*/

/* 芯片的 I2C 设备地址别名。
 * common.h 里叫 EEPROM_AT24C02_ADDR（0x50），
 * BootLoader 工程里叫 EEPROM_DEV_ADDR —— 两个名字指同一个东西，
 * 这里补个别名，让两个工程的 eeprom.c 能用同一份源码。 */
#define EEPROM_DEV_ADDR         EEPROM_AT24C02_ADDR

/* ---------- App 信息区的"已初始化"标记 ----------
 *
 * ★ 为什么需要这个标记（2026-11-03 真板踩出来的）★
 *   0x70 之后的区域第一次上电时是芯片出厂状态（全 0xFF），
 *   但如果是旧工程用过的板子，那里可能残留着旧数据。
 *   EEPROM_ReadString 的判据是"在 buf_size 内遇到 '\0' 就算读成功"，
 *   残留数据里随便一个 0x00 都会被当成字符串结尾 ⇒ 读出乱码还判成功。
 *   真板实测：菜单 [3] 显示 "APP 名 : "（空）、"序列号 : ·"（乱码）。
 *
 *   加一个 4 字节魔数放在最后写：魔数对得上才认为整块 App 信息可信，
 *   对不上就把所有字段重置成出厂默认值再写一遍。
 *   魔数最后写，这样"写到一半掉电"时魔数还是旧的 ⇒ 下次仍判无效 ⇒ 重写。
 * ----------------------------------------------------------------*/
#define EEPROM_APPINFO_MAGIC_ADDR   0xB0
#define EEPROM_APPINFO_MAGIC        0x41505049UL    /* 'A''P''P''I' */
/* ★ 0xB0 不是随便挑的：new_ver 字符串从 0xA8 起占 EEPROM_VER_SIZE(8) 字节，
 *   到 0xAF 结束。魔数原来也写 0xA8，正好把 new_ver 覆盖成 'IPPA1'
 *   （0x41505049 的小端字节序 49 50 50 41）——2026-11-03 真板踩出来的。 */

/* ---------- 版本名字符串 ---------- */
#define EEPROM_VER_ADDR         0x70        /* 版本名存放起始地址 */
#define EEPROM_VER_SIZE         8           /* 区域最大字节数（含 '\0'） */
#define EEPROM_VER_STR          "1.0.0"     /* 出厂默认版本名 */
/* ---------- App 名称 ---------- */
#define EEPROM_APP_ADDR         0x78        /* App 名存放起始地址 */
#define EEPROM_APP_SIZE         16          /* 区域最大字节数（含 '\0'） */
#define EEPROM_APP_STR          "STM32 OTA" /* 出厂默认 App 名（纯 ASCII，避免 UTF-8 被截断） */

/* ---------- 序列号（预留） ---------- */
#define EEPROM_SN_ADDR          0x88
#define EEPROM_SN_SIZE          16
#define EEPROM_SN_STR           "SN00000001"

/* ---------- APP 镜像大小（u32） ---------- */
#define EEPROM_SIZE_ADDR        0x98

/* ---------- 当前版本号数值（u32，大版本*10000+小版本*100+修订号） ---------- */
#define EEPROM_VERNUM_ADDR      0x9C

/* ---------- 待升级版本号 / 待升级固件长度（u32） ---------- */
#define EEPROM_NEWVER_ADDR      0xA0
#define EEPROM_NEWLEN_ADDR      0xA4
/* ---------- 待升级版本名字符串（云端下发的新版本名） ---------- */
#define EEPROM_NEWVER_STR_ADDR  0xA8

/* 一次读回所有 App 信息用这个结构体 */
typedef struct
{
    char ver[EEPROM_VER_SIZE];              /* 版本名，如 "1.0.0" */
    char app[EEPROM_APP_SIZE];              /* App 名 */
    char sn[EEPROM_SN_SIZE];                /* 序列号 */
    u32  app_size;                          /* APP 镜像大小（字节） */
    u32  ver_num;                           /* 当前版本号数值 */
    u32  new_ver_num;                       /* 待升级版本号数值（0 = 没有待升级） */
    u32  new_len;                           /* 待升级固件长度（字节） */
    char new_ver[EEPROM_VER_SIZE];          /* 待升级版本名，如 "1.0.1" */
} EEPROM_Info_t;

/* 全局唯一一份（定义在 eeprom.c，不要放 static 进头文件，
 * 否则每个 .c 各拿到一份独立副本，改了这边那边看不到） */
extern EEPROM_Info_t EEPROM_Data;

/* ---------- 字符串读写 ---------- */
u8 EEPROM_ReadString(u8 mem_addr, char *buf, u8 buf_size);
u8 EEPROM_WriteString(u8 mem_addr, const char *str, u8 max_len);

/* ---------- u32 读写 ---------- */
u8  EEPROM_WriteU32(u8 mem_addr, u32 value);
u32 EEPROM_ReadU32(u8 mem_addr);

/* ---------- App 信息一键读写 ---------- */
/* 返回 0 = 全部成功，1 = 有字段失败（读失败时字段已填默认值） */
u8 EEPROM_LoadAppInfo(EEPROM_Info_t *info);
u8 EEPROM_SaveAppInfo(EEPROM_Info_t *info);

/* ---------- 初始化探测 ---------- */
/* 返回 0 = 在线，1 = 不在线 */
u8 EEPROM_Init(void);

#endif
