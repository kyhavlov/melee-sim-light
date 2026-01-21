#pragma once

#include <stdint.h>

// Init-time loader for small landing-related tables derived from extracted move timelines.
//
// IMPORTANT: landing_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.
int landing_init(void);

// Returns the landing action to enter when grounding out of an AttackAir* action.
// - If the move's cmd_var[0] "landing lag enabled" window is active at the given action_frame,
//   returns the corresponding LandingAir* action.
// - Otherwise returns MSL_ACT_LANDING (auto-cancel landing).
//
// Decomp: ftCo_LandingAir_EnterWithLag checks fp->cmd_vars[0] set by the aerial's command script.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c
uint16_t landing_attackair_land_action(uint8_t char_id, uint16_t attackair_action_id,
                                       int16_t action_frame);

