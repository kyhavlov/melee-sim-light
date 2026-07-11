#include "mpcoll_source_ground.h"

#include <math.h>
#include <stddef.h>

#include "action_ids.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "ftcommon_ecb.h"
#include "input_axis.h"
#include "motion_state_owners.h"
#include "motion_state_runtime.h"
#include "mp_lib.h"
#include "mpcoll_context.h"
#include "mpcoll_ecb_points.h"
#include "mpcoll_ecb_pose.h"
#include "mpcoll_floor.h"
#include "mpcoll_wall_ceil.h"
#include "stage_collision.h"

enum { MSL_SOURCE_GROUND_MAX_SUBSTEPS = 32 };
static const float k_mpcoll_step_extent = 6.0f;

typedef struct MslSourceGroundCollData {
  float last_x;
  float last_y;
  float prev_x;
  float prev_y;
  float cur_x;
  float cur_y;
  MslEcbWorldPoints prev_ecb;
  MslEcbWorldPoints ecb;
  MslEcbWorldPoints desired_ecb;
  uint16_t floor_id;
  uint32_t env_flags;
  uint32_t prev_env_flags;
  uint8_t touching_floor;
} MslSourceGroundCollData;

static inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

static void interpolate_ecb(MslEcbWorldPoints* out, const MslEcbWorldPoints* a,
                            const MslEcbWorldPoints* b, float root_x, float root_y, float t) {
  mpcoll_ecb_world_points_from_rel(
      out, root_x, root_y, lerp(a->bottom_rel_y, b->bottom_rel_y, t),
      lerp(a->top_rel_y, b->top_rel_y, t), lerp(a->left_rel_x, b->left_rel_x, t),
      lerp(a->right_rel_x, b->right_rel_x, t), lerp(a->side_rel_y, b->side_rel_y, t), b->frame_u16);
}

static void clear_wall_ceiling_contact(MslBatch* batch, size_t idx) {
  batch->state.wall_kind[idx] = 0u;
  batch->state.wall_id[idx] = 0xFFFFu;
  batch->state.ceiling_id[idx] = 0xFFFFu;
  batch->state.wall_contact_x[idx] = 0.0f;
  batch->state.wall_contact_y[idx] = 0.0f;
  batch->state.wall_normal_x[idx] = 0.0f;
  batch->state.wall_normal_y[idx] = 0.0f;
}

static uint8_t edge_snap(MslBatch* batch, int bi, size_t idx, MslSourceGroundCollData* coll,
                         uint8_t mode) {
  float left_x = 0.0f;
  float left_y = 0.0f;
  float right_x = 0.0f;
  float right_y = 0.0f;
  if (!msl_mplib_floor_endpoint(batch, bi, coll->floor_id, -1, &left_x, &left_y, NULL) ||
      !msl_mplib_floor_endpoint(batch, bi, coll->floor_id, +1, &right_x, &right_y, NULL)) {
    return 0u;
  }
  int side = 0;
  if (coll->cur_x <= left_x) {
    side = -1;
  } else if (coll->cur_x >= right_x) {
    side = +1;
  }
  if (side == 0) {
    return 0u;
  }
  const float edge_y = side < 0 ? left_y : right_y;
  if (mode == (uint8_t)MSL_COLL_HANDLER_GROUND_B4B0_TEETER &&
      fabsf(coll->cur_y - edge_y) > k_mpcoll_step_extent) {
    return 0u;
  }
  if (mode == (uint8_t)MSL_COLL_HANDLER_GROUND_B4B0_TEETER) {
    const int facing = batch->state.facing[idx] ? 1 : -1;
    const float stick_x = stick_i8_to_unit(batch->state.input_main_x[idx]);
    if (facing != side || (side < 0 ? stick_x <= -0.75f : stick_x >= 0.75f)) {
      return 0u;
    }
    coll->env_flags |= (uint32_t)MSL_COLLIDE_EDGE;
  } else {
    coll->env_flags |=
        side < 0 ? (uint32_t)MSL_COLLIDE_RIGHT_EDGE : (uint32_t)MSL_COLLIDE_LEFT_EDGE;
  }
  coll->cur_x = side < 0 ? left_x : right_x;
  coll->cur_y = (side < 0 ? left_y : right_y) - coll->ecb.bottom_rel_y + 0.0001f;
  coll->touching_floor = 1u;
  return 1u;
}

