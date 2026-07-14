#include "mpcoll_source_ground.h"

#include <math.h>
#include <stddef.h>

#include "action_ids.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "combat_internal.h"
#include "ftcommon_ecb.h"
#include "falcon_specials.h"
#include "grab_attachment.h"
#include "input_axis.h"
#include "knockdown.h"
#include "match_flow.h"
#include "marth_specials.h"
#include "motion_state_owners.h"
#include "motion_state_runtime.h"
#include "mp_coll.h"
#include "mp_lib.h"
#include "mpcoll_ecb_points.h"
#include "mpcoll_ecb_pose.h"
#include "sheik_specials.h"
#include "spacie_specials.h"
#include "special_msids.h"
#include "stage_collision.h"
#include "state_flags.h"
#include "throw_flow.h"

enum { MSL_SOURCE_GROUND_MAX_SUBSTEPS = 255 };
static const float k_mpcoll_step_extent = 6.0f;
static const float k_stage_object_surface_match_epsilon = 0.0011f;

static uint8_t selector_has_ground_wrapper(uint8_t selector) {
  switch ((MslCollWrapperSelectorKind)selector) {
    case MSL_COLL_SELECTOR_GROUND_B108:
    case MSL_COLL_SELECTOR_GROUND_B2DC:
    case MSL_COLL_SELECTOR_GROUND_B4B0_NUDGE:
    case MSL_COLL_SELECTOR_GROUND_STOPWALL_B5C4:
    case MSL_COLL_SELECTOR_GROUND_B108_CONSTRAINED:
    case MSL_COLL_SELECTOR_GA_DAMAGE:
    case MSL_COLL_SELECTOR_GUARD_SETOFF_ALLOW_SDI:
    case MSL_COLL_SELECTOR_GA_CAPTURECUT:
    case MSL_COLL_SELECTOR_GA_CATCHCUT:
    case MSL_COLL_SELECTOR_GA_CLIFF_ACTION:
    case MSL_COLL_SELECTOR_GA_THROW:
    case MSL_COLL_SELECTOR_GA_ENTRY_CUSTOM:
    case MSL_COLL_SELECTOR_GA_B108_AIR471:
    case MSL_COLL_SELECTOR_GA_B2DC_AIR471:
    case MSL_COLL_SELECTOR_GA_B108_AIR_LEDGE_BOTH:
    case MSL_COLL_SELECTOR_FALCON_LW_END:
    case MSL_COLL_SELECTOR_FALCON_S_START:
    case MSL_COLL_SELECTOR_MARS_HI:
      return 1u;
    default:
      return 0u;
  }
}

static uint8_t selector_resolves_ground_from_live_ga(uint8_t selector) {
  switch ((MslCollWrapperSelectorKind)selector) {
    case MSL_COLL_SELECTOR_GA_DAMAGE:
    case MSL_COLL_SELECTOR_GA_CAPTURECUT:
    case MSL_COLL_SELECTOR_GA_CATCHCUT:
    case MSL_COLL_SELECTOR_GA_CLIFF_ACTION:
    case MSL_COLL_SELECTOR_GA_THROW:
    case MSL_COLL_SELECTOR_GA_ENTRY_CUSTOM:
    case MSL_COLL_SELECTOR_GA_B108_AIR471:
    case MSL_COLL_SELECTOR_GA_B2DC_AIR471:
    case MSL_COLL_SELECTOR_GA_B108_AIR_LEDGE_BOTH:
    case MSL_COLL_SELECTOR_FALCON_LW_END:
    case MSL_COLL_SELECTOR_MARS_HI:
      return 1u;
    default:
      return 0u;
  }
}

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

static uint16_t runtime_carried_floor_id(MslBatch* batch, int bi, uint16_t public_floor_id,
                                         float root_x, float root_y) {
  if (batch == NULL || public_floor_id == 0xFFFFu ||
      stage_collision_floor_line_is_runtime_fighter_solid(batch->state.stage_id[(size_t)bi],
                                                          public_floor_id) ||
      stage_collision_floor_line_stage_object_support_kind(batch->state.stage_id[(size_t)bi],
                                                           public_floor_id) !=
          (uint8_t)MSL_STAGE_OBJECT_SUPPORT_YOSHI_SHYGUY) {
    return public_floor_id;
  }
  const MslStageFloorGraph* floors =
      stage_collision_get_floor_graph(batch->state.stage_id[(size_t)bi]);
  const int randall_i = stage_collision_randall_floor_line_index(batch->state.stage_id[(size_t)bi]);
  if (floors == NULL || randall_i < 0 || (size_t)randall_i >= floors->line_count) {
    return public_floor_id;
  }
  const uint16_t randall_id = floors->lines[(size_t)randall_i].segment_i;
  MslMpLibFloorProjection projection = {0};
  if (!msl_mplib_project_floor(batch, bi, randall_id, root_x, root_y, &projection) ||
      fabsf(projection.correction_y) > k_stage_object_surface_match_epsilon) {
    return public_floor_id;
  }
  // Slippi's public line id aliases Yoshi stage-object floors. MSLSTG01 promotes Randall to a
  // generated path-transformed line, so a grounded root lying on that current-owned surface uses
  // the generated id internally for carry/projection and republishes the public id afterward.
  // The epsilon covers only the extracted f32 surface-vs-post-frame root quantization.
  // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,grStory_801E33E0}
  // refs/melee/src/melee/gr/ground.c::Ground_801C2FE0
  // data/stages/bin/grst.bin::MSLSTG01 platform_transforms(kind=randall)
  return projection.line_id;
}

