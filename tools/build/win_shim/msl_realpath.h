#ifndef MSL_REALPATH_H
#define MSL_REALPATH_H
#include <stddef.h>
char* _fullpath(char* absolute, const char* relative, size_t size);
static inline char* msl_win_realpath(const char* path, char* resolved)
{
    return _fullpath(resolved, path, 260);
}
#define realpath msl_win_realpath
#endif
