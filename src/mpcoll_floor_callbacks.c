#include "mpcoll_floor.h"
#include "mpcoll_ground.h"

#include <float.h>
#include <math.h>

#include "action_ids.h"
#include "batch_internal.h"
#include "buttons.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "escapeair_collision_owner.h"
#include "input_axis.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "mpcoll_ecb_pose.h"
#include "mpcoll_floor_skip.h"
#include "mpcoll_wall_ceil.h"
#include "sheik_specials.h"
#include "stage_collision.h"
#include "state_flags.h"

// Decomp constants shared with the floor owner and remaining coordinator code.
static const float k_floor_x_end_clamp = 0.1f;
static const float k_floor_y_bias = 0.0001f;
static const float k_mpcoll_substep_max_delta = 6.0f;
static const float k_ecb_vertical_unit = 1.0f;
// refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
static const float k_floor_edge_wall_probe_x_offset = 1.0f;
static const float k_floor_edge_wall_probe_y_offset = 1.0f;

uint8_t msl_mpcoll_80044838_floor_edge_snap_from_bottom(
    MslBatch* batch, int bi, const MslStageFloorGraph* g, int line_idx, float cur_bottom_x,
    float cur_bottom_y, uint8_t allow_hard_floor, uint16_t* ground_id_out, float* contact_x_out,
    float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      ground_id_out == NULL) {
    return 0u;
  }
  if (!allow_hard_floor && !g->lines[(size_t)line_idx].is_platform) {
    // Retained owner slice: this helper owns platform endpoint admission only. Hard-floor off-end
    // rows stay on the ordinary bottom-sweep/direct publication path because their source edge-snap
    // path needs current mpColl scratch floor ownership, not this platform endpoint helper.
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  float edge_x = l.x0;
  float edge_y = l.y0;
  // Source shape:
  // mpColl_80044838_Floor falls back to the current floor's left endpoint, then switches to the
  // right endpoint when `left.x <= bottom.x`. It places cur_pos so ECB bottom lands exactly on that
  // endpoint and refreshes floor.index through mpLib_8004DD90_Floor.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044838_Floor
  if (edge_x <= cur_bottom_x) {
    edge_x = l.x1;
    edge_y = l.y1;
  }
  int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, line_idx, edge_x, edge_y, NULL,
                                              floor_nx_out, floor_ny_out);
  if (out_line_idx < 0) {
    out_line_idx = line_idx;
  }
  *ground_id_out = g->lines[(size_t)out_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = edge_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = edge_y;
  }
  (void)cur_bottom_y;
  return 1u;
}

uint8_t msl_mpcoll_80044628_floor_wall_adjacent_fallback(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslMpcollOrderedWallCeilResult* wall_ceil, float cur_bottom_x, float cur_bottom_y,
    uint16_t skip_platform_segment_i, uint16_t* ground_id_out, float* contact_x_out,
    float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || wall_ceil == NULL || ground_id_out == NULL) {
    return 0u;
  }
  // Source side-floor fallback:
  // mpColl_80044628_Floor first tries mpCheckFloorRemap. If no floor was hit, the same inline2
  // wall pass can provide left/right wall bits; source then walks the raw MapLine graph with
  // mpLinePrevNonLeftWall / mpLineNextNonRightWall and projects the current bottom onto that floor.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_8004ACE4}
  // refs/melee/src/melee/mp/mplib.c::{
  //   mpLinePrevNonLeftWall,mpLineNextNonRightWall,mpLib_8004DD90_Floor}
  const uint8_t left_wall_hit = (wall_ceil->left_wall.hit != 0u &&
                                 wall_ceil->left_wall.mode != (uint8_t)MSL_MPCOLL_WALL_RESULT_NONE)
                                    ? 1u
                                    : (uint8_t)(wall_ceil->left_right_flags & 1u);
  const uint8_t right_wall_hit =
      (wall_ceil->right_wall.hit != 0u &&
       wall_ceil->right_wall.mode != (uint8_t)MSL_MPCOLL_WALL_RESULT_NONE)
          ? 1u
          : (uint8_t)((wall_ceil->left_right_flags & 2u) ? 1u : 0u);
  const uint8_t side_flags[2] = {left_wall_hit, right_wall_hit};
  const uint16_t wall_segment[2] = {
      (wall_ceil->left_wall.hit != 0u &&
       wall_ceil->left_wall.mode != (uint8_t)MSL_MPCOLL_WALL_RESULT_NONE)
          ? wall_ceil->left_wall.segment_id
          : wall_ceil->left_wall_id,
      (wall_ceil->right_wall.hit != 0u &&
       wall_ceil->right_wall.mode != (uint8_t)MSL_MPCOLL_WALL_RESULT_NONE)
          ? wall_ceil->right_wall.segment_id
          : wall_ceil->right_wall_id,
  };
  const MslStageRawLineKind skip_kind[2] = {MSL_STAGE_RAW_LINE_LEFT_WALL,
                                            MSL_STAGE_RAW_LINE_RIGHT_WALL};
  for (size_t side = 0; side < 2u; side++) {
    if (!side_flags[side] || wall_segment[side] == 0xFFFFu) {
      continue;
    }
    MslStageRawLineKind out_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
    uint16_t floor_segment_i = 0xFFFFu;
    const uint8_t found =
        (side == 0u)
            ? stage_collision_raw_line_prev_non_kind(stage_id, wall_segment[side], skip_kind[side],
                                                     &out_kind, &floor_segment_i)
            : stage_collision_raw_line_next_non_kind(stage_id, wall_segment[side], skip_kind[side],
                                                     &out_kind, &floor_segment_i);
    if (!found || out_kind != MSL_STAGE_RAW_LINE_FLOOR ||
        !stage_collision_floor_line_is_runtime_fighter_solid(stage_id, floor_segment_i)) {
      continue;
    }
    const int line_idx = stage_collision_floor_line_index(stage_id, floor_segment_i);
    if (line_idx < 0 || (size_t)line_idx >= g->line_count ||
        floor_line_is_skipped_platform(stage_id, g, line_idx, skip_platform_segment_i)) {
      continue;
    }
    float y_corr = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    const int projected_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, line_idx, cur_bottom_x,
                                                            cur_bottom_y, &y_corr, &nx, &ny);
    if (projected_line_idx < 0 || !(y_corr > 0.0f)) {
      continue;
    }
    *ground_id_out = g->lines[(size_t)projected_line_idx].segment_i;
    if (contact_x_out != NULL) {
      *contact_x_out = cur_bottom_x;
    }
    if (contact_y_out != NULL) {
      *contact_y_out = cur_bottom_y + y_corr;
    }
    if (floor_nx_out != NULL) {
      *floor_nx_out = nx;
    }
    if (floor_ny_out != NULL) {
      *floor_ny_out = ny;
    }
    batch->state.pos_y[idx] += y_corr;
    return 1u;
  }
  return 0u;
}

uint8_t fallspecial_sloped_ledge_main_floor_first_sustained_airborne_owner(
    const MslBatch* batch, size_t idx, const MslStageFloorGraph* g, uint32_t stage_id) {
  if (batch == NULL || g == NULL) {
    return 0u;
  }
  const int ground_line_idx =
      stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
  // First sustained FallSpecial callback on a flat main floor between sloped ledge floor segments:
  // in the current legal-stage graph this is Yoshi's Story main floor. On the first sustained
  // FallSpecial row after entry, source keeps the row airborne until the following callback even
  // though a simplified current-ECB/root projection can already snap to the main floor. Keep this
  // on the generated line-topology owner instead of delaying FallSpecial floor publication on other
  // legal-stage floor families.
  // data/stages/bin/*.bin::MSLSTG01 floor flags/links/geometry
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
  //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
  return (ground_line_idx >= 0 &&
          stage_collision_floor_line_is_flat_between_sloped_ledges(stage_id,
                                                                   batch->state.ground_id[idx]) &&
          batch->state.action_id[idx] == (uint16_t)MSL_ACT_FALL_SPECIAL &&
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_FALL_SPECIAL &&
          batch->state.seed_prev_action_frame[idx] <= 0)
             ? 1u
             : 0u;
}

uint8_t msl_mpcoll_8004a45c_floor_edge_snap(MslBatch* batch, size_t idx, int bi,
                                            const MslStageFloorGraph* g, uint32_t stage_id,
                                            int line_idx, float cur_bottom_x, uint8_t char_id,
                                            uint32_t anim, uint16_t ecb_frame, uint8_t was_grounded,
                                            uint16_t* ground_id_out, float* contact_x_out,
                                            float* contact_y_out, float* floor_nx_out,
                                            float* floor_ny_out) {
  if (batch == NULL || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      ground_id_out == NULL || contact_x_out == NULL || contact_y_out == NULL) {
    return 0u;
  }
  const MslStageFloorLine world_line = floor_line_world_for_env(batch, bi, g, line_idx);
  const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
  const uint8_t try_left = (cur_bottom_x <= world_line.x0) ? 1u : 0u;
  const uint8_t try_right = (cur_bottom_x >= world_line.x1) ? 1u : 0u;
  if (!try_left && !try_right) {
    return 0u;
  }

  const float edge_x = try_left ? world_line.x0 : world_line.x1;
  const float edge_y = try_left ? world_line.y0 : world_line.y1;
  MslEcbWorldPoints ecb = {0};
  msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, edge_x, edge_y, was_grounded);

  const float probe_ax =
      edge_x + (try_left ? k_floor_edge_wall_probe_x_offset : -k_floor_edge_wall_probe_x_offset);
  const float probe_ay = edge_y + k_floor_edge_wall_probe_y_offset;
  const float probe_bx = edge_x + (try_left ? ecb.right_rel_x : ecb.left_rel_x);
  const float probe_by = edge_y + (ecb.side_rel_y - ecb.bottom_rel_y);
  const MslStageWallGraph* wg = try_left ? stage_collision_get_left_wall_graph(stage_id)
                                         : stage_collision_get_right_wall_graph(stage_id);
  if (wall_blocks_floor_edge_probe(wg, probe_ax, probe_ay, probe_bx, probe_by)) {
    return 0u;
  }

  int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, line_idx, edge_x, edge_y, NULL,
                                              floor_nx_out, floor_ny_out);
  if (out_line_idx < 0) {
    out_line_idx = line_idx;
  }
  *ground_id_out = g->lines[(size_t)out_line_idx].segment_i;
  *contact_x_out = edge_x;
  *contact_y_out = edge_y;
  return 1u;
}

uint8_t msl_mpcoll_8004b108_capture_lw_flat_ledge_carry_owner(const MslBatch* batch, size_t idx,
                                                              int bi, const MslStageFloorGraph* g,
                                                              uint32_t stage_id, uint16_t action_id,
                                                              uint16_t current_ground_id,
                                                              uint16_t carried_ground_id,
                                                              int carried_line_idx) {
  if (batch == NULL || g == NULL || carried_line_idx < 0 ||
      (size_t)carried_line_idx >= g->line_count || current_ground_id == carried_ground_id ||
      !is_capture_lw_allow_ground_to_air_collision_action(action_id) ||
      batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_PASSIVE_STAND_B ||
      batch->state.action_frame[idx] > 2 ||
      g->lines[(size_t)carried_line_idx].segment_i != carried_ground_id ||
      !g->lines[(size_t)carried_line_idx].is_ledge ||
      stage_collision_floor_line_is_sloped(stage_id, carried_ground_id)) {
    return 0u;
  }
  const uint8_t owner_p = batch->state.grab_owner_port[idx];
  if (owner_p >= batch->config.num_players) {
    return 0u;
  }
  const size_t owner_idx = msl_idx_player(bi, (int)owner_p);
  // Source owner:
  // low Capture callbacks route through ft_8008403C/mpColl_8004B108 while attached to the grab
  // owner. Flat ledge floors can retain the carried CollData.floor.index when the owner is already
  // grounded on a different joint-0 floor; sloped/same-floor PulledHi loss stays outside this path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::ft_8008403C
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_8004A908_Floor}
  return (uint8_t)((batch->state.action_id[owner_idx] == (uint16_t)MSL_ACT_CATCH_DASH_PULL &&
                    g->lines[(size_t)carried_line_idx].joint_id == 0 &&
                    batch->state.on_ground[owner_idx] != 0u &&
                    batch->state.ground_id[owner_idx] != carried_ground_id)
                       ? 1u
                       : 0u);
}

uint8_t msl_mpcoll_8004b108_downbound_project_attack_speed(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint8_t was_grounded, uint16_t floor_segment_id, const MslMpcollFloorContact* source_contact) {
  if (batch == NULL || g == NULL || was_grounded != 0u ||
      !action_is_down_bound(batch->state.action_id[idx]) ||
      batch->state.speed_x_attack[idx] == 0.0f ||
      fabsf(batch->state.speed_y_attack[idx]) > 0.000001f) {
    return 0u;
  }

  float floor_nx =
      source_contact != NULL ? source_contact->normal_x : batch->state.ground_normal_x[idx];
  float floor_ny =
      source_contact != NULL ? source_contact->normal_y : batch->state.ground_normal_y[idx];
  if (source_contact == NULL && fabsf(floor_nx) <= 0.000001f &&
      fabsf(floor_ny - 1.0f) <= 0.000001f && floor_segment_id != 0xFFFFu) {
    const int floor_idx = stage_collision_floor_line_index(stage_id, floor_segment_id);
    if (!floor_line_normal_for_env(batch, bi, g, floor_idx, &floor_nx, &floor_ny)) {
      for (size_t line_i = 0; line_i < g->line_count; line_i++) {
        if (g->lines[line_i].segment_i == floor_segment_id &&
            floor_line_normal_for_env(batch, bi, g, (int)line_i, &floor_nx, &floor_ny)) {
          break;
        }
      }
    }
  }

  const float normal_len = sqrtf(floor_nx * floor_nx + floor_ny * floor_ny);
  if (normal_len <= 0.000001f) {
    return 0u;
  }

  floor_nx /= normal_len;
  floor_ny /= normal_len;
  const float ground_kb = batch->state.speed_x_attack[idx];
  const float old_kb_x = batch->state.speed_x_attack[idx];
  const float new_kb_x = floor_ny * ground_kb;
  const float new_kb_y = -floor_nx * ground_kb;
  batch->state.speed_x_attack[idx] = new_kb_x;
  batch->state.speed_y_attack[idx] = new_kb_y;
  batch->state.pos_x[idx] += new_kb_x - old_kb_x;

  float floor_y = 0.0f;
  const int floor_idx = stage_collision_floor_line_index(stage_id, floor_segment_id);
  if (floor_line_y_at_x_for_env(batch, bi, g, floor_idx, batch->state.pos_x[idx], &floor_y)) {
    batch->state.pos_y[idx] = floor_y + k_floor_y_bias;
    return 1u;
  }
  for (size_t line_i = 0; line_i < g->line_count; line_i++) {
    if (g->lines[line_i].segment_i == floor_segment_id &&
        floor_line_y_at_x_for_env(batch, bi, g, (int)line_i, batch->state.pos_x[idx], &floor_y)) {
      batch->state.pos_y[idx] = floor_y + k_floor_y_bias;
      break;
    }
  }

  // DownBound collision projects horizontal damage speed onto the floor tangent after landing, then
  // re-pins the root to the selected floor. Keep the projection in one mpColl_8004B108 owner packet
  // so pre-publication and committed-state callers cannot drift apart.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084F3C,ft_80082708}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  // data/stages/bin/*.bin::MSLSTG01 floor normal metadata
  return 1u;
}

MslMpcoll800471F8EscapeAirPacket msl_mpcoll_800471f8_escapeair_packet(const MslMpcollContext* ctx,
                                                                      uint8_t ecb_lock_active,
                                                                      uint8_t ecb_lock_timer_seed,
                                                                      float fallback_prev_x,
                                                                      float fallback_prev_y) {
  MslMpcoll800471F8EscapeAirPacket packet = {
      .prev_x = fallback_prev_x,
      .prev_y = fallback_prev_y,
      .entry_desired_bottom_owner = 0u,
  };
  if (ctx == NULL || ctx->batch == NULL) {
    return packet;
  }

  const MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  const uint16_t action_id = ctx->action_id;
  const uint16_t prev_action_id = ctx->prev_action_id;
  const uint8_t prev_action_is_jumpaerial = (prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                                             prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B)
                                                ? 1u
                                                : 0u;
  const uint8_t seed_prev_action_is_jumpaerial =
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B)
          ? 1u
          : 0u;
  packet.entry_desired_bottom_owner =
      (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed != 0u &&
       seed_prev_action_is_jumpaerial &&
       (batch->state.pos_y[idx] > k_floor_y_bias ||
        batch->state.floor_sweep_prev_pos_y[idx] > k_floor_y_bias) &&
       batch->state.seed_prev_action_frame[idx] <= 2 &&
       (prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR || batch->state.prev_action_frame[idx] <= 2))
          ? 1u
          : 0u;

  const uint8_t prev_action_is_jump =
      (prev_action_id == (uint16_t)MSL_ACT_JUMP_F || prev_action_id == (uint16_t)MSL_ACT_JUMP_B)
          ? 1u
          : 0u;
  const uint8_t seed_prev_action_is_jump =
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B)
          ? 1u
          : 0u;
  const uint8_t fresh_lr_edge =
      ((batch->state.input_buttons_pressed[idx] & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R)) != 0u)
          ? 1u
          : 0u;
  const uint8_t frame_start_jumpaerial_owner =
      ((((ecb_lock_timer_seed != 0u &&
          (packet.entry_desired_bottom_owner ||
           stage_collision_floor_line_is_platform(ctx->stage_id, batch->state.ground_id[idx]) ||
           stage_collision_floor_line_has_platform_transform(ctx->stage_id,
                                                             batch->state.ground_id[idx])) &&
          prev_action_is_jumpaerial) ||
         (ecb_lock_timer_seed == 0u && batch->state.action_frame[idx] <= 1)) &&
        seed_prev_action_is_jumpaerial))
          ? 1u
          : 0u;
  const uint8_t late_jump_fresh_edge_owner =
      (ecb_lock_timer_seed == 2u && fresh_lr_edge != 0u && prev_action_is_jump &&
       seed_prev_action_is_jump && batch->state.seed_prev_action_frame[idx] == 6 &&
       batch->state.action_frame[idx] <= 1)
          ? 1u
          : 0u;
  const uint8_t use_frame_start_last_pos =
      ((msl_motion_state_class3_has(ctx->char_id, action_id,
                                    MSL_MS_CLASS3_PHASE4_ESCAPE_AIR_COLL) &&
        !ecb_lock_active && ecb_lock_timer_seed == 0u) ||
       (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
        (frame_start_jumpaerial_owner != 0u || late_jump_fresh_edge_owner != 0u)))
          ? 1u
          : 0u;
  if (use_frame_start_last_pos != 0u) {
    packet.prev_x = batch->state.prev_pos_x[idx];
    packet.prev_y = batch->state.prev_pos_y[idx];
  }

  // Source packet: ftCo_EscapeAir_Coll -> ft_80082C74 -> ft_80081D0C publishes a single
  // CollData.last_pos / mpCollPrev root into mpColl_800471F8. Keep the JumpAerial entry,
  // late L/R edge, and class3 phase gates together so the coordinator consumes one previous-root
  // owner instead of rebuilding several local booleans.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754,mpColl_800471F8}
  // refs/melee/src/melee/mp/mplib.c::mpLineIntersectionH
  return packet;
}

