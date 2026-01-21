#include "hurtboxes.h"

#include <stdint.h>

#include "anim_pose.h"
#include "hurtcaps_tables.h"

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline void mtx34_mul_point(const float m[12], const float v[3], float* out_x, float* out_y,
                                   float* out_z) {
  const float x = v[0];
  const float y = v[1];
  const float z = v[2];
  // m is row-major 3x4: (m00 m01 m02 tx, m10 m11 m12 ty, m20 m21 m22 tz)
  *out_x = m[0] * x + m[1] * y + m[2] * z + m[3];
  *out_y = m[4] * x + m[5] * y + m[6] * z + m[7];
  *out_z = m[8] * x + m[9] * y + m[10] * z + m[11];
}

void hurtboxes_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Decomp semantics:
  // - Hurt capsule init records are `ftHurtboxInit` (refs/melee/src/melee/ft/chara/ftCommon/types.h).
  // - ftColl_HurtboxInit assigns offsets/scale and binds `hurt->capsule.bone` to
  //   `fp->parts[hurt->capsule.bone_idx].joint` (refs/melee/src/melee/ft/ftcoll.c::ftColl_HurtboxInit).
  // - Hurt capsule endpoint world positions are computed from (bone joint matrix, offsets) via
  //   lb_8000B1CC (refs/melee/src/melee/lb/lbcollision.c::checkPos), written into HurtCapsule.a_pos/b_pos.
  //
  // We approximate that pipeline using our SSANIM01 pose sampler:
  // - anim_pose_get_matrix(char_id, msid, frame, part_id=Fighter_Part, out_3x4)
  // and then applying fighter translation (pos_x/pos_y) in world space.

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.hurtcap_count[idx] = 0;
      if (p >= num_players) {
        continue;
      }

      const uint8_t char_id = batch->state.char_id[idx];
      const MslHurtCap* caps = NULL;
      uint16_t cap_count_u16 = 0;
      if (hurtcaps_get(char_id, &caps, &cap_count_u16) != 0 || caps == NULL || cap_count_u16 == 0) {
        continue;
      }

      const uint32_t anim_u32 = batch->state.animation_index[idx];
      if (anim_u32 > 0xFFFFu) {
        continue;
      }
      const int16_t af_i16 = batch->state.action_frame[idx];
      if (af_i16 < 0) {
        continue;
      }

      const uint16_t msid = (uint16_t)anim_u32;
      const uint16_t frame = (uint16_t)af_i16;
      uint16_t cap_count = cap_count_u16;
      if (cap_count > (uint16_t)MSL_MAX_HURTCAPS) {
        cap_count = (uint16_t)MSL_MAX_HURTCAPS;
      }

      const float pos_x = batch->state.pos_x[idx];
      const float pos_y = batch->state.pos_y[idx];

      // NOTE (scaling): vanilla uses an additional per-fighter scale factor in some hurtbox-derived
      // calculations, e.g. ftCo_800A0DA4 multiplies `hurt->capsule.scale` by `fp->x34_scale.y`.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_0A01.c: scale = hurt->capsule.scale * fp->x34_scale.y;
      //
      // Our current sim does not model `fp->x34_scale` yet (Fox/Falco default to 1.0), so capsule
      // radius is currently `init.scale` only.

      // Fallback policy: missing pose data for a specific capsule only drops that capsule, keeping
      // the rest usable under partial animation coverage.
      // Output order is compacted by available pose matrices (indices do not necessarily match init order).
      uint16_t out_count = 0;
      for (uint16_t ci = 0; ci < cap_count; ci++) {
        float m[12];
        if (anim_pose_get_matrix(char_id, msid, frame, caps[ci].bone_part_id, m) != 0) {
          continue;
        }

        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        float bx = 0.0f, by = 0.0f, bz = 0.0f;
        mtx34_mul_point(m, caps[ci].a_offset, &ax, &ay, &az);
        mtx34_mul_point(m, caps[ci].b_offset, &bx, &by, &bz);

        ax += pos_x;
        ay += pos_y;
        bx += pos_x;
        by += pos_y;

        const size_t hi = idx_hurtcap(bi, p, (int)out_count);
        batch->state.hurtcap_a_x[hi] = ax;
        batch->state.hurtcap_a_y[hi] = ay;
        batch->state.hurtcap_a_z[hi] = az;
        batch->state.hurtcap_b_x[hi] = bx;
        batch->state.hurtcap_b_y[hi] = by;
        batch->state.hurtcap_b_z[hi] = bz;
        batch->state.hurtcap_radius[hi] = caps[ci].scale;
        batch->state.hurtcap_is_grabbable[hi] = caps[ci].is_grabbable ? 1 : 0;
        batch->state.hurtcap_height[hi] = caps[ci].height;
        out_count++;
      }

      batch->state.hurtcap_count[idx] = (uint8_t)out_count;
    }
  }
}
