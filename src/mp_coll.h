#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"
#include "mpcoll_ecb_points.h"

typedef struct MslMpCollFrame {
  float prev_x;
  float prev_y;
  float cur_x;
  float cur_y;
  MslEcbWorldPoints prev_ecb;
  MslEcbWorldPoints ecb;
  MslEcbWorldPoints desired_ecb;
  float floor_prev_bottom_x;
  float floor_prev_bottom_y;
  uint32_t prev_env_flags;
  uint32_t env_flags;
  uint16_t left_wall_id;
  uint16_t right_wall_id;
  uint16_t floor_id;
  uint8_t grounded;
  uint8_t floor_prev_bottom_valid;
  uint8_t squeezed;
  uint8_t outer_stop;
} MslMpCollFrame;

typedef struct MslMpCollAirStepResult {
  uint16_t floor_id;
  uint8_t floor_contact;
  uint8_t floor_contact_soft;
  uint8_t touched_floor;
  uint8_t convergence_flags;
  uint8_t outer_stop;
} MslMpCollAirStepResult;

enum {
  MSL_MPCOLL_WALL_KIND_NONE = 0u,
  MSL_MPCOLL_WALL_KIND_LEFT = 1u,
  MSL_MPCOLL_WALL_KIND_RIGHT = 2u,
};

// Ordered wall/ceiling portion of mpColl_80046904/mpColl_8004ACE4. The caller owns floor
// resolution and invokes the ceiling retry after floor publication when required.
// refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpColl_8004ACE4}
uint8_t msl_mpcoll_resolve_walls_ceiling(MslBatch* batch, int bi, size_t idx,
                                         MslMpCollFrame* frame);
uint8_t msl_mpcoll_resolve_ceiling(MslBatch* batch, int bi, size_t idx, MslMpCollFrame* frame);
void msl_mpcoll_squeeze_vertical(MslBatch* batch, size_t idx, MslMpCollFrame* frame,
                                 uint8_t airborne, float y_after_ceiling, float y_after_floor);
void msl_mpcoll_clear_wall_ceiling(MslBatch* batch, size_t idx);

// One ordered mpColl_80046904 substep: wall pairs, ceiling, floor check/commit, and ceiling retry.
// Grounded mpColl_8004ACE4 falls through to this same owner when its carried-floor projection and
// edge recipe fail.
// refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpColl_8004ACE4}
MslMpCollAirStepResult msl_mpcoll_resolve_air_step(MslBatch* batch, int bi, size_t idx,
                                                   MslMpCollFrame* frame, uint16_t floor_skip,
                                                   uint8_t stay_airborne, uint8_t platform_pass,
                                                   uint8_t hard_floor_only);

// Legal-stage portions of mpCollGetSpeed{Floor,Wall,Ceiling} and mpColl_IsOnPlatform.
// Static lines publish zero motion; extracted moving-floor state supplies transformed-platform
// velocity. refs/melee/src/melee/mp/mpcoll.c::{mpCollGetSpeedFloor,
// mpCollGetSpeedLeftWall,mpCollGetSpeedRightWall,mpCollGetSpeedCeiling,mpColl_IsOnPlatform}
uint8_t mpcoll_get_speed_floor_static(const MslBatch* batch, int bi, size_t idx, float* x,
                                      float* y);
uint8_t mpcoll_get_speed_left_wall_static(const MslBatch* batch, int bi, size_t idx, float* x,
                                          float* y);
uint8_t mpcoll_get_speed_right_wall_static(const MslBatch* batch, int bi, size_t idx, float* x,
                                           float* y);
uint8_t mpcoll_get_speed_ceiling_static(const MslBatch* batch, int bi, size_t idx, float* x,
                                        float* y);
uint8_t mpcoll_is_on_platform(const MslBatch* batch, int bi, size_t idx);