static uint8_t msl_mpcoll_800471f8_escapeair_early_locked_floor(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    int prefer_line_idx, uint8_t stage_has_only_static_cardinal_hard_floors,
    uint8_t stage_has_height_platform_transform, float prev_x, float prev_y, float x, float y,
    float escapeair_bottom_rel0, uint16_t skip_platform_segment_i, const MslCommonParams* c,
    MslMpcollFloorHit* out) {
  if (batch == NULL || g == NULL || c == NULL || prefer_line_idx < 0 ||
      (size_t)prefer_line_idx >= g->line_count || out == NULL) {
    return 0u;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR) {
    return 0u;
  }

  // Source owner: ftCo_EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
  //   mpColl_80044838_Floor}
  if (g->lines[(size_t)prefer_line_idx].platform_transform_kind !=
      MSL_STAGE_PLATFORM_TRANSFORM_NONE) {
    MslMpcollFloorSweepResult hard_floor_sweep = {0};
    if (mpcoll_collect_bottom_sweep_hit(
            batch, idx, bi, g, stage_id, prev_x, prev_y + escapeair_bottom_rel0, x,
            y + escapeair_bottom_rel0, skip_platform_segment_i, -1, -1, c, &hard_floor_sweep) &&
        hard_floor_sweep.hit_line_idx >= 0 && !hard_floor_sweep.hit_is_platform &&
        !hard_floor_sweep.hit_has_platform_transform) {
      batch->state.pos_y[idx] = hard_floor_sweep.hit_y + k_floor_y_bias;
      out->result_mode = hard_floor_sweep.mode;
      out->contact.ground_id = hard_floor_sweep.hit_segment_id;
      out->contact.contact_x = hard_floor_sweep.hit_x;
      out->contact.contact_y = hard_floor_sweep.hit_y;
      out->contact.normal_x = hard_floor_sweep.normal_x;
      out->contact.normal_y = hard_floor_sweep.normal_y;
      return 1u;
    }
  }

  int candidate_lines[5];
  candidate_lines[0] = prefer_line_idx;
  candidate_lines[1] = g->lines[(size_t)prefer_line_idx].prev;
  candidate_lines[2] = g->lines[(size_t)prefer_line_idx].next;
  candidate_lines[3] = (stage_has_height_platform_transform && candidate_lines[1] >= 0 &&
                        (size_t)candidate_lines[1] < g->line_count)
                           ? g->lines[(size_t)candidate_lines[1]].prev
                           : -1;
  candidate_lines[4] = (stage_has_height_platform_transform && candidate_lines[2] >= 0 &&
                        (size_t)candidate_lines[2] < g->line_count)
                           ? g->lines[(size_t)candidate_lines[2]].next
                           : -1;
  for (size_t ci = 0; ci < 5u; ci++) {
    const int line_idx = candidate_lines[ci];
    if (line_idx < 0 || (size_t)line_idx >= g->line_count ||
        g->lines[(size_t)line_idx].is_platform ||
        stage_collision_floor_line_has_platform_transform(stage_id,
                                                          g->lines[(size_t)line_idx].segment_i)) {
      continue;
    }
    if (stage_has_height_platform_transform && g->lines[(size_t)line_idx].is_ledge &&
        (!floor_x_within_line_bounds(batch, bi, g, line_idx,
                                     batch->state.floor_sweep_prev_pos_x[idx]) ||
         !floor_x_within_line_bounds(batch, bi, g, line_idx, x))) {
      continue;
    }
    float line_y = 0.0f;
    if (!floor_line_y_at_x_ed5c_for_env(batch, bi, g, line_idx, x, &line_y)) {
      continue;
    }
    const uint8_t early_root_crossing =
        (((msl_escapeair_locked_bottom_owner_is_live_hard_floor(
               batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
           (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR ||
            batch->state.seed_prev_action_frame[idx] <= 0)) ||
          (!msl_escapeair_locked_bottom_owner_any(
               batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
           !stage_has_only_static_cardinal_hard_floors &&
           batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR)) &&
         prev_y > line_y + k_floor_y_bias && y <= line_y + k_floor_y_bias)
            ? 1u
            : 0u;
    const uint8_t bottom_crossing = (msl_escapeair_locked_bottom_owner_any(
                                         batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
                                     prev_y + escapeair_bottom_rel0 > line_y + k_floor_y_bias &&
                                     y + escapeair_bottom_rel0 <= line_y + k_floor_y_bias)
                                        ? 1u
                                        : 0u;
    if (!bottom_crossing && !early_root_crossing) {
      continue;
    }
    batch->state.pos_y[idx] = line_y + k_floor_y_bias;
    out->result_mode = (uint8_t)(bottom_crossing ? MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP
                                                 : MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION);
    out->contact.ground_id = g->lines[(size_t)line_idx].segment_i;
    out->contact.contact_x = x;
    out->contact.contact_y = line_y;
    out->contact.normal_x = 0.0f;
    out->contact.normal_y = 1.0f;
    return 1u;
  }
  return 0u;
}

static uint8_t msl_mpcoll_800471f8_escapeair_owner_zero_root_floor(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    int prefer_line_idx, uint8_t stage_has_height_platform_transform, float prev_y, float x,
    float y, MslMpcollFloorHit* out) {
  if (batch == NULL || g == NULL || prefer_line_idx < 0 ||
      (size_t)prefer_line_idx >= g->line_count || out == NULL) {
    return 0u;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR) {
    return 0u;
  }

  // Same source wrapper as above, but for direct seeds with an active lock and no serialized
  // desired-bottom owner.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
  int candidate_lines[5];
  candidate_lines[0] = prefer_line_idx;
  candidate_lines[1] = g->lines[(size_t)prefer_line_idx].prev;
  candidate_lines[2] = g->lines[(size_t)prefer_line_idx].next;
  candidate_lines[3] = (stage_has_height_platform_transform && candidate_lines[1] >= 0 &&
                        (size_t)candidate_lines[1] < g->line_count)
                           ? g->lines[(size_t)candidate_lines[1]].prev
                           : -1;
  candidate_lines[4] = (stage_has_height_platform_transform && candidate_lines[2] >= 0 &&
                        (size_t)candidate_lines[2] < g->line_count)
                           ? g->lines[(size_t)candidate_lines[2]].next
                           : -1;
  for (size_t ci = 0; ci < 5u; ci++) {
    const int line_idx = candidate_lines[ci];
    if (line_idx < 0 || (size_t)line_idx >= g->line_count ||
        g->lines[(size_t)line_idx].is_platform ||
        stage_collision_floor_line_has_platform_transform(stage_id,
                                                          g->lines[(size_t)line_idx].segment_i)) {
      continue;
    }
    const uint8_t line_is_ledge = g->lines[(size_t)line_idx].is_ledge ? 1u : 0u;
    const uint8_t ledge_span_owner =
        (line_is_ledge && stage_has_height_platform_transform &&
         floor_x_within_line_bounds(batch, bi, g, line_idx,
                                    batch->state.floor_sweep_prev_pos_x[idx]) &&
         floor_x_within_line_bounds(batch, bi, g, line_idx, x))
            ? 1u
            : 0u;
    if ((line_is_ledge && !ledge_span_owner) ||
        (!line_is_ledge && !floor_x_within_line_bounds(batch, bi, g, line_idx, x))) {
      continue;
    }
    float line_y = 0.0f;
    if (!floor_line_y_at_x_for_env(batch, bi, g, line_idx, x, &line_y)) {
      continue;
    }
    if (!(prev_y > line_y + k_floor_y_bias && y <= line_y + k_floor_y_bias)) {
      continue;
    }
    batch->state.pos_y[idx] = line_y + k_floor_y_bias;
    out->result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
    out->contact.ground_id = g->lines[(size_t)line_idx].segment_i;
    out->contact.contact_x = x;
    out->contact.contact_y = line_y;
    out->contact.normal_x = 0.0f;
    out->contact.normal_y = 1.0f;
    return 1u;
  }
  return 0u;
}

uint8_t msl_mpcoll_800471f8_escapeair_entry_floor_publication(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    int prefer_line_idx, uint8_t was_grounded, uint8_t stage_has_only_static_cardinal_hard_floors,
    uint8_t stage_has_height_platform_transform, uint8_t ecb_lock_timer_seed, float prev_x,
    float prev_y, float x, float y, float escapeair_bottom_rel0, uint16_t skip_platform_segment_i,
    const MslCommonParams* c, MslMpcollFloorHit* out) {
  if (batch == NULL || g == NULL || out == NULL || was_grounded != 0u ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR || prefer_line_idx < 0 ||
      ecb_lock_timer_seed == 0u || batch->state.action_frame[idx] > 3 ||
      batch->state.speed_y_self[idx] >= 0.0f) {
    return 0u;
  }
  if ((stage_has_only_static_cardinal_hard_floors || stage_has_height_platform_transform) &&
      (msl_escapeair_locked_bottom_owner_any(
           batch->state.coll_desired_ecb_bottom_locked_owner[idx]) ||
       batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR ||
       batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias) &&
      msl_mpcoll_800471f8_escapeair_early_locked_floor(
          batch, idx, bi, g, stage_id, prefer_line_idx, stage_has_only_static_cardinal_hard_floors,
          stage_has_height_platform_transform, prev_x, prev_y, x, y, escapeair_bottom_rel0,
          skip_platform_segment_i, c, out)) {
    return 1u;
  }
  if (!stage_has_only_static_cardinal_hard_floors &&
      !msl_escapeair_locked_bottom_owner_any(
          batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
      batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      msl_mpcoll_800471f8_escapeair_owner_zero_root_floor(
          batch, idx, bi, g, stage_id, prefer_line_idx, stage_has_height_platform_transform, prev_y,
          x, y, out)) {
    return 1u;
  }

  // Entry floor publication packet for EscapeAir_Coll's ft_80082C74 -> mpColl_800471F8 path.
  // The early locked bottom and owner-zero root cases are alternate source inputs to the same
  // `mpColl_80044628_Floor` / `mpColl_80044838_Floor` publication surface.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
  return 0u;
}

uint8_t msl_mpcoll_80047e14_fallspecial_prephysics_floor_sweep(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_bottom_x, float prev_bottom_y, float cur_bottom_rel_y, int prefer_line_idx,
    uint16_t skip_platform_segment_i, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || ground_id_out == NULL || c == NULL ||
      (prefer_line_idx >= 0 && (size_t)prefer_line_idx >= g->line_count)) {
    return 0u;
  }
  if (!is_common_fallspecial_action(batch->state.action_id[idx])) {
    return 0u;
  }
  (void)cur_bottom_rel_y;
  uint8_t carried_offspan_ledge_owner = 0u;
  if (prefer_line_idx >= 0 && (size_t)prefer_line_idx < g->line_count &&
      batch->state.ground_id[idx] == g->lines[(size_t)prefer_line_idx].segment_i) {
    const MslStageFloorLine* carried_line = &g->lines[(size_t)prefer_line_idx];
    carried_offspan_ledge_owner =
        (uint8_t)(carried_line->is_ledge && !carried_line->is_platform &&
                  floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx) &&
                  !floor_x_within_line_segment_strict(batch, bi, g, prefer_line_idx,
                                                      batch->state.pos_x[idx]) &&
                  mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx));
  }
  if (!carried_offspan_ledge_owner &&
      !(batch->state.prev_pos_y[idx] <= batch->state.floor_sweep_prev_pos_y[idx])) {
    return 0u;
  }

  // FallSpecial_Coll owner:
  // ft_80083090 copies the callback-visible root into CollData.cur_pos, then mpColl_80047E14
  // runs mpColl_80043754. The floor check (`mpColl_80044628_Floor`) must first see the ECB-bottom
  // sweep from the carried CollData position to that callback-visible root. Only then may
  // mpColl_80044838_Floor snap the root (ignore_bottom when ecb.bottom.y > 0).
  //
  // FallSpecial_Phys has already integrated `cur_pos` before FallSpecial_Coll runs. Same-frame
  // EscapeAir_Anim -> FallSpecial handoffs consume that integrated root plus the loaded
  // FallSpecial ECB bottom. Sustained FallSpecial rows normally use the carried CollData/root
  // endpoint; shallow rows whose frame-start root is inside the loaded FallSpecial bottom
  // neighborhood consume the current ECB endpoint from mpColl_LoadECB_inline's interpolation pass.
  // The ECB boundary comes from extracted `data/ecb/*_bottom.bin`, not a character-id branch.
  //
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
  //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80083090
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80047E14,mpColl_80043754,mpColl_80044628_Floor,mpColl_80044838_Floor}
  const uint8_t entered_from_escapeair_anim =
      (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
       batch->state.action_frame[idx] <= 0)
          ? 1u
          : 0u;
  const uint8_t sloped_ledge_main_first_sustained_airborne =
      fallspecial_sloped_ledge_main_floor_first_sustained_airborne_owner(batch, idx, g, stage_id);
  const uint8_t sustained_current_ecb_owner =
      (!entered_from_escapeair_anim && !sloped_ledge_main_first_sustained_airborne &&
       (carried_offspan_ledge_owner || batch->state.fall_fast[idx] == 0u) &&
       batch->state.prev_pos_y[idx] <= (cur_bottom_rel_y + k_ecb_vertical_unit))
          ? 1u
          : 0u;
  const uint8_t use_current_ecb_endpoint =
      (entered_from_escapeair_anim || sustained_current_ecb_owner || carried_offspan_ledge_owner)
          ? 1u
          : 0u;
  const float map_root_x =
      use_current_ecb_endpoint ? batch->state.pos_x[idx] : batch->state.prev_pos_x[idx];
  const float map_root_y =
      use_current_ecb_endpoint ? batch->state.pos_y[idx] : batch->state.prev_pos_y[idx];
  // mpColl_LoadECB_inline(flags=6) loads/interpolates the current FallSpecial ECB before
  // mpColl_80044628_Floor computes `cur_pos + ecb.bottom`.
  const float map_bottom_x = map_root_x;
  const float map_bottom_y =
      use_current_ecb_endpoint ? (map_root_y + cur_bottom_rel_y) : map_root_y;
  if (!(map_bottom_y <= prev_bottom_y)) {
    return 0u;
  }

  if (carried_offspan_ledge_owner) {
    MslMpcollFloorSweepResult hard_floor_sweep = {0};
    if (mpcoll_collect_bottom_sweep_hard_floor_result(batch, idx, bi, g, stage_id, prev_bottom_x,
                                                      prev_bottom_y, map_bottom_x, map_bottom_y,
                                                      prefer_line_idx, -1, 0u, &hard_floor_sweep) &&
        hard_floor_sweep.projected_line_idx >= 0 && hard_floor_sweep.projected_y_corr >= 0.0f) {
      // Off-span carried ledge FallSpecial:
      // `ftCo_FallSpecial_Coll -> ft_80083090 -> mpColl_80047E14` uses the current loaded ECB
      // bottom as the live floor producer. A stale carried ledge floor is not itself authority, but
      // it also must not block a real hard-floor bottom sweep accepted by mpColl_80044628_Floor.
      //
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
      //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,
      //   mpColl_80044838_Floor}
      batch->state.pos_y[idx] += hard_floor_sweep.projected_y_corr;
      *ground_id_out = hard_floor_sweep.projected_segment_id;
      if (contact_x_out != NULL) {
        *contact_x_out = hard_floor_sweep.hit_x;
      }
      if (contact_y_out != NULL) {
        *contact_y_out = hard_floor_sweep.hit_y;
      }
      if (floor_nx_out != NULL) {
        *floor_nx_out = hard_floor_sweep.normal_x;
      }
      if (floor_ny_out != NULL) {
        *floor_ny_out = hard_floor_sweep.normal_y;
      }
      return 1u;
    }
  }

  float nx = 0.0f;
  float ny = 1.0f;
  MslMpcollFloorSweepResult floor_sweep = {0};
  if (!mpcoll_collect_bottom_sweep_hit(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                                       map_bottom_x, map_bottom_y, skip_platform_segment_i,
                                       prefer_line_idx, -1, c, &floor_sweep)) {
    return 0u;
  }
  const int hit_line_idx = floor_sweep.hit_line_idx;
  const float ix = floor_sweep.hit_x;
  const float iy = floor_sweep.hit_y;
  nx = floor_sweep.normal_x;
  ny = floor_sweep.normal_y;
  if (hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_platform) {
    // Static soft platforms do not use this prephysics hard-floor helper. FallSpecial platform
    // publication remains with the normal ftCo_80096CC8-gated floor callback below, including its
    // first-crossing delay and already-below-platform handoff.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    //   ftCo_FallSpecial_Coll,ftCo_80096CC8}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    return 0u;
  }
  if (carried_offspan_ledge_owner &&
      (hit_line_idx < 0 || (size_t)hit_line_idx >= g->line_count ||
       g->lines[(size_t)hit_line_idx].is_ledge ||
       !floor_line_is_runtime_fighter_solid(g, stage_id, hit_line_idx))) {
    return 0u;
  }
  if (hit_line_idx >= 0 &&
      fallspecial_sustained_same_terminal_cardinal_floor_delay(
          batch, idx, bi, g, stage_id, batch->state.action_id[idx], batch->state.ground_id[idx],
          g->lines[(size_t)hit_line_idx].segment_i, hit_line_idx, iy, batch->state.pos_y[idx])) {
    return 0u;
  }
  const float snap_x = map_root_x;
  const float snap_y = map_root_y;
  float y_corr = 0.0f;
  const int out_line_idx =
      msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, snap_x, snap_y, &y_corr, &nx, &ny);
  if (out_line_idx < 0) {
    return 0u;
  }

  const float corrected_y = snap_y + y_corr;
  const float floor_contact_y = corrected_y - k_floor_y_bias;
  batch->state.pos_y[idx] = corrected_y;
  *ground_id_out = g->lines[(size_t)out_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = snap_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = floor_contact_y;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = ny;
  }
  (void)ix;
  (void)iy;
  return 1u;
}

