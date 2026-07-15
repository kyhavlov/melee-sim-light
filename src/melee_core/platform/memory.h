#ifndef MSL_CORE_PLATFORM_MEMORY_H
#define MSL_CORE_PLATFORM_MEMORY_H

// Seal hosted HSD allocation after the match bootstrap, and make a raw
// game-file allocation inaccessible once its native DAT graph is complete.
void msl_memory_finish_initialization(void);
void msl_memory_protect_allocation(const void* pointer);

#endif
