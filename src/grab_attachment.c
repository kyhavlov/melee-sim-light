#include "grab_attachment.h"

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "char_params.h"
#include "common_params.h"
#include "mpcoll_ground.h"
#include "move_tables.h"
#include "mtx34.h"
#include "stage_collision.h"

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

static inline int pose_part_origin_world_f32_without_transn_facing_yrot90(
    float* out_x, float* out_y, float* out_z, const MslBatch* batch, size_t player_idx,
    uint32_t anim_u32, float anim_frame_f32, uint16_t part_id, float fighter_pos_x,
    float fighter_pos_y, float fighter_pos_z, float fighter_scale_y, uint8_t facing_u8) {
  if (out_x == NULL || out_y == NULL || out_z == NULL || batch == NULL || anim_u32 > 0xFFFFu) {
    return -1;
  }

  float m[12];
  if (anim_pose_get_collision_matrix_f32(batch, player_idx, (uint16_t)anim_u32, anim_frame_f32,
                                         part_id, m) != 0) {
    return -1;
  }
  float transn[3] = {0.0f, 0.0f, 0.0f};
  if (anim_pose_get_transn_f32(batch->state.char_id[player_idx], (uint16_t)anim_u32, anim_frame_f32,
                               transn) != 0) {
    return -1;
  }

  const float zero[3] = {0.0f, 0.0f, 0.0f};
  float lx = 0.0f, ly = 0.0f, lz = 0.0f;
  msl_mtx34_mul_point(m, zero, &lx, &ly, &lz);
  lx -= transn[0];
  ly -= transn[1];
  lz -= transn[2];

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

static inline uint8_t throwf_release_uses_callback_local_transn_source(const MslBatch* batch,
                                                                       size_t owner_idx,
                                                                       uint8_t rel_hit_idx) {
  if (batch == NULL || rel_hit_idx != 0u ||
      batch->state.action_id[owner_idx] != (uint16_t)MSL_ACT_THROW_F) {
    return 0u;
  }

  MslThrowHitboxParams p = {0};
  if (!move_tables_throw_hitbox_params(batch->state.char_id[owner_idx], (uint16_t)MSL_ACT_THROW_F,
                                       rel_hit_idx, &p)) {
    return 0u;
  }

  // Falco's authored ThrowF release hit has a distinct payload from Fox's:
  // - Falco: damage 3, angle 45, kbg 135, wsk 0, bkb 35.
  // - Fox:   damage 3, angle 45, kbg 130, wsk 0, bkb 35.
  //
  // This is a move-data owner for the same-callback release-anchor source path below, not a
  // character-pair proxy. The runtime asks whether the current ThrowF script payload is the source
  // variant that samples FtPart_TransN2 before grounded ThrowF Phys consumes the current TransN
  // lane.
  // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowF"].events.set_throw_hitbox(idx=0)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
  return (uint8_t)(p.damage == 3.0f && p.angle == 45u && p.kbg == 135u && p.wsk == 0u &&
                   p.bkb == 35u);
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
  const uint16_t frame0 = msl_anim_frame_floor_u16(f);
  const uint8_t exact_integer_frame = (fabsf(f - (float)frame0) <= 1.0e-6f) ? 1u : 0u;

  float m0[12];
  if (anim_pose_get_collision_matrix_f32(batch, player_idx, (uint16_t)anim_u32, anim_frame_f32,
                                         part_id, m0) != 0) {
    return -1;
  }

  const float zero[3] = {0.0f, 0.0f, 0.0f};
  float lx0 = 0.0f, ly0 = 0.0f, lz0 = 0.0f;
  msl_mtx34_mul_point(m0, zero, &lx0, &ly0, &lz0);

  float lx = lx0;
  float ly = ly0;
  float lz = lz0;

  // Throw attachment samples the thrower's live skeleton anchor, not the BODY/ECB pose view.
  // Fractional frames use the live AObj/JObj local-SRT path (`anim_pose_get_collision_matrix_f32`),
  // which already includes the current root-translation track in the local hierarchy. Exact integer
  // frames intentionally fall through that helper's SSANIM01 matrix fast path; SSANIM stores root
  // TransN stripped into a separate tail, so recompose only on that data-contract boundary.
  //
  // Source/probe basis:
  // - ftCo_800DB368 constrains the victim XRotN to thrower FtPart_TransN2.
  // - ftCo_800DE508 samples that constrained XRotN with lb_8000B1CC before applying x1A70.
  // - Dolphin probes on FSP low throw show owner FtPart_TransN2 world Y equals
  //   (SSANIM zero-root TransN2 Y + current ThrowLw TransN.y) * model_scale.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // refs/melee/src/melee/ft/ft_081B.c::ft_80085030 (TransN offset consumer)
  // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
  if (exact_integer_frame != 0u) {
    float transn[3];
    if (pose_transn_f32(transn, char_id, anim_u32, anim_frame_f32) == 0) {
      lx += transn[0];
      ly += transn[1];
      lz += transn[2];
    }
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
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
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

static inline int throw_index_from_owner_action(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_THROW_F:
      return 0;
    case (uint16_t)MSL_ACT_THROW_B:
      return 1;
    case (uint16_t)MSL_ACT_THROW_HI:
      return 2;
    case (uint16_t)MSL_ACT_THROW_LW:
      return 3;
    default:
      return -1;
  }
}

static inline uint8_t attached_nonlow_throw_source_frame(float* out_frame, const MslBatch* batch,
                                                         size_t owner_idx, size_t victim_idx,
                                                         uint16_t owner_action,
                                                         float owner_anim_frame) {
  if (out_frame == NULL || batch == NULL) {
    return 0u;
  }
  if (owner_action != (uint16_t)MSL_ACT_THROW_B && owner_action != (uint16_t)MSL_ACT_THROW_HI) {
    return 0u;
  }
  const int throw_index = throw_index_from_owner_action(owner_action);
  if (throw_index < 0) {
    return 0u;
  }
  const MslCommonParams* c = msl_common_params();
  const MslCharParams* owner_ch = msl_char_params_fast(batch->state.char_id[owner_idx]);
  const MslCharParams* victim_ch = msl_char_params_fast(batch->state.char_id[victim_idx]);
  if (c == NULL || owner_ch == NULL || victim_ch == NULL) {
    return 0u;
  }
  float rate = 1.0f;
  if ((owner_ch->weight_independent_throws_mask & (uint8_t)(1u << throw_index)) == 0u) {
    if (!(victim_ch->weight > 0.0f) || !(c->throw_anim_speed_weight_mul > 0.0f)) {
      return 0u;
    }
    rate = 1.0f / (victim_ch->weight * c->throw_anim_speed_weight_mul);
  }
  if (!(rate > 0.0f) || !isfinite(rate) || !isfinite(owner_anim_frame)) {
    return 0u;
  }

  // Attachment pose source frame:
  // - ftCo_800DD4B0 computes the shared throw `anim_speed` as a source float.
  // - ftCo_800DD398 installs that same rate on thrower and thrown victim.
  // - ftCo_800DE508 samples the live JObj via lb_8000B1CC using fp->cur_anim_frame, not the
  //   simulator's Q16.16 action-frame guard.
  //
  // Keep discrete/script timing on the existing fixed-point timebase, but recover the nearest
  // source float frame for the attachment pose sample. This is bounded to attached non-low throw
  // placement, where the owner/victim pair and shared rate are source-owned.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD4B0,ftCo_800DD398}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  const float tick_f = floorf((owner_anim_frame / rate) + 0.5f);
  if (!(tick_f >= 0.0f) || tick_f > 1024.0f) {
    return 0u;
  }
  const float source_frame = tick_f * rate;
  if ((int)floorf(source_frame) != (int)batch->state.action_frame[owner_idx]) {
    return 0u;
  }
  *out_frame = source_frame;
  return 1u;
}

static inline uint8_t thrown_static_x1a70_offsets(float* out_y, float* out_z, const MslBatch* batch,
                                                  size_t vidx) {
  if (out_y == NULL || out_z == NULL || batch == NULL) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[vidx]);
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
static inline void grabbed_victim_anchor_world_at_owner_frame(float* out_x, float* out_y,
                                                              float* out_z, const MslBatch* batch,
                                                              int bi, int victim_p, int owner_p,
                                                              float owner_anim_frame);

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
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[oidx]);
  if (ch != NULL) {
    anchor_part = ch->grab_capture_anchor_part_id;
  }
  // CapturePulled/Wait/Damage Phys samples the live JObj graph with lb_8000B1CC after the frame's
  // AObj interpretation. Use the f32 collision-matrix path here; the integer SSANIM01 helper is a
  // close authored-pose approximation, but it can publish a different grabber anchor/XRotN delta
  // for Marth CatchPull and drift later BODY hit selection.
  // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18
  const float owner_scale_y = pose_model_scale_y(batch, oidx);
  if (pose_part_origin_world_f32_facing_yrot90(
          &ax, &ay, &az, batch, oidx, batch->state.animation_index[oidx],
          batch->state.anim_frame_f32[oidx], anchor_part, batch->state.pos_x[oidx],
          batch->state.pos_y[oidx], batch->state.pos_z[oidx], owner_scale_y,
          batch->state.facing[oidx]) != 0) {
    (void)pose_part_origin_world_facing_yrot90(
        &ax, &ay, &az, batch->state.char_id[oidx], batch->state.animation_index[oidx],
        batch->state.anim_frame_f32[oidx], anchor_part, batch->state.pos_x[oidx],
        batch->state.pos_y[oidx], batch->state.pos_z[oidx], owner_scale_y,
        batch->state.facing[oidx]);
  }

  float vx = batch->state.pos_x[vidx];
  float vy = batch->state.pos_y[vidx];
  float vz = batch->state.pos_z[vidx];
  const float victim_scale_y = pose_model_scale_y(batch, vidx);
  if (pose_part_origin_world_f32_facing_yrot90(
          &vx, &vy, &vz, batch, vidx, batch->state.animation_index[vidx],
          batch->state.anim_frame_f32[vidx], (uint16_t)MSL_FTPART_XROTN, batch->state.pos_x[vidx],
          batch->state.pos_y[vidx], batch->state.pos_z[vidx], victim_scale_y,
          batch->state.facing[vidx]) != 0) {
    (void)pose_part_origin_world_facing_yrot90(
        &vx, &vy, &vz, batch->state.char_id[vidx], batch->state.animation_index[vidx],
        batch->state.anim_frame_f32[vidx], (uint16_t)MSL_FTPART_XROTN, batch->state.pos_x[vidx],
        batch->state.pos_y[vidx], batch->state.pos_z[vidx], victim_scale_y,
        batch->state.facing[vidx]);
  }

  // Decomp-shaped application: `cur_pos += (sp20 - sp2c)` in world space.
  const float dx = ax - vx;
  const float dy = ay - vy;
  const float dz = az - vz;
  batch->state.pos_x[vidx] += dx;
  batch->state.pos_y[vidx] += dy;
  batch->state.pos_z[vidx] += dz;
}

