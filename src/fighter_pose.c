#include "fighter_pose.h"

#include <math.h>
#include <string.h>

#include "action_ids.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "anim_table.h"
#include "char_params.h"
#include "mtx34.h"
#include "specialhi_pose.h"

static uint8_t fighter_pose_publish_current(MslBatch* batch, size_t idx) {
  const uint32_t anim = batch->state.animation_index[idx];
  if (anim > (uint32_t)UINT16_MAX) {
    return 0u;
  }
  // HSD_AObjInterpretAnim has just rewritten the ordinary JObj tree. Persistent local-SRT state
  // only survives source paths that skip animation interpretation (Guard/GuardOn/GuardReflect).
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  anim_pose_live_discard(batch, idx);
  batch->state.collision_pose_action_id[idx] = batch->state.action_id[idx];
  batch->state.collision_pose_msid[idx] = (uint16_t)anim;
  batch->state.collision_pose_anim_frame[idx] = batch->state.anim_frame_f32[idx];
  batch->state.collision_pose_facing[idx] = batch->state.facing[idx] ? 1u : 0u;
  batch->state.collision_pose_valid[idx] = 1u;
  return 1u;
}

void fighter_pose_publish_animation_phase_fighter(MslBatch* batch, int bi, int p) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || p < 0 ||
      p >= (int)batch->config.num_players) {
    return;
  }
  const size_t idx = msl_idx_player(bi, p);
  // Fighter_8006A360 skips ftAnim_8006EBA4 during hitlag; the previous JObj matrices remain
  // the collision authority even though timers or callbacks can publish other fighter state.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  if (anim_timebase_effective_hitlag_frozen(batch, bi, p)) {
    return;
  }
  (void)fighter_pose_publish_current(batch, idx);
}

void fighter_pose_publish_animation_phase(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < players; p++) {
      fighter_pose_publish_animation_phase_fighter(batch, bi, p);
    }
  }
}

void fighter_pose_publish_motion_entry(MslBatch* batch, size_t idx, uint32_t transition_flags) {
  if (batch == NULL) {
    return;
  }
  // Fighter_ChangeMotionState runs ftCo_8009E7B4 before ftData_80085CD8 interprets the requested
  // AObj. A disabled->enabled chain therefore snapshots the preceding live pose, clears angular
  // carry, and only then lets the new motion own the ordinary animated JObjs. SkipAnim suppresses
  // interpretation but does not suppress this dynamics ownership transition.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009CB40,ftCo_8009E7B4}
  anim_pose_sync_dynamic_ownership(batch, idx);
  if ((transition_flags & (uint32_t)MSL_MOTION_FLAG_SKIP_ANIM) != 0u) {
    return;
  }
  // A later same-frame map or combat pass sees the new pose even when the ordinary priority-1
  // animation pass already ran.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006E9B4
  (void)fighter_pose_publish_current(batch, idx);
}

uint8_t fighter_pose_collision_pose(const MslBatch* batch, size_t idx,
                                    MslFighterCollisionPose* out) {
  if (batch == NULL || out == NULL) {
    return 0u;
  }
  if (batch->state.collision_pose_valid[idx] != 0u) {
    out->action_id = batch->state.collision_pose_action_id[idx];
    out->msid = batch->state.collision_pose_msid[idx];
    out->anim_frame = batch->state.collision_pose_anim_frame[idx];
    out->facing = batch->state.collision_pose_facing[idx] ? 1.0f : -1.0f;
    return 1u;
  }
  const uint32_t anim = batch->state.animation_index[idx];
  uint16_t msid = 0u;
  if (anim <= (uint32_t)UINT16_MAX) {
    msid = (uint16_t)anim;
  } else {
    msid = msl_motion_state_submotion_id(batch->state.char_id[idx], batch->state.action_id[idx]);
    if (msid == UINT16_MAX) {
      return 0u;
    }
  }
  out->action_id = batch->state.action_id[idx];
  out->msid = msid;
  out->anim_frame = batch->state.anim_frame_f32[idx];
  out->facing = batch->state.facing[idx] ? 1.0f : -1.0f;
  return 1u;
}

