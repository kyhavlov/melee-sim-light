#include "grab_attachment.h"

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "mtx34.h"

// Fighter_Part ids (GALE01).
// Source of truth: refs/melee/src/melee/ft/forward.h::Fighter_Part.
enum {
  MSL_FTPART_TRANSN = 1,    // FtPart_TransN
  MSL_FTPART_XROTN = 2,     // FtPart_XRotN (grab/capture victim alignment joint)
  MSL_FTPART_TRANSN2 = 52,  // FtPart_TransN2 (grab/capture constraint anchor; ftCo_800DB368)
};

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
  const float zero[3] = {0.0f, 0.0f, 0.0f};
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

static inline int pose_part_local_translation(float* out_x, float* out_y, float* out_z,
                                              uint8_t char_id, uint32_t anim_u32,
                                              float anim_frame_f32, uint16_t part_id) {
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
  float x0 = m0[3], y0 = m0[7], z0 = m0[11];
  float x1 = x0, y1 = y0, z1 = z0;
  if (frac > 0.0f) {
    const uint16_t frame1 = (uint16_t)(frame0 + 1u);
    if (frame1 != 0) {
      float m1[12];
      if (anim_pose_get_matrix(char_id, msid, frame1, part_id, m1) == 0) {
        x1 = m1[3];
        y1 = m1[7];
        z1 = m1[11];
      }
    }
  }
  const float a = (frac <= 0.0f) ? 0.0f : ((frac >= 1.0f) ? 1.0f : frac);
  *out_x = x0 + (x1 - x0) * a;
  *out_y = y0 + (y1 - y0) * a;
  *out_z = z0 + (z1 - z0) * a;
  return 0;
}

static inline int pose_part_local_point_world_facing_yrot90(
    float* out_x, float* out_y, float* out_z, uint8_t char_id, uint32_t anim_u32,
    float anim_frame_f32, uint16_t part_id, const float local_point[3], float fighter_pos_x,
    float fighter_pos_y, float fighter_pos_z, float fighter_scale_y, uint8_t facing_u8) {
  if (out_x == NULL || out_y == NULL || out_z == NULL || local_point == NULL) {
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
  msl_mtx34_mul_point(m0, local_point, &lx0, &ly0, &lz0);
  float lx1 = lx0, ly1 = ly0, lz1 = lz0;
  if (frac > 0.0f) {
    const uint16_t frame1 = (uint16_t)(frame0 + 1u);
    if (frame1 != 0) {
      float m1[12];
      if (anim_pose_get_matrix(char_id, msid, frame1, part_id, m1) == 0) {
        msl_mtx34_mul_point(m1, local_point, &lx1, &ly1, &lz1);
      }
    }
  }
  const float a = (frac <= 0.0f) ? 0.0f : ((frac >= 1.0f) ? 1.0f : frac);
  float lx = lx0 + (lx1 - lx0) * a;
  float ly = ly0 + (ly1 - ly0) * a;
  float lz = lz0 + (lz1 - lz0) * a;
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

static inline float pose_model_scale_y(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 1.0f;
  }
  float scale_y = batch->state.fighter_scale_y[idx];
  const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
  if (ch != NULL && ch->model_scaling > 0.0f) {
    scale_y *= ch->model_scaling;
  }
  return scale_y;
}

static inline float attachment_offset_scale_y(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 1.0f;
  }
  return batch->state.fighter_scale_y[idx];
}

static inline uint8_t action_is_capture_pulled_wait_victim(uint16_t action_id) {
  // Scope helper for throw-entry preservation gate:
  // - CapturePulledHi/WaitHi and CapturePulledLw/WaitLw are the common victim attachment states
  //   leading into thrown entry in the current slice.
  // - Keep CaptureDamage* excluded here so the one-frame skip stays narrowly targeted.
  switch (action_id) {
    case MSL_ACT_CAPTURE_PULLED_HI:
    case MSL_ACT_CAPTURE_WAIT_HI:
    case MSL_ACT_CAPTURE_PULLED_LW:
    case MSL_ACT_CAPTURE_WAIT_LW:
      return 1;
    default:
      return 0;
  }
}

