#pragma once

#include <stdint.h>

// Init-time loader for a small subset of ftCommonData constants used by locomotion/input gating.
//
// Source of truth: `data/common/ft_common_data.json` (ISO-derived from `_iso/PlCo.dat`).
// Extractor: `tools/extraction/extract_ftcommon_data.py` (decomp-first).
//
// IMPORTANT: common_params_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.

typedef struct MslCommonParams {
  // Deadzones / input thresholds
  float lstick_deadzone_x;
  float lstick_deadzone_y;
  float lstick_tilt_x_thresh;  // p_ftCommonData->x8_someStickThreshold
  float lstick_tilt_y_thresh;  // p_ftCommonData->xC
  float trigger_deadzone;      // p_ftCommonData->x10 (trigger deadzone used for held_inputs L/R)

  // Stick angle threshold used by several common IASA checks (including cliff/ledge options).
  // Decomp: p_ftCommonData->x20 (radians), used by e.g. ftCo_CliffClimb.c::ftCo_8009AAFC.
  float attack_angle_threshold_radians;  // p_ftCommonData->x20

  // Walk gating / walk-type thresholds (see refs/melee/src/melee/ft/ftwalkcommon.c)
  float walk_stick_threshold;  // p_ftCommonData->x24
  float walk_mid_vel_mul;      // p_ftCommonData->x28
  float walk_fast_vel_mul;     // p_ftCommonData->x2C
  float walk_accel_scale_mul;  // p_ftCommonData->x30

  // Turn / run thresholds (see refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c / ftCo_Run.c)
  float turn_stick_x_threshold;  // p_ftCommonData->x34
  float run_stick_x_threshold;   // p_ftCommonData->x58

  // Special move direction thresholds (B specials).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
  float special_stick_x_threshold_side;  // p_ftCommonData->x218
  float special_stick_y_threshold;       // p_ftCommonData->x21C
  float special_side_reverse_threshold;  // p_ftCommonData->x220
  float special_neutral_reverse_threshold;  // p_ftCommonData->x224

  // Dash flick threshold (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c)
  float dash_flick_abs;                // p_ftCommonData->x3C
  uint8_t dash_flick_tilt_max_frames;  // p_ftCommonData->x40 (dash_flick_tilt_max_frames)
  uint8_t _pad_u8_0[3];

  // Dash IASA windows (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA)
  float dash_iasa_vel_mul;  // p_ftCommonData->dash_iasa_vel_mul (0x54)
  float dash_iasa_x44;      // p_ftCommonData->x44
  float dash_iasa_x48;      // p_ftCommonData->x48
  float dash_iasa_x4c;      // p_ftCommonData->x4C

  // Jump / fastfall thresholds (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c / ftcommon.c)
  float tap_jump_threshold;          // p_ftCommonData->tap_jump_threshold (0x70)
  float tap_jump_release_threshold;  // p_ftCommonData->tap_jump_release_threshold (0x7C)
  float jump_back_x_threshold;       // p_ftCommonData->x78
  float fastfall_stick_threshold;    // p_ftCommonData->x88
  uint8_t fastfall_tilt_max_frames;  // p_ftCommonData->x8C (fastfall_tilt_max_frames)
  // Crouch threshold (Squat entry gate).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_CheckInput (fp->input.lstick.y < -p_ftCommonData->x90)
  float crouch_stick_threshold;  // p_ftCommonData->x90
  uint8_t tap_jump_tilt_max_frames;  // p_ftCommonData->x74 (tap_jump_tilt_max_frames)
  uint8_t _pad_u8_1[2];

  // Cliff / ledge common behavior (ftCo_Cliff*).
  //
  // Decomp pointers:
  // - refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081298 (drop stick gate)
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A804 (wait timer init)
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::inlineA0 / ftCo_8009AAFC (option stick/angle)
  float cliff_drop_stick_threshold;      // p_ftCommonData->x480
  float cliff_wait_percent_threshold;    // p_ftCommonData->x488
  float cliff_wait_frames_low_percent;   // p_ftCommonData->x48C
  float cliff_wait_frames_high_percent;  // p_ftCommonData->x490
  float cliff_option_stick_threshold;    // p_ftCommonData->x494
  uint16_t ledge_cooldown_frames;        // p_ftCommonData->ledge_cooldown (x498)

  // Offscreen death / match-flow (ft_0D31.c / ft_0C31.c).
  // Decomp pointers:
  // - refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3158 (dead_up_kb_vel_threshold)
  // - refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D3680 (dead_timer_frames)
  // - refs/melee/src/melee/ft/ft_0D31.c::ftCo_800D40B8 (dead_up_star_initial_frames)
  // - refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s::ftCo_DeadUpStar_Anim (dead_up_star_phase1/2_frames)
  // - refs/melee/build/GALE01/asm/melee/ft/ft_0D31.s (Rebirth/RebirthWait timers at 0x5D0/0x5D4)
  // - refs/melee/src/melee/ft/ft_0C31.c (entry_start_frames/entry_end_frames)
  float dead_up_kb_vel_threshold;        // p_ftCommonData->x4F0
  uint16_t dead_timer_frames;            // p_ftCommonData->x500 (DeadDown/Left/Right timer)
  uint16_t dead_up_star_initial_frames;  // p_ftCommonData->x504
  uint16_t dead_up_star_phase1_frames;   // p_ftCommonData->x508
  uint16_t dead_up_star_phase2_frames;   // p_ftCommonData->x50C
  uint16_t rebirth_timer_frames;         // p_ftCommonData->0x5D0
  uint16_t rebirth_wait_timer_frames;    // p_ftCommonData->0x5D4
  uint16_t entry_start_frames;           // p_ftCommonData->x6BC
  uint16_t entry_end_frames;             // p_ftCommonData->x6C0
  uint16_t _pad_u16_match_flow_0;

  // Ground friction multiplier when |gr_vel| > walk_max_vel (refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C)
  float high_speed_friction_mul;  // p_ftCommonData->x6C

  // Run friction multiplier (used in dash/run ground acceleration; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c)
  float run_friction_mul;  // p_ftCommonData->run_friction_mul (0x60)

  // Powershield / GuardReflect (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c and fighter.c)
  float powershield_reflect_trigger_min;      // p_ftCommonData->x18
  uint8_t powershield_reflect_window_frames;  // p_ftCommonData->x2A0
  uint8_t powershield_reflect_frames;         // p_ftCommonData->x2A4 (rounded)
  uint8_t powershield_reflect_total_frames;   // p_ftCommonData->x2B4 (rounded)
  uint8_t _pad_u8_2[1];

  // Shield defensive options (grounded) (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c)
  float spotdodge_stick_y_threshold;        // p_ftCommonData->x314
  float escape_stick_x_threshold;           // p_ftCommonData->x31C
  uint8_t spotdodge_flick_tilt_max_frames;  // p_ftCommonData->x318
  uint8_t escape_flick_tilt_max_frames;     // p_ftCommonData->x320
  uint8_t _pad_u8_2b[2];

  // Shield / guard constants (ftCo_Guard.c and fighter.c).
  // Source of truth: `data/common/ft_common_data.json` extractor comments map these to ftCommonData.
  // Guard pose update smoothing (ftCo_Guard.c::ftCo_80091BC4).
  float guard_stick_lerp_x44c;  // p_ftCommonData->guard_stick_lerp_x44c (0x44C)
  float start_shield_health;    // p_ftCommonData->x260 (start_shield_health)
  // Shield size scaling (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::inlineB0)
  float shield_size_lightshield_min;  // p_ftCommonData->x2D4 (shield_size_lightshield_min)
  float shield_size_lightshield_max;  // p_ftCommonData->x2D8 (shield_size_lightshield_max)
  float shield_size_min_scale;        // p_ftCommonData->x264 (shield_size_min_scale)
  float shield_recharge_per_frame;    // p_ftCommonData->x27C (shield_recharge_per_frame)
  float shield_hold_drain_mul;        // p_ftCommonData->x278 (shield_hold_drain_mul)
  float shield_hold_drain_base;       // p_ftCommonData->x2EC (shield_hold_drain_base)
  float shield_hold_drain_max;        // p_ftCommonData->x2F0 (shield_hold_drain_max)

  // Shield HP depletion on hit (blocking).
  // Decomp: refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // - shield_health -= x284 * (shieldDamageTaken*(1 - (lightshield_amount*(x2E0-x2DC)+x2DC))) + x288
  float shield_hit_damage_mul;       // p_ftCommonData->x284
  float shield_hit_damage_base;      // p_ftCommonData->x288
  float shield_hit_lightshield_min;  // p_ftCommonData->x2DC
  float shield_hit_lightshield_max;  // p_ftCommonData->x2E0

  // Shieldstun (GuardSetOff) duration shaping.
  //
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  // f = x28C*(x19A4*(1 - (lightshield_amount*(x2E8-x2E4)+x2E4))) + x290
  // anim_rate = (0.1 + end_frame) / f
  float shield_stun_mul;              // p_ftCommonData->x28C
  float shield_stun_base;             // p_ftCommonData->x290
  float shield_stun_lightshield_min;  // p_ftCommonData->x2E4
  float shield_stun_lightshield_max;  // p_ftCommonData->x2E8

  // Shield setoff pushback (grounded).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  float shield_setoff_push_mul;            // p_ftCommonData->x294
  float shield_setoff_push_max;            // p_ftCommonData->x298
  float shield_setoff_push_mul_non_yoshi;  // p_ftCommonData->x2BC

  // L-cancel window / lag divisor (refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c)
  uint8_t lcancel_window_frames;  // p_ftCommonData->xE4
  uint8_t _pad_u8_3[3];
  float lcancel_lag_div;  // p_ftCommonData->xE8

  // Landing lag for LandingFallSpecial when landing out of EscapeAir (airdodge).
  // Decomp: EscapeAir_Coll -> callback -> ftCo_LandingFallSpecial_Enter(..., p_ftCommonData->x344).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c:117
  //
  // Note: currently loaded for upcoming LandingFallSpecial timing modeling (not yet consumed in core logic).
  float landing_fall_special_lag_frames;  // p_ftCommonData->x344

  // Air dodge (EscapeAir) constants.
  // Decomp: ftCo_80099A9C / ftCo_EscapeAir_Phys.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c
  float escapeair_deadzone_x;      // p_ftCommonData->escapeair_deadzone.x (x32C)
  float escapeair_deadzone_y;      // p_ftCommonData->escapeair_deadzone.y (x330)
  uint8_t escapeair_timer_frames;  // p_ftCommonData->x334 (escapeair timer frames)
  uint8_t _pad_u8_4[3];
  float escapeair_force;  // p_ftCommonData->escapeair_force (x338)
  float escapeair_decay;  // p_ftCommonData->escapeair_decay (x33C)

  // FallSpecial mobility scalar (used to cap drift).
  // Decomp:
  // - EscapeAir_Anim -> ftCo_80096900(..., p_ftCommonData->x340, p_ftCommonData->x344)
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c
  // - FallSpecial phys clamps |target_vel| to `mv.co.fallspecial.mobility` which is
  //   `ca->air_drift_max * mobility_scalar`.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c
  float fall_special_mobility_scalar;  // p_ftCommonData->x340

  // Hitlag constants (ftCommon_CalcHitlag).
  // Decomp: refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  float hitlag_dmg_mul;    // p_ftCommonData->x198
  float hitlag_base;       // p_ftCommonData->x19C
  float hitlag_squat_mul;  // p_ftCommonData->x1A0

  // Knockback + Damage state entry helpers (subset).
  //
  // Decomp pointers:
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (Damage state entry)
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback (kb_squat_mul/kb_min)
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CheckAirMotion (air motion KB mul)
  // - refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (kb magnitude)
  float kb_weight_mul;   // p_ftCommonData->0xF4
  float kb_weight_mul2;  // p_ftCommonData->0xF8
  float kb_applied_max;  // p_ftCommonData->0x108
  float kb_base_term;    // p_ftCommonData->0x110
  float kb_dmg_mul;      // p_ftCommonData->0x114
  float kb_wsk_mul;      // p_ftCommonData->0x118
  float kb_growth_mul;   // p_ftCommonData->0x11C
  float kb_base_add;     // p_ftCommonData->0x120
  float kb_vel_mul;      // p_ftCommonData->x100
  float kb_min;          // p_ftCommonData->x104
  float kb_squat_mul;    // p_ftCommonData->x124
  float kb_ice_mul;      // p_ftCommonData->kb_ice_mul (+0x718)
  float kb_smashcharge_mul;  // p_ftCommonData->kb_smashcharge_mul (+0x7C4)
  // ftColl_80079AB0 percent-term override constants (p_ftCommonData->0x6D4/0x6D8).
  //
  // Used in the non-WSK else-branch when fp+0x2225 bit0 is set (decomp name: fp->x2225_b7):
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
  // - 0x80079B80: lwz r0, 0x6d8(p_ftCommonData)
  // - 0x80079B88: lwz r0, 0x6d4(p_ftCommonData)
  int32_t ftcoll_percent_base_x6d4;
  int32_t ftcoll_percent_base_x6d8;

  // Hitstun scaling + severity thresholds (ftCo_Damage.c).
  float damage_hitstun_mul;    // p_ftCommonData->0x154
  float damage_severity_x158;  // p_ftCommonData->0x158
  float damage_severity_x15c;  // p_ftCommonData->0x15C
  float damage_severity_x160;  // p_ftCommonData->0x160

  // Combo timer window after hitstun ends (used by combo victim clear logic).
  // Decomp:
  // - fp->x2098 = p_ftCommonData->x4CC when hitstun ends:
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
  // - decremented and used for clearing attacker fp->x2094 (combo victim):
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
  uint16_t combo_timer_post_hitstun_frames;  // p_ftCommonData->x4CC
  uint16_t _pad_u16_combo_0;

  // DamageFlyTop angle window (radians) (ftCo_8008DCE0 block_33).
  float damagefly_top_angle_min_radians;  // p_ftCommonData->0x234
  float damagefly_top_angle_max_radians;  // p_ftCommonData->0x238

  // Sakurai angle (hitbox angle 361) constants.
  float sakurai_air_radians;     // p_ftCommonData->x144_radians
  float sakurai_ground_deg_max;  // p_ftCommonData->x148 (degrees)
  float sakurai_kb_threshold;    // p_ftCommonData->x14C
  float sakurai_kb_max;          // p_ftCommonData->x150

  // "Air motion" KB velocity multiplier gate (ftCo_Damage_CheckAirMotion).
  float air_motion_kb_mul;          // p_ftCommonData->x190
  uint8_t air_motion_max_frames;    // p_ftCommonData->x18C
  uint8_t tech_lr_debounce_frames;  // p_ftCommonData->x1C

  // Tech / passive windows (ftCo_DownAttack.c / ftCo_PassiveStand.c).
  // Decomp:
  // - `fp->x680 < x250` gate (tech window).
  // - `ABS(lstick.x) >= x254` chooses tech-in-place vs tech-roll.
  float tech_window_frames;         // p_ftCommonData->x250
  float tech_roll_stick_threshold;  // p_ftCommonData->x254

  // Damage landing thresholds (ftCo_Damage_Coll).
  // Decomp: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
  float damagefly_downbound_kb_vel_threshold;  // p_ftCommonData->x1E0
  float damagefly_landing_kb_vel_threshold;    // p_ftCommonData->x1E4

  // Downed / knockdown thresholds + timers.
  // Decomp:
  // - DownStand input: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownStand.c::ftCo_800980BC
  // - DownBound->DownWait timer init: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_80097E8C
  float down_stand_stick_y_threshold;      // p_ftCommonData->x244
  float down_stick_x_threshold;            // p_ftCommonData->x248 (Down/roll stick gate)
  float down_attack_button_window_frames;  // p_ftCommonData->x24C
  float down_attack_cstick_up_threshold;   // p_ftCommonData->x7F4
  float down_wait_frames;                  // p_ftCommonData->x424
} MslCommonParams;

int common_params_init(void);
const MslCommonParams* msl_common_params(void);
