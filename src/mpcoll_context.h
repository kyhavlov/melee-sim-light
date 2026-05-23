#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "mpcoll_ecb_points.h"
#include "stage_collision.h"

typedef struct MslMpcollContext {
  MslBatch* batch;
  size_t idx;
  int bi;
  uint32_t stage_id;
  const MslStageFloorGraph* floor_graph;
  const MslStageCeilingGraph* ceiling_graph;
  const MslStageWallGraph* left_wall_graph;
  const MslStageWallGraph* right_wall_graph;
  uint8_t char_id;
  uint16_t action_id;
  uint16_t prev_action_id;
  uint32_t anim;
  uint16_t ecb_frame;
  uint8_t was_grounded;
  int prefer_floor_line_idx;
  const MslMpcollLoadedEcb* loaded_ecb;
} MslMpcollContext;

// Precondition: when batch is non-NULL, idx is a valid player-state index for that batch.
static inline MslMpcollContext mpcoll_context_make(MslBatch* batch, int bi, size_t idx,
                                                   uint32_t stage_id,
                                                   const MslStageFloorGraph* floor_graph,
                                                   const MslStageCeilingGraph* ceiling_graph,
                                                   const MslStageWallGraph* left_wall_graph,
                                                   const MslStageWallGraph* right_wall_graph) {
  MslMpcollContext ctx = {
      .batch = batch,
      .idx = idx,
      .bi = bi,
      .stage_id = stage_id,
      .floor_graph = floor_graph,
      .ceiling_graph = ceiling_graph,
      .left_wall_graph = left_wall_graph,
      .right_wall_graph = right_wall_graph,
      .char_id = 0u,
      .action_id = 0u,
      .prev_action_id = 0u,
      .anim = 0u,
      .ecb_frame = 0u,
      .was_grounded = 0u,
      .prefer_floor_line_idx = -1,
      .loaded_ecb = NULL,
  };
  if (batch != NULL) {
    ctx.char_id = batch->state.char_id[idx];
    ctx.action_id = batch->state.action_id[idx];
    ctx.prev_action_id = batch->state.prev_action_id[idx];
    ctx.anim = batch->state.animation_index[idx];
  }
  return ctx;
}
