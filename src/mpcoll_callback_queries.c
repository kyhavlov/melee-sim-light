#include "mpcoll_callback_queries.h"

#include <math.h>

#include "common_params.h"
#include "mp_lib.h"
#include "mpcoll_ecb_points.h"
#include "stage_collision.h"

static uint8_t query_floor_mask(const MslBatch* batch, size_t idx, uint8_t zero_bottom,
                                MslMpcollFloorMaskResult* out) {
  if (batch == NULL) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint16_t frame = msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
  MslEcbBottomWorldPoint prev = {0};
  MslEcbBottomWorldPoint cur = {0};
  msl_ecb_bottom_world_point_sample(
      &prev, batch->state.char_id[idx], batch->state.animation_index[idx],
      msl_ecb_prev_frame_u16(frame), batch->state.floor_sweep_prev_pos_x[idx],
      batch->state.floor_sweep_prev_pos_y[idx], zero_bottom);
  msl_ecb_bottom_world_point_sample(&cur, batch->state.char_id[idx],
                                    batch->state.animation_index[idx], frame,
                                    batch->state.pos_x[idx], batch->state.pos_y[idx], zero_bottom);
  MslStageQueryHit hit = {0};
  if (cur.y <= prev.y &&
      msl_mplib_sweep_floor((MslBatch*)batch, bi, idx, prev.x, prev.y, cur.x, cur.y,
                            batch->state.floor_skip_segment_id[idx], &hit)) {
    MslMpLibFloorProjection projection = {0};
    if (msl_mplib_project_floor(batch, bi, hit.segment_i, cur.x, cur.y, &projection)) {
      if (out != NULL) {
        out->ground_id = projection.line_id;
        out->corrected_pos_x = batch->state.pos_x[idx];
        out->corrected_pos_y = batch->state.pos_y[idx] + projection.correction_y;
      }
      return 1u;
    }
  }

  return 0u;
}

uint8_t msl_mpcoll_query_477e0_floor_mask(const MslBatch* batch, size_t idx,
                                          MslMpcollFloorMaskResult* out) {
  return query_floor_mask(batch, idx, 0u, out);
}

uint8_t msl_mpcoll_query_48654_floor_mask(const MslBatch* batch, size_t idx,
                                          MslMpcollFloorMaskResult* out) {
  return query_floor_mask(batch, idx, 1u, out);
}

uint8_t msl_mpcoll_query_capture_root_floor_mask(const MslBatch* batch, size_t idx,
                                                 MslMpcollFloorMaskResult* out) {
  if (batch == NULL) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint16_t floor = batch->state.ground_id[idx];
  MslMpLibFloorProjection projection = {0};
  if (floor != 0xFFFFu && floor != batch->state.floor_skip_segment_id[idx] &&
      msl_mplib_project_floor(batch, bi, floor, batch->state.pos_x[idx], batch->state.pos_y[idx],
                              &projection) &&
      projection.correction_y >= 0.0f) {
    if (out != NULL) {
      out->ground_id = projection.line_id;
      out->corrected_pos_x = batch->state.pos_x[idx];
      out->corrected_pos_y = batch->state.pos_y[idx] + projection.correction_y;
    }
    return 1u;
  }
  return query_floor_mask(batch, idx, 0u, out);
}

uint8_t msl_mpcoll_query_capturecut_connected_floor(const MslBatch* batch, size_t idx,
                                                    uint16_t owner_floor,
                                                    MslMpcollFloorMaskResult* out) {
  if (batch == NULL || owner_floor == 0xFFFFu) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const MslStageMap* map = stage_collision_get_map(batch->state.stage_id[(size_t)bi]);
  const MslCommonParams* common = msl_common_params();
  if (map == NULL || common == NULL) {
    return 0u;
  }
  // ftCo_800DC920 publishes mpLib_8005199C_Floor's candidate before testing the signed
  // mpLib_8004DD90_Floor correction against x3BC. A point modestly above its connected floor has
  // a negative correction and is accepted down to that extracted tolerance; it is not an
  // above-floor rejection.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_800DC920
  // data/common/ft_common_data.json::capture_release_floor_tolerance
  for (size_t i = 0; i < map->line_count; i++) {
    const MslStageMapLine* line = &map->lines[i];
    MslMpLibFloorProjection projection = {0};
    if (line->kind != (uint8_t)MSL_STAGE_RAW_LINE_FLOOR || line->fighter_solid == 0u ||
        !msl_mplib_project_floor(batch, bi, line->segment_i, batch->state.pos_x[idx],
                                 batch->state.pos_y[idx], &projection) ||
        projection.contact_y > batch->state.pos_y[idx]) {
      continue;
    }
    if (out != NULL) {
      out->candidate_ground_id = projection.line_id;
      out->candidate_published = 1u;
    }
    if (!stage_collision_map_lines_connected(batch->state.stage_id[(size_t)bi], owner_floor,
                                             projection.line_id) ||
        projection.correction_y < common->capture_release_floor_tolerance) {
      return 0u;
    }
    if (out != NULL) {
      out->ground_id = projection.line_id;
      out->corrected_pos_x = batch->state.pos_x[idx];
      out->corrected_pos_y = batch->state.pos_y[idx] + projection.correction_y;
    }
    return 1u;
  }
  return 0u;
}

void mpcoll_ground_refresh_grounded_root_floor_index(MslBatch* batch, int bi, int p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players) {
    return;
  }
  const size_t idx = msl_idx_player(bi, p);
  if (batch->state.on_ground[idx] == 0u || batch->state.ground_id[idx] == 0xFFFFu) {
    return;
  }
  MslMpLibFloorProjection projection = {0};
  if (msl_mplib_project_floor(batch, bi, batch->state.ground_id[idx], batch->state.pos_x[idx],
                              batch->state.pos_y[idx], &projection)) {
    batch->state.ground_id[idx] = projection.line_id;
    batch->state.ground_contact_x[idx] = projection.contact_x;
    batch->state.ground_contact_y[idx] = projection.contact_y;
    batch->state.ground_normal_x[idx] = projection.normal_x;
    batch->state.ground_normal_y[idx] = projection.normal_y;
  }
}
