#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

typedef struct MslFighterPoseFrame {
  float model_scale;
  float facing;
  float root[3];
  float constrained_root[3];
  float transn[3];
  float constrained_xrotn_origin[3];
  float xrotn_pivot[3];
  float xrotn_axis[3];
  float xrotn_cos;
  float xrotn_sin;
  uint8_t transn_valid;
  uint8_t constrained_root_valid;
  uint8_t constrained_xrotn;
  uint8_t xrotn_valid;
} MslFighterPoseFrame;

typedef struct MslFighterCollisionPose {
  uint16_t action_id;
  uint16_t msid;
  float anim_frame;
  float facing;
} MslFighterCollisionPose;

// Publish/read the persistent JObj pose consumed by later collision phases. Ordinary animation
// interpretation replaces it; hitlag and SkipAnim motion entries leave it untouched.
// refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ChangeMotionState}
// refs/melee/src/melee/ft/ftanim.c::{ftAnim_8006E9B4,ftAnim_8006EBA4}
void fighter_pose_publish_animation_phase(MslBatch* batch);
void fighter_pose_publish_animation_phase_fighter(MslBatch* batch, int bi, int p);
void fighter_pose_publish_motion_entry(MslBatch* batch, size_t fighter_idx,
                                       uint32_t transition_flags);
uint8_t fighter_pose_collision_pose(const MslBatch* batch, size_t fighter_idx,
                                    MslFighterCollisionPose* out);

int fighter_pose_frame_init(const MslBatch* batch, size_t fighter_idx, uint16_t msid,
                            float anim_frame, MslFighterPoseFrame* out);

// Sample one attachment point from the live fighter JObj pose, including the source-owned XRotN
// mutations used by Firefox/Firebird and DamageFlyRoll. The result is model-local, scaled space;
// callers apply the fighter root facing and world translation.
// refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
// refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFox_SpecialHi_RotateModel
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{doFlyRoll,ftCo_DamageFlyRoll_Phys}
int fighter_pose_attachment_local(const MslBatch* batch, size_t fighter_idx, uint16_t msid,
                                  float anim_frame, uint16_t part_id, const float offset[3],
                                  float out_xyz[3]);

// Sample both endpoints of one capsule from one shared bone matrix. `out_matrix` may be NULL;
// hurtbox collision passes use it to retain the exact matrix consumed by the narrowphase.
int fighter_pose_attachment_pair_local(const MslBatch* batch, size_t fighter_idx, uint16_t msid,
                                       float anim_frame, uint16_t part_id, const float a_offset[3],
                                       const float b_offset[3], float out_a[3], float out_b[3],
                                       float out_matrix[12]);

int fighter_pose_attachment_pair_from_matrix_local(const MslBatch* batch, size_t fighter_idx,
                                                   uint16_t msid, float anim_frame,
                                                   uint16_t part_id, const float matrix[12],
                                                   const float a_offset[3], const float b_offset[3],
                                                   float out_a[3], float out_b[3]);

int fighter_pose_attachment_pair_from_matrix_frame_local(const MslBatch* batch, size_t fighter_idx,
                                                         uint16_t part_id, const float matrix[12],
                                                         const MslFighterPoseFrame* frame,
                                                         const float a_offset[3],
                                                         const float b_offset[3], float out_a[3],
                                                         float out_b[3]);

// Compose one sampled bone matrix through the fighter's live outer JObj transforms and root into
// world space. This is the matrix passed to lbColl_80006E58 for scaled HurtCapsule radii.
// refs/melee/src/melee/lb/lbcollision.c::{lbColl_80006E58,lbColl_8000805C}
int fighter_pose_matrix_world_from_frame(const MslBatch* batch, size_t fighter_idx,
                                         uint16_t part_id, const float matrix[12],
                                         const MslFighterPoseFrame* frame, float out_world[12]);

float fighter_pose_model_scale(const MslBatch* batch, size_t fighter_idx);

// World root consumed by a posed part. Attached Thrown*/CaptureCaptain constrain FtPart_XRotN to
// the captor while `cur_pos` retains the x1A70 accessory offset; descendants therefore use the
// constrained joint origin rather than the public fighter root.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800DB368,ftCo_800DB464}
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
void fighter_pose_part_world_origin(const MslBatch* batch, size_t fighter_idx, uint16_t part_id,
                                    float* out_x, float* out_y, float* out_z);
