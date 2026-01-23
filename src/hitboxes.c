#include "hitboxes.h"

#include <stdint.h>

#include "anim_frame.h"
#include "anim_pose.h"
#include "hitboxes_tables.h"

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
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

void hitboxes_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // World-space hitbox centers are pose-driven:
  // - We interpret hitbox attachment records against the fighter's current submotion id
  //   (Slippi post-frame `animation_index`) and decomp-shaped anim/script time:
  //   fp->cur_anim_frame (Slippi post-frame `state_age`, float).
  //   refs/melee/src/melee/ft/ftaction.c::ftAction_80073240 (movescript timers use fp->cur_anim_frame)
  // - For each active hitbox, we sample the 3x4 bone matrix via anim_pose_get_matrix(...) and apply
  //   it to the bone-local offset (x,y,z), then translate by fighter (pos_x,pos_y,pos_z) to get world
  //   space.
  //
  // Combat note:
  // - combat_resolve() consumes these pose-driven world-space hitbox centers for hitbox-vs-hurtcap
  //   intersection (Pass 1).

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.hitbox_count[idx] = 0;

      // Clear fixed slots for stable debug readback.
      for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
        const size_t oi = idx_hitbox(bi, p, hi);
        batch->state.hitbox_enabled[oi] = 0;
        batch->state.hitbox_x[oi] = 0.0f;
        batch->state.hitbox_y[oi] = 0.0f;
        batch->state.hitbox_z[oi] = 0.0f;
        batch->state.hitbox_radius[oi] = 0.0f;
        batch->state.hitbox_damage[oi] = 0.0f;
        batch->state.hitbox_bone_part_id[oi] = 0;
        batch->state.hitbox_u16_0[oi] = 0;
        batch->state.hitbox_u16_1[oi] = 0;
        batch->state.hitbox_u16_2[oi] = 0;
        batch->state.hitbox_u16_3[oi] = 0;
        batch->state.hitbox_u16_4[oi] = 0;
        batch->state.hitbox_u16_5[oi] = 0;
        batch->state.hitbox_u16_6[oi] = 0;
        batch->state.hitbox_u16_7[oi] = 0;
        batch->state.hitbox_angle[oi] = 0;
        batch->state.hitbox_kbg[oi] = 0;
        batch->state.hitbox_wsk[oi] = 0;
        batch->state.hitbox_bkb[oi] = 0;
        batch->state.hitbox_element[oi] = 0;
        batch->state.hitbox_shield_damage[oi] = 0;
        batch->state.hitbox_sfx_severity[oi] = 0;
        batch->state.hitbox_sfx_kind[oi] = 0;
        batch->state.hitbox_flags[oi] = 0;
      }

      if (p >= num_players) {
        continue;
      }

      const uint32_t anim_u32 = batch->state.animation_index[idx];
      if (anim_u32 > 0xFFFFu) {
        continue;
      }
      const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);

      const uint8_t char_id = batch->state.char_id[idx];
      const uint16_t msid = (uint16_t)anim_u32;
      const uint16_t pose_frame = msl_anim_frame_floor_u16(anim_frame_f32);

      const MslHitboxEvent* events = NULL;
      uint16_t event_count = 0;
      if (hitboxes_get_events(char_id, msid, &events, &event_count) != 0 || events == NULL ||
          event_count == 0) {
        continue;
      }

      // Apply events up to this frame to derive the current active hitbox definitions by id.
      uint8_t have_def[MSL_MAX_HITBOXES] = {0};
      MslHitboxEvent def[MSL_MAX_HITBOXES] = {0};

      for (uint16_t ei = 0; ei < event_count; ei++) {
        const MslHitboxEvent* ev = &events[ei];
        // Decomp shape: movescript event timers are float-driven (fp->cur_anim_frame and
        // fp->frame_speed_mul) rather than an integer action_frame counter.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
        if ((float)ev->frame > anim_frame_f32) {
          continue;
        }

        if (ev->kind == 1) {
          // Clear record. hitbox_id==0xFF means clear-all; otherwise clear the specific slot.
          if (ev->hitbox_id == 0xFFu) {
            for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
              have_def[hi] = 0;
            }
          } else if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
            have_def[ev->hitbox_id] = 0;
          }
          continue;
        }

        if (ev->hitbox_id >= (uint8_t)MSL_MAX_HITBOXES) {
          continue;
        }
        def[ev->hitbox_id] = *ev;
        have_def[ev->hitbox_id] = 1;
      }

      const float pos_x = batch->state.pos_x[idx];
      const float pos_y = batch->state.pos_y[idx];
      const float pos_z = batch->state.pos_z[idx];

      uint8_t out_count = 0;
      for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
        if (!have_def[hi]) {
          continue;
        }

        float m[12];
        if (anim_pose_get_matrix(char_id, msid, pose_frame, def[hi].bone_part_id, m) != 0) {
          // Fallback policy: drop only this hitbox if its pose lookup fails.
          continue;
        }

        const float off[3] = {def[hi].x, def[hi].y, def[hi].z};
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        mtx34_mul_point(m, off, &cx, &cy, &cz);
        cx += pos_x;
        cy += pos_y;
        cz += pos_z;

        const size_t oi = idx_hitbox(bi, p, hi);
        batch->state.hitbox_enabled[oi] = 1;
        batch->state.hitbox_x[oi] = cx;
        batch->state.hitbox_y[oi] = cy;
        batch->state.hitbox_z[oi] = cz;
        batch->state.hitbox_radius[oi] = def[hi].radius;
        batch->state.hitbox_damage[oi] = def[hi].damage;
        batch->state.hitbox_bone_part_id[oi] = def[hi].bone_part_id;
        batch->state.hitbox_u16_0[oi] = def[hi].u16_0;
        batch->state.hitbox_u16_1[oi] = def[hi].u16_1;
        batch->state.hitbox_u16_2[oi] = def[hi].u16_2;
        batch->state.hitbox_u16_3[oi] = def[hi].u16_3;
        batch->state.hitbox_u16_4[oi] = def[hi].u16_4;
        batch->state.hitbox_u16_5[oi] = def[hi].u16_5;
        batch->state.hitbox_u16_6[oi] = def[hi].u16_6;
        batch->state.hitbox_u16_7[oi] = def[hi].u16_7;
        batch->state.hitbox_angle[oi] = def[hi].u16_0;
        batch->state.hitbox_kbg[oi] = def[hi].u16_1;
        batch->state.hitbox_wsk[oi] = def[hi].u16_2;
        batch->state.hitbox_bkb[oi] = def[hi].u16_3;
        batch->state.hitbox_element[oi] = (uint8_t)(def[hi].u16_4 & 0xFFu);
        batch->state.hitbox_shield_damage[oi] = (int8_t)((def[hi].u16_4 >> 8) & 0xFFu);
        batch->state.hitbox_sfx_severity[oi] = (uint8_t)(def[hi].u16_5 & 0xFFu);
        batch->state.hitbox_sfx_kind[oi] = (uint8_t)((def[hi].u16_5 >> 8) & 0xFFu);
        batch->state.hitbox_flags[oi] = def[hi].u16_6;
        out_count++;
      }

      batch->state.hitbox_count[idx] = out_count;
    }
  }
}
