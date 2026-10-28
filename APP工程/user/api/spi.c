/*=====================================================================
 * 文件：spi.c
 * 说明：SPI2 + W25Q128 驱动实现
 * 负责：D
 *
 * ★完成顺序建议★：
 *   第 1 步：SPI2_Init + SPI2_ReadWriteByte
 *   第 2 步：W25Q128_ReadID（读到 0xEF4018 就成功一半）
 *   第 3 步：W25Q128_WaitBusy + W25Q128_WriteEnable
 *   第 4 步：擦除、写、读
 *   第 5 步：W25Q128_SelfTest（跑通就算完成）
 *====================================================================*/
#include "spi.h"
#include "delay.h"
#include "usart.h"

/*=====================================================================
 * 函数：SPI2_Init
 * 负责：D
 *
 * 实现要点：
 *   1. 开时钟：RCC_APB2Periph_GPIOB + RCC_APB1Periph_SPI2
 *      ★SPI2 挂在 APB1（36MHz），不是 APB2★
 *   2. PB13/14/15 配 GPIO_Mode_AF_PP（复用推挽）
 *   3. PB12 配 GPIO_Mode_Out_PP（推挽输出），默认拉高
 *   4. SPI 参数：
 *        SPI_Mode_Master / SPI_DataSize_8b
 *        SPI_CPOL_High + SPI_CPHA_2Edge   （Mode 3）
 *        SPI_NSS_Soft / SPI_BaudRatePrescaler_2（36M/2=18MHz）
 *        SPI_FirstBit_MSB
 *   5. SPI_Cmd(SPI2, ENABLE)
 *====================================================================*/
void SPI2_Init(void)
{
    // 定义结构体
    GPIO_InitTypeDef GPIO_InitStructure;
    SPI_InitTypeDef SPI_InitStructure;
    // 使能时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    // 配置 GPIO
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15; // PB13/14/15

    /*
        配置为复用推挽输出模式
        因为 SPI2 是主机模式，所以 SCK、MOSI、MISO 都是推挽输出模式
        其次因为均为 片上外设 所以使用 复用模式
    */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 50MHz
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 该引脚为 片选引脚，配置为推挽输出模式 [CS]
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;  // 推挽输出模式
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz; // 50MHz
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    W25Q_CS_HIGH();

    SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;

    // 配置为 8 bit 是因为 该 SPI 连接为 W25Q128 设备，该设备协议规定为 8 bit
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    /*
        输出时钟为高电平，采样时钟为第二个上升沿
        之所以是上升沿有效是因为下一行配置有效边沿为第二个边沿，
        而配置时钟线空闲为 高电平，
        所以第一个边沿为下降沿，而第二个边沿为上升沿
    */
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
    // 第二个边沿为有效边沿
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;

    // 设置为软件管理，如果使用硬件管理他会主动放手从而不发送数据>>罢工
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;

    /*
        设置为 18MHz：APB1 = 36MHz，2 分频 = 18MHz
        这个值是有依据的，不是"感觉没问题"：
          STM32F103 数据手册规定 SPI 最高时钟就是 18MHz（fPCLK/2），
          W25Q128 本身支持到 104MHz，所以瓶颈在 STM32 这边。
          18MHz 正好是 F103 的规格上限，能跑，但没有余量。
        如果调试中出现数据错乱/读 ID 不对，第一件事就是改成
          SPI_BaudRatePrescaler_4（9MHz）再试
    */
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4; // 36M/2=18MHz

    // 设置为高位在前，考虑到读取需要拼接字节，设置为高位在前对应高位先出，读出数据后左移8位可以自动补0，可以直接与逻辑下一个字节拼接
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB; // 高位在前

    SPI_InitStructure.SPI_CRCPolynomial = 7;

    // 初始化 SPI2
    SPI_Init(SPI2, &SPI_InitStructure);

    // 使能 SPI2
    SPI_Cmd(SPI2, ENABLE);
}

/*=====================================================================
 * 函数：SPI2_ReadWriteByte
 * 负责：D
 *
 * ★全双工的本质★：发一个字节的同时必然收一个字节。
 *   所以"读"也是发 0xFF（发什么无所谓，收的才是有用的）。
 *
 * 三个标志位：
 *   TXE（发送缓冲空）  = 1 表示可以往里塞下一个字节了
 *   RXNE（接收缓冲非空）= 1 表示收到一个字节，可以取走了
 *   BSY（总线忙）      = 1 表示还在移位，此时不能动 CS
 *====================================================================*/
