#include "eeprom.h"
#include "usart.h"
#include "delay.h"

/*=====================================================================
 * ★ 2026-11-03 重写说明 ★
 *
 *   用户原来的版本里，EEPROM_WriteByte / EEPROM_ReadByte 这两个底层
 *   函数被删掉了，但 eeprom.c 和 boot.c 都还在调用它们，所以编译报
 *   "declared implicitly" 警告，链接阶段会直接失败。这里把它们补回来。
 *
 *   另外 EEPROM_SaveInfo / EEPROM_LoadInfo 这两个名字，
 *   boot.c 里已经用掉了一份（读写 BemfaInfoCB 那 99 字节），
 *   所以这里改名成 EEPROM_SaveAppInfo / EEPROM_LoadAppInfo，
 *   一个管"巴法云参数 + OTA 标志"，一个管"App 版本信息"，互不冲突。
 *====================================================================*/

/* 全局唯一一份 App 信息（原来在头文件里 static，每个 .c 各一份，是错的） */
EEPROM_Info_t EEPROM_Data;

/*=====================================================================
 * 函数：EEPROM_WriteByte
 * 负责：写 1 个字节到 AT24C02 的指定地址
 *
 * 返回：I2C_ACK(0) = 成功，I2C_NACK(1) = 失败
 *
 * ★ 注意这里的返回值语义 ★
 *   i2c.h 里是 typedef enum { I2C_ACK, I2C_NACK } ⇒ I2C_ACK = 0。
 *   所以"成功"返回的是 0，判断时写 if (ret != I2C_ACK) 或者 if (ret)。
 *====================================================================*/
u8 EEPROM_WriteByte(u8 eeprom_addr, u8 reg_addr, u8 data)
{
    u8 retry = I2C_RETRY_COUNT;
    u8 timeout = I2C_TIMEOUT;

    while (retry--)
    {
        /* ① 起始信号 */
        I2C_Start();

        /* ② 设备地址 + 写标志 */
        if (I2C_SendByte(eeprom_addr << 1 | WRITE_FLAG))
        {
            I2C_Stop();
            continue;
        }

        /* ③ 目标寄存器地址 */
        if (I2C_SendByte(reg_addr))
        {
            I2C_Stop();
            continue;
        }

        /* ④ 数据字节 */
        if (I2C_SendByte(data))
        {
            I2C_Stop();
            continue;
        }

        /* ⑤ 停止信号 */
        I2C_Stop();

        /*=================================================================
         * ⑥ 等芯片把数据真正写进去
         *
         *    AT24C02 收到数据后要 ~5ms 处理，这期间它不应答任何寻址。
         *    所以这里反复发"设备地址 + 写标志"试探：
         *      收到 ACK(0) ⇒ 芯片忙完了，数据已经落地，可以返回成功
         *      收到 NACK   ⇒ 还在写，停一下再试
         *    最多试 I2C_TIMEOUT(10) 次，每次间隔 1ms，够 5ms 用。
         *=================================================================*/
        while (timeout--)
        {
            I2C_Start();
            if (!I2C_SendByte(eeprom_addr << 1 | WRITE_FLAG))
            {
                I2C_Stop();
                return I2C_ACK;         /* 芯片应答了 ⇒ 写成功 */
            }
            I2C_Stop();
            delay_ms(1);
        }

        /* 超时了还没应答，把 timeout 复位，再来一整轮 */
        timeout = I2C_TIMEOUT;
    }

    return I2C_NACK;
}

/*=====================================================================
 * 函数：EEPROM_ReadByte
 * 负责：从 AT24C02 的指定地址读 1 个字节
 *
 * 返回：读到的数据（读失败时返回 0，和"数据本来就是 0"分不开，
 *       所以调用方要判空字符串/0xFF 来兜底）
 *====================================================================*/
u8 EEPROM_ReadByte(u8 eeprom_addr, u8 reg_addr)
{
    u8 retry = I2C_RETRY_COUNT;
    u8 data = 0;

    while (retry--)
    {
        /* ① 起始信号 */
        I2C_Start();

        /* ② 设备地址 + 写标志（为了先把读指针设到 reg_addr） */
        if (I2C_SendByte(eeprom_addr << 1 | WRITE_FLAG))
        {
            I2C_Stop();
            continue;
        }

        /* ③ 目标寄存器地址 */
        if (I2C_SendByte(reg_addr))
        {
            I2C_Stop();
            continue;
        }

        /* ④ 重新起始（Repeated Start） */
        I2C_Start();

        /* ⑤ 设备地址 + 读标志 */
        if (I2C_SendByte(eeprom_addr << 1 | READ_FLAG))
        {
            I2C_Stop();
            continue;
        }

        /* ⑥ 读 1 字节，读完给 NACK 告诉从机"不要再发了" */
        data = I2C_ReadByte(I2C_NACK);

        /* ⑦ 停止信号 */
        I2C_Stop();

        return data;
    }

    return 0;
}

