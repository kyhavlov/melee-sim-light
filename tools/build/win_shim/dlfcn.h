/* Windows shim for the one dladdr() use in the core: mapping a static
   pointer to an image-relative HSD id offset. */
#ifndef MSL_WIN_DLFCN_H
#define MSL_WIN_DLFCN_H

#include "msl_win_image.h"

typedef struct {
    const char* dli_fname;
    void* dli_fbase;
    const char* dli_sname;
    void* dli_saddr;
} Dl_info;

static inline int dladdr(const void* address, Dl_info* info)
{
    void* module = msl_win_module_of(address);
    if (module == NULL || info == NULL) {
        return 0;
    }
    info->dli_fname = "";
    info->dli_fbase = module;
    info->dli_sname = NULL;
    info->dli_saddr = NULL;
    return 1;
}

#endif