float fighter_pose_model_scale(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 1.0f;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  const float char_scale = ch != NULL && isfinite(ch->model_scaling) && ch->model_scaling > 0.0f
                               ? ch->model_scaling
                               : 1.0f;
  return batch->state.fighter_scale_y[idx] * char_scale;
}

void fighter_pose_part_world_origin(const MslBatch* batch, size_t idx, uint16_t part_id,
                                    float* out_x, float* out_y, float* out_z) {
  if (out_x == NULL || out_y == NULL || out_z == NULL) {
    return;
  }
  *out_x = batch != NULL ? batch->state.pos_x[idx] : 0.0f;
  *out_y = batch != NULL ? batch->state.pos_y[idx] : 0.0f;
  *out_z = batch != NULL ? batch->state.pos_z[idx] : 0.0f;
  if (batch == NULL || batch->state.grab_constraint_x2226_b2[idx] == 0u ||
      batch->state.grab_owner_port[idx] >= batch->config.num_players ||
      msl_anim_part_under_xrotn(batch->state.char_id[idx], part_id) == 0u) {
    return;
  }

  // ftCo_800DB368 constrains the victim's XRotN translation to the captor's TransN2. The later
  // accessory callback publishes `cur_pos = constrained_xrotn + x1A70`; collision still walks the
  // constrained JObj tree. `grab_offset_{y,z}` is the explicit x1A70 state retained by the
  // attachment owner, so remove it once when converting descendant matrices to world space.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800DB368,ftCo_800DB464}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE3FC,ftCo_800DE508}
  const float scale = batch->state.fighter_scale_y[idx];
  const float facing = batch->state.facing[idx] ? 1.0f : -1.0f;
  *out_x -= facing * batch->state.grab_offset_z[idx] * scale;
  *out_y -= batch->state.grab_offset_y[idx] * scale;
}

static uint8_t fighter_pose_xrotn_angle(const MslBatch* batch, size_t idx, float* out_angle) {
  const uint16_t pose_action = batch->state.collision_pose_valid[idx]
                                   ? batch->state.collision_pose_action_id[idx]
                                   : batch->state.action_id[idx];
  if (msl_specialhi_rotate_model_action(batch->state.char_id[idx], pose_action)) {
    float rotate_model = 0.0f;
    if (msl_specialhi_rotate_model_get_or_velocity(batch, idx, &rotate_model)) {
      *out_angle = msl_specialhi_xrotn_angle_from_rotate_model(rotate_model);
      return 1u;
    }
  }
  if (pose_action == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
    const float vx = batch->state.speed_air_x_self[idx] + batch->state.speed_x_attack[idx];
    const float vy = batch->state.speed_y_self[idx] + batch->state.speed_y_attack[idx];
    if (isfinite(vx) && isfinite(vy) && (vx != 0.0f || vy != 0.0f)) {
      const float facing = batch->state.facing[idx] ? 1.0f : -1.0f;
      *out_angle = facing * atan2f(vx, vy);
      return 1u;
    }
  }
  return 0u;
}

