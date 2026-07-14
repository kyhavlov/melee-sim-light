#include "shields.h"

#include "anim_table.h"
#include <math.h>
#include <stdint.h>

#include "action_ids.h"
#include "char_params.h"
#include "common_params.h"
#include "fighter_pose.h"
#include "guard_lifecycle.h"
#include "msl_math.h"
#include "shield_tilt_table.h"

static inline float clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static inline float apply_deadzone_f32(float v, float dz) {
  // Match tools/slippi/seed_history.py::apply_deadzone and common stick handling across the sim:
  // per-axis deadzone in unit space.
  return (fabsf(v) < dz) ? 0.0f : v;
}

static inline uint16_t clamp_u16(uint16_t x, uint16_t lo, uint16_t hi) {
  if (x < lo) {
    return lo;
  }
  if (x > hi) {
    return hi;
  }
  return x;
}

static inline float normalize_angle_180(float deg) {
  // Decomp: ftCo_Guard.c::normalizeAngle180 (single wrap into [-180, 180]).
  if (deg > 180.0f) {
    deg -= 360.0f;
  } else if (deg < -180.0f) {
    deg += 360.0f;
  }
  return deg;
}

static inline float normalize_angle_0(float deg) {
  // Decomp: ftCo_Guard.c::normalizeAngle0 (single wrap into [0, 360]).
  if (deg > 360.0f) {
    deg -= 360.0f;
  } else if (deg < 0.0f) {
    deg += 360.0f;
  }
  return deg;
}

static inline float sanitize_lightshield_amount(float light) {
  if (!isfinite(light)) {
    return 0.0f;
  }
  return clamp01(light);
}

void shields_guard_anim_update_tilt(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.hitlag_started_frame[idx] != 0u ||
      !msl_guard_lifecycle_action_updates_tilt(batch->state.action_id[idx])) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  const uint16_t neutral = msl_shield_guard_neutral_frame(batch->state.char_id[idx]);
  const uint16_t frame_count = msl_shield_guard_frame_count(batch->state.char_id[idx]);
  const uint16_t frame_max = frame_count > 0u ? (uint16_t)(frame_count - 1u) : neutral;

  // This function is called from the priority-1 Anim phase, after
  // input_apply_pre_input_snapshot has installed the previous post-frame controller sample and
  // before Fighter_Spaghetti publishes the current sample. It is therefore the direct
  // ftCo_80091BC4 owner; late pose/contact code must not repeat this recurrence.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_Spaghetti_8006AD10}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091BC4,ftCo_80091E78}
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  float stick_x = (float)batch->state.input_main_x[idx] * (1.0f / 80.0f);
  float stick_y = (float)batch->state.input_main_y[idx] * (1.0f / 80.0f);
  stick_x = apply_deadzone_f32(stick_x, c->lstick_deadzone_x);
  stick_y = apply_deadzone_f32(stick_y, c->lstick_deadzone_y);

  float rad = atan2f(stick_y, stick_x * facing_dir);
  if (rad < 0.0f) {
    rad += 2.0f * MSL_PI_F;
  }
  float deg = rad * (180.0f / MSL_PI_F);
  if (deg < 0.0f) {
    deg = 0.0f;
  } else if (deg > 359.0f) {
    deg = 359.0f;
  }
  const float offset = (float)batch->state.guard_tilt_x8[idx] - (float)neutral;
  const float delta = normalize_angle_180(deg - offset);
  const float next = (float)neutral + normalize_angle_0(delta * c->guard_stick_lerp_x44c + offset);
  batch->state.guard_tilt_x8[idx] = clamp_u16((uint16_t)next, 0u, frame_max);

  float mag = sqrtf(stick_x * stick_x + stick_y * stick_y);
  mag = clamp01(mag);
  const float prior = batch->state.guard_tilt_x4[idx];
  batch->state.guard_tilt_x4[idx] = prior + c->guard_stick_lerp_x44c * (mag - prior);
}