static void capture_pulled_lw_enter_hi_and_apply_delta(MslBatch* batch, int bi, int victim_p,
                                                       int owner_p, uint16_t ground_id) {
  const size_t vidx = msl_idx_player(bi, victim_p);
  const float cur_anim = batch->state.anim_frame_f32[vidx];
  batch->state.action_id[vidx] = (uint16_t)MSL_ACT_CAPTURE_PULLED_HI;
  batch->state.animation_index[vidx] = (uint32_t)MSL_SM_CAPTURE_PULLED_HI;
  batch->state.on_ground[vidx] = 0u;
  batch->state.speed_ground_x_self[vidx] = 0.0f;
  batch->state.ecb_lock_timer[vidx] = 0u;
  if (batch->state.floor_skip_segment_id != NULL) {
    batch->state.floor_skip_segment_id[vidx] = ground_id;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[vidx]);
  if (ch != NULL && ch->max_jumps > 0u) {
    batch->state.jumps_left[vidx] = (uint8_t)(ch->max_jumps - 1u);
  }
  msl_anim_timebase_enter(batch, vidx, cur_anim, 1.0f);
  grab_attachment_apply_capture_delta_now(batch, bi, victim_p, owner_p);
}

static void capture_pulled_lw_try_air_handoff_after_delta(MslBatch* batch, int bi, int victim_p,
                                                          int owner_p, float capture_delta_y) {
  if (batch == NULL) {
    return;
  }
  const size_t vidx = msl_idx_player(bi, victim_p);
  if (batch->state.action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_PULLED_LW ||
      batch->state.on_ground[vidx] == 0u) {
    return;
  }
  const size_t oidx = msl_idx_player(bi, owner_p);
  const uint8_t sustained_capture_pulled_lw =
      (uint8_t)(batch->state.frame_start_action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW &&
                batch->state.seed_prev_action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW);
  const uint32_t stage_id = batch->state.stage_id[bi];
  const uint16_t ground_id = batch->state.ground_id[vidx];
  const uint8_t fresh_ledge_floor_loss_entry =
      (uint8_t)(sustained_capture_pulled_lw == 0u && ground_id != 0xFFFFu &&
                batch->state.action_frame[vidx] >= 1 &&
                stage_collision_floor_line_is_ledge(stage_id, ground_id) != 0u);
  const uint8_t fresh_platform_floor_loss_entry =
      (uint8_t)(sustained_capture_pulled_lw == 0u && ground_id != 0xFFFFu &&
                batch->state.action_frame[vidx] >= 1 &&
                stage_collision_floor_line_is_platform(stage_id, ground_id) != 0u);
  if (sustained_capture_pulled_lw == 0u && fresh_ledge_floor_loss_entry == 0u &&
      fresh_platform_floor_loss_entry == 0u) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const uint8_t vertical_carry_handoff =
      (uint8_t)(c != NULL && capture_delta_y > c->capture_pulled_lw_air_delta_y *
                                                   batch->state.fighter_scale_y[vidx]);
  MslMpcollFloorMaskResult floor_result = {0xFFFFu, batch->state.pos_y[vidx],
                                           batch->state.pos_x[vidx]};
  uint8_t source_floor_loss_ok = vertical_carry_handoff;
  if (source_floor_loss_ok == 0u) {
    uint8_t floor_mask = mpcoll_800477e0_floor_mask_probe(batch, vidx, &floor_result);
    if (floor_mask == 0u && ground_id != 0xFFFFu &&
        (batch->state.ecb_lock_timer[vidx] != 0u || sustained_capture_pulled_lw == 0u)) {
      floor_mask = mpcoll_800477e0_capture_root_floor_mask_probe(batch, vidx, &floor_result);
    }
    if (floor_mask != 0u) {
      return;
    }
    if (ground_id != 0xFFFFu) {
      const uint8_t owner_on_same_floor = (uint8_t)(batch->state.on_ground[oidx] != 0u &&
                                                    batch->state.ground_id[oidx] == ground_id);
      source_floor_loss_ok =
          (uint8_t)(stage_collision_floor_line_is_platform(stage_id, ground_id) != 0u ||
                    (owner_on_same_floor != 0u &&
                     stage_collision_floor_line_is_ledge(stage_id, ground_id) != 0u &&
                     stage_collision_floor_line_is_sloped(stage_id, ground_id) != 0u));
    }
  }
  if (source_floor_loss_ok == 0u) {
    return;
  }

  // CapturePulledLw source handoff:
  // - Once the victim is a sustained frame-start CapturePulledLw row, Phys applies fn_800DAD18 and
  //   can enter CapturePulledHi from the vertical carry threshold (`p_ftCommonData->x3C4 *
  //   fp->x34_scale.y`). Replay-visible frame-1 CapturePulledLw rows have already completed the
  //   catch/connect entry; their Phys/Coll callbacks are source-owned even when the prior visible
  //   action was Catch/Wait/Passive rather than Lw. Keep fresh floor-loss admission bounded to
  //   MSLSTG01 platform/ledge floors plus the live floor-mask miss below instead of a predecessor
  //   action list. The owner-side same-frame helper below handles the bounded fresh cross-floor
  //   Hi-first path before applying a Lw anchor.
  // - Coll then calls ft_8008403C(gobj, fn_800DB230); a live mpColl_800477E0 floor-mask miss owns
  //   the source floor-loss callback for platform/ledge owners expressed by MSLSTG01. Grounded
  //   rows below the vertical threshold can sparse-miss the replay-visible compact floor probe while
  //   source CollData still owns/projects the previous hard floor; do not convert those to PulledHi.
  // data/common/ft_common_data.json::capture_pulled_lw_air_delta_y
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll,fn_800DB230,fn_800DAA40}
  // refs/melee/src/melee/ft/ft_081B.c::ft_8008403C
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_UnlockECB}
  capture_pulled_lw_enter_hi_and_apply_delta(batch, bi, victim_p, owner_p, ground_id);
}