u8 SPI2_ReadWriteByte(u8 data)
{
    // 设置超时时间
    u32 timeout = 0;

    // 如果 TXE 没有置位，则一直等待直到超时
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET)
    {
        if (++timeout > 50000)
            return 0xFF; // 超时返回 0xFF，主函数判断是否为错误码
    }

    // TXE 置位了，说明发送缓冲空，可以塞数据
    SPI_I2S_SendData(SPI2, data);

    // 重置超时时间
    timeout = 0;

    // 如果 RXNE 没有置位，则一直等待直到超时
    while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE) == RESET)
    {
        if (++timeout > 50000)
            return 0xFF; // 超时返回 0xFF，主函数判断是否为错误码
    }

    // 成功则返回接收到的数据
    return SPI_I2S_ReceiveData(SPI2);
}

/*=====================================================================
 * 函数：W25Q128_ReadID
 * 负责：D
 * 功能: 返回 W25Q128 的 JEDEC ID
 *
 * ★★ CS 必须成对：LOW 开始，HIGH 结束 ★★
 *   漏掉 HIGH 的话，芯片会认为这条指令永远没结束，
 *   后面所有操作都被当成"继续读 ID"，时序全乱。
 *====================================================================*/
u32 W25Q128_ReadID(void)
{
    u32 id = 0;

    // 准备三个 u8 拼凑 24 位
    u8 m, t, c;

    // 先拉低 CS 开始发送
    W25Q_CS_LOW();

    // 发送读取 ID 指令
    SPI2_ReadWriteByte(W25Q_JEDEC_ID);

    // 读取三个字节
    m = SPI2_ReadWriteByte(0xFF);
    t = SPI2_ReadWriteByte(0xFF);
    c = SPI2_ReadWriteByte(0xFF);

    // 事务结束，必须拉高 CS
    W25Q_CS_HIGH();

    id = ((u32)m << 16 | (u32)t << 8 | (u32)c);
    return id;
}

/*=====================================================================
 * 函数：W25Q128_Init
 * 负责：D
 *
 * 实现要点：
 *   1. delay_ms(10)      上电后等一会儿
 *   2. id = W25Q128_ReadID()
 *   3. return (id == 0xEF4018) ? 0 : 1
 *
 * 返回：0 = 成功，1 = 失败
 *====================================================================*/
u8 W25Q128_Init(void)
{
    u32 id;

    // 上电后等一会儿，Flash 内部需要时间稳定
    delay_ms(10);

    id = W25Q128_ReadID();

    /*----------------------------------------------------------------
     * ★判定规则（2026-10-27 上板实测后修正）★
     *
     * 原来这里写的是 if (id == 0xEF4018)，只能认 Winbond 原厂片。
     * 实测本板读回 0x522118（厂商 0x52 不是 Winbond），但容量码
     * 同样是 0x18 = 16MB，而且 0x000000~0xFFF000 全段实测可擦可写。
     *
     * 所以改成「只校验容量码」：
     *   id 低 8 位 == 0x18  → 容量 16MB，与 W25Q128 等价 → 通过
     *   0xEF4018（Winbond 原厂）低 8 位也是 0x18 → 照样通过
     *
     * 如果换成 8MB(0x17) 或 4MB(0x16) 的片子，这里会返回 1，
     * 此时要看主程序打印的实际 ID，再决定是否放行。
     *----------------------------------------------------------------*/
    if ((id & W25Q128_CAP_MASK) == W25Q_CAP_16MB)
        return 0;

    return 1;
}

/*=====================================================================
 * 函数：W25Q128_WaitBusy
 * 负责：D
 *
 * 实现要点：
 *   1. 拉低 CS → 发 0x05
 *   2. do { status = 读一字节 } while(status & 0x01)
 *   3. 拉高 CS
 *
 * ★注意★：CS 必须在循环外面拉低、循环结束后才拉高。
 *         如果每读一次状态就松一次 CS，读到的永远是上一次的快照。
 *
 * ★超时值为什么是 6000000★：
 *   查 W25Q128 数据手册 AC Characteristics：
 *     扇区擦除 tSE  typ 45ms / max 400ms   （4KB）
 *     块擦除   tBE2 typ 150ms / max 2000ms （64KB）
 *   本函数一次循环 = 一次 SPI2_ReadWriteByte（18MHz 下 8 bit ≈ 0.44us）
 *                 + 循环开销，实测约 0.6us。
 *   6000000 × 0.6us ≈ 3.6 秒，能覆盖 64KB 块擦除的最坏情况 2 秒。
 *   （如果降速到 9MHz，一次约 1.2us，6000000 × 1.2us ≈ 7.2 秒，更保险）
 *   ★如果只给 1000000，18MHz 下只有 0.6 秒，64KB 擦除还没完就超时退出了，
 *     接着写数据会写进正在擦除的块里，结果是固件损坏且不报错。★
 *====================================================================*/
