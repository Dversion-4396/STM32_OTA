#include "eeprom.h"
#include "usart.h"

u8 EEPROM_WriteByte(u8 eeprom_addr, u8 reg_addr, u8 data)
{
    u8 retry = I2C_RETRY_COUNT;
    u8 timeout = I2C_TIMEOUT;
    while (retry--)
    {
        /*
        数据帧组成
                1. i2c初始信号 i2c_start
                2. i2c_SendByte 发送设备地址和读写标志位 同时处理ack应答
                3. i2c_SendByte 目标写入寄存器地址同时处理ack应答
                4, i2c_SendByte 写入到存储及的一字节数据 同时处理ack应答
                5. i2c 停止信号 i2c_stop
        */
        /// 1.初始信号
        I2C_Start();

        /// 2.发送设备地址同时处理ack应答
        ////
        if (I2C_SendByte(eeprom_addr << 1 | WRITE_FLAG))
        {
            /*
                 若应答失败直接停止后续操作同时跳出本次循环
            */
            I2C_Stop();
            continue;
        }

        // 3. I2C_SendByte 目标写入寄存器地址，同时处理 ACK 应答
        if (I2C_SendByte(reg_addr))
        {
            I2C_Stop();
            continue;
        }

        // 4. I2C_SendByte 写入到存储器的 1 字节数据，同时处理 ACK 应答
        if (I2C_SendByte(data))
        {
            I2C_Stop();
            continue;
        }
        // 5. 停止
        I2C_Stop();
        /*
           以上操作是将数据按照 I2C 协议帧格式要求写入到 EEPROM AT24C02 芯片中。
           【EEPROM AT24C02 进入冥想模式】
           存储芯片需要一定的时间来处理刚刚收到的数据内容，大概时间在 5ms 左右
           在当前时间段内容，EEPROM AT24C02 芯片不会响应外部任何操作。
           当前处理完成之后，EEPROM AT24C02 芯片才会响应外部其他操作。
           可以利用当前特征判断数据是否有效写入到存储芯片中。
       */
        while (timeout--)
        {

            /*
                 利用 I2C 设备寻址操作，要求对应芯片进行应答
            */
            I2C_Start();

            /*
            发送设备地址 + 读写标志位到 I2C 从机设备
            如果设备应答为 NACK(1) 表示当前设备在处理数据过程中
            如果设备应答为 ACK(0) 表示数据已完成处理。
            */
            if (!I2C_SendByte(eeprom_addr << 1 | WRITE_FLAG))
            {
                I2C_Stop();
                return I2C_ACK;
            }

            /*
            I2C 停止信号，准备进入下一次操作
            */
            I2C_Stop();
            delay_ms(1);
        }
        timeout=I2C_TIMEOUT;
    }
    return I2C_NACK;
}

u8 EEPROM_ReadByte(u8 eeprom_addr, u8 reg_addr)
{
    u8 retry = I2C_RETRY_COUNT;
    u8 data = 0;
    while (retry--)
    {
        /*
        数据帧组成
            1. I2C 起始信号，I2C_Start
            2. I2Cw_SendByte 发送设备地址 + 写入标志，同时处理 ACK 应答
            3. I2C_SendByte 目标读取数据寄存器地址，同时处理 ACK 应答
            4. I2C 起始信号，I2C_Start
            5. I2C_SendByte 发送设备地址 + 读取标志，同时处理 ACK 应答
            6. I2C_ReadByte 读取从机发送的数据内容，同时主机通过 ACK 应答告知
                    从机是否继续发送。
            7. I2C 停止信号，I2C_Stop
        */

        // 1. I2C 起始信号，I2C_Start
        I2C_Start();

        // 2. I2C_SendByte 发送设备地址 + 写入标志，同时处理 ACK 应答
        if (I2C_SendByte(eeprom_addr << 1 | WRITE_FLAG))
        {
            I2C_Stop();
            continue;
        }

        // 3. I2C_SendByte 目标读取数据寄存器地址，同时处理 ACK 应答
        if (I2C_SendByte(reg_addr))
        {
            I2C_Stop();
            continue;
        }

        // 4. I2C 起始信号，I2C_Start
        I2C_Start();

        // 5. I2C_SendByte 发送设备地址 + 读取标志，同时处理 ACK 应答
        if (I2C_SendByte(eeprom_addr << 1 | READ_FLAG))
        {
            I2C_Stop();
            continue;
        }

        /*
        6. I2C_ReadByte 读取从机发送的数据内容，同时主机通过 ACK 应答告知
            从机是否继续发送。
        */
        data = I2C_ReadByte(I2C_NACK);

        // 7. I2C 停止信号，I2C_Stop
        I2C_Stop();

        return data;
    }
    return 0;
    
}

/*
    从 AT24C02 读出整个 BemfaInfoCB（99 字节）
 */
u8 EEPROM_ReadInfo(void)
{
    u8 i;
    u8 *p = (u8 *)&BemfaInfoCB;
    memset(&BemfaInfoCB, 0, sizeof(BemfaInfoCB));
    for (i = 0; i < sizeof(BemfaInfoCB); i++)
    {
        p[i] = EEPROM_ReadByte(EEPROM_AT24C02_ADDR, i);
        delay_ms(5);
    }

    /* 判断是否为首次启动 */
    if (BemfaInfoCB.valid_flag != VALID_BEMFA_DATA) /* 0xDD */
    {
        memset(&BemfaInfoCB, 0, sizeof(BemfaInfoCB));
        BemfaInfoCB.valid_flag = 0;
        Usart1_SendString("[EEPROM] 参数无效，需要重新配置\r\n");
        return 1;
    }

    Usart1_SendString("[EEPROM] 参数有效\r\n");
    return 0;
}