uint8_t grab_attachment_apply_capture_pulled_lw_phys_now(MslBatch* batch, int bi, int victim_p,
                                                         int owner_p) {
  if (batch == NULL) {
    return 0u;
  }
  const int num_players = (int)batch->config.num_players;
  if (victim_p < 0 || victim_p >= num_players || owner_p < 0 || owner_p >= num_players) {
    return 0u;
  }
  const size_t vidx = msl_idx_player(bi, victim_p);
  const size_t oidx = msl_idx_player(bi, owner_p);
  if (batch->state.action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_PULLED_LW ||
      batch->state.hitlag_started_frame[vidx] != 0u ||
      batch->state.hitlag_started_frame[oidx] != 0u) {
    return 0u;
  }
  if (batch->state.seed_prev_action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_PULLED_LW &&
      batch->state.frame_start_on_ground[vidx] != 0u && batch->state.on_ground[oidx] != 0u &&
      batch->state.action_id[oidx] == (uint16_t)MSL_ACT_CATCH_DASH_PULL &&
      batch->state.seed_prev_action_id[vidx] == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL &&
      batch->state.ground_id[vidx] != batch->state.ground_id[oidx]) {
    // Fresh dash-pull cross-floor LandingFallSpecial -> CapturePulledLw entry can already be
    // floor-loss-owned before the Lw Phys anchor. Convert to CapturePulledHi first, then apply the
    // single Hi anchor delta that fn_800DB230/fn_800DAC78 owns. Fresh ordinary Landing/DownBound
    // rows remain entry-owned Lw until a sustained victim Phys/Coll tick.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
    //   fn_800DB230,fn_800DAC78,fn_800DB6C8}
    capture_pulled_lw_enter_hi_and_apply_delta(batch, bi, victim_p, owner_p,
                                               batch->state.ground_id[vidx]);
    return 1u;
  }

  // Same source owner as grab_attachment_update_pre_collision(), exposed for CatchPull_Anim's
  // owner-side CapturePulled -> CaptureWait dispatch. If the victim's Lw Phys callback runs earlier
  // in the frame, fn_800DB6C8 must see CapturePulledHi and enter CaptureWaitHi rather than using the
  // stale frame-start CapturePulledLw variant.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CapturePulledLw_Phys,fn_800DB230_inline,fn_800DB6C8}
  const float pre_victim_y = batch->state.pos_y[vidx];
  grab_attachment_apply_capture_delta_now(batch, bi, victim_p, owner_p);
  capture_pulled_lw_try_air_handoff_after_delta(batch, bi, victim_p, owner_p,
                                                batch->state.pos_y[vidx] - pre_victim_y);
  return 1u;
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
  //   offsets. MSL keeps that source x1A70 vector in grab_offset_{y,z}; entry initializes it from
  //   Fighter_Create's TransN-XRotN basis, while teacher-forced reseed reconstructs the same hidden
  //   source state from the replay-visible attached world.
  // - ftCo_800DD724 release handling consumes that same attached world owner before detach.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
  batch->state.pos_x[vidx] = fmaf(batch->state.grab_offset_z[vidx], facing_dir * scale_y, ax);
  batch->state.pos_y[vidx] = batch->state.grab_offset_y[vidx] * scale_y + ay;
  batch->state.pos_z[vidx] = 0.0f;
}

