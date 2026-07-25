#ifndef MSL_PPC32_SHIM_STDIO_H
#define MSL_PPC32_SHIM_STDIO_H
#include <stdarg.h>
#include <stddef.h>
typedef struct _IO_FILE FILE;
extern FILE* stdin;
extern FILE* stdout;
extern FILE* stderr;
int printf(const char*, ...);
int fprintf(FILE*, const char*, ...);
int sprintf(char*, const char*, ...);
int snprintf(char*, size_t, const char*, ...);
int vprintf(const char*, va_list);
int vfprintf(FILE*, const char*, va_list);
int vsprintf(char*, const char*, va_list);
int vsnprintf(char*, size_t, const char*, va_list);
int puts(const char*);
int putchar(int);
#endif
