#include <dolphin/os.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void OSReport(char* format, ...)
{
    va_list args;

    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

void OSPanic(char* file, int line, char* format, ...)
{
    va_list args;

    fprintf(stderr, "OSPanic at %s:%d: ", file, line);
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    abort();
}

void HSD_Panic(char* file, u32 line, char* condition)
{
    fprintf(stderr, "HSD panic at %s:%lu: %s\n", file,
            (unsigned long) line, condition);
    abort();
}