uint8_t msl_mpcoll_80047e14_common_air_hard_floor_bottom_sweep(
    const MslMpcollContext* ctx, const MslBatch* batch, size_t idx, int bi,
    const MslStageFloorGraph* g, uint32_t stage_id, float prev_bottom_x, float prev_bottom_y,
    float cur_bottom_x, float cur_bottom_y, int prefer_line_idx, uint16_t skip_platform_segment_i,
    const MslCommonParams* c, uint16_t* ground_id_out, float* y_corr_out, float* contact_x_out,
    float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || ground_id_out == NULL || y_corr_out == NULL ||
      contact_x_out == NULL || contact_y_out == NULL || floor_nx_out == NULL ||
      floor_ny_out == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  uint8_t fall_fast_same_hard_floor_owner = 0u;
  uint8_t fall_fast_platform_to_hard_floor_owner = 0u;
  uint8_t fall_carried_ledge_to_connected_hard_floor_owner = 0u;
  uint8_t fall_carried_same_ledge_floor_in_span_owner = 0u;
  int fall_carried_line_idx = -1;
  if (action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] != 0u &&
      batch->state.ground_id[idx] != 0xFFFFu) {
    const int carried_line_idx =
        stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
    if (carried_line_idx >= 0 && (size_t)carried_line_idx < g->line_count) {
      const MslStageFloorLine* carried = &g->lines[(size_t)carried_line_idx];
      fall_fast_same_hard_floor_owner =
          (uint8_t)(!carried->is_platform && !carried->is_ledge &&
                    carried->platform_transform_kind == MSL_STAGE_PLATFORM_TRANSFORM_NONE &&
                    floor_line_is_runtime_fighter_solid(g, stage_id, carried_line_idx));
      fall_fast_platform_to_hard_floor_owner =
          (uint8_t)(carried->is_platform && !carried->is_ledge &&
                    floor_line_is_runtime_fighter_solid(g, stage_id, carried_line_idx));
    }
  }
  if (action_id == (uint16_t)MSL_ACT_FALL && batch->state.ground_id[idx] != 0xFFFFu) {
    const int carried_line_idx =
        stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
    if (carried_line_idx >= 0 && (size_t)carried_line_idx < g->line_count) {
      const MslStageFloorLine* carried = &g->lines[(size_t)carried_line_idx];
      fall_carried_ledge_to_connected_hard_floor_owner =
          (uint8_t)(carried->is_ledge && !carried->is_platform &&
                    floor_line_is_runtime_fighter_solid(g, stage_id, carried_line_idx) &&
                    !floor_x_within_line_segment_strict(batch, bi, g, carried_line_idx,
                                                        batch->state.pos_x[idx]));
      // Sibling owner for the IN-SPAN case: a Fall that carried a ledge floor (ran off the
      // ledge), drifted back over the SAME strip, and descends onto it had NO owner at all -
      // the connected owner above only arms off-span and the hard-floor producers exclude
      // is_ledge lines - so the fighter fell through the stage (fuzz family ledgedash,
      // fox/falco FD; the warm repro is simply run-off-ledge -> drift back -> fall).
      // Source mpCheckFloor lands it: the carried index is only the prefer hint, never an
      // exclusion.
      // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
      fall_carried_same_ledge_floor_in_span_owner =
          (uint8_t)(carried->is_ledge && !carried->is_platform &&
                    floor_line_is_runtime_fighter_solid(g, stage_id, carried_line_idx) &&
                    floor_x_within_line_segment_strict(batch, bi, g, carried_line_idx,
                                                       batch->state.pos_x[idx]));
      fall_carried_line_idx = carried_line_idx;
    }
  }
  const uint8_t action_uses_bottom_sweep_owner =
      (action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
       action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B || fall_fast_same_hard_floor_owner ||
       fall_fast_platform_to_hard_floor_owner || fall_carried_ledge_to_connected_hard_floor_owner ||
       fall_carried_same_ledge_floor_in_span_owner)
          ? 1u
          : 0u;
  const uint8_t probe_this_47e14_helper =
      (uint8_t)(action_uses_bottom_sweep_owner ||
                action_uses_ftco_80096cc8_floor_callback(action_id));
  if (probe_this_47e14_helper) {
    const MslMpcollSourcePhases helper_source_phases =
        ctx != NULL ? mpcoll_source_phases_for_motion_state(
                          ctx->char_id, action_id,
                          mpcoll_ft_check_ground_ledge_uses_no_ledge_path(ctx->batch, ctx->idx))
                    : 0u;
    const uint8_t helper_probe_owner =
        mpcoll_source_phases_has(helper_source_phases, MSL_MPCOLL_PHASE_AIR_477E0)
            ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_477E0
            : (mpcoll_source_phases_has(helper_source_phases, MSL_MPCOLL_PHASE_AIR_471F8)
                   ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_471F8
                   : (mpcoll_source_phases_has(helper_source_phases, MSL_MPCOLL_PHASE_AIR_473CC)
                          ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_473CC
                          : (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_47E14));
    mpcoll_floor_probe_begin(ctx, helper_probe_owner, helper_source_phases, prefer_line_idx,
                             (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_OWNER);
    mpcoll_floor_probe_bottom_interval(ctx, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                                       cur_bottom_y);
  }
  if (!mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx)) {
    if (probe_this_47e14_helper) {
      mpcoll_floor_probe_result(ctx, NULL, 0u, 0u,
                                (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_RUNTIME_PREV);
    }
    return 0u;
  }
  // NOTE: directionality lives in the per-branch intersection (horizontal ay>=by in floor_intersect_horiz; sloped 0.1 slop in msl_mplib_line_intersection); no producer descent gate in source mpCheckFloor.
  if (!action_uses_bottom_sweep_owner || !action_uses_ftco_80096cc8_floor_callback(action_id) ||
      is_common_fallspecial_action(action_id)) {
    if (probe_this_47e14_helper) {
      mpcoll_floor_probe_result(ctx, NULL, 0u, 0u, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_OWNER);
    }
    return 0u;
  }

  // Common-air hard-floor owner:
  // JumpAerialF/B call `ft_800835B0 -> ft_80083090_inline -> mpColl_80047E14`, whose floor
  // producer is not conditional on a restored CollData.floor.index. It calls
  // `mpColl_80044628_Floor`, which runs `mpCheckFloor` across active floor lines using the
  // callback-local ECB bottom. Keep this path to real runtime-owned bottom-sweep hard-floor hits.
  // Fall_Coll's `ft_800831CC -> mpColl_80047F40(flags=6)` consumes the same floor producer for:
  // - sustained fastfall rows already carrying either the same ordinary hard floor or a live
  //   live platform floor that the source platform callback can reject before the hard floor below is
  //   accepted;
  // - off-span carried ledge floors whose live callback bottom sweep hits a connected ordinary hard
  //   floor through the source MapLine graph.
  // Keep those Fall slices on carried/generated floor topology plus runtime-owned bottom sweep; do
  // not admit no-floor rows, in-span ledge continuation, transformed stage-object floors without
  // platform metadata, or root-only projections.
  // Jump, CliffJump, soft platforms, transformed platforms, and root-only projections stay on their
  // existing owners; their broad callback class is not enough to prove this
  // transformed-platform-to-hard-body continuation.
  //
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class FT80083090_PLATFORM_PASS_COLL
  // data/stages/bin/*.bin::MSLSTG01 floor fighter_solid/platform_transform metadata
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083090_inline,ft_800835B0}
  // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,
  //   mpColl_80047F40,mpColl_80044838_Floor}
  (void)skip_platform_segment_i;
  (void)c;
  MslMpcollFloorSweepResult floor_sweep = {0};
  uint8_t same_ledge_in_span_hit = 0u;
  if (!mpcoll_collect_bottom_sweep_hard_floor_result(batch, idx, bi, g, stage_id, prev_bottom_x,
                                                     prev_bottom_y, cur_bottom_x, cur_bottom_y,
                                                     prefer_line_idx, -1, 0u, &floor_sweep)) {
    // The hard-floor collect excludes is_ledge lines; the same-carried-ledge in-span
    // re-landing legitimately hits exactly that line. Use the unfiltered collect and accept
    // only the carried line itself.
    if (fall_carried_same_ledge_floor_in_span_owner && fall_carried_line_idx >= 0 &&
        mpcoll_collect_bottom_sweep_hit(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                                        cur_bottom_x, cur_bottom_y, skip_platform_segment_i,
                                        fall_carried_line_idx, -1, c, &floor_sweep) &&
        floor_sweep.hit_line_idx == fall_carried_line_idx) {
      same_ledge_in_span_hit = 1u;
      mpcoll_project_bottom_sweep_floor_result(batch, bi, g, stage_id, cur_bottom_y, &floor_sweep);
    } else {
      mpcoll_floor_probe_result(ctx, NULL, 0u, 0u,
                                (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
      return 0u;
    }
  }
  if (floor_sweep.hit_line_idx < 0 || floor_sweep.projected_line_idx < 0 ||
      (size_t)floor_sweep.hit_line_idx >= g->line_count ||
      (size_t)floor_sweep.projected_line_idx >= g->line_count) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, floor_sweep.hit, 0u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_LINE_FILTER);
    return 0u;
  }
  if (!same_ledge_in_span_hit &&
      (floor_sweep.hit_is_platform || floor_sweep.hit_is_ledge ||
       floor_sweep.hit_has_platform_transform || floor_sweep.projected_is_platform ||
       floor_sweep.projected_is_ledge || floor_sweep.projected_has_platform_transform)) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 0u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_LINE_FILTER);
    return 0u;
  }
  if (!(batch->state.pos_y[idx] < floor_sweep.hit_y - k_floor_y_bias)) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 0u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION);
    return 0u;
  }
  if (floor_sweep.projected_y_corr < 0.0f) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 0u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION);
    return 0u;
  }
  if (fall_fast_same_hard_floor_owner &&
      floor_sweep.projected_segment_id != batch->state.ground_id[idx]) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 1u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_LINE_FILTER);
    return 0u;
  }
  if (fall_fast_platform_to_hard_floor_owner &&
      floor_sweep.projected_segment_id == batch->state.ground_id[idx]) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 1u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_LINE_FILTER);
    return 0u;
  }
  if (fall_carried_ledge_to_connected_hard_floor_owner &&
      (floor_sweep.projected_segment_id == batch->state.ground_id[idx] ||
       fall_carried_line_idx < 0 ||
       !floor_lines_connected(g, fall_carried_line_idx, floor_sweep.projected_line_idx))) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 1u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_LINE_FILTER);
    return 0u;
  }

  *ground_id_out = floor_sweep.projected_segment_id;
  *y_corr_out = floor_sweep.projected_y_corr;
  *contact_x_out = floor_sweep.hit_x;
  *contact_y_out = floor_sweep.hit_y;
  *floor_nx_out = floor_sweep.normal_x;
  *floor_ny_out = floor_sweep.normal_y;
  mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 1u, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
  return 1u;
}

uint8_t msl_mpcoll_800473cc_damage_stay_airborne_hard_floor_sweep(
    const MslMpcollContext* ctx, MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g,
    uint32_t stage_id, MslMpcollSourcePhases source_phases, float prev_bottom_x,
    float prev_bottom_y, float cur_bottom_x, float cur_bottom_y, int prefer_line_idx,
    uint16_t* ground_id_out, float* contact_x_out, float* contact_y_out, float* floor_nx_out,
    float* floor_ny_out) {
  if (ctx == NULL || batch == NULL || g == NULL || ground_id_out == NULL || contact_x_out == NULL ||
      contact_y_out == NULL || floor_nx_out == NULL || floor_ny_out == NULL ||
      prefer_line_idx < 0 || (size_t)prefer_line_idx >= g->line_count) {
    return 0u;
  }
  const uint16_t action_id = ctx->action_id;
  const MslStageFloorLine* carried_line = &g->lines[(size_t)prefer_line_idx];
  const uint8_t carried_ordinary_hard_floor =
      (uint8_t)(carried_line->segment_i == batch->state.ground_id[idx] &&
                !carried_line->is_platform && !carried_line->is_ledge &&
                carried_line->platform_transform_kind == MSL_STAGE_PLATFORM_TRANSFORM_NONE &&
                floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx));
  const uint8_t carried_offspan_ledge_to_hard_floor =
      (uint8_t)(carried_line->segment_i == batch->state.ground_id[idx] &&
                !carried_line->is_platform && carried_line->is_ledge &&
                carried_line->platform_transform_kind == MSL_STAGE_PLATFORM_TRANSFORM_NONE &&
                floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx) &&
                !floor_x_within_line_bounds(batch, bi, g, prefer_line_idx,
                                            batch->state.pos_x[idx]));
  const uint8_t carried_platform_to_hard_floor =
      (uint8_t)(carried_line->segment_i == batch->state.ground_id[idx] &&
                carried_line->is_platform && !carried_line->is_ledge &&
                floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx) &&
                !is_damage_fly_collision_action(action_id) &&
                batch->state.damage_allow_sdi[idx] == 0u);
  const uint8_t owner_active =
      // DamageFly/DamageFall AIR_473CC stay-airborne hard-floor contact:
      // `ft_80081DD4` copies live CollData.cur_pos into last_pos, writes fp->cur_pos, then routes
      // DamageFly/DamageFall through `mpColl_800473CC`. That helper calls
      // `inline0(..., stay_airborne=true)`: a floor hit from `mpColl_80044628_Floor` is consumed by
      // `mpColl_80044948_Floor`, updating the carried floor and root/ECB projection while keeping
      // the fighter airborne and returning false to the caller. Require the live AIR_473CC phase,
      // a runtime-owned previous root, and an actual ECB-bottom sweep against the carried ordinary
      // hard floor. Off-span carried ledges use the same source floor producer when the live sweep
      // reaches a connected ordinary hard floor through the generated stage-line graph. Common
      // non-fly Damage rows may also carry a stale one-way platform floor after hitlag; with
      // `allow_sdi` off, source `mpColl_800473CC` still runs the ordinary floor producer and
      // consumes an actual hard-floor bottom sweep as stay-airborne FloorPush/FloorHug. Restored
      // public `ground_id`, transformed stage-object floors, active hitlag SDI rows, and
      // DamageFly/DamageFall terminal owners cannot enter this platform-to-hard-floor lane.
      //
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
      //   ftCo_DamageFly_Coll,ftCo_DamageFall_Coll}
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor,
      //   mpColl_80044948_Floor}
      // data/stages/bin/*.bin::MSLSTG01 floor prev/next links + ledge flags
      (batch->state.hitlag[idx] == 0u && is_damage_collision_landing_action(action_id) &&
       action_id != (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL &&
       mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_473CC) &&
       (carried_ordinary_hard_floor || carried_offspan_ledge_to_hard_floor ||
        carried_platform_to_hard_floor) &&
       mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx))
          ? 1u
          : 0u;
  if (!owner_active) {
    return 0u;
  }

  MslMpcollFloorSweepResult floor_sweep = {0};
  mpcoll_floor_probe_begin(ctx, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_473CC, source_phases,
                           prefer_line_idx, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
  mpcoll_floor_probe_bottom_interval(ctx, prev_bottom_x, prev_bottom_y, cur_bottom_x, cur_bottom_y);
  if (!mpcoll_collect_bottom_sweep_hard_floor_result(batch, idx, bi, g, stage_id, prev_bottom_x,
                                                     prev_bottom_y, cur_bottom_x, cur_bottom_y,
                                                     prefer_line_idx, -1, 0u, &floor_sweep)) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, 0u, 0u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
    return 0u;
  }
  if ((carried_ordinary_hard_floor && floor_sweep.hit_segment_id != carried_line->segment_i) ||
      (carried_offspan_ledge_to_hard_floor &&
       (floor_sweep.hit_segment_id == carried_line->segment_i ||
        !floor_lines_connected(g, prefer_line_idx, floor_sweep.hit_line_idx))) ||
      (carried_platform_to_hard_floor && (floor_sweep.hit_is_platform || floor_sweep.hit_is_ledge ||
                                          floor_sweep.hit_has_platform_transform))) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 0u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION);
    return 0u;
  }

  const float ecb_bottom_rel_y = cur_bottom_y - batch->state.pos_y[idx];
  const float projection_x = (ecb_bottom_rel_y <= 0.0f) ? cur_bottom_x : batch->state.pos_x[idx];
  const float projection_y = (ecb_bottom_rel_y <= 0.0f) ? cur_bottom_y : batch->state.pos_y[idx];
  float y_corr = 0.0f;
  float floor_nx = floor_sweep.normal_x;
  float floor_ny = floor_sweep.normal_y;
  const int projected_line_idx =
      msl_mplib_8004dd90_floor(batch, bi, g, floor_sweep.hit_line_idx, projection_x, projection_y,
                               &y_corr, &floor_nx, &floor_ny);
  if (projected_line_idx < 0 || (size_t)projected_line_idx >= g->line_count || y_corr < 0.0f) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 0u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION);
    return 0u;
  }
  const MslStageFloorLine* projected_line = &g->lines[(size_t)projected_line_idx];
  if (projected_line->is_platform || projected_line->is_ledge ||
      projected_line->platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE ||
      (carried_offspan_ledge_to_hard_floor &&
       !floor_lines_connected(g, prefer_line_idx, projected_line_idx))) {
    mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 0u,
                              (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_LINE_FILTER);
    return 0u;
  }

  floor_sweep.projected_line_idx = projected_line_idx;
  floor_sweep.projected_segment_id = projected_line->segment_i;
  floor_sweep.projected_is_platform = 0u;
  floor_sweep.projected_is_ledge = 0u;
  floor_sweep.projected_has_platform_transform = 0u;
  floor_sweep.projected_has_height_platform_transform = 0u;
  floor_sweep.projected_y_corr = y_corr;
  floor_sweep.normal_x = floor_nx;
  floor_sweep.normal_y = floor_ny;

  batch->state.pos_y[idx] += y_corr;
  *ground_id_out = projected_line->segment_i;
  *contact_x_out = floor_sweep.hit_x;
  *contact_y_out = floor_sweep.hit_y;
  *floor_nx_out = floor_nx;
  *floor_ny_out = floor_ny;
  mpcoll_record_callback_floor_result_with_mode(ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE,
                                                (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
                                                *ground_id_out, *contact_x_out, *contact_y_out,
                                                *floor_nx_out, *floor_ny_out);
  mpcoll_floor_probe_result(ctx, &floor_sweep, 1u, 1u, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
  return 1u;
}

uint8_t msl_mpcoll_80047e14_flags6_root_floor_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_root_x, float prev_root_y, float cur_root_x, float cur_root_y, float cur_bottom_y,
    uint16_t terminal_source_action_id, int prefer_line_idx, uint16_t skip_platform_segment_i,
    uint8_t ecb_lock_timer_seed, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || ground_id_out == NULL || c == NULL ||
      (prefer_line_idx >= 0 && (size_t)prefer_line_idx >= g->line_count)) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (action_id != (uint16_t)MSL_ACT_FALL || prefer_line_idx < 0) {
    return 0u;
  }
  if (batch->state.ground_id[idx] == 0xFFFFu) {
    return 0u;
  }
  if (!(cur_root_y <= prev_root_y)) {
    return 0u;
  }
  // Fall_Coll flags=6 floor owner:
  // Fall_Coll routes through `ft_800831CC`, which loads the callback-local ECB via
  // `mpColl_80047F40(flags=6)`. When a prefix-causal CollData lock, first Fall callback
  // hard-floor continuation, or same-step FoD height-platform contact source keeps the
  // callback-local collision owner live, source floor publication can let
  // `mpColl_80044838_Floor(ignore_bottom=true)` project from `cur_pos` even when the extracted pose
  // bottom is still above the floor. Direct replay reseeds expose that root crossing; keep it tied
  // to generated floor/transform ownership instead of letting a no-floor airborne row synthesize a
  // landing from a generic sweep.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80047F40,mpColl_80044628_Floor,mpColl_80044838_Floor}
  float nx = 0.0f;
  float ny = 1.0f;
  MslMpcollFloorSweepResult root_sweep = {0};
  const uint8_t root_sweep_hit = mpcoll_collect_bottom_sweep_hit(
      batch, idx, bi, g, stage_id, prev_root_x, prev_root_y, cur_root_x, cur_root_y,
      skip_platform_segment_i, prefer_line_idx, -1, c, &root_sweep);
  int hit_line_idx = root_sweep_hit ? root_sweep.hit_line_idx : -1;
  float ix = root_sweep.hit_x;
  float iy = root_sweep.hit_y;
  nx = root_sweep_hit ? root_sweep.normal_x : nx;
  ny = root_sweep_hit ? root_sweep.normal_y : ny;
  const uint8_t fall_platform_callback_admits_by_input =
      (stick_i8_to_unit(batch->state.input_main_y[idx]) > c->platform_air_land_stick_y_threshold)
          ? 1u
          : 0u;
  if (fall_platform_callback_admits_by_input) {
    uint8_t found_source_height_platform = 0u;
    int best_source_height_platform_line_idx = -1;
    float best_source_height_platform_dist2 = 0.0f;
    float best_source_height_platform_y = 0.0f;
    for (size_t li = 0; li < g->line_count; li++) {
      const uint16_t segment_i = g->lines[li].segment_i;
      if (!g->lines[li].is_platform ||
          !stage_collision_floor_line_has_height_platform_transform(stage_id, segment_i) ||
          !stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi,
                                                                              segment_i) ||
          !floor_x_within_line_bounds(batch, bi, g, (int)li, cur_root_x)) {
        continue;
      }
      float line_y = 0.0f;
      if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, cur_root_x, &line_y)) {
        continue;
      }
      if (!(prev_root_y > (line_y + k_floor_y_bias) && cur_root_y < (line_y - k_floor_y_bias))) {
        continue;
      }
      const float dx = cur_root_x - prev_root_x;
      const float dy = line_y - prev_root_y;
      const float dist2 = dx * dx + dy * dy;
      if (!found_source_height_platform || dist2 < best_source_height_platform_dist2 ||
          (dist2 == best_source_height_platform_dist2 &&
           segment_i < g->lines[(size_t)best_source_height_platform_line_idx].segment_i)) {
        found_source_height_platform = 1u;
        best_source_height_platform_line_idx = (int)li;
        best_source_height_platform_dist2 = dist2;
        best_source_height_platform_y = line_y;
      }
    }
    if (found_source_height_platform && best_source_height_platform_line_idx >= 0) {
      // Fall_Coll's flags=6 source path can publish a source-trusted transformed platform from
      // the callback root even when the generic sweep was anchored to the prior CollData floor.
      // This is generated stage-line/current-height ownership, not a FoD stage-id exception.
      // data/stages/bin/*.bin::MSLSTG01 platform_transforms(kind=height)
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
      // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
      hit_line_idx = best_source_height_platform_line_idx;
      ix = cur_root_x;
      iy = best_source_height_platform_y;
      nx = 0.0f;
      ny = 1.0f;
    }
  }
  uint8_t pending_damage_terminal_fall_root_owner = 0u;
  if (hit_line_idx < 0 && action_id == (uint16_t)MSL_ACT_FALL &&
      mpcoll_non_air_common_damage_submotion(batch->state.animation_index[idx]) &&
      batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] == 0u &&
      batch->state.ground_id[idx] != 0xFFFFu && prefer_line_idx >= 0 &&
      (size_t)prefer_line_idx < g->line_count &&
      g->lines[(size_t)prefer_line_idx].segment_i == batch->state.ground_id[idx] &&
      !g->lines[(size_t)prefer_line_idx].is_ledge &&
      !stage_collision_floor_line_has_platform_transform(
          stage_id, g->lines[(size_t)prefer_line_idx].segment_i) &&
      (!g->lines[(size_t)prefer_line_idx].is_platform || fall_platform_callback_admits_by_input) &&
      floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx) &&
      mpcoll_floor_sweep_prev_root_is_source_owned(batch, idx) &&
      floor_x_within_line_bounds(batch, bi, g, prefer_line_idx, cur_root_x)) {
    float carried_floor_y = 0.0f;
    if (floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, cur_root_x, &carried_floor_y) &&
        cur_root_y < (carried_floor_y - k_floor_y_bias) &&
        (!g->lines[(size_t)prefer_line_idx].is_platform ||
         terminal_source_action_id != (uint16_t)MSL_ACT_DAMAGE_LW_1 ||
         cur_bottom_y <= (carried_floor_y + k_floor_y_bias))) {
      // Terminal non-DamageAir common Damage -> Fall same-frame Coll owner:
      // Damage_Anim can enter Fall before the same fighter proc reaches map collision. In PFZ:925
      // the preserved DamageLw2 submotion reaches Fall_Coll's flags=6 path and admits the carried
      // static platform. DamageAir* terminal rows are excluded: QHP:8304 and doubles:3229 show
      // vanilla keeps those airborne Fall instead of borrowing the carried floor publication.
      // DamageLw1's terminal platform borrow still needs current-bottom authority; Demo2 rec1635
      // has root below Battlefield's side platform while the DamageLw1 pose bottom is above it, and
      // source stays in airborne Fall rather than root-snapping up to Wait. DamageLw2 keeps the
      // root-projection owner (PFZ:925).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Anim
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_Coll}
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047F40,mpColl_80044838_Floor}
      hit_line_idx = prefer_line_idx;
      ix = cur_root_x;
      iy = carried_floor_y;
      nx = 0.0f;
      ny = 1.0f;
      pending_damage_terminal_fall_root_owner = 1u;
    }
  }
  if (hit_line_idx < 0) {
    return 0u;
  }
  const uint8_t ledge_to_ledge_continuation =
      (hit_line_idx >= 0 && prefer_line_idx >= 0 && hit_line_idx != prefer_line_idx &&
       g->lines[(size_t)hit_line_idx].is_ledge && g->lines[(size_t)prefer_line_idx].is_ledge)
          ? 1u
          : 0u;
  const uint16_t hit_segment_i = (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count)
                                     ? g->lines[(size_t)hit_line_idx].segment_i
                                     : (uint16_t)0xFFFFu;
  const uint8_t hit_line_is_platform = (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count &&
                                        g->lines[(size_t)hit_line_idx].is_platform)
                                           ? 1u
                                           : 0u;
  const uint8_t hit_line_height_platform_state_trusted =
      (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count &&
       stage_collision_floor_line_has_height_platform_transform(stage_id, hit_segment_i) &&
       stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, hit_segment_i))
          ? 1u
          : 0u;
  uint8_t hit_line_height_platform_same_step_contact = 0u;
  uint8_t hit_line_height_platform_current_source = 0u;
  if (hit_line_height_platform_state_trusted) {
    hit_line_height_platform_current_source =
        stage_height_platform_line_has_current_source(batch, bi, stage_id, hit_segment_i);
    uint8_t platform_id = 0u;
    if (stage_collision_floor_line_platform_transform_id(stage_id, hit_segment_i, &platform_id) &&
        platform_id < 2u &&
        (batch->state.stage_fod_platform_height_source[(size_t)bi * 2u + (size_t)platform_id] &
         (uint8_t)MSL_FOD_PLATFORM_HEIGHT_SOURCE_SAME_STEP_CONTACT) != 0u) {
      hit_line_height_platform_same_step_contact = 1u;
    }
  }
  const uint8_t locked_fall_root_owner =
      (ecb_lock_timer_seed != 0u &&
       (!hit_line_height_platform_state_trusted || hit_line_height_platform_current_source))
          ? 1u
          : 0u;
  const uint8_t hit_line_connected_hard_floor =
      (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count && prefer_line_idx >= 0 &&
       (size_t)prefer_line_idx < g->line_count && !hit_line_is_platform &&
       !stage_collision_floor_line_has_platform_transform(stage_id, hit_segment_i) &&
       !stage_collision_floor_line_has_platform_transform(
           stage_id, g->lines[(size_t)prefer_line_idx].segment_i) &&
       stage_floor_graph_has_height_platform_transform(stage_id, g) &&
       floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx) &&
       batch->state.seed_prev_action_id[idx] == action_id && batch->state.action_frame[idx] <= 2 &&
       floor_lines_connected(g, prefer_line_idx, hit_line_idx))
          ? 1u
          : 0u;
  const uint8_t hit_line_from_source_height_platform =
      (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count && prefer_line_idx >= 0 &&
       (size_t)prefer_line_idx < g->line_count && !hit_line_is_platform &&
       !stage_collision_floor_line_has_platform_transform(stage_id, hit_segment_i) &&
       stage_collision_floor_line_has_height_platform_transform(
           stage_id, g->lines[(size_t)prefer_line_idx].segment_i) &&
       stage_height_platform_line_has_current_source(batch, bi, stage_id,
                                                     g->lines[(size_t)prefer_line_idx].segment_i) &&
       batch->state.seed_prev_action_id[idx] == action_id)
          ? 1u
          : 0u;
  const uint8_t fall_platform_callback_admits_hit_line =
      (!hit_line_is_platform ||
       stick_i8_to_unit(batch->state.input_main_y[idx]) > c->platform_air_land_stick_y_threshold)
          ? 1u
          : 0u;
  const uint8_t source_trusted_fall_root_owner =
      (hit_line_connected_hard_floor || hit_line_from_source_height_platform ||
       (hit_line_height_platform_same_step_contact && fall_platform_callback_admits_hit_line) ||
       pending_damage_terminal_fall_root_owner)
          ? 1u
          : 0u;
  // Same-step seed contacts represent the current transformed platform line before Fall_Coll calls
  // `ft_800831CC -> mpColl_80047F40(flags=6)`. Live grIzumi scheduler state alone is not collision
  // authority here; Fall still needs current source/contact or generated-platform CollData
  // ownership, otherwise root-crossing creates false platform landings from ordinary pass-through
  // frames.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047F40,mpColl_80044628_Floor}
  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
  // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height,static_y)
  if (!locked_fall_root_owner && !ledge_to_ledge_continuation && !source_trusted_fall_root_owner) {
    return 0u;
  }
  if (ledge_to_ledge_continuation && !locked_fall_root_owner &&
      (batch->state.action_frame[idx] <= 1 || batch->state.seed_prev_action_frame[idx] <= 0 ||
       (batch->state
            .state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX] &
        (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) == 0u)) {
    // Non-locked ledge-floor continuations still need the Fall script's allow-interrupt phase
    // before the root projection can publish Landing. Earlier fastfall ledge crossings have a
    // replay-visible floor.index, but vanilla keeps Fall airborne until the callback's script phase
    // permits the floor handoff. ECB-locked rows keep their distinct CollData owner above.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    // refs/melee/src/melee/ft/types.h::Fighter::allow_interrupt (fp+0x2218:0)
    return 0u;
  }
  if (hit_line_height_platform_same_step_contact && hit_line_is_platform) {
    // Source-trusted FoD height-platform contact:
    // same-step grIzumi/mpLib contact provenance owns the current platform height for this
    // callback. Snap from the flags=6 Fall root to that generated line only for the seeded frame;
    // The frame scheduler clears the seed source mask before subsequent rollout frames.
    // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    batch->state.pos_y[idx] = iy + k_floor_y_bias;
    *ground_id_out = hit_segment_i;
    if (contact_x_out != NULL) {
      *contact_x_out = ix;
    }
    if (contact_y_out != NULL) {
      *contact_y_out = iy;
    }
    if (floor_nx_out != NULL) {
      *floor_nx_out = nx;
    }
    if (floor_ny_out != NULL) {
      *floor_ny_out = ny;
    }
    return 1u;
  }

  const MslCharParams* chp = msl_char_params_fast(batch->state.char_id[idx]);
  const uint8_t fall_ledge_floor_uses_expanded_collision_model =
      (chp != NULL && isfinite(chp->model_scaling) && chp->model_scaling > 1.0f) ? 1u : 0u;
  if (fall_ledge_floor_uses_expanded_collision_model && action_id == (uint16_t)MSL_ACT_FALL &&
      batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_FALL && hit_line_idx >= 0 &&
      g->lines[(size_t)hit_line_idx].is_ledge &&
      (prefer_line_idx < 0 || !g->lines[(size_t)prefer_line_idx].is_ledge) &&
      batch->state.action_frame[idx] >= 2 && batch->state.action_frame[idx] <= 3 &&
      batch->state.speed_y_self[idx] < 0.0f && prev_root_y > (iy + k_floor_y_bias) &&
      cur_root_y < iy) {
    // Same source boundary as the existing bottom-sweep guard below, but scoped to true
    // center/non-ledge -> ledge crossings. Rows already carrying a ledge floor.index are later
    // callback continuations and may publish Landing through the ordinary flags=6 owner.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_LoadECB_inline}
    return 0u;
  }

  float y_corr = 0.0f;
  const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, cur_root_x,
                                                    cur_root_y, &y_corr, &nx, &ny);
  if (out_line_idx < 0 || y_corr < 0.0f) {
    return 0u;
  }

  const float corrected_y = cur_root_y + y_corr;
  batch->state.pos_y[idx] = corrected_y;
  *ground_id_out = g->lines[(size_t)out_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = cur_root_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = corrected_y - k_floor_y_bias;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = ny;
  }
  (void)ix;
  return 1u;
}

