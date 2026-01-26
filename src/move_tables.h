#pragma once

#include <stdint.h>

// Init-time loader for small extracted-data-derived lookup tables (move timelines, etc.).
//
// IMPORTANT: move_tables_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.
int move_tables_init(void);

// Returns whether cmd_var[0] is set at the given cur_anim_frame for an AttackAir* action.
// Used by locomotion to decide between LandingAir* (lag) and Landing (auto-cancel).
//
// Decomp: ftCo_LandingAir_EnterWithLag checks fp->cmd_vars[0] set by the aerial's command script.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c
uint8_t move_tables_attackair_cmd0_active(uint8_t char_id, uint16_t attackair_action_id,
                                          float cur_anim_frame_f32);

// Returns whether AttackAir* can be interrupted (IASA) at the given cur_anim_frame.
//
// Decomp: AttackAir IASA is gated by fp->allow_interrupt (DO_IASA macro).
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_AttackAir*"]["events"] allow_interrupt.
uint8_t move_tables_attackair_allow_interrupt(uint8_t char_id, uint16_t attackair_action_id,
                                              float cur_anim_frame_f32);
