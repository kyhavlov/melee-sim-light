#include "hitboxes.h"

#include <math.h>
#include <stdint.h>

#include "anim_frame.h"
#include "fighter_pose.h"
#include "fighter_script.h"
#include "hitboxes_tables.h"
#include "items.h"

enum {
  MSL_HITCAPSULE_DISABLED = 0,
  MSL_HITCAPSULE_ENABLED = 1,
  MSL_HITCAPSULE_NEW = 2,
  MSL_HITCAPSULE_ACTIVE = 3,
};

static inline size_t hitbox_index(size_t fighter_idx, uint8_t hitbox_id) {
  return fighter_idx * (size_t)MSL_MAX_HITBOXES + (size_t)hitbox_id;
}

void hitboxes_clear_player_active(MslBatch* batch, int bi, int p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players) {
    return;
  }
  fighter_script_disable_hitcapsules(batch, msl_idx_player(bi, p));
}

static uint8_t hitbox_world_position(const MslBatch* batch, size_t idx, uint8_t hitbox_id,
                                     float* out_x, float* out_y, float* out_z, float* out_radius) {
  const size_t hi = hitbox_index(idx, hitbox_id);
  MslFighterCollisionPose pose;
  if (!fighter_pose_collision_pose(batch, idx, &pose)) {
    return 0u;
  }
  const uint16_t part_id = batch->state.hitbox_bone_part_id[hi];
  const float offset[3] = {batch->state.hitbox_offset_x[hi], batch->state.hitbox_offset_y[hi],
                           batch->state.hitbox_offset_z[hi]};
  float local[3];
  if (fighter_pose_attachment_local(batch, idx, pose.msid, pose.anim_frame, part_id, offset,
                                    local) != 0) {
    return 0u;
  }

  *out_x = batch->state.pos_x[idx] + pose.facing * local[2];
  *out_y = batch->state.pos_y[idx] + local[1];
  *out_z = batch->state.pos_z[idx] - pose.facing * local[0];

  // Sheik's Chain accessory callback writes the ordinary x914 centers from solved article links.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_UpdateHitboxes
  float chain_x = 0.0f, chain_y = 0.0f, chain_z = 0.0f;
  if (sheik_chain_hitbox_world_pos(batch, idx, hitbox_id, &chain_x, &chain_y, &chain_z) != 0u) {
    *out_x = chain_x;
    *out_y = chain_y;
    *out_z = chain_z;
  }

  float radius = batch->state.hitbox_radius[hi];
  if (!msl_hitbox_ignore_fighter_scale(batch->state.hitbox_flags[hi])) {
    radius *= batch->state.fighter_scale_y[idx];
  }
  *out_radius = radius;
  return 1u;
}

void hitboxes_reseed_player(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  uint8_t count = 0u;
  for (uint8_t hitbox_id = 0; hitbox_id < (uint8_t)MSL_MAX_HITBOXES; hitbox_id++) {
    const size_t hi = hitbox_index(idx, hitbox_id);
    if (batch->state.hitbox_capsule_state[hi] == (uint8_t)MSL_HITCAPSULE_DISABLED) {
      batch->state.hitbox_enabled[hi] = 0u;
      batch->state.hitbox_prev_enabled[hi] = 0u;
      continue;
    }
    float x = 0.0f, y = 0.0f, z = 0.0f, radius = 0.0f;
    if (!hitbox_world_position(batch, idx, hitbox_id, &x, &y, &z, &radius)) {
      batch->state.hitbox_enabled[hi] = 0u;
      batch->state.hitbox_prev_enabled[hi] = 0u;
      continue;
    }

    // ftColl_8007AD18 initializes x58=x4C only on the real create edge. A replay seed in the
    // middle of that same live HitCapsule must begin with the seed-frame x4C already published;
    // the next priority-9 refresh then copies it into x58 before sampling the new x4C. This is
    // initialization of source-hidden state, not an alternate rollout path.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_8007AFC8}
    // refs/melee/src/melee/lb/types.h::HitCapsule::{x4C,x58}
    batch->state.hitbox_prev_x[hi] = x;
    batch->state.hitbox_prev_y[hi] = y;
    batch->state.hitbox_prev_z[hi] = z;
    batch->state.hitbox_x[hi] = x;
    batch->state.hitbox_y[hi] = y;
    batch->state.hitbox_z[hi] = z;
    (void)radius;
    batch->state.hitbox_prev_enabled[hi] = 1u;
    batch->state.hitbox_enabled[hi] = 1u;
    batch->state.hitbox_capsule_enabled[hi] = 1u;
    batch->state.hitbox_capsule_state[hi] = (uint8_t)MSL_HITCAPSULE_ACTIVE;
    batch->state.hitbox_enable_edge[hi] = 0u;
    batch->state.hitbox_pose_create[hi] = 0u;
    count++;
  }
  batch->state.hitbox_count[idx] = count;
}

void hitboxes_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      uint8_t count = 0u;
      for (uint8_t hitbox_id = 0; hitbox_id < (uint8_t)MSL_MAX_HITBOXES; hitbox_id++) {
        const size_t hi = hitbox_index(idx, hitbox_id);
        const uint8_t state = batch->state.hitbox_capsule_state[hi];
        if (state == (uint8_t)MSL_HITCAPSULE_DISABLED) {
          batch->state.hitbox_enabled[hi] = 0u;
          batch->state.hitbox_prev_enabled[hi] = 0u;
          continue;
        }

        float x = 0.0f, y = 0.0f, z = 0.0f, radius = 0.0f;
        if (!hitbox_world_position(batch, idx, hitbox_id, &x, &y, &z, &radius)) {
          batch->state.hitbox_enabled[hi] = 0u;
          batch->state.hitbox_prev_enabled[hi] = 0u;
          continue;
        }

        if (state == (uint8_t)MSL_HITCAPSULE_ENABLED || batch->state.hitbox_enabled[hi] == 0u) {
          // ftColl_8007AD18 initializes both x58 and x4C on a fresh enable edge.
          batch->state.hitbox_prev_x[hi] = x;
          batch->state.hitbox_prev_y[hi] = y;
          batch->state.hitbox_prev_z[hi] = z;
          batch->state.hitbox_capsule_state[hi] = (uint8_t)MSL_HITCAPSULE_NEW;
        } else {
          batch->state.hitbox_prev_x[hi] = batch->state.hitbox_x[hi];
          batch->state.hitbox_prev_y[hi] = batch->state.hitbox_y[hi];
          batch->state.hitbox_prev_z[hi] = batch->state.hitbox_z[hi];
          if (state == (uint8_t)MSL_HITCAPSULE_NEW) {
            batch->state.hitbox_capsule_state[hi] = (uint8_t)MSL_HITCAPSULE_ACTIVE;
          }
        }
        batch->state.hitbox_x[hi] = x;
        batch->state.hitbox_y[hi] = y;
        batch->state.hitbox_z[hi] = z;
        batch->state.hitbox_radius[hi] = radius;
        batch->state.hitbox_prev_enabled[hi] = 1u;
        batch->state.hitbox_enabled[hi] = 1u;
        batch->state.hitbox_capsule_enabled[hi] = 1u;
        count++;
      }
      batch->state.hitbox_count[idx] = count;
    }
  }
}
