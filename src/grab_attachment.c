#include "grab_attachment.h"

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "char_params.h"
#include "common_params.h"
#include "combat_internal.h"
#include "coll_env_flags.h"
#include "ecb_tables.h"
#include "ftcommon_ecb.h"
#include "fighter_pose.h"
#include "mpcoll_callback_queries.h"
#include "mpcoll_ecb_pose.h"
#include "mpcoll_source_air.h"
#include "motion_state_owners.h"

// Fighter_Part ids (GALE01).
// Source of truth: refs/melee/src/melee/ft/forward.h::Fighter_Part.
enum {
  MSL_FTPART_XROTN = 2,     // FtPart_XRotN (grab/capture victim alignment joint)
  MSL_FTPART_TRANSN2 = 52,  // FtPart_TransN2 (grab/capture constraint anchor; ftCo_800DB368)
};

void grab_attachment_install_xrotn_constraint(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.grab_constraint_x2226_b2[idx] != 0u) {
    return;
  }
  batch->state.grab_constraint_x2174_valid[idx] = 0u;
  const uint32_t anim = batch->state.animation_index[idx];
  float saved[3] = {0.0f, 0.0f, 0.0f};
  if (anim <= 0xFFFFu &&
      anim_pose_get_local_translation_f32(batch->state.char_id[idx], (uint16_t)anim,
                                          batch->state.anim_frame_f32[idx],
                                          (uint16_t)MSL_FTPART_XROTN, saved) == 0) {
    batch->state.grab_constraint_x2174_x[idx] = saved[0];
    batch->state.grab_constraint_x2174_y[idx] = saved[1];
    batch->state.grab_constraint_x2174_z[idx] = saved[2];
    batch->state.grab_constraint_x2174_valid[idx] = 1u;
  }
  // ftCo_800DB368 first switches XRotN to zero Euler rotation and saves local translation, then
  // prepends the TransN2 RObj and publishes x2226_b2. The simulator's constrained pose owner
  // handles the matrix substitution; this bit is the same lifetime/admission gate as source.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
  batch->state.grab_constraint_x2226_b2[idx] = 1u;
}

void grab_attachment_refresh_xrotn_constraint_after_anim(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.grab_constraint_x2226_b2[idx] == 0u) {
    return;
  }
  const uint32_t anim = batch->state.animation_index[idx];
  float saved[3] = {0.0f, 0.0f, 0.0f};
  if (anim <= 0xFFFFu &&
      anim_pose_get_animated_local_translation_f32(batch->state.char_id[idx], (uint16_t)anim,
                                                   batch->state.anim_frame_f32[idx],
                                                   (uint16_t)MSL_FTPART_XROTN, saved) == 0) {
    batch->state.grab_constraint_x2174_x[idx] = saved[0];
    batch->state.grab_constraint_x2174_y[idx] = saved[1];
    batch->state.grab_constraint_x2174_z[idx] = saved[2];
    batch->state.grab_constraint_x2174_valid[idx] = 1u;
  }
  // ftAnim_8006EBA4 interprets the JObj tree, then ftCo_800DB500 conditionally snapshots XRotN
  // into x2174 before the MotionState Anim callback may release the constraint. Preserve that
  // recurrent source field rather than treating ftCo_800DB368's entry value as immutable.
  // refs/melee/src/melee/ft/ftanim.c::{ftAnim_8006EBA4,ftAnim_8006F368}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB500
}

static inline int pose_part_origin_world_live(float* out_x, float* out_y, float* out_z,
                                              const MslBatch* batch, size_t player_idx,
                                              uint32_t anim_u32, float anim_frame_f32,
                                              uint16_t part_id, float fighter_pos_x,
                                              float fighter_pos_y, float fighter_pos_z,
                                              uint8_t facing_u8) {
  if (out_x == NULL || out_y == NULL || out_z == NULL || batch == NULL || anim_u32 > UINT16_MAX) {
    return -1;
  }
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  float local[3];
  if (fighter_pose_attachment_local(batch, player_idx, (uint16_t)anim_u32, anim_frame_f32, part_id,
                                    origin, local) != 0) {
    return -1;
  }
  // fighter_pose is the single live JObj owner: it composes the extracted float AObj track,
  // TransN/root-motion split, model scale, XRotN mutation, and constraint state. This wrapper only
  // applies the fighter root's +/-90-degree facing basis and world translation, matching
  // lb_8000B1CC's world origin read.
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DAC78,fn_800DAD18}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  const float facing = facing_u8 ? 1.0f : -1.0f;
  *out_x = fighter_pos_x + facing * local[2];
  *out_y = fighter_pos_y + local[1];
  *out_z = fighter_pos_z - facing * local[0];
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

