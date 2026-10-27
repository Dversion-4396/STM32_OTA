/* ============================================================================
 *  usart.c —— 三个 USART 的初始化与收发
 *
 *  对应关系：
 *      USART1   PA9(TX) / PA10(RX)   挂 APB2，PCLK2 = 72MHz，接 CH340 到电脑
 *      USART2   PA2(TX) / PA3(RX)    挂 APB1，PCLK1 = 36MHz
 *      USART3   PB10(TX)/ PB11(RX)   挂 APB1，PCLK1 = 36MHz，接 ESP8266(WiFi)
 *
 *  三个串口都是 8 位数据位、无校验、1 位停止位，初始化写法一致，只有
 *  时钟总线（72MHz / 36MHz）和引脚不同。
 * ========================================================================== */
////当flag为1时为寄存器配置为0时候为标准库
#define MY_FLAG 0
#include "usart.h"
#include "delay.h"
/* tim3_pwm.h 已移除：BootLoader 不需要 PWM */
/* ----------------------------------------------------------------------------
 *  USART1：PA9=TX，PA10=RX
 * -------------------------------------------------------------------------- */
USART_Data usart1_value = {0};

USART_Data usart2_value = {0};

USART_Data usart3_value = {0};
void Usart1_Init(u32 brr)
{
    /// 对 gpioa 和 usart1 进行时钟使能
    RCC->APB2ENR |= RCC_APB2_GPIOA_CLOCK_ENABLE | RCC_APB2_USART1_CLOCK_ENABLE;
    /// 对 tx===pa9 进行初始化（复用推挽输出）
    GPIOA->CRH &= ~(0x0f << 4);
    GPIOA->CRH |= (Alternate_Push_And_Pull_Speed_10MHz << 4);
    /// 对 rx===pa10 进行初始化（浮空输入）
    GPIOA->CRH &= ~(0x0f << 8);
    GPIOA->CRH |= (Floating_Input_Mode << 8);

    //------------------------------------
    // 配置 usart 模块
    /// 首先明确串口为关闭状态再进行配置
    Usart1_Off;
    /// 对 cr1 第 12 位设为 0：一个起始位 + 8 个数据位
    USART1->CR1 &= ~(0x01 << 12);
    /// 对第 10 位设 0：不需要校验位
    USART1->CR1 &= ~(0x01 << 10);
    /// 对 cr2 的 12、13 位设为 00：一个停止位
    USART1->CR2 &= ~(0x03 << 12);

    /// 然后对其波特率进行配置
    ///     USARTDIV = PCLK / 16 / brr，PCLK = 72MHz
    ///     BRR 的高 12 位是整数部分，低 4 位是小数部分（1/16 单位）
    ///     例：9600 @72MHz → div = 7500 → 整数 468、小数 12 → BRR = 0x1D4C
    /// 【注意】必须用整数运算：浮点写法在某些编译设置下会丢掉小数位，
    ///         BRR 一旦算错，波特率就跟着错，对方收到的是乱码。
    u32 usart_div = 72000000 / brr;                              // 16 倍的 USARTDIV
    u16 usart_mantissa = usart_div / 16;                         // 整数部分
    u32 usart_remainder = usart_div - usart_mantissa * 16;       // 余数
    u16 usart_fraction = (usart_remainder * 16 + brr / 2) / brr; // 小数部分，四舍五入
    if (16 == usart_fraction)                                    // 0.999 这种情况要进位
    {
        usart_fraction = 0;
        usart_mantissa += 1;
    }
    USART1->BRR = (usart_mantissa << 4) | usart_fraction;

    USART1->CR1 |= (0x01 << 5); // 使能 RXNE 中断
    USART1->CR1 |= (0x01 << 4); // 使能 IDLE 中断

    // 【中断配置补充】2. 使用 NVIC 注册对应 USART1_IRQn 同时配置优先级
    //  void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority) 设置中断优先级
    NVIC_SetPriority(USART1_IRQn, 1);
    // void NVIC_EnableIRQ(IRQn_Type IRQn) 使能当前中断
    NVIC_EnableIRQ(USART1_IRQn);
    /// 然后开放 tx 与 rx
    USART1->CR1 |= (0x01 << 3); // tx 发送使能
    USART1->CR1 |= (0x01 << 2); // rx 接收使能
    /// 设置完成后将其打开
    Usart1_Open;
}

