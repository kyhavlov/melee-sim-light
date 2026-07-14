#include "hurtboxes.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "action_ids.h"
#include "anim_pose.h"
#include "fighter_pose.h"
#include "hurtcaps_tables.h"
#include "items.h"
#include "mtx34.h"
#include "motion_state_owners.h"
#include "state_flags.h"

static inline size_t hurtcap_index(size_t fighter_idx, uint8_t cap_id) {
  return fighter_idx * (size_t)MSL_MAX_HURTCAPS + (size_t)cap_id;
}

static uint8_t hurtboxes_contact_demand(const MslBatch* batch, int bi, int defender,
                                        uint8_t item_demand) {
  if (item_demand != 0u) {
    return 1u;
  }
  const int players = (int)batch->config.num_players;
  for (int attacker = 0; attacker < players; attacker++) {
    if (attacker != defender && batch->state.hitbox_count[msl_idx_player(bi, attacker)] != 0u) {
      return 1u;
    }
  }
  return 0u;
}

static uint8_t hurtboxes_merged_status(const MslBatch* batch, size_t idx) {
  // ftColl_8007B868 gives x1988 (script) precedence over x198C (timer/system). Slippi publishes
  // the same merge. Per-capsule script state is kept separate below.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B62C,ftColl_8007B868}
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  uint8_t status = batch->state.script_hit_status_x1988[idx] != 0u
                       ? batch->state.script_hit_status_x1988[idx]
                       : batch->state.colanim_hit_status_x198c[idx];
  if (batch->debug_hit_status_override != NULL && batch->debug_hit_status_override[idx] != 0xFFu) {
    status = batch->debug_hit_status_override[idx];
  }
  return status;
}

static void hurtboxes_clear_player(MslBatch* batch, size_t idx) {
  // Fixed-capacity capsule slots are live only through hurtcap_count/geometry_valid. Invalidating
  // those owners is sufficient; the subsequent source refresh overwrites every published slot.
  // Clearing all 15 inactive payload slots here duplicated writes that no collision traversal can
  // observe and made lazy contact geometry more expensive than the source list lifetime.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B62C,ftColl_80078C70,ftColl_80078A2C}
  batch->state.hurtcap_count[idx] = 0u;
  batch->state.hurtcap_geometry_valid[idx] = 0u;
}

static void hurtboxes_refresh_impl(MslBatch* batch, uint8_t demand_gated) {
  if (batch == NULL) {
    return;
  }
  const int players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t item_demand =
        demand_gated != 0u ? items_row_has_fighter_collision_demand(batch, bi) : 0u;
    for (int p = 0; p < players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      hurtboxes_clear_player(batch, idx);
      batch->state.hurtbox_state[idx] = hurtboxes_merged_status(batch, idx);

      if (msl_action_owns_x2219_collision_skip(batch->state.action_id[idx])) {
        continue;
      }
      const MslHurtCap* caps = NULL;
      uint16_t count = 0u;
      if (hurtcaps_get(batch->state.char_id[idx], &caps, &count) != 0 || caps == NULL) {
        continue;
      }
      if (count > (uint16_t)MSL_MAX_HURTCAPS) {
        count = (uint16_t)MSL_MAX_HURTCAPS;
      }
      batch->state.hurtcap_count[idx] = (uint8_t)count;
      const float radius_scale = fighter_pose_model_scale(batch, idx);
      for (uint16_t cap = 0u; cap < count; cap++) {
        const size_t hi = hurtcap_index(idx, (uint8_t)cap);
        // HurtCapsule_Disabled (1) is invincible contact: narrowphase still runs and only damage
        // application is suppressed. Intangible (2) is the geometry-rejecting state.
        // refs/melee/src/melee/lb/forward.h::HurtCapsuleState
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
        batch->state.hurtcap_enabled[hi] = batch->state.script_hurtcap_state[hi] != 2u ? 1u : 0u;
        batch->state.hurtcap_is_grabbable[hi] = caps[cap].is_grabbable ? 1u : 0u;
        batch->state.hurtcap_height[hi] = caps[cap].height;
        batch->state.hurtcap_radius[hi] = caps[cap].scale * radius_scale;
      }

      const uint8_t build_geometry =
          demand_gated == 0u ? 1u : hurtboxes_contact_demand(batch, bi, p, item_demand);
      if (build_geometry == 0u) {
        continue;
      }
      MslFighterCollisionPose pose;
      if (!fighter_pose_collision_pose(batch, idx, &pose)) {
        continue;
      }
      const uint16_t msid = pose.msid;
      const float frame = pose.anim_frame;
      uint16_t part_ids[MSL_MAX_HURTCAPS];
      float matrices[MSL_MAX_HURTCAPS * 12u];
      uint8_t matrix_ok[MSL_MAX_HURTCAPS];
      for (uint16_t cap = 0u; cap < count; cap++) {
        part_ids[cap] = caps[cap].bone_part_id;
      }
      (void)anim_pose_get_collision_matrices_f32(batch, idx, msid, frame, part_ids, count, matrices,
                                                 matrix_ok);
      MslFighterPoseFrame pose_frame;
      if (fighter_pose_frame_init(batch, idx, msid, frame, &pose_frame) != 0) {
        continue;
      }
      uint8_t any = 0u;
      for (uint16_t cap = 0u; cap < count; cap++) {
        const size_t hi = hurtcap_index(idx, (uint8_t)cap);
        batch->hurtcap_matrix_valid[hi] = 0u;
        if (batch->state.hurtcap_enabled[hi] == 0u && batch->state.hurtcap_is_grabbable[hi] == 0u) {
          continue;
        }
        const float* matrix = &matrices[(size_t)cap * 12u];
        float* world = &batch->hurtcap_matrix[hi * 12u];
        if (matrix_ok[cap] == 0u ||
            fighter_pose_matrix_world_from_frame(batch, idx, caps[cap].bone_part_id, matrix,
                                                 &pose_frame, world) != 0) {
          batch->state.hurtcap_enabled[hi] = 0u;
          batch->state.hurtcap_radius[hi] = 0.0f;
          continue;
        }
        // lbColl consumes the completed JObj world matrix directly for both capsule endpoints.
        // Do not rebuild the same transform as a local attachment plus a second root conversion.
        // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
        batch->hurtcap_matrix_valid[hi] = 1u;
        msl_mtx34_mul_point(world, caps[cap].a_offset, &batch->state.hurtcap_a_x[hi],
                            &batch->state.hurtcap_a_y[hi], &batch->state.hurtcap_a_z[hi]);
        msl_mtx34_mul_point(world, caps[cap].b_offset, &batch->state.hurtcap_b_x[hi],
                            &batch->state.hurtcap_b_y[hi], &batch->state.hurtcap_b_z[hi]);
        any = 1u;
      }
      batch->state.hurtcap_geometry_valid[idx] = any;
    }
  }
}

void hurtboxes_refresh(MslBatch* batch) { hurtboxes_refresh_impl(batch, 0u); }

void hurtboxes_refresh_metadata(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      // Priority-9 HitCapsule publication only needs the merged fighter hit status. Capsule
      // metadata and posed endpoints belong to the later lb_8000B1CC/contact-demand refresh; do not
      // build the same 15-capsule table twice in one frame.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B868,ftColl_8007AE80,ftColl_80078C70}
      batch->state.hurtbox_state[idx] = hurtboxes_merged_status(batch, idx);
    }
  }
}

void hurtboxes_refresh_contact_geometry(MslBatch* batch) { hurtboxes_refresh_impl(batch, 1u); }
