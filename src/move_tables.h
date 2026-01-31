#pragma once

#include <stdint.h>

// Init-time loader for small extracted-data-derived lookup tables (move timelines, etc.).
//
// IMPORTANT: move_tables_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.
int move_tables_init(void);

typedef struct MslThrowHitboxParams {
  float damage;
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t element;
  uint8_t sfx_kind;
  uint8_t sfx_severity;
} MslThrowHitboxParams;

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

// Returns whether cmd_var[0] is set at the given cur_anim_frame for Dash.
//
// Decomp: Dash IASA gates late transitions on `fp->cmd_vars[0]`.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter (cmd_vars[0] reset on entry)
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Dash"]["events"] set_cmd_var(idx=0).
uint8_t move_tables_dash_cmd0_active(uint8_t char_id, float cur_anim_frame_f32);

// Returns whether CatchPull/CatchDashPull should enter CatchWait due to the move script setting
// fp->throw_flags (x2210) via the `set_throw_flags` command.
//
// Decomp: CatchPull_Anim transitions to CatchWait via fn_800DA1D8 when fp->throw_flags indicates the
// throw/capture setup point has been reached.
// refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CatchPull_Anim
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Catch*"]["events"] set_throw_flags.
uint8_t move_tables_catchpull_should_enter_wait(uint8_t char_id, uint16_t catch_action_id,
                                                float cur_anim_frame_f32);

// Returns whether a throw release frame is known (parsed from set_throw_flags timing).
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_flags.
uint8_t move_tables_throw_has_release(uint8_t char_id, uint16_t throw_action_id);

// Returns 1 and outputs the released hit_idx if cur_anim_frame_f32 is at/after the throw release frame.
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_flags.
uint8_t move_tables_throw_release_hit_idx(uint8_t char_id, uint16_t throw_action_id,
                                          float cur_anim_frame_f32, uint8_t* out_hit_idx);

// Returns 1 and outputs throw hitbox parameters for the requested hit_idx.
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_hitbox.
uint8_t move_tables_throw_hitbox_params(uint8_t char_id, uint16_t throw_action_id, uint8_t hit_idx,
                                        MslThrowHitboxParams* out);