void USART1_IRQHandler(void)
{
    if (usart1_value.flag)
    {
        memset(&usart1_value, 0, sizeof(USART_Data));
    }
    GPIO_SetBits(GPIOD, GPIO_Pin_7);

    /*
    【处理 RXNE 中断】
        1. 根据 USART1->SR 状态寄存器判断是否为 RXNE 中断触发
        2. 【重点，清除中断标志位】
            软件清除根据文档要求，是对 USART1->DR 读取操作即可清除
        3. 【数据存储到临时缓冲区】
            需要将 USART1->DR 寄存器中的数据，存储到 USART1_Data 底层
            u8 类型数组中。
    【特殊情况】
        当前 USART1_Data 底层数组有效元素个数已满 == USART1_BUFFER_SIZE
        需要对当前已接收数据进行处理，才可以进行后续的数据接收。
    */
    if (USART1->SR & (0x01 << 5))
    {
        /*
        1. 数据需要存储到 USART1_Data 底层 u8 类型 data 数组中
        2. usart1_value.count++ 作为下标操作，先提供数据，在累加数值
            可以满足数组【尾插法】方式数据添加
        3. USART1->DR 对当前 DR 进行读取操作，提取数据，同时【清除中断】
        */
        usart1_value.data[usart1_value.count++] = USART1->DR;

        if (USART1_BUFFER_SIZE == usart1_value.count)
        {
            // 数据已满，flag 标志位为 1
            usart1_value.flag = 1;

            // 当前 USART1_Data 底层数据已满，需要对数据进行处理
            // 【TODO】当前操作是临时数据，非后续唯一处理方式。

            // 将数据通过 USART1 发送到当前 PC 串口调试工具
            Usart1_SendString("RXNE : ");
            Usart1_SendBuffer(usart1_value.data, usart1_value.count);
            Usart1_SendString("\r\n");
        }
    }
    /*
    【处理 IDLE 中断】
        1. 根据 USART1->SR 状态寄存器判断是否为 IDLE 中断触发
        2. 【重点，清除中断标志位】
            通过软件序列进行中断标志位清除
            先读取 USART1->SR，再读取 USART1->DR
        3. 【数据处理】
            IDLE 表示数据发送完成，进行数据的必要处理
    */
    if (USART1->SR & (0x01 << 4))
    {
        /*
        【清除中断标志位】
        先读取 USART1->SR，再读取 USART1->DR
        在 if 判断中 USART1->SR 已读取，仅需要再次读取 USART1->DR 即可
        */
        USART1->DR;

        // 数据接收完成，flag 标志位为 1
        usart1_value.flag = 1;

        // 临时数据处理，数据回显到 PC 串口调试助手
        Usart1_SendString("IDLE : ");
        Usart1_SendBuffer(usart1_value.data, usart1_value.count);
        Usart1_SendString("\r\n");

        GPIO_SetBits(GPIOD, GPIO_Pin_7);
        Usart2_SendBuffer(usart1_value.data, usart1_value.count);
        Usart2_SendString("\r\n");
        GPIO_ResetBits(GPIOD, GPIO_Pin_7);
    }
    
}
/* ----------------------------------------------------------------------------
 *  USART2：PA2=TX，PA3=RX
 * -------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------------
 *  USART1 发送
 * -------------------------------------------------------------------------- */

/**
 * @brief USART1 模块发送一个字节数据到接收端
 *
 * @param byte 需要进行发送的一个字节数据
 */
void Usart1_SendByte(u8 byte)
{
    /// 【重点】每一次写 DR 之前，必须确认上一次数据已经发送完成
    ///     通过 USART1->SR 的 TC 标志位判断：
    ///         TC = 0  数据尚未发送完成
    ///         TC = 1  数据发送已完成
    ///     TC 的清除方式是"先读 SR、再写 DR"，下面 while 里读 SR 的动作
    ///     本身就完成了清除的第一步；写完 DR 后 TC 自动清零，重新计时。
    while (!(USART1->SR & (0x01 << 6)))
        ; // 读 SR 寄存器，等 TC 置 1
    /// 此为写操作：把数据交给 DR，硬件自动开始发送
    USART1->DR = byte;
}