static uint8_t resolve_floor(MslBatch* batch, int bi, size_t idx,
                             const MslStageFloorGraph* floor_graph, MslSourceGroundCollData* coll,
                             uint8_t handler_kind, MslMpcollOrderedWallCeilResult* ordered) {
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const int current_line_idx = stage_collision_floor_line_index(stage_id, coll->floor_id);
  float y_correction = 0.0f;
  float normal_x = 0.0f;
  float normal_y = 1.0f;
  const float bottom_y = coll->cur_y + coll->ecb.bottom_rel_y;
  const int projected_line_idx =
      msl_mplib_8004dd90_floor(batch, bi, floor_graph, current_line_idx, coll->cur_x, bottom_y,
                               &y_correction, &normal_x, &normal_y);
  const uint16_t projected_segment =
      projected_line_idx >= 0 ? floor_graph->lines[(size_t)projected_line_idx].segment_i : 0xFFFFu;
  const uint16_t floor_skip = batch->state.floor_skip_segment_id[idx];
  const uint8_t projected_fighter_solid =
      projected_line_idx >= 0
          ? stage_collision_floor_line_is_runtime_fighter_solid(stage_id, projected_segment)
          : 0u;
  if (projected_line_idx >= 0 && projected_fighter_solid &&
      (projected_segment != floor_skip || projected_segment == coll->floor_id)) {
    // mpColl_800488F4 applies mpLib_8004DD90_Floor's signed correction directly. Grounded mode-5
    // ECB loading fixes bottom.y at zero, so this is root-to-floor projection rather than an
    // airborne bottom-sweep admission heuristic.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800488F4,mpColl_8004B108}
    coll->cur_y += y_correction;
    mpcoll_ecb_world_points_from_rel(
        &coll->ecb, coll->cur_x, coll->cur_y, coll->ecb.bottom_rel_y, coll->ecb.top_rel_y,
        coll->ecb.left_rel_x, coll->ecb.right_rel_x, coll->ecb.side_rel_y, coll->ecb.frame_u16);
    coll->floor_id = projected_segment;
    coll->touching_floor = 1u;
    coll->env_flags |= (uint32_t)MSL_COLLIDE_FLOOR_PUSH;
    batch->state.ground_contact_x[idx] = coll->cur_x;
    batch->state.ground_contact_y[idx] = bottom_y + y_correction - 0.0001f;
    batch->state.ground_normal_x[idx] = normal_x;
    batch->state.ground_normal_y[idx] = normal_y;
    if (ordered != NULL) {
      ordered->hit_floor = 1u;
      ordered->touching_floor = 1u;
      ordered->y_after_floor = coll->cur_y;
      ordered->squeeze_flags |= (uint8_t)MSL_MPCOLL_ORDERED_SQUEEZE_FLOOR;
    }
    return 1u;
  }
  if (current_line_idx >= 0) {
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
    if (msl_mplib_floor_endpoint(batch, bi, coll->floor_id, -1, &left_x, &left_y, NULL) &&
        msl_mplib_floor_endpoint(batch, bi, coll->floor_id, +1, &right_x, &right_y, NULL)) {
      if (coll->cur_x < left_x) {
        coll->env_flags |= (uint32_t)MSL_COLLIDE_LEFT_LEDGE_SLIP;
      } else if (coll->cur_x > right_x) {
        coll->env_flags |= (uint32_t)MSL_COLLIDE_RIGHT_LEDGE_SLIP;
      }
    }
  }
  if (handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_B2DC_FALL ||
      handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_OTTOTTO ||
      handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_B4B0_TEETER) {
    return edge_snap(batch, bi, idx, coll, handler_kind) ? 2u : 0u;
  }
  return 0u;
}

