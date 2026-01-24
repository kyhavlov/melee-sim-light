#include "hurtboxes.h"

#include <stdint.h>

#include "anim_frame.h"
#include "anim_pose.h"
#include "hit_status_tables.h"
#include "hurtbox_modes_tables.h"
#include "hurtcaps_tables.h"
#include "mtx34.h"

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
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
  // and then applying fighter translation (pos_x/pos_y/pos_z) in world space.

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.hurtcap_count[idx] = 0;
      // Clear fixed slots for stable debug readback (and to avoid stale values when pose lookups
      // or script masks disable/skip specific capsules).
      for (int ci = 0; ci < MSL_MAX_HURTCAPS; ci++) {
        const size_t hi = idx_hurtcap(bi, p, ci);
        batch->state.hurtcap_enabled[hi] = 0;
        batch->state.hurtcap_a_x[hi] = 0.0f;
        batch->state.hurtcap_a_y[hi] = 0.0f;
        batch->state.hurtcap_a_z[hi] = 0.0f;
        batch->state.hurtcap_b_x[hi] = 0.0f;
        batch->state.hurtcap_b_y[hi] = 0.0f;
        batch->state.hurtcap_b_z[hi] = 0.0f;
        batch->state.hurtcap_radius[hi] = 0.0f;
        batch->state.hurtcap_is_grabbable[hi] = 0;
        batch->state.hurtcap_height[hi] = 0;
      }
      if (p >= num_players) {
        continue;
      }

      const uint8_t char_id = batch->state.char_id[idx];

      const uint32_t anim_u32 = batch->state.animation_index[idx];

      // Partial sim-owned hurtbox_state:
      // - If movescript-derived hit status (x1988) is nonzero, overwrite hurtbox_state with it.
      // - Otherwise, preserve passthrough (represents x198C in Slippi's send policy when x1988==0).
      uint8_t hit_status = 0;
      uint8_t have_hit_status_override = 0;
      if (batch->debug_hit_status_override != NULL) {
        const uint8_t ov = batch->debug_hit_status_override[idx];
        if (ov != 0xFFu) {
          hit_status = ov;
          have_hit_status_override = 1;
        }
      }

      if (anim_u32 > 0xFFFFu) {
        if (hit_status != 0) {
          batch->state.hurtbox_state[idx] = hit_status;
        }
        continue;
      }

      const uint16_t msid = (uint16_t)anim_u32;
      const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
      const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);

      if (!have_hit_status_override) {
        (void)hit_status_get(char_id, msid, frame, &hit_status);
      }
      if (hit_status != 0) {
        batch->state.hurtbox_state[idx] = hit_status;
      }

      const MslHurtCap* caps = NULL;
      uint16_t cap_count_u16 = 0;
      if (hurtcaps_get(char_id, &caps, &cap_count_u16) != 0 || caps == NULL || cap_count_u16 == 0) {
        continue;
      }
      uint16_t cap_count = cap_count_u16;
      if (cap_count > (uint16_t)MSL_MAX_HURTCAPS) {
        cap_count = (uint16_t)MSL_MAX_HURTCAPS;
      }

      const float pos_x = batch->state.pos_x[idx];
      const float pos_y = batch->state.pos_y[idx];
      const float pos_z = batch->state.pos_z[idx];

      // NOTE (scaling): Vanilla applies a per-fighter model scale factor (fp->x34_scale.y) to
      // hurt capsule derived quantities.
      //
      // - Radius: ftCo_800A0DA4 uses `scale = hurt->capsule.scale * fp->x34_scale.y`.
      //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_0A01.c::ftCo_800A0DA4
      //
      // - Endpoints: In-engine capsule endpoints are computed via lb_8000B1CC against the bone's
      //   joint matrix. Since fp->x34_scale is applied at the model level, this scaling is baked
      //   into the runtime joint matrices. Our SSANIM01 pose matrices are extracted without that
      //   runtime fighter-scale, so we apply the same scalar uniformly to the pose-space endpoints
      //   before adding world translation.
      //
      // Facing parity: In-engine joint matrices are also fighter-facing dependent: the fighter's
      // root part is rotated about Y based on `fp->facing_dir` (see ftPartSetRotY(fp, 0, ...),
      // refs/melee/src/melee/ft/fighter.c:1180-1182), and lb_8000B1CC uses that runtime joint matrix when
      // producing world endpoints (refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC).
      //
      // Our SSANIM pose matrices are extracted in a single canonical orientation, so we
      // *approximate* facing by mirroring pose-space X only. We do not apply a true facing
      // rotation here (which would mix X/Z); this matches the sim's current 2.5D convention.
      //
      // We intentionally use only the y component (as decomp does for collision/bounds), treating
      // it as a uniform scalar for x/y/z here.
      const float scale_y = batch->state.fighter_scale_y[idx];
      const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;

      // Fallback policy: missing pose data for a specific capsule only drops that capsule, keeping
      // the rest usable under partial animation coverage.
      //
      // Ordering policy:
      // - Preserve init-table capsule ordering/identity (slot i corresponds to `caps[i]`).
      // - Disabled/intangible capsules (movescript) and capsules with missing pose data are kept in
      //   their original slot but marked `hurtcap_enabled=0` and given radius=0.
      uint32_t can_hit_mask = 0xFFFFFFFFu;
      (void)hurtbox_modes_can_hit_mask(char_id, msid, frame, cap_count, &can_hit_mask);
      for (uint16_t ci = 0; ci < cap_count; ci++) {
        const size_t hi = idx_hurtcap(bi, p, (int)ci);
        batch->state.hurtcap_is_grabbable[hi] = caps[ci].is_grabbable ? 1 : 0;
        batch->state.hurtcap_height[hi] = caps[ci].height;

        if (((can_hit_mask >> ci) & 0x1u) == 0u) {
          continue;
        }
        float m[12];
        if (anim_pose_get_matrix(char_id, msid, frame, caps[ci].bone_part_id, m) != 0) {
          continue;
        }

        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        float bx = 0.0f, by = 0.0f, bz = 0.0f;
        msl_mtx34_mul_point(m, caps[ci].a_offset, &ax, &ay, &az);
        msl_mtx34_mul_point(m, caps[ci].b_offset, &bx, &by, &bz);

        ax *= (scale_y * facing_dir);
        ay *= scale_y;
        az *= scale_y;
        bx *= (scale_y * facing_dir);
        by *= scale_y;
        bz *= scale_y;

        ax += pos_x;
        ay += pos_y;
        az += pos_z;
        bx += pos_x;
        by += pos_y;
        bz += pos_z;

        batch->state.hurtcap_enabled[hi] = 1;
        batch->state.hurtcap_a_x[hi] = ax;
        batch->state.hurtcap_a_y[hi] = ay;
        batch->state.hurtcap_a_z[hi] = az;
        batch->state.hurtcap_b_x[hi] = bx;
        batch->state.hurtcap_b_y[hi] = by;
        batch->state.hurtcap_b_z[hi] = bz;
        batch->state.hurtcap_radius[hi] = caps[ci].scale * scale_y;
      }

      batch->state.hurtcap_count[idx] = (uint8_t)cap_count;
    }
  }
}
