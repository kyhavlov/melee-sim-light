#ifndef MSL_CORE_RUNTIME_OBSERVATION_H
#define MSL_CORE_RUNTIME_OBSERVATION_H

#include "runtime/wire.h"

typedef struct MslCoreMatch MslCoreMatch;

int msl_core_match_write_observation(const MslCoreMatch* match,
                                     uint8_t viewpoint_player,
                                     MslCoreObservation* output);
void msl_core_match_write_terminal(const MslCoreMatch* match,
                                   int32_t max_frame_id,
                                   MslCoreTerminal* output);

#endif