/**
 * @brief USART1 模块发送指定字节个数的数据
 *
 * @param Buffer 发送数据对应内存空间首地址
 * @param len    发送目标数据字节数
 */
void Usart1_SendBuffer(char *Buffer, u16 len)
{
    while (len--)
    {
        Usart1_SendByte(*Buffer);
        Buffer += 1;
    }
}

/**
 * @brief USART1 模块发送字符串数据到接收端
 *
 * @param str 发送的字符串数据内容
 */
void Usart1_SendString(const char *str)
{
    while (*str)
    {
        Usart1_SendByte(*str);
        str += 1;
    }
}
void Usart2_Init(u32 brr)
{
#if !(MY_FLAG)
    /// 1. 首先使能GPIOA 与USART2模块
    /*
        RCC_APB1PeriphClockCmd(uint32_t RCC_APB1Periph, FunctionalState NewState)
        为标准库函数为APB1进行使能
    */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    /*
    GPIO_InitTypeDef GPIO 模块对应初始化结构体。
    typedef struct
    {
        uint16_t GPIO_Pin;             // 引脚编号
        GPIOSpeed_TypeDef GPIO_Speed;  // 速度要求，如果为输入模式，当前参数无效
        GPIOMode_TypeDef GPIO_Mode;    // GPIO 工作模式
    } GPIO_InitTypeDef;
     */
    /*
        typedef enum
        {
        GPIO_Mode_AIN = 0x0,                    // 模拟输入
        GPIO_Mode_IN_FLOATING = 0x04,           // 浮空输入
        GPIO_Mode_IPD = 0x28,                   // 下拉输入
        GPIO_Mode_IPU = 0x48,                   // 上拉输入
        GPIO_Mode_Out_OD = 0x14,                // 通用开漏
        GPIO_Mode_Out_PP = 0x10,                // 通用推挽
        GPIO_Mode_AF_OD = 0x1C,                 // 复用开漏
        GPIO_Mode_AF_PP = 0x18                  // 复用推挽
        }GPIOMode_TypeDef;                      //
    */
    ////此为PA2 TX设置
    GPIO_InitTypeDef GPIO_InitStructure = {0};
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2; // 对应引脚号为2
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; /// 复用推挽输出
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /// 此为PA3 RX 设置
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3; // 对应引脚号为3
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; /// 浮空输入
    /*
    typedef struct
    {
        uint32_t USART_BaudRate;            // 波特率
        uint16_t USART_WordLength;          // 字长，对应 8 位数据位 or 9 位数据位
        uint16_t USART_StopBits;            // 停止位
        uint16_t USART_Parity;              // 是否采用校验位
        uint16_t USART_Mode;                // 当前 USART 开启模式，Tx 和 Rx
        uint16_t USART_HardwareFlowControl; // 硬件控制 or 软件控制
    } USART_InitTypeDef;
    */
    USART_InitTypeDef USART_InitStructure = {0};
    /// 波特率配置
    USART_InitStructure.USART_BaudRate = brr;
    /// 8n1配置
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    ////使能tx与rx
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    /// 软件触发模式
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    ///// 将设置传入
    USART_Init(USART2, &USART_InitStructure);
    /// 中断使能
    // USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);
    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);

    /*
   typedef struct
   {
       // IRQn 中断请求编号
       uint8_t NVIC_IRQChannel;
       // 中断对应【抢占优先级 占先优先级】
       uint8_t NVIC_IRQChannelPreemptionPriority;
       // 中断对应【次级优先级】
       uint8_t NVIC_IRQChannelSubPriority;
       // 当前中断是否开启标志位
       FunctionalState NVIC_IRQChannelCmd;
   } NVIC_InitTypeDef;
   */
    ///     中断相关配置
    NVIC_InitTypeDef NVIC_InitStructure = {0};
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;

    ////中断初始化函数
    NVIC_Init(&NVIC_InitStructure);

    ////开启串口二模块
    USART_Cmd(USART2, ENABLE);
#endif

