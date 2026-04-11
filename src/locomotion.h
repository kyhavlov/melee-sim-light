#pragma once

#include "batch_internal.h"
#include "common_params.h"

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

uint8_t locomotion_grounded_a_attack_try_enter_from_wait_iasa(
    MslBatch* batch, const MslCommonParams* c, size_t idx, uint16_t buttons_pressed, float stick_x,
    float stick_y, uint8_t tilt_timer_x, uint8_t tilt_timer_y, float facing_dir);
