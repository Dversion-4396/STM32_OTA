#include "lcd_init.h"
#include "delay.h"
#include "usart.h"

/*
	2.4 寸 ST7789V (240x320) 驱动 —— 底层实现
	==================================================================
	屏幕型号: 10P-2D4TFT_V2.1

	★ 与原来 ST7735S 版本的差异（不只是改尺寸，整段初始化都换了）:
	  1. 帧率寄存器: ST7735S 用 B1/B2/B3/B4，ST7789V 用 PORCTRL(B2)/FRCTRL2(C6)
	  2. 电源寄存器: ST7735S 用 C0/C1/C2/C3/C4，ST7789V 用 LCMCTRL(C0)/VDVVRHEN(C2)/
	     VRHS(C3)/VDVS(C4)/PWCTRL1(D0)
	  3. 伽马: ST7735S 是 16 参数，ST7789V 是 14 参数（正负各 14 个）
	  4. 新增 GCTRL(B7)、VCOMS(BB)、GATECTRL(E4)
	  5. 像素格式: 用 0x55（16bit RGB565），ST7735S 那份写的是 0x05
	  6. GRAM 偏移: ST7735S 需要 +2/+1，ST7789V 240x320 模组通常是 0/0

	★ 为什么用【软件 CS 控制 + 硬件 SPI 数据】:
	  官方例程里 LCD_Writ_Bus 每次都拉低 CS 再拉高, 这是"每字节一个片选周期",
	  对 ST7789V 完全没问题。好处是不用关心连续传输的时序, 代价是慢一点。
	  240x320 全屏 = 76800 像素 = 153600 字节, 每字节一次 CS 翻转,
	  实测约 500~600ms。所以 UI 层绝不能每帧全屏重画。
*/

/**
 *  @brief 初始化 LCD 的 4 个控制引脚 + SPI1
 */
void LCD_GPIO_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    SPI_InitTypeDef   SPI_InitStructure;

    /* 1. 开时钟: GPIOA(RES/SCK/MOSI) + GPIOE(DC/CS/BLK) + SPI1 + AFIO */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOE |
                           RCC_APB2Periph_SPI1  |
                           RCC_APB2Periph_AFIO, ENABLE);

    /* 2. 控制引脚: 推挽输出, 初始都为高
          RES 高 = 不复位
          DC  高 = 数据态
          CS  高 = 未选中
          BLK 低 = 背光先关, 初始化完再开, 避免上电花屏 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitStructure.GPIO_Pin = LCD_RES_PIN;
    GPIO_Init(LCD_RES_PORT, &GPIO_InitStructure);
    LCD_RES_Set();

    GPIO_InitStructure.GPIO_Pin = LCD_DC_PIN | LCD_CS_PIN | LCD_BLK_PIN;
    GPIO_Init(LCD_DC_PORT, &GPIO_InitStructure);
    LCD_DC_Set();
    LCD_CS_Set();
    LCD_BLK_Clr();

    /*
        3. SPI1: PA5 = SCK, PA7 = MOSI, 复用推挽
           ★ PA6(MISO) 不用: 屏幕只收不发, 用 SPI_Direction_1Line_Tx
             这样还能省一个引脚, 也避免 MISO 悬空引入干扰

           ★ 关于 VS1053: 手册注明 PA5/PA6/PA7 也接到 VS1053,
             但 VS1053 的片选被禁止、复位保持, 所以该芯片处于不响应状态,
             这三个脚可以安全用作 SPI1。本工程不用音频功能, 无冲突。
    */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_7;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /*
        4. SPI1 参数
           CPOL=High + CPHA=2Edge 是 ST7789V 要求的方式(模式3)
           预分频 4 -> 72MHz/4 = 18MHz
           ST7789V 写时序支持到 62.5MHz，18MHz 很保守但杜邦线走线更稳。
           想快可以把 SPI_BaudRatePrescaler_4 改成 _2（36MHz），
           但线长超过 10cm 时 36MHz 容易丢字节，建议先保持 18MHz 调通。
    */
    SPI_InitStructure.SPI_Direction         = SPI_Direction_1Line_Tx;
    SPI_InitStructure.SPI_Mode              = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize          = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL              = SPI_CPOL_High;
    SPI_InitStructure.SPI_CPHA              = SPI_CPHA_2Edge;
    SPI_InitStructure.SPI_NSS               = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_4;
    SPI_InitStructure.SPI_FirstBit          = SPI_FirstBit_MSB;
    SPI_InitStructure.SPI_CRCPolynomial     = 7;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_Cmd(SPI1, ENABLE);
}

