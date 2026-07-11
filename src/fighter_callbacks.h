#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

// Decomp-shaped fighter frame scheduler substrate.
//
// This owns callback ordering and per-fighter GObj/player-order context; subsystem modules still
// own their callback bodies. Keep this layer allocation-free and source-order oriented.
// refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate,Fighter_ProcessHit_8006D1EC}
typedef enum MslFighterCallbackPhase {
  MSL_FIGHTER_CALLBACK_PHASE_NONE = 0,
  MSL_FIGHTER_CALLBACK_PHASE_FRAME_BEGIN = 1,
  MSL_FIGHTER_CALLBACK_PHASE_PRE_INPUT_ANIM = 2,
  MSL_FIGHTER_CALLBACK_PHASE_INPUT = 3,
  MSL_FIGHTER_CALLBACK_PHASE_IASA = 4,
  MSL_FIGHTER_CALLBACK_PHASE_PHYS = 5,
  MSL_FIGHTER_CALLBACK_PHASE_COLL = 6,
  MSL_FIGHTER_CALLBACK_PHASE_PROCESS_HIT = 7,
  MSL_FIGHTER_CALLBACK_PHASE_POST_FRAME = 8,
} MslFighterCallbackPhase;

typedef struct MslFighterCallbackContext {
  MslBatch* batch;
  int bi;
  int p;
  int num_players;
  size_t idx;
  MslFighterCallbackPhase phase;
  uint16_t action_id;
  uint16_t prev_action_id;
  uint8_t char_id;
  uint8_t hitlag;
  uint8_t hitstun;
  uint8_t frozen_by_hitlag;
} MslFighterCallbackContext;

// Precondition: when batch is non-NULL, bi/p identify a valid player slot for that batch.
static inline MslFighterCallbackContext msl_fighter_callback_context_make(
    MslBatch* batch, int bi, int p, int num_players, MslFighterCallbackPhase phase) {
  const size_t idx = msl_idx_player(bi, p);
  MslFighterCallbackContext ctx = {
      .batch = batch,
      .bi = bi,
      .p = p,
      .num_players = num_players,
      .idx = idx,
      .phase = phase,
      .action_id = 0u,
      .prev_action_id = 0u,
      .char_id = 0u,
      .hitlag = 0u,
      .hitstun = 0u,
      .frozen_by_hitlag = 0u,
  };
  if (batch != NULL) {
    ctx.action_id = batch->state.action_id[idx];
    ctx.prev_action_id = batch->state.prev_action_id[idx];
    ctx.char_id = batch->state.char_id[idx];
    ctx.hitlag = batch->state.hitlag[idx];
    ctx.hitstun = batch->state.hitstun[idx];
    ctx.frozen_by_hitlag =
        (ctx.hitlag != 0u || batch->state.hitlag_started_frame[idx] != 0u) ? 1u : 0u;
  }
  return ctx;
}

static inline uint8_t msl_fighter_callback_phase_runs(const MslFighterCallbackContext* ctx) {
  if (ctx == NULL || ctx->batch == NULL) {
    return 0u;
  }
  // Fighter_8006A360 freezes animation advancement and anim_cb while hitlag remains active;
  // Fighter_procUpdate applies the same x2219_b5 gate to IASA and Phys. Map/Coll and ProcessHit
  // remain live in their later source phases. Centralize that phase distinction so character
  // modules cannot accidentally run procedural commands or terminal transitions while frozen.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate,Fighter_procMap,
  //   Fighter_ProcessHit_8006D1EC}
  switch (ctx->phase) {
    case MSL_FIGHTER_CALLBACK_PHASE_PRE_INPUT_ANIM:
    case MSL_FIGHTER_CALLBACK_PHASE_IASA:
    case MSL_FIGHTER_CALLBACK_PHASE_PHYS:
      return ctx->frozen_by_hitlag ? 0u : 1u;
    default:
      return 1u;
  }
}

int fighter_callbacks_step_frame(MslBatch* batch, const uint8_t* prev_input_bytes,
                                 size_t prev_input_stride_bytes, const uint8_t* input_bytes,
                                 size_t input_stride_bytes, uint8_t run_combat);