int fighter_pose_frame_init(const MslBatch* batch, size_t idx, uint16_t msid, float anim_frame,
                            MslFighterPoseFrame* out) {
  if (batch == NULL || out == NULL) {
    return -1;
  }
  memset(out, 0, sizeof(*out));
  MslFighterCollisionPose pose;
  if (!fighter_pose_collision_pose(batch, idx, &pose)) {
    return -1;
  }
  out->model_scale = fighter_pose_model_scale(batch, idx);
  if (!(out->model_scale > 0.0f)) {
    return -1;
  }
  out->facing = pose.facing;
  out->root[0] = batch->state.pos_x[idx];
  out->root[1] = batch->state.pos_y[idx];
  out->root[2] = batch->state.pos_z[idx];
  memcpy(out->constrained_root, out->root, sizeof(out->root));
  if (batch->state.grab_constraint_x2226_b2[idx] != 0u &&
      batch->state.grab_owner_port[idx] < batch->config.num_players) {
    const float live_facing = batch->state.facing[idx] ? 1.0f : -1.0f;
    out->constrained_root[0] -=
        live_facing * batch->state.grab_offset_z[idx] * batch->state.fighter_scale_y[idx];
    out->constrained_root[1] -= batch->state.grab_offset_y[idx] * batch->state.fighter_scale_y[idx];
    out->constrained_root_valid = 1u;
  }

  if (msl_anim_uses_root_motion(batch->state.char_id[idx], msid) == 0u &&
      anim_pose_get_transn_f32(batch->state.char_id[idx], msid, anim_frame, out->transn) == 0) {
    for (uint8_t axis = 0u; axis < 3u; axis++) {
      out->transn[axis] *= out->model_scale;
    }
    out->transn_valid = 1u;
  }

  if (batch->state.grab_constraint_x2226_b2[idx] != 0u &&
      batch->state.grab_owner_port[idx] < batch->config.num_players) {
    float xrotn[12];
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    if (anim_pose_get_collision_matrix_f32(batch, idx, msid, anim_frame, 2u, xrotn) == 0) {
      msl_mtx34_mul_point(xrotn, origin, &out->constrained_xrotn_origin[0],
                          &out->constrained_xrotn_origin[1], &out->constrained_xrotn_origin[2]);
      out->constrained_xrotn = 1u;
    }
  }

  float angle = 0.0f;
  if (!fighter_pose_xrotn_angle(batch, idx, &angle)) {
    return 0;
  }
  const uint16_t pose_action = batch->state.collision_pose_valid[idx]
                                   ? batch->state.collision_pose_action_id[idx]
                                   : batch->state.action_id[idx];
  const float pivot_frame = pose_action == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL ? 0.0f : anim_frame;
  float matrix[12];
  if (anim_pose_get_collision_matrix_f32(batch, idx, msid, pivot_frame, 2u, matrix) != 0) {
    return 0;
  }
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float unit_x[3] = {1.0f, 0.0f, 0.0f};
  float axis_end[3];
  msl_mtx34_mul_point(matrix, origin, &out->xrotn_pivot[0], &out->xrotn_pivot[1],
                      &out->xrotn_pivot[2]);
  msl_mtx34_mul_point(matrix, unit_x, &axis_end[0], &axis_end[1], &axis_end[2]);
  float axis_len_sq = 0.0f;
  for (uint8_t axis = 0u; axis < 3u; axis++) {
    out->xrotn_pivot[axis] *= out->model_scale;
    out->xrotn_axis[axis] = axis_end[axis] * out->model_scale - out->xrotn_pivot[axis];
    axis_len_sq += out->xrotn_axis[axis] * out->xrotn_axis[axis];
  }
  const float axis_len = sqrtf(axis_len_sq);
  if (!(axis_len > 0.0f)) {
    return 0;
  }
  for (uint8_t axis = 0u; axis < 3u; axis++) {
    out->xrotn_axis[axis] /= axis_len;
  }
  out->xrotn_cos = cosf(angle);
  out->xrotn_sin = sinf(angle);
  out->xrotn_valid = 1u;
  return 0;
}

static void fighter_pose_frame_apply_point(const MslFighterPoseFrame* frame, uint8_t under_xrotn,
                                           float point[3]) {
  if (under_xrotn != 0u && frame->constrained_xrotn != 0u) {
    // ftCo_800DB368 replaces the victim XRotN transform with the captor's TransN2 constraint after
    // saving the original local translation in x2174. Descendant matrices extracted from the
    // victim animation still contain that authored XRotN ancestor, so reduce them to XRotN-local
    // coordinates before attaching them to the constrained world origin. Applying only the new
    // root while retaining the old translation double-counts the ancestor.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800DB368
    for (uint8_t axis = 0u; axis < 3u; axis++) {
      point[axis] -= frame->constrained_xrotn_origin[axis];
    }
  }
  for (uint8_t axis = 0u; axis < 3u; axis++) {
    point[axis] *= frame->model_scale;
  }
  if (under_xrotn != 0u && frame->constrained_xrotn == 0u && frame->xrotn_valid != 0u) {
    const float p[3] = {point[0] - frame->xrotn_pivot[0], point[1] - frame->xrotn_pivot[1],
                        point[2] - frame->xrotn_pivot[2]};
    const float dot =
        frame->xrotn_axis[0] * p[0] + frame->xrotn_axis[1] * p[1] + frame->xrotn_axis[2] * p[2];
    const float cross[3] = {
        frame->xrotn_axis[1] * p[2] - frame->xrotn_axis[2] * p[1],
        frame->xrotn_axis[2] * p[0] - frame->xrotn_axis[0] * p[2],
        frame->xrotn_axis[0] * p[1] - frame->xrotn_axis[1] * p[0],
    };
    for (uint8_t axis = 0u; axis < 3u; axis++) {
      point[axis] = frame->xrotn_pivot[axis] + p[axis] * frame->xrotn_cos +
                    cross[axis] * frame->xrotn_sin +
                    frame->xrotn_axis[axis] * dot * (1.0f - frame->xrotn_cos);
    }
  }
  if (under_xrotn != 0u && frame->constrained_xrotn == 0u && frame->transn_valid != 0u) {
    for (uint8_t axis = 0u; axis < 3u; axis++) {
      point[axis] += frame->transn[axis];
    }
  }
}