uint8_t msl_mpcoll_80047e14_fallspecial_connected_hard_floor_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_root_y, float cur_root_x, float cur_root_y, int prefer_line_idx,
    uint16_t* ground_id_out, float* contact_x_out, float* contact_y_out, float* floor_nx_out,
    float* floor_ny_out) {
  if (batch == NULL || g == NULL || ground_id_out == NULL || prefer_line_idx < 0 ||
      (size_t)prefer_line_idx >= g->line_count) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (!is_common_fallspecial_action(action_id) || batch->state.ground_id[idx] == 0xFFFFu ||
      batch->state.ground_id[idx] != g->lines[(size_t)prefer_line_idx].segment_i ||
      batch->state.seed_prev_action_id[idx] != action_id ||
      batch->state.seed_prev_action_frame[idx] < 2 || batch->state.fall_fast[idx] != 0u ||
      batch->state.fall_fast_frame_start[idx] != 0u || batch->state.speed_y_self[idx] >= 0.0f ||
      !(cur_root_y <= prev_root_y) || !mpcoll_floor_sweep_prev_root_is_source_owned(batch, idx)) {
    return 0u;
  }
  const MslStageFloorLine* carried = &g->lines[(size_t)prefer_line_idx];
  if (carried->is_platform || carried->has_alternate_endpoint_link != 0u ||
      stage_collision_floor_line_has_platform_transform(stage_id, carried->segment_i) ||
      !floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx)) {
    return 0u;
  }

  // FallSpecial_Coll connected hard-floor root projection:
  // `ftCo_FallSpecial_Coll -> ft_80083090 -> mpColl_80047E14(flags=6)` consumes the source-owned
  // CollData.prev_pos/floor.index endpoint from `mpCollPrev`, then
  // `mpColl_80044838_Floor(ignore_bottom=true)` may project from the callback root along the
  // connected ordinary hard-floor graph even when the loaded FallSpecial ECB bottom is still above
  // the floor. Stages with generated alternate endpoint links (currently frozen Stadium's
  // transform-map adjacency) are not this source owner: without a source bottom/final floor result,
  // borrowing those alternate links fabricates same-height ledge contacts that vanilla keeps
  // airborne until the later publication callback. Keep the reconstruction to the shallow
  // later-sustained non-fastfall callback window: the preceding IPW callback and frame-start
  // fastfall rows remain airborne, and far-below offstage FallSpecial rows must not snap back to a
  // stale carried ledge. Fastfall/bottom-sweep rows remain on `mpColl_80044628_Floor`; platforms,
  // transformed floors, and alternate endpoint graphs stay on their callback-admitted sweep/final
  // publication owners.
  //
  // data/stages/bin/*.bin::MSLSTG01 floor prev/next/fighter_solid/platform_transform/
  //   alternate_endpoint_link metadata
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
  //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80083090
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,
  //   mpColl_80044838_Floor}
  if (floor_x_within_line_bounds(batch, bi, g, prefer_line_idx, cur_root_x)) {
    return 0u;
  }

  uint8_t found_projection = 0u;
  int projected_line_idx = -1;
  float projected_y = 0.0f;
  float best_lift = 0.0f;
  for (size_t li = 0; li < g->line_count; li++) {
    if ((int)li == prefer_line_idx || !floor_lines_connected(g, prefer_line_idx, (int)li) ||
        !g->lines[li].is_ledge || g->lines[li].is_platform ||
        stage_collision_floor_line_has_platform_transform(stage_id, g->lines[li].segment_i) ||
        !floor_line_is_runtime_fighter_solid(g, stage_id, (int)li) ||
        !floor_x_within_line_bounds(batch, bi, g, (int)li, cur_root_x)) {
      continue;
    }
    float line_y = 0.0f;
    if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, cur_root_x, &line_y)) {
      continue;
    }
    const float lift = (line_y + k_floor_y_bias) - cur_root_y;
    const float max_lift = fabsf(batch->state.speed_y_self[idx]) + (2.0f * k_ecb_vertical_unit);
    if (lift < 0.0f || lift > max_lift) {
      continue;
    }
    if (!found_projection || lift < best_lift ||
        (lift == best_lift &&
         g->lines[li].segment_i < g->lines[(size_t)projected_line_idx].segment_i)) {
      found_projection = 1u;
      projected_line_idx = (int)li;
      projected_y = line_y;
      best_lift = lift;
    }
  }
  if (!found_projection || projected_line_idx < 0) {
    return 0u;
  }
  const MslStageFloorLine* projected = &g->lines[(size_t)projected_line_idx];

  batch->state.pos_y[idx] = projected_y + k_floor_y_bias;
  *ground_id_out = projected->segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = cur_root_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = projected_y;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = 0.0f;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = 1.0f;
  }
  return 1u;
}

uint8_t msl_mpcoll_80047e14_reject_fall_same_floor_early_final_land(
    const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint16_t action_id, uint16_t seed_ground_id, uint16_t ground_id, int final_ground_line_idx,
    float contact_y, float root_y) {
  // Final-publication guard for Fall_Coll's same-floor root-crossing edge:
  // ftCo_Fall_Coll delegates landing to ft_80082B1C after mpColl's ECB-bottom floor sweep. A final
  // root projection can see a terminal-cardinal ledge floor one frame before that callback
  // publishes Landing; keep only the first same-floor ledge root crossing airborne and let the
  // following deeper frame consume the normal landing path. Require monotonic Fall action-frame
  // provenance so later loop-wrap Fall rows do not reuse the early-frame window. The center hard
  // floor is deliberately excluded because ordinary Fall_Coll main-floor crossings publish Landing
  // immediately.
  // data/stages/bin/*.bin::MSLSTG01 floor flags/links/platform-transform metadata
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082B1C
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
  //
  // One-frame-early stand-in for the unblended Fall ECB bottom: when the character's data mask
  // marks the CommonFall blended ECB as the collision consumer the swept bottom is source-true
  // and the game's plain mpCheckFloor bottom sweep lands the same-floor ledge crossing on THIS
  // frame (fall-floor probe PaleMajorEchidna p1 f3913: falco sweeps blended bottoms
  // 4.483 -> 4.518 across the Dream Land ledge floor at x -81.6, ret=1, y snapped to 0).
  if (batch != NULL && mpcoll_common_fall_blended_ecb_live_owner(batch, idx, action_id) != 0u) {
    return 0u;
  }
  return (uint8_t)(batch != NULL && g != NULL && action_id == (uint16_t)MSL_ACT_FALL &&
                   batch->state.seed_prev_action_id[idx] == action_id &&
                   batch->state.seed_prev_action_frame[idx] <= batch->state.action_frame[idx] &&
                   batch->state.action_frame[idx] <= 5 && batch->state.speed_y_self[idx] < 0.0f &&
                   final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
                   floor_line_is_terminal_cardinal_ledge_floor(batch, bi, g, stage_id,
                                                               final_ground_line_idx) &&
                   ground_id == seed_ground_id &&
                   batch->state.floor_sweep_prev_pos_y[idx] > contact_y && root_y < contact_y);
}

uint8_t floor_4a908_retry(MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g,
                          uint32_t stage_id, int persisted_line_idx, float prev_bottom_x,
                          float prev_bottom_y, float prev_side_mid_y, float cur_bottom_x,
                          float cur_bottom_y, uint16_t skip_platform_segment_i,
                          uint16_t* ground_id_out, float* contact_x_out, float* contact_y_out,
                          float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || ground_id_out == NULL || persisted_line_idx < 0 ||
      (size_t)persisted_line_idx >= g->line_count) {
    return 0u;
  }
  // Source retry:
  // - mpColl_8004A908_Floor first retries the normal previous-bottom -> current-bottom sweep with
  //   a NULL callback.
  // - If that does not find a disconnected floor, it retries from the previous ECB vertical
  //   midpoint (`0.5 * (prev_ecb.top.y + prev_ecb.bottom.y) + prev_pos.y`) to the current bottom.
  // - Both retries require a different floor that is not connected to the persisted
  //   CollData.floor.index. Platform admission remains floor_skip/fighter-solid gated.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A908_Floor
  // Conservative broad phase for the same source retry: if the two source sweeps' bounding box
  // cannot overlap any disconnected floor's world-line bounds, the exact mpCheckFloor passes below
  // would be guaranteed misses.
  const float sweep_min_x = ((prev_bottom_x < cur_bottom_x) ? prev_bottom_x : cur_bottom_x) -
                            (k_floor_x_end_clamp + k_ecb_vertical_unit);
  const float sweep_max_x = ((prev_bottom_x > cur_bottom_x) ? prev_bottom_x : cur_bottom_x) +
                            (k_floor_x_end_clamp + k_ecb_vertical_unit);
  float sweep_min_y = cur_bottom_y;
  float sweep_max_y = cur_bottom_y;
  if (prev_bottom_y < sweep_min_y) {
    sweep_min_y = prev_bottom_y;
  }
  if (prev_bottom_y > sweep_max_y) {
    sweep_max_y = prev_bottom_y;
  }
  if (prev_side_mid_y < sweep_min_y) {
    sweep_min_y = prev_side_mid_y;
  }
  if (prev_side_mid_y > sweep_max_y) {
    sweep_max_y = prev_side_mid_y;
  }
  sweep_min_y -= k_ecb_vertical_unit;
  sweep_max_y += k_ecb_vertical_unit;
  uint8_t may_hit_disconnected_floor = 0u;
  for (size_t li = 0; li < g->line_count; li++) {
    if ((int)li == persisted_line_idx) {
      continue;
    }
    const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, (int)li);
    const float line_min_x = ((l.x0 < l.x1) ? l.x0 : l.x1) - k_floor_x_end_clamp;
    const float line_max_x = ((l.x0 > l.x1) ? l.x0 : l.x1) + k_floor_x_end_clamp;
    const float line_min_y = ((l.y0 < l.y1) ? l.y0 : l.y1) - k_ecb_vertical_unit;
    const float line_max_y = ((l.y0 > l.y1) ? l.y0 : l.y1) + k_ecb_vertical_unit;
    if (sweep_max_x < line_min_x || sweep_min_x > line_max_x || sweep_max_y < line_min_y ||
        sweep_min_y > line_max_y || floor_lines_connected(g, (int)li, persisted_line_idx)) {
      continue;
    }
    may_hit_disconnected_floor = 1u;
    break;
  }
  if (!may_hit_disconnected_floor) {
    return 0u;
  }
  const float start_y[2] = {prev_bottom_y, prev_side_mid_y};
  int accepted_line_idx = -1;
  float nx = 0.0f;
  float ny = 1.0f;
  for (size_t pass = 0; pass < 2u; pass++) {
    int hit_line_idx = -1;
    float ix = 0.0f;
    float iy = 0.0f;
    float hit_nx = 0.0f;
    float hit_ny = 1.0f;
    if (!msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bottom_x, start_y[pass], cur_bottom_x,
                           cur_bottom_y, skip_platform_segment_i, persisted_line_idx, -1, NULL,
                           &hit_line_idx, &ix, &iy, &hit_nx, &hit_ny)) {
      continue;
    }
    if (hit_line_idx < 0 || (size_t)hit_line_idx >= g->line_count ||
        hit_line_idx == persisted_line_idx ||
        floor_lines_connected(g, hit_line_idx, persisted_line_idx)) {
      continue;
    }
    accepted_line_idx = hit_line_idx;
    nx = hit_nx;
    ny = hit_ny;
    break;
  }
  if (accepted_line_idx < 0) {
    return 0u;
  }

  float y_corr = 0.0f;
  const int projected_line_idx = msl_mplib_8004dd90_floor(
      batch, bi, g, accepted_line_idx, cur_bottom_x, cur_bottom_y, &y_corr,
      floor_nx_out != NULL ? floor_nx_out : &nx, floor_ny_out != NULL ? floor_ny_out : &ny);
  if (projected_line_idx < 0) {
    // Source immediately follows an accepted mpColl_8004A908_Floor with
    // mpColl_80044838_Floor, which endpoint-snaps the accepted floor even for hard floors.
    // Keep hard-floor endpoint snap scoped to this accepted disconnected-floor retry so ordinary
    // hard-floor off-end handling is not broadened.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004A908_Floor,mpColl_80044838_Floor}
    if (!msl_mpcoll_80044838_floor_edge_snap_from_bottom(
            batch, bi, g, accepted_line_idx, cur_bottom_x, cur_bottom_y, 1u, ground_id_out,
            contact_x_out, contact_y_out, floor_nx_out, floor_ny_out)) {
      return 0u;
    }
    if (contact_x_out != NULL) {
      batch->state.pos_x[idx] += (*contact_x_out - cur_bottom_x);
    }
    if (contact_y_out != NULL) {
      batch->state.pos_y[idx] += (*contact_y_out - cur_bottom_y);
    }
    return 1u;
  }

  *ground_id_out = g->lines[(size_t)projected_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = cur_bottom_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = cur_bottom_y + y_corr;
  }
  batch->state.pos_y[idx] += y_corr;
  return 1u;
}