static void record_floor_result(MslBatch* batch, size_t idx, uint8_t source, uint8_t mode,
                                uint16_t segment_id, float x, float y, float nx, float ny) {
  batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_FLOOR_MASK;
  batch->state.coll_floor_result_valid[idx] = 1u;
  batch->state.coll_floor_result_source[idx] = source;
  batch->state.coll_floor_result_mode[idx] = mode;
  batch->state.coll_floor_result_segment_id[idx] = segment_id;
  batch->state.coll_floor_result_contact_x[idx] = x;
  batch->state.coll_floor_result_contact_y[idx] = y;
  batch->state.coll_floor_result_normal_x[idx] = nx;
  batch->state.coll_floor_result_normal_y[idx] = ny;
}

static void interpolate_ecb(MslEcbWorldPoints* out, const MslEcbWorldPoints* a,
                            const MslEcbWorldPoints* b, float root_x, float root_y, float t) {
  mpcoll_ecb_world_points_from_rel(
      out, root_x, root_y, lerp(a->bottom_rel_y, b->bottom_rel_y, t),
      lerp(a->top_rel_y, b->top_rel_y, t), lerp(a->left_rel_x, b->left_rel_x, t),
      lerp(a->right_rel_x, b->right_rel_x, t), lerp(a->side_rel_y, b->side_rel_y, t), b->frame_u16);
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
  MslMpLibFloorProjection projection = {0};
  if (msl_mplib_project_floor(batch, bi, coll->floor_id, coll->cur_x,
                              coll->cur_y + coll->ecb.bottom_rel_y, &projection)) {
    coll->floor_id = projection.line_id;
    batch->state.ground_contact_x[idx] = projection.contact_x;
    batch->state.ground_contact_y[idx] = projection.contact_y;
    batch->state.ground_normal_x[idx] = projection.normal_x;
    batch->state.ground_normal_y[idx] = projection.normal_y;
  }
  coll->touching_floor = 1u;
  return 1u;
}