#if MY_FLAG
    /// 对 gpioa 和 usart2 进行时钟使能
    RCC->APB2ENR |= RCC_APB2_GPIOA_CLOCK_ENABLE;
    RCC->APB1ENR |= RCC_APB1_USART2_CLOCK_ENABLE;
    /// 对 tx===pa2 进行初始化
    GPIOA->CRL &= ~(0x0f << 8);
    GPIOA->CRL |= (Alternate_Push_And_Pull_Speed_10MHz << 8);
    /// 对 rx===pa3 进行初始化
    GPIOA->CRL &= ~(0x0f << 12);
    GPIOA->CRL |= (Floating_Input_Mode << 12);

    //------------------------------------
    Usart2_Off;
    /// 对 cr1 第 12 位设为 0：一个起始位 + 8 个数据位
    USART2->CR1 &= ~(0x01 << 12);
    /// 对第 10 位设 0：不需要校验位
    USART2->CR1 &= ~(0x01 << 10);
    /// 对 cr2 的 12、13 位设为 00：一个停止位
    USART2->CR2 &= ~(0x03 << 12);

    /// 然后对其波特率进行配置
    ///     USARTDIV = PCLK / 16 / brr，PCLK = 36MHz（USART2 挂在 APB1）
    ///     例：9600 @36MHz → div = 3750 → 整数 234、小数 6 → BRR = 0xEA6
    u32 usart_div = 36000000 / brr;             // 16 倍的 USARTDIV
    u16 usart_mantissa = (u16)(usart_div / 16); // 整数部分
    u32 usart_rem = usart_div % 16;             // 小数部分（1/16 单位的整数）
    u16 usart_fraction = (u16)usart_rem;        // 低 4 位就是它本身
    if (usart_fraction > 15)
        usart_fraction = 15;
    USART2->BRR = (u16)((usart_mantissa << 4) | usart_fraction);

    /// 然后开放 tx 与 rx
    USART2->CR1 |= (0x01 << 3); // tx
    USART2->CR1 |= (0x01 << 2); // rx
    USART2->CR1 |= (0x01 << 5); // 使能 RXNE 中断
    USART2->CR1 |= (0x01 << 4); // 使能 IDLE 中断

    // 【中断配置补充】2. 使用 NVIC 注册对应 USART1_IRQn 同时配置优先级
    //  void NVIC_SetPriority(IRQn_Type IRQn, uint32_t priority) 设置中断优先级
    NVIC_SetPriority(USART2_IRQn, 1);
    // void NVIC_EnableIRQ(IRQn_Type IRQn) 使能当前中断
    NVIC_EnableIRQ(USART2_IRQn);
    /// 然后开放 tx 与 rx
    USART2->CR1 |= (0x01 << 3); // tx 发送使能
    USART2->CR1 |= (0x01 << 2); // rx 接收使能
    /// 设置完成后将其打开
    /// 设置完成后将其打开
    Usart2_Open;
#endif
}
/*
    设置中断相关函数
*/
void USART2_IRQHandler(void)
{

    ////数据缓冲区清除
    if (usart2_value.flag)
    {
        memset(&usart2_value, 0, sizeof(USART_Data));
    }
    u16 data = 0;
    ////处理 RXNE 中断
    if (USART_GetITStatus(USART2, USART_IT_RXNE))
    {
        // 1. 将数据暂存
        data = USART_ReceiveData(USART2);

        // 2. 保证在收到数据之后，立刻将定时计数器 CNT 重新归零
        TIM_SetCounter(TIM7, 0);

        // 3. 判断当前 usart2_value 数据缓冲区中的 count 是否为 0
        if (!usart2_value.count)
        {
            // 如果为 0 表示第一次收到数据，【启动定时器】
            TIM_Cmd(TIM7, ENABLE);
        }

        // 4. 将数据存储到 USART2 接收数据缓冲区中。
        usart2_value.data[usart2_value.count++] = data;

    }
}

/* ----------------------------------------------------------------------------
 *  USART2 发送
 * -------------------------------------------------------------------------- */

/**
 * @brief USART2 模块发送一个字节数据到接收端
 *
 * @param byte 需要进行发送的一个字节数据
 */
