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

  // Walk gating / walk-type thresholds (see refs/melee/src/melee/ft/ftwalkcommon.c)
  float walk_stick_threshold;  // p_ftCommonData->x24
  float walk_mid_vel_mul;      // p_ftCommonData->x28
  float walk_fast_vel_mul;     // p_ftCommonData->x2C
  float walk_accel_scale_mul;  // p_ftCommonData->x30

  // Turn / run thresholds (see refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c / ftCo_Run.c)
  float turn_stick_x_threshold;  // p_ftCommonData->x34
  float run_stick_x_threshold;   // p_ftCommonData->x58

  // Dash flick threshold (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c)
  float dash_flick_abs;  // p_ftCommonData->x3C
  uint8_t dash_flick_tilt_max_frames;  // p_ftCommonData->x40 (dash_flick_tilt_max_frames)
  uint8_t _pad_u8_0[3];

  // Jump / fastfall thresholds (refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c / ftcommon.c)
  float tap_jump_threshold;          // p_ftCommonData->tap_jump_threshold (0x70)
  float tap_jump_release_threshold;  // p_ftCommonData->tap_jump_release_threshold (0x7C)
  float jump_back_x_threshold;       // p_ftCommonData->x78
  float fastfall_stick_threshold;    // p_ftCommonData->x88
  uint8_t tap_jump_tilt_max_frames;  // p_ftCommonData->x74 (tap_jump_tilt_max_frames)
  uint8_t _pad_u8_1[3];

  // Ground friction multiplier when |gr_vel| > walk_max_vel (refs/melee/src/melee/ft/ft_081B.c::ft_80084F3C)
  float high_speed_friction_mul;  // p_ftCommonData->x6C

  // Run friction multiplier (used in dash/run ground acceleration; refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c)
  float run_friction_mul;  // p_ftCommonData->run_friction_mul (0x60)
} MslCommonParams;

int common_params_init(void);
const MslCommonParams* msl_common_params(void);
