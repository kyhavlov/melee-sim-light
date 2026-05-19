#include "mpcoll_wall_ceil.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "match_flow.h"
#include "mpcoll_ecb_points.h"
#include "motion_state_owners.h"
#include "msl_math.h"
#include "mtx34.h"
#include "specialhi_pose.h"
#include "stage_collision.h"
#include "throw_flow.h"

// Decomp constants / shapes:
// - mpCheckCeiling treats ceilings as horizontal when |y0 - y1| <= 0.0001.
//   refs/melee/src/melee/mp/mplib.c::mpCheckCeiling
// - mpCheck{Left,Right}Wall treats walls as vertical when |x0 - x1| <= 0.0001.
//   refs/melee/src/melee/mp/mplib.c::mpCheckLeftWall
//   refs/melee/src/melee/mp/mplib.c::mpCheckRightWall
// - mpLineIntersection{H,V} clamps small off-end travel within ±0.1 before returning false.
//   refs/melee/src/melee/mp/mplib.c::mpLineIntersectionH
//   refs/melee/src/melee/mp/mplib.c::mpLineIntersectionV
// - mpLib_8004E090_Ceiling applies a -0.0001 bias to keep the point infinitesimally below the ceiling.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
// - mpLib_8004E398_LeftWall / mpLib_8004E684_RightWall use ±0.1 seam clamping when traversing prev/next.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall
// - mpLib_8004ED5C extends connected endpoints by 1 unit, guarded by a 0.001 distance threshold.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
static const float k_line_axis_thresh = 0.0001f;
static const float k_line_end_clamp = 0.1f;
static const float k_ceiling_y_bias = 0.0001f;
static const float k_ed5c_min_dist = 0.001f;
static const float k_ed5c_extend = 1.0f;

enum {
  MSL_WALL_NONE = 0,
  MSL_WALL_LEFT = 1,   // mplib CollLine_LeftWall
  MSL_WALL_RIGHT = 2,  // mplib CollLine_RightWall
};

enum { MSL_WALL_CANDIDATE_MAX = 9 };

enum {
  // Slippi/GALE01 character ids for the currently extracted SpecialHi collision ECB domain.
  MSL_MPCOLL_CHAR_FOX = 1,
  MSL_MPCOLL_CHAR_FALCO = 22,
};

enum { MSL_STAGE_YOSHIS_STORY_LOCAL = 8u };

// Wall env-flag split mirrors mpColl: every wall hit sets Push, but only the ECB side-point
// branch sets Hug. ftWallJump_8008169C and PassiveWall entry intentionally test the Hug bit, so
// bottom/top fallback contacts must not promote to walljump/tech authority.
// refs/melee/src/melee/mp/mpcoll.c::{
//   mpColl_80044E10_RightWall,mpColl_80045B74_LeftWall,mpColl_80048AB0_RightWall}
// refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
static inline void mark_left_wall_contact(MslBatch* batch, size_t idx, uint8_t hug) {
  batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_LEFT_WALL_PUSH;
  if (hug) {
    batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG;
  }
}

static inline void mark_right_wall_contact(MslBatch* batch, size_t idx, uint8_t hug) {
  batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_RIGHT_WALL_PUSH;
  if (hug) {
    batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG;
  }
}

static inline float min4f(float a, float b, float c, float d) {
  float m = (a < b) ? a : b;
  m = (c < m) ? c : m;
  return (d < m) ? d : m;
}

static inline float max4f(float a, float b, float c, float d) {
  float m = (a > b) ? a : b;
  m = (c > m) ? c : m;
  return (d > m) ? d : m;
}

static inline uint8_t wall_line_outside_box(const MslStageWallLine* l, float min_x, float max_x,
                                            float min_y, float max_y) {
  if (l->max_x < min_x || l->min_x > max_x) {
    return 1u;
  }
  return (uint8_t)(l->max_y < min_y || l->min_y > max_y);
}

static int grounded_right_wall_floor_adjacent_line_idx(const MslStageFloorGraph* fg,
                                                       const MslStageWallGraph* rwg,
                                                       uint32_t stage_id, uint16_t ground_id) {
  // Grounded right-wall checks exclude the floor-chain wall owners that are connected to the
  // current floor. Decomp resolves these through mpLib_80053394_Floor / mpLib_800536CC_Floor
  // before mpColl_80048AB0_RightWall / mpColl_800491C8_RightWall admit bottom-point wall hits.
  // refs/melee/src/melee/mp/mplib.c::{mpLib_80053394_Floor,mpLib_800536CC_Floor}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80048AB0_RightWall,mpColl_800491C8_RightWall}
  if (fg == NULL || rwg == NULL || fg->lines == NULL || rwg->lines == NULL) {
    return -1;
  }
  int floor_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (floor_line_idx < 0 || (size_t)floor_line_idx >= fg->line_count) {
    return -1;
  }
  (void)rwg;
  return fg->lines[(size_t)floor_line_idx].adjacent_right_wall;
}

static int grounded_left_wall_floor_adjacent_line_idx(const MslStageFloorGraph* fg,
                                                      const MslStageWallGraph* lwg,
                                                      uint32_t stage_id, uint16_t ground_id) {
  // Symmetric grounded floor-chain exclusion for left-wall checks.
  // refs/melee/src/melee/mp/mplib.c::{mpLib_80053448_Floor,mpLib_800534FC_Floor}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80049778_LeftWall,mpColl_80049EAC_LeftWall}
  if (fg == NULL || lwg == NULL || fg->lines == NULL || lwg->lines == NULL) {
    return -1;
  }
  int floor_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  if (floor_line_idx < 0 || (size_t)floor_line_idx >= fg->line_count) {
    return -1;
  }
  (void)lwg;
  return fg->lines[(size_t)floor_line_idx].adjacent_left_wall;
}

static inline float cross2(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

static inline uint8_t specialhi_launch_uses_runtime_xrotn_ecb(uint8_t char_id, uint16_t action_id) {
  if (char_id != (uint8_t)MSL_MPCOLL_CHAR_FOX && char_id != (uint8_t)MSL_MPCOLL_CHAR_FALCO) {
    return 0u;
  }
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_HI:
    case MSL_ACT_FX_SPECIAL_AIR_HI:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t mpcoll_damagefly_wall_asdi_latch_action(uint16_t action_id) {
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_DAMAGE_FLY_COLL);
}

static inline uint8_t mpcoll_wall_asdi_producer_action(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI ||
          mpcoll_damagefly_wall_asdi_latch_action(action_id))
             ? 1u
             : 0u;
}

static inline uint8_t mpcoll_active_hitlag_phase(const MslBatch* batch, size_t idx) {
  return (batch != NULL &&
          (batch->state.hitlag_pre_timer[idx] != 0u || batch->state.hitlag[idx] != 0u ||
           batch->state.hitlag_started_frame[idx] != 0u))
             ? 1u
             : 0u;
}

static inline uint8_t mpcoll_frozen_hitlag_phase(const MslBatch* batch, size_t idx) {
  return (batch != NULL &&
          (batch->state.hitlag[idx] != 0u || batch->state.hitlag_started_frame[idx] != 0u))
             ? 1u
             : 0u;
}

static inline void mpcoll_clear_wall_ceiling_contacts(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.wall_kind[idx] = 0;
  batch->state.damage_hitlag_wall_asdi_latch[idx] = 0u;
  batch->state.ceiling_contact_x[idx] = 0.0f;
  batch->state.ceiling_contact_y[idx] = 0.0f;
  batch->state.ceiling_normal_x[idx] = 0.0f;
  batch->state.ceiling_normal_y[idx] = 0.0f;
  batch->state.wall_contact_x[idx] = 0.0f;
  batch->state.wall_contact_y[idx] = 0.0f;
  batch->state.wall_normal_x[idx] = 0.0f;
  batch->state.wall_normal_y[idx] = 0.0f;
}

static inline void mpcoll_clear_wall_ceiling_provenance(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  mpcoll_clear_wall_ceiling_contacts(batch, idx);
  batch->state.wall_id[idx] = 0xFFFFu;
  batch->state.ceiling_id[idx] = 0xFFFFu;
}

static inline void mpcoll_clear_wall_ceiling_and_env_provenance(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  mpcoll_clear_wall_ceiling_provenance(batch, idx);
  batch->state.coll_env_flags[idx] = 0u;
  batch->state.coll_prev_env_flags[idx] = 0u;
}

static inline uint8_t mpcoll_action_uses_common_air_walljump_callback(uint16_t action_id) {
  // These common-air Coll callbacks route through ft_081B helpers that call
  // ftWallJump_8008169C after floor collision declines. If the side-point projection recovery
  // observes the current side point inside a wall, the recovered contact owns WallHug for this
  // callback family; DamageFly and other wall consumers remain Push-only unless their own side
  // branch or seed lane proves Hug.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
  // refs/melee/src/melee/ft/ftwalljump.c::ftWallJump_8008169C
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_COMMON_AIR_WALLJUMP_COLL);
}

static inline uint8_t mpcoll_action_uses_ft80081d0c_air_collision(uint16_t action_id) {
  // `ft_80082C74` delegates through `ft_80081D0C`, which loads the normal airborne ECB and calls
  // `mpColl_800471F8`; that source path runs the full `mpColl_80046904` airborne wall envelope on
  // both sides, but unlike common Jump/Fall callbacks it does not immediately call the walljump or
  // cliff post-consumers.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80046904}
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_FT80081D0C_AIR_COLL);
}

static inline uint8_t mpcoll_action_uses_ft_check_ground_ledge_air_collision(uint8_t char_id,
                                                                             uint16_t action_id) {
  // Fox/Falco aerial Side-B Start/Main/End and SpecialHiFall call `ft_CheckGroundAndLedge`, which
  // loads the airborne ECB and calls `mpColl_800473CC` or `mpColl_800471F8`. Both routes run the
  // full `mpColl_80046904` airborne wall envelope before floor/ledge consumers. This is the same
  // wall source owner as the ft_80081D0C airborne collision path, not a common-air walljump
  // callback; these actions receive Push/Hug provenance from mpColl but have no same-callback
  // ftWallJump_8008169C consumer. The generated class also marks common `ft_80082F28` wrappers
  // such as MissFoot/Pass, but those are not retained by this wall-envelope slice.
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class_bits)
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialAirSStart_Coll,ftFx_SpecialAirS_Coll,ftFx_SpecialAirSEnd_Coll}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_MissFoot.c::ftCo_MissFoot_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_Pass_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_800473CC,mpColl_800471F8,mpColl_80046904}
  if (msl_motion_state_class_has(char_id, action_id, MSL_MS_CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL) ==
      0u) {
    return 0u;
  }
  const uint16_t smid = msl_motion_state_submotion_id(char_id, action_id);
  return (uint8_t)(smid >= (uint16_t)MSL_SM_FX_SPECIAL_AIR_S_START &&
                   smid <= (uint16_t)MSL_SM_FX_SPECIAL_HI_FALL);
}

static inline void ecb_update_rot_bounds(float x, float y, float* io_min_x, float* io_max_x,
                                         float* io_min_y, float* io_max_y) {
  if (x < *io_min_x) {
    *io_min_x = x;
  }
  if (x > *io_max_x) {
    *io_max_x = x;
  }
  if (y < *io_min_y) {
    *io_min_y = y;
  }
  if (y > *io_max_y) {
    *io_max_y = y;
  }
}

