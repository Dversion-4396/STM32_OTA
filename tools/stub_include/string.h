/*=============================================================================
 * string.h —— IntelliSense 专用 stub
 * ★ 只给 VS Code 用，不参与 Keil 编译 ★
 *===========================================================================*/
#ifndef _STRING_H_STUB
#define _STRING_H_STUB

#include <stddef.h>

/*---------- 内存操作 ----------*/
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset(void *s, int c, size_t n);
int    memcmp(const void *s1, const void *s2, size_t n);
void  *memchr(const void *s, int c, size_t n);

/*---------- 字符串操作 ----------*/
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);
int    strcmp(const char *s1, const char *s2);
int    strncmp(const char *s1, const char *s2, size_t n);
int    strcoll(const char *s1, const char *s2);
size_t strlen(const char *s);
size_t strxfrm(char *dst, const char *src, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
size_t strcspn(const char *s, const char *reject);
size_t strspn(const char *s, const char *accept);
char  *strpbrk(const char *s, const char *accept);
char  *strstr(const char *haystack, const char *needle);
char  *strtok(char *s, const char *delim);
char  *strerror(int errnum);

#endif /* _STRING_H_STUB */
