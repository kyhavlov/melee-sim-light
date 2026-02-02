#include "grab_attachment.h"

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "char_params.h"
#include "mtx34.h"

// Fighter_Part ids (GALE01).
// Source of truth: refs/melee/src/melee/ft/forward.h::Fighter_Part.
enum {
  MSL_FTPART_TRANSN = 1,    // FtPart_TransN
  MSL_FTPART_XROTN = 2,     // FtPart_XRotN (grab/capture victim alignment joint)
  MSL_FTPART_RHANDN = 39,   // FtPart_RHandN (fallback anchor)
  MSL_FTPART_TRANSN2 = 52,  // FtPart_TransN2 (grab/capture constraint anchor; ftCo_800DB368)
};

static inline int grabbed_victim_transn_interp(float* out_x, float* out_y, float* out_z,
                                               const MslBatch* batch, size_t victim_idx);

static inline int pose_part_origin_world(float* out_x, float* out_y, float* out_z, uint8_t char_id,
                                         uint32_t anim_u32, float anim_frame_f32, uint16_t part_id,
                                         float fighter_pos_x, float fighter_pos_y,
                                         float fighter_pos_z, float fighter_scale_y,
                                         uint8_t facing_u8) {
  if (out_x == NULL || out_y == NULL || out_z == NULL) {
    return -1;
  }
  if (anim_u32 > 0xFFFFu) {
    return -1;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  const float f = msl_anim_frame_sanitize_f32(anim_frame_f32);
  const uint16_t frame = msl_anim_frame_floor_u16(f);

  float m[12];
  if (anim_pose_get_matrix(char_id, msid, frame, part_id, m) != 0) {
    return -1;
  }

  float lx = 0.0f, ly = 0.0f, lz = 0.0f;
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  msl_mtx34_mul_point(m, zero, &lx, &ly, &lz);

  // Facing transform:
  // - Simulator convention: apply facing as a mirror on X only.
  // - This matches the rest of the simulator's 2.5D convention (world Z is always 0.0f).
  //
  // Facing sign convention:
  // - `facing` is a 0/1 bit where 1 means facing right (Slippi post-frame direction > 0).
  //   tools/slippi/make_dataset_from_slp.py::_dir_to_facing
  const float facing_dir = facing_u8 ? 1.0f : -1.0f;
  lx *= fighter_scale_y * facing_dir;
  ly *= fighter_scale_y;
  lz *= fighter_scale_y;

  *out_x = lx + fighter_pos_x;
  *out_y = ly + fighter_pos_y;
  *out_z = lz + fighter_pos_z;
  return 0;
}

static inline int pose_part_origin_world_facing_yrot90(float* out_x, float* out_y, float* out_z,
                                                       uint8_t char_id, uint32_t anim_u32,
                                                       float anim_frame_f32, uint16_t part_id,
                                                       float fighter_pos_x, float fighter_pos_y,
                                                       float fighter_pos_z, float fighter_scale_y,
                                                       uint8_t facing_u8) {
  if (out_x == NULL || out_y == NULL || out_z == NULL) {
    return -1;
  }
  if (anim_u32 > 0xFFFFu) {
    return -1;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  const float f = msl_anim_frame_sanitize_f32(anim_frame_f32);
  const float base_f = floorf(f);
  const float frac = f - base_f;
  const uint16_t frame0 = msl_anim_frame_floor_u16(f);

  float m0[12];
  if (anim_pose_get_matrix(char_id, msid, frame0, part_id, m0) != 0) {
    return -1;
  }

  float lx0 = 0.0f, ly0 = 0.0f, lz0 = 0.0f;
  const float zero[3] = {0.0f, 0.0f, 0.0f};
  msl_mtx34_mul_point(m0, zero, &lx0, &ly0, &lz0);

  // Decomp-shaped: animation timebase can advance at fractional rates (ftAnim_SetAnimRate), so
  // interpolate joint translation when we have a non-integer anim frame.
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8
  float lx1 = lx0, ly1 = ly0, lz1 = lz0;
  if (frac > 0.0f) {
    const uint16_t frame1 = (uint16_t)(frame0 + 1u);
    if (frame1 != 0) {
      float m1[12];
      if (anim_pose_get_matrix(char_id, msid, frame1, part_id, m1) == 0) {
        msl_mtx34_mul_point(m1, zero, &lx1, &ly1, &lz1);
      }
    }
  }
  const float a = (frac <= 0.0f) ? 0.0f : ((frac >= 1.0f) ? 1.0f : frac);
  float lx = lx0 + (lx1 - lx0) * a;
  float ly = ly0 + (ly1 - ly0) * a;
  float lz = lz0 + (lz1 - lz0) * a;

  // Facing parity (decomp-first):
  // - Vanilla applies facing via a root-part Y rotation, which mixes X/Z:
  //   ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir))
  //   refs/melee/src/melee/ft/fighter.c
  //
  // Most sim pose consumers use the current 2.5D approximation (mirror X only). However,
  // grab/capture attachment points can have meaningful forward (Z) offsets; for those, we
  // apply the decomp-shaped +/-90° Y rotation so local Z contributes to world X.
  // Engine-space (decomp) root rotation:
  // - rotY = (M_PI_2 * fp->facing_dir), where fp->facing_dir is +1 when facing right, -1 when
  //   facing left.
  // - Rotation on the (local_x, local_z) pair:
  //     world_x =  fp->facing_dir * local_z
  //     world_z = -fp->facing_dir * local_x
  const float facing_dir = facing_u8 ? 1.0f : -1.0f;
  const float rx = facing_dir * lz;
  const float rz = -facing_dir * lx;
  lx = rx;
  lz = rz;

  lx *= fighter_scale_y;
  ly *= fighter_scale_y;
  lz *= fighter_scale_y;

  *out_x = lx + fighter_pos_x;
  *out_y = ly + fighter_pos_y;
  *out_z = lz + fighter_pos_z;
  return 0;
}

static inline void capture_victim_delta_apply(MslBatch* batch, int bi, int victim_p, int owner_p) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (victim_p < 0 || victim_p >= num_players || owner_p < 0 || owner_p >= num_players) {
    return;
  }

  const size_t vidx = msl_idx_player(bi, victim_p);
  const size_t oidx = msl_idx_player(bi, owner_p);

  // Decomp: capture pulled/wait/damage victim Phys aligns victim XRotN to the grabber's
  // `fp->mv.co.capturedamage.x18` joint each frame via:
  //   lb_8000B1CC(grabber->mv.co.capturedamage.x18, ..., &sp20)
  //   lb_8000B1CC(victim->parts[XRotN].joint, ..., &sp2c)
  //   fp->cur_pos += sp20 - sp2c
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::ftCo_CapturePulledHi_Phys
  //
  // `capturedamage.x18` is set from ftData.x8->x11 in:
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::fn_800D9CE8
  float ax = batch->state.pos_x[oidx];
  float ay = batch->state.pos_y[oidx];
  float az = batch->state.pos_z[oidx];
  uint16_t anchor_part = (uint16_t)MSL_FTPART_TRANSN2;
  const MslCharParams* ch = msl_char_params(batch->state.char_id[oidx]);
  if (ch != NULL) {
    anchor_part = ch->grab_capture_anchor_part_id;
  }
  // Contract: SSANIM01 matrices are unscaled; multiply translations by fighter_scale_y*model_scaling
  // to match lb_8000B1CC world space (ftCo_Attack100.s::fn_800DAD18).
  float owner_scale_y = batch->state.fighter_scale_y[oidx];
  if (ch != NULL && ch->model_scaling > 0.0f) {
    owner_scale_y *= ch->model_scaling;
  }
  (void)pose_part_origin_world_facing_yrot90(
      &ax, &ay, &az, batch->state.char_id[oidx], batch->state.animation_index[oidx],
      batch->state.anim_frame_f32[oidx], anchor_part, batch->state.pos_x[oidx],
      batch->state.pos_y[oidx], batch->state.pos_z[oidx], owner_scale_y, batch->state.facing[oidx]);

  float vx = batch->state.pos_x[vidx];
  float vy = batch->state.pos_y[vidx];
  float vz = batch->state.pos_z[vidx];
  float victim_scale_y = batch->state.fighter_scale_y[vidx];
  const MslCharParams* vch = msl_char_params(batch->state.char_id[vidx]);
  if (vch != NULL && vch->model_scaling > 0.0f) {
    victim_scale_y *= vch->model_scaling;
  }
  (void)pose_part_origin_world_facing_yrot90(
      &vx, &vy, &vz, batch->state.char_id[vidx], batch->state.animation_index[vidx],
      batch->state.anim_frame_f32[vidx], (uint16_t)MSL_FTPART_XROTN, batch->state.pos_x[vidx],
      batch->state.pos_y[vidx], batch->state.pos_z[vidx], victim_scale_y,
      batch->state.facing[vidx]);

  // Decomp-shaped application: `cur_pos += (sp20 - sp2c)` in world space.
  batch->state.pos_x[vidx] += ax - vx;
  batch->state.pos_y[vidx] += ay - vy;
  batch->state.pos_z[vidx] += az - vz;
}

static inline void grabbed_victim_anchor_world(float* out_x, float* out_y, float* out_z,
                                               const MslBatch* batch, int bi, int victim_p,
                                               int owner_p) {
  // Approximate the `lb_8000B1CC(fp->parts[ftParts_GetBoneIndex(fp, FtPart_XRotN)].joint)` anchor used by
  // ftCo_Thrown.c::ftCo_800DE508.
  //
  // Decomp-shaped structure:
  // - Thrown enter calls ftCo_800DB368 (re-parenting/setup) and installs ftCo_800DE508 as an
  //   accessory callback to drive victim position each frame.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
  // - The victim's FtPart_XRotN is re-parented under the grab owner during setup (ftCo_800DB368),
  //   so its world position is driven by the owner plus the victim's own animation.
  // - The accessory callback reads that world-space joint translation via lb_8000B1CC, then adds
  //   fp->x1A70.{y,z} (scaled) onto it.
  //
  // Simulator approximation:
  // - SSANIM01 v3 currently stores only a subset of Fighter_Part joints as u8 part ids, and does not
  //   include FtPart_ThrowN (part id 51) or inserted/runtime-reparented joints like FtPart_XRotN (2)
  //   for many suite-relevant motion states.
  // - Try FtPart_RHandN as a suite-focused proxy owner anchor when present; otherwise fall back to
  //   owner root (fp->cur_pos) and rely on victim TransN tail + offsets for stability.
  // - Add victim TransN tail (SSANIM01 v3) in the owner's (mirrored+scaled) space.
  if (out_x == NULL || out_y == NULL || out_z == NULL || batch == NULL) {
    return;
  }
  const size_t oidx = msl_idx_player(bi, owner_p);
  const size_t vidx = msl_idx_player(bi, victim_p);

  float ax = batch->state.pos_x[oidx];
  float ay = batch->state.pos_y[oidx];
  float az = batch->state.pos_z[oidx];
  // Keep thrown-anchor behavior stable while iterating on capture attachment: use the historical
  // suite-stable proxy FtPart_RHandN only.
  (void)pose_part_origin_world(
      &ax, &ay, &az, batch->state.char_id[oidx], batch->state.animation_index[oidx],
      batch->state.anim_frame_f32[oidx], (uint16_t)MSL_FTPART_RHANDN, batch->state.pos_x[oidx],
      batch->state.pos_y[oidx], batch->state.pos_z[oidx], batch->state.fighter_scale_y[oidx],
      batch->state.facing[oidx]);

  float tx = 0.0f, ty = 0.0f, tz = 0.0f;
  if (grabbed_victim_transn_interp(&tx, &ty, &tz, batch, vidx) != 0) {
    tx = 0.0f;
    ty = 0.0f;
    tz = 0.0f;
  }

  // Victim TransN tail scaling:
  // - anim_pose_get_transn returns the raw SSANIM01 v3 TransN tail in model space.
  // - Convert to engine/world units using ISO-derived `model_scaling` and per-fighter model scale
  //   (`fp->x34_scale.y` / fighter_scale_y).
  //
  // Decomp context:
  // - The thrown/capture position driver uses a world-space joint translation via lb_8000B1CC, and
  //   adds offsets scaled by fp->x34_scale.y:
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508.
  // - `model_scaling` is a per-character attribute used for TransN-like consumers:
  //   refs/melee/src/melee/ft/types.h::ftCo_DatAttrs::model_scaling.
  const MslCharParams* ch = msl_char_params(batch->state.char_id[vidx]);
  const float model_scaling = (ch != NULL) ? ch->model_scaling : 1.0f;
  const float victim_scale_y = batch->state.fighter_scale_y[vidx];

  // 2.5D convention:
  // - Use TransN.z as the forward-axis offset and apply it onto world X with facing_dir.
  const float facing_dir = batch->state.facing[vidx] ? 1.0f : -1.0f;
  ax += facing_dir * (tz * model_scaling * victim_scale_y);
  ay += ty * model_scaling * victim_scale_y;

  *out_x = ax;
  *out_y = ay;
  *out_z = 0.0f;
}

static inline int grabbed_victim_transn_interp(float* out_x, float* out_y, float* out_z,
                                               const MslBatch* batch, size_t victim_idx) {
  if (out_x == NULL || out_y == NULL || out_z == NULL || batch == NULL) {
    return -1;
  }
  const uint32_t anim_u32 = batch->state.animation_index[victim_idx];
  if (anim_u32 > 0xFFFFu) {
    return -1;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  const float f = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[victim_idx]);
  const float base_f = floorf(f);
  const float frac = f - base_f;
  const uint16_t frame0 = msl_anim_frame_floor_u16(f);

  float t0[3] = {0.0f, 0.0f, 0.0f};
  if (anim_pose_get_transn(batch->state.char_id[victim_idx], msid, frame0, t0) != 0) {
    return -1;
  }
  float t1[3] = {t0[0], t0[1], t0[2]};
  if (frac > 0.0f) {
    const uint16_t frame1 = (uint16_t)(frame0 + 1u);
    if (frame1 != 0) {
      float tmp[3] = {0.0f, 0.0f, 0.0f};
      if (anim_pose_get_transn(batch->state.char_id[victim_idx], msid, frame1, tmp) == 0) {
        t1[0] = tmp[0];
        t1[1] = tmp[1];
        t1[2] = tmp[2];
      }
    }
  }
  const float a = (frac <= 0.0f) ? 0.0f : ((frac >= 1.0f) ? 1.0f : frac);
  *out_x = t0[0] + (t1[0] - t0[0]) * a;
  *out_y = t0[1] + (t1[1] - t0[1]) * a;
  *out_z = t0[2] + (t1[2] - t0[2]) * a;
  return 0;
}

void grab_attachment_reseed_init(MslBatch* batch, int batch_index) {
  if (batch == NULL) {
    return;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  for (int p = 0; p < num_players; p++) {
    const size_t vidx = msl_idx_player(batch_index, p);
    batch->state.grab_offset_y[vidx] = 0.0f;
    batch->state.grab_offset_z[vidx] = 0.0f;

    uint8_t owner = batch->state.grab_owner_port[vidx];
    if (owner == 0xFFu || (int)owner >= num_players) {
      batch->state.grab_owner_port[vidx] = 0xFFu;
      continue;
    }
    if (!msl_action_is_grabbed_victim(batch->state.action_id[vidx])) {
      batch->state.grab_owner_port[vidx] = 0xFFu;
      continue;
    }

    if (msl_action_is_capture_pulled_wait_damage_victim(batch->state.action_id[vidx])) {
      // Capture pulled/wait/damage: victim translation is driven by a per-frame delta
      // (ftCo_Attack100.c::fn_800DAD18), not a persistent x1A70-like offset.
      batch->state.grab_offset_y[vidx] = 0.0f;
      batch->state.grab_offset_z[vidx] = 0.0f;
      continue;
    }

    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    grabbed_victim_anchor_world(&ax, &ay, &az, batch, batch_index, p, (int)owner);
    (void)az;

    const float scale_y = batch->state.fighter_scale_y[vidx];
    if (!(scale_y > 0.0f)) {
      continue;
    }
    const float facing_dir = batch->state.facing[vidx] ? 1.0f : -1.0f;
    batch->state.grab_offset_y[vidx] = (batch->state.pos_y[vidx] - ay) / scale_y;
    batch->state.grab_offset_z[vidx] = (batch->state.pos_x[vidx] - ax) / (scale_y * facing_dir);
  }
}

void grab_attachment_update_post_collision(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t vidx = msl_idx_player(bi, p);
      uint8_t owner = batch->state.grab_owner_port[vidx];
      if (owner == 0xFFu || (int)owner >= num_players) {
        batch->state.grab_owner_port[vidx] = 0xFFu;
        continue;
      }
      if (!msl_action_is_grabbed_victim(batch->state.action_id[vidx])) {
        batch->state.grab_owner_port[vidx] = 0xFFu;
        continue;
      }

      if (!msl_action_is_capture_pulled_wait_damage_victim(batch->state.action_id[vidx])) {
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        grabbed_victim_anchor_world(&ax, &ay, &az, batch, bi, p, (int)owner);
        (void)az;

        const float scale_y = batch->state.fighter_scale_y[vidx];
        if (!(scale_y > 0.0f)) {
          continue;
        }
        const float facing_dir = batch->state.facing[vidx] ? 1.0f : -1.0f;

        // Decomp-shaped axis mapping:
        // - ftCo_Thrown.c::ftCo_800DE508 uses x1A70.z as the "forward" offset term, applied onto
        //   pos.x with facing_dir, and adds x1A70.y to pos.y.
        batch->state.pos_x[vidx] = ax + facing_dir * (batch->state.grab_offset_z[vidx] * scale_y);
        batch->state.pos_y[vidx] = ay + batch->state.grab_offset_y[vidx] * scale_y;
        batch->state.pos_z[vidx] = 0.0f;
      }

      // Victim velocities:
      // - Decomp thrown victim Phys/Coll callbacks are empty (e.g. ftCo_ThrownF_Phys/Coll), and the
      //   victim position is driven each frame by an accessory callback writing fp->cur_pos
      //   (ftCo_Thrown.c::ftCo_800DE508). That means self/KB velocity terms should not be the
      //   driver of victim translation during the grabbed/thrown window.
      // - The engine does not necessarily zero these velocity fields explicitly; we do it here as
      //   a simulator approximation to avoid one-step drift and to prevent downstream systems (most
      //   notably stage collision) from consulting stale motion terms for attached victims.
      batch->state.speed_air_x_self[vidx] = 0.0f;
      batch->state.speed_ground_x_self[vidx] = 0.0f;
      batch->state.speed_y_self[vidx] = 0.0f;
      batch->state.speed_x_attack[vidx] = 0.0f;
      batch->state.speed_y_attack[vidx] = 0.0f;
    }
  }
}

void grab_attachment_update_pre_collision(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t vidx = msl_idx_player(bi, p);
      uint8_t owner = batch->state.grab_owner_port[vidx];
      if (owner == 0xFFu || (int)owner >= num_players) {
        batch->state.grab_owner_port[vidx] = 0xFFu;
        continue;
      }
      if (!msl_action_is_grabbed_victim(batch->state.action_id[vidx])) {
        batch->state.grab_owner_port[vidx] = 0xFFu;
        continue;
      }

      if (msl_action_is_capture_pulled_wait_damage_victim(batch->state.action_id[vidx])) {
        const size_t oidx = msl_idx_player(bi, (int)owner);
        // Hitlag freezes motion/physics advancement (no Phys/Coll callbacks).
        // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
        if (batch->state.hitlag_started_frame[vidx] != 0 ||
            batch->state.hitlag_started_frame[oidx] != 0) {
          continue;
        }
        // Decomp ordering: CapturePulled*/CaptureDamage* runs fn_800DAD18 in Phys, then runs Coll.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAD18,ftCo_CapturePulledHi_Coll}
        capture_victim_delta_apply(batch, bi, p, (int)owner);
      }
    }
  }
}