static inline uint8_t fighter_static_x1a70(float* out_y, float* out_z, const MslBatch* batch,
                                           size_t idx) {
  if (out_y == NULL || out_z == NULL || batch == NULL) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return 0u;
  }
  // Fighter_Create samples this vector once from the costume JObj rest tree. Wait1 is not an
  // equivalent proxy: Marth/Falcon/Sheik animate away a real rest-pose Z component there.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // data/characters/*.json::{static_x1a70_y,static_x1a70_z}
  *out_y = ch->static_x1a70_y;
  *out_z = ch->static_x1a70_z;
  return 1u;
}

static inline void grabbed_victim_anchor_world(float* out_x, float* out_y, float* out_z,
                                               const MslBatch* batch, int bi, int owner_p);
static inline void grabbed_victim_anchor_world_at_owner_frame(float* out_x, float* out_y,
                                                              float* out_z, const MslBatch* batch,
                                                              int bi, int owner_p,
                                                              float owner_anim_frame);

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
  // AObj interpretation. The shared fighter-pose owner publishes that exact live path.
  // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18
  (void)pose_part_origin_world_live(&ax, &ay, &az, batch, oidx, batch->state.animation_index[oidx],
                                    batch->state.anim_frame_f32[oidx], anchor_part,
                                    batch->state.pos_x[oidx], batch->state.pos_y[oidx],
                                    batch->state.pos_z[oidx], batch->state.facing[oidx]);

  float vx = batch->state.pos_x[vidx];
  float vy = batch->state.pos_y[vidx];
  float vz = batch->state.pos_z[vidx];
  (void)pose_part_origin_world_live(&vx, &vy, &vz, batch, vidx, batch->state.animation_index[vidx],
                                    batch->state.anim_frame_f32[vidx], (uint16_t)MSL_FTPART_XROTN,
                                    batch->state.pos_x[vidx], batch->state.pos_y[vidx],
                                    batch->state.pos_z[vidx], batch->state.facing[vidx]);

  // Decomp-shaped application: `cur_pos += (sp20 - sp2c)` in world space.
  const float dx = ax - vx;
  const float dy = ay - vy;
  const float dz = az - vz;
  batch->state.pos_x[vidx] += dx;
  batch->state.pos_y[vidx] += dy;
  batch->state.pos_z[vidx] += dz;
}

uint8_t grab_attachment_capture_low_to_high_now(MslBatch* batch, int bi, int victim_p,
                                                int owner_p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || victim_p < 0 ||
      victim_p >= (int)batch->config.num_players || owner_p < 0 ||
      owner_p >= (int)batch->config.num_players || victim_p == owner_p) {
    return 0u;
  }
  const size_t vidx = msl_idx_player(bi, victim_p);
  const uint16_t high_action = msl_action_capture_high_from_low(batch->state.action_id[vidx]);
  if (high_action == 0u) {
    return 0u;
  }
  const float cur_anim = batch->state.anim_frame_f32[vidx];
  const uint16_t ground_id = batch->state.ground_id[vidx];
  batch->state.action_id[vidx] = high_action;
  batch->state.animation_index[vidx] =
      (uint32_t)msl_motion_state_submotion_id(batch->state.char_id[vidx], high_action);
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
  return 1u;
}

