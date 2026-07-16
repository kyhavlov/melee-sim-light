#ifndef MSL_CORE_TEST_SMOKE_CONTEXT_H
#define MSL_CORE_TEST_SMOKE_CONTEXT_H

#include "platform/files.h"
#include "runtime/context.h"
#include "runtime/scalar.h"

#include <string.h>

typedef struct MslSmokeContext {
    MslCoreGameData game_data;
    MslCoreMatch match;
} MslSmokeContext;

static int msl_smoke_context_init(MslSmokeContext* context,
                                  const char* data_root)
{
    memset(context, 0, sizeof(*context));
    if (msl_memory_context_init(&context->game_data.memory,
                                MSL_MEMORY_GAME_DATA) != 0 ||
        msl_memory_context_init(&context->match.memory, MSL_MEMORY_MATCH) != 0)
    {
        return -1;
    }
    context->match.game_data = &context->game_data;
    context->match.random.value = 1;
    context->match.random.active = &context->match.random.value;
    msl_core_bind_match(&context->match);
    if (data_root != NULL) {
        msl_host_set_data_root(data_root);
    }
    return 0;
}

static void msl_smoke_context_destroy(MslSmokeContext* context)
{
#ifdef MSL_CORE_NATIVE
    msl_native_dat_context_destroy(&context->game_data.native_dat);
#endif
    msl_memory_context_destroy(&context->match.memory);
    msl_memory_context_destroy(&context->game_data.memory);
}

#endif