/**
 *  @brief 往 SPI 总线上写一个字节 (带片选翻转)
 *
 *  ★★ 这里必须等 BSY(总线忙) 清零, 不能只等 TXE —— 踩过的坑 ★★
 *
 *  错误写法(第一次就是这样, 结果屏幕全白):
 *      LCD_CS_Clr();
 *      while(TXE == RESET);          // 只等发送缓冲空
 *      SPI_I2S_SendData(SPI1, dat);
 *      LCD_CS_Set();                 // 立刻拉高片选
 *
 *  为什么错: TXE=1 只表示"发送缓冲空了, 数据已经移进移位寄存器",
 *           但那个字节【还在移位寄存器里一位一位往外发】。
 *           此时立刻拉高 CS, 屏幕认为这一字节的传输被中止 -> 丢弃。
 *           结果就是初始化序列全部无效, 屏幕保持白屏。
 *
 *  正确做法: 等 BSY 清零 —— 它表示整个字节(含移位寄存器)真的发完了。
 */
void LCD_Writ_Bus(u8 dat)
{
    LCD_CS_Clr();

    /* 1. 等发送缓冲空, 才能写新数据 */
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET)
    {
        /* 等 */
    }
    SPI_I2S_SendData(SPI1, dat);

    /*
        2. 等总线真正空闲 —— 这一步不能省!
           BSY=1 表示移位寄存器里还有数据在发。
           72MHz / 4 = 18MHz, 一个字节 8 bit 约 0.44us,
           忙等几十个周期就过去了, 开销可以忽略。
    */
    while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY) == SET)
    {
        /* 等移位寄存器发完 */
    }

    LCD_CS_Set();
}

/**
 *  @brief 写一个字节数据
 */
void LCD_WR_DATA8(u8 dat)
{
    LCD_Writ_Bus(dat);
}

/**
 *  @brief 写 16 位数据 (高字节在前)
 */
void LCD_WR_DATA(u16 dat)
{
    LCD_Writ_Bus((u8)(dat >> 8));
    LCD_Writ_Bus((u8)(dat & 0x00FF));
}

/**
 *  @brief 写命令 (DC 拉低表示命令, 写完恢复数据态)
 */
void LCD_WR_REG(u8 dat)
{
    LCD_DC_Clr();
    LCD_Writ_Bus(dat);
    LCD_DC_Set();
}

/**
 *  @brief 设置显示窗口 (列/行地址范围)
 *
 *  ★ ST7789V 240x320 模组一般不需要偏移（显存 240x320 = 可视区 240x320）。
 *    但有些模组可视区小于显存，会整体偏移。
 *    如果烧上去发现画面整体错位 / 边缘有彩条，改 lcd_init.h 里的
 *    LCD_X_OFFSET / LCD_Y_OFFSET 试（常见 0/80、80/0）。
 */
void LCD_Address_Set(u16 x1, u16 y1, u16 x2, u16 y2)
{
    LCD_WR_REG(0x2A);                       /* 列地址 */
    LCD_WR_DATA(x1 + LCD_X_OFFSET);
    LCD_WR_DATA(x2 + LCD_X_OFFSET);
    LCD_WR_REG(0x2B);                       /* 行地址 */
    LCD_WR_DATA(y1 + LCD_Y_OFFSET);
    LCD_WR_DATA(y2 + LCD_Y_OFFSET);
    LCD_WR_REG(0x2C);                       /* 开始写显存 */
}

