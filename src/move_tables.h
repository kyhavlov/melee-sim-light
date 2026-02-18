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

// Returns whether grounded Attack* can be interrupted (IASA) at the given cur_anim_frame.
//
// Decomp:
// - Most grounded Attack* IASA handlers gate on fp->allow_interrupt and then delegate to Wait IASA.
// refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackHi4.c,ftCo_AttackLw4.c}
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Attack*"]["events"] allow_interrupt.
uint8_t move_tables_grounded_attack_allow_interrupt(uint8_t char_id, uint16_t grounded_action_id,
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

// Returns whether CatchAttack's grabbed-only hitbox window is active at the given cur_anim_frame.
//
// Decomp tie-down:
// - CatchWait IASA enters CatchAttack via fn_800DA4FC.
//   refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{fn_800DA4C0,fn_800DA4FC}
// - The victim transitions to CaptureDamage* when CatchAttack's grabbed-only hitbox connects
//   (ftCo_800DC284 / ftCo_800DC3A4 call sites).
//   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800DC284,ftCo_800DC3A4}
//
// Source of truth:
// - data/moves/{fox,falco}.json moves["ftCo_SM_CatchAttack"]["events"] create_hitbox
//   (only_hit_grabbed=true) and clear_hitboxes.
uint8_t move_tables_catchattack_grabbed_hit_active(uint8_t char_id, float cur_anim_frame_f32);

// Returns whether a throw release frame is known (parsed from set_throw_flags timing).
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_flags.
uint8_t move_tables_throw_has_release(uint8_t char_id, uint16_t throw_action_id);

// Returns 1 and outputs the parsed throw release action-frame threshold.
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_flags.
uint8_t move_tables_throw_release_frame(uint8_t char_id, uint16_t throw_action_id,
                                        float* out_release_af);

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

// Returns whether a throw should flip the thrower's facing this frame.
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_flags(hit_idx=1),
// which maps to throw_flags_b4 in decomp:
// refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4 (case 1).
uint8_t move_tables_throw_should_flip_facing(uint8_t char_id, uint16_t throw_action_id,
                                             float prev_anim_frame_f32, float cur_anim_frame_f32);

// Returns whether throw cmd_var[1] is active (value==1) at the given cur_anim_frame.
//
// Decomp:
// - Throw-side blaster flow in ftFx_Throw_Anim switches on fp->cmd_vars[1]:
//   case 1 owns spawn/update, case 2 clears pointer, case 0 disables.
// refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_cmd_var(idx=1).
uint8_t move_tables_throw_cmd1_active(uint8_t char_id, uint16_t throw_action_id,
                                      float cur_anim_frame_f32);

// Returns whether a throw-script projectile pulse (throw_flags_b0) was crossed this frame.
//
// Decomp:
// - ftAction_80071974 sets fp->throw_flags_b0.
// - ftFx_Throw_Anim consumes throw_flags_b0 to spawn blaster shots during Throw{B,Hi,Lw}.
// refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
// refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
//
// Source of truth:
// data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_spawn_projectile.
uint8_t move_tables_throw_should_spawn_projectile(uint8_t char_id, uint16_t throw_action_id,
                                                  float prev_anim_frame_f32,
                                                  float cur_anim_frame_f32);

// Returns 1 and outputs the latest throw projectile pulse frame (max frame over
// `set_throw_spawn_projectile` events) for the throw action.
//
// Source of truth:
// data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_spawn_projectile.
uint8_t move_tables_throw_projectile_last_pulse_frame(uint8_t char_id, uint16_t throw_action_id,
                                                      int16_t* out_last_pulse_frame);