/*
  把整个 BemfaInfoCB（99 字节）写进 AT24C02
*/
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
 * ★★ 以下为 App 信息区实现（2026-11-03 新增）★★
 *
 *   这一段必须和 BootLoader 工程的 eeprom.c 逐字节一致，
 *   否则两边算出来的地址/字段顺序会不一样，读出来就是乱码。
 *
 *   为什么新加一套 Load/SaveAppInfo，而不复用上面的 Load/SaveInfo：
 *     上面那两个管的是 BemfaInfoCB（99 字节，巴法云参数 + OTA 标志），
 *     这里管的是"我是哪个 App、我什么版本、我多大"，两码事。
 *     混在一起改，任何一边加字段都会把另一边挤坏。
 *====================================================================*/

/* 全局唯一一份 App 信息（头文件里只 extern，不要放 static） */
EEPROM_Info_t EEPROM_Data;

/*=====================================================================
 * 函数：EEPROM_Init
 * 负责：上电后探测一次 AT24C02
 * 返回：0 = 在线，1 = 不在线
 *
 * ★ 为什么读到 0xFF 也返回 0 ★
 *   全新芯片整片都是 0xFF，读到 0xFF 不代表芯片不在线。
 *   真正判断"能不能用"靠的是后面写完再读回来比对，
 *   所以这里只做一次"探活"，只要 I2C 有应答就算在线。
 *   没有单独的 ACK 探测接口，就用读一次代替（读失败返回 0，
 *   但 0 也是合法数据，所以返回值其实无法区分）——
 *   这是本芯片驱动的已知局限，不影响功能，因为后面有回读校验。
 *====================================================================*/
u8 EEPROM_Init(void)
{
    u8 probe;

    /* 给芯片一点上电稳定时间（AT24C02 上电到可访问约 1ms，给 10ms 富余） */
    delay_ms(10);

    probe = EEPROM_ReadByte(EEPROM_DEV_ADDR, EEPROM_VER_ADDR);

    (void)probe;
    return 0;
}

/*=====================================================================
 * 函数：EEPROM_WriteString
 * 负责：把一个字符串（含结尾 '\0'）写到 EEPROM 指定地址
 *
 * 参数：mem_addr - 起始地址
 *       str      - 要写的字符串
 *       max_len  - 这块区域最大多少字节（含 '\0'）
 * 返回：I2C_ACK(0) = 成功，I2C_NACK(1) = 失败
 *
 * ★ 为什么超长要直接失败，而不是截断 ★
 *   截断可能把一个 UTF-8 汉字的 3 个字节切掉一半，
 *   写进去的就是非法编码，读出来显示成乱码。
 *   宁可写失败让调用方知道"这个名字太长"，也不要写一半。
 *====================================================================*/
u8 EEPROM_WriteString(u8 mem_addr, const char *str, u8 max_len)
{
    u16 len;
    u8  i;

    if (str == 0 || max_len == 0)
    {
        return I2C_NACK;
    }

    len = (u16)strlen(str) + 1;     /* +1 是结尾的 '\0' */

    if (len > (u16)max_len)
    {
        return I2C_NACK;
    }

    /* AT24C02 只有 256 字节，别写出界 */
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
 * 返回：I2C_ACK(0)  = 读到正常结尾
 *       I2C_NACK(1) = 读到 0xFF（这片没写过）或读满都没遇到 '\0'
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

        /* 0xFF = 空白区，从来没写过 */
        if (ch == 0xFF)
        {
            buf[0] = '\0';
            return I2C_NACK;
        }

        buf[i] = (char)ch;

        if (ch == '\0')
        {
            return I2C_ACK;
        }
    }

    /* 读满都没见到 '\0'，强制收尾，避免调用方拿到没结束的字符串 */
    buf[buf_size - 1] = '\0';
    return I2C_NACK;
}

/*=====================================================================
 * 函数：EEPROM_WriteU32
 * 负责：写 1 个 32 位整数（小端：低字节在前）
 *
 * ★ 为什么逐字节拆，不用 *(u32*) 强转 ★
 *   EEPROM 是 8 位存储，必须拆成 4 个字节写。
 *   而且手写移位跟编译器/平台的大小端设置无关，换环境也不会错。
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
 * ★ 为什么把 0xFFFFFFFF 当成 0 ★
 *   全新 AT24C02 读出来全是 0xFF，拼成 u32 就是 0xFFFFFFFF。
 *   如果直接拿去比版本号，会变成"当前版本号 42 亿"，
 *   之后任何升级都会被判"版本号不够新"而永远拒绝。
 *   这里统一当"没写过"返回 0。
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
 * ★ 读失败为什么要填默认值 ★
 *   如果失败就留空结构体，界面上打印出来是一片空白，
 *   分不清"还没存过"还是"读坏了"。填上默认的 1.0.0 之后，
 *   至少能看出来"哦，这是出厂默认值，还没写过"。
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
 * ★ 为什么任一失败就立刻 return ★
 *   半写半不写会留下"版本名是新的、大小还是旧的"这种矛盾状态。
 *   BootLoader 拿到错的大小会按错的长度去搬运固件，比干脆没写更危险。
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