static void enter_air_state(MslBatch* batch, int bi, int p, uint16_t action_id,
                            uint32_t animation_index) {
  const size_t idx = msl_idx_player(bi, p);
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return;
  }
  batch->state.jumps_left[idx] = ch->max_jumps > 0u ? (uint8_t)(ch->max_jumps - 1u) : 0u;
  // ftCommon_8007D5D4 locks CollData here. Packet 1 publishes the lock timer but leaves the
  // simulator's more specific desired-bottom provenance lane clear, matching the retained common
  // airborne callback boundary. Packet 2 will replace that consumer and can carry the full live
  // CollData lock packet end-to-end; exposing it to the legacy approximation changes its wall ECB.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80047E14}
  batch->state.ecb_lock_timer[idx] = MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR;
  batch->state.coll_desired_ecb_bottom_locked_owner[idx] = 0u;
  batch->state.fall_fast[idx] = 0u;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.on_ground[idx] = 0u;
  float air_x = batch->state.speed_air_x_self[idx];
  if (air_x > ch->air_drift_max) {
    air_x = ch->air_drift_max;
  } else if (air_x < -ch->air_drift_max) {
    air_x = -ch->air_drift_max;
  }
  batch->state.speed_air_x_self[idx] = air_x;
  motion_state_change(
      batch, bi, p, action_id, animation_index,
      action_id == (uint16_t)MSL_ACT_FALL ? (uint32_t)MSL_MOTION_ENTRY_KEEP_FASTFALL : 0u, 0.0f,
      1.0f, MSL_ANIM_ENTER_TICK_NONE);
}

static void enter_ottotto(MslBatch* batch, int bi, int p) {
  const size_t idx = msl_idx_player(bi, p);
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  motion_state_change(batch, bi, p, (uint16_t)MSL_ACT_OTTOTTO, (uint32_t)MSL_SM_OTTOTTO, 0u, 0.0f,
                      1.0f, MSL_ANIM_ENTER_TICK_NONE);
}

static void enter_stop_wall(MslBatch* batch, int bi, int p) {
  const size_t idx = msl_idx_player(bi, p);
  batch->state.speed_air_x_self[idx] = 0.0f;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.speed_y_self[idx] = 0.0f;
  batch->state.speed_x_attack[idx] = 0.0f;
  batch->state.speed_y_attack[idx] = 0.0f;
  motion_state_change(batch, bi, p, (uint16_t)MSL_ACT_STOP_WALL, (uint32_t)MSL_SM_STOP_WALL, 0u,
                      0.0f, 1.0f, MSL_ANIM_ENTER_TICK_NONE);
}

