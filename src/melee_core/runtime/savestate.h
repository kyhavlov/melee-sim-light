#ifndef MSL_CORE_RUNTIME_SAVESTATE_H
#define MSL_CORE_RUNTIME_SAVESTATE_H

#include <stddef.h>

typedef struct MslCoreMatch MslCoreMatch;

size_t msl_core_match_save_size(const MslCoreMatch* match);
int msl_core_match_save(const MslCoreMatch* match, void* buffer,
                        size_t buffer_size, size_t* written);
int msl_core_match_restore(MslCoreMatch* match, const void* buffer,
                           size_t buffer_size);
int msl_core_match_copy(MslCoreMatch* destination, const MslCoreMatch* source);

#endif