/**
 *  @brief 初始化 ST7789V
 *
 *  ★ 整个初始化约 400ms（复位 200ms + 背光 100ms + SleepOut 120ms），
 *    只在开机调用一次，不影响主循环。
 *
 *  ★ 初始化序列参考 ST7789V 数据手册的典型应用电路推荐值，
 *    这是 2.4"/2.8" 240x320 模块最通用的一套。如果烧上去偏色、
 *    对比度不对，优先调 PVGAMCTRL(0xE0)/NVGAMCTRL(0xE1) 两段伽马。
 */
void LCD_Init(void)
{
    LCD_GPIO_Init();

    /* 1. 硬复位: RES 低 100ms -> 高 100ms */
    LCD_RES_Clr();
    delay_ms(100);
    LCD_RES_Set();
    delay_ms(100);

    /* 2. 开背光 (先开再初始化, 有些屏不开背光看不到初始化过程) */
    LCD_BLK_Set();
    delay_ms(100);

    /* 3. 软件复位 */
    LCD_WR_REG(0x01);                       /* SWRESET */
    delay_ms(120);

    /* 4. Sleep out */
    LCD_WR_REG(0x11);                       /* SLPOUT */
    delay_ms(120);

    /* 5. 像素格式: 16 bit/pixel (RGB565) */
    LCD_WR_REG(0x3A);                       /* COLMOD */
    LCD_WR_DATA8(0x55);

    /* 6. 内存数据访问控制 (扫描方向) */
    LCD_WR_REG(0x36);                       /* MADCTL */
#if (USE_HORIZONTAL == 0)
    LCD_WR_DATA8(0x00);                     /* 竖屏 240x320，正常方向 */
#elif (USE_HORIZONTAL == 1)
    LCD_WR_DATA8(0xC0);                     /* 竖屏 240x320，旋转 180° */
#elif (USE_HORIZONTAL == 2)
    LCD_WR_DATA8(0x60);                     /* 横屏 320x240 */
#else
    LCD_WR_DATA8(0xA0);                     /* 横屏 320x240，旋转 180° */
#endif

    /* 7.  porch 设置 (前后肩) */
    LCD_WR_REG(0xB2);                       /* PORCTRL */
    LCD_WR_DATA8(0x0C); LCD_WR_DATA8(0x0C);
    LCD_WR_DATA8(0x00); LCD_WR_DATA8(0x33); LCD_WR_DATA8(0x33);

    /* 8. 栅极电压 */
    LCD_WR_REG(0xB7);                       /* GCTRL */
    LCD_WR_DATA8(0x35);

    /* 9. VCOM 电压 */
    LCD_WR_REG(0xBB);                       /* VCOMS */
    LCD_WR_DATA8(0x19);

    /* 10. LCM 控制 */
    LCD_WR_REG(0xC0);                       /* LCMCTRL */
    LCD_WR_DATA8(0x2C);

    /* 11. VDV 和 VRH 命令使能 */
    LCD_WR_REG(0xC2);                       /* VDVVRHEN */
    LCD_WR_DATA8(0x01);

    /* 12. VRH 设置 (VCOM 高压) */
    LCD_WR_REG(0xC3);                       /* VRHS */
    LCD_WR_DATA8(0x12);

    /* 13. VDV 设置 (VCOM 低压) */
    LCD_WR_REG(0xC4);                       /* VDVS */
    LCD_WR_DATA8(0x20);

    /* 14. 帧率控制: 60Hz */
    LCD_WR_REG(0xC6);                       /* FRCTRL2 */
    LCD_WR_DATA8(0x0F);

    /* 15. 电源控制 1 */
    LCD_WR_REG(0xD0);                       /* PWCTRL1 */
    LCD_WR_DATA8(0xA4); LCD_WR_DATA8(0xA1);

    /* 16. 正极性伽马校正 (14 参数) */
    LCD_WR_REG(0xE0);                       /* PVGAMCTRL */
    LCD_WR_DATA8(0xD0); LCD_WR_DATA8(0x04); LCD_WR_DATA8(0x0D);
    LCD_WR_DATA8(0x11); LCD_WR_DATA8(0x13); LCD_WR_DATA8(0x2B);
    LCD_WR_DATA8(0x3F); LCD_WR_DATA8(0x54); LCD_WR_DATA8(0x4C);
    LCD_WR_DATA8(0x18); LCD_WR_DATA8(0x0D); LCD_WR_DATA8(0x0B);
    LCD_WR_DATA8(0x1F); LCD_WR_DATA8(0x23);

    /* 17. 负极性伽马校正 (14 参数) */
    LCD_WR_REG(0xE1);                       /* NVGAMCTRL */
    LCD_WR_DATA8(0xD0); LCD_WR_DATA8(0x04); LCD_WR_DATA8(0x0C);
    LCD_WR_DATA8(0x11); LCD_WR_DATA8(0x13); LCD_WR_DATA8(0x2C);
    LCD_WR_DATA8(0x3F); LCD_WR_DATA8(0x44); LCD_WR_DATA8(0x51);
    LCD_WR_DATA8(0x2F); LCD_WR_DATA8(0x1F); LCD_WR_DATA8(0x1F);
    LCD_WR_DATA8(0x20); LCD_WR_DATA8(0x23);

    /* 18. 栅极控制 (VGH/VGL) */
    LCD_WR_REG(0xE4);                       /* GATECTRL */
    LCD_WR_DATA8(0x1D); LCD_WR_DATA8(0x00); LCD_WR_DATA8(0x00);

    /* 19. 反显开关
           ★ 2026-11-02 真板实测改成 INVOFF(0x20)。
             原代码是 INVON(0x21)，注释写着"ST7789V 模组出厂多为反显"，
             但用户这块 10P-2D4TFT_V2.1 实测不需要反显：
             开着 INVON 时，C_BG(0x0000 黑) 显示成白色、C_LOG(0xC618 浅灰)
             显示成深灰 —— 整屏颜色反了，背景是白的。
             判断方法：如果屏幕底色发白、深色文字反而看不见，就是反显开关反了。
             改回 INVON 就把 0x20 换成 0x21。 */
    LCD_WR_REG(0x20);                       /* INVOFF */

    /* 20. 正常显示模式 */
    LCD_WR_REG(0x13);                       /* NORON */

    /* 21. 开显示
           ★ 2026-11-02 改：先别开显示，等 LCD_UI_Init() 清完屏再开。
             原因：DISPON 之后屏幕立刻开始扫描 GRAM，而刚上电 GRAM 里是随机数据，
             会看到左上角闪一片花屏（用户反馈"启动的时候左上方好像有东西"）。
             DISPOFF 状态下写 GRAM 是不受影响的，写完了再开显示就一次到位。 */
    LCD_WR_REG(0x28);                       /* DISPOFF，先关着 */
    delay_ms(20);

    Usart1_SendString("[LCD] ST7789V init done, 240x320\r\n");
}

/**
 *  @brief 打开显示（GRAM 清干净之后调）
 *
 *  ★ 为什么不在 LCD_Init() 里直接开：
 *    上电时 GRAM 是随机数据，DISPON 会立刻把随机内容扫上屏，
 *    在 LCD_UI_Init() 清屏之前会有几百毫秒的花屏。
 *    DISPOFF → 清屏/画完 → DISPON，用户就看不到那一段。
 */
void LCD_Display_On(void)
{
    LCD_WR_REG(0x29);                       /* DISPON */
    delay_ms(20);
}
