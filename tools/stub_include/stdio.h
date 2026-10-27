/*=============================================================================
 * stdio.h —— IntelliSense 专用 stub
 * ★ 只给 VS Code 用，不参与 Keil 编译 ★
 *===========================================================================*/
#ifndef _STDIO_H_STUB
#define _STDIO_H_STUB

#include <stddef.h>
#include <stdarg.h>

/*---------- FILE 类型（占位）----------*/
typedef struct _FILE FILE;

/*---------- 标准流 ----------*/
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

/*---------- 输出函数（项目里用到 sprintf / printf）----------*/
int printf(const char *format, ...);
int sprintf(char *str, const char *format, ...);
int snprintf(char *str, size_t size, const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list ap);
int vsprintf(char *str, const char *format, va_list ap);
int vsnprintf(char *str, size_t size, const char *format, va_list ap);

/*---------- 输入函数 ----------*/
int scanf(const char *format, ...);
int sscanf(const char *str, const char *format, ...);
int fscanf(FILE *stream, const char *format, ...);

/*---------- 字符 I/O ----------*/
int putchar(int c);
int puts(const char *s);
int getchar(void);
int fputc(int c, FILE *stream);
int fputs(const char *s, FILE *stream);
int fgetc(FILE *stream);
char *fgets(char *s, int size, FILE *stream);

/*---------- 文件操作 ----------*/
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void perror(const char *s);

/*---------- 常量 ----------*/
#define EOF    (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif /* _STDIO_H_STUB */