uint8_t attackair_flags0_floor_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslEcbWorldPoints* prev_ecb, const MslEcbWorldPoints* cur_ecb, int prefer_line_idx,
    uint16_t skip_platform_segment_i, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || prev_ecb == NULL || cur_ecb == NULL || ground_id_out == NULL ||
      prefer_line_idx < 0 || (size_t)prefer_line_idx >= g->line_count) {
    return 0u;
  }
  (void)c;
  const uint16_t action_id = batch->state.action_id[idx];
  const uint8_t same_action_callback = (batch->state.prev_action_id[idx] == action_id ||
                                        batch->state.seed_prev_action_id[idx] == action_id)
                                           ? 1u
                                           : 0u;
  const uint16_t floor_skip_segment_id = batch->state.floor_skip_segment_id[idx];
  const int floor_skip_line_idx = stage_collision_floor_line_index(stage_id, floor_skip_segment_id);
  const uint8_t floor_skip_names_soft_floor =
      (floor_skip_segment_id != 0xFFFFu && floor_skip_line_idx >= 0 &&
       (g->lines[(size_t)floor_skip_line_idx].is_platform ||
        stage_collision_floor_line_has_platform_transform(stage_id, floor_skip_segment_id)))
          ? 1u
          : 0u;
  const uint8_t prefer_line_is_plain_hard_floor =
      (!g->lines[(size_t)prefer_line_idx].is_platform &&
       !stage_collision_floor_line_has_platform_transform(
           stage_id, g->lines[(size_t)prefer_line_idx].segment_i))
          ? 1u
          : 0u;
  const uint8_t carried_floor_skip_connected_hard_floor_owner =
      (floor_skip_names_soft_floor && prefer_line_is_plain_hard_floor) ? 1u : 0u;
  if (!is_attackair_action(action_id) || batch->state.speed_y_self[idx] >= 0.0f ||
      (!carried_floor_skip_connected_hard_floor_owner && floor_skip_segment_id != 0xFFFFu) ||
      !same_action_callback || !(cur_ecb->bottom_rel_y > 0.0f)) {
    return 0u;
  }

  // AttackAir_Coll routes through ft_80082C74 -> mpColl_800471F8, whose air-collision floor path
  // first requires `mpColl_80044628_Floor` to accept a bottom sweep, then lets
  // `mpColl_80044838_Floor(ignore_bottom=true)` project from the callback root when the loaded ECB
  // bottom is above the root. The generic air sweep skips the preferred floor.index after an
  // earlier projection attempt; that is too narrow for connected hard-floor seams/edges because
  // source `mpCheckFloor` still tests the carried floor chain before the root snap. A carried
  // floor_skip rejects only its selected soft platform in source, not every hard-floor candidate:
  // when the skip line is a platform/transform and the callback candidate is a generated hard
  // floor, let `msl_mpcheck_floor` filter the selected platform but keep connected hard floors
  // eligible. FoD height-transform platforms use this same source root snap when the line state is
  // source-trusted and the callback root is past the shallow first-phase platform-contact band.
  // AttackAir_Coll does not pass `ftCo_80096CC8`, so held-down input alone is not a platform-pass
  // reject here.
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 coll_cb_by_action
  // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
  // data/stages/bin/*.bin::MSLSTG01 floor segment links/fighter_solid flags
  // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
  float nx = 0.0f;
  float ny = 1.0f;
  MslMpcollFloorSweepResult floor_sweep = {0};
  int hit_line_idx = -1;
  float ix = 0.0f;
  float iy = 0.0f;
  if (!mpcoll_collect_bottom_sweep_hit(
          batch, idx, bi, g, stage_id, prev_ecb->bottom_x, prev_ecb->bottom_y, cur_ecb->bottom_x,
          cur_ecb->bottom_y, skip_platform_segment_i, prefer_line_idx, -1, NULL, &floor_sweep)) {
    // Replay seeds can expose FoD's current same-step height-platform contact after the visible ECB
    // bottom is already below the moving line. Source `mpColl_800471F8` consumes the hidden
    // CollData floor result and then projects from the callback root; reconstruct only that
    // current-source MSLSTG01 owner here so down-held input or a stale named platform height cannot
    // synthesize AttackAir landings.
    // agent_docs/DATA_CONTRACT.md::FoD platform height source mask
    // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
    //   mpColl_80044838_Floor}
    uint8_t found_source_height_platform = 0u;
    float best_lift = 0.0f;
    int best_line_idx = -1;
    float best_line_y = 0.0f;
    float best_nx = 0.0f;
    float best_ny = 1.0f;
    for (size_t li = 0; li < g->line_count; li++) {
      const uint16_t segment_i = g->lines[li].segment_i;
      if (!g->lines[li].is_platform ||
          !stage_collision_floor_line_has_height_platform_transform(stage_id, segment_i) ||
          !stage_height_platform_line_has_current_source(batch, bi, stage_id, segment_i) ||
          !stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi,
                                                                              segment_i) ||
          !floor_line_is_runtime_fighter_solid(g, stage_id, (int)li) ||
          !floor_x_within_line_segment_strict(batch, bi, g, (int)li, batch->state.pos_x[idx])) {
        continue;
      }
      float line_y = 0.0f;
      if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, batch->state.pos_x[idx], &line_y)) {
        continue;
      }
      float y_corr = 0.0f;
      float line_nx = 0.0f;
      float line_ny = 1.0f;
      const int out_line_idx =
          msl_mplib_8004dd90_floor(batch, bi, g, (int)li, batch->state.pos_x[idx],
                                   batch->state.pos_y[idx], &y_corr, &line_nx, &line_ny);
      if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count ||
          g->lines[(size_t)out_line_idx].segment_i != segment_i) {
        continue;
      }
      const float max_lift = fabsf(batch->state.speed_y_self[idx]) + cur_ecb->bottom_rel_y +
                             mpcoll_floor_projection_lift_allowance(cur_ecb);
      if (y_corr < 0.0f || y_corr > max_lift ||
          batch->state.pos_y[idx] > (line_y + k_floor_y_bias)) {
        continue;
      }
      if (!found_source_height_platform || y_corr < best_lift ||
          (y_corr == best_lift && segment_i < g->lines[(size_t)best_line_idx].segment_i)) {
        found_source_height_platform = 1u;
        best_lift = y_corr;
        best_line_idx = (int)li;
        best_line_y = line_y;
        best_nx = line_nx;
        best_ny = line_ny;
      }
    }
    if (!found_source_height_platform || best_line_idx < 0) {
      return 0u;
    }
    hit_line_idx = best_line_idx;
    ix = batch->state.pos_x[idx];
    iy = best_line_y;
    nx = best_nx;
    ny = best_ny;
  } else {
    hit_line_idx = floor_sweep.hit_line_idx;
    ix = floor_sweep.hit_x;
    iy = floor_sweep.hit_y;
    nx = floor_sweep.normal_x;
    ny = floor_sweep.normal_y;
  }
  if (hit_line_idx < 0 || (size_t)hit_line_idx >= g->line_count) {
    return 0u;
  }
  const uint16_t hit_segment_i = g->lines[(size_t)hit_line_idx].segment_i;
  const uint8_t hit_line_height_platform_state_trusted =
      (stage_collision_floor_line_has_height_platform_transform(stage_id, hit_segment_i) &&
       stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, hit_segment_i))
          ? 1u
          : 0u;
  const uint8_t hit_line_ordinary_hard_floor =
      (!g->lines[(size_t)hit_line_idx].is_platform &&
       !stage_collision_floor_line_has_platform_transform(stage_id, hit_segment_i))
          ? 1u
          : 0u;
  const float hit_line_prev_root_below_depth = iy - batch->state.prev_pos_y[idx];
  const uint8_t attackair_height_platform_root_snap =
      (hit_line_height_platform_state_trusted &&
       action_uses_shallow_attackair_platform_ecb_owner(batch->state.char_id[idx], action_id) &&
       (!move_tables_attackair_first_hitbox_phase(batch->state.char_id[idx], action_id,
                                                  batch->state.anim_frame_f32[idx]) ||
        hit_line_prev_root_below_depth > (2.0f * k_ecb_vertical_unit)))
          ? 1u
          : 0u;
  if (!hit_line_ordinary_hard_floor && !attackair_height_platform_root_snap) {
    return 0u;
  }
  if (hit_line_ordinary_hard_floor && !floor_lines_connected(g, prefer_line_idx, hit_line_idx)) {
    return 0u;
  }

  float y_corr = 0.0f;
  const int out_line_idx =
      msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, batch->state.pos_x[idx],
                               batch->state.pos_y[idx], &y_corr, &nx, &ny);
  if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count) {
    return 0u;
  }
  const uint16_t out_segment_i = g->lines[(size_t)out_line_idx].segment_i;
  const uint8_t out_line_height_platform_state_trusted =
      (stage_collision_floor_line_has_height_platform_transform(stage_id, out_segment_i) &&
       stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, out_segment_i))
          ? 1u
          : 0u;
  if (hit_line_ordinary_hard_floor &&
      (g->lines[(size_t)out_line_idx].is_platform ||
       stage_collision_floor_line_has_platform_transform(stage_id, out_segment_i))) {
    return 0u;
  }
  if (attackair_height_platform_root_snap && !out_line_height_platform_state_trusted) {
    return 0u;
  }
  if (y_corr < 0.0f || y_corr > (fabsf(batch->state.speed_y_self[idx]) +
                                 mpcoll_floor_projection_lift_allowance(cur_ecb))) {
    return 0u;
  }

  batch->state.pos_y[idx] += y_corr;
  *ground_id_out = out_segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = batch->state.pos_x[idx];
  }
  if (contact_y_out != NULL) {
    *contact_y_out = batch->state.pos_y[idx] - k_floor_y_bias;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = ny;
  }
  (void)ix;
  (void)iy;
  return 1u;
}

static uint8_t escapeair_locked_floor_bottom_sweep_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint8_t stage_has_height_platform_transform, const MslEcbWorldPoints* prev_ecb,
    uint16_t skip_platform_segment_i, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || prev_ecb == NULL || ground_id_out == NULL ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR ||
      stage_has_height_platform_transform == 0u || batch->state.ecb_lock_timer[idx] == 0u ||
      batch->state.speed_y_self[idx] >= 0.0f) {
    return 0u;
  }

  // EscapeAir_Coll's ft_80082C74 path consumes CollData_X130_Locked before the root snap:
  // `mpColl_80044628_Floor` checks the callback-local previous/locked bottom sweep, and
  // `mpColl_80044838_Floor(ignore_bottom=true)` can then project the EscapeAir root to the accepted
  // floor. Replay-visible EscapeAir poses often expose a zero current bottom; use the explicit
  // locked desired bottom when present, otherwise the prefix-causal previous ECB bottom from
  // CollData. Sustained soft-platform rows keep the existing platform countdown boundary; fresh
  // Jump/JumpAerial -> EscapeAir entries can publish either soft platforms or non-platform hard
  // floors from the same bottom-sweep owner.
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 coll_cb_by_action
  // data/stages/bin/*.bin::MSLSTG01 platform transform records
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
  float source_bottom_rel_y = prev_ecb->bottom_rel_y;
  if (batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
      msl_escapeair_locked_bottom_owner_any(
          batch->state.coll_desired_ecb_bottom_locked_owner[idx])) {
    source_bottom_rel_y = batch->state.coll_desired_ecb_bottom_rel_y[idx];
  }
  if (!(source_bottom_rel_y > k_floor_y_bias)) {
    return 0u;
  }

  float nx = 0.0f;
  float ny = 1.0f;
  MslMpcollFloorSweepResult floor_sweep = {0};
  if (!mpcoll_collect_bottom_sweep_hit(
          batch, idx, bi, g, stage_id, batch->state.floor_sweep_prev_pos_x[idx],
          batch->state.floor_sweep_prev_pos_y[idx] + source_bottom_rel_y, batch->state.pos_x[idx],
          batch->state.pos_y[idx] + source_bottom_rel_y, skip_platform_segment_i, -1, -1, c,
          &floor_sweep)) {
    return 0u;
  }
  const int hit_line_idx = floor_sweep.hit_line_idx;
  if (hit_line_idx < 0 || (size_t)hit_line_idx >= g->line_count) {
    return 0u;
  }

  const uint16_t hit_segment_i = g->lines[(size_t)hit_line_idx].segment_i;
  const uint8_t hit_line_is_platform = g->lines[(size_t)hit_line_idx].is_platform ? 1u : 0u;
  const uint8_t hit_line_is_ordinary_hard_floor =
      (!hit_line_is_platform &&
       !stage_collision_floor_line_has_platform_transform(stage_id, hit_segment_i))
          ? 1u
          : 0u;
  const uint8_t hit_line_has_height_platform_transform =
      stage_collision_floor_line_has_height_platform_transform(stage_id, hit_segment_i);
  const uint8_t fresh_jump_escapeair_entry =
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B)
          ? 1u
          : 0u;
  const uint8_t sustained_escapeair =
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR) ? 1u : 0u;
  if (!fresh_jump_escapeair_entry && !sustained_escapeair) {
    return 0u;
  }
  if (hit_line_is_ordinary_hard_floor && !fresh_jump_escapeair_entry) {
    return 0u;
  }
  if (!hit_line_is_platform && !hit_line_is_ordinary_hard_floor) {
    return 0u;
  }
  if (hit_line_has_height_platform_transform && sustained_escapeair &&
      batch->state.ecb_lock_timer[idx] > 2u) {
    // FoD side-platform EscapeAir rows still spend the earlier lock phases in the interpolation
    // gap: EWT:941 (timer 3) remains airborne, while EWT:942 (timer 2) is the first source-owned
    // platform publication. Static center platforms and fresh Jump/JumpAerial entry rows use their
    // own floor-callback path and are not delayed by this side-platform countdown boundary.
    // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_UnlockECB}
    return 0u;
  }

  float y_corr = 0.0f;
  const int out_line_idx =
      msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, batch->state.pos_x[idx],
                               batch->state.pos_y[idx], &y_corr, &nx, &ny);
  if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count) {
    return 0u;
  }
  const uint16_t out_segment_i = g->lines[(size_t)out_line_idx].segment_i;
  if (hit_line_is_platform && !g->lines[(size_t)out_line_idx].is_platform) {
    return 0u;
  }
  if (hit_line_is_ordinary_hard_floor &&
      (g->lines[(size_t)out_line_idx].is_platform ||
       stage_collision_floor_line_has_platform_transform(stage_id, out_segment_i))) {
    return 0u;
  }
  if (y_corr < 0.0f || y_corr > (source_bottom_rel_y + fabsf(batch->state.speed_y_self[idx]) +
                                 k_ecb_vertical_unit)) {
    return 0u;
  }

  batch->state.pos_y[idx] += y_corr;
  *ground_id_out = out_segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = batch->state.pos_x[idx];
  }
  if (contact_y_out != NULL) {
    *contact_y_out = batch->state.pos_y[idx] - k_floor_y_bias;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = ny;
  }
  return 1u;
}