static void capture_low_try_enter_high_after_delta(MslBatch* batch, int bi, int victim_p,
                                                   int owner_p, float capture_delta_y) {
  if (batch == NULL) {
    return;
  }
  const size_t vidx = msl_idx_player(bi, victim_p);
  if (msl_action_capture_high_from_low(batch->state.action_id[vidx]) == 0u ||
      batch->state.on_ground[vidx] == 0u) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL ||
      capture_delta_y <= c->capture_pulled_lw_air_delta_y * batch->state.fighter_scale_y[vidx]) {
    return;
  }

  // The three grounded CapturePulled/Wait/Damage Phys callbacks sample the same two live joints,
  // publish their world-space delta, and change to their high counterpart iff the pre-publication
  // `dy` exceeds x3C4 * fighter scale. Floor loss remains the generated Coll callback;
  // predecessor actions, stage kinds, and replay-visible floor ids are not part of this owner.
  // data/common/ft_common_data.json::capture_pulled_lw_air_delta_y
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CapturePulledLw_Phys,ftCo_CaptureWaitLw_Phys,ftCo_CaptureDamageLw_Phys,
  //   fn_800DAD18,fn_800DAA40}
  (void)grab_attachment_capture_low_to_high_now(batch, bi, victim_p, owner_p);
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
  // Airborne CaptureCaptain hangs from the same reparented XRotN/TransN2 anchor substrate as
  // attached Thrown* (ftCo_800DB368 reparent + ftCo_800DB464 accessory placement); without this
  // admission the Falcon Dive hang never snapped the victim to anchor+x1A70 (witness: victim
  // held ~11u above the source hang across the whole hold).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800DB368,ftCo_800DB464}
  if (!msl_action_is_thrown_victim(batch->state.action_id[vidx]) &&
      batch->state.action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_CAPTAIN) {
    return;
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  grabbed_victim_anchor_world(&ax, &ay, &az, batch, bi, owner_p);
  (void)az;
  const float scale_y = attachment_offset_scale_y(batch, vidx);
  if (!(scale_y > 0.0f)) {
    return;
  }
  const float facing_dir = batch->state.facing[vidx] ? 1.0f : -1.0f;
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
  if (!msl_action_is_thrown_victim(batch->state.action_id[vidx]) ||
      batch->state.grab_constraint_x2226_b2[vidx] == 0u) {
    return;
  }
  const uint16_t action = batch->state.action_id[vidx];
  if (action != (uint16_t)MSL_ACT_THROWN_F && action != (uint16_t)MSL_ACT_THROWN_B &&
      action != (uint16_t)MSL_ACT_THROWN_HI) {
    grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, owner_p);
    return;
  }

  const size_t oidx = msl_idx_player(bi, owner_p);
  float ax = batch->state.pos_x[oidx];
  float ay = batch->state.pos_y[oidx];
  float az = batch->state.pos_z[oidx];
  uint16_t owner_anchor_part = (uint16_t)MSL_FTPART_TRANSN2;
  const MslCharParams* och = msl_char_params_fast(batch->state.char_id[oidx]);
  if (och != NULL) {
    owner_anchor_part = och->grab_capture_anchor_part_id;
  }
  // Release consumes the same live attachment joint as the steady accessory callback, at the
  // already-advanced callback frame. When facing flips on this script pulse, the interpreted JObj
  // still uses the pre-flip facing snapshot passed by the throw Anim owner.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  if (pose_part_origin_world_live(&ax, &ay, &az, batch, oidx, batch->state.animation_index[oidx],
                                  release_anim_frame, owner_anchor_part, batch->state.pos_x[oidx],
                                  batch->state.pos_y[oidx], batch->state.pos_z[oidx],
                                  owner_pose_facing) != 0) {
    grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, owner_p);
    return;
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

static inline void throw_release_colldata_last_pos_from_sample_owner(float* out_x, float* out_y,
                                                                     const MslBatch* batch,
                                                                     size_t owner_idx) {
  if (out_x == NULL || out_y == NULL || batch == NULL) {
    return;
  }
  float bottom_rel_y = batch->state.coll_ecb_bottom_rel_y[owner_idx];
  float top_rel_y = batch->state.coll_ecb_top_rel_y[owner_idx];
  if (!isfinite(bottom_rel_y) || !isfinite(top_rel_y)) {
    const uint32_t anim = batch->state.animation_index[owner_idx];
    const int frame = (int)msl_anim_frame_floor_u16(
        msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[owner_idx]));
    bottom_rel_y = msl_ecb_bottom_rel_y(batch->state.char_id[owner_idx], anim, frame);
    top_rel_y = msl_ecb_top_rel_y(batch->state.char_id[owner_idx], anim, frame);
  }

  // ftCo_800DDDE4 writes released-fighter CollData.last_pos from the selected sample owner
  // (fp3) plus inline3=0 and inline2=0.5*(fp3->coll_data.ecb.top.y + bottom.y), then calls
  // mpColl_800471F8 on the constrained fighter.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  *out_x = batch->state.pos_x[owner_idx];
  *out_y = batch->state.pos_y[owner_idx] + 0.5f * (top_rel_y + bottom_rel_y);
}

void grab_attachment_falcon_dive_release_collision_now(MslBatch* batch, size_t constrained_idx,
                                                       size_t sample_owner_idx) {
  float source_last_x = batch->state.pos_x[sample_owner_idx];
  float source_last_y = batch->state.pos_y[sample_owner_idx];
  throw_release_colldata_last_pos_from_sample_owner(&source_last_x, &source_last_y, batch,
                                                    sample_owner_idx);
  const float target_x = batch->state.pos_x[constrained_idx];
  const float target_y = batch->state.pos_y[constrained_idx];

  // DDDE4 installs last_pos from the selected sample owner, marks Clear, installs the release
  // target as cur_pos, and then 471F8's mpCollPrev copies that target into prev_pos. These writes
  // are unconditional; floor contact only changes the final cur_pos.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8}
  batch->state.coll_last_pos_x[constrained_idx] = source_last_x;
  batch->state.coll_last_pos_y[constrained_idx] = source_last_y;
  batch->state.prev_pos_x[constrained_idx] = target_x;
  batch->state.prev_pos_y[constrained_idx] = target_y;
  batch->state.floor_sweep_prev_pos_x[constrained_idx] = target_x;
  batch->state.floor_sweep_prev_pos_y[constrained_idx] = target_y;
  batch->state.floor_sweep_prev_source_owned[constrained_idx] = 1u;
  batch->state.floor_sweep_prev_runtime_owned[constrained_idx] = 1u;
  const int bi = (int)(constrained_idx / (size_t)MSL_MAX_PLAYERS);
  const int p = (int)(constrained_idx % (size_t)MSL_MAX_PLAYERS);
  (void)mpcoll_source_air_run_release_471f8(batch, bi, p, source_last_x, source_last_y);
}