static uint8_t resolve_floor(MslBatch* batch, int bi, size_t idx, MslSourceGroundCollData* coll,
                             uint8_t handler_kind) {
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const float bottom_y = coll->cur_y + coll->ecb.bottom_rel_y;
  MslMpLibFloorProjection projection = {0};
  uint8_t projected =
      msl_mplib_project_floor(batch, bi, coll->floor_id, coll->cur_x, bottom_y, &projection);
  uint16_t projected_segment = projected ? projection.line_id : 0xFFFFu;
  uint8_t projected_fighter_solid =
      projected ? stage_collision_floor_line_is_runtime_fighter_solid(stage_id, projected_segment)
                : 0u;
  if (!projected_fighter_solid &&
      msl_mplib_project_fighter_floor(batch, bi, coll->floor_id, coll->cur_x, bottom_y,
                                      &projection)) {
    // Frozen Stadium retains inactive transformation lines in raw MapLine topology, but the
    // generated active fighter-floor graph connects the carried lip to the main floor selected by
    // the frozen-stage collision policy. The ordinary grounded projector consumes that active
    // graph rather than treating the inactive raw neighbor as a teeter endpoint.
    // data/stages/bin/grps.bin::MSLSTG01 fighter_solid + floor graph links
    // refs/slippi-ssbm-asm/Online/Core/Hacks/Stadium/IngameCheckIfFrozen.asm
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800488F4,mpColl_8004A908_Floor}
    projected = 1u;
    projected_segment = projection.line_id;
    projected_fighter_solid = 1u;
  }
  if (projected && projected_fighter_solid) {
    // mpColl_800488F4 applies mpLib_8004DD90_Floor's signed correction directly. Grounded mode-5
    // ECB loading fixes bottom.y at zero, so this is root-to-floor projection rather than an
    // airborne bottom-sweep admission heuristic.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800488F4,mpColl_8004B108}
    coll->cur_y += projection.correction_y;
    mpcoll_ecb_world_points_from_rel(
        &coll->ecb, coll->cur_x, coll->cur_y, coll->ecb.bottom_rel_y, coll->ecb.top_rel_y,
        coll->ecb.left_rel_x, coll->ecb.right_rel_x, coll->ecb.side_rel_y, coll->ecb.frame_u16);
    coll->floor_id = projected_segment;
    coll->touching_floor = 1u;
    coll->env_flags |= (uint32_t)MSL_COLLIDE_FLOOR_PUSH;
    batch->state.ground_contact_x[idx] = coll->cur_x;
    batch->state.ground_contact_y[idx] = projection.contact_y - 0.0001f;
    batch->state.ground_normal_x[idx] = projection.normal_x;
    batch->state.ground_normal_y[idx] = projection.normal_y;
    return 1u;
  }
  if (stage_collision_map_line(stage_id, coll->floor_id) != NULL) {
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
    int16_t left_adjacent = -1;
    int16_t right_adjacent = -1;
    if (msl_mplib_floor_endpoint(batch, bi, coll->floor_id, -1, &left_x, &left_y, &left_adjacent) &&
        msl_mplib_floor_endpoint(batch, bi, coll->floor_id, +1, &right_x, &right_y,
                                 &right_adjacent)) {
      uint8_t wall_join = 0u;
      if (coll->cur_x < left_x) {
        MslStageRawLineKind adjacent_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
        wall_join = (uint8_t)(left_adjacent >= 0 &&
                              stage_collision_raw_line_kind(stage_id, (uint16_t)left_adjacent,
                                                            &adjacent_kind) &&
                              adjacent_kind == MSL_STAGE_RAW_LINE_RIGHT_WALL);
        if (!wall_join) {
          coll->env_flags |= (uint32_t)MSL_COLLIDE_LEFT_LEDGE_SLIP;
        }
      } else if (coll->cur_x > right_x) {
        MslStageRawLineKind adjacent_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
        wall_join = (uint8_t)(right_adjacent >= 0 &&
                              stage_collision_raw_line_kind(stage_id, (uint16_t)right_adjacent,
                                                            &adjacent_kind) &&
                              adjacent_kind == MSL_STAGE_RAW_LINE_LEFT_WALL);
        if (!wall_join) {
          coll->env_flags |= (uint32_t)MSL_COLLIDE_RIGHT_LEDGE_SLIP;
        }
      }
      if (wall_join) {
        // A carried floor terminating in the corresponding wall remains grounded at the endpoint;
        // a bare endpoint publishes the ledge-slip bit and proceeds through the wrapper recipe.
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_800488F4
        coll->cur_x = coll->cur_x < left_x ? left_x : right_x;
        coll->cur_y = coll->cur_x == left_x ? left_y : right_y;
        mpcoll_ecb_world_points_from_rel(
            &coll->ecb, coll->cur_x, coll->cur_y, coll->ecb.bottom_rel_y, coll->ecb.top_rel_y,
            coll->ecb.left_rel_x, coll->ecb.right_rel_x, coll->ecb.side_rel_y, coll->ecb.frame_u16);
        coll->touching_floor = 1u;
        coll->env_flags |= (uint32_t)MSL_COLLIDE_FLOOR_PUSH;
        return 1u;
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
  if (action_id == (uint16_t)MSL_ACT_MISS_FOOT) {
    // ftCo_8009F39C enters MissFoot outside the Damage family: vertical knockback and the public
    // hitstun/in-damage flags are cleared at the same callback boundary.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_8009F39C
    batch->state.speed_y_attack[idx] = 0.0f;
    batch->state.hitstun[idx] = 0u;
    const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
    batch->state.state_flags[flags_i] &=
        (uint8_t) ~(uint8_t)(MSL_STATE_FLAG_221C_IS_HITSTUN | MSL_STATE_FLAG_221C_IN_DAMAGE);
  }
}

static void transfer_ground_to_air_keep_action(MslBatch* batch, size_t idx) {
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return;
  }
  // ftCommon_8007D5D4 changes the ground/air owner without changing MotionState. Damage_Coll's
  // ftCo_8008FC94 continuation uses this path after a non-facing floor loss.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008FC94
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  batch->state.ecb_lock_timer[idx] = MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR;
  batch->state.coll_desired_ecb_bottom_locked_owner[idx] = 0u;
  batch->state.fall_fast[idx] = 0u;
  batch->state.jumps_left[idx] = ch->max_jumps > 0u ? (uint8_t)(ch->max_jumps - 1u) : 0u;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.on_ground[idx] = 0u;
  batch->state.ground_id[idx] = 0xFFFFu;
  if (batch->state.speed_air_x_self[idx] > ch->air_drift_max) {
    batch->state.speed_air_x_self[idx] = ch->air_drift_max;
  } else if (batch->state.speed_air_x_self[idx] < -ch->air_drift_max) {
    batch->state.speed_air_x_self[idx] = -ch->air_drift_max;
  }
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

static void run_handler_transition(MslBatch* batch, int bi, int p, uint8_t installed_handler,
                                   uint8_t handler_kind, const MslSourceGroundCollData* coll) {
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
    if (installed_handler == (uint8_t)MSL_COLL_HANDLER_DAMAGE_COMMON) {
      // ft_800848DC checks the facing ledge-slip flag before Damage_Coll's ordinary
      // ftCo_8008FC94 continuation. MissFoot also clears vertical knockback on entry.
      // refs/melee/src/melee/ft/ft_081B.c::ft_800848DC
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_8008FC94}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_8009F39C
      if ((coll->env_flags & facing_slip) != 0u) {
        batch->state.speed_y_attack[idx] = 0.0f;
        enter_air_state(batch, bi, p, (uint16_t)MSL_ACT_MISS_FOOT, (uint32_t)MSL_SM_MISS_FOOT);
      } else {
        transfer_ground_to_air_keep_action(batch, idx);
      }
      return;
    }
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
}

// Character Coll callbacks consume the shared mpColl floor-loss result immediately, then perform
// their source-specific ground/air motion swap. Keep the geometry shared and dispatch only the
// actual callback continuation here.
// refs/melee/src/melee/ft/chara/{ftCaptain,ftMars,ftFox,ftSeak,ftZelda}::*_Coll
static uint8_t run_special_floor_loss_transition(MslBatch* batch, size_t idx,
                                                 uint16_t source_action) {
  if (msl_action_capture_high_from_low(source_action) != 0u) {
    const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
    const int p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
    const uint8_t owner = batch->state.grab_owner_port[idx];
    if (owner != 0xFFu && owner < batch->config.num_players && owner != (uint8_t)p) {
      // CapturePulled/Wait/Damage Lw all use the constrained 4B108 wrapper, then enter the paired
      // Hi state and republish fn_800DAA40's live anchor when the floor is lost.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
      //   ftCo_CapturePulledLw_Coll,fn_800DB230_inline,ftCo_CaptureWaitLw_Coll,
      //   fn_800DBED4_inline,ftCo_CaptureDamageLw_Coll,fn_800DC624_inline}
      return grab_attachment_capture_low_to_high_now(batch, bi, p, (int)owner);
    }
  }
  const uint8_t fx_kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[idx], source_action);
  if (fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD ||
      fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI) {
    // Grounded Firefox/Firebird hold and travel callbacks run 4B108, then D60C and the paired
    // aerial motion entry when the carried floor is lost. Preserve the live animation frame; the
    // travel entry also preserves its active hit state.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    //   ftFx_SpecialHiHold_Coll,ftFx_SpecialHiHold_GroundToAir,
    //   ftFx_SpecialHi_Coll,ftFx_SpecialHi_GroundToAir}
    const uint8_t air_kind = fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD
                                 ? (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD_AIR
                                 : (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI;
    const uint16_t next = spacie_fx_kind_action(batch->state.char_id[idx], air_kind);
    if (next != 0u) {
      const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
      const int p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
      const float frame = batch->state.anim_frame_f32[idx];
      batch->state.on_ground[idx] = 0u;
      batch->state.ground_id[idx] = 0xFFFFu;
      batch->state.speed_ground_x_self[idx] = 0.0f;
      batch->state.jumps_left[idx] = 0u;
      msl_ftcommon_lock_ecb_8007d60c(batch, idx);
      motion_state_change(batch, bi, p, next,
                          (uint32_t)msl_motion_state_submotion_id(batch->state.char_id[idx], next),
                          (uint32_t)MSL_MOTION_ENTRY_SKIP_HIT, frame, 1.0f,
                          MSL_ANIM_ENTER_TICK_NONE);
      return 1u;
    }
  }
  if (fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI_LANDING) {
    // The grounded end callback enters the aerial fall state directly when 4B108 loses support.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    //   ftFx_SpecialHiLanding_Coll,ftFx_SpecialHiLanding_GroundToAir}
    const uint16_t next =
        spacie_fx_kind_action(batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL);
    if (next != 0u) {
      const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
      const int p = (int)(idx % (size_t)MSL_MAX_PLAYERS);
      batch->state.on_ground[idx] = 0u;
      batch->state.ground_id[idx] = 0xFFFFu;
      motion_state_change(batch, bi, p, next,
                          (uint32_t)msl_motion_state_submotion_id(batch->state.char_id[idx], next),
                          0u, 0.0f, 1.0f, MSL_ANIM_ENTER_TICK_NONE);
      return 1u;
    }
  }
  if (fx_kind >= (uint8_t)MSL_FX_KIND_SPECIAL_LW_START &&
      fx_kind <= (uint8_t)MSL_FX_KIND_SPECIAL_LW_TURN) {
    // Every grounded Shine collision callback runs ftCommon_8007D5D4 before its paired
    // GroundToAir motion-state change. Publish GA_Air here; shine_update_post_collision consumes
    // the generated ground/air kind pairing and preserves the live animation frame.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    //   ftFx_SpecialLwStart_GroundToAir,ftFx_SpecialLwLoop_GroundToAir,
    //   ftFx_SpecialLwHit_GroundToAir,ftFx_SpecialLwEnd_GroundToAir,
    //   ftFx_SpecialLwTurn_GroundToAir}
    batch->state.on_ground[idx] = 0u;
    return 1u;
  }
  if (batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_MARTH &&
      marth_special_try_ground_to_air_swap(batch, idx)) {
    batch->state.on_ground[idx] = 0u;
    batch->state.ground_id[idx] = 0xFFFFu;
    batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
    return 1u;
  }
  if (batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_FALCON &&
      falcon_special_try_ground_to_air_swap(batch, idx)) {
    batch->state.on_ground[idx] = 0u;
    batch->state.ground_id[idx] = 0xFFFFu;
    if (!(source_action == (uint16_t)MSL_ACT_CA_SPECIAL_S_START ||
          source_action == (uint16_t)MSL_ACT_CA_SPECIAL_S ||
          source_action == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_LW_END)) {
      const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
      batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
      batch->state.speed_ground_x_self[idx] = 0.0f;
      batch->state.jumps_left[idx] =
          ch != NULL && ch->max_jumps > 0 ? (uint8_t)(ch->max_jumps - 1) : 0u;
    }
    return 1u;
  }
  if ((batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_SHEIK ||
       batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_ZELDA) &&
      sheik_special_try_ground_to_air_swap(batch, idx)) {
    batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
    batch->state.speed_ground_x_self[idx] = 0.0f;
    batch->state.on_ground[idx] = 0u;
    batch->state.ground_id[idx] = 0xFFFFu;
    batch->state.speed_y_self[idx] = 0.0f;
    return 1u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  const MslSpecialMsids* ms = msl_special_msids(batch->state.char_id[idx]);
  if (spacie_side_special_ground_floor_loss_to_air(batch, ms, ch, idx, source_action)) {
    batch->state.on_ground[idx] = 0u;
    batch->state.ground_id[idx] = 0xFFFFu;
    return 1u;
  }
  return 0u;
}

static void source_ground_callback(MslBatch* batch, int bi, int p, uint8_t handler_kind) {
  const size_t idx = msl_idx_player(bi, p);
  const uint16_t source_action = batch->state.action_id[idx];
  const uint8_t installed_handler = handler_kind;
  const uint8_t selector = batch->state.live_coll_wrapper_selector_kind[idx];
  const uint32_t plan = batch->state.live_coll_source_plan[idx];
  handler_kind = msl_coll_handler_source_ground_mode(handler_kind);
  if (handler_kind == (uint8_t)MSL_COLL_HANDLER_NONE) {
    if (msl_coll_handler_is_landing(installed_handler)) {
      handler_kind = (uint8_t)MSL_COLL_HANDLER_GROUND_B4B0_TEETER;
    } else if (installed_handler == (uint8_t)MSL_COLL_HANDLER_DOWN_BOUND ||
               installed_handler == (uint8_t)MSL_COLL_HANDLER_DOWN_B108 ||
               installed_handler == (uint8_t)MSL_COLL_HANDLER_PASSIVE_B108) {
      handler_kind = (uint8_t)MSL_COLL_HANDLER_GROUND_B108_FALL;
    } else if (installed_handler == (uint8_t)MSL_COLL_HANDLER_DOWN_B2DC ||
               installed_handler == (uint8_t)MSL_COLL_HANDLER_PASSIVE_B2DC) {
      handler_kind = (uint8_t)MSL_COLL_HANDLER_GROUND_B2DC_FALL;
    } else if (selector == (uint8_t)MSL_COLL_SELECTOR_FALCON_LW_END) {
      // Falcon Kick End selects 4B2DC only while cmd_vars[1] requests the edge-snap wrapper.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLwEnd_Coll
      handler_kind = batch->state.special_cmd1[idx] ? (uint8_t)MSL_COLL_HANDLER_GROUND_B2DC_FALL
                                                    : (uint8_t)MSL_COLL_HANDLER_GROUND_B108_FALL;
    } else if (selector == (uint8_t)MSL_COLL_SELECTOR_GROUND_B2DC ||
               selector == (uint8_t)MSL_COLL_SELECTOR_GA_CAPTURECUT ||
               selector == (uint8_t)MSL_COLL_SELECTOR_GA_CATCHCUT ||
               selector == (uint8_t)MSL_COLL_SELECTOR_GA_CLIFF_ACTION ||
               selector == (uint8_t)MSL_COLL_SELECTOR_GA_THROW ||
               selector == (uint8_t)MSL_COLL_SELECTOR_GA_B2DC_AIR471 ||
               selector == (uint8_t)MSL_COLL_SELECTOR_MARS_HI ||
               (selector == (uint8_t)MSL_COLL_SELECTOR_FALCON_S_START &&
                batch->state.special_cmd2[idx] == 0u)) {
      handler_kind = (uint8_t)MSL_COLL_HANDLER_GROUND_B2DC_FALL;
    } else {
      handler_kind = (uint8_t)MSL_COLL_HANDLER_GROUND_B108_FALL;
    }
  }
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
  coll.last_x = isfinite(batch->state.coll_last_pos_x[idx]) ? batch->state.coll_last_pos_x[idx]
                                                            : batch->state.prev_pos_x[idx];
  coll.last_y = isfinite(batch->state.coll_last_pos_y[idx]) ? batch->state.coll_last_pos_y[idx]
                                                            : batch->state.prev_pos_y[idx];
  coll.cur_x = batch->state.pos_x[idx];
  coll.cur_y = batch->state.pos_y[idx];
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const MslStageFloorGraph* floor_graph = stage_collision_get_floor_graph(stage_id);
  const int persisted_floor_i = stage_collision_floor_line_index(stage_id, persisted_floor_id);
  const uint16_t runtime_floor_id =
      runtime_carried_floor_id(batch, bi, persisted_floor_id, coll.cur_x, coll.cur_y);
  const int runtime_floor_i = stage_collision_floor_line_index(stage_id, runtime_floor_id);
  coll.floor_id = runtime_floor_id;
  if (floor_graph != NULL && runtime_floor_i >= 0 &&
      (size_t)runtime_floor_i < floor_graph->line_count) {
    float carry_dx = 0.0f;
    float carry_dy = 0.0f;
    if (stage_collision_floor_line_motion_delta(
            batch, bi, &floor_graph->lines[(size_t)runtime_floor_i], &carry_dx, &carry_dy)) {
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
  if (selector == (uint8_t)MSL_COLL_SELECTOR_GA_ENTRY_CUSTOM) {
    MslEcbWorldPoints custom = {0};
    if (match_flow_entry_custom_ecb(batch, idx, &custom)) {
      coll.desired_ecb = custom;
    }
  }
  if (selector == (uint8_t)MSL_COLL_SELECTOR_GROUND_STOPWALL_B5C4) {
    // mpColl_LoadECB_JObj(flags=9) combines the grounded bottom with a +/-1 horizontal envelope.
    // The recipe is extracted from the installed StopWall callback rather than keyed on its action.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B5C4,mpColl_LoadECB_JObj}
    coll.desired_ecb.left_rel_x = -1.0f;
    coll.desired_ecb.right_rel_x = 1.0f;
    mpcoll_ecb_world_points_from_rel(&coll.desired_ecb, coll.cur_x, coll.cur_y,
                                     coll.desired_ecb.bottom_rel_y, coll.desired_ecb.top_rel_y,
                                     coll.desired_ecb.left_rel_x, coll.desired_ecb.right_rel_x,
                                     coll.desired_ecb.side_rel_y, coll.desired_ecb.frame_u16);
  }
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
  MslEcbWorldPoints squeeze_restore_ecb = coll.prev_ecb;
  uint8_t squeezed = mpcoll_state_squeeze_restore_ecb_points(
      batch, idx, &squeeze_restore_ecb, coll.last_x, coll.last_y, msl_ecb_prev_frame_u16(frame));
  if (squeezed) {
    batch->state.coll_squeeze_restore_ecb_valid[idx] = 0u;
  }
  const float dx = coll.cur_x - coll.last_x;
  const float dy = coll.cur_y - coll.last_y;
  float max_delta = fmaxf(fabsf(dx), fabsf(dy));
  const float left_delta = fabsf(coll.desired_ecb.left_rel_x - coll.prev_ecb.left_rel_x);
  if (left_delta > max_delta) {
    max_delta = left_delta;
  }
  const float right_delta = fabsf(coll.desired_ecb.right_rel_x - coll.prev_ecb.right_rel_x);
  if (right_delta > max_delta) {
    max_delta = right_delta;
  }
  const float top_delta = fabsf(coll.desired_ecb.top_rel_y - coll.prev_ecb.top_rel_y);
  if (top_delta > max_delta) {
    max_delta = top_delta;
  }
  const float side_y_delta = fabsf(coll.desired_ecb.side_rel_y - coll.prev_ecb.side_rel_y);
  if (side_y_delta > max_delta) {
    max_delta = side_y_delta;
  }
  // All grounded wrappers use the same mpColl_80043754 subdivision driver as air callbacks: root
  // displacement and every changing ECB extent participate in the six-unit maximum.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
  int steps = max_delta > k_mpcoll_step_extent ? (int)(max_delta / k_mpcoll_step_extent) + 1 : 1;
  if (steps > MSL_SOURCE_GROUND_MAX_SUBSTEPS) {
    steps = MSL_SOURCE_GROUND_MAX_SUBSTEPS;
  }
  const float step_dx = dx / (float)steps;
  const float step_dy = dy / (float)steps;
  coll.cur_x = coll.last_x;
  coll.cur_y = coll.last_y;
  coll.touching_floor = 0u;
  msl_mpcoll_clear_wall_ceiling(batch, idx);
  batch->state.coll_floor_result_valid[idx] = 0u;
  batch->state.coll_floor_result_source[idx] = (uint8_t)MSL_MPCOLL_FLOOR_RESULT_NONE;
  batch->state.coll_floor_result_mode[idx] = (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE;
  coll.ecb = source_prev_ecb;
  MslEcbWorldPoints wall_prev_ecb = source_prev_ecb;
  for (int step = 1; step <= steps; step++) {
    coll.prev_x = coll.cur_x;
    coll.prev_y = coll.cur_y;
    wall_prev_ecb = coll.ecb;
    if (squeezed) {
      coll.ecb = squeeze_restore_ecb;
      squeezed = 0u;
      batch->state.coll_squeeze_restore_ecb_valid[idx] = 0u;
    }
    coll.cur_x += step_dx;
    coll.cur_y += step_dy;
    MslEcbWorldPoints interpolated_ecb = coll.ecb;
    interpolate_ecb(&interpolated_ecb, &coll.ecb, &coll.desired_ecb, coll.cur_x, coll.cur_y,
                    1.0f / (float)(steps - step + 1));
    coll.ecb = interpolated_ecb;
    batch->state.pos_x[idx] = coll.cur_x;
    batch->state.pos_y[idx] = coll.cur_y;
    batch->state.ground_id[idx] = coll.floor_id;
    uint8_t floor_result = 0u;
    uint8_t stop_substeps = 0u;
    uint8_t previous_squeezed = 0u;
    uint8_t repeat = 0u;
    do {
      MslMpCollFrame contact = {
          .prev_x = coll.prev_x,
          .prev_y = coll.prev_y,
          .cur_x = coll.cur_x,
          .cur_y = coll.cur_y,
          .prev_ecb = wall_prev_ecb,
          .ecb = coll.ecb,
          .desired_ecb = coll.desired_ecb,
          .prev_env_flags = batch->state.coll_prev_env_flags[idx],
          .env_flags = coll.env_flags,
          .left_wall_id = 0xFFFFu,
          .right_wall_id = 0xFFFFu,
          .floor_id = coll.floor_id,
          .grounded = 1u,
          .squeezed = squeezed,
      };
      uint8_t ceiling_hit = msl_mpcoll_resolve_walls_ceiling(batch, bi, idx, &contact);
      coll.ecb = contact.ecb;
      coll.desired_ecb = contact.desired_ecb;
      coll.cur_x = contact.cur_x;
      coll.cur_y = contact.cur_y;
      coll.env_flags = contact.env_flags;
      coll.touching_floor = 0u;
      floor_result = resolve_floor(batch, bi, idx, &coll, handler_kind);
      // Ordinary 488F4 carried-floor projection does not set b5. Edge/fallback floor handlers do;
      // otherwise fast grounded movement would stop after its first subdivision on every frame.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
      contact.outer_stop = floor_result >= 2u ? 1u : contact.outer_stop;
      stop_substeps = 0u;
      if (floor_result == 0u && persisted_floor_i >= 0) {
        // 4ACE4 reaches the ordinary 46904 search only from its valid carried-floor branch. An
        // invalid floor index skips directly to the final disconnected-floor 4A908 retry below.
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
        contact.cur_x = coll.cur_x;
        contact.cur_y = coll.cur_y;
        contact.ecb = coll.ecb;
        contact.env_flags = coll.env_flags;
        const MslMpCollAirStepResult air_result =
            msl_mpcoll_resolve_air_step(batch, bi, idx, &contact, coll.floor_id, 0u, 0u, 0u);
        coll.cur_x = contact.cur_x;
        coll.cur_y = contact.cur_y;
        coll.ecb = contact.ecb;
        coll.desired_ecb = contact.desired_ecb;
        coll.env_flags = contact.env_flags;
        if (air_result.floor_contact) {
          coll.floor_id = air_result.floor_id;
          coll.touching_floor = air_result.touched_floor;
          floor_result = 3u;
        }
        // mpColl_8004ACE4 sets x34_flags.b5 after the carried-floor miss and alternate-floor
        // search, irrespective of whether mpColl_80046904 finds a replacement. The shared
        // subdivision driver therefore stops at this sample instead of continuing the remaining
        // root displacement beyond the ledge.
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_8004ACE4}
        stop_substeps = 1u;
        contact.outer_stop = 1u;
      }
      if (floor_result != 0u) {
        const uint8_t result_mode = floor_result == 2u ? (uint8_t)MSL_MPCOLL_FLOOR_MODE_EDGE_SNAP
                                    : floor_result == 3u
                                        ? (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP
                                        : (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION;
        record_floor_result(batch, idx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, result_mode,
                            coll.floor_id, batch->state.ground_contact_x[idx],
                            batch->state.ground_contact_y[idx], batch->state.ground_normal_x[idx],
                            batch->state.ground_normal_y[idx]);
        batch->state.pos_x[idx] = coll.cur_x;
        batch->state.pos_y[idx] = coll.cur_y;
        contact.cur_x = coll.cur_x;
        contact.cur_y = coll.cur_y;
        contact.ecb = coll.ecb;
        contact.desired_ecb = coll.desired_ecb;
        contact.env_flags = coll.env_flags;
        if (floor_result != 3u) {
          const float y_after_floor = contact.cur_y;
          ceiling_hit = msl_mpcoll_resolve_ceiling(batch, bi, idx, &contact);
          if (ceiling_hit) {
            msl_mpcoll_squeeze_vertical(batch, idx, &contact, 0u, contact.cur_y, y_after_floor);
          }
        }
        coll.cur_x = contact.cur_x;
        coll.cur_y = contact.cur_y;
        coll.ecb = contact.ecb;
        coll.desired_ecb = contact.desired_ecb;
        coll.env_flags = contact.env_flags;
      }
      squeezed = contact.squeezed;
      if (squeezed) {
        (void)mpcoll_state_squeeze_restore_ecb_points(batch, idx, &squeeze_restore_ecb, coll.cur_x,
                                                      coll.cur_y, coll.ecb.frame_u16);
      }
      repeat = (uint8_t)(previous_squeezed != squeezed);
      previous_squeezed = squeezed;
      stop_substeps = contact.outer_stop;
    } while (repeat);
    batch->state.coll_geometry_generation[idx] =
        batch->state.stage_collision_geometry_generation[(size_t)bi];
    if (stop_substeps) {
      break;
    }
  }
  if (!coll.touching_floor && floor_graph != NULL && persisted_floor_i >= 0 &&
      stage_collision_floor_line_stage_object_support_kind(stage_id, persisted_floor_id) ==
          (uint8_t)MSL_STAGE_OBJECT_SUPPORT_YOSHI_SHYGUY &&
      !stage_collision_floor_line_is_runtime_fighter_solid(stage_id, persisted_floor_id) &&
      batch->state.stage_yoshi_shyguy_valid != NULL &&
      batch->state.stage_yoshi_shyguy_valid[bi] != 0u) {
    // grStory's Shy Guy item owns the carried CollData floor; the raw map line is deliberately not
    // selectable as new static fighter terrain.
    // refs/melee/src/melee/gr/grstory.c::grStory_801E3418
    // refs/melee/src/melee/it/items/itheiho.c
    // data/stages/bin/grst.bin::MSLSTG01 stage_object_support_kind
    coll.floor_id = persisted_floor_id;
    coll.touching_floor = 1u;
    coll.env_flags |= (uint32_t)MSL_COLLIDE_FLOOR_PUSH;
    record_floor_result(batch, idx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                        (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAGE_OBJECT_CARRY, coll.floor_id,
                        coll.cur_x, coll.cur_y, batch->state.ground_normal_x[idx],
                        batch->state.ground_normal_y[idx]);
  }
  if (floor_graph != NULL) {
    const float prev_bottom_x = coll.last_x;
    const float prev_bottom_y = coll.last_y + source_prev_ecb.bottom_rel_y;
    const float prev_side_mid_y =
        coll.last_y + 0.5f * (source_prev_ecb.top_rel_y + source_prev_ecb.bottom_rel_y);
    const float cur_bottom_x = coll.cur_x;
    const float cur_bottom_y = coll.cur_y + coll.ecb.bottom_rel_y;
    const float retry_start_y[2] = {prev_bottom_y, prev_side_mid_y};
    for (size_t pass = 0; pass < 2u; pass++) {
      MslStageQueryHit retry = {0};
      if (!msl_mplib_sweep_floor(batch, bi, idx, prev_bottom_x, retry_start_y[pass], cur_bottom_x,
                                 cur_bottom_y, batch->state.floor_skip_segment_id[idx], &retry) ||
          stage_collision_map_lines_connected(stage_id, persisted_floor_id, retry.segment_i)) {
        continue;
      }
      MslMpLibFloorProjection projection = {0};
      if (!msl_mplib_project_floor(batch, bi, retry.segment_i, cur_bottom_x, cur_bottom_y,
                                   &projection)) {
        continue;
      }
      // mpColl_8004A908_Floor retries the disconnected-floor bottom sweep, then immediately runs
      // the ordinary 44838 projection on the accepted MapLine.
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004A908_Floor,mpColl_80044838_Floor}
      coll.cur_y += projection.correction_y;
      coll.floor_id = projection.line_id;
      coll.touching_floor = 1u;
      coll.env_flags |= (uint32_t)MSL_COLLIDE_FLOOR_PUSH;
      batch->state.ground_contact_x[idx] = projection.contact_x;
      batch->state.ground_contact_y[idx] = projection.contact_y;
      batch->state.ground_normal_x[idx] = projection.normal_x;
      batch->state.ground_normal_y[idx] = projection.normal_y;
      record_floor_result(batch, idx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY,
                          (uint8_t)MSL_MPCOLL_FLOOR_MODE_4A908_RETRY, projection.line_id,
                          projection.contact_x, projection.contact_y, projection.normal_x,
                          projection.normal_y);
      break;
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
  if (runtime_floor_id != persisted_floor_id && coll.floor_id == runtime_floor_id) {
    // Keep the generated Randall id internal; CollData/public compare retains the source line id
    // shared by Yoshi stage-object floors.
    coll.floor_id = persisted_floor_id;
    if (batch->state.coll_floor_result_valid[idx] != 0u &&
        batch->state.coll_floor_result_segment_id[idx] == runtime_floor_id) {
      batch->state.coll_floor_result_segment_id[idx] = persisted_floor_id;
    }
  }
  coll.env_flags |= batch->state.coll_env_flags[idx] &
                    (uint32_t)(MSL_COLLIDE_LEFT_WALL_MASK | MSL_COLLIDE_RIGHT_WALL_MASK |
                               MSL_COLLIDE_CEILING_MASK);
  batch->state.coll_prev_env_flags[idx] = coll.prev_env_flags;
  batch->state.coll_env_flags[idx] = coll.env_flags;
  batch->state.pos_x[idx] = coll.cur_x;
  batch->state.pos_y[idx] = coll.cur_y;
  // The map callback owns CollData contact, not Fighter.ground_or_air. Grounded callers retain
  // GA_Ground while contact remains; callers that entered with GA_Air (notably same-frame
  // DownBound callbacks) retain GA_Air. Only the installed action callback changes that public
  // lane when floor loss causes a state transition.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
  batch->state.ground_id[idx] = coll.touching_floor ? coll.floor_id : persisted_floor_id;
  // CollData.cur_pos is the next callback's mpCollPrev source endpoint.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
  // refs/melee/src/melee/mp/mpcoll.c::mpCollPrev
  batch->state.coll_last_pos_x[idx] = coll.cur_x;
  batch->state.coll_last_pos_y[idx] = coll.cur_y;
  batch->state.coll_substep_prev_pos_x[idx] = coll.prev_x;
  batch->state.coll_substep_prev_pos_y[idx] = coll.prev_y;
  batch->state.coll_substep_cur_pos_x[idx] = coll.cur_x;
  batch->state.coll_substep_cur_pos_y[idx] = coll.cur_y;
  mpcoll_store_prev_ecb_points(batch, idx, &wall_prev_ecb);
  mpcoll_store_current_ecb_points(batch, idx, &coll.ecb);
  mpcoll_store_desired_ecb_points(batch, idx, &coll.desired_ecb);
  batch->state.coll_prev_ecb_bottom_valid[idx] = 1u;
  batch->state.coll_ecb_bottom_valid[idx] = 1u;
  batch->state.coll_desired_ecb_bottom_valid[idx] = 1u;
  batch->state.coll_geometry_generation[idx] =
      batch->state.stage_collision_geometry_generation[(size_t)bi];
  const uint8_t special_transition =
      coll.touching_floor ? 0u : run_special_floor_loss_transition(batch, idx, source_action);
  uint8_t selector_transition = 0u;
  if (!coll.touching_floor && !special_transition) {
    if (selector == (uint8_t)MSL_COLL_SELECTOR_GA_CAPTURECUT ||
        selector == (uint8_t)MSL_COLL_SELECTOR_GA_ENTRY_CUSTOM) {
      // These callbacks retain their MotionState and invoke ftCommon_8007D5D4 on floor loss.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::fn_800DC8D8
      // refs/melee/src/melee/ft/ft_0C31.c::fn_800C63E0
      combat_apply_ftCommon_8007D5D4_ground_to_air(batch, idx);
      selector_transition = 1u;
    } else if (selector == (uint8_t)MSL_COLL_SELECTOR_GA_THROW) {
      throw_flow_ground_loss_release(batch, bi, p);
      selector_transition = 1u;
    }
  }
  if (!special_transition && !selector_transition &&
      (msl_coll_handler_is_source_ground(installed_handler) ||
       msl_coll_handler_is_landing(installed_handler) ||
       msl_coll_source_plan_has(plan, MSL_COLL_SOURCE_FLOOR_LOSS_TO_FALL) ||
       msl_coll_source_plan_has(plan, MSL_COLL_SOURCE_CATCH_START_FLOOR_LOSS) ||
       installed_handler == (uint8_t)MSL_COLL_HANDLER_DAMAGE_COMMON ||
       (installed_handler >= (uint8_t)MSL_COLL_HANDLER_DOWN_BOUND &&
        installed_handler <= (uint8_t)MSL_COLL_HANDLER_PASSIVE_B2DC))) {
    run_handler_transition(batch, bi, p, installed_handler, handler_kind, &coll);
  }
  knockdown_update_post_collision_one(batch, bi, p);
}

uint8_t mpcoll_source_ground_run_installed_callback(MslBatch* batch, int bi, int p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players) {
    return 0u;
  }
  const size_t idx = msl_idx_player(bi, p);
  const uint8_t handler = batch->state.live_coll_handler_kind[idx];
  const uint8_t selector = batch->state.live_coll_wrapper_selector_kind[idx];
  const uint8_t fx_kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[idx], batch->state.action_id[idx]);
  const uint8_t ground_shine_callback =
      (uint8_t)(fx_kind >= (uint8_t)MSL_FX_KIND_SPECIAL_LW_START &&
                fx_kind <= (uint8_t)MSL_FX_KIND_SPECIAL_LW_TURN);
  if (!selector_has_ground_wrapper(selector) && !msl_coll_handler_is_source_ground(handler) &&
      !msl_coll_handler_is_landing(handler) && !ground_shine_callback) {
    return 0u;
  }
  if (selector_resolves_ground_from_live_ga(selector) && batch->state.on_ground[idx] == 0u) {
    // These installed callbacks choose their low-level wrapper from live ground_or_air. Let the
    // air kernel consume the same callback instead of claiming it here.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowF_Coll,
    //   ftCo_ThrowB_Coll,ftCo_ThrowHi_Coll,ftCo_ThrowLw_Coll}
    return 0u;
  }
  if (selector == (uint8_t)MSL_COLL_SELECTOR_GROUND_B108_CONSTRAINED &&
      batch->state.grab_constraint_x2226_b2[idx] != 0u) {
    // CapturePulled/Wait/Damage Lw suppress their installed 4B108 callback while constrained.
    // data/motion_state/owners/*.bin::MSLMSO01 coll_source_plan
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    //   ftCo_CapturePulledLw_Coll,ftCo_CaptureWaitLw_Coll,ftCo_CaptureDamageLw_Coll}
    return 0u;
  }
  batch->state.live_coll_callback_ran[idx] = 1u;
  source_ground_callback(batch, bi, p, handler);
  return 1u;
}

void mpcoll_source_ground_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.live_coll_callback_ran[idx] = 0u;
      (void)mpcoll_source_ground_run_installed_callback(batch, bi, p);
    }
  }
}
