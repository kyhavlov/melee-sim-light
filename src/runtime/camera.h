#ifndef MSL_CORE_RUNTIME_CAMERA_H
#define MSL_CORE_RUNTIME_CAMERA_H

#include "ft/forward.h"

#include <stdbool.h>

typedef struct MslCoreCameraState {
    bool vanilla_magnify_offscreen[6];
} MslCoreCameraState;

void msl_camera_state_init(MslCoreCameraState* state);
void msl_camera_state_bind(MslCoreCameraState* state);
void msl_camera_publish_match_visibility(Fighter_GObj* const* fighters,
                                         int fighter_count);
// grStadium_801D32D0's camera test for the jumbotron close-up.
bool msl_camera_stadium_closeup_fits(const Vec3* point);

#endif