static inline uint8_t specialhi_rotate_collision_point_xrotn(
    const MslBatch* batch, size_t idx, uint8_t char_id, uint16_t msid, uint16_t frame_u16,
    uint16_t part_id, float facing_dir, float model_scale, float* io_x, float* io_y, float* io_z) {
  if (batch == NULL || io_x == NULL || io_y == NULL || io_z == NULL ||
      !msl_anim_part_under_xrotn(char_id, part_id)) {
    return 0u;
  }
  (void)facing_dir;
  float rotate_model = 0.0f;
  if (!msl_specialhi_rotate_model_get_or_velocity(batch, idx, &rotate_model)) {
    return 0u;
  }

  float m[12];
  if (anim_pose_get_collision_matrix(batch, idx, msid, frame_u16, 2u, m) != 0) {
    return 0u;
  }
  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float local_x[3] = {1.0f, 0.0f, 0.0f};
  msl_mtx34_mul_point(m, origin, &ax0, &ay0, &az0);
  msl_mtx34_mul_point(m, local_x, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  const float angle = msl_specialhi_xrotn_angle_from_rotate_model(rotate_model);
  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

static inline uint8_t try_sample_jobj_ecb_points(MslEcbWorldPoints* out, const MslBatch* batch,
                                                 size_t idx, uint8_t char_id, uint32_t anim,
                                                 uint16_t action_id, uint16_t frame_u16,
                                                 float facing_dir, float pos_x, float pos_y) {
  if (out == NULL || batch == NULL || !(anim <= 0xFFFFu)) {
    return 0u;
  }

  const MslCharParams* ch = msl_char_params(char_id);
  if (ch == NULL || ch->ecb_joint_count == 0u) {
    return 0u;
  }
  const uint16_t* ecb_parts = ch->ecb_joints;
  const uint8_t ecb_part_count = ch->ecb_joint_count;
  const uint16_t msid = (uint16_t)anim;
  const float model_scaling =
      (ch != NULL && isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling
                                                                              : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[idx] * model_scaling;

  float min_x = 0.0f;
  float max_x = 0.0f;
  float min_y = 0.0f;
  float max_y = 0.0f;
  uint8_t have = 0u;
  for (uint16_t pi = 0; pi < ecb_part_count; pi++) {
    const uint16_t part_id = ecb_parts[pi];
    float m[12];
    if (anim_pose_get_collision_matrix(batch, idx, msid, frame_u16, part_id, m) != 0) {
      return 0u;
    }
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    float x = 0.0f, y = 0.0f, z = 0.0f;
    msl_mtx34_mul_point(m, origin, &x, &y, &z);
    x *= model_scale;
    y *= model_scale;
    z *= model_scale;
    if (specialhi_launch_uses_runtime_xrotn_ecb(char_id, action_id)) {
      (void)specialhi_rotate_collision_point_xrotn(batch, idx, char_id, msid, frame_u16, part_id,
                                                   facing_dir, model_scale, &x, &y, &z);
    }

    // Decomp: mpColl_LoadECB_JObj consumes `lb_8000B1CC` world-space x/y after fighter model
    // scale. SSANIM01 matrices are facing-independent; the fighter root Y rotation maps the
    // extracted local Z axis into stage X before mpColl reads the live JObj point. Keep that basis
    // after applying SpecialHi's XRotN launch rotation so left/right wall envelopes share the same
    // source-space ECB width.
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
    // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    // refs/melee/src/melee/ft/fighter.c (root `ftPartSetRotY(fp, 0, M_PI_2 * facing_dir)`)
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFox_SpecialHi_RotateModel
    // data/characters/{fox,falco}.json::model_scaling
    const float rel_x = facing_dir * z;
    const float rel_y = y;
    if (!have) {
      min_x = max_x = rel_x;
      min_y = max_y = rel_y;
      have = 1u;
    } else {
      ecb_update_rot_bounds(rel_x, rel_y, &min_x, &max_x, &min_y, &max_y);
    }
  }
  if (!have) {
    return 0u;
  }

  // Decomp: mpColl_LoadECB_JObj compares the JObj horizontal span with
  // `max(4.0f, ecb_source.x12C)`, where ft_80081B38 seeds x12C as `10.0f * fp->x34_scale.y`.
  // If the span is smaller, it recenters the existing sampled span around zero before the final
  // +/-2 clamp (`right_x = 0.5F * ABS(right_x - left_x)`). It does not expand the span to x12C.
  // This matters for horizontal Firefox at the right wall: vanilla projects from the centered
  // sampled half-width rather than the raw leftmost joint or an expanded x12C half-width.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081B38
  const float min_ecb_width = fmaxf(4.0f, 10.0f * batch->state.fighter_scale_y[idx]);
  const float ecb_width = fabsf(max_x - min_x);
  if (ecb_width < min_ecb_width) {
    const float half_width = 0.5f * ecb_width;
    max_x = half_width;
    min_x = -half_width;
  }

  if (max_x < 2.0f) {
    max_x = 2.0f;
  }
  if (min_x > -2.0f) {
    min_x = -2.0f;
  }
  if (min_y < 0.0f) {
    min_y = 0.0f;
  }
  const float side_offset_y = (ch != NULL) ? ch->ecb_side_y_offset : 0.0f;
  const float side_rel_y = side_offset_y + 0.5f * (min_y + max_y);

  out->left_rel_x = min_x;
  out->right_rel_x = max_x;
  out->bottom_rel_y = min_y;
  out->top_rel_y = max_y;
  out->side_rel_y = side_rel_y;
  out->frame_u16 = frame_u16;
  out->bottom_x = pos_x;
  out->bottom_y = pos_y + min_y;
  out->top_x = pos_x;
  out->top_y = pos_y + max_y;
  out->left_x = pos_x + min_x;
  out->left_y = pos_y + side_rel_y;
  out->right_x = pos_x + max_x;
  out->right_y = pos_y + side_rel_y;
  return 1u;
}

static inline void sample_collision_ecb_points(MslEcbWorldPoints* out, const MslBatch* batch,
                                               size_t idx, uint8_t char_id, uint32_t anim,
                                               uint16_t action_id, uint16_t frame_u16,
                                               float facing_dir, float pos_x, float pos_y,
                                               uint8_t lock_bottom_to_zero) {
  msl_ecb_world_points_sample(out, char_id, anim, frame_u16, facing_dir, pos_x, pos_y,
                              lock_bottom_to_zero);
  if (out == NULL || batch == NULL) {
    return;
  }
  (void)action_id;

  // Decomp: mpColl_LoadECB_JObj normalizes the sampled JObj ECB before wall/ceiling/floor tests.
  // In particular, if horizontal span is below max(4, x12C), it recenters the existing span around
  // zero, then enforces the final +/-2 minimum. This is not a wall projection tolerance: it is the
  // source ECB shape consumed by mpColl_80044E10_RightWall / mpColl_80045B74_LeftWall.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_LoadECB_inline}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081B38 (x12C = 10.0f * fp->x34_scale.y)
  float left_rel_x = out->left_rel_x;
  float right_rel_x = out->right_rel_x;
  const float min_ecb_width = fmaxf(4.0f, 10.0f * batch->state.fighter_scale_y[idx]);
  const float ecb_width = fabsf(right_rel_x - left_rel_x);
  if (ecb_width < min_ecb_width) {
    const float half_width = 0.5f * ecb_width;
    left_rel_x = -half_width;
    right_rel_x = half_width;
  }
  if (right_rel_x < 2.0f) {
    right_rel_x = 2.0f;
  }
  if (left_rel_x > -2.0f) {
    left_rel_x = -2.0f;
  }

  out->left_rel_x = left_rel_x;
  out->right_rel_x = right_rel_x;
  out->left_x = pos_x + left_rel_x;
  out->right_x = pos_x + right_rel_x;
  (void)char_id;
  (void)anim;
  (void)frame_u16;
}

static inline uint8_t is_cliff_hold_action(uint16_t a) {
  // Cliff / ledge hold actions use dedicated snap logic and should not be stage-collided.
  // Decomp: ftCo_CliffCatch_Phys snaps to the cliff point each frame.
  // refs/melee/src/melee/ft/ftcliffcommon.c::ftCo_CliffCatch_Phys
  switch (a) {
    case MSL_ACT_CLIFF_CATCH:
    case MSL_ACT_CLIFF_WAIT:
    case MSL_ACT_CLIFF_JUMP_SLOW1:
    case MSL_ACT_CLIFF_JUMP_QUICK1:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t is_grounded_cliff_option_action(uint16_t a, uint8_t on_ground) {
  if (!on_ground) {
    return 0u;
  }
  // Decomp: grounded CliffClimb/CliffAttack/CliffEscape collision callbacks all delegate to
  // ftCo_CliffClimb_Coll, whose grounded branch is only `ft_80084104` (floor-loss handling). Wall
  // and ceiling collision stay owned by the dedicated cliff state callbacks instead.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffAttack.c::ftCo_CliffAttack_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffEscape.c::ftCo_CliffEscape_Coll
  switch (a) {
    case MSL_ACT_CLIFF_CLIMB_SLOW:
    case MSL_ACT_CLIFF_CLIMB_QUICK:
    case MSL_ACT_CLIFF_ATTACK_SLOW:
    case MSL_ACT_CLIFF_ATTACK_QUICK:
    case MSL_ACT_CLIFF_ESCAPE_SLOW:
    case MSL_ACT_CLIFF_ESCAPE_QUICK:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t ceiling_lines_connected_prev_next(const MslStageCeilingGraph* g, int a,
                                                        int b) {
  if (g == NULL || a < 0 || b < 0 || (size_t)a >= g->line_count || (size_t)b >= g->line_count) {
    return 0;
  }
  if (a == b) {
    return 1;
  }
  int cur = a;
  for (size_t i = 0; i < g->line_count; i++) {
    const int16_t next = g->lines[(size_t)cur].next;
    if (next < 0 || (size_t)next >= g->line_count) {
      break;
    }
    if (next == b) {
      return 1;
    }
    cur = (int)next;
  }
  cur = a;
  for (size_t i = 0; i < g->line_count; i++) {
    const int16_t prev = g->lines[(size_t)cur].prev;
    if (prev < 0 || (size_t)prev >= g->line_count) {
      break;
    }
    if (prev == b) {
      return 1;
    }
    cur = (int)prev;
  }
  return 0;
}

static inline uint8_t wall_lines_connected_prev_next(const MslStageWallGraph* g, int a, int b) {
  if (g == NULL || a < 0 || b < 0 || (size_t)a >= g->line_count || (size_t)b >= g->line_count) {
    return 0;
  }
  if (a == b) {
    return 1;
  }
  int cur = a;
  for (size_t i = 0; i < g->line_count; i++) {
    const int16_t next = g->lines[(size_t)cur].next;
    if (next < 0 || (size_t)next >= g->line_count) {
      break;
    }
    if (next == b) {
      return 1;
    }
    cur = (int)next;
  }
  cur = a;
  for (size_t i = 0; i < g->line_count; i++) {
    const int16_t prev = g->lines[(size_t)cur].prev;
    if (prev < 0 || (size_t)prev >= g->line_count) {
      break;
    }
    if (prev == b) {
      return 1;
    }
    cur = (int)prev;
  }
  return 0;
}

static void ed5c_endpoints_generic(float x0, float y0, float x1, float y1, uint8_t has_prev_link,
                                   uint8_t has_next_link, float* ox0, float* oy0, float* ox1,
                                   float* oy1) {
  float ax0 = x0;
  float ay0 = y0;
  float ax1 = x1;
  float ay1 = y1;
  float dist = 0.0f;
  uint8_t have_dist = 0;
  if (has_prev_link) {
    const float dx = ax0 - ax1;
    const float dy = ay0 - ay1;
    dist = sqrtf(dx * dx + dy * dy);
    have_dist = 1;
    if (dist > k_ed5c_min_dist) {
      ax0 += (dx / dist) * k_ed5c_extend;
      ay0 += (dy / dist) * k_ed5c_extend;
    }
  }
  if (has_next_link) {
    if (!have_dist) {
      const float dx = ax0 - ax1;
      const float dy = ay0 - ay1;
      dist = sqrtf(dx * dx + dy * dy);
    }
    if (dist > k_ed5c_min_dist) {
      const float dx = ax1 - ax0;
      const float dy = ay1 - ay0;
      ax1 += (dx / dist) * k_ed5c_extend;
      ay1 += (dy / dist) * k_ed5c_extend;
    }
  }
  *ox0 = ax0;
  *oy0 = ay0;
  *ox1 = ax1;
  *oy1 = ay1;
}

static uint8_t intersect_segment(float x0, float y0, float x1, float y1, float ax, float ay,
                                 float bx, float by, float* ix_out, float* iy_out) {
  const float rx = bx - ax;
  const float ry = by - ay;
  const float sx = x1 - x0;
  const float sy = y1 - y0;
  const float denom = cross2(rx, ry, sx, sy);
  if (denom == 0.0f) {
    return 0;
  }
  const float qpx = x0 - ax;
  const float qpy = y0 - ay;
  const float t = cross2(qpx, qpy, sx, sy) / denom;
  const float u = cross2(qpx, qpy, rx, ry) / denom;
  if (!(t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f)) {
    return 0;
  }
  *ix_out = ax + rx * t;
  *iy_out = ay + ry * t;
  return 1;
}

static uint8_t intersect_segment_mplib(float x0, float y0, float x1, float y1, float ax, float ay,
                                       float bx, float by, float* ix_out, float* iy_out) {
  // Decomp `mpLineIntersection` is not a strict geometric segment test. It allows the moving point
  // to start/end within a 0.1 half-space slop around the static line before clamping the returned
  // point to the source segment. Sloped wall Hug checks consume that helper directly.
  // refs/melee/src/melee/mp/mplib.c::mpLineIntersection
  uint8_t b0_below_a = 0u;
  uint8_t b1_above_a = 0u;
  if (x0 <= x1) {
    if ((ax < x0 && bx < x0) || (x1 < ax && x1 < bx)) {
      return 0u;
    }
  } else if ((ax < x1 && bx < x1) || (x0 < ax && x0 < bx)) {
    return 0u;
  }
  if (y0 <= y1) {
    if ((ay < y0 && by < y0) || (y1 < ay && y1 < by)) {
      return 0u;
    }
  } else if ((ay < y1 && by < y1) || (y0 < ay && y0 < by)) {
    return 0u;
  }

  const double ah = (double)y1 - (double)y0;
  const double aw = (double)x1 - (double)x0;
  const double d0x = (double)ax - (double)x0;
  const double d0y = (double)ay - (double)y0;
  const double hs0 = (aw * d0y) - (ah * d0x);
  if (hs0 < 0.0) {
    if (hs0 < -0.1) {
      return 0u;
    }
    b0_below_a = 1u;
  }

  const double d1x = (double)bx - (double)x1;
  const double d1y = (double)by - (double)y1;
  const double hs1 = (aw * d1y) - (ah * d1x);
  if (hs1 > 0.0) {
    if (hs1 > 0.1) {
      return 0u;
    }
    b1_above_a = 1u;
  }
  if (hs0 == 0.0 && hs1 == 0.0) {
    return 0u;
  }

  const double det = (d0x * d1y) - (d0y * d1x);
  if (det < hs0) {
    if (det < hs1) {
      return 0u;
    }
  } else if (det > hs0) {
    if (det > hs1) {
      return 0u;
    }
  }

  const double bw = (double)bx - (double)ax;
  const double bh = (double)by - (double)ay;
  if ((bw == 0.0 && bh == 0.0) || (b0_below_a && b1_above_a) || (hs0 >= 0.0 && b1_above_a)) {
    return 0u;
  }

  const double area = (bw * ah) - (bh * aw);
  if (!(fabs(area) > 0.0001)) {
    return 0u;
  }
  const double t = ((bw * d0y) - (bh * d0x)) / area;
  if (t > 0.0) {
    if (t < 1.0) {
      *ix_out = (float)(aw * t + (double)x0);
      *iy_out = (float)(ah * t + (double)y0);
    } else {
      *ix_out = x1;
      *iy_out = y1;
    }
  } else {
    *ix_out = x0;
    *iy_out = y0;
  }
  return 1u;
}

static void remap2d(float ax0, float ay0, float ax1, float ay1, float bx0, float by0, float bx1,
                    float by1, float px, float py, float* out_x, float* out_y) {
  // Decomp: mpRemap2d remaps a point from segment A to segment B, clamping the projected segment
  // parameter to [0,1].
  // refs/melee/src/melee/mp/mplib.c::mpRemap2d
  const double dx = (double)ax1 - (double)ax0;
  const double dy = (double)ay1 - (double)ay0;
  const double f30 = (double)px - (double)ax0;
  const double f29 = (double)py - (double)ay0;
  const double dist2 = dy * dy + dx * dx;
  if (fabs(dist2) > 0.0001) {
    double t = (dy * f29 + dx * f30) / dist2;
    if (t > 1.0) {
      t = 1.0;
    } else if (t < 0.0) {
      t = 0.0;
    }
    *out_x = (float)((double)px + (1.0 - t) * ((double)bx0 - (double)ax0) +
                     t * ((double)bx1 - (double)ax1));
    *out_y = (float)((double)py + (1.0 - t) * ((double)by0 - (double)ay0) +
                     t * ((double)by1 - (double)ay1));
  } else {
    *out_x = px + (bx0 - ax0) + (bx1 - ax0);
    *out_y = py + (by0 - ay0) + (by1 - ay0);
  }
}

static uint8_t intersect_horiz_clamped(float x0, float y0, float x1, float ax, float ay, float bx,
                                       float by, uint8_t require_rising, float* ix_out,
                                       float* iy_out) {
  // Decomp: mpLineIntersectionH has a direction gate for floor/ceiling usage:
  // - floor checks only when ay >= by (falling / non-rising)
  // - ceiling checks only when ay <= by (rising / non-falling)
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
  // refs/melee/src/melee/mp/mplib.c::mpCheckCeiling
  if (require_rising) {
    if (!(ay <= by)) {
      return 0;
    }
  } else {
    if (!(ay >= by)) {
      return 0;
    }
  }

  const float min_x = (x0 < x1) ? x0 : x1;
  const float max_x = (x0 < x1) ? x1 : x0;
  if (ay == by) {
    if (ay != y0) {
      return 0;
    }
    if (ax < min_x || ax > max_x) {
      return 0;
    }
    *ix_out = ax;
    *iy_out = y0;
    return 1;
  }
  if (require_rising) {
    if (!(ay <= y0 && by >= y0)) {
      return 0;
    }
    const float t = (y0 - ay) / (by - ay);
    float ix = ax + (bx - ax) * t;
    if (ix < min_x - k_line_end_clamp) {
      return 0;
    }
    if (ix > max_x + k_line_end_clamp) {
      return 0;
    }
    if (ix < min_x) {
      ix = min_x;
    } else if (ix > max_x) {
      ix = max_x;
    }
    *ix_out = ix;
    *iy_out = y0;
    return 1;
  }

  if (!(ay >= y0 && by <= y0)) {
    return 0;
  }
  const float t = (ay - y0) / (ay - by);
  float ix = ax + (bx - ax) * t;
  if (ix < min_x - k_line_end_clamp) {
    return 0;
  }
  if (ix > max_x + k_line_end_clamp) {
    return 0;
  }
  if (ix < min_x) {
    ix = min_x;
  } else if (ix > max_x) {
    ix = max_x;
  }
  *ix_out = ix;
  *iy_out = y0;
  return 1;
}

static uint8_t intersect_vert_clamped(float x0, float y0, float y1, float ax, float ay, float bx,
                                      float by, uint8_t require_moving_right, float* ix_out,
                                      float* iy_out) {
  // Decomp: vertical wall intersection is gated on horizontal direction for vertical-ish walls:
  // - mpCheckLeftWall only when ax <= bx
  // - mpCheckRightWall only when ax >= bx
  // refs/melee/src/melee/mp/mplib.c::mpCheckLeftWall
  // refs/melee/src/melee/mp/mplib.c::mpCheckRightWall
  if (require_moving_right) {
    if (!(ax <= bx)) {
      return 0;
    }
  } else {
    if (!(ax >= bx)) {
      return 0;
    }
  }
  const float min_y = (y0 < y1) ? y0 : y1;
  const float max_y = (y0 < y1) ? y1 : y0;
  if (ax == bx) {
    if (ax != x0) {
      return 0;
    }
    if (ay < min_y || ay > max_y) {
      return 0;
    }
    *ix_out = x0;
    *iy_out = ay;
    return 1;
  }
  const float t = (x0 - ax) / (bx - ax);
  if (!(t >= 0.0f && t <= 1.0f)) {
    return 0;
  }
  float iy = ay + (by - ay) * t;
  if (iy < min_y - k_line_end_clamp) {
    return 0;
  }
  if (iy > max_y + k_line_end_clamp) {
    return 0;
  }
  if (iy < min_y) {
    iy = min_y;
  } else if (iy > max_y) {
    iy = max_y;
  }
  *ix_out = x0;
  *iy_out = iy;
  return 1;
}

static inline void normal_from_line(float x0, float y0, float x1, float y1, float* nx_out,
                                    float* ny_out) {
  float nx = -(y1 - y0);
  float ny = x1 - x0;
  const float len = sqrtf(nx * nx + ny * ny);
  if (len > 0.0f) {
    nx /= len;
    ny /= len;
  } else {
    nx = 0.0f;
    ny = 1.0f;
  }
  *nx_out = nx;
  *ny_out = ny;
}

static int ceiling_e090_project(const MslStageCeilingGraph* g, int line_idx, float x_in, float y_in,
                                float* y_out, float* nx_out, float* ny_out) {
  // Decomp: mpLib_8004E090_Ceiling traverses prev/next for ceiling-only lines, then returns a signed
  // correction and normal for the ceiling line above vec->x.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      !g->lines[(size_t)line_idx].fighter_solid) {
    return -1;
  }
  int dir = 0;
  int cur = line_idx;
  float x = x_in;
  float x0 = 0.0f, x1 = 0.0f;
  for (;;) {
    const MslStageCeilingLine* l = &g->lines[cur];
    x0 = l->x0;
    x1 = l->x1;
    if (x_in < x1) {
      if (dir != 1) {
        const int next = (int)l->next;
        if (next < 0) {
          if (x_in - x1 < -k_line_end_clamp) {
            return -1;
          }
          x = x1;
          break;
        }
        cur = next;
        dir = -1;
        continue;
      }
      x = x1;
    } else if (x_in > x0) {
      if (dir != -1) {
        const int prev = (int)l->prev;
        if (prev < 0) {
          if (x_in - x0 > k_line_end_clamp) {
            return -1;
          }
          x = x0;
          break;
        }
        cur = prev;
        dir = 1;
        continue;
      }
      x = x0;
    }
    break;
  }

  const MslStageCeilingLine* out = &g->lines[cur];
  const float y0 = out->y0;
  const float y1 = out->y1;
  if (y_out != NULL) {
    *y_out = (y1 - y0) * (x - x0) / (x1 - x0) + y0 - y_in - k_ceiling_y_bias;
  }
  if (nx_out != NULL && ny_out != NULL) {
    normal_from_line(x0, y0, x1, y1, nx_out, ny_out);
  }
  return cur;
}

static int left_wall_e398_project(const MslStageWallGraph* g, int line_idx, float x_in, float y_in,
                                  float* x_out, float* nx_out, float* ny_out) {
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      !g->lines[(size_t)line_idx].fighter_solid) {
    return -1;
  }
  int dir = 0;
  int cur = line_idx;
  float y = y_in;
  float y0 = 0.0f, y1 = 0.0f;
  for (;;) {
    const MslStageWallLine* l = &g->lines[cur];
    y0 = l->y0;
    y1 = l->y1;
    if (y_in < y0) {
      if (dir != 1) {
        const int prev = (int)l->prev;
        if (prev < 0) {
          if (y_in - y0 < -k_line_end_clamp) {
            return -1;
          }
          y = y0;
          break;
        }
        cur = prev;
        dir = -1;
        continue;
      }
    } else if (y_in > y1) {
      if (dir != -1) {
        const int next = (int)l->next;
        if (next < 0) {
          if (y_in - y1 > k_line_end_clamp) {
            return -1;
          }
          y = y1;
          break;
        }
        cur = next;
        dir = 1;
        continue;
      }
    }
    break;
  }
  const MslStageWallLine* out = &g->lines[cur];
  const float x0 = out->x0;
  const float x1 = out->x1;
  if (x_out != NULL) {
    *x_out = x0 + (x1 - x0) * (y - y0) / (y1 - y0) - x_in;
  }
  if (nx_out != NULL && ny_out != NULL) {
    normal_from_line(x0, y0, x1, y1, nx_out, ny_out);
  }
  return cur;
}

static int right_wall_e684_project(const MslStageWallGraph* g, int line_idx, float x_in, float y_in,
                                   float* x_out, float* nx_out, float* ny_out) {
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      !g->lines[(size_t)line_idx].fighter_solid) {
    return -1;
  }
  int dir = 0;
  int cur = line_idx;
  float y = y_in;
  float y0 = 0.0f, y1 = 0.0f;
  for (;;) {
    const MslStageWallLine* l = &g->lines[cur];
    y0 = l->y0;
    y1 = l->y1;
    if (y_in > y0) {
      if (dir != -1) {
        const int prev = (int)l->prev;
        if (prev < 0) {
          if (y_in - y0 > k_line_end_clamp) {
            return -1;
          }
          y = y0;
          break;
        }
        cur = prev;
        dir = 1;
        continue;
      }
      y = y0;
    } else if (y_in < y1) {
      if (dir != 1) {
        const int next = (int)l->next;
        if (next < 0) {
          if (y_in - y1 < -k_line_end_clamp) {
            return -1;
          }
          y = y1;
          break;
        }
        cur = next;
        dir = -1;
        continue;
      }
      y = y1;
    }
    break;
  }
  const MslStageWallLine* out = &g->lines[cur];
  const float x0 = out->x0;
  const float x1 = out->x1;
  if (x_out != NULL) {
    *x_out = x0 + (x1 - x0) * (y - y0) / (y1 - y0) - x_in;
  }
  if (nx_out != NULL && ny_out != NULL) {
    normal_from_line(x0, y0, x1, y1, nx_out, ny_out);
  }
  return cur;
}

static uint8_t ceiling_sweep_check(const MslStageCeilingGraph* g, float ax, float ay, float bx,
                                   float by, int prefer_line_idx, int* out_line_idx, float* out_ix,
                                   float* out_iy, float* out_nx, float* out_ny) {
  // refs/melee/src/melee/mp/mplib.c::mpCheckCeiling
  if (g == NULL || out_line_idx == NULL) {
    return 0;
  }
  const float sweep_min_x = (ax < bx) ? ax : bx;
  const float sweep_max_x = (ax > bx) ? ax : bx;
  const float sweep_min_y = (ay < by) ? ay : by;
  const float sweep_max_y = (ay > by) ? ay : by;
  if (sweep_max_x < g->min_x || sweep_min_x > g->max_x || sweep_max_y < g->min_y ||
      sweep_min_y > g->max_y) {
    return 0;
  }
  uint8_t found = 0;
  float best_dist2 = FLT_MAX;
  int best_idx = -1;
  int best_pref = -1;
  float best_ix = 0.0f, best_iy = 0.0f;
  float best_nx = 0.0f, best_ny = -1.0f;

  for (size_t li = 0; li < g->line_count; li++) {
    const MslStageCeilingLine* l = &g->lines[li];
    if (!l->fighter_solid) {
      continue;
    }
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    ed5c_endpoints_generic(l->x0, l->y0, l->x1, l->y1, l->has_prev_link, l->has_next_link, &x0, &y0,
                           &x1, &y1);

    float ix = 0.0f, iy = 0.0f;
    uint8_t hit = 0;
    const float dy = y0 - y1;
    if (fabsf(dy) > k_line_axis_thresh) {
      hit = intersect_segment(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy);
    } else {
      hit = intersect_horiz_clamped(x0, y0, x1, ax, ay, bx, by, 1, &ix, &iy);
    }
    if (!hit) {
      continue;
    }

    const float dx = ix - ax;
    const float dy2 = iy - ay;
    const float dist2 = dx * dx + dy2 * dy2;

    int pref = 0;
    if (prefer_line_idx >= 0) {
      if ((int)li == prefer_line_idx) {
        pref = 2;
      } else if (ceiling_lines_connected_prev_next(g, prefer_line_idx, (int)li)) {
        pref = 1;
      }
    }

    if (!found || (dist2 < best_dist2) || (dist2 == best_dist2 && pref > best_pref) ||
        (dist2 == best_dist2 && pref == best_pref &&
         g->lines[li].segment_i < g->lines[(size_t)best_idx].segment_i)) {
      found = 1;
      best_dist2 = dist2;
      best_idx = (int)li;
      best_pref = pref;
      best_ix = ix;
      best_iy = iy;
      if (fabsf(dy) <= k_line_axis_thresh) {
        best_nx = 0.0f;
        best_ny = -1.0f;
      } else {
        normal_from_line(x0, y0, x1, y1, &best_nx, &best_ny);
      }
    }
  }

  if (!found) {
    return 0;
  }
  *out_line_idx = best_idx;
  if (out_ix) {
    *out_ix = best_ix;
  }
  if (out_iy) {
    *out_iy = best_iy;
  }
  if (out_nx) {
    *out_nx = best_nx;
  }
  if (out_ny) {
    *out_ny = best_ny;
  }
  return 1;
}

static uint8_t wall_sweep_check(const MslStageWallGraph* g, uint8_t is_left_wall, float ax,
                                float ay, float bx, float by, int prefer_line_idx,
                                uint8_t use_mplib_slop, int* out_line_idx, float* out_ix,
                                float* out_iy, float* out_nx, float* out_ny) {
  if (g == NULL || out_line_idx == NULL) {
    return 0;
  }
  const float sweep_min_x = (ax < bx) ? ax : bx;
  const float sweep_max_x = (ax > bx) ? ax : bx;
  const float sweep_min_y = (ay < by) ? ay : by;
  const float sweep_max_y = (ay > by) ? ay : by;
  if (sweep_max_x < g->min_x || sweep_min_x > g->max_x || sweep_max_y < g->min_y ||
      sweep_min_y > g->max_y) {
    return 0;
  }
  uint8_t found = 0;
  float best_dist2 = FLT_MAX;
  int best_idx = -1;
  int best_pref = -1;
  float best_ix = 0.0f, best_iy = 0.0f;
  float best_nx = is_left_wall ? -1.0f : 1.0f;
  float best_ny = 0.0f;

  for (size_t li = 0; li < g->line_count; li++) {
    const MslStageWallLine* l = &g->lines[li];
    if (!l->fighter_solid) {
      continue;
    }
    if (l->max_x + k_line_end_clamp < sweep_min_x || l->min_x - k_line_end_clamp > sweep_max_x ||
        l->max_y + k_line_end_clamp < sweep_min_y || l->min_y - k_line_end_clamp > sweep_max_y) {
      continue;
    }
    // Decomp: mpCheck{Left,Right}Wall tests the raw wall segment endpoints, then
    // mpLineIntersectionV applies only its local +/-0.1 endpoint clamp for vertical walls. Do not
    // use the broader mpLib_8004ED5C endpoint extension here; that helper belongs to other
    // projection/traversal paths and over-admits FD ledge-side wall candidates above line 9.
    // refs/melee/src/melee/mp/mplib.c::{mpCheckLeftWall,mpCheckRightWall,mpLineIntersectionV}
    const float x0 = l->x0;
    const float y0 = l->y0;
    const float x1 = l->x1;
    const float y1 = l->y1;

    float ix = 0.0f, iy = 0.0f;
    uint8_t hit = 0;
    if (fabsf(x0 - x1) > k_line_axis_thresh) {
      hit = use_mplib_slop ? intersect_segment_mplib(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy)
                           : intersect_segment(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy);
    } else {
      hit = intersect_vert_clamped(x0, y0, y1, ax, ay, bx, by, is_left_wall, &ix, &iy);
    }
    if (!hit) {
      continue;
    }

    const float dx = ix - ax;
    const float dy = iy - ay;
    const float dist2 = dx * dx + dy * dy;

    int pref = 0;
    if (prefer_line_idx >= 0) {
      if ((int)li == prefer_line_idx) {
        pref = 2;
      } else if (wall_lines_connected_prev_next(g, prefer_line_idx, (int)li)) {
        pref = 1;
      }
    }

    if (!found || (dist2 < best_dist2) || (dist2 == best_dist2 && pref > best_pref) ||
        (dist2 == best_dist2 && pref == best_pref &&
         g->lines[li].segment_i < g->lines[(size_t)best_idx].segment_i)) {
      found = 1;
      best_dist2 = dist2;
      best_idx = (int)li;
      best_pref = pref;
      best_ix = ix;
      best_iy = iy;
      if (fabsf(x0 - x1) <= k_line_axis_thresh) {
        best_nx = is_left_wall ? -1.0f : 1.0f;
        best_ny = 0.0f;
      } else {
        normal_from_line(x0, y0, x1, y1, &best_nx, &best_ny);
      }
    }
  }

  if (!found) {
    return 0;
  }
  *out_line_idx = best_idx;
  if (out_ix) {
    *out_ix = best_ix;
  }
  if (out_iy) {
    *out_iy = best_iy;
  }
  if (out_nx) {
    *out_nx = best_nx;
  }
  if (out_ny) {
    *out_ny = best_ny;
  }
  return 1;
}

static inline void right_wall_envelope_consider(float cand_x, int line_idx,
                                                const MslStageWallGraph* g, float* io_best_x,
                                                int* io_best_line_idx, float* io_best_nx,
                                                float* io_best_ny) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      !g->lines[(size_t)line_idx].fighter_solid) {
    return;
  }
  if (*io_best_line_idx < 0 || cand_x > *io_best_x) {
    float nx = 1.0f, ny = 0.0f;
    const MslStageWallLine* l = &g->lines[(size_t)line_idx];
    normal_from_line(l->x0, l->y0, l->x1, l->y1, &nx, &ny);
    *io_best_x = cand_x;
    *io_best_line_idx = line_idx;
    *io_best_nx = nx;
    *io_best_ny = ny;
  }
}

static inline void left_wall_envelope_consider(float cand_x, int line_idx,
                                               const MslStageWallGraph* g, float* io_best_x,
                                               int* io_best_line_idx, float* io_best_nx,
                                               float* io_best_ny) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      !g->lines[(size_t)line_idx].fighter_solid) {
    return;
  }
  if (*io_best_line_idx < 0 || cand_x < *io_best_x) {
    float nx = -1.0f, ny = 0.0f;
    const MslStageWallLine* l = &g->lines[(size_t)line_idx];
    normal_from_line(l->x0, l->y0, l->x1, l->y1, &nx, &ny);
    *io_best_x = cand_x;
    *io_best_line_idx = line_idx;
    *io_best_nx = nx;
    *io_best_ny = ny;
  }
}