void shields_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // State flags (5 bytes) are captured from fighter offsets:
  // (0x2218, 0x221A, 0x221B, 0x221C, 0x221F) in that order.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm

  // Shield bubble size follows ftCo_Guard.c's inlineB0:
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:172-190.
  //
  // Notes:
  // - `initial_shield_size` is per-character (co attrs; ISO-extracted to data/characters/*.json).
  // - `shield_size_*` and `start_shield_health` are ftCommonData fields (ISO-extracted to
  //   data/common/ft_common_data.json).
  // - ShieldDesc center comes from the dedicated live shield JObj. MSLSHLD1 stores the sparse
  //   ftData.x20 target tree; anim_pose.c applies ftCo_80091E78 to persistent local SRT.

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);

      // Inactive/fail-closed descriptors publish no radius; a loaded active descriptor replaces
      // the center from its extracted shield-joint pose below.
      const float pos_x = batch->state.pos_x[idx];
      const float pos_y = batch->state.pos_y[idx];
      const float pos_z = batch->state.pos_z[idx];
      float sx = pos_x;
      float sy = pos_y;
      float sz = pos_z;
      float sr = 0.0f;

      const uint8_t stocks = batch->state.stocks[idx];
      const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
      const uint8_t shield_desc_active =
          (batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) != 0u;
      // x221B_b0 denotes the whole ShieldDesc family, not specifically the common Guard
      // descriptor. Character mechanics such as Marth Counter install their own descriptor data
      // and callback; they must not be projected through ftCo_Guard's joint/radius equation.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
      // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c
      const uint8_t guard_descriptor =
          msl_guard_lifecycle_action_uses_guard_shield(batch->state.action_id[idx]);
      if (stocks != 0 && shield_desc_active != 0u && guard_descriptor != 0u &&
          batch->state.shield_hp[idx] > 0.0f && c->start_shield_health > 0.0f) {
        const MslCharParams* ca = msl_char_params_fast(batch->state.char_id[idx]);
        if (ca != NULL) {
          const float scale_y = batch->state.fighter_scale_y[idx];
          // Fighter_UpdateModelScale installs ftCommon_GetModelScale(fp) on the fighter root,
          // which is fp->x34_scale.y * co_attrs.model_scaling. The shield joint is outside the
          // inverse-model-scale collision subtree, so both its center and its ftCo_80091D58 scale
          // inherit that complete root scale.
          // refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale
          // refs/melee/src/melee/ft/ftcommon.c::ftCommon_GetModelScale
          const float model_scale = scale_y * ca->model_scaling;
          if (!(model_scale > 0.0f) || !isfinite(model_scale)) {
            continue;
          }

          uint8_t center_valid = 0u;
          MslFighterCollisionPose pose;
          const uint16_t shield_part = msl_shield_part_id(batch->state.char_id[idx]);
          const float origin[3] = {0.0f, 0.0f, 0.0f};
          const float unit_x[3] = {1.0f, 0.0f, 0.0f};
          float local[3];
          float local_x_axis[3];
          float shield_parent_x_scale = 0.0f;
          if (shield_part != UINT16_MAX && fighter_pose_collision_pose(batch, idx, &pose) != 0u &&
              fighter_pose_attachment_pair_local(batch, idx, pose.msid, pose.anim_frame,
                                                 shield_part, origin, unit_x, local, local_x_axis,
                                                 NULL) == 0) {
            // ShieldDesc and ReflectDesc both attach offset-zero to ftData.x8->x11. Guard-family
            // SkipAnim states therefore consume the persistent live JObj updated in
            // ftCo_80091E78, while GuardSetOff naturally consumes its interpreted GuardDamage
            // tree. There is no late action-specific center reconstruction.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
            //   ftCo_80091D58,ftCo_80091E78,ftCo_80092450,ftCo_80092F2C}
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
            const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
            sx = pos_x + facing_dir * local[2];
            sy = pos_y + local[1];
            sz = pos_z - facing_dir * local[0];
            const float axis_x = local_x_axis[0] - local[0];
            const float axis_y = local_x_axis[1] - local[1];
            const float axis_z = local_x_axis[2] - local[2];
            shield_parent_x_scale = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
            center_valid = shield_parent_x_scale > 0.0f ? 1u : 0u;
          }

          // ftCo_800925A4 owns lightshield refresh during the priority-1 Anim callback. This late
          // pass only applies ftCo_80091D58's scale projection from the persistent result.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800925A4,ftCo_80091D58}
          const float light = sanitize_lightshield_amount(batch->state.lightshield_amount[idx]);
          const float hp_ratio = clamp01(batch->state.shield_hp[idx] / c->start_shield_health);
          float light_scale =
              (light * (c->shield_size_lightshield_max - c->shield_size_lightshield_min)) +
              c->shield_size_lightshield_min;
          if (light_scale < 0.0f) {
            light_scale = 0.0f;
          }
          const float n1 = hp_ratio * light_scale;
          const float n2 = 1.0f - c->shield_size_min_scale;
          const float scale = (n2 * n1) + c->shield_size_min_scale;

          // ftCo_80091D58 replaces the shield joint's local scale with this uniform value.
          // lbColl_80007BCC then measures ShieldDesc.size=1 through the complete live JObj matrix,
          // including any parent-chain scale. Derive that axis length from the same live pose used
          // for the center instead of assuming the fighter root is its only scale owner.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091D58,ftCo_80092450}
          // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_800077A0}
          if (center_valid != 0u) {
            sr = scale * ca->initial_shield_size * shield_parent_x_scale;
          }
        }
      }

      batch->state.shield_x[idx] = sx;
      batch->state.shield_y[idx] = sy;
      batch->state.shield_z[idx] = sz;
      batch->state.shield_radius[idx] = sr;
      // Descriptor creation/destruction owns x221B_b0 at motion-entry and callback sites. This
      // geometry phase never reconstructs descriptor identity from action history or radius; it
      // only fail-closes a dead fighter/broken shield.
      // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092450,ftCo_800925A4}
      if (!(batch->state.stocks[idx] != 0 && batch->state.shield_hp[idx] > 0.0f)) {
        batch->state.state_flags[flags_i] &=
            (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
      }
    }
  }
}