void W25Q128_WaitBusy(void)
{
    u8 status = 0;
    u32 timeout = 0;

    W25Q_CS_LOW();                        // 1. 片选拉低，整个轮询期间保持
    SPI2_ReadWriteByte(W25Q_READ_STATUS); //    发 0x05（读状态寄存器 1）

    do
    {
        status = SPI2_ReadWriteByte(0xFF); // 2. 读一字节状态
        if (++timeout > 6000000)
            break; //    超时保护，留够时间确保擦除完毕
    } while (status & 0x01); //    bit0 = BUSY，1 表示还在忙

    W25Q_CS_HIGH(); // 3. 片选拉高
}

/*=====================================================================
 * 函数：W25Q128_WriteEnable
 * 负责：D
 *
 * 实现要点：拉低 CS → 发 0x06 → 拉高 CS
 *
 * ★为什么每次擦/写之前都要发★：
 *   W25Q128 内部有个 WEL（Write Enable Latch）位，
 *   每完成一次擦除或写入，硬件会自动把 WEL 清 0。
 *   所以不能"发一次管一辈子"，每次都得重新发。
 *====================================================================*/
void W25Q128_WriteEnable(void)
{
    W25Q_CS_LOW();
    SPI2_ReadWriteByte(W25Q_WRITE_ENABLE); // 0x06
    W25Q_CS_HIGH();
}

/*=====================================================================
 * 函数：W25Q128_EraseSector
 * 负责：D
 *
 * 实现要点：
 *   addr = sector * 4096
 *   WriteEnable()
 *   拉低 CS → 0x20 → 3 字节地址 → 拉高 CS
 *   WaitBusy()
 *====================================================================*/
void W25Q128_EraseSector(u16 sector)
{
    // 一个扇区大小为 4KB
    u32 addr = (u32)sector * 4096;

    // 写和擦之前必须发送 0x06 写使能命令
    W25Q128_WriteEnable();

    W25Q_CS_LOW();
    SPI2_ReadWriteByte(W25Q_SECTOR_ERASE);             // 0x20
    SPI2_ReadWriteByte((u8)((addr & 0xFF0000) >> 16)); // 地址高字节
    SPI2_ReadWriteByte((u8)((addr & 0x00FF00) >> 8));  // 地址中字节
    SPI2_ReadWriteByte((u8)(addr & 0x0000FF));         // 地址低字节
    W25Q_CS_HIGH();                                    // 事务结束，拉高

    W25Q128_WaitBusy(); // 等擦完（4KB：typ 45ms / max 400ms）
}

/*=====================================================================
 * 函数：W25Q128_EraseBlock
 * 负责：D
 *
 * 实现要点：同 EraseSector，指令 0xD8，addr = block * 65536
 *====================================================================*/
void W25Q128_EraseBlock(u8 block)
{
    // 一个块大小为 64KB
    u32 addr = (u32)block * 65536;

    W25Q128_WriteEnable();

    W25Q_CS_LOW();
    SPI2_ReadWriteByte(W25Q_BLOCK_ERASE); // 0xD8
    SPI2_ReadWriteByte((u8)((addr & 0xFF0000) >> 16));
    SPI2_ReadWriteByte((u8)((addr & 0x00FF00) >> 8));
    SPI2_ReadWriteByte((u8)(addr & 0x0000FF));
    W25Q_CS_HIGH();

    W25Q128_WaitBusy(); // 等擦完（64KB：typ 150ms / max 2000ms）
}

/*=====================================================================
 * 函数：W25Q128_WritePage
 * 负责：D
 *
 * 实现要点：
 *   addr = page * 256
 *   WriteEnable()
 *   拉低 CS → 0x02 → 3 字节地址 → 256 字节数据 → 拉高 CS
 *   WaitBusy()
 *
 * ★注意★：这个函数写死写满 256 字节（一页）。
 *         如果要写不满一页的数据，用 W25Q128_Write()，它会处理读-改-写。
 *====================================================================*/
