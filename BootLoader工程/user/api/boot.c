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

// bootloader 事件标志位
u32 BootEventFlag = 0;

// OTA 擦除标志位
static u8 BootClear_Flag = 0;

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

    // (2) 判断是否主动进入 Bootloder
    Usart1_SendString("是否进入 进入 BootLoader 'Y' \r\n");
    for (i = 0; i < 50; i++)
    {

        if (usart1_value.flag && usart1_value.data[0] == 'Y')
        {
            usart1_value.flag = 0;
            usart1_value.count = 0;
            Usart1_SendString("已进入 bootloder \r\n");
            usart1_value.data[0] = 0;
            return;
        }
        delay_ms(100);

    }

    // (3) 如果未主动进入则判断是否有 OTA 事件
    if (BemfaInfoCB.OTA_flag == BOOT_STA_O)
    {
        Usart1_SendString("\r\n");
        BootEventFlag |= OTA_MODEM;
        Usart1_SendString("有 Bootloder 升级事件 \r\n");

        return;
    }
    Usart1_SendString("BootLoader 已退出跳转到APP\r\n");
    delay_ms(100);

    // (4) 如果没有进入 BootLoader，也没有待升级固件，直接跳转到 APP
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
    Usart1_SendString("[3] 查看当前 APP（版本名/版本号/大小）\r\n");
    Usart1_SendString("[4] OTA 升级搬运（从 W25Q128 搬到 A 区）\r\n");
    Usart1_SendString("[5] 查看回退备份（上一次升级前的版本）\r\n");
    Usart1_SendString("[6] 回退到备份版本（把备份搬回 A 区）\r\n");
    Usart1_SendString("[7] 备份当前 APP（手动刷新回退点）\r\n");
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

    /* ★ 空指针/空长度保护（2026-11-03 补回）★
     *   下面第一句就是 data[0]，如果 data 是 NULL 会直接硬件异常；
     *   如果 len 是 0 说明中断里没收到任何字节，也不该当命令处理。 */
    if (data == NULL || len == 0)
    {
        return;
    }

    // (9) 提取第一个字节作为命令，之所以不作标志位判断是因为在外部判断过了
    switch (data[0])
    {
    case '1':
        // (9.1) 擦除 APP 区域
        Usart1_SendString("\r\n[擦除] 开始擦除 APP 区（约 3 秒）...\r\n");

        if (Flash_EraseApp() == 0)
        {
            Usart1_SendString("[擦除] 擦除成功，APP 区已清空\r\n");
            Usart1_SendString("[擦除] 注意：此时 APP 已无法运行，请重新下载\r\n");
            BootClear_Flag = 1;
        }
        else
        {
            Usart1_SendString("[擦除] 擦除失败\r\n");
            Usart1_SendString("[擦除] 注意可能未完全擦除但已擦除部分内容\r\n");

        }

        BootLoader_Info();
        break;

    case '2':
        // (9.2) 跳转到 APP

        if (BootClear_Flag == 1)
        {

            // (9.2.1) 如果 APP 区已被擦除，提示无法跳转
            Usart1_SendString("\r\n[跳转] 警告: APP 区已被擦除，无法跳转\r\n");
            BootLoader_Info();
            break;
        }

        Usart1_SendString("\r\n[跳转] 正在跳转到 APP...\r\n"); 
        delay_ms(100);
        // (10) Bootloder 工作完成，重大分支
        Load_APP(FLASH_A_ADDR);

        // (10.1) 执行到这里说明跳转失败，可能 APP 区为空或损坏
        Usart1_SendString("[跳转] ★跳转失败(APP 区可能为空)\r\n");
        BootLoader_Info();
        break;

    case '3':
        // (9.3) 查看当前 APP 信息
        // ★ 数据从哪来 ★
        //   APP 每次启动时会把"自己是谁、什么版本、多大"写进 EEPROM，
        //   这里读的就是那份数据。如果 APP 从来没跑过（新板子/刚擦过），
        //   EEPROM_LoadAppInfo 会返回 1 并填上出厂默认值，
        //   所以下面既打印内容，也把"是不是真的存过"单独标出来。
        {
            u8 ret = EEPROM_LoadAppInfo(&EEPROM_Data);

            Usart1_SendString("\r\n--- 当前 APP 信息 ---\r\n");

            sprintf(buf, "  版本名    : %s\r\n", EEPROM_Data.ver);
            Usart1_SendString(buf);

            sprintf(buf, "  版本号    : %lu\r\n", (unsigned long)EEPROM_Data.ver_num);
            Usart1_SendString(buf);

            sprintf(buf, "  APP 名    : %s\r\n", EEPROM_Data.app);
            Usart1_SendString(buf);

            sprintf(buf, "  序列号    : %s\r\n", EEPROM_Data.sn);
            Usart1_SendString(buf);

            sprintf(buf, "  APP 大小  : %lu 字节 (%lu KB)\r\n",
                    (unsigned long)EEPROM_Data.app_size,
                    (unsigned long)(EEPROM_Data.app_size / 1024));
            Usart1_SendString(buf);

            /* ---- 待升级信息（有没有云端推过来的新固件） ---- */
            Usart1_SendString("--- 待升级信息 ---\r\n");
            if (EEPROM_Data.new_len == 0)
            {
                Usart1_SendString("  没有待升级固件\r\n");
            }
            else
            {
                sprintf(buf, "  新版本名  : %s\r\n",
                        (EEPROM_Data.new_ver[0] != 0) ? EEPROM_Data.new_ver : "(未知)");
                Usart1_SendString(buf);

                sprintf(buf, "  新版本号  : %lu\r\n", (unsigned long)EEPROM_Data.new_ver_num);
                Usart1_SendString(buf);

                sprintf(buf, "  新固件长度: %lu 字节 (%lu KB)\r\n",
                        (unsigned long)EEPROM_Data.new_len,
                        (unsigned long)(EEPROM_Data.new_len / 1024));
                Usart1_SendString(buf);

                /* 版本号比较：编码规则是 大版本*10000 + 小版本*100 + 修订号 */
                if (EEPROM_Data.new_ver_num > EEPROM_Data.ver_num)
                {
                    Usart1_SendString("  → 版本号比当前新，可以升级\r\n");
                }
                else
                {
                    Usart1_SendString("  → 版本号不比当前新，输入 [4] 也会拒绝\r\n");
                }
            }

            /* ---- OTA 标志 + 数据来源 ---- */
            Usart1_SendString("--- 状态 ---\r\n");
            sprintf(buf, "  OTA_flag  : 0x%08lX (%s)\r\n",
                    (unsigned long)BemfaInfoCB.OTA_flag,
                    (BemfaInfoCB.OTA_flag == BOOT_STA_O) ? "有待升级事件" : "无");
            Usart1_SendString(buf);

            sprintf(buf, "  W25Q128 ID: 0x%06lX\r\n", (unsigned long)W25Q128_ReadID());
            Usart1_SendString(buf);

            if (ret == 0)
            {
                Usart1_SendString("  数据来源  : EEPROM 已存过（APP 写过）\r\n");
            }
            else
            {
                Usart1_SendString("  数据来源  : EEPROM 没存过，上面是出厂默认值\r\n");
            }

            Usart1_SendString("----------------\r\n");
            BootLoader_Info();
        }
        break;

    case '4':
        // (9.4) OTA 升级搬运：把 W25Q128 里的新固件搬进片内 APP 区
        //
        // ★ 为什么单独给一个 [4]，而不是像以前那样一上电自动搬 ★
        //   自动搬的问题：一旦 W25Q128 里是半截固件，开机就自动擦掉好 APP，
        //   然后搬一半失败 ⇒ 变砖，连菜单都进不来。
        //   改成手动触发后，用户可以先 [3] 看清楚版本号和大小，
        //   确认没问题再按 [4]，出问题还能 [2] 跳回旧 APP。
        //
        // ★ 2026-11-03 改：真正的搬运逻辑挪进 Boot_DoUpgrade()，
        //   这里只负责"做判断 + 报结果"，和 ActiveEvent() 共用一份实现。
        {
            u8 ret;
            u8 has_image;

            Usart1_SendString("\r\n[升级] 开始 OTA 升级检查...\r\n");

            /* ---- 第 1 步：先看 EEPROM 里有没有待升级记录 ---- */
            (void)EEPROM_LoadAppInfo(&EEPROM_Data);

            /* ---- 第 2 步：W25Q128 里到底有没有固件 ----
             *   判据是 EEPROM 记录的 new_len，不是 OTA_flag。
             *   为什么：OTA_flag 只表示"云端通知过来了"，
             *   而 new_len 表示"APP 真的把固件下载进 W25Q128 了"。
             *   只有 flag 没有 len ⇒ 下载还没完成，搬了就是搬空气。 */
            has_image = (EEPROM_Data.new_len > 0) ? 1 : 0;

            if (has_image == 0)
            {
                Usart1_SendString("[升级] ★EEPROM 里没有待升级固件记录\r\n");
                Usart1_SendString("[升级] 说明 APP 还没下载完成，或下载失败了\r\n");
                BootLoader_Info();
                break;
            }

            sprintf(buf, "[升级] 待升级固件长度 = %lu 字节\r\n",
                    (unsigned long)EEPROM_Data.new_len);
            Usart1_SendString(buf);

            /* ---- 第 3 步：版本对比 ----
             *   编码规则 大版本*10000 + 小版本*100 + 修订号，
             *   所以直接比 u32 大小就等价于比版本先后。
             *   用字符串比会错："1.10.0" < "1.9.0"（字典序）。 */
            sprintf(buf, "[升级] 当前版本号 = %lu，新版本号 = %lu\r\n",
                    (unsigned long)EEPROM_Data.ver_num,
                    (unsigned long)EEPROM_Data.new_ver_num);
            Usart1_SendString(buf);

            if (EEPROM_Data.new_ver_num <= EEPROM_Data.ver_num)
            {
                Usart1_SendString("[升级] ★新版本号不比当前新，拒绝升级\r\n");
                Usart1_SendString("[升级] （同一版本重复刷写请用串口烧录工具）\r\n");
                BootLoader_Info();
                break;
            }

            Usart1_SendString("[升级] 版本号检查通过\r\n");

            /* ---- 第 4 步：校验 + 擦除 + 搬运 + 写回版本信息 ----
             *   Boot_DoUpgrade 内部先 Boot_VerifyFirmware 再 Flash_EraseApp，
             *   所以校验失败时 APP 区一个字节都没动。 */
            ret = Boot_DoUpgrade(EEPROM_Data.new_len,
                                 EEPROM_Data.new_ver_num,
                                 EEPROM_Data.new_ver);

            if (ret != 0)
            {
                if (ret == 1)
                {
                    Usart1_SendString("[升级] ★固件校验失败，APP 区未改动\r\n");
                }
                else if (ret == 2)
                {
                    Usart1_SendString("[升级] ★擦除失败\r\n");
                }
                else
                {
                    Usart1_SendString("\r\n[升级] ★搬运失败，APP 区已损坏\r\n");
                    Usart1_SendString("[升级] 请用串口烧录工具重新烧写 APP\r\n");
                }
                BootLoader_Info();
                break;
            }

            sprintf(buf, "[升级] ★升级完成，新版本 = %s\r\n", EEPROM_Data.ver);
            Usart1_SendString(buf);
            Usart1_SendString("[升级] 正在重启...\r\n");
            delay_ms(500);
            NVIC_SystemReset();
        }
        break;

    case '5':
        // (9.6) 查看回退备份
        Boot_BakShow();
        break;

    case '6':
        // (9.7) 回退：把备份搬回 A 区
        //
        // ★为什么给一个"清空式确认"★
        //   回退会覆盖当前 APP，是最危险的操作之一（仅次于 [1] 擦除）。
        //   所以这里先打印目标版本和长度，要求再输字母 y 才真的动手；
        //   输错任何一个键（或 10 秒不输入）都直接放弃。
        {
            u32 blen = 0;
            u32 bnum = 0;
            u32 bsum = 0;
            char bver[EEPROM_VER_SIZE];

            if (Boot_BakReadHdr(&blen, &bnum, &bsum, bver) != 0)
            {
                Usart1_SendString("\r\n[回退] 没有可用的备份\r\n");
                BootLoader_Info();
                break;
            }

            sprintf(buf, "\r\n[回退] 将把 %s (%lu, %lu 字节) 写回 A 区\r\n",
                    (bver[0] != 0) ? bver : "(未知)",
                    (unsigned long)bnum, (unsigned long)blen);
            Usart1_SendString(buf);
            Usart1_SendString("[回退] 当前 APP 会被覆盖。确认请输入字母 y，其他键取消：");

            usart1_value.flag  = 0;
            usart1_value.count = 0;

            {
                u16 wait;

                for (wait = 0; wait < 100; wait++)   /* 最多等 10 秒 */
                {
                    if (usart1_value.flag)
                    {
                        break;
                    }
                    delay_ms(100);
                }
            }

            if (usart1_value.flag && (usart1_value.data[0] == 'y' || usart1_value.data[0] == 'Y'))
            {
                Usart1_SendString("\r\n");
                (void)Boot_Rollback();
            }
            else
            {
                Usart1_SendString("\r\n[回退] 已取消\r\n");
            }
            usart1_value.flag  = 0;
            usart1_value.count = 0;
            BootLoader_Info();
        }
        break;

    case '7':
        // (9.8) 手动备份当前 APP
        //
        // 为什么要手动这一步：
        //   自动备份只发生在“即将升级”时（Boot_DoUpgrade 里）。
        //   如果你刚用串口烧了一个能跑的基线版本，
        //   W25Q128 里可能还是上一代（甚至是有 bug 的）副本。
        //   按这里把它刷成当前这个能跑的版本。
        Usart1_SendString("\r\n");
        if (Boot_BackupCurrent() == 0)
        {
            Usart1_SendString("[7] 备份完成，现在 [6] 回退到的就是这个版本\r\n");
        }
        else
        {
            Usart1_SendString("[7] 备份未完成（原因见上）\r\n");
        }
        BootLoader_Info();
        break;

    default:

        // (9.5) 输入错误，提示并重显菜单
        Usart1_SendString("输入错误，请重新输入\r\n");
        BootLoader_Info();
        break;
    }
}

