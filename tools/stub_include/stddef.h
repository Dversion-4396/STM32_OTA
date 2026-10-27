/*=============================================================================
 * stddef.h —— IntelliSense 专用 stub
 * ★ 只给 VS Code 用，不参与 Keil 编译 ★
 *===========================================================================*/
#ifndef _STDDEF_H_STUB
#define _STDDEF_H_STUB

/*---------- 类型 ----------*/
typedef unsigned int   size_t;
typedef int            ptrdiff_t;
typedef unsigned int   wchar_t;

/*---------- 空指针 ----------*/
#ifndef NULL
#define NULL ((void *)0)
#endif

/*---------- 偏移宏 ----------*/
#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif /* _STDDEF_H_STUB */