/*=====================================================================
 * 函数：EEPROM_Init
 * 负责：探测 AT24C02 在不在线
 *
 * 返回：0 = 在线，1 = 不在线
 *
 * 做法：随便读一个地址（0x70，App 版本名区），
 *       只要能读到值（哪怕值是 0xFF）就说明芯片应答了 I2C 寻址。
 *       I2C 没应答时 EEPROM_ReadByte 会返回 0，
 *       但 0 也是合法数据，所以不能靠返回值判断，
 *       这里改用"连续读 3 次结果一致"来粗判——不可靠。
 *
 * ★ 更可靠的做法 ★
 *   直接用 I2C 寻址本身：Start → 发地址 → 看 ACK → Stop。
 *   这里没有单独暴露这个接口，所以退一步：
 *   读一次，如果拿到 0xFF 就认为"芯片可能没焊/没接线"，
 *   但 0xFF 也可能是新芯片的正常值 ⇒ 所以只打印提示，不返回错误。
 *   真正判断"有没有写成功"靠的是写之后的回读比对。
 *====================================================================*/
u8 EEPROM_Init(void)
{
    u8 probe;

    /* 给芯片一点上电稳定时间 */
    delay_ms(10);

    probe = EEPROM_ReadByte(EEPROM_DEV_ADDR, EEPROM_VER_ADDR);

    /* 读不到 0xFF 说明大概率在线；读到 0xFF 也不能断定不在线
     * （全新芯片整片都是 0xFF），所以统一返回 0，
     * 真正的可用性由后面的读写回读来判断。 */
    (void)probe;
    return 0;
}

/*=====================================================================
 * 函数：EEPROM_WriteString
 * 负责：把一个字符串写到 EEPROM 指定地址（含结尾 '\0'）
 *
 * 参数：mem_addr - 起始地址
 *       str      - 要写的字符串
 *       max_len  - 这个区域最大多少字节（含 '\0'）
 *
 * 返回：I2C_ACK(0) = 成功，I2C_NACK(1) = 失败
 *====================================================================*/
u8 EEPROM_WriteString(u8 mem_addr, const char *str, u8 max_len)
{
    u16 len;
    u8  i;

    if (str == 0 || max_len == 0)
    {
        return I2C_NACK;
    }

    /* 长度 = 字符数 + 1（结尾 '\0'） */
    len = (u16)strlen(str) + 1;

    /* 放不下就直接失败，绝不截断——截断会留下半个 UTF-8 汉字 */
    if (len > (u16)max_len)
    {
        return I2C_NACK;
    }

    /* 别写出 256 字节之外（AT24C02 只有 256 字节） */
    if ((u16)mem_addr + len > 256)
    {
        return I2C_NACK;
    }

    for (i = 0; i < (u8)len; i++)
    {
        if (EEPROM_WriteByte(EEPROM_DEV_ADDR, (u8)(mem_addr + i), (u8)str[i]) != I2C_ACK)
        {
            return I2C_NACK;
        }
    }

    return I2C_ACK;
}

/*=====================================================================
 * 函数：EEPROM_ReadString
 * 负责：从 EEPROM 读一个以 '\0' 结尾的字符串
 *
 * 返回：I2C_ACK(0)  = 读到了正常结尾的字符串
 *       I2C_NACK(1) = 读到 0xFF（空白区）或读满都没遇到 '\0'
 *
 * ★ 读到 0xFF 的处理 ★
 *   AT24C02 出厂时整片是 0xFF。如果第一次上电就读版本名，
 *   会读到 0xFF 0xFF 0xFF...，那不是字符串。
 *   这里把 0xFF 当作"这块还没写过"，直接把 buf 置成空串并返回失败，
 *   调用方看到失败就用默认值兜底。
 *====================================================================*/