static uint8_t escapeair_locked_platform_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint16_t skip_platform_segment_i, uint8_t ecb_lock_timer_seed, float start_pose_bottom_rel_y,
    float current_pose_bottom_rel_y, float current_loaded_bottom_rel_y,
    float current_pose_top_rel_y, uint8_t locked_desired_bottom_owner,
    float locked_desired_bottom_rel_y, uint16_t* ground_id_out, float* contact_x_out,
    float* contact_y_out, float* floor_nx_out, float* floor_ny_out, const MslCommonParams* c) {
  const uint8_t fresh_jumpaerial_zero_bottom_entry =
      (batch != NULL &&
       (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
        batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
       batch->state.seed_prev_action_frame[idx] <= 1 &&
       fabsf(locked_desired_bottom_rel_y) <= k_floor_y_bias)
          ? 1u
          : 0u;
  if (batch == NULL || g == NULL || ground_id_out == NULL || !(start_pose_bottom_rel_y > 0.0f) ||
      !(current_pose_bottom_rel_y > 0.0f) ||
      !(current_pose_top_rel_y > current_pose_bottom_rel_y)) {
    return 0u;
  }

  // Locked EscapeAir platform floor callback:
  // ftCo_EscapeAir_Coll uses ft_80082C74 -> ft_80081D0C -> mpColl_800471F8. While
  // CollData_X130_Locked is active, mpColl_LoadECB_inline preserves the desired bottom and
  // mpColl_80046904 can land via mpColl_80044838_Floor(ignore_bottom=true), projecting from the
  // fighter root instead of the locked zero-bottom point. Keep this consumer platform-only and
  // require a real root-vs-ECB separation before accepting the root projection; shallow platform
  // grazes remain owned by the ordinary sweep/suppression path below.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_LoadECB_inline,mpColl_80043754,mpColl_80046904,mpColl_80044838_Floor}
  const float root_x = batch->state.pos_x[idx];
  const float root_y = batch->state.pos_y[idx];
  const float min_lift = fmaxf(k_ecb_vertical_unit, start_pose_bottom_rel_y - k_ecb_vertical_unit);
  const float max_lift = start_pose_bottom_rel_y + k_ecb_vertical_unit;

  uint8_t found = 0u;
  float best_lift = FLT_MAX;
  int best_line_idx = -1;
  float best_nx = 0.0f;
  float best_ny = 1.0f;

  for (size_t li = 0; li < g->line_count; li++) {
    if (!g->lines[li].is_platform) {
      continue;
    }
    if (!floor_line_is_runtime_fighter_solid(g, stage_id, (int)li)) {
      continue;
    }
    if (floor_line_is_skipped_platform(stage_id, g, (int)li, skip_platform_segment_i)) {
      continue;
    }

    float y_corr = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    const int out_line_idx =
        msl_mplib_8004dd90_floor(batch, bi, g, (int)li, root_x, root_y, &y_corr, &nx, &ny);
    if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count ||
        !g->lines[(size_t)out_line_idx].is_platform) {
      continue;
    }
    if (!floor_line_is_runtime_fighter_solid(g, stage_id, out_line_idx)) {
      continue;
    }
    if (floor_line_is_skipped_platform(stage_id, g, out_line_idx, skip_platform_segment_i)) {
      continue;
    }
    if (stage_collision_floor_line_has_height_platform_transform(
            stage_id, g->lines[(size_t)out_line_idx].segment_i) &&
        !stage_height_platform_line_has_current_source(batch, bi, stage_id,
                                                       g->lines[(size_t)out_line_idx].segment_i) &&
        batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
        ecb_lock_timer_seed > 2u) {
      // FoD side-platform EscapeAir rows still spend the high lock-countdown phase in the
      // interpolation gap: root projection can see the live transformed platform before
      // mpColl_80044628_Floor has produced the source bottom sweep consumed by
      // mpColl_80044838_Floor. Prefix-causal current-height evidence (direct event, ground
      // contact, or same-step contact) remains a source-owned exception; otherwise countdown <= 2
      // is the first retained side-platform publication phase.
      // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_UnlockECB}
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044838_Floor}
      // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
      continue;
    }
    if (g->lines[(size_t)out_line_idx].is_ledge &&
        (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
        batch->state.seed_prev_action_frame[idx] >= 2 &&
        batch->state.seed_prev_action_frame[idx] <= 4) {
      // Fresh JumpAerial -> EscapeAir ledge-floor contacts are still owned by the pre-entry
      // CollData/ECB lifetime. Do not let the platform-root projection helper consume the ledge as
      // a soft-platform floor before the same callback reaches the source EscapeAir row.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
      //   ftCo_80099A58,ftCo_EscapeAir_Coll}
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
      continue;
    }
    float start_y_corr = 0.0f;
    const int start_line_idx =
        msl_mplib_8004dd90_floor(batch, bi, g, out_line_idx, root_x, batch->state.prev_pos_y[idx],
                                 &start_y_corr, NULL, NULL);
    const float prev_root_x = batch->state.floor_sweep_prev_pos_x[idx];
    const float prev_root_y = batch->state.floor_sweep_prev_pos_y[idx];
    const int sweep_start_line_idx = msl_mplib_8004dd90_floor(
        batch, bi, g, out_line_idx, prev_root_x, prev_root_y, NULL, NULL, NULL);
    // One-step reseeds can expose the callback's CollData.prev_pos through the explicit
    // floor_sweep_prev_pos lane while prev_pos_y already names the current frame-start root. Accept
    // either start-root view for the same source line, then keep the depth gates below as the owner
    // boundary.
    // refs/melee/src/melee/ft/ft_081B.c::ft_80081D0C
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754}
    const uint8_t start_line_matches =
        (start_line_idx >= 0 &&
         g->lines[(size_t)start_line_idx].segment_i == g->lines[(size_t)out_line_idx].segment_i)
            ? 1u
            : 0u;
    const uint8_t sweep_start_line_matches =
        (sweep_start_line_idx >= 0 && g->lines[(size_t)sweep_start_line_idx].segment_i ==
                                          g->lines[(size_t)out_line_idx].segment_i)
            ? 1u
            : 0u;
    float platform_line_y = 0.0f;
    const uint8_t platform_line_y_valid =
        floor_line_y_at_x_for_env(batch, bi, g, out_line_idx, root_x, &platform_line_y);
    const uint8_t fresh_jumpaerial_escapeair_entry =
        (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B)
            ? 1u
            : 0u;
    const MslCharParams* entry_chp = msl_char_params_fast(batch->state.char_id[idx]);
    const uint16_t entry_ground_id = batch->state.ground_id[idx];
    float entry_ground_y = 0.0f;
    const int entry_ground_line_idx = stage_collision_floor_line_index(stage_id, entry_ground_id);
    const uint8_t entry_ground_has_elevated_floor_y =
        (entry_ground_line_idx >= 0 &&
         floor_line_y_at_x_for_env(batch, bi, g, entry_ground_line_idx, root_x, &entry_ground_y) &&
         entry_ground_y > k_floor_y_bias)
            ? 1u
            : 0u;
    const uint8_t entry_ground_is_platform_domain =
        (entry_ground_id != 0xFFFFu &&
         (stage_collision_floor_line_is_platform(stage_id, entry_ground_id) ||
          stage_collision_floor_line_has_platform_transform(stage_id, entry_ground_id) ||
          entry_ground_has_elevated_floor_y))
            ? 1u
            : 0u;
    const uint8_t fresh_kneebend_jump_escapeair_entry =
        (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
         entry_chp != NULL &&
         (uint8_t)(batch->state.seed_prev_action_frame[idx] + 2) ==
             entry_chp->jump_startup_frames &&
         entry_ground_is_platform_domain && batch->state.action_frame[idx] <= 1)
            ? 1u
            : 0u;
    const uint8_t fresh_jump_escapeair_entry =
        (fresh_jumpaerial_escapeair_entry || fresh_kneebend_jump_escapeair_entry) ? 1u : 0u;
    const uint8_t sustained_escapeair =
        (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR) ? 1u : 0u;
    const uint8_t fresh_kneebend_same_entry_floor =
        (!fresh_kneebend_jump_escapeair_entry || out_line_idx == entry_ground_line_idx) ? 1u : 0u;
    const uint8_t fresh_kneebend_has_downward_entry_step =
        (!fresh_kneebend_jump_escapeair_entry || batch->state.speed_y_self[idx] < 0.0f) ? 1u : 0u;
    const uint8_t fresh_jump_platform_root_crossing_owner =
        // Fresh Jump/KneeBend -> EscapeAir can enter EscapeAir before Fighter_procMap, then
        // EscapeAir_Coll consumes the same callback's root/platform crossing. KneeBend is included
        // because ftCo_KneeBend_Anim first enters Jump, then Jump_IASA can enter EscapeAir before
        // the collision callback. This is the downward counterpart to the existing locked platform
        // root projection: if the callback root starts above a static platform and the current
        // vertical step crosses it, publish the platform floor even though the pre-step root
        // projection is a negative correction.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
        (platform_line_y_valid && fresh_jump_escapeair_entry && fresh_kneebend_same_entry_floor &&
         ecb_lock_timer_seed <= 2u && root_y > (platform_line_y + k_floor_y_bias) &&
         (root_y + batch->state.speed_y_self[idx]) <= (platform_line_y + k_floor_y_bias))
            ? 1u
            : 0u;
    const uint8_t fresh_jump_platform_below_crossing_owner =
        // Same fresh-entry owner after the root has already crossed below the platform line by the
        // time this collision helper runs. The previous/root line checks above prove the callback
        // crossed this platform; accept the upward projection while it is bounded by the current
        // vertical step rather than by the locked desired bottom. Terminal KneeBend rows that enter
        // Jump and then EscapeAir in the same callback use this high-lock first callback; zero-
        // bottom first-JumpAerial-frame entries can also use this root crossing while the CollData
        // lock countdown is high. Later carried JumpAerial frames remain with the existing
        // interpolation/airborne guard.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
        ((start_line_matches || sweep_start_line_matches) && fresh_jump_escapeair_entry &&
         fresh_kneebend_same_entry_floor && fresh_kneebend_has_downward_entry_step &&
         (ecb_lock_timer_seed <= 2u || fresh_kneebend_jump_escapeair_entry ||
          (fresh_jumpaerial_zero_bottom_entry && ecb_lock_timer_seed >= 4u)) &&
         y_corr > 0.0f && y_corr <= (fabsf(batch->state.speed_y_self[idx]) + k_ecb_vertical_unit))
            ? 1u
            : 0u;
    const float platform_owner_lift =
        fresh_jump_platform_root_crossing_owner
            ? ((platform_line_y + k_floor_y_bias) - root_y)
            : ((fresh_jump_platform_below_crossing_owner && fresh_jumpaerial_zero_bottom_entry &&
                ecb_lock_timer_seed >= 4u)
                   ? (y_corr + k_floor_y_bias)
                   : y_corr);
    if (!start_line_matches && !sweep_start_line_matches &&
        !fresh_jump_platform_root_crossing_owner) {
      continue;
    }
    const uint8_t start_root_depth_owner =
        (!locked_desired_bottom_owner && start_y_corr >= min_lift && start_y_corr <= max_lift) ? 1u
                                                                                               : 0u;
    uint8_t locked_desired_bottom_sweep_owner = 0u;
    uint8_t locked_zero_bottom_sweep_owner = 0u;
    if (locked_desired_bottom_owner) {
      // Source ft_80081D0C publishes `coll->last_pos` to mpColl_800471F8; one-step and rollout
      // paths preserve that callback-local root in the explicit floor-sweep lane. Use it for the
      // locked desired-bottom floor producer as well as for the loaded-ECB producer below, otherwise
      // same-callback JumpAerial/EscapeAir handoffs can miss a platform crossing when public
      // `prev_pos` has already advanced past the producer start.
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081D0C
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
      const float source_prev_root_x = isfinite(batch->state.floor_sweep_prev_pos_x[idx])
                                           ? batch->state.floor_sweep_prev_pos_x[idx]
                                           : batch->state.prev_pos_x[idx];
      const float source_prev_root_y = isfinite(batch->state.floor_sweep_prev_pos_y[idx])
                                           ? batch->state.floor_sweep_prev_pos_y[idx]
                                           : batch->state.prev_pos_y[idx];
      const float prev_bottom_x = source_prev_root_x;
      const float prev_bottom_y = source_prev_root_y + locked_desired_bottom_rel_y;
      const float cur_bottom_x = root_x;
      const float cur_bottom_y = root_y + locked_desired_bottom_rel_y;
      if (mpcoll_bottom_sweep_hits_segment(batch, idx, bi, g, stage_id, prev_bottom_x,
                                           prev_bottom_y, cur_bottom_x, cur_bottom_y,
                                           skip_platform_segment_i, out_line_idx, -1, c,
                                           g->lines[(size_t)out_line_idx].segment_i)) {
        locked_desired_bottom_sweep_owner = 1u;
      }
      if (fabsf(locked_desired_bottom_rel_y) <= k_floor_y_bias &&
          mpcoll_bottom_sweep_hits_segment(
              batch, idx, bi, g, stage_id, batch->state.prev_pos_x[idx],
              batch->state.prev_pos_y[idx], root_x, root_y, skip_platform_segment_i, out_line_idx,
              -1, c, g->lines[(size_t)out_line_idx].segment_i)) {
        locked_zero_bottom_sweep_owner = 1u;
      }
    }
    uint8_t locked_loaded_ecb_bottom_sweep_owner = 0u;
    if (!locked_desired_bottom_owner && (fresh_jump_escapeair_entry || sustained_escapeair) &&
        start_pose_bottom_rel_y > current_loaded_bottom_rel_y + k_floor_y_bias) {
      // EscapeAir_Coll consumes the CollData ECB loaded for the callback, not just the serialized
      // desired-bottom lane. In late locked callbacks the previous loaded ECB bottom can still be
      // above a soft platform while the current loaded bottom has collapsed back toward the root;
      // mpColl_80044628_Floor accepts that live bottom sweep before mpColl_80044838_Floor projects
      // the root. This is the generic ft_80082C74/mpColl_800471F8 platform owner and covers both
      // fresh JumpAerial -> EscapeAir and sustained EscapeAir callbacks without a replay-specific
      // action-frame cap.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
      // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
      // refs/melee/src/melee/mp/mpcoll.c::{
      //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor,mpColl_80044838_Floor}
      const float source_prev_root_x = isfinite(batch->state.floor_sweep_prev_pos_x[idx])
                                           ? batch->state.floor_sweep_prev_pos_x[idx]
                                           : batch->state.prev_pos_x[idx];
      const float source_prev_root_y = isfinite(batch->state.floor_sweep_prev_pos_y[idx])
                                           ? batch->state.floor_sweep_prev_pos_y[idx]
                                           : batch->state.prev_pos_y[idx];
      if (mpcoll_bottom_sweep_hits_segment(batch, idx, bi, g, stage_id, source_prev_root_x,
                                           source_prev_root_y + start_pose_bottom_rel_y, root_x,
                                           root_y + current_loaded_bottom_rel_y,
                                           skip_platform_segment_i, out_line_idx, -1, c,
                                           g->lines[(size_t)out_line_idx].segment_i)) {
        locked_loaded_ecb_bottom_sweep_owner = 1u;
      }
    }

    const float distinct_platform_bottom_rel_y =
        (batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
         msl_escapeair_locked_bottom_owner_any(
             batch->state.coll_desired_ecb_bottom_locked_owner[idx]))
            ? batch->state.coll_desired_ecb_bottom_rel_y[idx]
            : current_pose_bottom_rel_y;
    const uint8_t distinct_platform_root_depth_owner =
        // Sustained EscapeAir can also resolve a different static soft-platform segment during the
        // lock window. That platform is a new floor candidate, not the carried CollData.floor.index
        // segment protected by the same-platform lifetime guards below. During
        // CollData_X130_Locked, `mpColl_LoadECB_inline` preserves the desired bottom; once the
        // current root is below the accepted platform by that source bottom depth it belongs to the
        // ordinary ft_80082C74/mpColl_800471F8 floor publication path.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{
        //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
        (g->lines[(size_t)out_line_idx].is_platform &&
         g->lines[(size_t)out_line_idx].segment_i != batch->state.ground_id[idx] &&
         !stage_collision_floor_line_has_platform_transform(
             stage_id, g->lines[(size_t)out_line_idx].segment_i) &&
         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
         batch->state.seed_prev_action_frame[idx] >= 2 && ecb_lock_timer_seed <= 5u &&
         y_corr >= distinct_platform_bottom_rel_y && y_corr <= current_pose_top_rel_y)
            ? 1u
            : 0u;
    const uint8_t suppress_large_distinct_platform_root_projection =
        // Desired-bottom precondition for oversized distinct-platform root snaps:
        // mpColl_80044838_Floor(ignore_bottom=true) is the consumer after
        // mpColl_80044628_Floor has accepted a callback-local bottom-floor hit. If the preserved
        // desired bottom did not sweep across the new platform and the root correction is larger than
        // the desired-bottom extent plus this frame's vertical step, the root projection is an
        // endpoint/projection artifact rather than a source floor result. Ordinary same-step platform
        // handoffs and later lock phases stay on the normal EscapeAir_Coll owner.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
        (g->lines[(size_t)out_line_idx].is_platform &&
         g->lines[(size_t)out_line_idx].segment_i != batch->state.ground_id[idx] &&
         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
         ecb_lock_timer_seed == 6u && batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
         msl_escapeair_locked_bottom_owner_any(
             batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
         !locked_desired_bottom_sweep_owner &&
         y_corr > (locked_desired_bottom_rel_y + fabsf(batch->state.speed_y_self[idx]) +
                   k_ecb_vertical_unit))
            ? 1u
            : 0u;
    const uint8_t current_root_depth_owner =
        // Same source callback owner as the start-root gate, but for rows where the platform line is
        // inside the current post-Phys EscapeAir ECB envelope while CollData_X130_Locked is still in
        // the handoff phase. Earlier lock frames remain with the interpolation-gap suppression path;
        // later same-platform pass-through frames are owned by the final floor publication guard.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
        //
        // Lock countdowns 7 through 5 are the late above-root phase where the current root can already
        // be below the accepted platform by the loaded EscapeAir bottom depth. Oversized
        // distinct-platform projections still require the desired-bottom floor precondition above.
        (batch->state.action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
         !suppress_large_distinct_platform_root_projection &&
         (fresh_jump_platform_root_crossing_owner || distinct_platform_root_depth_owner ||
          fresh_jump_platform_below_crossing_owner || locked_zero_bottom_sweep_owner ||
          locked_loaded_ecb_bottom_sweep_owner ||
          (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
           ((ecb_lock_timer_seed >= 5u && ecb_lock_timer_seed <= 7u &&
             y_corr >= current_pose_bottom_rel_y && y_corr <= current_pose_top_rel_y) ||
            (locked_desired_bottom_owner && ecb_lock_timer_seed <= 5u &&
             locked_desired_bottom_sweep_owner)))))
            ? 1u
            : 0u;
    if (!start_root_depth_owner && !current_root_depth_owner) {
      continue;
    }
    if (!found || platform_owner_lift < best_lift ||
        (platform_owner_lift == best_lift &&
         g->lines[(size_t)out_line_idx].segment_i < g->lines[(size_t)best_line_idx].segment_i)) {
      found = 1u;
      best_lift = platform_owner_lift;
      best_line_idx = out_line_idx;
      best_nx = nx;
      best_ny = ny;
    }
  }

  if (!found || best_line_idx < 0) {
    return 0u;
  }

  batch->state.pos_y[idx] += best_lift;
  *ground_id_out = g->lines[(size_t)best_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = root_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = root_y + best_lift - k_floor_y_bias;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = best_nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = best_ny;
  }
  return 1u;
}

static uint8_t escapeair_locked_hard_floor_zero_bottom_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    int prefer_line_idx, uint16_t skip_platform_segment_i, uint8_t locked_desired_bottom_owner,
    float locked_desired_bottom_rel_y, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || ground_id_out == NULL || c == NULL || prefer_line_idx < 0 ||
      (size_t)prefer_line_idx >= g->line_count || locked_desired_bottom_owner == 0u ||
      fabsf(locked_desired_bottom_rel_y) > k_floor_y_bias) {
    return 0u;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR ||
      (batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
       batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_B) ||
      batch->state.seed_prev_action_frame[idx] > 2) {
    return 0u;
  }
  if (g->lines[(size_t)prefer_line_idx].is_platform || g->lines[(size_t)prefer_line_idx].is_ledge ||
      stage_collision_floor_line_has_platform_transform(
          stage_id, g->lines[(size_t)prefer_line_idx].segment_i) ||
      !floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx)) {
    return 0u;
  }

  // Same-frame JumpAerial -> EscapeAir hard-floor handoff:
  // JumpAerial_IASA can change to EscapeAir before Fighter_procMap, and EscapeAir_Coll then runs
  // ft_80082C74 on the entered state while CollData_X130_Locked preserves the desired bottom at
  // zero. For hard floors this source path is a root-vs-floor sweep into
  // mpColl_80044838_Floor(ignore_bottom=true), not the soft-platform lifetime handled by
  // escapeair_locked_platform_root_projection. Keep the owner on non-platform/non-ledge floor.index
  // rows and require the explicit floor_sweep_prev_pos -> current-root crossing so sustained
  // EscapeAir and Yoshi ledge/platform remaps stay on their separate owners.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_LoadECB_inline,mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
  MslMpcollFloorSweepResult floor_sweep = {0};
  if (!mpcoll_collect_bottom_sweep_hit(
          batch, idx, bi, g, stage_id, batch->state.floor_sweep_prev_pos_x[idx],
          batch->state.floor_sweep_prev_pos_y[idx], batch->state.pos_x[idx],
          batch->state.pos_y[idx], skip_platform_segment_i, prefer_line_idx, -1, c, &floor_sweep) ||
      floor_sweep.hit_segment_id != g->lines[(size_t)prefer_line_idx].segment_i) {
    return 0u;
  }
  const int hit_line_idx = floor_sweep.hit_line_idx;

  float y_corr = 0.0f;
  float nx = 0.0f;
  float ny = 1.0f;
  const int out_line_idx =
      msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, batch->state.pos_x[idx],
                               batch->state.pos_y[idx], &y_corr, &nx, &ny);
  if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count ||
      g->lines[(size_t)out_line_idx].segment_i != g->lines[(size_t)prefer_line_idx].segment_i ||
      y_corr < 0.0f || y_corr > (fabsf(batch->state.speed_y_self[idx]) + k_ecb_vertical_unit)) {
    return 0u;
  }

  batch->state.pos_y[idx] += y_corr;
  *ground_id_out = g->lines[(size_t)out_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = batch->state.pos_x[idx];
  }
  if (contact_y_out != NULL) {
    *contact_y_out = batch->state.pos_y[idx] - k_floor_y_bias;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = ny;
  }
  return 1u;
}

uint8_t msl_mpcoll_800471f8_escapeair_locked_root_publication(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint8_t escapeair_locked, uint8_t ecb_lock_timer, uint8_t ecb_lock_timer_seed,
    uint8_t stage_has_height_platform_transform, const MslEcbWorldPoints* prev_ecb,
    const MslEcbWorldPoints* cur_ecb, uint16_t skip_platform_segment_i, int prefer_line_idx,
    uint8_t char_id, uint32_t anim, uint16_t ecb_frame_cur, uint8_t locked_desired_ecb_bottom_valid,
    const MslCommonParams* c, MslMpcollFloorContact* out, uint8_t* platform_root_hit_out) {
  if (batch == NULL || g == NULL || cur_ecb == NULL || out == NULL || c == NULL ||
      escapeair_locked == 0u || batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR) {
    return 0u;
  }
  const uint16_t prev_action_id = batch->state.prev_action_id[idx];
  const uint8_t jump_platform_root_owner =
      ((prev_action_id == (uint16_t)MSL_ACT_JUMP_F || prev_action_id == (uint16_t)MSL_ACT_JUMP_B ||
        prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
        prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
        batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
        batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B ||
        batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
        batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B))
          ? 1u
          : 0u;
  const int callback_pose_frame =
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
       batch->state.seed_prev_action_frame[idx] >= 0)
          ? ((int)batch->state.seed_prev_action_frame[idx] + 1)
          : batch->state.action_frame[idx];
  const uint8_t locked_desired_root_owner =
      (locked_desired_ecb_bottom_valid && prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) ? 1u : 0u;
  const uint8_t sustained_static_platform_root_owner =
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR) ? 1u : 0u;

  uint16_t ground_id = 0xFFFFu;
  float contact_x = 0.0f;
  float contact_y = 0.0f;
  float floor_nx = 0.0f;
  float floor_ny = 1.0f;
  if ((ecb_lock_timer >= 4u || jump_platform_root_owner || locked_desired_root_owner ||
       sustained_static_platform_root_owner) &&
      (batch->state.speed_y_self[idx] < 0.0f || jump_platform_root_owner) &&
      escapeair_locked_platform_root_projection(
          batch, idx, bi, g, stage_id, skip_platform_segment_i, ecb_lock_timer_seed,
          msl_ecb_bottom_rel_y(char_id, anim, (int)ecb_frame_cur),
          msl_ecb_bottom_rel_y(char_id, anim, callback_pose_frame), cur_ecb->bottom_rel_y,
          msl_ecb_top_rel_y(char_id, anim, callback_pose_frame), locked_desired_root_owner,
          batch->state.coll_desired_ecb_bottom_rel_y[idx], &ground_id, &contact_x, &contact_y,
          &floor_nx, &floor_ny, c)) {
    if (platform_root_hit_out != NULL) {
      *platform_root_hit_out = 1u;
    }
  } else if (escapeair_locked_floor_bottom_sweep_root_projection(
                 batch, idx, bi, g, stage_id, stage_has_height_platform_transform, prev_ecb,
                 skip_platform_segment_i, c, &ground_id, &contact_x, &contact_y, &floor_nx,
                 &floor_ny)) {
    if (platform_root_hit_out != NULL) {
      *platform_root_hit_out = 1u;
    }
  } else if (prefer_line_idx >= 0 && batch->state.speed_y_self[idx] < 0.0f &&
             escapeair_locked_hard_floor_zero_bottom_root_projection(
                 batch, idx, bi, g, stage_id, prefer_line_idx, skip_platform_segment_i,
                 locked_desired_root_owner, batch->state.coll_desired_ecb_bottom_rel_y[idx], c,
                 &ground_id, &contact_x, &contact_y, &floor_nx, &floor_ny)) {
  } else {
    return 0u;
  }

  *out = (MslMpcollFloorContact){
      .ground_id = ground_id,
      .contact_x = contact_x,
      .contact_y = contact_y,
      .normal_x = floor_nx,
      .normal_y = floor_ny,
  };
  // Locked-root publication packet for EscapeAir_Coll's ft_80082C74 -> mpColl_800471F8 path.
  // Platform root projection, locked bottom sweep, and zero-bottom hard-floor root projection are
  // the same source callback surface with different CollData_X130/ECB inputs.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_LoadECB_inline,mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
  return 1u;
}

