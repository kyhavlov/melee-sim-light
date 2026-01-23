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

  // Walk gating / walk-type thresholds (see refs/melee/src/melee/ft/ftwalkcommon.c)
  float walk_stick_threshold;  // p_ftCommonData->x24
  float walk_mid_vel_mul;      // p_ftCommonData->x28
  float walk_fast_vel_mul;     // p_ftCommonData->x2C
  float walk_accel_scale_mul;  // p_ftCommonData->x30

  // Turn / run thresholds (see refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c / ftCo_Run.c)
  float turn_stick_x_threshold;  // p_ftCommonData->x34
  float run_stick_x_threshold;   // p_ftCommonData->x58

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
  uint8_t tap_jump_tilt_max_frames;  // p_ftCommonData->x74 (tap_jump_tilt_max_frames)
  uint8_t _pad_u8_1[2];

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
  float start_shield_health;        // p_ftCommonData->x260 (start_shield_health)
  // Shield size scaling (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::inlineB0)
  float shield_size_lightshield_min;  // p_ftCommonData->x2D4 (shield_size_lightshield_min)
  float shield_size_lightshield_max;  // p_ftCommonData->x2D8 (shield_size_lightshield_max)
  float shield_size_min_scale;        // p_ftCommonData->x264 (shield_size_min_scale)
  float shield_recharge_per_frame;  // p_ftCommonData->x27C (shield_recharge_per_frame)
  float shield_hold_drain_mul;      // p_ftCommonData->x278 (shield_hold_drain_mul)
  float shield_hold_drain_base;     // p_ftCommonData->x2EC (shield_hold_drain_base)
  float shield_hold_drain_max;      // p_ftCommonData->x2F0 (shield_hold_drain_max)

  // Shield HP depletion on hit (blocking).
  // Decomp: refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // - shield_health -= x284 * (shieldDamageTaken*(1 - (lightshield_amount*(x2E0-x2DC)+x2DC))) + x288
  float shield_hit_damage_mul;  // p_ftCommonData->x284
  float shield_hit_damage_base; // p_ftCommonData->x288
  float shield_hit_lightshield_min;  // p_ftCommonData->x2DC
  float shield_hit_lightshield_max;  // p_ftCommonData->x2E0

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
} MslCommonParams;

int common_params_init(void);
const MslCommonParams* msl_common_params(void);
