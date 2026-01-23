#pragma once

#include "config.h"
#include "state.h"

struct MslBatch {
  int batch_size;
  MslConfig config;
  MslStateSoA state;

  // Debug-only per-fighter override for hit status eligibility (opcode 26).
  // Indexed like other per-player state arrays: [batch_size * MSL_MAX_PLAYERS].
  // Value 0xFF means "no override; use table lookup".
  uint8_t* debug_hit_status_override;
};

static inline size_t msl_idx_player(int bi, int p) {
  return (size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p;
}

static inline size_t msl_idx_item(int bi, int it) {
  return (size_t)bi * (size_t)MSL_MAX_ITEMS + (size_t)it;
}