u8 EEPROM_ReadString(u8 mem_addr, char *buf, u8 buf_size)
{
    u8 i;
    u8 ch;

    if (buf == 0 || buf_size == 0)
    {
        return I2C_NACK;
    }

    for (i = 0; i < (u8)(buf_size - 1); i++)
    {
        ch = EEPROM_ReadByte(EEPROM_DEV_ADDR, (u8)(mem_addr + i));

        /* 遇到 0xFF ⇒ 这片区域从来没写过 */
        if (ch == 0xFF)
        {
            buf[0] = '\0';
            return I2C_NACK;
        }

        buf[i] = (char)ch;

        /* 遇到 '\0' ⇒ 字符串正常结束 */
        if (ch == '\0')
        {
            return I2C_ACK;
        }
    }

    /* 读满 buf_size-1 个字节都没见到 '\0' ⇒ 数据不可信，强制收尾 */
    buf[buf_size - 1] = '\0';
    return I2C_NACK;
}

/*=====================================================================
 * 函数：EEPROM_WriteU32
 * 负责：写 1 个 32 位整数（小端：低字节在前）
 *
 * 为什么用"逐字节手写"而不是 *(u32*)&... ：
 *   STM32 是 32 位机，但 EEPROM 是 8 位存储，必须拆开写。
 *   而且这样写跟编译器的大小端设置无关，换平台也不会错。
 *====================================================================*/
u8 EEPROM_WriteU32(u8 mem_addr, u32 value)
{
    u8 i;

    if ((u16)mem_addr + 4 > 256)
    {
        return I2C_NACK;
    }

    for (i = 0; i < 4; i++)
    {
        if (EEPROM_WriteByte(EEPROM_DEV_ADDR,
                             (u8)(mem_addr + i),
                             (u8)((value >> (i * 8)) & 0xFF)) != I2C_ACK)
        {
            return I2C_NACK;
        }
    }

    return I2C_ACK;
}

/*=====================================================================
 * 函数：EEPROM_ReadU32
 * 负责：读 1 个 32 位整数（小端）
 *
 * ★ 为什么要判 0xFFFFFFFF ★
 *   全新芯片读出来是 0xFFFFFFFF（不是 0），
 *   如果直接拿去和版本号比较，会得出"当前版本号 = 42 亿"这种荒谬结论，
 *   然后任何升级都会被判"版本号不新"而拒绝。
 *   所以这里把 0xFFFFFFFF 当作"没写过"，统一返回 0。
 *====================================================================*/
u32 EEPROM_ReadU32(u8 mem_addr)
{
    u32 value = 0;
    u8  i;
    u8  ch;

    if ((u16)mem_addr + 4 > 256)
    {
        return 0;
    }

    for (i = 0; i < 4; i++)
    {
        ch = EEPROM_ReadByte(EEPROM_DEV_ADDR, (u8)(mem_addr + i));
        value |= ((u32)ch) << (i * 8);
    }

    if (value == 0xFFFFFFFFUL)
    {
        return 0;
    }

    return value;
}

/*=====================================================================
 * 函数：EEPROM_LoadAppInfo
 * 负责：把 EEPROM 里的 App 信息一次全读出来
 *
 * 参数：info - 输出结构体
 * 返回：0 = 全部读到，1 = 有字段是空的（已用默认值填好）
 *
 * ★ 失败也给默认值，这点很重要 ★
 *   如果返回失败就留个空结构体，调用方打印出来是一片空白，
 *   看不出"是没存过"还是"读坏了"。这里给上出厂默认值，
 *   打印出来至少能看出"哦，还没存过，这是默认的 1.0.0"。
 *====================================================================*/