void grab_attachment_apply_thrown_release_anchor_now(MslBatch* batch, int bi, int victim_p,
                                                     int owner_p, float release_anim_frame,
                                                     uint8_t owner_pose_facing) {
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

  const uint16_t action = batch->state.action_id[vidx];
  if (action != (uint16_t)MSL_ACT_THROWN_F && action != (uint16_t)MSL_ACT_THROWN_B &&
      action != (uint16_t)MSL_ACT_THROWN_HI) {
    grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, owner_p);
    return;
  }

  const size_t oidx = msl_idx_player(bi, owner_p);
  const uint8_t throwf_owner_before_victim_release =
      (uint8_t)(action == (uint16_t)MSL_ACT_THROWN_F && owner_p < victim_p &&
                throwf_release_uses_callback_local_transn_source(batch, oidx, 0u));
  if (throwf_owner_before_victim_release == 0u &&
      (action != (uint16_t)MSL_ACT_THROWN_B || owner_pose_facing == batch->state.facing[oidx])) {
    grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, owner_p);
    return;
  }

  float ax = batch->state.pos_x[oidx];
  float ay = batch->state.pos_y[oidx];
  float az = batch->state.pos_z[oidx];
  uint16_t owner_anchor_part = (uint16_t)MSL_FTPART_TRANSN2;
  const MslCharParams* och = msl_char_params_fast(batch->state.char_id[oidx]);
  if (och != NULL) {
    owner_anchor_part = och->grab_capture_anchor_part_id;
  }
  const float owner_scale_y = pose_model_scale_y(batch, oidx);
  if (throwf_owner_before_victim_release != 0u) {
    // ThrowF release owner:
    // ftCo_800DD724 consumes `set_throw_flags(hit_idx=0)` during the thrower's Anim callback, then
    // ftCo_800DDDE4 samples the thrower FtPart_TransN2 joint before grounded ThrowF Phys consumes
    // the current TransN root delta via ft_80085004/ft_80085030. The live collision matrix can
    // include that current TransN lane, while MSL's fighter root already carries the replay-visible
    // pre-Phys cur_pos. Subtract the extracted float TransN tail so the release snapshot keeps the
    // source JObj pose without double-counting root motion.
    //
    // Keep this to the authored Falco ThrowF release-hit payload. Fox's data-backed ThrowF release
    // uses the normal attached helper in aggregate controls. This only applies when the thrower GObj
    // runs before the victim GObj; owner-after-victim release rows have already run the victim's
    // callback and stay on the normal attached helper.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
    // refs/melee/src/melee/ft/ft_084E.c::{ft_80085004,ft_80085030}
    // refs/melee/src/melee/ft/ftanim.c (x68C_transNPos/x6A4_transNOffset split)
    // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowF"].events set_throw_hitbox/set_throw_flags
    if (pose_part_origin_world_f32_without_transn_facing_yrot90(
            &ax, &ay, &az, batch, oidx, batch->state.animation_index[oidx], release_anim_frame,
            owner_anchor_part, batch->state.pos_x[oidx], batch->state.pos_y[oidx],
            batch->state.pos_z[oidx], owner_scale_y, batch->state.facing[oidx]) != 0) {
      grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, owner_p);
      return;
    }
  } else {
    // Release consume samples the post-advance float owner JObj pose in the Throw Anim callback.
    // When set_throw_flags flip and release cross on the same script frame, the scalar facing flips
    // for post-frame state but the already-interpreted release JObj pose still uses the pre-flip
    // facing snapshot.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    // data/moves/{fox,falco}.json moves["ftCo_SM_Throw{F,B,Hi}"].events set_throw_flags
    (void)pose_part_origin_world_f32_facing_yrot90(
        &ax, &ay, &az, batch, oidx, batch->state.animation_index[oidx], release_anim_frame,
        owner_anchor_part, batch->state.pos_x[oidx], batch->state.pos_y[oidx],
        batch->state.pos_z[oidx], owner_scale_y, owner_pose_facing);
  }
  (void)az;
  const float scale_y = attachment_offset_scale_y(batch, vidx);
  if (!(scale_y > 0.0f)) {
    return;
  }
  const float facing_dir = batch->state.facing[vidx] ? 1.0f : -1.0f;
  batch->state.pos_x[vidx] = fmaf(batch->state.grab_offset_z[vidx], facing_dir * scale_y, ax);
  batch->state.pos_y[vidx] = batch->state.grab_offset_y[vidx] * scale_y + ay;
  batch->state.pos_z[vidx] = 0.0f;
}

