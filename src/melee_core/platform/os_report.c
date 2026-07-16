#include <dolphin/os.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void OSReport(char* format, ...)
{
    // Retail diagnostic reports have no gameplay side effects. Keeping them
    // silent in the headless runtime avoids formatting and stdio work on
    // reached reset/frame paths such as the source fallback camera ranges.
    // Fatal OSPanic/HSD_Panic paths below remain loud.
    (void) format;
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