/*=====================================================================
 * 备份 / 回退（2026-11-04 新增）
 *
 * ★为什么需要★
 *   OTA 升级的动作是"擦掉 A 区 480KB 再写新的"，旧固件在擦除那一刻就没了。
 *   新固件如果跑不起来（连不上 MQTT、死机、白屏），现场只能拆机接 ST-Link。
 *   所以升级前先把当前能跑的 APP 备份进 W25Q128，菜单 [6] 可以一键搬回来。
 *
 * ★放在 W25Q128 哪里★
 *   0x000000 ~ 0x07FFFF   OTA 下载暂存区（APP 端擦的是 block 0~7，正好 512KB）
 *   0x080000 ~ 0x080FFF   备份头（一个 4KB 扇区）
 *   0x081000 起           备份镜像
 *   APP 只擦到 512KB 为止，永远碰不到备份区。
 *
 * ★备份头 32 字节格式（按字节下标读写，不依赖结构体对齐）★
 *   0..3    'B' 'A' 'K' '1'   魔数
 *   4..7    镜像长度（字节，已按 256 对齐）
 *   8..11   版本号数值（1.0.2 = 10002）
 *   12..15  镜像累加校验和（逐字节相加，u32 自然回绕）
 *   16..22  版本名字符串（最多 7 字符 + '\0'）
 *   23..31  0xFF 填充
 *
 * ★为什么校验和用"逐字节相加"而不是 MD5★
 *   BootLoader 里没有 MD5 实现（Boot_VerifyFirmware 只比对了尾缀里的值）。
 *   备份是本地 Flash 到本地 Flash 的拷贝，出错只有"没写完"这一种，
 *   累加和足够发现。真要更强，等以后把 utils_md5.c 挪过来再换。
 *====================================================================*/