void grab_attachment_apply_falcon_dive_air_release_anchor_now(MslBatch* batch, int bi, int victim_p,
                                                              int falcon_p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (victim_p < 0 || victim_p >= num_players || falcon_p < 0 || falcon_p >= num_players ||
      victim_p == falcon_p) {
    return;
  }
  // ftCo_800DDDE4 selects Falcon as fp3 (sample owner) and CaptureCaptain as fp4 (constrained
  // fighter) when Falcon's x221B_b7 is clear. This helper owns only the source
  // `vec + fp4->x1A70` placement; the caller orders constraint clear, ECB unlock, and floor probe.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, falcon_p);
}

void grab_attachment_apply_falcon_dive_ground_release_anchor_now(MslBatch* batch, int bi,
                                                                 int falcon_p, int victim_p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (falcon_p < 0 || falcon_p >= num_players || victim_p < 0 || victim_p >= num_players ||
      falcon_p == victim_p) {
    return;
  }

  const size_t fidx = msl_idx_player(bi, falcon_p);
  const size_t vidx = msl_idx_player(bi, victim_p);

  float x1a70_y = 0.0f;
  float x1a70_z = 0.0f;
  if (!fighter_static_x1a70(&x1a70_y, &x1a70_z, batch, fidx)) {
    return;
  }

  float ax = batch->state.pos_x[vidx];
  float ay = batch->state.pos_y[vidx];
  float az = batch->state.pos_z[vidx];
  uint16_t victim_anchor_part = (uint16_t)MSL_FTPART_TRANSN2;
  const MslCharParams* victim_ch = msl_char_params_fast(batch->state.char_id[vidx]);
  if (victim_ch != NULL) {
    victim_anchor_part = victim_ch->grab_capture_anchor_part_id;
  }
  // Falcon Dive grounded-victim release uses the victim's raw joint mapped from
  // FtPart_TransN2 by ftParts_GetBoneIndex.
  //
  // Source owner:
  // - ftCo_800DDDE4 sets fp3=victim/fp4=Falcon when attacker x221B_b7 is true.
  // - lb_8000B1CC(fp3->parts[FtPart_TransN2].joint, ..., &vec) samples this anchor, then
  //   fp4->cur_pos is set from `vec + fp4->x1A70` before mpColl_800471F8.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/ft/forward.h::Fighter_Part
  // CaptureCaptain's generated target-character row cross-bakes Falcon's donor FigaTree onto the
  // victim skeleton, matching Fighter_ChangeMotionState(..., arg3=Falcon). This keeps the sampled
  // TransN2 joint character- and facing-correct without a replay-fit root offset.
  // tools/extraction/extract_fighter_anims.py::_figatree_source_character
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCaptain.c::ftCo_8009CA0C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  if (pose_part_origin_world_live(&ax, &ay, &az, batch, vidx, batch->state.animation_index[vidx],
                                  batch->state.anim_frame_f32[vidx], victim_anchor_part,
                                  batch->state.pos_x[vidx], batch->state.pos_y[vidx],
                                  batch->state.pos_z[vidx], batch->state.facing[vidx]) != 0) {
    return;
  }
  const float falcon_scale_y = attachment_offset_scale_y(batch, fidx);
  if (!(falcon_scale_y > 0.0f)) {
    return;
  }
  const float facing_dir = batch->state.facing[fidx] ? 1.0f : -1.0f;
  batch->state.pos_x[fidx] = fmaf(x1a70_z, facing_dir * falcon_scale_y, ax);
  batch->state.pos_y[fidx] = fmaf(x1a70_y, falcon_scale_y, ay);
  batch->state.pos_z[fidx] = 0.0f;
}

static uint8_t falcon_dive_dc920_place_constrained_xrotn(MslBatch* batch, size_t constrained_idx) {
  if (batch == NULL) {
    return 0u;
  }
  float x1a70_y = 0.0f;
  float x1a70_z = 0.0f;
  if (!fighter_static_x1a70(&x1a70_y, &x1a70_z, batch, constrained_idx)) {
    return 0u;
  }

  float joint_x = batch->state.pos_x[constrained_idx];
  float joint_y = batch->state.pos_y[constrained_idx];
  float joint_z = batch->state.pos_z[constrained_idx];
  const float scale_y = pose_model_scale_y(batch, constrained_idx);
  if (pose_part_origin_world_live(
          &joint_x, &joint_y, &joint_z, batch, constrained_idx,
          batch->state.animation_index[constrained_idx],
          batch->state.anim_frame_f32[constrained_idx], (uint16_t)MSL_FTPART_XROTN,
          batch->state.pos_x[constrained_idx], batch->state.pos_y[constrained_idx],
          batch->state.pos_z[constrained_idx], batch->state.facing[constrained_idx]) != 0) {
    return 0u;
  }
  const float facing_dir = batch->state.facing[constrained_idx] ? 1.0f : -1.0f;
  batch->state.pos_x[constrained_idx] = fmaf(x1a70_z, facing_dir * scale_y, joint_x);
  batch->state.pos_y[constrained_idx] = fmaf(x1a70_y, scale_y, joint_y);
  batch->state.pos_z[constrained_idx] = 0.0f;
  return 1u;
}

