#pragma once

#include "batch_internal.h"

// Locomotion action-state updates (Fox/Falco, FD).
//
// This module updates:
// - action_id / action_frame (for common movement states),
// - jumps_left,
// - facing,
// - speed_ground_x_self / speed_air_x_self (movement velocities),
// and relies on stage collision + physics integration to handle grounding and gravity.
//
// Ordering:
// - locomotion_update_pre() runs before physics_integrate().
// - locomotion_update_post_collision() runs after stage_collision_apply().

void locomotion_update_pre(MslBatch* batch);
void locomotion_update_post_collision(MslBatch* batch);