static inline void grabbed_victim_anchor_world(float* out_x, float* out_y, float* out_z,
                                               const MslBatch* batch, int bi, int victim_p,
                                               int owner_p);

void grab_attachment_query_thrown_anchor_world(float* out_x, float* out_y, float* out_z,
                                               const MslBatch* batch, int batch_index, int victim_p,
                                               int owner_p) {
  grabbed_victim_anchor_world(out_x, out_y, out_z, batch, batch_index, victim_p, owner_p);
}

void grab_attachment_apply_capture_delta_now(MslBatch* batch, int bi, int victim_p, int owner_p) {
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
  const float owner_scale_y = pose_model_scale_y(batch, oidx);
  (void)pose_part_origin_world_facing_yrot90(
      &ax, &ay, &az, batch->state.char_id[oidx], batch->state.animation_index[oidx],
      batch->state.anim_frame_f32[oidx], anchor_part, batch->state.pos_x[oidx],
      batch->state.pos_y[oidx], batch->state.pos_z[oidx], owner_scale_y, batch->state.facing[oidx]);

  float vx = batch->state.pos_x[vidx];
  float vy = batch->state.pos_y[vidx];
  float vz = batch->state.pos_z[vidx];
  const float victim_scale_y = pose_model_scale_y(batch, vidx);
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

void grab_attachment_apply_thrown_anchor_now(MslBatch* batch, int bi, int victim_p, int owner_p) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (victim_p < 0 || victim_p >= num_players || owner_p < 0 || owner_p >= num_players ||
      victim_p == owner_p) {
    return;
  }

  const size_t vidx = msl_idx_player(bi, victim_p);
  if (!msl_action_is_thrown_victim(batch->state.action_id[vidx])) {
    return;
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  grabbed_victim_anchor_world(&ax, &ay, &az, batch, bi, victim_p, owner_p);
  (void)az;

  const float scale_y = attachment_offset_scale_y(batch, vidx);
  if (!(scale_y > 0.0f)) {
    return;
  }
  const float facing_dir = batch->state.facing[vidx] ? 1.0f : -1.0f;

  // Decomp-shaped same-frame release ownership:
  // - throw Anim installs ftCo_800DE508-style thrown positioning before later release/hit
  //   resolution consumes the attachment for the frame.
  // - ftCo_800DE508 composes owner anchor world with victim x1A70.{z,y} offsets.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
  batch->state.pos_x[vidx] = fmaf(batch->state.grab_offset_z[vidx], facing_dir * scale_y, ax);
  batch->state.pos_y[vidx] = batch->state.grab_offset_y[vidx] * scale_y + ay;
  batch->state.pos_z[vidx] = 0.0f;
}

static inline uint8_t action_is_catch_pull_state(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_CATCH_PULL ||
          action_id == (uint16_t)MSL_ACT_CATCH_DASH_PULL)
             ? 1u
             : 0u;
}

