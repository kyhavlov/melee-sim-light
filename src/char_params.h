#pragma once

#include <stdint.h>

// Init-time loader for a small subset of per-character attributes needed for locomotion/physics.
//
// Source of truth: ISO-extracted character attributes under `data/characters/*.json`.
// Extractor: `tools/extraction/extract_character_attrs.py` (decomp-first).
//
// IMPORTANT: char_params_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.

typedef struct MslCharParams {
  // Combat (subset).
  // Decomp: ftCo_Damage knockback uses `fp->co_attrs.weight` (ftCo_Damage / ftColl).
  // Source of truth: ISO-extracted `data/characters/*.json` `weight`.
  float weight;

  // Ground locomotion
  float walk_init_vel;
  float walk_accel;
  float walk_max_vel;
  float gr_friction;
  float ground_max_horizontal_velocity;

  float dash_initial_velocity;
  float dash_run_acceleration_a;
  float dash_run_acceleration_b;
  float dash_run_terminal_velocity;

  float run_animation_scaling;

  // Turn / jump
  uint8_t turn_frames;
  uint8_t jump_startup_frames;
  uint8_t max_jumps;
  uint8_t landing_lag_frames;
  uint8_t landing_airn_lag_frames;
  uint8_t landing_airf_lag_frames;
  uint8_t landing_airb_lag_frames;
  uint8_t landing_airhi_lag_frames;
  uint8_t landing_airlw_lag_frames;
  uint8_t _pad_u8_0[1];

  float jump_h_initial_velocity;
  float jump_v_initial_velocity;
  float hop_v_initial_velocity;
  float ground_to_air_jump_momentum_multiplier;
  float jump_h_max_velocity;

  // Air physics
  float grav;
  float terminal_vel;
  float fast_fall_velocity;
  float air_max_horizontal_velocity;
  float air_drift_stick_mul;
  float aerial_drift_base;
  float air_drift_max;
  float aerial_friction;
  float air_jump_v_multiplier;
  float air_jump_h_multiplier;

  // Shield (refs/melee/src/melee/ft/types.h::ftCo_DatAttrs::initial_shield_size)
  float initial_shield_size;
} MslCharParams;

int char_params_init(void);
const MslCharParams* msl_char_params(uint8_t char_id);