void grab_attachment_dc920_release_now(MslBatch* batch, int bi, int owner_p, int victim_p,
                                       uint8_t constrained_owner) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (owner_p < 0 || owner_p >= num_players || victim_p < 0 || victim_p >= num_players ||
      owner_p == victim_p) {
    return;
  }
  const size_t fidx = msl_idx_player(bi, owner_p);
  const size_t vidx = msl_idx_player(bi, victim_p);
  if (batch->state.attached_victim_port[fidx] != (uint8_t)victim_p ||
      batch->state.grab_owner_port[vidx] != (uint8_t)owner_p) {
    return;
  }

  const size_t constrained_idx = constrained_owner ? fidx : vidx;
  const size_t sample_idx = constrained_owner ? vidx : fidx;
  const uint8_t constrained_was_grounded = batch->state.on_ground[constrained_idx] ? 1u : 0u;

  // DC920 unparents and clears x2226_b2 before sampling/placing the selected constrained fighter.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_800DC920
  const uint8_t constraint_was_set = batch->state.grab_constraint_x2226_b2[constrained_idx];
  batch->state.grab_constraint_x2226_b2[constrained_idx] = 0u;
  if (constraint_was_set != 0u) {
    // DC920 samples the selected fighter's own live XRotN after any DCFD4 Damage transition, then
    // applies that fighter's x1A70. This is distinct from DDDE4's TransN2 attachment placement.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_800DC920
    (void)falcon_dive_dc920_place_constrained_xrotn(batch, constrained_idx);
    const float fallback_root_x = batch->state.pos_x[constrained_idx];
    const float fallback_root_y = batch->state.pos_y[constrained_idx];

    MslMpcollFloorMaskResult floor = {0};
    const uint8_t connected_floor = msl_mpcoll_query_capturecut_connected_floor(
        batch, constrained_idx, batch->state.ground_id[sample_idx], &floor);
    if (floor.candidate_published != 0u) {
      // DC920 assigns the connected candidate floor before projection/tolerance acceptance. A later
      // x3BC rejection enters fallback with that candidate still published in CollData.floor.
      batch->state.ground_id[constrained_idx] = floor.candidate_ground_id;
    }
    if (connected_floor) {
      batch->state.pos_x[constrained_idx] = floor.corrected_pos_x;
      batch->state.pos_y[constrained_idx] = floor.corrected_pos_y;
      batch->state.ground_id[constrained_idx] = floor.ground_id;

      // mpColl_80043680 rebases cur_pos, prev_pos, and last_pos to the corrected root and marks the
      // ECB Clear. Publish every represented root/ownership lane as one packet.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043680
      batch->state.prev_pos_x[constrained_idx] = floor.corrected_pos_x;
      batch->state.prev_pos_y[constrained_idx] = floor.corrected_pos_y;
      batch->state.floor_sweep_prev_pos_x[constrained_idx] = floor.corrected_pos_x;
      batch->state.floor_sweep_prev_pos_y[constrained_idx] = floor.corrected_pos_y;
      batch->state.floor_sweep_prev_source_owned[constrained_idx] = 1u;
      batch->state.floor_sweep_prev_runtime_owned[constrained_idx] = 1u;
      batch->state.coll_last_pos_x[constrained_idx] = floor.corrected_pos_x;
      batch->state.coll_last_pos_y[constrained_idx] = floor.corrected_pos_y;
      batch->state.coll_substep_prev_pos_x[constrained_idx] = floor.corrected_pos_x;
      batch->state.coll_substep_prev_pos_y[constrained_idx] = floor.corrected_pos_y;
      batch->state.coll_substep_cur_pos_x[constrained_idx] = floor.corrected_pos_x;
      batch->state.coll_substep_cur_pos_y[constrained_idx] = floor.corrected_pos_y;
      mpcoll_clear_current_ecb_packet(batch, constrained_idx);
    } else {
      float source_last_x = batch->state.pos_x[sample_idx];
      float source_last_y = batch->state.pos_y[sample_idx];
      throw_release_colldata_last_pos_from_sample_owner(&source_last_x, &source_last_y, batch,
                                                        sample_idx);
      if (!constrained_was_grounded) {
        // DC920 unlocks only the constrained fighter, and only before the airborne 477E0 fallback.
        msl_ftcommon_unlock_ecb(batch, constrained_idx);
      }
      const uint8_t floor_hit = mpcoll_source_air_run_dc920_fallback(
          batch, bi, (int)(constrained_idx % (size_t)MSL_MAX_PLAYERS), source_last_x, source_last_y,
          constrained_was_grounded);
      if (floor_hit) {
        if (!constrained_was_grounded) {
          msl_ftcommon_8007d6a4(batch, msl_char_params_fast(batch->state.char_id[constrained_idx]),
                                constrained_idx);
        }
      } else if (constrained_was_grounded) {
        combat_apply_ftCommon_8007D5D4_ground_to_air(batch, constrained_idx);
      }
      if (!floor_hit) {
        batch->state.pos_x[constrained_idx] = fallback_root_x;
        batch->state.pos_y[constrained_idx] = fallback_root_y;
      }
    }
  }

  batch->state.attached_victim_port[fidx] = 0xFFu;
  batch->state.grab_owner_port[vidx] = 0xFFu;
  batch->state.grab_constraint_x2226_b2[fidx] = 0u;
  batch->state.grab_constraint_x2226_b2[vidx] = 0u;
  batch->state.falcon_specialhi_x221b_b7[fidx] = 0u;
  batch->state.catch_kind_x1a68[fidx] = 0u;
  batch->state.catch_target_mask_x1a6a[fidx] = 0u;
  batch->state.catch_target_mask_x1a6a[vidx] = 0u;
  const size_t fflags = fidx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
  const size_t vflags = vidx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
  batch->state.state_flags[fflags] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_B7;
  batch->state.state_flags[vflags] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_B7;
}