void W25Q128_WritePage(u16 page, u8 *buf)
{
    // 一页大小为 256 字节
    u32 addr = (u32)page * 256;

    //
    u16 i;

    W25Q128_WriteEnable();

    W25Q_CS_LOW();
    SPI2_ReadWriteByte(W25Q_PAGE_PROGRAM); // 0x02
    SPI2_ReadWriteByte((u8)((addr & 0xFF0000) >> 16));
    SPI2_ReadWriteByte((u8)((addr & 0x00FF00) >> 8));
    SPI2_ReadWriteByte((u8)(addr & 0x0000FF));

    // 连续写 256 字节
    for (i = 0; i < 256; i++)
    {
        SPI2_ReadWriteByte(buf[i]);
    }

    // 保证总线空闲为 高电平
    W25Q_CS_HIGH();

    W25Q128_WaitBusy(); // 等写完（一页：typ 0.7ms / max 3ms）
}

/*=====================================================================
 * 函数：W25Q128_Read
 * 负责：D
 *
 * 实现要点：
 *   拉低 CS → 0x03 → 3 字节地址
 *   循环 len 次：buf[i] = SPI2_ReadWriteByte(0xFF)
 *   拉高 CS
 *
 * ★读操作不需要 WriteEnable，也不需要 WaitBusy★
 *   因为读不会改变 Flash 内容，也不会有内部擦写过程。
 *====================================================================*/
void W25Q128_Read(u32 addr, u8 *buf, u32 len)
{
    u32 i;

    W25Q_CS_LOW();

    // 随意发送内容，注意高位在前，低位在后，保证读取时为 高前低后
    SPI2_ReadWriteByte(W25Q_READ_DATA); // 0x03
    SPI2_ReadWriteByte((u8)((addr & 0xFF0000) >> 16));
    SPI2_ReadWriteByte((u8)((addr & 0x00FF00) >> 8));
    SPI2_ReadWriteByte((u8)(addr & 0x0000FF));

    for (i = 0; i < len; i++)
    {
        buf[i] = SPI2_ReadWriteByte(0xFF); // 发什么无所谓，收的才有用
    }

    W25Q_CS_HIGH();
}

/*=====================================================================
 * 函数：W25Q128_Write
 * 负责：D
 *
 * ★★ 最容易出错的地方 ★★
 *
 * 实现要点：
 *   while(offset < len){
 *       page_addr   = (addr + offset) & ~0xFF;      // 页对齐
 *       page_offset = (addr + offset) & 0xFF;       // 页内偏移
 *       write_len   = min(256 - page_offset, len - offset);
 *
 *       if(write_len < 256){
 *           W25Q128_Read(page_addr, page_buf, 256);  // 先读整页
 *           for(i=0;i<write_len;i++) page_buf[page_offset+i] = buf[offset+i];
 *           W25Q128_WritePage(page_addr/256, page_buf);
 *       }else{
 *           W25Q128_WritePage(page_addr/256, &buf[offset]);
 *       }
 *       offset += write_len;
 *   }
 *
 * ★坑：不要在循环里擦扇区！跨扇区会把已写数据擦掉。调用前先擦好整个范围。
 * ★为什么不能直接写半页★：
 *   页编程指令只能把 bit 从 1 写成 0，不能从 0 写回 1。
 *   所以想改半页，必须先把整页读出来、改掉要改的那几个字节、
 *   擦掉整页、再把 256 字节整页写回去。
 *====================================================================*/
u8 W25Q128_Write(u32 addr, u8 *buf, u32 len)
{
    /*
        ★为什么用 static★：
        256 字节放栈上会吃掉一大块 RAM（本工程 RAM 只有 64KB），
        而且这个函数不会重入（单线程裸机），用 static 放 .bss 最省。
    */
    static u8 page_buf[256];

    u32 offset = 0;
    u32 page_addr;
    u32 page_offset;
    u32 write_len;
    u32 i;

    /* 越界保护：本板 Flash 实测为 16MB = 0x1000000
     *
     * ★为什么不能写 addr + len > 0x1000000★
     *   addr 和 len 都是 u32，addr + len 会回绕：
     *     addr=0x001000, len=0xFFFFFF00 → 0x00000F00，检查"通过"，
     *     然后越界写到不存在的地址上。
     *   正确写法是先比 addr 自己，再用减法比 len，永远不回绕。 */
    if (addr >= 0x1000000 || len > 0x1000000 - addr)
        return 1;

    while (offset < len)
    {
        page_addr = (addr + offset) & ~0xFF;  // 页对齐（低 8 位清零）
        page_offset = (addr + offset) & 0xFF; // 页内偏移
        write_len = 256 - page_offset;        // 本页还能写多少
        if (write_len > (len - offset))
        {
            write_len = len - offset; // 但不超过剩余长度
        }
        if (write_len < 256)
        {
            // 不满一页 → 读-改-写
            W25Q128_Read(page_addr, page_buf, 256);
            for (i = 0; i < write_len; i++)
            {
                page_buf[page_offset + i] = buf[offset + i];
            }
            W25Q128_WritePage((u16)(page_addr / 256), page_buf);
        }
        else
        {
            // 正好一整页 → 直接写，省一次读
            W25Q128_WritePage((u16)(page_addr / 256), &buf[offset]);
        }

        offset += write_len;
    }

    return 0;
}

