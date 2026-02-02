#include "hitboxes.h"

#include <stdint.h>

#include "anim_frame.h"
#include "anim_pose.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "mtx34.h"

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
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
      // Also derive the active hitbox definitions at the end of the previous integer frame
      // (pose_frame - 1), so we can reproduce Melee's hitlist clear-on-enable edge without relying
      // on sim-owned "previous frame hitbox enabled" state (important for teacher-forced reseed).
      //
      // Decomp: hitlists are cleared/copied only when the hitbox slot becomes enabled from Disabled
      // (or its hit_group changes).
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      uint8_t have_prev[MSL_MAX_HITBOXES] = {0};
      MslHitboxEvent def_prev[MSL_MAX_HITBOXES] = {0};

      // Build hitbox definitions for:
      // - pose_frame - 1 (previous integer frame): for detecting enable edges, and
      // - pose_frame (current integer frame): used to emit the active hitboxes this step.
      //
      // We intentionally avoid relying on sim-owned "last frame hitboxes" state so that a
      // teacher-forced reseed can still reproduce the correct enable edge just from move events.
      //
      // Decomp: hitlists are cleared/copied only when a hitbox slot becomes enabled from Disabled
      // (or its hit_group changes).
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      uint8_t cur_inited = 0;
      uint8_t group_active[MSL_HITLIST_GROUPS] = {0};

      for (uint16_t ei = 0; ei < event_count; ei++) {
        const MslHitboxEvent* ev = &events[ei];
        // Decomp shape: movescript event timers are float-driven (fp->cur_anim_frame and
        // fp->frame_speed_mul) rather than an integer action_frame counter.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
        if ((float)ev->frame > anim_frame_f32) {
          continue;
        }
        if (ev->frame > pose_frame) {
          // Events are extracted in chronological order; nothing after pose_frame can fire this step.
          break;
        }

        // Apply all events strictly before pose_frame to build the state at pose_frame-1.
        if (ev->frame < pose_frame) {
          if (ev->kind == 1) {
            if (ev->hitbox_id == 0xFFu) {
              for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
                have_prev[hi] = 0;
              }
            } else if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
              have_prev[ev->hitbox_id] = 0;
            }
          } else if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
            def_prev[ev->hitbox_id] = *ev;
            have_prev[ev->hitbox_id] = 1;
          }
          continue;
        }

        // We are at pose_frame: initialize the current state from the pose_frame-1 snapshot once.
        if (!cur_inited) {
          for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
            if (have_prev[hi]) {
              def[hi] = def_prev[hi];
              have_def[hi] = 1;
              const uint8_t g = hitlist_hit_group_from_u16_7(def[hi].u16_7);
              if (g < (uint8_t)MSL_HITLIST_GROUPS) {
                group_active[g] = 1;
              }
            }
          }
          cur_inited = 1;
        }

        // Apply pose_frame events to produce the current active definition set, and clear the
        // hitlist on a per-group rising edge (including clear->create sequences within the frame).
        if (ev->kind == 1) {
          if (ev->hitbox_id == 0xFFu) {
            for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
              have_def[hi] = 0;
            }
            for (uint8_t g = 0; g < (uint8_t)MSL_HITLIST_GROUPS; g++) {
              group_active[g] = 0;
            }
          } else if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
            const uint8_t hb = ev->hitbox_id;
            uint8_t old_g = 0;
            if (have_def[hb]) {
              old_g = hitlist_hit_group_from_u16_7(def[hb].u16_7);
            }
            have_def[hb] = 0;
            if (old_g < (uint8_t)MSL_HITLIST_GROUPS) {
              uint8_t any = 0;
              for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
                if (!have_def[hi]) {
                  continue;
                }
                if (hitlist_hit_group_from_u16_7(def[hi].u16_7) == old_g) {
                  any = 1;
                  break;
                }
              }
              group_active[old_g] = any;
            }
          }
        } else if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
          const uint8_t hb = ev->hitbox_id;
          const uint8_t new_g = hitlist_hit_group_from_u16_7(ev->u16_7);
          const uint8_t was_active = group_active[new_g];

          uint8_t old_g = 0;
          uint8_t had_old = 0;
          if (have_def[hb]) {
            had_old = 1;
            old_g = hitlist_hit_group_from_u16_7(def[hb].u16_7);
          }

          def[hb] = *ev;
          have_def[hb] = 1;
          group_active[new_g] = 1;

          if (!was_active) {
            // Decomp: ftColl_800768A0 clears (lbColl_80008440) unless it can copy an existing
            // active hitbox with the same hit_group.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
            // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008440
            hitlist_clear_group(batch, bi, p, new_g);
          }

          if (had_old && old_g != new_g && old_g < (uint8_t)MSL_HITLIST_GROUPS) {
            uint8_t any = 0;
            for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
              if (!have_def[hi]) {
                continue;
              }
              if (hitlist_hit_group_from_u16_7(def[hi].u16_7) == old_g) {
                any = 1;
                break;
              }
            }
            group_active[old_g] = any;
          }
        }
      }

      if (!cur_inited) {
        // No pose_frame events fired: the active set is the pose_frame-1 snapshot.
        for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
          if (have_prev[hi]) {
            def[hi] = def_prev[hi];
            have_def[hi] = 1;
          }
        }
      }

      // Hitlist clear-on-enable (per hit_group).
      //
      // Decomp:
      // - When a hitbox becomes enabled (or its hit_group changes), Melee clears its victim list
      //   unless it can copy an existing active hitbox with the same `hit_group`.
      // - This is mediated by ftColl_800768A0 calling lbColl_80008440 (clear) or lbColl_CopyHitCapsule (copy).
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008440
      //
      // Simulator policy: implemented above during pose_frame event application.

      const float pos_x = batch->state.pos_x[idx];
      const float pos_y = batch->state.pos_y[idx];
      const float pos_z = batch->state.pos_z[idx];
      const float scale_y = batch->state.fighter_scale_y[idx];
      const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;

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

        // NOTE (scaling + facing): In-engine attachment points come from `lb_8000B1CC` against the
        // bound joint's runtime HSD_JObj matrix (refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC).
        //
        // That runtime joint matrix already includes:
        // - per-fighter model scale (`fp->x34_scale.y`) via Fighter_UpdateModelScale ->
        //   HSD_JObjSetScale (refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale),
        // - per-fighter facing via a root-part Y rotation set from `fp->facing_dir`
        //   (ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir)),
        //    refs/melee/src/melee/ft/fighter.c:1180-1182).
        //
        // Our SSANIM01 v3 pose matrices are extracted in a single canonical orientation and do
        // not include the runtime facing rotation or fp->x34_scale. We apply scale in pose space
        // and apply the same decomp-shaped root facing rotation used elsewhere in the sim
        // (mixing X/Z).
        //
        // Decomp: the root part is rotated about Y by +/-90° based on `fp->facing_dir`:
        // `ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir))`.
        // refs/melee/src/melee/ft/fighter.c
        //
        // IMPORTANT: This must match hurtboxes_refresh() (hurtcaps) and other pose-derived geometry
        // (e.g. blaster spawn offsets). Inconsistent facing transforms can create suite-visible
        // false-positive BODY overlaps (hitlag/hitstun applied when ref has none).
        //
        // Policy:
        //   local = (pose_mtx * offset) * scale_y; local = rotY90(local, facing_dir); world = pos + local.
        const float off[3] = {def[hi].x, def[hi].y, def[hi].z};
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        msl_mtx34_mul_point(m, off, &cx, &cy, &cz);
        cx *= scale_y;
        cy *= scale_y;
        cz *= scale_y;

        // Decomp: apply root facing rotation (rotY = M_PI_2 * facing_dir), mixing X/Z.
        const float cx_rot_x = facing_dir * cz;
        const float cx_rot_z = -facing_dir * cx;
        cx = cx_rot_x;
        cz = cx_rot_z;
        cx += pos_x;
        cy += pos_y;
        cz += pos_z;

        float radius = def[hi].radius;
        if (!msl_hitbox_ignore_fighter_scale(def[hi].u16_6)) {
          radius *= scale_y;
        }

        const size_t oi = idx_hitbox(bi, p, hi);
        batch->state.hitbox_enabled[oi] = 1;
        batch->state.hitbox_x[oi] = cx;
        batch->state.hitbox_y[oi] = cy;
        batch->state.hitbox_z[oi] = cz;
        batch->state.hitbox_radius[oi] = radius;
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