#define BAK_BASE_ADDR    0x00080000UL              /* 512KB 处 */
#define BAK_HDR_ADDR     (BAK_BASE_ADDR)
#define BAK_IMG_ADDR     (BAK_BASE_ADDR + 0x1000UL)
#define BAK_HDR_LEN      32U
#define BAK_SEC_SIZE     4096UL

static void Bak_PutU32(u8 *p, u32 v)
{
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)((v >> 8) & 0xFF);
    p[2] = (u8)((v >> 16) & 0xFF);
    p[3] = (u8)((v >> 24) & 0xFF);
}

static u32 Bak_GetU32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u32 Bak_Sum(const u8 *p, u32 n)
{
    u32 s = 0;
    u32 i;

    for (i = 0; i < n; i++)
    {
        s += (u32)p[i];
    }
    return s;
}

/*---------------------------------------------------------------------
 * 从 A 区末尾往回找最后一个非 0xFF 的字节，得出当前 APP 实际占多少
 *
 * ★为什么不直接用 EEPROM_Data.app_size★
 *   APP 启动时写进去的是 Image$$ER_IROM1$$Limit - FLASH_A_ADDR，
 *   这个值【不含 RW 段的 Flash 初值副本】；
 *   而 BootLoader 升级完写进去的是完整镜像长度（含 RW 副本）。
 *   两个口径差一百多字节 —— 备份要是按小的那个截断，
 *   回退之后的固件末尾就缺一块，RW 初值全是垃圾，跑不起来。
 *   直接扫 Flash 最可靠，代价是几毫秒的读操作。
 *-------------------------------------------------------------------*/
