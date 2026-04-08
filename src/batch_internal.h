#pragma once

#include "config.h"
#include "state.h"

enum {
  MSL_RNG_SITE_DAMAGE_FLY_ROLL_GATE = 1,
  MSL_RNG_SITE_FTCOLL_ELECTRIC_CLANK_SFX = 2,
  MSL_RNG_SITE_FTWAIT_ANIM_VARIANT = 3,
  MSL_RNG_SITE_FTACTION_PSEUDO_RANDOM_SFX_CMD = 4,
  MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY = 5,
  MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_SECONDARY = 6,
  MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_JUMPAERIAL_ATTACKAIRB_CARRY = 7,
  MSL_RNG_SITE_COUNT = 8,
};

struct MslBatch {
  int batch_size;
  MslConfig config;
  MslStateSoA state;

  // Debug-only per-fighter override for hit status eligibility (opcode 26).
  // Indexed like other per-player state arrays: [batch_size * MSL_MAX_PLAYERS].
  // Value 0xFF means "no override; use table lookup".
  uint8_t* debug_hit_status_override;

  // Debug/triage RNG observability (no gameplay ownership by default):
  // - Shadow RNG stream starts from frame_pre_random_seed each step.
  // - Per-site consume counts are indexed by MSL_RNG_SITE_*.
  // - Optional TSV trace writes to MSL_RNG_TRACE_PATH when set.
  // - DamageFlyRoll gate is enabled by default; set MSL_RNG_ENABLE_DAMAGE_FLY_ROLL_GATE=1
  //   as a debug/triage kill-switch (disable) for ablations.
  // - Pseudo-random SFX command consumption is enabled by default; set
  //   MSL_RNG_DISABLE_PSEUDO_RANDOM_SFX_CMD=1 as a debug/triage kill-switch (disable).
  uint8_t debug_rng_enable_damage_fly_roll_gate;
  uint8_t debug_rng_disable_pseudo_random_sfx_cmd;
  uint8_t debug_rng_trace_enabled;
  void* debug_rng_trace_file;
  uint64_t debug_rng_trace_step_counter;
  uint32_t* debug_rng_shadow_seed;  // [batch]
  uint32_t* debug_rng_seed_in;      // [batch]
  uint32_t* debug_rng_seed_out;     // [batch]
  uint16_t* debug_rng_site_counts;  // [batch * MSL_RNG_SITE_COUNT]
};

static inline size_t msl_idx_player(int bi, int p) {
  return (size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p;
}

static inline size_t msl_idx_item(int bi, int it) {
  return (size_t)bi * (size_t)MSL_MAX_ITEMS + (size_t)it;
}
