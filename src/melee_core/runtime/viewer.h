#ifndef MSL_CORE_RUNTIME_VIEWER_H
#define MSL_CORE_RUNTIME_VIEWER_H

#include "runtime/wire.h"

typedef struct MslCoreMatch MslCoreMatch;

void msl_core_match_write_viewer(const MslCoreMatch* match,
                                 MslCoreViewerState* output);

#endif