static uint8_t mpcoll_throw_release_root_floor_sweep_step(const MslBatch* batch, size_t idx, int bi,
                                                          const MslStageFloorGraph* g,
                                                          uint32_t stage_id, float prev_root_x,
                                                          float prev_root_y, float cur_root_x,
                                                          float cur_root_y, uint16_t skip_segment_i,
                                                          MslMpcollFloorMaskResult* out) {
  if (batch == NULL || g == NULL || g->lines == NULL || g->line_count == 0) {
    return 0u;
  }
  const MslCommonParams* c = msl_common_params();
  MslMpcollFloorSweepResult floor_sweep = {0};
  if (!mpcoll_collect_bottom_sweep_floor_result(batch, idx, bi, g, stage_id, prev_root_x,
                                                prev_root_y, cur_root_x, cur_root_y, skip_segment_i,
                                                -1, -1, c, &floor_sweep)) {
    return 0u;
  }
  int out_line_idx = floor_sweep.projected_line_idx;
  if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count ||
      floor_sweep.projected_y_corr < 0.0f ||
      !floor_line_is_runtime_fighter_solid(g, stage_id, out_line_idx)) {
    return 0u;
  }
  const uint16_t projected_segment_i = g->lines[(size_t)out_line_idx].segment_i;
  const uint8_t projected_is_platform = g->lines[(size_t)out_line_idx].is_platform ? 1u : 0u;
  if (projected_is_platform != 0u && (batch->state.ground_id[idx] != projected_segment_i ||
                                      skip_segment_i == projected_segment_i)) {
    // Throw release `ftCo_800DDDE4` publishes the released fighter through mpColl_800471F8. For
    // soft platforms, keep that publication to the already-carried CollData.floor segment rather
    // than admitting arbitrary platform crossings from the x1A70 release vector.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754}
    return 0u;
  }

  if (out != NULL) {
    out->ground_id = projected_segment_i;
    out->corrected_pos_y = cur_root_y + floor_sweep.projected_y_corr;
    out->corrected_pos_x = cur_root_x;
  }
  return 1u;
}

static uint8_t mpcoll_floor_mask_probe_with_bottom_mode(const MslBatch* batch, size_t idx,
                                                        uint8_t lock_bottom_to_zero,
                                                        MslMpcollFloorMaskResult* out) {
  if (batch == NULL) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL) {
    return 0u;
  }

  // Reconstruct the shared floor-mask decision after mpCollPrev + mpColl_LoadECB_inline. Flags=6
  // retains the extracted ECB bottom (800477E0); flags=5 pins bottom.y to zero (80048654).
  // Callers decide whether and how to apply the owning motion-state callback.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082578,ft_80083C00}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_800477E0,mpColl_80048654}
  const uint8_t char_id = batch->state.char_id[idx];
  const uint32_t anim = batch->state.animation_index[idx];
  const uint16_t ecb_frame = msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
  const uint16_t ecb_frame_prev = msl_ecb_prev_frame_u16(ecb_frame);

  MslEcbBottomWorldPoint cur_bot = {0};
  MslEcbBottomWorldPoint prev_bot = {0};
  msl_ecb_bottom_world_point_sample(&cur_bot, char_id, anim, ecb_frame, batch->state.pos_x[idx],
                                    batch->state.pos_y[idx], lock_bottom_to_zero);
  msl_ecb_bottom_world_point_sample(&prev_bot, char_id, anim, ecb_frame_prev,
                                    batch->state.floor_sweep_prev_pos_x[idx],
                                    batch->state.floor_sweep_prev_pos_y[idx], lock_bottom_to_zero);

  const uint16_t skip_platform_segment_i = platform_floor_skip_segment_id(batch, idx, stage_id);
  const MslCommonParams* c = msl_common_params();
  MslMpcollFloorSweepResult floor_sweep = {0};
  if (cur_bot.y <= prev_bot.y && mpcoll_collect_bottom_sweep_floor_result(
                                     batch, idx, bi, g, stage_id, prev_bot.x, prev_bot.y, cur_bot.x,
                                     cur_bot.y, skip_platform_segment_i, -1, -1, c, &floor_sweep)) {
    const int out_line_idx = floor_sweep.projected_line_idx;
    if (out_line_idx >= 0) {
      if (out != NULL) {
        out->ground_id = g->lines[(size_t)out_line_idx].segment_i;
        out->corrected_pos_y = batch->state.pos_y[idx] + floor_sweep.projected_y_corr;
        out->corrected_pos_x = batch->state.pos_x[idx];
      }
      return 1u;
    }
    if (out != NULL) {
      out->ground_id = floor_sweep.hit_segment_id;
      out->corrected_pos_y =
          batch->state.pos_y[idx] + (floor_sweep.hit_y - cur_bot.y) + k_floor_y_bias;
      out->corrected_pos_x = batch->state.pos_x[idx];
    }
    return 1u;
  }

  {
    uint8_t found = 0u;
    float best_y_corr = FLT_MAX;
    int best_line_idx = -1;
    float y_corr = 0.0f;
    for (size_t li = 0; li < g->line_count; li++) {
      if (!floor_line_is_runtime_fighter_solid(g, stage_id, (int)li)) {
        continue;
      }
      if (g->lines[li].is_platform) {
        continue;
      }
      const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, (int)li, cur_bot.x, cur_bot.y,
                                                        &y_corr, NULL, NULL);
      if (out_line_idx >= 0 && y_corr >= 0.0f && (!found || y_corr < best_y_corr)) {
        found = 1u;
        best_y_corr = y_corr;
        best_line_idx = out_line_idx;
      }
    }
    if (found && best_line_idx >= 0) {
      if (out != NULL) {
        out->ground_id = g->lines[(size_t)best_line_idx].segment_i;
        out->corrected_pos_y = batch->state.pos_y[idx] + best_y_corr;
        out->corrected_pos_x = batch->state.pos_x[idx];
      }
      return 1u;
    }
  }

  return 0u;
}

uint8_t mpcoll_800477e0_floor_mask_probe(const MslBatch* batch, size_t idx,
                                         MslMpcollFloorMaskResult* out) {
  return mpcoll_floor_mask_probe_with_bottom_mode(batch, idx, 0u, out);
}

uint8_t mpcoll_80048654_floor_mask_probe(const MslBatch* batch, size_t idx,
                                         MslMpcollFloorMaskResult* out) {
  return mpcoll_floor_mask_probe_with_bottom_mode(batch, idx, 1u, out);
}

uint8_t mpcoll_800477e0_capture_root_floor_mask_probe(const MslBatch* batch, size_t idx,
                                                      MslMpcollFloorMaskResult* out) {
  if (batch == NULL) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL || g->lines == NULL || g->line_count == 0) {
    return 0u;
  }

  // CapturePulledLw immediate Coll callback owner:
  // - fn_800DB230 switches Lw -> Hi through ftCommon_8007D5D4 / ftCommon_UnlockECB, applies
  //   fn_800DAA40's root translation, then calls ft_80083C00.
  // - ft_80083C00 snapshots `fp->cur_pos` into CollData before mpColl_800477E0.
  // - For grounded capture victims, CollData.floor.index is still the authoritative nearby floor;
  //   the immediate Hi root can sit below that floor even when extracted ECB-bottom data is above
  //   it, so a bottom-only sweep misses a real floor-mask result.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DB230,fn_800DAECC}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083C00,ft_80082578}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800477E0
  int line_idx = -1;
  const uint16_t ground_id = batch->state.ground_id[idx];
  const uint16_t skip_platform_segment_i = platform_floor_skip_segment_id(batch, idx, stage_id);
  if (ground_id != 0xFFFFu) {
    if (ground_id == skip_platform_segment_i) {
      // Source floor-skip ownership:
      // - Pass / shield-drop writes CollData.floor_skip through mpUpdateFloorSkip.
      // - mpColl_800477E0 passes that skip into the floor query, so the stale platform
      //   CollData.floor.index must not be reused by this capture-root reconstruction.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A184,ftCo_8009A228}
      // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_800477E0}
      line_idx = -1;
    } else {
      line_idx = stage_collision_floor_line_index(stage_id, ground_id);
      if (!floor_line_is_runtime_fighter_solid(g, stage_id, line_idx)) {
        line_idx = -1;
      }
    }
  }

  float y_corr = 0.0f;
  int out_line_idx = -1;
  if (line_idx >= 0) {
    out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, line_idx, batch->state.pos_x[idx],
                                            batch->state.pos_y[idx], &y_corr, NULL, NULL);
    if (out_line_idx >= 0 && y_corr >= 0.0f) {
      if (out != NULL) {
        out->ground_id = g->lines[(size_t)out_line_idx].segment_i;
        out->corrected_pos_y = batch->state.pos_y[idx] + y_corr;
        out->corrected_pos_x = batch->state.pos_x[idx];
      }
      return 1u;
    }
  }

  uint8_t found = 0u;
  float best_y_corr = FLT_MAX;
  int best_line_idx = -1;
  for (size_t li = 0; li < g->line_count; li++) {
    if (!floor_line_is_runtime_fighter_solid(g, stage_id, (int)li)) {
      continue;
    }
    if (g->lines[li].is_platform) {
      continue;
    }
    out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, (int)li, batch->state.pos_x[idx],
                                            batch->state.pos_y[idx], &y_corr, NULL, NULL);
    if (out_line_idx >= 0 && y_corr >= 0.0f && (!found || y_corr < best_y_corr)) {
      found = 1u;
      best_y_corr = y_corr;
      best_line_idx = out_line_idx;
    }
  }
  if (found && best_line_idx >= 0) {
    if (out != NULL) {
      out->ground_id = g->lines[(size_t)best_line_idx].segment_i;
      out->corrected_pos_y = batch->state.pos_y[idx] + best_y_corr;
      out->corrected_pos_x = batch->state.pos_x[idx];
    }
    return 1u;
  }
  return 0u;
}

uint8_t mpcoll_800471f8_throw_release_root_floor_probe(const MslBatch* batch, size_t idx,
                                                       float source_last_x, float source_last_y,
                                                       MslMpcollFloorMaskResult* out) {
  if (batch == NULL || !isfinite(source_last_x) || !isfinite(source_last_y)) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL || g->lines == NULL || g->line_count == 0) {
    return 0u;
  }

  // ftCo_800DDDE4 sets CollData.last_pos from the selected sample owner and CollData.cur_pos from
  // the x1A70 release vector, then calls mpColl_800471F8. Source probe evidence across Marth
  // ThrowF/CaptureCut rows shows the published root comes from mpColl_80043754's intermediate
  // floor-hit substep, not from the raw x1A70 target.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754,mpColl_80046904}
  const float target_x = batch->state.pos_x[idx];
  const float target_y = batch->state.pos_y[idx];
  if (!isfinite(target_x) || !isfinite(target_y)) {
    return 0u;
  }
  const float dx = target_x - source_last_x;
  const float dy = target_y - source_last_y;
  float max_delta = fabsf(dx);
  if (fabsf(dy) > max_delta) {
    max_delta = fabsf(dy);
  }
  int steps = 1;
  if (max_delta > k_mpcoll_substep_max_delta) {
    steps = (int)(max_delta / k_mpcoll_substep_max_delta) + 1;
  }
  if (steps < 1) {
    steps = 1;
  }
  const float step_x = dx / (float)steps;
  const float step_y = dy / (float)steps;
  const uint16_t skip_segment_i = platform_floor_skip_segment_id(batch, idx, stage_id);

  float prev_root_x = source_last_x;
  float prev_root_y = source_last_y;
  for (int step = 0; step < steps; step++) {
    const float cur_root_x = prev_root_x + step_x;
    const float cur_root_y = prev_root_y + step_y;
    if (mpcoll_throw_release_root_floor_sweep_step(batch, idx, bi, g, stage_id, prev_root_x,
                                                   prev_root_y, cur_root_x, cur_root_y,
                                                   skip_segment_i, out)) {
      return 1u;
    }
    prev_root_x = cur_root_x;
    prev_root_y = cur_root_y;
  }
  return 0u;
}

uint8_t mpcoll_dc920_connected_floor_attempt(const MslBatch* batch, size_t constrained_idx,
                                             uint16_t sample_owner_ground_id,
                                             MslMpcollFloorMaskResult* out) {
  if (batch == NULL || sample_owner_ground_id == 0xFFFFu) {
    return 0u;
  }
  const int bi = (int)(constrained_idx / (size_t)MSL_MAX_PLAYERS);
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  const MslCommonParams* c = msl_common_params();
  const int source_line = stage_collision_floor_line_index(stage_id, sample_owner_ground_id);
  if (g == NULL || g->lines == NULL || source_line < 0 || (size_t)source_line >= g->line_count ||
      c == NULL) {
    return 0u;
  }

  // mpLib_8005199C_Floor returns the first source-ordered floor below the point. DC920 checks
  // connectivity only after that selection; it must not skip a disconnected first floor and hunt
  // for a later connected candidate.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8005199C_Floor
  int candidate_line = -1;
  for (size_t li = 0; li < g->line_count; li++) {
    const uint16_t segment_i = g->lines[li].segment_i;
    if (!stage_collision_floor_line_is_runtime_fighter_solid(stage_id, segment_i)) {
      continue;
    }
    float floor_y = 0.0f;
    if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, batch->state.pos_x[constrained_idx],
                                   &floor_y) ||
        batch->state.pos_y[constrained_idx] < floor_y) {
      continue;
    }
    candidate_line = (int)li;
    break;
  }
  if (candidate_line < 0 || !floor_lines_connected(g, source_line, candidate_line)) {
    return 0u;
  }

  if (out != NULL) {
    out->candidate_ground_id = g->lines[(size_t)candidate_line].segment_i;
    out->candidate_published = 1u;
  }

  float correction = 0.0f;
  const int projected_line =
      msl_mplib_8004dd90_floor(batch, bi, g, candidate_line, batch->state.pos_x[constrained_idx],
                               batch->state.pos_y[constrained_idx], &correction, NULL, NULL);
  if (projected_line < 0 || correction < c->capture_release_floor_tolerance) {
    return 0u;
  }
  if (out != NULL) {
    out->ground_id = g->lines[(size_t)projected_line].segment_i;
    out->corrected_pos_x = batch->state.pos_x[constrained_idx];
    out->corrected_pos_y = batch->state.pos_y[constrained_idx] + correction;
  }
  return 1u;
}

// Ground collision is organized around the same source wrapper families used by the decomp:
// - grounded allow-ground-to-air persistence: ft_80082708 -> mpColl_8004B108
// - common air/Fall/FallSpecial landing: ft_80083090/ft_800831CC -> mpColl_80047E14
// - airborne stay-airborne floor handoff: ft_80082C74 -> mpColl_800471F8/mpColl_800473CC
// Keep helper extraction inside those owners. Avoid broad per-line facts caches here: the source
// routines ask narrow mpLib/mpColl questions at the point where the callback needs them.
// refs/melee/src/melee/ft/ft_081B.c
// refs/melee/src/melee/mp/mpcoll.c

void mpcoll_floor_probe_clear(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.coll_floor_probe_valid[idx] = 0u;
  batch->state.coll_floor_probe_owner[idx] = (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_NONE;
  batch->state.coll_floor_probe_reject_reason[idx] = (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NONE;
  batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] = 0u;
  batch->state.coll_floor_probe_projection_hit[idx] = 0u;
  batch->state.coll_floor_probe_carried_source_owned[idx] = 0u;
  batch->state.coll_floor_probe_carried_runtime_owned[idx] = 0u;
  batch->state.coll_floor_probe_reject_bits[idx] = 0u;
  batch->state.coll_floor_probe_source_phases[idx] = 0u;
  batch->state.coll_floor_probe_carried_segment_id[idx] = 0xFFFFu;
  batch->state.coll_floor_probe_candidate_segment_id[idx] = 0xFFFFu;
  batch->state.coll_floor_probe_projected_segment_id[idx] = 0xFFFFu;
  batch->state.coll_floor_probe_candidate_line_idx[idx] = -1;
  batch->state.coll_floor_probe_projected_line_idx[idx] = -1;
  batch->state.coll_floor_probe_prev_bottom_x[idx] = 0.0f;
  batch->state.coll_floor_probe_prev_bottom_y[idx] = 0.0f;
  batch->state.coll_floor_probe_cur_bottom_x[idx] = 0.0f;
  batch->state.coll_floor_probe_cur_bottom_y[idx] = 0.0f;
}

void mpcoll_floor_probe_begin(const MslMpcollContext* ctx, uint8_t owner,
                              MslMpcollSourcePhases source_phases, int candidate_line_idx,
                              uint8_t reject_reason) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  if (owner == (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_47E14 &&
      batch->state.coll_floor_probe_valid[idx] != 0u &&
      batch->state.coll_floor_probe_owner[idx] == (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_473CC) {
    return;
  }
  batch->state.coll_floor_probe_valid[idx] = 1u;
  batch->state.coll_floor_probe_owner[idx] = owner;
  batch->state.coll_floor_probe_reject_reason[idx] = reject_reason;
  batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] = 0u;
  batch->state.coll_floor_probe_projection_hit[idx] = 0u;
  batch->state.coll_floor_probe_source_phases[idx] = (uint32_t)source_phases;
  batch->state.coll_floor_probe_carried_segment_id[idx] = batch->state.ground_id[idx];
  batch->state.coll_floor_probe_carried_source_owned[idx] =
      batch->state.floor_sweep_prev_source_owned[idx] ? 1u : 0u;
  batch->state.coll_floor_probe_carried_runtime_owned[idx] =
      batch->state.floor_sweep_prev_runtime_owned[idx] ? 1u : 0u;
  batch->state.coll_floor_probe_reject_bits[idx] = 0u;
  batch->state.coll_floor_probe_candidate_line_idx[idx] = (int16_t)candidate_line_idx;
  batch->state.coll_floor_probe_candidate_segment_id[idx] =
      mpcoll_probe_segment_for_line(ctx->floor_graph, candidate_line_idx);
  batch->state.coll_floor_probe_projected_line_idx[idx] = -1;
  batch->state.coll_floor_probe_projected_segment_id[idx] = 0xFFFFu;
}

void mpcoll_floor_probe_lines(const MslMpcollContext* ctx, int candidate_line_idx,
                              int projected_line_idx) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  if (candidate_line_idx >= 0) {
    batch->state.coll_floor_probe_candidate_line_idx[idx] = (int16_t)candidate_line_idx;
    batch->state.coll_floor_probe_candidate_segment_id[idx] =
        mpcoll_probe_segment_for_line(ctx->floor_graph, candidate_line_idx);
  }
  if (projected_line_idx >= 0) {
    batch->state.coll_floor_probe_projected_line_idx[idx] = (int16_t)projected_line_idx;
    batch->state.coll_floor_probe_projected_segment_id[idx] =
        mpcoll_probe_segment_for_line(ctx->floor_graph, projected_line_idx);
  }
}

