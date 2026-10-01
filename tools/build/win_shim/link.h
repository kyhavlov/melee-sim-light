/* Windows shim for the dl_iterate_phdr() image-range lookup in
   src/runtime/savestate.c. Reports exactly one "PT_LOAD segment" covering the
   PE image that contains the caller's anchor, which is what the consumer wants. */
#ifndef MSL_WIN_LINK_H
#define MSL_WIN_LINK_H

#include <stddef.h>
#include <stdint.h>

#include "msl_win_image.h"

#define ElfW(type) MslWinElf##type

#define PT_LOAD 1

typedef uint16_t MslWinElfHalf;
typedef uintptr_t MslWinElfAddr;
typedef size_t MslWinElfXword;

typedef struct {
    uint32_t p_type;
    MslWinElfAddr p_vaddr;
    MslWinElfXword p_memsz;
} MslWinElfPhdr;

struct dl_phdr_info {
    MslWinElfAddr dlpi_addr;
    const char* dlpi_name;
    const MslWinElfPhdr* dlpi_phdr;
    MslWinElfHalf dlpi_phnum;
};

/* The consumer passes its own anchor inside `data`; every caller in this tree
   filters by an anchor it owns, so reporting only the anchor's own module is
   both sufficient and cheaper than walking the loaded-module list.

   This reads `data` as a struct whose FIRST member is that anchor address,
   which is exactly src/runtime/savestate.c's ImageRangeQuery:

       typedef struct ImageRangeQuery {
           uintptr_t anchor;      <- read here
           uintptr_t base;
           size_t size;
       } ImageRangeQuery;

   If that member moves or changes type, this shim reads the wrong field and
   reports the module containing some other address - most likely none, which
   turns every savestate into "incompatible" rather than anything louder. The
   clean fix is a platform seam in savestate.c (an msl_platform_image_range()
   that Windows implements directly) instead of a fake ELF API; that is a
   change to the core, so it is proposed rather than made here. The only
   caller is savestate.c's current_image_range(). */
static inline int dl_iterate_phdr(int (*callback)(struct dl_phdr_info*, size_t,
                                                  void*),
                                  void* data)
{
    /* `data` is an ImageRangeQuery whose first member is the anchor address. */
    const void* anchor = (const void*) *(const uintptr_t*) data;
    void* module = msl_win_module_of(anchor);
    size_t image_size = msl_win_image_size(module);
    MslWinElfPhdr segment;
    struct dl_phdr_info info;

    if (module == NULL || image_size == 0) {
        return 0;
    }
    segment.p_type = PT_LOAD;
    segment.p_vaddr = 0;
    segment.p_memsz = image_size;
    info.dlpi_addr = (MslWinElfAddr) module;
    info.dlpi_name = "";
    info.dlpi_phdr = &segment;
    info.dlpi_phnum = 1;
    return callback(&info, sizeof info, data);
}

#endif