static inline void grabbed_victim_anchor_world(float* out_x, float* out_y, float* out_z,
                                               const MslBatch* batch, int bi, int owner_p) {
  const size_t oidx = msl_idx_player(bi, owner_p);
  grabbed_victim_anchor_world_at_owner_frame(out_x, out_y, out_z, batch, bi, owner_p,
                                             batch->state.anim_frame_f32[oidx]);
}

static inline void grabbed_victim_anchor_world_at_owner_frame(float* out_x, float* out_y,
                                                              float* out_z, const MslBatch* batch,
                                                              int bi, int owner_p,
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
  // - MSL represents the reparented XRotN world by sampling the owner's raw joint mapped from
  //   FtPart_TransN2 and stores the victim-side fp->x1A70 analog in grab_offset_{y,z}. Those
  //   offsets are initialized from Fighter_Create's TransN-XRotN basis for source entry rows, or
  //   reconstructed at teacher-forced reseed as explicit hidden source state.
  const size_t oidx = msl_idx_player(bi, owner_p);
  // Decomp-shaped result for ftCo_Thrown.c::ftCo_800DE508:
  // - Read world translation of victim FtPart_XRotN joint (lb_8000B1CC on re-parented joint).
  // - Then apply x1A70 offsets separately in caller.
  //
  // Runtime representation:
  // - owner_anchor_world: the character-specific raw joint mapped from FtPart_TransN2, using
  //   unscaled pose matrices with fighter_scale_y*model_scaling.
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
  // All attached victim states read the same live re-parented JObj with lb_8000B1CC. Action- and
  // throw-direction-specific pose samplers duplicated the pose owner's TransN, AObj, scale, and
  // constraint decisions and were not source mechanics. The centralized fighter-pose owner
  // already composes those source fields for the requested live frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  const int pose_status = pose_part_origin_world_live(
      &ax, &ay, &az, batch, oidx, batch->state.animation_index[oidx], owner_anim_frame,
      owner_anchor_part, batch->state.pos_x[oidx], batch->state.pos_y[oidx],
      batch->state.pos_z[oidx], batch->state.facing[oidx]);
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
  grabbed_victim_anchor_world(&ax, &ay, &az, batch, batch_index, owner_p);
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
  float x1a70_y, x1a70_z;
  if (!fighter_static_x1a70(&x1a70_y, &x1a70_z, batch, vidx)) {
    grab_attachment_recompute_offsets_for_thrown_entry(batch, batch_index, victim_p, owner_p);
    return;
  }

  // Source owner: Fighter_Create initializes fp->x1A70 from TransN - XRotN once, and
  // ftCo_800DE508 applies x1A70.z/x1A70.y to the attachment joint during Thrown*.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  batch->state.grab_offset_y[vidx] = x1a70_y;
  batch->state.grab_offset_z[vidx] = x1a70_z;
}