static u32 Bak_ScanAppLen(void)
{
    static u8 buf[FLASH_PAGE_SIZE];
    u32 page;
    u32 i;

    for (page = FLASH_A_PAGE_NUM; page > 0; page--)
    {
        u32 off = (page - 1) * FLASH_PAGE_SIZE;

        Flash_Read(FLASH_A_ADDR + off, buf, FLASH_PAGE_SIZE);
        for (i = FLASH_PAGE_SIZE; i > 0; i--)
        {
            if (buf[i - 1] != 0xFF)
            {
                return off + i;
            }
        }
    }
    return 0;
}

/*---------------------------------------------------------------------
 * 读备份头
 * 返回：0 = 有效；1 = 没有备份（魔数不对）；2 = 头内容非法
 *-------------------------------------------------------------------*/
u8 Boot_BakReadHdr(u32 *img_len, u32 *ver_num, u32 *sum, char *ver)
{
    u8 h[BAK_HDR_LEN];
    u32 i;

    W25Q128_Read(BAK_HDR_ADDR, h, BAK_HDR_LEN);
    if (h[0] != 'B' || h[1] != 'A' || h[2] != 'K' || h[3] != '1')
    {
        return 1;
    }

    *img_len = Bak_GetU32(&h[4]);
    *ver_num = Bak_GetU32(&h[8]);
    *sum     = Bak_GetU32(&h[12]);

    for (i = 0; i < (u32)(EEPROM_VER_SIZE - 1); i++)
    {
        ver[i] = (char)h[16 + i];
    }
    ver[EEPROM_VER_SIZE - 1] = '\0';

    if (*img_len == 0 || *img_len > FLASH_A_SIZE)
    {
        return 2;
    }
    return 0;
}

/*---------------------------------------------------------------------
 * 备份当前 A 区到 W25Q128
 * 返回：0 = 成功；1 = 当前没有可备份的 APP；2/3 = 写失败
 *
 * ★调用时机：Boot_DoUpgrade() 里、Flash_EraseApp() 之前★
 *   所以备份的一定是"本次要覆盖掉的、当前正在跑的那一版"。
 *-------------------------------------------------------------------*/
u8 Boot_BackupCurrent(void)
{
    static u8 buf[256];
    u8  hdr[BAK_HDR_LEN];
    u8  v[8];
    u32 sp;
    u32 rv;
    u32 len;
    u32 sum = 0;
    u32 i;
    u16 sec;
    char nb[16];

    (void)EEPROM_LoadAppInfo(&EEPROM_Data);

    /* ① 先确认当前 A 区里确实有一个能跑的镜像 */
    Flash_Read(FLASH_A_ADDR, v, 8);
    sp = Bak_GetU32(&v[0]);
    rv = Bak_GetU32(&v[4]);
    if (sp < 0x20000000 || sp > 0x20010000 ||
        rv < FLASH_A_ADDR || rv >= (FLASH_A_ADDR + FLASH_A_SIZE))
    {
        Usart1_SendString("[备份] 当前 A 区没有可运行的镜像，跳过备份\r\n");
        return 1;
    }

    len = Bak_ScanAppLen();
    if (len == 0)
    {
        Usart1_SendString("[备份] 读不到 APP 长度，跳过备份\r\n");
        return 1;
    }
    len = (len + 255UL) & ~255UL;          /* 按 256 对齐，W25Q128 整页写 */
    if (len > FLASH_A_SIZE)
    {
        len = FLASH_A_SIZE;
    }

    /* ② 擦备份区要用的扇区：1 个头 + 镜像若干 */
    Usart1_SendString("[备份] 备份当前 APP 到 W25Q128");
    sec = (u16)(BAK_BASE_ADDR / BAK_SEC_SIZE);
    for (i = 0; i < (1UL + (len + BAK_SEC_SIZE - 1UL) / BAK_SEC_SIZE); i++)
    {
        W25Q128_EraseSector((u16)(sec + i));
        Usart1_SendString(".");
    }

    /* ③ 写镜像，顺便算校验和 */
    for (i = 0; i < len; i += 256UL)
    {
        Flash_Read(FLASH_A_ADDR + i, buf, 256);
        sum += Bak_Sum(buf, 256);

        if (W25Q128_Write(BAK_IMG_ADDR + i, buf, 256) != 0)
        {
            Usart1_SendString("\r\n[备份] 写 W25Q128 失败\r\n");
            return 2;
        }
        if (i % 4096UL == 0)
        {
            Usart1_SendString(".");
        }
    }

    /* ④ 头最后写：镜像没写完时头是空的，回退会拒绝，不会搬半截 */
    hdr[0] = 'B';
    hdr[1] = 'A';
    hdr[2] = 'K';
    hdr[3] = '1';
    Bak_PutU32(&hdr[4],  len);
    Bak_PutU32(&hdr[8],  EEPROM_Data.ver_num);
    Bak_PutU32(&hdr[12], sum);
    for (i = 0; i < (u32)(EEPROM_VER_SIZE - 1); i++)
    {
        hdr[16 + i] = (u8)EEPROM_Data.ver[i];
    }
    hdr[16 + EEPROM_VER_SIZE - 1] = 0;
    for (i = (u32)(16 + EEPROM_VER_SIZE); i < BAK_HDR_LEN; i++)
    {
        hdr[i] = 0xFF;
    }

    if (W25Q128_Write(BAK_HDR_ADDR, hdr, BAK_HDR_LEN) != 0)
    {
        Usart1_SendString("\r\n[备份] 写备份头失败\r\n");
        return 3;
    }

    sprintf(nb, "%lu", (unsigned long)len);
    Usart1_SendString("\r\n[备份] 完成，已保存 ");
    Usart1_SendString(nb);
    Usart1_SendString(" 字节；需要回退时进菜单按 [6]\r\n");
    return 0;
}

