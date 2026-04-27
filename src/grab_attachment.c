#include "grab_attachment.h"

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_timebase.h"
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

static inline int pose_part_origin_world_f32_facing_yrot90(
    float* out_x, float* out_y, float* out_z, const MslBatch* batch, size_t player_idx,
    uint32_t anim_u32, float anim_frame_f32, uint16_t part_id, float fighter_pos_x,
    float fighter_pos_y, float fighter_pos_z, float fighter_scale_y, uint8_t facing_u8) {
  if (out_x == NULL || out_y == NULL || out_z == NULL || batch == NULL) {
    return -1;
  }
  if (anim_u32 > 0xFFFFu) {
    return -1;
  }

  float m[12];
  if (anim_pose_get_collision_matrix_f32(batch, player_idx, (uint16_t)anim_u32, anim_frame_f32,
                                         part_id, m) != 0) {
    return -1;
  }

  const float zero[3] = {0.0f, 0.0f, 0.0f};
  float lx = 0.0f, ly = 0.0f, lz = 0.0f;
  msl_mtx34_mul_point(m, zero, &lx, &ly, &lz);

  // Decomp-shaped facing/root application mirrors pose_part_origin_world_facing_yrot90(), but the
  // local JObj matrix comes from the float AObj track owner instead of integer SSANIM01
  // interpolation.
  // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
  // refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
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

static inline int pose_transn_f32(float out_xyz[3], uint8_t char_id, uint32_t anim_u32,
                                  float anim_frame_f32) {
  if (out_xyz == NULL || anim_u32 > 0xFFFFu) {
    return -1;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  const float f = msl_anim_frame_sanitize_f32(anim_frame_f32);
  const float base_f = floorf(f);
  const float frac = f - base_f;
  const uint16_t frame0 = msl_anim_frame_floor_u16(f);

  float t0[3];
  if (anim_pose_get_transn(char_id, msid, frame0, t0) != 0) {
    return -1;
  }
  float t1[3] = {t0[0], t0[1], t0[2]};
  if (frac > 0.0f) {
    const uint16_t frame1 = (uint16_t)(frame0 + 1u);
    if (frame1 != 0) {
      (void)anim_pose_get_transn(char_id, msid, frame1, t1);
    }
  }
  const float a = (frac <= 0.0f) ? 0.0f : ((frac >= 1.0f) ? 1.0f : frac);
  out_xyz[0] = t0[0] + (t1[0] - t0[0]) * a;
  out_xyz[1] = t0[1] + (t1[1] - t0[1]) * a;
  out_xyz[2] = t0[2] + (t1[2] - t0[2]) * a;
  return 0;
}

static inline int pose_part_origin_world_f32_with_transn_facing_yrot90(
    float* out_x, float* out_y, float* out_z, const MslBatch* batch, size_t player_idx,
    uint32_t anim_u32, float anim_frame_f32, uint16_t part_id, float fighter_pos_x,
    float fighter_pos_y, float fighter_pos_z, float fighter_scale_y, uint8_t facing_u8) {
  if (out_x == NULL || out_y == NULL || out_z == NULL || batch == NULL) {
    return -1;
  }
  if (anim_u32 > 0xFFFFu) {
    return -1;
  }

  const uint8_t char_id = batch->state.char_id[player_idx];
  const float f = msl_anim_frame_sanitize_f32(anim_frame_f32);
  const float base_f = floorf(f);
  const float frac = f - base_f;
  const uint16_t frame0 = msl_anim_frame_floor_u16(f);

  float m0[12];
  if (anim_pose_get_matrix(char_id, (uint16_t)anim_u32, frame0, part_id, m0) != 0) {
    return -1;
  }

  const float zero[3] = {0.0f, 0.0f, 0.0f};
  float lx0 = 0.0f, ly0 = 0.0f, lz0 = 0.0f;
  msl_mtx34_mul_point(m0, zero, &lx0, &ly0, &lz0);

  float lx1 = lx0, ly1 = ly0, lz1 = lz0;
  if (frac > 0.0f) {
    const uint16_t frame1 = (uint16_t)(frame0 + 1u);
    if (frame1 != 0) {
      float m1[12];
      if (anim_pose_get_matrix(char_id, (uint16_t)anim_u32, frame1, part_id, m1) == 0) {
        msl_mtx34_mul_point(m1, zero, &lx1, &ly1, &lz1);
      }
    }
  }
  const float a = (frac <= 0.0f) ? 0.0f : ((frac >= 1.0f) ? 1.0f : frac);
  float lx = lx0 + (lx1 - lx0) * a;
  float ly = ly0 + (ly1 - ly0) * a;
  float lz = lz0 + (lz1 - lz0) * a;

  // Throw attachment samples the thrower's live skeleton anchor, not the BODY/ECB pose view with
  // root TransN stripped. `extract_fighter_anims.py` stores the stripped TransN tail separately, so
  // recompose it here before applying the fighter root facing/model scale.
  // Use the stripped SSANIM matrix for the constrained anchor: the generic float-AObj sampler can
  // animate the capture anchor itself on early ThrowLw frames, but the source owner is the
  // constraint plus TransN pulse, not the unconstrained local AObj track.
  //
  // Source/probe basis:
  // - ftCo_800DB368 constrains the victim XRotN to thrower FtPart_TransN2.
  // - ftCo_800DE508 samples that constrained XRotN with lb_8000B1CC before applying x1A70.
  // - Dolphin probes on FSP low throw show owner FtPart_TransN2 world Y equals
  //   (SSANIM zero-root TransN2 Y + current ThrowLw TransN.y) * model_scale.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // refs/melee/src/melee/ft/ft_081B.c::ft_80085030 (TransN offset consumer)
  float transn[3];
  if (pose_transn_f32(transn, batch->state.char_id[player_idx], anim_u32, anim_frame_f32) == 0) {
    lx += transn[0];
    ly += transn[1];
    lz += transn[2];
  }

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

static inline uint8_t thrownlw_attached_to_throwlw(const MslBatch* batch, int bi, int victim_p,
                                                   int owner_p) {
  if (batch == NULL) {
    return 0u;
  }
  const size_t vidx = msl_idx_player(bi, victim_p);
  const size_t oidx = msl_idx_player(bi, owner_p);
  return (uint8_t)(batch->state.action_id[vidx] == (uint16_t)MSL_ACT_THROWN_LW &&
                   batch->state.action_id[oidx] == (uint16_t)MSL_ACT_THROW_LW);
}

static inline uint8_t action_is_nonlow_thrown(uint16_t action_id) {
  return (uint8_t)(action_id == (uint16_t)MSL_ACT_THROWN_F ||
                   action_id == (uint16_t)MSL_ACT_THROWN_B ||
                   action_id == (uint16_t)MSL_ACT_THROWN_HI);
}

static inline uint8_t thrown_static_x1a70_offsets(float* out_y, float* out_z, const MslBatch* batch,
                                                  size_t vidx) {
  if (out_y == NULL || out_z == NULL || batch == NULL) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params(batch->state.char_id[vidx]);
  if (ch == NULL) {
    return 0u;
  }
  float attach_local[3] = {0.0f, 0.0f, 0.0f};
  if (anim_pose_get_local_translation(batch->state.char_id[vidx], (uint16_t)MSL_SM_WAIT1_0, 0u,
                                      ch->grab_capture_anchor_part_id, attach_local) != 0) {
    return 0u;
  }
  // Fighter_UnkUpdateVecFromBones_8006876C stores fp->x1A70 from the fighter's base TransN/XRotN
  // offset. For Fox/Falco, the ISO part table exposes the same local rest offset at the mapped
  // FtPart_TransN2 / grab attachment part. Use the local SRT lane, not the parent-composed matrix
  // translation; Dolphin probes on Fox/Fox ThrowLw show x1A70.y is the raw -8.300001 local offset,
  // not the baked XRotN world height or a model-scaled value.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C
  // refs/melee/src/melee/ft/ftparts.c::ftParts_GetBoneIndex
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // data/characters/{fox,falco}.json `grab_capture_anchor_part_id`
  *out_y = attach_local[1];
  *out_z = attach_local[2];
  return 1u;
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
  if (thrownlw_attached_to_throwlw(batch, bi, victim_p, owner_p)) {
    float x1a70_y = 0.0f, x1a70_z = 0.0f;
    if (thrown_static_x1a70_offsets(&x1a70_y, &x1a70_z, batch, vidx)) {
      // ThrowLw/ThrownLw attached callback owner:
      // - ftCo_800DB368 constrains victim XRotN to the thrower's FtPart_TransN2 when entering
      //   ThrownLw, and ftCo_800DE508 applies fp->x1A70 onto that anchor.
      // - In live probes this constrained XRotN world is exposed through the owner attachment anchor
      //   path (ftData.x8->x11 / extracted grab_capture_anchor_part_id), not by the older stale
      //   per-reseed grab_offset bridge or a frame-25/rate-only TransN2 carveout.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
      // data/characters/{fox,falco}.json `grab_capture_anchor_part_id`
      batch->state.pos_x[vidx] = fmaf(x1a70_z, facing_dir * scale_y, ax);
      batch->state.pos_y[vidx] = x1a70_y * scale_y + ay;
      batch->state.pos_z[vidx] = 0.0f;
      return;
    }
  }
  // Common thrown-position owner:
  // - ftCo_800DE508 reads the reparented FtPart_XRotN world, then applies victim-side x1A70
  //   offsets. This sim keeps the shared reparented-joint owner in runtime and carries the
  //   victim-side residual in grab_offset_{y,z} until that lane is promoted fully live.
  // - ftCo_800DD724 release handling consumes that same attached world owner before detach.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
  batch->state.pos_x[vidx] = fmaf(batch->state.grab_offset_z[vidx], facing_dir * scale_y, ax);
  batch->state.pos_y[vidx] = batch->state.grab_offset_y[vidx] * scale_y + ay;
  batch->state.pos_z[vidx] = 0.0f;
}

void grab_attachment_apply_thrown_release_anchor_now(MslBatch* batch, int bi, int victim_p,
                                                     int owner_p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
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

  grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, owner_p);
}

static inline void grabbed_victim_anchor_world(float* out_x, float* out_y, float* out_z,
                                               const MslBatch* batch, int bi, int victim_p,
                                               int owner_p) {
  if (out_x == NULL || out_y == NULL || out_z == NULL || batch == NULL) {
    return;
  }
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
  const size_t vidx = msl_idx_player(bi, victim_p);
  const uint8_t use_thrownlw_transn_anchor =
      thrownlw_attached_to_throwlw(batch, bi, victim_p, owner_p);
  const uint8_t use_float_track_anchor = action_is_nonlow_thrown(batch->state.action_id[vidx]);
  int pose_status = -1;
  if (use_thrownlw_transn_anchor) {
    // ThrowLw/ThrownLw's attached anchor is owner FtPart_TransN2 after the current throw animation
    // TransN pulse is recomposed. This is separate from the non-low float-track anchor path because
    // low throw intentionally uses the thrower's TransN pulse as part of its attachment owner.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    pose_status = pose_part_origin_world_f32_with_transn_facing_yrot90(
        &ax, &ay, &az, batch, oidx, batch->state.animation_index[oidx],
        batch->state.anim_frame_f32[oidx], owner_anchor_part, batch->state.pos_x[oidx],
        batch->state.pos_y[oidx], batch->state.pos_z[oidx], owner_scale_y,
        batch->state.facing[oidx]);
  } else if (use_float_track_anchor) {
    // ThrownF/B/Hi attachment samples the live HSD AObj/JObj owner. CaptureWait/Pulled and low
    // throw stay on their separate attachment paths.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    pose_status = pose_part_origin_world_f32_facing_yrot90(
        &ax, &ay, &az, batch, oidx, batch->state.animation_index[oidx],
        batch->state.anim_frame_f32[oidx], owner_anchor_part, batch->state.pos_x[oidx],
        batch->state.pos_y[oidx], batch->state.pos_z[oidx], owner_scale_y,
        batch->state.facing[oidx]);
  } else {
    pose_status = pose_part_origin_world_facing_yrot90(
        &ax, &ay, &az, batch->state.char_id[oidx], batch->state.animation_index[oidx],
        batch->state.anim_frame_f32[oidx], owner_anchor_part, batch->state.pos_x[oidx],
        batch->state.pos_y[oidx], batch->state.pos_z[oidx], owner_scale_y,
        batch->state.facing[oidx]);
  }
  if (pose_status != 0) {
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

void grab_attachment_use_static_offsets_for_thrown_entry(MslBatch* batch, int batch_index,
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
  float transn_x = 0.0f, transn_y = 0.0f, transn_z = 0.0f;
  float xrotn_x = 0.0f, xrotn_y = 0.0f, xrotn_z = 0.0f;
  if (pose_part_local_translation(&transn_x, &transn_y, &transn_z, batch->state.char_id[vidx],
                                  (uint32_t)MSL_SM_WAIT1_0, 0.0f,
                                  (uint16_t)MSL_FTPART_TRANSN) != 0 ||
      pose_part_local_translation(&xrotn_x, &xrotn_y, &xrotn_z, batch->state.char_id[vidx],
                                  (uint32_t)MSL_SM_WAIT1_0, 0.0f,
                                  (uint16_t)MSL_FTPART_XROTN) != 0) {
    grab_attachment_recompute_offsets_for_thrown_entry(batch, batch_index, victim_p, owner_p);
    return;
  }

  // Source owner: Fighter_Create initializes fp->x1A70 from TransN - XRotN once, and
  // ftCo_800DE508 applies x1A70.z/x1A70.y to the attachment joint during Thrown*.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  batch->state.grab_offset_y[vidx] = transn_y - xrotn_y;
  batch->state.grab_offset_z[vidx] = transn_z - xrotn_z;
  (void)transn_x;
  (void)xrotn_x;
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
      const size_t oidx = msl_idx_player(batch_index, (int)owner);
      if (batch->state.attached_victim_port[oidx] == (uint8_t)p) {
        batch->state.attached_victim_port[oidx] = 0xFFu;
      }
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
        const size_t oidx = msl_idx_player(bi, (int)owner);
        if (batch->state.attached_victim_port[oidx] == (uint8_t)p) {
          batch->state.attached_victim_port[oidx] = 0xFFu;
        }
        batch->state.grab_owner_port[vidx] = 0xFFu;
        continue;
      }
      if (!msl_action_is_capture_pulled_wait_damage_victim(batch->state.action_id[vidx])) {
        // Attached Thrown* world position is now owned pre-collision by the shared callback path;
        // do not re-run the older post-collision proxy reconstruction here.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
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
        const size_t oidx = msl_idx_player(bi, (int)owner);
        if (batch->state.attached_victim_port[oidx] == (uint8_t)p) {
          batch->state.attached_victim_port[oidx] = 0xFFu;
        }
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
      } else if (msl_action_is_thrown_victim(batch->state.action_id[vidx])) {
        const uint16_t cur_action = batch->state.action_id[vidx];
        const uint16_t prev_action = batch->state.prev_action_id[vidx];
        const uint8_t thrown_entered_from_capture_wait_pulled =
            (uint8_t)(msl_action_is_thrown_victim(cur_action) &&
                      action_is_capture_pulled_wait_victim(prev_action));
        if (!thrown_entered_from_capture_wait_pulled) {
          // Common attached Thrown* owner:
          // - Thrown* Phys/Coll are empty; ftCo_800DE508 owns victim world position during the
          //   attached window before release consumes the attachment.
          // - Keep the already-proven low-throw entry handoff slice separate from the broader
          //   steady attached window; the immediate `CaptureWait* -> Thrown*` row still uses the
          //   dedicated handoff split in grab_flow.c.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{
          //   ftCo_800DE3FC,ftCo_800DE508,ftCo_ThrownF_Phys,ftCo_ThrownF_Coll,ftCo_ThrownB_Phys,
          //   ftCo_ThrownB_Coll,ftCo_ThrownHi_Phys,ftCo_ThrownHi_Coll,ftCo_ThrownLw_Phys,
          //   ftCo_ThrownLw_Coll
          // }
          grab_attachment_apply_thrown_anchor_now(batch, bi, p, (int)owner);
        }
      }
    }
  }
}
