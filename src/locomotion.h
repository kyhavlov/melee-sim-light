#pragma once

#include "batch_internal.h"
#include "char_params.h"
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
void locomotion_update_anim_callbacks_pre_input(MslBatch* batch);

// Common airborne AttackAir input owner used by Fall/Jump/JumpAerial and reused by DamageFall-
// shaped IASA ladders.
uint8_t locomotion_attackair_try_enter_from_air_iasa(MslBatch* batch, const MslCommonParams* c,
                                                     size_t idx);
void locomotion_update_post_collision(MslBatch* batch);

uint8_t locomotion_grounded_a_attack_try_enter_from_wait_iasa(
    MslBatch* batch, const MslCommonParams* c, size_t idx, uint16_t buttons_pressed, float stick_x,
    float stick_y, uint8_t tilt_timer_x, uint8_t tilt_timer_y, float facing_dir);

uint8_t locomotion_wait_iasa_locomotion_subset_try_enter(
    MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch, size_t idx,
    uint16_t buttons, uint16_t buttons_pressed, float stick_x, float stick_y, uint8_t tilt_timer_x,
    uint8_t tilt_timer_y, float facing_dir, uint16_t action_id_start);

// ftCo_80096900 FallSpecial entry (keep-fastfall + xc + landing-lag lanes). Exported for
// character-special anim-end handoffs (Marth Dolphin Slash).
void msl_locomotion_enter_fall_special_via_ftco_80096900(MslBatch* batch, size_t idx,
                                                         float landing_lag,
                                                         uint8_t allow_interrupt);

// ftCo_Fall_Enter entry and the modeled non-special tail of ftCo_Fall_IASA_Inner. Character
// modules that source-enter Fall from an Anim callback should run their character-special
// ftCo_SpecialAir_CheckInput equivalent before calling the tail.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_IASA_Inner}
void msl_locomotion_enter_fall_via_ftco_fall_enter(MslBatch* batch, const MslCharParams* ch,
                                                   size_t idx);
uint8_t msl_locomotion_run_fall_iasa_non_special_tail(MslBatch* batch, const MslCommonParams* c,
                                                      const MslCharParams* ch, size_t idx);