typedef struct MslWallCandidateList {
  int line_idx[MSL_WALL_CANDIDATE_MAX];
  uint8_t count;
  uint8_t has_hug;
  float first_ix;
  float first_iy;
} MslWallCandidateList;

static inline void wall_candidate_list_init(MslWallCandidateList* out) {
  out->count = 0u;
  out->has_hug = 0u;
  out->first_ix = 0.0f;
  out->first_iy = 0.0f;
}

static inline void right_wall_candidate_add(MslWallCandidateList* out, const MslStageWallGraph* g,
                                            int line_idx, uint8_t is_hug, float ix, float iy) {
  if (out == NULL || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      !g->lines[(size_t)line_idx].fighter_solid) {
    return;
  }
  for (uint8_t i = 0u; i < out->count; i++) {
    const int existing = out->line_idx[i];
    if (existing == line_idx || wall_lines_connected_prev_next(g, existing, line_idx)) {
      if (is_hug) {
        out->has_hug = 1u;
      }
      return;
    }
  }
  if (out->count >= (uint8_t)MSL_WALL_CANDIDATE_MAX) {
    return;
  }
  if (out->count == 0u) {
    out->first_ix = ix;
    out->first_iy = iy;
  }
  out->line_idx[out->count++] = line_idx;
  if (is_hug) {
    out->has_hug = 1u;
  }
}

static inline void left_wall_candidate_add(MslWallCandidateList* out, const MslStageWallGraph* g,
                                           int line_idx, uint8_t is_hug, float ix, float iy) {
  if (out == NULL || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      !g->lines[(size_t)line_idx].fighter_solid) {
    return;
  }
  for (uint8_t i = 0u; i < out->count; i++) {
    const int existing = out->line_idx[i];
    if (existing == line_idx || wall_lines_connected_prev_next(g, existing, line_idx)) {
      if (is_hug) {
        out->has_hug = 1u;
      }
      return;
    }
  }
  if (out->count >= (uint8_t)MSL_WALL_CANDIDATE_MAX) {
    return;
  }
  if (out->count == 0u) {
    out->first_ix = ix;
    out->first_iy = iy;
  }
  out->line_idx[out->count++] = line_idx;
  if (is_hug) {
    out->has_hug = 1u;
  }
}

static inline void wall_candidate_list_remove_connected(MslWallCandidateList* out,
                                                        const MslStageWallGraph* g,
                                                        int excluded_line_idx) {
  if (out == NULL || g == NULL || excluded_line_idx < 0) {
    return;
  }
  uint8_t write = 0u;
  uint8_t removed_hug = 0u;
  for (uint8_t read = 0u; read < out->count; read++) {
    const int line_idx = out->line_idx[read];
    if (line_idx == excluded_line_idx ||
        wall_lines_connected_prev_next(g, line_idx, excluded_line_idx)) {
      removed_hug = 1u;
      continue;
    }
    out->line_idx[write++] = line_idx;
  }
  out->count = write;
  if (removed_hug && write == 0u) {
    out->has_hug = 0u;
    out->first_ix = 0.0f;
    out->first_iy = 0.0f;
  }
}

static inline void right_wall_candidate_sweep(MslWallCandidateList* out, const MslStageWallGraph* g,
                                              float prev_x, float prev_y, float cur_x, float cur_y,
                                              int prefer_line_idx, uint8_t is_hug,
                                              int excluded_line_idx) {
  float ix = 0.0f, iy = 0.0f;
  float nx = 1.0f, ny = 0.0f;
  int hit = -1;
  if (wall_sweep_check(g, 0, prev_x, prev_y, cur_x, cur_y, prefer_line_idx, is_hug, &hit, &ix, &iy,
                       &nx, &ny) &&
      hit != excluded_line_idx) {
    right_wall_candidate_add(out, g, hit, is_hug, ix, iy);
  }
}

static inline void left_wall_candidate_sweep(MslWallCandidateList* out, const MslStageWallGraph* g,
                                             float prev_x, float prev_y, float cur_x, float cur_y,
                                             int prefer_line_idx, uint8_t is_hug,
                                             int excluded_line_idx) {
  float ix = 0.0f, iy = 0.0f;
  float nx = -1.0f, ny = 0.0f;
  int hit = -1;
  if (wall_sweep_check(g, 1, prev_x, prev_y, cur_x, cur_y, prefer_line_idx, is_hug, &hit, &ix, &iy,
                       &nx, &ny) &&
      hit != excluded_line_idx) {
    left_wall_candidate_add(out, g, hit, is_hug, ix, iy);
  }
}

static inline void right_wall_candidate_quad(MslWallCandidateList* out, const MslStageWallGraph* g,
                                             float prev_a_x, float prev_a_y, float prev_b_x,
                                             float prev_b_y, float cur_a_x, float cur_a_y,
                                             float cur_b_x, float cur_b_y) {
  if (out == NULL || g == NULL || g->lines == NULL) {
    return;
  }
  // Decomp: `mpLib_800511A4_RightWall` tests the two wall vertices against the swept ECB side
  // quad. FD uses static stage vertices, so `CollVtx.x10/x14` are the same as `pos` from
  // `mpLibLoad`; dynamic-stage remap data is not part of the current FD target domain.
  // refs/melee/src/melee/mp/mplib.c::{mpLibLoad,mpLib_800511A4_RightWall}
  float best_dist2 = FLT_MAX;
  int best_line_idx = -1;
  float best_ix = 0.0f;
  float best_iy = 0.0f;
  const float sweep_min_x = min4f(prev_a_x, prev_b_x, cur_a_x, cur_b_x) - k_line_end_clamp;
  const float sweep_max_x = max4f(prev_a_x, prev_b_x, cur_a_x, cur_b_x) + k_line_end_clamp;
  const float sweep_min_y = min4f(prev_a_y, prev_b_y, cur_a_y, cur_b_y) - k_line_end_clamp;
  const float sweep_max_y = max4f(prev_a_y, prev_b_y, cur_a_y, cur_b_y) + k_line_end_clamp;
  if (g->max_x < sweep_min_x || g->min_x > sweep_max_x || g->max_y < sweep_min_y ||
      g->min_y > sweep_max_y) {
    return;
  }

  for (size_t li = 0; li < g->line_count; li++) {
    const MslStageWallLine* l = &g->lines[li];
    if (!l->fighter_solid) {
      continue;
    }
    if (wall_line_outside_box(l, sweep_min_x, sweep_max_x, sweep_min_y, sweep_max_y)) {
      continue;
    }
    const float vx[2] = {l->x0, l->x1};
    const float vy[2] = {l->y0, l->y1};
    for (int vi = 0; vi < 2; vi++) {
      float remap_x = 0.0f;
      float remap_y = 0.0f;
      remap2d(prev_a_x, prev_a_y, prev_b_x, prev_b_y, cur_a_x, cur_a_y, cur_b_x, cur_b_y, vx[vi],
              vy[vi], &remap_x, &remap_y);

      const float vdx = vx[vi] - remap_x;
      const float vdy = vy[vi] - remap_y;
      if (vdx * vdx + vdy * vdy <= 0.001f) {
        continue;
      }

      float ix = 0.0f;
      float iy = 0.0f;
      if (!intersect_segment(cur_a_x, cur_a_y, cur_b_x, cur_b_y, remap_x, remap_y, vx[vi], vy[vi],
                             &ix, &iy)) {
        continue;
      }

      float dist2 = (ix - vx[vi]) * (ix - vx[vi]) + (iy - vy[vi]) * (iy - vy[vi]);
      if (vdx * (ix - vx[vi]) + vdy * (iy - vy[vi]) < 0.0f) {
        dist2 = -dist2;
      }
      if (best_line_idx < 0 || dist2 < best_dist2 ||
          (dist2 == best_dist2 && l->segment_i < g->lines[(size_t)best_line_idx].segment_i)) {
        best_dist2 = dist2;
        best_line_idx = (int)li;
        best_ix = ix;
        best_iy = iy;
      }
    }
  }

  if (best_line_idx >= 0) {
    right_wall_candidate_add(out, g, best_line_idx, 0u, best_ix, best_iy);
  }
}