void mpcoll_floor_probe_reject_bits(const MslMpcollContext* ctx, uint64_t bits,
                                    MslMpcollSourcePhases source_phases, int candidate_line_idx,
                                    int projected_line_idx) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  uint8_t owner = (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_NONE;
  if ((source_phases & (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_AIR_471F8) != 0u) {
    owner = (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_471F8;
  } else if ((source_phases & (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_AIR_477E0) != 0u) {
    owner = (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_477E0;
  } else if ((source_phases & (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_AIR_473CC) != 0u) {
    owner = (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_473CC;
  }
  if (owner != (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_NONE) {
    mpcoll_floor_probe_begin(ctx, owner, source_phases, candidate_line_idx,
                             (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION);
  }
  batch->state.coll_floor_probe_valid[idx] = 1u;
  batch->state.coll_floor_probe_reject_reason[idx] =
      (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION;
  batch->state.coll_floor_probe_reject_bits[idx] = bits;
  batch->state.coll_floor_probe_source_phases[idx] = (uint32_t)source_phases;
  mpcoll_floor_probe_lines(ctx, candidate_line_idx, projected_line_idx);
}

void mpcoll_floor_probe_bottom_interval(const MslMpcollContext* ctx, float prev_bottom_x,
                                        float prev_bottom_y, float cur_bottom_x,
                                        float cur_bottom_y) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  batch->state.coll_floor_probe_prev_bottom_x[idx] = prev_bottom_x;
  batch->state.coll_floor_probe_prev_bottom_y[idx] = prev_bottom_y;
  batch->state.coll_floor_probe_cur_bottom_x[idx] = cur_bottom_x;
  batch->state.coll_floor_probe_cur_bottom_y[idx] = cur_bottom_y;
}

void mpcoll_floor_probe_result(const MslMpcollContext* ctx, const MslMpcollFloorSweepResult* result,
                               uint8_t raw_hit, uint8_t projection_hit, uint8_t reject_reason) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  batch->state.coll_floor_probe_valid[idx] = 1u;
  batch->state.coll_floor_probe_reject_reason[idx] = reject_reason;
  batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] = raw_hit ? 1u : 0u;
  batch->state.coll_floor_probe_projection_hit[idx] = projection_hit ? 1u : 0u;
  if (result != NULL && result->hit_line_idx >= 0) {
    batch->state.coll_floor_probe_candidate_line_idx[idx] = (int16_t)result->hit_line_idx;
    batch->state.coll_floor_probe_candidate_segment_id[idx] = result->hit_segment_id;
  }
  if (result != NULL && result->projected_line_idx >= 0) {
    batch->state.coll_floor_probe_projected_line_idx[idx] = (int16_t)result->projected_line_idx;
    batch->state.coll_floor_probe_projected_segment_id[idx] = result->projected_segment_id;
  }
}

void mpcoll_discard_callback_floor_result(const MslMpcollContext* ctx) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  const uint8_t source = batch->state.coll_floor_result_source[idx];
  const uint8_t preserve_airborne_floorhug =
      (source == (uint8_t)MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE) ? 1u : 0u;
  if (!preserve_airborne_floorhug) {
    batch->state.coll_env_flags[idx] &= ~(uint32_t)MSL_COLLIDE_FLOOR_MASK;
  }
  if (batch->state.coll_floor_result_valid[idx] == 0u &&
      source == (uint8_t)MSL_MPCOLL_FLOOR_RESULT_NONE &&
      batch->state.coll_floor_result_mode[idx] == (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE) {
    return;
  }
  batch->state.coll_floor_result_valid[idx] = 0u;
  batch->state.coll_floor_result_source[idx] = (uint8_t)MSL_MPCOLL_FLOOR_RESULT_NONE;
  batch->state.coll_floor_result_mode[idx] = (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE;
}

void mpcoll_clear_callback_floor_result(const MslMpcollContext* ctx, float prev_x, float prev_y,
                                        float cur_x, float cur_y) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  mpcoll_discard_callback_floor_result(ctx);
  batch->state.coll_substep_prev_pos_x[idx] = prev_x;
  batch->state.coll_substep_prev_pos_y[idx] = prev_y;
  batch->state.coll_substep_cur_pos_x[idx] = cur_x;
  batch->state.coll_substep_cur_pos_y[idx] = cur_y;
  batch->state.coll_last_pos_x[idx] = prev_x;
  batch->state.coll_last_pos_y[idx] = prev_y;
}

static inline uint8_t mpcoll_default_floor_mode_for_source(uint8_t source) {
  switch (source) {
    case MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY:
      return (uint8_t)MSL_MPCOLL_FLOOR_MODE_4A908_RETRY;
    case MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE:
      return (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAY_AIRBORNE_PROJECTION;
    case MSL_MPCOLL_FLOOR_RESULT_DIRECT:
      return (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION;
    case MSL_MPCOLL_FLOOR_RESULT_NONE:
    default:
      return (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE;
  }
}

void mpcoll_record_callback_floor_result_with_mode(const MslMpcollContext* ctx, uint8_t source,
                                                   uint8_t mode, uint16_t segment_id,
                                                   float contact_x, float contact_y, float normal_x,
                                                   float normal_y) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  // Callback-local floor result scratch mirrors the source CollData floor hit before wrapper
  // writeback consumes it. Direct floor hits and mpColl_8004A908 retries both flow through this
  // lane so later publication reads a single source-shaped contact record.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044628_Floor,mpColl_8004A908_Floor}
  batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_FLOOR_MASK;
  batch->state.coll_floor_result_valid[idx] = 1u;
  batch->state.coll_floor_result_source[idx] = source;
  batch->state.coll_floor_result_mode[idx] = mode;
  batch->state.coll_floor_result_segment_id[idx] = segment_id;
  batch->state.coll_floor_result_contact_x[idx] = contact_x;
  batch->state.coll_floor_result_contact_y[idx] = contact_y;
  batch->state.coll_floor_result_normal_x[idx] = normal_x;
  batch->state.coll_floor_result_normal_y[idx] = normal_y;
}

void mpcoll_record_callback_floor_result(const MslMpcollContext* ctx, uint8_t source,
                                         uint16_t segment_id, float contact_x, float contact_y,
                                         float normal_x, float normal_y) {
  mpcoll_record_callback_floor_result_with_mode(
      ctx, source, mpcoll_default_floor_mode_for_source(source), segment_id, contact_x, contact_y,
      normal_x, normal_y);
}

void mpcoll_record_escapeair_floor_producer_runtime_authority(const MslMpcollContext* ctx) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  // Runtime-only source-authority lane for EscapeAir_Coll floor publication. This is deliberately
  // separate from replay-visible CollData.floor/ground_id and from reseeded desired-bottom lanes:
  // only the current live callback can write it. The map-collision callback snapshots the bit, then
  // clears it unless a live producer reasserts it for the next callback.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
  ctx->batch->state.coll_escapeair_floor_producer_runtime[ctx->idx] = 1u;
}

uint8_t mpcoll_callback_floor_result_valid(const MslMpcollContext* ctx) {
  if (ctx == NULL || ctx->batch == NULL) {
    return 0u;
  }
  return ctx->batch->state.coll_floor_result_valid[ctx->idx] ? 1u : 0u;
}

uint8_t mpcoll_floor_contact_from_callback_result(const MslMpcollContext* ctx,
                                                  MslMpcollFloorContact* io) {
  if (ctx == NULL || ctx->batch == NULL || io == NULL ||
      ctx->batch->state.coll_floor_result_valid[ctx->idx] == 0u) {
    return 0u;
  }
  const MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  io->ground_id = batch->state.coll_floor_result_segment_id[idx];
  io->contact_x = batch->state.coll_floor_result_contact_x[idx];
  io->contact_y = batch->state.coll_floor_result_contact_y[idx];
  io->normal_x = batch->state.coll_floor_result_normal_x[idx];
  io->normal_y = batch->state.coll_floor_result_normal_y[idx];
  return 1u;
}

void mpcoll_floor_reject_add_if_state(MslMpcollFloorRejectPacket* packet, uint8_t condition,
                                      uint64_t bit, MslMpcollFloorRejectRestore restore,
                                      uint32_t side_effects, uint32_t source_phases) {
  if (packet != NULL && condition) {
    packet->bits |= bit;
    packet->side_effects |= side_effects;
    packet->source_phases |= source_phases;
    if (packet->restore == (uint8_t)MSL_MPCOLL_FLOOR_REJECT_RESTORE_NONE) {
      packet->restore = (uint8_t)restore;
    }
  }
}

void mpcoll_floor_reject_add_escapeair_final_owners(
    MslMpcollFloorRejectPacket* packet, const MslEscapeAirFinalPublicationOwners* owners) {
  if (owners == NULL) {
    return;
  }
  const uint32_t phase = (uint32_t)MSL_MPCOLL_PHASE_AIR_471F8;
  mpcoll_floor_reject_add_if_state(packet, owners->sustained_same_platform_lock,
                                   MSL_MPCOLL_REJECT_SUSTAINED_ESCAPEAIR_SAME_PLATFORM_LOCK,
                                   MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_BOTTOM_TO_ROOT_REL, 0u,
                                   phase);
  mpcoll_floor_reject_add_if_state(packet, owners->sustained_same_ledge_lock,
                                   MSL_MPCOLL_REJECT_SUSTAINED_ESCAPEAIR_SAME_LEDGE_LOCK,
                                   MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_BOTTOM_TO_ROOT_REL, 0u,
                                   phase);
  mpcoll_floor_reject_add_if_state(packet, owners->locked_missing_bottom_owner,
                                   MSL_MPCOLL_REJECT_LOCKED_ESCAPEAIR_MISSING_BOTTOM_OWNER,
                                   MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u, phase);
  mpcoll_floor_reject_add_if_state(packet, owners->locked_desired_platform_without_bottom_sweep,
                                   MSL_MPCOLL_REJECT_LOCKED_DESIRED_PLATFORM_WITHOUT_BOTTOM_SWEEP,
                                   MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u, phase);
  mpcoll_floor_reject_add_if_state(
      packet, owners->locked_desired_nonplatform_without_bottom_sweep,
      MSL_MPCOLL_REJECT_LOCKED_DESIRED_NONPLATFORM_WITHOUT_BOTTOM_SWEEP,
      MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u, phase);
  mpcoll_floor_reject_add_if_state(
      packet, owners->escapeair_jumpaerial_soft_owner_early_direct_land,
      MSL_MPCOLL_REJECT_ESCAPEAIR_JUMPAERIAL_SOFT_OWNER_EARLY_DIRECT_LAND,
      MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u, phase);
  mpcoll_floor_reject_add_if_state(packet, owners->kneebend_ledge_missing_owner,
                                   MSL_MPCOLL_REJECT_KNEEBEND_ESCAPEAIR_SLOPE,
                                   MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u, phase);
  mpcoll_floor_reject_add_if_state(packet, owners->kneebend_static_platform_lock,
                                   MSL_MPCOLL_REJECT_KNEEBEND_ESCAPEAIR_STATIC_PLATFORM_LOCK,
                                   MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u, phase);
  mpcoll_floor_reject_add_if_state(packet, owners->jumpaerial_high_lift_ledge,
                                   MSL_MPCOLL_REJECT_JUMPAERIAL_ESCAPEAIR_HIGH_LIFT_LEDGE,
                                   MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_BOTTOM_TO_ROOT_REL, 0u,
                                   phase);
  mpcoll_floor_reject_add_if_state(packet, owners->jumpaerial_static_platform_overstep,
                                   MSL_MPCOLL_REJECT_JUMPAERIAL_ESCAPEAIR_STATIC_PLATFORM_OVERSTEP,
                                   MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_BOTTOM_TO_ROOT_REL, 0u,
                                   phase);
  mpcoll_floor_reject_add_if_state(packet, owners->cliff_ledge_locked,
                                   MSL_MPCOLL_REJECT_CLIFF_HORIZONTAL_LEDGE_LOCKED,
                                   MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u, phase);
}

static inline void mpcoll_init_floor_sweep_result(MslMpcollFloorSweepResult* out) {
  if (out == NULL) {
    return;
  }
  out->hit = 0u;
  out->mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE;
  out->hit_line_idx = -1;
  out->projected_line_idx = -1;
  out->hit_segment_id = 0xFFFFu;
  out->projected_segment_id = 0xFFFFu;
  out->hit_is_platform = 0u;
  out->hit_is_ledge = 0u;
  out->hit_has_platform_transform = 0u;
  out->hit_has_height_platform_transform = 0u;
  out->projected_is_platform = 0u;
  out->projected_is_ledge = 0u;
  out->projected_has_platform_transform = 0u;
  out->projected_has_height_platform_transform = 0u;
  out->hit_x = 0.0f;
  out->hit_y = 0.0f;
  out->projected_contact_x = 0.0f;
  out->projected_contact_y = 0.0f;
  out->projected_y_corr = 0.0f;
  out->normal_x = 0.0f;
  out->normal_y = 1.0f;
}

uint8_t mpcoll_collect_bottom_sweep_hit(const MslBatch* batch, size_t idx, int bi,
                                        const MslStageFloorGraph* g, uint32_t stage_id,
                                        float prev_bottom_x, float prev_bottom_y,
                                        float cur_bottom_x, float cur_bottom_y,
                                        uint16_t skip_platform_segment_i, int prefer_line_idx,
                                        int skip_line_idx, const MslCommonParams* c,
                                        MslMpcollFloorSweepResult* out) {
  if (out == NULL) {
    return 0u;
  }
  mpcoll_init_floor_sweep_result(out);
  if (batch == NULL || g == NULL) {
    return 0u;
  }
  (void)stage_id;

  int hit_line_idx = -1;
  float hit_x = 0.0f;
  float hit_y = 0.0f;
  float normal_x = 0.0f;
  float normal_y = 1.0f;
  if (!msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                         cur_bottom_y, skip_platform_segment_i, prefer_line_idx, skip_line_idx, c,
                         &hit_line_idx, &hit_x, &hit_y, &normal_x, &normal_y) ||
      hit_line_idx < 0 || (size_t)hit_line_idx >= g->line_count) {
    return 0u;
  }

  // Shared source-shaped bottom-sweep candidate collection:
  // `mpColl_80044628_Floor` owns the current ECB-bottom sweep and writes CollData.floor/contact
  // before projection/publication helpers decide whether the floor hit becomes grounded,
  // stay-airborne FloorHug, or a rejected edge/platform contact. Keeping this packet explicit
  // prevents Damage, EscapeAir, AttackAir, and moving-platform owners from carrying independent
  // local floor probes.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
  const MslStageFloorLine* hit_line = &g->lines[(size_t)hit_line_idx];
  const uint16_t hit_segment_id = hit_line->segment_i;
  out->hit = 1u;
  out->mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
  out->hit_line_idx = hit_line_idx;
  out->hit_segment_id = hit_segment_id;
  out->hit_is_platform = hit_line->is_platform ? 1u : 0u;
  out->hit_is_ledge = hit_line->is_ledge ? 1u : 0u;
  out->hit_has_platform_transform =
      hit_line->platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE ? 1u : 0u;
  out->hit_has_height_platform_transform =
      hit_line->platform_transform_kind == MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT ? 1u : 0u;
  out->projected_segment_id = hit_segment_id;
  out->projected_contact_x = hit_x;
  out->projected_contact_y = hit_y;
  out->hit_x = hit_x;
  out->hit_y = hit_y;
  out->normal_x = normal_x;
  out->normal_y = normal_y;
  return 1u;
}

void mpcoll_project_bottom_sweep_floor_result(const MslBatch* batch, int bi,
                                              const MslStageFloorGraph* g, uint32_t stage_id,
                                              float cur_bottom_y, MslMpcollFloorSweepResult* out) {
  if (batch == NULL || g == NULL || out == NULL || out->hit == 0u || out->hit_line_idx < 0 ||
      (size_t)out->hit_line_idx >= g->line_count) {
    return;
  }
  (void)stage_id;

  // Source projection phase after a successful bottom sweep:
  // `mpColl_80044838_Floor` / `mpColl_80044948_Floor` consume the accepted line from
  // `mpColl_80044628_Floor` and call the floor projection/remap helper. The projection fields are
  // deliberately stored beside the raw hit so later guards can compare raw vs remapped stage
  // ownership without repeating line walks.
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80044838_Floor,mpColl_80044948_Floor}
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  float y_corr = 0.0f;
  const int projected_line_idx = msl_mplib_8004dd90_floor(
      batch, bi, g, out->hit_line_idx, out->hit_x, cur_bottom_y, &y_corr, NULL, NULL);
  const MslStageFloorLine* projected_line =
      (projected_line_idx >= 0 && (size_t)projected_line_idx < g->line_count)
          ? &g->lines[(size_t)projected_line_idx]
          : NULL;
  const uint16_t projected_segment_id =
      projected_line != NULL ? projected_line->segment_i : out->hit_segment_id;
  out->projected_line_idx = projected_line_idx;
  out->projected_segment_id = projected_segment_id;
  out->projected_is_platform = (projected_line != NULL && projected_line->is_platform) ? 1u : 0u;
  out->projected_is_ledge = (projected_line != NULL && projected_line->is_ledge) ? 1u : 0u;
  out->projected_has_platform_transform =
      (projected_line != NULL &&
       projected_line->platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE)
          ? 1u
          : 0u;
  out->projected_has_height_platform_transform =
      (projected_line != NULL &&
       projected_line->platform_transform_kind == MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT)
          ? 1u
          : 0u;
  out->projected_contact_x = out->hit_x;
  out->projected_contact_y = out->hit_y;
  out->projected_y_corr = y_corr;
}

uint8_t mpcoll_collect_bottom_sweep_floor_result(
    const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_bottom_x, float prev_bottom_y, float cur_bottom_x, float cur_bottom_y,
    uint16_t skip_platform_segment_i, int prefer_line_idx, int skip_line_idx,
    const MslCommonParams* c, MslMpcollFloorSweepResult* out) {
  if (!mpcoll_collect_bottom_sweep_hit(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                                       cur_bottom_x, cur_bottom_y, skip_platform_segment_i,
                                       prefer_line_idx, skip_line_idx, c, out)) {
    return 0u;
  }
  mpcoll_project_bottom_sweep_floor_result(batch, bi, g, stage_id, cur_bottom_y, out);
  return 1u;
}

uint8_t mpcoll_collect_bottom_sweep_hard_floor_result(
    const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_bottom_x, float prev_bottom_y, float cur_bottom_x, float cur_bottom_y,
    int prefer_line_idx, int skip_line_idx, uint8_t admit_ledge, MslMpcollFloorSweepResult* out) {
  if (out == NULL) {
    return 0u;
  }
  mpcoll_init_floor_sweep_result(out);
  int hard_line_idx = -1;
  float hard_ix = 0.0f;
  float hard_iy = 0.0f;
  float hard_nx = 0.0f;
  float hard_ny = 1.0f;
  if (!msl_mpcheck_hard_floor(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                              cur_bottom_x, cur_bottom_y, prefer_line_idx, skip_line_idx,
                              admit_ledge, &hard_line_idx, &hard_ix, &hard_iy, &hard_nx,
                              &hard_ny) ||
      g == NULL || hard_line_idx < 0 || (size_t)hard_line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine* hit_line = &g->lines[(size_t)hard_line_idx];
  out->hit = 1u;
  out->mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
  out->hit_line_idx = hard_line_idx;
  out->hit_segment_id = hit_line->segment_i;
  out->projected_segment_id = hit_line->segment_i;
  out->hit_x = hard_ix;
  out->hit_y = hard_iy;
  out->projected_contact_x = hard_ix;
  out->projected_contact_y = hard_iy;
  out->normal_x = hard_nx;
  out->normal_y = hard_ny;
  out->hit_is_platform = hit_line->is_platform ? 1u : 0u;
  out->hit_is_ledge = hit_line->is_ledge ? 1u : 0u;
  out->hit_has_platform_transform =
      hit_line->platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE ? 1u : 0u;
  out->hit_has_height_platform_transform =
      hit_line->platform_transform_kind == MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT ? 1u : 0u;
  mpcoll_project_bottom_sweep_floor_result(batch, bi, g, stage_id, cur_bottom_y, out);
  return 1u;
}

uint8_t mpcoll_bottom_sweep_hits_segment(const MslBatch* batch, size_t idx, int bi,
                                         const MslStageFloorGraph* g, uint32_t stage_id,
                                         float prev_bottom_x, float prev_bottom_y,
                                         float cur_bottom_x, float cur_bottom_y,
                                         uint16_t skip_platform_segment_i, int prefer_line_idx,
                                         int skip_line_idx, const MslCommonParams* c,
                                         uint16_t target_segment_id) {
  MslMpcollFloorSweepResult sweep = {0};
  return (mpcoll_collect_bottom_sweep_hit(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                                          cur_bottom_x, cur_bottom_y, skip_platform_segment_i,
                                          prefer_line_idx, skip_line_idx, c, &sweep) &&
          sweep.hit_segment_id == target_segment_id)
             ? 1u
             : 0u;
}