void grab_attachment_reseed_init(MslBatch* batch, int batch_index) {
  if (batch == NULL) {
    return;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  // Rebuild the owner-side victim pointer before any attachment offset or constraint consumer.
  // Slippi exposes only the victim-side owner link; source keeps both fp->victim_gobj directions
  // live before ftCo_800DB368 installs the Dive constraint.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD398
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCaptain.c::ftCo_8009CA0C
  for (int owner = 0; owner < num_players; owner++) {
    const size_t idx = msl_idx_player(batch_index, owner);
    batch->state.attached_victim_port[idx] = 0xFFu;
    batch->state.grab_constraint_x2226_b2[idx] = 0u;
  }
  for (int victim = 0; victim < num_players; victim++) {
    const size_t vidx = msl_idx_player(batch_index, victim);
    const uint8_t owner = batch->state.grab_owner_port[vidx];
    if (owner == 0xFFu || owner >= (uint8_t)num_players || owner == (uint8_t)victim ||
        !msl_action_is_grabbed_victim(batch->state.action_id[vidx])) {
      batch->state.grab_owner_port[vidx] = 0xFFu;
      continue;
    }
    const size_t oidx = msl_idx_player(batch_index, (int)owner);
    if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_CAPTAIN &&
        (batch->state.char_id[oidx] != (uint8_t)MSL_CHAR_ID_FALCON ||
         batch->state.action_id[oidx] != (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH)) {
      batch->state.grab_owner_port[vidx] = 0xFFu;
      continue;
    }
    if (batch->state.attached_victim_port[oidx] == 0xFFu ||
        (uint8_t)victim < batch->state.attached_victim_port[oidx]) {
      batch->state.attached_victim_port[oidx] = (uint8_t)victim;
    }
  }

  for (int p = 0; p < num_players; p++) {
    const size_t vidx = msl_idx_player(batch_index, p);
    batch->state.grab_offset_y[vidx] = 0.0f;
    batch->state.grab_offset_z[vidx] = 0.0f;

    uint8_t owner = batch->state.grab_owner_port[vidx];
    if (owner == 0xFFu || (int)owner >= num_players) {
      batch->state.grab_owner_port[vidx] = 0xFFu;
      continue;
    }
    const size_t oidx = msl_idx_player(batch_index, (int)owner);
    if (batch->state.attached_victim_port[oidx] != (uint8_t)p) {
      // Source has one victim_gobj per owner. Deterministic lowest-port selection above owns the
      // reciprocal pair; losing duplicate victim rows must not retain a one-sided x1A5C link.
      batch->state.grab_owner_port[vidx] = 0xFFu;
      continue;
    }
    if (msl_action_is_thrown_victim(batch->state.action_id[vidx])) {
      // Every ordinary Thrown* entry calls ftCo_800DB368 before installing the motion and
      // accessory callback. Reconstruct its live XRotN constraint together with the reciprocal
      // victim pointers so DDDE4 release executes the source constraint-clear/ECB-unlock branch.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
      batch->state.grab_constraint_x2226_b2[vidx] = 1u;
    }
    if (msl_action_is_capture_pulled_wait_damage_victim(batch->state.action_id[vidx])) {
      // Capture pulled/wait/damage: victim translation is driven by a per-frame delta
      // (ftCo_Attack100.c::fn_800DAD18), not a persistent x1A70-like offset.
      batch->state.grab_offset_y[vidx] = 0.0f;
      batch->state.grab_offset_z[vidx] = 0.0f;
      continue;
    }

    if (batch->state.action_id[vidx] == (uint16_t)MSL_ACT_CAPTURE_CAPTAIN) {
      // CaptureCaptain hangs on the per-fighter STATIC x1A70 vector (Fighter_Create's
      // TransN-XRotN basis) — a constant, not hidden accumulated state. Reconstructing from the
      // seeded world would preserve a pre-snap connect-row gap; installing the static offsets
      // lets the entry-row step snap to anchor+x1A70 exactly like the live path.
      // refs/melee/src/melee/ft/fighter.c::Fighter_UnkUpdateVecFromBones_8006876C
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800DB368,ftCo_800DB464}
      grab_attachment_use_static_offsets_for_thrown_entry(batch, batch_index, p, (int)owner);
      continue;
    }

    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    grabbed_victim_anchor_world(&ax, &ay, &az, batch, batch_index, (int)owner);
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

void grab_attachment_falcon_dive_constraint_reseed_init(MslBatch* batch, int batch_index) {
  if (batch == NULL || batch_index < 0 || batch_index >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int owner_p = 0; owner_p < num_players; owner_p++) {
    const size_t oidx = msl_idx_player(batch_index, owner_p);
    if (batch->state.action_id[oidx] != (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH) {
      continue;
    }
    const uint8_t victim_p = batch->state.attached_victim_port[oidx];
    if (victim_p == 0xFFu || victim_p >= (uint8_t)num_players || victim_p == (uint8_t)owner_p) {
      continue;
    }
    const size_t vidx = msl_idx_player(batch_index, (int)victim_p);
    if (batch->state.grab_owner_port[vidx] != (uint8_t)owner_p ||
        batch->state.action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_CAPTAIN) {
      continue;
    }
    if (batch->state.falcon_specialhi_x221b_b7[oidx] != 0u) {
      batch->state.grab_constraint_x2226_b2[oidx] = 1u;
    } else {
      batch->state.grab_constraint_x2226_b2[vidx] = 1u;
    }
  }
}

uint8_t grab_attachment_map_callback_runs(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t selector = batch->state.live_coll_wrapper_selector_kind[idx];
  if (selector == (uint8_t)MSL_COLL_SELECTOR_FALCON_HICATCH_CONDITIONAL &&
      batch->state.falcon_specialhi_x221b_b7[idx] != 0u) {
    return 0u;
  }
  if ((selector == (uint8_t)MSL_COLL_SELECTOR_AIR_477E0_CONSTRAINED ||
       selector == (uint8_t)MSL_COLL_SELECTOR_GROUND_B108_CONSTRAINED) &&
      batch->state.grab_constraint_x2226_b2[idx] != 0u) {
    return 0u;
  }
  return 1u;
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

uint8_t grab_attachment_update_capture_phys_fighter(MslBatch* batch, int bi, int victim_p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || victim_p < 0 ||
      victim_p >= (int)batch->config.num_players) {
    return 0u;
  }
  const size_t vidx = msl_idx_player(bi, victim_p);
  if (!msl_action_is_capture_pulled_wait_damage_victim(batch->state.action_id[vidx])) {
    return 0u;
  }
  const uint8_t owner_p = batch->state.grab_owner_port[vidx];
  if (owner_p >= batch->config.num_players || owner_p == (uint8_t)victim_p) {
    return 1u;
  }
  const size_t oidx = msl_idx_player(bi, (int)owner_p);
  // Fighter_procUpdate runs for each fighter in GObj order. Sampling the linked owner here—not in
  // a later batch-wide pass—preserves whether an earlier/later owner has already completed its
  // own priority-4 physics update.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  if (batch->state.hitlag_started_frame[vidx] == 0u &&
      batch->state.hitlag_started_frame[oidx] == 0u) {
    const float pre_victim_y = batch->state.pos_y[vidx];
    grab_attachment_apply_capture_delta_now(batch, bi, victim_p, (int)owner_p);
    capture_low_try_enter_high_after_delta(batch, bi, victim_p, (int)owner_p,
                                           batch->state.pos_y[vidx] - pre_victim_y);
  }
  return 1u;
}

void grab_attachment_update_thrown_accessory_phase(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int victim_p = 0; victim_p < players; victim_p++) {
      const size_t vidx = msl_idx_player(bi, victim_p);
      if (!msl_action_is_thrown_victim(batch->state.action_id[vidx]) ||
          batch->state.hitlag_started_frame[vidx] != 0u) {
        continue;
      }
      const uint8_t owner_p = batch->state.grab_owner_port[vidx];
      if (owner_p >= (uint8_t)players || owner_p == (uint8_t)victim_p) {
        continue;
      }
      // Fighter_CallAcessoryCallbacks_8006C624 priority 8 runs after every fighter's priority-6
      // map callback. Throw entry installs accessory1 immediately; all four Thrown variants run
      // the same absolute live-JObj placement in their first eligible accessory phase.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_Create,Fighter_CallAcessoryCallbacks_8006C624}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
      grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, (int)owner_p);
    }
  }
}