static inline void left_wall_candidate_quad(MslWallCandidateList* out, const MslStageWallGraph* g,
                                            float prev_a_x, float prev_a_y, float prev_b_x,
                                            float prev_b_y, float cur_a_x, float cur_a_y,
                                            float cur_b_x, float cur_b_y) {
  if (out == NULL || g == NULL || g->lines == NULL) {
    return;
  }
  // Symmetric `mpLib_800515A0_LeftWall` representation for static FD walls.
  // refs/melee/src/melee/mp/mplib.c::mpLib_800515A0_LeftWall
  float best_dist2 = FLT_MAX;
  int best_line_idx = -1;
  float best_ix = 0.0f;
  float best_iy = 0.0f;
  const float sweep_min_x = min4f(prev_a_x, prev_b_x, cur_a_x, cur_b_x) - k_line_end_clamp;
  const float sweep_max_x = max4f(prev_a_x, prev_b_x, cur_a_x, cur_b_x) + k_line_end_clamp;
  const float sweep_min_y = min4f(prev_a_y, prev_b_y, cur_a_y, cur_b_y) - k_line_end_clamp;
  const float sweep_max_y = max4f(prev_a_y, prev_b_y, cur_a_y, cur_b_y) + k_line_end_clamp;
  if (g->max_x < sweep_min_x || g->min_x > sweep_max_x || g->max_y < sweep_min_y ||
      g->min_y > sweep_max_y) {
    return;
  }

  for (size_t li = 0; li < g->line_count; li++) {
    const MslStageWallLine* l = &g->lines[li];
    if (!l->fighter_solid) {
      continue;
    }
    if (wall_line_outside_box(l, sweep_min_x, sweep_max_x, sweep_min_y, sweep_max_y)) {
      continue;
    }
    const float vx[2] = {l->x0, l->x1};
    const float vy[2] = {l->y0, l->y1};
    for (int vi = 0; vi < 2; vi++) {
      float remap_x = 0.0f;
      float remap_y = 0.0f;
      remap2d(prev_a_x, prev_a_y, prev_b_x, prev_b_y, cur_a_x, cur_a_y, cur_b_x, cur_b_y, vx[vi],
              vy[vi], &remap_x, &remap_y);

      const float vdx = vx[vi] - remap_x;
      const float vdy = vy[vi] - remap_y;
      if (vdx * vdx + vdy * vdy <= 0.001f) {
        continue;
      }

      float ix = 0.0f;
      float iy = 0.0f;
      if (!intersect_segment(cur_a_x, cur_a_y, cur_b_x, cur_b_y, remap_x, remap_y, vx[vi], vy[vi],
                             &ix, &iy)) {
        continue;
      }

      float dist2 = (ix - vx[vi]) * (ix - vx[vi]) + (iy - vy[vi]) * (iy - vy[vi]);
      if (vdx * (ix - vx[vi]) + vdy * (iy - vy[vi]) < 0.0f) {
        dist2 = -dist2;
      }
      if (best_line_idx < 0 || dist2 < best_dist2 ||
          (dist2 == best_dist2 && l->segment_i < g->lines[(size_t)best_line_idx].segment_i)) {
        best_dist2 = dist2;
        best_line_idx = (int)li;
        best_ix = ix;
        best_iy = iy;
      }
    }
  }

  if (best_line_idx >= 0) {
    left_wall_candidate_add(out, g, best_line_idx, 0u, best_ix, best_iy);
  }
}

static uint8_t left_wall_air_envelope_min_x(const MslStageWallGraph* g,
                                            const MslWallCandidateList* candidates,
                                            const MslEcbWorldPoints* ecb, float cur_pos_x,
                                            float cur_pos_y, float* out_x, int* out_line_idx,
                                            float* out_nx, float* out_ny) {
  if (g == NULL || ecb == NULL || out_x == NULL || out_line_idx == NULL || out_nx == NULL ||
      out_ny == NULL || candidates == NULL || candidates->count == 0u) {
    return 0u;
  }

  // Decomp: mpColl_80046224_LeftWall resolves the whole airborne ECB envelope after
  // mpColl_80045B74_LeftWall has collected side/bottom/top candidate wall ids.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80045B74_LeftWall,mpColl_80046224_LeftWall}
  float best_x = FLT_MAX;
  int best_line_idx = -1;
  float best_nx = -1.0f;
  float best_ny = 0.0f;

  const float bot = cur_pos_y + ecb->bottom_rel_y;
  const float mid = cur_pos_y + ecb->side_rel_y;
  const float top = cur_pos_y + ecb->top_rel_y;

  const float right_x = ecb->right_rel_x;
  const float bottom_x = 0.0f;
  const float top_x_rel = 0.0f;
  const float bottom_y = ecb->bottom_rel_y;
  const float right_y = ecb->side_rel_y;
  const float top_y = ecb->top_rel_y;
  const float lower_denom = right_y - bottom_y;
  const float upper_denom = right_y - top_y;
  const float lower_slope = (lower_denom != 0.0f) ? (right_x / lower_denom) : 0.0f;
  const float upper_slope = (upper_denom != 0.0f) ? (right_x / upper_denom) : 0.0f;

  for (uint8_t ci = 0u; ci < candidates->count; ci++) {
    const int start_line_idx = candidates->line_idx[ci];
    if (start_line_idx < 0 || (size_t)start_line_idx >= g->line_count) {
      continue;
    }

    const MslStageWallLine* start = &g->lines[(size_t)start_line_idx];
    if (start->y0 > top) {
      left_wall_envelope_consider(start->x0, start_line_idx, g, &best_x, &best_line_idx, &best_nx,
                                  &best_ny);
      continue;
    }
    if (start->y1 < bot) {
      left_wall_envelope_consider(start->x1, start_line_idx, g, &best_x, &best_line_idx, &best_nx,
                                  &best_ny);
      continue;
    }

    const float sample_x[3] = {ecb->bottom_x, ecb->right_x, ecb->top_x};
    const float sample_y[3] = {ecb->bottom_y, ecb->right_y, ecb->top_y};
    for (int i = 0; i < 3; i++) {
      float x_corr = 0.0f;
      float nx = -1.0f, ny = 0.0f;
      const int out_idx =
          left_wall_e398_project(g, start_line_idx, sample_x[i], sample_y[i], &x_corr, &nx, &ny);
      if (out_idx >= 0) {
        left_wall_envelope_consider(cur_pos_x + x_corr, out_idx, g, &best_x, &best_line_idx,
                                    &best_nx, &best_ny);
      }
    }

    int j = start_line_idx;
    for (size_t guard = 0; j >= 0 && (size_t)j < g->line_count && guard < g->line_count; guard++) {
      const MslStageWallLine* l = &g->lines[(size_t)j];
      const float vy = l->y0;
      float edge_x = 0.0f;
      if (bot <= vy && vy <= mid) {
        edge_x = lower_slope * (vy - bot) + bottom_x;
      } else if (mid <= vy && vy <= top) {
        edge_x = upper_slope * (vy - top) + top_x_rel;
      } else if (vy < bot) {
        break;
      } else {
        j = (int)l->prev;
        continue;
      }
      left_wall_envelope_consider(l->x0 - edge_x, j, g, &best_x, &best_line_idx, &best_nx,
                                  &best_ny);
      j = (int)l->prev;
    }

    j = start_line_idx;
    for (size_t guard = 0; j >= 0 && (size_t)j < g->line_count && guard < g->line_count; guard++) {
      const MslStageWallLine* l = &g->lines[(size_t)j];
      const float vy = l->y1;
      float edge_x = 0.0f;
      if (bot <= vy && vy <= mid) {
        edge_x = lower_slope * (vy - bot) + bottom_x;
      } else if (mid <= vy && vy <= top) {
        edge_x = upper_slope * (vy - top) + top_x_rel;
      } else if (vy > top) {
        break;
      } else {
        j = (int)l->next;
        continue;
      }
      left_wall_envelope_consider(l->x1 - edge_x, j, g, &best_x, &best_line_idx, &best_nx,
                                  &best_ny);
      j = (int)l->next;
    }
  }

  if (best_line_idx < 0 || !(best_x < cur_pos_x)) {
    return 0u;
  }
  *out_x = best_x;
  *out_line_idx = best_line_idx;
  *out_nx = best_nx;
  *out_ny = best_ny;
  return 1u;
}

static uint8_t right_wall_air_envelope_max_x(const MslStageWallGraph* g,
                                             const MslWallCandidateList* candidates,
                                             const MslEcbWorldPoints* ecb, float cur_pos_x,
                                             float cur_pos_y, float* out_x, int* out_line_idx,
                                             float* out_nx, float* out_ny) {
  if (g == NULL || ecb == NULL || out_x == NULL || out_line_idx == NULL || out_nx == NULL ||
      out_ny == NULL || candidates == NULL || candidates->count == 0u) {
    return 0u;
  }

  // Decomp: after mpColl_80044E10_RightWall collects candidate wall ids, mpColl_800454A4_RightWall
  // resolves the whole airborne ECB envelope, not just the point that initially swept into the
  // wall. This matters for rotated SpecialHi JObj ECBs near FD's right lip, where the wall endpoint
  // and side envelope can own cur_pos.x even when the side point projection is not the max.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044E10_RightWall,mpColl_800454A4_RightWall}
  float best_x = -FLT_MAX;
  int best_line_idx = -1;
  float best_nx = 1.0f;
  float best_ny = 0.0f;

  const float bot = cur_pos_y + ecb->bottom_rel_y;
  const float mid = cur_pos_y + ecb->side_rel_y;
  const float top = cur_pos_y + ecb->top_rel_y;

  const float left_x = ecb->left_rel_x;
  const float bottom_x = 0.0f;
  const float top_x_rel = 0.0f;
  const float bottom_y = ecb->bottom_rel_y;
  const float left_y = ecb->side_rel_y;
  const float top_y = ecb->top_rel_y;
  const float lower_denom = left_y - bottom_y;
  const float upper_denom = left_y - top_y;
  const float lower_slope = (lower_denom != 0.0f) ? (left_x / lower_denom) : 0.0f;
  const float upper_slope = (upper_denom != 0.0f) ? (left_x / upper_denom) : 0.0f;

  for (uint8_t ci = 0u; ci < candidates->count; ci++) {
    const int start_line_idx = candidates->line_idx[ci];
    if (start_line_idx < 0 || (size_t)start_line_idx >= g->line_count) {
      continue;
    }

    const MslStageWallLine* start = &g->lines[(size_t)start_line_idx];
    if (start->y0 < bot) {
      right_wall_envelope_consider(start->x0, start_line_idx, g, &best_x, &best_line_idx, &best_nx,
                                   &best_ny);
      continue;
    }
    if (start->y1 > top) {
      right_wall_envelope_consider(start->x1, start_line_idx, g, &best_x, &best_line_idx, &best_nx,
                                   &best_ny);
      continue;
    }

    const float sample_x[3] = {ecb->bottom_x, ecb->left_x, ecb->top_x};
    const float sample_y[3] = {ecb->bottom_y, ecb->left_y, ecb->top_y};
    for (int i = 0; i < 3; i++) {
      float x_corr = 0.0f;
      float nx = 1.0f, ny = 0.0f;
      const int out_idx =
          right_wall_e684_project(g, start_line_idx, sample_x[i], sample_y[i], &x_corr, &nx, &ny);
      if (out_idx >= 0) {
        right_wall_envelope_consider(cur_pos_x + x_corr, out_idx, g, &best_x, &best_line_idx,
                                     &best_nx, &best_ny);
      }
    }

    int j = start_line_idx;
    for (size_t guard = 0; j >= 0 && (size_t)j < g->line_count && guard < g->line_count; guard++) {
      const MslStageWallLine* l = &g->lines[(size_t)j];
      const float vy = l->y1;
      float edge_x = 0.0f;
      if (bot <= vy && vy <= mid) {
        edge_x = lower_slope * (vy - bot) + bottom_x;
      } else if (mid <= vy && vy <= top) {
        edge_x = upper_slope * (vy - top) + top_x_rel;
      } else if (vy < bot) {
        break;
      } else {
        j = (int)l->next;
        continue;
      }
      right_wall_envelope_consider(l->x1 - edge_x, j, g, &best_x, &best_line_idx, &best_nx,
                                   &best_ny);
      j = (int)l->next;
    }

    j = start_line_idx;
    for (size_t guard = 0; j >= 0 && (size_t)j < g->line_count && guard < g->line_count; guard++) {
      const MslStageWallLine* l = &g->lines[(size_t)j];
      const float vy = l->y0;
      float edge_x = 0.0f;
      if (bot <= vy && vy <= mid) {
        edge_x = lower_slope * (vy - bot) + bottom_x;
      } else if (mid <= vy && vy <= top) {
        edge_x = upper_slope * (vy - top) + top_x_rel;
      } else if (vy > top) {
        break;
      } else {
        j = (int)l->prev;
        continue;
      }
      right_wall_envelope_consider(l->x0 - edge_x, j, g, &best_x, &best_line_idx, &best_nx,
                                   &best_ny);
      j = (int)l->prev;
    }
  }

  if (best_line_idx < 0 || !(best_x > cur_pos_x)) {
    return 0u;
  }
  *out_x = best_x;
  *out_line_idx = best_line_idx;
  *out_nx = best_nx;
  *out_ny = best_ny;
  return 1u;
}

// Decomp: mpColl ceiling-edge helper uses +/-1 offsets from the ceiling endpoint when probing for
// blocking walls before setting Collide_{Left,Right}Edge.
// refs/melee/src/melee/mp/mpcoll.c::mpColl_8004C328_Ceiling
static const float k_ceil_edge_wall_probe_x_offset = 1.0f;
static const float k_ceil_edge_wall_probe_y_offset = -1.0f;

static inline uint8_t wall_blocks_ceiling_edge_probe(const MslStageWallGraph* wg, float ax,
                                                     float ay, float bx, float by) {
  if (wg == NULL || wg->lines == NULL || wg->line_count == 0) {
    return 0;
  }
  for (size_t wi = 0; wi < wg->line_count; wi++) {
    const MslStageWallLine* w = &wg->lines[wi];
    float ix = 0.0f, iy = 0.0f;
    if (intersect_segment(w->x0, w->y0, w->x1, w->y1, ax, ay, bx, by, &ix, &iy)) {
      return 1;
    }
  }
  return 0;
}

static inline uint8_t ceiling_chain_endpoints(const MslStageCeilingGraph* g, int line_idx,
                                              float* out_left_x, float* out_left_y,
                                              float* out_right_x, float* out_right_y) {
  if (g == NULL || g->lines == NULL || g->line_count == 0) {
    return 0;
  }
  if (line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0;
  }

  // Ceiling line orientation is right->left (x0>=x1). prev links at the right endpoint, next
  // links at the left endpoint.
  int right_i = line_idx;
  for (size_t k = 0; k < g->line_count; k++) {
    const int16_t prev = g->lines[right_i].prev;
    if (prev < 0 || (size_t)prev >= g->line_count) {
      break;
    }
    right_i = (int)prev;
  }
  int left_i = line_idx;
  for (size_t k = 0; k < g->line_count; k++) {
    const int16_t next = g->lines[left_i].next;
    if (next < 0 || (size_t)next >= g->line_count) {
      break;
    }
    left_i = (int)next;
  }

  if (out_left_x) {
    *out_left_x = g->lines[left_i].x1;
  }
  if (out_left_y) {
    *out_left_y = g->lines[left_i].y1;
  }
  if (out_right_x) {
    *out_right_x = g->lines[right_i].x0;
  }
  if (out_right_y) {
    *out_right_y = g->lines[right_i].y0;
  }
  return 1;
}

static inline void ceiling_write_edge_suppression_flags(MslBatch* batch, size_t idx,
                                                        uint32_t stage_id,
                                                        const MslStageCeilingGraph* cg,
                                                        int line_idx,
                                                        const MslEcbWorldPoints* ecb) {
  if (batch == NULL || cg == NULL || ecb == NULL) {
    return;
  }
  float left_x = 0.0f, left_y = 0.0f, right_x = 0.0f, right_y = 0.0f;
  if (!ceiling_chain_endpoints(cg, line_idx, &left_x, &left_y, &right_x, &right_y)) {
    return;
  }

  const float fighter_x = batch->state.pos_x[idx];
  if (fighter_x <= left_x) {
    // Decomp: mpColl_8004C328_Ceiling uses mpCheckLeftWall(edge+{+1,-1}, ecb_right - ecb_top).
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004C328_Ceiling
    const float probe_ax = left_x + k_ceil_edge_wall_probe_x_offset;
    const float probe_ay = left_y + k_ceil_edge_wall_probe_y_offset;
    const float probe_bx = left_x + (ecb->right_rel_x /* top.x == 0 */);
    const float probe_by = left_y + (ecb->side_rel_y - ecb->top_rel_y);
    const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
    if (!wall_blocks_ceiling_edge_probe(lwg, probe_ax, probe_ay, probe_bx, probe_by)) {
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_RIGHT_EDGE;
      // Decomp parity: Collide_Edge is used as an aggregate “edge” marker (e.g. floor snap helper
      // mpColl_8004A678_Floor). Nothing consumes it yet in this sim, but setting it whenever we
      // set Collide_{Left,Right}Edge keeps the env flag surface closer to decomp.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A678_Floor
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_EDGE;
    }
  } else if (fighter_x >= right_x) {
    // Decomp: mpColl_8004C328_Ceiling uses mpCheckRightWall(edge+{-1,-1}, ecb_left - ecb_top).
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004C328_Ceiling
    const float probe_ax = right_x - k_ceil_edge_wall_probe_x_offset;
    const float probe_ay = right_y + k_ceil_edge_wall_probe_y_offset;
    const float probe_bx = right_x + (ecb->left_rel_x /* top.x == 0 */);
    const float probe_by = right_y + (ecb->side_rel_y - ecb->top_rel_y);
    const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);
    if (!wall_blocks_ceiling_edge_probe(rwg, probe_ax, probe_ay, probe_bx, probe_by)) {
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_LEFT_EDGE;
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A678_Floor
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_EDGE;
    }
  }
}

static inline void ecb_points_shift_x(MslEcbWorldPoints* ecb, float dx) {
  if (ecb == NULL || dx == 0.0f) {
    return;
  }
  ecb->bottom_x += dx;
  ecb->top_x += dx;
  ecb->left_x += dx;
  ecb->right_x += dx;
}

static inline void ecb_points_shift_x3(MslEcbWorldPoints* a, MslEcbWorldPoints* b,
                                       MslEcbWorldPoints* c, float dx) {
  ecb_points_shift_x(a, dx);
  ecb_points_shift_x(b, dx);
  ecb_points_shift_x(c, dx);
}

