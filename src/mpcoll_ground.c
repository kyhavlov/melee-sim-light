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
#include "sheik_specials.h"
#include "specialhi_pose.h"
#include "state_flags.h"
#include "stage_collision.h"
#include "input_axis.h"
#include "item_article_params.h"
#include "throw_flow.h"

// Decomp constants (mplib.c):
// - mpLib_8004DD90_Floor clamps small off-end X within ±0.1 before returning -1 (airborne).
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
// - mpLib_8004DD90_Floor applies a +0.0001 bias to the vertical correction to keep the point
//   infinitesimally above the floor line.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
// - mpCheckFloor treats floors as horizontal when |y0 - y1| <= 0.0001.
//   refs/melee/src/melee/mp/mplib.c::mpCheckFloor
static const float k_floor_x_end_clamp = 0.1f;
static const float k_floor_y_bias = 0.0001f;
static const float k_floor_horiz_dy_thresh = 0.0001f;

// refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
static const float k_floor_edge_wall_probe_x_offset = 1.0f;
static const float k_floor_edge_wall_probe_y_offset = 1.0f;
// Decomp ECB vertical unit in the callback path:
// - mpColl_80042384 enforces a minimum +1.0f vertical separation for desired ECB extents.
// - mpColl_LoadECB_JObj uses midpoint +/- 1.0f in its tightened vertical-span path.
// refs/melee/src/melee/mp/mpcoll.c::{mpColl_80042384,mpColl_LoadECB_JObj}
static const float k_ecb_vertical_unit = 1.0f;

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
      const MslMpcollSourcePhases source_phases = mpcoll_source_phases_for_motion_state(
          mpcoll_ctx.char_id, action_id,
          mpcoll_ft_check_ground_ledge_uses_no_ledge_path(batch, idx));
      mpcoll_floor_probe_clear(batch, idx);

      // Decomp: Fighter_procMap runs every frame (not gated by hitlag), and collision callbacks
      // such as ftCo_DamageFly_Coll internally select hitlag-specific mpColl paths when needed.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4

      // Match-flow actions use dedicated (or NULL) collision callbacks in decomp; this lite sim
      // skips the generic stage collision pass until those paths are implemented.
      // refs: src/match_flow.c::match_flow_should_stage_collide
      if (!match_flow_should_stage_collide(action_id)) {
        if (batch->state.stocks[idx] == 0u && batch->state.char_id[idx] == 0u &&
            action_id == (uint16_t)MSL_ACT_DEAD_DOWN) {
          // Inactive zero-stock slots have no live Fighter map callback. Preserve the Slippi
          // terminal slot shape installed by match_flow.c instead of converting it to a live
          // airborne DeadDown fighter.
          // refs/melee/src/melee/gm/gm_16AE.c::fn_8016B918_inline
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

      // Cliff / ledge hold actions use their own snap logic and should not be stage-grounded.
      if (msl_motion_state_cliff_hold_phys_snap(mpcoll_ctx.char_id, action_id)) {
        batch->state.on_ground[idx] = 0;
        continue;
      }

      if (msl_action_is_thrown_victim(action_id)) {
        const uint8_t owner = batch->state.grab_owner_port[idx];
        if (owner != 0xFFu && owner < (uint8_t)num_players && owner != (uint8_t)p) {
          // Decomp: common Thrown* states have empty Coll callbacks, so generic stage-collision
          // grounding does not run while the victim remains attached to the throw owner.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{
          //   ftCo_ThrownF_Coll,ftCo_ThrownB_Coll,ftCo_ThrownHi_Coll,ftCo_ThrownLw_Coll
          // }
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
            // Replay-visible Yoshi/Randall ground ids can carry the raw source line while the live
            // fighter support owner is the generated path-transformed stage-object line. Use the
            // generated line for CollData carry.
            //
            // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,Ground_801C2FE0}
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
            // Stage-object platform carry:
            // Randall's ground object refreshes collision before fighter map callbacks; grounded
            // riders keep CollData.floor.index and inherit the platform transform delta before
            // projection. FoD vertical carry remains owned by signed DD90 projection so replay
            // seeded height rows do not double-apply height velocity.
            // Grounded Fox/Falco SpecialN and KneeBend also use the ft_80083F88 allow-ground-to-air
            // path. That owner is admitted here only for Randall's path-transformed stage object;
            // FoD height-platform correction stays in the signed DD90 floor projection path.
            // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,Ground_801C2FE0}
            // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
            //   ftFx_SpecialNStart_Coll,ftFx_SpecialNLoop_Coll,ftFx_SpecialNEnd_Coll}
            // data/motion_state/owners/{fox,falco}.bin
            //   (MSLMSO01 class FT80083F88_GROUND_TO_AIR_COLL)
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
      // Decomp: Fighter_procMap decrements fp->ecb_lock before calling coll_cb/map callbacks and
      // clears CollData_X130_Locked when the countdown reaches 0.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
      //
      // This simulator stores only the countdown (`ecb_lock_timer`) and uses it as the lock gate.
      // Grounded snapshots should not carry a stale lock.
      uint8_t ecb_lock_timer = ecb_lock_timer_seed;
      if (batch->state.on_ground[idx]) {
        ecb_lock_timer = 0;
      } else if (ecb_lock_timer > 0u) {
        ecb_lock_timer = (uint8_t)(ecb_lock_timer - 1u);
      }
      batch->state.ecb_lock_timer[idx] = ecb_lock_timer;
      const uint8_t ecb_lock_active = (ecb_lock_timer > 0u) ? 1u : 0u;
      // Common Damage_Coll lock-bottom ownership:
      // seed ecb_lock_timer is a prefix-causal CollData_X130_Locked snapshot at frame start. The
      // generic countdown is still decremented before map callbacks, but replay-real common
      // DamageHi/N/Lw rows with a seeded value of 1 use the locked-bottom ECB in the same
      // ft_80081DD4 -> mpColl_800473CC callback pass. Keep this off DamageAir/DamageFly; those
      // callbacks have distinct floor-contact timing and negative locks that remain airborne.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_JObj}
      const uint8_t damage_collision_uses_seeded_lock_bottom =
          (is_damage_ground_collision_action(action_id) && ecb_lock_timer_seed != 0u) ? 1u : 0u;

      // Decomp shape: ECB is loaded each collision step and prev_ecb is a one-step lag:
      // mpCollInterpolateECB assigns prev_ecb = ecb before updating.
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
        // DownBound callback ordering is Anim then Coll in the same frame; sampling the next ECB
        // frame better matches the post-Anim pose used by ftCo_DownBound_Coll on the bounce frame.
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procMap}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
        //   ftCo_DownBound_Anim,ftCo_DownBound_Coll
        // }
        if (ecb_frame_bias_next != 0xFFFFu) {
          ecb_frame_bias_next = (uint16_t)(ecb_frame_bias_next + 1u);
        }
      } else if (is_damage_fly_collision_action(action_id) && batch->state.action_frame[idx] <= 2 &&
                 !(batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u)) {
        // Damage/DamageFly callback ordering is also Anim then Coll in Fighter_8006A360. On early
        // entry frames, sampling the post-Anim ECB pose reduces one-frame "still airborne" misses
        // before DownBound/Landing transitions.
        // Hitlag-exit rows are different: Fighter_8006D10C consumes
        // ftCo_Damage_OnExitHitlag before map, and the following ftCo_DamageFly_Coll /
        // ft_80081DD4 pass uses the current loaded DamageFly ECB for mpColl_80044628_Floor. The
        // next pose is stored for following-frame CollData continuity, but it is not the floor
        // sweep source on the exit frame.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006D10C
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
        //   ftCo_Damage_Anim,ftCo_DamageFly_Anim,ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
        if (ecb_frame_bias_next != 0xFFFFu) {
          ecb_frame_bias_next = (uint16_t)(ecb_frame_bias_next + 1u);
        }
      }

      // Decomp: some stage collision entrypoints load ECB with flags where `flags & 1` forces
      // desired_ecb.bottom.y = 0.0 (relative to cur_pos). This stabilizes grounded contact against
      // pose-driven ECB changes.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj (flags & 1)
      MslEcbBottomWorldPoint cur_bot = {0};
      MslEcbBottomWorldPoint prev_bot = {0};

      const float x = batch->state.pos_x[idx];
      const float y = batch->state.pos_y[idx];
      // Floor sweeps consume the frame-start CollData.prev_pos snapshot so pre-collision callbacks
      // such as hitlag SDI/ASDI remain visible to mpCheckFloor.
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpCheckFloor}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
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
        // Air-jump entry source owner:
        // every Fox/Falco `ftCo_JumpAerial_Enter_Basic` call routes through
        // `ftCommon_8007D5D4` before `Fighter_ChangeMotionState`, which locks CollData_X130 and
        // leaves the pre-entry `desired_ecb.bottom` live for the upcoming JumpAerial_Coll callback.
        // Most runtime entries use `locomotion_try_enter_jump_aerial_iasa`, which tags this owner
        // directly. This map-callback guard covers source-equivalent entry paths that materialize
        // JumpAerial outside that helper. Keep it keyed to the live Reflector-loop IASA marker so
        // ordinary air-jump EscapeAir rows do not inherit the preserved desired-bottom packet.
        //
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{
        //   ftCo_JumpAerial_Enter_Basic,ftCo_JumpAerial_Coll}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLwLoop_IASA
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
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
      // TODO: Once the current rollout burn-down stabilizes, fold this accumulating floor-owner
      // logic into a bounded collision-result packet. Floor sweep start provenance,
      // callback/source authority, suppress/reject reason, and final publication should be carried
      // as explicit fields instead of adding more scattered owner booleans here.
      const uint8_t escapeair_locked_jumpaerial_entry_desired_bottom_owner =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed != 0u &&
           seed_prev_action_is_jumpaerial &&
           (batch->state.pos_y[idx] > k_floor_y_bias ||
            batch->state.floor_sweep_prev_pos_y[idx] > k_floor_y_bias) &&
           batch->state.seed_prev_action_frame[idx] <= 2 &&
           (prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR ||
            batch->state.prev_action_frame[idx] <= 2))
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
      const uint8_t fresh_lr_edge = ((batch->state.input_buttons_pressed[idx] &
                                      (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R)) != 0u)
                                        ? 1u
                                        : 0u;
      // Same-frame JumpAerial -> EscapeAir IASA from a platform-domain floor can enter EscapeAir
      // before Fighter_procMap. In that entry-frame source path, ft_80082C74 calls mpCollPrev after
      // the action transition, so CollData.last_pos is the frame-start JumpAerial root and
      // CollData.cur_pos is the post-Phys EscapeAir root.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
      // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8}
      const uint8_t escapeair_jumpaerial_frame_start_owner =
          ((((ecb_lock_timer_seed != 0u &&
              (escapeair_locked_jumpaerial_entry_desired_bottom_owner ||
               stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]) ||
               stage_collision_floor_line_has_platform_transform(stage_id,
                                                                 batch->state.ground_id[idx])) &&
              prev_action_is_jumpaerial) ||
             (ecb_lock_timer_seed == 0u && batch->state.action_frame[idx] <= 1)) &&
            seed_prev_action_is_jumpaerial))
              ? 1u
              : 0u;
      // Late frame-start JumpF/B -> EscapeAir on a fresh shield edge keeps the frame-start
      // CollData root for the first EscapeAir_Coll mpCollPrev sweep while the source jump ECB lock
      // still has two frames remaining.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
      // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8}
      const uint8_t escapeair_late_jump_fresh_edge_owner =
          (ecb_lock_timer_seed == 2u && fresh_lr_edge != 0u && prev_action_is_jump &&
           seed_prev_action_is_jump && batch->state.seed_prev_action_frame[idx] == 6 &&
           batch->state.action_frame[idx] <= 1)
              ? 1u
              : 0u;
      // No-lock EscapeAir_Coll floor sweeps start from the frame-start CollData root, entry and
      // sustained frames alike: ft_80081D0C copies the previous callback's published
      // CollData.cur_pos into last_pos before writing the post-Phys root, and mpColl_80043754
      // sweeps from last_pos. The previous EscapeAir_Coll (or the pre-entry action's map callback
      // on the entry frame) published CollData.cur_pos as the corrected fighter root, so the
      // source sweep never starts from the older pre-physics root of the previous frame. Sweeping
      // from that older root both fabricates platform crossings on falling rows (prev bottom
      // still above the line) and misses waveland crossings on rising-then-falling rows (prev
      // bottom below the line trips mpLineIntersectionH's `b0y - a0y < -0.0001` reject).
      // While CollData_X130_Locked is live, `mpColl_LoadECB_inline` preserves the locked desired
      // bottom in both sweep endpoints; that locked family keeps its existing carried-root owners
      // until the locked prev/cur endpoint substrate replaces them.
      // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class3 PHASE4_ESCAPE_AIR_COLL)
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
      // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754,mpColl_800471F8,
      //   mpColl_LoadECB_inline}
      // refs/melee/src/melee/mp/mplib.c::mpLineIntersectionH
      const uint8_t escapeair_471f8_uses_frame_start_last_pos =
          ((msl_motion_state_class3_has(char_id, action_id, MSL_MS_CLASS3_PHASE4_ESCAPE_AIR_COLL) &&
            !ecb_lock_active && ecb_lock_timer_seed == 0u) ||
           (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            (escapeair_jumpaerial_frame_start_owner != 0u ||
             escapeair_late_jump_fresh_edge_owner != 0u)))
              ? 1u
              : 0u;
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
          // DamageFly_Coll calls ft_80081DD4, which copies CollData.cur_pos to last_pos before
          // writing the integrated fighter root into cur_pos and calling mpColl_800473CC. When the
          // callback-entry bottom is still above the carried floor, the current CollData root is the
          // authoritative sweep start and older floor_sweep_prev_pos state can synthesize a stale
          // DownBound. If the callback-entry bottom is already below the carried floor, keep the
          // older CollData floor-sweep root so source floor penetration/crossing rows still resolve.
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
      const float prev_x = escapeair_471f8_uses_frame_start_last_pos ? batch->state.prev_pos_x[idx]
                           : damagefly_473cc_uses_colldata_last_pos
                               ? batch->state.prev_pos_x[idx]
                               : batch->state.floor_sweep_prev_pos_x[idx];
      const float prev_y = escapeair_471f8_uses_frame_start_last_pos ? batch->state.prev_pos_y[idx]
                           : damagefly_473cc_uses_colldata_last_pos
                               ? batch->state.prev_pos_y[idx]
                               : batch->state.floor_sweep_prev_pos_y[idx];
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
        // The first live EscapeAir_Coll callback after JumpAerial entry can carry a hard-floor
        // producer even before the public root crosses the line. Require the current callback root
        // to have reached the carried desired-bottom floor envelope; later sustained EscapeAir
        // callbacks and restored seeds with only visible floor/root state cannot create this
        // runtime-only authority.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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

      // ECB bottom point for floor collision.
      // Decomp: mpLib_8004DD90_Floor and mpCheckFloor consume the ECB bottom point.
      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
      uint8_t lock_bottom_to_zero = was_grounded;
      if (action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
          action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) {
        // DownBound collision allows leaving/re-contacting ground while the action continues;
        // forcing ECB.bottom=0 for all previously-grounded snapshots suppresses that phase.
        // Keep pose-driven ECB bottom for DownBound to match the callback's ground/air behavior.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
        lock_bottom_to_zero = 0u;
      }
      const int escapeair_seed_floor_line_idx =
          stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
      const uint8_t escapeair_early_ledge_root_floor_owner =
          // Early sustained EscapeAir over a terminal-cardinal carried ledge floor uses the
          // ft_80082C74/mpColl_800471F8 root floor path once the replay-visible x2218
          // allow-interrupt bit is live. Platform-bearing stage graphs and mirrored controls
          // without that callback bit remain on their edge-suppression paths at this phase. Direct
          // one-step seeds may expose a preserved desired bottom here, but free rollout reaches the
          // same source callback through live CollData lifetime rather than a replay seed lane.
          // data/stages/bin/*.bin::MSLSTG01 line flags, links, and platform transform metadata
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
          // Throw release can enter DamageFly with an ECB-lock countdown still active, but the
          // next DamageFly_Coll floor pass uses the current DamageFly ECB source. Keeping the
          // generic ground->air zero-bottom carry here hides the floor crossing on low
          // release rows where CollData.floor.index is still valid.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_JObj}
          (is_damage_fly_collision_action(action_id) && ecb_lock_active &&
           batch->state.hitlag[idx] == 0u && batch->state.hitlag_pre_timer[idx] == 0u &&
           batch->state.action_frame[idx] <= 2 &&
           msl_action_is_thrown_victim(batch->state.seed_prev_action_id[idx]))
              ? 1u
              : 0u;
      const uint8_t escapeair_sustained_current_ecb_uses_pose_bottom =
          // `mpColl_LoadECB_inline` preserves desired_ecb.bottom while the lock bit is live, but
          // sustained EscapeAir callbacks reload/interpolate the current ECB from the EscapeAir
          // pose before `mpColl_80044628_Floor`. Keep the desired-bottom carry for later source
          // lock phases, but do not keep publishing the prior JumpAerial desired bottom as the
          // current ECB after the first EscapeAir entry callback has already run.
          //
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
           (escapeair_locked_jumpaerial_entry_desired_bottom_owner ||
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
        // ECB lock-bottom semantics while CollData_X130_Locked is active:
        // - ftCommon_8007D5D4 sets fp->ecb_lock and CollData_X130_Locked on ground->air transitions.
        // - mpColl_LoadECB_inline preserves desired_ecb.bottom while locked.
        // - Fall fastfall rows in `Fall_Coll` (`ft_800831CC` -> `mpColl_80047E14`), EscapeAir,
        //   AttackAir, Damage/DamageFly, and Fox/Falco aerial special callbacks can also resolve
        //   contact during this lock window. Jump/JumpAerial, non-fastfall Fall, MissFoot, and
        //   FallSpecial have distinct floor callback-phase owners and are not part of this retained
        //   floor slice.
        // - Shine narrows this to frame-start SpecialAirLwLoop/End owners; otherwise
        //   SpecialAirLwStart_Anim can change to Loop before collision and incorrectly inherit
        //   the loop/end floor-contact policy on the startup handoff frame.
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
        // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
        //   ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
        //   ftFx_SpecialAirLwStart_Anim,ftFx_SpecialAirLwLoop_Coll,ftFx_SpecialAirLwEnd_Coll}
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
        // Source-shaped current-ECB selection for fresh EscapeAir entries:
        // the persisted CollData state below owns the previous endpoint, while the current endpoint
        // still follows the just-loaded EscapeAir pose used by `ftCo_EscapeAir_Coll`.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
        // Source mpColl_LoadECB_inline preserves desired_ecb.bottom while the CollData lock bit is
        // live; top/side points still refresh from the current pose.
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
      // General no-lock EscapeAir entry prev-ECB lifetime:
      // any same-frame IASA -> EscapeAir entry reaches the map callback with the pre-entry
      // action's CollData current ECB still promoted into prev_ecb by mpCollInterpolateECB;
      // only the current/desired endpoint follows the just-loaded EscapeAir pose. Late jump
      // poses carry a high ECB bottom, so sampling the entered EscapeAir pose for the previous
      // endpoint drops the source sweep start below floors the pre-entry bottom was still above
      // (e.g. late JumpB wavelands onto the stage shell).
      // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
      // refs/melee/src/melee/mp/mpcoll.c::{
      //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor}
      const uint8_t escapeair_no_lock_entry_prev_ecb_lifetime =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && !ecb_lock_active &&
           ecb_lock_timer_seed == 0u && prev_action_id != action_id &&
           !escapeair_jumpaerial_prev_ecb_lifetime)
              ? 1u
              : 0u;
      const float pose_prev_ecb_rel =
          mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_prev, lock_bottom_to_zero);
      const float pre_entry_prev_ecb_rel =
          // Same-frame IASA -> EscapeAir still enters the map callback with the
          // pre-entry CollData current ECB as `prev_ecb`; source then loads/interpolates the
          // entered EscapeAir desired ECB before `mpColl_80044628_Floor`. Use the generated
          // MotionState submotion table for the pre-entry action instead of reusing the entered
          // EscapeAir pose, otherwise late platform crossings can miss the bottom-sweep owner.
          // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor}
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
        // Source `mpCollInterpolateECB` consumes x34_flags.b6 at callback interpolation time:
        // prev_ecb receives the squeezed current ecb, ecb is restored from x64_ecb, then b6 clears.
        // Clear before ordered producers run so a same-callback squeeze can save a fresh x64_ecb.
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
          // JumpAerial -> AttackAir/SpecialAirN entry callback-local ECB lifetime:
          // IASA can enter the aerial attack/special before the map callback, but the source
          // mpColl pass still promotes the carried JumpAerial CollData.ecb into prev_ecb before
          // loading/interpolating the entered action's desired ECB. Do not apply this to fresh
          // JumpF/JumpB -> AttackAir entries; those remain owned by the Jump collision substrate.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{
          //   ftCo_JumpAerial_IASA,ftCo_JumpAerial_Coll}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
          //   ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll}
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
          // Damage_IASA / Fall_IASA can enter AttackAir before Fighter_procMap, but
          // AttackAir_Coll still consumes the callback-local CollData current ECB promoted by
          // mpCollInterpolateECB before loading the entered AttackAir desired ECB. Use the
          // generated damage-owner tables for both DamageAir* and ground DamageHi/N/Lw source
          // families. Fighter_8006A360 advances the entered action before Fighter_procMap, so the
          // first map-callback pass is visible here as action_frame 2. Keep this as an entry-frame
          // CollData lifetime, not an AttackAir-wide floor shortcut.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
          //   ftCo_AttackAir_Enter,ftCo_AttackAir_Coll}
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor}
          (is_attackair_action(action_id) && batch->state.action_frame[idx] <= 2 &&
           (msl_damage_owner_is_damage_air_action(prev_action_id) ||
            msl_damage_owner_is_damage_air_action(batch->state.seed_prev_action_id[idx]) ||
            is_damage_ground_collision_action(prev_action_id) ||
            is_damage_ground_collision_action(batch->state.seed_prev_action_id[idx])))
              ? 1u
              : 0u;
      const uint8_t damage_entry_escapeair_ecb_consumer =
          // Damage/air-damage hitstun can enter EscapeAir before Fighter_procMap, but
          // EscapeAir_Coll uses the same CollData ordering as other air callbacks:
          // mpCollInterpolateECB first promotes the pre-entry current ECB into prev_ecb, then
          // loads/interpolates the entered EscapeAir ECB before mpColl_800471F8 asks
          // mpColl_80044628_Floor. Keep this to the first live entry callback and require the
          // explicit stored CollData ECB packet; visible ground_id/root/seeded previous action alone
          // is not floor-producer authority.
          //
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_800471F8,
          //   mpColl_80044628_Floor}
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
      // On EscapeAir entry-lifetime frames the CollData current-ECB lane can be live-stale:
      // last written on a long-past grounded frame with bottom rel exactly 0, which collapses
      // the prev bottom to the root so the entry-frame floor crossing is never seen (manual
      // repro: marth_still_airdodge_through_stage). A genuine airborne CollData bottom rel is
      // never exactly 0 (grounded collapses it; airborne poses keep the diamond above the
      // root), so the exact-zero lane while airborne marks staleness and defers to the
      // pre-entry pose sample. Seed-owned and rollout-evolved lanes carry real pose values
      // and keep winning. Stale NON-zero live lanes are covered by the last-resort EscapeAir
      // descending floor catch below in the apply pass.
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
      // CollData ECB lifetime substrate:
      // - mpColl_LoadECB_inline refreshes desired_ecb from the source ECB pose but preserves
      //   desired.bottom while CollData_X130_Locked is set.
      // - mpCollInterpolateECB first copies current ecb into prev_ecb, then moves ecb toward
      //   desired_ecb for the callback substep.
      // Runtime carries those three ECB point sets explicitly so EscapeAir/Fall/Jump callbacks ask
      // the same collision substrate instead of resampling previous motion rows locally.
      // refs/melee/src/melee/mp/mpcoll.c::{
      //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80043754}
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
          // Source CommonFall collision order:
          // mpCollInterpolateECB first promotes the previous callback's blended current ECB into
          // prev_ecb, then ftCo_Fall_Anim_Inner's selected submotion/blend publishes the current
          // ECB consumed by mpColl_80044628_Floor. Direct one-step reseed rows may only use this
          // lane when seed generation published the previous callback's CollData; after a replay
          // rollout advances past its reseed frame, the same live
          // mv.co.{fall,fallaerial,fallspecial}.x4/smid state is runtime-owned and must also
          // publish the blended current/desired ECB instead of the raw neutral
          // Fall/FallAerial/FallSpecial pose.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
          //   ftCo_Fall_Anim_Inner,ftCo_Fall_Coll}
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor}
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
          // During active Damage hitlag, the replay-visible Damage action can advance while source
          // CollData still carries the pre-hit JObj collision envelope. Use that hidden envelope for
          // both current and desired ECB in the frozen callback; otherwise mpColl_80044628_Floor can
          // accept/reject floors from a pose the source collision pass has not loaded yet.
          //
          // Dolphin probe evidence:
          // reports/triage/active_damage_agn794_forensic/rows/engine_dump_rows.txt
          //
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procMap}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
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

      // Collision env flags (subset) for Parity Project #2 (ledge grab mask parity).
      // Decomp: CollData carries env_flags and prev_env_flags across frames.
      // refs/melee/src/melee/lb/types.h::CollData
      batch->state.coll_prev_env_flags[idx] = batch->state.coll_env_flags[idx];
      batch->state.coll_env_flags[idx] = 0;
      if (was_grounded || !is_damage_collision_landing_action(action_id) ||
          (batch->state.hitlag[idx] == 0u && batch->state.hitlag_pre_timer[idx] == 0u)) {
        batch->state.damage_hitlag_floorhug_latch[idx] = 0u;
      }
      MslMpcollCollDataState coll_data = {0};
      mpcoll_colldata_state_load(&mpcoll_ctx, &coll_data, source_phases);

      // Contact persistence:
      // - CollData carries floor.index across frames and uses the line graph to traverse seams.
      // - Grounded resolution uses a per-line projection (mpLib_8004DD90_Floor) rather than
      //   reselecting from scratch each frame.
      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DD7C (example of floor.index persistence)
      uint8_t on_ground = 0;
      uint8_t floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE;
      const uint16_t seed_ground_id = coll_data.floor_index;
      uint16_t ground_id = seed_ground_id;

      float floor_nx = 0.0f;
      float floor_ny = 1.0f;
      float contact_x = cur_bottom_x;
      float contact_y = 0.0f;
      uint8_t escapeair_locked_platform_root_projection_hit = 0u;
      uint8_t escapeair_stale_platform_root_handoff_hit = 0u;
      uint8_t escapeair_fresh_jump_height_platform_handoff_hit = 0u;
      uint8_t escapeair_no_lock_static_platform_sweep_hit = 0u;
      const uint8_t escapeair_terminal_locked_static_platform_sweep_owner =
          // Terminal EscapeAir_Coll after CollData_X130 expiry:
          // the public lock countdown reaches zero before the callback, so current/desired ECB use
          // the ordinary EscapeAir pose. Source `ft_80082C74 -> mpColl_800471F8` still consumes the
          // previous callback's preserved desired-bottom packet only on the terminal seed frame,
          // when frame-start state carries an explicit locked-bottom owner and a source-owned
          // previous root.
          //
          // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
          // refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_LoadECB_inline,
          //   mpColl_800471F8,mpColl_80044628_Floor}
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
              // Replay seeds can carry a raw/non-fighter Yoshi line while the live support owner is
              // Randall's generated path-transformed floor. Use the generated line as CollData's
              // current floor for map-collision decisions; output shape remains governed by the
              // normal floor-publication path below.
              // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,Ground_801C2FE0}
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
          // Source cliff/CollData floor owner:
          // CliffCatch/CliffWait store `mv.co.cliff.ledge_id`; release/drop sets
          // fp->x2064_ledgeCooldown, and the following air collision wrappers branch on that
          // timer. Immediate cliff exits carry the floor owner through Fall/JumpAerial/EscapeAir
          // collision callbacks while the timer is live, while Slippi-visible lastGroundId may
          // still name a stale platform, same-side ledge, or wrong-side ledge.
          // Prefer the hidden cliff floor only when the visible floor owner is missing or still a
          // ledge/platform owner and the current root is strictly inside the carried ledge span. A
          // stale/wrong-side seed lane must not remap through mpLib endpoint projection to an
          // unrelated ledge, and the later publication path still requires the source bottom/root
          // producer to accept this carried floor.
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
          // Live CliffWait drop/release already has source-owned `mv.co.cliff.ledge_id`, even when
          // replay-visible CollData.floor.index still names the same cliff floor. Direct one-step
          // seed rows use explicit seeded cliff-floor reconstruction and must not turn every
          // current-line seed into a late EscapeAir root owner.
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
      const MslMpcollDamageActiveHitlagFloorOwner damage_active_hitlag_floor_owner =
          mpcoll_damage_active_hitlag_floor_owner(
              batch, idx, action_id, (uint8_t)(prefer_line_idx >= 0 && prefer_line_root_y_valid),
              prefer_line_is_platform, prefer_line_is_ledge, prefer_line_is_slope,
              prefer_line_has_platform_transform, prefer_line_is_fighter_solid,
              prefer_line_is_terminal_cardinal_hard_floor, prefer_line_root_y, prev_y,
              downdamage_x_axis_fresh_sdi_edge);
      const uint8_t damage_active_hitlag_downward_sdi_airborne_owner =
          (damage_active_hitlag_floor_owner.stay_airborne_floorhug &&
           damage_active_hitlag_floor_owner.source_floor_current)
              ? 1u
              : 0u;
      uint8_t damage_active_hitlag_root_below_bottom_above_floor_owner = 0u;
      if (batch->state.hitlag[idx] != 0u && is_damage_fly_collision_action(action_id) &&
          batch->state.tilt_timer_y_frame_start[idx] >= c->sdi_tilt_max_frames &&
          prefer_line_idx >= 0 && !prefer_line_is_platform && !prefer_line_is_ledge) {
        // DamageFly active-hitlag can move the root below the carried floor through
        // ftCo_Damage_OnEveryHitlag on a same-frame downward stick edge. When x671 was not already
        // in the Y-window at frame start and the ECB bottom remains above the floor, source mpColl
        // does not publish the stay-airborne floor projection until mpColl_80044628_Floor accepts
        // the current ECB bottom; keep the SDI-mutated root for that first below-root frame.
        // Existing carried-Y-window DamageFly rows remain on the FloorPush/FloorHug owner above.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
        // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
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
        // Source grounded inline2 ordering runs wall and ceiling collision before the floor pass.
        // Keep the scratch/result in the shared wall/ceiling helper so the floor pass can consume
        // same-frame wall side bits for mpColl_80044628_Floor.
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
          on_ground = 1u;
          ground_id = g->lines[(size_t)out_line_idx].segment_i;
          contact_x = x;
          contact_y = y + y_corr - k_floor_y_bias;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
              (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION, ground_id, contact_x, contact_y,
              floor_nx, floor_ny);
        } else {
          float ledge_line_y = 0.0f;
          if (floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, x, &ledge_line_y) &&
              prev_y > (ledge_line_y + k_floor_y_bias) && y <= ledge_line_y) {
            batch->state.pos_y[idx] = ledge_line_y + k_floor_y_bias;
            on_ground = 1u;
            ground_id = g->lines[(size_t)prefer_line_idx].segment_i;
            contact_x = x;
            contact_y = ledge_line_y;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION, ground_id, contact_x, contact_y,
                floor_nx, floor_ny);
          }
        }
      }

      if (!was_grounded && !on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          mpcoll_ground_escapeair_fall_iasa_source_owner(batch, idx) &&
          batch->state.action_frame[idx] <= 1 &&
          mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx) && prefer_line_idx >= 0 &&
          (size_t)prefer_line_idx < g->line_count) {
        // Same-frame Fall -> EscapeAir carried ledge endpoint floor handoff:
        // when Fall IASA enters EscapeAir before Fighter_procMap, source EscapeAir_Coll consumes
        // CollData.prev/cur roots through `mpColl_800471F8 -> mpColl_80044628_Floor`. FD-style
        // terminal ledge rows can start exactly on the floor endpoint; mpLineIntersectionH admits
        // a small +/-0.1 endpoint overshoot and clamps the hit to the endpoint. Keep this path to a
        // live runtime-owned previous root, the same carried fighter-solid cardinal ledge floor,
        // and an actual above/on-floor to below-floor crossing. Restored visible ground_id or
        // JumpAerial/KneeBend entries cannot enter this owner.
        //
        // data/stages/bin/*.bin::MSLSTG01 ledge floor flags + source endpoints
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
          on_ground = 1u;
          ground_id = carried_line->segment_i;
          contact_x = x;
          contact_y = cur_floor_y;
          floor_nx = 0.0f;
          floor_ny = 1.0f;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
              (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id, contact_x, contact_y,
              floor_nx, floor_ny);
        }
      }

      if (!was_grounded && !on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          prev_action_id != action_id && prev_action_is_jumpaerial && prefer_line_idx >= 0 &&
          batch->state.seed_prev_action_frame[idx] <= 2 && batch->state.action_frame[idx] <= 2) {
        // Fresh JumpAerial -> EscapeAir ledge bottom-sweep handoff:
        // ftCo_JumpAerial_IASA can enter EscapeAir before Fighter_procMap, then
        // EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8 samples frame-start CollData.ecb and
        // post-Phys EscapeAir cur_pos. The source mpCheckFloor test is a segment sweep from the
        // pre-entry JumpAerial ECB bottom to the current EscapeAir bottom. Treat fighter-solid
        // static ledge lines from MSLSTG01 as normal floor candidates here; the owner is the live
        // bottom sweep plus line metadata, not a stage-shape exception.
        // data/stages/bin/*.bin::MSLSTG01 ledge floor segments + prev/next links
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
          floor_nx = floor_sweep.normal_x;
          floor_ny = floor_sweep.normal_y;
          batch->state.pos_y[idx] = hit_y + k_floor_y_bias;
          on_ground = 1u;
          ground_id = g->lines[(size_t)hit_line_idx].segment_i;
          contact_x = hit_x;
          contact_y = hit_y;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_sweep.mode, ground_id,
              contact_x, contact_y, floor_nx, floor_ny);
        }
      }

      uint8_t escapeair_missing_bottom_hard_floor_sweep_owner = 0u;
      if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          prefer_line_has_platform_transform && prefer_line_idx >= 0 &&
          batch->state.speed_y_self[idx] < 0.0f) {
        // Live EscapeAir over a transformed platform:
        // ftCo_80099A58 can enter EscapeAir before the map callback, then every sustained
        // EscapeAir_Coll runs the normal mpColl_800471F8 floor producer. Source mpCheckFloor scans
        // all active floor lines; a carried transformed-platform floor.index must not stop that
        // live callback from accepting the hard floor crossed by the callback-local EscapeAir bottom.
        //
        // data/stages/bin/*.bin::MSLSTG01 platform_transform metadata
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
        //   ftCo_80099A58,ftCo_EscapeAir_Coll}
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
          on_ground = 1u;
          ground_id = g->lines[(size_t)line_idx].segment_i;
          contact_x = transformed_platform_entry_hard_floor_sweep.hit_x;
          contact_y = transformed_platform_entry_hard_floor_sweep.hit_y;
          floor_nx = transformed_platform_entry_hard_floor_sweep.normal_x;
          floor_ny = transformed_platform_entry_hard_floor_sweep.normal_y;
          escapeair_missing_bottom_hard_floor_sweep_owner = 1u;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
              transformed_platform_entry_hard_floor_sweep.mode, ground_id, contact_x, contact_y,
              floor_nx, floor_ny);
        }
      }
      if (!was_grounded && !on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          (stage_has_only_static_cardinal_hard_floors || stage_has_height_platform_transform) &&
          prefer_line_idx >= 0 && ecb_lock_timer_seed != 0u &&
          (msl_escapeair_locked_bottom_owner_any(
               batch->state.coll_desired_ecb_bottom_locked_owner[idx]) ||
           batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR ||
           batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias) &&
          batch->state.action_frame[idx] <= 3 && batch->state.speed_y_self[idx] < 0.0f) {
        // Early locked EscapeAir hard-floor handoff:
        // ftCo_EscapeAir_Coll enters through ft_80082C74/mpColl_800471F8. Runtime rollouts can
        // reach the first sustained EscapeAir callback without a replay-seeded desired-bottom lane,
        // but the source callback still loads the entered EscapeAir ECB and can publish
        // LandingFallSpecial when the zeroed/desired bottom or root-crossing point sweeps onto the
        // carried hard-floor chain. Keep this retained slice to the generated static-cardinal
        // hard-floor owner plus the height-platform raised hard-floor cases validated here;
        // platform-bearing ledge graphs still have adjacent zero-bottom/root-suppression differences
        // and stay on the narrower root-crossing owner below when the active lock is known but no
        // desired-bottom lane was serialized.
        // Direct replay-seeded rows with no active CollData lock stay on their explicit
        // desired/current ECB publication path instead of borrowing the locked handoff.
        //
        // data/stages/bin/*.bin::MSLSTG01 floor segment links
        // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
        const float escapeair_bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
        if (prefer_line_has_platform_transform) {
          MslMpcollFloorSweepResult transformed_platform_to_hard_floor_sweep = {0};
          if (mpcoll_collect_bottom_sweep_hit(
                  batch, idx, bi, g, stage_id, prev_x, prev_y + escapeair_bottom_rel0, x,
                  y + escapeair_bottom_rel0, skip_platform_segment_i, -1, -1, c,
                  &transformed_platform_to_hard_floor_sweep) &&
              transformed_platform_to_hard_floor_sweep.hit_line_idx >= 0 &&
              !transformed_platform_to_hard_floor_sweep.hit_is_platform &&
              !transformed_platform_to_hard_floor_sweep.hit_has_platform_transform) {
            // mpCheckFloor scans all active floor lines; a carried transformed-platform floor.index
            // must not restrict EscapeAir_Coll to connected platform candidates when the live
            // callback bottom/root sweep reaches the static hard-floor shell.
            //
            // data/stages/bin/*.bin::MSLSTG01 platform_transform metadata
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
            const int line_idx = transformed_platform_to_hard_floor_sweep.hit_line_idx;
            batch->state.pos_y[idx] =
                transformed_platform_to_hard_floor_sweep.hit_y + k_floor_y_bias;
            on_ground = 1u;
            ground_id = g->lines[(size_t)line_idx].segment_i;
            contact_x = transformed_platform_to_hard_floor_sweep.hit_x;
            contact_y = transformed_platform_to_hard_floor_sweep.hit_y;
            floor_nx = transformed_platform_to_hard_floor_sweep.normal_x;
            floor_ny = transformed_platform_to_hard_floor_sweep.normal_y;
            escapeair_missing_bottom_hard_floor_sweep_owner = 1u;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                transformed_platform_to_hard_floor_sweep.mode, ground_id, contact_x, contact_y,
                floor_nx, floor_ny);
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
        for (size_t ci = 0; ci < 5 && !on_ground; ci++) {
          const int line_idx = candidate_lines[ci];
          if (line_idx < 0 || (size_t)line_idx >= g->line_count ||
              g->lines[(size_t)line_idx].is_platform ||
              stage_collision_floor_line_has_platform_transform(
                  stage_id, g->lines[(size_t)line_idx].segment_i)) {
            continue;
          }
          if (stage_has_height_platform_transform && g->lines[(size_t)line_idx].is_ledge &&
              (!floor_x_within_line_bounds(batch, bi, g, line_idx,
                                           batch->state.floor_sweep_prev_pos_x[idx]) ||
               !floor_x_within_line_bounds(batch, bi, g, line_idx, x))) {
            // FoD ledge-floor publication in this early locked EscapeAir path is an in-span
            // callback floor continuation. Endpoint entry from outside the ledge segment is owned
            // by the fresh JumpAerial -> EscapeAir bottom-sweep handoff above; other rows remain
            // airborne until a real bottom/root crossing reaches an in-span source floor result.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
            continue;
          }
          float line_y = 0.0f;
          if (!floor_line_y_at_x_ed5c_for_env(batch, bi, g, line_idx, x, &line_y)) {
            continue;
          }
          const uint8_t escapeair_early_root_crossing =
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
          const uint8_t escapeair_bottom_crossing =
              // Bottom-sweep publication must consume a real CollData desired-bottom owner.
              // JumpAerial -> EscapeAir zero-bottom entries are handled by the root projection
              // owner below; using the pose bottom here lands shallow Battlefield ledge exits one
              // callback early.
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
              (msl_escapeair_locked_bottom_owner_any(
                   batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
               prev_y + escapeair_bottom_rel0 > line_y + k_floor_y_bias &&
               y + escapeair_bottom_rel0 <= line_y + k_floor_y_bias)
                  ? 1u
                  : 0u;
          if (escapeair_bottom_crossing || escapeair_early_root_crossing) {
            batch->state.pos_y[idx] = line_y + k_floor_y_bias;
            on_ground = 1u;
            ground_id = g->lines[(size_t)line_idx].segment_i;
            contact_x = x;
            contact_y = line_y;
            floor_nx = 0.0f;
            floor_ny = 1.0f;
            escapeair_missing_bottom_hard_floor_sweep_owner = 1u;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                (uint8_t)(escapeair_bottom_crossing ? MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP
                                                    : MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION),
                ground_id, contact_x, contact_y, floor_nx, floor_ny);
          }
        }
      }

      if (!was_grounded && !on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          !stage_has_only_static_cardinal_hard_floors && prefer_line_idx >= 0 &&
          ecb_lock_timer_seed != 0u &&
          !msl_escapeair_locked_bottom_owner_any(
              batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          batch->state.action_frame[idx] <= 3 && batch->state.speed_y_self[idx] < 0.0f) {
        // Non-FD replay-prefix gap for early locked EscapeAir floor handoff:
        // the source callback is still `ftCo_EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8`,
        // but some legal-stage direct seeds expose an active CollData_X130_Locked timer and
        // callback-local previous root without the desired-bottom replay lane. On non-ledge
        // hard-floor chains, a previous-root -> current-root crossing is enough for
        // mpColl_80044838_Floor to publish LandingFallSpecial. Static-cardinal hard-floor stages
        // retain the older desired-bottom/locked-owner branch above because their rollout locks
        // already cover that source shape and owner-zero free-running states are a different
        // hidden-state gap.
        // FoD ledge floors use the same owner only when both the previous and current callback roots
        // are inside the ledge span; endpoint entry from outside remains airborne.
        // data/stages/bin/*.bin::MSLSTG01 floor segment links
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
        for (size_t ci = 0; ci < 5 && !on_ground; ci++) {
          const int line_idx = candidate_lines[ci];
          if (line_idx < 0 || (size_t)line_idx >= g->line_count ||
              g->lines[(size_t)line_idx].is_platform ||
              stage_collision_floor_line_has_platform_transform(
                  stage_id, g->lines[(size_t)line_idx].segment_i)) {
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
          if (line_is_ledge && !ledge_span_owner) {
            continue;
          }
          if (!line_is_ledge && !floor_x_within_line_bounds(batch, bi, g, line_idx, x)) {
            continue;
          }
          float line_y = 0.0f;
          if (!floor_line_y_at_x_for_env(batch, bi, g, line_idx, x, &line_y)) {
            continue;
          }
          if (prev_y > line_y + k_floor_y_bias && y <= line_y + k_floor_y_bias) {
            batch->state.pos_y[idx] = line_y + k_floor_y_bias;
            on_ground = 1u;
            ground_id = g->lines[(size_t)line_idx].segment_i;
            contact_x = x;
            contact_y = line_y;
            floor_nx = 0.0f;
            floor_ny = 1.0f;
            escapeair_missing_bottom_hard_floor_sweep_owner = 1u;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION, ground_id, contact_x, contact_y,
                floor_nx, floor_ny);
          }
        }
      }

      const uint8_t common_damage_post_unlock_pose_bottom_owner =
          // Sustained airborne DamageHi/N/Lw after CollData_X130 unlock:
          // `ftCommon_8007D5D4` starts the ground-to-air common Damage episode with a zero-bottom
          // ECB lock, but `Fighter_procMap` decrements and clears CollData_X130 before later
          // `ftCo_Damage_Coll -> ft_80081DD4 -> mpColl_LoadECB_inline(flags=6)` callbacks. Once
          // the same Damage episode reaches a post-decrement lock counter of zero, source collision
          // consumes the generated Damage pose bottom instead of the stale zero-bottom/root handoff
          // still present in CollData.floor. Require live runtime floor-sweep provenance so
          // teacher-forced one-step seeds keep their existing replay-owned CollData endpoint.
          //
          // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
          // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_UnlockECB}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
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
              // DownBound_Coll uses the allow-ground-to-air floor persistence path. When a grounded
              // DownBound root traverses generated legal-stage slopes, source keeps the root pinned
              // to the persisted floor line until `mpColl_8004B108` reports floor loss; projecting
              // the positive DownBound ECB bottom instead leaves the root stranded below the slope.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
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
              // mpLib_8004DD90_Floor returns the floor line reached after prev/next traversal at a
              // seam. When a grounded callback moves from a generated legal-stage slope onto a flat
              // floor, the final root height belongs to the returned flat floor, not the slope
              // extrapolated past its endpoint.
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
            // The transformed FoD platform floor can be current source authority for a grounded
            // action-entry callback, but not for sustained same-action grounded motion. Without
            // this boundary, a stale sparse seed bit can make Dash/Run rows replace their current
            // floor index with the opposite height platform for one step.
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
            // LandingFallSpecial grIzumi height-platform source rows can already carry the
            // callback-current transformed floor root in CollData. Reapplying the generic
            // mpLib_8004DD90 +0.0001 bias on that retained source slice double-lifts it and can
            // later miss the DamageFly landing threshold. Do not apply this to ordinary
            // Landing/LandingAir rows: their source callback still publishes the normal
            // mpLib_8004DD90_Floor floor bias on the transformed platform. The stage source mask is
            // transient; the frame scheduler clears seed provenance after the frame, so later
            // rollout rows must be reasserted by live scheduler/contact ownership before this bypass
            // can apply.
            //
            // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
            y_corr = 0.0f;
          }
          if (is_capture_lw_allow_ground_to_air_collision_action(action_id) &&
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_STAND_B &&
              batch->state.action_frame[idx] <= 2 && resolved_line_idx != prefer_line_idx &&
              prefer_line_idx >= 0 && (size_t)prefer_line_idx < g->line_count &&
              g->lines[(size_t)prefer_line_idx].is_ledge &&
              !stage_collision_floor_line_is_sloped(stage_id,
                                                    g->lines[(size_t)prefer_line_idx].segment_i)) {
            const uint8_t owner_p = batch->state.grab_owner_port[idx];
            uint8_t owner_on_different_floor = 0u;
            if (owner_p < batch->config.num_players) {
              const size_t oidx = msl_idx_player(bi, (int)owner_p);
              owner_on_different_floor =
                  (uint8_t)(batch->state.action_id[oidx] == (uint16_t)MSL_ACT_CATCH_DASH_PULL &&
                            g->lines[(size_t)prefer_line_idx].joint_id == 0 &&
                            batch->state.on_ground[oidx] != 0u &&
                            batch->state.ground_id[oidx] !=
                                g->lines[(size_t)prefer_line_idx].segment_i);
            }
            if (owner_on_different_floor != 0u) {
              // CapturePulledLw flat-ledge CollData floor carry:
              // after fn_800DAD18 translates the victim, ft_8008403C/mpColl_8004B108 can keep the
              // stale CollData.floor.index for flat ledge floors instead of traversing to the
              // adjacent main floor. Do not apply this to same-floor sloped ledge carries; those
              // are the source floor-loss PulledHi path handled in grab_attachment.c.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
              //   ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll}
              // refs/melee/src/melee/ft/ft_081B.c::ft_8008403C
              // data/stages/bin/*.bin::MSLSTG01 segment.{ledge,endpoints}
              resolved_line_idx = prefer_line_idx;
              y_corr =
                  (g->lines[(size_t)prefer_line_idx].y0 + k_floor_y_bias) - batch->state.pos_y[idx];
              mpcoll_record_callback_floor_result_with_mode(
                  &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                  (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION,
                  g->lines[(size_t)prefer_line_idx].segment_i, cur_bottom_x,
                  g->lines[(size_t)prefer_line_idx].y0, 0.0f, 1.0f);
            }
          }
          const uint8_t keep_grounded_damage_hitlag_floor_snap =
              grounded_damage_hitlag_allows_downward_floor_projection(batch, idx, action_id);
          const uint8_t keep_capture_lw_floor_snap =
              is_capture_lw_allow_ground_to_air_collision_action(action_id);
          const uint8_t keep_slope_or_platform_floor_snap =
              grounded_persistence_allows_signed_dd90_y_correction(
                  batch, bi, g, prefer_line_idx, resolved_line_idx, idx, action_id,
                  batch->state.action_frame[idx]);
          // mpLib_8004DD90_Floor returns a signed correction; for stable grounded frames we only
          // need to resolve penetration. If we are already above the floor due to callback-local
          // projection drift, avoid snapping down in the collision substrate.
          //
          // Exception: grounded damage hitlag rows let `ftCo_Damage_OnEveryHitlag` move `cur_pos`
          // before grounded `ftCo_Damage_Coll` re-pins the fighter to floor through
          // `ft_800848DC -> ft_80082708 -> mpColl_8004B108`. Keep the downward correction only
          // for that owner path; the general grounded anti-snap clamp stays in place elsewhere.
          // Exception: low capture states call the allow-ground-to-air collision wrapper after
          // `fn_800DAD18`; it owns re-projecting the attached grounded victim back onto the floor.
          if (y_corr < 0.0f && !keep_grounded_damage_hitlag_floor_snap &&
              !keep_capture_lw_floor_snap && !keep_slope_or_platform_floor_snap &&
              !down_bound_slope_root_snap && !hidden_height_platform_remapped_to_solid_floor) {
            y_corr = 0.0f;
          }
          // Source floor-index traversal:
          // mpLib_8004DD90_Floor returns the projected line after following connected floor
          // prev/next links at seams. Keep that returned line for grounded callbacks, including
          // same-frame Dash entries through ft_800844EC -> ft_80082708 -> mpColl_8004B108; the
          // previous forced seed-floor owner for Dash entry was too broad at
          // legal-stage seam edges.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_800844EC,ft_80082708}
          // refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B108,mplib.c::mpLib_8004DD90_Floor}
          batch->state.pos_y[idx] += y_corr;
          on_ground = 1;
          floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION;
          ground_id = g->lines[(size_t)resolved_line_idx].segment_i;
          contact_y = (cur_bottom_y + y_corr);
        } else {
          // Decomp shape:
          // - Grounded collision uses mpLib_8004DD90_Floor to project onto the current floor line,
          //   but on failure it can still (a) snap to the current floor edge (mpColl_8004A45C_Floor,
          //   used by mpColl_8004B2DC) and/or (b) detect a floor hit via the swept segment test
          //   (mpCheckFloor-style) before concluding the fighter is airborne.
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
          //
          // This matters for downed rolls near the FD ledge: the integrated position can move
          // past the floor endpoint, but the motion segment still intersects the floor line and
          // the engine clamps the contact to the intersection point before leaving ground.
          uint8_t snapped_edge = 0;
          const MslStageFloorLine world_line =
              floor_line_world_for_env(batch, bi, g, prefer_line_idx);
          const MslStageFloorLine* l = &world_line;
          const float left_x = l->x0;
          const float left_y = l->y0;
          const float right_x = l->x1;
          const float right_y = l->y1;

          // mpColl_8004A45C_Floor: when the fighter passes beyond the current floor endpoint,
          // mpColl can snap the position to the edge point (if not blocked by a wall probe) while
          // keeping the fighter grounded for this collision result.
          if (mpcoll_source_phases_allow_floor_edge_snap(coll_data.source_phases)) {
            if (cur_bottom_x <= left_x) {
              const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
              MslEcbWorldPoints ecb = {0};
              msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, left_x, left_y,
                                          was_grounded);
              const float probe_ax = left_x + k_floor_edge_wall_probe_x_offset;
              const float probe_ay = left_y + k_floor_edge_wall_probe_y_offset;
              const float probe_bx = left_x + (ecb.right_rel_x /* bottom.x == 0 */);
              const float probe_by = left_y + (ecb.side_rel_y - ecb.bottom_rel_y);
              const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
              if (!wall_blocks_floor_edge_probe(lwg, probe_ax, probe_ay, probe_bx, probe_by)) {
                int out_line_idx2 = msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, left_x,
                                                             left_y, NULL, &floor_nx, &floor_ny);
                if (out_line_idx2 < 0) {
                  out_line_idx2 = prefer_line_idx;
                }
                batch->state.pos_x[idx] += (left_x - cur_bottom_x);
                batch->state.pos_y[idx] = left_y;
                on_ground = 1;
                floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_EDGE_SNAP;
                ground_id = g->lines[(size_t)out_line_idx2].segment_i;
                contact_x = left_x;
                contact_y = left_y;
                snapped_edge = 1;
              }
            } else if (cur_bottom_x >= right_x) {
              const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
              MslEcbWorldPoints ecb = {0};
              msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, right_x, right_y,
                                          was_grounded);
              const float probe_ax = right_x - k_floor_edge_wall_probe_x_offset;
              const float probe_ay = right_y + k_floor_edge_wall_probe_y_offset;
              const float probe_bx = right_x + (ecb.left_rel_x /* bottom.x == 0 */);
              const float probe_by = right_y + (ecb.side_rel_y - ecb.bottom_rel_y);
              const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);
              if (!wall_blocks_floor_edge_probe(rwg, probe_ax, probe_ay, probe_bx, probe_by)) {
                int out_line_idx2 = msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, right_x,
                                                             right_y, NULL, &floor_nx, &floor_ny);
                if (out_line_idx2 < 0) {
                  out_line_idx2 = prefer_line_idx;
                }
                batch->state.pos_x[idx] += (right_x - cur_bottom_x);
                batch->state.pos_y[idx] = right_y;
                on_ground = 1;
                floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_EDGE_SNAP;
                ground_id = g->lines[(size_t)out_line_idx2].segment_i;
                contact_x = right_x;
                contact_y = right_y;
                snapped_edge = 1;
              }
            }
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
            // Decomp: mpCheckFloor's horizontal intersection helper is gated on non-rising segments
            // (ay >= by), so equality must be allowed (horizontal motion with vy==0 can still sweep).
            // refs/melee/src/melee/mp/mplib.c::mpCheckFloor (the `if (ay >= by && mpLineIntersectionH(...))` gate)
            const uint8_t can_sweep =
                (uint8_t)(shared_sweep_cur_bottom_y <= shared_sweep_prev_bottom_y);
            const int floor_sweep_skip_line_idx =
                // mpCheckFloor checks the current CollData.floor.index. The generic lite-sim pass
                // skips the preferred line because ordinary floor.index projection has already
                // tried it above, but the teacher-forced cliff floor owner is restored only for this
                // air-callback floor sweep. Include it so the shared CollData owner consumes the
                // same ledge floor rather than relying on a downstream EscapeAir exception.
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
              // Decomp: desired_ecb.bottom.x is always 0.0, so clamping the ECB bottom contact X
              // corresponds to clamping the fighter position X.
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
              const uint16_t resolved_segment_i = floor_sweep.projected_segment_id;
              const uint8_t resolved_line_has_platform_transform =
                  floor_sweep.projected_has_platform_transform;
              const uint8_t resolved_line_has_height_platform_transform =
                  floor_sweep.projected_has_height_platform_transform;
              const uint8_t resolved_line_is_platform = floor_sweep.projected_is_platform;
              const uint8_t resolved_line_is_ledge = floor_sweep.projected_is_ledge;
              const uint8_t escapeair_entry_locked_platform_airborne =
                  // EscapeAir's entry/locked ECB path keeps early transformed-platform crossings
                  // airborne in the source callback until the locked collision snapshot can resolve
                  // normally. Use the mpLib-projected line id here: FoD's source graph can sweep one
                  // platform record then remap to another.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80047E14}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 4 && hit_line_is_platform &&
                   (hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   (ecb_lock_active || (prev_action_id == action_id &&
                                        resolved_segment_i != batch->state.ground_id[idx])))
                      ? 1u
                      : 0u;
              const uint8_t escapeair_jumpaerial_entry_ledge_airborne =
                  // Fresh JumpAerial -> EscapeAir can enter the map callback with a locked
                  // pre-entry ECB while mpLib's floor projection walks from the carried hard floor
                  // to the adjacent ledge floor. Source `EscapeAir_Coll` still consumes the
                  // JumpAerial CollData lifetime on this first callback, so ledge-floor remaps stay
                  // airborne until the entered EscapeAir row is published. Keep transformed and
                  // ordinary soft-platform contacts on their explicit owners; this guard is only the
                  // generated ledge-floor branch of the same pre-entry lifetime.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
                  //   ftCo_80099A58,ftCo_EscapeAir_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
                  (escapeair_jumpaerial_entry && ecb_lock_active &&
                   batch->state.seed_prev_action_frame[idx] >= 2 &&
                   batch->state.seed_prev_action_frame[idx] <= 4 &&
                   (hit_line_is_ledge || resolved_line_is_ledge))
                      ? 1u
                      : 0u;
              const uint8_t specialhi_fall_understage_hard_floor_clip =
                  // SpecialAirHi_Coll/SpecialHiFall_Coll reach ft_CheckGroundAndLedge through the
                  // same special-hi mpColl owner. If the previous root is still below the accepted
                  // hard floor by more than the live ECB
                  // neighborhood, this is not a legitimate top-surface landing; it is the sim having
                  // missed the earlier wall/ceiling owner and then accepting the floor from inside
                  // the stage on descent.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
                  (specialhi_understage_floor_clip_action(batch->state.char_id[idx], action_id) &&
                   !hit_line_is_platform && !resolved_line_is_platform &&
                   prev_y < (iy - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                             k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t specialhi_from_below_hard_floor_clip =
                  // mpColl_80044628_Floor's floor owner is an above-to-floor ECB-bottom crossing.
                  // If both callback-local bottom endpoints and the previous root already start
                  // below the accepted hard floor while SpecialAirHi/SpecialHiFall is descending,
                  // an upward root projection would rebound from the stage interior rather than
                  // source `ft_CheckGroundAndLedge` contact.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
                  (specialhi_floor_candidate_starts_below_source_floor(
                       batch->state.char_id[idx], action_id, prev_y, prev_bottom_y, cur_bottom_y,
                       iy, batch->state.speed_y_self[idx]) &&
                   !hit_line_is_platform && !resolved_line_is_platform)
                      ? 1u
                      : 0u;
              const float transformed_platform_bottom_penetration = iy - cur_bottom_y;
              const uint8_t damage_transformed_platform_offspan_contact =
                  // mpCheckFloor can report a transformed-platform endpoint contact from the ECB
                  // bottom sweep, but source floor handoff still requires the callback-local root
                  // to be in the live floor span before ftCo_80090184 may publish Passive/DownBound.
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
                  // FoD height-transform platform DamageFly endpoint contacts still need the live
                  // grIzumi platform/CollData substep owner. `ft_80081DD4` sets
                  // coll->ledge_snap_height before calling `mpColl_800473CC`; use the extracted
                  // character ledge-snap height as the endpoint bound instead of a replay-local x
                  // window. Do not suppress first live ECB-bottom crossings: source
                  // `mpCheckFloor` accepts horizontal floors exactly when the bottom segment crosses
                  // from above to below the line.
                  // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
                  // data/characters/{fox,falco}.json::ledge_snap_height
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
                  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
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
                  // DamageAir1/2/3 use `ftCo_Damage_Coll -> ft_80081DD4 -> mpColl_800473CC`.
                  // A FoD height-transform line whose sparse replay lane is only a valid/named
                  // pose is not the live grIzumi/mpLib floor owner for this callback. Require the
                  // current-owned platform packet (direct event, ground/same-step contact, live
                  // velocity, or scheduler) before allowing DamageAir to publish that transformed
                  // soft-platform floor; otherwise a rollout can land from a stale named height
                  // while source remains in airborne hitstun.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
                  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
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
                  // Late DamageFly rows can carry a stale hard-floor CollData.floor.index while a
                  // FoD height-transform platform is between the live root and the carried floor.
                  // Source `mpColl_80044628_Floor` still requires the live ECB-bottom floor
                  // precondition for the transformed line; do not let a deep bottom-only crossing
                  // publish a platform DownBound before the terminal DamageFly/DamageFall owner.
                  // The bound is the mpColl one-unit ECB neighborhood, narrowed to the terminal
                  // hitstun window so ordinary mid-hitstun platform landings remain live.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
                  //   ftCo_DamageFly_Anim,ftCo_DamageFly_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  ((hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   is_damage_fly_collision_action(action_id) && batch->state.hitstun[idx] <= 4u &&
                   !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]) &&
                   transformed_platform_bottom_penetration > (0.5f * k_ecb_vertical_unit))
                      ? 1u
                      : 0u;
              const uint8_t suppress_damage_transformed_platform_ecb_only_land =
                  // Damage/DamageFly platform contact is owned by ftCo_Damage*_Coll -> ft_80081DD4
                  // and mpCheckFloor's live CollData ECB sweep. Do not key this on residual KB sign:
                  // legal-stage platform landings can retain positive attack-Y while the ECB bottom
                  // descends through the floor. Reject only source-shaped platform contacts where
                  // the source floor pass has only an endpoint/off-span transformed-platform hit,
                  // plus the terminal stale hard-floor/height-platform owner. In-span static-y FoD
                  // support lines are ordinary platform floor contacts for the DamageFly
                  // tech/DownBound ladder.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
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
                  // AttackAirN/Lw's generated submotion rows use AttackAir_Coll
                  // (ft_80082C74 -> mpColl_800471F8), whose floor path first accepts a live
                  // ECB-bottom crossing in mpColl_80044628_Floor and only then snaps the root
                  // through mpColl_80044838_Floor(ignore_bottom=true). On FoD height-transform
                  // platforms, sustained AttackAirN/Lw can expose a transformed-platform ECB
                  // crossing while both callback root endpoints are already below the platform;
                  // keep that boundary airborne only while the transformed-platform pass owner is
                  // live: source-carried floor_skip for the candidate line or current raw down-held
                  // in-span input. Other AttackAir submotions and released/no-skip rows remain
                  // ordinary landing candidates unless their extracted submotion row proves this
                  // owner.
                  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
                  // FallSpecialB can continue an mpColl floor-skip episode after the down input starts
                  // to release. Slippi seeds do not expose CollData.floor_skip for this callback, so use
                  // the same source-owned evidence the callback consumes: transformed-platform
                  // projection with a carried non-platform floor.index.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
                  ((hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   action_id == (uint16_t)MSL_ACT_FALL_SPECIAL_B &&
                   !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]))
                      ? 1u
                      : 0u;
              const uint8_t suppress_cliff_ledge_locked_zero_bottom_hit =
                  // Source mpColl ordering for the restored cliff floor owner:
                  // mpColl_80046904 only consumes mpColl_80044838_Floor after
                  // mpColl_80044628_Floor's live ECB-bottom check. A direct one-step seed can
                  // reconstruct the cliff floor id while the locked EscapeAir bottom is still the
                  // zero-bottom handoff value; this can synthesize an early ledge-floor sweep
                  // before the interpolated source ECB bottom reaches the floor. Once the carried
                  // cliff floor's own source bottom sweep accepts, flat and generated sloped ledge
                  // floors both continue through the ordinary publication path.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpCollInterpolateECB,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
                  // mpCheckFloor owns the raw floor intersection before mpLib projection/remap. If
                  // that raw hit is Yoshi's low ledge floor but the carried CollData.floor.index
                  // projects to the elevated side platform, consume the low raw hit instead of
                  // snapping up to stale platform height.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
                    // AttackAir_Coll uses the same live `ft_80082C74 -> mpColl_800471F8`
                    // source floor producer as the ordinary air-collision path. If the first
                    // callback-local bottom sweep sees a transformed-platform candidate that remains
                    // airborne, the producer may still accept the hard floor crossed by the current
                    // ECB bottom before the frame leaves collision. This is not stale carried-floor
                    // authority: it requires the live AttackAir map callback phase and a real
                    // current bottom sweep against a fighter-solid non-platform floor.
                    //
                    // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_471F8
                    // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform_transform metadata
                    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
                  on_ground = 1;
                  floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
                  ground_id = attackair_hard_floor_substep.projected_segment_id;
                  contact_x = attackair_hard_floor_substep.hit_x;
                  contact_y = attackair_hard_floor_substep.hit_y;
                  floor_nx = attackair_hard_floor_substep.normal_x;
                  floor_ny = attackair_hard_floor_substep.normal_y;
                  mpcoll_record_callback_floor_result_with_mode(
                      &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                      (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id, contact_x, contact_y,
                      floor_nx, floor_ny);
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
                    on_ground = 1;
                    ground_id = g->lines[(size_t)out_line_idx2].segment_i;
                    contact_x = ix;
                    contact_y = iy;
                    mpcoll_record_callback_floor_result_with_mode(
                        &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                        (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id, contact_x,
                        contact_y, floor_nx, floor_ny);
                  } else {
                    // Sweep saw a floor segment, but projection failed (unexpected). Preserve the
                    // sweep contact point deterministically and apply the decomp-shaped +0.0001 floor
                    // bias.
                    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
                    batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
                    on_ground = 1;
                    ground_id = g->lines[(size_t)hit_line_idx].segment_i;
                    contact_x = ix;
                    contact_y = iy;
                    mpcoll_record_callback_floor_result_with_mode(
                        &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                        (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id, contact_x,
                        contact_y, floor_nx, floor_ny);
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
              // Decomp parity: mpColl_8004A45C_Floor can still set Collide_{Left,Right}Edge while
              // the floor collision pass does not report "touched_floor" (airborne), and the
              // ledge-grab block uses these bits as the `on_edge` suppression gate.
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
            // DamageFlyRoll hitlag-exit floor owner:
            // Fighter_8006A1BC runs ftCo_Damage_OnExitHitlag as hitlag reaches zero; absent a
            // proven floorhug/resting-floor owner above, the following DamageFlyRoll_Coll callback
            // must not let the generic floor projection publish DownBound from the frozen low roll
            // pose on the same frame. Bound this to rows whose callback-entry floor sweep root was
            // still above the selected source floor line so root-on-floor hitlag-exit rows can
            // publish DownBound.
            // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_Damage_OnExitHitlag,ftCo_DamageFlyRoll_Coll}
            (action_id == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL &&
             batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u &&
             damage_hitlag_exit_source_prev_above_floor &&
             batch->state.speed_y_attack[idx] < 0.0f && damage_hitlag_exit_projection_owner == 0u)
                ? 1u
                : 0u;
        // DamageFlyRoll ownership lane for this floor-projection pass:
        // - keep x221C_b6-gated roll handling after hitlag-exit callback consumption,
        // - avoid re-running this lane on the same frame that Fighter_8006D10C consumes
        //   ftCo_Damage_OnExitHitlag.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006D10C
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
        //   ftCo_Damage_OnExitHitlag,ftCo_DamageFlyRoll_Phys,ftCo_DamageFlyRoll_Coll
        // }
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
            // Active-hitlag ground-Damage platform-to-hard-floor FloorHug:
            // A grounded DamageHi/N/Lw victim can consume live downward SDI while CollData.floor
            // still names a one-way platform, then `Damage_Coll -> ft_80081DD4 ->
            // mpColl_800477E0` can accept the ordinary hard floor crossed by the current ECB
            // bottom and keep the fighter airborne via FloorPush/FloorHug. Keep this tied to the
            // runtime SDI-consumed lane and a platform carried floor so it cannot turn stale visible
            // ground ids or non-SDI hitlag rows into a hard-floor rescue. The helper below mirrors
            // `ftCo_Damage_OnEveryHitlag`'s allow-SDI/stick/timer predicate and also accepts the
            // same-frame runtime consumed latch after timers have applied the displacement.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800477E0,
            //   mpColl_80044628_Floor,mpColl_80044948_Floor}
            (batch->state.hitlag[idx] != 0u && is_damage_ground_collision_action(action_id) &&
             prefer_line_idx >= 0 && prefer_line_is_platform && prefer_line_is_fighter_solid &&
             damage_hitlag_floorhug_attempts_downward_sdi(batch, idx, c))
                ? 1u
                : 0u;
        const uint8_t active_damageair_platform_hard_floor_sweep_owner =
            // Airborne DamageAir shares the same live OnEveryHitlag -> Damage_Coll floor producer
            // as ground Damage. While `fp->allow_sdi` is live, source `ft_80081DD4` routes every
            // hitlag callback through `mpColl_800477E0`; a new SDI edge is not required on the exact
            // frame where the loaded ECB bottom crosses the ordinary hard floor below a stale
            // one-way platform. Require live `allow_sdi`, the generated AIR_477E0 owner, and the
            // actual callback-last-position sweep; restored platform ids and public root-below-floor
            // state are not authority for this path.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800477E0,
            //   mpColl_80044628_Floor,mpColl_80044948_Floor}
            (batch->state.hitlag[idx] != 0u && mpcoll_damageair_action(action_id) &&
             batch->state.damage_allow_sdi[idx] != 0u &&
             mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_477E0) &&
             prefer_line_idx >= 0 && prefer_line_is_platform && prefer_line_is_fighter_solid)
                ? 1u
                : 0u;
        const uint8_t active_damagefly_platform_hard_floor_sweep_owner =
            // DamageFly active-hitlag platform-to-hard-floor FloorHug:
            // `ft_80081DD4` dispatches every DamageFly/DamageFall callback through
            // `mpColl_800477E0` while `fp->allow_sdi` is live, before falling back to
            // `mpColl_800473CC` after hitlag. A live `ftCo_Damage_OnEveryHitlag` SDI displacement can
            // move the callback-local root from the carried one-way platform through the ordinary hard
            // floor below; source `mpColl_80044628_Floor -> mpColl_80044948_Floor` consumes that
            // current hard-floor bottom sweep as stay-airborne FloorPush|FloorHug. Require the same
            // runtime SDI-consumed lane and platform carried floor as the DamageAir owner so restored
            // DamageFly `ground_id` or post-hitlag below-floor roots cannot invent authority.
            //
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_Damage_OnEveryHitlag,ftCo_DamageFly_Coll}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,
            //   mpColl_80044948_Floor}
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
            // Active-hitlag Damage callback-displacement hard-floor FloorHug:
            // `ftCo_Damage_OnEveryHitlag` can move the current root/ECB below a hard floor while
            // CollData.floor.index still names a rejected one-way platform. Source then routes the
            // same Damage_Coll callback through `ft_80081DD4 -> mpColl_800477E0`, whose
            // `mpColl_80044628_Floor` scans active floor candidates before the stay-airborne
            // `mpColl_80044948_Floor` projection. Keep this to a same-callback downward
            // current-bottom displacement during frozen hitlag, a finite mpCollPrev endpoint, and
            // an actual current hard-floor bottom sweep; restored platform ground ids or
            // root-below-floor state alone cannot enter this owner because they do not provide a
            // previous-to-current bottom crossing.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800477E0,
            //   mpColl_80044628_Floor,mpColl_80044948_Floor}
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
            // Throw release can enter common Damage/DamageFly while the victim is already below the
            // persisted floor line and still in hitlag. The following Damage collision callback uses the
            // damage floor path to refresh FloorPush|FloorHug while keeping the fighter airborne;
            // there is no new OnEveryHitlag SDI input on these continuation rows, so keep this as a
            // separate Thrown* release owner rather than broadening the SDI predicate.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
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
            // Common DamageAir re-entry active-hitlag floorhug:
            // a BODY hit can refresh DamageAir1/2/3 into another DamageAir state while the victim
            // root is already below the persisted hard floor. The next
            // `ftCo_Damage_Coll -> ft_80081DD4` callback still runs the stay-airborne floor path
            // (`mpColl_800477E0`), projecting FloorPush/FloorHug without setting the fighter
            // grounded. Keep this separate from DamageHi/N/Lw, DamageFly, and DamageFlyRoll rows,
            // whose below-floor active-hitlag rows have their own owners and negative locks.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
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
            // Ground DamageHi/N/Lw can be made airborne by `ftCommon_8007D5D4` while the current
            // Damage_Coll callback is still in hitlag at the carried floor. Source keeps this as a
            // stay-airborne FloorPush/FloorHug result through
            // `ftCo_Damage_Coll -> ft_80081DD4 -> mpColl_800477E0`; it is not a grounded landing
            // and it is not a root clamp. Require a live hitlag entry frame plus the generated
            // ground-damage owner and an already-below-floor root before using the shared
            // stay-airborne projection below.
            //
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_477E0
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_8008DCE0,ftCo_Damage_Coll}
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
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
            // Ground DamageHi/N/Lw can also be launched airborne from a one-way platform while
            // the current Damage_Coll callback is still in hitlag. Source routes that row through
            // the same `ft_80081DD4 -> mpColl_800477E0` stay-airborne producer; platform contact is
            // accepted only when the live ECB-bottom sweep reaches the carried platform floor, so a
            // stale platform `ground_id` or later below-body root cannot invent this authority.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_8008DCE0,ftCo_Damage_Coll}
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,
            //   mpColl_80044948_Floor}
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
            // DamageAir -> DamageFly active-hitlag floorhug:
            // a same-frame ProcessHit can enter DamageFly from an already-airborne DamageAir
            // collision state while the root is below the persisted hard floor. The first
            // DamageFly_Coll callback still runs ft_80081DD4/mpColl_800477E0 with the source
            // Damage collision floor context, so mpColl_80044948_Floor may refresh
            // FloorPush|FloorHug and project the root while keeping the fighter airborne. Keep this
            // bounded to generated DamageAir -> DamageFly callback families; sustained DamageFly
            // rows and non-DamageAir hit entries remain on their existing SDI/loaded-ECB owners.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_8008DCE0,ftCo_Damage_Coll,ftCo_DamageFly_Coll}
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
            // AttackAir_Coll carries CollData.floor.index through ft_80082C74/mpColl_800471F8.
            // After mpColl_80044628_Floor accepts that ordinary hard floor, mpColl_80044838_Floor
            // can publish by root projection even when the current frame no longer has a fresh
            // top-to-bottom ECB crossing. This is the same source owner used by immediate
            // DamageAir -> AttackAir entries and by JumpAerial/common-air AttackAir starts; keep it
            // bounded to a carried static hard-floor id and a live callback floor packet instead of
            // trace-specific rescue branches. The source-owned floor_sweep_prev_pos lane is the
            // callback-local previous root consumed by source `mpCollPrev`; without it, a
            // replay-visible/restored `ground_id` is not authority to publish. The packet lifetime is
            // bounded by the extracted AttackAir command-script first create_hitbox frame plus the
            // following map-callback observation tick: after that point, sustained AttackAir* rows own
            // floor publication through a fresh bottom/root producer rather than stale carried
            // CollData.floor.index projection. The previous callback root must already be below the
            // candidate floor, which keeps ordinary fresh above-to-below AttackAir crossings on the
            // normal floor producer instead of turning every carried `ground_id` into a late landing.
            // DamageAir IASA entry frames keep their existing callback-local ECB owner and do not
            // consume stale DamageAir floor state here.
            // Require the root to be below the candidate by the current ECB bottom extent so shallow
            // AttackAir floor contacts remain owned by the normal bottom-sweep/projector path. Do
            // not run this on a frame whose prio-0 hitlag gate froze the fighter; source stays in
            // hitlag callbacks there and does not advance the AttackAir collision publication.
            // `hitlag_pre_timer` keeps the seed/frame-start hitlag-1 exit frame out of this owner
            // even after Fighter_8006A1BC decrements the visible timer to zero.
            // data/scripts/{fox,falco}.bin::MSLFTSC1 AttackAir create_hitbox frames
            // data/stages/bin/*.bin::MSLSTG01 segment links/fighter_solid flags
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
            //   ftCo_AttackAir_Enter,ftCo_AttackAir_Coll}
            // refs/melee/src/melee/ft/ftaction.c::ftAction_80073354
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
            on_ground = 1u;
            ground_id = g->lines[(size_t)attackair_projection_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = attackair_projection_floor_y;
            floor_nx = attackair_projection_nx;
            floor_ny = attackair_projection_ny;
            floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
                contact_x, contact_y, floor_nx, floor_ny);
          }
        }
        if (!on_ground && is_damage_ground_collision_action(action_id) &&
            batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u &&
            batch->state.action_frame[idx] <= 2 && raw_current_floor_line_idx >= 0 &&
            (g->lines[(size_t)raw_current_floor_line_idx].is_platform ||
             g->lines[(size_t)raw_current_floor_line_idx].platform_transform_kind !=
                 MSL_STAGE_PLATFORM_TRANSFORM_NONE) &&
            mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_477E0)) {
          // Ground DamageHi/N/Lw hitlag-exit floor producer:
          // `ftCommon_8007D5D4` may flip a grounded damage victim airborne during hitlag while the
          // carried CollData floor still names the source platform. On the hitlag-exit callback,
          // `ftCo_Damage_Coll -> ft_80081DD4` routes through `mpColl_800473CC`; the live
          // `mpColl_80044628_Floor` pass can then accept the ordinary hard floor crossed by the
          // current ECB bottom. Keep this to the hitlag-exit producer and require the real bottom
          // sweep against a non-platform hard floor, so stale platform `ground_id` alone cannot
          // publish.
          //
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   ftCo_8008DCE0,ftCo_Damage_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor,
          //   mpColl_80044838_Floor}
          MslMpcollFloorSweepResult damage_ground_hard_floor_sweep = {0};
          if (mpcoll_collect_bottom_sweep_hard_floor_result(
                  batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                  cur_bottom_y, prefer_line_idx, -1, 0u, &damage_ground_hard_floor_sweep) &&
              damage_ground_hard_floor_sweep.projected_line_idx >= 0 &&
              damage_ground_hard_floor_sweep.projected_y_corr >= 0.0f) {
            batch->state.pos_x[idx] += (damage_ground_hard_floor_sweep.hit_x - cur_bottom_x);
            batch->state.pos_y[idx] += damage_ground_hard_floor_sweep.projected_y_corr;
            on_ground = 1u;
            floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
            ground_id = damage_ground_hard_floor_sweep.projected_segment_id;
            contact_x = damage_ground_hard_floor_sweep.hit_x;
            contact_y = damage_ground_hard_floor_sweep.hit_y;
            floor_nx = damage_ground_hard_floor_sweep.normal_x;
            floor_ny = damage_ground_hard_floor_sweep.normal_y;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
                contact_x, contact_y, floor_nx, floor_ny);
          }
        }
        const uint8_t escapeair_jump_platform_root_owner =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active &&
             (prev_action_id == (uint16_t)MSL_ACT_JUMP_F ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_B ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B))
                ? 1u
                : 0u;
        const int escapeair_callback_pose_frame =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_frame[idx] >= 0)
                ? ((int)batch->state.seed_prev_action_frame[idx] + 1)
                : batch->state.action_frame[idx];
        const uint8_t escapeair_locked_desired_root_owner =
            (locked_desired_ecb_bottom_valid && prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR)
                ? 1u
                : 0u;
        const uint8_t escapeair_sustained_static_platform_root_owner =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR)
                ? 1u
                : 0u;
        if (!on_ground && escapeair_locked &&
            (ecb_lock_timer >= 4u || escapeair_jump_platform_root_owner ||
             escapeair_locked_desired_root_owner ||
             escapeair_sustained_static_platform_root_owner) &&
            (batch->state.speed_y_self[idx] < 0.0f || escapeair_jump_platform_root_owner) &&
            escapeair_locked_platform_root_projection(
                batch, idx, bi, g, stage_id, skip_platform_segment_i, ecb_lock_timer_seed,
                msl_ecb_bottom_rel_y(char_id, anim, (int)ecb_frame_cur),
                msl_ecb_bottom_rel_y(char_id, anim, escapeair_callback_pose_frame),
                cur_ecb_points.bottom_rel_y,
                msl_ecb_top_rel_y(char_id, anim, escapeair_callback_pose_frame),
                escapeair_locked_desired_root_owner,
                batch->state.coll_desired_ecb_bottom_rel_y[idx], &ground_id, &contact_x, &contact_y,
                &floor_nx, &floor_ny, c)) {
          on_ground = 1u;
          escapeair_locked_platform_root_projection_hit = 1u;
        }
        if (!on_ground && escapeair_locked_floor_bottom_sweep_root_projection(
                              batch, idx, bi, g, stage_id, stage_has_height_platform_transform,
                              &prev_ecb_points, skip_platform_segment_i, c, &ground_id, &contact_x,
                              &contact_y, &floor_nx, &floor_ny)) {
          on_ground = 1u;
          escapeair_locked_platform_root_projection_hit = 1u;
        }
        if (!on_ground && escapeair_locked && prefer_line_idx >= 0 &&
            batch->state.speed_y_self[idx] < 0.0f &&
            escapeair_locked_hard_floor_zero_bottom_root_projection(
                batch, idx, bi, g, stage_id, prefer_line_idx, skip_platform_segment_i,
                escapeair_locked_desired_root_owner,
                batch->state.coll_desired_ecb_bottom_rel_y[idx], c, &ground_id, &contact_x,
                &contact_y, &floor_nx, &floor_ny)) {
          on_ground = 1u;
        }
        if (!on_ground &&
            action_uses_sideb_air_ft_check_ground_and_ledge_floor_coll(char_id, action_id)) {
          // Direct ft_CheckGroundAndLedge floor callback:
          // `ft_CheckGroundAndLedge` uses the direct mpColl floor path (`mpColl_800473CC`, or
          // `mpColl_800471F8` while ledge cooldown/x2224_b2 is active). After
          // `mpColl_80044628_Floor` accepts an ECB-bottom floor hit, `mpColl_80044838_Floor`
          // projects from root when the loaded ECB bottom is positive. Do not pass the common-air
          // platform callback here: these direct helpers have no held-down platform-pass predicate.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
          //   ftFx_SpecialAirSStart_Coll,ftFx_SpecialAirS_Coll,ftFx_SpecialAirSEnd_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_800473CC,mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
                // DamageAir1/2/3 direct AIR_473CC floor publication still consumes the current
                // CollData/mpLib platform packet. A named/static sparse FoD height is valid stage
                // geometry, but it is not a live DamageAir floor owner unless grIzumi/mpLib current
                // state owns the packet for this callback.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
                // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
                // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
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
                on_ground = 1u;
                ground_id = g->lines[(size_t)out_line_idx].segment_i;
                contact_x = batch->state.pos_x[idx];
                contact_y = batch->state.pos_y[idx];
                mpcoll_record_callback_floor_result_with_mode(
                    &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, sideb_floor_sweep.mode,
                    ground_id, contact_x, contact_y, floor_nx, floor_ny);
                mpcoll_floor_probe_result(&mpcoll_ctx, &sideb_floor_sweep, 1u, 1u,
                                          (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
              } else if (hit_line_idx >= 0) {
                batch->state.pos_x[idx] += (ix - cur_bottom_x);
                batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
                on_ground = 1u;
                ground_id = g->lines[(size_t)hit_line_idx].segment_i;
                contact_x = ix;
                contact_y = iy;
                mpcoll_record_callback_floor_result_with_mode(
                    &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, sideb_floor_sweep.mode,
                    ground_id, contact_x, contact_y, floor_nx, floor_ny);
                mpcoll_floor_probe_result(&mpcoll_ctx, &sideb_floor_sweep, 1u, 0u,
                                          (uint8_t)MSL_MPCOLL_FLOOR_PROBE_ACCEPTED);
              }
            }
          } else {
            mpcoll_floor_probe_result(&mpcoll_ctx, &sideb_floor_sweep, 0u, 0u,
                                      (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_BOTTOM_SWEEP);
          }
        }
        // FallSpecial joins the jump family here: is_common_fallspecial_action excludes it
        // from the 47e14 owners and the hard-floor producers exclude is_ledge lines, so a
        // post-airdodge FallSpecial descending onto a sloped LEDGE strip (Yoshi's) had no
        // owner at all and fell into the stage keel (fuzz ledgedash falco/ys).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Coll
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor
        const uint8_t lr_is_jump_action = (uint8_t)(action_id == (uint16_t)MSL_ACT_JUMP_F ||
                                                    action_id == (uint16_t)MSL_ACT_JUMP_B ||
                                                    action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                                                    action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
                                                    action_id == (uint16_t)MSL_ACT_FALL_SPECIAL);
        if (!on_ground && lr_is_jump_action && batch->state.pos_y[idx] < 0.0f &&
            !stage_has_height_platform_transform && stage_has_only_static_cardinal_hard_floors) {
          // Jump-family ledge-strip floor catch: descending bottom crossings of LEDGE-STRIP
          // floors only (acceptance below) - the validated jump landing owners filter out
          // is_ledge lines, so a jump that rounded the lip corner and descends inside the
          // strip span had no owner at all and traversed the stage body
          // (fuzz seed 1135808358, act 28; still pinned by that seed after the faithful
          // mpCheckFloor pass - the jump owners do not run the ledge-admitting collect).
          // RETIRED here by that same pass: the EscapeAir entry / ground-departure-window /
          // sustained-dodge branches, all subsumed by the per-branch source intersection
          // (msl_mplib_line_intersection slop, floor_intersect_horiz ay>=by) plus the
          // ledge-admitting EscapeAir 471F8 producer.
          // Source-shaped per-endpoint basis: THIS frame's pose bottom rel at the current
          // root, LAST frame's converged bottom rel (the effective lane) at the previous
          // root. The strip crossing can happen while the ROOT rises (the pose rel shrinks
          // faster than the root climbs), so the admission is the BOTTOM descending.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_FallSpecial_Coll
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
                lr_sweep.hit_is_ledge &&
                // Near-flat floors only. 0.25 admits Yoshi's sloped ledge strips (nx 0.204,
                // the lateral-entry class's landing surface there) while still excluding the
                // steeper cliff slopes pinned by the shallow-cliff stay-airborne lock.
                fabsf(lr_sweep.normal_x) < 0.25f && batch->state.pos_y[idx] < lr_sweep.hit_y) {
              // Publish through the source segment remap: mpLib_8004DD90 walks from the hit
              // line to the segment under the CURRENT root (a seam landing publishes the
              // segment the snapped root rests on, not the segment the sweep crossed).
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
              contact_x = lr_sweep.hit_x;
              contact_y = lr_sweep.hit_y;
              floor_nx = lr_sweep.normal_x;
              floor_ny = lr_sweep.normal_y;
              mpcoll_record_callback_floor_result_with_mode(
                  &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                  (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP, ground_id, contact_x, contact_y,
                  floor_nx, floor_ny);
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
          // DamageAir non-SDI platform-to-hard-floor producer:
          // `ftCo_Damage_Coll -> ft_80081DD4` copies CollData.cur_pos into last_pos, writes
          // `fp->cur_pos`, and then routes non-allow_sdi rows through the normal air floor path.
          // When a live DamageAir callback carries a stale one-way-platform floor but its loaded
          // ECB bottom crosses the ordinary hard floor below, source `mpColl_80044628_Floor` may
          // publish that floor before the Damage_Coll grounded follow-up chooses Landing/Wait.
          // This consumes the callback-local CollData last_pos plus the stored previous ECB bottom;
          // restored platform ids, public root-below-floor state, and allow-SDI stay-airborne rows
          // are handled by their separate owners and cannot enter this path.
          //
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800473CC,
          //   mpColl_80044628_Floor,mpColl_80044838_Floor}
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
            on_ground = 1u;
            floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
            ground_id = damageair_hard_floor_sweep.projected_segment_id;
            contact_x = damageair_hard_floor_sweep.hit_x;
            contact_y = damageair_hard_floor_sweep.hit_y;
            floor_nx = damageair_hard_floor_sweep.normal_x;
            floor_ny = damageair_hard_floor_sweep.normal_y;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
                contact_x, contact_y, floor_nx, floor_ny);
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
          // Sustained ground-Damage platform-to-hard-floor FloorHug:
          // DamageHi/N/Lw can remain in `ftCo_Damage_Coll` after hitlag while the carried
          // CollData floor still names a FoD one-way platform. The generated owner for these
          // airborne ground-damage rows is the AIR_477E0 stay-airborne producer, and source
          // `ft_80081DD4 -> mpColl_800477E0 -> mpColl_80044628_Floor/mpColl_80044948_Floor`
          // consumes a live ECB-bottom hard-floor crossing as FloorPush/FloorHug without entering a
          // grounded landing state. Keep this off DamageFly/DamageFall terminal owners, restored
          // platform ids, and root-only below-floor rows by requiring runtime-owned previous root
          // plus the actual hard-floor bottom sweep.
          //
          // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_477E0
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,
          //   mpColl_80044948_Floor}
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
          // Damage hitlag-exit callback ownership:
          // - Damage entry writes `post_hitlag_cb = ftCo_Damage_OnExitHitlag`.
          // - Fighter_8006D10C invokes that callback on hitlag exit before Damage collision callback.
          // - DamageFly_Coll / Damage_Coll then resolve grounded contact via ft_80081DD4.
          // - In the non-allow_sdi air callback path, mpColl_80046904 may use
          //   mpColl_80044838_Floor(ignore_bottom=true), but only after mpColl_80044628_Floor has
          //   accepted the loaded ECB bottom as a floor hit. A carried active-hitlag FloorHug latch
          //   alone is not source authority to snap a hitlag-exit DamageFly root whose current ECB
          //   bottom remains above the floor.
          // - Restrict this to continuation frames where the current damage state has already hit
          //   the active-hitlag stay-airborne floorhug owner earlier in the same hitlag segment.
          //   Teacher-forced one-step rows do not seed that transient CollData continuation, and
          //   broad post-hitlag root snaps create new seed==ref damage landings outside the proven
          //   rollout-owned handoff.
          // - DamageFlyTop can also exit hitlag from a source-visible resting hard-floor FloorHug
          //   state: frame-start root and the prefix CollData floor sweep both start on the same
          //   biased hard floor, current root has dropped below it, and `mpColl_80044628_Floor`
          //   accepts the loaded bottom before root projection. This seeds the same hidden latch
          //   without admitting already-below-floor, platform, ledge, slope, or DamageFlyHi/Roll
          //   rows.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   ftCo_8008DCE0,ftCo_Damage_OnExitHitlag,ftCo_Damage_Coll,ftCo_DamageFly_Coll
          // }
          // refs/melee/src/melee/ft/fighter.c::Fighter_8006D10C
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
        // DamageAir1/2/3 active-hitlag floorhug is a callback-local FloorPush|FloorHug result:
        // it prevents inside-floor tunneling while hitlag is frozen, but vanilla does not carry it
        // as authority for the following hitlag-exit landing. Keep the post-hitlag floorhug latch on
        // DamageFly/DownDamage/release rows whose source callbacks have the retained exit-owner
        // proof; otherwise replay-real DamageAir rows land one frame early from stale CollData.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
        //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_OnExitHitlag,ftCo_Damage_Coll,ftCo_DamageFly_Coll}
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
            // Live Damage active-hitlag callback fallback:
            // `ftCo_Damage_OnEveryHitlag` can move `fp->cur_pos` while `allow_sdi` is live, and
            // DamageAir hitlag can also change the loaded ECB bottom without a new SDI edge on that
            // exact frame. In source, `ft_80081DD4` still calls `mpCollPrev` before writing the
            // callback-local root into CollData.cur_pos, so mpColl_800477E0 sees the frame-start
            // root as last_pos. Try that source endpoint only after the explicit carried
            // floor-sweep endpoint failed to produce a hard-floor hit, and only inside the live
            // Damage allow-SDI callback owner.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800477E0,
            //   mpColl_80044628_Floor}
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
          // Stay-airborne floor contact ownership:
          // - mpColl_80044628_Floor sets FloorPush|FloorHug when the ECB bottom segment touches the
          //   persisted floor line.
          // - Under `CollisionFlagAir_StayAirborne`, mpColl_80044948_Floor can then project the
          //   fighter back to the floor boundary without setting touched_floor/grounded.
          // Reproduce that by projecting against the persisted floor.index, correcting root Y, and
          // emitting floor env bits while keeping `on_ground=0`.
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
              // Keep this owner off ledge floor segments. mpColl edge/ledge suppression is owned
              // by separate floor-edge helpers, and replay-real AGN 4839/4840 stay airborne below
              // the FD ledge floor despite diagonal down input during DamageFlyTop hitlag.
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
              (!g->lines[(size_t)out_line_idx].is_ledge ||
               active_damage_ledge_edge_floorhug_owner) &&
              // Soft-platform stay-airborne projection needs a real current bottom sweep. A
              // replay-seeded carried floor.index alone can represent hard-floor CollData
              // continuation, but using it for static platforms snaps active-hitlag SDI upward to a
              // platform the source mpColl_80044628_Floor never accepted.
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
            // Same active-hitlag owner, root fallback:
            // mpColl_80044948_Floor can use the fighter root when the loaded ECB bottom is not the
            // usable floor-contact point. Keep the fallback inside the already-proven
            // OnEveryHitlag/downward-SDI owner and require the persisted floor line to be a
            // non-ledge segment containing the root X. Ledge rows remain excluded by the control
            // above.
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80044948_Floor,mpColl_80046904}
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
          // Decomp ownership: DamageFlyRoll_Coll resolves grounded follow-up via ft_80081DD4, which
          // runs mpColl_800473CC (air callback path) before ftCo_80090184 selection.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   ftCo_DamageFlyRoll_Phys,ftCo_DamageFlyRoll_Coll,ftCo_80090184
          // }
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80046904,mpColl_80044838_Floor}
          //
          // mpColl_LoadECB_JObj defines side-point Y as midpoint(bottom, top) + x124 offset.
          // Combine side- and bottom-point depths as a conservative callback-owned gate for the
          // root-based floor projection path (mpColl_80044838_Floor ignore_bottom branch).
          // This keeps shallow bottom-only crossings in-air while the DamageFlyRoll ownership lane
          // is still active, and turns over once the lane is deeply penetrated.
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
            // DamageFlyRoll terminal-downward live-pose turnover:
            // ftCo_DamageFlyRoll_Phys applies ft_80084EEC (gravity clamped by character terminal
            // velocity), calls doFlyRoll, then DamageFlyRoll_Coll consumes the live JObj ECB through
            // ft_80081DD4. Keep the near-side-depth root projection on that live JObj owner and a
            // terminal downward self-velocity; lower-terminal Fox rows with the same carried hard
            // floor remain airborne until their source bottom/root contact actually turns over.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
            //   ftCo_DamageFlyRoll_Phys,doFlyRoll,ftCo_DamageFlyRoll_Coll}
            // refs/melee/src/melee/ft/ft_084E.c::ft_80084EEC
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_800473CC,
            //   mpColl_80044838_Floor}
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
              // Decomp-owned callback math for this lane:
              // - mpLib_8004DD90_Floor yields projection delta in world-space y (`y_out`).
              // - mpColl_80044838_Floor(ignore_bottom=true) applies that delta directly to
              //   coll->cur_pos.y with no additional cap.
              // - mpColl_LoadECB_JObj defines side-point y as midpoint(bottom, top) + x124.
              // Keep root placement as "root-contact minus callback-owned side-depth threshold"
              // without an extra clamp.
              // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044838_Floor,mpColl_LoadECB_JObj}
              float root_y_corr = damageflyroll_root_proj_y_corr - damageflyroll_side_y_thresh;
              // Callback-owned cap for this DamageFlyRoll projection lane:
              // - ft_80081DD4 routes through mpColl_800473CC -> mpColl_LoadECB_inline -> mpColl_80042384.
              // - keep root lift bounded to the ECB vertical unit used by that callback path
              //   (not mpLib_8004ED5C endpoint-extension constants).
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
        // Fresh JumpAerial -> EscapeAir IASA handoff:
        // ftCo_80099A58 can change motion state before the map callback, but the first locked
        // EscapeAir collision pass still carries CollData's pre-entry ECB/floor lifetime. Do not let
        // generic floor projection consume that transition into LandingFallSpecial before the
        // source EscapeAir row is published.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
        //   ftCo_80099A58,ftCo_EscapeAir_Coll}
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
          // EscapeAir_Coll uses ft_80082C74 and the persisted CollData floor.index. Teacher-forced
          // one-step reseeds can begin after the full mpColl sweep has already placed root Y below
          // the floor while vanilla still resolves LandingFallSpecial from that same floor.index.
          // Keep this source floor-index handoff limited to rows with frame-start prev-ECB-bottom penetration and
          // post-physics root below the floor. Ledge-floor rows use this only in the early entry
          // window covered by the locked previous-ECB owner; later ledge handoffs are handled by
          // the root-crossing owner below, and already-below-ledge continuations stay on the normal
          // EscapeAir lock path so the previous-ECB carry cannot synthesize an extra
          // LandingFallSpecial frame.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          uint8_t cliff_ledge_floor_owner_floor_check_hit = 1u;
          if (cliff_ledge_floor_owner_active) {
            // mpColl_80046904 first requires mpColl_80044628_Floor to report an ECB-bottom floor
            // hit; only then can mpColl_80044838_Floor use the root point when ecb.bottom.y > 0.
            // The restored cliff floor id supplies CollData.floor.index, but it must not skip the
            // preceding bottom-sweep contact. This keeps shallow ledge-release EscapeAir rows
            // airborne until the loaded/interpolated ECB bottom actually crosses the source ledge
            // floor.
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
              // This root-below guard is for callback rows whose source CollData.floor.index already
              // owns a hard/platform floor handoff. Do not use it when CollData_X130_Locked's seeded
              // desired bottom is still above a non-platform floor, or for generated sloped ledge
              // entries whose floor-edge owner remains airborne.
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_8004A45C_Floor}
              ((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                batch->state.action_frame[idx] <= 4 && !projected_line_is_platform &&
                !projected_line_has_platform_transform &&
                batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
                msl_escapeair_locked_bottom_owner_any(
                    batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
                batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
                batch->state.coll_desired_ecb_bottom_rel_y[idx] > y_corr) ||
               // Fresh JumpAerial -> EscapeAir ledge entries with no desired-bottom depth remain
               // on the zero-bottom/edge-suppression owner for this callback; the desired-bottom
               // floor handoff above handles the matching source case with an explicit bottom
               // owner.
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
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
          // Sustained EscapeAir ledge-floor callback handoff:
          // ftCo_EscapeAir_Coll delegates to ft_80082C74 -> ft_80081D0C -> mpColl_800471F8.
          // While CollData_X130_Locked is still active, mpColl_LoadECB_inline can resolve the
          // callback from the persisted floor.index/root position even when the lite sim's raw
          // SSANIM bottom point stays above the floor. Keep this narrow to a real root crossing
          // from above the floor bias to below the floor in the same callback. Rows already hugging
          // the floor bias or already below the ledge floor remain owned by the normal EscapeAir lock
          // suppression and later collision phases.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
          // Cliff-owned EscapeAir root crossing:
          // ftCo_EscapeAir_Coll runs through ft_80082C74 -> ft_80081D0C -> mpColl_800471F8 while
          // x2064 keeps the released ledge floor in CollData. The root/last_pos sweep can cross
          // from above the stored ledge floor into its span before the raw ECB bottom reaches the
          // ledge line; source mpColl then publishes the stored ledge floor instead of letting the
          // airdodge pass under the stage. This is the live counterpart to the one-step seeded
          // carried-cliff-floor owner, and it is bounded by the generated ledge line and actual
          // current-root crossing.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
          // Require the frame-start root to already be below the ledge floor. If the frame starts
          // above the ledge, source waits for the normal bottom/root floor checks to mature instead
          // of using this late cliff-floor root publication.
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
          // Generic cliff-owned EscapeAir root projection. The restored cliff floor is publishable
          // only after the current callback producer has accepted that same carried floor;
          // strict span plus seeded provenance alone is not source authority.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
          // Fresh FallAerial -> EscapeAir ECB handoff:
          // EscapeAir_Coll still owns landing via ft_80082C74 on the entry collision pass, but the
          // motion-state change can move the sampled previous bottom below the floor before the
          // generic sweep observes the crossing. Project against the persisted floor.index only for
          // this fresh, non-ledge entry window, bounded by EscapeAir's own initial ECB bottom height.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{ftCo_80099A58,ftCo_EscapeAir_Coll}
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
            // Decomp shape: while CollData_X130_Locked is active, EscapeAir_Coll can still resolve
            // against the persisted floor.index via mpLib_8004DD90_Floor-style projection.
            // Keep the projection on non-rising rows; mpCheckFloor's floor intersection path is
            // likewise non-rising (`ay >= by`).
            // Ledge floor caveat: allow shallow ledge-floor contact only while the root is still
            // horizontally above the ledge floor and either (a) in the early locked entry window or
            // (b) in a later below-floor continuation that was already below the floor on the previous
            // CollData snapshot. The immediate above->below crossing phase is handled by the
            // root-crossing owner above; off-end rows and the intermediate below-ledge frame remain
            // airborne instead of teleporting up onto the stage.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
                // Locked EscapeAir projection uses the persisted CollData.floor.index owner. If the
                // FoD transformed-platform graph remaps that persisted line to a different platform
                // during the first locked frames, source keeps the airdodge airborne until the
                // ordinary collision sweep owns the handoff.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
                (out_line_idx >= 0 && batch->state.action_frame[idx] <= 4 &&
                 resolved_segment_i != batch->state.ground_id[idx] &&
                 stage_collision_floor_line_has_platform_transform(stage_id, resolved_segment_i))
                    ? 1u
                    : 0u;
            const uint8_t suppress_same_platform_projection_from_below =
                // CollData.floor.index projection is not a substitute for mpCheckFloor crossing.
                // If the carried floor is an elevated platform and both loaded ECB bottom endpoints
                // are already below that platform, EscapeAir_Coll must not snap upward through the
                // platform. Leave the low-floor/ledge contact for the ordinary sweep below.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_platform &&
                 (cur_bottom_y + y_corr) > k_floor_y_bias && prev_bottom_y <= k_floor_y_bias &&
                 cur_bottom_y <= k_floor_y_bias)
                    ? 1u
                    : 0u;
            const uint8_t suppress_off_end_ledge_remap_projection =
                // mpLib endpoint extension can make a persisted center floor project onto an
                // adjacent ledge floor even when the fighter root is outside that ledge segment.
                // In the early locked EscapeAir window, keep that off-end ledge remap airborne and
                // leave true in-bounds ledge/root continuations to the late below-floor owner.
                // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
                // The sustained EscapeAir ledge-floor projection is a callback-local floor
                // continuation, not an endpoint catch. If the previous CollData root was outside the
                // resolved ledge span, source `mpCheckFloor` does not let mpLib endpoint projection
                // synthesize an immediate LandingFallSpecial as the root enters from off-end.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                (stage_has_height_platform_transform && resolved_line_is_ledge &&
                 !resolved_ledge_prev_x_in_bounds && batch->state.action_frame[idx] <= 3)
                    ? 1u
                    : 0u;
            const uint8_t suppress_jumpaerial_entry_shallow_ledge_projection =
                // Fresh JumpAerial -> EscapeAir can carry the center-floor index while projecting
                // onto the adjacent Yoshi ledge. Source reaches this projection only after the
                // entered EscapeAir ECB bottom is deep enough for mpColl_80044838_Floor's snap; a
                // shallow remap is just persisted-floor projection, not a floor callback result.
                // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
                // The same depth owner applies when a teacher-forced seed restores the actual
                // cliff/CollData ledge floor up front. A live cliff floor id selects the correct
                // source floor, but mpColl_80044838_Floor still requires penetration at least as
                // deep as the entered EscapeAir ECB bottom before the root projection may snap onto
                // that ledge floor.
                // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044838_Floor}
                (cliff_ledge_floor_owner_active && is_ledge_floor && y_corr >= 0.0f &&
                 !escapeair_live_cliff_ledge_source_floor_owner &&
                 !projected_cliff_desired_bottom_sweep_hit &&
                 y_corr < (bottom_rel0 - k_floor_y_bias))
                    ? 1u
                    : 0u;
            const uint8_t suppress_cliff_ledge_remap_without_live_owner =
                // A restored cliff floor id is not enough to let generic mpLib projection remap a
                // non-ledge CollData floor onto the carried ledge. The cliff floor must first be
                // selected as the live callback owner; otherwise this is just stale seed state plus
                // endpoint graph traversal before mpColl_80044628_Floor owns the ledge candidate.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
                // Source depth owner:
                // mpColl_80044838_Floor only uses the root point (`ignore_bottom=true`) after
                // mpColl_LoadECB_inline has an above-root ECB bottom. A locked EscapeAir row whose
                // raw low-floor hit is shallower than the entry pose bottom remains owned by the
                // ordinary sweep/edge-suppression pass; deeper rows can consume the low raw floor
                // instead of remapping to a stale side-platform floor.index.
                // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
          // Locked EscapeAir stale-platform handoff:
          // CollData.floor.index can still name the platform from the JumpAerial entry frame after
          // root motion has carried the fighter over a different static platform. Source
          // EscapeAir_Coll still routes through ft_80082C74/mpColl_800471F8 and may publish the
          // platform under the current root instead of requiring the stale CollData floor.index to
          // be horizontally in span. Keep this out of height-transformed moving platforms and in
          // the locked early EscapeAir callback phase so FoD height-state and hard-floor handoffs stay on their
          // existing owners.
          //
          // data/stages/bin/*.bin::MSLSTG01 platform flags/endpoints
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
          // Fresh JumpF/JumpB -> EscapeAir floor handoff:
          // - ftCo_80099A58 changes to EscapeAir during IASA, then EscapeAir_Coll runs in the
          //   same Fighter proc and delegates floor handling to ft_80082C74.
          // - CollData.floor.index can traverse connected floor seams during mpLib_8004DD90_Floor;
          //   a teacher-forced seed that begins on a ledge floor can therefore land on the adjacent
          //   center floor in the same callback pass.
          // - Keep this off JumpAerial entry rows; those use the standard sweep/ledge suppression
          //   below and include replay-real controls that remain airborne past the right ledge.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
          //   ftCo_80099A58,ftCo_EscapeAir_Coll}
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
              // Fresh JumpF/JumpB -> EscapeAir can enter EscapeAir before Fighter_procMap, then the
              // source EscapeAir_Coll/mpColl_800471F8 pass may accept FoD's current height-platform
              // floor under the root even when CollData.floor.index still names the main floor. Keep
              // this to source-trusted MSLSTG01 height-transform platforms; ordinary static platforms
              // and sustained EscapeAir rows remain on their existing floor-index/skip owners.
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
              //   ftCo_80099A58,ftCo_EscapeAir_Coll}
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
            // AttackAir_Coll continues to run after hitlag exit through
            // `ft_80082C74 -> mpColl_800471F8`. Rows that entered hitlag while carrying a stale
            // platform floor can exit hitlag with the current ECB bottom crossing the ordinary hard
            // floor below. Keep the non-AttackAirLw extension to that hitlag-exit/platform source
            // shape; sustained AttackAir rows stay on the older AttackAirLw-only detector slice
            // because broad all-AttackAir hard-floor publication false-publishes DamageFlyRoll and
            // delayed-hit rollout owners.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
            // AttackAir_Coll live hard-floor continuation:
            // `ftCo_AttackAir_Coll -> ft_80082C74 -> ft_80081D0C -> mpColl_800471F8` runs the
            // current callback-local ECB bottom through `mpColl_80044628_Floor`. A restored/stale
            // platform floor id is not authority, but a same-callback AttackAirLw bottom sweep that
            // reaches a fighter-solid hard floor is. Other AttackAir variants require the narrower
            // hitlag-exit/platform source shape above; broad all-variant hard-floor publication is
            // validation-red around DamageFlyRoll and delayed-hit transition owners.
            //
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_471F8
            // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform_transform metadata
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
            batch->state.pos_y[idx] += attackair_hard_floor_sweep.projected_y_corr;
            on_ground = 1u;
            floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
            ground_id = attackair_hard_floor_sweep.projected_segment_id;
            contact_x = attackair_hard_floor_sweep.hit_x;
            contact_y = attackair_hard_floor_sweep.hit_y;
            floor_nx = attackair_hard_floor_sweep.normal_x;
            floor_ny = attackair_hard_floor_sweep.normal_y;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
                contact_x, contact_y, floor_nx, floor_ny);
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
          on_ground = 1u;
          floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
          common_fall_flags6_root_floor_projection_hit = 1u;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
              contact_x, contact_y, floor_nx, floor_ny);
        }
        if (!on_ground && jumpaerial_entry_ecb_consumer && is_attackair_action(action_id) &&
            ecb_lock_timer_seed == 0u && have_state_cur_ecb &&
            state_cur_ecb_points.bottom_rel_y > cur_bot.rel_y &&
            batch->state.action_frame[idx] <= 2) {
          // JumpAerial -> AttackAir entry static-platform bottom-sweep owner:
          // `JumpAerial_IASA` can enter AttackAir before `Fighter_procMap`; source
          // `AttackAir_Coll -> ft_80082C74 -> mpColl_800471F8` then promotes the pre-entry
          // JumpAerial CollData ECB before loading/interpolating the first AttackAir ECB. When the
          // entered AttackAir pose lowers the callback-local bottom, `mpColl_80044628_Floor` can
          // accept a soft platform even while public self_vel.y is still positive. This is the
          // static-platform counterpart to the existing JumpAerial action-entry ECB consumer: it
          // requires the hidden CollData ECB lane, no active CollData_X130 lock, and a generated
          // stage-line owner that is currently source-admitted. Non-fighter-solid stage-object
          // support lines such as Yoshi Shy Guys are not ordinary platforms unless the explicit
          // stage-item support lane says the object is live.
          //
          // data/stages/bin/*.bin::MSLSTG01 fighter_solid/stage_object_support_kind
          // data/stage_items/yoshi_shyguy.json
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor}
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
            on_ground = 1u;
            floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
            ground_id = entry_platform_sweep.hit_segment_id;
            contact_x = entry_platform_sweep.hit_x;
            contact_y = entry_platform_sweep.hit_y;
            floor_nx = entry_platform_sweep.normal_x;
            floor_ny = entry_platform_sweep.normal_y;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
                contact_x, contact_y, floor_nx, floor_ny);
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
          // JumpF/B -> ft_80081D0C entry static-platform bottom-sweep owner:
          // source common-air IASA can enter an aerial special before Fighter_procMap; the
          // destination Coll callback then runs `ft_80081D0C -> mpColl_800471F8` using the
          // pre-entry Jump CollData ECB as the previous callback-local bottom. When the entered
          // special pose lowers ECB.bottom, `mpColl_80044628_Floor` can accept a soft platform even
          // while self_vel.y remains positive. Keep this on generated AIR_471F8 callbacks and real
          // fighter-solid/current-support platform lines, not on arbitrary special action ids.
          //
          // data/motion_state/owners/{sheik,marth}.bin::MSLMSO01 phase AIR_471F8
          // data/stages/bin/*.bin::MSLSTG01 fighter_solid/stage_object_support_kind
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
          // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
          //   ftSk_SpecialAirNStart_Coll,doColl}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081D0C
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor}
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
            on_ground = 1u;
            floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
            ground_id = entry_platform_sweep.hit_segment_id;
            contact_x = entry_platform_sweep.hit_x;
            contact_y = entry_platform_sweep.hit_y;
            floor_nx = entry_platform_sweep.normal_x;
            floor_ny = entry_platform_sweep.normal_y;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
                contact_x, contact_y, floor_nx, floor_ny);
          }
        }
        if (!on_ground && mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_473CC) &&
            batch->state.coll_floor_probe_valid[idx] != 0u &&
            batch->state.coll_floor_probe_owner[idx] ==
                (uint8_t)MSL_MPCOLL_FLOOR_PROBE_OWNER_AIR_47E14 &&
            batch->state.coll_floor_probe_reject_reason[idx] ==
                (uint8_t)MSL_MPCOLL_FLOOR_PROBE_REJECT_NO_OWNER) {
          // Debug probe ownership: this point is after the generic flags=6/common-air helper. If a
          // DamageFly-style callback has already been classified as "no 47E14 owner", preserve the
          // actual source phase and floor interval for triage instead of leaving the residual row
          // with a misleading common-air owner packet. This does not affect floor publication.
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
          // FallSpecial_Coll uses ft_80083090 -> mpColl_80047E14. Floor contact is owned by
          // mpColl_80044628_Floor's bottom sweep into the callback-visible root, not by a later
          // post-physics root-y projection. The helper consumes the same broad source owner and
          // then applies mpColl_80044838_Floor's root snap if that bottom sweep hit.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
          //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80083090
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
          // No-lock EscapeAir platform sweep:
          // EscapeAir_Coll delegates through ft_80082C74/mpColl_800471F8, which loads the current
          // EscapeAir ECB and lets mpColl_80044628_Floor admit soft platforms without the
          // ftCo_80096CC8 down-stick pass-through callback. Same-frame no-lock JumpAerial ->
          // EscapeAir entries require replay seed-side JumpAerial provenance and use the
          // callback-local pre-entry JumpAerial/entered EscapeAir bottom sweep. Sustained EscapeAir
          // rows use the current EscapeAir bottom sweep when descending into the platform.
          // Transformed-platform rows without that source edge stay on their explicit platform
          // owners instead of allowing a stale static-y side snap.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
          uint8_t found_static_platform = 0u;
          int best_static_platform_line_idx = -1;
          float best_static_platform_dist2 = 0.0f;
          float best_static_platform_ix = 0.0f;
          float best_static_platform_iy = 0.0f;
          // Source `mpCollInterpolateECB` promotes the previous frame's final ECB into prev_ecb
          // before loading the current pose, so the sustained sweep's previous endpoint uses the
          // previous frame's EscapeAir pose bottom, not the just-loaded current pose. Sampling the
          // current pose here lifts the previous endpoint above platforms the source prev bottom
          // never reached, fabricating `mpLineIntersectionH` crossings.
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
                // mpColl_80044628_Floor observes a platform only when the callback-local ECB
                // bottom reaches that floor during the current vertical step. Dream Land top-
                // platform overstep rows can have the interpolated bottom cross high above the
                // root while the current EscapeAir step remains short of the platform; those stay
                // airborne until a later callback.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
              // FoD static-y transformed-platform remap:
              // `mpColl_800471F8` calls `mpColl_80044628_Floor`; on transformed FoD joints the
              // first pass is `mpCheckFloorRemap`, whose source vertex remap can accept the
              // generated static-y center platform even while CollData.floor still carries a
              // side height-platform segment. Keep this to the probed source-owned bottom sweep:
              // transformed carried floor, generated static-y platform metadata, callback-local
              // CollData prev/current bottom interval crossing the floor, and current EscapeAir ECB
              // side/root spanning the floor. CSO:318 accepts at `mpColl_80044628_Floor`; PTE:422
              // rejects because the probed previous bottom is already below the static-y floor.
              //
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=static_y)
              // reports/triage/newchar_sheik/fall_floor_probe_{cso_318,pte_422}_p0/
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
          // Fresh JumpAerial -> EscapeAir static-platform side/root publication:
          // `JumpAerial_IASA` can enter EscapeAir before `Fighter_procMap`; the same
          // `EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8` callback uses the entered
          // EscapeAir ECB. When the bottom point does not sweep downward but the current ECB spans a
          // static soft-platform floor, source can still accept the floor through the
          // mpColl_80044628_Floor side/adjacent-floor path and publish the root snap through
          // mpColl_80044838_Floor(ignore_bottom=true). Keep this to fresh entry callbacks and
          // static, fighter-solid platforms; sustained no-lock EscapeAir rows remain on the
          // current-bottom sweep owner above.
          //
          // data/motion_state/owners/*.bin::MSLMSO01 PHASE4_ESCAPE_AIR_COLL
          // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform_transform metadata
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
            on_ground = 1u;
            floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
            ground_id = g->lines[(size_t)best_entry_static_platform_line_idx].segment_i;
            contact_x = x;
            contact_y = best_entry_static_platform_y;
            floor_nx = 0.0f;
            floor_ny = 1.0f;
            escapeair_no_lock_static_platform_sweep_hit = 1u;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
                contact_x, contact_y, floor_nx, floor_ny);
          }
        }
        if (!on_ground &&
            is_just_entered_specialairn_end_from_loop(batch->state.char_id[idx], action_id,
                                                      prev_action_id,
                                                      batch->state.action_frame[idx]) &&
            prefer_line_idx >= 0 && batch->state.speed_y_self[idx] < 0.0f &&
            batch->state.pos_y[idx] <= -fabsf(cur_bot.rel_y)) {
          // SpecialAirNLoop_Anim can enter SpecialAirNEnd during the Anim callback, then the
          // entered state's collision callback still routes through AirCatchHit_Coll ->
          // ft_80082B1C -> Landing_Enter_Basic in the same Fighter proc. Teacher-forced one-step
          // reseeds that begin after the loop frame has already sunk the root below the persisted
          // floor.index need a callback-owned root projection on the just-entered End frame. Require
          // the root itself to be deeper than the current ECB-bottom height below the floor so ordinary
          // shallow Loop->End rows can remain airborne in SpecialAirNEnd.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
          //   ftFx_SpecialAirNLoop_Anim,ftFx_SpecialAirNEnd_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ftCo_AirCatchHit_Coll
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
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
        // Decomp: mpCheckFloor's horizontal intersection helper is gated on non-rising segments
        // (`ay >= by`), so upward sweeps should not report a floor crossing.
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
            // Sustained horizontal EscapeAir can stay airborne while sliding at the carried floor's
            // root bias. This is still EscapeAir_Coll ownership, not a generic
            // mpLib_8004DD90_Floor resting contact: downward/deep-penetration rows use the normal
            // ft_80082C74 landing paths, while pure horizontal floor-hug rows remain air through the
            // decay window.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
            //   ftCo_EscapeAir_Phys,ftCo_EscapeAir_Coll}
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
             // Fresh Jump/KneeBend -> EscapeAir entry can publish a horizontal air dodge already at
             // the carried floor's root bias. ftCo_EscapeAir_Coll owns real floor handoffs through
             // ft_80082C74, but the same-pass entry row is not grounded by a generic
             // mpLib_8004DD90_Floor resting contact when the EscapeAir self velocity has no downward
             // component. Keep this scoped to the fresh common-air entry owners; downward or
             // deep-penetration rows still use the existing EscapeAir floor handoff paths below.
             // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::{
             //   ftCo_Jump_Enter,ftCo_Jump_IASA}
             // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
             //   ftCo_80099A58,ftCo_80099A9C,ftCo_EscapeAir_Coll}
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
             // ftFx_SpecialHiBound_Enter changes motion state and ticks anim, but it does not call
             // ftCommon_8007D7FC. The rebound remains airborne on the entry collision row; later
             // Bound_Coll owns ground conversion through ft_CheckGroundAndLedge.
             // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
             //   ftFx_SpecialHiBound_Enter,ftFx_SpecialHiBound_Coll}
             (msl_motion_state_fx_special_kind(batch->state.char_id[idx],
                                               batch->state.seed_prev_action_id[idx]) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) &&
             batch->state.action_frame[idx] <= 2)
                ? 1u
                : 0u;
        const uint8_t downdamage_active_hitlag_resting_floor_contact =
            // DownDamage_Coll uses the same ft_80081DD4 floor callback as damage landing paths.
            // Active-hitlag DownDamage rows can arrive at the post-hitlag collision pass already
            // resting on the persisted floor line while still carrying the Damage hitstun lane.
            // After ftCo_DownDamage_Phys delegates to ftCo_Damage_Phys, the current self velocity
            // is no longer a reliable pre-collision predicate; the narrow source-shaped boundary
            // for the root fallback is the same post-hitlag DownDamage row plus downward attack KB.
            // Keep this off DamageAir/DamageFly active hitlag, where replay-real controls stay
            // airborne until their separate damage hitlag-exit handoff.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Phys
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
          // No-lock JumpAerial -> EscapeAir flat floor/ledge publication:
          // `JumpAerial_IASA` can enter EscapeAir before `Fighter_procMap`, and
          // `EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8` then asks
          // `mpColl_80044628_Floor` from the callback-local pre-entry JumpAerial prev ECB to the
          // loaded EscapeAir current ECB. When a stale carried floor.index would make the graph path
          // miss a new flat ledge/hard floor, source still accepts the static mpCheckFloor hit. Keep
          // this to flat, fighter-solid, non-platform lines; generated sloped ledges remain on the
          // edge/slope owner below and are protected by adjacent Yoshi controls.
          //
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
              on_ground = 1u;
              floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
              ground_id = static_floor_hit.segment_i;
              contact_x = static_floor_hit.x;
              contact_y = static_floor_hit.y;
              floor_nx = static_floor_hit.normal_x;
              floor_ny = static_floor_hit.normal_y;
              mpcoll_record_callback_floor_result_with_mode(
                  &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode,
                  ground_id, contact_x, contact_y, floor_nx, floor_ny);
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
          // No-lock JumpAerial -> EscapeAir carried cliff-floor handoff:
          // ftCliffCommon carries the source ledge-floor id while the ledge cooldown is live. If
          // the entered EscapeAir callback's bottom sweep crosses that same flat ledge floor, the
          // floor producer is `mpColl_80044628_Floor`, not the stale public `ground_id`. This is
          // the no-lock sibling of the locked EscapeAir cliff-floor handoff below.
          //
          // data/stages/bin/*.bin::MSLSTG01 is_ledge/fighter_solid floor metadata
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
            on_ground = 1u;
            floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
            contact_x = cliff_floor_sweep.hit_x;
            contact_y = cliff_floor_sweep.hit_y;
            floor_nx = cliff_floor_sweep.normal_x;
            floor_ny = cliff_floor_sweep.normal_y;
            mpcoll_record_callback_floor_result_with_mode(
                &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
                contact_x, contact_y, floor_nx, floor_ny);
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
          // EscapeAir lock semantics:
          // - keep shallow crossings on ledge floor segments airborne while lock is active;
          // - allow deep-penetration fallback above to ground deterministically.
          //
          // Decomp shape: floor-edge collision bits and ledge suppression are handled separately via
          // mpColl edge helpers, so touching a ledge floor segment is not always equivalent to
          // immediate grounded resolution in EscapeAir lock windows.
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
          const uint8_t escapeair_ledge_bottom_sweep_floor_owner =
              (escapeair_locked && hit_line_is_carried_cliff_ledge_floor &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               prev_bottom_y > (iy + k_floor_y_bias) && cur_bottom_y <= (iy + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t escapeair_cliff_ledge_floor_owner =
              // Live ledge-release x2064 floor provenance: ftCliffCommon carries the terminal
              // ledge floor into EscapeAir, but the carried id is only publication authority after
              // a sustained EscapeAir_Coll callback has crossed that same in-span floor. Fresh
              // JumpAerial -> EscapeAir entry rows can restore the same id/cooldown/span and even
              // report a carried-line bottom crossing, but source still treats that as provenance
              // until the EscapeAir callback owns the floor producer. Flat and generated sloped
              // ledge floors both stay on this same carried-id/cooldown/span/bottom-crossing path.
              // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
               // Callback ordering is Anim then Coll (Fighter_procMap/Fighter_8006A360). Require a
               // sustained EscapeAir ownership window (already EscapeAir at frame start) and post-
               // entry anim age before handing ledge-floor sweeps to floor projection.
               // On ledge floor segments this same handoff is also bounded by the fresh
               // CollData_X130_Locked window. Late-lock EscapeAir rows still carry a persisted
               // floor.index, but source can still publish a real in-span ECB-bottom ledge-floor
               // hit from mpColl_80044628_Floor. Rows without that bottom hit remain airborne until
               // the later callback phase.
               // refs/melee/src/melee/ft/fighter.c::{Fighter_procMap,Fighter_8006A360}
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
               // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
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
               // Decomp path: KneeBend transitions into jump, EscapeAir can be entered from the
               // jump startup window, and EscapeAir_Coll resolves landing via ft_80082C74 on the
               // same callback pass. Keep this handoff narrow to immediate KneeBend-entry rows.
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
               // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
               prev_action_id == (uint16_t)MSL_ACT_KNEE_BEND &&
               batch->state.action_frame[idx] <= 2 && !hit_line_matches_carried_cliff_ledge_floor &&
               !escapeair_fresh_horizontal_floorhug_airborne)
                  ? 1u
                  : 0u;
          const uint8_t escapeair_jump_entry_floor_handoff =
              (escapeair_locked &&
               // Decomp path: JumpF/JumpB can feed directly into EscapeAir through ftCo_80099A58,
               // and EscapeAir_Coll still owns landing via ft_80082C74 after the entered air-dodge
               // callback has a real ECB-bottom floor hit. Keep the entry-frame allowance narrow,
               // but also allow the live JumpF/B ledge-release handoff once the sustained
               // EscapeAir row's callback-local bottom crosses the carried terminal ledge floor.
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
               //   ftCo_80099A58,ftCo_EscapeAir_Coll}
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
              // Source floor handoff for locked EscapeAir:
              // ftCo_EscapeAir_Coll -> ft_80082C74 -> ft_80081D0C -> mpColl_800471F8 uses
              // mpColl_LoadECB_inline(flags=6) followed by mpColl_80043754 interpolation. Once the
              // loaded/interpolated bottom is deep enough for mpColl_80044838_Floor to own the
              // callback, ledge, hard-floor, and platform contacts share the same source predicate.
              // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
              // mpLib endpoint extension can clamp a floor query to a soft-platform endpoint even
              // when the current ECB bottom/root is outside that platform span. During the early
              // locked EscapeAir window, that off-end platform hit is not a stable source floor
              // callback result; keep it airborne like the adjacent off-end ledge remap guards.
              // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              (escapeair_locked && hit_line_idx >= 0 && hit_line_is_platform &&
               !hit_line_x_in_strict_segment && batch->state.action_frame[idx] <= 3)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_seed6_platform_land =
              // The first sustained EscapeAir platform/slope crossing after the CollData lock
              // enters the map callback with frame-start lock value 6. Source does not publish that
              // as the stable platform floor handoff yet; the handoff rows retained above are the
              // next lock phase, where the accepted line is inside the callback ECB envelope.
              // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
              // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_UnlockECB}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
              (escapeair_locked && ecb_lock_timer_seed == 6u &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               hit_line_idx >= 0 && hit_line_is_platform_or_slope &&
               batch->state.action_frame[idx] <= 3)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_vertical_af3_land =
              (escapeair_locked && !deep_lock_penetration && !escapeair_flags6_deep_floor_handoff &&
               hit_line_idx >= 0 && !hit_line_is_ledge &&
               // Decomp-shaped interpolation gap: in the early EscapeAir lock window, vertical-only
               // platform/slope sweep crossings can report a floor hit one frame earlier than
               // replay references. Once the current root is already below the accepted floor by at
               // least the entered EscapeAir ECB bottom extent, the source
               // ft_80082C74/mpColl_800471F8 floor handoff owns the landing instead of the shallow
               // interpolation-gap guard. Generated ledge floors have their own edge/root
               // publication guard later in the floor-writeback path.
               batch->state.action_frame[idx] == 3 &&
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
               // Sustained EscapeAir without CollData_X130_Locked should not inherit the previous
               // ECB owner. Keep the same vertical-only early-anim gap airborne and let the
               // following EscapeAir_Coll frame own the landing if replay still reaches the floor.
               // No-lock generated ledge-floor hits are handled by the ordinary current-ECB path;
               // the locked early-window ledge case is guarded above.
               // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
               // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpColl_80044628_Floor}
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
               batch->state.action_frame[idx] == 3 &&
               fabsf(cur_bottom_x - prev_bottom_x) <= (float)k_floor_horiz_dy_thresh)
                  ? 1u
                  : 0u;
          const uint8_t suppress_seeded_escapeair_first_locked_land =
              // Replay-seeded sustained EscapeAir row whose prefix-visible previous action is still
              // the pre-EscapeAir source. The true CollData current/desired ECB has just crossed the
              // ChangeMotionState boundary. Restrict this to platform/slope floor contacts where the
              // hidden floor/ECB lifetime can differ from a simple hard-floor sweep; ordinary hard
              // floors still land through ft_80082C74 on the same callback pass.
              // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
              // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
              (escapeair_locked && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.action_frame[idx] <= 4 &&
               (hit_line_has_platform_transform || hit_line_is_slope))
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_locked_desired_bottom_above_floor_land =
              // CollData_X130_Locked preserves desired_ecb.bottom during EscapeAir_Coll. A
              // zero-bottom locked owner is only authoritative for rows where source actually uses
              // the root/desired-zero handoff; if the seeded desired bottom remains above the
              // accepted floor, mpColl_80044628_Floor has not produced the source floor hit yet.
              // Keep this scoped to sustained EscapeAir lock rows so same-frame Jump/KneeBend and
              // explicit JumpAerial zero-bottom owners stay on their own callback paths.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_LoadECB_inline,mpColl_80044628_Floor}
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
              // Fresh KneeBend -> EscapeAir can land on ordinary floor/platform callback rows, but
              // ledge floors require the same source bottom-sweep owner as sustained EscapeAir
              // before the zero-bottom root owner may publish LandingFallSpecial.
              // data/stages/bin/*.bin::MSLSTG01 is_ledge + fighter_solid floor metadata
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
              // JumpAerialF -> EscapeAir entry can carry a locked previous bottom into a carried
              // ledge/platform floor line before the source EscapeAir_Coll callback publishes
              // stable floor ownership. Keep transformed platforms on their explicit positive locks
              // and JumpAerialB rows on the ordinary callback path seen in replay-real locks.
              // `action_frame <= 3` is the first EscapeAir_Coll callback window after
              // `ftCo_JumpAerial_IASA` has just changed state, while
              // `seed_prev_action_frame <= 6` is still before the JumpAerial source callback has
              // published a stable static-platform floor. Both action families are table-backed by
              // `MSLMSO01`; later rows use the ordinary EscapeAir floor owner.
              // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_is_platform &&
               !hit_line_has_platform_transform &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
               batch->state.action_frame[idx] <= 3 && ecb_lock_active &&
               batch->state.seed_prev_action_frame[idx] <= 6 && prev_bottom_y <= iy &&
               cur_bottom_y <= iy)
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_late_jump_entry_platform_lifetime =
              // Later JumpAerial -> EscapeAir platform rows can still carry the pre-entry
              // CollData/floor lifetime into the first EscapeAir callback. Source
              // mpColl_80044628_Floor must see a bottom sweep before mpColl_80044838_Floor may
              // publish LandingFallSpecial; late from-below platform contacts stay airborne until a
              // later callback owns the platform floor.
              // The `seed_prev_action_frame 5..6` boundary is the late JumpAerial callback phase
              // immediately before EscapeAir entry in the retained replay-real locks; earlier
              // JumpAerial phases are covered by the fresh entry path above, and later rows have no
              // live locked-ECB source owner.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
              // mpColl_800471F8 can snap the root only after mpColl_80044628_Floor accepts an ECB
              // bottom-floor hit. A platform projection whose current ECB bottom is still above the
              // platform is not a source floor callback result, even if root projection alone would
              // reach the line.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
              // Same owner as the flat no-lock JumpAerial -> EscapeAir publication above, but the
              // generated Yoshi sloped ledge strips are not flat floor publication points on the
              // entry callback when the carried floor is unlinked topology such as a side platform.
              // Same-floor and floor-graph-connected slope handoffs remain source floor
              // publications.
              // data/stages/bin/grst.bin::MSLSTG01 sloped ledge floor segments
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
              // Sustained same-action FallSpecial keeps the first-sustained root-crossing delay that
              // source exposes around ftCo_80096CC8 / mpColl_80047E14. The modelplay Side-B clip is
              // not that sustained owner: it is the just-entered aerial Side-B end destination path,
              // so hard-floor bottom hits are allowed there while soft/platform-like contacts remain
              // gated by the platform/ledge policy below.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
              //   ftCo_FallSpecial_Coll,ftCo_80096CC8}
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
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
              // FallSpecial_Coll routes through ft_80083090 -> mpColl_80047E14 with the
              // ftCo_80096CC8 platform callback. That callback accepts soft platforms unless the
              // callback-visible stick is below p_ftCommonData->x25C. Sustained FallSpecial also
              // needs the source bottom-sweep precondition before the first above-root ->
              // below-root static platform crossing can publish LandingFallSpecial; the just-entered
              // spacie SpecialHi end -> FallSpecial frame consumes distinct pre-entry CollData
              // lifetime and stays on the positive platform handoff.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
              //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
              //   ftFx_SpecialHiFall_Anim,ftFx_SpecialHiBound_Anim}
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
              // Ordinary Fall_Coll routes through ft_800831CC -> mpColl_80047E14(flags=6), whose
              // floor check consumes live CollData.ecb after mpColl_LoadECB_inline/interpolation.
              // This lite sim samples raw SSANIM ECB extents before the live collision-model scale
              // and CollData interpolation used by mpColl_LoadECB_inline. On enlarged collision
              // models, a first ledge-floor root crossing can therefore report a landing while the
              // live ECB still remains above the floor. Suppress only that expanded-model first
              // non-fastfall root-crossing frame and let the next below-floor Fall_Coll frame land.
              // Fastfall ledge crossings keep the ordinary Fall_Coll floor owner. Do not key
              // this on character id: Fox/Falco controls are distinguished by extracted
              // ftCo_DatAttrs::model_scaling in `data/characters/*.json`.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
              // refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale
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
              // Fall_Coll's no-floor-index path is a pure mpColl_80044628_Floor bottom sweep. A
              // shallow first contact without a persisted floor.index and before the movescript
              // allow-interrupt bit is live can remain airborne for one more callback; do not let
              // the lite root projection turn that shallow bottom graze into Landing. Rows with a
              // live floor.index stay on the explicit flags=6 projection owner above, and
              // allow-interrupt shallow Fall rows retain the ordinary floor handoff.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
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
          // DamageFlyRoll active-hitlag below-floor freeze:
          // - DamageFlyRoll keeps the victim airborne while hitlag is still frozen.
          // - Without a proven Damage_OnEveryHitlag stay-airborne floorhug owner above, do not let
          //   generic floor projection snap a below-floor DamageFlyRoll pose to the floor before
          //   ftCo_Damage_OnExitHitlag / DamageFlyRoll_Coll run.
          // Other active Damage/DamageFly rows keep the existing floorhug/projected-contact paths;
          // this is not a broad suppression of the damage collision family.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_OnExitHitlag,ftCo_Damage_Coll,
          //   ftCo_DamageFlyRoll_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpCollPrev,mpCheckFloor,mpColl_800477E0,mpColl_80046904}
          const uint8_t suppress_active_damage_hitlag_land =
              (batch->state.hitlag[idx] != 0u && is_damage_collision_landing_action(action_id))
                  ? 1u
                  : 0u;
          const uint8_t suppress_active_damage_hitlag_bottom_above_floor_land =
              // Active-hitlag Damage/DamageFly can move the root below the carried floor through
              // ftCo_Damage_OnEveryHitlag SDI before the collision callback runs. The generic sweep
              // below may see a root/floor projection, but mpColl_80044628_Floor must first accept
              // a current ECB-bottom floor hit before mpColl_80044948_Floor can publish the
              // stay-airborne floor projection. If the current bottom remains above the hit line,
              // keep the SDI-mutated root airborne for this frame.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
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
              // Damage_IASA can enter AttackAir after hitstun ends in the same Fighter proc. The
              // entered AttackAir_Coll owns ordinary hard-floor contact through ft_80082C74, but
              // platform/slope contacts can still reflect the pre-entry DamageAir floor lifetime and
              // stay airborne until the entered AttackAir callback has a stable floor producer.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
              //   ftCo_AttackAir_Enter,ftCo_AttackAir_Coll}
              (is_attackair_action(action_id) && hit_line_is_platform_or_slope &&
               (prev_action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_1 ||
                prev_action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_2 ||
                prev_action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_3) &&
               batch->state.action_frame[idx] <= 1)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locomotion_attackair_entry_platform_land =
              // Fall IASA can enter AttackAir before Fighter_procMap, but the entry frame still
              // carries the pre-entry CollData/ECB callback lifetime. Platform contact on that same
              // callback must not be consumed by a generic floor sweep before the AttackAir_Coll
              // owner publishes its stable landing result on a later frame.
              // Keep this to platform/slope contacts and fresh entry frames; hard-floor and
              // sustained AttackAir landings remain owned by the normal ft_80082C74 path. Jump
              // family AttackAir entries have a distinct locked-ECB/platform owner and are not part
              // of this slice.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
              //   ftCo_AttackAir_EnterFromMsid,ftCo_AttackAir_Coll}
              // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
              (is_attackair_action(action_id) && batch->state.action_frame[idx] <= 1 &&
               hit_line_is_platform_or_slope &&
               (prev_action_id == (uint16_t)MSL_ACT_FALL ||
                prev_action_id == (uint16_t)MSL_ACT_FALL_AERIAL))
                  ? 1u
                  : 0u;
          const uint8_t suppress_attackair_jumpaerial_stage_object_support_land =
              // JumpAerial_IASA can enter AttackAir before Fighter_procMap, but generated
              // non-fighter-solid stage-object support lines are not ordinary soft platforms for
              // the entered AttackAir callback. Source floor publication still needs a current
              // `mpColl_80044628_Floor` producer for a fighter-solid line, or an explicit live
              // support owner such as Yoshi's Shy Guy item lane. Do not let the generic raw
              // floor-sweep/projection fallback turn inactive support geometry into LandingAir*.
              //
              // data/stages/bin/*.bin::MSLSTG01 fighter_solid/stage_object_support_kind
              // data/stage_items/yoshi_shyguy.json
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
              // EscapeAir_Coll uses CollData's locked ECB snapshot through the early entry window.
              // When that snapshot carries a prior platform floor.index, mpColl can observe a
              // transformed-platform sweep before the source callback resolves LandingFallSpecial.
              // Keep those first locked/non-matching platform crossings airborne; sustained
              // same-platform crossings still use the floor handoff above.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
              // Runtime source path for transformed-platform pass-through remains the explicit
              // CollData.floor_skip lane (`platform_floor_skip_segment_id`) plus callbacks that
              // pass `ftCo_80096CC8`. EscapeAir_Coll routes through `ft_80082C74` and does not use
              // that platform-pass callback, so do not keep the older down-held EscapeAir fallback:
              // same-frame JumpAerial -> EscapeAir platform contacts are admitted or rejected by
              // the pre-entry CollData ECB bottom sweep above.
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{
              //   ftCo_8009A184,ftCo_8009A228}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
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
              // Source platform-pass ownership is input-lifetime sensitive. A down-held
              // AttackAir_Coll callback may retain a transformed FoD platform in CollData.floor_skip;
              // once the callback-visible stick releases above p_ftCommonData->x25C, the carried
              // skip is no longer a reason to reject the same AttackAir_Coll floor publication.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
              // AttackAir_Coll routes through ft_80082C74 -> mpColl_800471F8 rather than the
              // common ftCo_80096CC8 soft-platform callback, so down-held input alone must not
              // suppress endpoint-extension contacts or same-floor publications. For a strict
              // in-span FoD height-platform candidate whose segment differs from CollData.floor
              // and has no current grIzumi/mpLib floor-source bit or live scheduler owner, source
              // mpColl still applies the one-way platform pass decision before publishing
              // LandingAir*. Same-step/direct floor-source contacts and live scheduler contacts
              // retain the ordinary AttackAir floor publication path. Keep that
              // boundary tied to generated stage span data, carried floor identity, MSLSTG01
              // provenance bits, and the MSLFTSC1 AttackAir script owner instead of an action/row
              // slice.
              //
              // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
              // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
              // agent_docs/DATA_CONTRACT.md::FoD platform height source mask
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
              // A replay seed at a named FoD side-platform height is enough to reconstruct the
              // line, but it is not by itself proof that the current AttackAir_Coll callback has
              // a live grIzumi/mpLib floor owner. The early multi-create pending owner still needs
              // an actual platform-pass condition: either a source-carried floor_skip platform or
              // callback-visible down-held in-span pass input. Released/no-skip AttackAir rows can
              // publish through the ordinary ft_80082C74 floor handoff.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
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
              // Single-create AttackAir scripts have no later create-hitbox command to act as the
              // multi-create handoff above. For those scripts, a sparse FoD height-platform line
              // without current grIzumi/mpLib source is still only geometry reconstruction, not a
              // source-owned floor result for AttackAir_Coll. Require a current/live platform
              // source before publishing LandingAir* from that height-transform floor.
              //
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
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
              // AttackAir_Coll enters ft_80082C74 -> mpColl_800471F8 with the live CollData ECB
              // extents. FoD height-transform platform contacts at an endpoint can be accepted by
              // the ECB-bottom sweep while the callback-local root handoff is still owned by the
              // platform pass/floor-skip path. Keep that endpoint-contact frame airborne and
              // publish the explicit floor_skip lane; inboard transformed-platform root contacts
              // retain the ordinary ft_80082C74 LandingAir* path.
              // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
              // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
              // Continuation of the same FoD AttackAirN/Lw platform-pass owner after the initial
              // down-held platform skip is live. The first floor candidate after that moving-platform
              // pass can be a connected hard-floor edge rather than the skipped platform itself;
              // source still keeps the first root crossing airborne and publishes LandingAir* on the
              // following already-below-floor callback. This requires a real carried floor-skip lane
              // and does not apply to fresh AttackAir contacts or static support platforms.
              // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
              // Raw floor-sweep variant of the late-hit AttackAir off-span hard-floor edge owner.
              // mpLib endpoint extension can expose an adjacent hard floor before the source
              // AttackAir_Coll handoff has a strict in-span floor result.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
              // Soft-platform pass-through usually belongs to callbacks that pass ftCo_80096CC8.
              // AttackAir_Coll does not call mpUpdateFloorSkip; its retained FoD platform boundary
              // is the shallow ECB-only owner handled by the raw/projection/final guards below.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
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
              // mpCheckFloor can report a transformed-platform endpoint contact from the ECB bottom
              // sweep, but source floor handoff still requires the callback-local root to be in the
              // live floor span before ftCo_80090184 may publish Passive/DownBound.
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
              // FoD height-transform platform DamageFly endpoint contacts still need the live
              // grIzumi platform/CollData substep owner. `ft_80081DD4` sets
              // coll->ledge_snap_height before calling `mpColl_800473CC`; use the extracted
              // character ledge-snap height as the endpoint bound instead of a replay-local x
              // window. Do not suppress first live ECB-bottom crossings: source `mpCheckFloor`
              // accepts horizontal floors exactly when the bottom segment crosses from above to
              // below the line.
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
              // data/characters/{fox,falco}.json::ledge_snap_height
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
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
              // DamageAir1/2/3 use `ftCo_Damage_Coll -> ft_80081DD4 -> mpColl_800473CC`.
              // A valid/named FoD height-transform pose is source-trusted for static geometry, but
              // it is not current floor-publication authority unless the platform packet is owned
              // by this callback's grIzumi/mpLib state. Keep sparse replay-only heights from
              // landing mid-hitstun DamageAir while direct/same-step/velocity/scheduler-owned
              // platforms continue through the normal floor path.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
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
              // Late DamageFly rows can carry a stale hard-floor CollData.floor.index while a FoD
              // height-transform platform sits between the live root and the carried floor. Source
              // `mpColl_80044628_Floor` still requires the live ECB-bottom floor precondition for
              // the transformed line before `mpColl_80044838_Floor` may publish the floor handoff.
              // Keep the guard inside the terminal hitstun window and outside already-carried
              // platform floors so ordinary mid-hitstun platform landings remain live.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
              //   ftCo_DamageFly_Anim,ftCo_DamageFly_Coll}
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              ((hit_line_has_height_platform_transform ||
                projected_line_has_height_platform_transform) &&
               is_damage_fly_collision_action(action_id) && batch->state.hitstun[idx] <= 4u &&
               !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]) &&
               transformed_platform_bottom_penetration > (0.5f * k_ecb_vertical_unit))
                  ? 1u
                  : 0u;
          const uint8_t suppress_damage_transformed_platform_ecb_only_land =
              // Damage/DamageFly platform contact consumes the same live CollData ECB sweep as hard
              // floors. Positive attack-Y is not a pass-through predicate by itself; suppress only
              // source-shaped platform contacts where the CollData ECB precondition is not met:
              // endpoint/off-span transformed-platform contacts, or the terminal stale
              // hard-floor/height-platform owner. In-span static-y FoD support lines are ordinary
              // platform floor contacts for the DamageFly tech/DownBound ladder.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_80090184
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800986B0
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
              // AttackAirN/Lw's generated submotion rows use AttackAir_Coll
              // (ft_80082C74 -> mpColl_800471F8). That source path must first accept an ECB-bottom
              // hit in mpColl_80044628_Floor before mpColl_80044838_Floor(ignore_bottom=true) can
              // publish the LandingAir* root snap. On FoD height-transform platform endpoints,
              // sustained AttackAirN/Lw can expose a shallow transformed-platform ECB-bottom
              // crossing before the source callback has reached its stable LandingAir* handoff.
              // Keep only that endpoint/submotion boundary airborne; deeper bottom penetration,
              // inboard transformed-platform contacts, static platform/support contacts, and other
              // AttackAir submotions retain the normal AttackAir_Coll landing path.
              // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
              // Common-air platform pass-through owner: ftCo_80096CC8 rejects transformed
              // platform floors while the callback-visible stick is below x25C. Preserve the
              // rejected FoD floor.index as CollData.floor_skip so the next released callback frame
              // still rejects the same moving platform before the source landing handoff can
              // publish.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
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
                  // Same off-end ledge remap boundary as the deep projection path. The generic
                  // floor sweep can hit/remap an adjacent ledge through mpLib endpoint extension,
                  // but early locked EscapeAir should not ground when the root is outside that
                  // accepted ledge floor segment.
                  // refs/melee/src/melee/mp/mplib.c::{
                  //   mpLib_8004DD90_Floor,mpLib_8004ED5C}
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 3 && resolved_line_is_ledge &&
                   !resolved_ledge_x_in_bounds)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_escapeair_off_end_platform_land =
                  // Same endpoint-extension boundary for soft platforms: a projected line outside
                  // the strict generated platform span is not the current EscapeAir floor callback
                  // handoff, even though mpLib can clamp the query to the platform endpoint.
                  // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 3 && resolved_line_is_platform &&
                   !resolved_floor_x_in_strict_segment)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_escapeair_transformed_platform_land =
                  // EscapeAir transformed-platform floor contact must be evaluated after the
                  // mpLib projection/remap result. FoD can sweep one transformed platform record and
                  // resolve to another; using only the swept line over-admits first-entry air-dodge
                  // rows onto a neighboring platform.
                  //
                  // Fresh Jump/JumpAerial -> EscapeAir uses the same callback-local owner as the
                  // final remap guard below: IASA has entered EscapeAir before Fighter_procMap, and
                  // the EscapeAir_Coll pass can consume the transformed-platform projection result.
                  // Sustained EscapeAir over the same carried transformed floor is also a normal
                  // callback result once `escapeair_sustained_floor_handoff` has accepted it. This
                  // guard rejects stale remaps to a distinct transformed platform/floor owner.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
                  // Fox/Falco up-special collision owns bound/landing through its special callbacks,
                  // not the generic common-air transformed-platform sweep on the locked entry/fall
                  // rows. A same-step seed contact or live grIzumi scheduler/velocity state is
                  // the SpecialHiFall callback's current mpLib floor owner, so keep only
                  // stale/different transformed-platform projections airborne; ordinary hard
                  // floor/wall handling stays on the existing special-hi path.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialHi_Coll,ftFx_SpecialHiFall_Coll,ftFx_SpecialHiBound_Coll}
                  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
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
                  // Same hard-floor guard as the direct sweep path: a SpecialHi projection
                  // whose previous root is already below the accepted hard floor by more than the
                  // live ECB neighborhood is recovering from a missed underside collision, not source
                  // `ft_CheckGroundAndLedge` ownership.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
                  (specialhi_understage_floor_clip_action(batch->state.char_id[idx], action_id) &&
                   !stage_collision_floor_line_is_platform(stage_id, resolved_segment_i) &&
                   prev_y <
                       (proj_y - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                        k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_specialhi_from_below_hard_floor_clip =
                  // Projection/remap variant of the same SpecialHi floor-admission precondition:
                  // root projection cannot turn a floor candidate that both callback ECB bottoms
                  // already start below into a rebound/landing contact.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
                  (specialhi_floor_candidate_starts_below_source_floor(
                       batch->state.char_id[idx], action_id, prev_y, prev_bottom_y, cur_bottom_y,
                       proj_y + y_corr, batch->state.speed_y_self[idx]) &&
                   !stage_collision_floor_line_is_platform(stage_id, resolved_segment_i))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_specialairhi_floor_angle_land =
                  // Projection variant of SpecialAirHi_Coll's floor-angle branch: shallow
                  // floor.normal/self_vel contacts continue the launch and only update the
                  // collision pose, while steeper hard-floor contacts remain publishable for
                  // SpecialHiBound.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound}
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
                  // Same source boundary as the raw sweep suppressor above, after mpLib projection
                  // has remapped the candidate floor to the actual FoD height-transform segment.
                  // The retained owner is the generated AttackAirN/Hi/Lw submotion row, not the
                  // whole AttackAir callback class: first shallow transformed-platform contacts stay
                  // airborne during the extracted first HitCapsule create->clear phase until the
                  // callback reaches its stable floor handoff. Later hitbox phases and deeper/no-owner
                  // root crossings keep the ordinary ft_80082C74 floor handoff.
                  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
                  // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
                  // Projection/remap variant of the carried floor-skip shallow-root guard above.
                  // Once the FoD transformed-platform skip owner is live, the next candidate floor
                  // can be a connected hard-floor edge; source still waits until the following
                  // already-below-floor callback before snapping to LandingAir*.
                  (attackair_transformed_platform_floor_skip_active &&
                   floor_line_is_runtime_fighter_solid(g, stage_id, out_line_idx) &&
                   (!floor_x_within_line_segment_strict(batch, bi, g, out_line_idx, x) ||
                    floor_line_is_generated_stage_slope(batch, bi, g, out_line_idx)) &&
                   prev_y > (projected_contact_y + k_floor_y_bias) && y < projected_contact_y &&
                   batch->state.speed_y_self[idx] < 0.0f)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_attackair_offspan_hard_floor_edge_land =
                  // Projection/remap variant of the raw off-span hard-floor edge guard above.
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
                  // Projection/remap variant of the carried FoD floor-skip owner. While the live
                  // skip is still active, both callback roots can be below the transformed platform
                  // before source permits LandingAir* publication. Rows with no carried skip retain
                  // the ordinary ft_80082C74 handoff through the projected floor result.
                  // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
                  // Projection/remap path variant of the same mpColl floor precondition: if the
                  // resolved platform is above the current ECB bottom, mpColl_80044628_Floor has
                  // not accepted the bottom hit needed before root snapping.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && resolved_line_is_platform &&
                   cur_bottom_y > (projected_contact_y + k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_fallspecial_first_sustained_land =
                  // Projection/remap variant of the sloped-ledge main-floor first sustained
                  // FallSpecial callback guard. Keep this on generated line topology so other
                  // legal-stage FallSpecial floor handoffs continue through the normal callback
                  // path.
                  fallspecial_sloped_ledge_main_floor_first_sustained_airborne_owner(batch, idx, g,
                                                                                     stage_id);
              const uint8_t suppress_projected_escapeair_missing_bottom_owner_land =
                  // Projection/remap variant of the locked EscapeAir missing-bottom-owner guard.
                  // If the preserved desired-bottom lane is absent and the callback-local previous
                  // root is already below the accepted floor, the generic root projection is ahead
                  // of the source `mpColl_80044628_Floor` precondition.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   stage_has_height_platform_transform && ecb_lock_timer_seed != 0u &&
                   !msl_escapeair_locked_bottom_owner_any(
                       batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
                   batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias &&
                   batch->state.floor_sweep_prev_pos_y[idx] < iy)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_jumpaerial_escapeair_shallow_ledge_land =
                  // Fresh JumpAerial -> EscapeAir ledge remap depth:
                  // The generic sweep can hit/remap the carried center floor to an adjacent ledge,
                  // but source only reaches mpColl_80044838_Floor after mpCheckFloor has a bottom
                  // contact deep enough for the entered EscapeAir ECB. Shallow ledge remaps remain
                  // airborne; deeper rows still consume LandingFallSpecial through the normal
                  // EscapeAir_Coll floor callback.
                  // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
                  // Sustained EscapeAir ledge publication uses the same ft_80082C74 /
                  // mpColl_800471F8 floor handoff as the FD early-ledged floor owner above: the
                  // replay-visible fp+0x2218 allow-interrupt bit marks the callback phase where
                  // the ledge-root projection is source-owned. Without that bit, a carried center
                  // floor/index plus locked ECB bottom can project to FoD's adjacent generated
                  // ledge one callback early.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
                  // The raw mpCheckFloor sweep can hit Yoshi's low ledge floor while the carried
                  // CollData.floor.index still names the side platform. Do not let the projection
                  // helper replace that source floor hit with an elevated transformed-platform snap;
                  // consume the raw floor hit and keep the low hard-floor/ledge placement.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
                  // SpecialAirHi_Coll consumes ft_CheckGroundAndLedge -> mpColl_800473CC. Source
                  // stays airborne for shallow floor.normal/self_vel contacts, but
                  // mpColl_80044948_Floor still applies the floor correction and leaves the
                  // CollData floor env flags/normal for the callback's facing/rotateModel update.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound}
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpColl_800473CC,mpColl_80044628_Floor,mpColl_80044948_Floor}
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
              // Sweep saw a floor segment, but projection failed (often an off-end / edge case).
              // Source mpColl_80044838_Floor handles this by snapping the ECB bottom to the floor
              // endpoint after mpColl_80044628_Floor has accepted the floor hit. This is shared by
              // AttackAir/common-air/damage floor callbacks that route through mpColl_80046904.
              // DownDamage_Coll's ft_80081DD4 floor path can also resolve from the root point when
              // post-hitlag common Damage Phys has moved the ECB bottom past the normal projection
              // point. Keep that root fallback bounded to the same downward-KB owner used by the
              // resting-contact path below; other actions keep edge-suppression diagnostics.
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
              //   ftCo_DownDamage_Phys,ftCo_DownDamage_Coll}
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
                // Propagate edge suppression bits so mpColl-shaped ledge-grab checks can apply the
                // `on_edge` gate deterministically.
                batch->state.coll_env_flags[idx] |=
                    floor_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id,
                                                 anim, ecb_frame, was_grounded, loaded_ecb.current);
              }
            }
          }
          // Decomp anchor for this gate:
          // - Damage/DamageFly states own landing/DownBound decisions in their collision callbacks.
          // - The shared CollData floor-result path above now owns proven DamageAir active-hitlag,
          //   DamageFly hitlag-exit, DamageFlyRoll, and DownDamage floor publication slices. Keep
          //   generic "resting contact" floor projection out of the remaining hitstun frames so
          //   root-only state cannot bypass the callback-local loaded-ECB/bottom-sweep owner.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
        } else {
          if (prefer_line_idx >= 0 && !escapeair_sustained_floorhug_airborne &&
              !escapeair_fresh_horizontal_floorhug_airborne && !specialhi_bound_entry_airborne &&
              (batch->state.speed_y_self[idx] == 0.0f ||
               downdamage_active_hitlag_resting_floor_contact) &&
              batch->state.hitlag[idx] == 0 &&
              (batch->state.hitstun[idx] == 0 || downdamage_active_hitlag_resting_floor_contact)) {
            // Decomp: mpLib_8004DD90_Floor can resolve a resting contact even when no crossing
            // sweep is reported (e.g. vy==0 and the ECB bottom is already on the surface). Gate this
            // to "already on the surface" to avoid snapping to the floor from far below.
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
              // DownDamage hitlag-exit can resolve from the root point when downward KB has already
              // pushed the fighter into the persisted floor line before the visible self velocity
              // resumes. Bound the downward snap by the attack-KB displacement plus the ECB vertical
              // unit used by the callback path; this keeps ordinary airborne DownDamage rows out of
              // the floor-contact owner.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Coll
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
          // Terminal DamageFly IASA can enter Fall before the same frame's collision callback. The
          // destination Fall_Coll still consumes `ft_800831CC -> mpColl_80047E14(flags=6)` with the
          // carried CollData hard-floor index, so an already-below-floor root projects through
          // mpColl_80044838_Floor and lands immediately. Keep this off soft/ledge floors and bound
          // the projection by the already-below-floor carried root depth plus the terminal frame's
          // vertical displacement/KB and ECB unit.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_Coll}
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
        // Current stage-object support lifetime:
        // mpColl's static floor graph should not select the Shy Guy support line as a new floor,
        // but source CollData can still carry its current floor/support owner while the stage object
        // controller is live. Preserve the grounded support for this callback pass; ordinary
        // fighter-solid floors and new raw-line contacts continue through normal mpColl paths.
        // refs/melee/src/melee/lb/types.h::CollData
        // refs/melee/src/melee/gr/grstory.c::grStory_801E3418
        // refs/melee/src/melee/it/items/itheiho.c
        on_ground = 1u;
        floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAGE_OBJECT_CARRY;
        ground_id = seed_ground_id;
        contact_x = cur_bottom_x;
        contact_y = cur_bottom_y;
      }

      if (on_ground && was_grounded) {
        // Source ordering retries ceiling after a grounded floor collision in mpColl_8004ACE4.
        // Carry source-equivalent floor squeeze state into the retry so a pre-floor ceiling hit or
        // retry ceiling hit can trigger mpCollSqueezeVertical.
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
          is_capture_lw_allow_ground_to_air_collision_action(action_id) &&
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_STAND_B &&
          batch->state.action_frame[idx] <= 2 && ground_id != seed_ground_id &&
          prefer_line_idx >= 0 && (size_t)prefer_line_idx < g->line_count &&
          g->lines[(size_t)prefer_line_idx].segment_i == seed_ground_id &&
          g->lines[(size_t)prefer_line_idx].is_ledge &&
          !stage_collision_floor_line_is_sloped(stage_id, seed_ground_id)) {
        const uint8_t owner_p = batch->state.grab_owner_port[idx];
        uint8_t owner_on_different_floor = 0u;
        if (owner_p < batch->config.num_players) {
          const size_t oidx = msl_idx_player(bi, (int)owner_p);
          owner_on_different_floor =
              (uint8_t)(batch->state.action_id[oidx] == (uint16_t)MSL_ACT_CATCH_DASH_PULL &&
                        g->lines[(size_t)prefer_line_idx].joint_id == 0 &&
                        batch->state.on_ground[oidx] != 0u &&
                        batch->state.ground_id[oidx] != seed_ground_id);
        }
        if (owner_on_different_floor != 0u) {
          // CapturePulledLw flat-ledge CollData floor carry finalization:
          // the grounded floor path can retry wall/ceiling after `mpColl_8004B108`, which
          // re-materializes the adjacent main-floor direct result. Source low-capture callbacks
          // still carry `CollData.floor.index` for flat ledge floors when the victim is attached
          // to an owner on another floor. Reassert that carried floor immediately before final
          // publication; sloped same-floor ledges remain on the PulledHi floor-loss discriminator
          // in grab_attachment.c.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
          //   ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ft_8008403C
          // data/stages/bin/*.bin::MSLSTG01 segment.{ledge,endpoints}
          ground_id = seed_ground_id;
          contact_x = cur_bottom_x;
          contact_y = g->lines[(size_t)prefer_line_idx].y0;
          floor_nx = 0.0f;
          floor_ny = 1.0f;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
              (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION, ground_id, contact_x, contact_y,
              floor_nx, floor_ny);
        }
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
        // Final source retry for grounded inline2-style collision callbacks:
        // mpColl_8004ACE4 calls mpColl_8004A908_Floor after the ordinary floor/edge/ceiling loop.
        // The retained slice below owns both `mpColl_8004A908_Floor` sweeps: first previous
        // bottom, then previous ECB side-midpoint Y, accepting only a floor that is different from
        // and not connected to the persisted CollData.floor.index.
        //
        // This is deliberately not row/action-gated: the retry is part of the shared CollData floor
        // substrate for grounded callbacks. Airborne callbacks continue through their existing
        // `mpColl_80046904` / `mpColl_80047E14` owners above.
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004ACE4,mpColl_8004A908_Floor}
        if (floor_4a908_retry(batch, idx, bi, g, stage_id, prefer_line_idx, prev_bottom_x,
                              prev_bottom_y, prev_side_mid_y, cur_bottom_x, cur_bottom_y,
                              skip_platform_segment_i, &ground_id, &contact_x, &contact_y,
                              &floor_nx, &floor_ny)) {
          // Retained source consumer: the grounded inline2 callback consumes the local
          // mpColl_8004A908_Floor retry result after ordinary floor/edge/ceiling passes.
          // Store the result in CollData-shaped callback scratch, then let the shared final
          // grounded writeback consume that scratch below.
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004ACE4,mpColl_8004A908_Floor}
          mpcoll_record_callback_floor_result(&mpcoll_ctx,
                                              (uint8_t)MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY,
                                              ground_id, contact_x, contact_y, floor_nx, floor_ny);
          on_ground = 1u;
        }
      }
      if (on_ground && was_grounded &&
          is_capture_lw_allow_ground_to_air_collision_action(action_id) &&
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_STAND_B &&
          batch->state.action_frame[idx] <= 2 && ground_id != seed_ground_id &&
          prefer_line_idx >= 0 && (size_t)prefer_line_idx < g->line_count &&
          g->lines[(size_t)prefer_line_idx].segment_i == seed_ground_id &&
          g->lines[(size_t)prefer_line_idx].is_ledge &&
          !stage_collision_floor_line_is_sloped(stage_id, seed_ground_id)) {
        const uint8_t owner_p = batch->state.grab_owner_port[idx];
        uint8_t owner_on_different_floor = 0u;
        if (owner_p < batch->config.num_players) {
          const size_t oidx = msl_idx_player(bi, (int)owner_p);
          owner_on_different_floor =
              (uint8_t)(batch->state.action_id[oidx] == (uint16_t)MSL_ACT_CATCH_DASH_PULL &&
                        g->lines[(size_t)prefer_line_idx].joint_id == 0 &&
                        batch->state.on_ground[oidx] != 0u &&
                        batch->state.ground_id[oidx] != seed_ground_id);
        }
        if (owner_on_different_floor != 0u) {
          // CapturePulledLw flat-ledge CollData floor carry after mpColl_8004A908 retry:
          // the grounded inline2 retry can find the adjacent main floor after the ordinary
          // `mpColl_8004B108` pass. Low-capture source callbacks still carry the original flat
          // ledge `CollData.floor.index` while attached to an owner on another floor.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
          //   ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_8004A908_Floor}
          // data/stages/bin/*.bin::MSLSTG01 segment.{ledge,endpoints}
          ground_id = seed_ground_id;
          contact_x = cur_bottom_x;
          contact_y = g->lines[(size_t)prefer_line_idx].y0;
          floor_nx = 0.0f;
          floor_ny = 1.0f;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
              (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION, ground_id, contact_x, contact_y,
              floor_nx, floor_ny);
        }
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
        // EscapeAir hard-floor bottom-sweep producer:
        // `EscapeAir_Coll` delegates to `ft_80082C74 -> ft_80081D0C -> mpColl_800471F8`, whose
        // `mpColl_80044628_Floor` floor check consumes the live callback ECB bottom even when the
        // carried CollData floor still names a stale platform. Keep this to runtime-owned
        // `mpCollPrev` state; soft-platform and restored-only handoffs stay on their explicit
        // ordered owners. LEDGE floor lines are admitted per source (mpCheckFloor has no ledge
        // filter; the Yoshi's lip slope is an ordinary landable floor), and acceptance mirrors
        // mpColl_80044628_Floor: dd90 projection when it lifts the bottom, else land exactly on
        // the sweep contact (the dd90 `y > 0` gate belongs to its wall-adjacent fallback only).
        //
        // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_471F8
        // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform_transform metadata
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8,
        //   mpColl_80044628_Floor,mpColl_80044838_Floor}
        const uint8_t escapeair_hard_floor_sweep_hit =
            mpcoll_collect_bottom_sweep_hard_floor_result(
                batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                cur_bottom_y, prefer_line_idx, -1, 1u, &escapeair_hard_floor_sweep);
        const uint8_t escapeair_dd90_accept =
            (uint8_t)(escapeair_hard_floor_sweep_hit &&
                      escapeair_hard_floor_sweep.projected_line_idx >= 0 &&
                      escapeair_hard_floor_sweep.projected_y_corr >= 0.0f &&
                      batch->state.pos_y[idx] < escapeair_hard_floor_sweep.hit_y - k_floor_y_bias);
        // Contact-snap fallback stays scoped to ledge hits: that is the exact union of the
        // previously separate non-ledge (dd90-only) and ledge-sibling producers. Widening the
        // snap to dd90-rejected non-ledge hits is a separate slice with its own validation.
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
          on_ground = 1u;
          contact_x = escapeair_hard_floor_sweep.hit_x;
          contact_y = escapeair_hard_floor_sweep.hit_y;
          floor_nx = escapeair_hard_floor_sweep.normal_x;
          floor_ny = escapeair_hard_floor_sweep.normal_y;
          floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
              contact_x, contact_y, floor_nx, floor_ny);
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
          // Live JumpAerial -> EscapeAir desired-bottom hard-floor producer:
          // `ftCo_EscapeAir_Coll` calls `ft_80082C74 -> mpColl_800471F8`; with a live
          // JumpAerial CollData_X130 lock, `mpColl_LoadECB_inline` preserves the callback-local
          // desired ECB bottom. That desired bottom, not a stale public ground id or root-only row,
          // is the source floor producer that can cross an ordinary fighter-solid hard floor while
          // early EscapeAir root/zero-bottom rows remain airborne.
          //
          // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 phase AIR_471F8
          // data/stages/bin/*.bin::MSLSTG01 fighter_solid/platform_transform metadata
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_800471F8,
          //   mpColl_80044628_Floor}
          batch->state.pos_y[idx] += escapeair_desired_hard_floor_sweep.projected_y_corr;
          on_ground = 1u;
          ground_id = escapeair_desired_hard_floor_sweep.projected_segment_id;
          contact_x = escapeair_desired_hard_floor_sweep.hit_x;
          contact_y = escapeair_desired_hard_floor_sweep.hit_y;
          floor_nx = escapeair_desired_hard_floor_sweep.normal_x;
          floor_ny = escapeair_desired_hard_floor_sweep.normal_y;
          floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_BOTTOM_SWEEP;
          escapeair_live_nonplatform_root_floor_authority = 1u;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
              contact_x, contact_y, floor_nx, floor_ny);
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
        // Sustained JumpAerial -> EscapeAir hard-floor handoff:
        // source `EscapeAir_Coll` runs the live ft_80082C74/mpColl_800471F8 floor producer with
        // the runtime CollData_X130 owner written by the earlier JumpAerial/EscapeAir callback.
        // This admits only the carried same non-platform floor after the current callback root
        // projection consumes that live producer. Restored one-step CollData.floor/desired-bottom
        // state does not set the live owner and therefore cannot publish through this lane.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{
        //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
        batch->state.pos_y[idx] = escapeair_projection_line_y + k_floor_y_bias;
        on_ground = 1u;
        ground_id = escapeair_live_carried_floor_id;
        contact_x = x;
        contact_y = escapeair_projection_line_y;
        floor_nx = escapeair_projection_line_nx;
        floor_ny = escapeair_projection_line_ny;
        floor_result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_ROOT_PROJECTION;
        escapeair_live_nonplatform_root_floor_authority = 1u;
        mpcoll_record_callback_floor_result_with_mode(
            &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, floor_result_mode, ground_id,
            contact_x, contact_y, floor_nx, floor_ny);
        mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
      }
      if (!on_ground && escapeair_floor_producer_authority_in != 0u &&
          escapeair_next_root_reaches_carried_floor) {
        // Source-owned current-floor continuation for the next EscapeAir_Coll callback: the live
        // carried hard floor is still the same CollData floor and the current callback's root
        // projection velocity reaches it before the next map pass. Reseed rows and non-EscapeAir
        // callbacks cannot set this lane, so it is not a visible ground_id/root-crossing substitute.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
        mpcoll_record_escapeair_floor_producer_runtime_authority(&mpcoll_ctx);
      }
      if (!on_ground && escapeair_floor_producer_authority_in != 0u &&
          escapeair_live_carried_same_hard_floor &&
          y > (escapeair_projection_line_y + k_floor_y_bias)) {
        // The live EscapeAir_Coll floor producer remains current while the same carried hard-floor
        // callback stays airborne above the floor. Keep the runtime-only authority until the later
        // root projection consumes it; restored seeds cannot enter this path because they never set
        // `escapeair_floor_producer_authority_in`.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
        // EscapeAir_Coll's mpColl_800471F8 path can admit a connected hard-floor projection even
        // when the simplified bottom sweep has already moved below the line. Live JumpAerial ->
        // Live JumpAerial EscapeAir CollData owners use root crossing; replay-seeded CollData_X130
        // rows and the late JumpAerial IASA handoff use the explicit desired-bottom lane and
        // require that desired bottom to cross the carried non-platform floor this frame. The late
        // handoff stays on the same source ground segment; adjacent floor traversal is owned by the
        // older source-specific EscapeAir slices above, not this first visible callback. Sustained
        // seeded EscapeAir continuations are not retained here: without a fresh source
        // desired-bottom producer they must not reuse a stale CollData slice as floor authority.
        // Already-below locked bottoms are still rejected by the publication guard below. The
        // accepted floor remains fighter-solid and non-platform; soft-platform pass-through is still
        // owned by the explicit locked desired-bottom slice above.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
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
          // Current live EscapeAir callback has accepted the carried non-platform floor through
          // the same root-projection producer above. This is source authority only because the
          // CollData_X130 owner is runtime-written by JumpAerial/EscapeAir callback flow; direct
          // reseed rows with a restored floor id and root crossing keep owner=seeded/none and
          // therefore cannot publish through this lane.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
          // Landing_Coll may consume a transformed FoD platform only when the callback has a
          // current same-step/contact source owner, a fresh action-entry CollData handoff, or a
          // live grIzumi scheduler/velocity owner. `mpColl_8004B4B0` uses the floor-release helper
          // first, then retries a floor sweep with the carried floor as `floor_skip`; a live moving
          // FoD platform can therefore replace sustained Landing's carried hard-floor CollData.
          // Rows with only seed-provided FoD platform height and no live scheduler/contact source
          // restore carried floor here; this remains a source-authority guard against stale sparse
          // height seeds, not a moving-platform deferral.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC}
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
          ground_id = batch->state.ground_id[idx];
          contact_x = x;
          contact_y = current_line_y;
          floor_nx = batch->state.ground_normal_x[idx];
          floor_ny = batch->state.ground_normal_y[idx];
          batch->state.pos_y[idx] = current_line_y + k_floor_y_bias;
          mpcoll_record_callback_floor_result_with_mode(
              &mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
              (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAGE_OBJECT_CARRY, ground_id, contact_x, contact_y,
              floor_nx, floor_ny);
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
          is_capture_lw_allow_ground_to_air_collision_action(action_id) &&
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_STAND_B &&
          batch->state.action_frame[idx] <= 2 &&
          floor_publication.contact.ground_id != seed_ground_id && prefer_line_idx >= 0 &&
          (size_t)prefer_line_idx < g->line_count &&
          g->lines[(size_t)prefer_line_idx].segment_i == seed_ground_id &&
          g->lines[(size_t)prefer_line_idx].is_ledge &&
          !stage_collision_floor_line_is_sloped(stage_id, seed_ground_id)) {
        const uint8_t owner_p = batch->state.grab_owner_port[idx];
        uint8_t owner_on_different_floor = 0u;
        if (owner_p < batch->config.num_players) {
          const size_t oidx = msl_idx_player(bi, (int)owner_p);
          owner_on_different_floor =
              (uint8_t)(batch->state.action_id[oidx] == (uint16_t)MSL_ACT_CATCH_DASH_PULL &&
                        g->lines[(size_t)prefer_line_idx].joint_id == 0 &&
                        batch->state.on_ground[oidx] != 0u &&
                        batch->state.ground_id[oidx] != seed_ground_id);
        }
        if (owner_on_different_floor != 0u) {
          // CapturePulledLw flat-ledge final publication owner:
          // all grounded floor producers feed the callback scratch before wrapper writeback. If
          // the later direct/4A908 producers traverse from the carried flat ledge to the adjacent
          // main floor, source low-capture callbacks still publish the original CollData floor
          // while the attached owner is on another floor.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
          //   ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_8004A908_Floor}
          // data/stages/bin/*.bin::MSLSTG01 segment.{ledge,endpoints}
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
          // SpecialHiFall's current-frame FoD platform contact consumes the same
          // mpLib_8004DD90_Floor correction as hard floors. The generic transformed-platform
          // projection path can publish the floor id while leaving cur_pos on the raw line; apply
          // the source floor bias for the transient same-step grIzumi contact.
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Coll
          batch->state.pos_y[idx] += k_floor_y_bias;
        }
        const uint8_t suppress_escapeair_transformed_remap_land =
            // Final guard for FoD's transformed-platform remap path: several source-shaped
            // projection owners can set `on_ground` before the generic sweep suppression sees the
            // remapped line id. EscapeAir's locked entry frames should not convert to
            // LandingFallSpecial when the accepted transformed platform differs from the carried
            // CollData.floor.index.
            //
            // Fresh Jump/JumpAerial -> EscapeAir rows are the exception: IASA has changed the
            // motion state before Fighter_procMap, and the same EscapeAir_Coll callback has already
            // accepted the transformed-platform floor result. Do not discard that callback-local
            // result only because mpLib_8004DD90_Floor remapped the line across FoD's platform
            // graph. Scope the guard to elevated contacts: low Yoshi hard-floor remaps are ordinary
            // callback landings, and suppressing them produces rollout-only EscapeAir regressions.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && batch->state.action_frame[idx] <= 4 &&
             contact_y > k_floor_y_bias && resolved_line_has_platform_transform &&
             !escapeair_locked_platform_root_projection_hit &&
             !escapeair_no_lock_static_platform_sweep_hit &&
             ground_id != batch->state.ground_id[idx] &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_F &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_B &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_B)
                ? 1u
                : 0u;
        const uint8_t suppress_specialairhi_platform_land =
            // SpecialAirHi platform floor contact is consumed by ftCo_8009A134 inside
            // ftFox_SpecialHi_IsBound: platforms call mpUpdateFloorSkip and keep the launch
            // airborne instead of publishing a grounded/Bound handoff. The floor contact can still
            // snap cur_pos to the platform for that callback pass; the source-owned floor_skip then
            // excludes the same platform on following floor checks.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
            // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
            ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) &&
             stage_collision_floor_line_is_platform(stage_id, ground_id))
                ? 1u
                : 0u;
        const uint8_t suppress_sheik_vanish_start1_platform_pass_land =
            // Sheik Vanish Start1 uses a different ft_CheckGroundAndLedge branch than Firefox:
            // accepted platform contact during the early xC < ftSeakAttributes::x3C window calls
            // ftCo_8009A134, writes CollData.floor_skip, and keeps the current airborne travel
            // root rather than publishing grounded SpecialHiStart_1. When mpLib resolves the
            // accepted platform to a different floor segment than the carried CollData floor, keep
            // the callback-current root snap from mpColl_80044628_Floor; same-segment floor_skip
            // consumption only rejects the grounded publication and restores the pre-publication
            // airborne root.
            // data/characters/sheik.json::{sheik_vanish_travel_frames,
            //   sheik_vanish_ground_contact_min_frames}
            // data/motion_state/owners/sheik.bin::MSLMSO01 FT_CHECK_GROUND_LEDGE_AIR_COLL
            // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
            //   ftSk_SpecialAirHiStart_1_Anim,ftSk_SpecialAirHiStart_1_Coll}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
            (sheik_special_vanish_air_start1_platform_pass_active(batch, idx) &&
             stage_collision_floor_line_is_platform(stage_id, ground_id))
                ? 1u
                : 0u;
        const uint8_t sheik_vanish_start1_platform_pass_snaps_new_floor =
            (uint8_t)(suppress_sheik_vanish_start1_platform_pass_land &&
                      ground_id != batch->state.ground_id[idx]);
        const uint8_t suppress_specialairlw_start_stale_platform_land =
            // Aerial Shine startup enters from JumpF/B before Fighter_procMap. Source
            // `ftFx_SpecialAirLwStart_Coll -> ft_80081D0C` sets CollData.last_pos from the
            // callback-current root, then calls `mpColl_800471F8`; it cannot publish a platform
            // floor that is still above the current ECB bottom only because the prior JumpF/B
            // sweep crossed it. Later Shine air frames and true current-bottom platform contacts
            // keep the normal AirToGround path.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
            //   ftFx_SpecialAirLw_Enter,ftFx_SpecialAirLwStart_Coll}
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
            // SpecialHiFall transformed-platform contacts remain owned by the Fox/Falco
            // up-special fall callback. Same-step seed contacts and live grIzumi
            // scheduler/velocity contacts are current callback ownership from grIzumi/mpLib and may
            // publish SpecialHiLanding; stale
            // transformed-platform remaps, hard floors, and same-floor followups retain the
            // existing path.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialHiFall_Coll,ftFx_SpecialHiBound_Coll}
            // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
            (resolved_line_has_platform_transform &&
             (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL) &&
             !resolved_specialhi_height_platform_current_source &&
             ground_id != batch->state.ground_id[idx])
                ? 1u
                : 0u;
        const uint8_t suppress_specialhi_understage_hard_floor_land =
            // Final writeback guard for SpecialHi hard-floor clips from inside the stage. Some
            // floor result paths can set `on_ground` after the direct/projection suppression sites;
            // if the previous root was below the accepted hard floor by more than the live ECB
            // neighborhood, source collision should have consumed the stage underside before
            // `ft_CheckGroundAndLedge` could publish a landing.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
            (specialhi_understage_floor_clip_action(batch->state.char_id[idx], action_id) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             prev_y < (contact_y - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                       k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t suppress_specialhi_from_below_hard_floor_land =
            // Final writeback variant for SpecialAirHi/SpecialHiFall floor candidates that start
            // below the accepted hard floor. Source floor publication requires the live ECB bottom
            // sweep to enter the top surface; a later root projection must not synthesize a
            // floor-contact bit from inside the stage shell.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
            (specialhi_floor_candidate_starts_below_source_floor(
                 batch->state.char_id[idx], action_id, prev_y, prev_bottom_y, cur_bottom_y,
                 contact_y, batch->state.speed_y_self[idx]) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id))
                ? 1u
                : 0u;
        const uint8_t suppress_specialairhi_floor_angle_land =
            // SpecialAirHi_Coll only enters SpecialHiBound from a floor contact when the angle
            // between floor.normal and self_vel is outside the character's bound threshold.
            // Shallow floor-angle contacts keep the launch airborne and only rotate the model;
            // steeper hard-floor contacts must remain publishable so ftFx_SpecialHiBound_Enter can
            // consume them. Platform contacts are handled by the separate ftCo_8009A134/floor-skip
            // owner above.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound,ftFx_SpecialHiBound_Enter}
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
            // Source platform-pass ownership is input-lifetime sensitive. Once a carried
            // transformed-platform floor_skip reaches an AttackAir_Coll callback with released
            // pass input, the skip is no longer a source reason to reject LandingAir*.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
            // Final-publication variant of the strict in-span down-held FoD height-platform
            // AttackAir pass above. Endpoint-extension, same-floor, and current-source contacts
            // remain ordinary landing candidates; only a generated moving-platform floor result
            // whose root is strictly inside a different live segment and lacks current grIzumi/
            // mpLib source or live scheduler ownership can consume the one-way platform pass
            // decision.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
            // Final-publication variant of the first shallow FoD AttackAir transformed-platform
            // contact. Source AttackAir_Coll is in the MSLFTSC1 first HitCapsule create->clear
            // phase and preserves the callback-local CollData floor owner for the next frame only
            // when the transformed-platform pass owner is live: carried floor_skip or callback-
            // visible down-held in-span input. Released/no-skip shallow contacts retain ordinary
            // floor publication.
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
            // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
            // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
            // Marth-style multi-hit AttackAirN/Lw scripts can reach a post-clear tail while
            // cmd_var[0] still marks the aerial as landing-lag eligible. Source AttackAir_Coll
            // still needs a callback-local mpColl_80044628_Floor producer before
            // mpColl_80044838_Floor may publish LandingAir*. A sparse FoD height-platform line
            // reconstructed from seed height, with no current/same-step/live scheduler source and
            // no bottom/projection floor probe, is not that producer.
            //
            // data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id (late N/Lw owner)
            // data/scripts/<char>.bin::MSLFTSC1 set_cmd_var/create_hitbox/clear_hitboxes events
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            //   mpColl_80044838_Floor}
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
            // Final-publication variant of the FoD side-platform current-source boundary above.
            // A named platform height can reconstruct the line, but AttackAir_Coll still reaches
            // LandingAir* through the callback-local mpColl floor producer. In the early generated
            // create-hitbox band, a height-platform result with no current grIzumi/mpLib source is
            // only a restored/final projection candidate; source keeps the aerial airborne and the
            // rejected platform becomes the carried floor-skip owner for subsequent callbacks.
            // Later AttackAir rows, current-source contacts, and live scheduler contacts retain
            // ordinary floor publication.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
            // refs/melee/src/melee/mp/mpcoll.c::mpUpdateFloorSkip
            // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
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
            // AttackAirN's common collision callback is not allowed to promote a restored FoD
            // height-platform line into LandingAirN only because sparse seed/provenance named a
            // plausible platform height. Keep two source-owned cases here:
            // - restored/sparse platform contacts whose resolved segment differs from carried
            //   CollData.floor.index;
            // - the extracted late-owner tail after the hitbox script lifetime has ended.
            // Earlier same-carried-platform NAir root crossings can still publish LandingAirN
            // through the ordinary ft_80082C74 floor handoff.
            //
            // data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id
            // data/scripts/<char>.bin::MSLFTSC1 set_cmd_var/create_hitbox/clear_hitboxes events
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            //   mpColl_80044838_Floor}
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
            // Before AttackAirLw's first create_hitbox command has interpreted, the common
            // AttackAir_Coll path has not reached the script-authored DAir platform publication
            // owner. A restored FoD height-platform contact can name a plausible line, but source
            // still keeps the pre-create callback airborne; the adjacent first-create callback may
            // publish through the normal live platform path.
            //
            // data/motion_state/owners/<char>.bin::MSLMSO01 submotion_id
            // data/scripts/<char>.bin::MSLFTSC1 AttackAirLw create_hitbox events
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            //   mpColl_80044838_Floor}
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
            // Jump_Coll routes through ft_800835B0 with the soft-platform callback. During
            // upward JumpF/JumpB motion, transformed-platform sweeps can observe the platform
            // before the source callback publishes the landing handoff. Ordinary falling JumpF/B
            // one-way-platform contacts are kept on the landing path; rows that depend on hidden
            // grIzumi phase need source-owned scheduler state rather than a local below-root
            // rejection guard.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
            (resolved_line_has_platform_transform &&
             (action_id == (uint16_t)MSL_ACT_JUMP_F || action_id == (uint16_t)MSL_ACT_JUMP_B) &&
             batch->state.speed_y_self[idx] > k_floor_horiz_dy_thresh)
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_transformed_platform_ecb_only_final_land =
            // Final publication guard for the same sustained AttackAirN/Hi/Lw height-transform owner
            // handled in the raw/projection floor-sweep sites above. Some paths record the accepted
            // FoD platform only as final floor contact; reject sustained AttackAir ECB-bottom snaps
            // on the resolved moving platform while the extracted command script is inside its
            // first HitCapsule create->clear phase and a real floor-skip/pass/shallow-first-contact
            // owner is active. Fresh action entry, later hitbox phases, released contacts, deeper
            // transformed-platform contacts, and AttackAir submotions outside this generated owner
            // retain normal final floor publication.
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
            // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
            // Final publication guard for the carried FoD AttackAir platform-skip owner when the
            // floor result appears only after projection/remap. The first root crossing after the
            // transformed-platform pass stays airborne for one more source callback pass; already-
            // below-floor followups retain normal LandingAir* publication.
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
            // Final writeback variant for the same AttackAir_Coll endpoint-extension family. When
            // mpLib resolves a hard-floor candidate outside the strict segment span, the source
            // floor callback keeps the late-hit AttackAir row airborne for that first edge crossing;
            // in-span hard-floor contacts still publish LandingAir* normally.
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
            // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox events
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
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
            // Source `mpColl_800471F8` reaches `mpColl_80044838_Floor` only after
            // `mpColl_80044628_Floor` accepts the live ECB-bottom floor sweep. On generated hard
            // floors, a root crossing can project onto the connected floor while both previous and
            // current ECB bottoms are still above that floor; keep that first root-only
            // projection airborne and let the following bottom-owned callback publish LandingAir*.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            //   mpColl_80044838_Floor}
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
            // Single-create AttackAirF scripts still use the same common collision callback as
            // multi-hit aerials: ftCo_AttackAir_Coll -> ft_80082C74 -> mpColl_800471F8. For FoD
            // height-transform platforms, a replay seed can name a plausible platform line without
            // proving that this callback has current grIzumi/mpLib source ownership. The source
            // callback may publish LandingAirF only after mpColl_80044628_Floor accepts a
            // callback-local ECB-bottom floor result; a final root/line carry result from below is
            // not enough. Keep low hard-floor fair landings on the ordinary path: those are valid
            // mpColl_80044838 publications once the bottom-owner predicate has fired.
            //
            // Use the extracted submotion and MSLFTSC1 script shape (first create present, no
            // second create) to cover Marth fair-style height-platform contacts without making
            // Fox/Falco multi-hit fair or unrelated single-create aerials inherit this pending
            // owner.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            //   mpColl_80044838_Floor}
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
            // Final writeback variant of the carried FoD floor-skip owner above. Keep it scoped to
            // a live transformed-platform skip or off-span endpoint contact; released/no-skip
            // AttackAir rows can land through the ordinary ft_80082C74 final floor result even if
            // the root is already below the moving platform. Source floor ownership comes from the
            // accepted ECB-bottom hit, so an in-span transformed-platform result must not be
            // rejected only because the public root is already below the line unless the
            // source-owned floor_skip platform-pass lane is still live.
            // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
            // Sustained JumpAerial_Coll uses the same ft_800835B0 -> mpColl_80047E14 callback
            // family, but FoD height-transformed platform fastfall rows can observe a platform
            // sweep before the callback-local floor result is published as a landing only while a
            // generated platform floor.index is already carried and the root was already below the
            // platform. Fresh hard-floor-index rows, current-frame grIzumi same-step contacts, and
            // callback-local root crossings onto FoD side platforms use the ordinary
            // JumpAerial_Coll floor handoff.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80043754,mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
            // JumpAerial_Coll reaches final floor projection only after mpColl_80044628_Floor has
            // accepted a floor through ftCo_80096CC8. A static soft-platform rejection is
            // source-owned only when the current callback stick is below x25C, or when CollData
            // already carries that platform as floor_skip. Direct replay rows do not serialize that
            // hidden floor_skip every frame, so recover the same owner only when the previous
            // down-held callback's ECB-bottom sweep had already crossed below this static platform.
            // Previous-stick input by itself is not a hidden source lane; released-stick rows whose
            // prior callback endpoint was still above the platform publish ordinary Landing.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
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
            // `ftCo_Jump_Anim` runs before Fighter_procMap and enters Fall via
            // `ftCo_Fall_Enter`; the following collision callback is Fall_Coll even though the
            // frame-start owner was JumpF/B. Keep this with the transformed-platform fastfall
            // callback lifetime split below instead of publishing a Landing from stale carried
            // FoD platform floor state on the terminal Jump frame. Direct replay seeds can expose
            // the same first Fall callback as `seed_prev_action_id=JumpF/B`; this is still the
            // terminal jump anim owner, not sustained Fall.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
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
            // Fall_Coll routes through ft_800831CC -> mpColl_80047E14 with the same
            // ftCo_80096CC8 platform callback as Jump/JumpAerial. FoD height-transformed platform
            // rows can see a remapped platform sweep before the source callback publishes a landing
            // result; preserve the airborne Fall frame for off-span sustained same-action fastfall
            // contacts, down-held platform-pass contacts, and terminal JumpF/B -> Fall anim-owner
            // contacts. A true sustained in-span ECB-bottom hit without the ftCo_80096CC8 pass owner
            // is the mpColl_80044838_Floor publication path and lands normally even when the replay
            // seed same-step source bit has already been consumed by rollout; terminal Jump -> Fall
            // has not yet reached that sustained Fall owner.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
            // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80047E14,
            //   mpColl_80044838_Floor}
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
            // Fall's looping AObj can wrap the visible action frame one callback before the
            // callback-local floor owner publishes the carried stage-object floor -> hard-floor
            // handoff. Keep this to true loop-wrap Fall_Coll rows with a prefix-causal generated
            // platform floor.index and no live allow-interrupt bit; the following
            // below-floor callback remains on the ordinary Fall_Coll landing path.
            // data/stages/bin/*.bin::MSLSTG01 platform transform records
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
            //   ftCo_Fall_Anim,ftCo_Fall_Coll}
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
            // Fall_Coll stale one-way-platform first hard-floor contact:
            // `Fall_Coll -> ft_800831CC -> mpColl_80047E14(flags=6)` first needs
            // `mpColl_80044628_Floor` to accept a callback-local floor before
            // `mpColl_80044838_Floor(ignore_bottom=true)` can publish Landing. A carried
            // CollData.floor.index naming a soft platform is not enough source authority to publish
            // the hard floor underneath on the first shallow non-fastfall bottom crossing; vanilla
            // keeps Fall airborne and lands on the next deeper callback. Keep this to sustained
            // Fall rows carrying a generated one-way platform, no active ECB lock, a newly selected
            // ordinary hard floor, callback-local CollData facing moving opposite the horizontal
            // air drift, and a sub-unit bottom penetration. This is the first-frame edge where
            // `ft_80083090_inline` has called `mpCollSetFacingDir` for the loaded Fall ECB before
            // `mpColl_80044628_Floor`; the stale soft-platform floor.index is still the carried
            // floor authority, while the hard-floor bottom sweep has not yet reached the deeper
            // `mpColl_80044838_Floor` publication source. Same-facing first hard-floor crossings,
            // opposite-facing deeper contacts, fastfall/platform-source rows, and later deeper
            // callbacks stay on the existing Fall_Coll owners.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
            // Fall_Coll shallow hard-floor publication guard:
            // when frame-0 Fall_Coll starts below a generated terminal-cardinal hard floor but the
            // current ECB bottom is still inside the one-unit floor neighborhood, the
            // callback-local mpColl_80044838_Floor owner can keep non-fastfall Fall airborne until
            // the next deeper callback. Require the Fall script x2218 allow/B1 phase and the source
            // frame-0 predecessor to avoid borrowing the guard for later Fall rows or
            // DamageAir-entry Fall rows whose callback-local damage ECB already owns a deeper floor
            // publication. Fastfall and deeper bottom penetrations continue through the ordinary
            // landing path.
            // data/stages/bin/*.bin::MSLSTG01 floor flags/links/platform-transform metadata
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
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
            // Fall_Coll static soft-platform one-way admission:
            // `ft_800831CC -> mpColl_80047E14` first needs
            // `mpColl_80044628_Floor` to accept a callback-local floor. A sustained Fall row whose
            // previous and current callback roots are already below a static one-way platform has
            // not produced that source floor hit. If CollData is also carrying that same stale
            // one-way platform id, final root projection must not treat the carried id as a fresh
            // acceptance and snap upward to the platform. True above->platform crossings are owned
            // by the callback-local ECB bottom interval: that can be CommonFall's live blended-JObj
            // ECB after Fall_Anim has selected/blended the collision pose, or the ordinary neutral
            // Fall ECB when no source-owned blend is active. If the seed proves a nonzero
            // CommonFall blend but extracted character/action data says that blend is not a source
            // ECB owner, the static platform root-below guard still applies.
            // Fastfall rows also keep the ordinary bottom-sweep path; source Fall_Coll has a
            // stronger downward sweep and the existing Fall stale-platform owners do not borrow
            // non-fastfall guards for fastfall.
            // FoD height platforms remain on the transformed-platform owners above.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
            //   ftCo_Fall_Anim_Inner,ftCo_Fall_Coll}
            // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor,
            //   mpColl_80044838_Floor}
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
            // AttackAir_Anim -> Fall first-callback transformed-platform root-only guard:
            // `ftCo_AttackAir_Anim` can enter `ftCo_Fall_Enter` before Fighter_procMap while
            // already airborne. `ftCo_Fall_Enter` therefore does not call ftCommon_8007D5D4 and
            // the next `Fall_Coll -> ft_800831CC -> mpColl_80047E14` consumes the carried
            // AttackAir CollData lifetime. That provenance alone is not enough to publish a
            // transformed-platform Landing: source still must let `mpColl_80044628_Floor` accept an
            // active callback-local floor before the later root projection (`mpColl_80044838_Floor`)
            // can snap to the FoD platform. Reject only a frame-start Fall callback entered from a
            // previous AttackAir motion when the final floor is a FoD height platform with neither
            // current direct/contact height source nor live scheduler/velocity source. Same-frame
            // AttackAir_Anim -> Fall rows and visible/current FoD platform rows keep the ordinary
            // publication path.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_Coll}
            // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor,
            //   mpColl_80044838_Floor}
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
            // Fresh MissFoot ground-to-air floor-loss callback:
            // ftCo_8009F39C enters MissFoot through ftCommon_8007D5D4, which sets
            // CollData_X130_Locked and fp->ecb_lock before the next MissFoot_Coll. That first
            // serialized callback still routes through ft_80082F28 -> ft_CheckGroundAndLedge ->
            // mpColl_800473CC, but the locked CollData episode leaves shallow same-floor contact
            // airborne; later no-lock MissFoot rows retain the normal Landing path.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::{
            //   ftCo_8009F39C,ftCo_MissFoot_Coll}
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
            // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
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
            // Sustained post-hitlag DamageHi/N/Lw soft-platform carried-floor publication:
            // after hitlag/hitstun have ended, `ftCo_Damage_Coll -> ft_80081DD4` no longer has the
            // active-hitlag FloorPush/FloorHug producer. A stale carried one-way floor and a
            // source-owned replay seed mpCollPrev endpoint are not enough to publish a platform
            // landing through direct floor publication, mpColl_8004A45C's edge clamp, or the
            // terminal-hitstun frame's simulator bottom-sweep publication when the raw
            // source-shaped bottom probe rejected; source first needs the current callback's
            // `mpColl_80044628_Floor` bottom sweep to accept the platform.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor,
            //   mpColl_8004A45C_Floor}
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
            // Final publication guard for the same sloped-ledge main-floor first sustained
            // FallSpecial owner handled in the direct sweep/remap paths.
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
            // Final publication guard for FallSpecial_Coll soft-platform rows. Source reaches
            // `mpColl_80044838_Floor(ignore_bottom=true)` only after `mpColl_80044628_Floor`
            // accepts a platform via `ftCo_80096CC8`; down-held input rejects the platform, and
            // sustained FallSpecial rows whose callback-local bottom remains above the soft floor
            // have not satisfied that source precondition yet. The exception is the just-entered
            // spacie SpecialHi end -> FallSpecial frame: `ftFx_SpecialHiFall/Bound_Anim` changes
            // motion state before the collision callback, so the source callback consumes that
            // end-state CollData lifetime and may publish LandingFallSpecial immediately.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
            //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialHiFall_Anim,ftFx_SpecialHiBound_Anim}
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
            // EscapeAir_Coll may publish a carried cliff ledge floor only after the current callback
            // has produced that same floor. A restored cliff id/cooldown/span is provenance, not
            // publication authority. Sustained EscapeAir callback floor results and accepted
            // current-floor projection are live source authority regardless of
            // flat/sloped/generated ledge shape.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
            // Final guard for fresh JumpAerial -> EscapeAir ledge remaps that can be accepted by
            // multiple projection paths above. The same source owner also applies when the
            // teacher-forced seed already names a different hidden cliff ledge floor as
            // CollData.floor.index: mpColl_80044838_Floor can write back a ledge line with a large
            // upward root correction even though the source `EscapeAir_Coll` ledge/floor handoff
            // still uses the pre-entry CollData lifetime and publishes the row airborne. This is a
            // separate high-lift entry suppression, not the carried-cliff publication owner: static
            // carried cliff floors, flat or sloped, are admitted by the shared carried-cliff
            // authority packet once the current callback floor producer accepts the same carried
            // floor. The replay-real high-lift false-positive family is on horizontal terminal
            // ledge rows; generated sloped carried-floor handoffs have their own prefix proof and
            // must not be folded into this suppression.
            // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
            // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
            // First-callback JumpAerial -> EscapeAir static-platform guard:
            // `ftCo_JumpAerial_IASA` can enter EscapeAir from the same fighter update, but a
            // vertical-only callback-local floor sweep still belongs to the pre-entry JumpAerial
            // CollData lifetime. A static soft platform candidate seen only by the just-entered
            // EscapeAir root projection must therefore wait until the following sustained
            // EscapeAir_Coll pass. Keep this to the live locked-ECB entry window
            // (`seed_prev_action_frame == 0`, high x130 countdown) and require zero horizontal
            // source sweep; diagonal air-dodge entries with a real horizontal sweep can publish the
            // platform immediately through the ordinary floor owner.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            //   mpColl_80044838_Floor}
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
            // Fresh JumpAerial -> EscapeAir can also reach a static soft platform after the
            // callback-local bottom sweep is produced mostly by the pre-entry JumpAerial ECB
            // lifetime rather than by the current EscapeAir root step. Source
            // mpColl_80044628_Floor first accepts the bottom crossing and only then lets
            // mpColl_80044838_Floor project the root; if that final bottom correction is larger
            // than this callback's vertical step, it is the same pre-entry CollData handoff gap as
            // the ledge guard above, not a publishable LandingFallSpecial row. Same-frame
            // JumpAerial_IASA entries, locked desired-bottom/CollData_X130 start-platform rows,
            // and deeper current-step crossings remain on the ordinary EscapeAir_Coll floor owner.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
            // ftCo_EscapeAir_Coll consumes floor contact through ft_80082C74/mpColl_800471F8.
            // During the locked CollData window, replay-real downward EscapeAir continuations can
            // carry a soft-platform floor.index through the same platform after the aerial-jump
            // resource has already been spent. That carried index is CollData provenance, not a new
            // floor hit; do not turn it into LandingFallSpecial while the stick is still past the
            // extracted platform pass threshold and the snap is shallower than the entered
            // EscapeAir ECB bottom. Deeper penetrations have already reached the source
            // ft_80082C74/mpColl_800471F8 floor handoff. Ground-jump airdodges keep one jump
            // available and still land through EscapeAir_Coll's normal floor callback. Once
            // mpColl_80044628_Floor has accepted the preserved desired-bottom crossing, held-down
            // input is not a platform-pass reject for this owner: mpColl_800471F8 calls
            // mpColl_80044628_Floor with cb=NULL.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Enter
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter
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
            // Same CollData locked-floor owner as the same-platform guard above, but for carried
            // ledge floor segments. EscapeAir_Coll should not publish LandingFallSpecial from a
            // shallow final writeback on the same carried ledge floor when the floor-sweep source
            // row was already below that floor. It stays airborne until the entered EscapeAir ECB
            // reaches the source ft_80082C74/mpColl_800471F8 floor handoff depth. Once the locked
            // desired bottom actually sweeps through the carried ledge/floor this callback,
            // mpColl_80044628_Floor has produced the source authority needed by
            // mpColl_80044838_Floor, so do not suppress that publication here.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_800471F8,mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044838_Floor}
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
            // Adjacent generated ledge remaps use the same EscapeAir_Coll floor owner, but source
            // only publishes the root projection once fp+0x2218 allow_interrupt is live. Without
            // that bit the locked ECB/carry path can project from the center floor to an adjacent
            // ledge one callback early.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
            (escapeair_locked_platform_root_projection_hit &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             batch->state.seed_prev_action_frame[idx] <= 1 &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias)
                ? 1u
                : 0u;
        const uint8_t suppress_fresh_jumpaerial_downheld_nonplatform_stale_land =
            // Fresh JumpAerial -> EscapeAir may enter the collision callback before Fighter_procMap,
            // but a restored/no-owner non-platform publication still requires a source root/bottom
            // producer. Keep this rejection on the decomp-authored down-held input owner: steep
            // downward EscapeAir rows are still below the accepted floor in the callback-local
            // source state, while shallow diagonal air-dodges continue through the ordinary
            // EscapeAir_Coll hard-floor publication path.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
            //   ftCo_80099A58,ftCo_80099A9C,ftCo_EscapeAir_Coll}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,
            //   mpColl_80044838_Floor}
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
            // A replay seed with CollData_X130_Locked but no preserved desired-bottom owner cannot
            // prove that mpColl_80044628_Floor reached the source bottom-sweep precondition. If the
            // carried callback root was already below the accepted floor before this frame, reject
            // the upward root projection and leave the row airborne. True above->floor crossings
            // (for example FD EscapeAir landing rows) keep the normal floor publication path.
            // Fresh JumpAerial -> EscapeAir on an ordinary hard floor is also a source
            // ft_80082C74/mpColl_800471F8 handoff: the IASA transition happens before Fighter_procMap
            // and the first EscapeAir collision pass may publish LandingFallSpecial from the carried
            // hard-floor index even though the runtime desired-bottom lock has no replay seed owner.
            // Ledge, soft-platform, and transformed-platform rows stay on their explicit guards.
            // First-frame JumpAerial -> EscapeAir zero-bottom platform projections are explicit above:
            // when that source owner has accepted a same-platform root projection, do not erase it here.
            // Ground-jump airdodges and later carried JumpAerial frames remain suppressed.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
            // `mpColl_80044838_Floor(ignore_bottom=true)` is only reached after
            // `mpColl_80044628_Floor` sees an ECB-bottom floor crossing. Several carried root/
            // projection result modes can publish a root snap from the carried platform floor.index before that
            // source bottom sweep happens; keep those rows airborne until the preserved
            // `desired_ecb.bottom` actually crosses the accepted platform. This is a carried
            // floor.index guard, so keep it on the same seeded platform segment; different platform
            // segments are new floor candidates owned by the normal EscapeAir_Coll callback.
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80044628_Floor,mpColl_80044838_Floor,mpColl_80046904}
            (escapeair_episode.sustained && ecb_lock_timer_seed != 0u &&
             ground_id == seed_ground_id && !locked_desired_bottom_final_sweep_hit)
                ? 1u
                : 0u;
        const uint8_t runtime_live_jumpaerial_nonplatform_root_crossing =
            // Live JumpAerial -> EscapeAir hard-floor owner can publish the later nonplatform root
            // crossing from the carried desired-bottom lane. The soft/platform owner is different:
            // it is a platform/transform pass-through from the entry callback, so crossing a
            // nonplatform hard floor still requires current `EscapeAir_Coll` floor-producer
            // authority before final publication.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
            // Live JumpAerial -> EscapeAir can carry a CollData_X130 desired-bottom packet, but
            // non-platform publication still needs either a real desired-bottom floor crossing or
            // the later hard-floor root-projection phase. Soft/platform-origin owners are only
            // source authority for the platform packet, not for a direct hard-floor snap. Hard-floor
            // owners before the late countdown phase have the same desired-bottom precondition:
            // `mpColl_80044838_Floor` is downstream of `mpColl_80044628_Floor`, so a root
            // projection while the preserved desired bottom is still above the floor is not enough.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
            // Final guard for the same CollData_X130_Locked desired-bottom precondition on
            // non-platform floors. Root/zero-bottom result modes may publish a hard or
            // sloped floor result, but source cannot reach mpColl_80044838_Floor while the preserved
            // desired ECB bottom remains above the accepted floor.
            //
            // Live JumpAerial -> EscapeAir pass-through provenance from the entry callback should
            // not suppress the later same-callback
            // non-platform root crossing once EscapeAir reaches the carried hard floor; that
            // remains owned by ft_80082C74/mpColl_800471F8's non-platform path. Horizontal ledge
            // floors need the later locked interpolation phase before that root crossing is source
            // owned; rows without that callback-visible phase still keep the desired-bottom
            // precondition.
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
            // EscapeAir_Coll reaches `ft_80082C74 -> mpColl_800471F8`. A JumpAerial frame-2
            // soft/platform desired-bottom owner can survive into the EscapeAir callback, but that
            // owner only proves the carried soft/platform support probe; it does not authorize a
            // direct hard-floor publication from root projection while no current EscapeAir floor
            // producer has accepted the hard floor. The per-frame probe bits are consumed/rewritten
            // during the floor pass, so this final guard is anchored on the generated JumpAerial
            // frame-2 bottom owner rather than the transient reject-bit snapshot. Frame-0/1
            // desired-bottom owners stay on the ordinary publication path.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
            // Fresh KneeBend -> EscapeAir can publish normal hard/platform floor handoffs, but
            // ledge floors require the source bottom-sweep producer before final writeback from the
            // zero-bottom root owner.
            // data/stages/bin/*.bin::MSLSTG01 is_ledge + fighter_solid floor metadata
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
            // Ground-jump -> EscapeAir static-platform lock:
            // `KneeBend_Anim` may enter Jump, then `Jump_IASA` may immediately enter EscapeAir
            // before `Fighter_procMap`. The same map callback reaches EscapeAir_Coll while
            // ftCommon_8007D5D4's CollData_X130 lock still names the launch platform. A shallow
            // zero-bottom projection onto that same static platform is therefore the jump-launch
            // handoff for the remaining CollData_X130 lock lifetime, not a new LandingFallSpecial
            // floor publication. Deeper bottom/root crossings continue through the normal
            // EscapeAir_Coll floor owner.
            //
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::{ftCo_Jump_Enter,ftCo_Jump_IASA}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
            //   ftCo_80099A58,ftCo_EscapeAir_Coll}
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
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
            // Same source owner as the floor-sweep suppression above, kept as a final publication
            // guard because several mpColl result modes can accept the restored ledge floor.
            // Carried cliff ledge floors should not publish LandingFallSpecial from restored
            // id/cooldown/span provenance alone. Once the carried floor's source bottom/root
            // producer accepts it, flat and generated sloped ledge floors both continue through
            // the normal EscapeAir_Coll floor handoff.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpCollInterpolateECB,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
        mpcoll_floor_reject_add_damage_active_hitlag_owner(
            &final_floor_reject, damage_active_hitlag_floor_owner,
            damage_active_hitlag_root_below_bottom_above_floor_owner);
        if (final_floor_reject.bits != 0u) {
          if ((final_floor_reject.bits & MSL_MPCOLL_REJECT_SHEIK_VANISH_START1_PLATFORM_PASS) !=
              0u) {
            // `ftCo_8009A134` writes CollData.floor_skip as the source branch side effect; this can
            // happen before the simulator has a final floor publication object. Publish it when the
            // source-owned probe reject bit is emitted, not only in final-publication cleanup.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
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
          // `KneeBend_Anim` can enter Jump, then Jump IASA can enter EscapeAir before
          // `Fighter_procMap`; the same callback pass immediately runs
          // `EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8`. When that handoff traverses from a
          // generated legal-stage slope to the connected flat floor, the accepted floor contact is
          // the returned mpLib floor, not the carried slope contact from the seed row. Keep the
          // corrected contact in the final CollData writeback so the following
          // `LandingFallSpecial` publication does not restore stale slope height.
          //
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
          // `KneeBend_Anim` may enter Jump, then Jump IASA can enter EscapeAir before
          // `Fighter_procMap`; the same callback pass immediately runs
          // `EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8`. If the ordinary sweep does not
          // materialize a candidate, source still consumes the carried CollData floor when the
          // callback-local root crosses that same floor line.
          //
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
        // Final source guard for live JumpAerial -> EscapeAir CollData_X130 publication:
        // soft/platform-origin desired-bottom owners do not authorize direct non-platform floor
        // snaps, and early hard-floor countdown rows still need `mpColl_80044628_Floor` to observe
        // the preserved desired bottom crossing before `mpColl_80044838_Floor` can publish ground.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{
        //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
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
        // CapturePulledLw floor-loss callback:
        // fn_800DB230_inline switches to CapturePulledHi after source floor loss and should not be
        // re-grounded by the later generic floor publication on the same lost CollData.floor
        // segment. The handoff records that segment in floor_skip_segment_id for this frame only.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
        //   ftCo_CapturePulledLw_Coll,fn_800DB230,fn_800DAA40}
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
      if (floor_publication.on_ground && !was_grounded && action_is_down_bound(action_id) &&
          batch->state.speed_x_attack[idx] != 0.0f &&
          fabsf(batch->state.speed_y_attack[idx]) <= 0.000001f) {
        float floor_nx = floor_publication.contact.normal_x;
        float floor_ny = floor_publication.contact.normal_y;
        const float normal_len = sqrtf(floor_nx * floor_nx + floor_ny * floor_ny);
        if (normal_len > 0.000001f) {
          floor_nx /= normal_len;
          floor_ny /= normal_len;
          const float ground_kb = batch->state.speed_x_attack[idx];
          const float old_kb_x = batch->state.speed_x_attack[idx];
          const float new_kb_x = floor_ny * ground_kb;
          const float new_kb_y = -floor_nx * ground_kb;
          batch->state.speed_x_attack[idx] = new_kb_x;
          batch->state.speed_y_attack[idx] = new_kb_y;
          batch->state.pos_x[idx] += new_kb_x - old_kb_x;
          const int downbound_floor_idx =
              stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
          float downbound_line_y = 0.0f;
          if (floor_line_y_at_x_for_env(batch, bi, g, downbound_floor_idx, batch->state.pos_x[idx],
                                        &downbound_line_y)) {
            batch->state.pos_y[idx] = downbound_line_y + k_floor_y_bias;
          } else {
            for (size_t line_i = 0; line_i < g->line_count; line_i++) {
              if (g->lines[line_i].segment_i == batch->state.ground_id[idx] &&
                  floor_line_y_at_x_for_env(batch, bi, g, (int)line_i, batch->state.pos_x[idx],
                                            &downbound_line_y)) {
                batch->state.pos_y[idx] = downbound_line_y + k_floor_y_bias;
                break;
              }
            }
          }
        }
        // Airborne DownBound floor-contact KB projection:
        // ftCo_DownBound_Phys runs ft_80084F3C before ftCo_DownBound_Coll, but the Coll callback
        // can publish a same-frame floor through ft_80082708 -> mpColl_8004B108. When an airborne
        // DownBound contacts a generated legal-stage slope with horizontal residual ground KB,
        // source projects the residual xF0/x8c ground-KB scalar onto the accepted floor tangent
        // before the post-frame exposes speed_x/y_attack.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
        //   ftCo_DownBound_Phys,ftCo_DownBound_Coll}
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80084F3C,ft_80082708}
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
        // data/stages/bin/*.bin::MSLSTG01 floor normal metadata
      }
      mpcoll_commit_final_floor_state(&mpcoll_ctx, &floor_publication);
      if (batch->state.on_ground[idx] != 0u && !was_grounded &&
          is_damage_ground_collision_action(action_id)) {
        // Common DamageHi/N/Lw air-to-ground publication keeps the current damage motion but still
        // runs the grounded landing bundle that refreshes jumps used. Slippi exposes the inverse as
        // `jumps_left=max_jumps` on the same post-frame as the platform/floor publication.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
        // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D7FC,ftCommon_8007D6A4}
        const MslCharParams* damage_land_ch = msl_char_params_fast(batch->state.char_id[idx]);
        if (damage_land_ch != NULL) {
          batch->state.jumps_left[idx] = damage_land_ch->max_jumps;
        }
      }
      if (batch->state.on_ground[idx] != 0u && !was_grounded && action_is_down_bound(action_id) &&
          batch->state.speed_x_attack[idx] != 0.0f &&
          fabsf(batch->state.speed_y_attack[idx]) <= 0.000001f) {
        float floor_nx = batch->state.ground_normal_x[idx];
        float floor_ny = batch->state.ground_normal_y[idx];
        if (fabsf(floor_nx) <= 0.000001f && fabsf(floor_ny - 1.0f) <= 0.000001f &&
            batch->state.ground_id[idx] != 0xFFFFu) {
          const int downbound_floor_idx =
              stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
          if (!floor_line_normal_for_env(batch, bi, g, downbound_floor_idx, &floor_nx, &floor_ny)) {
            for (size_t line_i = 0; line_i < g->line_count; line_i++) {
              if (g->lines[line_i].segment_i == batch->state.ground_id[idx] &&
                  floor_line_normal_for_env(batch, bi, g, (int)line_i, &floor_nx, &floor_ny)) {
                break;
              }
            }
          }
        }
        const float normal_len = sqrtf(floor_nx * floor_nx + floor_ny * floor_ny);
        if (normal_len > 0.000001f) {
          floor_nx /= normal_len;
          floor_ny /= normal_len;
          const float ground_kb = batch->state.speed_x_attack[idx];
          const float old_kb_x = batch->state.speed_x_attack[idx];
          const float new_kb_x = floor_ny * ground_kb;
          const float new_kb_y = -floor_nx * ground_kb;
          batch->state.speed_x_attack[idx] = new_kb_x;
          batch->state.speed_y_attack[idx] = new_kb_y;
          batch->state.pos_x[idx] += new_kb_x - old_kb_x;
          const int downbound_floor_idx =
              stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
          float downbound_line_y = 0.0f;
          if (floor_line_y_at_x_for_env(batch, bi, g, downbound_floor_idx, batch->state.pos_x[idx],
                                        &downbound_line_y)) {
            batch->state.pos_y[idx] = downbound_line_y + k_floor_y_bias;
          } else {
            for (size_t line_i = 0; line_i < g->line_count; line_i++) {
              if (g->lines[line_i].segment_i == batch->state.ground_id[idx] &&
                  floor_line_y_at_x_for_env(batch, bi, g, (int)line_i, batch->state.pos_x[idx],
                                            &downbound_line_y)) {
                batch->state.pos_y[idx] = downbound_line_y + k_floor_y_bias;
                break;
              }
            }
          }
        }
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
          // Final DownBound carry fallback for the same source owner as
          // `mpcoll_maybe_refresh_downbound_airborne_floor_index`: generic hard-floor publication
          // can observe the main floor under a descending FoD side platform, but
          // `ft_80082708 -> mpColl_8004B108` keeps CollData.floor on the visible height-platform
          // line while returning GA_Air. Apply the grIzumi line delta after publication rejection
          // so the carried airborne DownBound pose follows the platform without becoming grounded.
          // Once grIzumi sends the line to the generated hidden target, the earlier hidden-floor
          // refresh owns CollData.floor instead; do not restore the carried hidden platform here.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
          // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
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
          // Fresh JumpAerial -> EscapeAir ledge exits can reject the first carried-floor
          // publication while still carrying source CollData_X130 desired-bottom state into the
          // next EscapeAir_Coll callback. Preserve only if the final publication left the fighter
          // airborne; once the carried ledge floor is actually published, the grounded transition
          // clears the lock.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
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
        // Source `mpColl_LoadECB_inline` preserves desired_ecb.bottom while CollData_X130_Locked is
        // active. The lite floor pass keeps Jump/JumpAerial floor ownership on its existing
        // callback-phase slice above, but JumpAerial_Coll, sustained EscapeAir continuations
        // (including replay-seeded CollData_X130 rows), and subsequent airborne wall/ceiling
        // passes still consume the same source CollData desired ECB until the lock expires.
        // This matters for aerial Reflector -> JumpAerial -> EscapeAir chains: source
        // `ftCo_JumpAerial_Enter_Basic` calls `ftCommon_8007D5D4`, and the later
        // `ftCo_JumpAerial_Coll` / `ftCo_EscapeAir_Coll` callbacks both preserve the locked
        // desired bottom through `mpColl_LoadECB_inline`.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        // refs/melee/src/melee/mp/mpcoll.c::{
        //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80045B74_LeftWall}
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
        // Carry the source loaded ECB through frozen Damage hitlag without using it as floor
        // authority on rows where `ftCo_Damage_OnEveryHitlag` did not consume a fresh downward SDI
        // displacement. This keeps the hidden CollData state available for later active-hitlag
        // floorhug rows while preventing unrelated FoD platform/root projection rows from using
        // the pre-Damage pose as a broad floor-contact shortcut.
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
        // SpecialHi's collision callback loads CollData.ecb from live JObj collision joints after
        // ftFox_SpecialHi_RotateModel mutates XRotN. `mpCollInterpolateECB` moves the current ECB
        // to the frame-start desired ECB, then the next callback's `mpCollPrev` observes that
        // packet while `desired_ecb` has advanced again. Publish that frame-start desired packet
        // as current/prev for the ongoing launch instead of the fixed-pose floor query ECB.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
        //   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Coll}
        // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
        // refs/melee/src/melee/mp/mpcoll.c::{
        //   mpColl_LoadECB_JObj,mpCollInterpolateECB,mpCollPrev,mpColl_80043754}
        cur_ecb_points = state_desired_ecb_points;
        stored_prev_ecb_points = state_desired_ecb_points;
      }
      if (active_damage_hitlag_ecb_carry && have_state_cur_ecb && !on_ground) {
        // Rejected active-hitlag floor probes do not publish their projected floor contact as
        // CollData.current. Source keeps the loaded Damage ECB current/prev packet live for the
        // next frozen/exit map callback; only an accepted floor result below may replace it.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
        //   ftCo_Damage_OnEveryHitlag,ftCo_DamageFly_Coll}
        // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor}
        cur_ecb_points = state_cur_ecb_points;
        stored_prev_ecb_points = state_cur_ecb_points;
      }
      mpcoll_store_prev_ecb_points(batch, idx, &stored_prev_ecb_points);
      mpcoll_store_current_ecb_points(batch, idx, &cur_ecb_points);
      mpcoll_store_desired_ecb_points(batch, idx, &stored_desired_ecb_points);
      // Active Damage hitlag can first materialize the source floor contact through the ordinary
      // mpColl floor-env bits before a later OnEveryHitlag SDI row needs the loaded ECB/floor
      // packet as stay-airborne FloorPush/FloorHug authority. In source, `inline0` clears
      // `env_flags` every map callback, but `CollData.floor/contact/ecb` and `cur_pos` remain the
      // callback-current state consumed by the next frozen hitlag map callback. Preserve that
      // runtime-produced CollData provenance for the rest of the hitlag segment, including no-SDI
      // continuation rows between the first contact and the later SDI consume. Teacher-forced
      // reseed rows may initialize hidden ECB state, but they cannot seed this runtime-only
      // floor-contact authority.
      //
      // Clear phase: any non-hitlag or non-Damage map callback writes zero here; `reseed_seed`
      // clears it before applying explicit seed ECB lanes.
      //
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
      //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
      // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_procMap}
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
      // refs/melee/src/melee/mp/mpcoll.c::{
      //   inline0,mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
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
        // CollData_X130_Locked is cleared by Fighter_procMap before the Fall_Coll map callback.
        // On the expiry frame, mpColl_LoadECB_inline stops preserving desired_ecb.bottom=0 and
        // reloads the ordinary Fall ECB pose. Runtime rollouts must promote that unlocked
        // callback-local ECB lifetime just like teacher-forced reseeds do; otherwise the next
        // fastfall frame can keep a stale zero-bottom ECB and publish a false Landing.
        // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
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
      // Frame-end converged CollData ecb.bottom rel: mpCollInterpolateECB converges ecb to
      // desired WITHIN the frame's substep loop (time reaches 1 on the last substep), so the
      // frame-level effective bottom is the locked-preserved desired bottom while
      // CollData_X130_Locked is held, otherwise the pose bottom; grounded CollData keeps the
      // bottom at the root. Consumed as the prev-endpoint rel by per-frame bottom sweeps.
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_LoadECB_inline}
      {
        float eff = 0.0f;
        if (!batch->state.on_ground[idx]) {
          eff = mpcoll_pose_ecb_bottom_rel_y(
              char_id, batch->state.animation_index[idx],
              msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]), 0u);
          if (common_fall_blended_ecb_consumer && have_common_fall_blended_current_ecb) {
            // The same ftCo_Fall_Anim_Inner-selected JObj pose consumed by this callback becomes
            // the frame-end current ECB packet observed by the next mpColl pass.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
            eff = common_fall_blended_current_ecb_points.bottom_rel_y;
          }
          if (batch->state.ecb_lock_timer[idx] != 0u) {
            // Same use_locked/lock_bottom_to_zero split as the validated consumers: an
            // owner-held lock preserves the explicit lane; a ground-departure lock holds the
            // grounded zero.
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