/*---------------------------------------------------------------------
 * 打印备份信息（菜单 [5]）
 *-------------------------------------------------------------------*/
void Boot_BakShow(void)
{
    u32 len = 0;
    u32 vnum = 0;
    u32 sum = 0;
    char ver[EEPROM_VER_SIZE];
    char buf[80];
    u8 ret;

    Usart1_SendString("\r\n--- 回退备份 ---\r\n");

    ret = Boot_BakReadHdr(&len, &vnum, &sum, ver);
    if (ret != 0)
    {
        Usart1_SendString("  没有备份（还没做过 OTA 升级，或备份被覆盖）\r\n");
        Usart1_SendString("  说明：每次 [4] OTA 升级前，会自动把当前 APP 备份一份\r\n");
    }
    else
    {
        sprintf(buf, "  备份版本名: %s\r\n", (ver[0] != 0) ? ver : "(未知)");
        Usart1_SendString(buf);
        sprintf(buf, "  备份版本号: %lu\r\n", (unsigned long)vnum);
        Usart1_SendString(buf);
        sprintf(buf, "  备份长度  : %lu 字节 (%lu KB)\r\n",
                (unsigned long)len, (unsigned long)(len / 1024));
        Usart1_SendString(buf);
        sprintf(buf, "  存放位置  : W25Q128 0x%06lX 起\r\n",
                (unsigned long)BAK_IMG_ADDR);
        Usart1_SendString(buf);
        Usart1_SendString("  输入 [6] 可以把这一份搬回 A 区（覆盖当前 APP）\r\n");
    }
    Usart1_SendString("----------------\r\n");
    BootLoader_Info();
}

/*---------------------------------------------------------------------
 * 回退：把备份搬回 A 区
 * 返回：0 = 成功；1 = 没有备份；2 = 校验和不符；3 = 向量表非法
 *       4 = 擦除失败；5 = 写入失败
 *
 * ★顺序刻意和升级一致：先校验，再擦，再写★
 *   校验不过时 A 区一个字节都没动，当前 APP 还能接着跑。
 *-------------------------------------------------------------------*/
u8 Boot_Rollback(void)
{
    static u8 buf[FLASH_PAGE_SIZE];
    u32 len = 0;
    u32 vnum = 0;
    u32 sum = 0;
    u32 calc = 0;
    u32 i;
    u32 n;
    u32 sp;
    u32 rv;
    u8  v[8];
    u8  ret;
    char ver[EEPROM_VER_SIZE];
    char nb[80];

    Usart1_SendString("\r\n[回退] 检查备份...\r\n");

    ret = Boot_BakReadHdr(&len, &vnum, &sum, ver);
    if (ret != 0)
    {
        Usart1_SendString("[回退] 没有可用的备份，放弃\r\n");
        return 1;
    }

    /* ① 先把备份整份读一遍算校验和，不对就绝不碰 A 区 */
    for (i = 0; i < len; i += 256UL)
    {
        W25Q128_Read(BAK_IMG_ADDR + i, buf, 256);
        calc += Bak_Sum(buf, 256);
    }
    if (calc != sum)
    {
        sprintf(nb, "[回退] 备份校验和不符（存=%lu 算=%lu），放弃\r\n",
                (unsigned long)sum, (unsigned long)calc);
        Usart1_SendString(nb);
        return 2;
    }

    /* ② 向量表检查：栈顶在 SRAM、复位向量在 APP 区 */
    W25Q128_Read(BAK_IMG_ADDR, v, 8);
    sp = Bak_GetU32(&v[0]);
    rv = Bak_GetU32(&v[4]);
    if (sp < 0x20000000 || sp > 0x20010000 ||
        rv < FLASH_A_ADDR || rv >= (FLASH_A_ADDR + FLASH_A_SIZE))
    {
        sprintf(nb, "[回退] 备份向量表非法（栈顶=0x%08lX 复位=0x%08lX），放弃\r\n",
                (unsigned long)sp, (unsigned long)rv);
        Usart1_SendString(nb);
        return 3;
    }

    sprintf(nb, "[回退] 目标版本 %s (%lu)，长度 %lu 字节\r\n",
            (ver[0] != 0) ? ver : "(未知)",
            (unsigned long)vnum, (unsigned long)len);
    Usart1_SendString(nb);

    /* ③ 擦 A 区 */
    Usart1_SendString("[回退] 开始擦除 APP 区（约 3 秒）...\r\n");
    if (Flash_EraseApp() != 0)
    {
        Usart1_SendString("[回退] 擦除失败\r\n");
        return 4;
    }

    /* ④ 搬回 */
    Usart1_SendString("[回退] 搬运中");
    for (i = 0; i < len; i += FLASH_PAGE_SIZE)
    {
        n = len - i;
        if (n > FLASH_PAGE_SIZE)
        {
            n = FLASH_PAGE_SIZE;
        }
        W25Q128_Read(BAK_IMG_ADDR + i, buf, n);
        if (n % 2)
        {
            buf[n] = 0xFF;                 /* Flash_Write 只接受偶数字节 */
            n++;
        }
        if (Flash_Write(FLASH_A_ADDR + i, buf, n) != 0)
        {
            Usart1_SendString("\r\n[回退] 写片内 Flash 失败\r\n");
            return 5;
        }
        if (i % 8192UL == 0)
        {
            Usart1_SendString(".");
        }
    }
    Usart1_SendString("\r\n");

    /* ⑤ 版本信息也一起回退 */
    (void)EEPROM_LoadAppInfo(&EEPROM_Data);
    memset(EEPROM_Data.ver, 0, sizeof(EEPROM_Data.ver));
    strncpy(EEPROM_Data.ver, ver, EEPROM_VER_SIZE - 1);
    EEPROM_Data.ver[EEPROM_VER_SIZE - 1] = '\0';
    EEPROM_Data.ver_num  = vnum;
    EEPROM_Data.app_size = len;
    EEPROM_Data.new_len     = 0;
    EEPROM_Data.new_ver_num = 0;
    EEPROM_Data.new_ver[0]  = '\0';
    if (EEPROM_SaveAppInfo(&EEPROM_Data) != 0)
    {
        Usart1_SendString("[回退] 版本信息写 EEPROM 失败（固件已搬回）\r\n");
    }

    /* ★云端版本号也要回退★
       APP 每次启动都会把 BemfaInfoCB.Version_ABuff 抄进 EEPROM_Data.ver。
       不回退这一份的话，回退完成、APP 一跑，
       菜单 [3] 又变回新版本号，云端也以为还是新版本，
       与实际运行的固件对不上。 */
    memset(BemfaInfoCB.Version_ABuff, 0, sizeof(BemfaInfoCB.Version_ABuff));
    strncpy((char *)BemfaInfoCB.Version_ABuff, ver,
            sizeof(BemfaInfoCB.Version_ABuff) - 1);
    BemfaInfoCB.Version_ABuff[sizeof(BemfaInfoCB.Version_ABuff) - 1] = '\0';

    /* ⑥ 清掉"有 OTA 事件"的标志，否则下次开机又自动去搬暂存区那份 */
    BemfaInfoCB.OTA_flag    = 0;
    BemfaInfoCB.OTA_firelen = 0;
    (void)EEPROM_SaveInfo();

    BootClear_Flag = 0;                    /* A 区已经有内容了，[2] 可以跳转 */
    Usart1_SendString("[回退] 完成，输入 [2] 跳转到该版本\r\n");
    return 0;
}