static inline void ecb_points_shift_y(MslEcbWorldPoints* ecb, float dy) {
  if (ecb == NULL || dy == 0.0f) {
    return;
  }
  ecb->bottom_y += dy;
  ecb->top_y += dy;
  ecb->left_y += dy;
  ecb->right_y += dy;
}

static inline void ecb_points_rebuild_x(MslEcbWorldPoints* ecb, float root_x) {
  if (ecb == NULL) {
    return;
  }
  ecb->bottom_x = root_x;
  ecb->top_x = root_x;
  ecb->left_x = root_x + ecb->left_rel_x;
  ecb->right_x = root_x + ecb->right_rel_x;
}

static inline void ecb_points_rebuild_y(MslEcbWorldPoints* ecb, float root_y) {
  if (ecb == NULL) {
    return;
  }
  ecb->bottom_y = root_y + ecb->bottom_rel_y;
  ecb->top_y = root_y + ecb->top_rel_y;
  ecb->left_y = root_y + ecb->side_rel_y;
  ecb->right_y = root_y + ecb->side_rel_y;
}

static uint8_t ceiling_try_adjacent_from_air_wall(MslBatch* batch, size_t idx, uint32_t stage_id,
                                                  const MslStageCeilingGraph* cg,
                                                  const MslEcbWorldPoints* ceiling_ecb) {
  if (batch == NULL || cg == NULL || ceiling_ecb == NULL || batch->state.wall_id[idx] == 0xFFFFu) {
    return 0u;
  }

  // Source `mpColl_80044AD8_Ceiling`: after airborne left/right wall resolution, the ceiling
  // check walks the raw MapLine graph from the contacted wall to the first non-wall neighbor.
  // This catches underside shell corners even when the direct top-point sweep misses the ceiling
  // segment itself.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044AD8_Ceiling
  MslStageRawLineKind out_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
  uint16_t ceiling_segment_i = 0xFFFFu;
  if (batch->state.wall_kind[idx] == MSL_WALL_LEFT) {
    if (!stage_collision_raw_line_next_non_kind(stage_id, batch->state.wall_id[idx],
                                                MSL_STAGE_RAW_LINE_LEFT_WALL, &out_kind,
                                                &ceiling_segment_i) ||
        out_kind != MSL_STAGE_RAW_LINE_CEILING) {
      return 0u;
    }
  } else if (batch->state.wall_kind[idx] == MSL_WALL_RIGHT) {
    if (!stage_collision_raw_line_prev_non_kind(stage_id, batch->state.wall_id[idx],
                                                MSL_STAGE_RAW_LINE_RIGHT_WALL, &out_kind,
                                                &ceiling_segment_i) ||
        out_kind != MSL_STAGE_RAW_LINE_CEILING) {
      return 0u;
    }
  } else {
    return 0u;
  }

  const int ceiling_line_idx = stage_collision_ceiling_line_index(stage_id, ceiling_segment_i);
  if (ceiling_line_idx < 0) {
    return 0u;
  }
  const float cur_tx = ceiling_ecb->top_x;
  const float cur_ty = ceiling_ecb->top_y;
  float y_corr = 0.0f;
  float nx = 0.0f;
  float ny = -1.0f;
  const int out_line_idx =
      ceiling_e090_project(cg, ceiling_line_idx, cur_tx, cur_ty, &y_corr, &nx, &ny);
  if (out_line_idx < 0 || !(y_corr < 0.0f)) {
    return 0u;
  }

  batch->state.pos_y[idx] += y_corr;
  batch->state.ceiling_id[idx] = cg->lines[(size_t)out_line_idx].segment_i;
  batch->state.ceiling_contact_x[idx] = cur_tx;
  batch->state.ceiling_contact_y[idx] = cur_ty + y_corr;
  batch->state.ceiling_normal_x[idx] = nx;
  batch->state.ceiling_normal_y[idx] = ny;
  batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_CEILING_PUSH;
  ceiling_write_edge_suppression_flags(batch, idx, stage_id, cg, out_line_idx, ceiling_ecb);
  return 1u;
}

static void mpcoll_squeeze_horizontal(MslBatch* batch, size_t idx, MslEcbWorldPoints* cur_ecb,
                                      float left, float right) {
  if (batch == NULL || cur_ecb == NULL) {
    return;
  }
  // Source `mpCollSqueezeHorizontal`: when both side walls collided in the same pass, shrink the
  // current ECB width around the remaining gap and recenter the root. The persistent x34/x64 ECB
  // storage is not modeled as a public seed lane yet, so this applies the source displacement and
  // carries the squeezed local ECB through the rest of this callback.
  // refs/melee/src/melee/mp/mpcoll.c::mpCollSqueezeHorizontal
  const float half_width = 0.5f * (right - left + cur_ecb->right_rel_x - cur_ecb->left_rel_x);
  const float root_x = (right + cur_ecb->right_rel_x) - half_width;
  batch->state.pos_x[idx] = root_x;
  cur_ecb->right_rel_x = half_width;
  cur_ecb->left_rel_x = -half_width;
  ecb_points_rebuild_x(cur_ecb, root_x);
}

static void mpcoll_squeeze_vertical(MslBatch* batch, size_t idx, MslEcbWorldPoints* cur_ecb,
                                    uint8_t airborne, float top, float bottom) {
  if (batch == NULL || cur_ecb == NULL) {
    return;
  }
  // Source `mpCollSqueezeVertical`: floor+ceiling in the same pass shrink/recenter the current ECB
  // according to grounded-vs-airborne ownership. The local ECB mutation feeds only the remainder of
  // this callback; no heap or replay-future seed lane is introduced.
  // refs/melee/src/melee/mp/mpcoll.c::mpCollSqueezeVertical
  const float height = top - bottom + cur_ecb->top_rel_y - cur_ecb->bottom_rel_y;
  if (height < 3.0f) {
    const float old_height = cur_ecb->top_rel_y - cur_ecb->bottom_rel_y;
    const float new_height = cur_ecb->top_rel_y + top - bottom;
    cur_ecb->top_rel_y = fminf(old_height, new_height);
    cur_ecb->bottom_rel_y = 0.0f;
    batch->state.pos_y[idx] = bottom;
  } else if (!airborne) {
    batch->state.pos_y[idx] = bottom;
    cur_ecb->top_rel_y = height + cur_ecb->bottom_rel_y;
  } else {
    batch->state.pos_y[idx] = 0.5f * (top + bottom);
    cur_ecb->top_rel_y = 0.5f * (cur_ecb->top_rel_y + cur_ecb->bottom_rel_y + height);
    cur_ecb->bottom_rel_y = cur_ecb->top_rel_y - height;
  }
  ecb_points_rebuild_y(cur_ecb, batch->state.pos_y[idx]);
}

static inline void ecb_points_apply_inline_horizontal_normalization(const MslBatch* batch,
                                                                    size_t idx,
                                                                    MslEcbWorldPoints* ecb) {
  if (batch == NULL || ecb == NULL) {
    return;
  }
  // Grounded ordered helpers consume the same CollData ECB loaded by mpColl_LoadECB_inline as the
  // standalone wall/ceiling pass: narrow horizontal spans are recentered, then clamped to the
  // source +/-2 minimum. `ecb->bottom_x` is the fighter root X because desired_ecb.bottom.x is 0.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_LoadECB_JObj}
  float left_rel_x = ecb->left_rel_x;
  float right_rel_x = ecb->right_rel_x;
  const float min_ecb_width = fmaxf(4.0f, 10.0f * batch->state.fighter_scale_y[idx]);
  const float ecb_width = fabsf(right_rel_x - left_rel_x);
  if (ecb_width < min_ecb_width) {
    const float half_width = 0.5f * ecb_width;
    left_rel_x = -half_width;
    right_rel_x = half_width;
  }
  if (right_rel_x < 2.0f) {
    right_rel_x = 2.0f;
  }
  if (left_rel_x > -2.0f) {
    left_rel_x = -2.0f;
  }
  const float root_x = ecb->bottom_x;
  ecb->left_rel_x = left_rel_x;
  ecb->right_rel_x = right_rel_x;
  ecb->left_x = root_x + left_rel_x;
  ecb->right_x = root_x + right_rel_x;
}

static uint8_t grounded_ordered_left_wall(MslBatch* batch, size_t idx, const MslStageFloorGraph* fg,
                                          const MslStageWallGraph* lwg,
                                          const MslEcbWorldPoints* prev_ecb,
                                          MslEcbWorldPoints* cur_ecb, uint16_t* wall_id_out,
                                          float* x_after_out) {
  if (batch == NULL || lwg == NULL || lwg->lines == NULL || prev_ecb == NULL || cur_ecb == NULL ||
      wall_id_out == NULL) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const int excluded_floor_wall =
      grounded_left_wall_floor_adjacent_line_idx(fg, lwg, stage_id, batch->state.ground_id[idx]);

  MslWallCandidateList candidates;
  wall_candidate_list_init(&candidates);
  // Grounded source candidate collection:
  // mpColl_80049778_LeftWall gathers side, bottom, top, current side-edge, and swept side-edge
  // candidates before mpColl_80049EAC_LeftWall resolves the full ECB envelope.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80049778_LeftWall,mpColl_80049EAC_LeftWall}
  left_wall_candidate_sweep(&candidates, lwg, prev_ecb->right_x, prev_ecb->right_y,
                            cur_ecb->right_x, cur_ecb->right_y, -1, 1u, -1);
  left_wall_candidate_sweep(&candidates, lwg, prev_ecb->bottom_x, prev_ecb->bottom_y,
                            cur_ecb->bottom_x, cur_ecb->bottom_y, -1, 0u, excluded_floor_wall);
  left_wall_candidate_sweep(&candidates, lwg, prev_ecb->top_x, prev_ecb->top_y, cur_ecb->top_x,
                            cur_ecb->top_y, -1, 0u, -1);
  left_wall_candidate_sweep(&candidates, lwg, cur_ecb->bottom_x, cur_ecb->bottom_y,
                            cur_ecb->right_x, cur_ecb->right_y, -1, 0u, excluded_floor_wall);
  left_wall_candidate_quad(&candidates, lwg, prev_ecb->bottom_x, prev_ecb->bottom_y,
                           prev_ecb->right_x, prev_ecb->right_y, cur_ecb->bottom_x,
                           cur_ecb->bottom_y, cur_ecb->right_x, cur_ecb->right_y);
  left_wall_candidate_sweep(&candidates, lwg, cur_ecb->top_x, cur_ecb->top_y, cur_ecb->right_x,
                            cur_ecb->right_y, -1, 0u, -1);
  left_wall_candidate_quad(&candidates, lwg, prev_ecb->right_x, prev_ecb->right_y, prev_ecb->top_x,
                           prev_ecb->top_y, cur_ecb->right_x, cur_ecb->right_y, cur_ecb->top_x,
                           cur_ecb->top_y);
  wall_candidate_list_remove_connected(&candidates, lwg, excluded_floor_wall);

  float envelope_x = 0.0f;
  int envelope_line_idx = -1;
  float envelope_nx = -1.0f;
  float envelope_ny = 0.0f;
  if (left_wall_air_envelope_min_x(lwg, &candidates, cur_ecb, batch->state.pos_x[idx],
                                   batch->state.pos_y[idx], &envelope_x, &envelope_line_idx,
                                   &envelope_nx, &envelope_ny)) {
    const float dx = envelope_x - batch->state.pos_x[idx];
    batch->state.pos_x[idx] = envelope_x;
    ecb_points_shift_x(cur_ecb, dx);
    batch->state.wall_kind[idx] = MSL_WALL_LEFT;
    batch->state.wall_id[idx] = lwg->lines[(size_t)envelope_line_idx].segment_i;
    batch->state.wall_contact_x[idx] = candidates.first_ix;
    batch->state.wall_contact_y[idx] = candidates.first_iy;
    batch->state.wall_normal_x[idx] = envelope_nx;
    batch->state.wall_normal_y[idx] = envelope_ny;
    mark_left_wall_contact(batch, idx, candidates.has_hug);
    *wall_id_out = batch->state.wall_id[idx];
    if (x_after_out != NULL) {
      *x_after_out = batch->state.pos_x[idx];
    }
    return 1u;
  }
  for (uint8_t ci = 0u; ci < candidates.count; ci++) {
    const int candidate_line_idx = candidates.line_idx[ci];
    const float sample_x[3] = {cur_ecb->right_x, cur_ecb->bottom_x, cur_ecb->top_x};
    const float sample_y[3] = {cur_ecb->right_y, cur_ecb->bottom_y, cur_ecb->top_y};
    for (size_t pass = 0; pass < 3u; pass++) {
      float x_corr = 0.0f;
      float nx = -1.0f;
      float ny = 0.0f;
      const int out_line_idx = left_wall_e398_project(lwg, candidate_line_idx, sample_x[pass],
                                                      sample_y[pass], &x_corr, &nx, &ny);
      if (out_line_idx < 0) {
        continue;
      }
      if (x_corr > 0.0f) {
        x_corr = 0.0f;
      }
      batch->state.pos_x[idx] += x_corr;
      ecb_points_shift_x(cur_ecb, x_corr);
      batch->state.wall_kind[idx] = MSL_WALL_LEFT;
      batch->state.wall_id[idx] = lwg->lines[(size_t)out_line_idx].segment_i;
      batch->state.wall_contact_x[idx] = candidates.first_ix;
      batch->state.wall_contact_y[idx] = candidates.first_iy;
      batch->state.wall_normal_x[idx] = nx;
      batch->state.wall_normal_y[idx] = ny;
      mark_left_wall_contact(batch, idx, candidates.has_hug);
      *wall_id_out = batch->state.wall_id[idx];
      if (x_after_out != NULL) {
        *x_after_out = batch->state.pos_x[idx];
      }
      return 1u;
    }
  }
  return 0u;
}

static uint8_t grounded_ordered_right_wall(MslBatch* batch, size_t idx,
                                           const MslStageFloorGraph* fg,
                                           const MslStageWallGraph* rwg,
                                           const MslEcbWorldPoints* prev_ecb,
                                           MslEcbWorldPoints* cur_ecb, uint16_t* wall_id_out,
                                           float* x_after_out) {
  if (batch == NULL || rwg == NULL || rwg->lines == NULL || prev_ecb == NULL || cur_ecb == NULL ||
      wall_id_out == NULL) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const int excluded_floor_wall =
      grounded_right_wall_floor_adjacent_line_idx(fg, rwg, stage_id, batch->state.ground_id[idx]);

  MslWallCandidateList candidates;
  wall_candidate_list_init(&candidates);
  // Symmetric grounded source candidate collection:
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80048AB0_RightWall,mpColl_800491C8_RightWall}
  right_wall_candidate_sweep(&candidates, rwg, prev_ecb->left_x, prev_ecb->left_y, cur_ecb->left_x,
                             cur_ecb->left_y, -1, 1u, -1);
  right_wall_candidate_sweep(&candidates, rwg, prev_ecb->bottom_x, prev_ecb->bottom_y,
                             cur_ecb->bottom_x, cur_ecb->bottom_y, -1, 0u, excluded_floor_wall);
  right_wall_candidate_sweep(&candidates, rwg, prev_ecb->top_x, prev_ecb->top_y, cur_ecb->top_x,
                             cur_ecb->top_y, -1, 0u, -1);
  right_wall_candidate_sweep(&candidates, rwg, cur_ecb->bottom_x, cur_ecb->bottom_y,
                             cur_ecb->left_x, cur_ecb->left_y, -1, 0u, excluded_floor_wall);
  right_wall_candidate_quad(&candidates, rwg, prev_ecb->bottom_x, prev_ecb->bottom_y,
                            prev_ecb->left_x, prev_ecb->left_y, cur_ecb->bottom_x,
                            cur_ecb->bottom_y, cur_ecb->left_x, cur_ecb->left_y);
  right_wall_candidate_sweep(&candidates, rwg, cur_ecb->top_x, cur_ecb->top_y, cur_ecb->left_x,
                             cur_ecb->left_y, -1, 0u, -1);
  right_wall_candidate_quad(&candidates, rwg, prev_ecb->left_x, prev_ecb->left_y, prev_ecb->top_x,
                            prev_ecb->top_y, cur_ecb->left_x, cur_ecb->left_y, cur_ecb->top_x,
                            cur_ecb->top_y);
  wall_candidate_list_remove_connected(&candidates, rwg, excluded_floor_wall);

  float envelope_x = 0.0f;
  int envelope_line_idx = -1;
  float envelope_nx = 1.0f;
  float envelope_ny = 0.0f;
  if (right_wall_air_envelope_max_x(rwg, &candidates, cur_ecb, batch->state.pos_x[idx],
                                    batch->state.pos_y[idx], &envelope_x, &envelope_line_idx,
                                    &envelope_nx, &envelope_ny)) {
    const float dx = envelope_x - batch->state.pos_x[idx];
    batch->state.pos_x[idx] = envelope_x;
    ecb_points_shift_x(cur_ecb, dx);
    batch->state.wall_kind[idx] = MSL_WALL_RIGHT;
    batch->state.wall_id[idx] = rwg->lines[(size_t)envelope_line_idx].segment_i;
    batch->state.wall_contact_x[idx] = candidates.first_ix;
    batch->state.wall_contact_y[idx] = candidates.first_iy;
    batch->state.wall_normal_x[idx] = envelope_nx;
    batch->state.wall_normal_y[idx] = envelope_ny;
    mark_right_wall_contact(batch, idx, candidates.has_hug);
    *wall_id_out = batch->state.wall_id[idx];
    if (x_after_out != NULL) {
      *x_after_out = batch->state.pos_x[idx];
    }
    return 1u;
  }
  for (uint8_t ci = 0u; ci < candidates.count; ci++) {
    const int candidate_line_idx = candidates.line_idx[ci];
    const float sample_x[3] = {cur_ecb->left_x, cur_ecb->bottom_x, cur_ecb->top_x};
    const float sample_y[3] = {cur_ecb->left_y, cur_ecb->bottom_y, cur_ecb->top_y};
    for (size_t pass = 0; pass < 3u; pass++) {
      float x_corr = 0.0f;
      float nx = 1.0f;
      float ny = 0.0f;
      const int out_line_idx = right_wall_e684_project(rwg, candidate_line_idx, sample_x[pass],
                                                       sample_y[pass], &x_corr, &nx, &ny);
      if (out_line_idx < 0) {
        continue;
      }
      if (x_corr < 0.0f) {
        x_corr = 0.0f;
      }
      batch->state.pos_x[idx] += x_corr;
      ecb_points_shift_x(cur_ecb, x_corr);
      batch->state.wall_kind[idx] = MSL_WALL_RIGHT;
      batch->state.wall_id[idx] = rwg->lines[(size_t)out_line_idx].segment_i;
      batch->state.wall_contact_x[idx] = candidates.first_ix;
      batch->state.wall_contact_y[idx] = candidates.first_iy;
      batch->state.wall_normal_x[idx] = nx;
      batch->state.wall_normal_y[idx] = ny;
      mark_right_wall_contact(batch, idx, candidates.has_hug);
      *wall_id_out = batch->state.wall_id[idx];
      if (x_after_out != NULL) {
        *x_after_out = batch->state.pos_x[idx];
      }
      return 1u;
    }
  }
  return 0u;
}