/*=====================================================================
 * 函数：W25Q128_SelfTest
 * 负责：D
 *
 * 测试流程（串口打印）：
 *   ===== W25Q128 自测 =====
 *   JEDEC ID：0xEF4018
 *   ID 正确
 *   擦除完成 / 擦除后全 FF
 *   写入完成 / 读回一致
 *   自测全部通过
 *
 * 返回：0 = 全部通过
 *       1 = ID 不对（SPI 没通）
 *       2 = 擦除后不是全 FF（擦除失败/芯片坏）
 *       3 = 读回不一致（写入失败）
 *
 * ★注意：这个函数会擦掉扇区 0（前 4KB）★
 *   在 APP 里跑没关系（数据在 W25Q128，不在 MCU Flash），
 *   但正式下载固件前别乱跑。
 *====================================================================*/
u8 W25Q128_SelfTest(void)
{
    u32 id;
    u8 i;
    u8 buf[16];
    char msg[80];

    Usart1_SendString("\r\nW25Q128 自测 \r\n");

    /* --- 第 1 步：读 ID --- */
    id = W25Q128_ReadID();
    sprintf(msg, "JEDEC ID：0x%06X\r\n", (unsigned int)id);
    Usart1_SendString(msg);

    /* 判定同 W25Q128_Init：只校验容量码 0x18(16MB)，不锁死厂商 */
    if ((id & W25Q128_CAP_MASK) != W25Q_CAP_16MB)
    {
        Usart1_SendString("ID 错误！\r\n");
        if (id == 0x000000)
        {
            Usart1_SendString("  0x000000 = 数据线 没接 / SPI 没使能 / CS 没拉低\r\n");
        }
        else if (id == 0xFFFFFF)
        {
            Usart1_SendString("  0xFFFFFF = 数据线电平 被拉高 / 芯片没供电\r\n");
        }
        else
        {
            Usart1_SendString("  容量码不是 0x18(16MB)，时钟模式或分频也可能有问题，试试降到 9MHz\r\n");
        }
        return 1;
    }
    if (id == W25Q128_JEDEC_ID)
        Usart1_SendString("ID 正确（Winbond 原厂 16MB）\r\n");
    else
        Usart1_SendString("ID 通过（非 Winbond 厂商，容量码 0x18 = 16MB）\r\n");

    /* --- 第 2 步：擦除扇区 0，验证全 FF --- */
    W25Q128_EraseSector(0);
    Usart1_SendString("擦除完成\r\n");

    W25Q128_Read(0, buf, 16);
    for (i = 0; i < 16; i++)
    {
        if (buf[i] != 0xFF)
        {
            sprintf(msg, "擦除后第 %d 字节是 0x%02X，不是 0xFF\r\n", i, buf[i]);
            Usart1_SendString(msg);
            return 2;
        }
    }
    Usart1_SendString("擦除后全 FF\r\n");

    /* --- 第 3 步：写入 16 字节图案，读回比对 --- */
    for (i = 0; i < 16; i++)
    {
        buf[i] = (u8)(0xA0 + i); // A0 A1 A2 ... AF
    }

    W25Q128_Write(0, buf, 16);
    Usart1_SendString("写入完成\r\n");

    // 先把 buf 清零，确保读回来的不是刚才留在 RAM 里的值
    for (i = 0; i < 16; i++)
    {
        buf[i] = 0;
    }

    W25Q128_Read(0, buf, 16);
    for (i = 0; i < 16; i++)
    {
        if (buf[i] != (u8)(0xA0 + i))
        {
            sprintf(msg, "读回不一致：第 %d 字节 期望 0x%02X 实际 0x%02X\r\n",
                    i, (unsigned int)(0xA0 + i), (unsigned int)buf[i]);
            Usart1_SendString(msg);
            return 3;
        }
    }
    Usart1_SendString("读回一致\r\n");

    /* --- 第 4 步：擦干净，别留垃圾 --- */
    W25Q128_EraseSector(0);

    Usart1_SendString("自测全部通过\r\n");
    return 0;
}