static inline void capture_wait_hi_handoff_bridge_to_lw(MslBatch* batch, int bi, int victim_p,
                                                        int owner_p) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (victim_p < 0 || victim_p >= num_players || owner_p < 0 || owner_p >= num_players) {
    return;
  }
  const size_t vidx = msl_idx_player(bi, victim_p);
  const size_t oidx = msl_idx_player(bi, owner_p);
  if (batch->state.action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_WAIT_HI ||
      batch->state.prev_action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_PULLED_HI) {
    return;
  }
  if (batch->state.action_id[oidx] != (uint16_t)MSL_ACT_CATCH_WAIT ||
      !action_is_catch_pull_state(batch->state.prev_action_id[oidx])) {
    return;
  }
  if ((batch->state.input_buttons[oidx] & (uint16_t)MSL_BUTTON_A) != 0u) {
    return;
  }
  if (batch->state.on_ground[oidx] == 0) {
    return;
  }
  if (batch->state.hitlag_started_frame[vidx] != 0 ||
      batch->state.hitlag_started_frame[oidx] != 0) {
    return;
  }

  // Decomp callback-order bridge:
  // - CatchPull_Anim can enter CatchWait and call victim transition fn_800DB6C8 in the same frame.
  // - On this handoff, CaptureWaitHi_Coll may immediately route through fn_800DBAC4 -> fn_800DBBF8,
  //   transitioning to CaptureWaitLw (0xE3) with Fighter_ChangeMotionState(..., flags=0x4000).
  // - This lite sim does not run the per-victim CaptureWait Coll callback inside the owner handoff
  //   callback chain; bridge the same-frame motion identity on grounded CatchPull->CatchWait handoff.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   fn_800DA1D8,ftCo_CaptureWaitHi_Coll,fn_800DBAC4,fn_800DBBF8
  // }
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Attack100.s::{
  //   fn_800DA1D8,ftCo_CaptureWaitHi_Coll,fn_800DBAC4,fn_800DBBF8
  // }
  //
  // Keep-frame enter (flags=0x4000 path) to preserve current action_frame while bumping motion
  // identity side-effects (instance_id / attack identity) through the standard enter helper.
  const float cur_anim = batch->state.anim_frame_f32[vidx];
  const float cur_rate = msl_f32_from_q16_16(batch->state.frame_speed_mul_fp_q16_16[vidx]);
  batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_WAIT_LW;
  batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_WAIT_LW;
  msl_anim_timebase_enter(batch, vidx, cur_anim, cur_rate);

  // Grounded handoff context: keep victim grounded with the owner's floor ownership and Y anchor.
  batch->state.on_ground[vidx] = 1u;
  if (batch->state.ground_id[oidx] != 0xFFFFu) {
    batch->state.ground_id[vidx] = batch->state.ground_id[oidx];
  }
  batch->state.pos_y[vidx] = batch->state.pos_y[oidx];
}

static inline void grabbed_victim_anchor_world(float* out_x, float* out_y, float* out_z,
                                               const MslBatch* batch, int bi, int victim_p,
                                               int owner_p) {
  (void)victim_p;
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
  // - Resolve owner anchor world from ISO-extracted `grab_capture_anchor_part_id` (capturedamage.x18
  //   source; fallback FtPart_TransN2).
  // - Minimal-state inference: replay seeds do not carry enough internal re-parented-joint state to
  //   reconstruct the full FtPart_XRotN world chain deterministically on their own.
  // - Use owner-anchor world as the thrown proxy and intentionally store the residual in
  //   grab_offset_{y,z} at reseed/entry to avoid double-count drift.
  if (out_x == NULL || out_y == NULL || out_z == NULL || batch == NULL) {
    return;
  }
  const size_t oidx = msl_idx_player(bi, owner_p);
  // Decomp-shaped proxy for ftCo_Thrown.c::ftCo_800DE508:
  // - Read world translation of victim FtPart_XRotN joint (lb_8000B1CC on re-parented joint).
  // - Then apply x1A70 offsets separately in caller.
  //
  // Proxy structure:
  // - owner_anchor_world: owner capturedamage.x18 proxy origin in world
  //   (`grab_capture_anchor_part_id`, fallback FtPart_TransN2), using unscaled pose matrices with
  //   fighter_scale_y*model_scaling.
  // - anchor_world := owner_anchor_world. Residual re-parent/constraint offset is represented by
  //   grab_offset_{y,z} (fp->x1A70 analog in this sim).
  //
  // Decomp anchors:
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18
  float ax = batch->state.pos_x[oidx];
  float ay = batch->state.pos_y[oidx];
  float az = batch->state.pos_z[oidx];
  uint16_t owner_anchor_part = (uint16_t)MSL_FTPART_TRANSN2;
  // Keep thrown-owner reconstruction on the older anchor proxy until the broader ThrowLw/ThrownLw
  // attachment family is closed cleanly. The proven current fixes in this area are bookkeeping and
  // release ordering, not a new attached-world anchor formula.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18
  const MslCharParams* och = msl_char_params(batch->state.char_id[oidx]);
  if (och != NULL) {
    owner_anchor_part = och->grab_capture_anchor_part_id;
  }
  const float owner_scale_y = pose_model_scale_y(batch, oidx);
  if (pose_part_origin_world_facing_yrot90(
          &ax, &ay, &az, batch->state.char_id[oidx], batch->state.animation_index[oidx],
          batch->state.anim_frame_f32[oidx], owner_anchor_part, batch->state.pos_x[oidx],
          batch->state.pos_y[oidx], batch->state.pos_z[oidx], owner_scale_y,
          batch->state.facing[oidx]) != 0) {
    ax = batch->state.pos_x[oidx];
    ay = batch->state.pos_y[oidx];
    az = batch->state.pos_z[oidx];
  }

  *out_x = ax;
  *out_y = ay;
  *out_z = 0.0f;
}