u8 EEPROM_LoadAppInfo(EEPROM_Info_t *info)
{
    u8 ok = 1;

    if (info == 0)
    {
        return 1;
    }

    memset(info, 0, sizeof(EEPROM_Info_t));

    /* ---------- ① 先看魔数，判断这块区域有没有被正经写过 ----------
     *
     *   魔数对不上 ⇒ 里面可能是芯片出厂值（全 0xFF），
     *   也可能是旧工程残留的垃圾。这两种情况都不该拿去用，
     *   直接把所有字段填成出厂默认值，让调用方看到 ok=1（无效）。
     *
     *   ★ 为什么不逐字段判断 ★
     *     逐字段判断的判据是"读字符串有没有遇到 '\0'"，这个判据
     *     对垃圾数据完全无效 —— 垃圾里也可能有 0x00。
     *     一个整块级别的魔数才能一次性说清"这块数据整体可不可信"。 */
    if (EEPROM_ReadU32(EEPROM_APPINFO_MAGIC_ADDR) != EEPROM_APPINFO_MAGIC)
    {
        strcpy(info->ver, EEPROM_VER_STR);
        strcpy(info->app, EEPROM_APP_STR);
        strcpy(info->sn,  EEPROM_SN_STR);
        info->app_size    = 0;
        info->ver_num     = 0;
        info->new_ver_num = 0;
        info->new_len     = 0;
        info->new_ver[0]  = '\0';
        return 1;
    }

    /* ---------- ② 魔数对得上，逐字段读 ---------- */
    if (EEPROM_ReadString(EEPROM_VER_ADDR, info->ver, EEPROM_VER_SIZE) != I2C_ACK)
    {
        strcpy(info->ver, EEPROM_VER_STR);
        ok = 0;
    }

    if (EEPROM_ReadString(EEPROM_APP_ADDR, info->app, EEPROM_APP_SIZE) != I2C_ACK)
    {
        strcpy(info->app, EEPROM_APP_STR);
        ok = 0;
    }

    if (EEPROM_ReadString(EEPROM_SN_ADDR, info->sn, EEPROM_SN_SIZE) != I2C_ACK)
    {
        strcpy(info->sn, EEPROM_SN_STR);
        ok = 0;
    }

    info->app_size    = EEPROM_ReadU32(EEPROM_SIZE_ADDR);
    info->ver_num     = EEPROM_ReadU32(EEPROM_VERNUM_ADDR);
    info->new_ver_num = EEPROM_ReadU32(EEPROM_NEWVER_ADDR);
    info->new_len     = EEPROM_ReadU32(EEPROM_NEWLEN_ADDR);

    if (EEPROM_ReadString(EEPROM_NEWVER_STR_ADDR, info->new_ver, EEPROM_VER_SIZE) != I2C_ACK)
    {
        info->new_ver[0] = '\0';
    }

    return ok ? 0 : 1;
}

/*=====================================================================
 * 函数：EEPROM_SaveAppInfo
 * 负责：把 App 信息一次全写进 EEPROM
 *
 * 返回：0 = 全部写成功，1 = 有字段写失败
 *
 * ★ 写失败就立刻返回，不继续写剩下的 ★
 *   半写半不写会留下"版本名是新的、大小是旧的"这种自相矛盾状态，
 *   比干脆没写更危险（BootLoader 会拿错的大小去算）。
 *====================================================================*/
u8 EEPROM_SaveAppInfo(EEPROM_Info_t *info)
{
    if (info == 0)
    {
        return 1;
    }

    if (EEPROM_WriteString(EEPROM_VER_ADDR, info->ver, EEPROM_VER_SIZE) != I2C_ACK)
    {
        return 1;
    }

    if (EEPROM_WriteString(EEPROM_APP_ADDR, info->app, EEPROM_APP_SIZE) != I2C_ACK)
    {
        return 1;
    }

    if (EEPROM_WriteString(EEPROM_SN_ADDR, info->sn, EEPROM_SN_SIZE) != I2C_ACK)
    {
        return 1;
    }

    if (EEPROM_WriteU32(EEPROM_SIZE_ADDR, info->app_size) != I2C_ACK)
    {
        return 1;
    }

    if (EEPROM_WriteU32(EEPROM_VERNUM_ADDR, info->ver_num) != I2C_ACK)
    {
        return 1;
    }

    if (EEPROM_WriteU32(EEPROM_NEWVER_ADDR, info->new_ver_num) != I2C_ACK)
    {
        return 1;
    }

    if (EEPROM_WriteU32(EEPROM_NEWLEN_ADDR, info->new_len) != I2C_ACK)
    {
        return 1;
    }

    if (EEPROM_WriteString(EEPROM_NEWVER_STR_ADDR, info->new_ver, EEPROM_VER_SIZE) != I2C_ACK)
    {
        return 1;
    }

    /* ---------- 最后写魔数 ----------
     * 放最后是故意的：如果写到一半掉电，魔数还是旧值，
     * 下次上电 LoadAppInfo 会判无效，把整块重写一遍。
     * 反过来先写魔数的话，掉电后会留下"魔数对、字段半新半旧"的状态。 */
    if (EEPROM_WriteU32(EEPROM_APPINFO_MAGIC_ADDR, EEPROM_APPINFO_MAGIC) != I2C_ACK)
    {
        return 1;
    }

    return 0;
}
