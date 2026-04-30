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

// Returns whether grounded smash charge (opcode 56 -> ftCo_800DEE84) was crossed this frame.
//
// Decomp:
// - grounded smash scripts issue "Start Smash Charge", which seeds fp->smash_attrs.state =
//   SmashState_PreCharge.
// - the later fighter input proc promotes PreCharge -> Charging when A is held.
// refs/melee/src/melee/ft/ftaction.c::ftAction_80073008
// refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEE84,ftCo_800DF0D0}
//
// Source of truth:
// data/moves/{fox,falco}.json moves["ftCo_SM_AttackS4"/"ftCo_SM_AttackHi4"/"ftCo_SM_AttackLw4"]
// .events start_smash_charge.
uint8_t move_tables_grounded_smash_charge_crossed(uint8_t char_id, uint16_t grounded_action_id,
                                                  float prev_anim_frame_f32,
                                                  float cur_anim_frame_f32,
                                                  uint8_t* out_hold_frames);

// Returns the damage multiplier argument from the grounded-smash start_smash_charge command.
//
// Decomp:
// - ftAction_80073008 passes command damage_mul into ftCo_800DEE84.
// - ftColl_8007ABD0 later calls ftCo_800DEEB8 to scale hitcapsule damage while
//   smash_attrs.state == SmashState_Release.
// refs/melee/src/melee/ft/ftaction.c::ftAction_80073008
// refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEE84,ftCo_800DEEB8}
// refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0
float move_tables_grounded_smash_charge_damage_mul(uint8_t char_id, uint16_t grounded_action_id);

// Returns whether EscapeN (spotdodge) can be interrupted (IASA) at the given cur_anim_frame.
//
// Decomp:
// - EscapeN timeline is command-driven through ftAction_80071950 (`allow_interrupt` command).
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_EscapeN_Anim
// refs/melee/src/melee/ft/ftaction.c::ftAction_80071950
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_EscapeN"]["events"] allow_interrupt.
uint8_t move_tables_escape_allow_interrupt(uint8_t char_id, uint16_t action_id,
                                           float cur_anim_frame_f32);

// Returns whether EscapeAir command-script cmd_var[0] is active at the given cur_anim_frame.
//
// Decomp:
// - EscapeAir_Phys uses cmd_vars[0] (`cmd_skip_decay`) to switch from velocity decay to the common
//   air helper ft_80084DB0.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Phys
// refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_EscapeAir"]["events"] set_cmd_var(idx=0).
uint8_t move_tables_escapeair_cmd0_active(uint8_t char_id, float cur_anim_frame_f32);

// Returns whether a Special* command-script cmd_var[0] window contains `action_frame`.
//
// Decomp:
// - Fox/Falco SpecialN Loop IASA sets mv.fx.SpecialN.isBlasterLoop only while cmd_vars[0] is set
//   and B is freshly pressed.
// refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
//   ftFx_SpecialNLoop_IASA,ftFx_SpecialAirNLoop_IASA}
// refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
//
// Source of truth: data/moves/{fox,falco}.json specials_by_msid["<msid>"].events set_cmd_var(idx=0).
uint8_t move_tables_special_cmd0_active_at_frame(uint8_t char_id, uint16_t msid, int action_frame);

// Returns whether EscapeF should consume a script-driven facing flip this frame.
//
// Decomp:
// - EscapeF anim callback flips facing when ftCheckThrowB3(fp) consumes the script-owned bit.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Anim
// refs/melee/src/melee/ft/inlines.h::ftCheckThrowB3
//
// Source of truth:
// - data/moves/{fox,falco}.json moves["ftCo_SM_EscapeF"]["events"] set_throw_flags(hit_idx=0).
uint8_t move_tables_escapef_should_flip_facing(uint8_t char_id, int16_t prev_action_frame,
                                               int16_t cur_action_frame);

// Returns whether jab combo gate (fp->x2218_b1) is active at the given cur_anim_frame.
//
// Decomp:
// - command ftAction_80071AE8 sets x2218_b1 from the action script.
// refs/melee/src/melee/ft/ftaction.c::ftAction_80071AE8
// refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071AE8
//
// Source of truth:
// data/moves/{fox,falco}.json moves["ftCo_SM_Attack11"/"ftCo_SM_Attack12"]["events"] set_jab_combo.
uint8_t move_tables_jab_combo_active(uint8_t char_id, uint16_t grounded_action_id,
                                     float cur_anim_frame_f32);

