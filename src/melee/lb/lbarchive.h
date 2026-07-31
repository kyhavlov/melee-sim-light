#ifndef _lbarchive_h_
#define _lbarchive_h_

#include <platform.h>

#include <baselib/forward.h>

#include <baselib/archive.h>

// Hosted varargs consume the sentinel as a pointer-width symbol slot. Keep
// retail's literal zero on the 32-bit source build.
#ifdef MSL_CORE_NATIVE
#define MSL_LBARCHIVE_END ((void**) 0)
#else
#define MSL_LBARCHIVE_END 0
#endif

void lbArchive_InitializeDAT(HSD_Archive* archive, void* data, size_t length);
void lbArchive_LoadSections(HSD_Archive* archive, void** symbols, ...);
HSD_Archive* lbArchive_LoadArchive(const char* filename);
HSD_Archive* lbArchive_LoadSymbols(const char* filename, void* symbols, ...);
HSD_Archive* lbArchive_80016DBC(const char* filename, void* symbols, ...);
void lbArchive_80016EFC(HSD_Archive*);
bool lbArchive_80016F80(HSD_Archive**, const char* filename);
bool lbArchive_80017040(HSD_Archive** dst, const char* filename, void* symbols,
                        ...);
bool lbArchive_800171CC(HSD_Archive** dst, const char* filename, void* symbols,
                        ...);
int lbArchiveRelocate(HSD_Archive*, u8*, size_t file_size, intptr_t base_addr);

#endif
