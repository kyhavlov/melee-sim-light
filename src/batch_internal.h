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
  MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_TERTIARY = 7,
  MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_JUMPAERIAL_ATTACKAIRB_CARRY = 8,
  MSL_RNG_SITE_YOSHI_SHYGUY_PATTERN = 9,
  MSL_RNG_SITE_YOSHI_SHYGUY_SPEED_INDEX = 10,
  MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY8 = 11,
  MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY8_BONUS = 12,
  MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY2 = 13,
  MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY2_BONUS = 14,
  MSL_RNG_SITE_YOSHI_SHYGUY_JITTER = 15,
  MSL_RNG_SITE_YOSHI_SHYGUY_TIMER_WAIT = 16,
  MSL_RNG_SITE_FOD_PLATFORM_WAIT = 17,
  MSL_RNG_SITE_FOD_PLATFORM_CHOICE = 18,
  MSL_RNG_SITE_FOD_PLATFORM_TARGET = 19,
  MSL_RNG_SITE_FOD_PLATFORM_TARGET_ADJUST = 20,
  MSL_RNG_SITE_FOD_PLATFORM_TARGET_SIDE = 21,
  MSL_RNG_SITE_FOD_PLATFORM_HIDDEN_WAIT = 22,
  MSL_RNG_SITE_DEAD_UP_FALL_SELECT = 23,
  MSL_RNG_SITE_FTCOLL_DAMAGE_EFFECT = 24,
  MSL_RNG_SITE_DEAD_UP_STAR_EFFECT_PREFIX = 25,
  MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_KEEP3 = 26,
  MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_VEL_Y8 = 27,
  MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_ROT_SIGN2 = 28,
  MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_ROT_RATE8 = 29,
  MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_XVEL_SIGN2 = 30,
  MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_XVEL8 = 31,
  MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_YVEL_MIN8 = 32,
  MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_GRAVITY8 = 33,
  MSL_RNG_SITE_SHEIK_NEEDLE_SHOOT_YPOS9 = 34,
  MSL_RNG_SITE_SHEIK_NEEDLE_DROP_ROT_RATE8 = 35,
  MSL_RNG_SITE_SHEIK_NEEDLE_DROP_ROT_SIGN2 = 36,
  MSL_RNG_SITE_SHEIK_NEEDLE_DROP_YVEL_MIN8 = 37,
  MSL_RNG_SITE_SHEIK_NEEDLE_DROP_GRAVITY8 = 38,
  // itSeakneedlethrown_UnkMotion0_Coll stage-hit fate: HSD_Randi(5) -> 0/1/2 stick (state 2),
  // 3/4 bounce (state 4). The bounce's vel-y + SetupBounce reuse the DAMAGE_CALLBACK_BOUNCE_* sites.
  MSL_RNG_SITE_SHEIK_NEEDLE_GROUND_HIT5 = 39,
  MSL_RNG_SITE_COUNT = 40,
};

enum {
  MSL_ROLLOUT_CLOCK_NONE = 0,
  MSL_ROLLOUT_CLOCK_HSD_RAND_STREAM = 1,
  MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED = 2,
  MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED_YOSHI_SHYGUY = 3,
};

enum {
  MSL_CAMERA_MODE_GAME = 0,
  MSL_CAMERA_MODE_FREE = 1,
};

struct MslBatch {
  int batch_size;
  MslConfig config;
  MslStateSoA state;