void grab_attachment_update_falcon_dive_accessory_phase(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int victim_p = 0; victim_p < num_players; victim_p++) {
      const size_t vidx = msl_idx_player(bi, victim_p);
      const uint8_t owner_p = batch->state.grab_owner_port[vidx];
      if (owner_p == 0xFFu || owner_p >= (uint8_t)num_players || owner_p == (uint8_t)victim_p ||
          batch->state.action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_CAPTAIN) {
        continue;
      }
      const size_t oidx = msl_idx_player(bi, (int)owner_p);
      if (batch->state.action_id[oidx] != (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH ||
          batch->state.attached_victim_port[oidx] != (uint8_t)victim_p) {
        continue;
      }
      if (batch->state.falcon_specialhi_x221b_b7[oidx] == 0u) {
        // accessory1 belongs to CaptureCaptain and observes the same recursively propagated
        // owner/victim x2219_b5 freeze as its animation owner.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB464
        if (!anim_timebase_effective_hitlag_frozen(batch, bi, victim_p)) {
          grab_attachment_apply_thrown_anchor_now(batch, bi, victim_p, (int)owner_p);
        }
      } else {
        // accessory4 belongs to Falcon and freezes with the owner's hitlag gate.
        // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialLw_800E550C
        if (!anim_timebase_effective_hitlag_frozen(batch, bi, (int)owner_p)) {
          batch->state.pos_x[oidx] = batch->state.pos_x[vidx];
          batch->state.pos_y[oidx] = batch->state.pos_y[vidx];
        }
      }
    }
  }
}