void Usart2_SendByte(u8 byte)
{
    while (!USART_GetFlagStatus(USART2, USART_FLAG_TC))
        ;

    /*
    void USART_SendData(USART_TypeDef *USARTx, uint16_t Data)
        指定串口数据发送函数，当前是利用 USART2 完成数据发送。
    */
    USART_SendData(USART2, byte);
}

/**
 * @brief USART2 模块发送指定字节个数的数据
 *
 * @param Buffer 发送数据对应内存空间首地址
 * @param len    发送目标数据字节数
 */
void Usart2_SendBuffer(u8 *Buffer, u16 len)
{
    while (len--)
    {
        Usart2_SendByte(*Buffer);
        Buffer += 1;
    }
}

/**
 * @brief USART2 模块发送字符串数据到接收端
 *
 * @param str 发送的字符串数据内容
 */
void Usart2_SendString(const char *str)
{
    while (*str)
    {
        Usart2_SendByte(*str);
        str += 1;
    }
}

/* ----------------------------------------------------------------------------
 *  USART3：PB10=TX，PB11=RX，接 ESP8266
 * -------------------------------------------------------------------------- */
void Usart3_Init(u32 brr)
{
    // 1. RCC 时钟使能模块，包括 USART3 和 GPIOB
    RCC->APB2ENR |= RCC_APB2_GPIOB_CLOCK_ENABLE;
    RCC->APB1ENR |= RCC_APB1_USART3_CLOCK_ENABLE;

    /*
    2. GPIOB 对应 IO 引脚配置
        PB10 【复用推挽输出模式】
        PB11 【浮空输入模式】
    */
    GPIOB->CRH &= ~(0x0F << 8);
    GPIOB->CRH |= (Alternate_Push_And_Pull_Speed_2MHz << 8);

    GPIOB->CRH &= ~(0x0F << 12);
    GPIOB->CRH |= (Floating_Input_Mode << 12);

    // 【明确关闭 USART3】
    USART3->CR1 &= ~(0x01 << 13);
    /*
    3. USART3 配置
        3.1 8N1 工作模式
        3.2 波特率设置，【重点】当前 STM32F103ZET6 USART3 对应时钟分支
            时钟基础频率为 36 MHz
        3.3 注册当前 USART3 对应 RXNE 和 IDLE 中断
        3.4 使能 USART3 Tx 和 Rx
        3.5 使能 USART3 工作
    */
    //  3.1 8N1 工作模式
    USART3->CR1 &= ~(0x01 << 12); // 8 位数据位 1 位起始位 N 位停止位
    USART3->CR1 &= ~(0x01 << 10); // 0 位校验位，关闭数据校验
    USART3->CR2 &= ~(0x03 << 12); // 明确当前 1 位停止位

    /*
    3.2 波特率设置，【重点】当前 STM32F103ZET6 USART3 对应时钟分支
        时钟基础频率为 36 MHz
    */
    float usart_div = 36000000 / 16 / brr;

    u16 usart_mantissa = (u16)usart_div;
    u16 usart_fraction = (u16)((usart_div - usart_mantissa) * 16);

    USART3->BRR = usart_mantissa << 4 | usart_fraction;

    // 3.3 注册当前 USART3 对应 RXNE 和 IDLE 中断
    USART3->CR1 |= (0x01 << 5); // RXNE 使能
    USART3->CR1 |= (0x01 << 4); // IDLE 使能

    NVIC_SetPriority(USART3_IRQn, 1);
    NVIC_EnableIRQ(USART3_IRQn);

    // 3.4 使能 USART3 Tx 和 Rx
    USART3->CR1 |= (0x01 << 3); // 使能 Tx
    USART3->CR1 |= (0x01 << 2); // 使能 Rx

    // 3.5 使能 USART3 工作
    USART3->CR1 |= (0x01 << 13);
}

/* ★ 中断函数名必须与启动文件向量表里的名字【逐字相同】，一个字都不能差：
 *      startup_stm32f10x_hd.s:119   DCD  USART3_IRQHandler
 *  C 语言区分大小写，写成 Usart3_IRQHandler（小写 s）就是另一个符号。
 *  后果不是编译报错，而是：链接器发现没有任何代码"调用"这个函数，就把它
 *  当无用代码整段删掉，向量表那一格于是落回启动文件的弱符号（默认空壳），
 *  里面是 "B ." —— 原地死循环。现象就是 USART3 一进中断 CPU 永久卡死，
 *  而接收线一空闲 IDLE 标志就是 1，所以 Usart3_Init 一跑完立刻卡住。
 *  结论：名字写成全大写 USART3_IRQHandler 即可，不需要任何额外的"保活"手段。 */