// Returns whether jab rapid gate (fp->x2218_b2) is active at the given cur_anim_frame.
//
// Decomp:
// - command ftAction_80071B28 sets x2218_b2 from the action script.
// refs/melee/src/melee/ft/ftaction.c::ftAction_80071B28
// refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071B28
//
// Source of truth:
// data/moves/{fox,falco}.json moves["ftCo_SM_Attack12"]["events"] set_jab_rapid.
uint8_t move_tables_jab_rapid_active(uint8_t char_id, uint16_t grounded_action_id,
                                     float cur_anim_frame_f32);

// Returns whether Attack100Loop crossed the script checkpoint that consumes mv.co.attack100.x4.
//
// Decomp:
// - Attack100Loop_Anim consumes throw_flags_b3; when the loop-start latch is set and x4 is false,
//   it enters Attack100End. Attack100Loop_IASA sets x4 from A pressed/held.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
//   ftCo_Attack100Loop_Anim,ftCo_Attack100Loop_IASA}
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Attack100Loop"]["events"]
// set_throw_flags(hit_idx=0).
uint8_t move_tables_attack100_loop_end_check_crossed(uint8_t char_id, int16_t prev_action_frame,
                                                     int16_t cur_action_frame);

// Returns whether cmd_var[0] is set at the given cur_anim_frame for Dash.
//
// Decomp: Dash IASA gates late transitions on `fp->cmd_vars[0]`.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter (cmd_vars[0] reset on entry)
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_Dash"]["events"] set_cmd_var(idx=0).
uint8_t move_tables_dash_cmd0_active(uint8_t char_id, float cur_anim_frame_f32);

// Returns whether cmd_var[0] is set at the given cur_anim_frame for RunBrake.
//
// Decomp:
// - ftCo_RunBrake_IASA only reaches fn_800C9CEC (TurnRun enter) when fp->cmd_vars[0] != 0.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::ftCo_RunBrake_IASA
// refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
//
// Source of truth: data/moves/{fox,falco}.json moves["ftCo_SM_RunBrake"]["events"] set_cmd_var(idx=0).
uint8_t move_tables_runbrake_cmd0_active(uint8_t char_id, float cur_anim_frame_f32);

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

// Return the first crossed throw projectile pulse frame in (prev, cur], if any.
// Output frame is sourced from data/moves/{fox,falco}.json set_throw_spawn_projectile events.
uint8_t move_tables_throw_crossed_projectile_pulse_frame(uint8_t char_id, uint16_t throw_action_id,
                                                         float prev_anim_frame_f32,
                                                         float cur_anim_frame_f32,
                                                         int16_t* out_pulse_frame);

// Returns 1 and outputs the earliest throw projectile pulse frame (min frame over
// `set_throw_spawn_projectile` events) for the throw action.
//
// Source of truth:
// data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_spawn_projectile.
uint8_t move_tables_throw_projectile_first_pulse_frame(uint8_t char_id, uint16_t throw_action_id,
                                                       int16_t* out_first_pulse_frame);

// Returns 1 and outputs the latest throw projectile pulse frame (max frame over
// `set_throw_spawn_projectile` events) for the throw action.
//
// Source of truth:
// data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_spawn_projectile.
uint8_t move_tables_throw_projectile_last_pulse_frame(uint8_t char_id, uint16_t throw_action_id,
                                                      int16_t* out_last_pulse_frame);

// Returns the 1-based ordinal of a throw projectile pulse frame for this throw action.
//
// Source of truth:
// data/moves/{fox,falco}.json moves["ftCo_SM_Throw*"]["events"] set_throw_spawn_projectile.
uint8_t move_tables_throw_projectile_pulse_ordinal(uint8_t char_id, uint16_t throw_action_id,
                                                   int16_t pulse_frame, uint8_t* out_ordinal);

// Emits pseudo-random SFX command HSD_Randi(random_range) pulses crossed this frame for a specific
// submotion id.
//
// Decomp:
// - Command opcode 38 (`ftAction_80071FC8`) consumes exactly one HSD_Randi(random_range) when the
//   event executes.
// refs/melee/src/melee/ft/ftaction.c::ftAction_80071FC8
// refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
//
// Source of truth:
// - data/moves/{fox,falco}.json specials_by_msid["<msid>"].events pseudo_random_sfx.
//
// Returns the number of crossed pulses copied to `out_random_ranges` (up to `max_out`).
uint8_t move_tables_special_pseudo_random_sfx_ranges_crossed(uint8_t char_id, uint16_t msid,
                                                             float prev_anim_frame_f32,
                                                             float cur_anim_frame_f32,
                                                             uint8_t* out_random_ranges,
                                                             uint8_t max_out);