static void run_handler_transition(MslBatch* batch, int bi, int p, uint8_t handler_kind,
                                   const MslSourceGroundCollData* coll) {
  const size_t idx = msl_idx_player(bi, p);
  if (!coll->touching_floor) {
    if (handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_RUN &&
        batch->state.action_id[idx] == (uint16_t)MSL_ACT_DASH &&
        batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_TURN &&
        batch->state.action_frame[idx] <= 1 && batch->state.speed_air_x_self[idx] != 0.0f) {
      // Turn_IASA's just-turned branch can enter Dash before this same-frame Dash_Coll. At a stage
      // ledge, the signed Dash self-velocity is the outgoing source-facing lane when the public
      // Turn facing snapshot lags that microphase. Do not apply it to arbitrary platform exits:
      // those preserve Dash's installed facing even when residual momentum points backward.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::{ftCo_Turn_IASA,fn_800C9C2C}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::{ftCo_Dash_Enter,ftCo_Dash_Coll}
      // data/stages/bin/*.bin::MSLSTG01 ledge floor identities
      const uint8_t source_facing = batch->state.speed_air_x_self[idx] > 0.0f ? 1u : 0u;
      const MslStageFloorLine* ledge_floor = stage_collision_get_ledge_floor_line(
          batch->state.stage_id[(size_t)bi], source_facing ? 1 : 0);
      if (ledge_floor != NULL && ledge_floor->segment_i == coll->floor_id) {
        batch->state.facing[idx] = source_facing;
      }
    }
    const int facing = batch->state.facing[idx] ? 1 : -1;
    const uint32_t facing_slip =
        facing < 0 ? (uint32_t)MSL_COLLIDE_RIGHT_LEDGE_SLIP : (uint32_t)MSL_COLLIDE_LEFT_LEDGE_SLIP;
    if ((handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_GUARD ||
         handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_GUARD_SETOFF) &&
        (coll->env_flags & facing_slip) != 0u) {
      batch->state.speed_y_attack[idx] = 0.0f;
      enter_air_state(batch, bi, p, (uint16_t)MSL_ACT_MISS_FOOT, (uint32_t)MSL_SM_MISS_FOOT);
    } else {
      enter_air_state(batch, bi, p, (uint16_t)MSL_ACT_FALL, (uint32_t)MSL_SM_FALL);
    }
    return;
  }
  if (handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_B4B0_TEETER &&
      (coll->env_flags & (uint32_t)MSL_COLLIDE_EDGE) != 0u) {
    enter_ottotto(batch, bi, p);
    return;
  }
  if (handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_RUN) {
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    const int facing = batch->state.facing[idx] ? 1 : -1;
    const uint32_t facing_wall =
        facing < 0 ? (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG : (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG;
    if (ch != NULL && (coll->env_flags & facing_wall) != 0u &&
        fabsf(batch->state.speed_ground_x_self[idx]) > ch->walk_max_vel) {
      enter_stop_wall(batch, bi, p);
    }
  }
  if (handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_B2DC_FALL &&
      (coll->env_flags & (uint32_t)(MSL_COLLIDE_LEFT_EDGE | MSL_COLLIDE_RIGHT_EDGE)) != 0u) {
    batch->state.speed_air_x_self[idx] = 0.0f;
    batch->state.speed_ground_x_self[idx] = 0.0f;
    batch->state.speed_y_self[idx] = 0.0f;
    batch->state.speed_x_attack[idx] = 0.0f;
    batch->state.speed_y_attack[idx] = 0.0f;
  }
}

static void source_ground_callback(MslBatch* batch, int bi, int p, uint8_t handler_kind) {
  const size_t idx = msl_idx_player(bi, p);
  if (handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_B4B0_TEETER) {
    const float facing = batch->state.facing[idx] ? 1.0f : -1.0f;
    if (batch->state.player_nudge_x[idx] != 0.0f &&
        batch->state.player_nudge_x[idx] * facing < 0.0f) {
      // ft_80084280 selects the non-teeter 4B2DC wrapper when the current xF8 overlap nudge points
      // behind the fighter.
      // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
      handler_kind = (uint8_t)MSL_COLL_HANDLER_GROUND_B2DC_FALL;
    }
  } else if (handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_GUARD_SETOFF) {
    // GuardSetOff_Coll selects ft_80084104/4B2DC while shield SDI is live and the ordinary
    // guard/MissFoot wrapper otherwise.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardSetOff_Coll
    handler_kind = batch->state.damage_allow_sdi[idx] ? (uint8_t)MSL_COLL_HANDLER_GROUND_B2DC_FALL
                                                      : (uint8_t)MSL_COLL_HANDLER_GROUND_GUARD;
  }
  MslSourceGroundCollData coll = {0};
  coll.floor_id = batch->state.ground_id[idx];
  const uint16_t persisted_floor_id = coll.floor_id;
  coll.last_x = isfinite(batch->state.floor_sweep_prev_pos_x[idx])
                    ? batch->state.floor_sweep_prev_pos_x[idx]
                    : batch->state.prev_pos_x[idx];
  coll.last_y = isfinite(batch->state.floor_sweep_prev_pos_y[idx])
                    ? batch->state.floor_sweep_prev_pos_y[idx]
                    : batch->state.prev_pos_y[idx];
  coll.cur_x = batch->state.pos_x[idx];
  coll.cur_y = batch->state.pos_y[idx];
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageFloorGraph* floor_graph = stage_collision_get_floor_graph(stage_id);
  const int persisted_floor_i = stage_collision_floor_line_index(stage_id, persisted_floor_id);
  if (floor_graph != NULL && persisted_floor_i >= 0 &&
      (size_t)persisted_floor_i < floor_graph->line_count) {
    float carry_dx = 0.0f;
    float carry_dy = 0.0f;
    if (stage_collision_floor_line_motion_delta(
            batch, bi, &floor_graph->lines[(size_t)persisted_floor_i], &carry_dx, &carry_dy)) {
      coll.cur_x += carry_dx;
      coll.cur_y += carry_dy;
    }
  }
  coll.prev_env_flags = batch->state.coll_env_flags[idx];
  coll.env_flags = 0u;
  const uint16_t frame = msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
  const float facing = batch->state.facing[idx] ? 1.0f : -1.0f;
  msl_ecb_world_points_sample(&coll.desired_ecb, batch->state.char_id[idx],
                              batch->state.animation_index[idx], frame, facing, coll.cur_x,
                              coll.cur_y, 1u);
  // mpCollInterpolateECB starts from CollData.ecb and copies that packet to prev_ecb before
  // interpolating toward the freshly loaded desired ECB. Do not reconstruct the start packet from
  // an animation frame: squeeze, lock, and earlier callback ownership can all make the persistent
  // CollData packet differ from a fresh pose sample.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
  if (!mpcoll_state_current_ecb_points(batch, idx, &coll.prev_ecb, coll.last_x, coll.last_y,
                                       msl_ecb_prev_frame_u16(frame))) {
    msl_ecb_world_points_sample(&coll.prev_ecb, batch->state.char_id[idx],
                                batch->state.animation_index[idx], msl_ecb_prev_frame_u16(frame),
                                facing, coll.last_x, coll.last_y, 1u);
  }
  const MslEcbWorldPoints source_prev_ecb = coll.prev_ecb;
  MslEcbWorldPoints interpolation_start_ecb = coll.prev_ecb;
  if (mpcoll_state_squeeze_restore_ecb_points(batch, idx, &interpolation_start_ecb, coll.last_x,
                                              coll.last_y, msl_ecb_prev_frame_u16(frame))) {
    batch->state.coll_squeeze_restore_ecb_valid[idx] = 0u;
  }
  const float dx = coll.cur_x - coll.last_x;
  const float dy = coll.cur_y - coll.last_y;
  float vertical_extent_delta = fabsf(dy);
  const float top_delta = fabsf(coll.desired_ecb.top_rel_y - coll.prev_ecb.top_rel_y);
  if (top_delta > vertical_extent_delta) {
    vertical_extent_delta = top_delta;
  }
  const float side_y_delta = fabsf(coll.desired_ecb.side_rel_y - coll.prev_ecb.side_rel_y);
  if (side_y_delta > vertical_extent_delta) {
    vertical_extent_delta = side_y_delta;
  }
  int steps = vertical_extent_delta > k_mpcoll_step_extent
                  ? (int)(vertical_extent_delta / k_mpcoll_step_extent) + 1
                  : 1;
  if (steps > MSL_SOURCE_GROUND_MAX_SUBSTEPS) {
    steps = MSL_SOURCE_GROUND_MAX_SUBSTEPS;
  }
  const float step_dx = dx / (float)steps;
  const float step_dy = dy / (float)steps;
  coll.cur_x = coll.last_x;
  coll.cur_y = coll.last_y;
  coll.touching_floor = 0u;
  clear_wall_ceiling_contact(batch, idx);
  const MslStageCeilingGraph* ceiling_graph = stage_collision_get_ceiling_graph(stage_id);
  const MslStageWallGraph* left_wall_graph = stage_collision_get_left_wall_graph(stage_id);
  const MslStageWallGraph* right_wall_graph = stage_collision_get_right_wall_graph(stage_id);
  MslMpcollContext context = mpcoll_context_make(batch, bi, idx, stage_id, floor_graph,
                                                 ceiling_graph, left_wall_graph, right_wall_graph);
  context.was_grounded = 1u;
  context.prefer_floor_line_idx = persisted_floor_i;
  mpcoll_clear_callback_floor_result(&context, coll.last_x, coll.last_y, coll.cur_x, coll.cur_y);
  MslEcbWorldPoints wall_prev_ecb = source_prev_ecb;
  for (int step = 1; step <= steps; step++) {
    const float t = (float)step / (float)steps;
    coll.prev_x = coll.cur_x;
    coll.prev_y = coll.cur_y;
    coll.cur_x += step_dx;
    coll.cur_y += step_dy;
    interpolate_ecb(&coll.ecb, &interpolation_start_ecb, &coll.desired_ecb, coll.cur_x, coll.cur_y,
                    t);
    batch->state.pos_x[idx] = coll.cur_x;
    batch->state.pos_y[idx] = coll.cur_y;
    batch->state.ground_id[idx] = coll.floor_id;
    MslMpcollOrderedWallCeilResult ordered = {0};
    mpcoll_grounded_wall_ceil_ordered_begin(&context, &wall_prev_ecb, &coll.ecb, &ordered);
    coll.ecb = ordered.cur_ecb_after;
    coll.cur_x = batch->state.pos_x[idx];
    coll.cur_y = batch->state.pos_y[idx];
    coll.env_flags |= batch->state.coll_env_flags[idx] &
                      (uint32_t)(MSL_COLLIDE_LEFT_WALL_MASK | MSL_COLLIDE_RIGHT_WALL_MASK |
                                 MSL_COLLIDE_CEILING_MASK);
    coll.touching_floor = 0u;
    const uint8_t floor_result =
        resolve_floor(batch, bi, idx, floor_graph, &coll, handler_kind, &ordered);
    if (floor_result != 0u) {
      const uint8_t result_mode = floor_result == 2u
                                      ? (uint8_t)MSL_MPCOLL_FLOOR_MODE_EDGE_SNAP
                                      : (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION;
      mpcoll_record_callback_floor_result_with_mode(
          &context, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, result_mode, coll.floor_id,
          batch->state.ground_contact_x[idx], batch->state.ground_contact_y[idx],
          batch->state.ground_normal_x[idx], batch->state.ground_normal_y[idx]);
      batch->state.pos_x[idx] = coll.cur_x;
      batch->state.pos_y[idx] = coll.cur_y;
      (void)mpcoll_grounded_ceiling_ordered_retry(&context, &wall_prev_ecb, &coll.ecb, &ordered);
      coll.cur_x = batch->state.pos_x[idx];
      coll.cur_y = batch->state.pos_y[idx];
    }
    if (floor_result == 2u) {
      break;
    }
    wall_prev_ecb = coll.ecb;
  }
  if (!coll.touching_floor && floor_graph != NULL && persisted_floor_i >= 0 &&
      carried_floor_line_is_live_yoshi_shyguy_support(batch, bi, floor_graph, stage_id,
                                                      persisted_floor_i)) {
    // grStory's Shy Guy item owns the carried CollData floor; the raw map line is deliberately not
    // selectable as new static fighter terrain.
    // refs/melee/src/melee/gr/grstory.c::grStory_801E3418
    // refs/melee/src/melee/it/items/itheiho.c
    // data/stages/bin/grst.bin::MSLSTG01 stage_object_support_kind
    coll.floor_id = persisted_floor_id;
    coll.touching_floor = 1u;
    coll.env_flags |= (uint32_t)MSL_COLLIDE_FLOOR_PUSH;
    mpcoll_record_callback_floor_result_with_mode(
        &context, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
        (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAGE_OBJECT_CARRY, coll.floor_id, coll.cur_x, coll.cur_y,
        batch->state.ground_normal_x[idx], batch->state.ground_normal_y[idx]);
  }
  if (floor_graph != NULL && persisted_floor_i >= 0) {
    uint16_t retry_floor_id = 0xFFFFu;
    float retry_x = 0.0f;
    float retry_y = 0.0f;
    float retry_nx = 0.0f;
    float retry_ny = 1.0f;
    const float prev_bottom_x = coll.last_x;
    const float prev_bottom_y = coll.last_y + source_prev_ecb.bottom_rel_y;
    const float prev_side_mid_y =
        coll.last_y + 0.5f * (source_prev_ecb.top_rel_y + source_prev_ecb.bottom_rel_y);
    const float cur_bottom_x = coll.cur_x;
    const float cur_bottom_y = coll.cur_y + coll.ecb.bottom_rel_y;
    batch->state.pos_x[idx] = coll.cur_x;
    batch->state.pos_y[idx] = coll.cur_y;
    if (floor_4a908_retry(batch, idx, bi, floor_graph, stage_id, persisted_floor_i, prev_bottom_x,
                          prev_bottom_y, prev_side_mid_y, cur_bottom_x, cur_bottom_y,
                          batch->state.floor_skip_segment_id != NULL
                              ? batch->state.floor_skip_segment_id[idx]
                              : 0xFFFFu,
                          &retry_floor_id, &retry_x, &retry_y, &retry_nx, &retry_ny)) {
      coll.cur_x = batch->state.pos_x[idx];
      coll.cur_y = retry_y;
      coll.floor_id = retry_floor_id;
      coll.touching_floor = 1u;
      coll.env_flags |= (uint32_t)MSL_COLLIDE_FLOOR_PUSH;
      batch->state.ground_contact_x[idx] = retry_x;
      batch->state.ground_contact_y[idx] = retry_y;
      batch->state.ground_normal_x[idx] = retry_nx;
      batch->state.ground_normal_y[idx] = retry_ny;
      batch->state.coll_floor_result_valid[idx] = 1u;
      batch->state.coll_floor_result_source[idx] =
          (uint8_t)MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY;
      batch->state.coll_floor_result_mode[idx] = (uint8_t)MSL_MPCOLL_FLOOR_MODE_4A908_RETRY;
      batch->state.coll_floor_result_segment_id[idx] = retry_floor_id;
      batch->state.coll_floor_result_contact_x[idx] = retry_x;
      batch->state.coll_floor_result_contact_y[idx] = retry_y;
      batch->state.coll_floor_result_normal_x[idx] = retry_nx;
      batch->state.coll_floor_result_normal_y[idx] = retry_ny;
    }
  }
  if (handler_kind == (uint8_t)MSL_COLL_HANDLER_GROUND_OTTOTTO && coll.touching_floor) {
    const int side = batch->state.facing[idx] ? 1 : -1;
    float edge_x = 0.0f;
    float edge_y = 0.0f;
    if (msl_mplib_floor_endpoint(batch, bi, persisted_floor_id, side, &edge_x, &edge_y, NULL) &&
        (fabsf(coll.last_x - edge_x) <= 0.0001f ||
         fabsf(batch->state.prev_pos_x[idx] - edge_x) <= 0.0001f ||
         fabsf(coll.cur_x - edge_x) <= fabsf(batch->state.player_nudge_x[idx]) + 0.0001f)) {
      // A steady Ottotto callback starts at the facing floor endpoint. Fighter_procUpdate may add
      // xF8_playerNudgeVel before Coll; 4B2DC/4A45C republishes the same endpoint rather than
      // allowing that transient overlap displacement to accumulate.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::{ftCo_Ottotto_Coll,ftCo_OttottoWait_Coll}
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor}
      coll.cur_x = edge_x;
      coll.cur_y = edge_y - coll.ecb.bottom_rel_y + 0.0001f;
      coll.floor_id = persisted_floor_id;
    }
  }
  coll.env_flags |= batch->state.coll_env_flags[idx] &
                    (uint32_t)(MSL_COLLIDE_LEFT_WALL_MASK | MSL_COLLIDE_RIGHT_WALL_MASK |
                               MSL_COLLIDE_CEILING_MASK);
  batch->state.coll_prev_env_flags[idx] = coll.prev_env_flags;
  batch->state.coll_env_flags[idx] = coll.env_flags;
  batch->state.pos_x[idx] = coll.cur_x;
  batch->state.pos_y[idx] = coll.cur_y;
  batch->state.on_ground[idx] = coll.touching_floor;
  batch->state.ground_id[idx] = coll.touching_floor ? coll.floor_id : persisted_floor_id;
  batch->state.coll_last_pos_x[idx] = coll.last_x;
  batch->state.coll_last_pos_y[idx] = coll.last_y;
  batch->state.coll_substep_prev_pos_x[idx] = coll.prev_x;
  batch->state.coll_substep_prev_pos_y[idx] = coll.prev_y;
  batch->state.coll_substep_cur_pos_x[idx] = coll.cur_x;
  batch->state.coll_substep_cur_pos_y[idx] = coll.cur_y;
  MslEcbWorldPoints stored_prev_ecb = source_prev_ecb;
  mpcoll_penultimate_interpolated_ecb(&stored_prev_ecb, &source_prev_ecb, &interpolation_start_ecb,
                                      &coll.ecb, coll.last_x, coll.last_y, coll.cur_x, coll.cur_y);
  mpcoll_store_prev_ecb_points(batch, idx, &stored_prev_ecb);
  mpcoll_store_current_ecb_points(batch, idx, &coll.ecb);
  mpcoll_store_desired_ecb_points(batch, idx, &coll.desired_ecb);
  run_handler_transition(batch, bi, p, handler_kind, &coll);
}

void mpcoll_source_ground_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.live_coll_migrated_ran[idx] = 0u;
      const uint8_t handler = batch->state.live_coll_handler_kind[idx];
      if (handler == (uint8_t)MSL_COLL_HANDLER_LEGACY) {
        continue;
      }
      batch->state.live_coll_migrated_ran[idx] = 1u;
      // Fighter_procMap decrements the ECB lock and unlocks CollData before invoking coll_cb.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      if (batch->state.ecb_lock_timer[idx] != 0u) {
        batch->state.ecb_lock_timer[idx]--;
        if (batch->state.ecb_lock_timer[idx] == 0u) {
          msl_ftcommon_unlock_ecb(batch, idx);
        }
      }
      source_ground_callback(batch, bi, p, handler);
    }
  }
}
