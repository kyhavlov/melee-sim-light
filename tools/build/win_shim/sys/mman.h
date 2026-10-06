/* Windows shim for the core's anonymous mmap use.
   On the include path for the Windows build only; no other host sees it. */
#ifndef MSL_WIN_SYS_MMAN_H
#define MSL_WIN_SYS_MMAN_H

#include <stddef.h>
#include <stdint.h>

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_ANON MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000
#define MAP_FAILED ((void*) -1)

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllimport) void* __stdcall VirtualAlloc(void* address, size_t size,
                                                   unsigned long type,
                                                   unsigned long protect);
__declspec(dllimport) int __stdcall VirtualFree(void* address, size_t size,
                                                unsigned long type);
__declspec(dllimport) int __stdcall VirtualProtect(void* address, size_t size,
                                                   unsigned long protect,
                                                   unsigned long* old);

#ifdef __cplusplus
}
#endif

/* MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE: zero-filled private pages, the
   only mmap shape the core asks for. */
static inline void* mmap(void* address, size_t length, int prot, int flags,
                         int fd, long long offset)
{
    void* mapping;
    (void) address;
    (void) prot;
    (void) flags;
    (void) fd;
    (void) offset;
    if (length == 0) {
        return MAP_FAILED;
    }
    mapping = VirtualAlloc(NULL, length, 0x1000u | 0x2000u, 0x04u);
    return mapping == NULL ? MAP_FAILED : mapping;
}

static inline int mprotect(void* address, size_t length, int prot)
{
    unsigned long previous = 0;
    unsigned long protect = 0x04u; /* PAGE_READWRITE */
    if ((prot & PROT_WRITE) == 0) {
        protect = (prot & PROT_READ) ? 0x02u /* PAGE_READONLY */
                                     : 0x01u /* PAGE_NOACCESS */;
    }
    return VirtualProtect(address, length, protect, &previous) ? 0 : -1;
}

static inline int munmap(void* address, size_t length)
{
    (void) length;
    if (address == NULL) {
        return 0;
    }
    /* MEM_RELEASE requires size 0 and the original allocation base. */
    return VirtualFree(address, 0, 0x8000u) ? 0 : -1;
}

#endif