  // Episode/match init scratch, allocated with the batch so `msl_batch_init_match` can remain
  // allocation-free when used as an RL reset path.
  MslSeed* match_init_seed_scratch;  // [batch]
  // Per-env RNG clock ownership mode. Match-init rollouts own the modeled HSD_Rand stream;
  // replay-reseeded rollouts normally keep the RNG seed-owned, with a narrow Slippi frame-start
  // RNG-clock mode for replay-carry rows that need the post-frame seed to advance.
  uint8_t* rollout_clock_rng_owned;  // [batch]
  // Runtime-only one-frame marker: Yoshi's Story Shy Guy installed an explicit spawn-frame HSD
  // seed during this step. Used to arbitrate with broader replay-frame RNG clock owners without
  // adding a replay schema lane.
  uint8_t* rollout_yoshi_shyguy_spawn_rng_installed;  // [batch]
  // Runtime-only replay playback marker: validation installed the current replay row's
  // frame_pre_random_seed before this step. This disables old synthetic replay-clock advancement
  // while preserving source-site admission through rollout_clock_rng_owned.
  uint8_t* replay_frame_rng_applied;  // [batch]
  // Runtime-only teacher-forced seed marker for the first frame after `reseed_seed*`. This is
  // narrower than normal gameplay runtime and is cleared after the committed step.
  uint8_t* replay_reseed_frame_active;  // [batch]
  // Runtime-only replay playback stage marker: the current Dream Land seed row exposes the first
  // post-publication Whispy wind state after a rollout has advanced past its reseed frame, so the
  // frame owes one source `ftColl_GetWindOffsetVec` application that happened before Slippi could
  // serialize `grOldPupupu.xDC`.
  uint8_t* replay_frame_dream_whispy_first_apply_pending;  // [batch]
  // Runtime-only replay playback source-site marker: the current replay row is in the
  // ftCo_800D3158 top-blast gate pre-state, so site 23 may consume the just-installed
  // frame_pre_random_seed. This is intentionally narrower than replay_frame_rng_applied.
  uint8_t* replay_frame_top_blast_rng_owned;  // [batch]
  // Replay validation rollout reseed mode. This stays true for `reseed_seed_rollout` even when the
  // RNG owner itself stays seed-owned; runtime uses it to advance frame-indexed stage/object owners
  // such as Randall without advancing frame_pre_random_seed.
  uint8_t* replay_rollout_reseeded;       // [batch]
  int32_t* replay_rollout_seed_frame_id;  // [batch]
  // Minimal live camera mode substrate for callbacks that query Camera_8003010C. Replay reseeds do
  // not expose this hidden CObj/debug-mode state, so they reset to normal gameplay camera mode.
  // refs/melee/src/melee/cm/camera.c::Camera_8003010C
  uint8_t* camera_mode;  // [batch]
  // Runtime hidden camera zoom scalar (`cm_80452C68.x2BC`) and return-delay counter (`x2BA`).
  // Fighter_procUpdate gates magnify damage on Camera_80031144() == 1.0f.
  // refs/melee/src/melee/cm/camera.c::{Camera_8002B0E0,Camera_80031144}
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  float* camera_zoom_scale_x2bc;    // [batch]
  uint16_t* camera_zoom_hold_x2ba;  // [batch]

  // Debug-only per-fighter override for hit status eligibility (opcode 26).
  // Indexed like other per-player state arrays: [batch_size * MSL_MAX_PLAYERS].
  // Value 0xFF means "no override; use table lookup".
  uint8_t* debug_hit_status_override;

  // Runtime-only collision-pose matrices sampled while building hurtcap endpoints. Combat's
  // ordinary BODY matrix-radius path consumes the same matrix later in the frame. This deliberately
  // stays out of serialized state so existing replay corpora keep their schema.
  uint8_t* hurtcap_matrix_valid;  // [batch * players * caps]
  float* hurtcap_matrix;          // [batch * players * caps * 12]

  // RNG ownership and trace state:
  // - rng_shadow_seed starts from frame_pre_random_seed each step and advances through modeled
  //   source-owned RNG consumers.
  // - rng_site_counts records per-MSL_RNG_SITE_* consumes and participates in replay-clock RNG
  //   ownership decisions.
  // Debug trace output remains separately named debug_* and writes to MSL_RNG_TRACE_PATH when set.
  // - DamageFlyRoll gate is enabled by default; set MSL_RNG_ENABLE_DAMAGE_FLY_ROLL_GATE=1
  //   as a debug/triage kill-switch (disable) for ablations.
  // - Pseudo-random SFX command consumption is enabled by default; set
  //   MSL_RNG_DISABLE_PSEUDO_RANDOM_SFX_CMD=1 as a debug/triage kill-switch (disable).
  uint8_t debug_rng_enable_damage_fly_roll_gate;
  uint8_t debug_rng_disable_pseudo_random_sfx_cmd;
  uint8_t debug_rng_trace_enabled;
  void* debug_rng_trace_file;
  uint64_t debug_rng_trace_step_counter;
  uint32_t* rng_shadow_seed;     // [batch]
  uint32_t* debug_rng_seed_in;   // [batch]
  uint32_t* debug_rng_seed_out;  // [batch]
  uint16_t* rng_site_counts;     // [batch * MSL_RNG_SITE_COUNT]
};

static inline size_t msl_idx_player(int bi, int p) {
  return (size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p;
}

static inline size_t msl_idx_item(int bi, int it) {
  return (size_t)bi * (size_t)MSL_MAX_ITEMS + (size_t)it;
}
