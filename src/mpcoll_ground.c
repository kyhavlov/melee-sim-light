#include "mpcoll_ground.h"
#include "char_registry.h"
#include "ids.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "action.h"
#include "action_ids.h"
#include "attack_id_tables.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "buttons.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "damage_terminal_owner.h"
#include "ecb_pose.h"
#include "escapeair_collision_owner.h"
#include "match_flow.h"
#include "mpcoll_ecb_pose.h"
#include "mpcoll_ecb_points.h"
#include "mpcoll_floor_skip.h"
#include "mpcoll_floor.h"
#include "motion_state_owners.h"
#include "mpcoll_wall_ceil.h"
#include "move_tables.h"
#include "msl_math.h"
#include "mtx34.h"
#include "puff_specials.h"
#include "sheik_specials.h"
#include "specialhi_pose.h"
#include "state_flags.h"
#include "stage_collision.h"
#include "input_axis.h"
#include "item_article_params.h"
#include "throw_flow.h"

//   refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
//   refs/melee/src/melee/mp/mplib.c::mpCheckFloor
static const float k_floor_x_end_clamp = 0.1f;
static const float k_floor_y_bias = 0.0001f;
static const float k_floor_horiz_dy_thresh = 0.0001f;

// refs/melee/src/melee/mp/mpcoll.c::{mpColl_80042384,mpColl_LoadECB_JObj}
static const float k_ecb_vertical_unit = 1.0f;

typedef struct MslMpcollFloorWriteback {
  const MslMpcollContext* ctx;
  uint8_t* on_ground;
  uint8_t* floor_result_mode;
  uint16_t* ground_id;
  float* contact_x;
  float* contact_y;
  float* floor_nx;
  float* floor_ny;
} MslMpcollFloorWriteback;

static inline void mpcoll_publish_source_floor_hit(const MslMpcollFloorWriteback* out,
                                                   uint8_t result_source, uint8_t result_mode,
                                                   uint16_t segment_id, float hit_contact_x,
                                                   float hit_contact_y, float normal_x,
                                                   float normal_y) {
  if (out == NULL || out->ctx == NULL) {
    return;
  }
  if (out->on_ground != NULL) {
    *out->on_ground = 1u;
  }
  if (out->floor_result_mode != NULL) {
    *out->floor_result_mode = result_mode;
  }
  if (out->ground_id != NULL) {
    *out->ground_id = segment_id;
  }
  if (out->contact_x != NULL) {
    *out->contact_x = hit_contact_x;
  }
  if (out->contact_y != NULL) {
    *out->contact_y = hit_contact_y;
  }
  if (out->floor_nx != NULL) {
    *out->floor_nx = normal_x;
  }
  if (out->floor_ny != NULL) {
    *out->floor_ny = normal_y;
  }
  mpcoll_record_callback_floor_result_with_mode(out->ctx, result_source, result_mode, segment_id,
                                                hit_contact_x, hit_contact_y, normal_x, normal_y);
}

static inline void mpcoll_publish_direct_source_floor_hit(const MslMpcollFloorWriteback* out,
                                                          uint8_t result_mode, uint16_t segment_id,
                                                          float hit_contact_x, float hit_contact_y,
                                                          float normal_x, float normal_y) {
  mpcoll_publish_source_floor_hit(out, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, result_mode,
                                  segment_id, hit_contact_x, hit_contact_y, normal_x, normal_y);
}

static inline void mpcoll_publish_direct_source_floor_hit_packet(const MslMpcollFloorWriteback* out,
                                                                 const MslMpcollFloorHit* hit) {
  if (hit == NULL) {
    return;
  }
  mpcoll_publish_direct_source_floor_hit(out, hit->result_mode, hit->contact.ground_id,
                                         hit->contact.contact_x, hit->contact.contact_y,
                                         hit->contact.normal_x, hit->contact.normal_y);
}

void mpcoll_ground_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();

  const int num_players = (int)batch->config.num_players;
  uint8_t homogeneous_stage = 0u;
  uint32_t homogeneous_stage_id = 0u;
  const MslStageFloorGraph* homogeneous_floor_graph = NULL;
  const MslStageCeilingGraph* homogeneous_ceiling_graph = NULL;
  const MslStageWallGraph* homogeneous_left_wall_graph = NULL;
  const MslStageWallGraph* homogeneous_right_wall_graph = NULL;
  uint8_t homogeneous_has_height_platform_transform = 0u;
  uint8_t homogeneous_has_only_static_cardinal_hard_floors = 0u;
  uint8_t homogeneous_has_alternate_floor_endpoint_links = 0u;
  if (batch->batch_size > 0) {
    homogeneous_stage = 1u;
    homogeneous_stage_id = batch->state.stage_id[0];
    for (int bi = 1; bi < batch->batch_size; bi++) {
      if (batch->state.stage_id[bi] != homogeneous_stage_id) {
        homogeneous_stage = 0u;
        break;
      }
    }
    if (homogeneous_stage) {
      homogeneous_floor_graph = stage_collision_get_floor_graph(homogeneous_stage_id);
      homogeneous_ceiling_graph = stage_collision_get_ceiling_graph(homogeneous_stage_id);
      homogeneous_left_wall_graph = stage_collision_get_left_wall_graph(homogeneous_stage_id);
      homogeneous_right_wall_graph = stage_collision_get_right_wall_graph(homogeneous_stage_id);
      homogeneous_has_height_platform_transform = stage_floor_graph_has_height_platform_transform(
          homogeneous_stage_id, homogeneous_floor_graph);
      homogeneous_has_only_static_cardinal_hard_floors =
          stage_collision_stage_has_only_static_cardinal_hard_floors(homogeneous_stage_id);
      homogeneous_has_alternate_floor_endpoint_links =
          stage_collision_stage_has_alternate_floor_endpoint_links(homogeneous_stage_id);
    }
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = homogeneous_stage ? homogeneous_stage_id : batch->state.stage_id[bi];
    const MslStageFloorGraph* g =
        homogeneous_stage ? homogeneous_floor_graph : stage_collision_get_floor_graph(stage_id);
    if (g == NULL || g->lines == NULL || g->line_count == 0) {
      continue;
    }
    const uint8_t stage_has_height_platform_transform =
        homogeneous_stage ? homogeneous_has_height_platform_transform
                          : stage_floor_graph_has_height_platform_transform(stage_id, g);
    const uint8_t stage_has_only_static_cardinal_hard_floors =
        homogeneous_stage ? homogeneous_has_only_static_cardinal_hard_floors
                          : stage_collision_stage_has_only_static_cardinal_hard_floors(stage_id);
    const uint8_t stage_has_alternate_floor_endpoint_links =
        homogeneous_stage ? homogeneous_has_alternate_floor_endpoint_links
                          : stage_collision_stage_has_alternate_floor_endpoint_links(stage_id);
    const MslStageCeilingGraph* cg =
        homogeneous_stage ? homogeneous_ceiling_graph : stage_collision_get_ceiling_graph(stage_id);
    const MslStageWallGraph* lwg = homogeneous_stage
                                       ? homogeneous_left_wall_graph
                                       : stage_collision_get_left_wall_graph(stage_id);
    const MslStageWallGraph* rwg = homogeneous_stage
                                       ? homogeneous_right_wall_graph
                                       : stage_collision_get_right_wall_graph(stage_id);

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      MslMpcollContext mpcoll_ctx = mpcoll_context_make(batch, bi, idx, stage_id, g, cg, lwg, rwg);
      const uint16_t action_id = mpcoll_ctx.action_id;
      const uint16_t prev_action_id = mpcoll_ctx.prev_action_id;
      MslMpcollSourcePhases source_phases = mpcoll_source_phases_for_motion_state(
          mpcoll_ctx.char_id, action_id,
          mpcoll_ft_check_ground_ledge_uses_no_ledge_path(batch, idx));
      if (puff_rollout_turn_coll_edge_snap(batch, idx)) {
        // Branch-shaped owner the static MSLMSO01 table cannot carry: ftPr_SpecialNTurn_Coll
        // picks its ground-check wrapper by |gr_vel| vs attr x74; at or below the threshold the
        // mpColl_8004A45C_Floor endpoint snap (EDGE_SNAP phase) keeps the turn grounded.
        // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialNTurn_Coll
        source_phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_EDGE_SNAP;
      }
      mpcoll_floor_probe_clear(batch, idx);

      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4

      if (!match_flow_should_stage_collide(action_id)) {
        if (batch->state.stocks[idx] == 0u && batch->state.char_id[idx] == 0u &&
            action_id == (uint16_t)MSL_ACT_DEAD_DOWN) {
          batch->state.on_ground[idx] = 1u;
          batch->state.ground_id[idx] = 0u;
          batch->state.ground_normal_x[idx] = 0.0f;
          batch->state.ground_normal_y[idx] = 1.0f;
          batch->state.ground_contact_x[idx] = 0.0f;
          batch->state.ground_contact_y[idx] = 0.0f;
          continue;
        }
        batch->state.on_ground[idx] = 0;
        continue;
      }

      if (msl_motion_state_cliff_hold_phys_snap(mpcoll_ctx.char_id, action_id)) {
        batch->state.on_ground[idx] = 0;
        continue;
      }

      if (msl_action_is_thrown_victim(action_id)) {
        const uint8_t owner = batch->state.grab_owner_port[idx];
        if (owner != 0xFFu && owner < (uint8_t)num_players && owner != (uint8_t)p) {
          continue;
        }
      }
      if (throw_flow_release_pending_for_victim(batch, bi, p)) {
        continue;
      }

      publish_common_air_transformed_platform_skip_from_root_crossing(batch, idx, bi, g, stage_id,
                                                                      source_phases, c);
      publish_attackair_transformed_platform_skip_from_root_crossing(batch, idx, bi, g, stage_id,
                                                                     source_phases, c);

      const uint8_t was_grounded = batch->state.prev_on_ground[idx] ? 1u : 0u;
      const uint8_t frame_start_grounded = batch->state.frame_start_on_ground[idx] ? 1u : 0u;
      mpcoll_ctx.was_grounded = was_grounded;
      const uint8_t ecb_lock_timer_seed = batch->state.ecb_lock_timer[idx];
      const uint8_t stage_object_platform_carry =
          (mpcoll_source_phases_preserve_grounded_floor(source_phases) &&
           grounded_action_allows_stage_object_platform_carry(action_id))
              ? 1u
              : 0u;
      const uint8_t ft80083f88_platform_carry =
          (mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_GROUND_B108) &&
           grounded_action_allows_height_platform_y_correction(
               action_id, (uint16_t)batch->state.action_frame[idx], batch->state.char_id[idx]))
              ? 1u
              : 0u;
      if ((was_grounded || frame_start_grounded) && batch->state.ground_id[idx] != 0xFFFFu &&
          (stage_object_platform_carry || ft80083f88_platform_carry)) {
        int carry_line_idx =
            stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
        if (carry_line_idx < 0 || (size_t)carry_line_idx >= g->line_count ||
            !stage_collision_floor_line_has_randall_platform_transform(
                stage_id, g->lines[(size_t)carry_line_idx].segment_i)) {
          const int randall_line_idx = mpcoll_find_current_randall_floor_line_at_root(
              batch, bi, g, batch->state.pos_x[idx], batch->state.pos_y[idx]);
          if (randall_line_idx >= 0) {
            // data/stages/bin/grst.bin::MSLSTG01 platform_transforms(kind=randall)
            carry_line_idx = randall_line_idx;
            batch->state.ground_id[idx] = g->lines[(size_t)randall_line_idx].segment_i;
          }
        }
        if (carry_line_idx >= 0 && (size_t)carry_line_idx < g->line_count &&
            g->lines[(size_t)carry_line_idx].is_platform) {
          float platform_dx = 0.0f;
          float platform_dy = 0.0f;
          const uint8_t randall_path_platform =
              stage_collision_floor_line_has_randall_platform_transform(
                  stage_id, g->lines[(size_t)carry_line_idx].segment_i);
          if ((stage_object_platform_carry || randall_path_platform) &&
              stage_collision_floor_line_motion_delta(batch, bi, &g->lines[(size_t)carry_line_idx],
                                                      &platform_dx, &platform_dy)) {
            // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
            // data/motion_state/owners/{fox,falco}.bin
            batch->state.pos_x[idx] += platform_dx;
            batch->state.pos_y[idx] += platform_dy;
            if (randall_path_platform) {
              float line_y = 0.0f;
              if (floor_line_y_at_x_for_env(batch, bi, g, carry_line_idx, batch->state.pos_x[idx],
                                            &line_y)) {
                line_y += platform_dy;
                batch->state.pos_y[idx] = line_y + k_floor_y_bias;
              }
            }
          }
        }
      }
      uint8_t ecb_lock_timer = ecb_lock_timer_seed;
      if (batch->state.on_ground[idx]) {
        ecb_lock_timer = 0;
      } else if (ecb_lock_timer > 0u) {
        ecb_lock_timer = (uint8_t)(ecb_lock_timer - 1u);
      }
      batch->state.ecb_lock_timer[idx] = ecb_lock_timer;
      const uint8_t ecb_lock_active = (ecb_lock_timer > 0u) ? 1u : 0u;
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_JObj}
      const uint8_t damage_collision_uses_seeded_lock_bottom =
          (is_damage_ground_collision_action(action_id) && ecb_lock_timer_seed != 0u) ? 1u : 0u;

      // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
      const uint8_t char_id = mpcoll_ctx.char_id;
      const uint32_t anim = mpcoll_ctx.anim;
      const MslAttackAirPlatformEcbOwner attackair_platform_owner =
          attackair_platform_ecb_owner(char_id, action_id);
      const uint8_t shallow_attackair_platform_ecb_owner = attackair_platform_owner.shallow;
      const uint8_t first_phase_attackair_platform_ecb_owner = attackair_platform_owner.first_phase;
      const uint8_t late_attackair_platform_ecb_owner = attackair_platform_owner.late;
      const uint16_t ecb_frame =
          msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
      mpcoll_ctx.ecb_frame = ecb_frame;
      uint16_t ecb_frame_prev = msl_ecb_prev_frame_u16(ecb_frame);
      uint16_t ecb_frame_bias_next = ecb_frame;
      if (action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
          action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) {
        if (ecb_frame_bias_next != 0xFFFFu) {
          ecb_frame_bias_next = (uint16_t)(ecb_frame_bias_next + 1u);
        }
      } else if (is_damage_fly_collision_action(action_id) && batch->state.action_frame[idx] <= 2 &&
                 !(batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u)) {
        if (ecb_frame_bias_next != 0xFFFFu) {
          ecb_frame_bias_next = (uint16_t)(ecb_frame_bias_next + 1u);
        }
      }

      // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj (flags & 1)
      MslEcbBottomWorldPoint cur_bot = {0};
      MslEcbBottomWorldPoint prev_bot = {0};

      const float x = batch->state.pos_x[idx];
      const float y = batch->state.pos_y[idx];
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpCheckFloor}
      const uint8_t prev_action_is_jumpaerial =
          (prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
           prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B)
              ? 1u
              : 0u;
      const uint8_t seed_prev_action_is_jumpaerial =
          (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
           batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B)
              ? 1u
              : 0u;
      if ((action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
           action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
          ecb_lock_timer_seed >= 9u && batch->state.action_frame[idx] <= 0 &&
          batch->state.shine_jump_iasa_entered_this_frame[idx] != 0u &&
          batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
          !msl_escapeair_locked_bottom_owner_any(
              batch->state.coll_desired_ecb_bottom_locked_owner[idx])) {
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
        const uint8_t entry_ground_is_soft_or_transform =
            (batch->state.ground_id[idx] != 0xFFFFu &&
             (stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]) ||
              stage_collision_floor_line_has_platform_transform(stage_id,
                                                                batch->state.ground_id[idx])))
                ? 1u
                : 0u;
        batch->state.coll_desired_ecb_bottom_locked_owner[idx] =
            msl_escapeair_locked_bottom_owner_for_live_jumpaerial_entry(
                entry_ground_is_soft_or_transform);
      }
      uint8_t damagefly_frame_start_bottom_above_carried_floor = 0u;
      if (is_damage_fly_collision_action(action_id) && batch->state.hitlag[idx] == 0u &&
          batch->state.hitstun[idx] != 0u &&
          batch->state.floor_sweep_prev_runtime_owned[idx] != 0u &&
          isfinite(batch->state.prev_pos_x[idx]) && isfinite(batch->state.prev_pos_y[idx]) &&
          isfinite(cur_bot.rel_y)) {
        const int carried_floor_line_idx =
            stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
        float carried_floor_y = 0.0f;
        if (carried_floor_line_idx >= 0 &&
            floor_line_y_at_x_for_env(batch, bi, g, carried_floor_line_idx,
                                      batch->state.prev_pos_x[idx], &carried_floor_y) &&
            (batch->state.prev_pos_y[idx] + cur_bot.rel_y) > (carried_floor_y + k_floor_y_bias)) {
          damagefly_frame_start_bottom_above_carried_floor = 1u;
        }
      }
      const uint8_t damagefly_473cc_uses_colldata_last_pos =
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800473CC,mpColl_80043754}
          // data/stages/bin/*.bin::MSLSTG01 carried floor segment geometry
          (is_damage_fly_collision_action(action_id) && batch->state.hitlag[idx] == 0u &&
           batch->state.hitstun[idx] != 0u &&
           batch->state.floor_sweep_prev_runtime_owned[idx] != 0u &&
           damagefly_frame_start_bottom_above_carried_floor != 0u &&
           (prev_action_id == action_id || batch->state.seed_prev_action_id[idx] == action_id) &&
           isfinite(batch->state.prev_pos_x[idx]) && isfinite(batch->state.prev_pos_y[idx]))
              ? 1u
              : 0u;
      const float fallback_prev_x = damagefly_473cc_uses_colldata_last_pos
                                        ? batch->state.prev_pos_x[idx]
                                        : batch->state.floor_sweep_prev_pos_x[idx];
      const float fallback_prev_y = damagefly_473cc_uses_colldata_last_pos
                                        ? batch->state.prev_pos_y[idx]
                                        : batch->state.floor_sweep_prev_pos_y[idx];
      const MslMpcoll800471F8EscapeAirPacket escapeair_471f8 = msl_mpcoll_800471f8_escapeair_packet(
          &mpcoll_ctx, ecb_lock_active, ecb_lock_timer_seed, fallback_prev_x, fallback_prev_y);
      const float prev_x = escapeair_471f8.prev_x;
      const float prev_y = escapeair_471f8.prev_y;
      mpcoll_clear_callback_floor_result(&mpcoll_ctx, prev_x, prev_y, x, y);
      if (action_id != (uint16_t)MSL_ACT_ESCAPE_AIR) {
        batch->state.coll_escapeair_floor_producer_runtime[idx] = 0u;
      }
      const int escapeair_entry_floor_line_idx =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
           batch->state.prev_action_id[idx] != action_id && batch->state.ground_id[idx] != 0xFFFFu)
              ? stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx])
              : -1;
      float escapeair_entry_floor_y = 0.0f;
      const uint8_t escapeair_entry_floor_y_valid =
          (escapeair_entry_floor_line_idx >= 0 &&
           floor_line_y_at_x_for_env(batch, bi, g, escapeair_entry_floor_line_idx, x,
                                     &escapeair_entry_floor_y))
              ? 1u
              : 0u;
      if (escapeair_entry_floor_y_valid != 0u &&
          msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
              batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
          batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
          !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]) &&
          !stage_collision_floor_line_has_platform_transform(stage_id,
                                                             batch->state.ground_id[idx]) &&
          !g->lines[(size_t)escapeair_entry_floor_line_idx].is_ledge &&
          !floor_line_is_generated_sloped_ledge(batch, bi, g, escapeair_entry_floor_line_idx) &&
          y > (escapeair_entry_floor_y + k_floor_y_bias) &&
          y <= (escapeair_entry_floor_y + batch->state.coll_desired_ecb_bottom_rel_y[idx] +
                k_floor_y_bias)) {
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800471F8}
        mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
      }
      const uint8_t escapeair_floor_producer_authority_in =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
           msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
               batch->state.coll_desired_ecb_bottom_locked_owner[idx]))
              ? batch->state.coll_escapeair_floor_producer_runtime[idx]
              : 0u;
      if (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR) {
        batch->state.coll_escapeair_floor_producer_runtime[idx] = 0u;
      }

      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
      uint8_t lock_bottom_to_zero = was_grounded;
      if (action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
          action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) {
        lock_bottom_to_zero = 0u;
      }
      const int escapeair_seed_floor_line_idx =
          stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
      const uint8_t escapeair_early_ledge_root_floor_owner =
          // data/stages/bin/*.bin::MSLSTG01 line flags, links, and platform transform metadata
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed >= 3u &&
           (batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                     (size_t)MSL_STATE_FLAGS_2218_INDEX] &
            (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) != 0u &&
           batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
           batch->state.seed_prev_action_frame[idx] <= 1 && batch->state.action_frame[idx] <= 3 &&
           escapeair_seed_floor_line_idx >= 0 &&
           floor_line_is_terminal_cardinal_ledge_floor(batch, bi, g, stage_id,
                                                       escapeair_seed_floor_line_idx) &&
           floor_x_within_line_bounds(batch, bi, g, escapeair_seed_floor_line_idx, x))
              ? 1u
              : 0u;
      if (escapeair_early_ledge_root_floor_owner) {
        lock_bottom_to_zero = 1u;
      }
      const uint8_t spacie_air_special_floor_owner =
          is_spacie_air_special_floor_collision_action(char_id, action_id) &&
          prev_action_id == action_id;
      const uint8_t common_air_collision_uses_locked_ecb_bottom =
          (ecb_lock_active &&
           ((action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] != 0u) ||
            is_attackair_action(action_id) ||
            (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_START) ||
            (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_LOOP) ||
            (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_END)))
              ? 1u
              : 0u;
      const uint8_t damagefly_release_entry_uses_pose_bottom =
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_JObj}
          (is_damage_fly_collision_action(action_id) && ecb_lock_active &&
           batch->state.hitlag[idx] == 0u && batch->state.hitlag_pre_timer[idx] == 0u &&
           batch->state.action_frame[idx] <= 2 &&
           msl_action_is_thrown_victim(batch->state.seed_prev_action_id[idx]))
              ? 1u
              : 0u;
      const uint8_t escapeair_sustained_current_ecb_uses_pose_bottom =
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && stage_has_height_platform_transform &&
           batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
           batch->state.action_frame[idx] >= 2)
              ? 1u
              : 0u;
      const uint8_t locked_desired_ecb_bottom_valid =
          (ecb_lock_active && batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
           msl_escapeair_locked_bottom_owner_any(
               batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
           (escapeair_471f8.entry_desired_bottom_owner ||
            (!escapeair_sustained_current_ecb_uses_pose_bottom &&
             msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
                 batch->state.coll_desired_ecb_bottom_locked_owner[idx])) ||
            action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
            (action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] != 0u) ||
            is_attackair_action(action_id) ||
            msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_COMMON_AIR_COLL)))
              ? 1u
              : 0u;
      uint8_t use_locked_desired_ecb_bottom = 0u;
      if (!lock_bottom_to_zero && !damagefly_release_entry_uses_pose_bottom &&
          ((action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active) ||
           (is_damage_collision_landing_action(action_id) &&
            (ecb_lock_active || damage_collision_uses_seeded_lock_bottom)) ||
           (spacie_air_special_floor_owner && ecb_lock_active) ||
           (common_air_collision_uses_locked_ecb_bottom && ecb_lock_active))) {
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
        // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
        if (locked_desired_ecb_bottom_valid) {
          use_locked_desired_ecb_bottom = 1u;
        } else {
          lock_bottom_to_zero = 1u;
        }
      }
      uint8_t lock_bottom_to_prev_frame = 0u;
      if (!lock_bottom_to_zero && !use_locked_desired_ecb_bottom &&
          action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && batch->state.action_frame[idx] >= 0 &&
          batch->state.action_frame[idx] <= 10 && batch->state.speed_y_self[idx] <= 0.0f &&
          prev_action_id != action_id) {
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
        lock_bottom_to_prev_frame = 1u;
      }
      const uint16_t ecb_frame_cur =
          lock_bottom_to_prev_frame ? ecb_frame_prev : ecb_frame_bias_next;
      const float desired_ecb_rel =
          use_locked_desired_ecb_bottom
              ? batch->state.coll_desired_ecb_bottom_rel_y[idx]
              : mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_cur, lock_bottom_to_zero);
      const float frame_start_desired_bottom_rel_y =
          batch->state.coll_desired_ecb_bottom_rel_y[idx];
      const float facing_dir_for_ecb = batch->state.facing[idx] ? 1.0f : -1.0f;
      MslEcbWorldPoints desired_ecb_points = {0};
      msl_ecb_world_points_sample(&desired_ecb_points, char_id, anim, ecb_frame_cur,
                                  facing_dir_for_ecb, x, y, lock_bottom_to_zero);
      if (use_locked_desired_ecb_bottom) {
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
        msl_ecb_world_points_preserve_desired_bottom_rel_y(&desired_ecb_points, x, y,
                                                           desired_ecb_rel);
      }
      const uint8_t escapeair_jumpaerial_entry =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && prev_action_id != action_id &&
           seed_prev_action_is_jumpaerial)
              ? 1u
              : 0u;
      const uint8_t escapeair_no_lock_jumpaerial_entry =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed == 0u &&
           batch->state.action_frame[idx] <= 1 && seed_prev_action_is_jumpaerial)
              ? 1u
              : 0u;
      const uint8_t escapeair_jumpaerial_source_entry =
          (escapeair_jumpaerial_entry || escapeair_no_lock_jumpaerial_entry) ? 1u : 0u;
      const uint16_t escapeair_jumpaerial_source_action_id =
          prev_action_is_jumpaerial ? prev_action_id : batch->state.seed_prev_action_id[idx];
      const int16_t escapeair_jumpaerial_source_action_frame =
          prev_action_is_jumpaerial ? (int16_t)(batch->state.prev_action_frame[idx] + 1)
                                    : batch->state.seed_prev_action_frame[idx];
      const float pre_entry_jumpaerial_ecb_rel =
          escapeair_jumpaerial_source_entry
              ? mpcoll_action_pose_ecb_bottom_rel_y(char_id, escapeair_jumpaerial_source_action_id,
                                                    escapeair_jumpaerial_source_action_frame, 0u)
              : 0.0f;
      const uint8_t escapeair_jumpaerial_prev_ecb_lifetime =
          ((escapeair_jumpaerial_entry || escapeair_no_lock_jumpaerial_entry) &&
           batch->state.seed_prev_action_frame[idx] >= 4)
              ? 1u
              : 0u;
      // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
      // refs/melee/src/melee/mp/mpcoll.c::{
      const uint8_t escapeair_no_lock_entry_prev_ecb_lifetime =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && !ecb_lock_active &&
           ecb_lock_timer_seed == 0u && prev_action_id != action_id &&
           !escapeair_jumpaerial_prev_ecb_lifetime)
              ? 1u
              : 0u;
      const float pose_prev_ecb_rel =
          mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_prev, lock_bottom_to_zero);
      const float pre_entry_prev_ecb_rel =
          // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
          // refs/melee/src/melee/mp/mpcoll.c::{
          (escapeair_jumpaerial_prev_ecb_lifetime)
              ? mpcoll_action_pose_ecb_bottom_rel_y(
                    char_id, batch->state.seed_prev_action_id[idx],
                    (int16_t)(batch->state.prev_action_frame[idx] + 1), lock_bottom_to_zero)
          : (escapeair_no_lock_entry_prev_ecb_lifetime)
              ? mpcoll_action_pose_ecb_bottom_rel_y(
                    char_id, prev_action_id, (int16_t)(batch->state.prev_action_frame[idx] + 1),
                    lock_bottom_to_zero)
              : pose_prev_ecb_rel;
      MslEcbWorldPoints state_cur_ecb_points = {0};
      const uint8_t have_state_cur_ecb = mpcoll_state_current_ecb_points(
          batch, idx, &state_cur_ecb_points, prev_x, prev_y, ecb_frame_prev);
      MslEcbWorldPoints state_desired_ecb_points = {0};
      const uint8_t have_state_desired_ecb = mpcoll_state_desired_ecb_points(
          batch, idx, &state_desired_ecb_points, prev_x, prev_y, ecb_frame_prev);
      uint16_t common_fall_neutral_msid = 0u;
      uint16_t common_fall_forward_msid = 0u;
      uint16_t common_fall_backward_msid = 0u;
      const uint8_t common_fall_blended_action =
          msl_action_common_fall_blend_msids(action_id, &common_fall_neutral_msid,
                                             &common_fall_forward_msid, &common_fall_backward_msid);
      const uint8_t common_fall_blended_seed_ecb_consumer =
          (batch->state.coll_common_fall_blended_ecb_seed_valid[idx] != 0u && have_state_cur_ecb &&
           have_state_desired_ecb && common_fall_blended_action &&
           batch->state.common_fall_blend_x4[idx] != 0.0f)
              ? 1u
              : 0u;
      const uint8_t common_fall_blended_live_ecb_consumer =
          (!lock_bottom_to_zero && mpcoll_replay_rollout_advanced_past_reseed(batch, bi) != 0u &&
           common_fall_blended_action &&
           mpcoll_common_fall_blended_ecb_live_owner(batch, idx, action_id) != 0u)
              ? 1u
              : 0u;
      const uint8_t common_fall_blended_ecb_consumer =
          (common_fall_blended_seed_ecb_consumer || common_fall_blended_live_ecb_consumer) ? 1u
                                                                                           : 0u;
      MslEcbWorldPoints squeeze_restore_ecb_points = {0};
      const uint8_t have_squeeze_restore_ecb = mpcoll_state_squeeze_restore_ecb_points(
          batch, idx, &squeeze_restore_ecb_points, prev_x, prev_y, ecb_frame_prev);
      if (have_squeeze_restore_ecb) {
        // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
        batch->state.coll_squeeze_restore_ecb_valid[idx] = 0u;
      }
      const uint8_t active_damage_hitlag_ecb_live_entry =
          (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
           is_damage_collision_landing_action(action_id) && prev_action_id != action_id &&
           !is_damage_collision_landing_action(prev_action_id) &&
           msl_motion_state_class_has(char_id, prev_action_id, MSL_MS_CLASS_ATTACK_AIR))
              ? 1u
              : 0u;
      const uint8_t active_damage_hitlag_ecb_exit_carry =
          (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u &&
           is_damage_collision_landing_action(action_id) &&
           batch->state.coll_damage_hitlag_ecb_valid[idx] != 0u &&
           damage_hitlag_exit_carry_source_is_thrown_needle(batch, idx) != 0u)
              ? 1u
              : 0u;
      const uint8_t active_damage_hitlag_ecb_carry =
          (have_state_cur_ecb && batch->state.hitlag_pre_timer[idx] != 0u &&
           (batch->state.hitlag[idx] != 0u || active_damage_hitlag_ecb_exit_carry != 0u) &&
           is_damage_collision_landing_action(action_id) &&
           (batch->state.coll_damage_hitlag_ecb_valid[idx] != 0u ||
            active_damage_hitlag_ecb_live_entry))
              ? 1u
              : 0u;
      const uint8_t active_damage_hitlag_ecb_consumer =
          (active_damage_hitlag_ecb_carry &&
           damage_hitlag_floorhug_attempts_downward_sdi(batch, idx, c))
              ? 1u
              : 0u;
      const uint8_t jumpaerial_entry_ecb_consumer =
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80043754}
          ((prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
           (is_attackair_action(action_id) ||
            (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_START) ||
            (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_LOOP) ||
            (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_END)))
              ? 1u
              : 0u;
      const uint8_t damage_entry_attackair_ecb_consumer =
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor}
          (is_attackair_action(action_id) && batch->state.action_frame[idx] <= 2 &&
           (msl_damage_owner_is_damage_air_action(prev_action_id) ||
            msl_damage_owner_is_damage_air_action(batch->state.seed_prev_action_id[idx]) ||
            is_damage_ground_collision_action(prev_action_id) ||
            is_damage_ground_collision_action(batch->state.seed_prev_action_id[idx])))
              ? 1u
              : 0u;
      const uint8_t damage_entry_escapeair_ecb_consumer =
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_800471F8,
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && prev_action_id != action_id &&
           batch->state.action_frame[idx] <= 2 &&
           (msl_damage_owner_is_damage_air_action(prev_action_id) ||
            is_damage_ground_collision_action(prev_action_id)))
              ? 1u
              : 0u;
      const uint8_t use_hidden_ecb_lifetime =
          (have_state_cur_ecb && !lock_bottom_to_zero &&
           (jumpaerial_entry_ecb_consumer || damage_entry_attackair_ecb_consumer ||
            damage_entry_escapeair_ecb_consumer || active_damage_hitlag_ecb_consumer ||
            common_fall_blended_ecb_consumer))
              ? 1u
              : ((escapeair_jumpaerial_prev_ecb_lifetime ||
                  escapeair_no_lock_entry_prev_ecb_lifetime) &&
                 !use_locked_desired_ecb_bottom);
      const uint8_t state_cur_ecb_stale_zero =
          (uint8_t)((escapeair_jumpaerial_prev_ecb_lifetime ||
                     escapeair_no_lock_entry_prev_ecb_lifetime) &&
                    have_state_cur_ecb && !was_grounded &&
                    state_cur_ecb_points.bottom_rel_y == 0.0f);
      const float state_cur_ecb_rel = (have_state_cur_ecb && !state_cur_ecb_stale_zero)
                                          ? state_cur_ecb_points.bottom_rel_y
                                          : pre_entry_prev_ecb_rel;
      MslEcbWorldPoints common_fall_blended_current_ecb_points = {0};
      const uint8_t have_common_fall_blended_current_ecb =
          (common_fall_blended_ecb_consumer &&
           mpcoll_common_fall_blended_ecb_points(&common_fall_blended_current_ecb_points, batch,
                                                 idx, char_id, common_fall_neutral_msid,
                                                 ecb_frame_cur, facing_dir_for_ecb, x, y))
              ? 1u
              : 0u;
      const float common_fall_blended_current_ecb_rel =
          have_common_fall_blended_current_ecb ? common_fall_blended_current_ecb_points.bottom_rel_y
                                               : desired_ecb_rel;
      const float hidden_current_ecb_rel = active_damage_hitlag_ecb_consumer ? state_cur_ecb_rel
                                           : common_fall_blended_ecb_consumer
                                               ? common_fall_blended_current_ecb_rel
                                               : desired_ecb_rel;
      const float prev_ecb_rel =
          (use_hidden_ecb_lifetime && !lock_bottom_to_zero) ? state_cur_ecb_rel : pose_prev_ecb_rel;
      // refs/melee/src/melee/mp/mpcoll.c::{
      if (use_hidden_ecb_lifetime) {
        mpcoll_bottom_world_point_from_rel(&cur_bot, x, y, hidden_current_ecb_rel, ecb_frame_cur);
        mpcoll_bottom_world_point_from_rel(&prev_bot, prev_x, prev_y, prev_ecb_rel, ecb_frame_prev);
        if (common_fall_blended_ecb_consumer) {
          batch->state.coll_common_fall_blended_ecb_seed_valid[idx] = 0u;
        }
      } else {
        batch->state.coll_common_fall_blended_ecb_seed_valid[idx] = 0u;
        msl_ecb_bottom_world_point_sample(&cur_bot, char_id, anim, ecb_frame_cur, x, y,
                                          lock_bottom_to_zero);
        msl_ecb_bottom_world_point_sample(&prev_bot, char_id, anim, ecb_frame_prev, prev_x, prev_y,
                                          lock_bottom_to_zero);
      }

      float cur_bottom_x = cur_bot.x;
      float cur_bottom_y = cur_bot.y;
      float prev_bottom_x = prev_bot.x;
      float prev_bottom_y = prev_bot.y;
      MslEcbWorldPoints cur_ecb_points = desired_ecb_points;
      MslEcbWorldPoints prev_ecb_points = {0};
      if (use_hidden_ecb_lifetime && have_state_cur_ecb) {
        prev_ecb_points = state_cur_ecb_points;
        if (common_fall_blended_ecb_consumer) {
          // refs/melee/src/melee/mp/mpcoll.c::{
          if (have_common_fall_blended_current_ecb) {
            cur_ecb_points = common_fall_blended_current_ecb_points;
            desired_ecb_points = common_fall_blended_current_ecb_points;
          } else {
            cur_ecb_points.bottom_rel_y = common_fall_blended_current_ecb_rel;
            cur_ecb_points.bottom_y = y + common_fall_blended_current_ecb_rel;
            desired_ecb_points.bottom_rel_y = common_fall_blended_current_ecb_rel;
            desired_ecb_points.bottom_y = y + common_fall_blended_current_ecb_rel;
          }
        }
        if (active_damage_hitlag_ecb_consumer) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
          mpcoll_ecb_world_points_from_rel(
              &cur_ecb_points, x, y, state_cur_ecb_points.bottom_rel_y,
              state_cur_ecb_points.top_rel_y, state_cur_ecb_points.left_rel_x,
              state_cur_ecb_points.right_rel_x, state_cur_ecb_points.side_rel_y, ecb_frame_cur);
          cur_bottom_x = cur_ecb_points.bottom_x;
          cur_bottom_y = cur_ecb_points.bottom_y;
          cur_bot.x = cur_ecb_points.bottom_x;
          cur_bot.y = cur_ecb_points.bottom_y;
          cur_bot.rel_y = cur_ecb_points.bottom_rel_y;
          cur_bot.frame_u16 = cur_ecb_points.frame_u16;
          desired_ecb_points = cur_ecb_points;
        }
      } else {
        msl_ecb_world_points_sample(&prev_ecb_points, char_id, anim, ecb_frame_prev,
                                    facing_dir_for_ecb, prev_x, prev_y, was_grounded);
      }
      uint8_t have_cur_specialhi_ecb = 0u;
      uint8_t have_prev_specialhi_ecb = 0u;
      uint8_t have_cur_vanish_ecb = 0u;
      uint8_t have_prev_vanish_ecb = 0u;
      uint8_t have_cur_damageflyroll_ecb = 0u;
      uint8_t have_prev_damageflyroll_ecb = 0u;
      uint8_t specialhi_jobj_ecb_active = mpcoll_ground_specialhi_uses_jobj_ecb(char_id, action_id);
      uint8_t vanish_jobj_ecb_active =
          mpcoll_vanish_ft_check_jobj_ecb_owner(char_id, action_id, source_phases);
      uint8_t damageflyroll_jobj_ecb_active =
          (mpcoll_ground_damageflyroll_uses_jobj_ecb(action_id) &&
           batch->state.speed_y_self[idx] <= -(3.0f * k_ecb_vertical_unit))
              ? 1u
              : 0u;
      if (!use_hidden_ecb_lifetime && !lock_bottom_to_zero && specialhi_jobj_ecb_active) {
        MslEcbWorldPoints specialhi_cur_ecb = cur_ecb_points;
        MslEcbWorldPoints specialhi_prev_ecb = prev_ecb_points;
        have_cur_specialhi_ecb = mpcoll_ground_try_sample_specialhi_jobj_ecb(
            &specialhi_cur_ecb, batch, idx, char_id, anim, action_id, ecb_frame, facing_dir_for_ecb,
            x, y);
        have_prev_specialhi_ecb = mpcoll_ground_try_sample_specialhi_jobj_ecb(
            &specialhi_prev_ecb, batch, idx, char_id, anim, action_id, ecb_frame_prev,
            facing_dir_for_ecb, prev_x, prev_y);
        if (have_cur_specialhi_ecb) {
          cur_ecb_points = specialhi_cur_ecb;
          cur_bottom_x = specialhi_cur_ecb.bottom_x;
          cur_bottom_y = specialhi_cur_ecb.bottom_y;
          cur_bot.x = specialhi_cur_ecb.bottom_x;
          cur_bot.y = specialhi_cur_ecb.bottom_y;
          cur_bot.rel_y = specialhi_cur_ecb.bottom_rel_y;
          cur_bot.frame_u16 = specialhi_cur_ecb.frame_u16;
        }
        if (have_prev_specialhi_ecb) {
          prev_ecb_points = specialhi_prev_ecb;
          prev_bottom_x = specialhi_prev_ecb.bottom_x;
          prev_bottom_y = specialhi_prev_ecb.bottom_y;
          prev_bot.x = specialhi_prev_ecb.bottom_x;
          prev_bot.y = specialhi_prev_ecb.bottom_y;
          prev_bot.rel_y = specialhi_prev_ecb.bottom_rel_y;
          prev_bot.frame_u16 = specialhi_prev_ecb.frame_u16;
        }
      }
      if (!use_hidden_ecb_lifetime && !lock_bottom_to_zero && vanish_jobj_ecb_active) {
        MslEcbWorldPoints vanish_cur_ecb = cur_ecb_points;
        MslEcbWorldPoints vanish_prev_ecb = prev_ecb_points;
        have_cur_vanish_ecb =
            mpcoll_vanish_jobj_ecb_points(&vanish_cur_ecb, batch, idx, char_id, (uint16_t)anim,
                                          ecb_frame, facing_dir_for_ecb, x, y);
        have_prev_vanish_ecb =
            mpcoll_vanish_jobj_ecb_points(&vanish_prev_ecb, batch, idx, char_id, (uint16_t)anim,
                                          ecb_frame_prev, facing_dir_for_ecb, prev_x, prev_y);
        if (have_cur_vanish_ecb) {
          cur_ecb_points = vanish_cur_ecb;
          cur_bottom_x = vanish_cur_ecb.bottom_x;
          cur_bottom_y = vanish_cur_ecb.bottom_y;
          cur_bot.x = vanish_cur_ecb.bottom_x;
          cur_bot.y = vanish_cur_ecb.bottom_y;
          cur_bot.rel_y = vanish_cur_ecb.bottom_rel_y;
          cur_bot.frame_u16 = vanish_cur_ecb.frame_u16;
        }
        if (have_prev_vanish_ecb) {
          prev_ecb_points = vanish_prev_ecb;
          prev_bottom_x = vanish_prev_ecb.bottom_x;
          prev_bottom_y = vanish_prev_ecb.bottom_y;
          prev_bot.x = vanish_prev_ecb.bottom_x;
          prev_bot.y = vanish_prev_ecb.bottom_y;
          prev_bot.rel_y = vanish_prev_ecb.bottom_rel_y;
          prev_bot.frame_u16 = vanish_prev_ecb.frame_u16;
        }
      }
      if (!use_hidden_ecb_lifetime && !lock_bottom_to_zero && damageflyroll_jobj_ecb_active) {
        MslEcbWorldPoints damageflyroll_cur_ecb = cur_ecb_points;
        MslEcbWorldPoints damageflyroll_prev_ecb = prev_ecb_points;
        have_cur_damageflyroll_ecb = mpcoll_ground_try_sample_damageflyroll_jobj_ecb(
            &damageflyroll_cur_ecb, batch, idx, char_id, anim, action_id, ecb_frame,
            facing_dir_for_ecb, x, y);
        have_prev_damageflyroll_ecb = mpcoll_ground_try_sample_damageflyroll_jobj_ecb(
            &damageflyroll_prev_ecb, batch, idx, char_id, anim, action_id, ecb_frame_prev,
            facing_dir_for_ecb, prev_x, prev_y);
        if (have_cur_damageflyroll_ecb) {
          cur_ecb_points = damageflyroll_cur_ecb;
          cur_bottom_x = damageflyroll_cur_ecb.bottom_x;
          cur_bottom_y = damageflyroll_cur_ecb.bottom_y;
          cur_bot.x = damageflyroll_cur_ecb.bottom_x;
          cur_bot.y = damageflyroll_cur_ecb.bottom_y;
          cur_bot.rel_y = damageflyroll_cur_ecb.bottom_rel_y;
          cur_bot.frame_u16 = damageflyroll_cur_ecb.frame_u16;
        }
        if (have_prev_damageflyroll_ecb) {
          prev_ecb_points = damageflyroll_prev_ecb;
          prev_bottom_x = damageflyroll_prev_ecb.bottom_x;
          prev_bottom_y = damageflyroll_prev_ecb.bottom_y;
          prev_bot.x = damageflyroll_prev_ecb.bottom_x;
          prev_bot.y = damageflyroll_prev_ecb.bottom_y;
          prev_bot.rel_y = damageflyroll_prev_ecb.bottom_rel_y;
          prev_bot.frame_u16 = damageflyroll_prev_ecb.frame_u16;
        }
      }
      uint8_t callback_stopped_at_substep = 0u;
      const float prev_side_mid_y =
          prev_y + (0.5f * (prev_ecb_points.top_rel_y + prev_ecb_points.bottom_rel_y));
      MslMpcollLoadedEcb loaded_ecb = {0};
      uint8_t current_ecb_source_mode = lock_bottom_to_zero
                                            ? (uint8_t)MSL_MPCOLL_ECB_SOURCE_FIXED_ZERO_BOTTOM
                                            : (uint8_t)MSL_MPCOLL_ECB_SOURCE_FIXED_POSE;
      uint8_t previous_ecb_source_mode = was_grounded
                                             ? (uint8_t)MSL_MPCOLL_ECB_SOURCE_FIXED_ZERO_BOTTOM
                                             : (uint8_t)MSL_MPCOLL_ECB_SOURCE_FIXED_POSE;
      uint8_t desired_ecb_source_mode = current_ecb_source_mode;
      if (use_locked_desired_ecb_bottom) {
        current_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_LOCKED_DESIRED_BOTTOM;
        desired_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_LOCKED_DESIRED_BOTTOM;
      }
      if (use_hidden_ecb_lifetime && have_state_cur_ecb) {
        previous_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_HIDDEN_COLLDATA;
        if (active_damage_hitlag_ecb_consumer) {
          current_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_HIDDEN_COLLDATA;
          desired_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_HIDDEN_COLLDATA;
        }
      }
      if (!use_hidden_ecb_lifetime && !lock_bottom_to_zero &&
          (specialhi_jobj_ecb_active || vanish_jobj_ecb_active || damageflyroll_jobj_ecb_active)) {
        if (have_cur_specialhi_ecb) {
          current_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_JOBJ;
        }
        if (have_prev_specialhi_ecb) {
          previous_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_JOBJ;
        }
        if (have_cur_vanish_ecb) {
          current_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_JOBJ;
        }
        if (have_prev_vanish_ecb) {
          previous_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_JOBJ;
        }
        if (have_cur_damageflyroll_ecb) {
          current_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_JOBJ;
        }
        if (have_prev_damageflyroll_ecb) {
          previous_ecb_source_mode = (uint8_t)MSL_MPCOLL_ECB_SOURCE_JOBJ;
        }
      }
      msl_mpcoll_loaded_ecb_set_current(&loaded_ecb, &cur_ecb_points, current_ecb_source_mode);
      msl_mpcoll_loaded_ecb_set_previous(&loaded_ecb, &prev_ecb_points, previous_ecb_source_mode);
      msl_mpcoll_loaded_ecb_set_desired(&loaded_ecb, &desired_ecb_points, desired_ecb_source_mode);
      mpcoll_ctx.loaded_ecb = &loaded_ecb;

      batch->state.coll_prev_env_flags[idx] = batch->state.coll_env_flags[idx];
      batch->state.coll_env_flags[idx] = 0;
      if (was_grounded || !is_damage_collision_landing_action(action_id) ||
          (batch->state.hitlag[idx] == 0u && batch->state.hitlag_pre_timer[idx] == 0u)) {
        batch->state.damage_hitlag_floorhug_latch[idx] = 0u;
      }
      MslMpcollCollDataState coll_data = {0};
      mpcoll_colldata_state_load(&mpcoll_ctx, &coll_data, source_phases);

      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      uint8_t on_ground = 0;
      uint8_t floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE;
      const uint16_t seed_ground_id = coll_data.floor_index;
      uint16_t ground_id = seed_ground_id;

      float floor_nx = 0.0f;
      float floor_ny = 1.0f;
      float contact_x = cur_bottom_x;
      float contact_y = 0.0f;
      const MslMpcollFloorWriteback floor_write = {
          .ctx = &mpcoll_ctx,
          .on_ground = &on_ground,
          .floor_result_mode = &floor_result_mode,
          .ground_id = &ground_id,
          .contact_x = &contact_x,
          .contact_y = &contact_y,
          .floor_nx = &floor_nx,
          .floor_ny = &floor_ny,
      };
      uint8_t escapeair_locked_root_publication_platform_hit = 0u;
      uint8_t escapeair_stale_platform_root_handoff_hit = 0u;
      uint8_t escapeair_fresh_jump_height_platform_handoff_hit = 0u;
      uint8_t escapeair_no_lock_static_platform_sweep_hit = 0u;
      const uint8_t escapeair_terminal_locked_static_platform_sweep_owner =
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_LoadECB_inline,
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active == 0u &&
           ecb_lock_timer_seed == 1u && prev_action_id == action_id &&
           stage_has_height_platform_transform == 0u &&
           batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
           msl_escapeair_locked_bottom_owner_any(
               batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
           mpcoll_floor_sweep_prev_root_is_source_owned(batch, idx))
              ? 1u
              : 0u;
      uint8_t common_fall_flags6_root_floor_projection_hit = 0u;
      uint8_t escapeair_live_cliff_ledge_bottom_sweep_owner = 0u;
      uint8_t escapeair_live_cliff_ledge_source_floor_owner = 0u;

      int prefer_line_idx = -1;
      int raw_current_floor_line_idx = -1;
      const uint16_t skip_platform_segment_i = coll_data.floor_skip_segment_id;
      uint8_t hidden_height_platform_remapped_to_solid_floor = 0u;
      if (ground_id != 0xFFFFu) {
        raw_current_floor_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
        prefer_line_idx = raw_current_floor_line_idx;
        if (!floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx)) {
          prefer_line_idx = -1;
          if (was_grounded || frame_start_grounded) {
            const int randall_line_idx =
                mpcoll_find_current_randall_floor_line_at_root(batch, bi, g, x, y);
            if (randall_line_idx >= 0) {
              // data/stages/bin/grst.bin::MSLSTG01 platform_transforms(kind=randall)
              prefer_line_idx = randall_line_idx;
            }
          }
        } else if (was_grounded) {
          int remap_line_idx = -1;
          if (hidden_height_platform_remaps_to_solid_floor(batch, bi, g, stage_id, prefer_line_idx,
                                                           x, y, &remap_line_idx)) {
            prefer_line_idx = remap_line_idx;
            hidden_height_platform_remapped_to_solid_floor = 1u;
          }
        }
      }
      uint8_t cliff_ledge_floor_owner_selected = 0u;
      if (action_consumes_cliff_ledge_floor_owner(char_id, action_id) &&
          batch->state.ledge_cooldown[idx] != 0 &&
          batch->state.cliff_ledge_floor_segment_id != NULL &&
          batch->state.cliff_ledge_floor_segment_id[idx] != 0xFFFFu) {
        const uint16_t cliff_ledge_floor_id = batch->state.cliff_ledge_floor_segment_id[idx];
        const int cliff_ledge_line_idx =
            stage_collision_floor_line_index(stage_id, cliff_ledge_floor_id);
        const uint8_t prefer_line_is_ledge_floor =
            (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].is_ledge) ? 1u : 0u;
        const uint8_t prefer_line_is_platform_floor =
            (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].is_platform) ? 1u : 0u;
        const uint8_t cliff_ledge_floor_strict_span_owner =
            (cliff_ledge_line_idx >= 0 &&
             floor_x_within_line_segment_strict(batch, bi, g, cliff_ledge_line_idx,
                                                batch->state.pos_x[idx]))
                ? 1u
                : 0u;
        const uint8_t cliff_ledge_floor_bottom_sweep_owner =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && cliff_ledge_line_idx >= 0 &&
             mpcoll_bottom_sweep_hits_segment(batch, idx, bi, g, stage_id, prev_bottom_x,
                                              prev_bottom_y, cur_bottom_x, cur_bottom_y,
                                              skip_platform_segment_i, cliff_ledge_line_idx, -1, c,
                                              cliff_ledge_floor_id))
                ? 1u
                : 0u;
        float cliff_ledge_floor_y = 0.0f;
        const uint8_t cliff_ledge_floor_y_valid =
            (cliff_ledge_line_idx >= 0 &&
             floor_line_y_at_x_for_env(batch, bi, g, cliff_ledge_line_idx, batch->state.pos_x[idx],
                                       &cliff_ledge_floor_y))
                ? 1u
                : 0u;
        const MslMpcollCarriedCliffLedgeFloorAuthority cliff_ledge_floor_authority =
            cliff_ledge_floor_y_valid
                ? mpcoll_carried_cliff_ledge_floor_authority(
                      batch, idx, bi, g, char_id, action_id, cliff_ledge_line_idx,
                      raw_current_floor_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
                      cliff_ledge_floor_y, ecb_lock_timer_seed,
                      cliff_ledge_floor_bottom_sweep_owner)
                : (MslMpcollCarriedCliffLedgeFloorAuthority){0};
        if ((prefer_line_idx < 0 || prefer_line_is_ledge_floor || prefer_line_is_platform_floor ||
             (cliff_ledge_floor_authority.candidate_matches &&
              cliff_ledge_floor_authority.owner_live && cliff_ledge_floor_authority.strict_span)) &&
            cliff_ledge_line_idx >= 0 &&
            floor_line_is_runtime_fighter_solid(g, stage_id, cliff_ledge_line_idx) &&
            g->lines[(size_t)cliff_ledge_line_idx].is_ledge &&
            cliff_ledge_floor_strict_span_owner) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044164
          const uint8_t raw_current_floor_matches_cliff_ledge_segment =
              (raw_current_floor_line_idx >= 0 &&
               g->lines[(size_t)raw_current_floor_line_idx].segment_i == cliff_ledge_floor_id)
                  ? 1u
                  : 0u;
          if (!raw_current_floor_matches_cliff_ledge_segment) {
            prefer_line_idx = cliff_ledge_line_idx;
            cliff_ledge_floor_owner_selected = 1u;
          }
        }
      }
      const uint8_t prefer_line_is_platform =
          (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].is_platform) ? 1u : 0u;
      const uint8_t prefer_line_is_slope =
          floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx);
      const uint8_t prefer_line_is_ledge =
          (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].is_ledge) ? 1u : 0u;
      const uint8_t prefer_line_has_platform_transform =
          (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].platform_transform_kind !=
                                       MSL_STAGE_PLATFORM_TRANSFORM_NONE)
              ? 1u
              : 0u;
      const uint8_t prefer_line_is_fighter_solid =
          (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].fighter_solid) ? 1u : 0u;
      const uint8_t prefer_line_is_terminal_cardinal_hard_floor =
          floor_line_is_terminal_cardinal_hard_floor(batch, bi, g, stage_id, prefer_line_idx);
      float prefer_line_root_y = 0.0f;
      const uint8_t prefer_line_root_y_valid =
          (prefer_line_idx >= 0 &&
           floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                     &prefer_line_root_y))
              ? 1u
              : 0u;
      const uint8_t prefer_line_is_platform_or_slope =
          (uint8_t)((prefer_line_is_platform || prefer_line_is_slope || prefer_line_is_ledge) ? 1u
                                                                                              : 0u);
      mpcoll_ctx.prefer_floor_line_idx = prefer_line_idx;
      const uint8_t cliff_ledge_floor_owner_matches_current_line =
          (prefer_line_is_ledge && action_consumes_cliff_ledge_floor_owner(char_id, action_id) &&
           batch->state.ledge_cooldown[idx] != 0 &&
           batch->state.cliff_ledge_floor_segment_id != NULL &&
           batch->state.cliff_ledge_floor_segment_id[idx] != 0xFFFFu &&
           prefer_line_idx == stage_collision_floor_line_index(
                                  stage_id, batch->state.cliff_ledge_floor_segment_id[idx]))
              ? 1u
              : 0u;
      const uint8_t cliff_ledge_floor_owner_live_current_line =
          // data/stages/bin/*.bin::MSLSTG01 alternate floor endpoint links
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_800471F8
          (stage_has_alternate_floor_endpoint_links &&
           cliff_ledge_floor_owner_matches_current_line &&
           batch->state.cliff_ledge_floor_segment_seeded != NULL &&
           batch->state.cliff_ledge_floor_segment_seeded[idx] == 0u)
              ? 1u
              : 0u;
      const uint8_t cliff_ledge_floor_owner_active =
          (cliff_ledge_floor_owner_selected || cliff_ledge_floor_owner_live_current_line) ? 1u : 0u;
      const MslEscapeAirCollEpisode escapeair_episode = msl_escapeair_coll_episode_make(
          msl_motion_state_class3_has(char_id, action_id, MSL_MS_CLASS3_PHASE4_ESCAPE_AIR_COLL),
          action_id, batch->state.seed_prev_action_id[idx], ecb_lock_active);
      const uint8_t escapeair_locked = escapeair_episode.locked;
      uint8_t deep_lock_penetration = 0u;
      const uint8_t downdamage_x_axis_fresh_sdi_edge =
          ((action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
            action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) &&
           fabsf(apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]),
                                c->lstick_deadzone_x)) >= c->lstick_tilt_x_thresh &&
           fabsf(apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_x[idx]),
                                c->lstick_deadzone_x)) < c->lstick_tilt_x_thresh)
              ? 1u
              : 0u;
      const uint8_t damage_active_hitlag_downward_sdi_airborne_owner =
          mpcoll_damage_active_hitlag_stay_airborne_floor_owner(
              batch, idx, action_id, (uint8_t)(prefer_line_idx >= 0 && prefer_line_root_y_valid),
              prefer_line_is_platform, prefer_line_is_ledge, prefer_line_is_slope,
              prefer_line_has_platform_transform, prefer_line_is_fighter_solid,
              prefer_line_is_terminal_cardinal_hard_floor, prefer_line_root_y, prev_y,
              downdamage_x_axis_fresh_sdi_edge);
      uint8_t damage_active_hitlag_root_below_bottom_above_floor_owner = 0u;
      if (batch->state.hitlag[idx] != 0u && is_damage_fly_collision_action(action_id) &&
          batch->state.tilt_timer_y_frame_start[idx] >= c->sdi_tilt_max_frames &&
          prefer_line_idx >= 0 && !prefer_line_is_platform && !prefer_line_is_ledge) {
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
        float current_bottom_floor_y = 0.0f;
        if (floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, cur_bottom_x,
                                      &current_bottom_floor_y) &&
            y < (current_bottom_floor_y - k_floor_y_bias) &&
            cur_bottom_y > (current_bottom_floor_y + k_floor_y_bias)) {
          damage_active_hitlag_root_below_bottom_above_floor_owner = 1u;
        }
      }

      MslMpcollOrderedWallCeilResult ordered_wall_ceil = {
          .left_right_flags = 0u,
          .squeeze_flags = 0u,
          .squeeze_flags_all = 0u,
          .hit_ceiling = 0u,
          .hit_floor = 0u,
          .touching_floor = 0u,
          .left_wall_id = 0xFFFFu,
          .right_wall_id = 0xFFFFu,
          .x_after_left_wall = 0.0f,
          .x_after_right_wall = 0.0f,
          .y_after_ceiling = 0.0f,
          .y_after_floor = 0.0f,
          .cur_ecb_after = {0},
      };
      if (was_grounded) {
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
        batch->state.wall_kind[idx] = 0u;
        batch->state.wall_contact_x[idx] = 0.0f;
        batch->state.wall_contact_y[idx] = 0.0f;
        batch->state.wall_normal_x[idx] = 0.0f;
        batch->state.wall_normal_y[idx] = 0.0f;
        batch->state.ceiling_contact_x[idx] = 0.0f;
        batch->state.ceiling_contact_y[idx] = 0.0f;
        batch->state.ceiling_normal_x[idx] = 0.0f;
        batch->state.ceiling_normal_y[idx] = 0.0f;
        mpcoll_grounded_wall_ceil_ordered_begin(&mpcoll_ctx, &prev_ecb_points, &cur_ecb_points,
                                                &ordered_wall_ceil);
        cur_ecb_points = ordered_wall_ceil.cur_ecb_after;
        msl_mpcoll_loaded_ecb_set_current(&loaded_ecb, &cur_ecb_points, loaded_ecb.current_mode);
        cur_bottom_x = cur_ecb_points.bottom_x;
        cur_bottom_y = cur_ecb_points.bottom_y;
      }

      if (!was_grounded && escapeair_early_ledge_root_floor_owner && prefer_line_idx >= 0) {
        float y_corr = 0.0f;
        const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, x, y,
                                                          &y_corr, &floor_nx, &floor_ny);
        const float max_lift = fabsf(batch->state.speed_y_self[idx]) +
                               msl_ecb_bottom_rel_y(char_id, anim, 0) + k_ecb_vertical_unit;
        if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift) {
          batch->state.pos_y[idx] += y_corr - k_floor_y_bias;
          mpcoll_publish_direct_source_floor_hit(&floor_write,
                                                 (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION,
                                                 g->lines[(size_t)out_line_idx].segment_i, x,
                                                 y + y_corr - k_floor_y_bias, floor_nx, floor_ny);
        } else {
          float ledge_line_y = 0.0f;
          if (floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, x, &ledge_line_y) &&
              prev_y > (ledge_line_y + k_floor_y_bias) && y <= ledge_line_y) {
            batch->state.pos_y[idx] = ledge_line_y + k_floor_y_bias;
            mpcoll_publish_direct_source_floor_hit(
                &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION,
                g->lines[(size_t)prefer_line_idx].segment_i, x, ledge_line_y, floor_nx, floor_ny);
          }
        }
      }

      if (!was_grounded && !on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          mpcoll_ground_escapeair_fall_iasa_source_owner(batch, idx) &&
          batch->state.action_frame[idx] <= 1 &&
          mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx) && prefer_line_idx >= 0 &&
          (size_t)prefer_line_idx < g->line_count) {
        // data/stages/bin/*.bin::MSLSTG01 ledge floor flags + source endpoints
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8,mpColl_80044628_Floor}
        // refs/melee/src/melee/mp/mplib.c::mpLineIntersectionH
        const MslStageFloorLine* carried_line = &g->lines[(size_t)prefer_line_idx];
        const uint8_t carried_cardinal_ledge_floor =
            (uint8_t)(carried_line->segment_i == batch->state.ground_id[idx] &&
                      carried_line->is_platform == 0u && carried_line->is_ledge != 0u &&
                      carried_line->platform_transform_kind == MSL_STAGE_PLATFORM_TRANSFORM_NONE &&
                      fabsf(carried_line->y1 - carried_line->y0) <= k_floor_horiz_dy_thresh &&
                      floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx));
        float prev_floor_y = 0.0f;
        float cur_floor_y = 0.0f;
        if (carried_cardinal_ledge_floor &&
            floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx,
                                      batch->state.floor_sweep_prev_pos_x[idx], &prev_floor_y) &&
            floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, x, &cur_floor_y) &&
            batch->state.floor_sweep_prev_pos_y[idx] >= (prev_floor_y - k_floor_horiz_dy_thresh) &&
            batch->state.floor_sweep_prev_pos_y[idx] <= (prev_floor_y + k_floor_y_bias) &&
            y < (cur_floor_y - k_floor_horiz_dy_thresh)) {
          batch->state.pos_y[idx] = cur_floor_y + k_floor_y_bias;
          mpcoll_publish_direct_source_floor_hit(
              &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, carried_line->segment_i, x,
              cur_floor_y, 0.0f, 1.0f);
        }
      }

      // Fresh JumpAerial -> EscapeAir ledge bottom-sweep handoff:
      if (!was_grounded && !on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          prev_action_id != action_id && prev_action_is_jumpaerial && prefer_line_idx >= 0 &&
          batch->state.seed_prev_action_frame[idx] <= 2 && batch->state.action_frame[idx] <= 2) {
        // data/stages/bin/*.bin::MSLSTG01 ledge floor segments + prev/next links
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8,mpCheckFloor}
        const float source_prev_bottom_y = (prev_y + pre_entry_jumpaerial_ecb_rel);
        MslMpcollFloorSweepResult floor_sweep = {0};
        if (mpcoll_collect_bottom_sweep_hit(
                batch, idx, bi, g, stage_id, prev_bottom_x, source_prev_bottom_y, cur_bottom_x,
                cur_bottom_y, skip_platform_segment_i, prefer_line_idx, -1, c, &floor_sweep) &&
            floor_sweep.hit_is_ledge && !floor_sweep.hit_is_platform &&
            !floor_sweep.hit_has_platform_transform && batch->state.ledge_cooldown[idx] != 0u &&
            batch->state.cliff_ledge_floor_segment_id != NULL &&
            batch->state.cliff_ledge_floor_segment_id[idx] != 0xFFFFu &&
            floor_sweep.hit_segment_id == batch->state.cliff_ledge_floor_segment_id[idx] &&
            !floor_x_within_line_bounds(batch, bi, g, floor_sweep.hit_line_idx, prev_x) &&
            floor_x_within_line_bounds(batch, bi, g, floor_sweep.hit_line_idx, x)) {
          const int hit_line_idx = floor_sweep.hit_line_idx;
          const float hit_x = floor_sweep.hit_x;
          const float hit_y = floor_sweep.hit_y;
          batch->state.pos_y[idx] = hit_y + k_floor_y_bias;
          mpcoll_publish_direct_source_floor_hit(&floor_write, floor_sweep.mode,
                                                 g->lines[(size_t)hit_line_idx].segment_i, hit_x,
                                                 hit_y, floor_sweep.normal_x, floor_sweep.normal_y);
        }
      }

      uint8_t escapeair_missing_bottom_hard_floor_sweep_owner = 0u;
      if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          prefer_line_has_platform_transform && prefer_line_idx >= 0 &&
          batch->state.speed_y_self[idx] < 0.0f) {
        // data/stages/bin/*.bin::MSLSTG01 platform_transform metadata
        // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
        const float escapeair_bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
        MslMpcollFloorSweepResult transformed_platform_entry_hard_floor_sweep = {0};
        if (mpcoll_collect_bottom_sweep_hit(batch, idx, bi, g, stage_id, prev_x,
                                            prev_y + escapeair_bottom_rel0, x,
                                            y + escapeair_bottom_rel0, skip_platform_segment_i, -1,
                                            -1, c, &transformed_platform_entry_hard_floor_sweep) &&
            transformed_platform_entry_hard_floor_sweep.hit_line_idx >= 0 &&
            !transformed_platform_entry_hard_floor_sweep.hit_is_platform &&
            !transformed_platform_entry_hard_floor_sweep.hit_has_platform_transform) {
          const int line_idx = transformed_platform_entry_hard_floor_sweep.hit_line_idx;
          batch->state.pos_y[idx] =
              transformed_platform_entry_hard_floor_sweep.hit_y + k_floor_y_bias;
          escapeair_missing_bottom_hard_floor_sweep_owner = 1u;
          mpcoll_publish_direct_source_floor_hit(
              &floor_write, transformed_platform_entry_hard_floor_sweep.mode,
              g->lines[(size_t)line_idx].segment_i,
              transformed_platform_entry_hard_floor_sweep.hit_x,
              transformed_platform_entry_hard_floor_sweep.hit_y,
              transformed_platform_entry_hard_floor_sweep.normal_x,
              transformed_platform_entry_hard_floor_sweep.normal_y);
        }
      }
      if (!on_ground) {
        MslMpcollFloorHit entry_471f8_hit = {0};
        if (msl_mpcoll_800471f8_escapeair_entry_floor_publication(
                batch, idx, bi, g, stage_id, prefer_line_idx, was_grounded,
                stage_has_only_static_cardinal_hard_floors, stage_has_height_platform_transform,
                ecb_lock_timer_seed, prev_x, prev_y, x, y, msl_ecb_bottom_rel_y(char_id, anim, 0),
                skip_platform_segment_i, c, &entry_471f8_hit)) {
          escapeair_missing_bottom_hard_floor_sweep_owner = 1u;
          mpcoll_publish_direct_source_floor_hit_packet(&floor_write, &entry_471f8_hit);
        }
      }

      const uint8_t common_damage_post_unlock_pose_bottom_owner =
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
          // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 DAMAGE_GROUND + DAMAGE_COMMON_COLL
          (is_common_damage_ground_pose_ecb_action(action_id) &&
           batch->state.on_ground[idx] == 0u && batch->state.hitlag[idx] == 0u &&
           batch->state.hitstun[idx] != 0u && ecb_lock_timer == 0u &&
           mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx))
              ? 1u
              : 0u;

      if (!on_ground && was_grounded && prefer_line_idx >= 0 &&
          !common_damage_post_unlock_pose_bottom_owner) {
        float y_corr = 0.0f;
        const int out_line_idx =
            msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                     &y_corr, &floor_nx, &floor_ny);
        if (out_line_idx >= 0) {
          int resolved_line_idx = out_line_idx;
          const uint8_t down_bound_slope_root_snap =
              (uint8_t)(action_is_down_bound(action_id) &&
                        (floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx) ||
                         floor_line_is_generated_stage_slope(batch, bi, g, out_line_idx)));
          if (down_bound_slope_root_snap) {
            float root_line_y = 0.0f;
            if (floor_line_y_at_x_for_env(batch, bi, g, out_line_idx, batch->state.pos_x[idx],
                                          &root_line_y)) {
              // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
              // refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B108,mplib.c::mpLib_8004DD90_Floor}
              y_corr = (root_line_y + k_floor_y_bias) - batch->state.pos_y[idx];
            }
          }
          if (!down_bound_slope_root_snap && resolved_line_idx != prefer_line_idx &&
              floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx) &&
              !floor_line_is_generated_stage_slope(batch, bi, g, resolved_line_idx)) {
            float root_line_y = 0.0f;
            if (floor_line_y_at_x_for_env(batch, bi, g, resolved_line_idx, batch->state.pos_x[idx],
                                          &root_line_y)) {
              // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
              y_corr = (root_line_y + k_floor_y_bias) - batch->state.pos_y[idx];
            }
          }
          const uint8_t same_step_height_platform_reproject = grounded_height_platform_reproject(
              batch, bi, g, stage_id, idx, action_id, batch->state.action_frame[idx],
              resolved_line_idx, &resolved_line_idx, &y_corr);
          if (hidden_height_platform_remapped_to_solid_floor &&
              resolved_line_idx == prefer_line_idx) {
            float root_line_y = 0.0f;
            if (floor_line_y_at_x_for_env(batch, bi, g, resolved_line_idx, batch->state.pos_x[idx],
                                          &root_line_y)) {
              y_corr = (root_line_y + k_floor_y_bias) - batch->state.pos_y[idx];
            }
          }
          if (resolved_line_idx != prefer_line_idx &&
              stage_collision_floor_line_has_height_platform_transform(
                  stage_id, g->lines[(size_t)resolved_line_idx].segment_i) &&
              stage_collision_floor_line_height_platform_state_is_source_trusted(
                  batch, bi, g->lines[(size_t)resolved_line_idx].segment_i) &&
              !same_step_height_platform_reproject &&
              !grounded_entry_same_step_height_platform_admits_line(
                  batch, bi, idx, stage_id, action_id,
                  g->lines[(size_t)resolved_line_idx].segment_i)) {
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC}
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            float root_line_y = 0.0f;
            if (floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                          &root_line_y)) {
              y_corr = (root_line_y + k_floor_y_bias) - batch->state.pos_y[idx];
            } else {
              y_corr = 0.0f;
            }
            resolved_line_idx = prefer_line_idx;
          }
          if (action_id == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL &&
              batch->state.action_frame[idx] <= 1 &&
              stage_collision_floor_line_has_height_platform_transform(
                  stage_id, g->lines[(size_t)resolved_line_idx].segment_i) &&
              stage_collision_floor_line_height_platform_state_is_source_trusted(
                  batch, bi, g->lines[(size_t)resolved_line_idx].segment_i) &&
              !same_step_height_platform_reproject) {
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
            y_corr = 0.0f;
          }
          if (msl_mpcoll_8004b108_capture_lw_flat_ledge_carry_owner(
                  batch, idx, bi, g, stage_id, action_id,
                  (resolved_line_idx >= 0 && (size_t)resolved_line_idx < g->line_count)
                      ? g->lines[(size_t)resolved_line_idx].segment_i
                      : 0xFFFFu,
                  prefer_line_idx >= 0 && (size_t)prefer_line_idx < g->line_count
                      ? g->lines[(size_t)prefer_line_idx].segment_i
                      : 0xFFFFu,
                  prefer_line_idx)) {
            resolved_line_idx = prefer_line_idx;
            y_corr =
                (g->lines[(size_t)prefer_line_idx].y0 + k_floor_y_bias) - batch->state.pos_y[idx];
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION,
                g->lines[(size_t)prefer_line_idx].segment_i, cur_bottom_x,
                g->lines[(size_t)prefer_line_idx].y0, 0.0f, 1.0f);
          }
          const uint8_t keep_grounded_damage_hitlag_floor_snap =
              grounded_damage_hitlag_allows_downward_floor_projection(batch, idx, action_id);
          const uint8_t keep_capture_lw_floor_snap =
              is_capture_lw_allow_ground_to_air_collision_action(action_id);
          const uint8_t keep_slope_or_platform_floor_snap =
              grounded_persistence_allows_signed_dd90_y_correction(
                  batch, bi, g, prefer_line_idx, resolved_line_idx, idx, action_id,
                  batch->state.action_frame[idx]);
          if (y_corr < 0.0f && !keep_grounded_damage_hitlag_floor_snap &&
              !keep_capture_lw_floor_snap && !keep_slope_or_platform_floor_snap &&
              !down_bound_slope_root_snap && !hidden_height_platform_remapped_to_solid_floor) {
            y_corr = 0.0f;
          }
          // refs/melee/src/melee/ft/ft_081B.c::{ft_800844EC,ft_80082708}
          // refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B108,mplib.c::mpLib_8004DD90_Floor}
          batch->state.pos_y[idx] += y_corr;
          on_ground = 1;
          floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION;
          ground_id = g->lines[(size_t)resolved_line_idx].segment_i;
          contact_y = (cur_bottom_y + y_corr);
        } else {
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
          uint8_t snapped_edge = 0;

          if (mpcoll_source_phases_allow_floor_edge_snap(coll_data.source_phases) &&
              msl_mpcoll_8004a45c_floor_edge_snap(batch, idx, bi, g, stage_id, prefer_line_idx,
                                                  cur_bottom_x, char_id, anim, ecb_frame,
                                                  was_grounded, &ground_id, &contact_x, &contact_y,
                                                  &floor_nx, &floor_ny)) {
            batch->state.pos_x[idx] += (contact_x - cur_bottom_x);
            batch->state.pos_y[idx] = contact_y;
            on_ground = 1u;
            floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_EDGE_SNAP;
            snapped_edge = 1u;
          }

          if (!snapped_edge) {
            const uint8_t landing_release_skip_floor_sweep =
                (action_uses_landing_floor_release_coll(action_id) &&
                 batch->state.action_frame[idx] <= 1)
                    ? 1u
                    : 0u;
            const float shared_sweep_prev_bottom_y =
                common_damage_post_unlock_pose_bottom_owner
                    ? (prev_y + mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_prev, 0u))
                    : prev_bottom_y;
            const float shared_sweep_cur_bottom_y =
                common_damage_post_unlock_pose_bottom_owner
                    ? (y + mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_cur, 0u))
                    : cur_bottom_y;
            // refs/melee/src/melee/mp/mplib.c::mpCheckFloor (the `if (ay >= by && mpLineIntersectionH(...))` gate)
            const uint8_t can_sweep =
                (uint8_t)(shared_sweep_cur_bottom_y <= shared_sweep_prev_bottom_y);
            const int floor_sweep_skip_line_idx =
                // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor
                cliff_ledge_floor_owner_active ? -1 : prefer_line_idx;
            MslMpcollFloorSweepResult floor_sweep = {0};
            const uint8_t floor_probe_tracks_shared_sweep =
                (mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_471F8) ||
                 mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_477E0) ||
                 mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_473CC))
                    ? 1u
                    : 0u;
            const uint8_t floor_probe_shared_candidate =
                (floor_probe_tracks_shared_sweep && !landing_release_skip_floor_sweep &&
                 !is_common_fallspecial_action(action_id))
                    ? 1u
                    : 0u;
            if (floor_probe_shared_candidate) {
              mpcoll_floor_probe_begin(
                  &mpcoll_ctx,
                  mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_477E0)
                      ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_477E0
                      : (mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_471F8)
                             ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_471F8
                             : (mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_473CC)
                                    ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_473CC
                                    : (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_47E14)),
                  source_phases, prefer_line_idx,
                  (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
              mpcoll_floor_probe_bottom_interval(&mpcoll_ctx, prev_bottom_x,
                                                 shared_sweep_prev_bottom_y, cur_bottom_x,
                                                 shared_sweep_cur_bottom_y);
            }
            const uint8_t shared_floor_sweep_hit =
                (!landing_release_skip_floor_sweep && !is_common_fallspecial_action(action_id) &&
                 can_sweep &&
                 mpcoll_collect_bottom_sweep_floor_result(
                     batch, idx, bi, g, stage_id, prev_bottom_x, shared_sweep_prev_bottom_y,
                     cur_bottom_x, shared_sweep_cur_bottom_y, skip_platform_segment_i,
                     prefer_line_idx, floor_sweep_skip_line_idx, c, &floor_sweep))
                    ? 1u
                    : 0u;
            if (floor_probe_shared_candidate) {
              mpcoll_floor_probe_result(
                  &mpcoll_ctx, shared_floor_sweep_hit ? &floor_sweep : NULL, shared_floor_sweep_hit,
                  (uint8_t)(shared_floor_sweep_hit && floor_sweep.projected_line_idx >= 0),
                  shared_floor_sweep_hit ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION
                                         : (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
            }
            if (!landing_release_skip_floor_sweep && !is_common_fallspecial_action(action_id) &&
                can_sweep && shared_floor_sweep_hit) {
              const int hit_line_idx = floor_sweep.hit_line_idx;
              const int out_line_idx2 = floor_sweep.projected_line_idx;
              const float ix = floor_sweep.hit_x;
              const float iy = floor_sweep.hit_y;
              const float y_corr2 = floor_sweep.projected_y_corr;
              floor_nx = floor_sweep.normal_x;
              floor_ny = floor_sweep.normal_y;
              const uint8_t hit_line_is_platform = floor_sweep.hit_is_platform;
              const uint8_t hit_line_is_ledge = floor_sweep.hit_is_ledge;
              const uint8_t hit_line_has_platform_transform =
                  floor_sweep.hit_has_platform_transform;
              const uint8_t hit_line_has_height_platform_transform =
                  floor_sweep.hit_has_height_platform_transform;
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
              const uint16_t resolved_segment_i = floor_sweep.projected_segment_id;
              const uint8_t resolved_line_has_platform_transform =
                  floor_sweep.projected_has_platform_transform;
              const uint8_t resolved_line_has_height_platform_transform =
                  floor_sweep.projected_has_height_platform_transform;
              const uint8_t resolved_line_is_platform = floor_sweep.projected_is_platform;
              const uint8_t resolved_line_is_ledge = floor_sweep.projected_is_ledge;
              const uint8_t escapeair_entry_locked_platform_airborne =
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80047E14}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 4 && hit_line_is_platform &&
                   (hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   (ecb_lock_active || (prev_action_id == action_id &&
                                        resolved_segment_i != batch->state.ground_id[idx])))
                      ? 1u
                      : 0u;
              const uint8_t escapeair_jumpaerial_entry_ledge_airborne =
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
                  (escapeair_jumpaerial_entry && ecb_lock_active &&
                   batch->state.seed_prev_action_frame[idx] >= 2 &&
                   batch->state.seed_prev_action_frame[idx] <= 4 &&
                   (hit_line_is_ledge || resolved_line_is_ledge))
                      ? 1u
                      : 0u;
              const uint8_t specialhi_fall_understage_hard_floor_clip =
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
                  (specialhi_understage_floor_clip_action(batch->state.char_id[idx], action_id) &&
                   !hit_line_is_platform && !resolved_line_is_platform &&
                   prev_y < (iy - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                             k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t specialhi_from_below_hard_floor_clip =
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
                  (specialhi_floor_candidate_starts_below_source_floor(
                       batch->state.char_id[idx], action_id, prev_y, prev_bottom_y, cur_bottom_y,
                       iy, batch->state.speed_y_self[idx]) &&
                   !hit_line_is_platform && !resolved_line_is_platform)
                      ? 1u
                      : 0u;
              const float transformed_platform_bottom_penetration = iy - cur_bottom_y;
              const uint8_t damage_transformed_platform_offspan_contact =
                  // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (((hit_line_has_platform_transform &&
                     !floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, x)) ||
                    (resolved_line_has_platform_transform && out_line_idx2 >= 0 &&
                     !floor_x_within_line_segment_strict(batch, bi, g, out_line_idx2, x))) &&
                   is_damage_collision_landing_action(action_id) &&
                   batch->state.speed_y_attack[idx] > 0.0f && batch->state.hitstun[idx] != 0u)
                      ? 1u
                      : 0u;
              const MslCharParams* damage_floor_chp = msl_char_params_fast(char_id);
              const float damage_height_platform_edge_slack =
                  (damage_floor_chp != NULL && isfinite(damage_floor_chp->ledge_snap_height))
                      ? (damage_floor_chp->ledge_snap_height * batch->state.fighter_scale_y[idx])
                      : k_ecb_vertical_unit;
              const uint8_t hit_line_height_platform_state_trusted =
                  (hit_line_has_height_platform_transform &&
                   stage_collision_floor_line_height_platform_state_is_source_trusted(
                       batch, bi, g->lines[(size_t)hit_line_idx].segment_i))
                      ? 1u
                      : 0u;
              const uint8_t resolved_line_height_platform_state_trusted =
                  (resolved_line_has_height_platform_transform &&
                   stage_collision_floor_line_height_platform_state_is_source_trusted(
                       batch, bi, resolved_segment_i))
                      ? 1u
                      : 0u;
              const uint8_t hit_line_height_platform_state_current_owned =
                  (hit_line_has_height_platform_transform &&
                   stage_collision_floor_line_height_platform_state_is_current_owned(
                       batch, bi, g->lines[(size_t)hit_line_idx].segment_i))
                      ? 1u
                      : 0u;
              const uint8_t resolved_line_height_platform_state_current_owned =
                  (resolved_line_has_height_platform_transform &&
                   stage_collision_floor_line_height_platform_state_is_current_owned(
                       batch, bi, resolved_segment_i))
                      ? 1u
                      : 0u;
              const uint8_t damage_height_platform_live_bottom_crossing =
                  ((hit_line_height_platform_state_trusted ||
                    resolved_line_height_platform_state_trusted) &&
                   prev_bottom_y > (iy + k_floor_y_bias) && cur_bottom_y <= (iy + k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t damage_height_platform_pending_owner =
                  // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
                  // data/characters/{fox,falco}.json::ledge_snap_height
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
                  // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor}
                  // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLineIntersectionH}
                  (((hit_line_has_height_platform_transform &&
                     floor_line_x_near_endpoint_for_env(batch, bi, g, hit_line_idx, x,
                                                        damage_height_platform_edge_slack)) ||
                    (resolved_line_has_height_platform_transform && out_line_idx2 >= 0 &&
                     floor_line_x_near_endpoint_for_env(batch, bi, g, out_line_idx2, x,
                                                        damage_height_platform_edge_slack))) &&
                   !damage_height_platform_live_bottom_crossing &&
                   is_damage_collision_landing_action(action_id) &&
                   batch->state.speed_y_attack[idx] > 0.0f && batch->state.hitstun[idx] != 0u)
                      ? 1u
                      : 0u;
              const uint8_t damageair_height_platform_missing_current_owner =
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
                  (((hit_line_has_height_platform_transform &&
                     !hit_line_height_platform_state_current_owned) ||
                    (resolved_line_has_height_platform_transform &&
                     !resolved_line_height_platform_state_current_owned)) &&
                   mpcoll_damageair_action(action_id) && batch->state.hitlag[idx] == 0u &&
                   batch->state.hitstun[idx] != 0u)
                      ? 1u
                      : 0u;
              const uint8_t damage_terminal_height_platform_stale_floor =
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  ((hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   is_damage_fly_collision_action(action_id) && batch->state.hitstun[idx] <= 4u &&
                   !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]) &&
                   transformed_platform_bottom_penetration > (0.5f * k_ecb_vertical_unit))
                      ? 1u
                      : 0u;
              const uint8_t suppress_damage_transformed_platform_ecb_only_land =
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
                  // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpCheckFloor}
                  (damage_transformed_platform_offspan_contact ||
                   damage_height_platform_pending_owner ||
                   damageair_height_platform_missing_current_owner ||
                   damage_terminal_height_platform_stale_floor)
                      ? 1u
                      : 0u;
              const uint8_t attackair_transformed_platform_projection_pass_owner =
                  ((skip_platform_segment_i != 0xFFFFu &&
                    ((hit_line_has_height_platform_transform &&
                      floor_line_is_skipped_platform(stage_id, g, hit_line_idx,
                                                     skip_platform_segment_i)) ||
                     (resolved_line_has_height_platform_transform &&
                      floor_line_is_skipped_platform(stage_id, g, out_line_idx2,
                                                     skip_platform_segment_i)))) ||
                   ((hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                    platform_pass_input_below_raw_threshold(batch, idx, c) && c != NULL &&
                    stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                        c->platform_air_land_stick_y_threshold))
                      ? 1u
                      : 0u;
              const uint8_t suppress_attackair_transformed_platform_ecb_only_land =
                  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
                  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  ((hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   attackair_transformed_platform_projection_pass_owner &&
                   late_attackair_platform_ecb_owner && prev_action_id == action_id &&
                   move_tables_attackair_second_create_hitbox_phase(
                       char_id, action_id, batch->state.anim_frame_f32[idx]) &&
                   prev_y < iy && y < iy &&
                   transformed_platform_bottom_penetration > k_floor_y_bias)
                      ? 1u
                      : 0u;
              const uint8_t suppress_fallspecial_b_transformed_platform_skip =
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
                  ((hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   action_id == (uint16_t)MSL_ACT_FALL_SPECIAL_B &&
                   !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]))
                      ? 1u
                      : 0u;
              const uint8_t suppress_cliff_ledge_locked_zero_bottom_hit =
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  (cliff_ledge_floor_owner_active &&
                   batch->state.cliff_ledge_floor_segment_seeded != NULL &&
                   batch->state.cliff_ledge_floor_segment_seeded[idx] != 0u && hit_line_idx >= 0 &&
                   (g->lines[(size_t)hit_line_idx].segment_i ==
                        batch->state.cliff_ledge_floor_segment_id[idx] ||
                    resolved_segment_i == batch->state.cliff_ledge_floor_segment_id[idx]) &&
                   !escapeair_live_cliff_ledge_bottom_sweep_owner && ecb_lock_timer_seed > 1u)
                      ? 1u
                      : 0u;
              const uint8_t escapeair_low_floor_raw_hit_over_stale_platform_remap =
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
                  // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 4 && !hit_line_is_platform &&
                   resolved_line_has_platform_transform && resolved_line_is_platform &&
                   iy <= k_floor_y_bias && (cur_bottom_y + y_corr2) > k_floor_y_bias)
                      ? 1u
                      : 0u;
              if (escapeair_low_floor_raw_hit_over_stale_platform_remap) {
                batch->state.pos_x[idx] += (ix - cur_bottom_x);
                batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
                on_ground = 1;
                floor_result_mode = floor_sweep.mode;
                ground_id = g->lines[(size_t)hit_line_idx].segment_i;
                contact_x = ix;
                contact_y = iy;
              } else {
                MslMpcollFloorSweepResult attackair_hard_floor_substep = {0};
                const uint8_t attackair_hard_floor_substep_owner =
                    // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_471F8
                    // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform_transform metadata
                    // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
                    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
                    (suppress_attackair_transformed_platform_ecb_only_land &&
                     action_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
                     mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_471F8) &&
                     mpcoll_collect_bottom_sweep_hard_floor_result(
                         batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                         cur_bottom_y, prefer_line_idx, -1, 0u, &attackair_hard_floor_substep) &&
                     attackair_hard_floor_substep.projected_line_idx >= 0 &&
                     attackair_hard_floor_substep.projected_y_corr >= 0.0f)
                        ? 1u
                        : 0u;
                if (attackair_hard_floor_substep_owner) {
                  batch->state.pos_x[idx] += (attackair_hard_floor_substep.hit_x - cur_bottom_x);
                  batch->state.pos_y[idx] += attackair_hard_floor_substep.projected_y_corr;
                  mpcoll_publish_direct_source_floor_hit(
                      &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
                      attackair_hard_floor_substep.projected_segment_id,
                      attackair_hard_floor_substep.hit_x, attackair_hard_floor_substep.hit_y,
                      attackair_hard_floor_substep.normal_x, attackair_hard_floor_substep.normal_y);
                } else if (escapeair_entry_locked_platform_airborne ||
                           escapeair_jumpaerial_entry_ledge_airborne ||
                           specialhi_fall_understage_hard_floor_clip ||
                           specialhi_from_below_hard_floor_clip ||
                           suppress_damage_transformed_platform_ecb_only_land ||
                           suppress_attackair_transformed_platform_ecb_only_land ||
                           suppress_fallspecial_b_transformed_platform_skip ||
                           suppress_cliff_ledge_locked_zero_bottom_hit) {
                  batch->state.coll_env_flags[idx] |= floor_edge_suppression_flags(
                      batch, idx, stage_id, g, hit_line_idx, char_id, anim, ecb_frame, was_grounded,
                      loaded_ecb.current);
                } else {
                  batch->state.pos_x[idx] += (ix - cur_bottom_x);

                  if (out_line_idx2 >= 0) {
                    batch->state.pos_y[idx] += y_corr2;
                    mpcoll_publish_direct_source_floor_hit(
                        &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
                        g->lines[(size_t)out_line_idx2].segment_i, ix, iy, floor_nx, floor_ny);
                  } else {
                    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
                    batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
                    mpcoll_publish_direct_source_floor_hit(
                        &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
                        g->lines[(size_t)hit_line_idx].segment_i, ix, iy, floor_nx, floor_ny);
                  }
                }
              }
            } else if (!landing_release_skip_floor_sweep &&
                       msl_mpcoll_80044628_floor_wall_adjacent_fallback(
                           batch, idx, bi, g, stage_id, &ordered_wall_ceil, cur_bottom_x,
                           cur_bottom_y, skip_platform_segment_i, &ground_id, &contact_x,
                           &contact_y, &floor_nx, &floor_ny)) {
              on_ground = 1u;
              floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
            } else {
              batch->state.coll_env_flags[idx] |=
                  floor_edge_suppression_flags(batch, idx, stage_id, g, prefer_line_idx, char_id,
                                               anim, ecb_frame, was_grounded, loaded_ecb.current);
            }
          }
        }
      } else {
        int hit_line_idx = -1;
        float ix = 0.0f, iy = 0.0f;
        float damage_hitlag_exit_floor_y = 0.0f;
        const uint8_t damage_hitlag_exit_floor_y_valid =
            (prefer_line_idx >= 0 &&
             floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       &damage_hitlag_exit_floor_y))
                ? 1u
                : 0u;
        const float damage_hitlag_exit_prev_floor_dy =
            batch->state.floor_sweep_prev_pos_y[idx] -
            (damage_hitlag_exit_floor_y + k_floor_y_bias);
        const float damage_hitlag_exit_frame_start_floor_dy =
            batch->state.prev_pos_y[idx] - (damage_hitlag_exit_floor_y + k_floor_y_bias);
        const uint8_t damagefly_hitlag_exit_resting_hard_floor_owner =
            (action_id == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP && prefer_line_idx >= 0 &&
             !g->lines[(size_t)prefer_line_idx].is_platform &&
             !g->lines[(size_t)prefer_line_idx].is_ledge &&
             !floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx) &&
             damage_hitlag_exit_floor_y_valid &&
             isfinite(batch->state.floor_sweep_prev_pos_x[idx]) &&
             isfinite(batch->state.floor_sweep_prev_pos_y[idx]) &&
             floor_x_within_line_bounds(batch, bi, g, prefer_line_idx,
                                        batch->state.floor_sweep_prev_pos_x[idx]) &&
             fabsf(damage_hitlag_exit_prev_floor_dy) <= k_floor_horiz_dy_thresh &&
             fabsf(damage_hitlag_exit_frame_start_floor_dy) <= k_floor_horiz_dy_thresh &&
             batch->state.pos_y[idx] < (damage_hitlag_exit_floor_y - k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t damage_hitlag_exit_floor_precondition =
            (!is_damage_fly_collision_action(action_id) ||
             damagefly_hitlag_exit_resting_hard_floor_owner ||
             (damage_hitlag_exit_floor_y_valid &&
              cur_bottom_y <= (damage_hitlag_exit_floor_y + k_floor_y_bias)))
                ? 1u
                : 0u;
        const uint8_t damage_hitlag_exit_projection_owner =
            (is_damage_collision_landing_action(action_id) &&
             batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u &&
             (batch->state.damage_hitlag_floorhug_latch[idx] != 0u ||
              damagefly_hitlag_exit_resting_hard_floor_owner) &&
             damage_hitlag_exit_floor_precondition)
                ? 1u
                : 0u;
        float damage_hitlag_exit_source_prev_floor_y = 0.0f;
        const uint8_t damage_hitlag_exit_source_prev_above_floor =
            (damage_hitlag_exit_floor_y_valid &&
             isfinite(batch->state.floor_sweep_prev_pos_x[idx]) &&
             isfinite(batch->state.floor_sweep_prev_pos_y[idx]) &&
             floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx,
                                       batch->state.floor_sweep_prev_pos_x[idx],
                                       &damage_hitlag_exit_source_prev_floor_y) &&
             floor_x_within_line_bounds(batch, bi, g, prefer_line_idx,
                                        batch->state.floor_sweep_prev_pos_x[idx]) &&
             batch->state.floor_sweep_prev_pos_y[idx] >
                 (damage_hitlag_exit_source_prev_floor_y + k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t suppress_damageflyroll_hitlag_exit_floor_land =
            (action_id == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL &&
             batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u &&
             damage_hitlag_exit_source_prev_above_floor &&
             batch->state.speed_y_attack[idx] < 0.0f && damage_hitlag_exit_projection_owner == 0u)
                ? 1u
                : 0u;
        const uint8_t damageflyroll_iasa_lockout =
            (action_id == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL &&
             msl_state_flags_221c_hitstun_at(batch->state.state_flags, idx) &&
             batch->state.hitlag_pre_timer[idx] == 0u && batch->state.hitlag[idx] == 0u)
                ? 1u
                : 0u;
        const uint8_t active_damage_hitlag_carried_source_floor_contact =
            (batch->state.coll_damage_hitlag_floor_contact_runtime[idx] != 0u) ? 1u : 0u;
        const uint8_t active_damage_hitlag_stay_airborne_floor_owner =
            (batch->state.hitlag[idx] != 0u && is_damage_collision_landing_action(action_id) &&
             prefer_line_idx >= 0 &&
             action_uses_active_hitlag_downward_sdi_floorhug(action_id, batch, idx) &&
             (damage_hitlag_floorhug_attempts_downward_sdi(batch, idx, c) ||
              (active_damage_hitlag_carried_source_floor_contact &&
               batch->state.pos_y[idx] < (prefer_line_root_y - k_floor_y_bias))))
                ? 1u
                : 0u;
        const uint8_t active_ground_damage_platform_hard_floor_sweep_owner =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800477E0,
            (batch->state.hitlag[idx] != 0u && is_damage_ground_collision_action(action_id) &&
             prefer_line_idx >= 0 && prefer_line_is_platform && prefer_line_is_fighter_solid &&
             damage_hitlag_floorhug_attempts_downward_sdi(batch, idx, c))
                ? 1u
                : 0u;
        const uint8_t active_damageair_platform_hard_floor_sweep_owner =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800477E0,
            (batch->state.hitlag[idx] != 0u && mpcoll_damageair_action(action_id) &&
             batch->state.damage_allow_sdi[idx] != 0u &&
             mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_477E0) &&
             prefer_line_idx >= 0 && prefer_line_is_platform && prefer_line_is_fighter_solid)
                ? 1u
                : 0u;
        const uint8_t active_damagefly_platform_hard_floor_sweep_owner =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,
            (batch->state.hitlag[idx] != 0u && is_damage_fly_collision_action(action_id) &&
             batch->state.damage_allow_sdi[idx] != 0u && prefer_line_idx >= 0 &&
             prefer_line_is_platform && prefer_line_is_fighter_solid &&
             batch->state.damage_hitlag_downward_sdi_consumed[idx] != 0u)
                ? 1u
                : 0u;
        const uint8_t active_damage_hitlag_uses_callback_last_pos =
            (active_ground_damage_platform_hard_floor_sweep_owner ||
             active_damageair_platform_hard_floor_sweep_owner ||
             active_damagefly_platform_hard_floor_sweep_owner)
                ? 1u
                : 0u;
        const float active_damage_hitlag_prev_root_x =
            active_damage_hitlag_uses_callback_last_pos ? batch->state.coll_substep_prev_pos_x[idx]
                                                        : batch->state.floor_sweep_prev_pos_x[idx];
        const float active_damage_hitlag_prev_root_y =
            active_damage_hitlag_uses_callback_last_pos ? batch->state.coll_substep_prev_pos_y[idx]
                                                        : batch->state.floor_sweep_prev_pos_y[idx];
        const float active_damage_hitlag_prev_bottom_x = active_damage_hitlag_prev_root_x;
        const float active_damage_hitlag_prev_bottom_y =
            active_damage_hitlag_prev_root_y + prev_ecb_rel;
        const float active_damage_hitlag_seed_prev_bottom_y =
            batch->state.floor_sweep_prev_pos_y[idx] + prev_ecb_rel;
        const float active_damage_hitlag_callback_prev_bottom_y =
            batch->state.coll_substep_prev_pos_y[idx] +
            batch->state.coll_prev_ecb_bottom_rel_y[idx];
        const float active_damage_hitlag_cur_bottom_x =
            active_damage_hitlag_uses_callback_last_pos ? batch->state.coll_substep_cur_pos_x[idx]
                                                        : cur_bottom_x;
        const float active_damage_hitlag_cur_bottom_y =
            active_damage_hitlag_uses_callback_last_pos
                ? (batch->state.coll_substep_cur_pos_y[idx] + cur_bot.rel_y)
                : cur_bottom_y;
        const uint8_t active_damage_hitlag_hard_floor_sweep_owner =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800477E0,
            (batch->state.hitlag[idx] != 0u && active_damage_hitlag_uses_callback_last_pos &&
             ((active_damage_hitlag_cur_bottom_y <
               active_damage_hitlag_seed_prev_bottom_y - k_floor_horiz_dy_thresh) ||
              (active_damage_hitlag_uses_callback_last_pos &&
               active_damage_hitlag_cur_bottom_y <
                   active_damage_hitlag_callback_prev_bottom_y - k_floor_horiz_dy_thresh)) &&
             isfinite(active_damage_hitlag_prev_bottom_x) &&
             isfinite(active_damage_hitlag_prev_bottom_y))
                ? 1u
                : 0u;
        const uint8_t active_damage_thrown_release_floor_owner =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
            (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
             is_damage_collision_landing_action(action_id) &&
             msl_action_is_thrown_victim(batch->state.seed_prev_action_id[idx]) &&
             isfinite(batch->state.floor_sweep_prev_pos_y[idx]) &&
             batch->state.floor_sweep_prev_pos_y[idx] > batch->state.pos_y[idx] &&
             batch->state.pos_y[idx] < k_floor_y_bias)
                ? 1u
                : 0u;
        const uint8_t active_common_damage_entry_floor_owner =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
            (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
             is_damage_collision_landing_action(action_id) &&
             !is_damage_fly_collision_action(action_id) && batch->state.action_frame[idx] == 1 &&
             batch->state.seed_prev_action_id[idx] != action_id &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_DAMAGE_AIR_1 ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_DAMAGE_AIR_2 ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_DAMAGE_AIR_3) &&
             batch->state.pos_y[idx] < k_floor_y_bias && prefer_line_idx >= 0)
                ? 1u
                : 0u;
        const uint8_t active_ground_damage_entry_floor_owner =
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_477E0
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
            (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
             is_damage_ground_collision_action(action_id) && batch->state.action_frame[idx] <= 2 &&
             batch->state.seed_prev_action_id[idx] != action_id &&
             batch->state.pos_y[idx] < k_floor_y_bias && prefer_line_idx >= 0 &&
             !prefer_line_is_platform && !prefer_line_is_ledge && prefer_line_is_fighter_solid)
                ? 1u
                : 0u;
        const uint8_t active_ground_damage_platform_floor_owner =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,
            (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
             is_damage_ground_collision_action(action_id) && batch->state.action_frame[idx] <= 2 &&
             batch->state.seed_prev_action_id[idx] != action_id && prefer_line_idx >= 0 &&
             prefer_line_is_platform && prefer_line_is_fighter_solid &&
             mpcoll_bottom_sweep_hits_segment(batch, idx, bi, g, stage_id, prev_bottom_x,
                                              prev_bottom_y, cur_bottom_x, cur_bottom_y,
                                              skip_platform_segment_i, prefer_line_idx, -1, c,
                                              g->lines[(size_t)prefer_line_idx].segment_i))
                ? 1u
                : 0u;
        const uint8_t active_damagefly_from_damageair_entry_floor_owner =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
            (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
             is_damage_fly_collision_action(action_id) && batch->state.action_frame[idx] == 1 &&
             msl_damage_owner_is_damage_air_action(batch->state.seed_prev_action_id[idx]) &&
             batch->state.seed_prev_action_id[idx] != action_id &&
             batch->state.pos_y[idx] < k_floor_y_bias && prefer_line_idx >= 0)
                ? 1u
                : 0u;
        uint8_t damage_air473cc_stay_airborne_carried_hard_floor_contact = 0u;
        if (!on_ground &&
            msl_mpcoll_800473cc_damage_stay_airborne_hard_floor_sweep(
                &mpcoll_ctx, batch, idx, bi, g, stage_id, source_phases, prev_bottom_x,
                prev_bottom_y, cur_bottom_x, cur_bottom_y, prefer_line_idx, &ground_id, &contact_x,
                &contact_y, &floor_nx, &floor_ny)) {
          damage_air473cc_stay_airborne_carried_hard_floor_contact = 1u;
        }
        if (!on_ground && is_damage_collision_landing_action(action_id) &&
            batch->state.coll_floor_probe_valid[idx] == 0u) {
          mpcoll_floor_probe_begin(&mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_477E0,
                                   source_phases, prefer_line_idx,
                                   (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_OWNER);
        }
        const int16_t attackair_first_create_frame =
            move_tables_attackair_first_create_hitbox_frame(char_id, action_id);
        const uint8_t attackair_carried_floor_entry_packet_live =
            (attackair_first_create_frame >= 0 &&
             batch->state.anim_frame_f32[idx] <= (float)(attackair_first_create_frame + 1))
                ? 1u
                : 0u;
        const uint8_t attackair_carried_hard_floor_root_projection_owner =
            // data/scripts/{fox,falco}.bin::MSLFTSC1 AttackAir create_hitbox frames
            // data/stages/bin/*.bin::MSLSTG01 segment links/fighter_solid flags
            // refs/melee/src/melee/ft/ftaction.c::ftAction_80073354
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{
            (is_attackair_action(action_id) && batch->state.hitlag[idx] == 0u &&
             batch->state.hitlag_pre_timer[idx] == 0u &&
             batch->state.hitlag_started_frame[idx] == 0u &&
             mpcoll_floor_sweep_prev_root_is_source_owned(batch, idx) &&
             attackair_carried_floor_entry_packet_live && batch->state.ground_id[idx] != 0xFFFFu &&
             !msl_damage_owner_is_damage_air_action(prev_action_id) &&
             !msl_damage_owner_is_damage_air_action(batch->state.seed_prev_action_id[idx]) &&
             mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_471F8))
                ? 1u
                : 0u;
        if (!on_ground && attackair_carried_hard_floor_root_projection_owner) {
          const int carried_line_idx =
              stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
          int attackair_projection_line_idx = -1;
          float attackair_projection_y_corr = 0.0f;
          float attackair_projection_floor_y = 0.0f;
          float attackair_projection_nx = 0.0f;
          float attackair_projection_ny = 1.0f;
          if (carried_line_idx >= 0 && (size_t)carried_line_idx < g->line_count &&
              !g->lines[(size_t)carried_line_idx].is_platform &&
              !g->lines[(size_t)carried_line_idx].is_ledge &&
              g->lines[(size_t)carried_line_idx].platform_transform_kind ==
                  MSL_STAGE_PLATFORM_TRANSFORM_NONE &&
              floor_line_is_runtime_fighter_solid(g, stage_id, carried_line_idx)) {
            float y_corr = 0.0f;
            float floor_y = 0.0f;
            float root_floor_nx = 0.0f;
            float root_floor_ny = 1.0f;
            const int out_line_idx = msl_mplib_8004dd90_floor(
                batch, bi, g, carried_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
                &y_corr, &root_floor_nx, &root_floor_ny);
            if (out_line_idx == carried_line_idx && y_corr >= 0.0f &&
                floor_line_y_at_x_for_env(batch, bi, g, carried_line_idx, batch->state.pos_x[idx],
                                          &floor_y) &&
                isfinite(batch->state.floor_sweep_prev_pos_x[idx]) &&
                isfinite(batch->state.floor_sweep_prev_pos_y[idx]) &&
                floor_x_within_line_bounds(batch, bi, g, carried_line_idx,
                                           batch->state.floor_sweep_prev_pos_x[idx]) &&
                batch->state.floor_sweep_prev_pos_y[idx] < (floor_y - k_floor_y_bias) &&
                batch->state.pos_y[idx] < (floor_y + cur_bot.rel_y)) {
              attackair_projection_line_idx = carried_line_idx;
              attackair_projection_y_corr = y_corr;
              attackair_projection_floor_y = floor_y;
              attackair_projection_nx = root_floor_nx;
              attackair_projection_ny = root_floor_ny;
            }
          }
          if (attackair_projection_line_idx < 0 && carried_line_idx >= 0 &&
              (size_t)carried_line_idx < g->line_count) {
            for (size_t li = 0; li < g->line_count; li++) {
              const uint16_t segment_i = g->lines[li].segment_i;
              if (g->lines[li].is_platform || g->lines[li].is_ledge ||
                  g->lines[li].platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE ||
                  !floor_line_is_runtime_fighter_solid(g, stage_id, (int)li) ||
                  !floor_lines_connected(g, carried_line_idx, (int)li) ||
                  !floor_x_within_line_bounds(batch, bi, g, (int)li, batch->state.pos_x[idx])) {
                continue;
              }
              float floor_y = 0.0f;
              if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, batch->state.pos_x[idx],
                                             &floor_y) ||
                  batch->state.pos_y[idx] >= (floor_y - k_floor_y_bias) ||
                  !isfinite(batch->state.floor_sweep_prev_pos_x[idx]) ||
                  !isfinite(batch->state.floor_sweep_prev_pos_y[idx]) ||
                  !floor_x_within_line_bounds(batch, bi, g, (int)li,
                                              batch->state.floor_sweep_prev_pos_x[idx]) ||
                  batch->state.floor_sweep_prev_pos_y[idx] >= (floor_y - k_floor_y_bias) ||
                  batch->state.pos_y[idx] >= (floor_y + cur_bot.rel_y)) {
                continue;
              }
              float y_corr = 0.0f;
              float root_floor_nx = 0.0f;
              float root_floor_ny = 1.0f;
              const int out_line_idx = msl_mplib_8004dd90_floor(
                  batch, bi, g, (int)li, batch->state.pos_x[idx], batch->state.pos_y[idx], &y_corr,
                  &root_floor_nx, &root_floor_ny);
              if (out_line_idx != (int)li || y_corr < 0.0f) {
                continue;
              }
              if (attackair_projection_line_idx < 0 || y_corr < attackair_projection_y_corr ||
                  (y_corr == attackair_projection_y_corr &&
                   segment_i < g->lines[(size_t)attackair_projection_line_idx].segment_i)) {
                attackair_projection_line_idx = (int)li;
                attackair_projection_y_corr = y_corr;
                attackair_projection_floor_y = floor_y;
                attackair_projection_nx = root_floor_nx;
                attackair_projection_ny = root_floor_ny;
              }
            }
          }
          if (attackair_projection_line_idx >= 0) {
            batch->state.pos_y[idx] += attackair_projection_y_corr;
            mpcoll_publish_direct_source_floor_hit(
                &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION,
                g->lines[(size_t)attackair_projection_line_idx].segment_i, batch->state.pos_x[idx],
                attackair_projection_floor_y, attackair_projection_nx, attackair_projection_ny);
          }
        }
        // Ground DamageHi/N/Lw hitlag-exit floor producer:
        if (!on_ground && is_damage_ground_collision_action(action_id) &&
            batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u &&
            batch->state.action_frame[idx] <= 2 && raw_current_floor_line_idx >= 0 &&
            (g->lines[(size_t)raw_current_floor_line_idx].is_platform ||
             g->lines[(size_t)raw_current_floor_line_idx].platform_transform_kind !=
                 MSL_STAGE_PLATFORM_TRANSFORM_NONE) &&
            mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_477E0)) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor,
          MslMpcollFloorSweepResult damage_ground_hard_floor_sweep = {0};
          if (mpcoll_collect_bottom_sweep_hard_floor_result(
                  batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                  cur_bottom_y, prefer_line_idx, -1, 0u, &damage_ground_hard_floor_sweep) &&
              damage_ground_hard_floor_sweep.projected_line_idx >= 0 &&
              damage_ground_hard_floor_sweep.projected_y_corr >= 0.0f) {
            batch->state.pos_x[idx] += (damage_ground_hard_floor_sweep.hit_x - cur_bottom_x);
            batch->state.pos_y[idx] += damage_ground_hard_floor_sweep.projected_y_corr;
            mpcoll_publish_direct_source_floor_hit(
                &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
                damage_ground_hard_floor_sweep.projected_segment_id,
                damage_ground_hard_floor_sweep.hit_x, damage_ground_hard_floor_sweep.hit_y,
                damage_ground_hard_floor_sweep.normal_x, damage_ground_hard_floor_sweep.normal_y);
          }
        }
        if (!on_ground) {
          MslMpcollFloorContact locked_root_471f8 = {0};
          if (msl_mpcoll_800471f8_escapeair_locked_root_publication(
                  batch, idx, bi, g, stage_id, escapeair_locked, ecb_lock_timer,
                  ecb_lock_timer_seed, stage_has_height_platform_transform, &prev_ecb_points,
                  &cur_ecb_points, skip_platform_segment_i, prefer_line_idx, char_id, anim,
                  ecb_frame_cur, locked_desired_ecb_bottom_valid, c, &locked_root_471f8,
                  &escapeair_locked_root_publication_platform_hit)) {
            ground_id = locked_root_471f8.ground_id;
            contact_x = locked_root_471f8.contact_x;
            contact_y = locked_root_471f8.contact_y;
            floor_nx = locked_root_471f8.normal_x;
            floor_ny = locked_root_471f8.normal_y;
            on_ground = 1u;
          }
        }
        if (!on_ground &&
            action_uses_sideb_air_ft_check_ground_and_ledge_floor_coll(char_id, action_id)) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
          // refs/melee/src/melee/mp/mpcoll.c::{
          MslMpcollFloorSweepResult sideb_floor_sweep = {0};
          mpcoll_floor_probe_begin(&mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_473CC,
                                   source_phases, prefer_line_idx,
                                   (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
          mpcoll_floor_probe_bottom_interval(&mpcoll_ctx, prev_bottom_x, prev_bottom_y,
                                             cur_bottom_x, cur_bottom_y);
          const uint8_t direct_floor_sweep_hit = mpcoll_collect_bottom_sweep_floor_result(
              batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x, cur_bottom_y,
              skip_platform_segment_i, prefer_line_idx, -1, NULL, &sideb_floor_sweep);
          if (direct_floor_sweep_hit) {
            const uint8_t damageair_height_platform_missing_current_owner =
                // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
                (mpcoll_damageair_action(action_id) && batch->state.hitlag[idx] == 0u &&
                 batch->state.hitstun[idx] != 0u &&
                 ((sideb_floor_sweep.hit_has_height_platform_transform &&
                   !stage_collision_floor_line_height_platform_state_is_current_owned(
                       batch, bi, sideb_floor_sweep.hit_segment_id)) ||
                  (sideb_floor_sweep.projected_has_height_platform_transform &&
                   !stage_collision_floor_line_height_platform_state_is_current_owned(
                       batch, bi, sideb_floor_sweep.projected_segment_id))))
                    ? 1u
                    : 0u;
            if (damageair_height_platform_missing_current_owner) {
              mpcoll_floor_probe_result(&mpcoll_ctx, &sideb_floor_sweep, 1u, 0u,
                                        (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION);
            } else {
              const int hit_line_idx = sideb_floor_sweep.hit_line_idx;
              const float ix = sideb_floor_sweep.hit_x;
              const float iy = sideb_floor_sweep.hit_y;
              float y_corr = 0.0f;
              floor_nx = sideb_floor_sweep.normal_x;
              floor_ny = sideb_floor_sweep.normal_y;
              const int out_line_idx =
                  msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, batch->state.pos_x[idx],
                                           batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
              if (out_line_idx >= 0 && y_corr >= 0.0f) {
                batch->state.pos_y[idx] += y_corr;
                mpcoll_publish_direct_source_floor_hit(
                    &floor_write, sideb_floor_sweep.mode, g->lines[(size_t)out_line_idx].segment_i,
                    batch->state.pos_x[idx], batch->state.pos_y[idx], floor_nx, floor_ny);
                mpcoll_floor_probe_result(&mpcoll_ctx, &sideb_floor_sweep, 1u, 1u,
                                          (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
              } else if (hit_line_idx >= 0) {
                batch->state.pos_x[idx] += (ix - cur_bottom_x);
                batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
                mpcoll_publish_direct_source_floor_hit(&floor_write, sideb_floor_sweep.mode,
                                                       g->lines[(size_t)hit_line_idx].segment_i, ix,
                                                       iy, floor_nx, floor_ny);
                mpcoll_floor_probe_result(&mpcoll_ctx, &sideb_floor_sweep, 1u, 0u,
                                          (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
              }
            }
          } else {
            mpcoll_floor_probe_result(&mpcoll_ctx, &sideb_floor_sweep, 0u, 0u,
                                      (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
          }
        }
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor
        const uint8_t lr_is_jump_action = (uint8_t)(action_id == (uint16_t)MSL_ACT_JUMP_F ||
                                                    action_id == (uint16_t)MSL_ACT_JUMP_B ||
                                                    action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                                                    action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
                                                    action_id == (uint16_t)MSL_ACT_FALL_SPECIAL);
        if (!on_ground && lr_is_jump_action && batch->state.pos_y[idx] < 0.0f &&
            !stage_has_height_platform_transform && stage_has_only_static_cardinal_hard_floors) {
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor}
          float lr_cur_rel = mpcoll_pose_ecb_bottom_rel_y(
              char_id, batch->state.animation_index[idx],
              msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]), 0u);
          const float lr_prev_rel = batch->state.coll_effective_bottom_rel_prev_valid[idx]
                                        ? batch->state.coll_effective_bottom_rel_prev[idx]
                                        : lr_cur_rel;
          const float lr_prev_x = batch->state.prev_pos_x[idx];
          const float lr_prev_y = batch->state.prev_pos_y[idx];
          if (!(batch->state.pos_y[idx] + lr_cur_rel < lr_prev_y + lr_prev_rel)) {
            lr_cur_rel = -1.0f;  // bottom not descending: decline
          }
          {
            MslMpcollFloorSweepResult lr_sweep = {0};
            if (lr_cur_rel >= 0.0f &&
                mpcoll_collect_bottom_sweep_hit(batch, idx, bi, g, stage_id, lr_prev_x,
                                                lr_prev_y + lr_prev_rel, batch->state.pos_x[idx],
                                                batch->state.pos_y[idx] + lr_cur_rel,
                                                skip_platform_segment_i, -1, -1, c, &lr_sweep) &&
                lr_sweep.hit_line_idx >= 0 && (size_t)lr_sweep.hit_line_idx < g->line_count &&
                !lr_sweep.hit_is_platform && !lr_sweep.hit_has_platform_transform &&
                lr_sweep.hit_is_ledge && fabsf(lr_sweep.normal_x) < 0.25f &&
                batch->state.pos_y[idx] < lr_sweep.hit_y) {
              batch->state.pos_y[idx] = lr_sweep.hit_y;
              on_ground = 1u;
              {
                float rm_nx = lr_sweep.normal_x;
                float rm_ny = lr_sweep.normal_y;
                const int rm_idx = msl_mplib_8004dd90_floor(batch, bi, g, lr_sweep.hit_line_idx,
                                                            batch->state.pos_x[idx], lr_sweep.hit_y,
                                                            NULL, &rm_nx, &rm_ny);
                if (rm_idx >= 0 && (size_t)rm_idx < g->line_count) {
                  ground_id = g->lines[(size_t)rm_idx].segment_i;
                  lr_sweep.normal_x = rm_nx;
                  lr_sweep.normal_y = rm_ny;
                } else {
                  ground_id = lr_sweep.hit_segment_id;
                }
              }
              mpcoll_publish_direct_source_floor_hit(
                  &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id,
                  lr_sweep.hit_x, lr_sweep.hit_y, lr_sweep.normal_x, lr_sweep.normal_y);
            }
          }
        }
        if (!on_ground && batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] != 0u &&
            batch->state.damage_allow_sdi[idx] == 0u && mpcoll_damageair_action(action_id) &&
            prefer_line_idx >= 0 && prefer_line_is_platform && prefer_line_is_fighter_solid &&
            batch->state.coll_prev_ecb_bottom_valid[idx] != 0u &&
            mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx) &&
            isfinite(batch->state.coll_last_pos_x[idx]) &&
            isfinite(batch->state.coll_last_pos_y[idx])) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800473CC,
          const float damageair_prev_bottom_x = batch->state.coll_last_pos_x[idx];
          const float damageair_prev_bottom_y =
              batch->state.coll_last_pos_y[idx] + batch->state.coll_prev_ecb_bottom_rel_y[idx];
          MslMpcollFloorSweepResult damageair_hard_floor_sweep = {0};
          mpcoll_floor_probe_begin(&mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_473CC,
                                   source_phases, prefer_line_idx,
                                   (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
          mpcoll_floor_probe_bottom_interval(&mpcoll_ctx, damageair_prev_bottom_x,
                                             damageair_prev_bottom_y, cur_bottom_x, cur_bottom_y);
          if (cur_bottom_y <= damageair_prev_bottom_y &&
              mpcoll_collect_bottom_sweep_hard_floor_result(
                  batch, idx, bi, g, stage_id, damageair_prev_bottom_x, damageair_prev_bottom_y,
                  cur_bottom_x, cur_bottom_y, prefer_line_idx, -1, 0u,
                  &damageair_hard_floor_sweep) &&
              damageair_hard_floor_sweep.projected_line_idx >= 0 &&
              damageair_hard_floor_sweep.projected_y_corr >= 0.0f) {
            batch->state.pos_y[idx] += damageair_hard_floor_sweep.projected_y_corr;
            mpcoll_publish_direct_source_floor_hit(
                &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
                damageair_hard_floor_sweep.projected_segment_id, damageair_hard_floor_sweep.hit_x,
                damageair_hard_floor_sweep.hit_y, damageair_hard_floor_sweep.normal_x,
                damageair_hard_floor_sweep.normal_y);
            mpcoll_floor_probe_result(&mpcoll_ctx, &damageair_hard_floor_sweep, 1u, 1u,
                                      (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
          } else {
            mpcoll_floor_probe_result(&mpcoll_ctx, &damageair_hard_floor_sweep,
                                      damageair_hard_floor_sweep.hit ? 1u : 0u, 0u,
                                      damageair_hard_floor_sweep.hit
                                          ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION
                                          : (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
          }
        }
        if (!on_ground && batch->state.hitlag[idx] == 0u &&
            batch->state.damage_allow_sdi[idx] == 0u &&
            is_damage_ground_collision_action(action_id) &&
            mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_477E0) &&
            prefer_line_idx >= 0 && prefer_line_is_platform && prefer_line_is_fighter_solid &&
            mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx)) {
          // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_477E0
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,
          MslMpcollFloorSweepResult ground_damage_hard_floor_sweep = {0};
          mpcoll_floor_probe_begin(&mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_477E0,
                                   source_phases, prefer_line_idx,
                                   (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
          mpcoll_floor_probe_bottom_interval(&mpcoll_ctx, prev_bottom_x, prev_bottom_y,
                                             cur_bottom_x, cur_bottom_y);
          if (mpcoll_collect_bottom_sweep_hard_floor_result(
                  batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                  cur_bottom_y, prefer_line_idx, -1, 0u, &ground_damage_hard_floor_sweep) &&
              ground_damage_hard_floor_sweep.projected_line_idx >= 0 &&
              ground_damage_hard_floor_sweep.projected_y_corr >= 0.0f) {
            batch->state.pos_y[idx] += ground_damage_hard_floor_sweep.projected_y_corr;
            ground_id = ground_damage_hard_floor_sweep.projected_segment_id;
            contact_x = ground_damage_hard_floor_sweep.hit_x;
            contact_y = ground_damage_hard_floor_sweep.hit_y;
            floor_nx = ground_damage_hard_floor_sweep.normal_x;
            floor_ny = ground_damage_hard_floor_sweep.normal_y;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE,
                (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id, contact_x, contact_y,
                floor_nx, floor_ny);
            mpcoll_floor_probe_result(&mpcoll_ctx, &ground_damage_hard_floor_sweep, 1u, 1u,
                                      (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
          } else {
            mpcoll_floor_probe_result(&mpcoll_ctx, &ground_damage_hard_floor_sweep,
                                      ground_damage_hard_floor_sweep.hit ? 1u : 0u, 0u,
                                      ground_damage_hard_floor_sweep.hit
                                          ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION
                                          : (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
          }
        }
        if (!on_ground && damage_hitlag_exit_projection_owner && prefer_line_idx >= 0) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpColl_80044838_Floor}
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float proj_x = 0.0f;
          float proj_y = 0.0f;
          stay_airborne_floor_projection_point(batch->state.pos_x[idx], batch->state.pos_y[idx],
                                               cur_bottom_x, cur_bottom_y, &proj_x, &proj_y);
          float y_corr = 0.0f;
          const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, proj_x,
                                                            proj_y, &y_corr, &floor_nx, &floor_ny);
          if (out_line_idx >= 0 && y_corr >= 0.0f) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = proj_x;
            contact_y = proj_y + y_corr;
            batch->state.damage_hitlag_floorhug_latch[idx] = 0u;
          }
        }
        uint8_t active_damage_hitlag_airborne_floor_contact = 0u;
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
        const uint8_t active_damage_floorhug_carries_hitlag_exit_latch =
            (is_damage_fly_collision_action(action_id) ||
             action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
             action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D ||
             active_damage_thrown_release_floor_owner)
                ? 1u
                : 0u;
        if (!on_ground && !damage_air473cc_stay_airborne_carried_hard_floor_contact &&
            active_damage_hitlag_hard_floor_sweep_owner) {
          MslMpcollFloorSweepResult active_damage_hard_floor_sweep = {0};
          mpcoll_floor_probe_bottom_interval(
              &mpcoll_ctx, active_damage_hitlag_prev_bottom_x, active_damage_hitlag_prev_bottom_y,
              active_damage_hitlag_cur_bottom_x, active_damage_hitlag_cur_bottom_y);
          uint8_t active_damage_hard_floor_sweep_hit =
              mpcoll_collect_bottom_sweep_hard_floor_result(
                  batch, idx, bi, g, stage_id, batch->state.floor_sweep_prev_pos_x[idx],
                  active_damage_hitlag_seed_prev_bottom_y, active_damage_hitlag_cur_bottom_x,
                  active_damage_hitlag_cur_bottom_y, prefer_line_idx, -1, 0u,
                  &active_damage_hard_floor_sweep);
          if (!active_damage_hard_floor_sweep_hit && active_damage_hitlag_uses_callback_last_pos) {
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800477E0,
            active_damage_hard_floor_sweep_hit = mpcoll_collect_bottom_sweep_hard_floor_result(
                batch, idx, bi, g, stage_id, batch->state.coll_substep_prev_pos_x[idx],
                active_damage_hitlag_callback_prev_bottom_y, active_damage_hitlag_cur_bottom_x,
                active_damage_hitlag_cur_bottom_y, prefer_line_idx, -1, 0u,
                &active_damage_hard_floor_sweep);
          }
          if (active_damage_hard_floor_sweep_hit &&
              active_damage_hard_floor_sweep.projected_line_idx >= 0 &&
              active_damage_hard_floor_sweep.projected_y_corr >= 0.0f &&
              batch->state.pos_y[idx] < active_damage_hard_floor_sweep.hit_y - k_floor_y_bias) {
            batch->state.pos_y[idx] += active_damage_hard_floor_sweep.projected_y_corr;
            ground_id = active_damage_hard_floor_sweep.projected_segment_id;
            contact_x = active_damage_hard_floor_sweep.hit_x;
            contact_y = active_damage_hard_floor_sweep.hit_y;
            floor_nx = active_damage_hard_floor_sweep.normal_x;
            floor_ny = active_damage_hard_floor_sweep.normal_y;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE,
                (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id, contact_x, contact_y,
                floor_nx, floor_ny);
            mpcoll_floor_probe_result(&mpcoll_ctx, &active_damage_hard_floor_sweep, 1u, 1u,
                                      (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
            active_damage_hitlag_airborne_floor_contact = 1u;
          } else {
            mpcoll_floor_probe_result(&mpcoll_ctx, &active_damage_hard_floor_sweep,
                                      active_damage_hard_floor_sweep.hit ? 1u : 0u, 0u,
                                      active_damage_hard_floor_sweep.hit
                                          ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION
                                          : (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
          }
        }
        if (!on_ground && !damage_air473cc_stay_airborne_carried_hard_floor_contact &&
            !active_damage_hitlag_airborne_floor_contact &&
            (active_damage_hitlag_stay_airborne_floor_owner ||
             active_damage_thrown_release_floor_owner || active_common_damage_entry_floor_owner ||
             active_ground_damage_entry_floor_owner || active_ground_damage_platform_floor_owner ||
             active_damagefly_from_damageair_entry_floor_owner)) {
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor,mpColl_80046904}
          float proj_x = 0.0f;
          float proj_y = 0.0f;
          stay_airborne_floor_projection_point(batch->state.pos_x[idx], batch->state.pos_y[idx],
                                               cur_bottom_x, cur_bottom_y, &proj_x, &proj_y);
          float y_corr = 0.0f;
          const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, proj_x,
                                                            proj_y, &y_corr, &floor_nx, &floor_ny);
          MslMpcollFloorSweepResult active_damage_projection_probe = {0};
          if (out_line_idx >= 0) {
            active_damage_projection_probe.hit = 1u;
            active_damage_projection_probe.hit_line_idx = out_line_idx;
            active_damage_projection_probe.projected_line_idx = out_line_idx;
            active_damage_projection_probe.hit_segment_id =
                g->lines[(size_t)out_line_idx].segment_i;
            active_damage_projection_probe.projected_segment_id =
                g->lines[(size_t)out_line_idx].segment_i;
          }
          uint8_t active_damage_platform_floor_hit = 1u;
          if (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_platform) {
            active_damage_platform_floor_hit =
                (mpcoll_bottom_sweep_hits_segment(batch, idx, bi, g, stage_id, prev_bottom_x,
                                                  prev_bottom_y, cur_bottom_x, cur_bottom_y,
                                                  skip_platform_segment_i, out_line_idx, -1, c,
                                                  g->lines[(size_t)out_line_idx].segment_i))
                    ? 1u
                    : 0u;
          }
          uint8_t active_damage_hard_floor_hit = 1u;
          if (out_line_idx >= 0 && !g->lines[(size_t)out_line_idx].is_platform &&
              !g->lines[(size_t)out_line_idx].is_ledge) {
            active_damage_hard_floor_hit = active_damage_hard_floor_projection_source_accepted(
                batch, idx, bi, g, out_line_idx, cur_bottom_x, cur_bottom_y,
                active_damage_hitlag_carried_source_floor_contact, c);
          }
          float ledge_edge_x = 0.0f;
          float ledge_edge_y = 0.0f;
          const uint8_t active_damage_ledge_edge_floorhug_owner =
              (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_ledge != 0u &&
               active_damage_hitlag_ledge_edge_floorhug_owner(batch, idx, bi, g, stage_id,
                                                              out_line_idx, proj_x, c,
                                                              &ledge_edge_x, &ledge_edge_y))
                  ? 1u
                  : 0u;
          if (out_line_idx >= 0 && y_corr >= 0.0f &&
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
              (!g->lines[(size_t)out_line_idx].is_ledge ||
               active_damage_ledge_edge_floorhug_owner) &&
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
              (!g->lines[(size_t)out_line_idx].is_platform || active_damage_platform_floor_hit) &&
              active_damage_hard_floor_hit) {
            if (active_damage_ledge_edge_floorhug_owner) {
              batch->state.pos_x[idx] += (ledge_edge_x - proj_x);
              batch->state.pos_y[idx] += (ledge_edge_y - proj_y);
              contact_x = ledge_edge_x;
              contact_y = ledge_edge_y;
            } else {
              batch->state.pos_y[idx] += y_corr;
              contact_x = proj_x;
              contact_y = proj_y + y_corr;
            }
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE,
                active_damage_ledge_edge_floorhug_owner
                    ? (uint8_t)MSL_MPCOLL_FLOOR_MODE_EDGE_SNAP
                    : (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAY_AIRBORNE_PROJECTION,
                ground_id, contact_x, contact_y, floor_nx, floor_ny);
            mpcoll_floor_probe_result(&mpcoll_ctx, &active_damage_projection_probe, 0u, 1u,
                                      (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
            active_damage_hitlag_airborne_floor_contact = 1u;
            if (active_damage_floorhug_carries_hitlag_exit_latch) {
              batch->state.damage_hitlag_floorhug_latch[idx] = 1u;
            }
          } else if (prefer_line_idx >= 0 && !g->lines[(size_t)prefer_line_idx].is_ledge &&
                     !g->lines[(size_t)prefer_line_idx].is_platform) {
            // refs/melee/src/melee/mp/mpcoll.c::{
            float root_y_corr = 0.0f;
            const int root_line_idx = msl_mplib_8004dd90_floor(
                batch, bi, g, prefer_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
                &root_y_corr, &floor_nx, &floor_ny);
            float root_bottom_floor_y = 0.0f;
            const uint8_t root_current_bottom_floor_hit =
                (uint8_t)((root_line_idx >= 0 &&
                           floor_line_y_at_x_for_env(batch, bi, g, root_line_idx, cur_bottom_x,
                                                     &root_bottom_floor_y) &&
                           cur_bottom_y <= (root_bottom_floor_y + k_floor_y_bias))
                              ? 1u
                              : 0u);
            const uint8_t root_bottom_floor_hit =
                (is_damage_fly_collision_action(action_id) ||
                 batch->state.tilt_timer_y_frame_start[idx] >= c->sdi_tilt_max_frames)
                    ? root_current_bottom_floor_hit
                    : 1u;
            if (root_line_idx >= 0 && root_y_corr >= 0.0f &&
                !g->lines[(size_t)root_line_idx].is_ledge &&
                floor_x_within_line_bounds(batch, bi, g, root_line_idx, batch->state.pos_x[idx]) &&
                root_bottom_floor_hit) {
              batch->state.pos_y[idx] += root_y_corr;
              ground_id = g->lines[(size_t)root_line_idx].segment_i;
              contact_x = batch->state.pos_x[idx];
              contact_y = batch->state.pos_y[idx];
              mpcoll_record_callback_floor_result_with_mode(
                  &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE,
                  (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION, ground_id, contact_x, contact_y,
                  floor_nx, floor_ny);
              MslMpcollFloorSweepResult active_damage_root_probe = {0};
              active_damage_root_probe.hit = 1u;
              active_damage_root_probe.hit_line_idx = root_line_idx;
              active_damage_root_probe.projected_line_idx = root_line_idx;
              active_damage_root_probe.hit_segment_id = g->lines[(size_t)root_line_idx].segment_i;
              active_damage_root_probe.projected_segment_id =
                  g->lines[(size_t)root_line_idx].segment_i;
              mpcoll_floor_probe_result(&mpcoll_ctx, &active_damage_root_probe, 0u, 1u,
                                        (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
              active_damage_hitlag_airborne_floor_contact = 1u;
              if (active_damage_floorhug_carries_hitlag_exit_latch) {
                batch->state.damage_hitlag_floorhug_latch[idx] = 1u;
              }
            }
          }
          if (!active_damage_hitlag_airborne_floor_contact) {
            mpcoll_floor_probe_result(
                &mpcoll_ctx, out_line_idx >= 0 ? &active_damage_projection_probe : NULL, 0u,
                (out_line_idx >= 0 && y_corr >= 0.0f) ? 1u : 0u,
                out_line_idx >= 0 ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION
                                  : (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
          }
        }
        float damageflyroll_root_proj_y_corr = 0.0f;
        float damageflyroll_side_y_thresh = 0.0f;
        int damageflyroll_root_proj_line_idx = -1;
        uint8_t damageflyroll_root_proj_ready = 0u;
        uint8_t damageflyroll_deep_side_penetration = 0u;
        if (!on_ground && damageflyroll_iasa_lockout && prefer_line_idx >= 0) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80046904,mpColl_80044838_Floor}
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
          const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
          MslEcbWorldPoints ecb_world = {0};
          msl_ecb_world_points_sample(&ecb_world, char_id, anim, ecb_frame_cur, facing_dir,
                                      batch->state.pos_x[idx], batch->state.pos_y[idx],
                                      lock_bottom_to_zero);
          damageflyroll_side_y_thresh =
              ecb_world.side_rel_y + ecb_world.bottom_rel_y + k_floor_y_bias;

          damageflyroll_root_proj_line_idx = msl_mplib_8004dd90_floor(
              batch, bi, g, prefer_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
              &damageflyroll_root_proj_y_corr, &floor_nx, &floor_ny);
          if (damageflyroll_root_proj_line_idx >= 0 && damageflyroll_root_proj_y_corr >= 0.0f) {
            damageflyroll_root_proj_ready = 1u;
            // refs/melee/src/melee/ft/ft_084E.c::ft_80084EEC
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_800473CC,
            const uint8_t damageflyroll_terminal_downward_root_owner =
                (have_cur_damageflyroll_ecb != 0u &&
                 batch->state.speed_y_self[idx] <= -(3.0f * k_ecb_vertical_unit))
                    ? 1u
                    : 0u;
            if ((damageflyroll_jobj_ecb_active == 0u &&
                 damageflyroll_root_proj_y_corr >= damageflyroll_side_y_thresh) ||
                (damageflyroll_terminal_downward_root_owner &&
                 damageflyroll_root_proj_y_corr + k_ecb_vertical_unit >=
                     damageflyroll_side_y_thresh)) {
              damageflyroll_deep_side_penetration = 1u;
              const float root_x = batch->state.pos_x[idx];
              const float root_y = batch->state.pos_y[idx];
              const float contact_y_root = root_y + damageflyroll_root_proj_y_corr;
              // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044838_Floor,mpColl_LoadECB_JObj}
              float root_y_corr = damageflyroll_root_proj_y_corr - damageflyroll_side_y_thresh;
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_inline,mpColl_80042384}
              if (root_y_corr > k_ecb_vertical_unit) {
                root_y_corr = k_ecb_vertical_unit;
              }
              batch->state.pos_y[idx] = root_y + root_y_corr;
              on_ground = 1;
              floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
              ground_id = g->lines[(size_t)damageflyroll_root_proj_line_idx].segment_i;
              contact_x = root_x;
              contact_y = contact_y_root;
            }
          }
        }
        const float escapeair_frame_start_prev_bottom_y =
            prev_y + msl_ecb_bottom_rel_y(char_id, anim, (int)ecb_frame_prev);
        const uint8_t escapeair_fresh_jumpaerial_entry_lock =
            (escapeair_jumpaerial_entry && ecb_lock_active &&
             batch->state.seed_prev_action_frame[idx] >= 2 &&
             batch->state.seed_prev_action_frame[idx] <= 4)
                ? 1u
                : 0u;
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
        const uint8_t escapeair_prev_ecb_ledge_entry_floor =
            (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].is_ledge &&
             batch->state.action_frame[idx] <= 3)
                ? 1u
                : 0u;
        if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && prefer_line_idx >= 0 &&
            prev_y <= k_floor_y_bias && batch->state.speed_y_self[idx] < 0.0f &&
            batch->state.pos_y[idx] < 0.0f &&
            (!g->lines[(size_t)prefer_line_idx].is_ledge || escapeair_prev_ecb_ledge_entry_floor) &&
            escapeair_frame_start_prev_bottom_y <= k_floor_y_bias &&
            !(escapeair_fresh_jumpaerial_entry_lock && prefer_line_is_platform_or_slope) &&
            !(ecb_lock_active == 0u &&
              (prev_action_id == action_id ||
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR) &&
              batch->state.action_frame[idx] <= 6)) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          uint8_t cliff_ledge_floor_owner_floor_check_hit = 1u;
          if (cliff_ledge_floor_owner_active) {
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            cliff_ledge_floor_owner_floor_check_hit = mpcoll_bottom_sweep_hits_segment(
                batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                cur_bottom_y, skip_platform_segment_i, prefer_line_idx, -1, c,
                batch->state.cliff_ledge_floor_segment_id[idx]);
          }
          const float cliff_ledge_floor_owner_depth_gate =
              cliff_ledge_floor_owner_active ? fabsf(msl_ecb_bottom_rel_y(char_id, anim, 0)) : 0.0f;
          const float max_lift = cliff_ledge_floor_owner_active
                                     ? (msl_ecb_bottom_rel_y(char_id, anim, 0) + fabsf(y - prev_y) +
                                        fabsf(batch->state.speed_y_self[idx]) +
                                        mpcoll_floor_projection_lift_allowance(&cur_ecb_points))
                                     : (cur_bot.rel_y + fabsf(batch->state.speed_y_self[idx]) +
                                        (2.0f * k_ecb_vertical_unit));
          const uint8_t projected_line_is_platform =
              (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_platform) ? 1u : 0u;
          const uint8_t projected_line_is_slope =
              (out_line_idx >= 0 && floor_line_is_generated_stage_slope(batch, bi, g, out_line_idx))
                  ? 1u
                  : 0u;
          const uint8_t projected_line_is_ledge =
              (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_ledge) ? 1u : 0u;
          const uint8_t projected_line_has_platform_transform =
              (out_line_idx >= 0 && stage_collision_floor_line_has_platform_transform(
                                        stage_id, g->lines[(size_t)out_line_idx].segment_i))
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_root_below_projection =
              // refs/melee/src/melee/mp/mpcoll.c::{
              ((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                batch->state.action_frame[idx] <= 4 && !projected_line_is_platform &&
                !projected_line_has_platform_transform &&
                batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
                msl_escapeair_locked_bottom_owner_any(
                    batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
                batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
                batch->state.coll_desired_ecb_bottom_rel_y[idx] > y_corr) ||
               // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044838_Floor}
               ((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                 batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
                batch->state.action_frame[idx] <= 2 && projected_line_is_ledge &&
                batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias) ||
               (projected_line_is_slope &&
                ((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                  batch->state.action_frame[idx] <= 2) ||
                 batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND)) ||
               (escapeair_fresh_jumpaerial_entry_lock &&
                (projected_line_is_platform || projected_line_is_slope || projected_line_is_ledge)))
                  ? 1u
                  : 0u;
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift &&
              !suppress_escapeair_root_below_projection &&
              (!cliff_ledge_floor_owner_active ||
               (cliff_ledge_floor_owner_floor_check_hit &&
                y_corr + k_floor_y_bias >= cliff_ledge_floor_owner_depth_gate))) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
          }
        }
        if (!on_ground && escapeair_locked && prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            batch->state.action_frame[idx] >= 4 && prefer_line_idx >= 0 &&
            batch->state.speed_y_self[idx] < 0.0f && prev_y > k_floor_y_bias &&
            batch->state.pos_y[idx] < 0.0f) {
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          const float max_lift = fabsf(batch->state.speed_y_self[idx]);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift) {
            batch->state.pos_y[idx] += (y_corr - k_floor_y_bias);
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
          }
        }
        if (!on_ground && cliff_ledge_floor_owner_active &&
            action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && prefer_line_idx >= 0 &&
            ecb_lock_timer_seed != 0u && batch->state.action_frame[idx] <= 2 &&
            batch->state.speed_y_self[idx] < 0.0f) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
          float ledge_y = 0.0f;
          if (floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, x, &ledge_y) &&
              batch->state.floor_sweep_prev_pos_y[idx] > ledge_y + k_floor_y_bias &&
              batch->state.prev_pos_y[idx] < ledge_y - k_floor_y_bias &&
              y <= ledge_y + k_floor_y_bias &&
              floor_x_within_line_bounds(batch, bi, g, prefer_line_idx, x)) {
            batch->state.pos_y[idx] = ledge_y + k_floor_y_bias;
            on_ground = 1;
            ground_id = g->lines[(size_t)prefer_line_idx].segment_i;
            contact_x = x;
            contact_y = ledge_y;
          }
        }

        if (!on_ground &&
            (cliff_ledge_floor_owner_active || cliff_ledge_floor_owner_matches_current_line) &&
            action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && prefer_line_idx >= 0 &&
            ecb_lock_timer_seed != 0u && batch->state.ledge_cooldown[idx] != 0u &&
            batch->state.cliff_ledge_floor_segment_id != NULL &&
            batch->state.cliff_ledge_floor_segment_id[idx] != 0xFFFFu &&
            !(batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
              batch->state.action_frame[idx] <= 2)) {
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
          float y_corr = 0.0f;
          const int carried_cliff_projection_line_idx = stage_collision_floor_line_index(
              stage_id, batch->state.cliff_ledge_floor_segment_id[idx]);
          const int cliff_projection_line_idx = (carried_cliff_projection_line_idx >= 0)
                                                    ? carried_cliff_projection_line_idx
                                                    : prefer_line_idx;
          const int out_line_idx = msl_mplib_8004dd90_floor(
              batch, bi, g, cliff_projection_line_idx, batch->state.pos_x[idx],
              batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          const float bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
          const float max_lift = bottom_rel0 + fabsf(y - prev_y) +
                                 fabsf(batch->state.speed_y_self[idx]) +
                                 mpcoll_floor_projection_lift_allowance(&cur_ecb_points);
          const uint8_t raw_current_matches_projected_cliff_floor =
              (raw_current_floor_line_idx >= 0 && out_line_idx >= 0 &&
               g->lines[(size_t)raw_current_floor_line_idx].segment_i ==
                   g->lines[(size_t)out_line_idx].segment_i)
                  ? 1u
                  : 0u;
          float projected_cliff_line_y = 0.0f;
          const uint8_t raw_current_cliff_floor_root_continuation =
              (raw_current_matches_projected_cliff_floor &&
               floor_line_y_at_x_for_env(batch, bi, g, out_line_idx, batch->state.pos_x[idx],
                                         &projected_cliff_line_y) &&
               batch->state.floor_sweep_prev_pos_y[idx] <
                   (projected_cliff_line_y - k_floor_y_bias) &&
               batch->state.pos_y[idx] <= (projected_cliff_line_y + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t cliff_ledge_floor_bottom_sweep_projection_owner =
              (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               mpcoll_bottom_sweep_hits_segment(batch, idx, bi, g, stage_id, prev_bottom_x,
                                                prev_bottom_y, cur_bottom_x, cur_bottom_y,
                                                skip_platform_segment_i, out_line_idx, -1, c,
                                                batch->state.cliff_ledge_floor_segment_id[idx]))
                  ? 1u
                  : 0u;
          float desired_projection_floor_y = 0.0f;
          const uint8_t desired_projection_floor_valid =
              (floor_line_y_at_x_for_env(batch, bi, g, out_line_idx, batch->state.pos_x[idx],
                                         &desired_projection_floor_y))
                  ? 1u
                  : 0u;
          const float desired_bottom_prev_y = batch->state.floor_sweep_prev_pos_y[idx] +
                                              batch->state.coll_desired_ecb_bottom_rel_y[idx];
          const float desired_bottom_cur_projection_y =
              batch->state.pos_y[idx] + batch->state.coll_desired_ecb_bottom_rel_y[idx];
          const uint8_t desired_bottom_crosses_projected_floor =
              (desired_projection_floor_valid &&
               desired_bottom_prev_y > (desired_projection_floor_y + k_floor_y_bias) &&
               desired_bottom_cur_projection_y <= (desired_projection_floor_y + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t flags_2218 = batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                                              (size_t)MSL_STATE_FLAGS_2218_INDEX];
          const uint8_t desired_bottom_owner_seedprev_escapeair =
              (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) != 0u ||
                (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_B1) == 0u ||
                stage_collision_stage_has_only_static_cardinal_hard_floors(stage_id)))
                  ? 1u
                  : 0u;
          const uint8_t desired_bottom_owner_seedprev_jumpaerial =
              (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B)
                  ? 1u
                  : 0u;
          const uint8_t cliff_ledge_floor_desired_root_projection_owner =
              (batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
               msl_escapeair_locked_bottom_owner_any(
                   batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
               (desired_bottom_owner_seedprev_escapeair ||
                desired_bottom_owner_seedprev_jumpaerial ||
                msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
                    batch->state.coll_desired_ecb_bottom_locked_owner[idx])) &&
               isfinite(batch->state.floor_sweep_prev_pos_x[idx]) &&
               floor_x_within_line_segment_strict(batch, bi, g, out_line_idx,
                                                  batch->state.floor_sweep_prev_pos_x[idx]) &&
               desired_bottom_crosses_projected_floor)
                  ? 1u
                  : 0u;
          const MslMpcollCarriedCliffLedgeFloorAuthority projection_authority =
              (out_line_idx >= 0 &&
               floor_line_y_at_x_for_env(batch, bi, g, out_line_idx, batch->state.pos_x[idx],
                                         &projected_cliff_line_y))
                  ? mpcoll_carried_cliff_ledge_floor_authority(
                        batch, idx, bi, g, char_id, action_id, out_line_idx,
                        raw_current_floor_line_idx, batch->state.pos_x[idx],
                        batch->state.pos_y[idx], projected_cliff_line_y, ecb_lock_timer_seed,
                        (uint8_t)(cliff_ledge_floor_bottom_sweep_projection_owner ||
                                  cliff_ledge_floor_desired_root_projection_owner))
                  : (MslMpcollCarriedCliffLedgeFloorAuthority){0};
          if (out_line_idx >= 0 && batch->state.cliff_ledge_floor_segment_id != NULL &&
              g->lines[(size_t)out_line_idx].segment_i ==
                  batch->state.cliff_ledge_floor_segment_id[idx] &&
              floor_x_within_line_segment_strict(batch, bi, g, out_line_idx,
                                                 batch->state.pos_x[idx]) &&
              (batch->state.speed_y_self[idx] < 0.0f ||
               projection_authority.current_floor_source_owned ||
               projection_authority.live_desired_bottom_authority) &&
              projection_authority.source_authority &&
              ((raw_current_cliff_floor_root_continuation && y_corr >= 0.0f) ||
               projection_authority.live_desired_bottom_authority ||
               y_corr >= (bottom_rel0 - k_floor_y_bias)) &&
              y_corr <= max_lift) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
            escapeair_live_cliff_ledge_bottom_sweep_owner =
                projection_authority.callback_bottom_root_accepted;
            escapeair_live_cliff_ledge_source_floor_owner = projection_authority.source_authority;
          }
        }

        if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && prefer_line_idx >= 0 &&
            !g->lines[(size_t)prefer_line_idx].is_ledge &&
            (prev_action_id == (uint16_t)MSL_ACT_FALL_AERIAL ||
             prev_action_id == (uint16_t)MSL_ACT_FALL_AERIAL_F ||
             prev_action_id == (uint16_t)MSL_ACT_FALL_AERIAL_B)) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          const float bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                       &y_corr, &floor_nx, &floor_ny);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= bottom_rel0) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = cur_bottom_x;
            contact_y = cur_bottom_y + y_corr;
          }
        }
        if (!on_ground && escapeair_locked && prefer_line_idx >= 0 &&
            !(escapeair_fresh_jumpaerial_entry_lock && prefer_line_is_platform_or_slope)) {
          const float bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
          deep_lock_penetration = (bottom_rel0 > 0.0f && cur_bottom_y <= -bottom_rel0 &&
                                   batch->state.speed_y_self[idx] < 0.0f)
                                      ? 1u
                                      : 0u;
          if (deep_lock_penetration) {
            // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
            // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            float y_corr = 0.0f;
            const int out_line_idx =
                msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                         &y_corr, &floor_nx, &floor_ny);
            const float ledge_projection_depth_limit =
                bottom_rel0 + fabsf(y - prev_y) +
                (cliff_ledge_floor_owner_active
                     ? mpcoll_floor_projection_lift_allowance(&cur_ecb_points)
                     : 0.0f);
            const uint8_t is_ledge_floor = g->lines[(size_t)prefer_line_idx].is_ledge;
            const uint8_t ledge_x_in_bounds =
                floor_x_within_line_bounds(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx]);
            const uint8_t ledge_escapeair_phase_owner =
                (is_ledge_floor && ledge_x_in_bounds &&
                 (batch->state.action_frame[idx] <= 3 ||
                  (prev_y <= k_floor_y_bias && batch->state.action_frame[idx] >= 5)))
                    ? 1u
                    : 0u;
            const uint16_t resolved_segment_i =
                (out_line_idx >= 0) ? g->lines[(size_t)out_line_idx].segment_i : 0xFFFFu;
            const uint8_t resolved_line_is_ledge =
                (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_ledge) ? 1u : 0u;
            const uint8_t resolved_ledge_x_in_bounds =
                (resolved_line_is_ledge &&
                 floor_x_within_line_bounds(batch, bi, g, out_line_idx, batch->state.pos_x[idx]))
                    ? 1u
                    : 0u;
            const uint8_t suppress_transformed_remap_projection =
                // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
                (out_line_idx >= 0 && batch->state.action_frame[idx] <= 4 &&
                 resolved_segment_i != batch->state.ground_id[idx] &&
                 stage_collision_floor_line_has_platform_transform(stage_id, resolved_segment_i))
                    ? 1u
                    : 0u;
            const uint8_t suppress_same_platform_projection_from_below =
                // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_platform &&
                 (cur_bottom_y + y_corr) > k_floor_y_bias && prev_bottom_y <= k_floor_y_bias &&
                 cur_bottom_y <= k_floor_y_bias)
                    ? 1u
                    : 0u;
            const uint8_t suppress_off_end_ledge_remap_projection =
                // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
                (resolved_line_is_ledge && !resolved_ledge_x_in_bounds &&
                 batch->state.action_frame[idx] <= 3)
                    ? 1u
                    : 0u;
            const uint8_t resolved_ledge_prev_x_in_bounds =
                (resolved_line_is_ledge &&
                 floor_x_within_line_bounds(batch, bi, g, out_line_idx,
                                            batch->state.floor_sweep_prev_pos_x[idx]))
                    ? 1u
                    : 0u;
            const uint8_t suppress_ledge_endpoint_entry_projection =
                // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                (stage_has_height_platform_transform && resolved_line_is_ledge &&
                 !resolved_ledge_prev_x_in_bounds && batch->state.action_frame[idx] <= 3)
                    ? 1u
                    : 0u;
            const uint8_t suppress_jumpaerial_entry_shallow_ledge_projection =
                // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                (resolved_line_is_ledge && !is_ledge_floor &&
                 resolved_segment_i != seed_ground_id &&
                 (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                  batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
                 batch->state.seed_prev_action_frame[idx] <= 4 &&
                 batch->state.action_frame[idx] <= 3 && y_corr >= 0.0f &&
                 y_corr < (bottom_rel0 - k_floor_y_bias))
                    ? 1u
                    : 0u;
            uint8_t projected_cliff_desired_bottom_sweep_hit = 0u;
            if (cliff_ledge_floor_owner_active && is_ledge_floor && out_line_idx >= 0 &&
                batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
                msl_escapeair_locked_bottom_owner_any(
                    batch->state.coll_desired_ecb_bottom_locked_owner[idx])) {
              projected_cliff_desired_bottom_sweep_hit = mpcoll_bottom_sweep_hits_segment(
                  batch, idx, bi, g, stage_id, prev_x,
                  prev_y + batch->state.coll_desired_ecb_bottom_rel_y[idx], x,
                  y + batch->state.coll_desired_ecb_bottom_rel_y[idx], skip_platform_segment_i,
                  out_line_idx, -1, c, resolved_segment_i);
            }
            const uint8_t suppress_cliff_ledge_floor_shallow_projection =
                // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044838_Floor}
                (cliff_ledge_floor_owner_active && is_ledge_floor && y_corr >= 0.0f &&
                 !escapeair_live_cliff_ledge_source_floor_owner &&
                 !projected_cliff_desired_bottom_sweep_hit &&
                 y_corr < (bottom_rel0 - k_floor_y_bias))
                    ? 1u
                    : 0u;
            const uint8_t suppress_cliff_ledge_remap_without_live_owner =
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                (cliff_ledge_floor_owner_matches_current_line && !cliff_ledge_floor_owner_active &&
                 !is_ledge_floor && resolved_line_is_ledge)
                    ? 1u
                    : 0u;
            int low_raw_line_idx = -1;
            float low_raw_y = 0.0f;
            const float low_raw_max_lift = fabsf(cur_bottom_y - prev_bottom_y) +
                                           fabsf(batch->state.speed_y_self[idx]) + bottom_rel0 +
                                           mpcoll_floor_projection_lift_allowance(&cur_ecb_points);
            if (suppress_same_platform_projection_from_below &&
                floor_find_low_raw_floor_contact(batch, idx, bi, g, stage_id, cur_bottom_x,
                                                 cur_bottom_y, low_raw_max_lift,
                                                 skip_platform_segment_i, c, &low_raw_line_idx,
                                                 &low_raw_y, &floor_nx, &floor_ny) &&
                // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044838_Floor}
                (low_raw_y - cur_bottom_y) >= (bottom_rel0 - k_floor_y_bias)) {
              batch->state.pos_y[idx] += (low_raw_y - cur_bottom_y) + k_floor_y_bias;
              ground_id = g->lines[(size_t)low_raw_line_idx].segment_i;
              on_ground = 1;
              contact_x = cur_bottom_x;
              contact_y = low_raw_y;
            } else if (out_line_idx >= 0 && y_corr >= 0.0f &&
                       !suppress_transformed_remap_projection &&
                       !suppress_same_platform_projection_from_below &&
                       !suppress_off_end_ledge_remap_projection &&
                       !suppress_ledge_endpoint_entry_projection &&
                       !suppress_jumpaerial_entry_shallow_ledge_projection &&
                       !suppress_cliff_ledge_floor_shallow_projection &&
                       !suppress_cliff_ledge_remap_without_live_owner &&
                       (!is_ledge_floor ||
                        (ledge_escapeair_phase_owner && y_corr <= ledge_projection_depth_limit))) {
              batch->state.pos_y[idx] += y_corr;
              ground_id = resolved_segment_i;
              on_ground = 1;
              contact_x = cur_bottom_x;
              contact_y = cur_bottom_y + y_corr;
            }
          }
        }
        if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed == 1u &&
            prefer_line_idx >= 0 &&
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            g->lines[(size_t)prefer_line_idx].is_platform &&
            !floor_x_within_line_bounds(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx]) &&
            batch->state.speed_y_self[idx] < 0.0f && batch->state.action_frame[idx] <= 3) {
          // data/stages/bin/*.bin::MSLSTG01 platform flags/endpoints
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
          uint8_t found_platform = 0u;
          float best_lift = 0.0f;
          int best_line_idx = -1;
          float best_nx = 0.0f;
          float best_ny = 1.0f;
          for (size_t li = 0; li < g->line_count; li++) {
            if ((int)li == prefer_line_idx || !g->lines[li].is_platform ||
                stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                         g->lines[li].segment_i) ||
                !floor_line_is_runtime_fighter_solid(g, stage_id, (int)li) ||
                !floor_x_within_line_bounds(batch, bi, g, (int)li, batch->state.pos_x[idx])) {
              continue;
            }
            float line_y = 0.0f;
            if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, batch->state.pos_x[idx],
                                           &line_y)) {
              continue;
            }
            const float y_corr = (line_y + k_floor_y_bias) - batch->state.pos_y[idx];
            const float max_lift = msl_ecb_bottom_rel_y(char_id, anim, 0) +
                                   fabsf(batch->state.speed_y_self[idx]) +
                                   mpcoll_floor_projection_lift_allowance(&cur_ecb_points);
            if (y_corr >= 0.0f && y_corr <= max_lift && (!found_platform || y_corr < best_lift)) {
              found_platform = 1u;
              best_lift = y_corr;
              best_line_idx = (int)li;
              best_nx = 0.0f;
              best_ny = 1.0f;
            }
          }
          if (found_platform && best_line_idx >= 0) {
            batch->state.pos_y[idx] += best_lift;
            on_ground = 1;
            ground_id = g->lines[(size_t)best_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
            floor_nx = best_nx;
            floor_ny = best_ny;
            escapeair_stale_platform_root_handoff_hit = 1u;
          }
        }
        if (!on_ground && escapeair_locked && prefer_line_idx >= 0 &&
            (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B) &&
            batch->state.action_frame[idx] <= 2 && batch->state.speed_y_self[idx] < 0.0f) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          const float max_lift =
              fabsf(batch->state.speed_y_self[idx]) + (2.0f * k_ecb_vertical_unit);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
          } else {
            uint8_t found_height_platform = 0u;
            float best_height_platform_lift = 0.0f;
            int best_height_platform_line_idx = -1;
            float best_height_platform_nx = 0.0f;
            float best_height_platform_ny = 1.0f;
            for (size_t li = 0; li < g->line_count; li++) {
              if (!g->lines[li].is_platform ||
                  !stage_collision_floor_line_has_height_platform_transform(
                      stage_id, g->lines[li].segment_i) ||
                  !stage_collision_floor_line_height_platform_state_is_source_trusted(
                      batch, bi, g->lines[li].segment_i) ||
                  !floor_line_is_runtime_fighter_solid(g, stage_id, (int)li) ||
                  !floor_x_within_line_bounds(batch, bi, g, (int)li, batch->state.pos_x[idx])) {
                continue;
              }
              float line_y = 0.0f;
              if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, batch->state.pos_x[idx],
                                             &line_y)) {
                continue;
              }
              const float lift = (line_y + (2.0f * k_floor_y_bias)) - batch->state.pos_y[idx];
              if (lift < 0.0f || lift > max_lift ||
                  batch->state.pos_y[idx] > (line_y + (2.0f * k_floor_y_bias))) {
                continue;
              }
              if (!found_height_platform || lift < best_height_platform_lift ||
                  (lift == best_height_platform_lift &&
                   g->lines[li].segment_i <
                       g->lines[(size_t)best_height_platform_line_idx].segment_i)) {
                found_height_platform = 1u;
                best_height_platform_lift = lift;
                best_height_platform_line_idx = (int)li;
                best_height_platform_nx = 0.0f;
                best_height_platform_ny = 1.0f;
              }
            }
            if (found_height_platform && best_height_platform_line_idx >= 0) {
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
              // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              batch->state.pos_y[idx] += best_height_platform_lift;
              on_ground = 1;
              ground_id = g->lines[(size_t)best_height_platform_line_idx].segment_i;
              contact_x = batch->state.pos_x[idx];
              contact_y = batch->state.pos_y[idx] - k_floor_y_bias;
              floor_nx = best_height_platform_nx;
              floor_ny = best_height_platform_ny;
              escapeair_fresh_jump_height_platform_handoff_hit = 1u;
            }
          }
        }
        const uint8_t attackair_hitlag_exit_platform_to_hard_floor_owner =
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
            (is_attackair_action(action_id) && action_id != (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
             batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u &&
             prefer_line_idx >= 0 && prefer_line_is_platform && !prefer_line_is_ledge)
                ? 1u
                : 0u;
        if (!on_ground && batch->state.speed_y_self[idx] < 0.0f && is_attackair_action(action_id) &&
            (action_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW ||
             attackair_hitlag_exit_platform_to_hard_floor_owner) &&
            mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx) &&
            mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_471F8)) {
          MslMpcollFloorSweepResult attackair_hard_floor_sweep = {0};
          if (mpcoll_collect_bottom_sweep_hard_floor_result(
                  batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                  cur_bottom_y, prefer_line_idx, -1, 0u, &attackair_hard_floor_sweep) &&
              attackair_hard_floor_sweep.projected_line_idx >= 0 &&
              batch->state.pos_y[idx] < attackair_hard_floor_sweep.hit_y - k_floor_y_bias &&
              attackair_hard_floor_sweep.projected_y_corr >= 0.0f) {
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_471F8
            // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform_transform metadata
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
            batch->state.pos_y[idx] += attackair_hard_floor_sweep.projected_y_corr;
            mpcoll_publish_direct_source_floor_hit(
                &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
                attackair_hard_floor_sweep.projected_segment_id, attackair_hard_floor_sweep.hit_x,
                attackair_hard_floor_sweep.hit_y, attackair_hard_floor_sweep.normal_x,
                attackair_hard_floor_sweep.normal_y);
          }
        }
        float common_air_hard_floor_bottom_y_corr = 0.0f;
        if (!on_ground && batch->state.speed_y_self[idx] < 0.0f &&
            msl_mpcoll_80047e14_common_air_hard_floor_bottom_sweep(
                &mpcoll_ctx, batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                cur_bottom_x, cur_bottom_y, prefer_line_idx, skip_platform_segment_i, c, &ground_id,
                &common_air_hard_floor_bottom_y_corr, &contact_x, &contact_y, &floor_nx,
                &floor_ny)) {
          batch->state.pos_y[idx] += common_air_hard_floor_bottom_y_corr;
          common_fall_flags6_root_floor_projection_hit = 1u;
          mpcoll_publish_direct_source_floor_hit(
              &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id, contact_x,
              contact_y, floor_nx, floor_ny);
        }
        if (!on_ground && jumpaerial_entry_ecb_consumer && is_attackair_action(action_id) &&
            ecb_lock_timer_seed == 0u && have_state_cur_ecb &&
            state_cur_ecb_points.bottom_rel_y > cur_bot.rel_y &&
            batch->state.action_frame[idx] <= 2) {
          // data/stages/bin/*.bin::MSLSTG01 fighter_solid/stage_object_support_kind
          // data/stage_items/yoshi_shyguy.json
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{
          MslMpcollFloorSweepResult entry_platform_sweep = {0};
          const float source_prev_bottom_y = y + state_cur_ecb_points.bottom_rel_y;
          if (mpcoll_collect_bottom_sweep_hit(
                  batch, idx, bi, g, stage_id, prev_x, source_prev_bottom_y, x, cur_bottom_y,
                  skip_platform_segment_i, -1, -1, c, &entry_platform_sweep) &&
              entry_platform_sweep.hit_is_platform && entry_platform_sweep.hit_line_idx >= 0 &&
              (floor_line_is_runtime_fighter_solid(g, stage_id,
                                                   entry_platform_sweep.hit_line_idx) ||
               carried_floor_line_is_live_yoshi_shyguy_support(
                   batch, bi, g, stage_id, entry_platform_sweep.hit_line_idx)) &&
              !entry_platform_sweep.hit_has_platform_transform) {
            batch->state.pos_y[idx] = entry_platform_sweep.hit_y + k_floor_y_bias;
            mpcoll_publish_direct_source_floor_hit(
                &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
                entry_platform_sweep.hit_segment_id, entry_platform_sweep.hit_x,
                entry_platform_sweep.hit_y, entry_platform_sweep.normal_x,
                entry_platform_sweep.normal_y);
          }
        }
        const uint8_t jump_entry_ft80081d0c_platform_ecb_consumer =
            (!is_attackair_action(action_id) &&
             mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_471F8) &&
             (prev_action_id == (uint16_t)MSL_ACT_JUMP_F ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_B ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B))
                ? 1u
                : 0u;
        if (!on_ground && jump_entry_ft80081d0c_platform_ecb_consumer &&
            ecb_lock_timer_seed == 0u && have_state_cur_ecb &&
            state_cur_ecb_points.bottom_rel_y > cur_bot.rel_y &&
            batch->state.action_frame[idx] <= 2) {
          // data/motion_state/owners/{sheik,marth}.bin::MSLMSO01 phase AIR_471F8
          // data/stages/bin/*.bin::MSLSTG01 fighter_solid/stage_object_support_kind
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081D0C
          // refs/melee/src/melee/mp/mpcoll.c::{
          MslMpcollFloorSweepResult entry_platform_sweep = {0};
          const float source_prev_bottom_y = y + state_cur_ecb_points.bottom_rel_y;
          if (mpcoll_collect_bottom_sweep_hit(
                  batch, idx, bi, g, stage_id, prev_x, source_prev_bottom_y, x, cur_bottom_y,
                  skip_platform_segment_i, -1, -1, c, &entry_platform_sweep) &&
              entry_platform_sweep.hit_is_platform && entry_platform_sweep.hit_line_idx >= 0 &&
              (floor_line_is_runtime_fighter_solid(g, stage_id,
                                                   entry_platform_sweep.hit_line_idx) ||
               carried_floor_line_is_live_yoshi_shyguy_support(
                   batch, bi, g, stage_id, entry_platform_sweep.hit_line_idx)) &&
              !entry_platform_sweep.hit_has_platform_transform) {
            batch->state.pos_y[idx] = entry_platform_sweep.hit_y + k_floor_y_bias;
            mpcoll_publish_direct_source_floor_hit(
                &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
                entry_platform_sweep.hit_segment_id, entry_platform_sweep.hit_x,
                entry_platform_sweep.hit_y, entry_platform_sweep.normal_x,
                entry_platform_sweep.normal_y);
          }
        }
        if (!on_ground && mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_473CC) &&
            batch->state.coll_floor_probe_valid[idx] != 0u &&
            batch->state.coll_floor_probe_owner[idx] ==
                (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_47E14 &&
            batch->state.coll_floor_probe_reject_reason[idx] ==
                (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_OWNER) {
          // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 AIR_473CC
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_800473CC
          mpcoll_floor_probe_begin(&mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_473CC,
                                   source_phases, prefer_line_idx,
                                   (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_OWNER);
          mpcoll_floor_probe_bottom_interval(&mpcoll_ctx, prev_bottom_x, prev_bottom_y,
                                             cur_bottom_x, cur_bottom_y);
        }
        const uint16_t terminal_damage_source_action =
            is_common_damage_ground_pose_ecb_action(batch->state.seed_prev_action_id[idx])
                ? batch->state.seed_prev_action_id[idx]
                : prev_action_id;
        if (!on_ground && batch->state.speed_y_self[idx] < 0.0f &&
            msl_mpcoll_80047e14_flags6_root_floor_projection(
                batch, idx, bi, g, stage_id, prev_x, prev_y, x, y, cur_bottom_y,
                terminal_damage_source_action, prefer_line_idx, skip_platform_segment_i,
                ecb_lock_timer_seed, c, &ground_id, &contact_x, &contact_y, &floor_nx, &floor_ny)) {
          on_ground = 1u;
          common_fall_flags6_root_floor_projection_hit = 1u;
        }
        if (!on_ground && batch->state.speed_y_self[idx] < 0.0f &&
            msl_mpcoll_80047e14_fallspecial_connected_hard_floor_root_projection(
                batch, idx, bi, g, stage_id, prev_y, x, y, prefer_line_idx, &ground_id, &contact_x,
                &contact_y, &floor_nx, &floor_ny)) {
          on_ground = 1u;
          floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
        }
        if (!on_ground && attackair_flags0_floor_root_projection(
                              batch, idx, bi, g, stage_id, &prev_ecb_points, &cur_ecb_points,
                              prefer_line_idx, skip_platform_segment_i, c, &ground_id, &contact_x,
                              &contact_y, &floor_nx, &floor_ny)) {
          on_ground = 1u;
        }
        if (!on_ground && is_common_fallspecial_action(action_id) &&
            batch->state.speed_y_self[idx] < 0.0f) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80083090
          // refs/melee/src/melee/mp/mpcoll.c::{
          if (msl_mpcoll_80047e14_fallspecial_prephysics_floor_sweep(
                  batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bot.rel_y,
                  prefer_line_idx, skip_platform_segment_i, c, &ground_id, &contact_x, &contact_y,
                  &floor_nx, &floor_ny)) {
            on_ground = 1;
          }
        }
        if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            (prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR ||
             escapeair_no_lock_jumpaerial_entry) &&
            !ecb_lock_active &&
            (ecb_lock_timer_seed == 0u ||
             escapeair_terminal_locked_static_platform_sweep_owner != 0u) &&
            batch->state.speed_y_self[idx] < 0.0f) {
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
          uint8_t found_static_platform = 0u;
          int best_static_platform_line_idx = -1;
          float best_static_platform_dist2 = 0.0f;
          float best_static_platform_ix = 0.0f;
          float best_static_platform_iy = 0.0f;
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor}
          // refs/melee/src/melee/mp/mplib.c::mpLineIntersectionH
          const float prev_pose_bottom_y =
              escapeair_no_lock_jumpaerial_entry
                  ? prev_bottom_y
                  : (prev_y + mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_prev, 0u));
          const float cur_pose_bottom_y =
              escapeair_no_lock_jumpaerial_entry
                  ? cur_bottom_y
                  : (y + mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_bias_next, 0u));
          const uint16_t carried_floor_segment_i = batch->state.ground_id[idx];
          const uint8_t carried_floor_is_transformed =
              stage_collision_floor_line_has_platform_transform(stage_id, carried_floor_segment_i);
          if (prev_pose_bottom_y >= cur_pose_bottom_y) {
            for (size_t li = 0; li < g->line_count; li++) {
              const uint8_t line_is_static_y_transform =
                  stage_collision_floor_line_has_static_y_platform_transform(
                      stage_id, g->lines[li].segment_i);
              if (!g->lines[li].is_platform ||
                  (line_is_static_y_transform != 0u && carried_floor_is_transformed != 0u) ||
                  !floor_line_admitted_by_source_callback(batch, idx, g, stage_id, (int)li, 0xFFFFu,
                                                          c)) {
                continue;
              }
              float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
              floor_ed5c_endpoints(batch, bi, g, (int)li, &x0, &y0, &x1, &y1);
              float ix = 0.0f, iy = 0.0f;
              if (!floor_intersect_horiz(x0, y0, x1, prev_x, prev_pose_bottom_y, x,
                                         cur_pose_bottom_y, &ix, &iy)) {
                continue;
              }
              if (!escapeair_no_lock_jumpaerial_entry &&
                  !stage_collision_floor_line_has_height_platform_transform(
                      stage_id, g->lines[li].segment_i) &&
                  (iy - cur_pose_bottom_y) >
                      (fabsf(batch->state.speed_y_self[idx]) + k_floor_y_bias)) {
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
                continue;
              }
              const float dx = ix - prev_x;
              const float dy = iy - prev_pose_bottom_y;
              const float dist2 = dx * dx + dy * dy;
              if (!found_static_platform || dist2 < best_static_platform_dist2 ||
                  (dist2 == best_static_platform_dist2 &&
                   g->lines[li].segment_i <
                       g->lines[(size_t)best_static_platform_line_idx].segment_i)) {
                found_static_platform = 1u;
                best_static_platform_line_idx = (int)li;
                best_static_platform_dist2 = dist2;
                best_static_platform_ix = ix;
                best_static_platform_iy = iy;
              }
            }
          }
          if (!found_static_platform && carried_floor_is_transformed != 0u) {
            for (size_t li = 0; li < g->line_count; li++) {
              const MslStageFloorLine* line = &g->lines[li];
              const uint8_t line_is_static_y_transform =
                  stage_collision_floor_line_has_static_y_platform_transform(stage_id,
                                                                             line->segment_i);
              if (!line->is_platform || line_is_static_y_transform == 0u ||
                  !floor_line_admitted_by_source_callback(batch, idx, g, stage_id, (int)li, 0xFFFFu,
                                                          c)) {
                continue;
              }
              const MslStageFloorLine world = floor_line_world_for_env(batch, bi, g, (int)li);
              float ix = 0.0f;
              float iy = 0.0f;
              if (!floor_intersect_horiz(world.x0, world.y0, world.x1, prev_bottom_x, prev_bottom_y,
                                         cur_bottom_x, cur_bottom_y, &ix, &iy)) {
                continue;
              }
              if (!(y < (iy - k_floor_y_bias) &&
                    (y + cur_ecb_points.side_rel_y) >= (iy - k_floor_y_bias))) {
                continue;
              }
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=static_y)
              // refs/melee/src/melee/mp/mpcoll.c::{
              // refs/melee/src/melee/mp/mplib.c::{mpCheckFloorRemap,mpLineIntersectionH}
              const float dx = ix - prev_bottom_x;
              const float dy = iy - prev_bottom_y;
              const float dist2 = dx * dx + dy * dy;
              if (!found_static_platform || dist2 < best_static_platform_dist2 ||
                  (dist2 == best_static_platform_dist2 &&
                   line->segment_i < g->lines[(size_t)best_static_platform_line_idx].segment_i)) {
                found_static_platform = 1u;
                best_static_platform_line_idx = (int)li;
                best_static_platform_dist2 = dist2;
                best_static_platform_ix = ix;
                best_static_platform_iy = iy;
              }
            }
          }
          if (found_static_platform && best_static_platform_line_idx >= 0) {
            batch->state.pos_y[idx] = best_static_platform_iy + k_floor_y_bias;
            on_ground = 1;
            ground_id = g->lines[(size_t)best_static_platform_line_idx].segment_i;
            contact_x = best_static_platform_ix;
            contact_y = best_static_platform_iy;
            floor_nx = 0.0f;
            floor_ny = 1.0f;
            escapeair_no_lock_static_platform_sweep_hit = 1u;
          }
        }
        if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            escapeair_no_lock_jumpaerial_entry && !ecb_lock_active && ecb_lock_timer_seed == 0u &&
            fabsf(batch->state.speed_y_self[idx]) <= k_floor_horiz_dy_thresh) {
          // data/motion_state/owners/*.bin::MSLMSO01 PHASE4_ESCAPE_AIR_COLL
          // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform_transform metadata
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
          uint8_t found_entry_static_platform = 0u;
          int best_entry_static_platform_line_idx = -1;
          float best_entry_static_platform_lift = 0.0f;
          float best_entry_static_platform_y = 0.0f;
          for (size_t li = 0; li < g->line_count; li++) {
            const MslStageFloorLine* line = &g->lines[li];
            if (!line->is_platform ||
                line->platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE ||
                !floor_line_is_runtime_fighter_solid(g, stage_id, (int)li) ||
                !floor_line_admitted_by_source_callback(batch, idx, g, stage_id, (int)li, 0xFFFFu,
                                                        c) ||
                !floor_x_within_line_segment_strict(batch, bi, g, (int)li, x)) {
              continue;
            }
            float line_y = 0.0f;
            if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, x, &line_y)) {
              continue;
            }
            if (!(cur_bottom_y <= (line_y + k_floor_y_bias) &&
                  (y + cur_ecb_points.side_rel_y) >= (line_y - k_floor_y_bias) &&
                  y < (line_y - k_floor_y_bias))) {
              continue;
            }
            const float lift = (line_y + k_floor_y_bias) - y;
            if (!found_entry_static_platform || lift < best_entry_static_platform_lift ||
                (lift == best_entry_static_platform_lift &&
                 line->segment_i <
                     g->lines[(size_t)best_entry_static_platform_line_idx].segment_i)) {
              found_entry_static_platform = 1u;
              best_entry_static_platform_line_idx = (int)li;
              best_entry_static_platform_lift = lift;
              best_entry_static_platform_y = line_y;
            }
          }
          if (found_entry_static_platform && best_entry_static_platform_line_idx >= 0) {
            batch->state.pos_y[idx] = best_entry_static_platform_y + k_floor_y_bias;
            escapeair_no_lock_static_platform_sweep_hit = 1u;
            mpcoll_publish_direct_source_floor_hit(
                &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION,
                g->lines[(size_t)best_entry_static_platform_line_idx].segment_i, x,
                best_entry_static_platform_y, 0.0f, 1.0f);
          }
        }
        if (!on_ground &&
            is_just_entered_specialairn_end_from_loop(batch->state.char_id[idx], action_id,
                                                      prev_action_id,
                                                      batch->state.action_frame[idx]) &&
            prefer_line_idx >= 0 && batch->state.speed_y_self[idx] < 0.0f &&
            batch->state.pos_y[idx] <= -fabsf(cur_bot.rel_y)) {
          // refs/melee/src/melee/ft/ft_081B.c::ftCo_AirCatchHit_Coll
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          const float max_lift =
              cur_bot.rel_y + fabsf(batch->state.speed_y_self[idx]) + (2.0f * k_ecb_vertical_unit);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
          }
        }
        const float raw_sweep_prev_bottom_y =
            common_damage_post_unlock_pose_bottom_owner
                ? (prev_y + mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_prev, 0u))
                : prev_bottom_y;
        const float raw_sweep_cur_bottom_y =
            common_damage_post_unlock_pose_bottom_owner
                ? (y + mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_cur, 0u))
                : cur_bottom_y;
        // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
        const uint8_t can_sweep = (uint8_t)(raw_sweep_cur_bottom_y <= raw_sweep_prev_bottom_y);
        float escapeair_floorhug_line_y = 0.0f;
        const uint8_t escapeair_floorhug_line_y_valid =
            (prefer_line_idx >= 0 &&
             floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       &escapeair_floorhug_line_y))
                ? 1u
                : 0u;
        const uint8_t escapeair_sustained_floorhug_airborne =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.action_frame[idx] >= 2 &&
             fabsf(batch->state.speed_y_self[idx]) <= k_floor_horiz_dy_thresh &&
             escapeair_floorhug_line_y_valid != 0u &&
             fabsf(batch->state.pos_y[idx] - (escapeair_floorhug_line_y + k_floor_y_bias)) <=
                 k_floor_horiz_dy_thresh &&
             prefer_line_idx >= 0 && !g->lines[(size_t)prefer_line_idx].is_ledge)
                ? 1u
                : 0u;
        const uint8_t escapeair_fresh_horizontal_floorhug_airborne =
            (escapeair_locked &&
             (prev_action_id == (uint16_t)MSL_ACT_KNEE_BEND ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_F ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_B) &&
             batch->state.action_frame[idx] <= 1 &&
             fabsf(batch->state.speed_y_self[idx]) <= k_floor_horiz_dy_thresh &&
             escapeair_floorhug_line_y_valid != 0u &&
             fabsf(batch->state.pos_y[idx] - (escapeair_floorhug_line_y + k_floor_y_bias)) <=
                 k_floor_horiz_dy_thresh &&
             prefer_line_idx >= 0 && !g->lines[(size_t)prefer_line_idx].is_ledge)
                ? 1u
                : 0u;
        const uint8_t specialhi_bound_entry_airborne =
            ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_HI_BOUND) &&
             (msl_motion_state_fx_special_kind(batch->state.char_id[idx],
                                               batch->state.seed_prev_action_id[idx]) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) &&
             batch->state.action_frame[idx] <= 2)
                ? 1u
                : 0u;
        const uint8_t downdamage_active_hitlag_resting_floor_contact =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            ((action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
              action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) &&
             batch->state.hitlag_pre_timer[idx] != 0u && batch->state.speed_y_attack[idx] < 0.0f)
                ? 1u
                : 0u;
        if (!on_ground && escapeair_no_lock_jumpaerial_entry && can_sweep &&
            !active_damage_hitlag_airborne_floor_contact &&
            !damage_active_hitlag_downward_sdi_airborne_owner) {
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
          // data/stages/bin/*.bin::MSLSTG01 fighter_solid/is_ledge/platform_transform metadata
          MslStageQueryHit static_floor_hit = {0};
          if (stage_collision_static_query(stage_id, (uint32_t)MSL_STAGE_QUERY_FLOOR, prev_bottom_x,
                                           raw_sweep_prev_bottom_y, cur_bottom_x,
                                           raw_sweep_cur_bottom_y, skip_platform_segment_i, -1, -1,
                                           &static_floor_hit)) {
            const int static_floor_line_idx =
                stage_collision_floor_line_index(stage_id, static_floor_hit.segment_i);
            if (static_floor_line_idx >= 0 && (size_t)static_floor_line_idx < g->line_count &&
                floor_line_is_runtime_fighter_solid(g, stage_id, static_floor_line_idx) &&
                !g->lines[(size_t)static_floor_line_idx].is_platform &&
                !floor_line_is_generated_stage_slope(batch, bi, g, static_floor_line_idx) &&
                !stage_collision_floor_line_has_platform_transform(stage_id,
                                                                   static_floor_hit.segment_i)) {
              batch->state.pos_y[idx] = static_floor_hit.y + k_floor_y_bias;
              mpcoll_publish_direct_source_floor_hit(
                  &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
                  static_floor_hit.segment_i, static_floor_hit.x, static_floor_hit.y,
                  static_floor_hit.normal_x, static_floor_hit.normal_y);
            }
          }
        }
        if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && !ecb_lock_active &&
            ecb_lock_timer_seed == 0u && batch->state.action_frame[idx] <= 2 &&
            seed_prev_action_is_jumpaerial && can_sweep && batch->state.ledge_cooldown[idx] != 0u &&
            batch->state.cliff_ledge_floor_segment_id != NULL &&
            batch->state.cliff_ledge_floor_segment_id[idx] != 0xFFFFu &&
            !active_damage_hitlag_airborne_floor_contact &&
            !damage_active_hitlag_downward_sdi_airborne_owner) {
          // data/stages/bin/*.bin::MSLSTG01 is_ledge/fighter_solid floor metadata
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
          MslMpcollFloorSweepResult cliff_floor_sweep = {0};
          if (mpcoll_collect_bottom_sweep_hard_floor_result(
                  batch, idx, bi, g, stage_id, prev_bottom_x, raw_sweep_prev_bottom_y, cur_bottom_x,
                  raw_sweep_cur_bottom_y, prefer_line_idx, -1, 1u, &cliff_floor_sweep) &&
              cliff_floor_sweep.hit_line_idx >= 0 &&
              (size_t)cliff_floor_sweep.hit_line_idx < g->line_count &&
              cliff_floor_sweep.hit_is_ledge != 0u &&
              cliff_floor_sweep.hit_segment_id == batch->state.cliff_ledge_floor_segment_id[idx] &&
              !cliff_floor_sweep.hit_has_platform_transform &&
              floor_x_within_line_segment_strict(batch, bi, g, cliff_floor_sweep.hit_line_idx,
                                                 cliff_floor_sweep.hit_x)) {
            if (cliff_floor_sweep.projected_line_idx >= 0 &&
                cliff_floor_sweep.projected_y_corr >= 0.0f) {
              batch->state.pos_y[idx] += cliff_floor_sweep.projected_y_corr;
              ground_id = cliff_floor_sweep.projected_segment_id;
            } else {
              batch->state.pos_x[idx] += (cliff_floor_sweep.hit_x - cur_bottom_x);
              batch->state.pos_y[idx] +=
                  (cliff_floor_sweep.hit_y - raw_sweep_cur_bottom_y) + k_floor_y_bias;
              ground_id = cliff_floor_sweep.hit_segment_id;
            }
            mpcoll_publish_direct_source_floor_hit(
                &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id,
                cliff_floor_sweep.hit_x, cliff_floor_sweep.hit_y, cliff_floor_sweep.normal_x,
                cliff_floor_sweep.normal_y);
          }
        }
        MslMpcollFloorSweepResult raw_floor_sweep = {0};
        if (!on_ground && !active_damage_hitlag_airborne_floor_contact &&
            !damage_active_hitlag_downward_sdi_airborne_owner && can_sweep &&
            mpcoll_collect_bottom_sweep_hit(batch, idx, bi, g, stage_id, prev_bottom_x,
                                            raw_sweep_prev_bottom_y, cur_bottom_x,
                                            raw_sweep_cur_bottom_y, skip_platform_segment_i,
                                            prefer_line_idx, -1, c, &raw_floor_sweep)) {
          hit_line_idx = raw_floor_sweep.hit_line_idx;
          ix = raw_floor_sweep.hit_x;
          iy = raw_floor_sweep.hit_y;
          floor_nx = raw_floor_sweep.normal_x;
          floor_ny = raw_floor_sweep.normal_y;
          const uint8_t hit_line_is_platform = raw_floor_sweep.hit_is_platform;
          const uint8_t hit_line_is_slope =
              floor_line_is_generated_stage_slope(batch, bi, g, hit_line_idx);
          const uint8_t hit_line_is_ledge = raw_floor_sweep.hit_is_ledge;
          const uint8_t hit_line_is_platform_or_slope =
              (uint8_t)((hit_line_is_platform || hit_line_is_slope) ? 1u : 0u);
          const uint8_t hit_line_is_platform_slope_or_ledge =
              (uint8_t)((hit_line_is_platform || hit_line_is_slope || hit_line_is_ledge) ? 1u : 0u);
          const uint8_t hit_line_is_generated_sloped_ledge =
              floor_line_is_generated_sloped_ledge(batch, bi, g, hit_line_idx);
          const uint8_t hit_line_has_platform_transform =
              raw_floor_sweep.hit_has_platform_transform;
          const uint8_t hit_line_has_height_platform_transform =
              raw_floor_sweep.hit_has_height_platform_transform;
          const uint8_t hit_line_x_in_strict_segment =
              (hit_line_idx >= 0 &&
               floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, cur_bottom_x))
                  ? 1u
                  : 0u;
          const uint8_t hit_line_matches_carried_cliff_ledge_floor =
              (hit_line_is_ledge && hit_line_x_in_strict_segment &&
               batch->state.ledge_cooldown[idx] != 0u &&
               batch->state.cliff_ledge_floor_segment_id != NULL &&
               batch->state.cliff_ledge_floor_segment_id[idx] ==
                   g->lines[(size_t)hit_line_idx].segment_i)
                  ? 1u
                  : 0u;
          const uint8_t hit_line_is_current_carried_cliff_ledge_floor =
              (hit_line_matches_carried_cliff_ledge_floor && raw_current_floor_line_idx >= 0 &&
               g->lines[(size_t)raw_current_floor_line_idx].segment_i ==
                   g->lines[(size_t)hit_line_idx].segment_i)
                  ? 1u
                  : 0u;
          const uint8_t hit_line_is_carried_cliff_ledge_floor =
              (hit_line_matches_carried_cliff_ledge_floor &&
               (cliff_ledge_floor_owner_active || hit_line_is_current_carried_cliff_ledge_floor))
                  ? 1u
                  : 0u;
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
          const uint8_t escapeair_ledge_bottom_sweep_floor_owner =
              (escapeair_locked && hit_line_is_carried_cliff_ledge_floor &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               prev_bottom_y > (iy + k_floor_y_bias) && cur_bottom_y <= (iy + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t escapeair_cliff_ledge_floor_owner =
              // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
              (escapeair_locked && hit_line_is_carried_cliff_ledge_floor &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.action_frame[idx] >= 3 && batch->state.action_frame[idx] <= 4 &&
               y < (iy - k_floor_y_bias))
                  ? 1u
                  : 0u;
          if (escapeair_cliff_ledge_floor_owner) {
            escapeair_live_cliff_ledge_bottom_sweep_owner = 1u;
            escapeair_live_cliff_ledge_source_floor_owner = 1u;
          } else if (escapeair_ledge_bottom_sweep_floor_owner &&
                     batch->state.ledge_cooldown[idx] != 0u &&
                     batch->state.cliff_ledge_floor_segment_id != NULL &&
                     batch->state.cliff_ledge_floor_segment_id[idx] ==
                         g->lines[(size_t)hit_line_idx].segment_i) {
            escapeair_live_cliff_ledge_source_floor_owner = 1u;
          }
          const uint8_t escapeair_sustained_floor_handoff =
              (escapeair_locked &&
               // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
               prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.action_frame[idx] >= 3 &&
               (hit_line_idx < 0 || !g->lines[(size_t)hit_line_idx].is_ledge ||
                ecb_lock_timer >= 5u || escapeair_early_ledge_root_floor_owner ||
                escapeair_ledge_bottom_sweep_floor_owner || escapeair_cliff_ledge_floor_owner))
                  ? 1u
                  : 0u;
          const uint8_t escapeair_kneebend_entry_floor_handoff =
              (escapeair_locked &&
               // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
               prev_action_id == (uint16_t)MSL_ACT_KNEE_BEND &&
               batch->state.action_frame[idx] <= 2 && !hit_line_matches_carried_cliff_ledge_floor &&
               !escapeair_fresh_horizontal_floorhug_airborne)
                  ? 1u
                  : 0u;
          const uint8_t escapeair_jump_entry_floor_handoff =
              (escapeair_locked &&
               // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
               (((prev_action_id == (uint16_t)MSL_ACT_JUMP_F ||
                  prev_action_id == (uint16_t)MSL_ACT_JUMP_B) &&
                 batch->state.action_frame[idx] <= 1) ||
                (prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                 (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
                  batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B) &&
                 batch->state.seed_prev_action_frame[idx] <= 4 &&
                 batch->state.action_frame[idx] <= 3 && hit_line_is_ledge &&
                 escapeair_ledge_bottom_sweep_floor_owner)) &&
               !escapeair_fresh_horizontal_floorhug_airborne)
                  ? 1u
                  : 0u;
          const uint8_t escapeair_flags6_deep_floor_handoff =
              // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
              // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline,mpColl_80043754,mpColl_80044838_Floor}
              (escapeair_locked && hit_line_idx >= 0 && hit_line_x_in_strict_segment &&
               (!hit_line_matches_carried_cliff_ledge_floor || cliff_ledge_floor_owner_active ||
                hit_line_is_current_carried_cliff_ledge_floor) &&
               (iy - cur_bottom_y) >= msl_ecb_bottom_rel_y(char_id, anim, 0))
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_ledge_land =
              (escapeair_locked && !deep_lock_penetration &&
               !escapeair_live_cliff_ledge_source_floor_owner && hit_line_idx >= 0 &&
               g->lines[(size_t)hit_line_idx].is_ledge && !escapeair_sustained_floor_handoff &&
               !escapeair_early_ledge_root_floor_owner && !escapeair_kneebend_entry_floor_handoff &&
               !escapeair_jump_entry_floor_handoff && !escapeair_flags6_deep_floor_handoff)
                  ? 1u
                  : 0u;
          const uint8_t suppress_stale_carried_cliff_ledge_land =
              (escapeair_locked && hit_line_matches_carried_cliff_ledge_floor &&
               !cliff_ledge_floor_owner_active && !hit_line_is_current_carried_cliff_ledge_floor)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_off_end_platform_land =
              // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
              (escapeair_locked && hit_line_idx >= 0 && hit_line_is_platform &&
               !hit_line_x_in_strict_segment && batch->state.action_frame[idx] <= 3)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_seed6_platform_land =
              // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
              (escapeair_locked && ecb_lock_timer_seed == 6u &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               hit_line_idx >= 0 && hit_line_is_platform_or_slope &&
               batch->state.action_frame[idx] <= 3)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_vertical_af3_land =
              (escapeair_locked && !deep_lock_penetration && !escapeair_flags6_deep_floor_handoff &&
               hit_line_idx >= 0 && !hit_line_is_ledge && batch->state.action_frame[idx] == 3 &&
               (hit_line_is_platform_or_slope ||
                (prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                 (batch->state.pos_y[idx] > -0.25f * k_ecb_vertical_unit ||
                  batch->state.prev_pos_y[idx] < 0.0f))) &&
               fabsf(cur_bottom_x - prev_bottom_x) <= (float)k_floor_horiz_dy_thresh)
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_no_lock_vertical_af3_land =
              (!escapeair_locked && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_idx >= 0 &&
               !hit_line_is_platform &&
               // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
               // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpColl_80044628_Floor}
               batch->state.action_frame[idx] == 3 &&
               fabsf(cur_bottom_x - prev_bottom_x) <= (float)k_floor_horiz_dy_thresh)
                  ? 1u
                  : 0u;
          const uint8_t suppress_seeded_escapeair_first_locked_land =
              // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
              (escapeair_locked && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.action_frame[idx] <= 4 &&
               (hit_line_has_platform_transform || hit_line_is_slope))
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_locked_desired_bottom_above_floor_land =
              // refs/melee/src/melee/mp/mpcoll.c::{
              (escapeair_locked && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.action_frame[idx] <= 4 && !hit_line_is_platform &&
               !hit_line_has_platform_transform && !escapeair_early_ledge_root_floor_owner &&
               !escapeair_cliff_ledge_floor_owner &&
               batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
               msl_escapeair_locked_bottom_owner_any(
                   batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
               batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
               hit_line_idx >= 0 &&
               (cur_bottom_y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) >
                   (iy + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t suppress_kneebend_escapeair_missing_ledge_owner_entry_land =
              // data/stages/bin/*.bin::MSLSTG01 is_ledge + fighter_solid floor metadata
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              (cliff_ledge_floor_owner_active && escapeair_locked &&
               action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_is_ledge &&
               (!escapeair_live_cliff_ledge_source_floor_owner ||
                batch->state.action_frame[idx] <= 2) &&
               ((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                 batch->state.action_frame[idx] <= 2) ||
                (ecb_lock_timer_seed != 0u && !was_grounded &&
                 batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
                 batch->state.seed_prev_action_frame[idx] >= 4)))
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_jump_entry_platform_from_below =
              // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
              (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_is_platform &&
               !hit_line_has_platform_transform &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
               batch->state.action_frame[idx] <= 3 && ecb_lock_active &&
               batch->state.seed_prev_action_frame[idx] <= 6 && prev_bottom_y <= iy &&
               cur_bottom_y <= iy)
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_late_jump_entry_platform_lifetime =
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_is_platform &&
               !hit_line_has_platform_transform &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
               batch->state.action_frame[idx] <= 3 && ecb_lock_active &&
               batch->state.seed_prev_action_frame[idx] >= 5 &&
               batch->state.seed_prev_action_frame[idx] <= 6)
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_platform_root_snap_without_bottom_hit =
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_is_platform &&
               cur_bottom_y > (iy + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t jumpaerial_carried_cliff_ledge_deep_entry_owner =
              (hit_line_is_carried_cliff_ledge_floor &&
               (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
               batch->state.floor_sweep_prev_pos_y[idx] <=
                   (iy -
                    ((2.0f * msl_ecb_bottom_rel_y(char_id, anim, 0)) - (10.0f * k_floor_y_bias))))
                  ? 1u
                  : 0u;
          const uint8_t suppress_jumpaerial_escapeair_entry_land =
              ((escapeair_fresh_jumpaerial_entry_lock && hit_line_is_platform_slope_or_ledge &&
                !jumpaerial_carried_cliff_ledge_deep_entry_owner) ||
               (hit_line_matches_carried_cliff_ledge_floor &&
                (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                 batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
                batch->state.seed_prev_action_frame[idx] >= 4 &&
                batch->state.action_frame[idx] <= 2 &&
                !jumpaerial_carried_cliff_ledge_deep_entry_owner))
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_no_lock_jumpaerial_entry_sloped_ledge_land =
              // data/stages/bin/grst.bin::MSLSTG01 sloped ledge floor segments
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              (hit_line_is_generated_sloped_ledge &&
               g->lines[(size_t)hit_line_idx].segment_i != seed_ground_id &&
               !floor_lines_connected(g, hit_line_idx,
                                      stage_collision_floor_line_index(stage_id, seed_ground_id)) &&
               (escapeair_no_lock_jumpaerial_entry ||
                ((action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                  action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
                 (batch->state.input_buttons[idx] &
                  (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) != 0u)))
                  ? 1u
                  : 0u;
          const uint8_t fallspecial_entered_from_sideb_air_end =
              (is_common_fallspecial_action(action_id) &&
               is_spacie_sideb_air_end_fallspecial_source(char_id,
                                                          batch->state.seed_prev_action_id[idx]))
                  ? 1u
                  : 0u;
          const uint8_t suppress_fallspecial_entry_af3_land =
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80047E14}
              (is_common_fallspecial_action(action_id) &&
               batch->state.prev_action_frame[idx] == 3 && batch->state.speed_y_self[idx] < 0.0f &&
               (hit_line_is_platform_slope_or_ledge || !fallspecial_entered_from_sideb_air_end))
                  ? 1u
                  : 0u;
          const uint8_t fallspecial_entered_from_specialhi_end =
              (is_common_fallspecial_action(action_id) &&
               is_spacie_specialhi_end_fallspecial_source(batch->state.char_id[idx],
                                                          batch->state.seed_prev_action_id[idx]))
                  ? 1u
                  : 0u;
          const uint8_t suppress_fallspecial_platform_first_root_crossing =
              // refs/melee/src/melee/mp/mpcoll.c::{
              (is_common_fallspecial_action(action_id) && hit_line_is_platform &&
               (platform_pass_input_below_raw_threshold(batch, idx, c) ||
                !fallspecial_entered_from_specialhi_end) &&
               batch->state.prev_pos_y[idx] > iy && batch->state.pos_y[idx] < iy)
                  ? 1u
                  : 0u;
          const uint8_t suppress_fallspecial_same_floor_early_root_crossing =
              (hit_line_idx >= 0 &&
               fallspecial_sustained_same_terminal_cardinal_floor_delay(
                   batch, idx, bi, g, stage_id, action_id, batch->state.ground_id[idx],
                   g->lines[(size_t)hit_line_idx].segment_i, hit_line_idx, iy,
                   batch->state.pos_y[idx]) &&
               batch->state.prev_pos_y[idx] > iy)
                  ? 1u
                  : 0u;
          const MslCharParams* floor_cross_chp = msl_char_params_fast(char_id);
          const uint8_t fall_ledge_floor_uses_expanded_collision_model =
              (floor_cross_chp != NULL && isfinite(floor_cross_chp->model_scaling) &&
               floor_cross_chp->model_scaling > 1.0f)
                  ? 1u
                  : 0u;
          const uint8_t suppress_fall_ledge_floor_first_root_crossing =
              // ftCo_DatAttrs::model_scaling in `data/characters/*.json`.
              // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor}
              (fall_ledge_floor_uses_expanded_collision_model &&
               action_id == (uint16_t)MSL_ACT_FALL && prev_action_id == (uint16_t)MSL_ACT_FALL &&
               hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_ledge &&
               (prefer_line_idx < 0 || !g->lines[(size_t)prefer_line_idx].is_ledge) &&
               batch->state.fall_fast[idx] == 0u && batch->state.action_frame[idx] >= 2 &&
               batch->state.action_frame[idx] <= 3 && batch->state.speed_y_self[idx] < 0.0f &&
               prev_y > (iy + k_floor_y_bias) && batch->state.pos_y[idx] < iy)
                  ? 1u
                  : 0u;
          const uint8_t fall_no_floor_shallow_fastfall_contact =
              // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
              // refs/melee/src/melee/ft/types.h::Fighter::allow_interrupt (fp+0x2218:0)
              (action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] != 0u &&
               batch->state.ground_id[idx] == 0xFFFFu && ecb_lock_timer_seed == 0u &&
               batch->state.action_frame[idx] <= 4 && hit_line_idx >= 0 &&
               fabsf(batch->state.speed_air_x_self[idx]) > 0.5f &&
               (iy - cur_bottom_y) > k_floor_y_bias && (iy - cur_bottom_y) < 0.35f &&
               (batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                         (size_t)MSL_STATE_FLAGS_2218_INDEX] &
                (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) == 0u)
                  ? 1u
                  : 0u;
          const uint8_t suppress_damageflyroll_shallow_land =
              (damageflyroll_iasa_lockout && !damageflyroll_deep_side_penetration &&
               damageflyroll_root_proj_ready &&
               damageflyroll_root_proj_y_corr < damageflyroll_side_y_thresh)
                  ? 1u
                  : 0u;
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{
          const uint8_t suppress_active_damage_hitlag_land =
              (batch->state.hitlag[idx] != 0u && is_damage_collision_landing_action(action_id))
                  ? 1u
                  : 0u;
          const uint8_t suppress_active_damage_hitlag_bottom_above_floor_land =
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
              (suppress_active_damage_hitlag_land &&
               batch->state.tilt_timer_y_frame_start[idx] >= c->sdi_tilt_max_frames &&
               damage_hitlag_floorhug_attempts_downward_sdi(batch, idx, c) != 0u &&
               hit_line_idx >= 0 && !hit_line_is_platform &&
               !g->lines[(size_t)hit_line_idx].is_ledge && cur_bottom_y > (iy + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t suppress_damageflyroll_below_floor_active_hitlag_land =
              (action_id == MSL_ACT_DAMAGE_FLY_ROLL && batch->state.hitlag[idx] != 0u &&
               batch->state.pos_y[idx] < 0.0f)
                  ? 1u
                  : 0u;
          const uint8_t suppress_damageair_attackair_entry_land =
              (is_attackair_action(action_id) && hit_line_is_platform_or_slope &&
               (prev_action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_1 ||
                prev_action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_2 ||
                prev_action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_3) &&
               batch->state.action_frame[idx] <= 1)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locomotion_attackair_entry_platform_land =
              // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
              (is_attackair_action(action_id) && batch->state.action_frame[idx] <= 1 &&
               hit_line_is_platform_or_slope &&
               (prev_action_id == (uint16_t)MSL_ACT_FALL ||
                prev_action_id == (uint16_t)MSL_ACT_FALL_AERIAL))
                  ? 1u
                  : 0u;
          const uint8_t suppress_attackair_jumpaerial_stage_object_support_land =
              // data/stages/bin/*.bin::MSLSTG01 fighter_solid/stage_object_support_kind
              // data/stage_items/yoshi_shyguy.json
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              (is_attackair_action(action_id) && batch->state.action_frame[idx] <= 2 &&
               hit_line_idx >= 0 &&
               (prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
                batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
               !floor_line_is_runtime_fighter_solid(g, stage_id, hit_line_idx) &&
               stage_collision_floor_line_stage_object_support_kind(
                   stage_id, g->lines[(size_t)hit_line_idx].segment_i) !=
                   (uint8_t)MSL_STAGE_OBJECT_SUPPORT_NONE &&
               !carried_floor_line_is_live_yoshi_shyguy_support(batch, bi, g, stage_id,
                                                                hit_line_idx))
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_entry_locked_platform_land =
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80047E14}
              (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && batch->state.action_frame[idx] <= 4 &&
               hit_line_is_platform && hit_line_has_platform_transform &&
               (!escapeair_sustained_floor_handoff ||
                (hit_line_idx >= 0 &&
                 g->lines[(size_t)hit_line_idx].segment_i != batch->state.ground_id[idx])) &&
               (ecb_lock_active ||
                (prev_action_id == action_id &&
                 g->lines[(size_t)hit_line_idx].segment_i != batch->state.ground_id[idx])))
                  ? 1u
                  : 0u;
          const uint8_t carried_transformed_platform_skip =
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
              0u;
          float proj_x = 0.0f;
          float proj_y = 0.0f;
          stay_airborne_floor_projection_point(batch->state.pos_x[idx], batch->state.pos_y[idx],
                                               cur_bottom_x, cur_bottom_y, &proj_x, &proj_y);
          float y_corr = 0.0f;
          const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, proj_x,
                                                            proj_y, &y_corr, NULL, NULL);
          const uint16_t projected_segment_i =
              (out_line_idx >= 0) ? g->lines[(size_t)out_line_idx].segment_i : 0xFFFFu;
          const uint8_t projected_line_has_platform_transform =
              stage_collision_floor_line_has_platform_transform(stage_id, projected_segment_i);
          const uint8_t projected_line_has_height_platform_transform =
              stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                       projected_segment_i);
          const uint8_t attackair_transformed_platform_floor_skip_active =
              (skip_platform_segment_i != 0xFFFFu && first_phase_attackair_platform_ecb_owner &&
               prev_action_id == action_id &&
               stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                        skip_platform_segment_i))
                  ? 1u
                  : 0u;
          const uint8_t attackair_transformed_platform_floor_skip_released =
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpClearFloorSkip}
              (skip_platform_segment_i != 0xFFFFu && prev_action_id == action_id &&
               stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                        skip_platform_segment_i) &&
               c != NULL &&
               stick_i8_to_unit(batch->state.input_main_y[idx]) >
                   c->platform_air_land_stick_y_threshold)
                  ? 1u
                  : 0u;
          const float attackair_transformed_platform_edge_slack =
              fmaxf(fabsf(cur_ecb_points.left_rel_x), fabsf(cur_ecb_points.right_rel_x)) +
              k_floor_x_end_clamp;
          float attackair_transformed_platform_line_y = 0.0f;
          const uint8_t attackair_transformed_platform_line_valid =
              (hit_line_idx >= 0 &&
               floor_line_y_at_x_for_env(batch, bi, g, hit_line_idx, x,
                                         &attackair_transformed_platform_line_y))
                  ? 1u
                  : 0u;
          const uint8_t attackair_transformed_platform_edge_contact =
              (hit_line_idx >= 0 && hit_line_has_height_platform_transform &&
               floor_line_x_near_endpoint_for_env(batch, bi, g, hit_line_idx, cur_bottom_x,
                                                  attackair_transformed_platform_edge_slack))
                  ? 1u
                  : 0u;
          const uint8_t attackair_transformed_platform_root_edge_contact =
              (hit_line_idx >= 0 && hit_line_has_height_platform_transform &&
               floor_line_x_near_endpoint_for_env(batch, bi, g, hit_line_idx, x,
                                                  attackair_transformed_platform_edge_slack))
                  ? 1u
                  : 0u;
          const uint8_t projected_attackair_transformed_platform_edge_contact =
              (out_line_idx >= 0 && projected_line_has_height_platform_transform &&
               floor_line_x_near_endpoint_for_env(batch, bi, g, out_line_idx, cur_bottom_x,
                                                  attackair_transformed_platform_edge_slack))
                  ? 1u
                  : 0u;
          const uint8_t hit_line_height_platform_current_source =
              (hit_line_has_height_platform_transform &&
               stage_height_platform_line_has_current_source(
                   batch, bi, stage_id, g->lines[(size_t)hit_line_idx].segment_i))
                  ? 1u
                  : 0u;
          const uint8_t hit_line_height_platform_live_scheduler =
              (hit_line_has_height_platform_transform &&
               stage_height_platform_line_has_live_scheduler_source(
                   batch, bi, stage_id, g->lines[(size_t)hit_line_idx].segment_i))
                  ? 1u
                  : 0u;
          const int16_t attackair_second_create_frame =
              move_tables_attackair_second_create_hitbox_frame(char_id, action_id);
          const int16_t attackair_first_create_frame =
              move_tables_attackair_first_create_hitbox_frame(char_id, action_id);
          const uint8_t attackairlw_live_bottom_floor_owner =
              (action_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW && attackair_first_create_frame >= 0 &&
               attackair_second_create_frame >= 0 &&
               batch->state.action_frame[idx] >= (uint16_t)attackair_first_create_frame &&
               batch->state.action_frame[idx] <= (uint16_t)attackair_second_create_frame &&
               (batch->state.floor_sweep_prev_runtime_owned[idx] != 0u ||
                batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] != 0u))
                  ? 1u
                  : 0u;
          const uint8_t attackair_transformed_platform_downheld_inspan_pass =
              // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
              // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              (hit_line_idx >= 0 && hit_line_has_height_platform_transform &&
               !hit_line_height_platform_current_source &&
               !hit_line_height_platform_live_scheduler &&
               g->lines[(size_t)hit_line_idx].segment_i != batch->state.ground_id[idx] &&
               floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, x) &&
               !attackairlw_live_bottom_floor_owner &&
               platform_pass_input_below_raw_threshold(batch, idx, c) && c != NULL &&
               stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                   c->platform_air_land_stick_y_threshold)
                  ? 1u
                  : 0u;
          const uint8_t attackair_transformed_platform_edge_owner_active =
              (attackair_transformed_platform_floor_skip_active ||
               attackair_transformed_platform_downheld_inspan_pass)
                  ? 1u
                  : 0u;
          const uint8_t projected_line_height_platform_current_source =
              (projected_line_has_height_platform_transform &&
               stage_height_platform_line_has_current_source(batch, bi, stage_id,
                                                             projected_segment_i))
                  ? 1u
                  : 0u;
          const uint8_t projected_line_height_platform_live_scheduler =
              (projected_line_has_height_platform_transform &&
               stage_height_platform_line_has_live_scheduler_source(batch, bi, stage_id,
                                                                    projected_segment_i))
                  ? 1u
                  : 0u;
          const uint8_t attackair_single_create_script =
              (attackair_first_create_frame >= 0 && attackair_second_create_frame < 0) ? 1u : 0u;
          const uint8_t attackair_height_platform_no_current_first_phase =
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
              (((hit_line_has_height_platform_transform &&
                 !hit_line_height_platform_current_source &&
                 !hit_line_height_platform_live_scheduler) ||
                (projected_line_has_height_platform_transform &&
                 !projected_line_height_platform_current_source &&
                 !projected_line_height_platform_live_scheduler)) &&
               is_attackair_action(action_id) && prev_action_id == action_id &&
               attackair_transformed_platform_edge_owner_active &&
               attackair_second_create_frame >= 0 &&
               !attackair_transformed_platform_floor_skip_released &&
               batch->state.anim_frame_f32[idx] < (float)attackair_second_create_frame)
                  ? 1u
                  : 0u;
          const uint8_t attackair_height_platform_no_current_single_create =
              // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              // data/scripts/<char>.bin::MSLFTSC1 create_hitbox events
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
              (((hit_line_has_height_platform_transform &&
                 !hit_line_height_platform_current_source &&
                 !hit_line_height_platform_live_scheduler) ||
                (projected_line_has_height_platform_transform &&
                 !projected_line_height_platform_current_source &&
                 !projected_line_height_platform_live_scheduler)) &&
               is_attackair_action(action_id) && prev_action_id == action_id &&
               attackair_single_create_script && attackair_transformed_platform_line_valid &&
               y < attackair_transformed_platform_line_y &&
               !attackair_transformed_platform_floor_skip_released)
                  ? 1u
                  : 0u;
          const float attackair_transformed_platform_prev_below_depth =
              attackair_transformed_platform_line_y - prev_y;
          const uint8_t attackair_transformed_platform_shallow_edge_continuation =
              (attackair_transformed_platform_edge_owner_active &&
               attackair_transformed_platform_line_valid &&
               attackair_transformed_platform_root_edge_contact &&
               attackair_transformed_platform_prev_below_depth > k_floor_y_bias &&
               attackair_transformed_platform_prev_below_depth <= (2.0f * k_ecb_vertical_unit))
                  ? 1u
                  : 0u;
          const uint8_t suppress_attackair_transformed_platform_root_below_land =
              // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
              // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              (((attackair_transformed_platform_edge_owner_active &&
                 (attackair_transformed_platform_edge_contact ||
                  attackair_transformed_platform_root_edge_contact)) ||
                attackair_transformed_platform_shallow_edge_continuation) &&
               (hit_line_is_platform || hit_line_has_height_platform_transform) &&
               first_phase_attackair_platform_ecb_owner && prev_action_id == action_id &&
               move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                        batch->state.anim_frame_f32[idx]) &&
               attackair_transformed_platform_line_valid &&
               !attackair_transformed_platform_floor_skip_released &&
               y < attackair_transformed_platform_line_y)
                  ? 1u
                  : 0u;
          const uint8_t suppress_attackair_transformed_platform_floor_skip_first_crossing_land =
              // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              (attackair_transformed_platform_floor_skip_active &&
               floor_line_is_runtime_fighter_solid(g, stage_id, hit_line_idx) &&
               !attackair_transformed_platform_floor_skip_released &&
               (!floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, x) ||
                floor_line_is_generated_stage_slope(batch, bi, g, hit_line_idx)) &&
               attackair_transformed_platform_line_valid &&
               prev_y > (attackair_transformed_platform_line_y + k_floor_y_bias) &&
               y < attackair_transformed_platform_line_y && batch->state.speed_y_self[idx] < 0.0f)
                  ? 1u
                  : 0u;
          const uint8_t suppress_attackair_offspan_hard_floor_edge_land =
              // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
              (hit_line_idx >= 0 &&
               floor_line_is_runtime_fighter_solid(g, stage_id, hit_line_idx) &&
               !hit_line_has_height_platform_transform && shallow_attackair_platform_ecb_owner &&
               prev_action_id == action_id &&
               move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                        batch->state.anim_frame_f32[idx]) &&
               !floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, x) &&
               attackair_transformed_platform_line_valid &&
               prev_y > (attackair_transformed_platform_line_y + k_floor_y_bias) &&
               y < attackair_transformed_platform_line_y && batch->state.speed_y_self[idx] < 0.0f)
                  ? 1u
                  : 0u;
          const uint8_t suppress_downheld_transformed_platform_land =
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
              (((hit_line_is_platform && hit_line_has_platform_transform && c != NULL &&
                 mpcoll_source_phases_has(coll_data.source_phases,
                                          MSL_MPCOLL_PHASE_PLATFORM_PASS) &&
                 (stick_i8_to_unit(batch->state.input_main_y[idx]) <=
                      c->platform_air_land_stick_y_threshold ||
                  stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                      c->platform_air_land_stick_y_threshold)) ||
                attackair_transformed_platform_downheld_inspan_pass) ||
               (projected_line_has_platform_transform &&
                action_id == (uint16_t)MSL_ACT_FALL_SPECIAL_B &&
                !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx])) ||
               suppress_attackair_transformed_platform_root_below_land ||
               suppress_attackair_transformed_platform_floor_skip_first_crossing_land ||
               suppress_attackair_offspan_hard_floor_edge_land || carried_transformed_platform_skip)
                  ? 1u
                  : 0u;
          const float transformed_platform_bottom_penetration = iy - cur_bottom_y;
          const uint8_t damage_transformed_platform_offspan_contact =
              // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              (((hit_line_has_platform_transform &&
                 !floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, x)) ||
                (projected_line_has_platform_transform && out_line_idx >= 0 &&
                 !floor_x_within_line_segment_strict(batch, bi, g, out_line_idx, x))) &&
               is_damage_collision_landing_action(action_id) &&
               batch->state.speed_y_attack[idx] > 0.0f && batch->state.hitstun[idx] != 0u)
                  ? 1u
                  : 0u;
          const MslCharParams* damage_floor_chp = msl_char_params_fast(char_id);
          const float damage_height_platform_edge_slack =
              (damage_floor_chp != NULL && isfinite(damage_floor_chp->ledge_snap_height))
                  ? (damage_floor_chp->ledge_snap_height * batch->state.fighter_scale_y[idx])
                  : k_ecb_vertical_unit;
          const uint8_t hit_line_height_platform_state_trusted =
              (hit_line_has_height_platform_transform &&
               stage_collision_floor_line_height_platform_state_is_source_trusted(
                   batch, bi, g->lines[(size_t)hit_line_idx].segment_i))
                  ? 1u
                  : 0u;
          const uint8_t projected_line_height_platform_state_trusted =
              (projected_line_has_height_platform_transform &&
               stage_collision_floor_line_height_platform_state_is_source_trusted(
                   batch, bi, projected_segment_i))
                  ? 1u
                  : 0u;
          const uint8_t hit_line_height_platform_state_current_owned =
              (hit_line_has_height_platform_transform &&
               stage_collision_floor_line_height_platform_state_is_current_owned(
                   batch, bi, g->lines[(size_t)hit_line_idx].segment_i))
                  ? 1u
                  : 0u;
          const uint8_t projected_line_height_platform_state_current_owned =
              (projected_line_has_height_platform_transform &&
               stage_collision_floor_line_height_platform_state_is_current_owned(
                   batch, bi, projected_segment_i))
                  ? 1u
                  : 0u;
          const uint8_t damage_height_platform_live_bottom_crossing =
              ((hit_line_height_platform_state_trusted ||
                projected_line_height_platform_state_trusted) &&
               prev_bottom_y > (iy + k_floor_y_bias) && cur_bottom_y <= (iy + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t damage_height_platform_pending_owner =
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
              // data/characters/{fox,falco}.json::ledge_snap_height
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor}
              // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLineIntersectionH}
              (((hit_line_has_height_platform_transform &&
                 floor_line_x_near_endpoint_for_env(batch, bi, g, hit_line_idx, x,
                                                    damage_height_platform_edge_slack)) ||
                (projected_line_has_height_platform_transform && out_line_idx >= 0 &&
                 floor_line_x_near_endpoint_for_env(batch, bi, g, out_line_idx, x,
                                                    damage_height_platform_edge_slack))) &&
               !damage_height_platform_live_bottom_crossing &&
               is_damage_collision_landing_action(action_id) &&
               batch->state.speed_y_attack[idx] > 0.0f && batch->state.hitstun[idx] != 0u)
                  ? 1u
                  : 0u;
          const uint8_t damageair_height_platform_missing_current_owner =
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
              (((hit_line_has_height_platform_transform &&
                 !hit_line_height_platform_state_current_owned) ||
                (projected_line_has_height_platform_transform &&
                 !projected_line_height_platform_state_current_owned)) &&
               mpcoll_damageair_action(action_id) && batch->state.hitlag[idx] == 0u &&
               batch->state.hitstun[idx] != 0u)
                  ? 1u
                  : 0u;
          const uint8_t damage_terminal_height_platform_stale_floor =
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              ((hit_line_has_height_platform_transform ||
                projected_line_has_height_platform_transform) &&
               is_damage_fly_collision_action(action_id) && batch->state.hitstun[idx] <= 4u &&
               !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]) &&
               transformed_platform_bottom_penetration > (0.5f * k_ecb_vertical_unit))
                  ? 1u
                  : 0u;
          const uint8_t suppress_damage_transformed_platform_ecb_only_land =
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpCheckFloor}
              (damage_transformed_platform_offspan_contact ||
               damage_height_platform_pending_owner ||
               damageair_height_platform_missing_current_owner ||
               damage_terminal_height_platform_stale_floor)
                  ? 1u
                  : 0u;
          const uint8_t attackair_transformed_platform_existing_ecb_only_land =
              ((((attackair_transformed_platform_edge_owner_active &&
                  (attackair_transformed_platform_edge_contact ||
                   attackair_transformed_platform_root_edge_contact ||
                   projected_attackair_transformed_platform_edge_contact)) ||
                 attackair_transformed_platform_shallow_edge_continuation) &&
                first_phase_attackair_platform_ecb_owner && prev_action_id == action_id && y < iy &&
                !attackair_transformed_platform_floor_skip_released &&
                move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                         batch->state.anim_frame_f32[idx]) &&
                transformed_platform_bottom_penetration > k_floor_y_bias &&
                (attackair_transformed_platform_shallow_edge_continuation ||
                 transformed_platform_bottom_penetration <= k_ecb_vertical_unit)))
                  ? 1u
                  : 0u;
          const uint8_t suppress_attackair_transformed_platform_ecb_only_land =
              // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
              // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
              // refs/melee/src/melee/mp/mpcoll.c::{
              (attackair_height_platform_no_current_first_phase ||
               attackair_height_platform_no_current_single_create ||
               attackair_transformed_platform_existing_ecb_only_land)
                  ? 1u
                  : 0u;
          if (suppress_locked_ledge_land || suppress_stale_carried_cliff_ledge_land ||
              suppress_locked_off_end_platform_land || suppress_locked_seed6_platform_land ||
              suppress_locked_vertical_af3_land || suppress_escapeair_no_lock_vertical_af3_land ||
              suppress_seeded_escapeair_first_locked_land || suppress_fallspecial_entry_af3_land ||
              suppress_escapeair_locked_desired_bottom_above_floor_land ||
              suppress_kneebend_escapeair_missing_ledge_owner_entry_land ||
              suppress_fallspecial_platform_first_root_crossing ||
              suppress_fallspecial_same_floor_early_root_crossing ||
              suppress_escapeair_late_jump_entry_platform_lifetime ||
              suppress_escapeair_platform_root_snap_without_bottom_hit ||
              suppress_escapeair_jump_entry_platform_from_below ||
              suppress_jumpaerial_escapeair_entry_land ||
              suppress_escapeair_no_lock_jumpaerial_entry_sloped_ledge_land ||
              suppress_fall_ledge_floor_first_root_crossing ||
              fall_no_floor_shallow_fastfall_contact || escapeair_sustained_floorhug_airborne ||
              escapeair_fresh_horizontal_floorhug_airborne || specialhi_bound_entry_airborne ||
              suppress_damageflyroll_shallow_land || suppress_damageair_attackair_entry_land ||
              suppress_damageflyroll_hitlag_exit_floor_land ||
              suppress_locomotion_attackair_entry_platform_land ||
              suppress_attackair_jumpaerial_stage_object_support_land ||
              suppress_escapeair_entry_locked_platform_land ||
              suppress_downheld_transformed_platform_land ||
              suppress_damage_transformed_platform_ecb_only_land ||
              suppress_attackair_transformed_platform_ecb_only_land) {
            if (suppress_attackair_transformed_platform_root_below_land ||
                suppress_attackair_transformed_platform_ecb_only_land ||
                attackair_transformed_platform_downheld_inspan_pass) {
              const uint8_t force_attackair_source_skip =
                  suppress_attackair_transformed_platform_ecb_only_land ? 1u : 0u;
              publish_attackair_transformed_platform_floor_skip_from_sweep(
                  batch, idx, bi, g, stage_id, c, force_attackair_source_skip, hit_line_idx, x,
                  prev_y, y);
            } else if (suppress_downheld_transformed_platform_land && hit_line_idx >= 0 &&
                       hit_line_has_height_platform_transform &&
                       mpcoll_source_phases_has(coll_data.source_phases,
                                                MSL_MPCOLL_PHASE_PLATFORM_PASS) &&
                       batch->state.floor_skip_segment_id != NULL) {
              // refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms
              msl_mpcoll_update_floor_skip(batch, idx, g->lines[(size_t)hit_line_idx].segment_i);
            } else if (suppress_attackair_transformed_platform_floor_skip_first_crossing_land &&
                       batch->state.floor_skip_segment_id != NULL) {
              msl_mpcoll_clear_floor_skip(batch, idx);
            }
            batch->state.coll_env_flags[idx] |=
                floor_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id, anim,
                                             ecb_frame, was_grounded, loaded_ecb.current);
          } else {
            if (out_line_idx >= 0) {
              const uint16_t resolved_segment_i = g->lines[(size_t)out_line_idx].segment_i;
              const uint8_t resolved_line_has_platform_transform =
                  stage_collision_floor_line_has_platform_transform(stage_id, resolved_segment_i);
              const uint8_t resolved_line_is_ledge =
                  g->lines[(size_t)out_line_idx].is_ledge ? 1u : 0u;
              const uint8_t resolved_line_is_platform =
                  g->lines[(size_t)out_line_idx].is_platform ? 1u : 0u;
              const uint8_t resolved_ledge_x_in_bounds =
                  (resolved_line_is_ledge &&
                   floor_x_within_line_bounds(batch, bi, g, out_line_idx, batch->state.pos_x[idx]))
                      ? 1u
                      : 0u;
              const uint8_t resolved_floor_x_in_strict_segment =
                  floor_x_within_line_segment_strict(batch, bi, g, out_line_idx, cur_bottom_x);
              const uint8_t suppress_projected_escapeair_off_end_ledge_land =
                  // refs/melee/src/melee/mp/mplib.c::{
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 3 && resolved_line_is_ledge &&
                   !resolved_ledge_x_in_bounds)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_escapeair_off_end_platform_land =
                  // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 3 && resolved_line_is_platform &&
                   !resolved_floor_x_in_strict_segment)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_escapeair_transformed_platform_land =
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
                  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 4 && resolved_line_has_platform_transform &&
                   (!escapeair_sustained_floor_handoff ||
                    resolved_segment_i != batch->state.ground_id[idx]) &&
                   (ecb_lock_active || (prev_action_id == action_id &&
                                        resolved_segment_i != batch->state.ground_id[idx])) &&
                   batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_F &&
                   batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_B &&
                   batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
                   batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_B)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_specialhi_transformed_platform_land =
                  (resolved_line_has_platform_transform &&
                   !stage_height_platform_line_has_same_step_contact_source(batch, bi, stage_id,
                                                                            resolved_segment_i) &&
                   !stage_height_platform_line_has_live_scheduler_source(batch, bi, stage_id,
                                                                         resolved_segment_i) &&
                   (((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
                      (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) &&
                     ecb_lock_active) ||
                    ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
                      (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL) &&
                     resolved_segment_i != batch->state.ground_id[idx])))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_specialhi_understage_hard_floor_clip =
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
                  (specialhi_understage_floor_clip_action(batch->state.char_id[idx], action_id) &&
                   !stage_collision_floor_line_is_platform(stage_id, resolved_segment_i) &&
                   prev_y <
                       (proj_y - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                        k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_specialhi_from_below_hard_floor_clip =
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
                  (specialhi_floor_candidate_starts_below_source_floor(
                       batch->state.char_id[idx], action_id, prev_y, prev_bottom_y, cur_bottom_y,
                       proj_y + y_corr, batch->state.speed_y_self[idx]) &&
                   !stage_collision_floor_line_is_platform(stage_id, resolved_segment_i))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_specialairhi_floor_angle_land =
                  // data/characters/{fox,falco}.json::firefox_bound_angle_degrees
                  (specialairhi_floor_contact_angle_continues_launch(
                       action_id, char_id, floor_nx, floor_ny, batch->state.speed_air_x_self[idx],
                       batch->state.speed_y_self[idx]) &&
                   !stage_collision_floor_line_is_platform(stage_id, resolved_segment_i))
                      ? 1u
                      : 0u;
              const uint8_t resolved_line_has_height_platform_transform =
                  stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                           resolved_segment_i);
              const uint8_t projected_line_is_current_carried_cliff_floor =
                  (raw_current_floor_line_idx >= 0 && out_line_idx >= 0 &&
                   batch->state.cliff_ledge_floor_segment_id != NULL &&
                   resolved_segment_i == batch->state.cliff_ledge_floor_segment_id[idx] &&
                   g->lines[(size_t)raw_current_floor_line_idx].segment_i == resolved_segment_i)
                      ? 1u
                      : 0u;
              const float projected_contact_y = proj_y + y_corr;
              const float projected_transformed_platform_bottom_penetration =
                  projected_contact_y - cur_bottom_y;
              const uint8_t suppress_projected_attackair_transformed_platform_ecb_only_land =
                  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
                  // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
                  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  (((attackair_transformed_platform_edge_owner_active &&
                     projected_transformed_platform_bottom_penetration > k_floor_y_bias &&
                     projected_transformed_platform_bottom_penetration <= k_ecb_vertical_unit) ||
                    (projected_contact_y - prev_y > k_floor_y_bias &&
                     projected_contact_y - prev_y <= (2.0f * k_ecb_vertical_unit))) &&
                   resolved_line_has_height_platform_transform &&
                   shallow_attackair_platform_ecb_owner && prev_action_id == action_id &&
                   move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                            batch->state.anim_frame_f32[idx]) &&
                   y < projected_contact_y)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_attackair_transformed_platform_floor_skip_land =
                  (attackair_transformed_platform_floor_skip_active &&
                   floor_line_is_runtime_fighter_solid(g, stage_id, out_line_idx) &&
                   (!floor_x_within_line_segment_strict(batch, bi, g, out_line_idx, x) ||
                    floor_line_is_generated_stage_slope(batch, bi, g, out_line_idx)) &&
                   prev_y > (projected_contact_y + k_floor_y_bias) && y < projected_contact_y &&
                   batch->state.speed_y_self[idx] < 0.0f)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_attackair_offspan_hard_floor_edge_land =
                  (out_line_idx >= 0 &&
                   floor_line_is_runtime_fighter_solid(g, stage_id, out_line_idx) &&
                   !resolved_line_has_height_platform_transform &&
                   shallow_attackair_platform_ecb_owner && prev_action_id == action_id &&
                   move_tables_attackair_second_create_hitbox_phase(
                       char_id, action_id, batch->state.anim_frame_f32[idx]) &&
                   !floor_x_within_line_segment_strict(batch, bi, g, out_line_idx, x) &&
                   prev_y > (projected_contact_y + k_floor_y_bias) && y < projected_contact_y &&
                   batch->state.speed_y_self[idx] < 0.0f)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_attackair_transformed_platform_below_land =
                  // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  (resolved_line_has_height_platform_transform &&
                   late_attackair_platform_ecb_owner && prev_action_id == action_id &&
                   move_tables_attackair_last_create_hitbox_phase(
                       char_id, action_id, batch->state.anim_frame_f32[idx]) &&
                   prev_y < projected_contact_y && y < projected_contact_y &&
                   batch->state.speed_y_self[idx] <= k_floor_horiz_dy_thresh)
                      ? 1u
                      : 0u;
              const float escapeair_entry_bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
              const uint8_t suppress_projected_escapeair_platform_root_snap_without_bottom_hit =
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && resolved_line_is_platform &&
                   cur_bottom_y > (projected_contact_y + k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_fallspecial_first_sustained_land =
                  fallspecial_sloped_ledge_main_floor_first_sustained_airborne_owner(batch, idx, g,
                                                                                     stage_id);
              const uint8_t suppress_projected_escapeair_missing_bottom_owner_land =
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   stage_has_height_platform_transform && ecb_lock_timer_seed != 0u &&
                   !msl_escapeair_locked_bottom_owner_any(
                       batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
                   batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias &&
                   batch->state.floor_sweep_prev_pos_y[idx] < iy)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_jumpaerial_escapeair_shallow_ledge_land =
                  // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 3 && resolved_line_is_ledge &&
                   prefer_line_idx >= 0 && !g->lines[(size_t)prefer_line_idx].is_ledge &&
                   resolved_segment_i != seed_ground_id &&
                   (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                    batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
                   batch->state.seed_prev_action_frame[idx] <= 4 && y_corr >= 0.0f &&
                   y_corr < (escapeair_entry_bottom_rel0 - k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_escapeair_ledge_without_allow_interrupt =
                  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  // refs/melee/src/melee/ft/types.h::Fighter::allow_interrupt (fp+0x2218:0)
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed != 0u &&
                   prev_action_id == action_id && ecb_lock_timer_seed >= 2u &&
                   resolved_line_is_ledge && prefer_line_idx >= 0 &&
                   !g->lines[(size_t)prefer_line_idx].is_ledge &&
                   resolved_segment_i != seed_ground_id &&
                   ((batch->state.cliff_ledge_floor_segment_id != NULL &&
                     resolved_segment_i == batch->state.cliff_ledge_floor_segment_id[idx] &&
                     !cliff_ledge_floor_owner_active &&
                     !projected_line_is_current_carried_cliff_floor) ||
                    (stage_has_height_platform_transform &&
                     !floor_x_within_line_segment_strict(batch, bi, g, out_line_idx,
                                                         batch->state.pos_x[idx]))) &&
                   (batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                             (size_t)MSL_STATE_FLAGS_2218_INDEX] &
                    (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) == 0u)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_cliff_floor_without_live_owner =
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.cliff_ledge_floor_segment_id != NULL &&
                   resolved_segment_i == batch->state.cliff_ledge_floor_segment_id[idx] &&
                   !escapeair_live_cliff_ledge_source_floor_owner &&
                   !cliff_ledge_floor_owner_active &&
                   !projected_line_is_current_carried_cliff_floor)
                      ? 1u
                      : 0u;
              const uint8_t escapeair_low_floor_raw_hit_over_stale_platform_remap =
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
                  // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 4 && !hit_line_is_platform &&
                   resolved_line_has_platform_transform &&
                   stage_collision_floor_line_is_platform(stage_id, resolved_segment_i) &&
                   iy <= k_floor_y_bias && (proj_y + y_corr) > k_floor_y_bias)
                      ? 1u
                      : 0u;
              if (escapeair_low_floor_raw_hit_over_stale_platform_remap) {
                batch->state.pos_x[idx] += (ix - cur_bottom_x);
                batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
                on_ground = 1;
                ground_id = g->lines[(size_t)hit_line_idx].segment_i;
                contact_x = ix;
                contact_y = iy;
              } else if (suppress_projected_escapeair_transformed_platform_land ||
                         suppress_projected_escapeair_off_end_ledge_land ||
                         suppress_projected_escapeair_off_end_platform_land ||
                         suppress_projected_escapeair_platform_root_snap_without_bottom_hit ||
                         suppress_projected_fallspecial_first_sustained_land ||
                         suppress_projected_escapeair_missing_bottom_owner_land ||
                         suppress_projected_jumpaerial_escapeair_shallow_ledge_land ||
                         suppress_projected_escapeair_ledge_without_allow_interrupt ||
                         suppress_projected_cliff_floor_without_live_owner ||
                         suppress_projected_specialhi_transformed_platform_land ||
                         suppress_projected_specialhi_understage_hard_floor_clip ||
                         suppress_projected_specialhi_from_below_hard_floor_clip ||
                         suppress_projected_specialairhi_floor_angle_land ||
                         suppress_projected_attackair_transformed_platform_ecb_only_land ||
                         suppress_projected_attackair_transformed_platform_floor_skip_land ||
                         suppress_projected_attackair_offspan_hard_floor_edge_land ||
                         suppress_projected_attackair_transformed_platform_below_land ||
                         suppress_active_damage_hitlag_bottom_above_floor_land ||
                         damage_active_hitlag_root_below_bottom_above_floor_owner ||
                         damage_active_hitlag_downward_sdi_airborne_owner ||
                         suppress_damageflyroll_hitlag_exit_floor_land ||
                         suppress_damageflyroll_below_floor_active_hitlag_land) {
                if (suppress_projected_specialairhi_floor_angle_land) {
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  batch->state.pos_y[idx] += y_corr;
                  ground_id = resolved_segment_i;
                  contact_x = proj_x;
                  contact_y = proj_y + y_corr;
                  mpcoll_record_callback_floor_result_with_mode(
                      &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE,
                      (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAY_AIRBORNE_PROJECTION, ground_id, contact_x,
                      contact_y, floor_nx, floor_ny);
                }
                if (suppress_projected_attackair_transformed_platform_ecb_only_land ||
                    suppress_projected_attackair_transformed_platform_floor_skip_land) {
                  if (suppress_projected_attackair_transformed_platform_ecb_only_land) {
                    publish_attackair_transformed_platform_floor_skip_from_sweep(
                        batch, idx, bi, g, stage_id, c, 1u, out_line_idx, x, prev_y, y);
                  } else {
                    msl_mpcoll_clear_floor_skip(batch, idx);
                  }
                }
                batch->state.coll_env_flags[idx] |=
                    floor_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id,
                                                 anim, ecb_frame, was_grounded, loaded_ecb.current);
              } else {
                batch->state.pos_y[idx] += y_corr;
                if (suppress_active_damage_hitlag_land) {
                  ground_id = resolved_segment_i;
                  contact_x = proj_x;
                  contact_y = proj_y + y_corr;
                  mpcoll_record_callback_floor_result_with_mode(
                      &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE,
                      (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAY_AIRBORNE_PROJECTION, ground_id, contact_x,
                      contact_y, floor_nx, floor_ny);
                } else {
                  on_ground = 1;
                  floor_result_mode = raw_floor_sweep.mode;
                  ground_id = resolved_segment_i;
                  contact_x = ix;
                  contact_y = iy;
                }
              }
            } else {
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              if (!suppress_active_damage_hitlag_land &&
                  !suppress_damageflyroll_hitlag_exit_floor_land &&
                  !suppress_damageflyroll_below_floor_active_hitlag_land &&
                  msl_mpcoll_80044838_floor_edge_snap_from_bottom(
                      batch, bi, g, hit_line_idx, cur_bottom_x, cur_bottom_y, 0u, &ground_id,
                      &contact_x, &contact_y, &floor_nx, &floor_ny)) {
                batch->state.pos_x[idx] += (contact_x - cur_bottom_x);
                batch->state.pos_y[idx] += (contact_y - cur_bottom_y);
                on_ground = 1;
                floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_EDGE_SNAP;
              } else if (downdamage_active_hitlag_resting_floor_contact) {
                float root_y_corr = 0.0f;
                const int root_line_idx = msl_mplib_8004dd90_floor(
                    batch, bi, g, prefer_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
                    &root_y_corr, &floor_nx, &floor_ny);
                const float downward_bound =
                    fabsf(batch->state.speed_y_attack[idx]) + k_ecb_vertical_unit;
                if (root_line_idx >= 0 && fabsf(root_y_corr) <= downward_bound) {
                  batch->state.pos_y[idx] += root_y_corr;
                  on_ground = 1;
                  floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
                  ground_id = g->lines[(size_t)root_line_idx].segment_i;
                  contact_x = batch->state.pos_x[idx];
                  contact_y = batch->state.pos_y[idx];
                }
              }
              if (!on_ground) {
                batch->state.coll_env_flags[idx] |=
                    floor_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id,
                                                 anim, ecb_frame, was_grounded, loaded_ecb.current);
              }
            }
          }
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
        } else {
          if (prefer_line_idx >= 0 && !escapeair_sustained_floorhug_airborne &&
              !escapeair_fresh_horizontal_floorhug_airborne && !specialhi_bound_entry_airborne &&
              (batch->state.speed_y_self[idx] == 0.0f ||
               downdamage_active_hitlag_resting_floor_contact) &&
              batch->state.hitlag[idx] == 0 &&
              (batch->state.hitstun[idx] == 0 || downdamage_active_hitlag_resting_floor_contact)) {
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            float y_corr = 0.0f;
            const int out_line_idx =
                msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                         &y_corr, &floor_nx, &floor_ny);
            if (out_line_idx >= 0 &&
                fabsf(y_corr - k_floor_y_bias) <= (float)k_floor_horiz_dy_thresh) {
              batch->state.pos_y[idx] += y_corr;
              on_ground = 1;
              floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION;
              ground_id = g->lines[(size_t)out_line_idx].segment_i;
              contact_x = cur_bottom_x;
              contact_y = cur_bottom_y + y_corr;
            } else if (downdamage_active_hitlag_resting_floor_contact &&
                       batch->state.speed_y_attack[idx] < 0.0f) {
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044838_Floor}
              float root_y_corr = 0.0f;
              const int root_line_idx = msl_mplib_8004dd90_floor(
                  batch, bi, g, prefer_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
                  &root_y_corr, &floor_nx, &floor_ny);
              const float downward_bound =
                  fabsf(batch->state.speed_y_attack[idx]) + k_ecb_vertical_unit;
              if (root_line_idx >= 0 && fabsf(root_y_corr) <= downward_bound) {
                batch->state.pos_y[idx] += root_y_corr;
                on_ground = 1;
                floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
                ground_id = g->lines[(size_t)root_line_idx].segment_i;
                contact_x = batch->state.pos_x[idx];
                contact_y = batch->state.pos_y[idx];
              }
            }
          }
        }
      }

      if (!on_ground && action_id == (uint16_t)MSL_ACT_FALL && prefer_line_idx >= 0 &&
          (is_damage_fly_collision_action(prev_action_id) ||
           is_damage_fly_collision_action(batch->state.seed_prev_action_id[idx])) &&
          batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] == 0u &&
          batch->state.speed_y_self[idx] < 0.0f &&
          isfinite(batch->state.floor_sweep_prev_pos_y[idx]) &&
          !g->lines[(size_t)prefer_line_idx].is_platform &&
          !g->lines[(size_t)prefer_line_idx].is_ledge) {
        float floor_y = 0.0f;
        const uint8_t root_line_valid = floor_line_y_at_x_for_env(
            batch, bi, g, prefer_line_idx, batch->state.pos_x[idx], &floor_y);
        const float root_y_corr = (floor_y + k_floor_y_bias) - batch->state.pos_y[idx];
        const uint8_t already_below_source_floor =
            (root_line_valid &&
             batch->state.floor_sweep_prev_pos_y[idx] <= (floor_y + k_floor_y_bias) &&
             batch->state.pos_y[idx] <= (floor_y + k_floor_y_bias))
                ? 1u
                : 0u;
        const float previous_below_source_floor_depth =
            root_line_valid
                ? fmaxf(0.0f, (floor_y + k_floor_y_bias) - batch->state.floor_sweep_prev_pos_y[idx])
                : 0.0f;
        const float projection_bound =
            previous_below_source_floor_depth +
            fabsf(batch->state.pos_y[idx] - batch->state.floor_sweep_prev_pos_y[idx]) +
            fabsf(batch->state.speed_y_attack[idx]) + k_ecb_vertical_unit;
        if (already_below_source_floor && root_y_corr >= 0.0f && root_y_corr <= projection_bound) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044838_Floor}
          batch->state.pos_y[idx] += root_y_corr;
          on_ground = 1u;
          floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
          ground_id = g->lines[(size_t)prefer_line_idx].segment_i;
          contact_x = batch->state.pos_x[idx];
          contact_y = batch->state.pos_y[idx];
          floor_nx = 0.0f;
          floor_ny = 1.0f;
        }
      }

      if (!on_ground && was_grounded && prefer_line_idx < 0 &&
          carried_floor_line_is_live_yoshi_shyguy_support(batch, bi, g, stage_id,
                                                          raw_current_floor_line_idx)) {
        on_ground = 1u;
        floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAGE_OBJECT_CARRY;
        ground_id = seed_ground_id;
        contact_x = cur_bottom_x;
        contact_y = cur_bottom_y;
      }

      if (on_ground && was_grounded) {
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
        ordered_wall_ceil.hit_floor = 1u;
        ordered_wall_ceil.touching_floor = 1u;
        ordered_wall_ceil.squeeze_flags |= (uint8_t)MSL_MPCOLL_ORDERED_SQUEEZE_FLOOR;
        ordered_wall_ceil.y_after_floor = batch->state.pos_y[idx];
        MslEcbWorldPoints post_floor_ecb = {0};
        msl_ecb_world_points_sample(&post_floor_ecb, char_id, anim, ecb_frame_cur,
                                    facing_dir_for_ecb, batch->state.pos_x[idx],
                                    batch->state.pos_y[idx], lock_bottom_to_zero);
        (void)mpcoll_grounded_ceiling_ordered_retry(&mpcoll_ctx, &prev_ecb_points, &post_floor_ecb,
                                                    &ordered_wall_ceil);
        cur_ecb_points = ordered_wall_ceil.cur_ecb_after;
        msl_mpcoll_loaded_ecb_set_current(&loaded_ecb, &cur_ecb_points, loaded_ecb.current_mode);
        cur_bottom_x = cur_ecb_points.bottom_x;
        cur_bottom_y = cur_ecb_points.bottom_y;
      }
      if (on_ground && was_grounded &&
          msl_mpcoll_8004b108_capture_lw_flat_ledge_carry_owner(
              batch, idx, bi, g, stage_id, action_id, ground_id, seed_ground_id, prefer_line_idx)) {
        mpcoll_publish_direct_source_floor_hit(
            &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION, seed_ground_id,
            cur_bottom_x, g->lines[(size_t)prefer_line_idx].y0, 0.0f, 1.0f);
      }

      if (!on_ground && was_grounded && prefer_line_idx >= 0) {
        float substep_prev_x = 0.0f;
        float substep_prev_y = 0.0f;
        float substep_cur_x = 0.0f;
        float substep_cur_y = 0.0f;
        MslEcbWorldPoints substep_cur_ecb = {0};
        if (grounded_sideb_substep_floor_loss(batch, bi, g, char_id, action_id, prefer_line_idx,
                                              &prev_ecb_points, &cur_ecb_points, prev_x, prev_y, x,
                                              y, &substep_prev_x, &substep_prev_y, &substep_cur_x,
                                              &substep_cur_y, &substep_cur_ecb)) {
          batch->state.pos_x[idx] = substep_cur_x;
          batch->state.pos_y[idx] = substep_cur_y;
          cur_ecb_points = substep_cur_ecb;
          msl_mpcoll_loaded_ecb_set_current(&loaded_ecb, &cur_ecb_points, loaded_ecb.current_mode);
          cur_bottom_x = substep_cur_ecb.bottom_x;
          cur_bottom_y = substep_cur_ecb.bottom_y;
          mpcoll_clear_callback_floor_result(&mpcoll_ctx, substep_prev_x, substep_prev_y,
                                             substep_cur_x, substep_cur_y);
          callback_stopped_at_substep = 1u;
        }
      }

      if (was_grounded && prefer_line_idx >= 0) {
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004ACE4,mpColl_8004A908_Floor}
        if (floor_4a908_retry(batch, idx, bi, g, stage_id, prefer_line_idx, prev_bottom_x,
                              prev_bottom_y, prev_side_mid_y, cur_bottom_x, cur_bottom_y,
                              skip_platform_segment_i, &ground_id, &contact_x, &contact_y,
                              &floor_nx, &floor_ny)) {
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004ACE4,mpColl_8004A908_Floor}
          mpcoll_record_callback_floor_result(&mpcoll_ctx,
                                              (uint8_t)MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY,
                                              ground_id, contact_x, contact_y, floor_nx, floor_ny);
          on_ground = 1u;
        }
      }
      if (on_ground && was_grounded &&
          msl_mpcoll_8004b108_capture_lw_flat_ledge_carry_owner(
              batch, idx, bi, g, stage_id, action_id, ground_id, seed_ground_id, prefer_line_idx)) {
        mpcoll_publish_direct_source_floor_hit(
            &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION, seed_ground_id,
            cur_bottom_x, g->lines[(size_t)prefer_line_idx].y0, 0.0f, 1.0f);
      }

      int escapeair_projection_line_idx =
          (prefer_line_idx >= 0)
              ? prefer_line_idx
              : stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
      if (escapeair_projection_line_idx < 0 && batch->state.ground_id[idx] != 0xFFFFu &&
          (size_t)batch->state.ground_id[idx] < g->line_count &&
          g->lines[(size_t)batch->state.ground_id[idx]].segment_i == batch->state.ground_id[idx]) {
        escapeair_projection_line_idx = (int)batch->state.ground_id[idx];
      }
      float escapeair_projection_line_y = 0.0f;
      const uint8_t escapeair_projection_line_y_valid =
          (escapeair_projection_line_idx >= 0 &&
           floor_line_y_at_x_for_env(batch, bi, g, escapeair_projection_line_idx, x,
                                     &escapeair_projection_line_y))
              ? 1u
              : 0u;
      float escapeair_projection_line_nx = 0.0f;
      float escapeair_projection_line_ny = 1.0f;
      const uint8_t escapeair_projection_line_normal_valid =
          floor_line_normal_for_env(batch, bi, g, escapeair_projection_line_idx,
                                    &escapeair_projection_line_nx, &escapeair_projection_line_ny);
      uint8_t escapeair_live_nonplatform_root_floor_authority = 0u;
      const uint16_t escapeair_live_carried_floor_id = batch->state.ground_id[idx];
      const uint8_t escapeair_live_carried_same_hard_floor =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed > 1u &&
           escapeair_projection_line_idx >= 0 &&
           (size_t)escapeair_projection_line_idx < g->line_count &&
           msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
               batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
           escapeair_live_carried_floor_id != 0xFFFFu &&
           g->lines[(size_t)escapeair_projection_line_idx].segment_i ==
               escapeair_live_carried_floor_id &&
           !g->lines[(size_t)escapeair_projection_line_idx].is_ledge &&
           !stage_collision_floor_line_is_platform(stage_id, escapeair_live_carried_floor_id) &&
           !stage_collision_floor_line_has_platform_transform(stage_id,
                                                              escapeair_live_carried_floor_id) &&
           escapeair_projection_line_y_valid != 0u && escapeair_projection_line_normal_valid != 0u)
              ? 1u
              : 0u;
      const uint8_t escapeair_current_root_crosses_carried_floor =
          (escapeair_live_carried_same_hard_floor &&
           prev_y > (escapeair_projection_line_y + k_floor_y_bias) &&
           y <= (escapeair_projection_line_y + k_floor_y_bias))
              ? 1u
              : 0u;
      const uint8_t escapeair_next_root_reaches_carried_floor =
          (escapeair_live_carried_same_hard_floor &&
           y > (escapeair_projection_line_y + k_floor_y_bias) &&
           (y + batch->state.speed_y_self[idx]) <= (escapeair_projection_line_y + k_floor_y_bias))
              ? 1u
              : 0u;
      if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx) &&
          mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_471F8)) {
        MslMpcollFloorSweepResult escapeair_hard_floor_sweep = {0};
        mpcoll_floor_probe_begin(&mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_471F8,
                                 source_phases, prefer_line_idx,
                                 (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
        mpcoll_floor_probe_bottom_interval(&mpcoll_ctx, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                                           cur_bottom_y);
        // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_471F8
        // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform_transform metadata
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8,
        const uint8_t escapeair_hard_floor_sweep_hit =
            mpcoll_collect_bottom_sweep_hard_floor_result(
                batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                cur_bottom_y, prefer_line_idx, -1, 1u, &escapeair_hard_floor_sweep);
        const uint8_t escapeair_dd90_accept =
            (uint8_t)(escapeair_hard_floor_sweep_hit &&
                      escapeair_hard_floor_sweep.projected_line_idx >= 0 &&
                      escapeair_hard_floor_sweep.projected_y_corr >= 0.0f &&
                      batch->state.pos_y[idx] < escapeair_hard_floor_sweep.hit_y - k_floor_y_bias);
        if (escapeair_dd90_accept ||
            (escapeair_hard_floor_sweep_hit && escapeair_hard_floor_sweep.hit_line_idx >= 0 &&
             escapeair_hard_floor_sweep.hit_is_ledge)) {
          if (escapeair_dd90_accept) {
            batch->state.pos_y[idx] += escapeair_hard_floor_sweep.projected_y_corr;
            ground_id = escapeair_hard_floor_sweep.projected_segment_id;
          } else {
            batch->state.pos_x[idx] += (escapeair_hard_floor_sweep.hit_x - cur_bottom_x);
            batch->state.pos_y[idx] +=
                (escapeair_hard_floor_sweep.hit_y - cur_bottom_y) + k_floor_y_bias;
            ground_id = escapeair_hard_floor_sweep.hit_segment_id;
          }
          mpcoll_publish_direct_source_floor_hit(
              &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id,
              escapeair_hard_floor_sweep.hit_x, escapeair_hard_floor_sweep.hit_y,
              escapeair_hard_floor_sweep.normal_x, escapeair_hard_floor_sweep.normal_y);
          mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
          mpcoll_floor_probe_result(&mpcoll_ctx, &escapeair_hard_floor_sweep, 1u,
                                    escapeair_dd90_accept,
                                    (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
        } else {
          mpcoll_floor_probe_result(&mpcoll_ctx, &escapeair_hard_floor_sweep,
                                    escapeair_hard_floor_sweep_hit,
                                    (uint8_t)(escapeair_hard_floor_sweep.projected_line_idx >= 0),
                                    escapeair_hard_floor_sweep_hit
                                        ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION
                                        : (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
        }
      }
      if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
              batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
          batch->state.coll_desired_ecb_bottom_valid[idx] != 0u && ecb_lock_timer_seed > 1u &&
          batch->state.speed_y_self[idx] <= 0.0f) {
        MslMpcollFloorSweepResult escapeair_desired_hard_floor_sweep = {0};
        const float prev_desired_bottom_y =
            prev_y + batch->state.coll_desired_ecb_bottom_rel_y[idx];
        const float cur_desired_bottom_y = y + batch->state.coll_desired_ecb_bottom_rel_y[idx];
        mpcoll_floor_probe_begin(&mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_471F8,
                                 source_phases, prefer_line_idx,
                                 (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
        mpcoll_floor_probe_bottom_interval(&mpcoll_ctx, prev_x, prev_desired_bottom_y, x,
                                           cur_desired_bottom_y);
        const uint8_t escapeair_desired_hard_floor_sweep_hit =
            mpcoll_collect_bottom_sweep_hard_floor_result(
                batch, idx, bi, g, stage_id, prev_x, prev_desired_bottom_y, x, cur_desired_bottom_y,
                prefer_line_idx, -1, 0u, &escapeair_desired_hard_floor_sweep);
        if (escapeair_desired_hard_floor_sweep_hit &&
            escapeair_desired_hard_floor_sweep.projected_line_idx >= 0 &&
            escapeair_desired_hard_floor_sweep.projected_y_corr >= 0.0f &&
            batch->state.pos_y[idx] < escapeair_desired_hard_floor_sweep.hit_y - k_floor_y_bias) {
          // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_471F8
          // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform_transform metadata
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800471F8,
          batch->state.pos_y[idx] += escapeair_desired_hard_floor_sweep.projected_y_corr;
          escapeair_live_nonplatform_root_floor_authority = 1u;
          mpcoll_publish_direct_source_floor_hit(
              &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP,
              escapeair_desired_hard_floor_sweep.projected_segment_id,
              escapeair_desired_hard_floor_sweep.hit_x, escapeair_desired_hard_floor_sweep.hit_y,
              escapeair_desired_hard_floor_sweep.normal_x,
              escapeair_desired_hard_floor_sweep.normal_y);
          mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
          mpcoll_floor_probe_result(&mpcoll_ctx, &escapeair_desired_hard_floor_sweep, 1u, 1u,
                                    (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
        } else {
          mpcoll_floor_probe_result(
              &mpcoll_ctx, &escapeair_desired_hard_floor_sweep,
              escapeair_desired_hard_floor_sweep_hit,
              (uint8_t)(escapeair_desired_hard_floor_sweep.projected_line_idx >= 0),
              escapeair_desired_hard_floor_sweep_hit
                  ? (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_PROJECTION
                  : (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
        }
      }
      if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
              batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
          batch->state.coll_desired_ecb_bottom_valid[idx] != 0u && ecb_lock_timer_seed > 1u &&
          batch->state.speed_y_self[idx] <= 0.0f) {
        // EscapeAir_Coll routes through ft_80082C74 -> ft_80081D0C -> mpColl_800471F8.
        // CollData_X130_Locked keeps the pre-entry Jump/JumpAerial ECB bottom for the callback;
        // if that callback-local bottom reaches a platform/ledge floor, mpColl_80044628_Floor
        // publishes it before mpColl_80044838_Floor projects the root. Zero-bottom locked entries
        // use the callback root as the source bottom.
        // data/motion_state/owners/{fox,falco,sheik,zelda}.bin::MSLMSO01 phase AIR_471F8
        // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform/ledge floor metadata
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800471F8,
        //   mpColl_80044628_Floor,mpColl_80044838_Floor}
        const float locked_bottom_rel =
            (batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias)
                ? batch->state.coll_desired_ecb_bottom_rel_y[idx]
                : 0.0f;
        MslMpcollFloorSweepResult escapeair_locked_floor_sweep = {0};
        const uint8_t escapeair_locked_floor_sweep_hit = mpcoll_collect_bottom_sweep_floor_result(
            batch, idx, bi, g, stage_id, prev_x, prev_y + locked_bottom_rel, x,
            y + locked_bottom_rel, skip_platform_segment_i, -1, -1, c,
            &escapeair_locked_floor_sweep);
        const uint8_t locked_sweep_projected =
            (uint8_t)(escapeair_locked_floor_sweep_hit &&
                      escapeair_locked_floor_sweep.projected_line_idx >= 0 &&
                      escapeair_locked_floor_sweep.projected_y_corr >= 0.0f);
        if (locked_sweep_projected) {
          const int locked_line_idx = escapeair_locked_floor_sweep.projected_line_idx;
          const uint16_t locked_segment_i = escapeair_locked_floor_sweep.projected_segment_id;
          const uint8_t locked_floor_admitted =
              (locked_line_idx >= 0 && (size_t)locked_line_idx < g->line_count &&
               floor_line_is_runtime_fighter_solid(g, stage_id, locked_line_idx))
                  ? 1u
                  : 0u;
          if (locked_floor_admitted) {
            batch->state.pos_y[idx] += escapeair_locked_floor_sweep.projected_y_corr;
            if (!stage_collision_floor_line_is_platform(stage_id, locked_segment_i) &&
                !stage_collision_floor_line_has_platform_transform(stage_id, locked_segment_i)) {
              escapeair_live_nonplatform_root_floor_authority = 1u;
            }
            mpcoll_publish_direct_source_floor_hit(
                &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, locked_segment_i,
                escapeair_locked_floor_sweep.hit_x, escapeair_locked_floor_sweep.hit_y,
                escapeair_locked_floor_sweep.normal_x, escapeair_locked_floor_sweep.normal_y);
            mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
            mpcoll_floor_probe_result(&mpcoll_ctx, &escapeair_locked_floor_sweep, 1u, 1u,
                                      (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
          }
        }
      }
      if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          escapeair_floor_producer_authority_in != 0u &&
          escapeair_current_root_crosses_carried_floor &&
          !(batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
            batch->state.shine_jump_preserved_desired_bottom[idx] != 0u &&
            msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
                batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
            batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
            (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) >
                (escapeair_projection_line_y + k_floor_y_bias) &&
            (batch->state.coll_desired_ecb_bottom_locked_owner[idx] ==
                 (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_SOFT_OR_TRANSFORM ||
             ecb_lock_timer_seed > 4u))) {
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{
        batch->state.pos_y[idx] = escapeair_projection_line_y + k_floor_y_bias;
        escapeair_live_nonplatform_root_floor_authority = 1u;
        mpcoll_publish_direct_source_floor_hit(
            &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION,
            escapeair_live_carried_floor_id, x, escapeair_projection_line_y,
            escapeair_projection_line_nx, escapeair_projection_line_ny);
        mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
      }
      if (!on_ground && escapeair_floor_producer_authority_in != 0u &&
          escapeair_next_root_reaches_carried_floor) {
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
        mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
      }
      if (!on_ground && escapeair_floor_producer_authority_in != 0u &&
          escapeair_live_carried_same_hard_floor &&
          y > (escapeair_projection_line_y + k_floor_y_bias)) {
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_800471F8
        mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
      }
      const uint8_t escapeair_seeded_desired_bottom_nonplatform_crossing =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed > 1u &&
           escapeair_projection_line_idx >= 0 && escapeair_projection_line_y_valid != 0u &&
           batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
           msl_escapeair_locked_bottom_owner_is_seeded(
               batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
           (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
           !stage_collision_floor_line_is_platform(
               stage_id, g->lines[(size_t)escapeair_projection_line_idx].segment_i) &&
           (prev_y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) >
               (escapeair_projection_line_y + k_floor_y_bias) &&
           (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) <=
               (escapeair_projection_line_y + k_floor_y_bias))
              ? 1u
              : 0u;
      const uint8_t escapeair_late_jumpaerial_desired_bottom_nonplatform_crossing =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed > 1u &&
           escapeair_projection_line_idx >= 0 && escapeair_projection_line_y_valid != 0u &&
           batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
           (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
           (batch->state.seed_prev_action_frame[idx] == 3 ||
            (floor_line_is_generated_sloped_ledge(batch, bi, g, escapeair_projection_line_idx) &&
             batch->state.seed_prev_action_frame[idx] <= 4)) &&
           batch->state.action_frame[idx] <= 2 &&
           g->lines[(size_t)escapeair_projection_line_idx].segment_i ==
               batch->state.ground_id[idx] &&
           !stage_collision_floor_line_is_platform(
               stage_id, g->lines[(size_t)escapeair_projection_line_idx].segment_i) &&
           (prev_y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) >
               (escapeair_projection_line_y + k_floor_y_bias) &&
           (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) <=
               (escapeair_projection_line_y + k_floor_y_bias))
              ? 1u
              : 0u;
      if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          ((batch->state.action_frame[idx] >= 3 &&
            msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
                batch->state.coll_desired_ecb_bottom_locked_owner[idx])) ||
           escapeair_seeded_desired_bottom_nonplatform_crossing ||
           escapeair_late_jumpaerial_desired_bottom_nonplatform_crossing) &&
          ecb_lock_timer_seed > 1u && escapeair_projection_line_idx >= 0 &&
          batch->state.speed_y_self[idx] <= 0.0f) {
        // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
        float escapeair_y_corr = 0.0f;
        float escapeair_nx = 0.0f;
        float escapeair_ny = 1.0f;
        const uint8_t live_jumpaerial_root_floor_producer =
            (msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
                 batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
             escapeair_floor_producer_authority_in != 0u && ecb_lock_timer_seed > 1u &&
             escapeair_projection_line_idx >= 0 &&
             (size_t)escapeair_projection_line_idx < g->line_count &&
             g->lines[(size_t)escapeair_projection_line_idx].segment_i == seed_ground_id &&
             !g->lines[(size_t)escapeair_projection_line_idx].is_ledge &&
             !stage_collision_floor_line_is_platform(stage_id, seed_ground_id) &&
             !stage_collision_floor_line_has_platform_transform(stage_id, seed_ground_id) &&
             escapeair_projection_line_y_valid != 0u &&
             escapeair_current_root_crosses_carried_floor)
                ? 1u
                : 0u;
        const int escapeair_line_idx =
            msl_mplib_8004dd90_floor(batch, bi, g, escapeair_projection_line_idx, x, y,
                                     &escapeair_y_corr, &escapeair_nx, &escapeair_ny);
        if (escapeair_line_idx >= 0 && escapeair_y_corr > 0.0f &&
            !stage_collision_floor_line_is_platform(
                stage_id, g->lines[(size_t)escapeair_line_idx].segment_i) &&
            !(batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
              batch->state.shine_jump_preserved_desired_bottom[idx] != 0u &&
              msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
                  batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
              batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
              (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) > (escapeair_y_corr + y) &&
              (batch->state.coll_desired_ecb_bottom_locked_owner[idx] ==
                   (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_SOFT_OR_TRANSFORM ||
               ecb_lock_timer_seed > 4u))) {
          ground_id = g->lines[(size_t)escapeair_line_idx].segment_i;
          contact_x = x;
          contact_y = y + escapeair_y_corr - k_floor_y_bias;
          floor_nx = escapeair_nx;
          floor_ny = escapeair_ny;
          on_ground = 1u;
          if (live_jumpaerial_root_floor_producer && ground_id == seed_ground_id &&
              escapeair_line_idx == escapeair_projection_line_idx) {
            escapeair_live_nonplatform_root_floor_authority = 1u;
            mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
          }
        } else if (escapeair_projection_line_idx >= 0 &&
                   !stage_collision_floor_line_is_platform(
                       stage_id, g->lines[(size_t)escapeair_projection_line_idx].segment_i)) {
          if (escapeair_projection_line_y_valid != 0u &&
              escapeair_projection_line_normal_valid != 0u &&
              (y <= escapeair_projection_line_y + k_floor_y_bias ||
               (y > escapeair_projection_line_y + k_floor_y_bias &&
                (y + batch->state.speed_y_self[idx]) <=
                    escapeair_projection_line_y + k_floor_y_bias))) {
            ground_id = g->lines[(size_t)escapeair_projection_line_idx].segment_i;
            contact_x = x;
            contact_y = escapeair_projection_line_y;
            floor_nx = escapeair_projection_line_nx;
            floor_ny = escapeair_projection_line_ny;
            on_ground = 1u;
            if (live_jumpaerial_root_floor_producer) {
              escapeair_live_nonplatform_root_floor_authority = 1u;
              mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
            }
          }
        }
        if (!escapeair_live_nonplatform_root_floor_authority && on_ground &&
            msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
                batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
            ecb_lock_timer_seed > 1u && ground_id == seed_ground_id &&
            !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
            !stage_collision_floor_line_has_platform_transform(stage_id, ground_id) &&
            (escapeair_projection_line_idx < 0 ||
             (size_t)escapeair_projection_line_idx >= g->line_count ||
             !g->lines[(size_t)escapeair_projection_line_idx].is_ledge) &&
            escapeair_floor_producer_authority_in != 0u && prev_y > (contact_y + k_floor_y_bias) &&
            y <= (contact_y + k_floor_y_bias)) {
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
          escapeair_live_nonplatform_root_floor_authority = 1u;
          mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
        }
      }

      if (on_ground && action_uses_landing_floor_release_coll(action_id) &&
          ground_id != batch->state.ground_id[idx] &&
          stage_collision_floor_line_has_height_platform_transform(stage_id, ground_id) &&
          stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi,
                                                                             ground_id) &&
          !stage_height_platform_line_has_live_scheduler_source(batch, bi, stage_id, ground_id) &&
          !stage_height_platform_line_has_same_step_contact_source(batch, bi, stage_id,
                                                                   ground_id) &&
          !grounded_entry_same_step_height_platform_admits_line(batch, bi, idx, stage_id, action_id,
                                                                ground_id)) {
        const int current_line_idx =
            stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
        float current_line_y = 0.0f;
        if (current_line_idx >= 0 &&
            floor_line_y_at_x_for_env(batch, bi, g, current_line_idx, x, &current_line_y)) {
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC}
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          batch->state.pos_y[idx] = current_line_y + k_floor_y_bias;
          mpcoll_publish_direct_source_floor_hit(
              &floor_write, (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAGE_OBJECT_CARRY,
              batch->state.ground_id[idx], x, current_line_y, batch->state.ground_normal_x[idx],
              batch->state.ground_normal_y[idx]);
        }
      }
      const uint8_t callback_floor_valid = mpcoll_callback_floor_result_valid(&mpcoll_ctx);
      MslMpcollFloorPublication floor_publication = {
          .on_ground = on_ground,
          .result_mode =
              callback_floor_valid ? batch->state.coll_floor_result_mode[idx] : floor_result_mode,
          .airborne_ground_id = ground_id,
          .contact =
              {
                  .ground_id = ground_id,
                  .contact_x = contact_x,
                  .contact_y = contact_y,
                  .normal_x = floor_nx,
                  .normal_y = floor_ny,
              },
          .cur_bottom_x = cur_bottom_x,
          .cur_bottom_y = cur_bottom_y,
      };
      if (floor_publication.on_ground && was_grounded &&
          msl_mpcoll_8004b108_capture_lw_flat_ledge_carry_owner(
              batch, idx, bi, g, stage_id, action_id, floor_publication.contact.ground_id,
              seed_ground_id, prefer_line_idx)) {
        floor_publication.contact.ground_id = seed_ground_id;
        floor_publication.contact.contact_x = cur_bottom_x;
        floor_publication.contact.contact_y = g->lines[(size_t)prefer_line_idx].y0;
        floor_publication.contact.normal_x = 0.0f;
        floor_publication.contact.normal_y = 1.0f;
        mpcoll_record_callback_floor_result_with_mode(
            &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_publication.result_mode,
            floor_publication.contact.ground_id, floor_publication.contact.contact_x,
            floor_publication.contact.contact_y, floor_publication.contact.normal_x,
            floor_publication.contact.normal_y);
      }
      if (floor_publication.on_ground) {
        const MslMpcollFinalFloorLineState final_floor =
            mpcoll_final_floor_line_state(batch, bi, g, stage_id, ground_id, x);
        const int final_ground_line_idx = final_floor.line_idx;
        const uint8_t resolved_line_has_platform_transform = final_floor.has_platform_transform;
        const uint8_t resolved_line_has_height_platform_transform =
            final_floor.has_height_platform_transform;
        const int16_t final_attackair_first_create_frame =
            move_tables_attackair_first_create_hitbox_frame(char_id, action_id);
        const int16_t final_attackair_second_create_frame =
            move_tables_attackair_second_create_hitbox_frame(char_id, action_id);
        const uint8_t final_attackair_is_fair =
            (msl_motion_state_submotion_id(char_id, action_id) == (uint16_t)MSL_SM_ATTACK_AIR_F)
                ? 1u
                : 0u;
        const uint8_t final_attackair_single_create_script =
            (final_attackair_first_create_frame >= 0 && final_attackair_second_create_frame < 0)
                ? 1u
                : 0u;
        const uint8_t final_attackairlw_live_platform_publication_owner =
            mpcoll_attackairlw_air471f8_live_platform_publication_owner(
                batch, idx, &final_floor, action_id, final_attackair_first_create_frame,
                final_attackair_second_create_frame, skip_platform_segment_i);
        const uint8_t resolved_line_height_platform_same_step_contact =
            final_floor.height_same_step_contact;
        const uint8_t resolved_line_height_platform_current_source =
            final_floor.height_current_source;
        const uint8_t resolved_line_height_platform_live_scheduler_source =
            final_floor.height_live_scheduler_source;
        const uint8_t resolved_specialhi_height_platform_current_source =
            (resolved_line_height_platform_same_step_contact ||
             resolved_line_height_platform_live_scheduler_source)
                ? 1u
                : 0u;
        if (resolved_specialhi_height_platform_current_source &&
            (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL)) {
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          batch->state.pos_y[idx] += k_floor_y_bias;
        }
        const uint8_t suppress_escapeair_transformed_remap_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && batch->state.action_frame[idx] <= 4 &&
             contact_y > k_floor_y_bias && resolved_line_has_platform_transform &&
             !escapeair_locked_root_publication_platform_hit &&
             !escapeair_no_lock_static_platform_sweep_hit &&
             ground_id != batch->state.ground_id[idx] &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_F &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_B &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_B)
                ? 1u
                : 0u;
        const uint8_t suppress_specialairhi_platform_land =
            // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
            ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) &&
             stage_collision_floor_line_is_platform(stage_id, ground_id))
                ? 1u
                : 0u;
        const uint8_t suppress_sheik_vanish_start1_platform_pass_land =
            // data/characters/sheik.json::{sheik_vanish_travel_frames,
            // data/motion_state/owners/sheik.bin::MSLMSO01 FT_CHECK_GROUND_LEDGE_AIR_COLL
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
            (sheik_special_vanish_air_start1_platform_pass_active(batch, idx) &&
             stage_collision_floor_line_is_platform(stage_id, ground_id))
                ? 1u
                : 0u;
        const uint8_t sheik_vanish_start1_platform_pass_snaps_new_floor =
            (uint8_t)(suppress_sheik_vanish_start1_platform_pass_land &&
                      ground_id != batch->state.ground_id[idx]);
        const uint8_t suppress_specialairlw_start_stale_platform_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081D0C
            // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8}
            ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START) &&
             batch->state.action_frame[idx] <= 1 &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B) &&
             stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             contact_y > (cur_bottom_y + k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t suppress_specialhi_transformed_platform_land =
            (resolved_line_has_platform_transform &&
             (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL) &&
             !resolved_specialhi_height_platform_current_source &&
             ground_id != batch->state.ground_id[idx])
                ? 1u
                : 0u;
        const uint8_t suppress_specialhi_understage_hard_floor_land =
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
            (specialhi_understage_floor_clip_action(batch->state.char_id[idx], action_id) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             prev_y < (contact_y - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                       k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t suppress_specialhi_from_below_hard_floor_land =
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
            (specialhi_floor_candidate_starts_below_source_floor(
                 batch->state.char_id[idx], action_id, prev_y, prev_bottom_y, cur_bottom_y,
                 contact_y, batch->state.speed_y_self[idx]) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id))
                ? 1u
                : 0u;
        const uint8_t suppress_specialairhi_floor_angle_land =
            // data/characters/{fox,falco}.json::firefox_bound_angle_degrees
            (specialairhi_floor_contact_angle_continues_launch(
                 action_id, char_id, floor_nx, floor_ny, batch->state.speed_air_x_self[idx],
                 batch->state.speed_y_self[idx]) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id))
                ? 1u
                : 0u;
        const float jump_transformed_platform_line_y = final_floor.line_y;
        const uint8_t jump_transformed_platform_line_valid = final_floor.line_y_valid;
        const uint8_t transformed_platform_root_crossed_this_frame =
            (jump_transformed_platform_line_valid &&
             prev_y > (jump_transformed_platform_line_y + k_floor_y_bias) &&
             y <= (jump_transformed_platform_line_y + k_floor_y_bias))
                ? 1u
                : 0u;
        const float jump_transformed_platform_bottom_penetration =
            jump_transformed_platform_line_y - cur_bottom_y;
        const uint8_t final_attackair_transformed_platform_floor_skip_active =
            (skip_platform_segment_i != 0xFFFFu && shallow_attackair_platform_ecb_owner &&
             prev_action_id == action_id &&
             stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                      skip_platform_segment_i))
                ? 1u
                : 0u;
        const uint8_t final_attackair_transformed_platform_floor_skip_released =
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpClearFloorSkip}
            (skip_platform_segment_i != 0xFFFFu && prev_action_id == action_id &&
             stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                      skip_platform_segment_i) &&
             c != NULL &&
             stick_i8_to_unit(batch->state.input_main_y[idx]) >
                 c->platform_air_land_stick_y_threshold)
                ? 1u
                : 0u;
        const float final_attackair_transformed_platform_prev_below_depth =
            jump_transformed_platform_line_y - prev_y;
        const uint8_t final_attackair_non_dair_prev_downheld_pass =
            (c != NULL && action_id != (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
             stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                 c->platform_air_land_stick_y_threshold)
                ? 1u
                : 0u;
        const uint8_t final_attackair_transformed_platform_downheld_inspan_pass =
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
            (final_ground_line_idx >= 0 && final_floor.line != NULL &&
             resolved_line_has_height_platform_transform &&
             !resolved_line_height_platform_current_source &&
             !resolved_line_height_platform_live_scheduler_source &&
             !(action_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
               final_attackair_second_create_frame >= 0 &&
               batch->state.action_frame[idx] <=
                   (uint16_t)(final_attackair_second_create_frame + 1) &&
               (batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] != 0u ||
                batch->state.floor_sweep_prev_runtime_owned[idx] != 0u)) &&
             final_floor.line->segment_i != batch->state.ground_id[idx] &&
             floor_x_within_line_segment_strict(batch, bi, g, final_ground_line_idx, x) &&
             (platform_pass_input_below_raw_threshold(batch, idx, c) ||
              final_attackair_non_dair_prev_downheld_pass) &&
             c != NULL &&
             stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                 c->platform_air_land_stick_y_threshold)
                ? 1u
                : 0u;
        const uint8_t final_attackair_transformed_platform_shallow_first_contact =
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
            // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
            // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
            (resolved_line_has_height_platform_transform &&
             (final_attackair_transformed_platform_floor_skip_active ||
              final_attackair_transformed_platform_downheld_inspan_pass) &&
             first_phase_attackair_platform_ecb_owner && prev_action_id == action_id &&
             jump_transformed_platform_line_valid &&
             final_attackair_transformed_platform_prev_below_depth > k_floor_y_bias &&
             final_attackair_transformed_platform_prev_below_depth <=
                 (2.0f * k_ecb_vertical_unit) &&
             y < jump_transformed_platform_line_y &&
             move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                      batch->state.anim_frame_f32[idx]))
                ? 1u
                : 0u;
        const uint8_t final_attackair_late_cmd0_tail_no_current_source =
            // data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id (late N/Lw owner)
            // data/scripts/<char>.bin::MSLFTSC1 set_cmd_var/create_hitbox/clear_hitboxes events
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            (resolved_line_has_height_platform_transform &&
             !resolved_line_height_platform_current_source &&
             !resolved_line_height_platform_live_scheduler_source &&
             action_uses_late_attackair_platform_ecb_owner(char_id, action_id) &&
             prev_action_id == action_id &&
             move_tables_attackair_cmd0_active(char_id, action_id,
                                               batch->state.anim_frame_f32[idx]) &&
             !move_tables_attackair_hitbox_script_lifetime(char_id, action_id,
                                                           batch->state.anim_frame_f32[idx]) &&
             jump_transformed_platform_line_valid && y < jump_transformed_platform_line_y &&
             batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] == 0u &&
             batch->state.coll_floor_probe_projection_hit[idx] == 0u)
                ? 1u
                : 0u;
        const uint8_t final_attackair_height_platform_no_current_first_phase =
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
            // refs/melee/src/melee/mp/mpcoll.c::mpUpdateFloorSkip
            // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox events
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
            (resolved_line_has_height_platform_transform &&
             !resolved_line_height_platform_current_source &&
             !resolved_line_height_platform_live_scheduler_source &&
             is_attackair_action(action_id) && prev_action_id == action_id &&
             final_attackair_second_create_frame >= 0 &&
             !final_attackair_transformed_platform_floor_skip_released &&
             batch->state.action_frame[idx] <= (uint16_t)final_attackair_second_create_frame &&
             jump_transformed_platform_line_valid && y < jump_transformed_platform_line_y)
                ? 1u
                : 0u;
        const uint8_t final_attackairn_height_platform_no_current_script_owner =
            // data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id
            // data/scripts/<char>.bin::MSLFTSC1 set_cmd_var/create_hitbox/clear_hitboxes events
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            (floor_publication.on_ground && final_ground_line_idx >= 0 &&
             resolved_line_has_height_platform_transform &&
             !resolved_line_height_platform_same_step_contact &&
             !resolved_line_height_platform_live_scheduler_source &&
             msl_motion_state_submotion_id(char_id, action_id) == (uint16_t)MSL_SM_ATTACK_AIR_N &&
             prev_action_id == action_id &&
             !final_attackair_transformed_platform_floor_skip_released &&
             move_tables_attackair_cmd0_active(char_id, action_id,
                                               batch->state.anim_frame_f32[idx]) &&
             ((final_floor.line != NULL &&
               final_floor.line->segment_i != batch->state.ground_id[idx]) ||
              (action_uses_late_attackair_platform_ecb_owner(char_id, action_id) &&
               !move_tables_attackair_hitbox_script_lifetime(char_id, action_id,
                                                             batch->state.anim_frame_f32[idx]))) &&
             jump_transformed_platform_line_valid && y < jump_transformed_platform_line_y)
                ? 1u
                : 0u;
        const uint8_t final_attackairlw_height_platform_pre_first_create_owner =
            // data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id
            // data/scripts/<char>.bin::MSLFTSC1 AttackAirLw create_hitbox events
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
            // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            (floor_publication.on_ground && final_ground_line_idx >= 0 &&
             resolved_line_has_height_platform_transform &&
             msl_motion_state_submotion_id(char_id, action_id) == (uint16_t)MSL_SM_ATTACK_AIR_LW &&
             prev_action_id == action_id && final_attackair_first_create_frame >= 0 &&
             !final_attackair_transformed_platform_floor_skip_released &&
             batch->state.action_frame[idx] < (uint16_t)final_attackair_first_create_frame &&
             jump_transformed_platform_line_valid && y < jump_transformed_platform_line_y)
                ? 1u
                : 0u;
        const uint8_t final_attackair_transformed_platform_pass_owner_active =
            (final_attackair_transformed_platform_floor_skip_active ||
             final_attackair_transformed_platform_downheld_inspan_pass ||
             final_attackair_transformed_platform_shallow_first_contact ||
             final_attackair_height_platform_no_current_first_phase)
                ? 1u
                : 0u;
        const uint8_t suppress_airborne_transformed_platform_pre_handoff_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            // refs/melee/src/melee/mp/mpcoll.c::{
            (resolved_line_has_platform_transform &&
             (action_id == (uint16_t)MSL_ACT_JUMP_F || action_id == (uint16_t)MSL_ACT_JUMP_B) &&
             batch->state.speed_y_self[idx] > k_floor_horiz_dy_thresh)
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_transformed_platform_ecb_only_final_land =
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
            // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{
            (!final_attackairlw_live_platform_publication_owner &&
             (final_attackair_height_platform_no_current_first_phase ||
              final_attackairn_height_platform_no_current_script_owner ||
              final_attackairlw_height_platform_pre_first_create_owner ||
              final_attackair_late_cmd0_tail_no_current_source ||
              (final_attackair_transformed_platform_pass_owner_active &&
               resolved_line_has_height_platform_transform && prev_action_id == action_id &&
               first_phase_attackair_platform_ecb_owner &&
               !final_attackair_transformed_platform_floor_skip_released &&
               move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                        batch->state.anim_frame_f32[idx]) &&
               jump_transformed_platform_line_valid && y < jump_transformed_platform_line_y &&
               (final_attackair_transformed_platform_shallow_first_contact ||
                (jump_transformed_platform_bottom_penetration > k_floor_y_bias &&
                 jump_transformed_platform_bottom_penetration <= k_ecb_vertical_unit &&
                 final_attackair_transformed_platform_prev_below_depth <=
                     (2.0f * k_ecb_vertical_unit))))))
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_transformed_platform_floor_skip_final_land =
            (final_attackair_transformed_platform_floor_skip_active && final_ground_line_idx >= 0 &&
             floor_line_is_runtime_fighter_solid(g, stage_id, final_ground_line_idx) &&
             !final_attackair_transformed_platform_floor_skip_released &&
             resolved_line_has_height_platform_transform &&
             !floor_x_within_line_segment_strict(batch, bi, g, final_ground_line_idx, x) &&
             jump_transformed_platform_line_valid &&
             prev_y > (jump_transformed_platform_line_y + k_floor_y_bias) &&
             y < jump_transformed_platform_line_y && batch->state.speed_y_self[idx] < 0.0f)
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_offspan_hard_floor_edge_final_land =
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
            // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox events
            // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
            (final_ground_line_idx >= 0 &&
             floor_line_is_runtime_fighter_solid(g, stage_id, final_ground_line_idx) &&
             !resolved_line_has_height_platform_transform && shallow_attackair_platform_ecb_owner &&
             prev_action_id == action_id &&
             move_tables_attackair_second_create_hitbox_phase(char_id, action_id,
                                                              batch->state.anim_frame_f32[idx]) &&
             !floor_x_within_line_segment_strict(batch, bi, g, final_ground_line_idx, x) &&
             jump_transformed_platform_line_valid &&
             prev_y > (jump_transformed_platform_line_y + k_floor_y_bias) &&
             y < jump_transformed_platform_line_y && batch->state.speed_y_self[idx] < 0.0f)
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_hard_floor_root_projection_without_bottom_final_land =
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            (floor_publication.on_ground && final_ground_line_idx >= 0 &&
             floor_line_is_runtime_fighter_solid(g, stage_id, final_ground_line_idx) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !resolved_line_has_height_platform_transform && is_attackair_action(action_id) &&
             prev_action_id == action_id && prev_bottom_y > (contact_y + k_floor_y_bias) &&
             cur_bottom_y > (contact_y + k_floor_y_bias) && y < contact_y &&
             batch->state.speed_y_self[idx] < 0.0f)
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_single_create_no_bottom_owner_final_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            // data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id
            // data/moves/<char>.json moves["ftCo_SM_AttackAirF"].events create/clear_hitboxes
            (floor_publication.on_ground && final_ground_line_idx >= 0 &&
             resolved_line_has_height_platform_transform &&
             !resolved_line_height_platform_current_source &&
             !resolved_line_height_platform_live_scheduler_source &&
             is_attackair_action(action_id) && prev_action_id == action_id &&
             final_attackair_is_fair != 0u && final_attackair_single_create_script != 0u &&
             move_tables_attackair_cmd0_active(char_id, action_id,
                                               batch->state.anim_frame_f32[idx]) &&
             jump_transformed_platform_line_valid && y < jump_transformed_platform_line_y &&
             batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] == 0u &&
             batch->state.coll_floor_probe_projection_hit[idx] == 0u)
                ? 1u
                : 0u;
        const uint8_t final_attackair_transformed_platform_offspan_contact =
            (final_ground_line_idx >= 0 && resolved_line_has_height_platform_transform &&
             !floor_x_within_line_segment_strict(batch, bi, g, final_ground_line_idx, x))
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_transformed_platform_below_final_land =
            // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
            // refs/melee/src/melee/mp/mpcoll.c::{
            (resolved_line_has_height_platform_transform && shallow_attackair_platform_ecb_owner &&
             (final_attackair_transformed_platform_offspan_contact ||
              final_attackair_transformed_platform_floor_skip_active) &&
             !final_attackair_transformed_platform_floor_skip_released &&
             prev_action_id == action_id && jump_transformed_platform_line_valid &&
             move_tables_attackair_last_create_hitbox_phase(char_id, action_id,
                                                            batch->state.anim_frame_f32[idx]) &&
             prev_y < jump_transformed_platform_line_y && y < jump_transformed_platform_line_y &&
             batch->state.speed_y_self[idx] <= k_floor_horiz_dy_thresh)
                ? 1u
                : 0u;
        const uint8_t suppress_jumpaerial_transformed_platform_fastfall_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
            // refs/melee/src/melee/mp/mpcoll.c::{
            (resolved_line_has_height_platform_transform &&
             (action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             prev_action_id == action_id && batch->state.fall_fast[idx] != 0u &&
             !resolved_line_height_platform_same_step_contact &&
             !transformed_platform_root_crossed_this_frame &&
             stage_collision_floor_line_has_platform_transform(stage_id,
                                                               batch->state.ground_id[idx]) &&
             jumpaerial_terminal_fastfall_descent(batch, idx))
                ? 1u
                : 0u;
        const float jumpaerial_static_platform_pose_bottom_y =
            y + mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_bias_next, 0u);
        const uint8_t jumpaerial_static_platform_current_rejects =
            (c != NULL && stick_i8_to_unit(batch->state.input_main_y[idx]) <=
                              c->platform_air_land_stick_y_threshold)
                ? 1u
                : 0u;
        const uint8_t jumpaerial_static_platform_prior_rejected =
            (c != NULL && !jumpaerial_static_platform_current_rejects &&
             stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                 c->platform_air_land_stick_y_threshold &&
             prev_bottom_y > (contact_y + k_floor_y_bias) &&
             (y - batch->state.speed_y_self[idx]) +
                     mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame, 0u) <
                 (contact_y - k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t jumpaerial_static_platform_floor_skip_active =
            (batch->state.floor_skip_segment_id != NULL &&
             batch->state.floor_skip_segment_id[idx] == ground_id &&
             stage_collision_floor_line_is_platform(stage_id, ground_id)) ||
                    jumpaerial_static_platform_prior_rejected
                ? 1u
                : 0u;
        const uint8_t suppress_jumpaerial_static_platform_from_below_final_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            (c != NULL && stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !resolved_line_has_platform_transform &&
             (action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             prev_action_id == action_id &&
             (jumpaerial_static_platform_current_rejects ||
              jumpaerial_static_platform_floor_skip_active) &&
             jumpaerial_static_platform_pose_bottom_y < (contact_y - k_floor_y_bias) &&
             batch->state.speed_y_self[idx] < 0.0f)
                ? 1u
                : 0u;
        const uint8_t terminal_ground_jump_anim_entered_fall =
            (action_id == (uint16_t)MSL_ACT_FALL &&
             (prev_action_id == (uint16_t)MSL_ACT_JUMP_F ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_B ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B) &&
             batch->state.action_frame[idx] <= 1)
                ? 1u
                : 0u;
        const uint8_t final_fall_transformed_platform_offspan_contact =
            (final_ground_line_idx >= 0 && resolved_line_has_height_platform_transform &&
             !floor_x_within_line_segment_strict(batch, bi, g, final_ground_line_idx, x))
                ? 1u
                : 0u;
        const uint8_t suppress_fall_transformed_platform_fastfall_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80047E14,
            (resolved_line_has_height_platform_transform && action_id == (uint16_t)MSL_ACT_FALL &&
             (final_fall_transformed_platform_offspan_contact ||
              terminal_ground_jump_anim_entered_fall ||
              platform_pass_input_below_raw_threshold(batch, idx, c)) &&
             (prev_action_id == action_id || terminal_ground_jump_anim_entered_fall) &&
             batch->state.fall_fast[idx] != 0u &&
             !resolved_line_height_platform_same_step_contact &&
             !transformed_platform_root_crossed_this_frame &&
             !common_fall_flags6_root_floor_projection_hit)
                ? 1u
                : 0u;
        const float fall_loop_end_frame = (action_id == (uint16_t)MSL_ACT_FALL && anim <= 0xFFFFu)
                                              ? msl_anim_end_frame(char_id, (uint16_t)anim)
                                              : 0.0f;
        const uint8_t suppress_fall_loop_wrap_stage_object_floor_to_hard_floor_land =
            // data/stages/bin/*.bin::MSLSTG01 platform transform records
            // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBE8
            // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_FALL &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_FALL &&
             batch->state.seed_prev_action_frame[idx] >= 0 && fall_loop_end_frame > 0.0f &&
             msl_anim_is_looping(char_id, (uint16_t)anim) &&
             ((float)batch->state.seed_prev_action_frame[idx] + 1.0f) >= fall_loop_end_frame &&
             batch->state.action_frame[idx] <= 1 && batch->state.fall_fast[idx] != 0u &&
             batch->state.speed_y_self[idx] < 0.0f &&
             stage_collision_floor_line_has_platform_transform(stage_id,
                                                               batch->state.ground_id[idx]) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !(final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
               g->lines[(size_t)final_ground_line_idx].is_ledge) &&
             ground_id != batch->state.ground_id[idx] && prev_y > (contact_y + k_floor_y_bias) &&
             (batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                       (size_t)MSL_STATE_FLAGS_2218_INDEX] &
              (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) == 0u)
                ? 1u
                : 0u;
        const uint8_t suppress_fall_stale_platform_first_hard_floor_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
            // refs/melee/src/melee/mp/mpcoll.c::{
            // data/stages/bin/*.bin::MSLSTG01 floor flags
            (action_id == (uint16_t)MSL_ACT_FALL &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_FALL &&
             batch->state.fall_fast[idx] == 0u && ecb_lock_timer_seed == 0u &&
             batch->state.ground_id[idx] != 0xFFFFu &&
             stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]) &&
             (batch->state.speed_air_x_self[idx] * batch->state.facing_dir1[idx]) < -0.5f &&
             final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
             !g->lines[(size_t)final_ground_line_idx].is_platform &&
             !g->lines[(size_t)final_ground_line_idx].is_ledge &&
             !stage_collision_floor_line_has_platform_transform(stage_id, ground_id) &&
             ground_id != batch->state.ground_id[idx] && batch->state.speed_y_self[idx] < 0.0f &&
             prev_bottom_y > (contact_y + k_floor_y_bias) &&
             cur_bottom_y < (contact_y - k_floor_y_bias) &&
             (contact_y - cur_bottom_y) < k_ecb_vertical_unit)
                ? 1u
                : 0u;
        const uint8_t suppress_fall_shallow_terminal_hard_floor_land =
            // data/stages/bin/*.bin::MSLSTG01 floor flags/links/platform-transform metadata
            // refs/melee/src/melee/ft/types.h::Fighter::x2218
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_FALL &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_FALL &&
             batch->state.seed_prev_action_frame[idx] == 0 && batch->state.fall_fast[idx] == 0u &&
             batch->state.speed_y_self[idx] < 0.0f &&
             ((batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                        (size_t)MSL_STATE_FLAGS_2218_INDEX] &
               (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1)) ==
              (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1)) &&
             final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
             floor_line_is_terminal_cardinal_hard_floor(batch, bi, g, stage_id,
                                                        final_ground_line_idx) &&
             ground_id == seed_ground_id && ground_id == batch->state.ground_id[idx] &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             y < (contact_y - k_floor_y_bias) && cur_bottom_y > (contact_y - k_ecb_vertical_unit))
                ? 1u
                : 0u;
        const uint8_t suppress_fall_static_platform_from_below_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
            // refs/melee/src/melee/mp/mpcoll.c::{
            // data/stages/bin/*.bin::MSLSTG01 floor flags
            (action_id == (uint16_t)MSL_ACT_FALL &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_FALL &&
             final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
             batch->state.ground_id[idx] == g->lines[(size_t)final_ground_line_idx].segment_i &&
             g->lines[(size_t)final_ground_line_idx].is_platform &&
             !resolved_line_has_platform_transform && batch->state.fall_fast[idx] == 0u &&
             batch->state.speed_y_self[idx] < 0.0f && prev_y < (contact_y - k_floor_y_bias) &&
             y < (contact_y - k_floor_y_bias) &&
             !(prev_bottom_y > (contact_y + k_floor_y_bias) &&
               cur_bottom_y < (contact_y - k_floor_y_bias) &&
               (common_fall_blended_ecb_consumer != 0u ||
                batch->state.common_fall_blend_x4[idx] == 0.0f)))
                ? 1u
                : 0u;
        const uint8_t suppress_fall_attackair_entry_transformed_platform_root_only_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms
            (action_id == (uint16_t)MSL_ACT_FALL && prev_action_id == (uint16_t)MSL_ACT_FALL &&
             batch->state.action_frame[idx] <= 1 &&
             is_attackair_action(batch->state.seed_prev_action_id[idx]) &&
             stage_collision_floor_line_has_height_platform_transform(stage_id, ground_id) &&
             !stage_height_platform_line_has_current_source(batch, bi, stage_id, ground_id) &&
             !stage_height_platform_line_has_live_scheduler_source(batch, bi, stage_id,
                                                                   ground_id) &&
             batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] == 0u &&
             batch->state.coll_floor_probe_projection_hit[idx] == 0u &&
             floor_publication.result_mode == (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP &&
             batch->state.speed_y_self[idx] < 0.0f)
                ? 1u
                : 0u;
        const uint8_t suppress_missfoot_ecb_lock_first_floor_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800473CC}
            (action_id == (uint16_t)MSL_ACT_MISS_FOOT && batch->state.action_frame[idx] <= 1 &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_MISS_FOOT &&
             ecb_lock_timer_seed >= 9u &&
             mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_473CC) &&
             floor_publication.result_mode == (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             ground_id == batch->state.ground_id[idx] &&
             cur_bottom_y < (contact_y + k_floor_y_bias) &&
             (contact_y - cur_bottom_y) <= k_ecb_vertical_unit)
                ? 1u
                : 0u;
        const float final_landing_lift = contact_y - cur_bottom_y;
        const uint8_t suppress_damage_sustained_platform_without_bottom_sweep =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor,
            (floor_publication.on_ground && was_grounded == 0u &&
             (is_common_damage_ground_pose_ecb_action(action_id) ||
              (is_common_damage_ground_pose_ecb_action(prev_action_id) &&
               batch->state.seed_prev_action_id[idx] == prev_action_id)) &&
             batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] == 0u &&
             ((floor_publication.result_mode == (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP &&
               batch->state.frame_start_hitstun[idx] == 1u) ||
              floor_publication.result_mode == (uint8_t)MSL_MPCOLL_FLOOR_MODE_EDGE_SNAP ||
              floor_publication.result_mode == (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION) &&
             (stage_collision_floor_line_is_platform(stage_id, ground_id) ||
              (final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
               g->lines[(size_t)final_ground_line_idx].is_platform) ||
              (ground_id < g->line_count && g->lines[(size_t)ground_id].is_platform) ||
              mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_PLATFORM_PASS)) &&
             batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] == 0u)
                ? 1u
                : 0u;
        const float escapeair_entry_bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
        const uint8_t suppress_fallspecial_first_sustained_current_ecb_land =
            (fallspecial_sloped_ledge_main_floor_first_sustained_airborne_owner(batch, idx, g,
                                                                                stage_id) &&
             final_landing_lift >= 0.0f)
                ? 1u
                : 0u;
        const uint8_t fallspecial_final_entered_from_specialhi_end =
            (is_common_fallspecial_action(action_id) &&
             is_spacie_specialhi_end_fallspecial_source(batch->state.char_id[idx],
                                                        batch->state.seed_prev_action_id[idx]))
                ? 1u
                : 0u;
        const uint8_t suppress_fallspecial_platform_final_without_source_bottom =
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            (is_common_fallspecial_action(action_id) &&
             stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             (platform_pass_input_below_raw_threshold(batch, idx, c) ||
              (!fallspecial_final_entered_from_specialhi_end &&
               cur_bottom_y > (contact_y + k_floor_y_bias))))
                ? 1u
                : 0u;
        const uint8_t suppress_fall_same_floor_early_final_land =
            msl_mpcoll_80047e14_reject_fall_same_floor_early_final_land(
                batch, idx, bi, g, stage_id, action_id, seed_ground_id, ground_id,
                final_ground_line_idx, contact_y, y);
        uint8_t locked_desired_bottom_final_sweep_hit = 1u;
        if (escapeair_episode.sustained && ecb_lock_timer_seed != 0u &&
            batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
            msl_escapeair_locked_bottom_owner_any(
                batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
            final_ground_line_idx >= 0 &&
            stage_collision_floor_line_is_platform(stage_id, ground_id)) {
          locked_desired_bottom_final_sweep_hit = mpcoll_bottom_sweep_hits_segment(
              batch, idx, bi, g, stage_id, prev_x,
              prev_y + batch->state.coll_desired_ecb_bottom_rel_y[idx], x,
              y + batch->state.coll_desired_ecb_bottom_rel_y[idx], skip_platform_segment_i,
              final_ground_line_idx, -1, c, ground_id);
        }
        const uint8_t final_ground_line_is_ledge =
            (final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
             g->lines[(size_t)final_ground_line_idx].is_ledge)
                ? 1u
                : 0u;
        const uint8_t final_ledge_x_in_bounds =
            (final_ground_line_is_ledge &&
             floor_x_within_line_bounds(batch, bi, g, final_ground_line_idx, cur_bottom_x))
                ? 1u
                : 0u;
        const uint8_t final_ground_line_is_sloped_ledge =
            floor_line_is_generated_sloped_ledge(batch, bi, g, final_ground_line_idx);
        const uint8_t carried_cliff_ledge_final_floor =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.cliff_ledge_floor_segment_id != NULL &&
             ground_id == batch->state.cliff_ledge_floor_segment_id[idx] &&
             final_ground_line_is_ledge)
                ? 1u
                : 0u;
        const uint8_t carried_cliff_ledge_callback_result =
            (carried_cliff_ledge_final_floor && callback_floor_valid &&
             batch->state.coll_floor_result_segment_id[idx] == ground_id)
                ? 1u
                : 0u;
        const uint8_t carried_cliff_ledge_callback_floor_authority =
            carried_cliff_ledge_callback_result ? 1u : 0u;
        const MslMpcollCarriedCliffLedgeFloorAuthority carried_cliff_ledge_authority =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
            carried_cliff_ledge_final_floor
                ? mpcoll_carried_cliff_ledge_floor_authority(
                      batch, idx, bi, g, char_id, action_id, final_ground_line_idx,
                      raw_current_floor_line_idx, batch->state.pos_x[idx], y, contact_y,
                      ecb_lock_timer_seed,
                      (uint8_t)(escapeair_live_cliff_ledge_source_floor_owner ||
                                carried_cliff_ledge_callback_floor_authority))
                : (MslMpcollCarriedCliffLedgeFloorAuthority){0};
        const uint8_t carried_cliff_ledge_floor_source_authority =
            carried_cliff_ledge_authority.source_authority;
        const uint8_t carried_cliff_ledge_floor_restored_only =
            carried_cliff_ledge_authority.restored_only;
        const uint8_t carried_cliff_ledge_restored_entry_above_floor =
            (carried_cliff_ledge_floor_restored_only && ecb_lock_timer_seed != 0u &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             batch->state.floor_sweep_prev_pos_y[idx] >
                 (contact_y - ((2.0f * escapeair_entry_bottom_rel0) - (10.0f * k_floor_y_bias))))
                ? 1u
                : 0u;
        const int seed_ground_line_idx_for_final =
            stage_collision_floor_line_index(stage_id, seed_ground_id);
        const uint8_t seed_ground_line_is_ledge =
            (seed_ground_line_idx_for_final >= 0 &&
             (size_t)seed_ground_line_idx_for_final < g->line_count &&
             g->lines[(size_t)seed_ground_line_idx_for_final].is_ledge)
                ? 1u
                : 0u;
        const uint8_t suppress_jumpaerial_escapeair_high_lift_ledge_final_land =
            // separate high-lift entry suppression, not the carried-cliff publication owner
            // generated sloped carried-floor handoffs have their own prefix proof
            // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && batch->state.action_frame[idx] <= 3 &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             batch->state.seed_prev_action_frame[idx] <= 4 && final_ground_line_is_ledge &&
             !final_ground_line_is_sloped_ledge &&
             final_landing_lift > (escapeair_entry_bottom_rel0 + (2.0f * k_ecb_vertical_unit)))
                ? 1u
                : 0u;
        const uint8_t suppress_jumpaerial_escapeair_first_locked_static_platform_land =
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             batch->state.seed_prev_action_frame[idx] == 0 && batch->state.action_frame[idx] <= 1 &&
             ecb_lock_timer_seed >= 8u &&
             fabsf(batch->state.floor_sweep_prev_pos_x[idx] - x) <= k_floor_horiz_dy_thresh &&
             msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
                 batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
             stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !stage_collision_floor_line_has_platform_transform(stage_id, ground_id) &&
             batch->state.speed_y_self[idx] < 0.0f && final_landing_lift >= 0.0f)
                ? 1u
                : 0u;
        const uint8_t suppress_jumpaerial_escapeair_static_platform_overstep_final_land =
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.action_frame[idx] <= 2 && batch->state.ecb_lock_timer[idx] == 0 &&
             !msl_escapeair_locked_bottom_owner_any(
                 batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !stage_collision_floor_line_has_platform_transform(stage_id, ground_id) &&
             batch->state.speed_y_self[idx] < 0.0f &&
             final_landing_lift > (fabsf(batch->state.speed_y_self[idx]) + k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t suppress_sustained_escapeair_same_platform_lock_land =
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754,mpColl_80044628_Floor}
            (escapeair_episode.sustained && ecb_lock_timer_seed != 0u &&
             batch->state.jumps_left[idx] == 0u && ground_id == seed_ground_id &&
             stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !locked_desired_bottom_final_sweep_hit &&
             !stage_collision_floor_line_has_platform_transform(stage_id, ground_id) &&
             final_landing_lift < escapeair_entry_bottom_rel0 &&
             (batch->state.ledge_drop_floor_skip_segment_id == NULL ||
              batch->state.ledge_drop_floor_skip_segment_id[idx] != ground_id) &&
             c != NULL &&
             stick_i8_to_unit(batch->state.input_main_y[idx]) <=
                 c->platform_air_land_stick_y_threshold &&
             stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                 c->platform_air_land_stick_y_threshold)
                ? 1u
                : 0u;
        const uint8_t suppress_sustained_escapeair_same_ledge_lock_land =
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{
            (escapeair_episode.sustained && ecb_lock_timer_seed != 0u &&
             ground_id == seed_ground_id && seed_ground_line_is_ledge &&
             final_ground_line_is_ledge &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !escapeair_live_cliff_ledge_source_floor_owner &&
             !locked_desired_bottom_final_sweep_hit &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
             batch->state.floor_sweep_prev_pos_y[idx] < contact_y && final_landing_lift >= 0.0f &&
             final_landing_lift < escapeair_entry_bottom_rel0)
                ? 1u
                : 0u;
        const uint8_t suppress_sustained_escapeair_adjacent_ledge_without_allow_interrupt =
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            // refs/melee/src/melee/ft/types.h::Fighter::allow_interrupt (fp+0x2218:0)
            (stage_has_height_platform_transform && escapeair_episode.sustained &&
             ecb_lock_timer_seed != 0u && final_ground_line_is_ledge &&
             !seed_ground_line_is_ledge && ecb_lock_timer_seed >= 2u &&
             !carried_cliff_ledge_floor_source_authority &&
             (!floor_x_within_line_segment_strict(batch, bi, g, final_ground_line_idx,
                                                  batch->state.pos_x[idx]) ||
              carried_cliff_ledge_final_floor) &&
             (batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                       (size_t)MSL_STATE_FLAGS_2218_INDEX] &
              (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) == 0u)
                ? 1u
                : 0u;
        const uint8_t fresh_jumpaerial_zero_bottom_platform_projection_hit =
            (escapeair_locked_root_publication_platform_hit &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             batch->state.seed_prev_action_frame[idx] <= 1 &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias)
                ? 1u
                : 0u;
        const uint8_t suppress_fresh_jumpaerial_downheld_nonplatform_stale_land =
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            (c != NULL && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed != 0u &&
             !msl_escapeair_locked_bottom_owner_any(
                 batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
             batch->state.action_frame[idx] <= 3 &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             batch->state.seed_prev_action_frame[idx] <= 1 &&
             stick_i8_to_unit(batch->state.input_main_y[idx]) <=
                 c->platform_air_land_stick_y_threshold &&
             stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                 c->platform_air_land_stick_y_threshold &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !resolved_line_has_platform_transform &&
             !escapeair_live_nonplatform_root_floor_authority &&
             escapeair_floor_producer_authority_in == 0u && final_landing_lift >= 0.0f &&
             batch->state.coll_substep_prev_pos_y[idx] < contact_y &&
             batch->state.coll_substep_cur_pos_y[idx] < contact_y)
                ? 1u
                : 0u;
        const uint8_t suppress_locked_escapeair_missing_bottom_owner_land =
            // refs/melee/src/melee/mp/mpcoll.c::{
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed != 0u &&
             !msl_escapeair_locked_bottom_owner_any(
                 batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias &&
             !fresh_jumpaerial_zero_bottom_platform_projection_hit &&
             !escapeair_fresh_jump_height_platform_handoff_hit &&
             !escapeair_stale_platform_root_handoff_hit &&
             !escapeair_missing_bottom_hard_floor_sweep_owner && final_landing_lift >= 0.0f &&
             batch->state.floor_sweep_prev_pos_y[idx] < contact_y &&
             !((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
               batch->state.seed_prev_action_frame[idx] <= 4 && final_ground_line_idx >= 0 &&
               !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
               !resolved_line_has_platform_transform && !final_ground_line_is_ledge))
                ? 1u
                : 0u;
        const uint8_t suppress_locked_desired_platform_without_bottom_sweep =
            // refs/melee/src/melee/mp/mpcoll.c::{
            (escapeair_episode.sustained && ecb_lock_timer_seed != 0u &&
             ground_id == seed_ground_id && !locked_desired_bottom_final_sweep_hit)
                ? 1u
                : 0u;
        const uint8_t runtime_live_jumpaerial_nonplatform_root_crossing =
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor,mpColl_800471F8}
            (msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
                 batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
             ecb_lock_timer_seed > 1u &&
             (!final_ground_line_is_ledge || ecb_lock_timer_seed >= 3u) &&
             prev_y > (contact_y + k_floor_y_bias) && y <= (contact_y + k_floor_y_bias) &&
             batch->state.action_frame[idx] >= 4)
                ? 1u
                : 0u;
        const uint8_t live_jumpaerial_owner_nonplatform_without_bottom_sweep =
            // refs/melee/src/melee/mp/mpcoll.c::{
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed != 0u &&
             batch->state.shine_jump_preserved_desired_bottom[idx] != 0u &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !resolved_line_has_platform_transform && !escapeair_early_ledge_root_floor_owner &&
             !locked_desired_bottom_final_sweep_hit &&
             batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
             msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
                 batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
             (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) > (contact_y + k_floor_y_bias) &&
             (batch->state.coll_desired_ecb_bottom_locked_owner[idx] ==
                  (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_SOFT_OR_TRANSFORM ||
              ecb_lock_timer_seed > 4u))
                ? 1u
                : 0u;
        const uint8_t suppress_locked_desired_nonplatform_without_bottom_sweep =
            // refs/melee/src/melee/mp/mpcoll.c::{
            (live_jumpaerial_owner_nonplatform_without_bottom_sweep ||
             (escapeair_episode.sustained && ecb_lock_timer_seed != 0u &&
              !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
              !resolved_line_has_platform_transform && !escapeair_early_ledge_root_floor_owner &&
              !runtime_live_jumpaerial_nonplatform_root_crossing &&
              !escapeair_live_nonplatform_root_floor_authority &&
              escapeair_floor_producer_authority_in == 0u &&
              batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
              msl_escapeair_locked_bottom_owner_any(
                  batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
              batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
              (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) > (contact_y + k_floor_y_bias)))
                ? 1u
                : 0u;
        const float jumpaerial_f_frame2_bottom_rel_y =
            msl_ecb_bottom_rel_y(char_id, (uint32_t)MSL_SM_JUMP_AERIAL_F, 2);
        const float jumpaerial_b_frame2_bottom_rel_y =
            msl_ecb_bottom_rel_y(char_id, (uint32_t)MSL_SM_JUMP_AERIAL_B, 2);
        const uint8_t carried_desired_bottom_is_jumpaerial_frame2 =
            (fabsf(batch->state.coll_desired_ecb_bottom_rel_y[idx] -
                   jumpaerial_f_frame2_bottom_rel_y) <= k_floor_y_bias ||
             fabsf(batch->state.coll_desired_ecb_bottom_rel_y[idx] -
                   jumpaerial_b_frame2_bottom_rel_y) <= k_floor_y_bias)
                ? 1u
                : 0u;
        const uint8_t suppress_escapeair_jumpaerial_soft_owner_early_direct_land =
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.coll_desired_ecb_bottom_locked_owner[idx] ==
                 (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_SOFT_OR_TRANSFORM &&
             carried_desired_bottom_is_jumpaerial_frame2 != 0u &&
             escapeair_floor_producer_authority_in == 0u &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !resolved_line_has_platform_transform &&
             batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias)
                ? 1u
                : 0u;
        const uint8_t suppress_kneebend_escapeair_missing_ledge_owner_final_land =
            // data/stages/bin/*.bin::MSLSTG01 is_ledge + fighter_solid floor metadata
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
             batch->state.seed_prev_action_frame[idx] >= 4 && ecb_lock_timer_seed != 0u &&
             !was_grounded && cliff_ledge_floor_owner_active && final_ground_line_is_ledge &&
             batch->state.cliff_ledge_floor_segment_id != NULL &&
             ground_id == batch->state.cliff_ledge_floor_segment_id[idx] &&
             !carried_cliff_ledge_floor_source_authority && !locked_desired_bottom_final_sweep_hit)
                ? 1u
                : 0u;
        const uint8_t suppress_kneebend_escapeair_static_platform_lock_land =
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed != 0u &&
             ground_id == seed_ground_id &&
             stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !stage_collision_floor_line_has_platform_transform(stage_id, ground_id) &&
             final_landing_lift <= k_floor_y_bias &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND ||
              (escapeair_episode.sustained && batch->state.jumps_left[idx] != 0u &&
               ecb_lock_timer_seed > 1u)))
                ? 1u
                : 0u;
        const uint8_t suppress_cliff_ledge_locked_final_land =
            // refs/melee/src/melee/mp/mpcoll.c::{
            (carried_cliff_ledge_restored_entry_above_floor ||
             (carried_cliff_ledge_floor_restored_only &&
              (!final_ledge_x_in_bounds ||
               final_landing_lift < (escapeair_entry_bottom_rel0 + k_ecb_vertical_unit)) &&
              ecb_lock_timer_seed > 1u))
                ? 1u
                : 0u;
        const MslEscapeAirFinalPublicationOwners escapeair_final_owners = {
            .sustained_same_platform_lock = suppress_sustained_escapeair_same_platform_lock_land,
            .sustained_same_ledge_lock =
                suppress_sustained_escapeair_same_ledge_lock_land ||
                suppress_sustained_escapeair_adjacent_ledge_without_allow_interrupt,
            .locked_missing_bottom_owner =
                suppress_locked_escapeair_missing_bottom_owner_land ||
                suppress_fresh_jumpaerial_downheld_nonplatform_stale_land,
            .locked_desired_platform_without_bottom_sweep =
                suppress_locked_desired_platform_without_bottom_sweep,
            .locked_desired_nonplatform_without_bottom_sweep =
                suppress_locked_desired_nonplatform_without_bottom_sweep,
            .escapeair_jumpaerial_soft_owner_early_direct_land =
                suppress_escapeair_jumpaerial_soft_owner_early_direct_land,
            .kneebend_ledge_missing_owner =
                suppress_kneebend_escapeair_missing_ledge_owner_final_land,
            .kneebend_static_platform_lock = suppress_kneebend_escapeair_static_platform_lock_land,
            .jumpaerial_high_lift_ledge = suppress_jumpaerial_escapeair_high_lift_ledge_final_land,
            .jumpaerial_static_platform_overstep =
                suppress_jumpaerial_escapeair_static_platform_overstep_final_land ||
                suppress_jumpaerial_escapeair_first_locked_static_platform_land,
            .cliff_ledge_locked = suppress_cliff_ledge_locked_final_land,
        };
        MslMpcollFloorRejectPacket final_floor_reject = {0};
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_escapeair_transformed_remap_land,
            MSL_MPCOLL_REJECT_ESCAPEAIR_TRANSFORMED_REMAP,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_KEEP_CURRENT, 0u, (uint32_t)MSL_MPCOLL_PHASE_AIR_471F8);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_specialairhi_platform_land,
            MSL_MPCOLL_REJECT_SPECIALAIRHI_PLATFORM, MSL_MPCOLL_FLOOR_REJECT_RESTORE_KEEP_CURRENT,
            (uint32_t)MSL_MPCOLL_FLOOR_REJECT_SIDE_FLOOR_SKIP_TO_CONTACT,
            (uint32_t)MSL_MPCOLL_PHASE_GROUND_B108);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_sheik_vanish_start1_platform_pass_land,
                                         MSL_MPCOLL_REJECT_SHEIK_VANISH_START1_PLATFORM_PASS,
                                         sheik_vanish_start1_platform_pass_snaps_new_floor
                                             ? MSL_MPCOLL_FLOOR_REJECT_RESTORE_KEEP_CURRENT
                                             : MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y,
                                         0u, (uint32_t)MSL_MPCOLL_PHASE_AIR_473CC);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_fallspecial_first_sustained_current_ecb_land,
                                         MSL_MPCOLL_REJECT_FALLSPECIAL_FIRST_SUSTAINED,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_fallspecial_platform_final_without_source_bottom,
                                         MSL_MPCOLL_REJECT_FALLSPECIAL_PLATFORM_NO_SOURCE_BOTTOM,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_damage_sustained_platform_without_bottom_sweep,
            MSL_MPCOLL_REJECT_DAMAGE_SUSTAINED_PLATFORM_NO_BOTTOM_SWEEP,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_XY, 0u,
            (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_fall_same_floor_early_final_land,
            MSL_MPCOLL_REJECT_FALL_SAME_FLOOR_EARLY, MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y,
            0u, (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_specialhi_transformed_platform_land,
                                         MSL_MPCOLL_REJECT_SPECIALHI_TRANSFORMED_PLATFORM,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_KEEP_CURRENT, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_GROUND_B108);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_specialhi_understage_hard_floor_land,
            MSL_MPCOLL_REJECT_SPECIALHI_UNDERSTAGE_HARD_FLOOR,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_SPECIALHI_UNDERSTAGE_CLEARANCE, 0u,
            (uint32_t)MSL_MPCOLL_PHASE_GROUND_B108);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_specialhi_from_below_hard_floor_land,
                                         MSL_MPCOLL_REJECT_SPECIALHI_FROM_BELOW_HARD_FLOOR,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_GROUND_B108);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_specialairhi_floor_angle_land,
                                         MSL_MPCOLL_REJECT_SPECIALAIRHI_FLOOR_ANGLE,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_KEEP_CURRENT, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_GROUND_B108);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_airborne_transformed_platform_pre_handoff_land,
            MSL_MPCOLL_REJECT_AIRBORNE_TRANSFORMED_PLATFORM_PRE_HANDOFF,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
            (uint32_t)MSL_MPCOLL_PHASE_GROUND_B108);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_attackair_transformed_platform_ecb_only_final_land,
            MSL_MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_ECB_ONLY,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y,
            (uint32_t)MSL_MPCOLL_FLOOR_REJECT_SIDE_ATTACKAIR_PUBLISH_SKIP_FROM_SWEEP,
            (uint32_t)MSL_MPCOLL_PHASE_AIR_471F8);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_attackair_transformed_platform_floor_skip_final_land,
            MSL_MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_FLOOR_SKIP,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y,
            (uint32_t)MSL_MPCOLL_FLOOR_REJECT_SIDE_ATTACKAIR_CLEAR_FLOOR_SKIP,
            (uint32_t)MSL_MPCOLL_PHASE_AIR_471F8);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_attackair_offspan_hard_floor_edge_final_land,
                                         MSL_MPCOLL_REJECT_ATTACKAIR_OFFSPAN_HARD_FLOOR_EDGE,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_AIR_471F8);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject,
            suppress_attackair_hard_floor_root_projection_without_bottom_final_land,
            MSL_MPCOLL_REJECT_ATTACKAIR_HARD_SLOPE_ROOT_WITHOUT_BOTTOM,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
            (uint32_t)MSL_MPCOLL_PHASE_AIR_471F8);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_attackair_single_create_no_bottom_owner_final_land,
            MSL_MPCOLL_REJECT_ATTACKAIR_SINGLE_CREATE_NO_BOTTOM_OWNER,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
            (uint32_t)MSL_MPCOLL_PHASE_AIR_471F8);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_attackair_transformed_platform_below_final_land,
                                         MSL_MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_BELOW,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_AIR_471F8);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_jumpaerial_transformed_platform_fastfall_land,
                                         MSL_MPCOLL_REJECT_JUMPAERIAL_TRANSFORMED_PLATFORM_FASTFALL,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_jumpaerial_static_platform_from_below_final_land,
                                         MSL_MPCOLL_REJECT_JUMPAERIAL_STATIC_PLATFORM_FROM_BELOW,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_fall_transformed_platform_fastfall_land,
                                         MSL_MPCOLL_REJECT_FALL_TRANSFORMED_PLATFORM_FASTFALL,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_fall_loop_wrap_stage_object_floor_to_hard_floor_land,
            MSL_MPCOLL_REJECT_FALL_LOOP_WRAP_STAGE_OBJECT_FLOOR_TO_HARD_FLOOR,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
            (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_fall_stale_platform_first_hard_floor_land,
                                         MSL_MPCOLL_REJECT_FALL_STALE_PLATFORM_FIRST_HARD_FLOOR,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_fall_shallow_terminal_hard_floor_land,
                                         MSL_MPCOLL_REJECT_FALL_SHALLOW_TERMINAL_HARD_FLOOR,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_fall_static_platform_from_below_land,
                                         MSL_MPCOLL_REJECT_FALL_STATIC_PLATFORM_FROM_BELOW,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, suppress_fall_attackair_entry_transformed_platform_root_only_land,
            MSL_MPCOLL_REJECT_FALL_ATTACKAIR_ENTRY_TRANSFORMED_PLATFORM_ROOT_ONLY,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
            (uint32_t)MSL_MPCOLL_PHASE_PLATFORM_PASS);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_missfoot_ecb_lock_first_floor_land,
                                         MSL_MPCOLL_REJECT_MISSFOOT_ECB_LOCK_FIRST_FLOOR,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_AIR_473CC);
        mpcoll_floor_reject_add_escapeair_final_owners(&final_floor_reject,
                                                       &escapeair_final_owners);
        mpcoll_floor_reject_add_if_state(&final_floor_reject,
                                         suppress_specialairlw_start_stale_platform_land,
                                         MSL_MPCOLL_REJECT_SPECIALAIRLW_START_STALE_PLATFORM,
                                         MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
                                         (uint32_t)MSL_MPCOLL_PHASE_GROUND_B108);
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, damage_active_hitlag_root_below_bottom_above_floor_owner,
            MSL_MPCOLL_REJECT_DAMAGE_ACTIVE_HITLAG_ROOT_BELOW_BOTTOM_ABOVE_FLOOR,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
            (uint32_t)(MSL_MPCOLL_PHASE_AIR_473CC | MSL_MPCOLL_PHASE_AIR_477E0));
        mpcoll_floor_reject_add_if_state(
            &final_floor_reject, damage_active_hitlag_downward_sdi_airborne_owner,
            MSL_MPCOLL_REJECT_DAMAGE_ACTIVE_HITLAG_DOWNWARD_SDI_AIRBORNE,
            MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y, 0u,
            (uint32_t)(MSL_MPCOLL_PHASE_AIR_473CC | MSL_MPCOLL_PHASE_AIR_477E0));
        if (final_floor_reject.bits != 0u) {
          if ((final_floor_reject.bits & MSL_MPCOLL_REJECT_SHEIK_VANISH_START1_PLATFORM_PASS) !=
              0u) {
            // refs/melee/src/melee/mp/mpcoll.c::mpUpdateFloorSkip
            msl_mpcoll_update_floor_skip(batch, idx, (uint16_t)final_ground_line_idx);
          }
          mpcoll_floor_probe_reject_bits(&mpcoll_ctx, final_floor_reject.bits,
                                         (MslMpcollSourcePhases)final_floor_reject.source_phases,
                                         final_ground_line_idx, final_ground_line_idx);
          mpcoll_floor_probe_bottom_interval(&mpcoll_ctx, prev_bottom_x, prev_bottom_y,
                                             cur_bottom_x, cur_bottom_y);
          mpcoll_apply_final_floor_rejection_bits(
              &mpcoll_ctx, &floor_publication, final_floor_reject, y, cur_bottom_x, cur_bottom_y,
              cur_bot.rel_y, &prev_ecb_points, final_ground_line_idx, x, prev_y);
        }
      }

      if (floor_publication.on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
          batch->state.seed_prev_action_frame[idx] <= 2 && prefer_line_idx >= 0 &&
          floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx) &&
          floor_publication.contact.ground_id != seed_ground_id &&
          !stage_collision_floor_line_is_platform(stage_id, floor_publication.contact.ground_id)) {
        const int kneebend_escapeair_line_idx =
            stage_collision_floor_line_index(stage_id, floor_publication.contact.ground_id);
        float kneebend_escapeair_line_y = 0.0f;
        if (kneebend_escapeair_line_idx >= 0 &&
            floor_line_y_at_x_for_env(batch, bi, g, kneebend_escapeair_line_idx, x,
                                      &kneebend_escapeair_line_y)) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/{mpcoll.c::mpColl_800471F8,mplib.c::mpLib_8004DD90_Floor}
          floor_publication.contact.contact_x = x;
          floor_publication.contact.contact_y = kneebend_escapeair_line_y;
          batch->state.pos_y[idx] = kneebend_escapeair_line_y + k_floor_y_bias;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
              (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION, floor_publication.contact.ground_id,
              floor_publication.contact.contact_x, floor_publication.contact.contact_y,
              floor_publication.contact.normal_x, floor_publication.contact.normal_y);
        }
      }

      if (!floor_publication.on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
          batch->state.seed_prev_action_frame[idx] >= 3 && batch->state.action_frame[idx] <= 1 &&
          seed_ground_id != 0xFFFFu && batch->state.speed_y_self[idx] < 0.0f) {
        const int kneebend_escapeair_floor_idx =
            stage_collision_floor_line_index(stage_id, seed_ground_id);
        float kneebend_escapeair_floor_y = 0.0f;
        float kneebend_escapeair_floor_nx = 0.0f;
        float kneebend_escapeair_floor_ny = 1.0f;
        if (kneebend_escapeair_floor_idx >= 0 &&
            (size_t)kneebend_escapeair_floor_idx < g->line_count &&
            !g->lines[(size_t)kneebend_escapeair_floor_idx].is_ledge &&
            floor_x_within_line_segment_strict(batch, bi, g, kneebend_escapeair_floor_idx, x) &&
            floor_line_y_at_x_for_env(batch, bi, g, kneebend_escapeair_floor_idx, x,
                                      &kneebend_escapeair_floor_y) &&
            floor_line_normal_for_env(batch, bi, g, kneebend_escapeair_floor_idx,
                                      &kneebend_escapeair_floor_nx, &kneebend_escapeair_floor_ny) &&
            prev_y >= (kneebend_escapeair_floor_y - k_floor_y_bias) &&
            y <= (kneebend_escapeair_floor_y + k_floor_y_bias)) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_800471F8
          floor_publication.on_ground = 1u;
          floor_publication.result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
          floor_publication.contact.ground_id = seed_ground_id;
          floor_publication.contact.contact_x = x;
          floor_publication.contact.contact_y = kneebend_escapeair_floor_y;
          floor_publication.contact.normal_x = kneebend_escapeair_floor_nx;
          floor_publication.contact.normal_y = kneebend_escapeair_floor_ny;
          batch->state.pos_y[idx] = kneebend_escapeair_floor_y + k_floor_y_bias;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
              (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION, seed_ground_id,
              floor_publication.contact.contact_x, floor_publication.contact.contact_y,
              floor_publication.contact.normal_x, floor_publication.contact.normal_y);
        }
      }

      if (floor_publication.on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          ecb_lock_timer_seed != 0u && batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
          batch->state.shine_jump_preserved_desired_bottom[idx] != 0u &&
          msl_escapeair_locked_bottom_owner_is_live_jumpaerial(
              batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
          floor_publication.result_mode != (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP &&
          batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
          (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) >
              (floor_publication.contact.contact_y + k_floor_y_bias) &&
          (batch->state.coll_desired_ecb_bottom_locked_owner[idx] ==
               (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_SOFT_OR_TRANSFORM ||
           ecb_lock_timer_seed > 4u)) {
        // refs/melee/src/melee/mp/mpcoll.c::{
        floor_publication.on_ground = 0u;
        floor_publication.result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE;
        batch->state.pos_y[idx] = y;
        mpcoll_record_callback_floor_result_with_mode(
            &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_NONE, (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE,
            0xFFFFu, 0.0f, 0.0f, 0.0f, 1.0f);
      }

      if (floor_publication.on_ground && action_id == (uint16_t)MSL_ACT_CAPTURE_PULLED_HI &&
          prev_action_id == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW &&
          batch->state.floor_skip_segment_id != NULL &&
          batch->state.floor_skip_segment_id[idx] == floor_publication.contact.ground_id) {
        // refs/melee/src/melee/ft/ft_081B.c::{ft_8008403C,ft_80083C00}
        floor_publication.on_ground = 0u;
        floor_publication.result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE;
        batch->state.pos_y[idx] = y;
        mpcoll_record_callback_floor_result_with_mode(
            &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_NONE, (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE,
            floor_publication.contact.ground_id, floor_publication.contact.contact_x,
            floor_publication.contact.contact_y, floor_publication.contact.normal_x,
            floor_publication.contact.normal_y);
      }

      mpcoll_materialize_floor_publication_result(&mpcoll_ctx, &floor_publication);
      if ((floor_publication.on_ground &&
           (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR ||
            (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) ||
            is_common_fallspecial_action(action_id))) ||
          damage_active_hitlag_downward_sdi_airborne_owner ||
          damage_active_hitlag_root_below_bottom_above_floor_owner) {
        mpcoll_apply_late_floor_publication_guards(
            &mpcoll_ctx, &floor_publication, ecb_lock_timer_seed, prefer_line_idx, seed_ground_id,
            y, prev_x, prev_bottom_x, prev_bottom_y, cur_bottom_x, cur_bottom_y, &cur_ecb_points,
            raw_current_floor_line_idx, escapeair_stale_platform_root_handoff_hit,
            (uint8_t)(escapeair_fresh_jump_height_platform_handoff_hit ||
                      escapeair_no_lock_static_platform_sweep_hit),
            damage_active_hitlag_downward_sdi_airborne_owner,
            damage_active_hitlag_root_below_bottom_above_floor_owner);
      }
      (void)mpcoll_materialize_active_damage_hitlag_stay_airborne_floor(&mpcoll_ctx,
                                                                        &floor_publication);
      uint8_t downbound_floor_projection_applied = 0u;
      if (floor_publication.on_ground) {
        downbound_floor_projection_applied = msl_mpcoll_8004b108_downbound_project_attack_speed(
            batch, idx, bi, g, stage_id, was_grounded, batch->state.ground_id[idx],
            &floor_publication.contact);
      }
      mpcoll_commit_final_floor_state(&mpcoll_ctx, &floor_publication);
      if (batch->state.on_ground[idx] != 0u && !was_grounded &&
          is_damage_ground_collision_action(action_id)) {
        const MslCharParams* damage_land_ch = msl_char_params_fast(batch->state.char_id[idx]);
        if (damage_land_ch != NULL) {
          batch->state.jumps_left[idx] = damage_land_ch->max_jumps;
        }
      }
      if (batch->state.on_ground[idx] != 0u && downbound_floor_projection_applied == 0u) {
        (void)msl_mpcoll_8004b108_downbound_project_attack_speed(
            batch, idx, bi, g, stage_id, was_grounded, batch->state.ground_id[idx], NULL);
      }
      if (!batch->state.on_ground[idx] && action_is_down_bound(action_id) &&
          seed_ground_id != 0xFFFFu &&
          stage_collision_floor_line_has_height_platform_transform(stage_id, seed_ground_id) &&
          stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi,
                                                                             seed_ground_id)) {
        const int carry_line_idx = stage_collision_floor_line_index(stage_id, seed_ground_id);
        float platform_dx = 0.0f;
        float platform_dy = 0.0f;
        float carry_line_y = 0.0f;
        if (carry_line_idx >= 0 && (size_t)carry_line_idx < g->line_count &&
            floor_x_within_line_segment_strict(batch, bi, g, carry_line_idx, x) &&
            floor_line_y_at_x_for_env(batch, bi, g, carry_line_idx, x, &carry_line_y) &&
            carry_line_y >= -k_floor_y_bias &&
            stage_collision_floor_line_motion_delta(batch, bi, &g->lines[(size_t)carry_line_idx],
                                                    &platform_dx, &platform_dy) &&
            y > k_floor_y_bias) {
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
          // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
          batch->state.pos_x[idx] = x + platform_dx;
          batch->state.pos_y[idx] = y + platform_dy;
          batch->state.ground_id[idx] = seed_ground_id;
          batch->state.ground_contact_x[idx] = x + platform_dx;
          batch->state.ground_contact_y[idx] = y + platform_dy;
        }
      }
      MslEcbWorldPoints stored_prev_ecb_points =
          have_state_cur_ecb ? state_cur_ecb_points : prev_ecb_points;
      MslEcbWorldPoints stored_desired_ecb_points = desired_ecb_points;
      const uint8_t nonfastfall_fall_generic_lock_bottom =
          (uint8_t)(action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] == 0u &&
                    msl_escapeair_locked_bottom_owner_is_seeded(
                        batch->state.coll_desired_ecb_bottom_locked_owner[idx]));
      uint8_t stored_locked_desired_bottom_owner =
          (use_locked_desired_ecb_bottom && ecb_lock_active &&
           !nonfastfall_fall_generic_lock_bottom)
              ? msl_escapeair_locked_bottom_owner_preserve_or_seeded(
                    batch->state.coll_desired_ecb_bottom_locked_owner[idx])
              : 0u;
      const uint8_t escapeair_airborne_jumpaerial_cliff_locked_bottom_carry =
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800471F8}
          (escapeair_episode.active && seed_prev_action_is_jumpaerial && c != NULL &&
           batch->state.ledge_cooldown[idx] >=
               (uint8_t)(c->ledge_cooldown_frames > 5u ? c->ledge_cooldown_frames - 5u : 0u) &&
           batch->state.on_ground[idx] == 0u && batch->state.cliff_ledge_floor_segment_id != NULL &&
           batch->state.cliff_ledge_floor_segment_id[idx] != 0xFFFFu &&
           fabsf(frame_start_desired_bottom_rel_y) > k_floor_y_bias)
              ? 1u
              : 0u;
      if (!stored_locked_desired_bottom_owner && ecb_lock_active &&
          ((escapeair_episode.active &&
            ((stage_has_height_platform_transform &&
              msl_escapeair_locked_bottom_owner_is_seeded(
                  batch->state.coll_desired_ecb_bottom_locked_owner[idx])) ||
             batch->state.coll_desired_ecb_bottom_locked_owner[idx] ==
                 (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_LIVE_JUMPAERIAL_SOFT_OR_TRANSFORM ||
             escapeair_airborne_jumpaerial_cliff_locked_bottom_carry)) ||
           action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
           action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
           (action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] != 0u) ||
           msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_COMMON_AIR_COLL)) &&
          batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
          (msl_escapeair_locked_bottom_owner_any(
               batch->state.coll_desired_ecb_bottom_locked_owner[idx]) ||
           ((action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
             action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
            batch->state.shine_jump_iasa_entered_this_frame[idx] != 0u))) {
        // refs/melee/src/melee/mp/mpcoll.c::{
        const float stored_desired_bottom_rel_y =
            escapeair_airborne_jumpaerial_cliff_locked_bottom_carry
                ? frame_start_desired_bottom_rel_y
                : batch->state.coll_desired_ecb_bottom_rel_y[idx];
        msl_ecb_world_points_preserve_desired_bottom_rel_y(&stored_desired_ecb_points, x, y,
                                                           stored_desired_bottom_rel_y);
        if (msl_escapeair_locked_bottom_owner_any(
                batch->state.coll_desired_ecb_bottom_locked_owner[idx])) {
          stored_locked_desired_bottom_owner = msl_escapeair_locked_bottom_owner_preserve_or_seeded(
              batch->state.coll_desired_ecb_bottom_locked_owner[idx]);
        }
        if ((action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
             action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
            batch->state.shine_jump_iasa_entered_this_frame[idx] != 0u &&
            msl_motion_state_fx_special_kind(batch->state.char_id[idx],
                                             batch->state.seed_prev_action_id[idx]) ==
                (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_LOOP &&
            stored_desired_bottom_rel_y > k_floor_y_bias) {
          batch->state.shine_jump_preserved_desired_bottom[idx] = 1u;
        }
        if (escapeair_airborne_jumpaerial_cliff_locked_bottom_carry) {
          stored_locked_desired_bottom_owner =
              (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_SEEDED_COLL_X130;
        }
      }
      if (!ecb_lock_active || floor_publication.on_ground ||
          !(action_id == (uint16_t)MSL_ACT_ESCAPE_AIR ||
            action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B)) {
        batch->state.shine_jump_preserved_desired_bottom[idx] = 0u;
      }
      if (escapeair_airborne_jumpaerial_cliff_locked_bottom_carry && ecb_lock_active) {
        msl_ecb_world_points_preserve_desired_bottom_rel_y(&stored_desired_ecb_points, x, y,
                                                           frame_start_desired_bottom_rel_y);
        stored_locked_desired_bottom_owner =
            (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_SEEDED_COLL_X130;
      }
      const float active_damage_hitlag_contact_floor_y = batch->state.ground_contact_y[idx];
      const uint8_t active_damage_hitlag_floor_contact_carry =
          (batch->state.hitlag[idx] != 0u &&
           (action_uses_active_hitlag_downward_sdi_floorhug(action_id, batch, idx) ||
            mpcoll_damageair_action(action_id)) &&
           (batch->state.coll_env_flags[idx] & (uint32_t)MSL_COLLIDE_FLOOR_MASK) != 0u &&
           batch->state.ground_id[idx] != 0xFFFFu &&
           isfinite(active_damage_hitlag_contact_floor_y) &&
           batch->state.pos_y[idx] >= (active_damage_hitlag_contact_floor_y - k_floor_y_bias))
              ? 1u
              : 0u;
      if (active_damage_hitlag_ecb_carry && have_state_cur_ecb &&
          !active_damage_hitlag_ecb_consumer) {
        cur_ecb_points = state_cur_ecb_points;
        stored_desired_ecb_points = state_cur_ecb_points;
      }
      if (active_damage_hitlag_floor_contact_carry && !active_damage_hitlag_ecb_carry) {
        const float floor_contact_bottom_rel =
            active_damage_hitlag_contact_floor_y - batch->state.pos_y[idx];
        mpcoll_ecb_world_points_from_rel(
            &cur_ecb_points, batch->state.pos_x[idx], batch->state.pos_y[idx],
            floor_contact_bottom_rel, cur_ecb_points.top_rel_y, cur_ecb_points.left_rel_x,
            cur_ecb_points.right_rel_x, cur_ecb_points.side_rel_y, cur_ecb_points.frame_u16);
        stored_desired_ecb_points = cur_ecb_points;
      }
      const uint8_t active_damage_hitlag_exit_frame =
          (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u) ? 1u : 0u;
      const uint8_t active_damage_hitlag_existing_floor_contact_carry =
          ((batch->state.hitlag[idx] != 0u || active_damage_hitlag_exit_frame) &&
           is_damage_collision_landing_action(action_id) &&
           batch->state.coll_damage_hitlag_floor_contact_runtime[idx] != 0u)
              ? 1u
              : 0u;
      if (!callback_stopped_at_substep) {
        const MslEcbWorldPoints* interpolation_start_ecb =
            have_squeeze_restore_ecb ? &squeeze_restore_ecb_points : &stored_prev_ecb_points;
        mpcoll_penultimate_interpolated_ecb(&stored_prev_ecb_points, &stored_prev_ecb_points,
                                            interpolation_start_ecb, &cur_ecb_points, prev_x,
                                            prev_y, x, y);
      }
      if (specialhi_jobj_ecb_active && have_state_desired_ecb) {
        // refs/melee/src/melee/mp/mpcoll.c::{
        cur_ecb_points = state_desired_ecb_points;
        stored_prev_ecb_points = state_desired_ecb_points;
      }
      if (active_damage_hitlag_ecb_carry && have_state_cur_ecb && !on_ground) {
        // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor}
        cur_ecb_points = state_cur_ecb_points;
        stored_prev_ecb_points = state_cur_ecb_points;
      }
      mpcoll_store_prev_ecb_points(batch, idx, &stored_prev_ecb_points);
      mpcoll_store_current_ecb_points(batch, idx, &cur_ecb_points);
      mpcoll_store_desired_ecb_points(batch, idx, &stored_desired_ecb_points);
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
      // refs/melee/src/melee/mp/mpcoll.c::{
      const uint8_t active_damage_hitlag_floor_contact_runtime_next =
          ((active_damage_hitlag_floor_contact_carry && !active_damage_hitlag_ecb_carry) ||
           active_damage_hitlag_existing_floor_contact_carry)
              ? 1u
              : 0u;
      batch->state.coll_damage_hitlag_ecb_valid[idx] =
          (active_damage_hitlag_ecb_carry || active_damage_hitlag_floor_contact_runtime_next) ? 1u
                                                                                              : 0u;
      if (active_damage_hitlag_ecb_carry == 0u) {
        batch->state.coll_damage_hitlag_ecb_source_kind[idx] = MSL_DAMAGE_HITLAG_ECB_SOURCE_NONE;
      }
      batch->state.coll_damage_hitlag_floor_contact_runtime[idx] =
          active_damage_hitlag_floor_contact_runtime_next;
      const float unlocked_fall_pose_bottom_rel =
          (action_id == (uint16_t)MSL_ACT_FALL && ecb_lock_timer <= 1u)
              ? mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_cur, 0u)
              : 0.0f;
      if (!batch->state.on_ground[idx] && action_id == (uint16_t)MSL_ACT_FALL &&
          ecb_lock_timer <= 1u &&
          batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias &&
          unlocked_fall_pose_bottom_rel > k_floor_y_bias) {
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
        MslEcbWorldPoints unlocked_current_ecb = {0};
        MslEcbWorldPoints unlocked_desired_ecb = {0};
        msl_ecb_world_points_sample(&unlocked_current_ecb, char_id, anim, ecb_frame_prev,
                                    facing_dir_for_ecb, prev_x, prev_y, 0u);
        msl_ecb_world_points_sample(&unlocked_desired_ecb, char_id, anim, ecb_frame_cur,
                                    facing_dir_for_ecb, x, y, 0u);
        mpcoll_store_prev_ecb_points(batch, idx, &unlocked_current_ecb);
        mpcoll_store_current_ecb_points(batch, idx, &unlocked_current_ecb);
        mpcoll_store_desired_ecb_points(batch, idx, &unlocked_desired_ecb);
        batch->state.ecb_lock_timer[idx] = 0u;
      }
      if (batch->state.on_ground[idx] && batch->state.ground_id[idx] != 0xFFFFu) {
        MslMpcollFloorContact final_root_contact = {
            .ground_id = batch->state.ground_id[idx],
            .contact_x = batch->state.ground_contact_x[idx],
            .contact_y = batch->state.ground_contact_y[idx],
            .normal_x = batch->state.ground_normal_x[idx],
            .normal_y = batch->state.ground_normal_y[idx],
        };
        if (mpcoll_grounded_final_root_flat_seam_remap(&mpcoll_ctx, &final_root_contact)) {
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
              (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION, final_root_contact.ground_id,
              final_root_contact.contact_x, final_root_contact.contact_y,
              final_root_contact.normal_x, final_root_contact.normal_y);
          mpcoll_commit_grounded_floor_contact(&mpcoll_ctx, &final_root_contact, was_grounded);
        }
      }
      batch->state.coll_prev_ecb_bottom_valid[idx] = 1u;
      batch->state.coll_ecb_bottom_valid[idx] = 1u;
      batch->state.coll_desired_ecb_bottom_valid[idx] = 1u;
      batch->state.coll_desired_ecb_bottom_locked_owner[idx] = stored_locked_desired_bottom_owner;
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_LoadECB_inline}
      {
        float eff = 0.0f;
        if (!batch->state.on_ground[idx]) {
          eff = mpcoll_pose_ecb_bottom_rel_y(
              char_id, batch->state.animation_index[idx],
              msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]), 0u);
          if (common_fall_blended_ecb_consumer && have_common_fall_blended_current_ecb) {
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
            eff = common_fall_blended_current_ecb_points.bottom_rel_y;
          }
          if (batch->state.ecb_lock_timer[idx] != 0u) {
            eff = (batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
                   msl_escapeair_locked_bottom_owner_any(
                       batch->state.coll_desired_ecb_bottom_locked_owner[idx]))
                      ? batch->state.coll_desired_ecb_bottom_rel_y[idx]
                      : 0.0f;
          }
        }
        batch->state.coll_effective_bottom_rel_prev[idx] = eff;
        batch->state.coll_effective_bottom_rel_prev_valid[idx] = 1u;
      }
    }
  }
}