static void fighter_pose_local_to_world(const MslFighterPoseFrame* frame, uint8_t under_xrotn,
                                        float point[3]) {
  const float x = point[0];
  const float y = point[1];
  const float z = point[2];
  const float* root = under_xrotn != 0u && frame->constrained_root_valid != 0u
                          ? frame->constrained_root
                          : frame->root;
  point[0] = root[0] + frame->facing * z;
  point[1] = root[1] + y;
  point[2] = root[2] - frame->facing * x;
}

int fighter_pose_matrix_world_from_frame(const MslBatch* batch, size_t idx, uint16_t part_id,
                                         const float matrix[12], const MslFighterPoseFrame* frame,
                                         float out_world[12]) {
  if (batch == NULL || matrix == NULL || frame == NULL || out_world == NULL) {
    return -1;
  }
  const uint8_t under_xrotn = msl_anim_part_under_xrotn(batch->state.char_id[idx], part_id);
  if (frame->constrained_xrotn == 0u && frame->xrotn_valid == 0u) {
    // The common JObj path is only model scale, optional TransN, then the fighter root/facing
    // basis change. Apply that affine transform directly instead of mapping four points and
    // subtracting their origins back into a matrix. This is the same HSD_JObjSetupMatrix result;
    // the expanded path below remains for constrained or action-rotated XRotN owners.
    // refs/melee/src/melee/ft/ftparts.c::ftParts_8007462C
    // refs/melee/src/sysdolphin/baselib/jobj.c::HSD_JObjSetupMatrix
    const float scale = frame->model_scale;
    const float facing = frame->facing;
    out_world[0] = facing * scale * matrix[8];
    out_world[1] = facing * scale * matrix[9];
    out_world[2] = facing * scale * matrix[10];
    out_world[4] = scale * matrix[4];
    out_world[5] = scale * matrix[5];
    out_world[6] = scale * matrix[6];
    out_world[8] = -facing * scale * matrix[0];
    out_world[9] = -facing * scale * matrix[1];
    out_world[10] = -facing * scale * matrix[2];

    float local_x = scale * matrix[3];
    float local_y = scale * matrix[7];
    float local_z = scale * matrix[11];
    if (under_xrotn != 0u && frame->transn_valid != 0u) {
      local_x += frame->transn[0];
      local_y += frame->transn[1];
      local_z += frame->transn[2];
    }
    const float* root = under_xrotn != 0u && frame->constrained_root_valid != 0u
                            ? frame->constrained_root
                            : frame->root;
    out_world[3] = root[0] + facing * local_z;
    out_world[7] = root[1] + local_y;
    out_world[11] = root[2] - facing * local_x;
    return 0;
  }
  const float basis[4][3] = {
      {0.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f},
      {0.0f, 1.0f, 0.0f},
      {0.0f, 0.0f, 1.0f},
  };
  float world[4][3];
  for (uint8_t i = 0u; i < 4u; i++) {
    msl_mtx34_mul_point(matrix, basis[i], &world[i][0], &world[i][1], &world[i][2]);
    fighter_pose_frame_apply_point(frame, under_xrotn, world[i]);
    fighter_pose_local_to_world(frame, under_xrotn, world[i]);
  }
  for (uint8_t row = 0u; row < 3u; row++) {
    out_world[(size_t)row * 4u] = world[1][row] - world[0][row];
    out_world[(size_t)row * 4u + 1u] = world[2][row] - world[0][row];
    out_world[(size_t)row * 4u + 2u] = world[3][row] - world[0][row];
    out_world[(size_t)row * 4u + 3u] = world[0][row];
  }
  return 0;
}