/*=====================================================================
 * 函数：Boot_DoUpgrade
 * 负责：B（把 W25Q128 里的新固件搬进片内 APP 区，并写回版本信息）
 *
 * 参数：new_len     - W25Q128 里固件的总长度（含 28 字节尾缀）
 *       new_ver_num - 新版本号数值（大版本*10000+小版本*100+修订号）
 *       new_ver     - 新版本名字符串，可为 NULL 或空串
 * 返回：0 = 升级成功   1 = 校验失败   2 = 擦除失败   3 = 搬运失败
 *
 * ★★★ 为什么要把这段抽成函数（2026-11-03 真板踩出来的）★★★
 *
 *   原来升级有两条路，各写了一份几乎一样的代码：
 *     路 A：菜单 [4] 手动触发 —— 写完版本信息、清标志，很完整
 *     路 B：ActiveEvent() 上电自动触发 —— 只搬固件，【不写版本信息】
 *
 *   真板实测：APP 下载完固件 → 置 OTA_flag → 重启 →
 *   BootLoader 走的是路 B（因为 OTA_flag 非 0，根本不会停在菜单），
 *   于是固件搬好了、APP 也跑起来了，但 EEPROM 里的版本号还是旧的 1.0.0。
 *   后果：云端每次对比都判"版本号不新"，反复下发同一个固件，永远升不上去。
 *
 *   抽成一个函数之后，两条路都调它，就不可能再出现"某条路漏了写回"。
 *====================================================================*/
