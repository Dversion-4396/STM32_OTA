#ifndef __EEPROM_H
#define __EEPROM_H

/* ★ 2026-11-03 修正 ★
 *   原来这里写的是 #include "eeprom.h"（自己包含自己），
 *   导致 u8 / u16 这些类型一个都没定义，编译直接报 5 个 error。
 *   正确做法是包含 common.h（它里面有 stm32f10x.h + u8 等类型别名）
 *   和 i2c.h（EEPROM_WriteByte/ReadByte 要用 I2C_ACK_t）。
 */
#include "common.h"
#include "i2c.h"

/* ==================== 用户配置区：只改这里 ==================== */

/* I2C 7 位设备地址（AT24C02 通常 0x50，A2/A1/A0 接地） */
#define EEPROM_DEV_ADDR         0x50

/*=====================================================================
 * ★★ 地址分配（2026-11-03 重排，务必看完这段再改）★★
 *
 *   原来的地址是 版本号 0x00 / App名 0x10 / 序列号 0x20，
 *   但是 AT24C02 的前 99 字节（0x00 ~ 0x62）已经被 BemfaInfo_CB
 *   结构体占用了（巴法云 UID / Topic / 版本号 / OTA 标志，见 common.h）。
 *   所以版本号写 0x00 会把 valid_flag 和 UID 一起覆盖掉，
 *   结果就是"存完版本号，巴法云参数就失效了，MQTT 再也连不上"。
 *
 *   现在把 App 信息整体挪到 0x70 之后，跟 BemfaInfo_CB 完全错开：
 *
 *   片内地址          长度   内容
 *   ---------------------------------------------------------------
 *   0x00 ~ 0x62       99     BemfaInfo_CB（common.h，不能动）
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
 *   合计用到 0x70 ~ 0xA7 共 56 字节，AT24C02 有 256 字节，够用。
 *===================================================================*/

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

/*=====================================================================
 * ★ 关于 I2C_ACK / I2C_NACK ★
 *   原来这个头文件里写了
 *       #define I2C_NACK 0x00
 *       #define I2C_ACK  0x01
 *   而 i2c.h 里是
 *       typedef enum { I2C_ACK, I2C_NACK } I2C_ACK_t;   // ACK=0, NACK=1
 *   两边值正好相反，而且 #define 会盖掉 enum 的常量名，后果是：
 *       EEPROM_WriteByte() 成功时返回 0（I2C_ACK），
 *       但 eeprom.c 里拿它和 0x01 比 ⇒ 永远判"失败" ⇒ 一个字节都写不进去。
 *   所以这两个宏必须删掉，统一用 i2c.h 的枚举（ACK=0 成功 / NACK=1 失败）。
 *===================================================================*/

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

/* ★ 原来这里是 static EEPROM_Info_t EEPROM_Data;
 *   static 放头文件 = 每个包含它的 .c 各拿到一份独立副本，
 *   eeprom.c 里填的 data，boot.c 里看不到（各是各的）。
 *   改成 extern 声明 + 在 eeprom.c 里定义一份。 */
extern EEPROM_Info_t EEPROM_Data;

/* ---------- 底层：单字节读写 ---------- */
u8 EEPROM_WriteByte(u8 eeprom_addr, u8 reg_addr, u8 data);
u8 EEPROM_ReadByte(u8 eeprom_addr, u8 reg_addr);

/* ---------- 字符串读写 ---------- */
/* 单独读一个字符串：传地址宏和缓冲区即可
 * 例：EEPROM_ReadString(EEPROM_VER_ADDR, buf, sizeof(buf)); */
u8 EEPROM_ReadString(u8 mem_addr, char *buf, u8 buf_size);
u8 EEPROM_WriteString(u8 mem_addr, const char *str, u8 max_len);

/* ---------- u32 读写（版本号数值、大小都用它） ---------- */
u8  EEPROM_WriteU32(u8 mem_addr, u32 value);
u32 EEPROM_ReadU32(u8 mem_addr);

/*=====================================================================
 * ★ 命名说明（避免和 boot.c 里那两个撞名）★
 *   boot.c 里的 EEPROM_ReadInfo() / EEPROM_SaveInfo() 读写的是
 *   BemfaInfoCB（巴法云参数 + OTA 标志，占 0x00~0x62）。
 *   这里新加的是 App 信息（版本名/大小/版本号，占 0x70~0xA7），
 *   所以名字带 App 后缀，两边互不干扰。
 *===================================================================*/

/* 一键读全部 App 信息。返回 0 = 成功，1 = 有字段读失败（失败时字段给默认值） */
u8 EEPROM_LoadAppInfo(EEPROM_Info_t *info);

/* 一键写全部 App 信息。返回 0 = 成功，1 = 有字段写失败 */
u8 EEPROM_SaveAppInfo(EEPROM_Info_t *info);

/* 初始化：探测芯片是否在线。返回 0 = 在线，1 = 不在线 */
u8 EEPROM_Init(void);

#endif