int fighter_pose_attachment_pair_from_matrix_frame_local(const MslBatch* batch, size_t idx,
                                                         uint16_t part_id, const float matrix[12],
                                                         const MslFighterPoseFrame* frame,
                                                         const float a_offset[3],
                                                         const float b_offset[3], float out_a[3],
                                                         float out_b[3]) {
  if (batch == NULL || matrix == NULL || frame == NULL || a_offset == NULL || b_offset == NULL ||
      out_a == NULL || out_b == NULL) {
    return -1;
  }
  msl_mtx34_mul_point(matrix, a_offset, &out_a[0], &out_a[1], &out_a[2]);
  msl_mtx34_mul_point(matrix, b_offset, &out_b[0], &out_b[1], &out_b[2]);
  const uint8_t under_xrotn = msl_anim_part_under_xrotn(batch->state.char_id[idx], part_id);
  fighter_pose_frame_apply_point(frame, under_xrotn, out_a);
  fighter_pose_frame_apply_point(frame, under_xrotn, out_b);
  return 0;
}

int fighter_pose_attachment_local(const MslBatch* batch, size_t idx, uint16_t msid,
                                  float anim_frame, uint16_t part_id, const float offset[3],
                                  float out[3]) {
  if (batch == NULL || offset == NULL || out == NULL) {
    return -1;
  }
  float matrix[12];
  MslFighterPoseFrame frame;
  if (anim_pose_get_collision_matrix_f32(batch, idx, msid, anim_frame, part_id, matrix) != 0 ||
      fighter_pose_frame_init(batch, idx, msid, anim_frame, &frame) != 0) {
    return -1;
  }
  msl_mtx34_mul_point(matrix, offset, &out[0], &out[1], &out[2]);
  fighter_pose_frame_apply_point(
      &frame, msl_anim_part_under_xrotn(batch->state.char_id[idx], part_id), out);
  return 0;
}

int fighter_pose_attachment_pair_local(const MslBatch* batch, size_t idx, uint16_t msid,
                                       float anim_frame, uint16_t part_id, const float a_offset[3],
                                       const float b_offset[3], float out_a[3], float out_b[3],
                                       float out_matrix[12]) {
  if (batch == NULL || a_offset == NULL || b_offset == NULL || out_a == NULL || out_b == NULL) {
    return -1;
  }
  float matrix[12];
  MslFighterPoseFrame frame;
  if (anim_pose_get_collision_matrix_f32(batch, idx, msid, anim_frame, part_id, matrix) != 0 ||
      fighter_pose_frame_init(batch, idx, msid, anim_frame, &frame) != 0) {
    return -1;
  }
  if (out_matrix != NULL) {
    memcpy(out_matrix, matrix, sizeof(matrix));
  }
  return fighter_pose_attachment_pair_from_matrix_frame_local(batch, idx, part_id, matrix, &frame,
                                                              a_offset, b_offset, out_a, out_b);
}

int fighter_pose_attachment_pair_from_matrix_local(const MslBatch* batch, size_t idx, uint16_t msid,
                                                   float anim_frame, uint16_t part_id,
                                                   const float matrix[12], const float a_offset[3],
                                                   const float b_offset[3], float out_a[3],
                                                   float out_b[3]) {
  MslFighterPoseFrame frame;
  if (fighter_pose_frame_init(batch, idx, msid, anim_frame, &frame) != 0) {
    return -1;
  }
  return fighter_pose_attachment_pair_from_matrix_frame_local(batch, idx, part_id, matrix, &frame,
                                                              a_offset, b_offset, out_a, out_b);
}
