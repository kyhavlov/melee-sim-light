#pragma once

#include "config.h"
#include "state.h"

struct MslBatch {
  int batch_size;
  MslConfig config;
  MslStateSoA state;
};

static inline size_t msl_idx_player(int bi, int p) {
  return (size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p;
}

static inline size_t msl_idx_item(int bi, int it) {
  return (size_t)bi * (size_t)MSL_MAX_ITEMS + (size_t)it;
}