void grab_attachment_recompute_offsets_for_thrown_entry(MslBatch* batch, int batch_index,
                                                        int victim_p, int owner_p) {
  if (batch == NULL) {
    return;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (victim_p < 0 || victim_p >= num_players || owner_p < 0 || owner_p >= num_players ||
      victim_p == owner_p) {
    return;
  }

  const size_t vidx = msl_idx_player(batch_index, victim_p);
  const float scale_y = attachment_offset_scale_y(batch, vidx);
  if (!(scale_y > 0.0f)) {
    return;
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  grabbed_victim_anchor_world(&ax, &ay, &az, batch, batch_index, victim_p, owner_p);
  (void)az;

  const float facing_dir = batch->state.facing[vidx] ? 1.0f : -1.0f;
  batch->state.grab_offset_y[vidx] = (batch->state.pos_y[vidx] - ay) / scale_y;
  batch->state.grab_offset_z[vidx] = (batch->state.pos_x[vidx] - ax) / (scale_y * facing_dir);
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

    const float scale_y = attachment_offset_scale_y(batch, vidx);
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

      if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI) {
        capture_wait_hi_handoff_bridge_to_lw(batch, bi, p, (int)owner);
      }

      if (!msl_action_is_capture_pulled_wait_damage_victim(batch->state.action_id[vidx])) {
        const uint16_t cur_action = batch->state.action_id[vidx];
        const uint16_t prev_action = batch->state.prev_action_id[vidx];
        // One-frame reconstruction skip scope:
        // - Only when transitioning CapturePulled*/CaptureWait* -> Thrown*.
        // - Decomp anchor: thrown entry installs the accessory-driven victim position callback in
        //   ftCo_800DE3FC after throw setup; capture victim loops are maintained by fn_800DAD18.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18
        const uint8_t thrown_entered_from_capture_wait_pulled =
            (uint8_t)(msl_action_is_thrown_victim(cur_action) &&
                      action_is_capture_pulled_wait_victim(prev_action));
        const uint8_t throwlw_entry_reconstruct_now =
            (uint8_t)(thrown_entered_from_capture_wait_pulled &&
                      (cur_action == (uint16_t)MSL_ACT_THROWN_LW ||
                       batch->state.action_id[msl_idx_player(bi, (int)owner)] ==
                           (uint16_t)MSL_ACT_THROW_LW));

        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        grabbed_victim_anchor_world(&ax, &ay, &az, batch, bi, p, (int)owner);
        (void)az;

        const float scale_y = attachment_offset_scale_y(batch, vidx);
        if (!(scale_y > 0.0f)) {
          continue;
        }
        const float facing_dir = batch->state.facing[vidx] ? 1.0f : -1.0f;

        // Decomp-shaped axis mapping:
        // - ftCo_Thrown.c::ftCo_800DE508 uses x1A70.z as the "forward" offset term, applied onto
        //   pos.x with facing_dir, and adds x1A70.y to pos.y.
        //
        // Throw-entry exactness:
        // - On CapturePulled*/CaptureWait* -> Thrown* transition, offsets are recomputed from current
        //   world position.
        // - Preserve that world position exactly on the same frame (no float round-trip through
        //   offset->reconstruct) and start callback-style reconstruction on subsequent frames.
        if (!thrown_entered_from_capture_wait_pulled || throwlw_entry_reconstruct_now) {
          batch->state.pos_x[vidx] =
              fmaf(batch->state.grab_offset_z[vidx], facing_dir * scale_y, ax);
          batch->state.pos_y[vidx] = batch->state.grab_offset_y[vidx] * scale_y + ay;
          batch->state.pos_z[vidx] = 0.0f;
        }
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
        grab_attachment_apply_capture_delta_now(batch, bi, p, (int)owner);
      }
    }
  }
}