void USART3_IRQHandler(void)
{
    /*------------------------------------------------------------------
     * USART3 中断服务函数（BootLoader 版）
     *
     * ★★ 重要：中断里只做"收数据 + 置标志"，不要放业务逻辑 ★★
     *
     * 原来这里做了三件不该做的事：
     *   1. 每次进中断都重配 PWM（TIM3_CH4_PB1_PWM）
     *      → 严重拖慢中断，4G 数据会丢
     *   2. if(flag) memset 清整个结构体
     *      → 把 flag/count/data 全清掉，数据丢失
     *   3. 判断 "PWM"/"Beep"/"Led" 字符串
     *      → 这是给 WiFi 模块用的，4G 的 AT 数据会误触发
     *
     * 现在改为：只收数据、只置标志，业务逻辑移到主循环
     *------------------------------------------------------------------*/

    /*---------- 1. RXNE：收到一个字节 ----------*/
    if (USART3->SR & (0x01 << 5))
    {
        if (usart3_value.count < USART3_BUFFER_SIZE)
        {
            usart3_value.data[usart3_value.count++] = USART3->DR;
        }
        else
        {
            /* 缓冲满，读掉丢弃，防止溢出 */
            USART3->DR;
        }
    }

    /*---------- 2. IDLE：一帧数据收完 ----------*/
    if (USART3->SR & (0x01 << 4))
    {
        USART3->DR;                  /* 读 DR 清 IDLE 标志 */
        usart3_value.flag = 1;       /* 只置标志，交给主循环处理 */
    }
}

/* ----------------------------------------------------------------------------
 *  USART3 发送
 * -------------------------------------------------------------------------- */

/**
 * @brief USART3 模块发送一个字节数据到接收端
 *
 * @param byte 需要进行发送的一个字节数据
 */
void Usart3_SendByte(u8 byte)
{
    /// 与 USART1/2 同理，写 DR 之前先等 TC 标志位置 1
    while (!(USART3->SR & (0x01 << 6)))
        ;
    USART3->DR = byte;
}

/**
 * @brief USART3 模块发送指定字节个数的数据
 *
 * @param buffer 发送数据对应内存空间首地址
 * @param len    发送目标数据字节数
 */
void Usart3_SendBuffer(char *buffer, u16 len)
{
    while (len--)
    {
        Usart3_SendByte(*buffer);
        buffer += 1;
    }
}

/**
 * @brief USART3 模块发送字符串数据到接收端
 *
 * @param str 发送的字符串数据内容
 */
void Usart3_SendString(const char *str)
{
    while (*str)
    {
        Usart3_SendByte(*str);
        str += 1;
    }
}
void RS485_Init(u16 brr)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOD, ENABLE);
    Usart2_Init(brr);
    GPIO_InitTypeDef GPIO_InitStructure = {0};

    GPIO_InitStructure.GPIO_Pin = RS485_GPIO_PIN;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(RS485_GPIO_GROUP, &GPIO_InitStructure);
    GPIO_ResetBits(RS485_GPIO_GROUP, RS485_GPIO_PIN);
}
void RS485_SendBuffer(u8 *data, u16 count)
{

    // 1. 切换控制当前 485 芯片工作模式引脚为高电平状态【485 发送数据模式】
    GPIO_SetBits(RS485_GPIO_GROUP, RS485_GPIO_PIN);

    // 2. 利用 USART2 模块进行数据发送
    Usart2_SendBuffer(data, count);

    // 3. 判断当前 USART2 对应的 TC 标志位是否完成发送，
    // 如果完成 485 芯片转入接收模式
    while (!USART_GetFlagStatus(USART2, USART_FLAG_TC))
        ;

    // 4. 切换控制当前 485 芯片工作模式引脚为低电平状态【485 接收数据模式】
    GPIO_ResetBits(RS485_GPIO_GROUP, RS485_GPIO_PIN);
}
