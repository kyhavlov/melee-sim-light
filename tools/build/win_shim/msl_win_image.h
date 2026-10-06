/* Windows shim: the loaded module containing an address, and its image
   base and size, from the PE headers. Shared by dlfcn.h and link.h. */
#ifndef MSL_WIN_WIN_IMAGE_H
#define MSL_WIN_WIN_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllimport) int __stdcall GetModuleHandleExA(unsigned long flags,
                                                       const char* name,
                                                       void** module);

#ifdef __cplusplus
}
#endif

#define MSL_WIN_GET_MODULE_FROM_ADDRESS 0x00000004u
#define MSL_WIN_GET_MODULE_UNCHANGED_REFCOUNT 0x00000002u

static inline void* msl_win_module_of(const void* address)
{
    void* module = NULL;
    if (!GetModuleHandleExA(MSL_WIN_GET_MODULE_FROM_ADDRESS |
                                MSL_WIN_GET_MODULE_UNCHANGED_REFCOUNT,
                            (const char*) address, &module))
    {
        return NULL;
    }
    return module;
}

/* SizeOfImage from the PE optional header at base + e_lfanew + 0x50 (PE32+). */
static inline size_t msl_win_image_size(const void* base)
{
    const unsigned char* bytes = (const unsigned char*) base;
    int32_t pe_offset;
    uint16_t magic;
    if (base == NULL || bytes[0] != 'M' || bytes[1] != 'Z') {
        return 0;
    }
    pe_offset = *(const int32_t*) (bytes + 0x3c);
    if (pe_offset <= 0) {
        return 0;
    }
    if (*(const uint32_t*) (bytes + pe_offset) != 0x00004550u) { /* "PE\0\0" */
        return 0;
    }
    magic = *(const uint16_t*) (bytes + pe_offset + 24);
    if (magic != 0x20b && magic != 0x10b) {
        return 0;
    }
    return (size_t) *(const uint32_t*) (bytes + pe_offset + 24 + 56);
}

#endif