u8 Boot_DoUpgrade(u32 new_len, u32 new_ver_num, const char *new_ver)
{
    u32 real_len = 0;

    /* ---- 第 1 步：校验固件（大小 / 魔数 / 尾缀 / 向量表） ----
     *   必须在 Flash_EraseApp 之前。校验失败时 APP 区一个字节都没动。 */
    if (Boot_VerifyFirmware(new_len, &real_len) != 0)
    {
        return 1;
    }

    /* ---- 第 1.5 步：备份当前 APP（回退用，2026-11-04 新增） ----
     *   必须在 Flash_EraseApp 之前，备份的才是"即将被覆盖的、现在正在跑的"那一版。
     *   备份失败不阻止升级：备份是保险，不是前提；
     *   真失败了也只是"这次升级后没有回退目标"，照样能升。 */
    if (Boot_BackupCurrent() != 0)
    {
        Usart1_SendString("[升级] 备份未完成，本次升级后没有回退目标（不影响升级）\r\n");
    }

    /* ---- 第 2 步：擦除 APP 区 ---- */
    Usart1_SendString("[升级] 开始擦除 APP 区（约 3 秒）...\r\n");
    if (Flash_EraseApp() != 0)
    {
        return 2;
    }

    /* ---- 第 3 步：搬运 ---- */
    if (Boot_CopyFirmware(real_len) != 0)
    {
        return 3;
    }

    /* ---- 第 4 步：把新版本信息写回 EEPROM ----
     *   ★ 这一步不能省 ★
     *   不更新 ver / ver_num，下次开机还是旧版本号，
     *   云端会一直判"版本号不新"重复下发同一个固件。
     *
     *   先 Load 再改：保住 app（App 名）和 sn（序列号）不被冲成空串。 */
    (void)EEPROM_LoadAppInfo(&EEPROM_Data);

    if (new_ver != 0 && new_ver[0] != '\0')
    {
        memset(EEPROM_Data.ver, 0, sizeof(EEPROM_Data.ver));
        strncpy(EEPROM_Data.ver, new_ver, EEPROM_VER_SIZE - 1);
        EEPROM_Data.ver[EEPROM_VER_SIZE - 1] = '\0';
    }

    EEPROM_Data.ver_num     = new_ver_num;
    EEPROM_Data.app_size    = real_len;      /* 真实镜像长度（不含尾缀） */
    EEPROM_Data.new_len     = 0;             /* 待升级记录清掉 */
    EEPROM_Data.new_ver_num = 0;
    EEPROM_Data.new_ver[0]  = '\0';

    if (EEPROM_SaveAppInfo(&EEPROM_Data) != 0)
    {
        Usart1_SendString("[升级] ★版本信息写 EEPROM 失败（固件已搬好）\r\n");
    }
    else
    {
        Usart1_SendString("[升级] 版本信息已更新\r\n");
    }

    /* ---- 第 5 步：清掉"有 OTA 事件"的标志 ----
     *   不清的话，下次开机 OTA_flag 还是 BOOT_STA_O，
     *   又会跳过 5 秒窗口自动进升级流程。 */
    BemfaInfoCB.OTA_flag    = 0;
    BemfaInfoCB.OTA_firelen = 0;
    (void)EEPROM_SaveInfo();

    return 0;
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
    u8  ret;

    if (!(BootEventFlag & OTA_MODEM))
    {
        return;
    }
    BootEventFlag &= ~OTA_MODEM;

    /*=================================================================
     * ★ 2026-11-03 重写：这条路上电自动触发，原来只搬固件不写版本信息，
     *   导致"固件升了、版本号还是旧的"，云端会反复下发同一个固件。
     *   现在统一调 Boot_DoUpgrade()，和菜单 [4] 走同一套逻辑。
     *
     *   三个前置判断顺序不能变：
     *     ① 有没有固件（new_len > 0）
     *     ② 版本号是不是更新（new_ver_num > ver_num）
     *     ③ 固件本身合不合法（Boot_DoUpgrade 里的 Boot_VerifyFirmware）
     *   任意一条不过 ⇒ 清标志、跳回旧 APP，绝不动 APP 区。
     *=================================================================*/
    (void)EEPROM_LoadAppInfo(&EEPROM_Data);

    /* ---- ① 有没有待升级固件 ----
     *   判据用 new_len 不用 OTA_flag：flag 只表示云端通知来了，
     *   len 才表示 APP 真的把固件下载进 W25Q128 了。 */
    if (EEPROM_Data.new_len == 0)
    {
        Usart1_SendString("[BOOT] EEPROM 里没有待升级固件，跳过升级\r\n");
        BemfaInfoCB.OTA_flag    = 0;
        BemfaInfoCB.OTA_firelen = 0;
        (void)EEPROM_SaveInfo();
        Load_APP(FLASH_A_ADDR);
        return;
    }

    /* ---- ② 版本对比 ----
     *   BootLoader 里没有 Usart1_SendNumber（只有 SendString/SendBuffer/SendByte），
     *   所以这里用 sprintf 拼一行打出来。 */
    {
        char numbuf[64];
        sprintf(numbuf, "[BOOT] 当前版本号 = %lu，新版本号 = %lu\r\n",
                (unsigned long)EEPROM_Data.ver_num,
                (unsigned long)EEPROM_Data.new_ver_num);
        Usart1_SendString(numbuf);
    }

    if (EEPROM_Data.new_ver_num <= EEPROM_Data.ver_num)
    {
        Usart1_SendString("[BOOT] ★新版本号不比当前新，拒绝升级\r\n");
        BemfaInfoCB.OTA_flag    = 0;
        BemfaInfoCB.OTA_firelen = 0;
        (void)EEPROM_SaveInfo();
        Load_APP(FLASH_A_ADDR);
        return;
    }

    /* ---- ③ 校验 + 擦除 + 搬运 + 写回版本信息 ---- */
    ret = Boot_DoUpgrade(EEPROM_Data.new_len,
                         EEPROM_Data.new_ver_num,
                         EEPROM_Data.new_ver);

    if (ret != 0)
    {
        if (ret == 1)
        {
            Usart1_SendString("[BOOT] ★固件校验失败，APP 区未改动\r\n");
        }
        else if (ret == 2)
        {
            Usart1_SendString("[BOOT] ★擦除失败\r\n");
        }
        else
        {
            Usart1_SendString("[BOOT] ★搬运失败，APP 区已损坏\r\n");
        }
        BemfaInfoCB.OTA_flag    = 0;
        BemfaInfoCB.OTA_firelen = 0;
        (void)EEPROM_SaveInfo();
        Load_APP(FLASH_A_ADDR);
        return;
    }

    Usart1_SendString("[BOOT] 升级完成，新版本 = ");
    Usart1_SendString(EEPROM_Data.ver);
    Usart1_SendString("\r\n[BOOT] 正在重置系统\r\n");
    delay_ms(500);
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
    /* ★2026-11-02 改：原来是 (stack_top & 0x2FFE0000) != 0x20000000，
     * 这个掩码有洞 —— 0x60000000 & 0x2FFE0000 也等于 0x20000000，
     * 明显不在 SRAM 里的地址能骗过检查。改成直接比范围：
     * F103ZE 的 SRAM 是 0x20000000 ~ 0x2000FFFF（64KB）。★ */
    if (stack_top < 0x20000000 || stack_top > 0x20010000)
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

    //用于返回的长度
    u32 real_len;

    // 判断固件长度是否合法
    if (fw_total_len < 28)
    {
        Usart1_SendString("BOOT VERIFY FIRMWARE: 无效固件长度\r\n");
        return 1;
    }

    //判断是否有输出指针
    if (out_real_len == NULL)
    {
        Usart1_SendString("BOOT VERIFY FIRMWARE: 无效输出指针\r\n");
        return 1;
    }

    /*=================================================================
     * ★2026-11-02 新增：固件总大小上限检查（防变砖）★
     *
     * 为什么要加在这里、而不是等 Boot_CopyFirmware 里再查：
     *   ActiveEvent() 的顺序是
     *       Boot_VerifyFirmware() → Flash_EraseApp() → Boot_CopyFirmware()
     *   Flash_EraseApp() 会把 A 区 240 页全擦掉。如果固件超限，
     *   Boot_CopyFirmware() 里那句 if(fw_len > FLASH_A_SIZE) return 1
     *   确实会拒绝，但那时 A 区已经擦空了 —— 设备直接变砖，只能拆机烧录。
     *
     *   所以必须把大小检查提到「擦除之前」，也就是这里。
     *
     * 上限取 FLASH_A_SIZE（480KB）。W25Q128 有 16MB，本身放得下更大，
     * 但片内 APP 区只有 480KB，超了搬不进去。
     *================================================================*/
    // 判断是否超过 A区大小
     if (fw_total_len > FLASH_A_SIZE)
    {
        Usart1_SendString("BOOT VERIFY FIRMWARE: 固件超过 A 区 480KB，拒绝升级\r\n");
        return 1;
    }

    // 读尾缀 28 字节
    W25Q128_Read(fw_total_len - 28, buf, 28);

    // 判断魔数 是否正确
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

    /*=================================================================
     * ★2026-11-02 新增：向量表合法性检查（防搬进去一个跑不起来的固件）★
     *
     * 固件搬到 A 区后，Load_APP() 会读 A 区头 8 字节：
     *   [0] 栈顶（必须落在 0x20000000 起的 64KB RAM 内）
     *   [4] Reset_Handler（必须落在 A 区内）
     * 但那是「搬完之后」才检查的 —— 检查不过就 return，设备卡在 BootLoader
     * 菜单里出不来（虽然不会彻底变砖，但要人工接串口救）。
     *
     * 所以在搬之前先从 W25Q128 读一下这个固件的向量表，不合法就拒绝。
     * 这一步很便宜（只读 8 字节），能拦住「下载到一半断了」「下错文件了」
     * 「把 BootLoader 自己当 APP 传上来了」这三类。
     *
     * 具体判据（与 Load_APP 保持一致）：
     *   ① 栈顶必须在 0x20000000 ~ 0x20010000 之间（F103ZE 的 64KB SRAM）
     *   ② Reset_Handler 必须在 [FLASH_A_ADDR, FLASH_A_ADDR+FLASH_A_SIZE) 内
     *================================================================*/
    {
        u8 vec[8];
        u32 fw_stack;
        u32 fw_reset;
        W25Q128_Read(0, vec, 8);
        fw_stack = (u32)vec[0] | ((u32)vec[1] << 8) | ((u32)vec[2] << 16) | ((u32)vec[3] << 24);
        fw_reset = (u32)vec[4] | ((u32)vec[5] << 8) | ((u32)vec[6] << 16) | ((u32)vec[7] << 24);

        /* ★2026-11-02 改：原来是 (stack_top & 0x2FFE0000) != 0x20000000，
         * 这个掩码有洞 —— 0x60000000 & 0x2FFE0000 也等于 0x20000000，
         * 一个明显不在 SRAM 里的地址能骗过检查。改成直接比范围。★ */
        if (fw_stack < 0x20000000 || fw_stack > 0x20010000)
        {
            Usart1_SendString("BOOT VERIFY FIRMWARE: 固件栈顶非法，拒绝升级\r\n");
            return 1;
        }
        if (fw_reset < FLASH_A_ADDR || fw_reset >= (FLASH_A_ADDR + FLASH_A_SIZE))
        {
            Usart1_SendString("BOOT VERIFY FIRMWARE: 固件重置向量不在 APP 区，拒绝升级\r\n");
            return 1;
        }
        /* 顺手把两个值打出来，出问题时一眼就能看出是哪个不对 */
        {
            char hexbuf[64];
            sprintf(hexbuf, "BOOT VERIFY FIRMWARE: 栈顶=0x%08X 复位向量=0x%08X\r\n",
                    (unsigned int)fw_stack, (unsigned int)fw_reset);
            Usart1_SendString(hexbuf);
        }
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
 * 参数：无
 * 返回：0 = 参数有效，1 = 参数无效（首次上电 / 未配置 / 数据损坏）
 *
 * ★ 2026-11-03 改：返回值从 void 改成 u8 ★
 *   原来返回 void，调用方没法知道"读到的是真参数还是被清空了"，
 *   main.c 里就写不出"参数无效时顺手写默认值"这种分支。
 *   APP 工程里同名函数本来就返回 u8，这里统一成一样的签名。
 *
 * 实现要点：
 *   u8 *p = (u8*)&BemfaInfoCB;
 *   memset(&BemfaInfoCB, 0, sizeof(BemfaInfoCB));
 *   for(i = 0; i < sizeof(BemfaInfoCB); i++){
 *       p[i] = EEPROM_ReadByte(EEPROM_AT24C02_ADDR, i);
 *   }
 *====================================================================*/
u8 EEPROM_ReadInfo(void)
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
        return 1;
    }
    else
    {
        Usart1_SendString("[EEPROM] 参数有效\r\n");
    }

    return 0;
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