uint8_t mpcoll_grounded_ceiling_ordered_retry(const MslMpcollContext* ctx,
                                              const MslEcbWorldPoints* prev_ecb,
                                              MslEcbWorldPoints* cur_ecb,
                                              MslMpcollOrderedWallCeilResult* io) {
  if (ctx == NULL || ctx->batch == NULL || prev_ecb == NULL || cur_ecb == NULL) {
    return 0u;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  const uint32_t stage_id = ctx->stage_id;
  const MslStageCeilingGraph* cg = ctx->ceiling_graph;
  if (cg == NULL || cg->lines == NULL || cg->line_count == 0u) {
    return 0u;
  }

  int prefer_line_idx = -1;
  if (batch->state.ceiling_id[idx] != 0xFFFFu) {
    prefer_line_idx = stage_collision_ceiling_line_index(stage_id, batch->state.ceiling_id[idx]);
  }

  int hit_line_idx = -1;
  float ix = 0.0f;
  float iy = 0.0f;
  float nx = 0.0f;
  float ny = -1.0f;
  uint8_t hit_ceiling =
      ceiling_sweep_check(cg, prev_ecb->top_x, prev_ecb->top_y, cur_ecb->top_x, cur_ecb->top_y,
                          prefer_line_idx, &hit_line_idx, &ix, &iy, &nx, &ny);
  if (!hit_ceiling && io != NULL) {
    // Source `mpColl_80044AD8_Ceiling`: when the direct top sweep misses, same-frame wall side
    // bits can walk the raw MapLine graph from the contacted wall to an adjacent ceiling.
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044AD8_Ceiling
    MslStageRawLineKind out_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
    uint16_t ceiling_segment_i = 0xFFFFu;
    if ((io->left_right_flags & 1u) != 0u && io->left_wall_id != 0xFFFFu &&
        stage_collision_raw_line_next_non_kind(stage_id, io->left_wall_id,
                                               MSL_STAGE_RAW_LINE_LEFT_WALL, &out_kind,
                                               &ceiling_segment_i) &&
        out_kind == MSL_STAGE_RAW_LINE_CEILING) {
      hit_line_idx = stage_collision_ceiling_line_index(stage_id, ceiling_segment_i);
      hit_ceiling = (hit_line_idx >= 0) ? 1u : 0u;
      ix = cur_ecb->top_x;
      iy = cur_ecb->top_y;
    } else if ((io->left_right_flags & 2u) != 0u && io->right_wall_id != 0xFFFFu &&
               stage_collision_raw_line_prev_non_kind(stage_id, io->right_wall_id,
                                                      MSL_STAGE_RAW_LINE_RIGHT_WALL, &out_kind,
                                                      &ceiling_segment_i) &&
               out_kind == MSL_STAGE_RAW_LINE_CEILING) {
      hit_line_idx = stage_collision_ceiling_line_index(stage_id, ceiling_segment_i);
      hit_ceiling = (hit_line_idx >= 0) ? 1u : 0u;
      ix = cur_ecb->top_x;
      iy = cur_ecb->top_y;
    }
  }
  if (!hit_ceiling) {
    if (io != NULL && io->hit_floor && io->hit_ceiling) {
      mpcoll_squeeze_vertical(batch, idx, cur_ecb, (uint8_t)(io->touching_floor ? 0u : 1u),
                              io->y_after_ceiling, io->y_after_floor);
      io->cur_ecb_after = *cur_ecb;
      io->squeeze_flags_all |= io->squeeze_flags;
    }
    return 0u;
  }
  float y_corr = 0.0f;
  const int out_line_idx =
      ceiling_e090_project(cg, hit_line_idx, cur_ecb->top_x, cur_ecb->top_y, &y_corr, &nx, &ny);
  if (out_line_idx < 0) {
    return 0u;
  }
  if (y_corr > 0.0f) {
    y_corr = 0.0f;
  }
  batch->state.pos_y[idx] += y_corr;
  ecb_points_shift_y(cur_ecb, y_corr);
  batch->state.ceiling_id[idx] = cg->lines[(size_t)out_line_idx].segment_i;
  batch->state.ceiling_contact_x[idx] = ix;
  batch->state.ceiling_contact_y[idx] = iy;
  batch->state.ceiling_normal_x[idx] = nx;
  batch->state.ceiling_normal_y[idx] = ny;
  batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_CEILING_MASK;
  ceiling_write_edge_suppression_flags(batch, idx, stage_id, cg, out_line_idx, cur_ecb);
  if (io != NULL) {
    io->hit_ceiling = 1u;
    io->squeeze_flags |= (uint8_t)MSL_MPCOLL_ORDERED_SQUEEZE_CEILING;
    io->y_after_ceiling = batch->state.pos_y[idx];
    if (io->hit_floor) {
      mpcoll_squeeze_vertical(batch, idx, cur_ecb, (uint8_t)(io->touching_floor ? 0u : 1u),
                              io->y_after_ceiling, io->y_after_floor);
    }
    io->cur_ecb_after = *cur_ecb;
    io->squeeze_flags_all |= io->squeeze_flags;
  }
  return 1u;
}

void mpcoll_grounded_wall_ceil_ordered_begin(const MslMpcollContext* ctx,
                                             const MslEcbWorldPoints* prev_ecb,
                                             const MslEcbWorldPoints* cur_ecb,
                                             MslMpcollOrderedWallCeilResult* out) {
  MslMpcollOrderedWallCeilResult result = {
      .left_right_flags = 0u,
      .squeeze_flags = 0u,
      .squeeze_flags_all = 0u,
      .hit_ceiling = 0u,
      .hit_floor = 0u,
      .touching_floor = 0u,
      .left_wall_id = 0xFFFFu,
      .right_wall_id = 0xFFFFu,
      .x_after_left_wall = 0.0f,
      .x_after_right_wall = 0.0f,
      .y_after_ceiling = 0.0f,
      .y_after_floor = 0.0f,
      .cur_ecb_after = {0},
  };
  if (out != NULL) {
    *out = result;
  }
  if (ctx == NULL || ctx->batch == NULL || prev_ecb == NULL || cur_ecb == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  const MslStageFloorGraph* fg = ctx->floor_graph;
  const MslStageWallGraph* lwg = ctx->left_wall_graph;
  const MslStageWallGraph* rwg = ctx->right_wall_graph;

  // Source order for grounded inline2:
  // left wall, right wall, left wall retry, right wall retry, horizontal squeeze, then ceiling.
  // This retained helper models the same left/right observation order and feeds the floor pass the
  // wall side bits used by mpColl_80044628_Floor's raw adjacent-line fallback.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
  MslEcbWorldPoints cur = *cur_ecb;
  MslEcbWorldPoints prev = *prev_ecb;
  ecb_points_apply_inline_horizontal_normalization(batch, idx, &cur);
  ecb_points_apply_inline_horizontal_normalization(batch, idx, &prev);
  uint8_t hit_left = 0u;
  uint8_t hit_right = 0u;
  if (grounded_ordered_left_wall(batch, idx, fg, lwg, &prev, &cur, &result.left_wall_id,
                                 &result.x_after_left_wall)) {
    hit_left = 1u;
    result.left_right_flags |= 1u;
    result.squeeze_flags |= (uint8_t)MSL_MPCOLL_ORDERED_SQUEEZE_LEFT_WALL;
  }
  if (grounded_ordered_right_wall(batch, idx, fg, rwg, &prev, &cur, &result.right_wall_id,
                                  &result.x_after_right_wall)) {
    hit_right = 1u;
    result.left_right_flags |= 2u;
    result.squeeze_flags |= (uint8_t)MSL_MPCOLL_ORDERED_SQUEEZE_RIGHT_WALL;
  }
  if (grounded_ordered_left_wall(batch, idx, fg, lwg, &prev, &cur, &result.left_wall_id,
                                 &result.x_after_left_wall)) {
    hit_left = 1u;
    result.left_right_flags |= 1u;
    result.squeeze_flags |= (uint8_t)MSL_MPCOLL_ORDERED_SQUEEZE_LEFT_WALL;
  }
  if (grounded_ordered_right_wall(batch, idx, fg, rwg, &prev, &cur, &result.right_wall_id,
                                  &result.x_after_right_wall)) {
    hit_right = 1u;
    result.left_right_flags |= 2u;
    result.squeeze_flags |= (uint8_t)MSL_MPCOLL_ORDERED_SQUEEZE_RIGHT_WALL;
  }

  if (hit_left && hit_right) {
    mpcoll_squeeze_horizontal(batch, idx, &cur, result.x_after_right_wall,
                              result.x_after_left_wall);
  }

  (void)mpcoll_grounded_ceiling_ordered_retry(ctx, &prev, &cur, &result);
  result.cur_ecb_after = cur;
  result.squeeze_flags_all |= result.squeeze_flags;
  if (out != NULL) {
    *out = result;
  }
}

void mpcoll_wall_ceil_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];
    const MslStageFloorGraph* fg = stage_collision_get_floor_graph(stage_id);
    const MslStageCeilingGraph* cg = stage_collision_get_ceiling_graph(stage_id);
    const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
    const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      MslMpcollContext ctx = mpcoll_context_make(batch, bi, idx, stage_id, fg, cg, lwg, rwg);
      const uint16_t action_id = ctx.action_id;

      const uint8_t prev_wall_kind = batch->state.wall_kind[idx];
      const uint16_t prev_wall_id = batch->state.wall_id[idx];
      const uint16_t prev_ceiling_id = batch->state.ceiling_id[idx];
      const uint8_t prev_had_ceiling =
          (batch->state.coll_prev_env_flags[idx] & (uint32_t)MSL_COLLIDE_CEILING_MASK) ? 1u : 0u;

      // Decomp: hitlag gates Anim/IASA/Phys in `Fighter_8006A360`, but `Fighter_procMap` still
      // calls each fighter's collision callback. Refresh wall/ceiling metadata during hitlag so
      // stale SpecialHi Hug bits cannot survive into DamageFly_Coll on the hitlag-exit frame.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procMap}
      if (!match_flow_should_stage_collide(action_id)) {
        // Match-flow states (Dead*/Rebirth*/Entry*) do not run the generic stage-collision
        // callbacks. Treat this as a CollData provenance reset, not just a visible contact clear,
        // so a stale wall/ceiling index from the pre-death action cannot arm persistence when
        // RebirthWait exits back to Fall.
        //
        // Source shape:
        // - Dead* -> Rebirth runs Fighter_UnkProcessDeath_80068354 /
        //   Fighter_UnkInitReset_80067C98 before Rebirth.
        // - Rebirth/RebirthWait have dedicated collision callbacks, not the common Fall collision
        //   persistence path.
        // refs/melee/src/melee/ft/fighter.c::{
        //   Fighter_UnkProcessDeath_80068354,Fighter_UnkInitReset_80067C98}
        // refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_Rebirth_Coll,ftCo_RebirthWait_Coll}
        mpcoll_clear_wall_ceiling_and_env_provenance(batch, idx);
        continue;
      }
      if (is_cliff_hold_action(action_id)) {
        mpcoll_clear_wall_ceiling_contacts(batch, idx);
        continue;
      }
      if (is_grounded_cliff_option_action(action_id, batch->state.on_ground[idx])) {
        mpcoll_clear_wall_ceiling_contacts(batch, idx);
        continue;
      }
      if (msl_action_is_thrown_victim(action_id)) {
        const uint8_t owner = batch->state.grab_owner_port[idx];
        if (owner != 0xFFu && owner < (uint8_t)num_players && owner != (uint8_t)p) {
          // Common Thrown* states have empty Coll callbacks while attached, so wall/ceiling/edge
          // contact metadata is not stage-owned during the attached window.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{
          //   ftCo_ThrownF_Coll,ftCo_ThrownB_Coll,ftCo_ThrownHi_Coll,ftCo_ThrownLw_Coll
          // }
          mpcoll_clear_wall_ceiling_provenance(batch, idx);
          continue;
        }
      }
      if (throw_flow_release_pending_for_victim(batch, bi, p)) {
        mpcoll_clear_wall_ceiling_provenance(batch, idx);
        continue;
      }

      const uint8_t was_grounded = batch->state.prev_on_ground[idx] ? 1u : 0u;
      if (was_grounded) {
        // Grounded inline2 callbacks are handled in source order inside mpcoll_ground_apply:
        // walls -> ceiling -> floor -> ceiling retry -> 4A908/44838. Re-running the generic
        // wall/ceiling pass here would observe post-floor positions and overwrite the shared
        // same-frame scratch/result.
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
        continue;
      }
      const uint8_t grounded_now = batch->state.on_ground[idx] ? 1u : 0u;
      const uint8_t char_id = ctx.char_id;
      const uint32_t anim = ctx.anim;
      const uint8_t damagefly_hitlag_wall_refresh =
          (uint8_t)(mpcoll_damagefly_wall_asdi_latch_action(action_id) &&
                    mpcoll_frozen_hitlag_phase(batch, idx));
      const uint16_t ecb_frame =
          msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
      const uint16_t ecb_frame_prev = msl_ecb_prev_frame_u16(ecb_frame);
      const int grounded_left_floor_adj_line_idx =
          grounded_now ? grounded_left_wall_floor_adjacent_line_idx(fg, lwg, stage_id,
                                                                    batch->state.ground_id[idx])
                       : -1;
      const int grounded_right_floor_adj_line_idx =
          grounded_now ? grounded_right_wall_floor_adjacent_line_idx(fg, rwg, stage_id,
                                                                     batch->state.ground_id[idx])
                       : -1;

      const float prev_x = batch->state.prev_pos_x[idx];
      const float prev_y = batch->state.prev_pos_y[idx];
      const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;

      MslEcbWorldPoints cur_ecb = {0};
      MslEcbWorldPoints prev_ecb = {0};
      // Decomp: wall and ceiling collision consume ECB-derived points:
      // - wall: side point (left/right extent at side_y)
      // - ceiling: top point
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_Fixed
      sample_collision_ecb_points(&cur_ecb, batch, idx, char_id, anim, action_id, ecb_frame, fd,
                                  batch->state.pos_x[idx], batch->state.pos_y[idx], was_grounded);
      sample_collision_ecb_points(&prev_ecb, batch, idx, char_id, anim, action_id, ecb_frame_prev,
                                  fd, prev_x, prev_y, was_grounded);
      const uint8_t nonfastfall_fall_generic_lock_bottom =
          (uint8_t)(action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] == 0u &&
                    batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 1u);
      if (!was_grounded && batch->state.ecb_lock_timer[idx] != 0u &&
          batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
          batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u &&
          !nonfastfall_fall_generic_lock_bottom) {
        // Source `mpColl_LoadECB_inline` preserves CollData.desired_ecb.bottom while
        // CollData_X130_Locked is live. Wall/ceiling callbacks consume the same loaded ECB as floor
        // callbacks for the retained locked-bottom family. Keep generic owner-1 non-fastfall Fall
        // out of this wall/ceiling preserve path: the floor owner slice above already excludes
        // non-fastfall Fall, and carrying the generic seed/runtime owner here stale-lifts the
        // current bottom ECB after the source lock has expired.
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80045B74_LeftWall}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
        msl_ecb_world_points_preserve_desired_bottom_rel_y(
            &cur_ecb, batch->state.pos_x[idx], batch->state.pos_y[idx],
            batch->state.coll_desired_ecb_bottom_rel_y[idx]);
      }
      MslEcbWorldPoints cur_right_ecb = cur_ecb;
      MslEcbWorldPoints prev_right_ecb = prev_ecb;
      MslEcbWorldPoints cur_specialhi_wall_ecb = cur_ecb;
      MslEcbWorldPoints prev_specialhi_wall_ecb = prev_ecb;
      const uint8_t use_common_air_walljump_callback =
          mpcoll_action_uses_common_air_walljump_callback(action_id);
      if (specialhi_launch_uses_runtime_xrotn_ecb(char_id, action_id)) {
        // SpecialHi uses a live JObj ECB source while the launch model rotates around XRotN.
        // The same sampled CollData.ecb feeds both wall sides; the right-wall path below was the
        // first retained slice, and the SpecialAirHi left-wall envelope now consumes the same
        // scoped ECB basis instead of a static-table proxy.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{
        //   mpColl_LoadECB_JObj,mpColl_80044E10_RightWall,mpColl_80045B74_LeftWall}
        const uint8_t have_cur_specialhi_ecb = try_sample_jobj_ecb_points(
            &cur_specialhi_wall_ecb, batch, idx, char_id, anim, action_id, ecb_frame, fd,
            batch->state.pos_x[idx], batch->state.pos_y[idx]);
        const uint8_t have_prev_specialhi_ecb =
            try_sample_jobj_ecb_points(&prev_specialhi_wall_ecb, batch, idx, char_id, anim,
                                       action_id, ecb_frame_prev, fd, prev_x, prev_y);
        if (have_cur_specialhi_ecb) {
          cur_right_ecb = cur_specialhi_wall_ecb;
        }
        if (have_prev_specialhi_ecb) {
          prev_right_ecb = prev_specialhi_wall_ecb;
        }
      }

      // Reset per-frame wall/ceiling contact outputs; ids persist when detached (mirrors floor_id behavior).
      batch->state.wall_kind[idx] = 0;
      batch->state.wall_contact_x[idx] = 0.0f;
      batch->state.wall_contact_y[idx] = 0.0f;
      batch->state.wall_normal_x[idx] = 0.0f;
      batch->state.wall_normal_y[idx] = 0.0f;
      batch->state.ceiling_contact_x[idx] = 0.0f;
      batch->state.ceiling_contact_y[idx] = 0.0f;
      batch->state.ceiling_normal_x[idx] = 0.0f;
      batch->state.ceiling_normal_y[idx] = 0.0f;

      // Left wall collision (hit by the fighter's right ECB side).
      if (lwg && lwg->lines && lwg->line_count) {
        // Decomp: mpLib_8004E398_LeftWall consumes a (x,y) point; mpColl passes ECB side points.
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046224_LeftWall
        const uint8_t use_specialhi_left_envelope =
            specialhi_launch_uses_runtime_xrotn_ecb(char_id, action_id);
        // DamageFly's Coll callback enters the left-wall airborne envelope through mpColl. Source
        // `mpColl_80045B74_LeftWall` is not attack-velocity-sign gated, but the current simulator's
        // static wall candidate model still over-publishes left-wall contact on some non-Yoshi rows
        // when negative attack velocity is admitted generally. Keep the retained slice to the
        // validated positive-kb FD path plus Yoshi's transformed lower-left wall until the candidate
        // model carries the full source envelope/order. Hug still comes only from the candidate side
        // sweep below.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80045B74_LeftWall,mpColl_80046224_LeftWall}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::ftCo_800C15F4
        const uint8_t use_damagefly_left_envelope =
            (uint8_t)(mpcoll_damagefly_wall_asdi_latch_action(action_id) &&
                      (batch->state.stage_id[bi] == (uint32_t)MSL_STAGE_YOSHIS_STORY_LOCAL ||
                       batch->state.speed_x_attack[idx] > 0.0f));
        const uint8_t use_common_air_left_envelope = use_common_air_walljump_callback;
        const uint8_t use_ft80081d0c_left_envelope =
            mpcoll_action_uses_ft80081d0c_air_collision(action_id);
        const uint8_t use_ft_check_ground_ledge_air_left_envelope =
            mpcoll_action_uses_ft_check_ground_ledge_air_collision(char_id, action_id);
        const uint8_t use_left_air_envelope =
            (uint8_t)(use_specialhi_left_envelope || use_damagefly_left_envelope ||
                      use_common_air_left_envelope || use_ft80081d0c_left_envelope ||
                      use_ft_check_ground_ledge_air_left_envelope);
        const MslEcbWorldPoints* left_cur_ecb =
            use_specialhi_left_envelope ? &cur_specialhi_wall_ecb : &cur_ecb;
        const MslEcbWorldPoints* left_prev_ecb =
            use_specialhi_left_envelope ? &prev_specialhi_wall_ecb : &prev_ecb;
        const float cur_rx = left_cur_ecb->right_x;
        const float cur_ry = left_cur_ecb->right_y;
        const float prev_rx = left_prev_ecb->right_x;
        const float prev_ry = left_prev_ecb->right_y;

        int prefer_line_idx = -1;
        if (prev_wall_kind == MSL_WALL_LEFT && prev_wall_id != 0xFFFFu) {
          prefer_line_idx = stage_collision_left_wall_line_index(stage_id, prev_wall_id);
        }

        // Persistence: if we were already attached to a left wall last frame, project first.
        // Decomp: CollData.left_facing_wall.index persists; mpLib_8004E398_LeftWall stays attached.
        // refs/melee/src/melee/lb/types.h::CollData
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004E398_LeftWall
        const uint8_t use_jumpaerial_locked_left_wall_persistence =
            (uint8_t)(use_common_air_left_envelope &&
                      (action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                       action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
                      batch->state.ecb_lock_timer[idx] != 0u &&
                      batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u);
        if (!grounded_now && prev_wall_kind == MSL_WALL_LEFT && prefer_line_idx >= 0 &&
            (!use_left_air_envelope || use_jumpaerial_locked_left_wall_persistence)) {
          float x_corr = 0.0f;
          float nx = -1.0f, ny = 0.0f;
          const int out_line_idx =
              left_wall_e398_project(lwg, prefer_line_idx, cur_rx, cur_ry, &x_corr, &nx, &ny);
          if (out_line_idx >= 0) {
            if (x_corr > 0.0f) {
              x_corr = 0.0f;
            }
            batch->state.pos_x[idx] += x_corr;
            batch->state.wall_kind[idx] = MSL_WALL_LEFT;
            batch->state.wall_id[idx] = lwg->lines[(size_t)out_line_idx].segment_i;
            batch->state.wall_contact_x[idx] = cur_rx + x_corr;
            batch->state.wall_contact_y[idx] = cur_ry;
            batch->state.wall_normal_x[idx] = nx;
            batch->state.wall_normal_y[idx] = ny;
            const uint8_t prev_hug =
                (batch->state.coll_prev_env_flags[idx] & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) ? 1u
                                                                                              : 0u;
            mark_left_wall_contact(batch, idx, prev_hug);
            ecb_points_shift_x3(&cur_ecb, &cur_right_ecb, &cur_specialhi_wall_ecb, x_corr);
          }
        }

        // Decomp: `mpColl_80045B74_LeftWall` collects a fixed-capacity candidate list from
        // side, bottom, and top sweeps; `mpColl_80046224_LeftWall` then resolves the full
        // airborne ECB envelope against that list. Retain that envelope for source callbacks that
        // call the same airborne mpColl owner: SpecialAirHi's live-JObj launch collision, common
        // Jump/Fall walljump callbacks, DamageFly wall-tech/reflect paths, and ft_80082C74's
        // ft_80081D0C path for AttackAir/EscapeAir/common airborne landing callbacks.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80045B74_LeftWall,mpColl_80046224_LeftWall}
        if (batch->state.wall_kind[idx] == 0 && use_left_air_envelope) {
          MslWallCandidateList candidates;
          wall_candidate_list_init(&candidates);
          left_wall_candidate_sweep(&candidates, lwg, prev_rx, prev_ry, cur_rx, cur_ry,
                                    prefer_line_idx, 1u, -1);
          left_wall_candidate_sweep(&candidates, lwg, left_prev_ecb->bottom_x,
                                    left_prev_ecb->bottom_y, left_cur_ecb->bottom_x,
                                    left_cur_ecb->bottom_y, prefer_line_idx, 0u,
                                    grounded_left_floor_adj_line_idx);
          left_wall_candidate_sweep(&candidates, lwg, left_prev_ecb->top_x, left_prev_ecb->top_y,
                                    left_cur_ecb->top_x, left_cur_ecb->top_y, prefer_line_idx, 0u,
                                    -1);
          // Decomp: after the three point sweeps, mpColl_80045B74_LeftWall also checks the current
          // bottom->right and top->right ECB edges with mpCheckLeftWall. These add Push-only
          // candidates; Hug remains owned by the side-point sweep above. Keep the lite floor-chain
          // adjacency exclusion here until the full CollData floor/wall joint-skip state is
          // modeled; otherwise same-frame floor admission can also perturb unrelated wall pushes.
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_80045B74_LeftWall
          left_wall_candidate_sweep(&candidates, lwg, left_cur_ecb->bottom_x,
                                    left_cur_ecb->bottom_y, left_cur_ecb->right_x,
                                    left_cur_ecb->right_y, prefer_line_idx, 0u,
                                    grounded_left_floor_adj_line_idx);
          left_wall_candidate_quad(
              &candidates, lwg, left_prev_ecb->bottom_x, left_prev_ecb->bottom_y,
              left_prev_ecb->right_x, left_prev_ecb->right_y, left_cur_ecb->bottom_x,
              left_cur_ecb->bottom_y, left_cur_ecb->right_x, left_cur_ecb->right_y);
          left_wall_candidate_sweep(&candidates, lwg, left_cur_ecb->top_x, left_cur_ecb->top_y,
                                    left_cur_ecb->right_x, left_cur_ecb->right_y, prefer_line_idx,
                                    0u, -1);
          left_wall_candidate_quad(&candidates, lwg, left_prev_ecb->right_x, left_prev_ecb->right_y,
                                   left_prev_ecb->top_x, left_prev_ecb->top_y,
                                   left_cur_ecb->right_x, left_cur_ecb->right_y,
                                   left_cur_ecb->top_x, left_cur_ecb->top_y);

          float envelope_x = 0.0f;
          int envelope_line_idx = -1;
          float envelope_nx = -1.0f, envelope_ny = 0.0f;
          if (left_wall_air_envelope_min_x(lwg, &candidates, left_cur_ecb, batch->state.pos_x[idx],
                                           batch->state.pos_y[idx], &envelope_x, &envelope_line_idx,
                                           &envelope_nx, &envelope_ny)) {
            const float dx = envelope_x - batch->state.pos_x[idx];
            batch->state.pos_x[idx] = envelope_x;
            batch->state.wall_kind[idx] = MSL_WALL_LEFT;
            batch->state.wall_id[idx] = lwg->lines[(size_t)envelope_line_idx].segment_i;
            batch->state.wall_contact_x[idx] = candidates.first_ix;
            batch->state.wall_contact_y[idx] = candidates.first_iy;
            batch->state.wall_normal_x[idx] = envelope_nx;
            batch->state.wall_normal_y[idx] = envelope_ny;
            // SpecialAirHi consumes the wall resolution for rebound/hitlag provenance, not the
            // common-air PassiveWall/WallJump Hug consumer. DamageFly and common Jump/Fall
            // callbacks do immediately consume Collide_LeftWallHug after this mpColl path:
            // DamageFly for FlyReflect/PassiveWall, Jump/Fall for walljump. The ft_80081D0C path
            // still receives the source mpColl Hug bit, but has no same-callback walljump consumer.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FlyReflect.c::ftCo_800C15F4
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            mark_left_wall_contact(
                batch, idx,
                (uint8_t)((use_common_air_left_envelope || use_ft80081d0c_left_envelope ||
                           use_ft_check_ground_ledge_air_left_envelope ||
                           use_damagefly_left_envelope)
                              ? candidates.has_hug
                              : 0u));
            ecb_points_shift_x3(&cur_ecb, &cur_right_ecb, &cur_specialhi_wall_ecb, dx);
          }
        }
        if (batch->state.wall_kind[idx] == 0 &&
            (!use_left_air_envelope || use_damagefly_left_envelope)) {
          float ix = 0.0f, iy = 0.0f;
          float nx = -1.0f, ny = 0.0f;
          int hit_line_idx = -1;
          if (wall_sweep_check(lwg, 1, prev_rx, prev_ry, cur_rx, cur_ry, prefer_line_idx, 0u,
                               &hit_line_idx, &ix, &iy, &nx, &ny)) {
            float x_corr = 0.0f;
            const int out_line_idx =
                left_wall_e398_project(lwg, hit_line_idx, cur_rx, cur_ry, &x_corr, &nx, &ny);
            if (out_line_idx >= 0) {
              if (x_corr > 0.0f) {
                x_corr = 0.0f;
              }
              batch->state.pos_x[idx] += x_corr;
              batch->state.wall_kind[idx] = MSL_WALL_LEFT;
              batch->state.wall_id[idx] = lwg->lines[(size_t)out_line_idx].segment_i;
              batch->state.wall_contact_x[idx] = ix;
              batch->state.wall_contact_y[idx] = iy;
              batch->state.wall_normal_x[idx] = nx;
              batch->state.wall_normal_y[idx] = ny;
              // During active DamageFly hitlag, Fighter_procMap refreshes wall contact metadata
              // for ftCo_Damage_OnExitHitlag ASDI provenance. Point-local projection is still only
              // stale wall-id provenance; the source Hug bit remains owned by the side-sweep
              // envelope above, which ftCo_DamageFly_Coll may consume immediately.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
              //   ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
              // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_procMap}
              mark_left_wall_contact(
                  batch, idx,
                  (uint8_t)((damagefly_hitlag_wall_refresh || use_damagefly_left_envelope) ? 0u
                                                                                           : 1u));
              ecb_points_shift_x3(&cur_ecb, &cur_right_ecb, &cur_specialhi_wall_ecb, x_corr);
            }
          }
        }
        if (batch->state.wall_kind[idx] == 0 && !grounded_now) {
          // Teacher-forced CollData persistence recovery:
          // - In-engine CollData can enter this frame with a persisted wall index from the prior
          //   mpColl step. One-step reseeds expose position/velocity but not that hidden index.
          // - If the current side point is already infinitesimally inside the wall, recover the
          //   same mpLib_8004E398 projection before bottom/top fallback ordering. The 0.1 bound is
          //   the decomp endpoint clamp used by mpLineIntersectionV / mpLib wall traversal.
          // refs/melee/src/melee/lb/types.h::CollData
          // refs/melee/src/melee/mp/mplib.c::{mpLib_8004E398_LeftWall,mpLineIntersectionV}
          float best_corr = 0.0f;
          float best_nx = -1.0f, best_ny = 0.0f;
          int best_line_idx = -1;
          for (size_t li = 0; li < lwg->line_count; li++) {
            float x_corr = 0.0f;
            float cand_nx = -1.0f, cand_ny = 0.0f;
            const int out_line_idx =
                left_wall_e398_project(lwg, (int)li, cur_rx, cur_ry, &x_corr, &cand_nx, &cand_ny);
            if (out_line_idx < 0 || x_corr > 0.0f || fabsf(x_corr) > k_line_end_clamp) {
              continue;
            }
            if (best_line_idx < 0 || fabsf(x_corr) < fabsf(best_corr) ||
                (fabsf(x_corr) == fabsf(best_corr) &&
                 lwg->lines[(size_t)out_line_idx].segment_i <
                     lwg->lines[(size_t)best_line_idx].segment_i)) {
              best_line_idx = out_line_idx;
              best_corr = x_corr;
              best_nx = cand_nx;
              best_ny = cand_ny;
            }
          }
          if (best_line_idx >= 0) {
            batch->state.pos_x[idx] += best_corr;
            batch->state.wall_kind[idx] = MSL_WALL_LEFT;
            batch->state.wall_id[idx] = lwg->lines[(size_t)best_line_idx].segment_i;
            batch->state.wall_contact_x[idx] = cur_rx + best_corr;
            batch->state.wall_contact_y[idx] = cur_ry;
            batch->state.wall_normal_x[idx] = best_nx;
            batch->state.wall_normal_y[idx] = best_ny;
            // This side-point projection recovery reconstructs mpLib projection from a persisted
            // line id or sub-frame overlap. It owns WallHug only for common-air callbacks that
            // immediately consume ftWallJump_8008169C; DamageFly wall-tech/ASDI paths stay
            // Push-only unless their own side branch or seed lane proves Hug.
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80045B74_LeftWall,mpColl_80046224_LeftWall}
            mark_left_wall_contact(batch, idx,
                                   mpcoll_action_uses_common_air_walljump_callback(action_id));
            ecb_points_shift_x3(&cur_ecb, &cur_right_ecb, &cur_specialhi_wall_ecb, best_corr);
          }
        }

        if (batch->state.wall_kind[idx] == 0 && !use_left_air_envelope) {
          const float cur_bx = cur_ecb.bottom_x;
          const float cur_by = cur_ecb.bottom_y;
          const float prev_bx = prev_ecb.bottom_x;
          const float prev_by = prev_ecb.bottom_y;

          float ix2 = 0.0f, iy2 = 0.0f;
          float nx2 = -1.0f, ny2 = 0.0f;
          int hit2 = -1;
          if (wall_sweep_check(lwg, 1, prev_bx, prev_by, cur_bx, cur_by, prefer_line_idx, 0u, &hit2,
                               &ix2, &iy2, &nx2, &ny2) &&
              hit2 != grounded_left_floor_adj_line_idx) {
            float x_corr = 0.0f;
            const int out_line_idx =
                left_wall_e398_project(lwg, hit2, cur_bx, cur_by, &x_corr, &nx2, &ny2);
            if (out_line_idx >= 0) {
              if (x_corr > 0.0f) {
                x_corr = 0.0f;
              }
              batch->state.pos_x[idx] += x_corr;
              batch->state.wall_kind[idx] = MSL_WALL_LEFT;
              batch->state.wall_id[idx] = lwg->lines[(size_t)out_line_idx].segment_i;
              batch->state.wall_contact_x[idx] = ix2;
              batch->state.wall_contact_y[idx] = iy2;
              batch->state.wall_normal_x[idx] = nx2;
              batch->state.wall_normal_y[idx] = ny2;
              mark_left_wall_contact(batch, idx, 0u);
              ecb_points_shift_x3(&cur_ecb, &cur_right_ecb, &cur_specialhi_wall_ecb, x_corr);
            }
          }
        }

        if (batch->state.wall_kind[idx] == 0 && !use_left_air_envelope) {
          const float cur_tx = cur_ecb.top_x;
          const float cur_ty = cur_ecb.top_y;
          const float prev_tx = prev_ecb.top_x;
          const float prev_ty = prev_ecb.top_y;

          float ix2 = 0.0f, iy2 = 0.0f;
          float nx2 = -1.0f, ny2 = 0.0f;
          int hit2 = -1;
          if (wall_sweep_check(lwg, 1, prev_tx, prev_ty, cur_tx, cur_ty, prefer_line_idx, 0u, &hit2,
                               &ix2, &iy2, &nx2, &ny2)) {
            float x_corr = 0.0f;
            const int out_line_idx =
                left_wall_e398_project(lwg, hit2, cur_tx, cur_ty, &x_corr, &nx2, &ny2);
            if (out_line_idx >= 0) {
              if (x_corr > 0.0f) {
                x_corr = 0.0f;
              }
              batch->state.pos_x[idx] += x_corr;
              batch->state.wall_kind[idx] = MSL_WALL_LEFT;
              batch->state.wall_id[idx] = lwg->lines[(size_t)out_line_idx].segment_i;
              batch->state.wall_contact_x[idx] = ix2;
              batch->state.wall_contact_y[idx] = iy2;
              batch->state.wall_normal_x[idx] = nx2;
              batch->state.wall_normal_y[idx] = ny2;
              mark_left_wall_contact(batch, idx, 0u);
              ecb_points_shift_x3(&cur_ecb, &cur_right_ecb, &cur_specialhi_wall_ecb, x_corr);
            }
          }
        }
      }

      // Right wall collision (hit by the fighter's left ECB side). If already touching a left wall,
      // skip the right-wall pass (FD never requires both; keep deterministic).
      if (batch->state.wall_kind[idx] == 0 && rwg && rwg->lines && rwg->line_count) {
        // Decomp: mpLib_8004E684_RightWall consumes a (x,y) point; mpColl passes ECB side points.
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004E684_RightWall
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_800454A4_RightWall
        const uint8_t use_specialhi_right_envelope =
            specialhi_launch_uses_runtime_xrotn_ecb(char_id, action_id);
        const uint8_t use_damagefly_right_envelope =
            (uint8_t)(mpcoll_damagefly_wall_asdi_latch_action(action_id) &&
                      batch->state.speed_x_attack[idx] < 0.0f);
        const uint8_t use_common_air_right_envelope = use_common_air_walljump_callback;
        const uint8_t use_ft80081d0c_right_envelope =
            mpcoll_action_uses_ft80081d0c_air_collision(action_id);
        const uint8_t use_ft_check_ground_ledge_air_right_envelope =
            mpcoll_action_uses_ft_check_ground_ledge_air_collision(char_id, action_id);
        const uint8_t use_right_air_envelope =
            (uint8_t)(use_specialhi_right_envelope || use_damagefly_right_envelope ||
                      use_common_air_right_envelope || use_ft80081d0c_right_envelope ||
                      use_ft_check_ground_ledge_air_right_envelope);
        const float cur_lx = cur_right_ecb.left_x;
        const float cur_ly = cur_right_ecb.left_y;
        const float prev_lx = prev_right_ecb.left_x;
        const float prev_ly = prev_right_ecb.left_y;

        int prefer_line_idx = -1;
        if (prev_wall_kind == MSL_WALL_RIGHT && prev_wall_id != 0xFFFFu) {
          prefer_line_idx = stage_collision_right_wall_line_index(stage_id, prev_wall_id);
        }

        const uint8_t use_jumpaerial_locked_right_wall_persistence =
            (uint8_t)(use_common_air_right_envelope &&
                      (action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                       action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
                      batch->state.ecb_lock_timer[idx] != 0u &&
                      batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u);
        if (!grounded_now && prev_wall_kind == MSL_WALL_RIGHT && prefer_line_idx >= 0 &&
            (!use_right_air_envelope || use_jumpaerial_locked_right_wall_persistence)) {
          float x_corr = 0.0f;
          float nx = 1.0f, ny = 0.0f;
          const int out_line_idx =
              right_wall_e684_project(rwg, prefer_line_idx, cur_lx, cur_ly, &x_corr, &nx, &ny);
          if (out_line_idx >= 0) {
            if (x_corr < 0.0f) {
              x_corr = 0.0f;
            }
            batch->state.pos_x[idx] += x_corr;
            batch->state.wall_kind[idx] = MSL_WALL_RIGHT;
            batch->state.wall_id[idx] = rwg->lines[(size_t)out_line_idx].segment_i;
            batch->state.wall_contact_x[idx] = cur_lx + x_corr;
            batch->state.wall_contact_y[idx] = cur_ly;
            batch->state.wall_normal_x[idx] = nx;
            batch->state.wall_normal_y[idx] = ny;
            const uint8_t prev_hug =
                (batch->state.coll_prev_env_flags[idx] & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) ? 1u
                                                                                               : 0u;
            mark_right_wall_contact(batch, idx, prev_hug);
            ecb_points_shift_x3(&cur_ecb, &cur_right_ecb, &cur_specialhi_wall_ecb, x_corr);
          }
        }

        if (batch->state.wall_kind[idx] == 0) {
          // Decomp: `mpColl_80044E10_RightWall` first collects a de-duplicated candidate wall list
          // from ECB side, bottom/top point sweeps, current bottom/top side edges, and the swept
          // side-edge quad helpers before `mpColl_800454A4_RightWall` resolves the maximum push X.
          // Keep the same fixed nine-slot capacity and source ordering; no heap allocation.
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_80044E10_RightWall,mpColl_800454A4_RightWall}
          MslWallCandidateList candidates;
          wall_candidate_list_init(&candidates);

          right_wall_candidate_sweep(&candidates, rwg, prev_lx, prev_ly, cur_lx, cur_ly,
                                     prefer_line_idx, 1u, -1);
          right_wall_candidate_sweep(&candidates, rwg, prev_right_ecb.bottom_x,
                                     prev_right_ecb.bottom_y, cur_right_ecb.bottom_x,
                                     cur_right_ecb.bottom_y, prefer_line_idx, 0u,
                                     grounded_right_floor_adj_line_idx);
          right_wall_candidate_sweep(&candidates, rwg, prev_right_ecb.top_x, prev_right_ecb.top_y,
                                     cur_right_ecb.top_x, cur_right_ecb.top_y, prefer_line_idx, 0u,
                                     -1);
          // Decomp: `mpColl_80044E10_RightWall` also checks the current bottom->left and top->left
          // ECB edges before resolving the max-X envelope. These candidates are Push-only; only
          // the side-point sweep sets Collide_RightWallHug for PassiveWall/WallJump callbacks.
          // Preserve the lite floor-chain adjacency exclusion until CollData's source joint-skip
          // state is modeled for this same-frame floor/wall combination.
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044E10_RightWall
          right_wall_candidate_sweep(&candidates, rwg, cur_right_ecb.bottom_x,
                                     cur_right_ecb.bottom_y, cur_right_ecb.left_x,
                                     cur_right_ecb.left_y, prefer_line_idx, 0u,
                                     grounded_right_floor_adj_line_idx);
          right_wall_candidate_quad(
              &candidates, rwg, prev_right_ecb.bottom_x, prev_right_ecb.bottom_y,
              prev_right_ecb.left_x, prev_right_ecb.left_y, cur_right_ecb.bottom_x,
              cur_right_ecb.bottom_y, cur_right_ecb.left_x, cur_right_ecb.left_y);
          right_wall_candidate_sweep(&candidates, rwg, cur_right_ecb.top_x, cur_right_ecb.top_y,
                                     cur_right_ecb.left_x, cur_right_ecb.left_y, prefer_line_idx,
                                     0u, -1);
          right_wall_candidate_quad(&candidates, rwg, prev_right_ecb.left_x, prev_right_ecb.left_y,
                                    prev_right_ecb.top_x, prev_right_ecb.top_y,
                                    cur_right_ecb.left_x, cur_right_ecb.left_y, cur_right_ecb.top_x,
                                    cur_right_ecb.top_y);

          float envelope_x = 0.0f;
          int envelope_line_idx = -1;
          float envelope_nx = 1.0f, envelope_ny = 0.0f;
          if (right_wall_air_envelope_max_x(rwg, &candidates, &cur_right_ecb,
                                            batch->state.pos_x[idx], batch->state.pos_y[idx],
                                            &envelope_x, &envelope_line_idx, &envelope_nx,
                                            &envelope_ny)) {
            const float dx = envelope_x - batch->state.pos_x[idx];
            batch->state.pos_x[idx] = envelope_x;
            batch->state.wall_kind[idx] = MSL_WALL_RIGHT;
            batch->state.wall_id[idx] = rwg->lines[(size_t)envelope_line_idx].segment_i;
            batch->state.wall_contact_x[idx] = candidates.first_ix;
            batch->state.wall_contact_y[idx] = candidates.first_iy;
            batch->state.wall_normal_x[idx] = envelope_nx;
            batch->state.wall_normal_y[idx] = envelope_ny;
            mark_right_wall_contact(batch, idx,
                                    use_specialhi_right_envelope ? 0u : candidates.has_hug);
            ecb_points_shift_x3(&cur_ecb, &cur_right_ecb, &cur_specialhi_wall_ecb, dx);
          }
        }
        if (batch->state.wall_kind[idx] == 0 && !grounded_now) {
          // Symmetric persisted-index recovery for right walls.
          // refs/melee/src/melee/lb/types.h::CollData
          // refs/melee/src/melee/mp/mplib.c::{mpLib_8004E684_RightWall,mpLineIntersectionV}
          float best_corr = 0.0f;
          float best_nx = 1.0f, best_ny = 0.0f;
          int best_line_idx = -1;
          for (size_t li = 0; li < rwg->line_count; li++) {
            float x_corr = 0.0f;
            float cand_nx = 1.0f, cand_ny = 0.0f;
            const int out_line_idx =
                right_wall_e684_project(rwg, (int)li, cur_lx, cur_ly, &x_corr, &cand_nx, &cand_ny);
            if (out_line_idx < 0 || x_corr < 0.0f || fabsf(x_corr) > k_line_end_clamp) {
              continue;
            }
            if (best_line_idx < 0 || fabsf(x_corr) < fabsf(best_corr) ||
                (fabsf(x_corr) == fabsf(best_corr) &&
                 rwg->lines[(size_t)out_line_idx].segment_i <
                     rwg->lines[(size_t)best_line_idx].segment_i)) {
              best_line_idx = out_line_idx;
              best_corr = x_corr;
              best_nx = cand_nx;
              best_ny = cand_ny;
            }
          }
          if (best_line_idx >= 0) {
            batch->state.pos_x[idx] += best_corr;
            batch->state.wall_kind[idx] = MSL_WALL_RIGHT;
            batch->state.wall_id[idx] = rwg->lines[(size_t)best_line_idx].segment_i;
            batch->state.wall_contact_x[idx] = cur_lx + best_corr;
            batch->state.wall_contact_y[idx] = cur_ly;
            batch->state.wall_normal_x[idx] = best_nx;
            batch->state.wall_normal_y[idx] = best_ny;
            // Symmetric side-point projection recovery.
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80044E10_RightWall,mpColl_800454A4_RightWall}
            mark_right_wall_contact(batch, idx,
                                    mpcoll_action_uses_common_air_walljump_callback(action_id));
            ecb_points_shift_x3(&cur_ecb, &cur_right_ecb, &cur_specialhi_wall_ecb, best_corr);
          }
        }
      }

      // Ceiling collision (hit by the fighter's top ECB point).
      if (cg && cg->lines && cg->line_count) {
        // Decomp: mpLib_8004E090_Ceiling consumes the ECB top point.
        // SpecialAirHi's collision callback is in the same JObj-ECB owner family as its wall
        // contacts: the launch model rotates XRotN before mpColl samples the top point. Reuse the
        // live SpecialHi ECB sample here so upward recoveries collide with stage undersides instead
        // of letting the generic unrotated top point miss until the later floor landing.
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044C74_Ceiling
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
        const uint8_t use_specialhi_ceiling_ecb =
            specialhi_launch_uses_runtime_xrotn_ecb(char_id, action_id);
        const MslEcbWorldPoints* ceiling_cur_ecb =
            use_specialhi_ceiling_ecb ? &cur_specialhi_wall_ecb : &cur_ecb;
        const MslEcbWorldPoints* ceiling_prev_ecb =
            use_specialhi_ceiling_ecb ? &prev_specialhi_wall_ecb : &prev_ecb;
        const float cur_tx = ceiling_cur_ecb->top_x;
        const float cur_ty = ceiling_cur_ecb->top_y;
        const float prev_tx = ceiling_prev_ecb->top_x;
        const float prev_ty = ceiling_prev_ecb->top_y;

        int prefer_line_idx = -1;
        if (prev_ceiling_id != 0xFFFFu) {
          prefer_line_idx = stage_collision_ceiling_line_index(stage_id, prev_ceiling_id);
        }

        // If we had a ceiling contact last frame, attempt a projection first (persistence).
        // Decomp: CollData.ceiling.index persists; mpLib_8004E090_Ceiling is used to stay attached.
        // refs/melee/src/melee/lb/types.h::CollData
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004E090_Ceiling
        if (prev_had_ceiling && prefer_line_idx >= 0) {
          float y_corr = 0.0f;
          float nx = 0.0f, ny = -1.0f;
          const int out_line_idx =
              ceiling_e090_project(cg, prefer_line_idx, cur_tx, cur_ty, &y_corr, &nx, &ny);
          if (out_line_idx >= 0) {
            if (y_corr > 0.0f) {
              y_corr = 0.0f;
            }
            batch->state.pos_y[idx] += y_corr;
            batch->state.ceiling_id[idx] = cg->lines[(size_t)out_line_idx].segment_i;
            batch->state.ceiling_normal_x[idx] = nx;
            batch->state.ceiling_normal_y[idx] = ny;
            batch->state.ceiling_contact_x[idx] = cur_tx;
            batch->state.ceiling_contact_y[idx] = cur_ty + y_corr;
            batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_CEILING_MASK;
            ceiling_write_edge_suppression_flags(batch, idx, stage_id, cg, out_line_idx,
                                                 ceiling_cur_ecb);
          }
        } else {
          float ix = 0.0f, iy = 0.0f;
          float nx = 0.0f, ny = -1.0f;
          int hit_line_idx = -1;
          if (ceiling_sweep_check(cg, prev_tx, prev_ty, cur_tx, cur_ty, prefer_line_idx,
                                  &hit_line_idx, &ix, &iy, &nx, &ny)) {
            float y_corr = 0.0f;
            const int out_line_idx =
                ceiling_e090_project(cg, hit_line_idx, cur_tx, cur_ty, &y_corr, &nx, &ny);
            if (out_line_idx >= 0) {
              if (y_corr > 0.0f) {
                y_corr = 0.0f;
              }
              batch->state.pos_y[idx] += y_corr;
              batch->state.ceiling_id[idx] = cg->lines[(size_t)out_line_idx].segment_i;
              batch->state.ceiling_normal_x[idx] = nx;
              batch->state.ceiling_normal_y[idx] = ny;
              batch->state.ceiling_contact_x[idx] = ix;
              batch->state.ceiling_contact_y[idx] = iy;
              batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_CEILING_MASK;
              ceiling_write_edge_suppression_flags(batch, idx, stage_id, cg, out_line_idx,
                                                   ceiling_cur_ecb);
            }
          }
        }
        if ((batch->state.coll_env_flags[idx] & (uint32_t)MSL_COLLIDE_CEILING_MASK) == 0u &&
            batch->state.wall_kind[idx] != MSL_WALL_NONE) {
          (void)ceiling_try_adjacent_from_air_wall(batch, idx, stage_id, cg, ceiling_cur_ecb);
        }
        if ((batch->state.coll_env_flags[idx] & (uint32_t)MSL_COLLIDE_CEILING_MASK) == 0u &&
            use_specialhi_ceiling_ecb && fg != NULL && fg->lines != NULL && fg->line_count != 0u &&
            cur_ty > prev_ty) {
          // Source has already sampled SpecialAirHi's live JObj ECB before ceiling/floor collision
          // callbacks run. The extracted stage shell represents hard top surfaces in the floor
          // graph and does not duplicate every underside as a ceiling line, so use the rotated
          // SpecialHi top-point sweep against hard floor segments as the missing underside
          // substrate. Soft platforms stay one-way and are intentionally excluded.
          //
          // Keep this owner on the top point only. A root-position crossing of a top floor line is
          // not a ceiling hit in mpColl_80044C74_Ceiling; treating it as one clips side-riding Up-B
          // movement as soon as the root reaches floor height even when the sampled top point has no
          // obstruction above it.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Coll
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044C74_Ceiling}
          int best_line_idx = -1;
          float best_ix = 0.0f;
          float best_iy = 0.0f;
          float best_dist2 = 0.0f;
          for (size_t li = 0; li < fg->line_count; li++) {
            const MslStageFloorLine* line = &fg->lines[li];
            if (line->fighter_solid == 0u || line->is_platform != 0u) {
              continue;
            }
            float cand_ix = 0.0f;
            float cand_iy = 0.0f;
            if (!intersect_segment(line->x0, line->y0, line->x1, line->y1, prev_tx, prev_ty, cur_tx,
                                   cur_ty, &cand_ix, &cand_iy)) {
              continue;
            }
            const float dx = cand_ix - prev_tx;
            const float dy = cand_iy - prev_ty;
            const float dist2 = dx * dx + dy * dy;
            if (best_line_idx < 0 || dist2 < best_dist2 ||
                (dist2 == best_dist2 &&
                 line->segment_i < fg->lines[(size_t)best_line_idx].segment_i)) {
              best_line_idx = (int)li;
              best_ix = cand_ix;
              best_iy = cand_iy;
              best_dist2 = dist2;
            }
          }
          if (best_line_idx >= 0) {
            const MslStageFloorLine* line = &fg->lines[(size_t)best_line_idx];
            const float y_corr = (best_iy - k_ceiling_y_bias) - cur_ty;
            if (y_corr < 0.0f) {
              batch->state.pos_y[idx] += y_corr;
            }
            batch->state.ceiling_id[idx] = line->segment_i;
            batch->state.ceiling_normal_x[idx] = 0.0f;
            batch->state.ceiling_normal_y[idx] = -1.0f;
            batch->state.ceiling_contact_x[idx] = best_ix;
            batch->state.ceiling_contact_y[idx] = best_iy;
            batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_CEILING_MASK;
          }
        }
      }

      // Damage_OnExitHitlag wall-ASDI provenance:
      // `wall_id` persists after detach, so the hitlag-exit ASDI owner cannot key on that id alone.
      // Latch when the DamageFly collision callback observes a wall during active hitlag. The
      // A same-frame SpecialAirHi_Coll contact before combat starts hitlag is not generic
      // DamageFly_Coll evidence for ftCo_Damage_OnExitHitlag wall-ASDI projection. Retain the
      // existing source-backed left-wall SpecialHi envelope slice only when the previous CollData
      // wall mask proves continuing left-wall provenance; right-wall damage-entry scrapes remain
      // open and must not stale-project ASDI on hitlag exit.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_procMap}
      const uint8_t active_hitlag_phase = mpcoll_active_hitlag_phase(batch, idx);
      const uint8_t same_frame_specialhi_continuing_left_wall =
          (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI &&
           batch->state.wall_kind[idx] == MSL_WALL_LEFT &&
           (batch->state.coll_prev_env_flags[idx] & (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK) != 0u)
              ? 1u
              : 0u;
      if (mpcoll_wall_asdi_producer_action(action_id) &&
          (active_hitlag_phase || same_frame_specialhi_continuing_left_wall)) {
        if (batch->state.wall_kind[idx] == MSL_WALL_LEFT ||
            batch->state.wall_kind[idx] == MSL_WALL_RIGHT) {
          batch->state.damage_hitlag_wall_asdi_latch[idx] = 1u;
        }
      } else {
        batch->state.damage_hitlag_wall_asdi_latch[idx] = 0u;
      }
    }
  }
}
