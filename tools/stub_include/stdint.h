/*=============================================================================
 * stdint.h —— IntelliSense 专用 stub
 *
 * ★ 这个文件只给 VS Code 的 IntelliSense 用，不参与 Keil 实际编译 ★
 *   Keil 编译时用的是 ARMCC 自带的 <stdint.h>（在 Keil 安装目录里）
 *
 * 作用：给 CMSIS / 标准外设库提供整数类型定义，
 *       让 IntelliSense 能正确解析 stm32f10x.h 和 core_cm3.h。
 *
 * 为什么不用真实头文件：
 *   - ARMCC 的 stdint.h 用 __int64/_ARMABI，GCC 模式解析不了
 *   - MinGW 的 stdint.h 依赖 Windows CRT，链路太脏
 *   本文件零依赖，只提供类型定义，最稳。
 *===========================================================================*/
#ifndef _STDINT_H_STUB
#define _STDINT_H_STUB

/*---------- 8 位 ----------*/
typedef signed char        int8_t;
typedef unsigned char      uint8_t;

/*---------- 16 位 ----------*/
typedef short              int16_t;
typedef unsigned short     uint16_t;

/*---------- 32 位 ----------*/
typedef int                int32_t;
typedef unsigned int       uint32_t;

/*---------- 64 位 ----------*/
typedef long long          int64_t;
typedef unsigned long long uint64_t;

/*---------- 最小/最大宽度整数 ----------*/
typedef int8_t   int_least8_t;
typedef uint8_t  uint_least8_t;
typedef int16_t  int_least16_t;
typedef uint16_t uint_least16_t;
typedef int32_t  int_least32_t;
typedef uint32_t uint_least32_t;
typedef int64_t  int_least64_t;
typedef uint64_t uint_least64_t;

/*---------- 最快宽度整数 ----------*/
typedef int32_t  int_fast8_t;
typedef uint32_t uint_fast8_t;
typedef int32_t  int_fast16_t;
typedef uint32_t uint_fast16_t;
typedef int32_t  int_fast32_t;
typedef uint32_t uint_fast32_t;
typedef int64_t  int_fast64_t;
typedef uint64_t uint_fast64_t;

/*---------- 指针宽度整数（Cortex-M3 是 32 位）----------*/
typedef int32_t  intptr_t;
typedef uint32_t uintptr_t;

/*---------- 最大宽度整数 ----------*/
typedef int64_t  intmax_t;
typedef uint64_t uintmax_t;

/*---------- 边界值宏 ----------*/
#define INT8_MIN    (-128)
#define INT8_MAX    (127)
#define UINT8_MAX   (255)
#define INT16_MIN   (-32768)
#define INT16_MAX   (32767)
#define UINT16_MAX  (65535)
#define INT32_MIN   (-2147483647 - 1)
#define INT32_MAX   (2147483647)
#define UINT32_MAX  (4294967295U)

/*---------- 常量后缀宏 ----------*/
#define INT8_C(x)    (x)
#define INT16_C(x)   (x)
#define INT32_C(x)   (x)
#define UINT8_C(x)   (x)
#define UINT16_C(x)  (x)
#define UINT32_C(x)  (x##U)

#endif /* _STDINT_H_STUB */