static inline void grabbed_victim_anchor_world(float* out_x, float* out_y, float* out_z,
                                               const MslBatch* batch, int bi, int victim_p,
                                               int owner_p) {
  const size_t oidx = msl_idx_player(bi, owner_p);
  grabbed_victim_anchor_world_at_owner_frame(out_x, out_y, out_z, batch, bi, victim_p, owner_p,
                                             batch->state.anim_frame_f32[oidx]);
}

static inline void grabbed_victim_anchor_world_at_owner_frame(float* out_x, float* out_y,
                                                              float* out_z, const MslBatch* batch,
                                                              int bi, int victim_p, int owner_p,
                                                              float owner_anim_frame) {
  if (out_x == NULL || out_y == NULL || out_z == NULL || batch == NULL) {
    return;
  }
  // Source owner:
  // - Thrown enter calls ftCo_800DB368 (re-parenting/setup) and installs ftCo_800DE508 as an
  //   accessory callback to drive victim position each frame.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
  // - The victim's FtPart_XRotN is re-parented under the grab owner during setup (ftCo_800DB368),
  //   so its world position is driven by the owner plus the victim's own animation.
  // - The accessory callback reads that world-space joint translation via lb_8000B1CC, then adds
  //   fp->x1A70.{y,z} (scaled) onto it.
  // - MSL represents the reparented XRotN world by sampling the owner's extracted
  //   `grab_capture_anchor_part_id` path and stores the victim-side fp->x1A70 analog in
  //   grab_offset_{y,z}. Those offsets are initialized from Fighter_Create's TransN-XRotN basis for
  //   source entry rows, or reconstructed at teacher-forced reseed as explicit hidden source state.
  const size_t oidx = msl_idx_player(bi, owner_p);
  // Decomp-shaped result for ftCo_Thrown.c::ftCo_800DE508:
  // - Read world translation of victim FtPart_XRotN joint (lb_8000B1CC on re-parented joint).
  // - Then apply x1A70 offsets separately in caller.
  //
  // Runtime representation:
  // - owner_anchor_world: owner capturedamage.x18/TransN2 attachment origin in world
  //   (`grab_capture_anchor_part_id`, fallback FtPart_TransN2), using unscaled pose matrices with
  //   fighter_scale_y*model_scaling.
  // - victim-side x1A70 is represented by grab_offset_{y,z}; callers apply it after this anchor
  //   sample, matching ftCo_800DE508's `pos += fp->x1A70 * scale` order.
  //
  // Decomp anchors:
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18
  float ax = batch->state.pos_x[oidx];
  float ay = batch->state.pos_y[oidx];
  float az = batch->state.pos_z[oidx];
  uint16_t owner_anchor_part = (uint16_t)MSL_FTPART_TRANSN2;
  const MslCharParams* och = msl_char_params_fast(batch->state.char_id[oidx]);
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
        &ax, &ay, &az, batch, oidx, batch->state.animation_index[oidx], owner_anim_frame,
        owner_anchor_part, batch->state.pos_x[oidx], batch->state.pos_y[oidx],
        batch->state.pos_z[oidx], owner_scale_y, batch->state.facing[oidx]);
  } else if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_THROWN_F) {
    // ThrowF/ThrownF attachment owner:
    // - ftCo_800DE508 samples the constrained victim FtPart_XRotN and then applies x1A70.
    // - The thrower's grounded ThrowF Phys has already carried the script TransN root into cur_pos
    //   through ft_80085004/ft_80085030 before the accessory placement owner runs. Use the stripped
    //   SSANIM/root-relative joint lane here so the current-frame TransN root is not counted both in
    //   the thrower root and again in the sampled attachment joint.
    // - Keep ThrowB/ThrowHi on the float AObj/JObj path below; replay-real ThrowHi/ThrowB locks show
    //   that their current owner still needs the live float track.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_ThrowF_Phys
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80085004,ft_80085030}
    pose_status = pose_part_origin_world_facing_yrot90(
        &ax, &ay, &az, batch->state.char_id[oidx], batch->state.animation_index[oidx],
        owner_anim_frame, owner_anchor_part, batch->state.pos_x[oidx], batch->state.pos_y[oidx],
        batch->state.pos_z[oidx], owner_scale_y, batch->state.facing[oidx]);
  } else if (use_float_track_anchor) {
    // ThrownF/B/Hi attachment samples the live HSD AObj/JObj owner. CaptureWait/Pulled and low
    // throw stay on their separate attachment paths.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    float pose_frame = owner_anim_frame;
    const uint16_t owner_action = batch->state.action_id[oidx];
    if (attached_nonlow_throw_source_frame(&pose_frame, batch, oidx, vidx, owner_action,
                                           owner_anim_frame) == 0u) {
      pose_frame = owner_anim_frame;
    }
    pose_status = pose_part_origin_world_f32_facing_yrot90(
        &ax, &ay, &az, batch, oidx, batch->state.animation_index[oidx], pose_frame,
        owner_anchor_part, batch->state.pos_x[oidx], batch->state.pos_y[oidx],
        batch->state.pos_z[oidx], owner_scale_y, batch->state.facing[oidx]);
  } else {
    pose_status = pose_part_origin_world_facing_yrot90(
        &ax, &ay, &az, batch->state.char_id[oidx], batch->state.animation_index[oidx],
        owner_anim_frame, owner_anchor_part, batch->state.pos_x[oidx], batch->state.pos_y[oidx],
        batch->state.pos_z[oidx], owner_scale_y, batch->state.facing[oidx]);
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
        // Attached Thrown* world position is owned pre-collision by the shared source callback path;
        // do not run a second post-collision placement pass.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
      }

      // Victim velocities:
      // - Decomp thrown victim Phys/Coll callbacks are empty (e.g. ftCo_ThrownF_Phys/Coll), and the
      //   victim position is driven each frame by an accessory callback writing fp->cur_pos
      //   (ftCo_Thrown.c::ftCo_800DE508). That means self/KB velocity terms should not be the
      //   driver of victim translation during the grabbed/thrown window.
      // - MSL keeps public velocity lanes non-driving while the source accessory owner controls the
      //   position. This is representation hygiene for attached victims, not an alternate movement
      //   owner; detach/release restores normal Damage/Thrown aftermath velocity ownership.
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
        if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_PULLED_LW &&
            batch->state.seed_prev_action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_PULLED_LW &&
            batch->state.frame_start_on_ground[vidx] != 0u && batch->state.on_ground[oidx] != 0u &&
            batch->state.action_id[oidx] == (uint16_t)MSL_ACT_CATCH_DASH_PULL &&
            batch->state.seed_prev_action_id[vidx] == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL &&
            batch->state.ground_id[vidx] != batch->state.ground_id[oidx]) {
          // Fresh dash-pull cross-floor LandingFallSpecial -> CapturePulledLw rows are source
          // floor-loss-owned before the normal Lw Phys anchor. Enter Hi first so fn_800DAC78
          // applies the single airborne anchor delta. Same-floor and ordinary Landing/DownBound
          // fresh rows remain entry-owned and take the Lw path below.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
          //   fn_800DB230,fn_800DAC78,ftCo_CapturePulledLw_Phys}
          capture_pulled_lw_enter_hi_and_apply_delta(batch, bi, p, (int)owner,
                                                     batch->state.ground_id[vidx]);
        } else {
          const float pre_victim_y = batch->state.pos_y[vidx];
          grab_attachment_apply_capture_delta_now(batch, bi, p, (int)owner);
          capture_pulled_lw_try_air_handoff_after_delta(batch, bi, p, (int)owner,
                                                        batch->state.pos_y[vidx] - pre_victim_y);
        }
      } else if (msl_action_is_thrown_victim(batch->state.action_id[vidx])) {
        const uint16_t cur_action = batch->state.action_id[vidx];
        const uint16_t prev_action = batch->state.prev_action_id[vidx];
        const uint8_t thrown_entered_from_capture_wait_pulled =
            (uint8_t)(msl_action_is_thrown_victim(cur_action) &&
                      action_is_capture_pulled_wait_victim(prev_action));
        const uint8_t low_throw_entry = thrownlw_attached_to_throwlw(batch, bi, p, (int)owner);
        if ((!thrown_entered_from_capture_wait_pulled || !low_throw_entry) &&
            batch->state.hitlag_started_frame[vidx] == 0u) {
          // Common attached Thrown* owner:
          // - Thrown* Phys/Coll are empty; ftCo_800DE508 owns victim world position through the
          //   victim's accessory1 callback during the attached window before release consumes the
          //   attachment.
          // - Fighter_CallAcessoryCallbacks_8006C624 returns early under x2219_b5 hitlag and only
          //   runs accessory3_cb, so the accessory1 position driver must stay frozen while the
          //   victim is in the frame's post-decrement hitlag gate.
          // - CatchWait -> ThrowF/B/Hi entry installs the accessory callback before the later
          //   post-Phys accessory pass. The thrower's ThrowF/B/Hi Phys root motion has already
          //   updated the owner root by then, so non-low entry rows must run this placement after
          //   Phys instead of keeping only the immediate entry-time anchor.
          // - Low throw keeps a distinct immediate entry handoff because ftCo_800DE3FC/ftCo_800DB368
          //   installs the reparented XRotN owner before the steady accessory callback window.
          // refs/melee/src/melee/ft/fighter.c::Fighter_CallAcessoryCallbacks_8006C624
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
