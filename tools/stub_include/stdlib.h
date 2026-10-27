/*=============================================================================
 * stdlib.h —— IntelliSense 专用 stub
 * ★ 只给 VS Code 用，不参与 Keil 编译 ★
 *===========================================================================*/
#ifndef _STDLIB_H_STUB
#define _STDLIB_H_STUB

#include <stddef.h>

/*---------- 数值转换 ----------*/
int    atoi(const char *s);
long   atol(const char *s);
double atof(const char *s);
long   strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
double strtod(const char *s, char **endptr);

/*---------- 内存分配 ----------*/
void  *malloc(size_t size);
void  *calloc(size_t nmemb, size_t size);
void  *realloc(void *ptr, size_t size);
void   free(void *ptr);

/*---------- 进程控制 ----------*/
void   abort(void);
void   exit(int status);
int    atexit(void (*func)(void));

/*---------- 环境 ----------*/
char  *getenv(const char *name);

/*---------- 排序与查找 ----------*/
void   qsort(void *base, size_t nmemb, size_t size,
             int (*compar)(const void *, const void *));
void  *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
               int (*compar)(const void *, const void *));

/*---------- 整数运算 ----------*/
int    abs(int j);
long   labs(long j);
int    rand(void);
void   srand(unsigned int seed);

/*---------- 常量 ----------*/
#define RAND_MAX 32767
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif /* _STDLIB_H_STUB */
