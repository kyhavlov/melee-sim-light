#include "mpcoll_ground.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action.h"
#include "action_ids.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "match_flow.h"
#include "mpcoll_ecb_points.h"
#include "motion_state_owners.h"
#include "mpcoll_wall_ceil.h"
#include "move_tables.h"
#include "msl_math.h"
#include "mtx34.h"
#include "specialhi_pose.h"
#include "state_flags.h"
#include "stage_collision.h"
#include "input_axis.h"
#include "throw_flow.h"

// Decomp constants (mplib.c):
// - mpLib_8004DD90_Floor clamps small off-end X within ±0.1 before returning -1 (airborne).
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
// - mpLib_8004DD90_Floor applies a +0.0001 bias to the vertical correction to keep the point
//   infinitesimally above the floor line.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
// - mpLib_8004ED5C extends connected endpoints by 1 unit, guarded by a 0.001 distance threshold.
//   refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
// - mpCheckFloor treats floors as horizontal when |y0 - y1| <= 0.0001.
//   refs/melee/src/melee/mp/mplib.c::mpCheckFloor
static const float k_floor_x_end_clamp = 0.1f;
static const float k_floor_y_bias = 0.0001f;
static const float k_floor_ed5c_min_dist = 0.001f;
static const float k_floor_horiz_dy_thresh = 0.0001f;
static const float k_floor_ed5c_extend = 1.0f;
// Decomp: mpColl_80043754 splits collision callbacks into ceil(max(root/ECB delta) / 6.0)
// substeps. Keep the threshold source-named because high root-motion callbacks can publish their
// floor-loss result at an intermediate substep instead of at the already-integrated final root.
// refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
static const float k_mpcoll_substep_max_delta = 6.0f;

static inline uint8_t mpcoll_ground_specialhi_launch_uses_jobj_ecb(uint8_t char_id,
                                                                   uint16_t action_id) {
  if (char_id != 1u && char_id != 22u) {
    return 0u;
  }
  return (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI ||
          action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI)
             ? 1u
             : 0u;
}

static inline uint8_t mpcoll_ground_specialhi_rotate_collision_point_xrotn(
    const MslBatch* batch, size_t idx, uint8_t char_id, uint16_t msid, uint16_t frame_u16,
    uint16_t part_id, float model_scale, float* io_x, float* io_y, float* io_z) {
  if (batch == NULL || io_x == NULL || io_y == NULL || io_z == NULL ||
      !msl_anim_part_under_xrotn(char_id, part_id)) {
    return 0u;
  }
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

static inline uint8_t mpcoll_ground_try_sample_specialhi_jobj_ecb(
    MslEcbWorldPoints* out, const MslBatch* batch, size_t idx, uint8_t char_id, uint32_t anim,
    uint16_t action_id, uint16_t frame_u16, float facing_dir, float pos_x, float pos_y) {
  if (out == NULL || batch == NULL || !(anim <= 0xFFFFu) ||
      !mpcoll_ground_specialhi_launch_uses_jobj_ecb(char_id, action_id)) {
    return 0u;
  }

  const MslCharParams* ch = msl_char_params(char_id);
  if (ch == NULL || ch->ecb_joint_count == 0u) {
    return 0u;
  }
  const uint16_t msid = (uint16_t)anim;
  const float model_scaling =
      (isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[idx] * model_scaling;

  float min_x = 0.0f;
  float max_x = 0.0f;
  float min_y = 0.0f;
  float max_y = 0.0f;
  uint8_t have = 0u;
  for (uint16_t pi = 0; pi < ch->ecb_joint_count; pi++) {
    const uint16_t part_id = ch->ecb_joints[pi];
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
    (void)mpcoll_ground_specialhi_rotate_collision_point_xrotn(batch, idx, char_id, msid, frame_u16,
                                                               part_id, model_scale, &x, &y, &z);

    // SpecialHi floor collision uses the same live JObj ECB source as the retained wall/ledge
    // owners: mpColl_LoadECB_JObj reads collision joints after ftFox_SpecialHi_RotateModel mutates
    // FtPart_XRotN, then normalizes the ECB before mpColl_80044628_Floor consumes the bottom sweep.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    //   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Coll}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044628_Floor}
    // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    const float rel_x = facing_dir * z;
    const float rel_y = y;
    if (!have) {
      min_x = max_x = rel_x;
      min_y = max_y = rel_y;
      have = 1u;
    } else {
      if (rel_x < min_x) {
        min_x = rel_x;
      }
      if (rel_x > max_x) {
        max_x = rel_x;
      }
      if (rel_y < min_y) {
        min_y = rel_y;
      }
      if (rel_y > max_y) {
        max_y = rel_y;
      }
    }
  }
  if (!have) {
    return 0u;
  }

  // `mpColl_LoadECB_JObj` normalizes live JObj ECB points before floor checks. The current
  // simulator only extracts the collision JObj origins for this SpecialHi floor path, so keep the
  // normalized horizontal envelope bounded by the same mpColl-shaped source constants used by the
  // retained wall/ledge owner:
  // - +/-2.0f is mpColl's minimum side-span unit for narrowed ECB envelopes (4.0f full width).
  // - 10.0f * fighter scale is the character-scale minimum body width used by the SpecialHi
  //   JObj-ECB bridge when collision joints collapse during XRotN rotation.
  // These are source-geometry clamps for the callback-local ECB, not replay-row tolerances.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80042384}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFox_SpecialHi_RotateModel
  const float min_ecb_width = fmaxf(4.0f, 10.0f * batch->state.fighter_scale_y[idx]);
  const float ecb_width = fabsf(max_x - min_x);
  if (ecb_width < min_ecb_width) {
    const float half_width = 0.5f * min_ecb_width;
    min_x = -half_width;
    max_x = half_width;
  }
  // Keep the final side extents at least +/-2.0f for the same mpColl minimum-width reason above.
  if (max_x < 2.0f) {
    max_x = 2.0f;
  }
  if (min_x > -2.0f) {
    min_x = -2.0f;
  }
  if (min_y < 0.0f) {
    min_y = 0.0f;
  }
  const float side_rel_y = ch->ecb_side_y_offset + (0.5f * (min_y + max_y));

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

// Decomp: mpColl floor-edge helpers use +/-1 offsets from the floor endpoint when probing for
// blocking walls before setting Collide_{Left,Right}Edge.
// refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
static const float k_floor_edge_wall_probe_x_offset = 1.0f;
static const float k_floor_edge_wall_probe_y_offset = 1.0f;
// Decomp ECB vertical unit in the callback path:
// - mpColl_80042384 enforces a minimum +1.0f vertical separation for desired ECB extents.
// - mpColl_LoadECB_JObj uses midpoint +/- 1.0f in its tightened vertical-span path.
// refs/melee/src/melee/mp/mpcoll.c::{mpColl_80042384,mpColl_LoadECB_JObj}
static const float k_ecb_vertical_unit = 1.0f;
// FoD height-transform floor-skip publication checks the callback root against a transformed
// platform across the previous/current CollData ECB envelope. Source `mpColl_LoadECB_inline`
// tightens desired ECB extents with midpoint +/- 1.0f; the skip probe covers both frame-start and
// current samples, so the lookup uses two vertical ECB units as a bounded endpoint envelope rather
// than an unlabelled replay-row tolerance.
// refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
static const float k_transformed_platform_skip_lookup_slop = 2.0f * k_ecb_vertical_unit;

enum {
  MSL_MPCOLL_FLOOR_RESULT_NONE = 0u,
  MSL_MPCOLL_FLOOR_RESULT_DIRECT = 1u,
  MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY = 2u,
};

enum {
  MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL = 2u,
  MSL_STAGE_FINAL_DESTINATION_LOCAL = 32u,
};

typedef struct MslMpcollFloorContact {
  uint16_t ground_id;
  float contact_x;
  float contact_y;
  float normal_x;
  float normal_y;
} MslMpcollFloorContact;

typedef struct MslMpcollFloorPublication {
  uint8_t on_ground;
  uint16_t airborne_ground_id;
  MslMpcollFloorContact contact;
  float cur_bottom_x;
  float cur_bottom_y;
} MslMpcollFloorPublication;

static inline float cross2(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

static inline uint8_t is_cliff_hold_action(uint16_t a) {
  // Cliff / ledge hold actions use dedicated snap logic and should not be stage-grounded.
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

static inline uint8_t is_damage_collision_landing_action(uint16_t a) {
  // Damage collision callbacks can resolve grounded contact while hitstun remains active:
  // - ftCo_Damage_Coll
  // - ftCo_DamageFly_Coll
  // - ftCo_DownDamage_Coll (air path calls ft_80081DD4 before downed follow-up handling)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Coll
  return (uint8_t)(msl_motion_state_common_class_has(a, MSL_MS_CLASS_DAMAGE_COMMON_COLL) ||
                   msl_motion_state_common_class_has(a, MSL_MS_CLASS_DAMAGE_FLY_COLL) ||
                   msl_motion_state_common_class_has(a, MSL_MS_CLASS_DAMAGE_FALL_COLL));
}

static inline uint8_t is_damage_fly_collision_action(uint16_t a) {
  return msl_motion_state_common_class_has(a, MSL_MS_CLASS_DAMAGE_FLY_COLL);
}

static inline uint8_t is_damage_ground_collision_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_DAMAGE_HI_1:
    case MSL_ACT_DAMAGE_HI_2:
    case MSL_ACT_DAMAGE_HI_3:
    case MSL_ACT_DAMAGE_N_1:
    case MSL_ACT_DAMAGE_N_2:
    case MSL_ACT_DAMAGE_N_3:
    case MSL_ACT_DAMAGE_LW_1:
    case MSL_ACT_DAMAGE_LW_2:
    case MSL_ACT_DAMAGE_LW_3:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t is_capture_lw_allow_ground_to_air_collision_action(uint16_t a) {
  // Low capture callbacks use `ft_8008403C -> ft_80082708 -> mpColl_8004B108`, so grounded
  // rows may receive a downward floor projection after `fn_800DAD18` moves the victim XRotN
  // toward the owner anchor.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //   ftCo_CapturePulledLw_Coll,ftCo_CaptureWaitLw_Coll,ftCo_CaptureDamageLw_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_8008403C,ft_80082708}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  switch (a) {
    case MSL_ACT_CAPTURE_PULLED_LW:
    case MSL_ACT_CAPTURE_WAIT_LW:
    case MSL_ACT_CAPTURE_DAMAGE_LW:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t is_attackair_action(uint16_t a) {
  return msl_motion_state_common_class_has(a, MSL_MS_CLASS_ATTACK_AIR);
}

static inline uint8_t is_spacie_air_special_floor_collision_action(uint16_t a) {
  // Fox/Falco aerial special collision callbacks can resolve floor contact while carrying an ECB
  // lock from a preceding jump or ground->air handoff. Use the same locked-bottom floor loading
  // policy as other air-collision owners that explicitly call ground-contact helpers.
  //
  // Decomp anchors:
  // - SpecialAirLw{Loop,End}_Coll -> ft_80081D0C -> AirToGround.
  //   Start/Hit/Turn intentionally stay out of this locked-bottom subset. The caller also requires
  //   the frame-start action to already be Loop/End, because applying this to the same-frame
  //   SpecialAirLwStart_Anim -> Loop handoff grounds Shine startup too early.
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //     ftFx_SpecialAirLwLoop_Coll,ftFx_SpecialAirLwEnd_Coll}
  switch (a) {
    case MSL_ACT_FX_SPECIAL_AIR_LW_LOOP:
    case MSL_ACT_FX_SPECIAL_AIR_LW_END:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t is_spacie_ground_sideb_allow_ground_to_air_callback(uint8_t char_id,
                                                                          uint16_t action_id) {
  enum {
    MSL_MSO_CB_FTFX_SPECIALSSTART_COLL = 888u,
    MSL_MSO_CB_FTFX_SPECIALS_COLL = 892u,
  };
  const uint16_t coll_cb = msl_motion_state_coll_cb_id(char_id, action_id);
  // Table-backed MotionState callback owner:
  // grounded Fox/Falco Side-B Start/Main collision callbacks call ft_80082708, which routes to
  // mpColl_8004B108 -> mpColl_80043754 -> mpColl_8004ACE4. Use the generated callback identity
  // rather than a local action-id list so this owner stays tied to MSLMSO01 provenance.
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 coll_cb_by_action
  // data/motion_state/owners/callback_symbols.json::{
  //   ftFx_SpecialSStart_Coll,ftFx_SpecialS_Coll}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialSStart_Coll,ftFx_SpecialS_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
  return (uint8_t)((coll_cb == (uint16_t)MSL_MSO_CB_FTFX_SPECIALSSTART_COLL ||
                    coll_cb == (uint16_t)MSL_MSO_CB_FTFX_SPECIALS_COLL)
                       ? 1u
                       : 0u);
}

static inline uint8_t is_common_fallspecial_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_FALL_SPECIAL:
    case MSL_ACT_FALL_SPECIAL_F:
    case MSL_ACT_FALL_SPECIAL_B:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t is_spacie_specialhi_end_fallspecial_source(uint16_t a) {
  switch (a) {
    case MSL_ACT_FX_SPECIAL_HI_FALL:
    case MSL_ACT_FX_SPECIAL_HI_BOUND:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t action_uses_ftco_80096cc8_floor_callback(uint16_t a) {
  // Source callback:
  // - returns true for hard floors;
  // - returns true for platform floors only when fp->input.lstick.y > p_ftCommonData->x25C;
  // - returns false for held-down platform pass-through, so mpColl_80044628_Floor rejects the line.
  //
  // Decomp owners:
  // - common-air/fallspecial collision path routes through ft_80083090_inline / mpColl_80047E14.
  // - CliffJump2 uses ft_800835B0, which passes the same callback to ft_80083090_inline.
  // - PassiveWall/PassiveCeil use the same common air collision helper after the tech surface bounce.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083090_inline,ft_800831CC,ft_800835B0}
  if (a == (uint16_t)MSL_ACT_ESCAPE_AIR) {
    // EscapeAir_Coll delegates through ft_80082C74/mpColl_800471F8 rather than the
    // ftCo_80096CC8 platform-pass callback. Do not apply the common-air down-stick platform
    // rejection to sustained EscapeAir soft-platform floor checks.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
    return 0u;
  }
  if (msl_motion_state_common_class_has(a, MSL_MS_CLASS_COMMON_AIR_COLL)) {
    return 1u;
  }
  switch (a) {
    case MSL_ACT_JUMP_F:
    case MSL_ACT_JUMP_B:
    case MSL_ACT_JUMP_AERIAL_F:
    case MSL_ACT_JUMP_AERIAL_B:
    case MSL_ACT_FALL:
    case MSL_ACT_FALL_F:
    case MSL_ACT_FALL_B:
    case MSL_ACT_FALL_AERIAL:
    case MSL_ACT_FALL_AERIAL_F:
    case MSL_ACT_FALL_AERIAL_B:
    case MSL_ACT_FALL_SPECIAL:
    case MSL_ACT_FALL_SPECIAL_F:
    case MSL_ACT_FALL_SPECIAL_B:
    case MSL_ACT_CLIFF_JUMP_SLOW2:
    case MSL_ACT_CLIFF_JUMP_QUICK2:
    case MSL_ACT_PASSIVE_WALL:
    case MSL_ACT_PASSIVE_WALL_JUMP:
    case MSL_ACT_PASSIVE_CEIL:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t is_just_entered_specialairn_end_from_loop(uint16_t action_id,
                                                                uint16_t prev_action_id,
                                                                int16_t action_frame) {
  return (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END &&
          prev_action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP && action_frame == 0)
             ? 1u
             : 0u;
}

static inline uint8_t damage_hitlag_floorhug_attempts_downward_sdi(const MslBatch* batch,
                                                                   size_t idx,
                                                                   const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  // Collision-owner bridge for active-hitlag Damage/DamageFly rows:
  // - ftCo_Damage_OnEveryHitlag reads the current stick after input processing and mutates
  //   `fp->cur_pos` before the motion-state collision callback runs.
  // - ft_80081DD4 then routes `allow_sdi` rows through mpColl_800477E0, where
  //   mpColl_80044628_Floor / mpColl_80044948_Floor can raise FloorPush|FloorHug while keeping
  //   the fighter airborne (`CollisionFlagAir_StayAirborne`).
  // - Restrict this stay-airborne floor correction bridge to rows where OnEveryHitlag can actually
  //   consume an SDI input and move cur_pos downward. A held downward stick after x670/x671 have
  //   already been reset to 0xFE is not a new callback displacement and must not re-project the
  //   root to the floor on each frozen hitlag frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
  // refs/melee/src/melee/ft/fighter.c (x670/x671 timer update and hitlag-active x221A flags)
  const float sx =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float sy =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (batch->state.damage_hitlag_downward_sdi_consumed[idx] != 0u) {
    return 1u;
  }
  // ftCo_Damage_OnEveryHitlag tests Fighter_Spaghetti's deadzoned fp->input.lstick vector, not raw
  // pad axes. Keep the floor-projection admission predicate on that same source vector.
  // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
  const float mag_sq = sx * sx + sy * sy;
  if (mag_sq < c->sdi_radius * c->sdi_radius) {
    return 0u;
  }
  if (sy >= 0.0f) {
    return 0u;
  }

  const uint16_t action_id = batch->state.action_id[idx];
  enum { MSL_STATE_FLAGS_221A_BYTE_INDEX = 1 };
  const size_t flags_i =
      idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221A_BYTE_INDEX;
  enum { MSL_STATE_FLAG_221A_B3 = 0x10 };
  const uint8_t sdi_tilt_window = (batch->state.tilt_timer_x[idx] < c->sdi_tilt_max_frames ||
                                   batch->state.tilt_timer_y[idx] < c->sdi_tilt_max_frames)
                                      ? 1u
                                      : 0u;
  const uint8_t timer_window_owner =
      (((batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_221A_B3) != 0u ||
        is_damage_fly_collision_action(action_id) || action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D ||
        batch->state.phantom_damage_pending_x1898[idx] > 0.0f) &&
       sdi_tilt_window)
          ? 1u
          : 0u;
  const float prev_sx =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_x[idx]), c->lstick_deadzone_x);
  const float prev_sy =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_y[idx]), c->lstick_deadzone_y);
  const float prev_mag_sq = prev_sx * prev_sx + prev_sy * prev_sy;
  const uint8_t first_active_common_damage_radius_crossing =
      (!is_damage_fly_collision_action(action_id) && batch->state.action_frame[idx] == 1 &&
       batch->state.tilt_timer_x[idx] == 254u && batch->state.tilt_timer_y[idx] == 254u &&
       batch->state.seed_prev_action_id[idx] != action_id &&
       mag_sq >= c->sdi_radius * c->sdi_radius && prev_mag_sq < c->sdi_radius * c->sdi_radius)
          ? 1u
          : 0u;
  return (uint8_t)((timer_window_owner || first_active_common_damage_radius_crossing) ? 1u : 0u);
}

static inline uint8_t action_uses_active_hitlag_downward_sdi_floorhug(uint16_t action_id,
                                                                      const MslBatch* batch,
                                                                      size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // Active-hitlag OnEveryHitlag -> mpColl floorhug ownership is source-proven for DamageFly and
  // DownDamage callback paths. Common DamageAir re-entry has its own explicit owner below; sustained
  // same-action DamageAir continuation frames can still receive SDI displacement, but that does not
  // by itself publish FloorPush|FloorHug against the carried floor line.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{ftCo_8009F184,ftCo_DownDamage_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
  if (is_damage_fly_collision_action(action_id) || action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
      action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) {
    return 1u;
  }
  return (batch->state.phantom_damage_pending_x1898[idx] > 0.0f) ? 1u : 0u;
}

static inline uint8_t grounded_damage_hitlag_allows_downward_floor_projection(const MslBatch* batch,
                                                                              size_t idx,
                                                                              uint16_t action_id) {
  if (batch == NULL) {
    return 0u;
  }
  // Grounded damage collision owner:
  // - `ftCo_Damage_OnEveryHitlag` can move `fp->cur_pos.y` before collision.
  // - grounded `ftCo_Damage_Coll` then routes through `ft_800848DC -> ft_80082708 ->
  //   mpColl_8004B108`, which keeps the row floor-owned.
  // - Keep the downward floor projection on these active-hitlag grounded damage rows so the
  //   generic grounded anti-snap clamp does not strand a "grounded" fighter several units above
  //   the stage.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll
  // }
  // refs/melee/src/melee/ft/ft_081B.c::{ft_800848DC,ft_80082708}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  if (!is_damage_collision_landing_action(action_id)) {
    return 0u;
  }
  return (batch->state.on_ground[idx] != 0u && batch->state.hitlag_pre_timer[idx] != 0u &&
          batch->state.hitlag[idx] != 0u)
             ? 1u
             : 0u;
}

static inline void stay_airborne_floor_projection_point(float fighter_x, float fighter_y,
                                                        float bottom_x, float bottom_y,
                                                        float* proj_x_out, float* proj_y_out) {
  if (proj_x_out == NULL || proj_y_out == NULL) {
    return;
  }
  // mpColl_80044948_Floor uses the ECB bottom point only when `ecb.bottom.y <= 0`.
  // When the bottom point sits above the root, the stay-airborne floor projection uses root pos
  // instead. This matters for DamageFlyTop hitlag rows where root is below the floor but the ECB
  // bottom is already above it.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044948_Floor
  if ((bottom_y - fighter_y) <= 0.0f) {
    *proj_x_out = bottom_x;
    *proj_y_out = bottom_y;
  } else {
    *proj_x_out = fighter_x;
    *proj_y_out = fighter_y;
  }
}

static inline float mpcoll_pose_ecb_bottom_rel_y(uint8_t char_id, uint32_t anim, uint16_t frame_u16,
                                                 uint8_t force_zero_bottom) {
  if (force_zero_bottom) {
    return 0.0f;
  }
  return msl_ecb_bottom_rel_y(char_id, anim, (int)frame_u16);
}

static inline float mpcoll_action_pose_ecb_bottom_rel_y(uint8_t char_id, uint16_t action_id,
                                                        int16_t action_frame,
                                                        uint8_t force_zero_bottom) {
  if (force_zero_bottom) {
    return 0.0f;
  }
  const uint16_t smid = msl_motion_state_submotion_id(char_id, action_id);
  if (smid == 0xFFFFu) {
    return 0.0f;
  }
  const uint16_t frame = msl_ecb_frame_u16_from_anim_frame((float)action_frame);
  return msl_ecb_bottom_rel_y(char_id, smid, (int)frame);
}

static inline void mpcoll_bottom_world_point_from_rel(MslEcbBottomWorldPoint* out, float pos_x,
                                                      float pos_y, float rel_y,
                                                      uint16_t frame_u16) {
  if (out == NULL) {
    return;
  }
  out->x = pos_x;
  out->y = pos_y + rel_y;
  out->rel_y = rel_y;
  out->frame_u16 = frame_u16;
}

static inline void mpcoll_ecb_world_points_from_rel(MslEcbWorldPoints* out, float pos_x,
                                                    float pos_y, float bottom_rel_y,
                                                    float top_rel_y, float left_rel_x,
                                                    float right_rel_x, float side_rel_y,
                                                    uint16_t frame_u16) {
  if (out == NULL) {
    return;
  }
  out->bottom_rel_y = bottom_rel_y;
  out->top_rel_y = top_rel_y;
  out->left_rel_x = left_rel_x;
  out->right_rel_x = right_rel_x;
  out->side_rel_y = side_rel_y;
  out->frame_u16 = frame_u16;
  out->bottom_x = pos_x;
  out->bottom_y = pos_y + bottom_rel_y;
  out->top_x = pos_x;
  out->top_y = pos_y + top_rel_y;
  out->left_x = pos_x + left_rel_x;
  out->left_y = pos_y + side_rel_y;
  out->right_x = pos_x + right_rel_x;
  out->right_y = pos_y + side_rel_y;
}

static inline uint8_t mpcoll_rel_ecb_is_finite(float bottom_rel_y, float top_rel_y,
                                               float left_rel_x, float right_rel_x,
                                               float side_rel_y) {
  return (uint8_t)(isfinite(bottom_rel_y) && isfinite(top_rel_y) && isfinite(left_rel_x) &&
                   isfinite(right_rel_x) && isfinite(side_rel_y));
}

static inline uint8_t mpcoll_state_current_ecb_points(const MslBatch* batch, size_t idx,
                                                      MslEcbWorldPoints* out, float pos_x,
                                                      float pos_y, uint16_t frame_u16) {
  const float bottom_rel_y = batch->state.coll_ecb_bottom_rel_y[idx];
  const float top_rel_y = batch->state.coll_ecb_top_rel_y[idx];
  const float left_rel_x = batch->state.coll_ecb_left_rel_x[idx];
  const float right_rel_x = batch->state.coll_ecb_right_rel_x[idx];
  const float side_rel_y = batch->state.coll_ecb_side_rel_y[idx];
  if (!batch->state.coll_ecb_bottom_valid[idx] ||
      !mpcoll_rel_ecb_is_finite(bottom_rel_y, top_rel_y, left_rel_x, right_rel_x, side_rel_y)) {
    return 0u;
  }
  mpcoll_ecb_world_points_from_rel(out, pos_x, pos_y, bottom_rel_y, top_rel_y, left_rel_x,
                                   right_rel_x, side_rel_y, frame_u16);
  return 1u;
}

static inline void mpcoll_store_current_ecb_points(MslBatch* batch, size_t idx,
                                                   const MslEcbWorldPoints* ecb) {
  batch->state.coll_ecb_bottom_rel_y[idx] = ecb->bottom_rel_y;
  batch->state.coll_ecb_top_rel_y[idx] = ecb->top_rel_y;
  batch->state.coll_ecb_left_rel_x[idx] = ecb->left_rel_x;
  batch->state.coll_ecb_right_rel_x[idx] = ecb->right_rel_x;
  batch->state.coll_ecb_side_rel_y[idx] = ecb->side_rel_y;
}

static inline void mpcoll_store_prev_ecb_points(MslBatch* batch, size_t idx,
                                                const MslEcbWorldPoints* ecb) {
  batch->state.coll_prev_ecb_bottom_rel_y[idx] = ecb->bottom_rel_y;
  batch->state.coll_prev_ecb_top_rel_y[idx] = ecb->top_rel_y;
  batch->state.coll_prev_ecb_left_rel_x[idx] = ecb->left_rel_x;
  batch->state.coll_prev_ecb_right_rel_x[idx] = ecb->right_rel_x;
  batch->state.coll_prev_ecb_side_rel_y[idx] = ecb->side_rel_y;
}

static inline void mpcoll_store_desired_ecb_points(MslBatch* batch, size_t idx,
                                                   const MslEcbWorldPoints* ecb) {
  batch->state.coll_desired_ecb_bottom_rel_y[idx] = ecb->bottom_rel_y;
  batch->state.coll_desired_ecb_top_rel_y[idx] = ecb->top_rel_y;
  batch->state.coll_desired_ecb_left_rel_x[idx] = ecb->left_rel_x;
  batch->state.coll_desired_ecb_right_rel_x[idx] = ecb->right_rel_x;
  batch->state.coll_desired_ecb_side_rel_y[idx] = ecb->side_rel_y;
}

static inline void mpcoll_discard_callback_floor_result(const MslMpcollContext* ctx) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  batch->state.coll_floor_result_valid[idx] = 0u;
  batch->state.coll_floor_result_source[idx] = (uint8_t)MSL_MPCOLL_FLOOR_RESULT_NONE;
  batch->state.coll_floor_result_segment_id[idx] = 0xFFFFu;
  batch->state.coll_floor_result_contact_x[idx] = 0.0f;
  batch->state.coll_floor_result_contact_y[idx] = 0.0f;
  batch->state.coll_floor_result_normal_x[idx] = 0.0f;
  batch->state.coll_floor_result_normal_y[idx] = 1.0f;
}

static inline void mpcoll_clear_callback_floor_result(const MslMpcollContext* ctx, float prev_x,
                                                      float prev_y, float cur_x, float cur_y) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  mpcoll_discard_callback_floor_result(ctx);
  batch->state.coll_substep_prev_pos_x[idx] = prev_x;
  batch->state.coll_substep_prev_pos_y[idx] = prev_y;
  batch->state.coll_substep_cur_pos_x[idx] = cur_x;
  batch->state.coll_substep_cur_pos_y[idx] = cur_y;
}

static inline void mpcoll_record_callback_floor_result(const MslMpcollContext* ctx, uint8_t source,
                                                       uint16_t segment_id, float contact_x,
                                                       float contact_y, float normal_x,
                                                       float normal_y) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  // Callback-local floor result scratch mirrors the source CollData floor hit before wrapper
  // writeback consumes it. Direct floor hits and mpColl_8004A908 retries both flow through this
  // lane so later publication reads a single source-shaped contact record.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044628_Floor,mpColl_8004A908_Floor}
  batch->state.coll_floor_result_valid[idx] = 1u;
  batch->state.coll_floor_result_source[idx] = source;
  batch->state.coll_floor_result_segment_id[idx] = segment_id;
  batch->state.coll_floor_result_contact_x[idx] = contact_x;
  batch->state.coll_floor_result_contact_y[idx] = contact_y;
  batch->state.coll_floor_result_normal_x[idx] = normal_x;
  batch->state.coll_floor_result_normal_y[idx] = normal_y;
}

static inline void mpcoll_floor_contact_from_callback_result(const MslMpcollContext* ctx,
                                                             MslMpcollFloorContact* io) {
  if (ctx == NULL || ctx->batch == NULL || io == NULL ||
      ctx->batch->state.coll_floor_result_valid[ctx->idx] == 0u) {
    return;
  }
  const MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  io->ground_id = batch->state.coll_floor_result_segment_id[idx];
  io->contact_x = batch->state.coll_floor_result_contact_x[idx];
  io->contact_y = batch->state.coll_floor_result_contact_y[idx];
  io->normal_x = batch->state.coll_floor_result_normal_x[idx];
  io->normal_y = batch->state.coll_floor_result_normal_y[idx];
}

static inline uint8_t action_allows_floor_edge_snap(uint16_t a) {
  // Decomp: mpColl_8004A45C_Floor (edge snap) is used by mpColl_8004B2DC (flags=2), which is
  // called by ft_800827A0. Motion states that use ft_80084104 (ft_800827A0) as their collision
  // callback include:
  // - DownAttack, DownForward, DownBack (DownRoll)
  // - EscapeF/EscapeB/EscapeN (rolls / spotdodge)
  // - Grounded attacks (Attack11..AttackLw4), including AttackDash and AttackS4S.
  // - Common AppealSR/SL through ftCo_AppealS_Coll -> ft_80084104.
  // - Grounded Catch/CatchDash and the later CatchPull/CatchWait/CatchAttack/CatchCut family
  //   through ftCo_Catch*_Coll -> ft_800841B8 -> ft_800827A0.
  // - Grounded ThrowF/B/Hi/Lw through ftCo_Throw*_Coll -> ft_800841B8 -> ft_800827A0.
  // - Fox/Falco grounded SpecialSEnd, whose collision callback uses ft_800827A0 after the main
  //   Side-B travel phase has already converted through ft_80082708 when floor is lost.
  // - PassiveStandF/B tech-roll grounded continuation.
  //
  // DownWait/DownStand use ft_80083F88 -> ft_80082708 -> mpColl_8004B108 (allow-ground-to-air),
  // NOT ft_80084104 -> ft_800827A0 -> mpColl_8004B2DC (edge snap).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_DownWait_Coll,ftCo_DownStand_Coll}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_DownAttack_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80084104}
  //
  // Implementation note: we gate by action_id here as a proxy for "this motion state uses the
  // ft_80084104 collision callback chain". We intentionally do not include locomotion states
  // (Walk/Run/Dash/etc.) so walking/running off ledges still produces a ground->air transition.
  //
  // This lite sim uses the same edge-snap fallback when mpLib_8004DD90_Floor projection fails on
  // a persisted floor line, to avoid spurious ground loss at floor endpoints/seams near the FD
  // ledge.
  //
  // Decomp anchors:
  // - refs/melee/src/melee/ft/ft_081B.c::ft_80084104 (calls ft_800827A0)
  // - refs/melee/src/melee/ft/ft_081B.c::ft_800827A0 (calls mpColl_8004B2DC)
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
  // - refs/melee/src/melee/ft/ft_081B.c::ft_80082708 (calls mpColl_8004B108)
  // - refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B2DC (uses mpColl_8004A45C_Floor)
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::ftCo_Escape_Coll
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_Coll
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_Coll
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
  //     ftCo_Catch_Coll,ftCo_CatchDash_Coll,ftCo_CatchPull_Coll,ftCo_CatchWait_Coll,
  //     ftCo_CatchAttack_Coll,ftCo_CatchCut_Coll}
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_AppealS_Coll
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_Throw{F,B,Hi,Lw}_Coll
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveStand.c::ftCo_PassiveStand_Coll
  if (a >= (uint16_t)MSL_ACT_ATTACK_11 && a <= (uint16_t)MSL_ACT_ATTACK_LW4) {
    return 1;
  }
  switch (a) {
    // DownBound intentionally excluded from this bucket:
    // - generic edge-snap bucket here models ft_800827A0 -> mpColl_8004B2DC users,
    // - DownBound_Coll uses ft_80082708 -> mpColl_8004B108 (allow-ground-to-air path) and should
    //   not inherit the ft_800827A0 edge-snap helper semantics.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80082708,ft_800827A0}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_8004B2DC}
    // Decomp: DownWait/DownStand use ft_80083F88 -> ft_80082708 -> mpColl_8004B108
    // (allow-ground-to-air), not ft_80084104 -> ft_800827A0 -> mpColl_8004B2DC (edge snap).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownWait_Coll
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownStand.c::ftCo_DownStand_Coll
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80084104}
    case MSL_ACT_DOWN_ATTACK_U:
    case MSL_ACT_DOWN_FOWARD_U:
    case MSL_ACT_DOWN_BACK_U:
    case MSL_ACT_DOWN_ATTACK_D:
    case MSL_ACT_DOWN_FOWARD_D:
    case MSL_ACT_DOWN_BACK_D:
    case MSL_ACT_ESCAPE_F:
    case MSL_ACT_ESCAPE_B:
    case MSL_ACT_ESCAPE_N:
    case MSL_ACT_PASSIVE_STAND_F:
    case MSL_ACT_PASSIVE_STAND_B:
    case MSL_ACT_APPEAL_SR:
    case MSL_ACT_APPEAL_SL:
    case MSL_ACT_CATCH:
    case MSL_ACT_CATCH_PULL:
    case MSL_ACT_CATCH_DASH:
    case MSL_ACT_CATCH_DASH_PULL:
    case MSL_ACT_CATCH_WAIT:
    case MSL_ACT_CATCH_ATTACK:
    case MSL_ACT_CATCH_CUT:
    case MSL_ACT_THROW_F:
    case MSL_ACT_THROW_B:
    case MSL_ACT_THROW_HI:
    case MSL_ACT_THROW_LW:
    case MSL_ACT_FX_SPECIAL_S_END:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t action_is_down_bound(uint16_t a) {
  return (uint8_t)(a == (uint16_t)MSL_ACT_DOWN_BOUND_U || a == (uint16_t)MSL_ACT_DOWN_BOUND_D);
}

static inline uint8_t action_uses_landing_floor_release_coll(uint16_t a) {
  // Landing-family collision callbacks route through ft_80084280 -> mpColl_8004B4B0
  // (inline2 flags=1), which uses the current floor-id release helper (mpColl_8004A678_Floor)
  // rather than the generic edge-snap helper path (mpColl_8004A45C_Floor).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B4B0,mpColl_8004A678_Floor,mpColl_8004A45C_Floor}
  switch (a) {
    case MSL_ACT_LANDING:
    case MSL_ACT_LANDING_FALL_SPECIAL:
    case MSL_ACT_LANDING_AIR_N:
    case MSL_ACT_LANDING_AIR_F:
    case MSL_ACT_LANDING_AIR_B:
    case MSL_ACT_LANDING_AIR_HI:
    case MSL_ACT_LANDING_AIR_LW:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t floor_lines_connected(const MslStageFloorGraph* g, int a, int b) {
  if (g == NULL) {
    return 0;
  }
  if (a < 0 || b < 0) {
    return 0;
  }
  if (a == b) {
    return 1;
  }
  // Decomp shape: mpLinesConnected(line_a, line_b) checks connectivity in the stage collision
  // graph. FD is a simple chain, so a bounded walk is sufficient.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor (uses mpLineGetPrev/Next traversal)
  int cur = a;
  for (size_t i = 0; i < g->line_count; i++) {
    const int16_t next = g->lines[cur].next;
    if (next < 0) {
      break;
    }
    if (next == b) {
      return 1;
    }
    cur = (int)next;
  }
  cur = a;
  for (size_t i = 0; i < g->line_count; i++) {
    const int16_t prev = g->lines[cur].prev;
    if (prev < 0) {
      break;
    }
    if (prev == b) {
      return 1;
    }
    cur = (int)prev;
  }
  return 0;
}

static inline uint8_t stage_floor_graph_has_height_platform_transform(uint32_t stage_id,
                                                                      const MslStageFloorGraph* g) {
  if (g == NULL || g->lines == NULL) {
    return 0u;
  }
  for (size_t i = 0; i < g->line_count; i++) {
    if (stage_collision_floor_line_has_height_platform_transform(stage_id, g->lines[i].segment_i)) {
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t stage_height_platform_line_has_current_source(const MslBatch* batch, int bi,
                                                                    uint32_t stage_id,
                                                                    uint16_t segment_i) {
  if (batch == NULL || bi < 0) {
    return 0u;
  }
  uint8_t platform_id = 0u;
  if (!stage_collision_floor_line_platform_transform_id(stage_id, segment_i, &platform_id) ||
      platform_id >= 2u) {
    return 0u;
  }
  const uint8_t source =
      batch->state.stage_fod_platform_height_source[(size_t)bi * 2u + (size_t)platform_id];
  return (uint8_t)((source & (uint8_t)(MSL_FOD_PLATFORM_HEIGHT_SOURCE_DIRECT_EVENT |
                                       MSL_FOD_PLATFORM_HEIGHT_SOURCE_GROUND_CONTACT |
                                       MSL_FOD_PLATFORM_HEIGHT_SOURCE_SAME_STEP_CONTACT)) != 0u);
}

static inline uint8_t stage_height_platform_line_has_same_step_contact_source(const MslBatch* batch,
                                                                              int bi,
                                                                              uint32_t stage_id,
                                                                              uint16_t segment_i) {
  if (batch == NULL || bi < 0) {
    return 0u;
  }
  uint8_t platform_id = 0u;
  if (!stage_collision_floor_line_platform_transform_id(stage_id, segment_i, &platform_id) ||
      platform_id >= 2u) {
    return 0u;
  }
  const uint8_t source =
      batch->state.stage_fod_platform_height_source[(size_t)bi * 2u + (size_t)platform_id];
  return (uint8_t)((source & (uint8_t)MSL_FOD_PLATFORM_HEIGHT_SOURCE_SAME_STEP_CONTACT) != 0u);
}

static inline MslStageFloorLine floor_line_world_for_env(const MslBatch* batch, int bi,
                                                         const MslStageFloorGraph* g,
                                                         int line_idx) {
  MslStageFloorLine out = {0};
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return out;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  if (line->platform_transform_kind == 0u) {
    return *line;
  }
  (void)stage_collision_floor_line_world(batch, bi, line, &out);
  return out;
}

static inline uint8_t floor_x_within_line_bounds(const MslBatch* batch, int bi,
                                                 const MslStageFloorGraph* g, int line_idx,
                                                 float x) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  const float min_x = (l.x0 < l.x1) ? l.x0 : l.x1;
  const float max_x = (l.x0 > l.x1) ? l.x0 : l.x1;
  return (uint8_t)((x >= (min_x - k_floor_x_end_clamp) && x <= (max_x + k_floor_x_end_clamp)) ? 1u
                                                                                              : 0u);
}

static inline uint8_t floor_x_within_line_segment_strict(const MslBatch* batch, int bi,
                                                         const MslStageFloorGraph* g, int line_idx,
                                                         float x) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  const float min_x = (l.x0 < l.x1) ? l.x0 : l.x1;
  const float max_x = (l.x0 > l.x1) ? l.x0 : l.x1;
  return (uint8_t)((x >= min_x && x <= max_x) ? 1u : 0u);
}

static inline uint8_t floor_line_admitted_by_source_callback(const MslBatch* batch, size_t idx,
                                                             const MslStageFloorGraph* g,
                                                             uint32_t stage_id, int line_idx,
                                                             uint16_t skip_platform_segment_i,
                                                             const MslCommonParams* c);

static inline uint8_t floor_line_y_at_x_for_env(const MslBatch* batch, int bi,
                                                const MslStageFloorGraph* g, int line_idx, float x,
                                                float* y_out) {
  if (y_out == NULL || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  if (!floor_x_within_line_bounds(batch, bi, g, line_idx, x)) {
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  const float dx = l.x1 - l.x0;
  if (fabsf(dx) <= k_floor_horiz_dy_thresh) {
    return 0u;
  }
  const float t = (x - l.x0) / dx;
  *y_out = l.y0 + ((l.y1 - l.y0) * t);
  return 1u;
}

static inline uint8_t grounded_same_step_height_platform_reproject(
    const MslBatch* batch, int bi, const MslStageFloorGraph* g, uint32_t stage_id, size_t idx,
    uint16_t action_id, uint16_t action_frame, int current_line_idx, int* out_line_idx,
    float* out_y_corr) {
  if (batch == NULL || g == NULL || g->lines == NULL || out_line_idx == NULL ||
      out_y_corr == NULL || current_line_idx < 0 || (size_t)current_line_idx >= g->line_count ||
      !action_uses_landing_floor_release_coll(action_id) || action_frame > 1u) {
    return 0u;
  }
  // grIzumi updates FoD height-platform JObjs before fighter map callbacks. If a one-step seed
  // starts on the static main floor but the sparse seed lane proves same-step platform contact,
  // Landing_Coll/ft_80084280 consumes the transformed platform floor for this callback instead of
  // stale CollData.floor.index. Keep this tied to MSLSTG01 height-platform data plus the transient
  // SAME_STEP_CONTACT source bit; the frame scheduler clears the bit after the seeded frame.
  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  int best_line_idx = -1;
  float best_lift = 0.0f;
  for (size_t li = 0; li < g->line_count; li++) {
    const uint16_t segment_i = g->lines[li].segment_i;
    if (!g->lines[li].is_platform ||
        !stage_collision_floor_line_has_height_platform_transform(stage_id, segment_i) ||
        !stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, segment_i) ||
        !stage_height_platform_line_has_same_step_contact_source(batch, bi, stage_id, segment_i) ||
        !floor_x_within_line_bounds(batch, bi, g, (int)li, batch->state.pos_x[idx])) {
      continue;
    }
    float line_y = 0.0f;
    if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, batch->state.pos_x[idx], &line_y)) {
      continue;
    }
    const float lift = (line_y + k_floor_y_bias) - batch->state.pos_y[idx];
    if (lift < 0.0f) {
      continue;
    }
    if (best_line_idx < 0 || lift < best_lift ||
        (lift == best_lift && g->lines[li].segment_i < g->lines[(size_t)best_line_idx].segment_i)) {
      best_line_idx = (int)li;
      best_lift = lift;
    }
  }
  if (best_line_idx < 0 || best_line_idx == current_line_idx) {
    return 0u;
  }
  *out_line_idx = best_line_idx;
  *out_y_corr = best_lift;
  return 1u;
}

static inline uint8_t floor_line_x_near_endpoint_for_env(const MslBatch* batch, int bi,
                                                         const MslStageFloorGraph* g, int line_idx,
                                                         float x, float max_dist) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count || max_dist < 0.0f) {
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  const float d0 = fabsf(x - l.x0);
  const float d1 = fabsf(x - l.x1);
  return (d0 <= max_dist || d1 <= max_dist) ? 1u : 0u;
}

static uint8_t floor_find_low_raw_floor_contact(const MslBatch* batch, size_t idx, int bi,
                                                const MslStageFloorGraph* g, uint32_t stage_id,
                                                float x, float y, float max_lift,
                                                uint16_t skip_platform_segment_i,
                                                const MslCommonParams* c, int* out_line_idx,
                                                float* out_y, float* out_nx, float* out_ny) {
  if (batch == NULL || g == NULL || out_line_idx == NULL || out_y == NULL) {
    return 0u;
  }

  uint8_t found = 0u;
  float best_lift = FLT_MAX;
  int best_idx = -1;
  float best_y = 0.0f;
  float best_nx = 0.0f;
  float best_ny = 1.0f;

  for (size_t li = 0; li < g->line_count; li++) {
    if (g->lines[li].is_platform) {
      continue;
    }
    if (!floor_line_admitted_by_source_callback(batch, idx, g, stage_id, (int)li,
                                                skip_platform_segment_i, c)) {
      continue;
    }
    float line_y = 0.0f;
    if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, x, &line_y)) {
      continue;
    }
    if (line_y > k_floor_y_bias) {
      continue;
    }
    const float lift = line_y - y;
    if (lift < -k_floor_y_bias || lift > max_lift) {
      continue;
    }
    const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, (int)li);
    float nx = -(l.y1 - l.y0);
    float ny = l.x1 - l.x0;
    const float len = sqrtf(nx * nx + ny * ny);
    if (len > 0.0f) {
      nx /= len;
      ny /= len;
    } else {
      nx = 0.0f;
      ny = 1.0f;
    }
    if (!found || lift < best_lift ||
        (lift == best_lift && g->lines[li].segment_i < g->lines[(size_t)best_idx].segment_i)) {
      found = 1u;
      best_lift = lift;
      best_idx = (int)li;
      best_y = line_y;
      best_nx = nx;
      best_ny = ny;
    }
  }

  if (!found) {
    return 0u;
  }
  *out_line_idx = best_idx;
  *out_y = best_y;
  if (out_nx != NULL) {
    *out_nx = best_nx;
  }
  if (out_ny != NULL) {
    *out_ny = best_ny;
  }
  return 1u;
}

static inline uint8_t grounded_action_allows_platform_carry_y_correction(uint16_t action_id,
                                                                         uint16_t action_frame) {
  // Source/table-backed grounded floor-persistence owner. MSLMSO01 groups grounded callbacks that
  // keep CollData.floor.index attached through common map-collision owners (`ft_80084280`,
  // `ft_800844EC`, `ft_80084104`, `ft_800841B8`, or adjacent guarded variants), so generated
  // legal-stage slopes and moving platform floors should consume signed mpLib_8004DD90_Floor
  // correction rather than preserving a stale root height.
  //
  // Landing/LandingAir entry keeps the existing root/ECB handoff guard: the entry frame owns a
  // separate landing root projection before sustained grounded persistence takes over.
  //
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC,ft_80084104,ft_800841B8}
  // refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B2DC,mplib.c::mpLib_8004DD90_Floor}
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class GROUNDED_STAGE_OBJECT_CARRY_COLL)
  if (action_uses_landing_floor_release_coll(action_id) && action_frame <= 1u) {
    return 0u;
  }
  return msl_motion_state_common_class_has(action_id,
                                           MSL_MS_CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL);
}

static inline uint8_t grounded_action_allows_stage_object_platform_carry(uint16_t action_id) {
  // Table-backed MotionState collision owner. The generated class groups grounded callbacks that
  // preserve an existing floor attachment through common map-collision owners and should inherit a
  // moving stage object's transform before projection. DownBound/DownWait/DownStand/DownSpot,
  // Passive, and DownDamage callbacks are intentionally excluded by the extractor because they
  // route through separate downed/damage owners.
  //
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC,ft_80084104,ft_800845B4}
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class GROUNDED_STAGE_OBJECT_CARRY_COLL)
  return msl_motion_state_common_class_has(action_id,
                                           MSL_MS_CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL);
}

static inline uint8_t grounded_entry_same_step_height_platform_admits_line(
    const MslBatch* batch, int bi, size_t idx, uint32_t stage_id, uint16_t action_id,
    uint16_t selected_segment_i) {
  if (!stage_collision_floor_line_has_height_platform_transform(stage_id, selected_segment_i) ||
      !stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi,
                                                                          selected_segment_i) ||
      !stage_height_platform_line_has_same_step_contact_source(batch, bi, stage_id,
                                                               selected_segment_i)) {
    return 0u;
  }
  // Same-step FoD platform contact can replace a stale seed floor index during a grounded action
  // entry callback: Fighter_ChangeMotionState updates the motion state before the new action's
  // map-collision callback consumes CollData via the common grounded stage-object path. Sustained
  // same-action grounded states must not keep using a sparse seed source bit to jump to a different
  // height platform; live stage/contact code must reassert that authority on later frames.
  //
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC}
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 GROUNDED_STAGE_OBJECT_CARRY_COLL)
  return (uint8_t)(grounded_action_allows_stage_object_platform_carry(action_id) &&
                   batch->state.seed_prev_action_id[idx] != action_id);
}

static inline uint8_t floor_line_is_generated_stage_slope(const MslBatch* batch, int bi,
                                                          const MslStageFloorGraph* g,
                                                          int line_idx) {
  if (batch == NULL || bi < 0 || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  if (g->lines[(size_t)line_idx].fighter_solid == 0u) {
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  return (uint8_t)(l.y0 != l.y1);
}

static inline uint8_t floor_line_is_generated_sloped_ledge(const MslBatch* batch, int bi,
                                                           const MslStageFloorGraph* g,
                                                           int line_idx) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      g->lines[(size_t)line_idx].is_ledge == 0u) {
    return 0u;
  }
  return floor_line_is_generated_stage_slope(batch, bi, g, line_idx);
}

static inline int8_t platform_pass_current_raw_stick_y(const MslBatch* batch, size_t idx) {
  enum {
    MSL_LOCAL_UCF_PADBUF_SIZE = 4u,
    MSL_LOCAL_UCF_PADBUF_MASK = MSL_LOCAL_UCF_PADBUF_SIZE - 1u
  };
  if (batch == NULL || batch->state.ucf_padbuf_index == NULL ||
      batch->state.ucf_padbuf_stick_y == NULL) {
    return (batch != NULL && batch->state.input_main_y != NULL) ? batch->state.input_main_y[idx]
                                                                : 0;
  }
  const uint8_t base = batch->state.ucf_padbuf_index[idx];
  const uint8_t slot = (uint8_t)(base & (uint8_t)MSL_LOCAL_UCF_PADBUF_MASK);
  return batch->state.ucf_padbuf_stick_y[idx * (size_t)MSL_LOCAL_UCF_PADBUF_SIZE + (size_t)slot];
}

static inline uint8_t platform_pass_input_below_raw_threshold(const MslBatch* batch, size_t idx,
                                                              const MslCommonParams* c) {
  if (c == NULL) {
    return 0u;
  }
  // The FoD transformed-platform floor-skip seed lane is derived from Slippi's raw signed stick
  // byte before the simulator's 80-unit UCF clamp scale. Use the same p_ftCommonData->x25C
  // threshold on the raw pad-byte scale for this pass-through owner; otherwise shallow release
  // rows just below the 80-scale threshold are incorrectly retained as platform passes.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_procMap}
  const int threshold_i8 = (int)floorf(c->platform_air_land_stick_y_threshold * 127.0f);
  const int8_t stick_y_i8 = platform_pass_current_raw_stick_y(batch, idx);
  return ((int)stick_y_i8 <= threshold_i8) ? 1u : 0u;
}

static inline void publish_common_air_transformed_platform_skip_from_root_crossing(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslCommonParams* c) {
  if (batch == NULL || c == NULL || g == NULL || g->lines == NULL ||
      batch->state.floor_skip_segment_id == NULL ||
      batch->state.floor_skip_segment_id[idx] != 0xFFFFu || batch->state.on_ground[idx] != 0u) {
    return;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (!action_uses_ftco_80096cc8_floor_callback(action_id)) {
    return;
  }
  const uint8_t current_stick_released =
      (stick_i8_to_unit(batch->state.input_main_y[idx]) > c->platform_air_land_stick_y_threshold)
          ? 1u
          : 0u;
  if (current_stick_released && stick_i8_to_unit(batch->state.prev_input_main_y[idx]) >
                                    c->platform_air_land_stick_y_threshold) {
    return;
  }
  const float prev_y = batch->state.floor_sweep_prev_pos_y[idx];
  const float y = batch->state.pos_y[idx];
  if (!isfinite(prev_y) || !isfinite(y) || !(prev_y > y)) {
    return;
  }
  const float x = batch->state.pos_x[idx];
  for (size_t li = 0; li < g->line_count; li++) {
    const uint16_t segment_i = g->lines[li].segment_i;
    if (!g->lines[li].is_platform ||
        !stage_collision_floor_line_has_height_platform_transform(stage_id, segment_i) ||
        !stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, segment_i)) {
      continue;
    }
    float line_y = 0.0f;
    if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, x, &line_y)) {
      continue;
    }
    if (prev_y > (line_y + k_floor_y_bias) && y < (line_y - k_floor_y_bias)) {
      uint8_t platform_id = 0u;
      const uint8_t same_step_contact_source =
          (current_stick_released &&
           stage_collision_floor_line_platform_transform_id(stage_id, segment_i, &platform_id) &&
           platform_id < 2u &&
           (batch->state.stage_fod_platform_height_source[(size_t)bi * 2u + (size_t)platform_id] &
            (uint8_t)MSL_FOD_PLATFORM_HEIGHT_SOURCE_SAME_STEP_CONTACT) != 0u)
              ? 1u
              : 0u;
      if (same_step_contact_source) {
        continue;
      }
      // Common-air transformed-platform pass-through owner:
      // ft_800831CC/ft_800835B0 callbacks pass ftCo_80096CC8 to mpColl_80044628_Floor; while the
      // callback-visible stick window is below x25C, the source rejects platform lines. Preserve
      // the rejected FoD platform as CollData.floor_skip for later same-action callback frames,
      // matching the generated MSLSTG01 transformed-platform seed lane without relying on replay
      // rows. If the current stick has released and the only platform owner is a same-step contact
      // source, do not synthesize a new skip from stale prior input; source has already admitted
      // that floor for the current callback.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
      // refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
      // data/stages/bin/griz.bin::MSLSTG01 platform_transforms
      batch->state.floor_skip_segment_id[idx] = segment_i;
      return;
    }
  }
}

static inline uint8_t jumpaerial_terminal_fastfall_descent(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const MslCharParams* chp = msl_char_params(batch->state.char_id[idx]);
  if (chp == NULL || !isfinite(chp->terminal_vel) || !(chp->terminal_vel > 0.0f)) {
    return 0u;
  }
  return (uint8_t)(batch->state.speed_y_self[idx] <=
                   -(chp->terminal_vel - k_floor_horiz_dy_thresh));
}

static inline float specialhi_understage_floor_reject_clearance(const MslEcbWorldPoints* prev_ecb) {
  if (prev_ecb == NULL || !isfinite(prev_ecb->bottom_rel_y)) {
    return k_ecb_vertical_unit;
  }
  // Decomp ECB ownership gives the floor/ceiling callbacks a live CollData ECB span. Use the
  // previous bottom-to-root extent plus mpColl's minimum vertical ECB unit as the shallow-penetration
  // allowance instead of a stage/replay-local depth constant.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80042384,mpColl_LoadECB_JObj}
  return fabsf(prev_ecb->bottom_rel_y) + k_ecb_vertical_unit;
}

static inline uint8_t specialhi_understage_floor_clip_action(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI ||
          action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL)
             ? 1u
             : 0u;
}

static inline uint8_t specialhi_floor_candidate_starts_below_source_floor(
    uint16_t action_id, float prev_root_y, float prev_bottom_y, float cur_bottom_y, float floor_y,
    float speed_y_self) {
  if (!specialhi_understage_floor_clip_action(action_id) || speed_y_self >= 0.0f) {
    return 0u;
  }
  const float floor_top = floor_y + k_floor_y_bias;
  return (prev_root_y < floor_top && prev_bottom_y < floor_top && cur_bottom_y < floor_top) ? 1u
                                                                                            : 0u;
}

static inline uint8_t specialairhi_floor_contact_angle_continues_launch(
    uint16_t action_id, uint8_t char_id, float floor_normal_x, float floor_normal_y,
    float speed_x_self, float speed_y_self) {
  if (action_id != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI) {
    return 0u;
  }
  const MslCharParams* chp = msl_char_params(char_id);
  if (chp == NULL) {
    return 0u;
  }
  const float n_mag = sqrtf(floor_normal_x * floor_normal_x + floor_normal_y * floor_normal_y);
  const float v_mag = sqrtf(speed_x_self * speed_x_self + speed_y_self * speed_y_self);
  if (!(n_mag > 0.0f) || !(v_mag > 0.0f)) {
    return 0u;
  }
  float dot = (floor_normal_x * speed_x_self + floor_normal_y * speed_y_self) / (n_mag * v_mag);
  if (dot > 1.0f) {
    dot = 1.0f;
  } else if (dot < -1.0f) {
    dot = -1.0f;
  }
  const float threshold = (90.0f + chp->firefox_bound_angle_degrees) * (MSL_PI_F / 180.0f);
  return (dot > cosf(threshold)) ? 1u : 0u;
}

static inline float mpcoll_floor_projection_lift_allowance(const MslEcbWorldPoints* cur_ecb) {
  if (cur_ecb == NULL || !isfinite(cur_ecb->bottom_rel_y)) {
    return k_ecb_vertical_unit;
  }
  // Floor projection bridges should be bounded by the live CollData ECB bottom-to-root extent, not a
  // replay-local literal. Source mpColl loads the ECB for the callback before floor projection and
  // keeps a minimum vertical unit while tightening the span.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80042384}
  return fabsf(cur_ecb->bottom_rel_y) + k_ecb_vertical_unit;
}

static inline uint8_t grounded_persistence_allows_signed_dd90_y_correction(
    const MslBatch* batch, int bi, const MslStageFloorGraph* g, int current_line_idx,
    int projected_line_idx, uint16_t action_id, uint16_t action_frame) {
  if (g == NULL || projected_line_idx < 0 || (size_t)projected_line_idx >= g->line_count) {
    return 0u;
  }
  // Source mpLib_8004DD90_Floor returns signed projection correction. Keep the old anti-snap
  // guard for flat hard floors, but admitted legal-stage slopes and actively moving transformed
  // platforms must apply downward correction while grounded: otherwise downhill movement and
  // descending FoD platforms leave grounded fighters stranded above their current floor.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
  if (batch == NULL || bi < 0) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[(size_t)bi];
  const uint8_t generated_slope_owner =
      (floor_line_is_generated_stage_slope(batch, bi, g, current_line_idx) ||
       floor_line_is_generated_stage_slope(batch, bi, g, projected_line_idx))
          ? 1u
          : 0u;
  if (action_uses_landing_floor_release_coll(action_id) && action_frame > 1u &&
      generated_slope_owner) {
    // Sustained Landing/LandingAir/LandingFallSpecial callbacks share
    // `ftCo_Landing_Coll -> ft_80084280 -> mpColl_8004B4B0`, so they keep the same grounded floor
    // persistence over legal-stage slopes after the landing entry frame's root/ECB handoff is done.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B4B0,mpColl_8004A678_Floor}
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    return 1u;
  }
  if (grounded_action_allows_platform_carry_y_correction(action_id, action_frame) &&
      generated_slope_owner) {
    // Yoshi's Story and FoD both expose admitted sloped floor segments in MSLSTG01. Grounded
    // persistence should keep the fighter attached through source graph traversal, including
    // slope-to-flat endpoint handoffs, rather than preserving a stale root height over raised lip
    // segments.
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    return 1u;
  }
  if (!grounded_action_allows_platform_carry_y_correction(action_id, action_frame)) {
    return 0u;
  }
  uint8_t platform_id = 0u;
  if (!stage_collision_floor_line_platform_transform_id(
          stage_id, g->lines[(size_t)projected_line_idx].segment_i, &platform_id) ||
      platform_id >= 2u) {
    return 0u;
  }
  const size_t pidx = (size_t)bi * 2u + (size_t)platform_id;
  return (uint8_t)(batch->state.stage_fod_platform_scheduler_valid[pidx] ||
                   batch->state.stage_fod_platform_velocity_valid[pidx]);
}

static void floor_ed5c_endpoints(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                 int line_idx, float* x0_out, float* y0_out, float* x1_out,
                                 float* y1_out) {
  // Decomp: mpLib_8004ED5C expands endpoints by 1 unit for connected endpoints.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  float x0 = l.x0;
  float y0 = l.y0;
  float x1 = l.x1;
  float y1 = l.y1;

  float dist = 0.0f;
  uint8_t have_dist = 0;
  if (l.has_prev_link) {
    const float dx = x0 - x1;
    const float dy = y0 - y1;
    dist = sqrtf(dx * dx + dy * dy);
    have_dist = 1;
    if (dist > k_floor_ed5c_min_dist) {
      x0 += (dx / dist) * k_floor_ed5c_extend;
      y0 += (dy / dist) * k_floor_ed5c_extend;
    }
  }
  if (l.has_next_link) {
    if (!have_dist) {
      const float dx = x0 - x1;
      const float dy = y0 - y1;
      dist = sqrtf(dx * dx + dy * dy);
    }
    if (dist > k_floor_ed5c_min_dist) {
      const float dx = x1 - x0;
      const float dy = y1 - y0;
      x1 += (dx / dist) * k_floor_ed5c_extend;
      y1 += (dy / dist) * k_floor_ed5c_extend;
    }
  }

  *x0_out = x0;
  *y0_out = y0;
  *x1_out = x1;
  *y1_out = y1;
}

static inline uint8_t floor_line_y_at_x_ed5c_for_env(const MslBatch* batch, int bi,
                                                     const MslStageFloorGraph* g, int line_idx,
                                                     float x, float* y_out) {
  if (y_out == NULL || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
  floor_ed5c_endpoints(batch, bi, g, line_idx, &x0, &y0, &x1, &y1);
  const float min_x = (x0 < x1) ? x0 : x1;
  const float max_x = (x0 > x1) ? x0 : x1;
  if (x < (min_x - k_floor_x_end_clamp) || x > (max_x + k_floor_x_end_clamp)) {
    return 0u;
  }
  const float dx = x1 - x0;
  if (fabsf(dx) <= k_floor_horiz_dy_thresh) {
    return 0u;
  }
  const float t = (x - x0) / dx;
  *y_out = y0 + ((y1 - y0) * t);
  return 1u;
}

static int msl_mplib_8004dd90_floor(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                    int line_idx, float x_in, float y_in, float* y_out,
                                    float* nx_out, float* ny_out) {
  // Decomp: mpLib_8004DD90_Floor traverses prev/next (floor-only) and returns a vertical
  // correction and normal for the line under vec->x.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return -1;
  }

  int dir = 0;
  int cur = line_idx;
  float x = 0.0f;
  float x0 = 0.0f, x1 = 0.0f;
  for (;;) {
    const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, cur);
    x0 = l.x0;
    x1 = l.x1;
    x = x_in;
    if (x < x0) {
      if (dir != 1) {
        const int prev = (int)l.prev;
        if (prev < 0) {
          if (x - x0 < -k_floor_x_end_clamp) {
            return -1;
          }
          x = x0;
          break;
        }
        cur = prev;
        dir = -1;
        continue;
      }
      x = x0;
      break;
    }
    if (x > x1) {
      // Decomp shape: the "dir" guard is asymmetric: it sets dir=-1 when traversing prev, but
      // does not set dir=+1 when traversing next.
      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      if (dir != -1) {
        const int next = (int)l.next;
        if (next < 0) {
          if (x - x1 > k_floor_x_end_clamp) {
            return -1;
          }
          x = x1;
          break;
        }
        cur = next;
        continue;
      }
      x = x1;
      break;
    }
    break;
  }

  const MslStageFloorLine out = floor_line_world_for_env(batch, bi, g, cur);
  const float y0 = out.y0;
  const float y1 = out.y1;

  if (y_out != NULL) {
    // decomp: +0.0001 bias
    *y_out = (y1 - y0) * (x - x0) / (x1 - x0) + y0 - y_in + k_floor_y_bias;
  }
  if (nx_out != NULL || ny_out != NULL) {
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
    if (nx_out != NULL) {
      *nx_out = nx;
    }
    if (ny_out != NULL) {
      *ny_out = ny;
    }
  }
  return cur;
}

static inline float mpcoll_absmaxf(float a, float b) {
  const float aa = fabsf(a);
  const float bb = fabsf(b);
  return (aa > bb) ? aa : bb;
}

static uint8_t grounded_sideb_substep_floor_loss(
    const MslBatch* batch, int bi, const MslStageFloorGraph* g, uint8_t char_id, uint16_t action_id,
    int prefer_line_idx, const MslEcbWorldPoints* prev_ecb, const MslEcbWorldPoints* cur_ecb,
    float prev_x, float prev_y, float cur_x, float cur_y, float* sub_prev_x_out,
    float* sub_prev_y_out, float* sub_cur_x_out, float* sub_cur_y_out,
    MslEcbWorldPoints* sub_cur_ecb_out) {
  if (batch == NULL || g == NULL || prev_ecb == NULL || cur_ecb == NULL || sub_prev_x_out == NULL ||
      sub_prev_y_out == NULL || sub_cur_x_out == NULL || sub_cur_y_out == NULL ||
      sub_cur_ecb_out == NULL || prefer_line_idx < 0 ||
      !is_spacie_ground_sideb_allow_ground_to_air_callback(char_id, action_id)) {
    return 0u;
  }

  float max_delta = mpcoll_absmaxf(cur_x - prev_x, cur_y - prev_y);
  max_delta = fmaxf(max_delta, mpcoll_absmaxf(cur_ecb->left_rel_x - prev_ecb->left_rel_x,
                                              cur_ecb->right_rel_x - prev_ecb->right_rel_x));
  max_delta = fmaxf(max_delta, mpcoll_absmaxf(cur_ecb->top_rel_y - prev_ecb->top_rel_y,
                                              cur_ecb->side_rel_y - prev_ecb->side_rel_y));
  if (!(max_delta > k_mpcoll_substep_max_delta)) {
    return 0u;
  }

  const int steps = ((int)(max_delta / k_mpcoll_substep_max_delta)) + 1;
  if (steps <= 1) {
    return 0u;
  }
  const float inv_steps = 1.0f / (float)steps;
  const float step_x = (cur_x - prev_x) * inv_steps;
  const float step_y = (cur_y - prev_y) * inv_steps;

  float sub_prev_x = prev_x;
  float sub_prev_y = prev_y;
  for (int step = 1; step <= steps; step++) {
    const float t = (float)step * inv_steps;
    const float sub_x = prev_x + step_x * (float)step;
    const float sub_y = prev_y + step_y * (float)step;
    MslEcbWorldPoints sub_ecb = {
        .bottom_x = sub_x,
        .bottom_y =
            sub_y + prev_ecb->bottom_rel_y + (cur_ecb->bottom_rel_y - prev_ecb->bottom_rel_y) * t,
        .top_x = sub_x,
        .top_y = sub_y + prev_ecb->top_rel_y + (cur_ecb->top_rel_y - prev_ecb->top_rel_y) * t,
        .left_x = sub_x + prev_ecb->left_rel_x + (cur_ecb->left_rel_x - prev_ecb->left_rel_x) * t,
        .left_y = sub_y + prev_ecb->side_rel_y + (cur_ecb->side_rel_y - prev_ecb->side_rel_y) * t,
        .right_x =
            sub_x + prev_ecb->right_rel_x + (cur_ecb->right_rel_x - prev_ecb->right_rel_x) * t,
        .right_y = sub_y + prev_ecb->side_rel_y + (cur_ecb->side_rel_y - prev_ecb->side_rel_y) * t,
        .left_rel_x = prev_ecb->left_rel_x + (cur_ecb->left_rel_x - prev_ecb->left_rel_x) * t,
        .right_rel_x = prev_ecb->right_rel_x + (cur_ecb->right_rel_x - prev_ecb->right_rel_x) * t,
        .bottom_rel_y =
            prev_ecb->bottom_rel_y + (cur_ecb->bottom_rel_y - prev_ecb->bottom_rel_y) * t,
        .top_rel_y = prev_ecb->top_rel_y + (cur_ecb->top_rel_y - prev_ecb->top_rel_y) * t,
        .side_rel_y = prev_ecb->side_rel_y + (cur_ecb->side_rel_y - prev_ecb->side_rel_y) * t,
        .frame_u16 = cur_ecb->frame_u16,
    };
    if (msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, sub_ecb.bottom_x, sub_ecb.bottom_y,
                                 NULL, NULL, NULL) < 0) {
      // Source stop point: mpColl_80043754 advances CollData.cur_pos one substep at a time, and
      // mpColl_8004ACE4 sets the callback stop bit after the carried floor cannot be projected and
      // the grounded callback falls through the air collision helper. Publish the intermediate
      // cur_pos, not the already-integrated root, for the following GroundToAir transition.
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_8004ACE4}
      *sub_prev_x_out = sub_prev_x;
      *sub_prev_y_out = sub_prev_y;
      *sub_cur_x_out = sub_x;
      *sub_cur_y_out = sub_y;
      *sub_cur_ecb_out = sub_ecb;
      return 1u;
    }
    sub_prev_x = sub_x;
    sub_prev_y = sub_y;
  }
  return 0u;
}

static inline uint16_t platform_floor_skip_segment_id(const MslBatch* batch, size_t idx,
                                                      uint32_t stage_id) {
  if (batch == NULL) {
    return 0xFFFFu;
  }
  if (batch->state.floor_skip_segment_id != NULL &&
      batch->state.floor_skip_segment_id[idx] != 0xFFFFu) {
    const uint16_t skip = batch->state.floor_skip_segment_id[idx];
    if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR) {
      // Fighter_ChangeMotionState clears CollData.floor_skip before EscapeAir_Coll owns
      // ft_80082C74. Replay-prefix seeds can still observe the old platform skip lane; do not let
      // that stale Pass/shield-drop floor_skip suppress the EscapeAir floor callback itself.
      // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
      // refs/melee/src/melee/mp/mpcoll.c::mpClearFloorSkip
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
      return 0xFFFFu;
    }
    return (stage_collision_floor_line_is_platform(stage_id, skip) ||
            stage_collision_floor_line_has_height_platform_transform(stage_id, skip))
               ? skip
               : 0xFFFFu;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu ||
      (!stage_collision_floor_line_is_platform(stage_id, ground_id) &&
       !stage_collision_floor_line_has_height_platform_transform(stage_id, ground_id))) {
    return 0xFFFFu;
  }

  const uint16_t action_id = batch->state.action_id[idx];
  if (action_id == (uint16_t)MSL_ACT_PASS ||
      batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_PASS) {
    // Platform floor-skip ownership:
    // - ftCo_8009A228 / ftCo_8009A184 call mpUpdateFloorSkip after entering Pass or an
    //   action-specific air pass state.
    // - mpColl_80044628_Floor rejects a platform when `floor.index == floor_skip`.
    // Backward-compatible seed path: replay rows do not carry hidden CollData.floor_skip, so first
    // Pass frame reseeds recover it from the carried platform floor.index. Runtime Pass entry writes
    // `state.floor_skip_segment_id` directly.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A184,ftCo_8009A228}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
    return ground_id;
  }
  if (action_id != (uint16_t)MSL_ACT_PASS &&
      batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_PASS &&
      batch->state.on_ground[idx] == 0u &&
      stage_collision_floor_line_is_platform(stage_id, ground_id)) {
    // Pass -> airborne destination same-frame floor-skip lifetime:
    // Pass_IASA can enter AttackAir, SpecialAir, EscapeAir, item throw/catch, or other airborne
    // destination callbacks before the map callback, but the source CollData.floor_skip written by
    // the platform-pass entry remains the floor callback's local skip owner for that collision
    // pass. Replay-prefix rows can begin on the first Pass frame before the hidden lane is
    // serialized, so recover the current platform floor.index for this shared Pass IASA handoff.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A228,ftCo_Pass_IASA}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    //   ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll}
    // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
    return ground_id;
  }
  return 0xFFFFu;
}

static inline uint8_t action_consumes_cliff_ledge_floor_owner(uint8_t char_id, uint16_t action_id) {
  enum {
    // MSLMSO01 callback_symbols.json: ftCo_EscapeAir_Coll.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    MSL_MSO_CB_FTCO_ESCAPEAIR_COLL = 339u,
  };
  // EscapeAir_Coll is the Fox/Falco cliff-exit consumer covered by the current RL1 surface:
  // ftCo_EscapeAir_Coll -> ft_80082C74 -> ft_80081D0C -> mpColl_800471F8. The hidden floor line is
  // selected through the shared CollData floor-owner path below only while source ledge-release
  // cooldown is live; this predicate is extracted MotionState callback ownership, not a replay
  // outcome or local action-id slice.
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 coll_cb_by_action)
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800471F8
  return (msl_motion_state_coll_cb_id(char_id, action_id) ==
          (uint16_t)MSL_MSO_CB_FTCO_ESCAPEAIR_COLL)
             ? 1u
             : 0u;
}

static inline uint8_t action_uses_shallow_attackair_platform_ecb_owner(uint8_t char_id,
                                                                       uint16_t action_id) {
  enum {
    // MSLMSO01 callback_symbols.json: ftCo_AttackAir_Coll.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    MSL_MSO_CB_FTCO_ATTACKAIR_COLL = 30u,
  };
  // AttackAir_Coll is the generated MotionState collision callback for the common aerial attack
  // family. The retained FoD transformed-platform ECB-only boundary is further restricted by
  // generated submotion: replay-real AttackAirB rows expose ordinary same-platform landing
  // outcomes, so the callback id alone is too broad. Use extracted submotion ids instead of a local
  // action-id list. This older sustained transformed-platform owner is N/Lw-only; AttackAirHi
  // uses the narrower first create->clear helper below so its later hitbox phase can publish the
  // ordinary platform landing path.
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 coll_cb_by_action)
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 submotion_id)
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
  if (msl_motion_state_coll_cb_id(char_id, action_id) != (uint16_t)MSL_MSO_CB_FTCO_ATTACKAIR_COLL) {
    return 0u;
  }
  const uint16_t smid = msl_motion_state_submotion_id(char_id, action_id);
  return (smid == (uint16_t)MSL_SM_ATTACK_AIR_N || smid == (uint16_t)MSL_SM_ATTACK_AIR_LW) ? 1u
                                                                                           : 0u;
}

static inline uint8_t action_uses_first_phase_attackair_platform_ecb_owner(uint8_t char_id,
                                                                           uint16_t action_id) {
  enum {
    // MSLMSO01 callback_symbols.json: ftCo_AttackAir_Coll.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    MSL_MSO_CB_FTCO_ATTACKAIR_COLL = 30u,
  };
  // First shallow transformed-platform contacts are bounded by generated N/Hi/Lw submotion data
  // plus the MSLFTSC1 first HitCapsule create->clear phase. This admits AttackAirHi only for that
  // first-phase owner; later UpAir hitbox phases stay on the ordinary ft_80082C74 platform handoff.
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 coll_cb_by_action, submotion_id)
  // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox / clear_hitboxes
  if (msl_motion_state_coll_cb_id(char_id, action_id) != (uint16_t)MSL_MSO_CB_FTCO_ATTACKAIR_COLL) {
    return 0u;
  }
  const uint16_t smid = msl_motion_state_submotion_id(char_id, action_id);
  return (smid == (uint16_t)MSL_SM_ATTACK_AIR_N || smid == (uint16_t)MSL_SM_ATTACK_AIR_HI ||
          smid == (uint16_t)MSL_SM_ATTACK_AIR_LW)
             ? 1u
             : 0u;
}

static inline uint8_t action_uses_late_attackair_platform_ecb_owner(uint8_t char_id,
                                                                    uint16_t action_id) {
  enum {
    // MSLMSO01 callback_symbols.json: ftCo_AttackAir_Coll.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
    MSL_MSO_CB_FTCO_ATTACKAIR_COLL = 30u,
  };
  // The late/second-create transformed-platform ECB owner is generated submotion data, not the
  // whole AttackAir_Coll class. AttackAirHi's second phase publishes the ordinary FoD platform
  // landing path; its retained transformed-platform boundary is the first create->clear phase
  // handled by action_uses_shallow_attackair_platform_ecb_owner plus MSLFTSC1 first-phase timing.
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 coll_cb_by_action, submotion_id)
  // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox / clear_hitboxes
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  if (msl_motion_state_coll_cb_id(char_id, action_id) != (uint16_t)MSL_MSO_CB_FTCO_ATTACKAIR_COLL) {
    return 0u;
  }
  const uint16_t smid = msl_motion_state_submotion_id(char_id, action_id);
  return (smid == (uint16_t)MSL_SM_ATTACK_AIR_N || smid == (uint16_t)MSL_SM_ATTACK_AIR_LW) ? 1u
                                                                                           : 0u;
}

static inline uint8_t action_uses_sideb_air_ft_check_ground_and_ledge_floor_coll(
    uint8_t char_id, uint16_t action_id) {
  // Fox/Falco aerial Side-B collision callbacks call `ft_CheckGroundAndLedge`, which snapshots
  // `fp->cur_pos` into CollData and runs `mpColl_800473CC` / `mpColl_800471F8` before entering the
  // grounded Side-B consumers. Unlike common-air `ft_80082C74` callbacks, this path does not pass
  // `ftCo_80096CC8`, so held-down platform input does not reject the floor. SpecialHiFall shares
  // the generated `ft_CheckGroundAndLedge` wall-envelope owner, but its floor/LandingFallSpecial
  // timing also depends on the SpecialHi source callbacks and is intentionally not admitted by this
  // Side-B floor path.
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class_bits)
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 submotion_id)
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialAirSStart_Coll,ftFx_SpecialAirS_Coll,ftFx_SpecialAirSEnd_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_800471F8}
  if (msl_motion_state_class_has(char_id, action_id, MSL_MS_CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL) ==
      0u) {
    return 0u;
  }
  const uint16_t smid = msl_motion_state_submotion_id(char_id, action_id);
  return (uint8_t)(smid >= (uint16_t)MSL_SM_FX_SPECIAL_AIR_S_START &&
                   smid <= (uint16_t)MSL_SM_FX_SPECIAL_AIR_S_END);
}

static inline uint8_t floor_line_is_skipped_platform(uint32_t stage_id, const MslStageFloorGraph* g,
                                                     int line_idx, uint16_t skip_segment_i) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count || skip_segment_i == 0xFFFFu) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  return (uint8_t)(line->segment_i == skip_segment_i &&
                   (line->is_platform || stage_collision_floor_line_has_height_platform_transform(
                                             stage_id, line->segment_i)));
}

static inline void publish_attackair_transformed_platform_floor_skip(MslBatch* batch, size_t idx,
                                                                     const MslStageFloorGraph* g,
                                                                     int line_idx) {
  if (batch == NULL || g == NULL || batch->state.floor_skip_segment_id == NULL || line_idx < 0 ||
      (size_t)line_idx >= g->line_count) {
    return;
  }
  // AttackAir_Coll itself does not call mpUpdateFloorSkip, but source floor callbacks keep the
  // CollData skip/floor owner live after a soft-platform pass-through contact. FoD's
  // height-transformed platforms can expose that owner over several AttackAirN/Lw frames after the
  // initial down-held contact. Publish only the exact transformed-platform line rejected by this
  // source owner so later frames skip the same platform instead of re-landing through a fresh
  // generic sweep.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpUpdateFloorSkip}
  batch->state.floor_skip_segment_id[idx] = g->lines[(size_t)line_idx].segment_i;
}

static inline void publish_attackair_transformed_platform_floor_skip_from_sweep(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    int line_idx, float x, float prev_y, float y) {
  if (batch == NULL || g == NULL || batch->state.floor_skip_segment_id == NULL) {
    return;
  }
  if (line_idx >= 0 && (size_t)line_idx < g->line_count &&
      stage_collision_floor_line_has_height_platform_transform(
          stage_id, g->lines[(size_t)line_idx].segment_i)) {
    publish_attackair_transformed_platform_floor_skip(batch, idx, g, line_idx);
    return;
  }
  const float lo = fminf(prev_y, y) - k_ecb_vertical_unit;
  const float hi = fmaxf(prev_y, y) + k_ecb_vertical_unit;
  for (size_t li = 0; li < g->line_count; li++) {
    const MslStageFloorLine* line = &g->lines[li];
    if (!line->is_platform ||
        !stage_collision_floor_line_has_height_platform_transform(stage_id, line->segment_i)) {
      continue;
    }
    if (x < fminf(line->x0, line->x1) - k_transformed_platform_skip_lookup_slop ||
        x > fmaxf(line->x0, line->x1) + k_transformed_platform_skip_lookup_slop) {
      continue;
    }
    float line_y = 0.0f;
    if (floor_line_y_at_x_for_env(batch, bi, g, (int)li, x, &line_y) && line_y >= lo &&
        line_y <= hi) {
      publish_attackair_transformed_platform_floor_skip(batch, idx, g, (int)li);
      return;
    }
  }
}

static inline uint8_t floor_line_is_runtime_fighter_solid(const MslStageFloorGraph* g,
                                                          uint32_t stage_id, int line_idx) {
  (void)stage_id;
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  return g->lines[(size_t)line_idx].fighter_solid ? 1u : 0u;
}

static inline uint8_t carried_floor_line_is_live_yoshi_shyguy_support(const MslBatch* batch, int bi,
                                                                      const MslStageFloorGraph* g,
                                                                      uint32_t stage_id,
                                                                      int line_idx) {
  if (batch == NULL || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const uint16_t segment_i = g->lines[(size_t)line_idx].segment_i;
  if (stage_collision_floor_line_stage_object_support_kind(stage_id, segment_i) !=
      (uint8_t)MSL_STAGE_OBJECT_SUPPORT_YOSHI_SHYGUY) {
    return 0u;
  }
  if (floor_line_is_runtime_fighter_solid(g, stage_id, line_idx)) {
    return 0u;
  }
  // Yoshi's Story Shy Guy support line is a generated MSLSTG01 stage-object support owner, not a
  // general raw-platform predicate. Preserve only the carried current CollData support while the
  // Heiho/Shy Guy stage-object controller seed lane says that owner is live; new contacts still use
  // normal fighter-solid floor admission.
  // data/stages/bin/grst.bin::MSLSTG01 stage_object_support_kind=yoshi_shyguy
  // refs/melee/src/melee/gr/grstory.c::{reset_shyguy_timer,grStory_801E3418}
  // refs/melee/src/melee/it/items/itheiho.c::{it_802D8618,it_802D9714,it_802D98C4}
  // data/stage_items/yoshi_shyguy.json
  return (batch->state.stage_yoshi_shyguy_valid != NULL &&
          batch->state.stage_yoshi_shyguy_valid[bi] != 0u)
             ? 1u
             : 0u;
}

static inline uint8_t floor_line_admitted_by_source_callback(const MslBatch* batch, size_t idx,
                                                             const MslStageFloorGraph* g,
                                                             uint32_t stage_id, int line_idx,
                                                             uint16_t skip_platform_segment_i,
                                                             const MslCommonParams* c) {
  if (!floor_line_is_runtime_fighter_solid(g, stage_id, line_idx)) {
    return 0u;
  }
  if (floor_line_is_skipped_platform(stage_id, g, line_idx, skip_platform_segment_i)) {
    return 0u;
  }
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  if (!line->is_platform) {
    return 1u;
  }
  if (batch == NULL || c == NULL) {
    return 1u;
  }
  if (!action_uses_ftco_80096cc8_floor_callback(batch->state.action_id[idx])) {
    return 1u;
  }
  // Source callback consumes `fp->input.lstick.y` as updated for the current frame. Replay rows where
  // the hidden callback-visible input differs need a narrower seed owner; do not use previous input
  // broadly here because many platform-stage rows depend on current-frame release landing.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_procMap}
  const float stick_y = stick_i8_to_unit(batch->state.input_main_y[idx]);
  return (uint8_t)(stick_y > c->platform_air_land_stick_y_threshold);
}

static uint8_t floor_intersect_horiz(float x0, float y0, float x1, float ax, float ay, float bx,
                                     float by, float* ix_out, float* iy_out) {
  // Decomp: mpLineIntersectionH (used by mpCheckFloor on horizontal-ish floor lines).
  // refs/melee/src/melee/mp/mplib.c::mpLineIntersectionH
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
  if (ix_out == NULL || iy_out == NULL) {
    return 0;
  }

  // mpCheckFloor gate: only consider falling / non-rising motion segments (ay >= by in this sim's
  // y-up coordinate system).
  if (!(ay >= by)) {
    return 0;
  }

  float min_ax = 0.0f;
  float max_ax = 0.0f;
  if (x0 < x1) {
    if ((ax < x0 && bx < x0) || (x1 < ax && x1 < bx)) {
      return 0;
    }
    if ((ay - y0) < -k_floor_horiz_dy_thresh || (by - y0) > k_floor_horiz_dy_thresh) {
      return 0;
    }
    min_ax = x0;
    max_ax = x1;
  } else {
    if ((ax < x1 && bx < x1) || (x0 < ax && x0 < bx)) {
      return 0;
    }
    if ((by - y0) < -k_floor_horiz_dy_thresh || (ay - y0) > k_floor_horiz_dy_thresh) {
      return 0;
    }
    min_ax = x1;
    max_ax = x0;
  }

  const double dby = (double)by - (double)ay;
  const double dbx = (double)bx - (double)ax;
  if (fabs(dby) < (double)k_floor_horiz_dy_thresh) {
    return 0;
  }

  double new_x = dbx / dby * (double)(y0 - ay) + (double)ax;
  double dx = new_x - (double)min_ax;
  if (dx < 0.0) {
    if (dx < -(double)k_floor_x_end_clamp) {
      return 0;
    }
    new_x = (double)min_ax;
  }
  if (new_x - (double)max_ax > 0.0) {
    if (new_x - (double)max_ax > (double)k_floor_x_end_clamp) {
      return 0;
    }
    new_x = (double)max_ax;
  }

  *ix_out = (float)new_x;
  *iy_out = y0;
  return 1;
}

static uint8_t floor_intersect_segment(float x0, float y0, float x1, float y1, float ax, float ay,
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

static inline uint8_t floor_chain_endpoints(const MslBatch* batch, int bi,
                                            const MslStageFloorGraph* g, int line_idx,
                                            float* out_left_x, float* out_left_y,
                                            float* out_right_x, float* out_right_y) {
  if (g == NULL || g->lines == NULL || g->line_count == 0) {
    return 0;
  }
  if (line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0;
  }

  int left_i = line_idx;
  for (size_t k = 0; k < g->line_count; k++) {
    const int16_t prev = g->lines[left_i].prev;
    if (prev < 0) {
      break;
    }
    if ((size_t)prev >= g->line_count) {
      break;
    }
    left_i = (int)prev;
  }
  int right_i = line_idx;
  for (size_t k = 0; k < g->line_count; k++) {
    const int16_t next = g->lines[right_i].next;
    if (next < 0) {
      break;
    }
    if ((size_t)next >= g->line_count) {
      break;
    }
    right_i = (int)next;
  }

  if (out_left_x) {
    *out_left_x = floor_line_world_for_env(batch, bi, g, left_i).x0;
  }
  if (out_left_y) {
    *out_left_y = floor_line_world_for_env(batch, bi, g, left_i).y0;
  }
  if (out_right_x) {
    *out_right_x = floor_line_world_for_env(batch, bi, g, right_i).x1;
  }
  if (out_right_y) {
    *out_right_y = floor_line_world_for_env(batch, bi, g, right_i).y1;
  }
  return 1;
}

static inline uint8_t wall_blocks_floor_edge_probe(const MslStageWallGraph* wg, float ax, float ay,
                                                   float bx, float by) {
  // Decomp parity note: mpColl_8004A45C_Floor uses mpCheckLeftWall/mpCheckRightWall to ensure a
  // wall has not stopped the fighter before setting edge suppression bits.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
  if (wg == NULL || wg->lines == NULL || wg->line_count == 0) {
    return 0;
  }

  for (size_t wi = 0; wi < wg->line_count; wi++) {
    const MslStageWallLine* w = &wg->lines[wi];
    float ix = 0.0f, iy = 0.0f;
    if (floor_intersect_segment(w->x0, w->y0, w->x1, w->y1, ax, ay, bx, by, &ix, &iy)) {
      return 1;
    }
  }
  return 0;
}

static inline void floor_write_edge_suppression_flags(MslBatch* batch, size_t idx,
                                                      uint32_t stage_id,
                                                      const MslStageFloorGraph* fg, int line_idx,
                                                      uint8_t char_id, uint32_t anim,
                                                      uint16_t ecb_frame, uint8_t was_grounded) {
  if (batch == NULL || fg == NULL) {
    return;
  }
  if (line_idx < 0 || (size_t)line_idx >= fg->line_count) {
    return;
  }

  // Floor edge suppression (Collide_LeftEdge / Collide_RightEdge).
  //
  // Decomp: mpColl sets these bits in a floor-edge helper which snaps the fighter to the floor
  // endpoint when their position goes beyond the chain end, provided a wall check does not block.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
  //
  // Decomp: mpColl suppresses ledge-grab checks while "on edge" by testing these bits.
  // refs/melee/src/melee/mp/mpcoll.c (mpColl_80046904 ledge-grab block; `on_edge` gate)
  float left_x0 = 0.0f, left_y0 = 0.0f, right_x1 = 0.0f, right_y1 = 0.0f;
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  if (!floor_chain_endpoints(batch, bi, fg, line_idx, &left_x0, &left_y0, &right_x1, &right_y1)) {
    return;
  }

  const float left_x = (left_x0 < right_x1) ? left_x0 : right_x1;
  const float right_x = (left_x0 < right_x1) ? right_x1 : left_x0;
  const float left_y = (left_x0 < right_x1) ? left_y0 : right_y1;
  const float right_y = (left_x0 < right_x1) ? right_y1 : left_y0;

  const float fighter_x = batch->state.pos_x[idx];
  if (fighter_x <= left_x) {
    const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
    MslEcbWorldPoints ecb = {0};
    msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, fighter_x,
                                batch->state.pos_y[idx], was_grounded);
    const float probe_ax = left_x + k_floor_edge_wall_probe_x_offset;
    const float probe_ay = left_y + k_floor_edge_wall_probe_y_offset;
    const float probe_bx = left_x + (ecb.right_rel_x /* bottom.x == 0 */);
    const float probe_by = left_y + (ecb.side_rel_y - ecb.bottom_rel_y);
    const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
    if (!wall_blocks_floor_edge_probe(lwg, probe_ax, probe_ay, probe_bx, probe_by)) {
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_RIGHT_EDGE;
      // Decomp: mpColl_8004A678_Floor also sets Collide_Edge when snapping to floor endpoints.
      // In this sim, set Collide_Edge whenever any edge suppression bit is set as a cheap parity
      // win and to future-proof other gates.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A678_Floor
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_EDGE;
    }
  } else if (fighter_x >= right_x) {
    const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
    MslEcbWorldPoints ecb = {0};
    msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, fighter_x,
                                batch->state.pos_y[idx], was_grounded);
    const float probe_ax = right_x - k_floor_edge_wall_probe_x_offset;
    const float probe_ay = right_y + k_floor_edge_wall_probe_y_offset;
    const float probe_bx = right_x + (ecb.left_rel_x /* bottom.x == 0 */);
    const float probe_by = right_y + (ecb.side_rel_y - ecb.bottom_rel_y);
    const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);
    if (!wall_blocks_floor_edge_probe(rwg, probe_ax, probe_ay, probe_bx, probe_by)) {
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_LEFT_EDGE;
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A678_Floor
      batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_EDGE;
    }
  }
}

static inline void mpcoll_commit_grounded_floor_contact(const MslMpcollContext* ctx,
                                                        const MslMpcollFloorContact* contact,
                                                        uint8_t was_grounded) {
  if (ctx == NULL || ctx->batch == NULL || ctx->floor_graph == NULL || contact == NULL) {
    return;
  }
  // Final grounded writeback for the mpColl floor owner:
  // - floor contact sets Collide_FloorPush/FloorHug style floor bits,
  // - floor-edge helper may set left/right edge suppression,
  // - CollData floor id/contact/normal become the next callback's persisted floor state.
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80044628_Floor,mpColl_80046F78,mpColl_8004A45C_Floor}
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_FLOOR_MASK;
  floor_write_edge_suppression_flags(
      batch, idx, ctx->stage_id, ctx->floor_graph,
      stage_collision_floor_line_index(ctx->stage_id, contact->ground_id), ctx->char_id, ctx->anim,
      ctx->ecb_frame, was_grounded);
  batch->state.ground_id[idx] = contact->ground_id;
  batch->state.ground_normal_x[idx] = contact->normal_x;
  batch->state.ground_normal_y[idx] = contact->normal_y;
  batch->state.ground_contact_x[idx] = contact->contact_x;
  batch->state.ground_contact_y[idx] = contact->contact_y;
}

static inline void mpcoll_commit_airborne_floor_state(const MslMpcollContext* ctx,
                                                      uint16_t ground_id) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  // Airborne CollData keeps floor.index provenance but clears live contact publication.
  // refs/melee/src/melee/lb/types.h::CollData
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  batch->state.ground_id[idx] = ground_id;
  batch->state.ground_normal_x[idx] = 0.0f;
  batch->state.ground_normal_y[idx] = 1.0f;
  batch->state.ground_contact_x[idx] = 0.0f;
  batch->state.ground_contact_y[idx] = 0.0f;
}

static inline void mpcoll_reject_floor_publication(const MslMpcollContext* ctx,
                                                   uint8_t* on_ground_io, uint16_t* ground_id_io,
                                                   float* contact_x_io, float* contact_y_io,
                                                   uint16_t airborne_ground_id,
                                                   float restored_pos_y, float restored_contact_x,
                                                   float restored_contact_y) {
  if (ctx == NULL || ctx->batch == NULL || on_ground_io == NULL || ground_id_io == NULL ||
      contact_x_io == NULL || contact_y_io == NULL) {
    return;
  }
  // Final wrapper-owned rejection of an already-built floor result. Source callbacks can collect a
  // candidate floor and still leave ground_or_air airborne when the callback-local publication
  // preconditions are not met; preserve that shape by changing only the final publication state.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044628_Floor,mpColl_80044838_Floor}
  *on_ground_io = 0u;
  *ground_id_io = airborne_ground_id;
  ctx->batch->state.pos_y[ctx->idx] = restored_pos_y;
  *contact_x_io = restored_contact_x;
  *contact_y_io = restored_contact_y;
}

static inline void mpcoll_reject_floor_publication_to_current_bottom(
    const MslMpcollContext* ctx, uint8_t* on_ground_io, uint16_t* ground_id_io, float* contact_x_io,
    float* contact_y_io, uint16_t airborne_ground_id, float restored_pos_y, float cur_bottom_x,
    float cur_bottom_y) {
  mpcoll_reject_floor_publication(ctx, on_ground_io, ground_id_io, contact_x_io, contact_y_io,
                                  airborne_ground_id, restored_pos_y, cur_bottom_x, cur_bottom_y);
}

static inline uint8_t mpcoll_maybe_refresh_downbound_airborne_floor_index(
    const MslMpcollContext* ctx, float cur_bottom_x, float cur_bottom_y, uint16_t* ground_id_io) {
  if (ctx == NULL || ctx->batch == NULL || ctx->floor_graph == NULL || ground_id_io == NULL ||
      ctx->prefer_floor_line_idx < 0 || !action_is_down_bound(ctx->action_id)) {
    return 0u;
  }
  // DownBound_Coll uses the allow-ground-to-air mpColl path: the fighter can remain airborne while
  // CollData.floor.index updates across connected floor seams. Preserve airborne ground_or_air, but
  // refresh the persisted floor id and generated-slope root height when DD90 projection can resolve
  // the current floor point.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  MslBatch* batch = ctx->batch;
  const MslStageFloorGraph* g = ctx->floor_graph;
  const int out_line_idx = msl_mplib_8004dd90_floor(batch, ctx->bi, g, ctx->prefer_floor_line_idx,
                                                    cur_bottom_x, cur_bottom_y, NULL, NULL, NULL);
  if (out_line_idx < 0) {
    return 0u;
  }
  *ground_id_io = g->lines[(size_t)out_line_idx].segment_i;
  if (floor_line_is_generated_stage_slope(batch, ctx->bi, g, ctx->prefer_floor_line_idx) ||
      floor_line_is_generated_stage_slope(batch, ctx->bi, g, out_line_idx)) {
    float root_line_y = 0.0f;
    if (floor_line_y_at_x_for_env(batch, ctx->bi, g, out_line_idx, batch->state.pos_x[ctx->idx],
                                  &root_line_y)) {
      batch->state.pos_y[ctx->idx] = root_line_y + k_floor_y_bias;
    }
  }
  return 1u;
}

static inline void mpcoll_commit_final_floor_state(const MslMpcollContext* ctx,
                                                   const MslMpcollFloorPublication* publication) {
  if (ctx == NULL || ctx->batch == NULL || ctx->floor_graph == NULL || publication == NULL) {
    return;
  }
  // Final mpColl wrapper writeback. Candidate collection and source-shaped publication guards have
  // already decided ground_or_air; this step only commits the resulting CollData floor/contact
  // lanes in one place.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082708,ft_80082C74,ft_800831CC,ft_80084280}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044628_Floor}
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  const uint8_t on_ground = publication->on_ground;
  batch->state.on_ground[idx] = on_ground;
  if (on_ground) {
    MslMpcollFloorContact contact = publication->contact;
    mpcoll_floor_contact_from_callback_result(ctx, &contact);
    mpcoll_commit_grounded_floor_contact(ctx, &contact, ctx->was_grounded);
    return;
  }

  uint16_t ground_id = publication->airborne_ground_id;
  (void)mpcoll_maybe_refresh_downbound_airborne_floor_index(ctx, publication->cur_bottom_x,
                                                            publication->cur_bottom_y, &ground_id);
  mpcoll_commit_airborne_floor_state(ctx, ground_id);
}

static uint8_t msl_mpcheck_floor(const MslBatch* batch, size_t idx, int bi,
                                 const MslStageFloorGraph* g, uint32_t stage_id, float ax, float ay,
                                 float bx, float by, uint16_t skip_platform_segment_i,
                                 int prefer_line_idx, int skip_line_idx, const MslCommonParams* c,
                                 int* out_line_idx, float* out_ix, float* out_iy, float* out_nx,
                                 float* out_ny) {
  // Decomp: mpCheckFloor iterates floor lines, intersects segment A->B with each, and chooses the
  // closest intersection to A (min dist^2), with stage-defined deterministic ordering on ties.
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
  if (g == NULL || out_line_idx == NULL) {
    return 0;
  }

  uint8_t found = 0;
  float best_dist2 = FLT_MAX;
  int best_idx = -1;
  int best_pref = -1;
  float best_ix = 0.0f, best_iy = 0.0f;
  float best_nx = 0.0f, best_ny = 1.0f;

  for (size_t li = 0; li < g->line_count; li++) {
    if (skip_line_idx >= 0 && (int)li == skip_line_idx) {
      continue;
    }
    if (!floor_line_admitted_by_source_callback(batch, idx, g, stage_id, (int)li,
                                                skip_platform_segment_i, c)) {
      continue;
    }
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    floor_ed5c_endpoints(batch, bi, g, (int)li, &x0, &y0, &x1, &y1);

    float ix = 0.0f, iy = 0.0f;
    const float dy = y0 - y1;
    uint8_t hit = 0;
    if (fabsf(dy) > k_floor_horiz_dy_thresh) {
      hit = floor_intersect_segment(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy);
    } else {
      hit = floor_intersect_horiz(x0, y0, x1, ax, ay, bx, by, &ix, &iy);
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
      } else if (floor_lines_connected(g, prefer_line_idx, (int)li)) {
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

      // Normal matches mpCheckFloor: perpendicular to the line direction, normalized.
      // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
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
      best_nx = nx;
      best_ny = ny;
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

static uint8_t msl_mpcoll_80044838_floor_edge_snap_from_bottom(
    MslBatch* batch, int bi, const MslStageFloorGraph* g, int line_idx, float cur_bottom_x,
    float cur_bottom_y, uint8_t allow_hard_floor, uint16_t* ground_id_out, float* contact_x_out,
    float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      ground_id_out == NULL) {
    return 0u;
  }
  if (!allow_hard_floor && !g->lines[(size_t)line_idx].is_platform) {
    // Retained owner slice: platform endpoint admission. Hard-floor off-end cases need the full
    // same-frame mpColl scratch/order port before this fallback can be safely broadened.
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  float edge_x = l.x0;
  float edge_y = l.y0;
  // Source shape:
  // mpColl_80044838_Floor falls back to the current floor's left endpoint, then switches to the
  // right endpoint when `left.x <= bottom.x`. It places cur_pos so ECB bottom lands exactly on that
  // endpoint and refreshes floor.index through mpLib_8004DD90_Floor.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044838_Floor
  if (edge_x <= cur_bottom_x) {
    edge_x = l.x1;
    edge_y = l.y1;
  }
  int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, line_idx, edge_x, edge_y, NULL,
                                              floor_nx_out, floor_ny_out);
  if (out_line_idx < 0) {
    out_line_idx = line_idx;
  }
  *ground_id_out = g->lines[(size_t)out_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = edge_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = edge_y;
  }
  (void)cur_bottom_y;
  return 1u;
}

static uint8_t msl_mpcoll_80044628_floor_wall_adjacent_fallback(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslMpcollOrderedWallCeilResult* wall_ceil, float cur_bottom_x, float cur_bottom_y,
    uint16_t skip_platform_segment_i, uint16_t* ground_id_out, float* contact_x_out,
    float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || wall_ceil == NULL || ground_id_out == NULL) {
    return 0u;
  }
  // Source side-floor fallback:
  // mpColl_80044628_Floor first tries mpCheckFloorRemap. If no floor was hit, the same inline2
  // wall pass can provide left/right wall bits; source then walks the raw MapLine graph with
  // mpLinePrevNonLeftWall / mpLineNextNonRightWall and projects the current bottom onto that floor.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_8004ACE4}
  // refs/melee/src/melee/mp/mplib.c::{
  //   mpLinePrevNonLeftWall,mpLineNextNonRightWall,mpLib_8004DD90_Floor}
  const uint8_t side_flags[2] = {
      (uint8_t)(wall_ceil->left_right_flags & 1u),
      (uint8_t)((wall_ceil->left_right_flags & 2u) ? 1u : 0u),
  };
  const uint16_t wall_segment[2] = {wall_ceil->left_wall_id, wall_ceil->right_wall_id};
  const MslStageRawLineKind skip_kind[2] = {MSL_STAGE_RAW_LINE_LEFT_WALL,
                                            MSL_STAGE_RAW_LINE_RIGHT_WALL};
  for (size_t side = 0; side < 2u; side++) {
    if (!side_flags[side] || wall_segment[side] == 0xFFFFu) {
      continue;
    }
    MslStageRawLineKind out_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
    uint16_t floor_segment_i = 0xFFFFu;
    const uint8_t found =
        (side == 0u)
            ? stage_collision_raw_line_prev_non_kind(stage_id, wall_segment[side], skip_kind[side],
                                                     &out_kind, &floor_segment_i)
            : stage_collision_raw_line_next_non_kind(stage_id, wall_segment[side], skip_kind[side],
                                                     &out_kind, &floor_segment_i);
    if (!found || out_kind != MSL_STAGE_RAW_LINE_FLOOR ||
        !stage_collision_floor_line_is_runtime_fighter_solid(stage_id, floor_segment_i)) {
      continue;
    }
    const int line_idx = stage_collision_floor_line_index(stage_id, floor_segment_i);
    if (line_idx < 0 || (size_t)line_idx >= g->line_count ||
        floor_line_is_skipped_platform(stage_id, g, line_idx, skip_platform_segment_i)) {
      continue;
    }
    float y_corr = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    const int projected_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, line_idx, cur_bottom_x,
                                                            cur_bottom_y, &y_corr, &nx, &ny);
    if (projected_line_idx < 0 || !(y_corr > 0.0f)) {
      continue;
    }
    *ground_id_out = g->lines[(size_t)projected_line_idx].segment_i;
    if (contact_x_out != NULL) {
      *contact_x_out = cur_bottom_x;
    }
    if (contact_y_out != NULL) {
      *contact_y_out = cur_bottom_y + y_corr;
    }
    if (floor_nx_out != NULL) {
      *floor_nx_out = nx;
    }
    if (floor_ny_out != NULL) {
      *floor_ny_out = ny;
    }
    batch->state.pos_y[idx] += y_corr;
    return 1u;
  }
  return 0u;
}

static uint8_t fallspecial_yoshi_main_floor_first_sustained_airborne_owner(const MslBatch* batch,
                                                                           size_t idx,
                                                                           uint32_t stage_id) {
  if (batch == NULL) {
    return 0u;
  }
  enum { MSL_STAGE_YOSHIS_STORY_LOCAL = 8u };
  enum { MSL_YOSHIS_STORY_MAIN_FLOOR_SEGMENT = 3u };
  // Yoshi's Story main-floor first sustained FallSpecial callback:
  // `MSLSTG01` segment 3 is the flat main floor connected to the low side ledges. On the first
  // sustained FallSpecial row after entry, source keeps the row airborne until the following
  // callback even though a simplified current-ECB/root projection can already snap to segment 3.
  // Keep this stage-line owner explicit instead of delaying FallSpecial floor publication on other
  // legal-stage floor families.
  // data/stages/bin/grst.bin::MSLSTG01 floor segment 3
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
  //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
  return (stage_id == (uint32_t)MSL_STAGE_YOSHIS_STORY_LOCAL &&
          batch->state.ground_id[idx] == (uint16_t)MSL_YOSHIS_STORY_MAIN_FLOOR_SEGMENT &&
          batch->state.action_id[idx] == (uint16_t)MSL_ACT_FALL_SPECIAL &&
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_FALL_SPECIAL &&
          batch->state.seed_prev_action_frame[idx] <= 0)
             ? 1u
             : 0u;
}

static uint8_t msl_mpcoll_80047e14_fallspecial_prephysics_floor_sweep(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_bottom_x, float prev_bottom_y, float cur_bottom_rel_y, int prefer_line_idx,
    uint16_t skip_platform_segment_i, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || ground_id_out == NULL || c == NULL ||
      (prefer_line_idx >= 0 && (size_t)prefer_line_idx >= g->line_count)) {
    return 0u;
  }
  if (!is_common_fallspecial_action(batch->state.action_id[idx])) {
    return 0u;
  }
  (void)cur_bottom_rel_y;
  if (!(batch->state.prev_pos_y[idx] <= batch->state.floor_sweep_prev_pos_y[idx])) {
    return 0u;
  }

  // FallSpecial_Coll owner:
  // ft_80083090 copies the callback-visible root into CollData.cur_pos, then mpColl_80047E14
  // runs mpColl_80043754. The floor check (`mpColl_80044628_Floor`) must first see the ECB-bottom
  // sweep from the carried CollData position to that callback-visible root. Only then may
  // mpColl_80044838_Floor snap the root (ignore_bottom when ecb.bottom.y > 0).
  //
  // FallSpecial_Phys has already integrated `cur_pos` before FallSpecial_Coll runs. Same-frame
  // EscapeAir_Anim -> FallSpecial handoffs consume that integrated root plus the loaded
  // FallSpecial ECB bottom. Sustained FallSpecial rows normally use the carried CollData/root
  // endpoint; shallow rows whose frame-start root is inside the loaded FallSpecial bottom
  // neighborhood consume the current ECB endpoint from mpColl_LoadECB_inline's interpolation pass.
  // The ECB boundary comes from extracted `data/ecb/*_bottom.bin`, not a character-id branch.
  //
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
  //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80083090
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80047E14,mpColl_80043754,mpColl_80044628_Floor,mpColl_80044838_Floor}
  const uint8_t entered_from_escapeair_anim =
      (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
       batch->state.action_frame[idx] <= 0)
          ? 1u
          : 0u;
  const uint8_t yoshi_main_first_sustained_airborne =
      fallspecial_yoshi_main_floor_first_sustained_airborne_owner(batch, idx, stage_id);
  const uint8_t sustained_current_ecb_owner =
      (!entered_from_escapeair_anim && !yoshi_main_first_sustained_airborne &&
       batch->state.fall_fast[idx] == 0u &&
       batch->state.prev_pos_y[idx] <= (cur_bottom_rel_y + k_ecb_vertical_unit))
          ? 1u
          : 0u;
  const uint8_t use_current_ecb_endpoint =
      (entered_from_escapeair_anim || sustained_current_ecb_owner) ? 1u : 0u;
  const float map_root_x =
      use_current_ecb_endpoint ? batch->state.pos_x[idx] : batch->state.prev_pos_x[idx];
  const float map_root_y =
      use_current_ecb_endpoint ? batch->state.pos_y[idx] : batch->state.prev_pos_y[idx];
  // mpColl_LoadECB_inline(flags=6) loads/interpolates the current FallSpecial ECB before
  // mpColl_80044628_Floor computes `cur_pos + ecb.bottom`.
  const float map_bottom_x = map_root_x;
  const float map_bottom_y =
      use_current_ecb_endpoint ? (map_root_y + cur_bottom_rel_y) : map_root_y;
  if (!(map_bottom_y <= prev_bottom_y)) {
    return 0u;
  }

  int hit_line_idx = -1;
  float ix = 0.0f;
  float iy = 0.0f;
  float nx = 0.0f;
  float ny = 1.0f;
  if (!msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, map_bottom_x,
                         map_bottom_y, skip_platform_segment_i, prefer_line_idx, -1, c,
                         &hit_line_idx, &ix, &iy, &nx, &ny)) {
    return 0u;
  }
  if (hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_platform) {
    // Static soft platforms do not use this prephysics hard-floor helper. FallSpecial platform
    // publication remains with the normal ftCo_80096CC8-gated floor callback below, including its
    // first-crossing delay and already-below-platform handoff.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    //   ftCo_FallSpecial_Coll,ftCo_80096CC8}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    return 0u;
  }
  if (hit_line_idx >= 0 && !g->lines[(size_t)hit_line_idx].is_platform &&
      stage_id == (uint32_t)MSL_STAGE_FINAL_DESTINATION_LOCAL &&
      batch->state.ground_id[idx] == 1u &&
      g->lines[(size_t)hit_line_idx].segment_i == batch->state.ground_id[idx] &&
      batch->state.seed_prev_action_id[idx] == batch->state.action_id[idx] &&
      batch->state.seed_prev_action_frame[idx] <= 5 && batch->state.action_frame[idx] <= 6 &&
      batch->state.speed_y_self[idx] < 0.0f && batch->state.fall_fast[idx] != 0u &&
      batch->state.floor_sweep_prev_pos_y[idx] > iy && batch->state.pos_y[idx] < iy) {
    // FD main-floor early FallSpecial owner:
    // The callback-visible bottom sweep can cross the same generated hard floor before source
    // publishes LandingFallSpecial. Adjacent floor/seam and non-FD contacts remain on the normal
    // landing path.
    // data/stages/bin/grnla.bin::MSLSTG01 floor segment 1
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
    //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
    return 0u;
  }
  const float snap_x = map_root_x;
  const float snap_y = map_root_y;
  float y_corr = 0.0f;
  const int out_line_idx =
      msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, snap_x, snap_y, &y_corr, &nx, &ny);
  if (out_line_idx < 0) {
    return 0u;
  }

  const float corrected_y = snap_y + y_corr;
  const float floor_contact_y = corrected_y - k_floor_y_bias;
  batch->state.pos_y[idx] = corrected_y;
  *ground_id_out = g->lines[(size_t)out_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = snap_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = floor_contact_y;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = ny;
  }
  (void)ix;
  (void)iy;
  return 1u;
}

static uint8_t msl_mpcoll_80047e14_flags6_root_floor_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    float prev_root_x, float prev_root_y, float cur_root_x, float cur_root_y, int prefer_line_idx,
    uint16_t skip_platform_segment_i, uint8_t ecb_lock_timer_seed, const MslCommonParams* c,
    uint16_t* ground_id_out, float* contact_x_out, float* contact_y_out, float* floor_nx_out,
    float* floor_ny_out) {
  if (batch == NULL || g == NULL || ground_id_out == NULL || c == NULL ||
      (prefer_line_idx >= 0 && (size_t)prefer_line_idx >= g->line_count)) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (action_id != (uint16_t)MSL_ACT_FALL || prefer_line_idx < 0) {
    return 0u;
  }
  if (batch->state.ground_id[idx] == 0xFFFFu) {
    return 0u;
  }
  if (!(cur_root_y <= prev_root_y)) {
    return 0u;
  }

  // Fall_Coll flags=6 floor owner:
  // Fall_Coll routes through `ft_800831CC`, which loads the callback-local ECB via
  // `mpColl_80047F40(flags=6)`. When a prefix-causal CollData lock, first Fall callback
  // hard-floor continuation, or same-step FoD height-platform contact source keeps the
  // callback-local collision owner live, source floor publication can let
  // `mpColl_80044838_Floor(ignore_bottom=true)` project from `cur_pos` even when the extracted pose
  // bottom is still above the floor. Direct replay reseeds expose that root crossing; keep it tied
  // to generated floor/transform ownership instead of letting a no-floor airborne row synthesize a
  // landing from a generic sweep.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80047F40,mpColl_80044628_Floor,mpColl_80044838_Floor}
  int hit_line_idx = -1;
  float ix = 0.0f;
  float iy = 0.0f;
  float nx = 0.0f;
  float ny = 1.0f;
  if (!msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_root_x, prev_root_y, cur_root_x,
                         cur_root_y, skip_platform_segment_i, prefer_line_idx, -1, c, &hit_line_idx,
                         &ix, &iy, &nx, &ny)) {
    hit_line_idx = -1;
  }
  const uint8_t fall_platform_callback_admits_by_input =
      (stick_i8_to_unit(batch->state.input_main_y[idx]) > c->platform_air_land_stick_y_threshold)
          ? 1u
          : 0u;
  if (fall_platform_callback_admits_by_input) {
    uint8_t found_source_height_platform = 0u;
    int best_source_height_platform_line_idx = -1;
    float best_source_height_platform_dist2 = 0.0f;
    float best_source_height_platform_y = 0.0f;
    for (size_t li = 0; li < g->line_count; li++) {
      const uint16_t segment_i = g->lines[li].segment_i;
      if (!g->lines[li].is_platform ||
          !stage_collision_floor_line_has_height_platform_transform(stage_id, segment_i) ||
          !stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi,
                                                                              segment_i) ||
          !floor_x_within_line_bounds(batch, bi, g, (int)li, cur_root_x)) {
        continue;
      }
      float line_y = 0.0f;
      if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, cur_root_x, &line_y)) {
        continue;
      }
      if (!(prev_root_y > (line_y + k_floor_y_bias) && cur_root_y < (line_y - k_floor_y_bias))) {
        continue;
      }
      const float dx = cur_root_x - prev_root_x;
      const float dy = line_y - prev_root_y;
      const float dist2 = dx * dx + dy * dy;
      if (!found_source_height_platform || dist2 < best_source_height_platform_dist2 ||
          (dist2 == best_source_height_platform_dist2 &&
           segment_i < g->lines[(size_t)best_source_height_platform_line_idx].segment_i)) {
        found_source_height_platform = 1u;
        best_source_height_platform_line_idx = (int)li;
        best_source_height_platform_dist2 = dist2;
        best_source_height_platform_y = line_y;
      }
    }
    if (found_source_height_platform && best_source_height_platform_line_idx >= 0) {
      // Fall_Coll's flags=6 source path can publish a source-trusted transformed platform from
      // the callback root even when the generic sweep was anchored to the prior CollData floor.
      // This is generated stage-line/current-height ownership, not a FoD stage-id exception.
      // data/stages/bin/*.bin::MSLSTG01 platform_transforms(kind=height)
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
      // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
      hit_line_idx = best_source_height_platform_line_idx;
      ix = cur_root_x;
      iy = best_source_height_platform_y;
      nx = 0.0f;
      ny = 1.0f;
    }
  }
  if (hit_line_idx < 0) {
    return 0u;
  }
  const uint8_t locked_fall_root_owner = (ecb_lock_timer_seed != 0u) ? 1u : 0u;
  const uint8_t ledge_to_ledge_continuation =
      (hit_line_idx >= 0 && prefer_line_idx >= 0 && hit_line_idx != prefer_line_idx &&
       g->lines[(size_t)hit_line_idx].is_ledge && g->lines[(size_t)prefer_line_idx].is_ledge)
          ? 1u
          : 0u;
  const uint16_t hit_segment_i = (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count)
                                     ? g->lines[(size_t)hit_line_idx].segment_i
                                     : (uint16_t)0xFFFFu;
  const uint8_t hit_line_is_platform = (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count &&
                                        g->lines[(size_t)hit_line_idx].is_platform)
                                           ? 1u
                                           : 0u;
  const uint8_t hit_line_height_platform_state_trusted =
      (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count &&
       stage_collision_floor_line_has_height_platform_transform(stage_id, hit_segment_i) &&
       stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, hit_segment_i))
          ? 1u
          : 0u;
  uint8_t hit_line_height_platform_same_step_contact = 0u;
  if (hit_line_height_platform_state_trusted) {
    uint8_t platform_id = 0u;
    if (stage_collision_floor_line_platform_transform_id(stage_id, hit_segment_i, &platform_id) &&
        platform_id < 2u &&
        (batch->state.stage_fod_platform_height_source[(size_t)bi * 2u + (size_t)platform_id] &
         (uint8_t)MSL_FOD_PLATFORM_HEIGHT_SOURCE_SAME_STEP_CONTACT) != 0u) {
      hit_line_height_platform_same_step_contact = 1u;
    }
  }
  const uint8_t hit_line_connected_hard_floor =
      (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count && prefer_line_idx >= 0 &&
       (size_t)prefer_line_idx < g->line_count && !hit_line_is_platform &&
       !stage_collision_floor_line_has_platform_transform(stage_id, hit_segment_i) &&
       !stage_collision_floor_line_has_platform_transform(
           stage_id, g->lines[(size_t)prefer_line_idx].segment_i) &&
       stage_floor_graph_has_height_platform_transform(stage_id, g) &&
       floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx) &&
       batch->state.seed_prev_action_id[idx] == action_id && batch->state.action_frame[idx] <= 2 &&
       floor_lines_connected(g, prefer_line_idx, hit_line_idx))
          ? 1u
          : 0u;
  const uint8_t hit_line_from_source_height_platform =
      (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count && prefer_line_idx >= 0 &&
       (size_t)prefer_line_idx < g->line_count && !hit_line_is_platform &&
       !stage_collision_floor_line_has_platform_transform(stage_id, hit_segment_i) &&
       stage_collision_floor_line_has_height_platform_transform(
           stage_id, g->lines[(size_t)prefer_line_idx].segment_i) &&
       stage_height_platform_line_has_current_source(batch, bi, stage_id,
                                                     g->lines[(size_t)prefer_line_idx].segment_i) &&
       batch->state.seed_prev_action_id[idx] == action_id)
          ? 1u
          : 0u;
  const uint8_t fall_platform_callback_admits_hit_line =
      (!hit_line_is_platform ||
       stick_i8_to_unit(batch->state.input_main_y[idx]) > c->platform_air_land_stick_y_threshold)
          ? 1u
          : 0u;
  const uint8_t source_trusted_fall_root_owner =
      (hit_line_connected_hard_floor || hit_line_from_source_height_platform ||
       (hit_line_height_platform_same_step_contact && fall_platform_callback_admits_hit_line))
          ? 1u
          : 0u;
  if (!locked_fall_root_owner && !ledge_to_ledge_continuation && !source_trusted_fall_root_owner) {
    return 0u;
  }
  if (ledge_to_ledge_continuation && !locked_fall_root_owner &&
      (batch->state.action_frame[idx] <= 1 || batch->state.seed_prev_action_frame[idx] <= 0 ||
       (batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES] & 0x80u) == 0u)) {
    // Non-locked ledge-floor continuations still need the Fall script's allow-interrupt phase
    // before the root projection can publish Landing. Earlier fastfall ledge crossings have a
    // replay-visible floor.index, but vanilla keeps Fall airborne until the callback's script phase
    // permits the floor handoff. ECB-locked rows keep their distinct CollData owner above.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    // refs/melee/src/melee/ft/types.h::Fighter::allow_interrupt (fp+0x2218:0)
    return 0u;
  }
  if (hit_line_height_platform_same_step_contact && hit_line_is_platform) {
    // Source-trusted FoD height-platform contact:
    // same-step grIzumi/mpLib contact provenance owns the current platform height for this
    // callback. Snap from the flags=6 Fall root to that generated line only for the seeded frame;
    // The frame scheduler clears the seed source mask before subsequent rollout frames.
    // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
    // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
    batch->state.pos_y[idx] = iy + k_floor_y_bias;
    *ground_id_out = hit_segment_i;
    if (contact_x_out != NULL) {
      *contact_x_out = ix;
    }
    if (contact_y_out != NULL) {
      *contact_y_out = iy;
    }
    if (floor_nx_out != NULL) {
      *floor_nx_out = nx;
    }
    if (floor_ny_out != NULL) {
      *floor_ny_out = ny;
    }
    return 1u;
  }

  const MslCharParams* chp = msl_char_params(batch->state.char_id[idx]);
  const uint8_t fall_ledge_floor_uses_expanded_collision_model =
      (chp != NULL && isfinite(chp->model_scaling) && chp->model_scaling > 1.0f) ? 1u : 0u;
  if (fall_ledge_floor_uses_expanded_collision_model && action_id == (uint16_t)MSL_ACT_FALL &&
      batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_FALL && hit_line_idx >= 0 &&
      g->lines[(size_t)hit_line_idx].is_ledge &&
      (prefer_line_idx < 0 || !g->lines[(size_t)prefer_line_idx].is_ledge) &&
      batch->state.action_frame[idx] >= 2 && batch->state.action_frame[idx] <= 3 &&
      batch->state.speed_y_self[idx] < 0.0f && prev_root_y > (iy + k_floor_y_bias) &&
      cur_root_y < iy) {
    // Same source boundary as the existing bottom-sweep guard below, but scoped to true
    // center/non-ledge -> ledge crossings. Rows already carrying a ledge floor.index are later
    // callback continuations and may publish Landing through the ordinary flags=6 owner.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_LoadECB_inline}
    return 0u;
  }

  float y_corr = 0.0f;
  const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, cur_root_x,
                                                    cur_root_y, &y_corr, &nx, &ny);
  if (out_line_idx < 0 || y_corr < 0.0f) {
    return 0u;
  }

  const float corrected_y = cur_root_y + y_corr;
  batch->state.pos_y[idx] = corrected_y;
  *ground_id_out = g->lines[(size_t)out_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = cur_root_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = corrected_y - k_floor_y_bias;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = ny;
  }
  (void)ix;
  return 1u;
}

static inline uint8_t msl_mpcoll_80047e14_reject_fallspecial_same_floor_early_final_land(
    const MslBatch* batch, size_t idx, uint32_t stage_id, uint16_t action_id,
    uint16_t seed_ground_id, uint16_t ground_id, float final_landing_lift, float contact_y,
    float root_y) {
  // Final guard for the same FD main-floor early FallSpecial callback owner handled in the direct
  // sweep path. This catches later projection paths that can still publish a root snap on the same
  // generated hard-floor segment.
  // data/stages/bin/grnla.bin::MSLSTG01 floor segment 1
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
  //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
  return (uint8_t)(batch != NULL && is_common_fallspecial_action(action_id) &&
                   stage_id == (uint32_t)MSL_STAGE_FINAL_DESTINATION_LOCAL &&
                   batch->state.ground_id[idx] == 1u &&
                   batch->state.seed_prev_action_id[idx] == action_id &&
                   batch->state.seed_prev_action_frame[idx] <= 5 &&
                   batch->state.action_frame[idx] <= 6 && batch->state.speed_y_self[idx] < 0.0f &&
                   batch->state.fall_fast[idx] != 0u && ground_id == seed_ground_id &&
                   final_landing_lift >= 0.0f &&
                   batch->state.floor_sweep_prev_pos_y[idx] > contact_y && root_y < contact_y);
}

static inline uint8_t msl_mpcoll_80047e14_reject_fall_same_floor_early_final_land(
    const MslBatch* batch, size_t idx, const MslStageFloorGraph* g, uint32_t stage_id,
    uint16_t action_id, uint16_t seed_ground_id, uint16_t ground_id, int final_ground_line_idx,
    float contact_y, float root_y) {
  // Final-publication guard for Fall_Coll's same-floor root-crossing edge:
  // ftCo_Fall_Coll delegates landing to ft_80082B1C after mpColl's ECB-bottom floor sweep. A final
  // root projection can see the FD ledge floor one frame before that callback publishes Landing;
  // keep only the first same-floor ledge root crossing airborne and let the following deeper frame
  // consume the normal landing path. Require monotonic Fall action-frame provenance so later
  // loop-wrap Fall rows do not reuse the early-frame window. The center hard floor is deliberately
  // excluded because ordinary Fall_Coll main-floor crossings publish Landing immediately.
  // data/stages/bin/grnla.bin::MSLSTG01 ledge floor segment geometry
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082B1C
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
  return (uint8_t)(batch != NULL && g != NULL && action_id == (uint16_t)MSL_ACT_FALL &&
                   stage_id == (uint32_t)MSL_STAGE_FINAL_DESTINATION_LOCAL &&
                   batch->state.seed_prev_action_id[idx] == action_id &&
                   batch->state.seed_prev_action_frame[idx] <= batch->state.action_frame[idx] &&
                   batch->state.action_frame[idx] <= 5 && batch->state.speed_y_self[idx] < 0.0f &&
                   final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
                   g->lines[(size_t)final_ground_line_idx].is_ledge &&
                   ground_id == seed_ground_id &&
                   batch->state.floor_sweep_prev_pos_y[idx] > contact_y && root_y < contact_y);
}

static uint8_t floor_4a908_retry(MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g,
                                 uint32_t stage_id, int persisted_line_idx, float prev_bottom_x,
                                 float prev_bottom_y, float prev_side_mid_y, float cur_bottom_x,
                                 float cur_bottom_y, uint16_t skip_platform_segment_i,
                                 uint16_t* ground_id_out, float* contact_x_out,
                                 float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || ground_id_out == NULL || persisted_line_idx < 0 ||
      (size_t)persisted_line_idx >= g->line_count) {
    return 0u;
  }
  // Source retry:
  // - mpColl_8004A908_Floor first retries the normal previous-bottom -> current-bottom sweep with
  //   a NULL callback.
  // - If that does not find a disconnected floor, it retries from the previous ECB vertical
  //   midpoint (`0.5 * (prev_ecb.top.y + prev_ecb.bottom.y) + prev_pos.y`) to the current bottom.
  // - Both retries require a different floor that is not connected to the persisted
  //   CollData.floor.index. Platform admission remains floor_skip/fighter-solid gated.
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A908_Floor
  const float start_y[2] = {prev_bottom_y, prev_side_mid_y};
  int accepted_line_idx = -1;
  float nx = 0.0f;
  float ny = 1.0f;
  for (size_t pass = 0; pass < 2u; pass++) {
    int hit_line_idx = -1;
    float ix = 0.0f;
    float iy = 0.0f;
    float hit_nx = 0.0f;
    float hit_ny = 1.0f;
    if (!msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bottom_x, start_y[pass], cur_bottom_x,
                           cur_bottom_y, skip_platform_segment_i, persisted_line_idx, -1, NULL,
                           &hit_line_idx, &ix, &iy, &hit_nx, &hit_ny)) {
      continue;
    }
    if (hit_line_idx < 0 || (size_t)hit_line_idx >= g->line_count ||
        hit_line_idx == persisted_line_idx ||
        floor_lines_connected(g, hit_line_idx, persisted_line_idx)) {
      continue;
    }
    accepted_line_idx = hit_line_idx;
    nx = hit_nx;
    ny = hit_ny;
    break;
  }
  if (accepted_line_idx < 0) {
    return 0u;
  }

  float y_corr = 0.0f;
  const int projected_line_idx = msl_mplib_8004dd90_floor(
      batch, bi, g, accepted_line_idx, cur_bottom_x, cur_bottom_y, &y_corr,
      floor_nx_out != NULL ? floor_nx_out : &nx, floor_ny_out != NULL ? floor_ny_out : &ny);
  if (projected_line_idx < 0) {
    // Source immediately follows an accepted mpColl_8004A908_Floor with
    // mpColl_80044838_Floor, which endpoint-snaps the accepted floor even for hard floors.
    // Keep hard-floor endpoint snap scoped to this accepted disconnected-floor retry so ordinary
    // hard-floor off-end handling is not broadened.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004A908_Floor,mpColl_80044838_Floor}
    if (!msl_mpcoll_80044838_floor_edge_snap_from_bottom(
            batch, bi, g, accepted_line_idx, cur_bottom_x, cur_bottom_y, 1u, ground_id_out,
            contact_x_out, contact_y_out, floor_nx_out, floor_ny_out)) {
      return 0u;
    }
    if (contact_x_out != NULL) {
      batch->state.pos_x[idx] += (*contact_x_out - cur_bottom_x);
    }
    if (contact_y_out != NULL) {
      batch->state.pos_y[idx] += (*contact_y_out - cur_bottom_y);
    }
    return 1u;
  }

  *ground_id_out = g->lines[(size_t)projected_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = cur_bottom_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = cur_bottom_y + y_corr;
  }
  batch->state.pos_y[idx] += y_corr;
  return 1u;
}

static uint8_t attackair_flags0_floor_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslEcbWorldPoints* prev_ecb, const MslEcbWorldPoints* cur_ecb, int prefer_line_idx,
    uint16_t skip_platform_segment_i, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || prev_ecb == NULL || cur_ecb == NULL || ground_id_out == NULL ||
      prefer_line_idx < 0 || (size_t)prefer_line_idx >= g->line_count) {
    return 0u;
  }
  (void)c;
  const uint16_t action_id = batch->state.action_id[idx];
  if (!is_attackair_action(action_id) || batch->state.speed_y_self[idx] >= 0.0f ||
      batch->state.floor_skip_segment_id[idx] != 0xFFFFu ||
      batch->state.prev_action_id[idx] != action_id || !(cur_ecb->bottom_rel_y > 0.0f)) {
    return 0u;
  }

  // AttackAir_Coll routes through ft_80082C74 -> mpColl_800471F8, whose air-collision floor path
  // first requires `mpColl_80044628_Floor` to accept a bottom sweep, then lets
  // `mpColl_80044838_Floor(ignore_bottom=true)` project from the callback root when the loaded ECB
  // bottom is above the root. The generic air sweep skips the preferred floor.index after an
  // earlier projection attempt; that is too narrow for connected hard-floor seams/edges because
  // source `mpCheckFloor` still tests the carried floor chain before the root snap. FoD
  // height-transform platforms use this same source root snap when the line state is source-trusted
  // and the callback root is past the shallow first-phase platform-contact band. AttackAir_Coll does
  // not pass `ftCo_80096CC8`, so held-down input alone is not a platform-pass reject here; the
  // explicit floor_skip/shallow-contact guards above still own first-phase pass-through rows.
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 coll_cb_by_action
  // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
  // data/stages/bin/*.bin::MSLSTG01 floor segment links/fighter_solid flags
  // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
  int hit_line_idx = -1;
  float ix = 0.0f;
  float iy = 0.0f;
  float nx = 0.0f;
  float ny = 1.0f;
  if (!msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_ecb->bottom_x, prev_ecb->bottom_y,
                         cur_ecb->bottom_x, cur_ecb->bottom_y, skip_platform_segment_i,
                         prefer_line_idx, -1, NULL, &hit_line_idx, &ix, &iy, &nx, &ny)) {
    return 0u;
  }
  if (hit_line_idx < 0 || (size_t)hit_line_idx >= g->line_count) {
    return 0u;
  }
  const uint16_t hit_segment_i = g->lines[(size_t)hit_line_idx].segment_i;
  const uint8_t hit_line_height_platform_state_trusted =
      (stage_collision_floor_line_has_height_platform_transform(stage_id, hit_segment_i) &&
       stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, hit_segment_i))
          ? 1u
          : 0u;
  const uint8_t hit_line_ordinary_hard_floor =
      (!g->lines[(size_t)hit_line_idx].is_platform &&
       !stage_collision_floor_line_has_platform_transform(stage_id, hit_segment_i))
          ? 1u
          : 0u;
  const float hit_line_prev_root_below_depth = iy - batch->state.prev_pos_y[idx];
  const uint8_t attackair_height_platform_root_snap =
      (hit_line_height_platform_state_trusted &&
       action_uses_shallow_attackair_platform_ecb_owner(batch->state.char_id[idx], action_id) &&
       (!move_tables_attackair_first_hitbox_phase(batch->state.char_id[idx], action_id,
                                                  batch->state.anim_frame_f32[idx]) ||
        hit_line_prev_root_below_depth > (2.0f * k_ecb_vertical_unit)))
          ? 1u
          : 0u;
  if (!hit_line_ordinary_hard_floor && !attackair_height_platform_root_snap) {
    return 0u;
  }
  if (hit_line_ordinary_hard_floor && !floor_lines_connected(g, prefer_line_idx, hit_line_idx)) {
    return 0u;
  }

  float y_corr = 0.0f;
  const int out_line_idx =
      msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, batch->state.pos_x[idx],
                               batch->state.pos_y[idx], &y_corr, &nx, &ny);
  if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count) {
    return 0u;
  }
  const uint16_t out_segment_i = g->lines[(size_t)out_line_idx].segment_i;
  const uint8_t out_line_height_platform_state_trusted =
      (stage_collision_floor_line_has_height_platform_transform(stage_id, out_segment_i) &&
       stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, out_segment_i))
          ? 1u
          : 0u;
  if (hit_line_ordinary_hard_floor &&
      (g->lines[(size_t)out_line_idx].is_platform ||
       stage_collision_floor_line_has_platform_transform(stage_id, out_segment_i))) {
    return 0u;
  }
  if (attackair_height_platform_root_snap && !out_line_height_platform_state_trusted) {
    return 0u;
  }
  if (y_corr < 0.0f || y_corr > (fabsf(batch->state.speed_y_self[idx]) +
                                 mpcoll_floor_projection_lift_allowance(cur_ecb))) {
    return 0u;
  }

  batch->state.pos_y[idx] += y_corr;
  *ground_id_out = out_segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = batch->state.pos_x[idx];
  }
  if (contact_y_out != NULL) {
    *contact_y_out = batch->state.pos_y[idx] - k_floor_y_bias;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = ny;
  }
  (void)ix;
  (void)iy;
  return 1u;
}

static uint8_t escapeair_locked_floor_bottom_sweep_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslEcbWorldPoints* prev_ecb, uint16_t skip_platform_segment_i, const MslCommonParams* c,
    uint16_t* ground_id_out, float* contact_x_out, float* contact_y_out, float* floor_nx_out,
    float* floor_ny_out) {
  if (batch == NULL || g == NULL || prev_ecb == NULL || ground_id_out == NULL ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR ||
      batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL ||
      batch->state.ecb_lock_timer[idx] == 0u || batch->state.speed_y_self[idx] >= 0.0f) {
    return 0u;
  }

  // EscapeAir_Coll's ft_80082C74 path consumes CollData_X130_Locked before the root snap:
  // `mpColl_80044628_Floor` checks the callback-local previous/locked bottom sweep, and
  // `mpColl_80044838_Floor(ignore_bottom=true)` can then project the EscapeAir root to the accepted
  // floor. Replay-visible EscapeAir poses often expose a zero current bottom; use the explicit
  // locked desired bottom when present, otherwise the prefix-causal previous ECB bottom from
  // CollData. Sustained soft-platform rows keep the existing platform countdown boundary; fresh
  // Jump/JumpAerial -> EscapeAir entries can publish either soft platforms or non-platform hard
  // floors from the same bottom-sweep owner.
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 coll_cb_by_action
  // data/stages/bin/*.bin::MSLSTG01 platform transform records
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
  float source_bottom_rel_y = prev_ecb->bottom_rel_y;
  if (batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
      batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u) {
    source_bottom_rel_y = batch->state.coll_desired_ecb_bottom_rel_y[idx];
  }
  if (!(source_bottom_rel_y > k_floor_y_bias)) {
    return 0u;
  }

  int hit_line_idx = -1;
  float ix = 0.0f;
  float iy = 0.0f;
  float nx = 0.0f;
  float ny = 1.0f;
  if (!msl_mpcheck_floor(batch, idx, bi, g, stage_id, batch->state.floor_sweep_prev_pos_x[idx],
                         batch->state.floor_sweep_prev_pos_y[idx] + source_bottom_rel_y,
                         batch->state.pos_x[idx], batch->state.pos_y[idx] + source_bottom_rel_y,
                         skip_platform_segment_i, -1, -1, c, &hit_line_idx, &ix, &iy, &nx, &ny)) {
    return 0u;
  }
  if (hit_line_idx < 0 || (size_t)hit_line_idx >= g->line_count) {
    return 0u;
  }

  const uint16_t hit_segment_i = g->lines[(size_t)hit_line_idx].segment_i;
  const uint8_t hit_line_is_platform = g->lines[(size_t)hit_line_idx].is_platform ? 1u : 0u;
  const uint8_t hit_line_is_ordinary_hard_floor =
      (!hit_line_is_platform &&
       !stage_collision_floor_line_has_platform_transform(stage_id, hit_segment_i))
          ? 1u
          : 0u;
  const uint8_t hit_line_has_height_platform_transform =
      stage_collision_floor_line_has_height_platform_transform(stage_id, hit_segment_i);
  const uint8_t fresh_jump_escapeair_entry =
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B)
          ? 1u
          : 0u;
  const uint8_t sustained_escapeair =
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR) ? 1u : 0u;
  if (!fresh_jump_escapeair_entry && !sustained_escapeair) {
    return 0u;
  }
  if (hit_line_is_ordinary_hard_floor && !fresh_jump_escapeair_entry) {
    return 0u;
  }
  if (!hit_line_is_platform && !hit_line_is_ordinary_hard_floor) {
    return 0u;
  }
  if (hit_line_has_height_platform_transform && sustained_escapeair &&
      batch->state.ecb_lock_timer[idx] > 2u) {
    // FoD side-platform EscapeAir rows still spend the earlier lock phases in the interpolation
    // gap: EWT:941 (timer 3) remains airborne, while EWT:942 (timer 2) is the first source-owned
    // platform publication. Static center platforms and fresh Jump/JumpAerial entry rows use their
    // own floor-callback path and are not delayed by this side-platform countdown boundary.
    // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_UnlockECB}
    return 0u;
  }

  float y_corr = 0.0f;
  const int out_line_idx =
      msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, batch->state.pos_x[idx],
                               batch->state.pos_y[idx], &y_corr, &nx, &ny);
  if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count) {
    return 0u;
  }
  const uint16_t out_segment_i = g->lines[(size_t)out_line_idx].segment_i;
  if (hit_line_is_platform && !g->lines[(size_t)out_line_idx].is_platform) {
    return 0u;
  }
  if (hit_line_is_ordinary_hard_floor &&
      (g->lines[(size_t)out_line_idx].is_platform ||
       stage_collision_floor_line_has_platform_transform(stage_id, out_segment_i))) {
    return 0u;
  }
  if (y_corr < 0.0f || y_corr > (source_bottom_rel_y + fabsf(batch->state.speed_y_self[idx]) +
                                 k_ecb_vertical_unit)) {
    return 0u;
  }

  batch->state.pos_y[idx] += y_corr;
  *ground_id_out = out_segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = batch->state.pos_x[idx];
  }
  if (contact_y_out != NULL) {
    *contact_y_out = batch->state.pos_y[idx] - k_floor_y_bias;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = ny;
  }
  (void)ix;
  (void)iy;
  return 1u;
}

static uint8_t escapeair_locked_platform_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint16_t skip_platform_segment_i, uint8_t ecb_lock_timer_seed, float start_pose_bottom_rel_y,
    float current_pose_bottom_rel_y, float current_pose_top_rel_y,
    uint8_t locked_desired_bottom_owner, float locked_desired_bottom_rel_y, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out,
    const MslCommonParams* c) {
  const uint8_t fresh_jumpaerial_zero_bottom_entry =
      (batch != NULL &&
       (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
        batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
       batch->state.seed_prev_action_frame[idx] <= 1 &&
       fabsf(locked_desired_bottom_rel_y) <= k_floor_y_bias)
          ? 1u
          : 0u;
  if (batch == NULL || g == NULL || ground_id_out == NULL || !(start_pose_bottom_rel_y > 0.0f) ||
      !(current_pose_bottom_rel_y > 0.0f) ||
      !(current_pose_top_rel_y > current_pose_bottom_rel_y)) {
    return 0u;
  }

  // Locked EscapeAir platform floor callback:
  // ftCo_EscapeAir_Coll uses ft_80082C74 -> ft_80081D0C -> mpColl_800471F8. While
  // CollData_X130_Locked is active, mpColl_LoadECB_inline preserves the desired bottom and
  // mpColl_80046904 can land via mpColl_80044838_Floor(ignore_bottom=true), projecting from the
  // fighter root instead of the locked zero-bottom point. Keep this consumer platform-only and
  // require a real root-vs-ECB separation before accepting the root projection; shallow platform
  // grazes remain owned by the ordinary sweep/suppression path below.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_LoadECB_inline,mpColl_80043754,mpColl_80046904,mpColl_80044838_Floor}
  const float root_x = batch->state.pos_x[idx];
  const float root_y = batch->state.pos_y[idx];
  const float min_lift = fmaxf(k_ecb_vertical_unit, start_pose_bottom_rel_y - k_ecb_vertical_unit);
  const float max_lift = start_pose_bottom_rel_y + k_ecb_vertical_unit;

  uint8_t found = 0u;
  float best_lift = FLT_MAX;
  int best_line_idx = -1;
  float best_nx = 0.0f;
  float best_ny = 1.0f;

  for (size_t li = 0; li < g->line_count; li++) {
    if (!g->lines[li].is_platform) {
      continue;
    }
    if (!floor_line_is_runtime_fighter_solid(g, stage_id, (int)li)) {
      continue;
    }
    if (floor_line_is_skipped_platform(stage_id, g, (int)li, skip_platform_segment_i)) {
      continue;
    }

    float y_corr = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    const int out_line_idx =
        msl_mplib_8004dd90_floor(batch, bi, g, (int)li, root_x, root_y, &y_corr, &nx, &ny);
    if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count ||
        !g->lines[(size_t)out_line_idx].is_platform) {
      continue;
    }
    if (!floor_line_is_runtime_fighter_solid(g, stage_id, out_line_idx)) {
      continue;
    }
    if (floor_line_is_skipped_platform(stage_id, g, out_line_idx, skip_platform_segment_i)) {
      continue;
    }
    if (g->lines[(size_t)out_line_idx].is_ledge &&
        (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
        batch->state.seed_prev_action_frame[idx] >= 2 &&
        batch->state.seed_prev_action_frame[idx] <= 4) {
      // Fresh JumpAerial -> EscapeAir ledge-floor contacts are still owned by the pre-entry
      // CollData/ECB lifetime. Do not let the platform-root projection helper consume the ledge as
      // a soft-platform floor before the same callback reaches the source EscapeAir row.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
      //   ftCo_80099A58,ftCo_EscapeAir_Coll}
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
      continue;
    }
    float start_y_corr = 0.0f;
    const int start_line_idx =
        msl_mplib_8004dd90_floor(batch, bi, g, out_line_idx, root_x, batch->state.prev_pos_y[idx],
                                 &start_y_corr, NULL, NULL);
    const float prev_root_x = batch->state.floor_sweep_prev_pos_x[idx];
    const float prev_root_y = batch->state.floor_sweep_prev_pos_y[idx];
    const int sweep_start_line_idx = msl_mplib_8004dd90_floor(
        batch, bi, g, out_line_idx, prev_root_x, prev_root_y, NULL, NULL, NULL);
    // One-step reseeds can expose the callback's CollData.prev_pos through the explicit
    // floor_sweep_prev_pos lane while prev_pos_y already names the current frame-start root. Accept
    // either start-root view for the same source line, then keep the depth gates below as the owner
    // boundary.
    // refs/melee/src/melee/ft/ft_081B.c::ft_80081D0C
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754}
    const uint8_t start_line_matches =
        (start_line_idx >= 0 &&
         g->lines[(size_t)start_line_idx].segment_i == g->lines[(size_t)out_line_idx].segment_i)
            ? 1u
            : 0u;
    const uint8_t sweep_start_line_matches =
        (sweep_start_line_idx >= 0 && g->lines[(size_t)sweep_start_line_idx].segment_i ==
                                          g->lines[(size_t)out_line_idx].segment_i)
            ? 1u
            : 0u;
    float platform_line_y = 0.0f;
    const uint8_t platform_line_y_valid =
        floor_line_y_at_x_for_env(batch, bi, g, out_line_idx, root_x, &platform_line_y);
    const uint8_t fresh_jumpaerial_platform_root_crossing_owner =
        // Fresh JumpAerial -> EscapeAir can enter EscapeAir before Fighter_procMap, then
        // EscapeAir_Coll consumes the same callback's root/platform crossing. This is the downward
        // counterpart to the existing locked platform root projection: if the callback root starts
        // above a static platform and the current vertical step crosses it, publish the platform
        // floor even though the pre-step root projection is a negative correction.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
        (platform_line_y_valid &&
         (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
         ecb_lock_timer_seed <= 2u && root_y > (platform_line_y + k_floor_y_bias) &&
         (root_y + batch->state.speed_y_self[idx]) <= (platform_line_y + k_floor_y_bias))
            ? 1u
            : 0u;
    const uint8_t fresh_jumpaerial_platform_below_crossing_owner =
        // Same fresh-entry owner after the root has already crossed below the platform line by the
        // time this collision helper runs. The previous/root line checks above prove the callback
        // crossed this platform; accept the upward projection while it is bounded by the current
        // vertical step rather than by the locked desired bottom. Zero-bottom first-JumpAerial-frame
        // entries can still use this root crossing while the CollData lock countdown is high; later
        // carried JumpAerial frames remain with the existing interpolation/airborne guard.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
        ((start_line_matches || sweep_start_line_matches) &&
         (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
         (ecb_lock_timer_seed <= 2u ||
          (fresh_jumpaerial_zero_bottom_entry && ecb_lock_timer_seed >= 4u)) &&
         y_corr > 0.0f && y_corr <= (fabsf(batch->state.speed_y_self[idx]) + k_ecb_vertical_unit))
            ? 1u
            : 0u;
    const float platform_owner_lift =
        fresh_jumpaerial_platform_root_crossing_owner
            ? ((platform_line_y + k_floor_y_bias) - root_y)
            : ((fresh_jumpaerial_platform_below_crossing_owner &&
                fresh_jumpaerial_zero_bottom_entry && ecb_lock_timer_seed >= 4u)
                   ? (y_corr + k_floor_y_bias)
                   : y_corr);
    if (!start_line_matches && !sweep_start_line_matches &&
        !fresh_jumpaerial_platform_root_crossing_owner) {
      continue;
    }
    const uint8_t start_root_depth_owner =
        (!locked_desired_bottom_owner && start_y_corr >= min_lift && start_y_corr <= max_lift) ? 1u
                                                                                               : 0u;
    uint8_t locked_desired_bottom_sweep_owner = 0u;
    uint8_t locked_zero_bottom_sweep_owner = 0u;
    if (locked_desired_bottom_owner) {
      int sweep_line_idx = -1;
      const float prev_bottom_x = batch->state.prev_pos_x[idx];
      const float prev_bottom_y = batch->state.prev_pos_y[idx] + locked_desired_bottom_rel_y;
      const float cur_bottom_x = root_x;
      const float cur_bottom_y = root_y + locked_desired_bottom_rel_y;
      if (msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                            cur_bottom_y, skip_platform_segment_i, out_line_idx, -1, c,
                            &sweep_line_idx, NULL, NULL, NULL, NULL) &&
          sweep_line_idx >= 0 &&
          g->lines[(size_t)sweep_line_idx].segment_i == g->lines[(size_t)out_line_idx].segment_i) {
        locked_desired_bottom_sweep_owner = 1u;
      }
      if (fabsf(locked_desired_bottom_rel_y) <= k_floor_y_bias &&
          msl_mpcheck_floor(batch, idx, bi, g, stage_id, batch->state.prev_pos_x[idx],
                            batch->state.prev_pos_y[idx], root_x, root_y, skip_platform_segment_i,
                            out_line_idx, -1, c, &sweep_line_idx, NULL, NULL, NULL, NULL) &&
          sweep_line_idx >= 0 &&
          g->lines[(size_t)sweep_line_idx].segment_i == g->lines[(size_t)out_line_idx].segment_i) {
        locked_zero_bottom_sweep_owner = 1u;
      }
    }

    const float distinct_platform_bottom_rel_y =
        (batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
         batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u)
            ? batch->state.coll_desired_ecb_bottom_rel_y[idx]
            : current_pose_bottom_rel_y;
    const uint8_t distinct_platform_root_depth_owner =
        // Sustained EscapeAir can also resolve a different static soft-platform segment during the
        // lock window. That platform is a new floor candidate, not the carried CollData.floor.index
        // segment protected by the same-platform lifetime guards below. During
        // CollData_X130_Locked, `mpColl_LoadECB_inline` preserves the desired bottom; once the
        // current root is below the accepted platform by that source bottom depth it belongs to the
        // ordinary ft_80082C74/mpColl_800471F8 floor publication path.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{
        //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
        (g->lines[(size_t)out_line_idx].is_platform &&
         g->lines[(size_t)out_line_idx].segment_i != batch->state.ground_id[idx] &&
         !stage_collision_floor_line_has_platform_transform(
             stage_id, g->lines[(size_t)out_line_idx].segment_i) &&
         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
         batch->state.seed_prev_action_frame[idx] >= 2 && ecb_lock_timer_seed <= 5u &&
         y_corr >= distinct_platform_bottom_rel_y && y_corr <= current_pose_top_rel_y)
            ? 1u
            : 0u;
    const uint8_t suppress_large_distinct_platform_root_projection =
        // Desired-bottom precondition for oversized distinct-platform root snaps:
        // mpColl_80044838_Floor(ignore_bottom=true) is the consumer after
        // mpColl_80044628_Floor has accepted a callback-local bottom-floor hit. If the preserved
        // desired bottom did not sweep across the new platform and the root correction is larger than
        // the desired-bottom extent plus this frame's vertical step, the root projection is an
        // endpoint/projection artifact rather than a source floor result. Ordinary same-step platform
        // handoffs and later lock phases stay on the normal EscapeAir_Coll owner.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
        (g->lines[(size_t)out_line_idx].is_platform &&
         g->lines[(size_t)out_line_idx].segment_i != batch->state.ground_id[idx] &&
         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
         ecb_lock_timer_seed == 6u && batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
         batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u &&
         !locked_desired_bottom_sweep_owner &&
         y_corr > (locked_desired_bottom_rel_y + fabsf(batch->state.speed_y_self[idx]) +
                   k_ecb_vertical_unit))
            ? 1u
            : 0u;
    const uint8_t current_root_depth_owner =
        // Same source callback owner as the start-root gate, but for rows where the platform line is
        // inside the current post-Phys EscapeAir ECB envelope while CollData_X130_Locked is still in
        // the handoff phase. Earlier lock frames remain with the interpolation-gap suppression path;
        // later same-platform pass-through frames are owned by the final floor publication guard.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
        //
        // Lock countdowns 7 through 5 are the late above-root phase where the current root can already
        // be below the accepted platform by the loaded EscapeAir bottom depth. Oversized
        // distinct-platform projections still require the desired-bottom floor precondition above.
        (batch->state.action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
         !suppress_large_distinct_platform_root_projection &&
         (fresh_jumpaerial_platform_root_crossing_owner || distinct_platform_root_depth_owner ||
          fresh_jumpaerial_platform_below_crossing_owner || locked_zero_bottom_sweep_owner ||
          (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
           ((ecb_lock_timer_seed >= 5u && ecb_lock_timer_seed <= 7u &&
             y_corr >= current_pose_bottom_rel_y && y_corr <= current_pose_top_rel_y) ||
            (locked_desired_bottom_owner && ecb_lock_timer_seed <= 5u &&
             locked_desired_bottom_sweep_owner)))))
            ? 1u
            : 0u;
    if (!start_root_depth_owner && !current_root_depth_owner) {
      continue;
    }
    if (!found || platform_owner_lift < best_lift ||
        (platform_owner_lift == best_lift &&
         g->lines[(size_t)out_line_idx].segment_i < g->lines[(size_t)best_line_idx].segment_i)) {
      found = 1u;
      best_lift = platform_owner_lift;
      best_line_idx = out_line_idx;
      best_nx = nx;
      best_ny = ny;
    }
  }

  if (!found || best_line_idx < 0) {
    return 0u;
  }

  batch->state.pos_y[idx] += best_lift;
  *ground_id_out = g->lines[(size_t)best_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = root_x;
  }
  if (contact_y_out != NULL) {
    *contact_y_out = root_y + best_lift - k_floor_y_bias;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = best_nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = best_ny;
  }
  return 1u;
}

static uint8_t escapeair_locked_hard_floor_zero_bottom_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    int prefer_line_idx, uint16_t skip_platform_segment_i, uint8_t locked_desired_bottom_owner,
    float locked_desired_bottom_rel_y, const MslCommonParams* c, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
  if (batch == NULL || g == NULL || ground_id_out == NULL || c == NULL || prefer_line_idx < 0 ||
      (size_t)prefer_line_idx >= g->line_count || locked_desired_bottom_owner == 0u ||
      fabsf(locked_desired_bottom_rel_y) > k_floor_y_bias) {
    return 0u;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR ||
      (batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
       batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_B) ||
      batch->state.seed_prev_action_frame[idx] > 2) {
    return 0u;
  }
  if (g->lines[(size_t)prefer_line_idx].is_platform || g->lines[(size_t)prefer_line_idx].is_ledge ||
      stage_collision_floor_line_has_platform_transform(
          stage_id, g->lines[(size_t)prefer_line_idx].segment_i) ||
      !floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx)) {
    return 0u;
  }

  // Same-frame JumpAerial -> EscapeAir hard-floor handoff:
  // JumpAerial_IASA can change to EscapeAir before Fighter_procMap, and EscapeAir_Coll then runs
  // ft_80082C74 on the entered state while CollData_X130_Locked preserves the desired bottom at
  // zero. For hard floors this source path is a root-vs-floor sweep into
  // mpColl_80044838_Floor(ignore_bottom=true), not the soft-platform lifetime handled by
  // escapeair_locked_platform_root_projection. Keep the owner on non-platform/non-ledge floor.index
  // rows and require the explicit floor_sweep_prev_pos -> current-root crossing so sustained
  // EscapeAir and Yoshi ledge/platform remaps stay on their separate owners.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_LoadECB_inline,mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
  int hit_line_idx = -1;
  if (!msl_mpcheck_floor(batch, idx, bi, g, stage_id, batch->state.floor_sweep_prev_pos_x[idx],
                         batch->state.floor_sweep_prev_pos_y[idx], batch->state.pos_x[idx],
                         batch->state.pos_y[idx], skip_platform_segment_i, prefer_line_idx, -1, c,
                         &hit_line_idx, NULL, NULL, NULL, NULL) ||
      hit_line_idx < 0 || (size_t)hit_line_idx >= g->line_count ||
      g->lines[(size_t)hit_line_idx].segment_i != g->lines[(size_t)prefer_line_idx].segment_i) {
    return 0u;
  }

  float y_corr = 0.0f;
  float nx = 0.0f;
  float ny = 1.0f;
  const int out_line_idx =
      msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, batch->state.pos_x[idx],
                               batch->state.pos_y[idx], &y_corr, &nx, &ny);
  if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count ||
      g->lines[(size_t)out_line_idx].segment_i != g->lines[(size_t)prefer_line_idx].segment_i ||
      y_corr < 0.0f || y_corr > (fabsf(batch->state.speed_y_self[idx]) + k_ecb_vertical_unit)) {
    return 0u;
  }

  batch->state.pos_y[idx] += y_corr;
  *ground_id_out = g->lines[(size_t)out_line_idx].segment_i;
  if (contact_x_out != NULL) {
    *contact_x_out = batch->state.pos_x[idx];
  }
  if (contact_y_out != NULL) {
    *contact_y_out = batch->state.pos_y[idx] - k_floor_y_bias;
  }
  if (floor_nx_out != NULL) {
    *floor_nx_out = nx;
  }
  if (floor_ny_out != NULL) {
    *floor_ny_out = ny;
  }
  return 1u;
}

uint8_t mpcoll_800477e0_floor_mask_probe(const MslBatch* batch, size_t idx,
                                         MslMpcollFloorMaskResult* out) {
  if (batch == NULL) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL) {
    return 0u;
  }

  // ft_80082578 sets CollData.cur_pos from fp->cur_pos, then mpColl_800477E0 runs
  // mpCollPrev + mpColl_LoadECB_inline(flags=6) and reports `env_flags & Collide_FloorMask`.
  // Reconstruct only that floor-mask decision here; callers decide whether and how to apply the
  // owning motion-state callback.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082578,ft_80083C00}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800477E0
  const uint8_t char_id = batch->state.char_id[idx];
  const uint32_t anim = batch->state.animation_index[idx];
  const uint16_t ecb_frame = msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
  const uint16_t ecb_frame_prev = msl_ecb_prev_frame_u16(ecb_frame);

  MslEcbBottomWorldPoint cur_bot = {0};
  MslEcbBottomWorldPoint prev_bot = {0};
  msl_ecb_bottom_world_point_sample(&cur_bot, char_id, anim, ecb_frame, batch->state.pos_x[idx],
                                    batch->state.pos_y[idx], 0u);
  msl_ecb_bottom_world_point_sample(&prev_bot, char_id, anim, ecb_frame_prev,
                                    batch->state.floor_sweep_prev_pos_x[idx],
                                    batch->state.floor_sweep_prev_pos_y[idx], 0u);

  int line_idx = -1;
  float ix = 0.0f;
  float iy = 0.0f;
  const uint16_t skip_platform_segment_i = platform_floor_skip_segment_id(batch, idx, stage_id);
  const MslCommonParams* c = msl_common_params();
  if (cur_bot.y <= prev_bot.y &&
      msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bot.x, prev_bot.y, cur_bot.x, cur_bot.y,
                        skip_platform_segment_i, -1, -1, c, &line_idx, &ix, &iy, NULL, NULL)) {
    float y_corr = 0.0f;
    const int out_line_idx =
        msl_mplib_8004dd90_floor(batch, bi, g, line_idx, ix, cur_bot.y, &y_corr, NULL, NULL);
    if (out_line_idx >= 0) {
      if (out != NULL) {
        out->ground_id = g->lines[(size_t)out_line_idx].segment_i;
        out->corrected_pos_y = batch->state.pos_y[idx] + y_corr;
      }
      return 1u;
    }
    if (out != NULL) {
      out->ground_id = g->lines[(size_t)line_idx].segment_i;
      out->corrected_pos_y = batch->state.pos_y[idx] + (iy - cur_bot.y) + k_floor_y_bias;
    }
    return 1u;
  }

  {
    uint8_t found = 0u;
    float best_y_corr = FLT_MAX;
    int best_line_idx = -1;
    float y_corr = 0.0f;
    for (size_t li = 0; li < g->line_count; li++) {
      if (!floor_line_is_runtime_fighter_solid(g, stage_id, (int)li)) {
        continue;
      }
      if (g->lines[li].is_platform) {
        continue;
      }
      const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, (int)li, cur_bot.x, cur_bot.y,
                                                        &y_corr, NULL, NULL);
      if (out_line_idx >= 0 && y_corr >= 0.0f && (!found || y_corr < best_y_corr)) {
        found = 1u;
        best_y_corr = y_corr;
        best_line_idx = out_line_idx;
      }
    }
    if (found && best_line_idx >= 0) {
      if (out != NULL) {
        out->ground_id = g->lines[(size_t)best_line_idx].segment_i;
        out->corrected_pos_y = batch->state.pos_y[idx] + best_y_corr;
      }
      return 1u;
    }
  }

  return 0u;
}

uint8_t mpcoll_800477e0_capture_root_floor_mask_probe(const MslBatch* batch, size_t idx,
                                                      MslMpcollFloorMaskResult* out) {
  if (batch == NULL) {
    return 0u;
  }
  const int bi = (int)(idx / (size_t)MSL_MAX_PLAYERS);
  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL || g->lines == NULL || g->line_count == 0) {
    return 0u;
  }

  // CapturePulledLw immediate Coll callback owner:
  // - fn_800DB230 switches Lw -> Hi through ftCommon_8007D5D4 / ftCommon_UnlockECB, applies
  //   fn_800DAA40's root translation, then calls ft_80083C00.
  // - ft_80083C00 snapshots `fp->cur_pos` into CollData before mpColl_800477E0.
  // - For grounded capture victims, CollData.floor.index is still the authoritative nearby floor;
  //   the immediate Hi root can sit below that floor even when extracted ECB-bottom data is above
  //   it, so a bottom-only sweep misses a real floor-mask result.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{fn_800DB230,fn_800DAECC}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083C00,ft_80082578}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800477E0
  int line_idx = -1;
  const uint16_t ground_id = batch->state.ground_id[idx];
  const uint16_t skip_platform_segment_i = platform_floor_skip_segment_id(batch, idx, stage_id);
  if (ground_id != 0xFFFFu) {
    if (ground_id == skip_platform_segment_i) {
      // Source floor-skip ownership:
      // - Pass / shield-drop writes CollData.floor_skip through mpUpdateFloorSkip.
      // - mpColl_800477E0 passes that skip into the floor query, so the stale platform
      //   CollData.floor.index must not be reused by this capture-root reconstruction.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A184,ftCo_8009A228}
      // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_800477E0}
      line_idx = -1;
    } else {
      line_idx = stage_collision_floor_line_index(stage_id, ground_id);
      if (!floor_line_is_runtime_fighter_solid(g, stage_id, line_idx)) {
        line_idx = -1;
      }
    }
  }

  float y_corr = 0.0f;
  int out_line_idx = -1;
  if (line_idx >= 0) {
    out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, line_idx, batch->state.pos_x[idx],
                                            batch->state.pos_y[idx], &y_corr, NULL, NULL);
    if (out_line_idx >= 0 && y_corr >= 0.0f) {
      if (out != NULL) {
        out->ground_id = g->lines[(size_t)out_line_idx].segment_i;
        out->corrected_pos_y = batch->state.pos_y[idx] + y_corr;
      }
      return 1u;
    }
  }

  uint8_t found = 0u;
  float best_y_corr = FLT_MAX;
  int best_line_idx = -1;
  for (size_t li = 0; li < g->line_count; li++) {
    if (!floor_line_is_runtime_fighter_solid(g, stage_id, (int)li)) {
      continue;
    }
    if (g->lines[li].is_platform) {
      continue;
    }
    out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, (int)li, batch->state.pos_x[idx],
                                            batch->state.pos_y[idx], &y_corr, NULL, NULL);
    if (out_line_idx >= 0 && y_corr >= 0.0f && (!found || y_corr < best_y_corr)) {
      found = 1u;
      best_y_corr = y_corr;
      best_line_idx = out_line_idx;
    }
  }
  if (found && best_line_idx >= 0) {
    if (out != NULL) {
      out->ground_id = g->lines[(size_t)best_line_idx].segment_i;
      out->corrected_pos_y = batch->state.pos_y[idx] + best_y_corr;
    }
    return 1u;
  }
  return 0u;
}

// Ground collision is organized around the same source wrapper families used by the decomp:
// - grounded allow-ground-to-air persistence: ft_80082708 -> mpColl_8004B108
// - common air/Fall/FallSpecial landing: ft_80083090/ft_800831CC -> mpColl_80047E14
// - airborne stay-airborne floor handoff: ft_80082C74 -> mpColl_800471F8/mpColl_800473CC
// Keep helper extraction inside those owners. Avoid broad per-line facts caches here: the source
// routines ask narrow mpLib/mpColl questions at the point where the callback needs them.
// refs/melee/src/melee/ft/ft_081B.c
// refs/melee/src/melee/mp/mpcoll.c
void mpcoll_ground_apply(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint32_t stage_id = batch->state.stage_id[bi];
    const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
    if (g == NULL || g->lines == NULL || g->line_count == 0) {
      continue;
    }
    const MslStageCeilingGraph* cg = stage_collision_get_ceiling_graph(stage_id);
    const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
    const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      MslMpcollContext mpcoll_ctx = mpcoll_context_make(batch, bi, idx, stage_id, g, cg, lwg, rwg);
      const uint16_t action_id = mpcoll_ctx.action_id;
      const uint16_t prev_action_id = mpcoll_ctx.prev_action_id;

      // Decomp: Fighter_procMap runs every frame (not gated by hitlag), and collision callbacks
      // such as ftCo_DamageFly_Coll internally select hitlag-specific mpColl paths when needed.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4

      // Match-flow actions use dedicated (or NULL) collision callbacks in decomp; this lite sim
      // skips the generic stage collision pass until those paths are implemented.
      // refs: src/match_flow.c::match_flow_should_stage_collide
      if (!match_flow_should_stage_collide(action_id)) {
        if (batch->state.stocks[idx] == 0u && batch->state.char_id[idx] == 0u &&
            action_id == (uint16_t)MSL_ACT_DEAD_DOWN) {
          // Inactive zero-stock slots have no live Fighter map callback. Preserve the Slippi
          // terminal slot shape installed by match_flow.c instead of converting it to a live
          // airborne DeadDown fighter.
          // refs/melee/src/melee/gm/gm_16AE.c::fn_8016B918_inline
          batch->state.on_ground[idx] = 1u;
          batch->state.ground_id[idx] = 0u;
          batch->state.ground_normal_x[idx] = 0.0f;
          batch->state.ground_normal_y[idx] = 1.0f;
          batch->state.ground_contact_x[idx] = 0.0f;
          batch->state.ground_contact_y[idx] = 0.0f;
          continue;
        }
        batch->state.on_ground[idx] = 0;
        continue;
      }

      // Cliff / ledge hold actions use their own snap logic and should not be stage-grounded.
      if (is_cliff_hold_action(action_id)) {
        batch->state.on_ground[idx] = 0;
        continue;
      }

      if (msl_action_is_thrown_victim(action_id)) {
        const uint8_t owner = batch->state.grab_owner_port[idx];
        if (owner != 0xFFu && owner < (uint8_t)num_players && owner != (uint8_t)p) {
          // Decomp: common Thrown* states have empty Coll callbacks, so generic stage-collision
          // grounding does not run while the victim remains attached to the throw owner.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{
          //   ftCo_ThrownF_Coll,ftCo_ThrownB_Coll,ftCo_ThrownHi_Coll,ftCo_ThrownLw_Coll
          // }
          continue;
        }
      }
      if (throw_flow_release_pending_for_victim(batch, bi, p)) {
        continue;
      }

      publish_common_air_transformed_platform_skip_from_root_crossing(batch, idx, bi, g, stage_id,
                                                                      c);

      const uint8_t was_grounded = batch->state.prev_on_ground[idx] ? 1u : 0u;
      mpcoll_ctx.was_grounded = was_grounded;
      const uint8_t ecb_lock_timer_seed = batch->state.ecb_lock_timer[idx];
      if (was_grounded && batch->state.ground_id[idx] != 0xFFFFu &&
          grounded_action_allows_stage_object_platform_carry(action_id)) {
        const int carry_line_idx =
            stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
        if (carry_line_idx >= 0 && (size_t)carry_line_idx < g->line_count &&
            g->lines[(size_t)carry_line_idx].is_platform) {
          float platform_dx = 0.0f;
          float platform_dy = 0.0f;
          if (stage_collision_floor_line_motion_delta(batch, bi, &g->lines[(size_t)carry_line_idx],
                                                      &platform_dx, &platform_dy)) {
            // Stage-object platform carry:
            // Randall's ground object refreshes collision before fighter map callbacks; grounded
            // riders keep CollData.floor.index and inherit the platform transform delta before
            // projection. FoD vertical carry remains owned by signed DD90 projection so replay
            // seeded height rows do not double-apply height velocity.
            // refs/melee/src/melee/gr/grstory.c::{grStory_801E3370,Ground_801C2FE0}
            // refs/melee/src/melee/mp/mplib.c::mpLib_80055E9C
            batch->state.pos_x[idx] += platform_dx;
            batch->state.pos_y[idx] += platform_dy;
          }
        }
      }

      // Decomp: Fighter_procMap decrements fp->ecb_lock before calling coll_cb/map callbacks and
      // clears CollData_X130_Locked when the countdown reaches 0.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
      //
      // This simulator stores only the countdown (`ecb_lock_timer`) and uses it as the lock gate.
      // Grounded snapshots should not carry a stale lock.
      uint8_t ecb_lock_timer = ecb_lock_timer_seed;
      if (batch->state.on_ground[idx]) {
        ecb_lock_timer = 0;
      } else if (ecb_lock_timer > 0u) {
        ecb_lock_timer = (uint8_t)(ecb_lock_timer - 1u);
      }
      batch->state.ecb_lock_timer[idx] = ecb_lock_timer;
      const uint8_t ecb_lock_active = (ecb_lock_timer > 0u) ? 1u : 0u;
      // Common Damage_Coll lock-bottom ownership:
      // seed ecb_lock_timer is a prefix-causal CollData_X130_Locked snapshot at frame start. The
      // generic countdown is still decremented before map callbacks, but replay-real common
      // DamageHi/N/Lw rows with a seeded value of 1 use the locked-bottom ECB in the same
      // ft_80081DD4 -> mpColl_800473CC callback pass. Keep this off DamageAir/DamageFly; those
      // callbacks have distinct floor-contact timing and negative locks that remain airborne.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_JObj}
      const uint8_t damage_collision_uses_seeded_lock_bottom =
          (is_damage_ground_collision_action(action_id) && ecb_lock_timer_seed != 0u) ? 1u : 0u;

      // Decomp shape: ECB is loaded each collision step and prev_ecb is a one-step lag:
      // mpCollInterpolateECB assigns prev_ecb = ecb before updating.
      // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
      const uint8_t char_id = mpcoll_ctx.char_id;
      const uint32_t anim = mpcoll_ctx.anim;
      const uint8_t shallow_attackair_platform_ecb_owner =
          action_uses_shallow_attackair_platform_ecb_owner(char_id, action_id);
      const uint8_t first_phase_attackair_platform_ecb_owner =
          action_uses_first_phase_attackair_platform_ecb_owner(char_id, action_id);
      const uint16_t ecb_frame =
          msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
      mpcoll_ctx.ecb_frame = ecb_frame;
      uint16_t ecb_frame_prev = msl_ecb_prev_frame_u16(ecb_frame);
      uint16_t ecb_frame_bias_next = ecb_frame;
      if (action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
          action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) {
        // DownBound callback ordering is Anim then Coll in the same frame; sampling the next ECB
        // frame better matches the post-Anim pose used by ftCo_DownBound_Coll on the bounce frame.
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procMap}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{
        //   ftCo_DownBound_Anim,ftCo_DownBound_Coll
        // }
        if (ecb_frame_bias_next != 0xFFFFu) {
          ecb_frame_bias_next = (uint16_t)(ecb_frame_bias_next + 1u);
        }
      } else if (is_damage_fly_collision_action(action_id) && batch->state.action_frame[idx] <= 2) {
        // Damage/DamageFly callback ordering is also Anim then Coll in Fighter_8006A360. On early
        // entry frames, sampling the post-Anim ECB pose reduces one-frame "still airborne" misses
        // before DownBound/Landing transitions.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Anim,ftCo_DamageFly_Anim,ftCo_Damage_Coll,ftCo_DamageFly_Coll}
        if (ecb_frame_bias_next != 0xFFFFu) {
          ecb_frame_bias_next = (uint16_t)(ecb_frame_bias_next + 1u);
        }
      }

      // Decomp: some stage collision entrypoints load ECB with flags where `flags & 1` forces
      // desired_ecb.bottom.y = 0.0 (relative to cur_pos). This stabilizes grounded contact against
      // pose-driven ECB changes.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj (flags & 1)
      MslEcbBottomWorldPoint cur_bot = {0};
      MslEcbBottomWorldPoint prev_bot = {0};

      const float x = batch->state.pos_x[idx];
      const float y = batch->state.pos_y[idx];
      // Floor sweeps consume the frame-start CollData.prev_pos snapshot so pre-collision callbacks
      // such as hitlag SDI/ASDI remain visible to mpCheckFloor.
      // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpCheckFloor}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
      const uint8_t escapeair_locked_jumpaerial_entry_desired_bottom_owner =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed != 0u &&
           (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
           (batch->state.pos_y[idx] > k_floor_y_bias ||
            batch->state.floor_sweep_prev_pos_y[idx] > k_floor_y_bias) &&
           batch->state.seed_prev_action_frame[idx] <= 2 &&
           (prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR ||
            batch->state.prev_action_frame[idx] <= 2))
              ? 1u
              : 0u;
      const uint8_t escapeair_471f8_uses_frame_start_last_pos =
          // Same-frame JumpAerial -> EscapeAir IASA enters EscapeAir before Fighter_procMap. In
          // that entry-frame source path, ft_80082C74 consumes the frame-start JumpAerial CollData
          // root as `last_pos` before writing the post-Phys EscapeAir root into CollData.cur_pos.
          // Already-seeded EscapeAir rows have run the entry callback on the previous frame, so
          // their source CollData.cur_pos is the prefix floor_sweep_prev_pos seed lane instead.
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_80043754
          (escapeair_locked_jumpaerial_entry_desired_bottom_owner &&
           (prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B))
              ? 1u
              : 0u;
      const float prev_x = escapeair_471f8_uses_frame_start_last_pos
                               ? batch->state.prev_pos_x[idx]
                               : batch->state.floor_sweep_prev_pos_x[idx];
      const float prev_y = escapeair_471f8_uses_frame_start_last_pos
                               ? batch->state.prev_pos_y[idx]
                               : batch->state.floor_sweep_prev_pos_y[idx];
      mpcoll_clear_callback_floor_result(&mpcoll_ctx, prev_x, prev_y, x, y);

      // ECB bottom point for floor collision.
      // Decomp: mpLib_8004DD90_Floor and mpCheckFloor consume the ECB bottom point.
      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
      uint8_t lock_bottom_to_zero = was_grounded;
      if (action_id == (uint16_t)MSL_ACT_DOWN_BOUND_U ||
          action_id == (uint16_t)MSL_ACT_DOWN_BOUND_D) {
        // DownBound collision allows leaving/re-contacting ground while the action continues;
        // forcing ECB.bottom=0 for all previously-grounded snapshots suppresses that phase.
        // Keep pose-driven ECB bottom for DownBound to match the callback's ground/air behavior.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
        lock_bottom_to_zero = 0u;
      }
      const int escapeair_seed_floor_line_idx =
          stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
      const uint8_t escapeair_early_ledge_root_floor_owner =
          // Early sustained EscapeAir over FD's left in-bounds carried ledge floor uses the
          // ft_80082C74/mpColl_800471F8 root floor path once the replay-visible x2218
          // allow-interrupt bit is live. Mirrored FD/Battlefield ledge controls without that
          // callback bit remain on the edge-suppression path at this phase. Direct one-step seeds
          // may expose a preserved desired bottom here, but free rollout reaches the same source
          // callback through live CollData lifetime rather than a replay seed lane.
          // data/stages/bin/grnla.bin::MSLSTG01 ledge floor segment 0
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
           stage_id == (uint32_t)MSL_STAGE_FINAL_DESTINATION_LOCAL &&
           batch->state.ground_id[idx] == 0u && ecb_lock_timer_seed >= 3u &&
           (batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES] & 0x80u) != 0u &&
           batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
           batch->state.seed_prev_action_frame[idx] <= 1 && batch->state.action_frame[idx] <= 3 &&
           escapeair_seed_floor_line_idx >= 0 &&
           g->lines[(size_t)escapeair_seed_floor_line_idx].is_ledge &&
           floor_x_within_line_bounds(batch, bi, g, escapeair_seed_floor_line_idx, x))
              ? 1u
              : 0u;
      if (escapeair_early_ledge_root_floor_owner) {
        lock_bottom_to_zero = 1u;
      }
      const uint8_t spacie_air_special_floor_owner =
          is_spacie_air_special_floor_collision_action(action_id) && prev_action_id == action_id;
      const uint8_t common_air_collision_uses_locked_ecb_bottom =
          (ecb_lock_active &&
           ((action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] != 0u) ||
            is_attackair_action(action_id) ||
            action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_START ||
            action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP ||
            action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END))
              ? 1u
              : 0u;
      const uint8_t damagefly_release_entry_uses_pose_bottom =
          // Throw release can enter DamageFly with an ECB-lock countdown still active, but the
          // next DamageFly_Coll floor pass uses the current DamageFly ECB source. Keeping the
          // generic ground->air zero-bottom approximation here hides the floor crossing on low
          // release rows where CollData.floor.index is still valid.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_JObj}
          (is_damage_fly_collision_action(action_id) && ecb_lock_active &&
           batch->state.hitlag[idx] == 0u && batch->state.hitlag_pre_timer[idx] == 0u &&
           batch->state.action_frame[idx] <= 2 &&
           msl_action_is_thrown_victim(batch->state.seed_prev_action_id[idx]))
              ? 1u
              : 0u;
      const uint8_t locked_desired_ecb_bottom_valid =
          (ecb_lock_active && batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
           batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u &&
           (escapeair_locked_jumpaerial_entry_desired_bottom_owner ||
            batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 2u ||
            batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 3u ||
            (action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] != 0u) ||
            is_attackair_action(action_id) ||
            msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_COMMON_AIR_COLL)))
              ? 1u
              : 0u;
      uint8_t use_locked_desired_ecb_bottom = 0u;
      if (!lock_bottom_to_zero && !damagefly_release_entry_uses_pose_bottom &&
          ((action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active) ||
           (is_damage_collision_landing_action(action_id) &&
            (ecb_lock_active || damage_collision_uses_seeded_lock_bottom)) ||
           (spacie_air_special_floor_owner && ecb_lock_active) ||
           (common_air_collision_uses_locked_ecb_bottom && ecb_lock_active))) {
        // ECB lock-bottom semantics while CollData_X130_Locked is active:
        // - ftCommon_8007D5D4 sets fp->ecb_lock and CollData_X130_Locked on ground->air transitions.
        // - mpColl_LoadECB_inline preserves desired_ecb.bottom while locked.
        // - Fall fastfall rows in `Fall_Coll` (`ft_800831CC` -> `mpColl_80047E14`), EscapeAir,
        //   AttackAir, Damage/DamageFly, and the Fox/Falco aerial special callbacks above can also
        //   resolve grounded contact during this lock window. Jump/JumpAerial, non-fastfall Fall,
        //   MissFoot, and FallSpecial have distinct floor callback-phase owners and are not part
        //   of this retained floor slice.
        // - Shine narrows this to frame-start SpecialAirLwLoop/End owners; otherwise
        //   SpecialAirLwStart_Anim can change to Loop before collision and incorrectly inherit
        //   the loop/end floor-contact policy on the startup handoff frame.
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
        // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
        //   ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
        //   ftFx_SpecialAirLwStart_Anim,ftFx_SpecialAirLwLoop_Coll,ftFx_SpecialAirLwEnd_Coll}
        if (locked_desired_ecb_bottom_valid) {
          use_locked_desired_ecb_bottom = 1u;
        } else {
          lock_bottom_to_zero = 1u;
        }
      }
      uint8_t lock_bottom_to_prev_frame = 0u;
      if (!lock_bottom_to_zero && !use_locked_desired_ecb_bottom &&
          action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && batch->state.action_frame[idx] >= 0 &&
          batch->state.action_frame[idx] <= 10 && batch->state.speed_y_self[idx] <= 0.0f &&
          prev_action_id != action_id) {
        // Source-shaped current-ECB selection for fresh EscapeAir entries:
        // the persisted CollData state below owns the previous endpoint, while the current endpoint
        // still follows the just-loaded EscapeAir pose used by `ftCo_EscapeAir_Coll`.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
        lock_bottom_to_prev_frame = 1u;
      }
      const uint16_t ecb_frame_cur =
          lock_bottom_to_prev_frame ? ecb_frame_prev : ecb_frame_bias_next;
      const float desired_ecb_rel =
          use_locked_desired_ecb_bottom
              ? batch->state.coll_desired_ecb_bottom_rel_y[idx]
              : mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_cur, lock_bottom_to_zero);
      const float facing_dir_for_ecb = batch->state.facing[idx] ? 1.0f : -1.0f;
      MslEcbWorldPoints desired_ecb_points = {0};
      msl_ecb_world_points_sample(&desired_ecb_points, char_id, anim, ecb_frame_cur,
                                  facing_dir_for_ecb, x, y, lock_bottom_to_zero);
      if (use_locked_desired_ecb_bottom) {
        // Source mpColl_LoadECB_inline preserves desired_ecb.bottom while the CollData lock bit is
        // live; top/side points still refresh from the current pose.
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
        msl_ecb_world_points_preserve_desired_bottom_rel_y(&desired_ecb_points, x, y,
                                                           desired_ecb_rel);
      }
      const uint8_t escapeair_jumpaerial_entry =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && prev_action_id != action_id &&
           (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B))
              ? 1u
              : 0u;
      const uint8_t escapeair_jumpaerial_prev_ecb_lifetime =
          (escapeair_jumpaerial_entry && batch->state.seed_prev_action_frame[idx] >= 4 &&
           batch->state.pos_y[idx] > k_floor_y_bias)
              ? 1u
              : 0u;
      const float pose_prev_ecb_rel =
          mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_prev, lock_bottom_to_zero);
      const float pre_entry_prev_ecb_rel =
          // Same-frame JumpAerial -> EscapeAir IASA still enters the map callback with the
          // pre-entry CollData current ECB as `prev_ecb`; source then loads/interpolates the
          // entered EscapeAir desired ECB before `mpColl_80044628_Floor`. Use the generated
          // MotionState submotion table for the pre-entry action instead of reusing the entered
          // EscapeAir pose, otherwise late platform crossings can miss the bottom-sweep owner.
          // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor}
          (escapeair_jumpaerial_prev_ecb_lifetime &&
           (prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B))
              ? mpcoll_action_pose_ecb_bottom_rel_y(char_id, prev_action_id,
                                                    batch->state.prev_action_frame[idx],
                                                    lock_bottom_to_zero)
              : pose_prev_ecb_rel;
      MslEcbWorldPoints state_cur_ecb_points = {0};
      const uint8_t have_state_cur_ecb = mpcoll_state_current_ecb_points(
          batch, idx, &state_cur_ecb_points, prev_x, prev_y, ecb_frame_prev);
      const uint8_t jumpaerial_entry_ecb_consumer =
          // JumpAerial -> AttackAir/SpecialAirN entry callback-local ECB lifetime:
          // IASA can enter the aerial attack/special before the map callback, but the source
          // mpColl pass still promotes the carried JumpAerial CollData.ecb into prev_ecb before
          // loading/interpolating the entered action's desired ECB. Do not apply this to fresh
          // JumpF/JumpB -> AttackAir entries; those remain owned by the Jump collision substrate.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::{
          //   ftCo_JumpAerial_IASA,ftCo_JumpAerial_Coll}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
          //   ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80043754}
          ((prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
           (is_attackair_action(action_id) ||
            action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_START ||
            action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP ||
            action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_END))
              ? 1u
              : 0u;
      const uint8_t use_hidden_ecb_lifetime =
          (have_state_cur_ecb && !lock_bottom_to_zero && jumpaerial_entry_ecb_consumer)
              ? 1u
              : (escapeair_jumpaerial_prev_ecb_lifetime && !use_locked_desired_ecb_bottom);
      const float state_cur_ecb_rel =
          have_state_cur_ecb ? state_cur_ecb_points.bottom_rel_y : pre_entry_prev_ecb_rel;
      const float prev_ecb_rel =
          (use_hidden_ecb_lifetime && !lock_bottom_to_zero) ? state_cur_ecb_rel : pose_prev_ecb_rel;
      // CollData ECB lifetime substrate:
      // - mpColl_LoadECB_inline refreshes desired_ecb from the source ECB pose but preserves
      //   desired.bottom while CollData_X130_Locked is set.
      // - mpCollInterpolateECB first copies current ecb into prev_ecb, then moves ecb toward
      //   desired_ecb for the callback substep.
      // Runtime carries those three ECB point sets explicitly so EscapeAir/Fall/Jump callbacks ask
      // the same collision substrate instead of resampling previous motion rows locally.
      // refs/melee/src/melee/mp/mpcoll.c::{
      //   mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80043754}
      if (use_hidden_ecb_lifetime) {
        mpcoll_bottom_world_point_from_rel(&cur_bot, x, y, desired_ecb_rel, ecb_frame_cur);
        mpcoll_bottom_world_point_from_rel(&prev_bot, prev_x, prev_y, prev_ecb_rel, ecb_frame_prev);
      } else {
        msl_ecb_bottom_world_point_sample(&cur_bot, char_id, anim, ecb_frame_cur, x, y,
                                          lock_bottom_to_zero);
        msl_ecb_bottom_world_point_sample(&prev_bot, char_id, anim, ecb_frame_prev, prev_x, prev_y,
                                          lock_bottom_to_zero);
      }

      float cur_bottom_x = cur_bot.x;
      float cur_bottom_y = cur_bot.y;
      float prev_bottom_x = prev_bot.x;
      float prev_bottom_y = prev_bot.y;
      MslEcbWorldPoints cur_ecb_points = desired_ecb_points;
      MslEcbWorldPoints prev_ecb_points = {0};
      if (use_hidden_ecb_lifetime && have_state_cur_ecb) {
        prev_ecb_points = state_cur_ecb_points;
      } else {
        msl_ecb_world_points_sample(&prev_ecb_points, char_id, anim, ecb_frame_prev,
                                    facing_dir_for_ecb, prev_x, prev_y, was_grounded);
      }
      if (!use_hidden_ecb_lifetime && !lock_bottom_to_zero &&
          mpcoll_ground_specialhi_launch_uses_jobj_ecb(char_id, action_id)) {
        MslEcbWorldPoints specialhi_cur_ecb = cur_ecb_points;
        MslEcbWorldPoints specialhi_prev_ecb = prev_ecb_points;
        const uint8_t have_cur_specialhi_ecb = mpcoll_ground_try_sample_specialhi_jobj_ecb(
            &specialhi_cur_ecb, batch, idx, char_id, anim, action_id, ecb_frame, facing_dir_for_ecb,
            x, y);
        const uint8_t have_prev_specialhi_ecb = mpcoll_ground_try_sample_specialhi_jobj_ecb(
            &specialhi_prev_ecb, batch, idx, char_id, anim, action_id, ecb_frame_prev,
            facing_dir_for_ecb, prev_x, prev_y);
        if (have_cur_specialhi_ecb) {
          cur_ecb_points = specialhi_cur_ecb;
          cur_bottom_x = specialhi_cur_ecb.bottom_x;
          cur_bottom_y = specialhi_cur_ecb.bottom_y;
          cur_bot.x = specialhi_cur_ecb.bottom_x;
          cur_bot.y = specialhi_cur_ecb.bottom_y;
          cur_bot.rel_y = specialhi_cur_ecb.bottom_rel_y;
          cur_bot.frame_u16 = specialhi_cur_ecb.frame_u16;
        }
        if (have_prev_specialhi_ecb) {
          prev_ecb_points = specialhi_prev_ecb;
          prev_bottom_x = specialhi_prev_ecb.bottom_x;
          prev_bottom_y = specialhi_prev_ecb.bottom_y;
          prev_bot.x = specialhi_prev_ecb.bottom_x;
          prev_bot.y = specialhi_prev_ecb.bottom_y;
          prev_bot.rel_y = specialhi_prev_ecb.bottom_rel_y;
          prev_bot.frame_u16 = specialhi_prev_ecb.frame_u16;
        }
      }
      const float prev_side_mid_y =
          prev_y + (0.5f * (prev_ecb_points.top_rel_y + prev_ecb_points.bottom_rel_y));

      // Collision env flags (subset) for Parity Project #2 (ledge grab mask parity).
      // Decomp: CollData carries env_flags and prev_env_flags across frames.
      // refs/melee/src/melee/lb/types.h::CollData
      batch->state.coll_prev_env_flags[idx] = batch->state.coll_env_flags[idx];
      batch->state.coll_env_flags[idx] = 0;
      if (was_grounded || !is_damage_collision_landing_action(action_id) ||
          (batch->state.hitlag[idx] == 0u && batch->state.hitlag_pre_timer[idx] == 0u)) {
        batch->state.damage_hitlag_floorhug_latch[idx] = 0u;
      }

      // Contact persistence:
      // - CollData carries floor.index across frames and uses the line graph to traverse seams.
      // - Grounded resolution uses a per-line projection (mpLib_8004DD90_Floor) rather than
      //   reselecting from scratch each frame.
      // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DD7C (example of floor.index persistence)
      uint8_t on_ground = 0;
      const uint16_t seed_ground_id = batch->state.ground_id[idx];
      uint16_t ground_id = seed_ground_id;

      float floor_nx = 0.0f;
      float floor_ny = 1.0f;
      float contact_x = cur_bottom_x;
      float contact_y = 0.0f;
      uint8_t escapeair_locked_platform_root_projection_hit = 0u;
      uint8_t escapeair_stale_platform_root_handoff_hit = 0u;
      uint8_t escapeair_fresh_jump_height_platform_handoff_hit = 0u;
      uint8_t escapeair_no_lock_static_platform_sweep_hit = 0u;
      uint8_t common_fall_flags6_root_floor_projection_hit = 0u;

      int prefer_line_idx = -1;
      int raw_current_floor_line_idx = -1;
      if (ground_id != 0xFFFFu) {
        raw_current_floor_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
        prefer_line_idx = raw_current_floor_line_idx;
        if (!floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx)) {
          prefer_line_idx = -1;
        }
      }
      uint8_t cliff_ledge_floor_owner_selected = 0u;
      if (action_consumes_cliff_ledge_floor_owner(char_id, action_id) &&
          batch->state.ledge_cooldown[idx] != 0 &&
          batch->state.cliff_ledge_floor_segment_id != NULL &&
          batch->state.cliff_ledge_floor_segment_id[idx] != 0xFFFFu) {
        const uint16_t cliff_ledge_floor_id = batch->state.cliff_ledge_floor_segment_id[idx];
        const int cliff_ledge_line_idx =
            stage_collision_floor_line_index(stage_id, cliff_ledge_floor_id);
        const uint8_t prefer_line_is_ledge_floor =
            (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].is_ledge) ? 1u : 0u;
        const uint8_t prefer_line_is_platform_floor =
            (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].is_platform) ? 1u : 0u;
        if ((prefer_line_idx < 0 || prefer_line_is_ledge_floor || prefer_line_is_platform_floor) &&
            cliff_ledge_line_idx >= 0 &&
            floor_line_is_runtime_fighter_solid(g, stage_id, cliff_ledge_line_idx) &&
            g->lines[(size_t)cliff_ledge_line_idx].is_ledge) {
          // Source cliff/CollData floor owner:
          // CliffCatch/CliffWait store `mv.co.cliff.ledge_id`; release/drop sets
          // fp->x2064_ledgeCooldown, and the following air collision wrappers branch on that
          // timer. Immediate cliff exits carry the floor owner through Fall/JumpAerial/EscapeAir
          // collision callbacks while the timer is live, while Slippi-visible lastGroundId may
          // still name a stale platform, same-side ledge, or wrong-side ledge.
          // Prefer the hidden cliff floor only when the visible floor owner is missing or still a
          // ledge/platform owner; do not replace an already-updated hard main-floor index.
          // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          prefer_line_idx = cliff_ledge_line_idx;
          cliff_ledge_floor_owner_selected =
              (raw_current_floor_line_idx != cliff_ledge_line_idx) ? 1u : 0u;
        }
      }
      const uint8_t prefer_line_is_platform =
          (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].is_platform) ? 1u : 0u;
      const uint8_t prefer_line_is_slope =
          floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx);
      const uint8_t prefer_line_is_ledge =
          (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].is_ledge) ? 1u : 0u;
      const uint8_t prefer_line_is_platform_or_slope =
          (uint8_t)((prefer_line_is_platform || prefer_line_is_slope || prefer_line_is_ledge) ? 1u
                                                                                              : 0u);
      mpcoll_ctx.prefer_floor_line_idx = prefer_line_idx;
      const uint8_t cliff_ledge_floor_owner_active =
          (cliff_ledge_floor_owner_selected && prefer_line_is_ledge &&
           action_consumes_cliff_ledge_floor_owner(char_id, action_id) &&
           batch->state.ledge_cooldown[idx] != 0 &&
           batch->state.cliff_ledge_floor_segment_id != NULL &&
           batch->state.cliff_ledge_floor_segment_id[idx] != 0xFFFFu &&
           prefer_line_idx == stage_collision_floor_line_index(
                                  stage_id, batch->state.cliff_ledge_floor_segment_id[idx]))
              ? 1u
              : 0u;
      const uint16_t skip_platform_segment_i = platform_floor_skip_segment_id(batch, idx, stage_id);
      const uint8_t escapeair_locked =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active) ? 1u : 0u;
      uint8_t deep_lock_penetration = 0u;
      const uint8_t downdamage_x_axis_fresh_sdi_edge =
          ((action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
            action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) &&
           fabsf(apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[idx]),
                                c->lstick_deadzone_x)) >= c->lstick_tilt_x_thresh &&
           fabsf(apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_x[idx]),
                                c->lstick_deadzone_x)) < c->lstick_tilt_x_thresh)
              ? 1u
              : 0u;
      const uint8_t damage_active_hitlag_downward_sdi_airborne_owner =
          // Active-hitlag SDI/floor handoff:
          // - ftCo_Damage_OnEveryHitlag mutates cur_pos before the motion-state collision callback.
          // - ft_80081DD4's allow-SDI path can keep a below-floor SDI root airborne via
          //   mpColl_800477E0 / mpColl_80044948_Floor while hitlag remains frozen.
          // - DownDamageU/D re-enter that common callback through ftCo_8009F184, but floor-adjacent
          //   rows with a fresh x670 horizontal edge publish the horizontal displacement without
          //   treating the y component as a below-floor stay-airborne owner; the later
          //   DownDamage_Coll path owns floor contact. Rows whose fresh edge is vertical remain on
          //   the below-floor active-hitlag owner.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   ftCo_8008DCE0,ftCo_Damage_OnEveryHitlag}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
          //   ftCo_8009F184,ftCo_DownDamage_Phys,ftCo_DownDamage_Coll}
          // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // - The retained non-DownDamage slice is DamageAir3 active hitlag only: source routes it
          //   through the common Damage_Coll / mpColl_800477E0 allow-SDI floor path after a consumed
          //   downward OnEveryHitlag displacement. DamageAir2 floor-adjacent controls stay on the
          //   normal floor clamp path, and DamageFly* remains deferred until its separate collision
          //   handoff owner is closed. This avoids turning generic below-floor damage poses into a
          //   broad runtime floor-suppression bridge.
          ((batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
            batch->state.damage_hitlag_downward_sdi_consumed[idx] != 0u) &&
           (((action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
              action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) &&
             !downdamage_x_axis_fresh_sdi_edge) ||
            (action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_3 &&
             isfinite(batch->state.floor_sweep_prev_pos_y[idx]) &&
             batch->state.floor_sweep_prev_pos_y[idx] > k_floor_y_bias &&
             batch->state.pos_y[idx] < -k_floor_y_bias && batch->state.speed_y_attack[idx] < 0.0f)))
              ? 1u
              : 0u;
      uint8_t damage_active_hitlag_root_below_bottom_above_floor_owner = 0u;
      if (batch->state.hitlag[idx] != 0u && is_damage_fly_collision_action(action_id) &&
          batch->state.tilt_timer_y_frame_start[idx] >= c->sdi_tilt_max_frames &&
          prefer_line_idx >= 0 && !prefer_line_is_platform && !prefer_line_is_ledge) {
        // DamageFly active-hitlag can move the root below the carried floor through
        // ftCo_Damage_OnEveryHitlag on a same-frame downward stick edge. When x671 was not already
        // in the Y-window at frame start and the ECB bottom remains above the floor, source mpColl
        // does not publish the stay-airborne floor projection until mpColl_80044628_Floor accepts
        // the current ECB bottom; keep the SDI-mutated root for that first below-root frame.
        // Existing carried-Y-window DamageFly rows remain on the FloorPush/FloorHug owner above.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
        // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
        float current_bottom_floor_y = 0.0f;
        if (floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, cur_bottom_x,
                                      &current_bottom_floor_y) &&
            y < (current_bottom_floor_y - k_floor_y_bias) &&
            cur_bottom_y > (current_bottom_floor_y + k_floor_y_bias)) {
          damage_active_hitlag_root_below_bottom_above_floor_owner = 1u;
        }
      }

      MslMpcollOrderedWallCeilResult ordered_wall_ceil = {
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
      if (was_grounded) {
        // Source grounded inline2 ordering runs wall and ceiling collision before the floor pass.
        // Keep the scratch/result in the shared wall/ceiling helper so the floor pass can consume
        // same-frame wall side bits for mpColl_80044628_Floor.
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
        batch->state.wall_kind[idx] = 0u;
        batch->state.wall_contact_x[idx] = 0.0f;
        batch->state.wall_contact_y[idx] = 0.0f;
        batch->state.wall_normal_x[idx] = 0.0f;
        batch->state.wall_normal_y[idx] = 0.0f;
        batch->state.ceiling_contact_x[idx] = 0.0f;
        batch->state.ceiling_contact_y[idx] = 0.0f;
        batch->state.ceiling_normal_x[idx] = 0.0f;
        batch->state.ceiling_normal_y[idx] = 0.0f;
        mpcoll_grounded_wall_ceil_ordered_begin(&mpcoll_ctx, &prev_ecb_points, &cur_ecb_points,
                                                &ordered_wall_ceil);
        cur_ecb_points = ordered_wall_ceil.cur_ecb_after;
        cur_bottom_x = cur_ecb_points.bottom_x;
        cur_bottom_y = cur_ecb_points.bottom_y;
      }

      if (!was_grounded && escapeair_early_ledge_root_floor_owner && prefer_line_idx >= 0) {
        float y_corr = 0.0f;
        const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, x, y,
                                                          &y_corr, &floor_nx, &floor_ny);
        const float max_lift = fabsf(batch->state.speed_y_self[idx]) +
                               msl_ecb_bottom_rel_y(char_id, anim, 0) + k_ecb_vertical_unit;
        if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift) {
          batch->state.pos_y[idx] += y_corr - k_floor_y_bias;
          on_ground = 1u;
          ground_id = g->lines[(size_t)out_line_idx].segment_i;
          contact_x = x;
          contact_y = y + y_corr - k_floor_y_bias;
        } else {
          float ledge_line_y = 0.0f;
          if (floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, x, &ledge_line_y) &&
              prev_y > (ledge_line_y + k_floor_y_bias) && y <= ledge_line_y) {
            batch->state.pos_y[idx] = ledge_line_y + k_floor_y_bias;
            on_ground = 1u;
            ground_id = g->lines[(size_t)prefer_line_idx].segment_i;
            contact_x = x;
            contact_y = ledge_line_y;
          }
        }
      }

      uint8_t escapeair_missing_bottom_hard_floor_sweep_owner = 0u;
      if (!was_grounded && !on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          (stage_id == (uint32_t)MSL_STAGE_FINAL_DESTINATION_LOCAL ||
           stage_id == (uint32_t)MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL) &&
          prefer_line_idx >= 0 && ecb_lock_timer_seed != 0u &&
          (batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u ||
           batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR ||
           batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias) &&
          batch->state.action_frame[idx] <= 3 && batch->state.speed_y_self[idx] < 0.0f) {
        // Early locked EscapeAir hard-floor handoff:
        // ftCo_EscapeAir_Coll enters through ft_80082C74/mpColl_800471F8. Runtime rollouts can
        // reach the first sustained EscapeAir callback without a replay-seeded desired-bottom lane,
        // but the source callback still loads the entered EscapeAir ECB and can publish
        // LandingFallSpecial when the zeroed/desired bottom or root-crossing point sweeps onto the
        // carried hard-floor chain. Keep this retained slice to the existing FD owner plus the FoD
        // raised hard-floor cases validated here; Battlefield/Yoshi/Dream Land ledge-floor timing
        // still has adjacent zero-bottom/root-suppression differences and remains outside this
        // source-completion item. The owner-zero root crossing below remains a narrower
        // replay-prefix gap where the active lock is known but no desired-bottom lane was
        // serialized.
        // Direct replay-seeded rows with no active CollData lock stay on their explicit
        // desired/current ECB publication path instead of borrowing the locked handoff.
        //
        // data/stages/bin/*.bin::MSLSTG01 floor segment links
        // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
        const float escapeair_bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
        int candidate_lines[5];
        candidate_lines[0] = prefer_line_idx;
        candidate_lines[1] = g->lines[(size_t)prefer_line_idx].prev;
        candidate_lines[2] = g->lines[(size_t)prefer_line_idx].next;
        candidate_lines[3] = (stage_id == (uint32_t)MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL &&
                              candidate_lines[1] >= 0 && (size_t)candidate_lines[1] < g->line_count)
                                 ? g->lines[(size_t)candidate_lines[1]].prev
                                 : -1;
        candidate_lines[4] = (stage_id == (uint32_t)MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL &&
                              candidate_lines[2] >= 0 && (size_t)candidate_lines[2] < g->line_count)
                                 ? g->lines[(size_t)candidate_lines[2]].next
                                 : -1;
        for (size_t ci = 0; ci < 5 && !on_ground; ci++) {
          const int line_idx = candidate_lines[ci];
          if (line_idx < 0 || (size_t)line_idx >= g->line_count ||
              g->lines[(size_t)line_idx].is_platform ||
              stage_collision_floor_line_has_platform_transform(
                  stage_id, g->lines[(size_t)line_idx].segment_i)) {
            continue;
          }
          if (stage_id == (uint32_t)MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL &&
              g->lines[(size_t)line_idx].is_ledge &&
              (!floor_x_within_line_bounds(batch, bi, g, line_idx,
                                           batch->state.floor_sweep_prev_pos_x[idx]) ||
               !floor_x_within_line_bounds(batch, bi, g, line_idx, x))) {
            // FoD ledge-floor publication in this early locked EscapeAir path is an in-span
            // callback floor continuation. Endpoint entry from outside the ledge segment remains
            // airborne until a real bottom/root crossing reaches an in-span source floor result.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
            continue;
          }
          float line_y = 0.0f;
          if (!floor_line_y_at_x_ed5c_for_env(batch, bi, g, line_idx, x, &line_y)) {
            continue;
          }
          const uint8_t escapeair_early_root_crossing =
              (((batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 3u &&
                 (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR ||
                  batch->state.seed_prev_action_frame[idx] <= 0)) ||
                (batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 0u &&
                 stage_id != (uint32_t)MSL_STAGE_FINAL_DESTINATION_LOCAL &&
                 batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR)) &&
               prev_y > line_y + k_floor_y_bias && y <= line_y + k_floor_y_bias)
                  ? 1u
                  : 0u;
          const uint8_t escapeair_bottom_crossing =
              // Bottom-sweep publication must consume a real CollData desired-bottom owner.
              // JumpAerial -> EscapeAir zero-bottom entries are handled by the root projection
              // owner below; using the pose bottom here lands shallow Battlefield ledge exits one
              // callback early.
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
              (batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u &&
               prev_y + escapeair_bottom_rel0 > line_y + k_floor_y_bias &&
               y + escapeair_bottom_rel0 <= line_y + k_floor_y_bias)
                  ? 1u
                  : 0u;
          if (escapeair_bottom_crossing || escapeair_early_root_crossing) {
            batch->state.pos_y[idx] = line_y + k_floor_y_bias;
            on_ground = 1u;
            ground_id = g->lines[(size_t)line_idx].segment_i;
            contact_x = x;
            contact_y = line_y;
            floor_nx = 0.0f;
            floor_ny = 1.0f;
            escapeair_missing_bottom_hard_floor_sweep_owner = 1u;
          }
        }
      }

      if (!was_grounded && !on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          stage_id != (uint32_t)MSL_STAGE_FINAL_DESTINATION_LOCAL && prefer_line_idx >= 0 &&
          ecb_lock_timer_seed != 0u &&
          batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 0u &&
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          batch->state.action_frame[idx] <= 3 && batch->state.speed_y_self[idx] < 0.0f) {
        // Non-FD replay-prefix gap for early locked EscapeAir floor handoff:
        // the source callback is still `ftCo_EscapeAir_Coll -> ft_80082C74 -> mpColl_800471F8`,
        // but some legal-stage direct seeds expose an active CollData_X130_Locked timer and
        // callback-local previous root without the desired-bottom replay lane. On non-ledge
        // hard-floor chains, a previous-root -> current-root crossing is enough for
        // mpColl_80044838_Floor to publish LandingFallSpecial. FD retains the older
        // desired-bottom/locked-owner branch above because its rollout locks already cover that
        // source shape and owner-zero free-running FD states are a different hidden-state gap.
        // FoD ledge floors use the same owner only when both the previous and current callback roots
        // are inside the ledge span; endpoint entry from outside remains airborne.
        // data/stages/bin/*.bin::MSLSTG01 floor segment links
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
        int candidate_lines[5];
        candidate_lines[0] = prefer_line_idx;
        candidate_lines[1] = g->lines[(size_t)prefer_line_idx].prev;
        candidate_lines[2] = g->lines[(size_t)prefer_line_idx].next;
        candidate_lines[3] = (stage_id == (uint32_t)MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL &&
                              candidate_lines[1] >= 0 && (size_t)candidate_lines[1] < g->line_count)
                                 ? g->lines[(size_t)candidate_lines[1]].prev
                                 : -1;
        candidate_lines[4] = (stage_id == (uint32_t)MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL &&
                              candidate_lines[2] >= 0 && (size_t)candidate_lines[2] < g->line_count)
                                 ? g->lines[(size_t)candidate_lines[2]].next
                                 : -1;
        for (size_t ci = 0; ci < 5 && !on_ground; ci++) {
          const int line_idx = candidate_lines[ci];
          if (line_idx < 0 || (size_t)line_idx >= g->line_count ||
              g->lines[(size_t)line_idx].is_platform ||
              stage_collision_floor_line_has_platform_transform(
                  stage_id, g->lines[(size_t)line_idx].segment_i)) {
            continue;
          }
          const uint8_t line_is_ledge = g->lines[(size_t)line_idx].is_ledge ? 1u : 0u;
          const uint8_t ledge_span_owner =
              (line_is_ledge && stage_id == (uint32_t)MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL &&
               floor_x_within_line_bounds(batch, bi, g, line_idx,
                                          batch->state.floor_sweep_prev_pos_x[idx]) &&
               floor_x_within_line_bounds(batch, bi, g, line_idx, x))
                  ? 1u
                  : 0u;
          if (line_is_ledge && !ledge_span_owner) {
            continue;
          }
          if (!line_is_ledge && !floor_x_within_line_bounds(batch, bi, g, line_idx, x)) {
            continue;
          }
          float line_y = 0.0f;
          if (!floor_line_y_at_x_for_env(batch, bi, g, line_idx, x, &line_y)) {
            continue;
          }
          if (prev_y > line_y + k_floor_y_bias && y <= line_y + k_floor_y_bias) {
            batch->state.pos_y[idx] = line_y + k_floor_y_bias;
            on_ground = 1u;
            ground_id = g->lines[(size_t)line_idx].segment_i;
            contact_x = x;
            contact_y = line_y;
            floor_nx = 0.0f;
            floor_ny = 1.0f;
            escapeair_missing_bottom_hard_floor_sweep_owner = 1u;
          }
        }
      }

      if (was_grounded && prefer_line_idx >= 0) {
        float y_corr = 0.0f;
        const int out_line_idx =
            msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                     &y_corr, &floor_nx, &floor_ny);
        if (out_line_idx >= 0) {
          int resolved_line_idx = out_line_idx;
          const uint8_t down_bound_slope_root_snap =
              (uint8_t)(action_is_down_bound(action_id) &&
                        (floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx) ||
                         floor_line_is_generated_stage_slope(batch, bi, g, out_line_idx)));
          if (down_bound_slope_root_snap) {
            float root_line_y = 0.0f;
            if (floor_line_y_at_x_for_env(batch, bi, g, out_line_idx, batch->state.pos_x[idx],
                                          &root_line_y)) {
              // DownBound_Coll uses the allow-ground-to-air floor persistence path. When a grounded
              // DownBound root traverses generated legal-stage slopes, source keeps the root pinned
              // to the persisted floor line until `mpColl_8004B108` reports floor loss; projecting
              // the positive DownBound ECB bottom instead leaves the root stranded below the slope.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
              // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
              // refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B108,mplib.c::mpLib_8004DD90_Floor}
              y_corr = (root_line_y + k_floor_y_bias) - batch->state.pos_y[idx];
            }
          }
          if (!down_bound_slope_root_snap && resolved_line_idx != prefer_line_idx &&
              floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx) &&
              !floor_line_is_generated_stage_slope(batch, bi, g, resolved_line_idx)) {
            float root_line_y = 0.0f;
            if (floor_line_y_at_x_for_env(batch, bi, g, resolved_line_idx, batch->state.pos_x[idx],
                                          &root_line_y)) {
              // mpLib_8004DD90_Floor returns the floor line reached after prev/next traversal at a
              // seam. When a grounded callback moves from a generated legal-stage slope onto a flat
              // floor, the final root height belongs to the returned flat floor, not the slope
              // extrapolated past its endpoint.
              // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
              y_corr = (root_line_y + k_floor_y_bias) - batch->state.pos_y[idx];
            }
          }
          const uint8_t same_step_height_platform_reproject =
              grounded_same_step_height_platform_reproject(
                  batch, bi, g, stage_id, idx, action_id, batch->state.action_frame[idx],
                  resolved_line_idx, &resolved_line_idx, &y_corr);
          if (resolved_line_idx != prefer_line_idx &&
              stage_collision_floor_line_has_height_platform_transform(
                  stage_id, g->lines[(size_t)resolved_line_idx].segment_i) &&
              stage_collision_floor_line_height_platform_state_is_source_trusted(
                  batch, bi, g->lines[(size_t)resolved_line_idx].segment_i) &&
              !same_step_height_platform_reproject &&
              !grounded_entry_same_step_height_platform_admits_line(
                  batch, bi, idx, stage_id, action_id,
                  g->lines[(size_t)resolved_line_idx].segment_i)) {
            // The transformed FoD platform floor can be current source authority for a grounded
            // action-entry callback, but not for sustained same-action grounded motion. Without
            // this boundary, a stale sparse seed bit can make Dash/Run rows replace their current
            // floor index with the opposite height platform for one step.
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC}
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            float root_line_y = 0.0f;
            if (floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                          &root_line_y)) {
              y_corr = (root_line_y + k_floor_y_bias) - batch->state.pos_y[idx];
            } else {
              y_corr = 0.0f;
            }
            resolved_line_idx = prefer_line_idx;
          }
          if (action_uses_landing_floor_release_coll(action_id) &&
              batch->state.action_frame[idx] <= 1 &&
              stage_collision_floor_line_has_height_platform_transform(
                  stage_id, g->lines[(size_t)resolved_line_idx].segment_i) &&
              stage_collision_floor_line_height_platform_state_is_source_trusted(
                  batch, bi, g->lines[(size_t)resolved_line_idx].segment_i) &&
              !same_step_height_platform_reproject) {
            // Landing/LandingFallSpecial grIzumi height-platform source rows already carry the
            // callback-current transformed floor root in CollData. Reapplying the generic
            // mpLib_8004DD90 +0.0001 bias on the same source-trusted frame double-lifts the
            // retained landing owner and can later miss the DamageFly landing threshold. Keep this
            // out of ordinary grounded motion states such as Dash: a sparse seed source bit alone
            // is not source authority to replace their current floor index. The stage source mask
            // is transient; the frame scheduler clears seed provenance after the frame, so later rollout rows
            // must be reasserted by live scheduler/contact ownership before this bypass can apply.
            //
            // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
            y_corr = 0.0f;
          }
          const uint8_t keep_grounded_damage_hitlag_floor_snap =
              grounded_damage_hitlag_allows_downward_floor_projection(batch, idx, action_id);
          const uint8_t keep_capture_lw_floor_snap =
              is_capture_lw_allow_ground_to_air_collision_action(action_id);
          const uint8_t keep_slope_or_platform_floor_snap =
              grounded_persistence_allows_signed_dd90_y_correction(batch, bi, g, prefer_line_idx,
                                                                   resolved_line_idx, action_id,
                                                                   batch->state.action_frame[idx]);
          // mpLib_8004DD90_Floor returns a signed correction; for stable grounded frames we only
          // need to resolve penetration. If we are already above the floor due to upstream
          // approximation drift, avoid snapping down in the collision substrate.
          //
          // Exception: grounded damage hitlag rows let `ftCo_Damage_OnEveryHitlag` move `cur_pos`
          // before grounded `ftCo_Damage_Coll` re-pins the fighter to floor through
          // `ft_800848DC -> ft_80082708 -> mpColl_8004B108`. Keep the downward correction only
          // for that owner path; the general grounded anti-snap clamp stays in place elsewhere.
          // Exception: low capture states call the allow-ground-to-air collision wrapper after
          // `fn_800DAD18`; it owns re-projecting the attached grounded victim back onto the floor.
          if (y_corr < 0.0f && !keep_grounded_damage_hitlag_floor_snap &&
              !keep_capture_lw_floor_snap && !keep_slope_or_platform_floor_snap &&
              !down_bound_slope_root_snap) {
            y_corr = 0.0f;
          }
          // Source floor-index traversal:
          // mpLib_8004DD90_Floor returns the projected line after following connected floor
          // prev/next links at seams. Keep that returned line for grounded callbacks, including
          // same-frame Dash entries through ft_800844EC -> ft_80082708 -> mpColl_8004B108; the
          // previous bridge that forced Dash entry to keep seed floor.index was too broad at
          // legal-stage seam edges.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_800844EC,ft_80082708}
          // refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B108,mplib.c::mpLib_8004DD90_Floor}
          batch->state.pos_y[idx] += y_corr;
          on_ground = 1;
          ground_id = g->lines[(size_t)resolved_line_idx].segment_i;
          contact_y = (cur_bottom_y + y_corr);
        } else {
          // Decomp shape:
          // - Grounded collision uses mpLib_8004DD90_Floor to project onto the current floor line,
          //   but on failure it can still (a) snap to the current floor edge (mpColl_8004A45C_Floor,
          //   used by mpColl_8004B2DC) and/or (b) detect a floor hit via the swept segment test
          //   (mpCheckFloor-style) before concluding the fighter is airborne.
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
          //
          // This matters for downed rolls near the FD ledge: the integrated position can move
          // past the floor endpoint, but the motion segment still intersects the floor line and
          // the engine clamps the contact to the intersection point before leaving ground.
          uint8_t snapped_edge = 0;
          const MslStageFloorLine world_line =
              floor_line_world_for_env(batch, bi, g, prefer_line_idx);
          const MslStageFloorLine* l = &world_line;
          const float left_x = l->x0;
          const float left_y = l->y0;
          const float right_x = l->x1;
          const float right_y = l->y1;

          // mpColl_8004A45C_Floor: when the fighter passes beyond the current floor endpoint,
          // mpColl can snap the position to the edge point (if not blocked by a wall probe) while
          // keeping the fighter grounded for this collision result.
          if (action_allows_floor_edge_snap(action_id)) {
            if (cur_bottom_x <= left_x) {
              const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
              MslEcbWorldPoints ecb = {0};
              msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, left_x, left_y,
                                          was_grounded);
              const float probe_ax = left_x + k_floor_edge_wall_probe_x_offset;
              const float probe_ay = left_y + k_floor_edge_wall_probe_y_offset;
              const float probe_bx = left_x + (ecb.right_rel_x /* bottom.x == 0 */);
              const float probe_by = left_y + (ecb.side_rel_y - ecb.bottom_rel_y);
              const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
              if (!wall_blocks_floor_edge_probe(lwg, probe_ax, probe_ay, probe_bx, probe_by)) {
                int out_line_idx2 = msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, left_x,
                                                             left_y, NULL, &floor_nx, &floor_ny);
                if (out_line_idx2 < 0) {
                  out_line_idx2 = prefer_line_idx;
                }
                batch->state.pos_x[idx] += (left_x - cur_bottom_x);
                batch->state.pos_y[idx] = left_y;
                on_ground = 1;
                ground_id = g->lines[(size_t)out_line_idx2].segment_i;
                contact_x = left_x;
                contact_y = left_y;
                snapped_edge = 1;
              }
            } else if (cur_bottom_x >= right_x) {
              const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
              MslEcbWorldPoints ecb = {0};
              msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, right_x, right_y,
                                          was_grounded);
              const float probe_ax = right_x - k_floor_edge_wall_probe_x_offset;
              const float probe_ay = right_y + k_floor_edge_wall_probe_y_offset;
              const float probe_bx = right_x + (ecb.left_rel_x /* bottom.x == 0 */);
              const float probe_by = right_y + (ecb.side_rel_y - ecb.bottom_rel_y);
              const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);
              if (!wall_blocks_floor_edge_probe(rwg, probe_ax, probe_ay, probe_bx, probe_by)) {
                int out_line_idx2 = msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, right_x,
                                                             right_y, NULL, &floor_nx, &floor_ny);
                if (out_line_idx2 < 0) {
                  out_line_idx2 = prefer_line_idx;
                }
                batch->state.pos_x[idx] += (right_x - cur_bottom_x);
                batch->state.pos_y[idx] = right_y;
                on_ground = 1;
                ground_id = g->lines[(size_t)out_line_idx2].segment_i;
                contact_x = right_x;
                contact_y = right_y;
                snapped_edge = 1;
              }
            }
          }

          if (!snapped_edge) {
            const uint8_t landing_release_skip_floor_sweep =
                (action_uses_landing_floor_release_coll(action_id) &&
                 batch->state.action_frame[idx] <= 1)
                    ? 1u
                    : 0u;
            // Decomp: mpCheckFloor's horizontal intersection helper is gated on non-rising segments
            // (ay >= by), so equality must be allowed (horizontal motion with vy==0 can still sweep).
            // refs/melee/src/melee/mp/mplib.c::mpCheckFloor (the `if (ay >= by && mpLineIntersectionH(...))` gate)
            const uint8_t can_sweep = (uint8_t)(cur_bottom_y <= prev_bottom_y);
            int hit_line_idx = -1;
            float ix = 0.0f, iy = 0.0f;
            const int floor_sweep_skip_line_idx =
                // mpCheckFloor checks the current CollData.floor.index. The generic lite-sim pass
                // skips the preferred line because ordinary floor.index projection has already
                // tried it above, but the teacher-forced cliff floor owner is restored only for this
                // air-callback floor sweep. Include it so the shared CollData owner consumes the
                // same ledge floor rather than relying on a downstream EscapeAir exception.
                // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor
                cliff_ledge_floor_owner_active ? -1 : prefer_line_idx;
            if (!landing_release_skip_floor_sweep && !is_common_fallspecial_action(action_id) &&
                can_sweep &&
                msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                                  cur_bottom_x, cur_bottom_y, skip_platform_segment_i,
                                  prefer_line_idx, floor_sweep_skip_line_idx, c, &hit_line_idx, &ix,
                                  &iy, &floor_nx, &floor_ny)) {
              const uint8_t hit_line_is_platform =
                  (hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_platform) ? 1u : 0u;
              const uint8_t hit_line_is_ledge =
                  (hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_ledge) ? 1u : 0u;
              const uint8_t hit_line_has_platform_transform =
                  (hit_line_idx >= 0 && stage_collision_floor_line_has_platform_transform(
                                            stage_id, g->lines[(size_t)hit_line_idx].segment_i))
                      ? 1u
                      : 0u;
              const uint8_t hit_line_has_height_platform_transform =
                  (hit_line_idx >= 0 && stage_collision_floor_line_has_height_platform_transform(
                                            stage_id, g->lines[(size_t)hit_line_idx].segment_i))
                      ? 1u
                      : 0u;
              // Decomp: desired_ecb.bottom.x is always 0.0, so clamping the ECB bottom contact X
              // corresponds to clamping the fighter position X.
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
              float y_corr2 = 0.0f;
              const int out_line_idx2 = msl_mplib_8004dd90_floor(
                  batch, bi, g, hit_line_idx, ix, cur_bottom_y, &y_corr2, NULL, NULL);
              const uint16_t resolved_segment_i = (out_line_idx2 >= 0)
                                                      ? g->lines[(size_t)out_line_idx2].segment_i
                                                      : g->lines[(size_t)hit_line_idx].segment_i;
              const uint8_t resolved_line_has_platform_transform =
                  stage_collision_floor_line_has_platform_transform(stage_id, resolved_segment_i);
              const uint8_t resolved_line_has_height_platform_transform =
                  stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                           resolved_segment_i);
              const uint8_t resolved_line_is_platform =
                  (out_line_idx2 >= 0 && g->lines[(size_t)out_line_idx2].is_platform) ? 1u : 0u;
              const uint8_t resolved_line_is_ledge =
                  (out_line_idx2 >= 0 && g->lines[(size_t)out_line_idx2].is_ledge) ? 1u : 0u;
              const uint8_t escapeair_entry_locked_platform_airborne =
                  // EscapeAir's entry/locked ECB path keeps early transformed-platform crossings
                  // airborne in the source callback until the locked collision snapshot can resolve
                  // normally. Use the mpLib-projected line id here: FoD's source graph can sweep one
                  // platform record then remap to another.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80047E14}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 4 && hit_line_is_platform &&
                   (hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   (ecb_lock_active || (prev_action_id == action_id &&
                                        resolved_segment_i != batch->state.ground_id[idx])))
                      ? 1u
                      : 0u;
              const uint8_t escapeair_jumpaerial_entry_ledge_airborne =
                  // Fresh JumpAerial -> EscapeAir can enter the map callback with a locked
                  // pre-entry ECB while mpLib's floor projection walks from the carried hard floor
                  // to the adjacent ledge floor. Source `EscapeAir_Coll` still consumes the
                  // JumpAerial CollData lifetime on this first callback, so ledge-floor remaps stay
                  // airborne until the entered EscapeAir row is published. Keep transformed and
                  // ordinary soft-platform contacts on their explicit owners; this guard is only the
                  // generated ledge-floor branch of the same pre-entry lifetime.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
                  //   ftCo_80099A58,ftCo_EscapeAir_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
                  (escapeair_jumpaerial_entry && ecb_lock_active &&
                   batch->state.seed_prev_action_frame[idx] >= 2 &&
                   batch->state.seed_prev_action_frame[idx] <= 4 &&
                   (hit_line_is_ledge || resolved_line_is_ledge))
                      ? 1u
                      : 0u;
              const uint8_t specialhi_fall_understage_hard_floor_clip =
                  // SpecialAirHi_Coll/SpecialHiFall_Coll reach ft_CheckGroundAndLedge through the
                  // same special-hi mpColl owner. If the previous root is still below the accepted
                  // hard floor by more than the live ECB
                  // neighborhood, this is not a legitimate top-surface landing; it is the sim having
                  // missed the earlier wall/ceiling owner and then accepting the floor from inside
                  // the stage on descent.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
                  (specialhi_understage_floor_clip_action(action_id) && !hit_line_is_platform &&
                   !resolved_line_is_platform &&
                   prev_y < (iy - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                             k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t specialhi_from_below_hard_floor_clip =
                  // mpColl_80044628_Floor's floor owner is an above-to-floor ECB-bottom crossing.
                  // If both callback-local bottom endpoints and the previous root already start
                  // below the accepted hard floor while SpecialAirHi/SpecialHiFall is descending,
                  // an upward root projection would rebound from the stage interior rather than
                  // source `ft_CheckGroundAndLedge` contact.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
                  (specialhi_floor_candidate_starts_below_source_floor(
                       action_id, prev_y, prev_bottom_y, cur_bottom_y, iy,
                       batch->state.speed_y_self[idx]) &&
                   !hit_line_is_platform && !resolved_line_is_platform)
                      ? 1u
                      : 0u;
              const float transformed_platform_bottom_penetration = iy - cur_bottom_y;
              const uint8_t damage_transformed_platform_offspan_contact =
                  // mpCheckFloor can report a transformed-platform endpoint contact from the ECB
                  // bottom sweep, but source floor handoff still requires the callback-local root
                  // to be in the live floor span before ftCo_80090184 may publish Passive/DownBound.
                  // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (((hit_line_has_platform_transform &&
                     !floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, x)) ||
                    (resolved_line_has_platform_transform && out_line_idx2 >= 0 &&
                     !floor_x_within_line_segment_strict(batch, bi, g, out_line_idx2, x))) &&
                   is_damage_collision_landing_action(action_id) &&
                   batch->state.speed_y_attack[idx] > 0.0f && batch->state.hitstun[idx] != 0u)
                      ? 1u
                      : 0u;
              const MslCharParams* damage_floor_chp = msl_char_params(char_id);
              const float damage_height_platform_edge_slack =
                  (damage_floor_chp != NULL && isfinite(damage_floor_chp->ledge_snap_height))
                      ? (damage_floor_chp->ledge_snap_height * batch->state.fighter_scale_y[idx])
                      : k_ecb_vertical_unit;
              const uint8_t hit_line_height_platform_state_trusted =
                  (hit_line_has_height_platform_transform &&
                   stage_collision_floor_line_height_platform_state_is_source_trusted(
                       batch, bi, g->lines[(size_t)hit_line_idx].segment_i))
                      ? 1u
                      : 0u;
              const uint8_t resolved_line_height_platform_state_trusted =
                  (resolved_line_has_height_platform_transform &&
                   stage_collision_floor_line_height_platform_state_is_source_trusted(
                       batch, bi, resolved_segment_i))
                      ? 1u
                      : 0u;
              const uint8_t damage_height_platform_live_bottom_crossing =
                  ((hit_line_height_platform_state_trusted ||
                    resolved_line_height_platform_state_trusted) &&
                   prev_bottom_y > (iy + k_floor_y_bias) && cur_bottom_y <= (iy + k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t damage_height_platform_pending_owner =
                  // FoD height-transform platform DamageFly endpoint contacts still need the live
                  // grIzumi platform/CollData substep owner. `ft_80081DD4` sets
                  // coll->ledge_snap_height before calling `mpColl_800473CC`; use the extracted
                  // character ledge-snap height as the endpoint bound instead of a replay-local x
                  // window. Do not suppress first live ECB-bottom crossings: source
                  // `mpCheckFloor` accepts horizontal floors exactly when the bottom segment crosses
                  // from above to below the line.
                  // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
                  // data/characters/{fox,falco}.json::ledge_snap_height
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
                  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
                  // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor}
                  // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLineIntersectionH}
                  (((hit_line_has_height_platform_transform &&
                     floor_line_x_near_endpoint_for_env(batch, bi, g, hit_line_idx, x,
                                                        damage_height_platform_edge_slack)) ||
                    (resolved_line_has_height_platform_transform && out_line_idx2 >= 0 &&
                     floor_line_x_near_endpoint_for_env(batch, bi, g, out_line_idx2, x,
                                                        damage_height_platform_edge_slack))) &&
                   !damage_height_platform_live_bottom_crossing &&
                   is_damage_collision_landing_action(action_id) &&
                   batch->state.speed_y_attack[idx] > 0.0f && batch->state.hitstun[idx] != 0u)
                      ? 1u
                      : 0u;
              const uint8_t damage_terminal_height_platform_stale_floor =
                  // Late DamageFly rows can carry a stale hard-floor CollData.floor.index while a
                  // FoD height-transform platform is between the live root and the carried floor.
                  // Source `mpColl_80044628_Floor` still requires the live ECB-bottom floor
                  // precondition for the transformed line; do not let a deep bottom-only crossing
                  // publish a platform DownBound before the terminal DamageFly/DamageFall owner.
                  // The bound is the mpColl one-unit ECB neighborhood, narrowed to the terminal
                  // hitstun window so ordinary mid-hitstun platform landings remain live.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
                  //   ftCo_DamageFly_Anim,ftCo_DamageFly_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  ((hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   is_damage_fly_collision_action(action_id) && batch->state.hitstun[idx] <= 4u &&
                   !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]) &&
                   transformed_platform_bottom_penetration > (0.5f * k_ecb_vertical_unit))
                      ? 1u
                      : 0u;
              const uint8_t suppress_damage_transformed_platform_ecb_only_land =
                  // Damage/DamageFly platform contact is owned by ftCo_Damage*_Coll -> ft_80081DD4
                  // and mpCheckFloor's live CollData ECB sweep. Do not key this on residual KB sign:
                  // legal-stage platform landings can retain positive attack-Y while the ECB bottom
                  // descends through the floor. Reject only source-shaped platform contacts where
                  // the source floor pass has only an endpoint/off-span transformed-platform hit,
                  // plus the terminal stale hard-floor/height-platform owner. In-span static-y FoD
                  // support lines are ordinary platform floor contacts for the DamageFly
                  // tech/DownBound ladder.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
                  // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpCheckFloor}
                  (damage_transformed_platform_offspan_contact ||
                   damage_height_platform_pending_owner ||
                   damage_terminal_height_platform_stale_floor)
                      ? 1u
                      : 0u;
              const uint8_t suppress_attackair_transformed_platform_ecb_only_land =
                  // AttackAirN/Lw's generated submotion rows use AttackAir_Coll
                  // (ft_80082C74 -> mpColl_800471F8), whose floor path first accepts a live
                  // ECB-bottom crossing in mpColl_80044628_Floor and only then snaps the root
                  // through mpColl_80044838_Floor(ignore_bottom=true). On FoD height-transform
                  // platforms, sustained AttackAirN/Lw can expose a transformed-platform ECB
                  // crossing while both callback root endpoints are already below the platform;
                  // keep that boundary airborne until the source callback has a root/ECB floor
                  // handoff. Other AttackAir submotions remain ordinary landing candidates unless
                  // their extracted submotion row proves this owner.
                  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
                  ((hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   action_uses_late_attackair_platform_ecb_owner(char_id, action_id) &&
                   prev_action_id == action_id &&
                   move_tables_attackair_second_create_hitbox_phase(
                       char_id, action_id, batch->state.anim_frame_f32[idx]) &&
                   prev_y < iy && y < iy &&
                   transformed_platform_bottom_penetration > k_floor_y_bias)
                      ? 1u
                      : 0u;
              const uint8_t suppress_fallspecial_b_transformed_platform_skip =
                  // FallSpecialB can continue an mpColl floor-skip episode after the down input starts
                  // to release. Slippi seeds do not expose CollData.floor_skip for this callback, so use
                  // the same source-owned evidence the callback consumes: transformed-platform
                  // projection with a carried non-platform floor.index.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
                  ((hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   action_id == (uint16_t)MSL_ACT_FALL_SPECIAL_B &&
                   !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]))
                      ? 1u
                      : 0u;
              const uint8_t suppress_cliff_horizontal_ledge_locked_zero_bottom_hit =
                  // Source mpColl ordering for the restored cliff floor owner:
                  // mpColl_80046904 only consumes mpColl_80044838_Floor after
                  // mpColl_80044628_Floor's live ECB-bottom check. A direct one-step seed can
                  // reconstruct the cliff floor id while the locked EscapeAir bottom is still the
                  // zero-bottom handoff value; on flat ledge floors this can synthesize an
                  // early horizontal-floor sweep before the interpolated source ECB bottom reaches
                  // the floor. Keep sloped ledges with the raw low-floor owner below, and let later
                  // lock phases consume the horizontal ledge once the callback-local lock timer has
                  // advanced.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpCollInterpolateECB,mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (cliff_ledge_floor_owner_active &&
                   batch->state.cliff_ledge_floor_segment_seeded != NULL &&
                   batch->state.cliff_ledge_floor_segment_seeded[idx] != 0u && hit_line_idx >= 0 &&
                   (g->lines[(size_t)hit_line_idx].segment_i ==
                        batch->state.cliff_ledge_floor_segment_id[idx] ||
                    resolved_segment_i == batch->state.cliff_ledge_floor_segment_id[idx]) &&
                   !floor_line_is_generated_stage_slope(
                       batch, bi, g,
                       (resolved_segment_i == batch->state.cliff_ledge_floor_segment_id[idx] &&
                        out_line_idx2 >= 0)
                           ? out_line_idx2
                           : hit_line_idx) &&
                   ecb_lock_timer_seed > 1u)
                      ? 1u
                      : 0u;
              const uint8_t escapeair_low_floor_raw_hit_over_stale_platform_remap =
                  // mpCheckFloor owns the raw floor intersection before mpLib projection/remap. If
                  // that raw hit is Yoshi's low ledge floor but the carried CollData.floor.index
                  // projects to the elevated side platform, consume the low raw hit instead of
                  // snapping up to stale platform height.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
                  // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 4 && !hit_line_is_platform &&
                   resolved_line_has_platform_transform && resolved_line_is_platform &&
                   iy <= k_floor_y_bias && (cur_bottom_y + y_corr2) > k_floor_y_bias)
                      ? 1u
                      : 0u;
              if (escapeair_low_floor_raw_hit_over_stale_platform_remap) {
                batch->state.pos_x[idx] += (ix - cur_bottom_x);
                batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
                on_ground = 1;
                ground_id = g->lines[(size_t)hit_line_idx].segment_i;
                contact_x = ix;
                contact_y = iy;
              } else if (escapeair_entry_locked_platform_airborne ||
                         escapeair_jumpaerial_entry_ledge_airborne ||
                         specialhi_fall_understage_hard_floor_clip ||
                         specialhi_from_below_hard_floor_clip ||
                         suppress_damage_transformed_platform_ecb_only_land ||
                         suppress_attackair_transformed_platform_ecb_only_land ||
                         suppress_fallspecial_b_transformed_platform_skip ||
                         suppress_cliff_horizontal_ledge_locked_zero_bottom_hit) {
                floor_write_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id,
                                                   anim, ecb_frame, was_grounded);
              } else {
                batch->state.pos_x[idx] += (ix - cur_bottom_x);

                if (out_line_idx2 >= 0) {
                  batch->state.pos_y[idx] += y_corr2;
                  on_ground = 1;
                  ground_id = g->lines[(size_t)out_line_idx2].segment_i;
                  contact_x = ix;
                  contact_y = iy;
                } else {
                  // Sweep saw a floor segment, but projection failed (unexpected). Preserve the sweep
                  // contact point deterministically and apply the decomp-shaped +0.0001 floor bias.
                  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
                  batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
                  on_ground = 1;
                  ground_id = g->lines[(size_t)hit_line_idx].segment_i;
                  contact_x = ix;
                  contact_y = iy;
                }
              }
            } else if (!landing_release_skip_floor_sweep &&
                       msl_mpcoll_80044628_floor_wall_adjacent_fallback(
                           batch, idx, bi, g, stage_id, &ordered_wall_ceil, cur_bottom_x,
                           cur_bottom_y, skip_platform_segment_i, &ground_id, &contact_x,
                           &contact_y, &floor_nx, &floor_ny)) {
              on_ground = 1u;
            } else {
              // Decomp parity: mpColl_8004A45C_Floor can still set Collide_{Left,Right}Edge while
              // the floor collision pass does not report "touched_floor" (airborne), and the
              // ledge-grab block uses these bits as the `on_edge` suppression gate.
              floor_write_edge_suppression_flags(batch, idx, stage_id, g, prefer_line_idx, char_id,
                                                 anim, ecb_frame, was_grounded);
            }
          }
        }
      } else {
        int hit_line_idx = -1;
        float ix = 0.0f, iy = 0.0f;
        float damage_hitlag_exit_floor_y = 0.0f;
        const uint8_t damage_hitlag_exit_floor_y_valid =
            (prefer_line_idx >= 0 &&
             floor_line_y_at_x_for_env(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       &damage_hitlag_exit_floor_y))
                ? 1u
                : 0u;
        const uint8_t damage_hitlag_exit_floor_precondition =
            (!is_damage_fly_collision_action(action_id) ||
             (damage_hitlag_exit_floor_y_valid &&
              cur_bottom_y <= (damage_hitlag_exit_floor_y + k_floor_y_bias)))
                ? 1u
                : 0u;
        const uint8_t damage_hitlag_exit_projection_owner =
            (is_damage_collision_landing_action(action_id) &&
             batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u &&
             batch->state.damage_hitlag_floorhug_latch[idx] != 0u &&
             damage_hitlag_exit_floor_precondition)
                ? 1u
                : 0u;
        // DamageFlyRoll ownership lane for this floor-projection pass:
        // - keep x221C_b6-gated roll handling after hitlag-exit callback consumption,
        // - avoid re-running this lane on the same frame that Fighter_8006D10C consumes
        //   ftCo_Damage_OnExitHitlag.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006D10C
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
        //   ftCo_Damage_OnExitHitlag,ftCo_DamageFlyRoll_Phys,ftCo_DamageFlyRoll_Coll
        // }
        const uint8_t damageflyroll_iasa_lockout =
            (action_id == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL &&
             msl_state_flags_221c_b6_at(batch->state.state_flags, idx) &&
             batch->state.hitlag_pre_timer[idx] == 0u && batch->state.hitlag[idx] == 0u)
                ? 1u
                : 0u;
        const uint8_t active_damage_hitlag_stay_airborne_floor_owner =
            (batch->state.hitlag[idx] != 0u && is_damage_collision_landing_action(action_id) &&
             prefer_line_idx >= 0 &&
             action_uses_active_hitlag_downward_sdi_floorhug(action_id, batch, idx) &&
             damage_hitlag_floorhug_attempts_downward_sdi(batch, idx, c))
                ? 1u
                : 0u;
        const uint8_t active_damage_thrown_release_floor_owner =
            // Throw release can enter common Damage/DamageFly while the victim is already below the
            // persisted floor line and still in hitlag. The following Damage collision callback uses the
            // damage floor path to refresh FloorPush|FloorHug while keeping the fighter airborne;
            // there is no new OnEveryHitlag SDI input on these continuation rows, so keep this as a
            // separate Thrown* release owner rather than broadening the SDI predicate.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
            (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
             is_damage_collision_landing_action(action_id) &&
             msl_action_is_thrown_victim(batch->state.seed_prev_action_id[idx]) &&
             isfinite(batch->state.floor_sweep_prev_pos_y[idx]) &&
             batch->state.floor_sweep_prev_pos_y[idx] > batch->state.pos_y[idx] &&
             batch->state.pos_y[idx] < k_floor_y_bias)
                ? 1u
                : 0u;
        const uint8_t active_common_damage_entry_floor_owner =
            // Common DamageAir re-entry active-hitlag floorhug:
            // a BODY hit can refresh DamageAir1/2/3 into another DamageAir state while the victim
            // root is already below the persisted hard floor. The next
            // `ftCo_Damage_Coll -> ft_80081DD4` callback still runs the stay-airborne floor path
            // (`mpColl_800477E0`), projecting FloorPush/FloorHug without setting the fighter
            // grounded. Keep this separate from DamageHi/N/Lw, DamageFly, and DamageFlyRoll rows,
            // whose below-floor active-hitlag rows have their own owners and negative locks.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
            (batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] != 0u &&
             is_damage_collision_landing_action(action_id) &&
             !is_damage_fly_collision_action(action_id) && batch->state.action_frame[idx] == 1 &&
             batch->state.seed_prev_action_id[idx] != action_id &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_DAMAGE_AIR_1 ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_DAMAGE_AIR_2 ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_DAMAGE_AIR_3) &&
             batch->state.pos_y[idx] < k_floor_y_bias && prefer_line_idx >= 0)
                ? 1u
                : 0u;
        const uint8_t escapeair_jump_platform_root_owner =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active &&
             (prev_action_id == (uint16_t)MSL_ACT_JUMP_F ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_B ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B))
                ? 1u
                : 0u;
        const int escapeair_callback_pose_frame =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_frame[idx] >= 0)
                ? ((int)batch->state.seed_prev_action_frame[idx] + 1)
                : batch->state.action_frame[idx];
        const uint8_t escapeair_locked_desired_root_owner =
            (locked_desired_ecb_bottom_valid && prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR)
                ? 1u
                : 0u;
        const uint8_t escapeair_sustained_static_platform_root_owner =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR)
                ? 1u
                : 0u;
        if (!on_ground && escapeair_locked &&
            (ecb_lock_timer >= 4u || escapeair_jump_platform_root_owner ||
             escapeair_locked_desired_root_owner ||
             escapeair_sustained_static_platform_root_owner) &&
            (batch->state.speed_y_self[idx] < 0.0f || escapeair_jump_platform_root_owner) &&
            escapeair_locked_platform_root_projection(
                batch, idx, bi, g, stage_id, skip_platform_segment_i, ecb_lock_timer_seed,
                msl_ecb_bottom_rel_y(char_id, anim, (int)ecb_frame_cur),
                msl_ecb_bottom_rel_y(char_id, anim, escapeair_callback_pose_frame),
                msl_ecb_top_rel_y(char_id, anim, escapeair_callback_pose_frame),
                escapeair_locked_desired_root_owner,
                batch->state.coll_desired_ecb_bottom_rel_y[idx], &ground_id, &contact_x, &contact_y,
                &floor_nx, &floor_ny, c)) {
          on_ground = 1u;
          escapeair_locked_platform_root_projection_hit = 1u;
        }
        if (!on_ground &&
            escapeair_locked_floor_bottom_sweep_root_projection(
                batch, idx, bi, g, stage_id, &prev_ecb_points, skip_platform_segment_i, c,
                &ground_id, &contact_x, &contact_y, &floor_nx, &floor_ny)) {
          on_ground = 1u;
          escapeair_locked_platform_root_projection_hit = 1u;
        }
        if (!on_ground && escapeair_locked && prefer_line_idx >= 0 &&
            batch->state.speed_y_self[idx] < 0.0f &&
            escapeair_locked_hard_floor_zero_bottom_root_projection(
                batch, idx, bi, g, stage_id, prefer_line_idx, skip_platform_segment_i,
                escapeair_locked_desired_root_owner,
                batch->state.coll_desired_ecb_bottom_rel_y[idx], c, &ground_id, &contact_x,
                &contact_y, &floor_nx, &floor_ny)) {
          on_ground = 1u;
        }
        if (!on_ground &&
            action_uses_sideb_air_ft_check_ground_and_ledge_floor_coll(char_id, action_id) &&
            cur_bottom_y <= prev_bottom_y) {
          // Aerial Side-B direct ground/ledge callback:
          // `ft_CheckGroundAndLedge` uses the direct mpColl floor path (`mpColl_800473CC`, or
          // `mpColl_800471F8` while ledge cooldown/x2224_b2 is active). After
          // `mpColl_80044628_Floor` accepts an ECB-bottom floor hit, `mpColl_80044838_Floor`
          // projects from root when the loaded ECB bottom is positive. Do not pass the common-air
          // platform callback here: Side-B's direct helper has no held-down platform-pass predicate.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
          //   ftFx_SpecialAirSStart_Coll,ftFx_SpecialAirS_Coll,ftFx_SpecialAirSEnd_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_800473CC,mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
          int hit_line_idx = -1;
          float ix = 0.0f;
          float iy = 0.0f;
          if (msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                                cur_bottom_x, cur_bottom_y, skip_platform_segment_i,
                                prefer_line_idx, -1, NULL, &hit_line_idx, &ix, &iy, &floor_nx,
                                &floor_ny)) {
            float y_corr = 0.0f;
            const int out_line_idx =
                msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, batch->state.pos_x[idx],
                                         batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
            if (out_line_idx >= 0 && y_corr >= 0.0f) {
              batch->state.pos_y[idx] += y_corr;
              on_ground = 1u;
              ground_id = g->lines[(size_t)out_line_idx].segment_i;
              contact_x = batch->state.pos_x[idx];
              contact_y = batch->state.pos_y[idx];
            } else if (hit_line_idx >= 0) {
              batch->state.pos_x[idx] += (ix - cur_bottom_x);
              batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
              on_ground = 1u;
              ground_id = g->lines[(size_t)hit_line_idx].segment_i;
              contact_x = ix;
              contact_y = iy;
            }
          }
        }
        if (!on_ground && damage_hitlag_exit_projection_owner && prefer_line_idx >= 0) {
          // Damage hitlag-exit callback ownership:
          // - Damage entry writes `post_hitlag_cb = ftCo_Damage_OnExitHitlag`.
          // - Fighter_8006D10C invokes that callback on hitlag exit before Damage collision callback.
          // - DamageFly_Coll / Damage_Coll then resolve grounded contact via ft_80081DD4.
          // - In the non-allow_sdi air callback path, mpColl_80046904 may use
          //   mpColl_80044838_Floor(ignore_bottom=true), but only after mpColl_80044628_Floor has
          //   accepted the loaded ECB bottom as a floor hit. A carried active-hitlag FloorHug latch
          //   alone is not source authority to snap a hitlag-exit DamageFly root whose current ECB
          //   bottom remains above the floor.
          // - Restrict this to continuation frames where the current damage state has already hit
          //   the active-hitlag stay-airborne floorhug owner earlier in the same hitlag segment.
          //   Teacher-forced one-step rows do not seed that transient CollData continuation, and
          //   broad post-hitlag root snaps create new seed==ref damage landings outside the proven
          //   rollout-owned handoff.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   ftCo_8008DCE0,ftCo_Damage_OnExitHitlag,ftCo_Damage_Coll,ftCo_DamageFly_Coll
          // }
          // refs/melee/src/melee/ft/fighter.c::Fighter_8006D10C
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80046904,mpColl_80044838_Floor}
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float proj_x = 0.0f;
          float proj_y = 0.0f;
          stay_airborne_floor_projection_point(batch->state.pos_x[idx], batch->state.pos_y[idx],
                                               cur_bottom_x, cur_bottom_y, &proj_x, &proj_y);
          float y_corr = 0.0f;
          const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, proj_x,
                                                            proj_y, &y_corr, &floor_nx, &floor_ny);
          if (out_line_idx >= 0 && y_corr >= 0.0f) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = proj_x;
            contact_y = proj_y + y_corr;
            batch->state.damage_hitlag_floorhug_latch[idx] = 0u;
          }
        }
        uint8_t active_damage_hitlag_airborne_floor_contact = 0u;
        if (!on_ground &&
            (active_damage_hitlag_stay_airborne_floor_owner ||
             active_damage_thrown_release_floor_owner || active_common_damage_entry_floor_owner)) {
          // Stay-airborne floor contact ownership:
          // - mpColl_80044628_Floor sets FloorPush|FloorHug when the ECB bottom segment touches the
          //   persisted floor line.
          // - Under `CollisionFlagAir_StayAirborne`, mpColl_80044948_Floor can then project the
          //   fighter back to the floor boundary without setting touched_floor/grounded.
          // Reproduce that by projecting against the persisted floor.index, correcting root Y, and
          // emitting floor env bits while keeping `on_ground=0`.
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor,mpColl_80046904}
          float proj_x = 0.0f;
          float proj_y = 0.0f;
          stay_airborne_floor_projection_point(batch->state.pos_x[idx], batch->state.pos_y[idx],
                                               cur_bottom_x, cur_bottom_y, &proj_x, &proj_y);
          float y_corr = 0.0f;
          const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, proj_x,
                                                            proj_y, &y_corr, &floor_nx, &floor_ny);
          uint8_t active_damage_platform_floor_hit = 1u;
          if (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_platform) {
            int swept_line_idx = -1;
            active_damage_platform_floor_hit =
                (cur_bottom_y <= prev_bottom_y &&
                 msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                                   cur_bottom_x, cur_bottom_y, skip_platform_segment_i,
                                   out_line_idx, -1, c, &swept_line_idx, NULL, NULL, NULL, NULL) &&
                 swept_line_idx >= 0 &&
                 g->lines[(size_t)swept_line_idx].segment_i ==
                     g->lines[(size_t)out_line_idx].segment_i)
                    ? 1u
                    : 0u;
          }
          uint8_t active_damage_hard_floor_hit = 1u;
          if (out_line_idx >= 0 && !g->lines[(size_t)out_line_idx].is_platform &&
              !g->lines[(size_t)out_line_idx].is_ledge &&
              batch->state.tilt_timer_y_frame_start[idx] >= c->sdi_tilt_max_frames) {
            // mpColl_80044948_Floor projects from the root only after the floor collision pass has
            // accepted the ECB bottom as a current floor contact. A DamageFlyTop SDI row can move
            // the root below the floor while the current ECB bottom is still above it; vanilla
            // keeps that below-floor pose airborne rather than root-projecting immediately.
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
            float bottom_floor_y = 0.0f;
            active_damage_hard_floor_hit =
                (floor_line_y_at_x_for_env(batch, bi, g, out_line_idx, cur_bottom_x,
                                           &bottom_floor_y) &&
                 cur_bottom_y <= (bottom_floor_y + k_floor_y_bias))
                    ? 1u
                    : 0u;
          }
          if (out_line_idx >= 0 && y_corr >= 0.0f &&
              // Keep this bridge off ledge floor segments. mpColl edge/ledge suppression is owned
              // by separate floor-edge helpers, and replay-real AGN 4839/4840 stay airborne below
              // the FD ledge floor despite diagonal down input during DamageFlyTop hitlag.
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
              !g->lines[(size_t)out_line_idx].is_ledge &&
              // Soft-platform stay-airborne projection needs a real current bottom sweep. A
              // replay-seeded carried floor.index alone can represent hard-floor CollData
              // continuation, but using it for static platforms snaps active-hitlag SDI upward to a
              // platform the source mpColl_80044628_Floor never accepted.
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
              (!g->lines[(size_t)out_line_idx].is_platform || active_damage_platform_floor_hit) &&
              active_damage_hard_floor_hit) {
            batch->state.pos_y[idx] += y_corr;
            batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_FLOOR_MASK;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = proj_x;
            contact_y = proj_y + y_corr;
            active_damage_hitlag_airborne_floor_contact = 1u;
            batch->state.damage_hitlag_floorhug_latch[idx] = 1u;
          } else if (prefer_line_idx >= 0 && !g->lines[(size_t)prefer_line_idx].is_ledge &&
                     !g->lines[(size_t)prefer_line_idx].is_platform) {
            // Same active-hitlag owner, root fallback:
            // mpColl_80044948_Floor can use the fighter root when the loaded ECB bottom is not the
            // usable floor-contact point. Keep the fallback inside the already-proven
            // OnEveryHitlag/downward-SDI owner and require the persisted floor line to be a
            // non-ledge segment containing the root X. Ledge rows remain excluded by the control
            // above.
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80044948_Floor,mpColl_80046904}
            float root_y_corr = 0.0f;
            const int root_line_idx = msl_mplib_8004dd90_floor(
                batch, bi, g, prefer_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
                &root_y_corr, &floor_nx, &floor_ny);
            float root_bottom_floor_y = 0.0f;
            const uint8_t root_bottom_floor_hit =
                (batch->state.tilt_timer_y_frame_start[idx] < c->sdi_tilt_max_frames)
                    ? 1u
                    : (uint8_t)((root_line_idx >= 0 &&
                                 floor_line_y_at_x_for_env(batch, bi, g, root_line_idx,
                                                           cur_bottom_x, &root_bottom_floor_y) &&
                                 cur_bottom_y <= (root_bottom_floor_y + k_floor_y_bias))
                                    ? 1u
                                    : 0u);
            if (root_line_idx >= 0 && root_y_corr >= 0.0f &&
                !g->lines[(size_t)root_line_idx].is_ledge &&
                floor_x_within_line_bounds(batch, bi, g, root_line_idx, batch->state.pos_x[idx]) &&
                root_bottom_floor_hit) {
              batch->state.pos_y[idx] += root_y_corr;
              batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_FLOOR_MASK;
              ground_id = g->lines[(size_t)root_line_idx].segment_i;
              contact_x = batch->state.pos_x[idx];
              contact_y = batch->state.pos_y[idx];
              active_damage_hitlag_airborne_floor_contact = 1u;
              batch->state.damage_hitlag_floorhug_latch[idx] = 1u;
            }
          }
        }
        float damageflyroll_root_proj_y_corr = 0.0f;
        float damageflyroll_side_y_thresh = 0.0f;
        int damageflyroll_root_proj_line_idx = -1;
        uint8_t damageflyroll_root_proj_ready = 0u;
        uint8_t damageflyroll_deep_side_penetration = 0u;
        if (!on_ground && damageflyroll_iasa_lockout && prefer_line_idx >= 0) {
          // Decomp ownership: DamageFlyRoll_Coll resolves grounded follow-up via ft_80081DD4, which
          // runs mpColl_800473CC (air callback path) before ftCo_80090184 selection.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   ftCo_DamageFlyRoll_Phys,ftCo_DamageFlyRoll_Coll,ftCo_80090184
          // }
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80046904,mpColl_80044838_Floor}
          //
          // mpColl_LoadECB_JObj defines side-point Y as midpoint(bottom, top) + x124 offset.
          // Combine side- and bottom-point depths as a conservative callback-owned gate for the
          // root-based floor projection path (mpColl_80044838_Floor ignore_bottom branch).
          // This keeps shallow bottom-only crossings in-air while the DamageFlyRoll ownership lane
          // is still active, and turns over once the lane is deeply penetrated.
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
          const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
          MslEcbWorldPoints ecb_world = {0};
          msl_ecb_world_points_sample(&ecb_world, char_id, anim, ecb_frame_cur, facing_dir,
                                      batch->state.pos_x[idx], batch->state.pos_y[idx],
                                      lock_bottom_to_zero);
          damageflyroll_side_y_thresh =
              ecb_world.side_rel_y + ecb_world.bottom_rel_y + k_floor_y_bias;

          damageflyroll_root_proj_line_idx = msl_mplib_8004dd90_floor(
              batch, bi, g, prefer_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
              &damageflyroll_root_proj_y_corr, &floor_nx, &floor_ny);
          if (damageflyroll_root_proj_line_idx >= 0 && damageflyroll_root_proj_y_corr >= 0.0f) {
            damageflyroll_root_proj_ready = 1u;
            if (damageflyroll_root_proj_y_corr >= damageflyroll_side_y_thresh) {
              damageflyroll_deep_side_penetration = 1u;
              const float root_x = batch->state.pos_x[idx];
              const float root_y = batch->state.pos_y[idx];
              const float contact_y_root = root_y + damageflyroll_root_proj_y_corr;
              // Decomp-owned callback math for this lane:
              // - mpLib_8004DD90_Floor yields projection delta in world-space y (`y_out`).
              // - mpColl_80044838_Floor(ignore_bottom=true) applies that delta directly to
              //   coll->cur_pos.y with no additional cap.
              // - mpColl_LoadECB_JObj defines side-point y as midpoint(bottom, top) + x124.
              // Keep root placement as "root-contact minus callback-owned side-depth threshold"
              // without an extra clamp.
              // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044838_Floor,mpColl_LoadECB_JObj}
              float root_y_corr = damageflyroll_root_proj_y_corr - damageflyroll_side_y_thresh;
              // Callback-owned cap for this DamageFlyRoll projection lane:
              // - ft_80081DD4 routes through mpColl_800473CC -> mpColl_LoadECB_inline -> mpColl_80042384.
              // - keep root lift bounded to the ECB vertical unit used by that callback path
              //   (not mpLib_8004ED5C endpoint-extension constants).
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_LoadECB_inline,mpColl_80042384}
              if (root_y_corr > k_ecb_vertical_unit) {
                root_y_corr = k_ecb_vertical_unit;
              }
              batch->state.pos_y[idx] = root_y + root_y_corr;
              on_ground = 1;
              ground_id = g->lines[(size_t)damageflyroll_root_proj_line_idx].segment_i;
              contact_x = root_x;
              contact_y = contact_y_root;
            }
          }
        }
        const float escapeair_frame_start_prev_bottom_y =
            prev_y + msl_ecb_bottom_rel_y(char_id, anim, (int)ecb_frame_prev);
        const uint8_t escapeair_fresh_jumpaerial_entry_lock =
            (escapeair_jumpaerial_entry && ecb_lock_active &&
             batch->state.seed_prev_action_frame[idx] >= 2 &&
             batch->state.seed_prev_action_frame[idx] <= 4)
                ? 1u
                : 0u;
        // Fresh JumpAerial -> EscapeAir IASA handoff:
        // ftCo_80099A58 can change motion state before the map callback, but the first locked
        // EscapeAir collision pass still carries CollData's pre-entry ECB/floor lifetime. Do not let
        // generic floor projection consume that transition into LandingFallSpecial before the
        // source EscapeAir row is published.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
        //   ftCo_80099A58,ftCo_EscapeAir_Coll}
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
        const uint8_t escapeair_prev_ecb_ledge_entry_floor =
            (prefer_line_idx >= 0 && g->lines[(size_t)prefer_line_idx].is_ledge &&
             batch->state.action_frame[idx] <= 3)
                ? 1u
                : 0u;
        if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && prefer_line_idx >= 0 &&
            prev_y <= k_floor_y_bias && batch->state.speed_y_self[idx] < 0.0f &&
            batch->state.pos_y[idx] < 0.0f &&
            (!g->lines[(size_t)prefer_line_idx].is_ledge || escapeair_prev_ecb_ledge_entry_floor) &&
            escapeair_frame_start_prev_bottom_y <= k_floor_y_bias &&
            !(escapeair_fresh_jumpaerial_entry_lock && prefer_line_is_platform_or_slope) &&
            !(ecb_lock_active == 0u && prev_action_id == action_id &&
              batch->state.action_frame[idx] <= 6)) {
          // EscapeAir_Coll uses ft_80082C74 and the persisted CollData floor.index. Teacher-forced
          // one-step reseeds can begin after the full mpColl sweep has already placed root Y below
          // the floor while vanilla still resolves LandingFallSpecial from that same floor.index.
          // Keep this source floor-index handoff limited to rows with frame-start prev-ECB-bottom penetration and
          // post-physics root below the floor. Ledge-floor rows use this only in the early entry
          // window covered by the locked prev-ECB approximation; later ledge handoffs are handled by
          // the root-crossing owner below, and already-below-ledge continuations stay on the normal
          // EscapeAir lock path so the previous-ECB approximation cannot synthesize an extra
          // LandingFallSpecial frame.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          uint8_t cliff_ledge_floor_owner_floor_check_hit = 1u;
          if (cliff_ledge_floor_owner_active) {
            // mpColl_80046904 first requires mpColl_80044628_Floor to report an ECB-bottom floor
            // hit; only then can mpColl_80044838_Floor use the root point when ecb.bottom.y > 0.
            // The restored cliff floor id supplies CollData.floor.index, but it must not skip the
            // preceding bottom-sweep contact. This keeps shallow ledge-release EscapeAir rows
            // airborne until the loaded/interpolated ECB bottom actually crosses the source ledge
            // floor.
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            int cliff_hit_line_idx = -1;
            cliff_ledge_floor_owner_floor_check_hit =
                (msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                                   cur_bottom_x, cur_bottom_y, skip_platform_segment_i,
                                   prefer_line_idx, -1, c, &cliff_hit_line_idx, NULL, NULL, NULL,
                                   NULL) &&
                 cliff_hit_line_idx >= 0 &&
                 g->lines[(size_t)cliff_hit_line_idx].segment_i ==
                     batch->state.cliff_ledge_floor_segment_id[idx])
                    ? 1u
                    : 0u;
          }
          const float cliff_ledge_floor_owner_depth_gate =
              cliff_ledge_floor_owner_active ? fabsf(msl_ecb_bottom_rel_y(char_id, anim, 0)) : 0.0f;
          const float max_lift = cliff_ledge_floor_owner_active
                                     ? (msl_ecb_bottom_rel_y(char_id, anim, 0) + fabsf(y - prev_y) +
                                        fabsf(batch->state.speed_y_self[idx]) +
                                        mpcoll_floor_projection_lift_allowance(&cur_ecb_points))
                                     : (cur_bot.rel_y + fabsf(batch->state.speed_y_self[idx]) +
                                        (2.0f * k_ecb_vertical_unit));
          const uint8_t projected_line_is_platform =
              (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_platform) ? 1u : 0u;
          const uint8_t projected_line_is_slope =
              (out_line_idx >= 0 && floor_line_is_generated_stage_slope(batch, bi, g, out_line_idx))
                  ? 1u
                  : 0u;
          const uint8_t projected_line_is_ledge =
              (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_ledge) ? 1u : 0u;
          const uint8_t projected_line_has_platform_transform =
              (out_line_idx >= 0 && stage_collision_floor_line_has_platform_transform(
                                        stage_id, g->lines[(size_t)out_line_idx].segment_i))
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_root_below_projection =
              // This root-below guard is for callback rows whose source CollData.floor.index already
              // owns a hard/platform floor handoff. Do not use it when CollData_X130_Locked's seeded
              // desired bottom is still above a non-platform floor, or for generated sloped ledge
              // entries whose floor-edge owner remains airborne.
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_8004A45C_Floor}
              ((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                batch->state.action_frame[idx] <= 4 && !projected_line_is_platform &&
                !projected_line_has_platform_transform &&
                batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
                batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u &&
                batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
                batch->state.coll_desired_ecb_bottom_rel_y[idx] > y_corr) ||
               // Fresh JumpAerial -> EscapeAir ledge entries with no desired-bottom depth remain
               // on the zero-bottom/edge-suppression owner for this callback; the desired-bottom
               // floor handoff above handles the matching source case with an explicit bottom
               // owner.
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
               // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044838_Floor}
               ((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                 batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
                batch->state.action_frame[idx] <= 2 && projected_line_is_ledge &&
                batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias) ||
               (projected_line_is_slope &&
                ((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                  batch->state.action_frame[idx] <= 2) ||
                 batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND)) ||
               (escapeair_fresh_jumpaerial_entry_lock &&
                (projected_line_is_platform || projected_line_is_slope || projected_line_is_ledge)))
                  ? 1u
                  : 0u;
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift &&
              !suppress_escapeair_root_below_projection &&
              (!cliff_ledge_floor_owner_active ||
               (cliff_ledge_floor_owner_floor_check_hit &&
                y_corr + k_floor_y_bias >= cliff_ledge_floor_owner_depth_gate))) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
          }
        }
        if (!on_ground && escapeair_locked && prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            batch->state.action_frame[idx] >= 4 && prefer_line_idx >= 0 &&
            batch->state.speed_y_self[idx] < 0.0f && prev_y > k_floor_y_bias &&
            batch->state.pos_y[idx] < 0.0f) {
          // Sustained EscapeAir ledge-floor callback handoff:
          // ftCo_EscapeAir_Coll delegates to ft_80082C74 -> ft_80081D0C -> mpColl_800471F8.
          // While CollData_X130_Locked is still active, mpColl_LoadECB_inline can resolve the
          // callback from the persisted floor.index/root position even when the lite sim's raw
          // SSANIM bottom point stays above the floor. Keep this narrow to a real root crossing
          // from above the floor bias to below the floor in the same callback. Rows already hugging
          // the floor bias or already below the ledge floor remain owned by the normal EscapeAir lock
          // suppression and later collision phases.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          const float max_lift = fabsf(batch->state.speed_y_self[idx]);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift) {
            batch->state.pos_y[idx] += (y_corr - k_floor_y_bias);
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
          }
        }
        if (!on_ground && cliff_ledge_floor_owner_active &&
            action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && prefer_line_idx >= 0 &&
            floor_line_is_generated_stage_slope(batch, bi, g, prefer_line_idx) &&
            batch->state.speed_y_self[idx] < 0.0f &&
            !(escapeair_fresh_jumpaerial_entry_lock && prefer_line_is_platform_or_slope)) {
          // Sloped cliff-floor root owner:
          // mpColl_80044838_Floor(ignore_bottom=true) applies the root projection after the air
          // callback has accepted the floor. For Yoshi's sloped ledge floors, the raw bottom sweep
          // and mpLib projection boundary is the generated sloped ledge itself; once the restored
          // Cliff/CollData floor id is active, use that same floor line and require root penetration
          // at least as deep as the entered EscapeAir ECB bottom span. Horizontal ledges stay on the
          // ordinary bottom-sweep path above so locked zero-bottom interpolation cannot snap early.
          // data/stages/bin/grst.bin::MSLSTG01 line 2/6
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_80044628_Floor,mpColl_80044838_Floor}
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          const float bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
          const float max_lift = bottom_rel0 + fabsf(y - prev_y) +
                                 fabsf(batch->state.speed_y_self[idx]) +
                                 mpcoll_floor_projection_lift_allowance(&cur_ecb_points);
          if (out_line_idx >= 0 &&
              g->lines[(size_t)out_line_idx].segment_i ==
                  batch->state.cliff_ledge_floor_segment_id[idx] &&
              floor_x_within_line_bounds(batch, bi, g, out_line_idx, batch->state.pos_x[idx]) &&
              y_corr >= (bottom_rel0 - k_floor_y_bias) && y_corr <= max_lift) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
          }
        }
        if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && prefer_line_idx >= 0 &&
            !g->lines[(size_t)prefer_line_idx].is_ledge &&
            (prev_action_id == (uint16_t)MSL_ACT_FALL_AERIAL ||
             prev_action_id == (uint16_t)MSL_ACT_FALL_AERIAL_F ||
             prev_action_id == (uint16_t)MSL_ACT_FALL_AERIAL_B)) {
          // Fresh FallAerial -> EscapeAir ECB handoff:
          // EscapeAir_Coll still owns landing via ft_80082C74 on the entry collision pass, but the
          // motion-state change can move the sampled previous bottom below the floor before the
          // generic sweep observes the crossing. Project against the persisted floor.index only for
          // this fresh, non-ledge entry window, bounded by EscapeAir's own initial ECB bottom height.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{ftCo_80099A58,ftCo_EscapeAir_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          const float bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                       &y_corr, &floor_nx, &floor_ny);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= bottom_rel0) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = cur_bottom_x;
            contact_y = cur_bottom_y + y_corr;
          }
        }
        if (!on_ground && escapeair_locked && prefer_line_idx >= 0 &&
            !(escapeair_fresh_jumpaerial_entry_lock && prefer_line_is_platform_or_slope)) {
          const float bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
          deep_lock_penetration = (bottom_rel0 > 0.0f && cur_bottom_y <= -bottom_rel0 &&
                                   batch->state.speed_y_self[idx] < 0.0f)
                                      ? 1u
                                      : 0u;
          if (deep_lock_penetration) {
            // Decomp shape: while CollData_X130_Locked is active, EscapeAir_Coll can still resolve
            // against the persisted floor.index via mpLib_8004DD90_Floor-style projection.
            // Keep the projection on non-rising rows; mpCheckFloor's floor intersection path is
            // likewise non-rising (`ay >= by`).
            // Ledge floor caveat: allow shallow ledge-floor contact only while the root is still
            // horizontally above the ledge floor and either (a) in the early locked entry window or
            // (b) in a later below-floor continuation that was already below the floor on the previous
            // CollData snapshot. The immediate above->below crossing phase is handled by the
            // root-crossing owner above; off-end rows and the intermediate below-ledge frame remain
            // airborne instead of teleporting up onto the stage.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
            // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            float y_corr = 0.0f;
            const int out_line_idx =
                msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                         &y_corr, &floor_nx, &floor_ny);
            const float ledge_projection_depth_limit =
                bottom_rel0 + fabsf(y - prev_y) +
                (cliff_ledge_floor_owner_active
                     ? mpcoll_floor_projection_lift_allowance(&cur_ecb_points)
                     : 0.0f);
            const uint8_t is_ledge_floor = g->lines[(size_t)prefer_line_idx].is_ledge;
            const uint8_t ledge_x_in_bounds =
                floor_x_within_line_bounds(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx]);
            const uint8_t ledge_escapeair_phase_owner =
                (is_ledge_floor && ledge_x_in_bounds &&
                 (batch->state.action_frame[idx] <= 3 ||
                  (prev_y <= k_floor_y_bias && batch->state.action_frame[idx] >= 5)))
                    ? 1u
                    : 0u;
            const uint16_t resolved_segment_i =
                (out_line_idx >= 0) ? g->lines[(size_t)out_line_idx].segment_i : 0xFFFFu;
            const uint8_t resolved_line_is_ledge =
                (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_ledge) ? 1u : 0u;
            const uint8_t resolved_ledge_x_in_bounds =
                (resolved_line_is_ledge &&
                 floor_x_within_line_bounds(batch, bi, g, out_line_idx, batch->state.pos_x[idx]))
                    ? 1u
                    : 0u;
            const uint8_t suppress_transformed_remap_projection =
                // Locked EscapeAir projection uses the persisted CollData.floor.index owner. If the
                // FoD transformed-platform graph remaps that persisted line to a different platform
                // during the first locked frames, source keeps the airdodge airborne until the
                // ordinary collision sweep owns the handoff.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
                (out_line_idx >= 0 && batch->state.action_frame[idx] <= 4 &&
                 resolved_segment_i != batch->state.ground_id[idx] &&
                 stage_collision_floor_line_has_platform_transform(stage_id, resolved_segment_i))
                    ? 1u
                    : 0u;
            const uint8_t suppress_same_platform_projection_from_below =
                // CollData.floor.index projection is not a substitute for mpCheckFloor crossing.
                // If the carried floor is an elevated platform and both loaded ECB bottom endpoints
                // are already below that platform, EscapeAir_Coll must not snap upward through the
                // platform. Leave the low-floor/ledge contact for the ordinary sweep below.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                (out_line_idx >= 0 && g->lines[(size_t)out_line_idx].is_platform &&
                 (cur_bottom_y + y_corr) > k_floor_y_bias && prev_bottom_y <= k_floor_y_bias &&
                 cur_bottom_y <= k_floor_y_bias)
                    ? 1u
                    : 0u;
            const uint8_t suppress_off_end_ledge_remap_projection =
                // mpLib endpoint extension can make a persisted center floor project onto an
                // adjacent ledge floor even when the fighter root is outside that ledge segment.
                // In the early locked EscapeAir window, keep that off-end ledge remap airborne and
                // leave true in-bounds ledge/root continuations to the late below-floor owner.
                // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                (resolved_line_is_ledge && !resolved_ledge_x_in_bounds &&
                 batch->state.action_frame[idx] <= 3)
                    ? 1u
                    : 0u;
            const uint8_t resolved_ledge_prev_x_in_bounds =
                (resolved_line_is_ledge &&
                 floor_x_within_line_bounds(batch, bi, g, out_line_idx,
                                            batch->state.floor_sweep_prev_pos_x[idx]))
                    ? 1u
                    : 0u;
            const uint8_t suppress_ledge_endpoint_entry_projection =
                // The sustained EscapeAir ledge-floor projection is a callback-local floor
                // continuation, not an endpoint catch. If the previous CollData root was outside the
                // resolved ledge span, source `mpCheckFloor` does not let mpLib endpoint projection
                // synthesize an immediate LandingFallSpecial as the root enters from off-end.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                (stage_id == (uint32_t)MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL &&
                 resolved_line_is_ledge && !resolved_ledge_prev_x_in_bounds &&
                 batch->state.action_frame[idx] <= 3)
                    ? 1u
                    : 0u;
            const uint8_t suppress_jumpaerial_entry_shallow_ledge_projection =
                // Fresh JumpAerial -> EscapeAir can carry the center-floor index while projecting
                // onto the adjacent Yoshi ledge. Source reaches this projection only after the
                // entered EscapeAir ECB bottom is deep enough for mpColl_80044838_Floor's snap; a
                // shallow remap is just persisted-floor projection, not a floor callback result.
                // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                (resolved_line_is_ledge && !is_ledge_floor &&
                 resolved_segment_i != seed_ground_id &&
                 (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                  batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
                 batch->state.seed_prev_action_frame[idx] <= 4 &&
                 batch->state.action_frame[idx] <= 3 && y_corr >= 0.0f &&
                 y_corr < (bottom_rel0 - k_floor_y_bias))
                    ? 1u
                    : 0u;
            const uint8_t suppress_cliff_ledge_floor_shallow_projection =
                // The same depth owner applies when a teacher-forced seed restores the actual
                // cliff/CollData ledge floor up front. A live cliff floor id selects the correct
                // source floor, but mpColl_80044838_Floor still requires penetration at least as
                // deep as the entered EscapeAir ECB bottom before the root projection may snap onto
                // that ledge floor.
                // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044838_Floor}
                (cliff_ledge_floor_owner_active && is_ledge_floor && y_corr >= 0.0f &&
                 y_corr < (bottom_rel0 - k_floor_y_bias))
                    ? 1u
                    : 0u;
            int low_raw_line_idx = -1;
            float low_raw_y = 0.0f;
            const float low_raw_max_lift = fabsf(cur_bottom_y - prev_bottom_y) +
                                           fabsf(batch->state.speed_y_self[idx]) + bottom_rel0 +
                                           mpcoll_floor_projection_lift_allowance(&cur_ecb_points);
            if (suppress_same_platform_projection_from_below &&
                floor_find_low_raw_floor_contact(batch, idx, bi, g, stage_id, cur_bottom_x,
                                                 cur_bottom_y, low_raw_max_lift,
                                                 skip_platform_segment_i, c, &low_raw_line_idx,
                                                 &low_raw_y, &floor_nx, &floor_ny) &&
                // Source depth owner:
                // mpColl_80044838_Floor only uses the root point (`ignore_bottom=true`) after
                // mpColl_LoadECB_inline has an above-root ECB bottom. A locked EscapeAir row whose
                // raw low-floor hit is shallower than the entry pose bottom remains owned by the
                // ordinary sweep/edge-suppression pass; deeper rows can consume the low raw floor
                // instead of remapping to a stale side-platform floor.index.
                // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044838_Floor}
                (low_raw_y - cur_bottom_y) >= (bottom_rel0 - k_floor_y_bias)) {
              batch->state.pos_y[idx] += (low_raw_y - cur_bottom_y) + k_floor_y_bias;
              ground_id = g->lines[(size_t)low_raw_line_idx].segment_i;
              on_ground = 1;
              contact_x = cur_bottom_x;
              contact_y = low_raw_y;
            } else if (out_line_idx >= 0 && y_corr >= 0.0f &&
                       !suppress_transformed_remap_projection &&
                       !suppress_same_platform_projection_from_below &&
                       !suppress_off_end_ledge_remap_projection &&
                       !suppress_ledge_endpoint_entry_projection &&
                       !suppress_jumpaerial_entry_shallow_ledge_projection &&
                       !suppress_cliff_ledge_floor_shallow_projection &&
                       (!is_ledge_floor ||
                        (ledge_escapeair_phase_owner && y_corr <= ledge_projection_depth_limit))) {
              batch->state.pos_y[idx] += y_corr;
              ground_id = resolved_segment_i;
              on_ground = 1;
              contact_x = cur_bottom_x;
              contact_y = cur_bottom_y + y_corr;
            }
          }
        }
        if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed == 1u &&
            prefer_line_idx >= 0 &&
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            g->lines[(size_t)prefer_line_idx].is_platform &&
            !floor_x_within_line_bounds(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx]) &&
            batch->state.speed_y_self[idx] < 0.0f && batch->state.action_frame[idx] <= 3) {
          // Locked EscapeAir stale-platform handoff:
          // CollData.floor.index can still name the platform from the JumpAerial entry frame after
          // root motion has carried the fighter over a different static platform. Source
          // EscapeAir_Coll still routes through ft_80082C74/mpColl_800471F8 and may publish the
          // platform under the current root instead of requiring the stale CollData floor.index to
          // be horizontally in span. Keep this out of height-transformed moving platforms and in
          // the locked early EscapeAir callback phase so FoD height-state and hard-floor handoffs stay on their
          // existing owners.
          //
          // data/stages/bin/*.bin::MSLSTG01 platform flags/endpoints
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline}
          uint8_t found_platform = 0u;
          float best_lift = 0.0f;
          int best_line_idx = -1;
          float best_nx = 0.0f;
          float best_ny = 1.0f;
          for (size_t li = 0; li < g->line_count; li++) {
            if ((int)li == prefer_line_idx || !g->lines[li].is_platform ||
                stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                         g->lines[li].segment_i) ||
                !floor_line_is_runtime_fighter_solid(g, stage_id, (int)li) ||
                !floor_x_within_line_bounds(batch, bi, g, (int)li, batch->state.pos_x[idx])) {
              continue;
            }
            float line_y = 0.0f;
            if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, batch->state.pos_x[idx],
                                           &line_y)) {
              continue;
            }
            const float y_corr = (line_y + k_floor_y_bias) - batch->state.pos_y[idx];
            const float max_lift = msl_ecb_bottom_rel_y(char_id, anim, 0) +
                                   fabsf(batch->state.speed_y_self[idx]) +
                                   mpcoll_floor_projection_lift_allowance(&cur_ecb_points);
            if (y_corr >= 0.0f && y_corr <= max_lift && (!found_platform || y_corr < best_lift)) {
              found_platform = 1u;
              best_lift = y_corr;
              best_line_idx = (int)li;
              best_nx = 0.0f;
              best_ny = 1.0f;
            }
          }
          if (found_platform && best_line_idx >= 0) {
            batch->state.pos_y[idx] += best_lift;
            on_ground = 1;
            ground_id = g->lines[(size_t)best_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
            floor_nx = best_nx;
            floor_ny = best_ny;
            escapeair_stale_platform_root_handoff_hit = 1u;
          }
        }
        if (!on_ground && escapeair_locked && prefer_line_idx >= 0 &&
            (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_F ||
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_B) &&
            batch->state.action_frame[idx] <= 2 && batch->state.speed_y_self[idx] < 0.0f) {
          // Fresh JumpF/JumpB -> EscapeAir floor handoff:
          // - ftCo_80099A58 changes to EscapeAir during IASA, then EscapeAir_Coll runs in the
          //   same Fighter proc and delegates floor handling to ft_80082C74.
          // - CollData.floor.index can traverse connected floor seams during mpLib_8004DD90_Floor;
          //   a teacher-forced seed that begins on a ledge floor can therefore land on the adjacent
          //   center floor in the same callback pass.
          // - Keep this off JumpAerial entry rows; those use the standard sweep/ledge suppression
          //   below and include replay-real controls that remain airborne past the right ledge.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
          //   ftCo_80099A58,ftCo_EscapeAir_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          const float max_lift =
              fabsf(batch->state.speed_y_self[idx]) + (2.0f * k_ecb_vertical_unit);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
          } else {
            uint8_t found_height_platform = 0u;
            float best_height_platform_lift = 0.0f;
            int best_height_platform_line_idx = -1;
            float best_height_platform_nx = 0.0f;
            float best_height_platform_ny = 1.0f;
            for (size_t li = 0; li < g->line_count; li++) {
              if (!g->lines[li].is_platform ||
                  !stage_collision_floor_line_has_height_platform_transform(
                      stage_id, g->lines[li].segment_i) ||
                  !stage_collision_floor_line_height_platform_state_is_source_trusted(
                      batch, bi, g->lines[li].segment_i) ||
                  !floor_line_is_runtime_fighter_solid(g, stage_id, (int)li) ||
                  !floor_x_within_line_bounds(batch, bi, g, (int)li, batch->state.pos_x[idx])) {
                continue;
              }
              float line_y = 0.0f;
              if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, batch->state.pos_x[idx],
                                             &line_y)) {
                continue;
              }
              const float lift = (line_y + (2.0f * k_floor_y_bias)) - batch->state.pos_y[idx];
              if (lift < 0.0f || lift > max_lift ||
                  batch->state.pos_y[idx] > (line_y + (2.0f * k_floor_y_bias))) {
                continue;
              }
              if (!found_height_platform || lift < best_height_platform_lift ||
                  (lift == best_height_platform_lift &&
                   g->lines[li].segment_i <
                       g->lines[(size_t)best_height_platform_line_idx].segment_i)) {
                found_height_platform = 1u;
                best_height_platform_lift = lift;
                best_height_platform_line_idx = (int)li;
                best_height_platform_nx = 0.0f;
                best_height_platform_ny = 1.0f;
              }
            }
            if (found_height_platform && best_height_platform_line_idx >= 0) {
              // Fresh JumpF/JumpB -> EscapeAir can enter EscapeAir before Fighter_procMap, then the
              // source EscapeAir_Coll/mpColl_800471F8 pass may accept FoD's current height-platform
              // floor under the root even when CollData.floor.index still names the main floor. Keep
              // this to source-trusted MSLSTG01 height-transform platforms; ordinary static platforms
              // and sustained EscapeAir rows remain on their existing floor-index/skip owners.
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
              //   ftCo_80099A58,ftCo_EscapeAir_Coll}
              // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              batch->state.pos_y[idx] += best_height_platform_lift;
              on_ground = 1;
              ground_id = g->lines[(size_t)best_height_platform_line_idx].segment_i;
              contact_x = batch->state.pos_x[idx];
              contact_y = batch->state.pos_y[idx] - k_floor_y_bias;
              floor_nx = best_height_platform_nx;
              floor_ny = best_height_platform_ny;
              escapeair_fresh_jump_height_platform_handoff_hit = 1u;
            }
          }
        }
        if (!on_ground && batch->state.speed_y_self[idx] < 0.0f &&
            msl_mpcoll_80047e14_flags6_root_floor_projection(
                batch, idx, bi, g, stage_id, prev_x, prev_y, x, y, prefer_line_idx,
                skip_platform_segment_i, ecb_lock_timer_seed, c, &ground_id, &contact_x, &contact_y,
                &floor_nx, &floor_ny)) {
          on_ground = 1u;
          common_fall_flags6_root_floor_projection_hit = 1u;
        }
        if (!on_ground && attackair_flags0_floor_root_projection(
                              batch, idx, bi, g, stage_id, &prev_ecb_points, &cur_ecb_points,
                              prefer_line_idx, skip_platform_segment_i, c, &ground_id, &contact_x,
                              &contact_y, &floor_nx, &floor_ny)) {
          on_ground = 1u;
        }
        if (!on_ground && is_common_fallspecial_action(action_id) &&
            batch->state.speed_y_self[idx] < 0.0f) {
          // FallSpecial_Coll uses ft_80083090 -> mpColl_80047E14. Floor contact is owned by
          // mpColl_80044628_Floor's bottom sweep into the callback-visible root, not by a later
          // post-physics root-y projection. The helper consumes the same broad source owner and
          // then applies mpColl_80044838_Floor's root snap if that bottom sweep hit.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
          //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80083090
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
          if (msl_mpcoll_80047e14_fallspecial_prephysics_floor_sweep(
                  batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y, cur_bot.rel_y,
                  prefer_line_idx, skip_platform_segment_i, c, &ground_id, &contact_x, &contact_y,
                  &floor_nx, &floor_ny)) {
            on_ground = 1;
          }
        }
        if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && !ecb_lock_active &&
            ecb_lock_timer_seed == 0u && batch->state.speed_y_self[idx] < 0.0f) {
          // Sustained no-lock EscapeAir platform sweep:
          // EscapeAir_Coll delegates through ft_80082C74/mpColl_800471F8, which loads the current
          // EscapeAir ECB and lets mpColl_80044628_Floor admit soft platforms without the
          // ftCo_80096CC8 down-stick pass-through callback. If CollData.floor.index still names a
          // hard floor, the source floor hit can still be a static soft platform under the current
          // ECB bottom. Scan generated fighter-solid soft platforms and require the pose ECB bottom
          // to cross the platform during this callback. FoD's generated static-y center platform is
          // admitted only when the carried CollData.floor.index is still a static/hard floor; a
          // carried transformed-platform floor index stays on its explicit platform source owner
          // instead of allowing a stale static-y side snap.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
          // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
          uint8_t found_static_platform = 0u;
          int best_static_platform_line_idx = -1;
          float best_static_platform_dist2 = 0.0f;
          float best_static_platform_ix = 0.0f;
          float best_static_platform_iy = 0.0f;
          const float prev_pose_bottom_y =
              prev_y + mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame, 0u);
          const float cur_pose_bottom_y =
              y + mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_bias_next, 0u);
          const uint16_t carried_floor_segment_i = batch->state.ground_id[idx];
          const uint8_t carried_floor_is_transformed =
              stage_collision_floor_line_has_platform_transform(stage_id, carried_floor_segment_i);
          if (prev_pose_bottom_y >= cur_pose_bottom_y) {
            for (size_t li = 0; li < g->line_count; li++) {
              const uint8_t line_is_static_y_transform =
                  stage_collision_floor_line_has_static_y_platform_transform(
                      stage_id, g->lines[li].segment_i);
              if (!g->lines[li].is_platform ||
                  (line_is_static_y_transform != 0u && carried_floor_is_transformed != 0u) ||
                  !floor_line_admitted_by_source_callback(batch, idx, g, stage_id, (int)li, 0xFFFFu,
                                                          c)) {
                continue;
              }
              float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
              floor_ed5c_endpoints(batch, bi, g, (int)li, &x0, &y0, &x1, &y1);
              float ix = 0.0f, iy = 0.0f;
              if (!floor_intersect_horiz(x0, y0, x1, prev_x, prev_pose_bottom_y, x,
                                         cur_pose_bottom_y, &ix, &iy)) {
                continue;
              }
              if ((iy - cur_pose_bottom_y) >
                  (fabsf(batch->state.speed_y_self[idx]) + k_floor_y_bias)) {
                // mpColl_80044628_Floor observes a platform only when the callback-local ECB
                // bottom reaches that floor during the current vertical step. Dream Land top-
                // platform overstep rows can have the interpolated bottom cross high above the
                // root while the current EscapeAir step remains short of the platform; those stay
                // airborne until a later callback.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044628_Floor}
                continue;
              }
              const float dx = ix - prev_x;
              const float dy = iy - prev_pose_bottom_y;
              const float dist2 = dx * dx + dy * dy;
              if (!found_static_platform || dist2 < best_static_platform_dist2 ||
                  (dist2 == best_static_platform_dist2 &&
                   g->lines[li].segment_i <
                       g->lines[(size_t)best_static_platform_line_idx].segment_i)) {
                found_static_platform = 1u;
                best_static_platform_line_idx = (int)li;
                best_static_platform_dist2 = dist2;
                best_static_platform_ix = ix;
                best_static_platform_iy = iy;
              }
            }
          }
          if (found_static_platform && best_static_platform_line_idx >= 0) {
            batch->state.pos_y[idx] = best_static_platform_iy + k_floor_y_bias;
            on_ground = 1;
            ground_id = g->lines[(size_t)best_static_platform_line_idx].segment_i;
            contact_x = best_static_platform_ix;
            contact_y = best_static_platform_iy;
            floor_nx = 0.0f;
            floor_ny = 1.0f;
            escapeair_no_lock_static_platform_sweep_hit = 1u;
          }
        }
        if (!on_ground &&
            is_just_entered_specialairn_end_from_loop(action_id, prev_action_id,
                                                      batch->state.action_frame[idx]) &&
            prefer_line_idx >= 0 && batch->state.speed_y_self[idx] < 0.0f &&
            batch->state.pos_y[idx] <= -fabsf(cur_bot.rel_y)) {
          // SpecialAirNLoop_Anim can enter SpecialAirNEnd during the Anim callback, then the
          // entered state's collision callback still routes through AirCatchHit_Coll ->
          // ft_80082B1C -> Landing_Enter_Basic in the same Fighter proc. Teacher-forced one-step
          // reseeds that begin after the loop frame has already sunk the root below the persisted
          // floor.index need a callback-owned root projection on the just-entered End frame. Require
          // the root itself to be deeper than the current ECB-bottom height below the floor so ordinary
          // shallow Loop->End rows can remain airborne in SpecialAirNEnd.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
          //   ftFx_SpecialAirNLoop_Anim,ftFx_SpecialAirNEnd_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ftCo_AirCatchHit_Coll
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float y_corr = 0.0f;
          const int out_line_idx =
              msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          const float max_lift =
              cur_bot.rel_y + fabsf(batch->state.speed_y_self[idx]) + (2.0f * k_ecb_vertical_unit);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
          }
        }
        // Decomp: mpCheckFloor's horizontal intersection helper is gated on non-rising segments
        // (`ay >= by`), so upward sweeps should not report a floor crossing.
        // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
        const uint8_t can_sweep = (uint8_t)(cur_bottom_y <= prev_bottom_y);
        const uint8_t escapeair_sustained_floorhug_airborne =
            // Sustained horizontal EscapeAir can stay airborne while sliding at FD's floor bias.
            // This is still EscapeAir_Coll ownership, not a generic mpLib_8004DD90_Floor resting
            // contact: downward/deep-penetration rows use the normal ft_80082C74 landing paths, while
            // pure horizontal floor-hug rows remain air through the decay window.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
            //   ftCo_EscapeAir_Phys,ftCo_EscapeAir_Coll}
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.action_frame[idx] >= 2 &&
             fabsf(batch->state.speed_y_self[idx]) <= k_floor_horiz_dy_thresh &&
             fabsf(batch->state.pos_y[idx] - k_floor_y_bias) <= k_floor_horiz_dy_thresh &&
             prefer_line_idx >= 0 && !g->lines[(size_t)prefer_line_idx].is_ledge)
                ? 1u
                : 0u;
        const uint8_t escapeair_fresh_horizontal_floorhug_airborne =
            (escapeair_locked &&
             // Fresh Jump/KneeBend -> EscapeAir entry can publish a horizontal air dodge already at
             // FD's floor bias. ftCo_EscapeAir_Coll owns real floor handoffs through ft_80082C74, but
             // the same-pass entry row is not grounded by a generic mpLib_8004DD90_Floor resting
             // contact when the EscapeAir self velocity has no downward component. Keep this scoped to
             // the fresh common-air entry owners; downward or deep-penetration rows still use the
             // existing EscapeAir floor handoff paths below.
             // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::{
             //   ftCo_Jump_Enter,ftCo_Jump_IASA}
             // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
             //   ftCo_80099A58,ftCo_80099A9C,ftCo_EscapeAir_Coll}
             (prev_action_id == (uint16_t)MSL_ACT_KNEE_BEND ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_F ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_B) &&
             batch->state.action_frame[idx] <= 1 &&
             fabsf(batch->state.speed_y_self[idx]) <= k_floor_horiz_dy_thresh &&
             fabsf(batch->state.pos_y[idx] - k_floor_y_bias) <= k_floor_horiz_dy_thresh &&
             prefer_line_idx >= 0 && !g->lines[(size_t)prefer_line_idx].is_ledge)
                ? 1u
                : 0u;
        const uint8_t specialhi_bound_entry_airborne =
            (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI_BOUND &&
             // ftFx_SpecialHiBound_Enter changes motion state and ticks anim, but it does not call
             // ftCommon_8007D7FC. The rebound remains airborne on the entry collision row; later
             // Bound_Coll owns ground conversion through ft_CheckGroundAndLedge.
             // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
             //   ftFx_SpecialHiBound_Enter,ftFx_SpecialHiBound_Coll}
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI &&
             batch->state.action_frame[idx] <= 2)
                ? 1u
                : 0u;
        const uint8_t downdamage_active_hitlag_resting_floor_contact =
            // DownDamage_Coll uses the same ft_80081DD4 floor callback as damage landing paths.
            // Active-hitlag DownDamage rows can arrive at the post-hitlag collision pass already
            // resting on the persisted floor line while still carrying the Damage hitstun lane.
            // After ftCo_DownDamage_Phys delegates to ftCo_Damage_Phys, the current self velocity
            // is no longer a reliable pre-collision predicate; the narrow source-shaped boundary
            // for the root fallback is the same post-hitlag DownDamage row plus downward attack KB.
            // Keep this off DamageAir/DamageFly active hitlag, where replay-real controls stay
            // airborne until their separate damage hitlag-exit handoff.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Phys
            // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            ((action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
              action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) &&
             batch->state.hitlag_pre_timer[idx] != 0u && batch->state.speed_y_attack[idx] < 0.0f)
                ? 1u
                : 0u;
        if (!on_ground && !active_damage_hitlag_airborne_floor_contact &&
            !damage_active_hitlag_downward_sdi_airborne_owner && can_sweep &&
            msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                              cur_bottom_x, cur_bottom_y, skip_platform_segment_i, prefer_line_idx,
                              -1, c, &hit_line_idx, &ix, &iy, &floor_nx, &floor_ny)) {
          const uint8_t hit_line_is_platform =
              (hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_platform) ? 1u : 0u;
          const uint8_t hit_line_is_slope =
              floor_line_is_generated_stage_slope(batch, bi, g, hit_line_idx);
          const uint8_t hit_line_is_ledge =
              (hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_ledge) ? 1u : 0u;
          const uint8_t hit_line_is_platform_or_slope =
              (uint8_t)((hit_line_is_platform || hit_line_is_slope) ? 1u : 0u);
          const uint8_t hit_line_is_platform_slope_or_ledge =
              (uint8_t)((hit_line_is_platform || hit_line_is_slope || hit_line_is_ledge) ? 1u : 0u);
          const uint8_t hit_line_has_platform_transform =
              (hit_line_idx >= 0 && stage_collision_floor_line_has_platform_transform(
                                        stage_id, g->lines[(size_t)hit_line_idx].segment_i))
                  ? 1u
                  : 0u;
          const uint8_t hit_line_has_height_platform_transform =
              (hit_line_idx >= 0 && stage_collision_floor_line_has_height_platform_transform(
                                        stage_id, g->lines[(size_t)hit_line_idx].segment_i))
                  ? 1u
                  : 0u;
          const uint8_t hit_line_x_in_strict_segment =
              (hit_line_idx >= 0 &&
               floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, cur_bottom_x))
                  ? 1u
                  : 0u;
          // EscapeAir lock semantics:
          // - keep shallow crossings on ledge floor segments airborne while lock is active;
          // - allow deep-penetration fallback above to ground deterministically.
          //
          // Decomp shape: floor-edge collision bits and ledge suppression are handled separately via
          // mpColl edge helpers, so touching a ledge floor segment is not always equivalent to
          // immediate grounded resolution in EscapeAir lock windows.
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
          const uint8_t escapeair_sustained_floor_handoff =
              (escapeair_locked &&
               // Callback ordering is Anim then Coll (Fighter_procMap/Fighter_8006A360). Require a
               // sustained EscapeAir ownership window (already EscapeAir at frame start) and post-
               // entry anim age before handing ledge-floor sweeps to floor projection.
               // On ledge floor segments this same handoff is also bounded by the fresh
               // CollData_X130_Locked window. Late-lock EscapeAir rows still carry a persisted
               // floor.index, but replay-real rows remain airborne over the ledge floor until a
               // later callback phase; letting those low-x130 frames resolve here grounds air dodge
               // several frames early.
               // refs/melee/src/melee/ft/fighter.c::{Fighter_procMap,Fighter_8006A360}
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
               // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
               prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.action_frame[idx] >= 3 &&
               (hit_line_idx < 0 || !g->lines[(size_t)hit_line_idx].is_ledge ||
                ecb_lock_timer >= 5u || escapeair_early_ledge_root_floor_owner))
                  ? 1u
                  : 0u;
          const uint8_t escapeair_kneebend_entry_floor_handoff =
              (escapeair_locked &&
               // Decomp path: KneeBend transitions into jump, EscapeAir can be entered from the
               // jump startup window, and EscapeAir_Coll resolves landing via ft_80082C74 on the
               // same callback pass. Keep this handoff narrow to immediate KneeBend-entry rows.
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
               // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
               prev_action_id == (uint16_t)MSL_ACT_KNEE_BEND &&
               batch->state.action_frame[idx] <= 2 && !escapeair_fresh_horizontal_floorhug_airborne)
                  ? 1u
                  : 0u;
          const uint8_t escapeair_jump_entry_floor_handoff =
              (escapeair_locked &&
               // Decomp path: JumpF/JumpB can feed directly into EscapeAir through ftCo_80099A58,
               // and EscapeAir_Coll still owns same-pass landing via ft_80082C74.
               // Keep this restricted to the immediate post-entry window after JumpF/JumpB.
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
               //   ftCo_80099A58,ftCo_EscapeAir_Coll}
               // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
               (prev_action_id == (uint16_t)MSL_ACT_JUMP_F ||
                prev_action_id == (uint16_t)MSL_ACT_JUMP_B) &&
               batch->state.action_frame[idx] <= 1 && !escapeair_fresh_horizontal_floorhug_airborne)
                  ? 1u
                  : 0u;
          const uint8_t escapeair_flags6_deep_floor_handoff =
              // Source floor handoff for locked EscapeAir:
              // ftCo_EscapeAir_Coll -> ft_80082C74 -> ft_80081D0C -> mpColl_800471F8 uses
              // mpColl_LoadECB_inline(flags=6) followed by mpColl_80043754 interpolation. Once the
              // loaded/interpolated bottom is deep enough for mpColl_80044838_Floor to own the
              // callback, ledge, hard-floor, and platform contacts share the same source predicate.
              // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_LoadECB_inline,mpColl_80043754,mpColl_80044838_Floor}
              (escapeair_locked && hit_line_idx >= 0 && hit_line_x_in_strict_segment &&
               (iy - cur_bottom_y) >= msl_ecb_bottom_rel_y(char_id, anim, 0))
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_ledge_land =
              (escapeair_locked && !deep_lock_penetration && hit_line_idx >= 0 &&
               g->lines[(size_t)hit_line_idx].is_ledge && !escapeair_sustained_floor_handoff &&
               !escapeair_early_ledge_root_floor_owner && !escapeair_kneebend_entry_floor_handoff &&
               !escapeair_jump_entry_floor_handoff && !escapeair_flags6_deep_floor_handoff)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_off_end_platform_land =
              // mpLib endpoint extension can clamp a floor query to a soft-platform endpoint even
              // when the current ECB bottom/root is outside that platform span. During the early
              // locked EscapeAir window, that off-end platform hit is not a stable source floor
              // callback result; keep it airborne like the adjacent off-end ledge remap guards.
              // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              (escapeair_locked && hit_line_idx >= 0 && hit_line_is_platform &&
               !hit_line_x_in_strict_segment && batch->state.action_frame[idx] <= 3)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_seed6_platform_land =
              // The first sustained EscapeAir platform/slope crossing after the CollData lock
              // enters the map callback with frame-start lock value 6. Source does not publish that
              // as the stable platform floor handoff yet; the handoff rows retained above are the
              // next lock phase, where the accepted line is inside the callback ECB envelope.
              // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
              // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D5D4,ftCommon_UnlockECB}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
              (escapeair_locked && ecb_lock_timer_seed == 6u &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               hit_line_idx >= 0 && hit_line_is_platform_or_slope &&
               batch->state.action_frame[idx] <= 3)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_vertical_af3_land =
              (escapeair_locked && !deep_lock_penetration && !escapeair_flags6_deep_floor_handoff &&
               hit_line_idx >= 0 && !hit_line_is_ledge &&
               // Decomp-shaped interpolation gap: in the early EscapeAir lock window, vertical-only
               // platform/slope sweep crossings can report a floor hit one frame earlier than
               // replay references. Once the current root is already below the accepted floor by at
               // least the entered EscapeAir ECB bottom extent, the source
               // ft_80082C74/mpColl_800471F8 floor handoff owns the landing instead of the shallow
               // interpolation-gap guard.
               batch->state.action_frame[idx] == 3 &&
               (hit_line_is_platform_or_slope ||
                (prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                 (batch->state.pos_y[idx] > -0.25f * k_ecb_vertical_unit ||
                  batch->state.prev_pos_y[idx] < 0.0f))) &&
               fabsf(cur_bottom_x - prev_bottom_x) <= (float)k_floor_horiz_dy_thresh)
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_no_lock_vertical_af3_land =
              (!escapeair_locked && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_idx >= 0 &&
               !hit_line_is_platform &&
               // Sustained EscapeAir without CollData_X130_Locked should not inherit the previous
               // ECB approximation. Keep the same vertical-only early-anim gap airborne and let the
               // following EscapeAir_Coll frame own the landing if replay still reaches the floor.
               // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
               // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
               batch->state.action_frame[idx] == 3 &&
               fabsf(cur_bottom_x - prev_bottom_x) <= (float)k_floor_horiz_dy_thresh)
                  ? 1u
                  : 0u;
          const uint8_t suppress_seeded_escapeair_first_locked_land =
              // Replay-seeded sustained EscapeAir row whose prefix-visible previous action is still
              // the pre-EscapeAir source. The true CollData current/desired ECB has just crossed the
              // ChangeMotionState boundary. Restrict this to platform/slope floor contacts where the
              // hidden floor/ECB lifetime can differ from a simple hard-floor sweep; ordinary hard
              // floors still land through ft_80082C74 on the same callback pass.
              // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
              // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
              (escapeair_locked && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.action_frame[idx] <= 4 &&
               (hit_line_has_platform_transform || hit_line_is_slope))
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_locked_desired_bottom_above_floor_land =
              // CollData_X130_Locked preserves desired_ecb.bottom during EscapeAir_Coll. The lite
              // sim's zero-bottom lock is only an approximation for rows where source actually uses
              // the root/desired-zero handoff; if the seeded desired bottom remains above the
              // accepted floor, mpColl_80044628_Floor has not produced the source floor hit yet.
              // Keep this scoped to sustained EscapeAir lock rows so same-frame Jump/KneeBend and
              // explicit JumpAerial zero-bottom owners stay on their own callback paths.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_LoadECB_inline,mpColl_80044628_Floor}
              (escapeair_locked && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               batch->state.action_frame[idx] <= 4 && !hit_line_is_platform &&
               !hit_line_has_platform_transform && !escapeair_early_ledge_root_floor_owner &&
               batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
               batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u &&
               batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
               hit_line_idx >= 0 &&
               (cur_bottom_y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) >
                   (iy + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t hit_line_is_sloped_ledge =
              floor_line_is_generated_sloped_ledge(batch, bi, g, hit_line_idx);
          const uint8_t suppress_kneebend_escapeair_slope_entry_land =
              // Fresh KneeBend -> EscapeAir can land on ordinary floor/platform callback rows, but
              // generated sloped ledge floors still route through the edge/ledge suppression side of
              // ft_80082C74. Do not let the zero-bottom root approximation convert that first sloped
              // ledge crossing into LandingFallSpecial.
              // data/stages/bin/*.bin::MSLSTG01 is_ledge + generated sloped floor geometry
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
              (escapeair_locked && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
               ((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                 batch->state.action_frame[idx] <= 2) ||
                (ecb_lock_timer_seed != 0u && !was_grounded &&
                 batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
                 batch->state.seed_prev_action_frame[idx] >= 4)) &&
               hit_line_is_sloped_ledge)
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_jump_entry_platform_from_below =
              // JumpAerialF -> EscapeAir entry can carry a locked previous bottom into a carried
              // ledge/platform floor line before the source EscapeAir_Coll callback publishes
              // stable floor ownership. Keep transformed platforms on their explicit positive locks
              // and JumpAerialB rows on the ordinary callback path seen in replay-real locks.
              // `action_frame <= 3` is the first EscapeAir_Coll callback window after
              // `ftCo_JumpAerial_IASA` has just changed state, while
              // `seed_prev_action_frame <= 6` is still before the JumpAerial source callback has
              // published a stable static-platform floor. Both action families are table-backed by
              // `MSLMSO01`; later rows use the ordinary EscapeAir floor owner.
              // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_is_platform &&
               !hit_line_has_platform_transform &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
               batch->state.action_frame[idx] <= 3 && ecb_lock_active &&
               batch->state.seed_prev_action_frame[idx] <= 6 && prev_bottom_y <= iy &&
               cur_bottom_y <= iy)
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_late_jump_entry_platform_lifetime =
              // Later JumpAerial -> EscapeAir platform rows can still carry the pre-entry
              // CollData/floor lifetime into the first EscapeAir callback. Source
              // mpColl_80044628_Floor must see a bottom sweep before mpColl_80044838_Floor may
              // publish LandingFallSpecial; late from-below platform contacts stay airborne until a
              // later callback owns the platform floor.
              // The `seed_prev_action_frame 5..6` boundary is the late JumpAerial callback phase
              // immediately before EscapeAir entry in the retained replay-real locks; earlier
              // JumpAerial phases are covered by the fresh entry path above, and later rows have no
              // live locked-ECB source owner.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_is_platform &&
               !hit_line_has_platform_transform &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
               batch->state.action_frame[idx] <= 3 && ecb_lock_active &&
               batch->state.seed_prev_action_frame[idx] >= 5 &&
               batch->state.seed_prev_action_frame[idx] <= 6)
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_platform_root_snap_without_bottom_hit =
              // mpColl_800471F8 can snap the root only after mpColl_80044628_Floor accepts an ECB
              // bottom-floor hit. A platform projection whose current ECB bottom is still above the
              // platform is not a source floor callback result, even if root projection alone would
              // reach the line.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_is_platform &&
               cur_bottom_y > (iy + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t suppress_jumpaerial_escapeair_entry_land =
              (escapeair_fresh_jumpaerial_entry_lock && hit_line_is_platform_slope_or_ledge) ? 1u
                                                                                             : 0u;
          const uint8_t suppress_fallspecial_entry_af3_land =
              (is_common_fallspecial_action(action_id) &&
               batch->state.prev_action_frame[idx] == 3 && batch->state.speed_y_self[idx] < 0.0f)
                  ? 1u
                  : 0u;
          const uint8_t fallspecial_entered_from_specialhi_end =
              (is_common_fallspecial_action(action_id) &&
               is_spacie_specialhi_end_fallspecial_source(batch->state.seed_prev_action_id[idx]))
                  ? 1u
                  : 0u;
          const uint8_t suppress_fallspecial_platform_first_root_crossing =
              // FallSpecial_Coll routes through ft_80083090 -> mpColl_80047E14 with the
              // ftCo_80096CC8 platform callback. That callback accepts soft platforms unless the
              // callback-visible stick is below p_ftCommonData->x25C. Sustained FallSpecial also
              // needs the source bottom-sweep precondition before the first above-root ->
              // below-root static platform crossing can publish LandingFallSpecial; the just-entered
              // spacie SpecialHi end -> FallSpecial frame consumes distinct pre-entry CollData
              // lifetime and stays on the positive platform handoff.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
              //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
              //   ftFx_SpecialHiFall_Anim,ftFx_SpecialHiBound_Anim}
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
              (is_common_fallspecial_action(action_id) && hit_line_is_platform &&
               (platform_pass_input_below_raw_threshold(batch, idx, c) ||
                !fallspecial_entered_from_specialhi_end) &&
               batch->state.prev_pos_y[idx] > iy && batch->state.pos_y[idx] < iy)
                  ? 1u
                  : 0u;
          const uint8_t suppress_fallspecial_same_floor_early_root_crossing =
              // FallSpecial_Coll's early FD main-floor callback can cross the carried hard-floor root
              // while source still leaves the fighter airborne; adjacent floor/seam and non-FD
              // handoffs have separate callback-visible mpColl owners and still land.
              // data/stages/bin/grnla.bin::MSLSTG01 floor segment 1
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
              //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
              // refs/melee/src/melee/ft/ft_081B.c::ft_80083090
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
              (is_common_fallspecial_action(action_id) && hit_line_idx >= 0 &&
               !hit_line_is_platform && stage_id == (uint32_t)MSL_STAGE_FINAL_DESTINATION_LOCAL &&
               batch->state.ground_id[idx] == 1u &&
               g->lines[(size_t)hit_line_idx].segment_i == batch->state.ground_id[idx] &&
               batch->state.seed_prev_action_id[idx] == action_id &&
               batch->state.seed_prev_action_frame[idx] <= 5 &&
               batch->state.action_frame[idx] <= 6 && batch->state.speed_y_self[idx] < 0.0f &&
               batch->state.fall_fast[idx] != 0u && batch->state.prev_pos_y[idx] > iy &&
               batch->state.pos_y[idx] < iy)
                  ? 1u
                  : 0u;
          const MslCharParams* floor_cross_chp = msl_char_params(char_id);
          const uint8_t fall_ledge_floor_uses_expanded_collision_model =
              (floor_cross_chp != NULL && isfinite(floor_cross_chp->model_scaling) &&
               floor_cross_chp->model_scaling > 1.0f)
                  ? 1u
                  : 0u;
          const uint8_t suppress_fall_ledge_floor_first_root_crossing =
              // Ordinary Fall_Coll routes through ft_800831CC -> mpColl_80047E14(flags=6), whose
              // floor check consumes live CollData.ecb after mpColl_LoadECB_inline/interpolation.
              // This lite sim samples raw SSANIM ECB extents before the live collision-model scale
              // and CollData interpolation used by mpColl_LoadECB_inline. On enlarged collision
              // models, a first ledge-floor root crossing can therefore report a landing while the
              // live ECB still remains above the floor. Suppress only that expanded-model first
              // non-fastfall root-crossing frame and let the next below-floor Fall_Coll frame land.
              // Fastfall ledge crossings keep the ordinary Fall_Coll floor owner. Do not key
              // this on character id: Fox/Falco controls are distinguished by extracted
              // ftCo_DatAttrs::model_scaling in `data/characters/*.json`.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
              // refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale
              // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor}
              (fall_ledge_floor_uses_expanded_collision_model &&
               action_id == (uint16_t)MSL_ACT_FALL && prev_action_id == (uint16_t)MSL_ACT_FALL &&
               hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_ledge &&
               (prefer_line_idx < 0 || !g->lines[(size_t)prefer_line_idx].is_ledge) &&
               batch->state.fall_fast[idx] == 0u && batch->state.action_frame[idx] >= 2 &&
               batch->state.action_frame[idx] <= 3 && batch->state.speed_y_self[idx] < 0.0f &&
               prev_y > (iy + k_floor_y_bias) && batch->state.pos_y[idx] < iy)
                  ? 1u
                  : 0u;
          const uint8_t fall_no_floor_shallow_fastfall_contact =
              // Fall_Coll's no-floor-index path is a pure mpColl_80044628_Floor bottom sweep. A
              // shallow first contact without a persisted floor.index and before the movescript
              // allow-interrupt bit is live can remain airborne for one more callback; do not let
              // the lite root projection turn that shallow bottom graze into Landing. Rows with a
              // live floor.index stay on the explicit flags=6 projection owner above, and
              // allow-interrupt shallow Fall rows retain the ordinary floor handoff.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
              // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
              // refs/melee/src/melee/ft/types.h::Fighter::allow_interrupt (fp+0x2218:0)
              (action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] != 0u &&
               batch->state.ground_id[idx] == 0xFFFFu && ecb_lock_timer_seed == 0u &&
               batch->state.action_frame[idx] <= 4 && hit_line_idx >= 0 &&
               fabsf(batch->state.speed_air_x_self[idx]) > 0.5f &&
               (iy - cur_bottom_y) > k_floor_y_bias && (iy - cur_bottom_y) < 0.35f &&
               (batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES] & 0x80u) == 0u)
                  ? 1u
                  : 0u;
          const uint8_t suppress_damageflyroll_shallow_land =
              (damageflyroll_iasa_lockout && !damageflyroll_deep_side_penetration &&
               damageflyroll_root_proj_ready &&
               damageflyroll_root_proj_y_corr < damageflyroll_side_y_thresh)
                  ? 1u
                  : 0u;
          // DamageFlyRoll active-hitlag below-floor freeze:
          // - DamageFlyRoll keeps the victim airborne while hitlag is still frozen.
          // - Without a proven Damage_OnEveryHitlag stay-airborne floorhug owner above, do not let
          //   generic floor projection snap a below-floor DamageFlyRoll pose to the floor before
          //   ftCo_Damage_OnExitHitlag / DamageFlyRoll_Coll run.
          // Other active Damage/DamageFly rows keep the existing floorhug/projected-contact paths;
          // this is not a broad suppression of the damage collision family.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_OnExitHitlag,ftCo_Damage_Coll,
          //   ftCo_DamageFlyRoll_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{
          //   mpCollPrev,mpCheckFloor,mpColl_800477E0,mpColl_80046904}
          const uint8_t suppress_active_damage_hitlag_land =
              (batch->state.hitlag[idx] != 0u && is_damage_collision_landing_action(action_id))
                  ? 1u
                  : 0u;
          const uint8_t suppress_active_damage_hitlag_bottom_above_floor_land =
              // Active-hitlag Damage/DamageFly can move the root below the carried floor through
              // ftCo_Damage_OnEveryHitlag SDI before the collision callback runs. The generic sweep
              // below may see a root/floor projection, but mpColl_80044628_Floor must first accept
              // a current ECB-bottom floor hit before mpColl_80044948_Floor can publish the
              // stay-airborne floor projection. If the current bottom remains above the hit line,
              // keep the SDI-mutated root airborne for this frame.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
              (suppress_active_damage_hitlag_land &&
               batch->state.tilt_timer_y_frame_start[idx] >= c->sdi_tilt_max_frames &&
               damage_hitlag_floorhug_attempts_downward_sdi(batch, idx, c) != 0u &&
               hit_line_idx >= 0 && !hit_line_is_platform &&
               !g->lines[(size_t)hit_line_idx].is_ledge && cur_bottom_y > (iy + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t suppress_damageflyroll_below_floor_active_hitlag_land =
              (action_id == MSL_ACT_DAMAGE_FLY_ROLL && batch->state.hitlag[idx] != 0u &&
               batch->state.pos_y[idx] < 0.0f)
                  ? 1u
                  : 0u;
          const uint8_t suppress_damageair_attackair_entry_land =
              // Damage_IASA can enter AttackAir after hitstun ends in the same Fighter proc. The
              // entry frame should not immediately consume the DamageAir floor sweep into Landing;
              // vanilla publishes the new AttackAir row airborne, with the later AttackAir_Coll
              // path owning landing on a subsequent callback.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
              //   ftCo_AttackAir_Enter,ftCo_AttackAir_Coll}
              (is_attackair_action(action_id) &&
               (prev_action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_1 ||
                prev_action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_2 ||
                prev_action_id == (uint16_t)MSL_ACT_DAMAGE_AIR_3) &&
               batch->state.action_frame[idx] <= 1)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locomotion_attackair_entry_platform_land =
              // Fall IASA can enter AttackAir before Fighter_procMap, but the entry frame still
              // carries the pre-entry CollData/ECB callback lifetime. Platform contact on that same
              // callback must not be consumed by a generic floor sweep before the AttackAir_Coll
              // owner publishes its stable landing result on a later frame.
              // Keep this to platform/slope contacts and fresh entry frames; hard-floor and
              // sustained AttackAir landings remain owned by the normal ft_80082C74 path. Jump
              // family AttackAir entries have a distinct locked-ECB/platform owner and are not part
              // of this slice.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::{
              //   ftCo_AttackAir_EnterFromMsid,ftCo_AttackAir_Coll}
              // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
              (is_attackair_action(action_id) && batch->state.action_frame[idx] <= 1 &&
               hit_line_is_platform_or_slope &&
               (prev_action_id == (uint16_t)MSL_ACT_FALL ||
                prev_action_id == (uint16_t)MSL_ACT_FALL_AERIAL))
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_entry_locked_platform_land =
              // EscapeAir_Coll uses CollData's locked ECB snapshot through the early entry window.
              // When that snapshot carries a prior platform floor.index, mpColl can observe a
              // transformed-platform sweep before the source callback resolves LandingFallSpecial.
              // Keep those first locked/non-matching platform crossings airborne; sustained
              // same-platform crossings still use the floor handoff above.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80047E14}
              (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && batch->state.action_frame[idx] <= 4 &&
               hit_line_is_platform && hit_line_has_platform_transform &&
               (!escapeair_sustained_floor_handoff ||
                (hit_line_idx >= 0 &&
                 g->lines[(size_t)hit_line_idx].segment_i != batch->state.ground_id[idx])) &&
               (ecb_lock_active ||
                (prev_action_id == action_id &&
                 g->lines[(size_t)hit_line_idx].segment_i != batch->state.ground_id[idx])))
                  ? 1u
                  : 0u;
          const uint8_t carried_transformed_platform_skip =
              // Runtime source path for transformed-platform pass-through remains the explicit
              // CollData.floor_skip lane (`platform_floor_skip_segment_id`) plus callbacks that
              // pass `ftCo_80096CC8`. EscapeAir_Coll routes through `ft_80082C74` and does not use
              // that platform-pass callback, so do not keep the older down-held EscapeAir fallback:
              // same-frame JumpAerial -> EscapeAir platform contacts are admitted or rejected by
              // the pre-entry CollData ECB bottom sweep above.
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{
              //   ftCo_8009A184,ftCo_8009A228}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
              0u;
          float proj_x = 0.0f;
          float proj_y = 0.0f;
          stay_airborne_floor_projection_point(batch->state.pos_x[idx], batch->state.pos_y[idx],
                                               cur_bottom_x, cur_bottom_y, &proj_x, &proj_y);
          float y_corr = 0.0f;
          const int out_line_idx = msl_mplib_8004dd90_floor(batch, bi, g, hit_line_idx, proj_x,
                                                            proj_y, &y_corr, NULL, NULL);
          const uint16_t projected_segment_i =
              (out_line_idx >= 0) ? g->lines[(size_t)out_line_idx].segment_i : 0xFFFFu;
          const uint8_t projected_line_has_platform_transform =
              stage_collision_floor_line_has_platform_transform(stage_id, projected_segment_i);
          const uint8_t projected_line_has_height_platform_transform =
              stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                       projected_segment_i);
          const uint8_t attackair_transformed_platform_floor_skip_active =
              (skip_platform_segment_i != 0xFFFFu && first_phase_attackair_platform_ecb_owner &&
               prev_action_id == action_id &&
               stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                        skip_platform_segment_i))
                  ? 1u
                  : 0u;
          const float attackair_transformed_platform_edge_slack =
              fmaxf(fabsf(cur_ecb_points.left_rel_x), fabsf(cur_ecb_points.right_rel_x)) +
              k_floor_x_end_clamp;
          float attackair_transformed_platform_line_y = 0.0f;
          const uint8_t attackair_transformed_platform_line_valid =
              (hit_line_idx >= 0 &&
               floor_line_y_at_x_for_env(batch, bi, g, hit_line_idx, x,
                                         &attackair_transformed_platform_line_y))
                  ? 1u
                  : 0u;
          const uint8_t attackair_transformed_platform_edge_contact =
              (hit_line_idx >= 0 && hit_line_has_height_platform_transform &&
               floor_line_x_near_endpoint_for_env(batch, bi, g, hit_line_idx, cur_bottom_x,
                                                  attackair_transformed_platform_edge_slack))
                  ? 1u
                  : 0u;
          const uint8_t attackair_transformed_platform_root_edge_contact =
              (hit_line_idx >= 0 && hit_line_has_height_platform_transform &&
               floor_line_x_near_endpoint_for_env(batch, bi, g, hit_line_idx, x,
                                                  attackair_transformed_platform_edge_slack))
                  ? 1u
                  : 0u;
          const uint8_t projected_attackair_transformed_platform_edge_contact =
              (out_line_idx >= 0 && projected_line_has_height_platform_transform &&
               floor_line_x_near_endpoint_for_env(batch, bi, g, out_line_idx, cur_bottom_x,
                                                  attackair_transformed_platform_edge_slack))
                  ? 1u
                  : 0u;
          const uint8_t attackair_transformed_platform_downheld_pass_input =
              platform_pass_input_below_raw_threshold(batch, idx, c);
          const uint8_t attackair_transformed_platform_edge_owner_active =
              (attackair_transformed_platform_floor_skip_active ||
               attackair_transformed_platform_downheld_pass_input)
                  ? 1u
                  : 0u;
          const float attackair_transformed_platform_prev_below_depth =
              attackair_transformed_platform_line_y - prev_y;
          const uint8_t attackair_transformed_platform_shallow_edge_continuation =
              (attackair_transformed_platform_line_valid &&
               attackair_transformed_platform_root_edge_contact &&
               attackair_transformed_platform_prev_below_depth > k_floor_y_bias &&
               attackair_transformed_platform_prev_below_depth <= (2.0f * k_ecb_vertical_unit))
                  ? 1u
                  : 0u;
          const uint8_t suppress_attackair_transformed_platform_root_below_land =
              // AttackAir_Coll enters ft_80082C74 -> mpColl_800471F8 with the live CollData ECB
              // extents. FoD height-transform platform contacts at an endpoint can be accepted by
              // the ECB-bottom sweep while the callback-local root handoff is still owned by the
              // platform pass/floor-skip path. Keep that endpoint-contact frame airborne and
              // publish the explicit floor_skip lane; inboard transformed-platform root contacts
              // retain the ordinary ft_80082C74 LandingAir* path.
              // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
              // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              (((attackair_transformed_platform_edge_owner_active &&
                 (attackair_transformed_platform_edge_contact ||
                  attackair_transformed_platform_root_edge_contact)) ||
                attackair_transformed_platform_shallow_edge_continuation) &&
               (hit_line_is_platform || hit_line_has_height_platform_transform) &&
               first_phase_attackair_platform_ecb_owner && prev_action_id == action_id &&
               move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                        batch->state.anim_frame_f32[idx]) &&
               attackair_transformed_platform_line_valid &&
               y < attackair_transformed_platform_line_y)
                  ? 1u
                  : 0u;
          const uint8_t suppress_attackair_transformed_platform_floor_skip_first_crossing_land =
              // Continuation of the same FoD AttackAirN/Lw platform-pass owner after the initial
              // down-held platform skip is live. The first floor candidate after that moving-platform
              // pass can be a connected hard-floor edge rather than the skipped platform itself;
              // source still keeps the first root crossing airborne and publishes LandingAir* on the
              // following already-below-floor callback. This requires a real carried floor-skip lane
              // and does not apply to fresh AttackAir contacts or static support platforms.
              // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
              (attackair_transformed_platform_floor_skip_active &&
               floor_line_is_runtime_fighter_solid(g, stage_id, hit_line_idx) &&
               (!floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, x) ||
                floor_line_is_generated_stage_slope(batch, bi, g, hit_line_idx)) &&
               attackair_transformed_platform_line_valid &&
               prev_y > (attackair_transformed_platform_line_y + k_floor_y_bias) &&
               y < attackair_transformed_platform_line_y && batch->state.speed_y_self[idx] < 0.0f)
                  ? 1u
                  : 0u;
          const uint8_t suppress_attackair_offspan_hard_floor_edge_land =
              // Raw floor-sweep variant of the late-hit AttackAir off-span hard-floor edge owner.
              // mpLib endpoint extension can expose an adjacent hard floor before the source
              // AttackAir_Coll handoff has a strict in-span floor result.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
              (hit_line_idx >= 0 &&
               floor_line_is_runtime_fighter_solid(g, stage_id, hit_line_idx) &&
               !hit_line_has_height_platform_transform && shallow_attackair_platform_ecb_owner &&
               prev_action_id == action_id &&
               move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                        batch->state.anim_frame_f32[idx]) &&
               !floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, x) &&
               attackair_transformed_platform_line_valid &&
               prev_y > (attackair_transformed_platform_line_y + k_floor_y_bias) &&
               y < attackair_transformed_platform_line_y && batch->state.speed_y_self[idx] < 0.0f)
                  ? 1u
                  : 0u;
          const uint8_t suppress_downheld_transformed_platform_land =
              // Soft-platform pass-through usually belongs to callbacks that pass ftCo_80096CC8.
              // AttackAir_Coll does not call mpUpdateFloorSkip; its retained FoD platform boundary
              // is the shallow ECB-only owner handled by the raw/projection/final guards below.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
              ((hit_line_is_platform && hit_line_has_platform_transform && c != NULL &&
                action_uses_ftco_80096cc8_floor_callback(action_id) &&
                (stick_i8_to_unit(batch->state.input_main_y[idx]) <=
                     c->platform_air_land_stick_y_threshold ||
                 stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                     c->platform_air_land_stick_y_threshold)) ||
               (projected_line_has_platform_transform &&
                action_id == (uint16_t)MSL_ACT_FALL_SPECIAL_B &&
                !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx])) ||
               suppress_attackair_transformed_platform_root_below_land ||
               suppress_attackair_transformed_platform_floor_skip_first_crossing_land ||
               suppress_attackair_offspan_hard_floor_edge_land || carried_transformed_platform_skip)
                  ? 1u
                  : 0u;
          const float transformed_platform_bottom_penetration = iy - cur_bottom_y;
          const uint8_t damage_transformed_platform_offspan_contact =
              // mpCheckFloor can report a transformed-platform endpoint contact from the ECB bottom
              // sweep, but source floor handoff still requires the callback-local root to be in the
              // live floor span before ftCo_80090184 may publish Passive/DownBound.
              // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              (((hit_line_has_platform_transform &&
                 !floor_x_within_line_segment_strict(batch, bi, g, hit_line_idx, x)) ||
                (projected_line_has_platform_transform && out_line_idx >= 0 &&
                 !floor_x_within_line_segment_strict(batch, bi, g, out_line_idx, x))) &&
               is_damage_collision_landing_action(action_id) &&
               batch->state.speed_y_attack[idx] > 0.0f && batch->state.hitstun[idx] != 0u)
                  ? 1u
                  : 0u;
          const MslCharParams* damage_floor_chp = msl_char_params(char_id);
          const float damage_height_platform_edge_slack =
              (damage_floor_chp != NULL && isfinite(damage_floor_chp->ledge_snap_height))
                  ? (damage_floor_chp->ledge_snap_height * batch->state.fighter_scale_y[idx])
                  : k_ecb_vertical_unit;
          const uint8_t hit_line_height_platform_state_trusted =
              (hit_line_has_height_platform_transform &&
               stage_collision_floor_line_height_platform_state_is_source_trusted(
                   batch, bi, g->lines[(size_t)hit_line_idx].segment_i))
                  ? 1u
                  : 0u;
          const uint8_t projected_line_height_platform_state_trusted =
              (projected_line_has_height_platform_transform &&
               stage_collision_floor_line_height_platform_state_is_source_trusted(
                   batch, bi, projected_segment_i))
                  ? 1u
                  : 0u;
          const uint8_t damage_height_platform_live_bottom_crossing =
              ((hit_line_height_platform_state_trusted ||
                projected_line_height_platform_state_trusted) &&
               prev_bottom_y > (iy + k_floor_y_bias) && cur_bottom_y <= (iy + k_floor_y_bias))
                  ? 1u
                  : 0u;
          const uint8_t damage_height_platform_pending_owner =
              // FoD height-transform platform DamageFly endpoint contacts still need the live
              // grIzumi platform/CollData substep owner. `ft_80081DD4` sets
              // coll->ledge_snap_height before calling `mpColl_800473CC`; use the extracted
              // character ledge-snap height as the endpoint bound instead of a replay-local x
              // window. Do not suppress first live ECB-bottom crossings: source `mpCheckFloor`
              // accepts horizontal floors exactly when the bottom segment crosses from above to
              // below the line.
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
              // data/characters/{fox,falco}.json::ledge_snap_height
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
              // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80044628_Floor}
              // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLineIntersectionH}
              (((hit_line_has_height_platform_transform &&
                 floor_line_x_near_endpoint_for_env(batch, bi, g, hit_line_idx, x,
                                                    damage_height_platform_edge_slack)) ||
                (projected_line_has_height_platform_transform && out_line_idx >= 0 &&
                 floor_line_x_near_endpoint_for_env(batch, bi, g, out_line_idx, x,
                                                    damage_height_platform_edge_slack))) &&
               !damage_height_platform_live_bottom_crossing &&
               is_damage_collision_landing_action(action_id) &&
               batch->state.speed_y_attack[idx] > 0.0f && batch->state.hitstun[idx] != 0u)
                  ? 1u
                  : 0u;
          const uint8_t damage_terminal_height_platform_stale_floor =
              // Late DamageFly rows can carry a stale hard-floor CollData.floor.index while a FoD
              // height-transform platform sits between the live root and the carried floor. Source
              // `mpColl_80044628_Floor` still requires the live ECB-bottom floor precondition for
              // the transformed line before `mpColl_80044838_Floor` may publish the floor handoff.
              // Keep the guard inside the terminal hitstun window and outside already-carried
              // platform floors so ordinary mid-hitstun platform landings remain live.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
              //   ftCo_DamageFly_Anim,ftCo_DamageFly_Coll}
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              ((hit_line_has_height_platform_transform ||
                projected_line_has_height_platform_transform) &&
               is_damage_fly_collision_action(action_id) && batch->state.hitstun[idx] <= 4u &&
               !stage_collision_floor_line_is_platform(stage_id, batch->state.ground_id[idx]) &&
               transformed_platform_bottom_penetration > (0.5f * k_ecb_vertical_unit))
                  ? 1u
                  : 0u;
          const uint8_t suppress_damage_transformed_platform_ecb_only_land =
              // Damage/DamageFly platform contact consumes the same live CollData ECB sweep as hard
              // floors. Positive attack-Y is not a pass-through predicate by itself; suppress only
              // source-shaped platform contacts where the CollData ECB precondition is not met:
              // endpoint/off-span transformed-platform contacts, or the terminal stale
              // hard-floor/height-platform owner. In-span static-y FoD support lines are ordinary
              // platform floor contacts for the DamageFly tech/DownBound ladder.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_80090184
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c::ftCo_800986B0
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpCheckFloor}
              (damage_transformed_platform_offspan_contact ||
               damage_height_platform_pending_owner || damage_terminal_height_platform_stale_floor)
                  ? 1u
                  : 0u;
          const uint8_t suppress_attackair_transformed_platform_ecb_only_land =
              // AttackAirN/Lw's generated submotion rows use AttackAir_Coll
              // (ft_80082C74 -> mpColl_800471F8). That source path must first accept an ECB-bottom
              // hit in mpColl_80044628_Floor before mpColl_80044838_Floor(ignore_bottom=true) can
              // publish the LandingAir* root snap. On FoD height-transform platform endpoints,
              // sustained AttackAirN/Lw can expose a shallow transformed-platform ECB-bottom
              // crossing before the source callback has reached its stable LandingAir* handoff.
              // Keep only that endpoint/submotion boundary airborne; deeper bottom penetration,
              // inboard transformed-platform contacts, static platform/support contacts, and other
              // AttackAir submotions retain the normal AttackAir_Coll landing path.
              // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
              // refs/melee/src/melee/mp/mpcoll.c::{
              //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
              (((attackair_transformed_platform_edge_owner_active &&
                 (attackair_transformed_platform_edge_contact ||
                  attackair_transformed_platform_root_edge_contact ||
                  projected_attackair_transformed_platform_edge_contact)) ||
                attackair_transformed_platform_shallow_edge_continuation) &&
               first_phase_attackair_platform_ecb_owner && prev_action_id == action_id && y < iy &&
               move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                        batch->state.anim_frame_f32[idx]) &&
               transformed_platform_bottom_penetration > k_floor_y_bias &&
               (attackair_transformed_platform_shallow_edge_continuation ||
                transformed_platform_bottom_penetration <= k_ecb_vertical_unit))
                  ? 1u
                  : 0u;
          if (suppress_locked_ledge_land || suppress_locked_off_end_platform_land ||
              suppress_locked_seed6_platform_land || suppress_locked_vertical_af3_land ||
              suppress_escapeair_no_lock_vertical_af3_land ||
              suppress_seeded_escapeair_first_locked_land || suppress_fallspecial_entry_af3_land ||
              suppress_escapeair_locked_desired_bottom_above_floor_land ||
              suppress_kneebend_escapeair_slope_entry_land ||
              suppress_fallspecial_platform_first_root_crossing ||
              suppress_fallspecial_same_floor_early_root_crossing ||
              suppress_escapeair_late_jump_entry_platform_lifetime ||
              suppress_escapeair_platform_root_snap_without_bottom_hit ||
              suppress_escapeair_jump_entry_platform_from_below ||
              suppress_jumpaerial_escapeair_entry_land ||
              suppress_fall_ledge_floor_first_root_crossing ||
              fall_no_floor_shallow_fastfall_contact || escapeair_sustained_floorhug_airborne ||
              escapeair_fresh_horizontal_floorhug_airborne || specialhi_bound_entry_airborne ||
              suppress_damageflyroll_shallow_land || suppress_damageair_attackair_entry_land ||
              suppress_locomotion_attackair_entry_platform_land ||
              suppress_escapeair_entry_locked_platform_land ||
              suppress_downheld_transformed_platform_land ||
              suppress_damage_transformed_platform_ecb_only_land ||
              suppress_attackair_transformed_platform_ecb_only_land) {
            if (suppress_attackair_transformed_platform_root_below_land ||
                suppress_attackair_transformed_platform_ecb_only_land) {
              publish_attackair_transformed_platform_floor_skip_from_sweep(
                  batch, idx, bi, g, stage_id, hit_line_idx, x, prev_y, y);
            } else if (suppress_downheld_transformed_platform_land && hit_line_idx >= 0 &&
                       hit_line_has_height_platform_transform &&
                       action_uses_ftco_80096cc8_floor_callback(action_id) &&
                       batch->state.floor_skip_segment_id != NULL) {
              // Common-air platform pass-through owner: ftCo_80096CC8 rejects transformed
              // platform floors while the callback-visible stick is below x25C. Preserve the
              // rejected FoD floor.index as CollData.floor_skip so the next released callback frame
              // still rejects the same moving platform before the source landing handoff can
              // publish.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
              // refs/melee/src/melee/ft/ft_081B.c::{ft_800831CC,ft_800835B0}
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
              // data/stages/bin/griz.bin::MSLSTG01 platform_transforms
              batch->state.floor_skip_segment_id[idx] = g->lines[(size_t)hit_line_idx].segment_i;
            } else if (suppress_attackair_transformed_platform_floor_skip_first_crossing_land &&
                       batch->state.floor_skip_segment_id != NULL) {
              batch->state.floor_skip_segment_id[idx] = 0xFFFFu;
            }
            floor_write_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id, anim,
                                               ecb_frame, was_grounded);
          } else {
            if (out_line_idx >= 0) {
              const uint16_t resolved_segment_i = g->lines[(size_t)out_line_idx].segment_i;
              const uint8_t resolved_line_has_platform_transform =
                  stage_collision_floor_line_has_platform_transform(stage_id, resolved_segment_i);
              const uint8_t resolved_line_is_ledge =
                  g->lines[(size_t)out_line_idx].is_ledge ? 1u : 0u;
              const uint8_t resolved_line_is_platform =
                  g->lines[(size_t)out_line_idx].is_platform ? 1u : 0u;
              const uint8_t resolved_ledge_x_in_bounds =
                  (resolved_line_is_ledge &&
                   floor_x_within_line_bounds(batch, bi, g, out_line_idx, batch->state.pos_x[idx]))
                      ? 1u
                      : 0u;
              const uint8_t resolved_floor_x_in_strict_segment =
                  floor_x_within_line_segment_strict(batch, bi, g, out_line_idx, cur_bottom_x);
              const uint8_t suppress_projected_escapeair_off_end_ledge_land =
                  // Same off-end ledge remap boundary as the deep projection path. The generic
                  // floor sweep can hit/remap an adjacent ledge through mpLib endpoint extension,
                  // but early locked EscapeAir should not ground when the root is outside that
                  // accepted ledge floor segment.
                  // refs/melee/src/melee/mp/mplib.c::{
                  //   mpLib_8004DD90_Floor,mpLib_8004ED5C}
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 3 && resolved_line_is_ledge &&
                   !resolved_ledge_x_in_bounds)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_escapeair_off_end_platform_land =
                  // Same endpoint-extension boundary for soft platforms: a projected line outside
                  // the strict generated platform span is not the current EscapeAir floor callback
                  // handoff, even though mpLib can clamp the query to the platform endpoint.
                  // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 3 && resolved_line_is_platform &&
                   !resolved_floor_x_in_strict_segment)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_escapeair_transformed_platform_land =
                  // EscapeAir transformed-platform floor contact must be evaluated after the
                  // mpLib projection/remap result. FoD can sweep one transformed platform record and
                  // resolve to another; using only the swept line over-admits first-entry air-dodge
                  // rows onto a neighboring platform.
                  //
                  // Fresh Jump/JumpAerial -> EscapeAir uses the same callback-local owner as the
                  // final remap guard below: IASA has entered EscapeAir before Fighter_procMap, and
                  // the EscapeAir_Coll pass can consume the transformed-platform projection result.
                  // Sustained EscapeAir over the same carried transformed floor is also a normal
                  // callback result once `escapeair_sustained_floor_handoff` has accepted it; this
                  // guard only rejects remaps to a distinct transformed platform/floor owner.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
                  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 4 && resolved_line_has_platform_transform &&
                   (!escapeair_sustained_floor_handoff ||
                    resolved_segment_i != batch->state.ground_id[idx]) &&
                   (ecb_lock_active || (prev_action_id == action_id &&
                                        resolved_segment_i != batch->state.ground_id[idx])) &&
                   batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_F &&
                   batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_B &&
                   batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
                   batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_B)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_specialhi_transformed_platform_land =
                  // Fox/Falco up-special collision owns bound/landing through its special callbacks,
                  // not the generic common-air transformed-platform sweep on the locked entry/fall
                  // rows. Keep only these transformed-platform projections airborne; ordinary hard
                  // floor/wall handling stays on the existing special-hi path.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialHi_Coll,ftFx_SpecialHiFall_Coll,ftFx_SpecialHiBound_Coll}
                  (resolved_line_has_platform_transform &&
                   ((action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI && ecb_lock_active) ||
                    (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL &&
                     resolved_segment_i != batch->state.ground_id[idx])))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_specialhi_understage_hard_floor_clip =
                  // Same hard-floor guard as the direct sweep path: a SpecialHi projection
                  // whose previous root is already below the accepted hard floor by more than the
                  // live ECB neighborhood is recovering from a missed underside collision, not source
                  // `ft_CheckGroundAndLedge` ownership.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
                  (specialhi_understage_floor_clip_action(action_id) &&
                   !stage_collision_floor_line_is_platform(stage_id, resolved_segment_i) &&
                   prev_y <
                       (proj_y - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                        k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_specialhi_from_below_hard_floor_clip =
                  // Projection/remap variant of the same SpecialHi floor-admission precondition:
                  // root projection cannot turn a floor candidate that both callback ECB bottoms
                  // already start below into a rebound/landing contact.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
                  (specialhi_floor_candidate_starts_below_source_floor(
                       action_id, prev_y, prev_bottom_y, cur_bottom_y, proj_y + y_corr,
                       batch->state.speed_y_self[idx]) &&
                   !stage_collision_floor_line_is_platform(stage_id, resolved_segment_i))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_specialairhi_floor_angle_land =
                  // Projection variant of SpecialAirHi_Coll's floor-angle branch: shallow
                  // floor.normal/self_vel contacts continue the launch and only update the
                  // collision pose, while steeper hard-floor contacts remain publishable for
                  // SpecialHiBound.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound}
                  // data/characters/{fox,falco}.json::firefox_bound_angle_degrees
                  (specialairhi_floor_contact_angle_continues_launch(
                       action_id, char_id, floor_nx, floor_ny, batch->state.speed_air_x_self[idx],
                       batch->state.speed_y_self[idx]) &&
                   !stage_collision_floor_line_is_platform(stage_id, resolved_segment_i))
                      ? 1u
                      : 0u;
              const uint8_t resolved_line_has_height_platform_transform =
                  stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                           resolved_segment_i);
              const float projected_contact_y = proj_y + y_corr;
              const float projected_transformed_platform_bottom_penetration =
                  projected_contact_y - cur_bottom_y;
              const uint8_t suppress_projected_attackair_transformed_platform_ecb_only_land =
                  // Same source boundary as the raw sweep suppressor above, after mpLib projection
                  // has remapped the candidate floor to the actual FoD height-transform segment.
                  // The retained owner is the generated AttackAirN/Hi/Lw submotion row, not the
                  // whole AttackAir callback class: first shallow transformed-platform contacts stay
                  // airborne during the extracted first HitCapsule create->clear phase until the
                  // callback reaches its stable floor handoff. Later hitbox phases and deeper/no-owner
                  // root crossings keep the ordinary ft_80082C74 floor handoff.
                  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
                  // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (((attackair_transformed_platform_edge_owner_active &&
                     projected_transformed_platform_bottom_penetration > k_floor_y_bias &&
                     projected_transformed_platform_bottom_penetration <= k_ecb_vertical_unit) ||
                    (projected_contact_y - prev_y > k_floor_y_bias &&
                     projected_contact_y - prev_y <= (2.0f * k_ecb_vertical_unit))) &&
                   resolved_line_has_height_platform_transform &&
                   shallow_attackair_platform_ecb_owner && prev_action_id == action_id &&
                   move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                            batch->state.anim_frame_f32[idx]) &&
                   y < projected_contact_y)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_attackair_transformed_platform_floor_skip_land =
                  // Projection/remap variant of the carried floor-skip shallow-root guard above.
                  // Once the FoD transformed-platform skip owner is live, the next candidate floor
                  // can be a connected hard-floor edge; source still waits until the following
                  // already-below-floor callback before snapping to LandingAir*.
                  (attackair_transformed_platform_floor_skip_active &&
                   floor_line_is_runtime_fighter_solid(g, stage_id, out_line_idx) &&
                   (!floor_x_within_line_segment_strict(batch, bi, g, out_line_idx, x) ||
                    floor_line_is_generated_stage_slope(batch, bi, g, out_line_idx)) &&
                   prev_y > (projected_contact_y + k_floor_y_bias) && y < projected_contact_y &&
                   batch->state.speed_y_self[idx] < 0.0f)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_attackair_offspan_hard_floor_edge_land =
                  // Projection/remap variant of the raw off-span hard-floor edge guard above.
                  (out_line_idx >= 0 &&
                   floor_line_is_runtime_fighter_solid(g, stage_id, out_line_idx) &&
                   !resolved_line_has_height_platform_transform &&
                   shallow_attackair_platform_ecb_owner && prev_action_id == action_id &&
                   move_tables_attackair_second_create_hitbox_phase(
                       char_id, action_id, batch->state.anim_frame_f32[idx]) &&
                   !floor_x_within_line_segment_strict(batch, bi, g, out_line_idx, x) &&
                   prev_y > (projected_contact_y + k_floor_y_bias) && y < projected_contact_y &&
                   batch->state.speed_y_self[idx] < 0.0f)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_attackair_transformed_platform_below_land =
                  // Projection/remap variant of the carried FoD floor-skip owner. While the live
                  // skip is still active, both callback roots can be below the transformed platform
                  // before source permits LandingAir* publication. Rows with no carried skip retain
                  // the ordinary ft_80082C74 handoff through the projected floor result.
                  // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (resolved_line_has_height_platform_transform &&
                   action_uses_late_attackair_platform_ecb_owner(char_id, action_id) &&
                   prev_action_id == action_id &&
                   move_tables_attackair_last_create_hitbox_phase(
                       char_id, action_id, batch->state.anim_frame_f32[idx]) &&
                   prev_y < projected_contact_y && y < projected_contact_y &&
                   batch->state.speed_y_self[idx] <= k_floor_horiz_dy_thresh)
                      ? 1u
                      : 0u;
              const float escapeair_entry_bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
              const uint8_t suppress_projected_escapeair_platform_root_snap_without_bottom_hit =
                  // Projection/remap path variant of the same mpColl floor precondition: if the
                  // resolved platform is above the current ECB bottom, mpColl_80044628_Floor has
                  // not accepted the bottom hit needed before root snapping.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && resolved_line_is_platform &&
                   cur_bottom_y > (projected_contact_y + k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_fallspecial_first_sustained_land =
                  // Projection/remap variant of the Yoshi main-floor first sustained FallSpecial
                  // callback guard. Keep this on the data-backed stage segment owner so the other
                  // legal-stage FallSpecial floor handoffs continue through the normal callback path.
                  fallspecial_yoshi_main_floor_first_sustained_airborne_owner(batch, idx, stage_id);
              const uint8_t suppress_projected_escapeair_missing_bottom_owner_land =
                  // Projection/remap variant of the locked EscapeAir missing-bottom-owner guard.
                  // If the preserved desired-bottom lane is absent and the callback-local previous
                  // root is already below the accepted floor, the generic root projection is ahead
                  // of the source `mpColl_80044628_Floor` precondition.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{
                  //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed != 0u &&
                   batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 0u &&
                   batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias &&
                   batch->state.floor_sweep_prev_pos_y[idx] < iy)
                      ? 1u
                      : 0u;
              const uint8_t suppress_projected_jumpaerial_escapeair_shallow_ledge_land =
                  // Fresh JumpAerial -> EscapeAir ledge remap depth:
                  // The generic sweep can hit/remap the carried center floor to an adjacent ledge,
                  // but source only reaches mpColl_80044838_Floor after mpCheckFloor has a bottom
                  // contact deep enough for the entered EscapeAir ECB. Shallow ledge remaps remain
                  // airborne; deeper rows still consume LandingFallSpecial through the normal
                  // EscapeAir_Coll floor callback.
                  // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 3 && resolved_line_is_ledge &&
                   prefer_line_idx >= 0 && !g->lines[(size_t)prefer_line_idx].is_ledge &&
                   resolved_segment_i != seed_ground_id &&
                   (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                    batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
                   batch->state.seed_prev_action_frame[idx] <= 4 && y_corr >= 0.0f &&
                   y_corr < (escapeair_entry_bottom_rel0 - k_floor_y_bias))
                      ? 1u
                      : 0u;
              const uint8_t escapeair_low_floor_raw_hit_over_stale_platform_remap =
                  // The raw mpCheckFloor sweep can hit Yoshi's low ledge floor while the carried
                  // CollData.floor.index still names the side platform. Do not let the projection
                  // helper replace that source floor hit with an elevated transformed-platform snap;
                  // consume the raw floor hit and keep the low hard-floor/ledge placement.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
                  // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLib_8004DD90_Floor}
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 4 && !hit_line_is_platform &&
                   resolved_line_has_platform_transform &&
                   stage_collision_floor_line_is_platform(stage_id, resolved_segment_i) &&
                   iy <= k_floor_y_bias && (proj_y + y_corr) > k_floor_y_bias)
                      ? 1u
                      : 0u;
              if (escapeair_low_floor_raw_hit_over_stale_platform_remap) {
                batch->state.pos_x[idx] += (ix - cur_bottom_x);
                batch->state.pos_y[idx] += (iy - cur_bottom_y) + k_floor_y_bias;
                on_ground = 1;
                ground_id = g->lines[(size_t)hit_line_idx].segment_i;
                contact_x = ix;
                contact_y = iy;
              } else if (suppress_projected_escapeair_transformed_platform_land ||
                         suppress_projected_escapeair_off_end_ledge_land ||
                         suppress_projected_escapeair_off_end_platform_land ||
                         suppress_projected_escapeair_platform_root_snap_without_bottom_hit ||
                         suppress_projected_fallspecial_first_sustained_land ||
                         suppress_projected_escapeair_missing_bottom_owner_land ||
                         suppress_projected_jumpaerial_escapeair_shallow_ledge_land ||
                         suppress_projected_specialhi_transformed_platform_land ||
                         suppress_projected_specialhi_understage_hard_floor_clip ||
                         suppress_projected_specialhi_from_below_hard_floor_clip ||
                         suppress_projected_specialairhi_floor_angle_land ||
                         suppress_projected_attackair_transformed_platform_ecb_only_land ||
                         suppress_projected_attackair_transformed_platform_floor_skip_land ||
                         suppress_projected_attackair_offspan_hard_floor_edge_land ||
                         suppress_projected_attackair_transformed_platform_below_land ||
                         suppress_active_damage_hitlag_bottom_above_floor_land ||
                         damage_active_hitlag_root_below_bottom_above_floor_owner ||
                         damage_active_hitlag_downward_sdi_airborne_owner ||
                         suppress_damageflyroll_below_floor_active_hitlag_land) {
                if (suppress_projected_attackair_transformed_platform_ecb_only_land ||
                    suppress_projected_attackair_transformed_platform_floor_skip_land) {
                  if (suppress_projected_attackair_transformed_platform_ecb_only_land) {
                    publish_attackair_transformed_platform_floor_skip_from_sweep(
                        batch, idx, bi, g, stage_id, out_line_idx, x, prev_y, y);
                  } else if (batch->state.floor_skip_segment_id != NULL) {
                    batch->state.floor_skip_segment_id[idx] = 0xFFFFu;
                  }
                }
                floor_write_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id,
                                                   anim, ecb_frame, was_grounded);
              } else {
                batch->state.pos_y[idx] += y_corr;
                if (suppress_active_damage_hitlag_land) {
                  batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_FLOOR_MASK;
                  ground_id = resolved_segment_i;
                  contact_x = proj_x;
                  contact_y = proj_y + y_corr;
                } else {
                  on_ground = 1;
                  ground_id = resolved_segment_i;
                  contact_x = ix;
                  contact_y = iy;
                }
              }
            } else {
              // Sweep saw a floor segment, but projection failed (often an off-end / edge case).
              // Source mpColl_80044838_Floor handles this by snapping the ECB bottom to the floor
              // endpoint after mpColl_80044628_Floor has accepted the floor hit. This is shared by
              // AttackAir/common-air/damage floor callbacks that route through mpColl_80046904.
              // DownDamage_Coll's ft_80081DD4 floor path can also resolve from the root point when
              // post-hitlag common Damage Phys has moved the ECB bottom past the normal projection
              // point. Keep that root fallback bounded to the same downward-KB owner used by the
              // resting-contact path below; other actions keep edge-suppression diagnostics.
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
              //   ftCo_DownDamage_Phys,ftCo_DownDamage_Coll}
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              if (!suppress_active_damage_hitlag_land &&
                  !suppress_damageflyroll_below_floor_active_hitlag_land &&
                  msl_mpcoll_80044838_floor_edge_snap_from_bottom(
                      batch, bi, g, hit_line_idx, cur_bottom_x, cur_bottom_y, 0u, &ground_id,
                      &contact_x, &contact_y, &floor_nx, &floor_ny)) {
                batch->state.pos_x[idx] += (contact_x - cur_bottom_x);
                batch->state.pos_y[idx] += (contact_y - cur_bottom_y);
                on_ground = 1;
              } else if (downdamage_active_hitlag_resting_floor_contact) {
                float root_y_corr = 0.0f;
                const int root_line_idx = msl_mplib_8004dd90_floor(
                    batch, bi, g, prefer_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
                    &root_y_corr, &floor_nx, &floor_ny);
                const float downward_bound =
                    fabsf(batch->state.speed_y_attack[idx]) + k_ecb_vertical_unit;
                if (root_line_idx >= 0 && fabsf(root_y_corr) <= downward_bound) {
                  batch->state.pos_y[idx] += root_y_corr;
                  on_ground = 1;
                  ground_id = g->lines[(size_t)root_line_idx].segment_i;
                  contact_x = batch->state.pos_x[idx];
                  contact_y = batch->state.pos_y[idx];
                }
              }
              if (!on_ground) {
                // Propagate edge suppression bits so mpColl-shaped ledge-grab checks can apply the
                // `on_edge` gate deterministically.
                floor_write_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id,
                                                   anim, ecb_frame, was_grounded);
              }
            }
          }
          // Decomp anchor for this gate:
          // - Damage/DamageFly states own landing/DownBound decisions in their collision callbacks;
          //   keep generic "resting contact" floor projection out of hitstun frames.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
          // TODO(decomp-coll-coverage): once DamageFly* per-action Coll callback coverage is fully
          // modeled here, this gate may be relaxed/removed in favor of callback-owned landing flow.
        } else {
          if (prefer_line_idx >= 0 && !escapeair_sustained_floorhug_airborne &&
              !escapeair_fresh_horizontal_floorhug_airborne && !specialhi_bound_entry_airborne &&
              (batch->state.speed_y_self[idx] == 0.0f ||
               downdamage_active_hitlag_resting_floor_contact) &&
              batch->state.hitlag[idx] == 0 &&
              (batch->state.hitstun[idx] == 0 || downdamage_active_hitlag_resting_floor_contact)) {
            // Decomp: mpLib_8004DD90_Floor can resolve a resting contact even when no crossing
            // sweep is reported (e.g. vy==0 and the ECB bottom is already on the surface). Gate this
            // to "already on the surface" to avoid snapping to the floor from far below.
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            float y_corr = 0.0f;
            const int out_line_idx =
                msl_mplib_8004dd90_floor(batch, bi, g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                         &y_corr, &floor_nx, &floor_ny);
            if (out_line_idx >= 0 &&
                fabsf(y_corr - k_floor_y_bias) <= (float)k_floor_horiz_dy_thresh) {
              batch->state.pos_y[idx] += y_corr;
              on_ground = 1;
              ground_id = g->lines[(size_t)out_line_idx].segment_i;
              contact_x = cur_bottom_x;
              contact_y = cur_bottom_y + y_corr;
            } else if (downdamage_active_hitlag_resting_floor_contact &&
                       batch->state.speed_y_attack[idx] < 0.0f) {
              // DownDamage hitlag-exit can resolve from the root point when downward KB has already
              // pushed the fighter into the persisted floor line before the visible self velocity
              // resumes. Bound the downward snap by the attack-KB displacement plus the ECB vertical
              // unit used by the callback path; this keeps ordinary airborne DownDamage rows out of
              // the floor-contact owner.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Coll
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044838_Floor}
              float root_y_corr = 0.0f;
              const int root_line_idx = msl_mplib_8004dd90_floor(
                  batch, bi, g, prefer_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
                  &root_y_corr, &floor_nx, &floor_ny);
              const float downward_bound =
                  fabsf(batch->state.speed_y_attack[idx]) + k_ecb_vertical_unit;
              if (root_line_idx >= 0 && fabsf(root_y_corr) <= downward_bound) {
                batch->state.pos_y[idx] += root_y_corr;
                on_ground = 1;
                ground_id = g->lines[(size_t)root_line_idx].segment_i;
                contact_x = batch->state.pos_x[idx];
                contact_y = batch->state.pos_y[idx];
              }
            }
          }
        }
      }

      if (!on_ground && was_grounded && prefer_line_idx < 0 &&
          carried_floor_line_is_live_yoshi_shyguy_support(batch, bi, g, stage_id,
                                                          raw_current_floor_line_idx)) {
        // Current stage-object support lifetime:
        // mpColl's static floor graph should not select the Shy Guy support line as a new floor,
        // but source CollData can still carry its current floor/support owner while the stage object
        // controller is live. Preserve the grounded support for this callback pass; ordinary
        // fighter-solid floors and new raw-line contacts continue through normal mpColl paths.
        // refs/melee/src/melee/lb/types.h::CollData
        // refs/melee/src/melee/gr/grstory.c::grStory_801E3418
        // refs/melee/src/melee/it/items/itheiho.c
        on_ground = 1u;
        ground_id = seed_ground_id;
        contact_x = cur_bottom_x;
        contact_y = cur_bottom_y;
      }

      if (on_ground && was_grounded) {
        // Source ordering retries ceiling after a grounded floor collision in mpColl_8004ACE4.
        // Carry source-equivalent floor squeeze state into the retry so a pre-floor ceiling hit or
        // retry ceiling hit can trigger mpCollSqueezeVertical.
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004ACE4
        ordered_wall_ceil.hit_floor = 1u;
        ordered_wall_ceil.touching_floor = 1u;
        ordered_wall_ceil.squeeze_flags |= (uint8_t)MSL_MPCOLL_ORDERED_SQUEEZE_FLOOR;
        ordered_wall_ceil.y_after_floor = batch->state.pos_y[idx];
        MslEcbWorldPoints post_floor_ecb = {0};
        msl_ecb_world_points_sample(&post_floor_ecb, char_id, anim, ecb_frame_cur,
                                    facing_dir_for_ecb, batch->state.pos_x[idx],
                                    batch->state.pos_y[idx], lock_bottom_to_zero);
        (void)mpcoll_grounded_ceiling_ordered_retry(&mpcoll_ctx, &prev_ecb_points, &post_floor_ecb,
                                                    &ordered_wall_ceil);
        cur_ecb_points = ordered_wall_ceil.cur_ecb_after;
        cur_bottom_x = cur_ecb_points.bottom_x;
        cur_bottom_y = cur_ecb_points.bottom_y;
      }

      if (!on_ground && was_grounded && prefer_line_idx >= 0) {
        float substep_prev_x = 0.0f;
        float substep_prev_y = 0.0f;
        float substep_cur_x = 0.0f;
        float substep_cur_y = 0.0f;
        MslEcbWorldPoints substep_cur_ecb = {0};
        if (grounded_sideb_substep_floor_loss(batch, bi, g, char_id, action_id, prefer_line_idx,
                                              &prev_ecb_points, &cur_ecb_points, prev_x, prev_y, x,
                                              y, &substep_prev_x, &substep_prev_y, &substep_cur_x,
                                              &substep_cur_y, &substep_cur_ecb)) {
          batch->state.pos_x[idx] = substep_cur_x;
          batch->state.pos_y[idx] = substep_cur_y;
          cur_ecb_points = substep_cur_ecb;
          cur_bottom_x = substep_cur_ecb.bottom_x;
          cur_bottom_y = substep_cur_ecb.bottom_y;
          mpcoll_clear_callback_floor_result(&mpcoll_ctx, substep_prev_x, substep_prev_y,
                                             substep_cur_x, substep_cur_y);
        }
      }

      if (!on_ground && was_grounded && prefer_line_idx >= 0) {
        // Final source retry for grounded inline2-style collision callbacks:
        // mpColl_8004ACE4 calls mpColl_8004A908_Floor after the ordinary floor/edge/ceiling loop.
        // The retained slice below owns both `mpColl_8004A908_Floor` sweeps: first previous
        // bottom, then previous ECB side-midpoint Y, accepting only a floor that is different from
        // and not connected to the persisted CollData.floor.index.
        //
        // This is deliberately not row/action-gated: the retry is part of the shared CollData floor
        // substrate for grounded callbacks. Airborne callbacks continue through their existing
        // `mpColl_80046904` / `mpColl_80047E14` owners above.
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004ACE4,mpColl_8004A908_Floor}
        if (floor_4a908_retry(batch, idx, bi, g, stage_id, prefer_line_idx, prev_bottom_x,
                              prev_bottom_y, prev_side_mid_y, cur_bottom_x, cur_bottom_y,
                              skip_platform_segment_i, &ground_id, &contact_x, &contact_y,
                              &floor_nx, &floor_ny)) {
          // Retained source consumer: the grounded inline2 callback consumes the local
          // mpColl_8004A908_Floor retry result after ordinary floor/edge/ceiling passes fail.
          // Store the result in CollData-shaped callback scratch, then let the shared final
          // grounded writeback consume that scratch below.
          // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004ACE4,mpColl_8004A908_Floor}
          mpcoll_record_callback_floor_result(&mpcoll_ctx,
                                              (uint8_t)MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY,
                                              ground_id, contact_x, contact_y, floor_nx, floor_ny);
          on_ground = 1u;
        }
      }

      int escapeair_projection_line_idx =
          (prefer_line_idx >= 0)
              ? prefer_line_idx
              : stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
      if (escapeair_projection_line_idx < 0 && batch->state.ground_id[idx] != 0xFFFFu &&
          (size_t)batch->state.ground_id[idx] < g->line_count &&
          g->lines[(size_t)batch->state.ground_id[idx]].segment_i == batch->state.ground_id[idx]) {
        escapeair_projection_line_idx = (int)batch->state.ground_id[idx];
      }
      float escapeair_projection_line_y = 0.0f;
      const uint8_t escapeair_projection_line_y_valid =
          (escapeair_projection_line_idx >= 0 &&
           floor_line_y_at_x_for_env(batch, bi, g, escapeair_projection_line_idx, x,
                                     &escapeair_projection_line_y))
              ? 1u
              : 0u;
      const uint8_t escapeair_seeded_desired_bottom_nonplatform_crossing =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed > 1u &&
           escapeair_projection_line_idx >= 0 && escapeair_projection_line_y_valid != 0u &&
           batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
           batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 1u &&
           (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
           !stage_collision_floor_line_is_platform(
               stage_id, g->lines[(size_t)escapeair_projection_line_idx].segment_i) &&
           (prev_y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) >
               (escapeair_projection_line_y + k_floor_y_bias) &&
           (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) <=
               (escapeair_projection_line_y + k_floor_y_bias))
              ? 1u
              : 0u;
      const uint8_t escapeair_late_jumpaerial_desired_bottom_nonplatform_crossing =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed > 1u &&
           escapeair_projection_line_idx >= 0 && escapeair_projection_line_y_valid != 0u &&
           batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
           (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
           batch->state.seed_prev_action_frame[idx] == 3 && batch->state.action_frame[idx] <= 2 &&
           g->lines[(size_t)escapeair_projection_line_idx].segment_i ==
               batch->state.ground_id[idx] &&
           !stage_collision_floor_line_is_platform(
               stage_id, g->lines[(size_t)escapeair_projection_line_idx].segment_i) &&
           (prev_y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) >
               (escapeair_projection_line_y + k_floor_y_bias) &&
           (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) <=
               (escapeair_projection_line_y + k_floor_y_bias))
              ? 1u
              : 0u;
      if (!on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          ((batch->state.action_frame[idx] >= 3 &&
            (batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 2u ||
             batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 3u)) ||
           escapeair_seeded_desired_bottom_nonplatform_crossing ||
           escapeair_late_jumpaerial_desired_bottom_nonplatform_crossing) &&
          ecb_lock_timer_seed > 1u && escapeair_projection_line_idx >= 0 &&
          batch->state.speed_y_self[idx] <= 0.0f) {
        // EscapeAir_Coll's mpColl_800471F8 path can admit a connected hard-floor projection even
        // when the simplified bottom sweep has already moved below the line. Live JumpAerial ->
        // EscapeAir CollData owners (internal values 2/3) use root crossing; replay-seeded owner-1
        // rows and the late JumpAerial IASA handoff use the explicit desired-bottom lane and
        // require that desired bottom to cross the carried non-platform floor this frame. The late
        // handoff stays on the same source ground segment; adjacent floor traversal is owned by the
        // older source-specific EscapeAir slices above, not this first visible callback. Sustained
        // seeded EscapeAir continuations are not retained here: without a fresh source
        // desired-bottom producer they must not reuse a stale CollData slice as floor authority.
        // Already-below locked bottoms are still rejected by the publication guard below. The
        // accepted floor remains fighter-solid and non-platform; soft-platform pass-through is still
        // owned by the explicit locked desired-bottom slice above.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
        // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_800471F8
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
        float escapeair_y_corr = 0.0f;
        float escapeair_nx = 0.0f;
        float escapeair_ny = 1.0f;
        const int escapeair_line_idx =
            msl_mplib_8004dd90_floor(batch, bi, g, escapeair_projection_line_idx, x, y,
                                     &escapeair_y_corr, &escapeair_nx, &escapeair_ny);
        if (escapeair_line_idx >= 0 && escapeair_y_corr > 0.0f &&
            !stage_collision_floor_line_is_platform(
                stage_id, g->lines[(size_t)escapeair_line_idx].segment_i)) {
          ground_id = g->lines[(size_t)escapeair_line_idx].segment_i;
          contact_x = x;
          contact_y = y + escapeair_y_corr - k_floor_y_bias;
          floor_nx = escapeair_nx;
          floor_ny = escapeair_ny;
          on_ground = 1u;
        } else if (escapeair_projection_line_idx >= 0 &&
                   !stage_collision_floor_line_is_platform(
                       stage_id, g->lines[(size_t)escapeair_projection_line_idx].segment_i)) {
          if (escapeair_projection_line_y_valid != 0u &&
              (y <= escapeair_projection_line_y + k_floor_y_bias ||
               (y > escapeair_projection_line_y + k_floor_y_bias &&
                (y + batch->state.speed_y_self[idx]) <=
                    escapeair_projection_line_y + k_floor_y_bias))) {
            ground_id = g->lines[(size_t)escapeair_projection_line_idx].segment_i;
            contact_x = x;
            contact_y = escapeair_projection_line_y;
            floor_nx = 0.0f;
            floor_ny = 1.0f;
            on_ground = 1u;
          }
        }
      }

      if (on_ground) {
        const uint8_t resolved_line_has_platform_transform =
            stage_collision_floor_line_has_platform_transform(stage_id, ground_id);
        const uint8_t resolved_line_has_height_platform_transform =
            stage_collision_floor_line_has_height_platform_transform(stage_id, ground_id);
        uint8_t resolved_line_height_platform_same_step_contact = 0u;
        if (resolved_line_has_height_platform_transform &&
            stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi,
                                                                               ground_id)) {
          uint8_t platform_id = 0u;
          if (stage_collision_floor_line_platform_transform_id(stage_id, ground_id, &platform_id) &&
              platform_id < 2u &&
              (batch->state
                   .stage_fod_platform_height_source[(size_t)bi * 2u + (size_t)platform_id] &
               (uint8_t)MSL_FOD_PLATFORM_HEIGHT_SOURCE_SAME_STEP_CONTACT) != 0u) {
            resolved_line_height_platform_same_step_contact = 1u;
          }
        }
        const int final_ground_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
        const uint8_t suppress_escapeair_transformed_remap_land =
            // Final guard for FoD's transformed-platform remap path: several source-shaped
            // projection owners can set `on_ground` before the generic sweep suppression sees the
            // remapped line id. EscapeAir's locked entry frames should not convert to
            // LandingFallSpecial when the accepted transformed platform differs from the carried
            // CollData.floor.index.
            //
            // Fresh Jump/JumpAerial -> EscapeAir rows are the exception: IASA has changed the
            // motion state before Fighter_procMap, and the same EscapeAir_Coll callback has already
            // accepted the transformed-platform floor result. Do not discard that callback-local
            // result only because mpLib_8004DD90_Floor remapped the line across FoD's platform
            // graph. Scope the guard to elevated contacts: low Yoshi hard-floor remaps are ordinary
            // callback landings, and suppressing them produces rollout-only EscapeAir regressions.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && batch->state.action_frame[idx] <= 4 &&
             contact_y > k_floor_y_bias && resolved_line_has_platform_transform &&
             !escapeair_locked_platform_root_projection_hit &&
             !escapeair_no_lock_static_platform_sweep_hit &&
             ground_id != batch->state.ground_id[idx] &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_F &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_B &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_B)
                ? 1u
                : 0u;
        const uint8_t suppress_specialairhi_platform_land =
            // SpecialAirHi platform floor contact is consumed by ftCo_8009A134 inside
            // ftFox_SpecialHi_IsBound: platforms call mpUpdateFloorSkip and keep the launch
            // airborne instead of publishing a grounded/Bound handoff. The floor contact can still
            // snap cur_pos to the platform for that callback pass; the source-owned floor_skip then
            // excludes the same platform on following floor checks.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
            // refs/melee/src/melee/mp/mpcoll.c::{mpUpdateFloorSkip,mpColl_80044628_Floor}
            (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI &&
             stage_collision_floor_line_is_platform(stage_id, ground_id))
                ? 1u
                : 0u;
        const uint8_t suppress_specialhi_transformed_platform_land =
            // SpecialHiFall transformed-platform contacts remain owned by the Fox/Falco
            // up-special fall callback. Hard floors and same-floor followups retain the normal
            // result.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialHiFall_Coll,ftFx_SpecialHiBound_Coll}
            (resolved_line_has_platform_transform &&
             action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL &&
             ground_id != batch->state.ground_id[idx])
                ? 1u
                : 0u;
        const uint8_t suppress_specialhi_understage_hard_floor_land =
            // Final writeback guard for SpecialHi hard-floor clips from inside the stage. Some
            // floor result paths can set `on_ground` after the direct/projection suppression sites;
            // if the previous root was below the accepted hard floor by more than the live ECB
            // neighborhood, source collision should have consumed the stage underside before
            // `ft_CheckGroundAndLedge` could publish a landing.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
            (specialhi_understage_floor_clip_action(action_id) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             prev_y < (contact_y - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                       k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t suppress_specialhi_from_below_hard_floor_land =
            // Final writeback variant for SpecialAirHi/SpecialHiFall floor candidates that start
            // below the accepted hard floor. Source floor publication requires the live ECB bottom
            // sweep to enter the top surface; a later root projection must not synthesize a
            // floor-contact bit from inside the stage shell.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
            (specialhi_floor_candidate_starts_below_source_floor(action_id, prev_y, prev_bottom_y,
                                                                 cur_bottom_y, contact_y,
                                                                 batch->state.speed_y_self[idx]) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id))
                ? 1u
                : 0u;
        const uint8_t suppress_specialairhi_floor_angle_land =
            // SpecialAirHi_Coll only enters SpecialHiBound from a floor contact when the angle
            // between floor.normal and self_vel is outside the character's bound threshold.
            // Shallow floor-angle contacts keep the launch airborne and only rotate the model;
            // steeper hard-floor contacts must remain publishable so ftFx_SpecialHiBound_Enter can
            // consume them. Platform contacts are handled by the separate ftCo_8009A134/floor-skip
            // owner above.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound,ftFx_SpecialHiBound_Enter}
            // data/characters/{fox,falco}.json::firefox_bound_angle_degrees
            (specialairhi_floor_contact_angle_continues_launch(
                 action_id, char_id, floor_nx, floor_ny, batch->state.speed_air_x_self[idx],
                 batch->state.speed_y_self[idx]) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id))
                ? 1u
                : 0u;
        float jump_transformed_platform_line_y = 0.0f;
        const uint8_t jump_transformed_platform_line_valid =
            (final_ground_line_idx >= 0 &&
             floor_line_y_at_x_for_env(batch, bi, g, final_ground_line_idx, x,
                                       &jump_transformed_platform_line_y))
                ? 1u
                : 0u;
        const float jump_transformed_platform_bottom_penetration =
            jump_transformed_platform_line_y - cur_bottom_y;
        const uint8_t final_attackair_transformed_platform_floor_skip_active =
            (skip_platform_segment_i != 0xFFFFu && shallow_attackair_platform_ecb_owner &&
             prev_action_id == action_id &&
             stage_collision_floor_line_has_height_platform_transform(stage_id,
                                                                      skip_platform_segment_i))
                ? 1u
                : 0u;
        const uint8_t final_attackair_transformed_platform_downheld_pass_input =
            platform_pass_input_below_raw_threshold(batch, idx, c);
        const float final_attackair_transformed_platform_prev_below_depth =
            jump_transformed_platform_line_y - prev_y;
        const uint8_t final_attackair_transformed_platform_shallow_first_contact =
            // Final-publication variant of the first shallow FoD AttackAir transformed-platform
            // contact. Source AttackAir_Coll is in the MSLFTSC1 first HitCapsule create->clear
            // phase and preserves the callback-local CollData floor owner for the next frame when
            // the previous root is already just below the moving platform. This is independent from
            // the down-held platform-pass input owner and is bounded by the generated
            // submotion/script and stage-transform data.
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
            // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
            // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
            (resolved_line_has_height_platform_transform &&
             first_phase_attackair_platform_ecb_owner && prev_action_id == action_id &&
             jump_transformed_platform_line_valid &&
             final_attackair_transformed_platform_prev_below_depth > k_floor_y_bias &&
             final_attackair_transformed_platform_prev_below_depth <=
                 (2.0f * k_ecb_vertical_unit) &&
             y < jump_transformed_platform_line_y &&
             move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                      batch->state.anim_frame_f32[idx]))
                ? 1u
                : 0u;
        const uint8_t final_attackair_transformed_platform_pass_owner_active =
            (final_attackair_transformed_platform_floor_skip_active ||
             final_attackair_transformed_platform_downheld_pass_input ||
             final_attackair_transformed_platform_shallow_first_contact)
                ? 1u
                : 0u;
        const uint8_t suppress_airborne_transformed_platform_pre_handoff_land =
            // Jump_Coll routes through ft_800835B0 with the soft-platform callback. During upward
            // JumpF/JumpB motion, transformed-platform sweeps can observe the platform before the
            // source callback publishes the landing handoff. The same source owner applies to the
            // first shallow/slow bottom sweep after both callback root points are already below the
            // transformed platform: mpColl_80044628_Floor can see FloorPush/FloorHug on the ECB
            // bottom while mpColl_80044838_Floor's root projection has not reached a stable
            // transformed-platform snap yet. Fast or deeper bottom penetrations remain on the
            // ordinary Jump_Coll landing path.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80047E14,mpColl_80044628_Floor,mpColl_80044838_Floor}
            (resolved_line_has_platform_transform &&
             (action_id == (uint16_t)MSL_ACT_JUMP_F || action_id == (uint16_t)MSL_ACT_JUMP_B) &&
             (batch->state.speed_y_self[idx] > k_floor_horiz_dy_thresh ||
              (prev_y < contact_y && y < contact_y && jump_transformed_platform_line_valid &&
               fabsf(batch->state.speed_y_self[idx]) <= k_ecb_vertical_unit &&
               jump_transformed_platform_bottom_penetration > k_floor_y_bias &&
               jump_transformed_platform_bottom_penetration <= k_ecb_vertical_unit)))
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_transformed_platform_ecb_only_final_land =
            // Final publication guard for the same sustained AttackAirN/Hi/Lw height-transform owner
            // handled in the raw/projection floor-sweep sites above. Some paths record the accepted
            // FoD platform only as final floor contact; reject sustained AttackAir ECB-bottom snaps
            // on the resolved moving platform while the extracted command script is inside its
            // first HitCapsule create->clear phase and a real floor-skip/pass/shallow-first-contact
            // owner is active. Fresh action entry, later hitbox phases, released contacts, deeper
            // transformed-platform contacts, and AttackAir submotions outside this generated owner
            // retain normal final floor publication.
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
            // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox/clear_hitboxes events
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
            (final_attackair_transformed_platform_pass_owner_active &&
             resolved_line_has_height_platform_transform &&
             first_phase_attackair_platform_ecb_owner && prev_action_id == action_id &&
             move_tables_attackair_first_hitbox_phase(char_id, action_id,
                                                      batch->state.anim_frame_f32[idx]) &&
             jump_transformed_platform_line_valid && y < jump_transformed_platform_line_y &&
             (final_attackair_transformed_platform_shallow_first_contact ||
              (jump_transformed_platform_bottom_penetration > k_floor_y_bias &&
               jump_transformed_platform_bottom_penetration <= k_ecb_vertical_unit &&
               final_attackair_transformed_platform_prev_below_depth <=
                   (2.0f * k_ecb_vertical_unit))))
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_transformed_platform_floor_skip_final_land =
            // Final publication guard for the carried FoD AttackAir platform-skip owner when the
            // floor result appears only after projection/remap. The first root crossing after the
            // transformed-platform pass stays airborne for one more source callback pass; already-
            // below-floor followups retain normal LandingAir* publication.
            (final_attackair_transformed_platform_floor_skip_active && final_ground_line_idx >= 0 &&
             floor_line_is_runtime_fighter_solid(g, stage_id, final_ground_line_idx) &&
             (!floor_x_within_line_segment_strict(batch, bi, g, final_ground_line_idx, x) ||
              floor_line_is_generated_stage_slope(batch, bi, g, final_ground_line_idx)) &&
             jump_transformed_platform_line_valid &&
             prev_y > (jump_transformed_platform_line_y + k_floor_y_bias) &&
             y < jump_transformed_platform_line_y && batch->state.speed_y_self[idx] < 0.0f)
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_offspan_hard_floor_edge_final_land =
            // Final writeback variant for the same AttackAir_Coll endpoint-extension family. When
            // mpLib resolves a hard-floor candidate outside the strict segment span, the source
            // floor callback keeps the late-hit AttackAir row airborne for that first edge crossing;
            // in-span hard-floor contacts still publish LandingAir* normally.
            // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
            // data/scripts/{fox,falco}.bin::MSLFTSC1 create_hitbox events
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/mp/mplib.c::{mpLib_8004DD90_Floor,mpLib_8004ED5C}
            (final_ground_line_idx >= 0 &&
             floor_line_is_runtime_fighter_solid(g, stage_id, final_ground_line_idx) &&
             !resolved_line_has_height_platform_transform && shallow_attackair_platform_ecb_owner &&
             prev_action_id == action_id &&
             move_tables_attackair_second_create_hitbox_phase(char_id, action_id,
                                                              batch->state.anim_frame_f32[idx]) &&
             !floor_x_within_line_segment_strict(batch, bi, g, final_ground_line_idx, x) &&
             jump_transformed_platform_line_valid &&
             prev_y > (jump_transformed_platform_line_y + k_floor_y_bias) &&
             y < jump_transformed_platform_line_y && batch->state.speed_y_self[idx] < 0.0f)
                ? 1u
                : 0u;
        const uint8_t suppress_attackair_transformed_platform_below_final_land =
            // Final writeback variant of the carried FoD floor-skip owner above. Keep it scoped to
            // a live transformed-platform skip; released/no-skip AttackAir rows can land through
            // the ordinary ft_80082C74 final floor result even if the root is already below the
            // moving platform.
            // data/stages/bin/griz.bin::MSLSTG01 height platform transforms
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_800471F8,mpColl_80044628_Floor,mpColl_80044838_Floor}
            (resolved_line_has_height_platform_transform && shallow_attackair_platform_ecb_owner &&
             prev_action_id == action_id && jump_transformed_platform_line_valid &&
             move_tables_attackair_last_create_hitbox_phase(char_id, action_id,
                                                            batch->state.anim_frame_f32[idx]) &&
             prev_y < jump_transformed_platform_line_y && y < jump_transformed_platform_line_y &&
             batch->state.speed_y_self[idx] <= k_floor_horiz_dy_thresh)
                ? 1u
                : 0u;
        const uint8_t suppress_jumpaerial_transformed_platform_fastfall_land =
            // Sustained JumpAerial_Coll uses the same ft_800835B0 -> mpColl_80047E14 callback
            // family, but FoD height-transformed platform fastfall rows can observe a platform
            // sweep before the callback-local floor result is published as a landing only while a
            // generated platform floor.index is already carried. Fresh hard-floor-index rows
            // landing onto FoD side platforms use the ordinary JumpAerial_Coll floor handoff.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80043754,mpColl_80047E14,mpColl_80044628_Floor}
            (resolved_line_has_height_platform_transform &&
             (action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             prev_action_id == action_id && batch->state.fall_fast[idx] != 0u &&
             stage_collision_floor_line_has_platform_transform(stage_id,
                                                               batch->state.ground_id[idx]) &&
             jumpaerial_terminal_fastfall_descent(batch, idx))
                ? 1u
                : 0u;
        const float jumpaerial_static_platform_pose_bottom_y =
            y + mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_bias_next, 0u);
        const uint8_t suppress_jumpaerial_static_platform_from_below_final_land =
            // JumpAerial_Coll reaches final floor projection only after mpColl_80044628_Floor has
            // accepted a floor through ftCo_80096CC8. When a just-released platform pass has already
            // carried the JumpAerial root below a static soft platform and the generated pose ECB
            // bottom is now shallowly below that platform, the zero-bottom/root approximation can
            // synthesize Landing from a floor the source callback is still rejecting. No-pass rows
            // whose live pose bottom remains above the platform keep the ordinary landing path even
            // when their roots are below it.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            (c != NULL && stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !resolved_line_has_platform_transform &&
             (action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             prev_action_id == action_id &&
             (stick_i8_to_unit(batch->state.input_main_y[idx]) <=
                  c->platform_air_land_stick_y_threshold ||
              stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                  c->platform_air_land_stick_y_threshold) &&
             jumpaerial_static_platform_pose_bottom_y < (contact_y - k_floor_y_bias) &&
             batch->state.speed_y_self[idx] < 0.0f)
                ? 1u
                : 0u;
        const uint8_t suppress_fall_transformed_platform_fastfall_land =
            // Fall_Coll routes through ft_800831CC -> mpColl_80047E14 with the same
            // ftCo_80096CC8 platform callback as Jump/JumpAerial. FoD height-transformed platform
            // rows can see a remapped platform sweep before the source callback publishes a landing
            // result; preserve the airborne Fall frame for sustained same-action fastfall contacts
            // only. Fresh Fall entries, hard-floor contacts, and static-y stage-object supports
            // such as Randall still use the normal landing path.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80047E14}
            (resolved_line_has_height_platform_transform && action_id == (uint16_t)MSL_ACT_FALL &&
             prev_action_id == action_id && batch->state.fall_fast[idx] != 0u &&
             !resolved_line_height_platform_same_step_contact &&
             !common_fall_flags6_root_floor_projection_hit)
                ? 1u
                : 0u;
        const float fall_loop_end_frame = (action_id == (uint16_t)MSL_ACT_FALL && anim <= 0xFFFFu)
                                              ? msl_anim_end_frame(char_id, (uint16_t)anim)
                                              : 0.0f;
        const uint8_t suppress_fall_loop_wrap_stage_object_floor_to_hard_floor_land =
            // Fall's looping AObj can wrap the visible action frame one callback before the
            // callback-local floor owner publishes the carried stage-object floor -> hard-floor
            // handoff. Keep this to true loop-wrap Fall_Coll rows with a prefix-causal generated
            // platform floor.index and no live allow-interrupt bit; the following
            // below-floor callback remains on the ordinary Fall_Coll landing path.
            // data/stages/bin/*.bin::MSLSTG01 platform transform records
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{
            //   ftCo_Fall_Anim,ftCo_Fall_Coll}
            // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBE8
            // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_FALL &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_FALL &&
             batch->state.seed_prev_action_frame[idx] >= 0 && fall_loop_end_frame > 0.0f &&
             msl_anim_is_looping(char_id, (uint16_t)anim) &&
             ((float)batch->state.seed_prev_action_frame[idx] + 1.0f) >= fall_loop_end_frame &&
             batch->state.action_frame[idx] <= 1 && batch->state.fall_fast[idx] != 0u &&
             batch->state.speed_y_self[idx] < 0.0f &&
             stage_collision_floor_line_has_platform_transform(stage_id,
                                                               batch->state.ground_id[idx]) &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !(final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
               g->lines[(size_t)final_ground_line_idx].is_ledge) &&
             ground_id != batch->state.ground_id[idx] && prev_y > (contact_y + k_floor_y_bias) &&
             (batch->state.state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES] & 0x80u) == 0u)
                ? 1u
                : 0u;
        const float final_landing_lift = contact_y - cur_bottom_y;
        const float escapeair_entry_bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
        const uint8_t suppress_fallspecial_first_sustained_current_ecb_land =
            // Final publication guard for the same Yoshi main-floor first sustained FallSpecial
            // owner handled in the direct sweep/remap paths.
            (fallspecial_yoshi_main_floor_first_sustained_airborne_owner(batch, idx, stage_id) &&
             final_landing_lift >= 0.0f)
                ? 1u
                : 0u;
        const uint8_t suppress_fallspecial_same_floor_early_final_land =
            msl_mpcoll_80047e14_reject_fallspecial_same_floor_early_final_land(
                batch, idx, stage_id, action_id, seed_ground_id, ground_id, final_landing_lift,
                contact_y, y);
        const uint8_t fallspecial_final_entered_from_specialhi_end =
            (is_common_fallspecial_action(action_id) &&
             is_spacie_specialhi_end_fallspecial_source(batch->state.seed_prev_action_id[idx]))
                ? 1u
                : 0u;
        const uint8_t suppress_fallspecial_platform_final_without_source_bottom =
            // Final publication guard for FallSpecial_Coll soft-platform rows. Source reaches
            // `mpColl_80044838_Floor(ignore_bottom=true)` only after `mpColl_80044628_Floor`
            // accepts a platform via `ftCo_80096CC8`; down-held input rejects the platform, and
            // sustained FallSpecial rows whose callback-local bottom remains above the soft floor
            // have not satisfied that source precondition yet. The exception is the just-entered
            // spacie SpecialHi end -> FallSpecial frame: `ftFx_SpecialHiFall/Bound_Anim` changes
            // motion state before the collision callback, so the source callback consumes that
            // end-state CollData lifetime and may publish LandingFallSpecial immediately.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
            //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialHiFall_Anim,ftFx_SpecialHiBound_Anim}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            (is_common_fallspecial_action(action_id) &&
             stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             (platform_pass_input_below_raw_threshold(batch, idx, c) ||
              (!fallspecial_final_entered_from_specialhi_end &&
               cur_bottom_y > (contact_y + k_floor_y_bias))))
                ? 1u
                : 0u;
        const uint8_t suppress_fall_same_floor_early_final_land =
            msl_mpcoll_80047e14_reject_fall_same_floor_early_final_land(
                batch, idx, g, stage_id, action_id, seed_ground_id, ground_id,
                final_ground_line_idx, contact_y, y);
        uint8_t locked_desired_bottom_final_sweep_hit = 1u;
        if (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            ecb_lock_timer_seed != 0u && batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
            batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u &&
            final_ground_line_idx >= 0 &&
            stage_collision_floor_line_is_platform(stage_id, ground_id)) {
          int locked_sweep_line_idx = -1;
          locked_desired_bottom_final_sweep_hit =
              (msl_mpcheck_floor(batch, idx, bi, g, stage_id, prev_x,
                                 prev_y + batch->state.coll_desired_ecb_bottom_rel_y[idx], x,
                                 y + batch->state.coll_desired_ecb_bottom_rel_y[idx],
                                 skip_platform_segment_i, final_ground_line_idx, -1, c,
                                 &locked_sweep_line_idx, NULL, NULL, NULL, NULL) &&
               locked_sweep_line_idx >= 0 &&
               g->lines[(size_t)locked_sweep_line_idx].segment_i == ground_id)
                  ? 1u
                  : 0u;
        }
        const uint8_t final_ground_line_is_ledge =
            (final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
             g->lines[(size_t)final_ground_line_idx].is_ledge)
                ? 1u
                : 0u;
        const uint8_t final_ledge_x_in_bounds =
            (final_ground_line_is_ledge &&
             floor_x_within_line_bounds(batch, bi, g, final_ground_line_idx, cur_bottom_x))
                ? 1u
                : 0u;
        const uint8_t final_ground_line_is_sloped_ledge =
            floor_line_is_generated_sloped_ledge(batch, bi, g, final_ground_line_idx);
        const int seed_ground_line_idx_for_final =
            stage_collision_floor_line_index(stage_id, seed_ground_id);
        const uint8_t seed_ground_line_is_ledge =
            (seed_ground_line_idx_for_final >= 0 &&
             (size_t)seed_ground_line_idx_for_final < g->line_count &&
             g->lines[(size_t)seed_ground_line_idx_for_final].is_ledge)
                ? 1u
                : 0u;
        const uint8_t suppress_jumpaerial_escapeair_high_lift_ledge_final_land =
            // Final guard for fresh JumpAerial -> EscapeAir ledge remaps that can be accepted by
            // multiple projection paths above. The same source owner also applies when the
            // teacher-forced seed already names a different hidden cliff ledge floor as
            // CollData.floor.index: mpColl_80044838_Floor can write back a ledge line with a large
            // upward root correction even though the source `EscapeAir_Coll` ledge/floor handoff
            // still uses the pre-entry CollData lifetime and publishes the row airborne. Later
            // same-cliff/deeper handoffs are covered by separate positive locks and must continue
            // to land through the normal EscapeAir floor callback.
            // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
            // refs/melee/src/melee/ft/ftcliffcommon.c::ftCliffCommon_80081370
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && batch->state.action_frame[idx] <= 3 &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             batch->state.seed_prev_action_frame[idx] <= 4 && final_ground_line_is_ledge &&
             !final_ground_line_is_sloped_ledge &&
             final_landing_lift > (escapeair_entry_bottom_rel0 + (2.0f * k_ecb_vertical_unit)))
                ? 1u
                : 0u;
        const uint8_t suppress_jumpaerial_escapeair_static_platform_overstep_final_land =
            // Fresh JumpAerial -> EscapeAir can also reach a static soft platform after the
            // callback-local bottom sweep is produced mostly by the pre-entry JumpAerial ECB
            // lifetime rather than by the current EscapeAir root step. Source
            // mpColl_80044628_Floor first accepts the bottom crossing and only then lets
            // mpColl_80044838_Floor project the root; if that final bottom correction is larger
            // than this callback's vertical step, it is the same pre-entry CollData handoff gap as
            // the ledge guard above, not a publishable LandingFallSpecial row. Same-frame
            // JumpAerial_IASA entries, locked desired-bottom/CollData_X130 start-platform rows,
            // and deeper current-step crossings remain on the ordinary EscapeAir_Coll floor owner.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.action_frame[idx] <= 2 && batch->state.ecb_lock_timer[idx] == 0 &&
             batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 0u &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !stage_collision_floor_line_has_platform_transform(stage_id, ground_id) &&
             batch->state.speed_y_self[idx] < 0.0f &&
             final_landing_lift > (fabsf(batch->state.speed_y_self[idx]) + k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t suppress_sustained_escapeair_same_platform_lock_land =
            // ftCo_EscapeAir_Coll consumes floor contact through ft_80082C74/mpColl_800471F8.
            // During the locked CollData window, replay-real downward EscapeAir continuations can
            // carry a soft-platform floor.index through the same platform after the aerial-jump
            // resource has already been spent. That carried index is CollData provenance, not a new
            // floor hit; do not turn it into LandingFallSpecial while the stick is still past the
            // extracted platform pass threshold and the snap is shallower than the entered
            // EscapeAir ECB bottom. Deeper penetrations have already reached the source
            // ft_80082C74/mpColl_800471F8 floor handoff. Ground-jump airdodges keep one jump
            // available and still land through EscapeAir_Coll's normal floor callback.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Enter
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Enter
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80043754}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             ecb_lock_timer_seed != 0u && batch->state.jumps_left[idx] == 0u &&
             ground_id == seed_ground_id &&
             stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !stage_collision_floor_line_has_platform_transform(stage_id, ground_id) &&
             final_landing_lift < escapeair_entry_bottom_rel0 &&
             (batch->state.ledge_drop_floor_skip_segment_id == NULL ||
              batch->state.ledge_drop_floor_skip_segment_id[idx] != ground_id) &&
             c != NULL &&
             stick_i8_to_unit(batch->state.input_main_y[idx]) <=
                 c->platform_air_land_stick_y_threshold &&
             stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                 c->platform_air_land_stick_y_threshold)
                ? 1u
                : 0u;
        const uint8_t suppress_sustained_escapeair_same_ledge_lock_land =
            // Same CollData locked-floor owner as the same-platform guard above, but for carried
            // ledge floor segments. EscapeAir_Coll should not publish LandingFallSpecial from a
            // shallow final writeback on the same carried ledge floor when the floor-sweep source
            // row was already below that floor. It stays airborne until the entered EscapeAir ECB
            // reaches the source ft_80082C74/mpColl_800471F8 floor handoff depth.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_800471F8,mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             ecb_lock_timer_seed != 0u && ground_id == seed_ground_id &&
             seed_ground_line_is_ledge && final_ground_line_is_ledge &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
             batch->state.floor_sweep_prev_pos_y[idx] < contact_y && final_landing_lift >= 0.0f &&
             final_landing_lift < escapeair_entry_bottom_rel0)
                ? 1u
                : 0u;
        const uint8_t fresh_jumpaerial_zero_bottom_platform_projection_hit =
            (escapeair_locked_platform_root_projection_hit &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             batch->state.seed_prev_action_frame[idx] <= 1 &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias)
                ? 1u
                : 0u;
        const uint8_t suppress_locked_escapeair_missing_bottom_owner_land =
            // A replay seed with CollData_X130_Locked but no preserved desired-bottom owner cannot
            // prove that mpColl_80044628_Floor reached the source bottom-sweep precondition. If the
            // carried callback root was already below the accepted floor before this frame, reject
            // the upward root projection and leave the row airborne. True above->floor crossings
            // (for example FD EscapeAir landing rows) keep the normal floor publication path.
            // Fresh JumpAerial -> EscapeAir on an ordinary hard floor is also a source
            // ft_80082C74/mpColl_800471F8 handoff: the IASA transition happens before Fighter_procMap
            // and the first EscapeAir collision pass may publish LandingFallSpecial from the carried
            // hard-floor index even though the runtime desired-bottom lock has no replay seed owner.
            // Ledge, soft-platform, and transformed-platform rows stay on their explicit guards.
            // First-frame JumpAerial -> EscapeAir zero-bottom platform projections are explicit above:
            // when that source owner has accepted a same-platform root projection, do not erase it here.
            // Ground-jump airdodges and later carried JumpAerial frames remain suppressed.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_timer_seed != 0u &&
             batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 0u &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias &&
             !fresh_jumpaerial_zero_bottom_platform_projection_hit &&
             !escapeair_fresh_jump_height_platform_handoff_hit &&
             !escapeair_stale_platform_root_handoff_hit &&
             !escapeair_missing_bottom_hard_floor_sweep_owner && final_landing_lift >= 0.0f &&
             batch->state.floor_sweep_prev_pos_y[idx] < contact_y &&
             !((batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
                batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
               batch->state.seed_prev_action_frame[idx] <= 4 && final_ground_line_idx >= 0 &&
               !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
               !resolved_line_has_platform_transform && !final_ground_line_is_ledge))
                ? 1u
                : 0u;
        const uint8_t suppress_locked_desired_platform_without_bottom_sweep =
            // `mpColl_80044838_Floor(ignore_bottom=true)` is only reached after
            // `mpColl_80044628_Floor` sees an ECB-bottom floor crossing. Several approximation
            // paths can publish a root snap from the carried platform floor.index before that
            // source bottom sweep happens; keep those rows airborne until the preserved
            // `desired_ecb.bottom` actually crosses the accepted platform. This is a carried
            // floor.index guard, so keep it on the same seeded platform segment; different platform
            // segments are new floor candidates owned by the normal EscapeAir_Coll callback.
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80044628_Floor,mpColl_80044838_Floor,mpColl_80046904}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             ecb_lock_timer_seed != 0u && ground_id == seed_ground_id &&
             !locked_desired_bottom_final_sweep_hit)
                ? 1u
                : 0u;
        const uint8_t runtime_live_jumpaerial_nonplatform_root_crossing =
            ((batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 2u ||
              batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 3u) &&
             ecb_lock_timer_seed > 1u &&
             (!final_ground_line_is_ledge || ecb_lock_timer_seed >= 3u) &&
             prev_y > (contact_y + k_floor_y_bias) && y <= (contact_y + k_floor_y_bias) &&
             batch->state.action_frame[idx] >= 4)
                ? 1u
                : 0u;
        const uint8_t suppress_locked_desired_nonplatform_without_bottom_sweep =
            // Final guard for the same CollData_X130_Locked desired-bottom precondition on
            // non-platform floors. Root/zero-bottom approximation paths may publish a hard or
            // sloped floor result, but source cannot reach mpColl_80044838_Floor while the preserved
            // desired ECB bottom remains above the accepted floor.
            //
            // Runtime owner values 2 and 3 are the JumpAerial -> EscapeAir pass-through provenance
            // from the entry callback. They should not suppress the later same-callback
            // non-platform root crossing once EscapeAir reaches the carried hard floor; that
            // remains owned by ft_80082C74/mpColl_800471F8's non-platform path. Horizontal ledge
            // floors need the later locked interpolation phase before that root crossing is source
            // owned; rows without that callback-visible phase still keep the desired-bottom
            // precondition.
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_LoadECB_inline,mpColl_80044628_Floor,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             ecb_lock_timer_seed != 0u &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             !resolved_line_has_platform_transform &&
             batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
             batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u &&
             !escapeair_early_ledge_root_floor_owner &&
             !runtime_live_jumpaerial_nonplatform_root_crossing &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
             (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) > (contact_y + k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t suppress_kneebend_escapeair_slope_final_land =
            // Fresh KneeBend -> EscapeAir can publish normal hard/platform floor handoffs, but
            // generated sloped ledge floors remain owned by mpColl's floor-edge path on the entry
            // frame. Reject final writeback from the zero-bottom approximation on those slopes.
            // data/stages/bin/*.bin::MSLSTG01 is_ledge + generated sloped floor geometry
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
             batch->state.seed_prev_action_frame[idx] >= 4 && ecb_lock_timer_seed != 0u &&
             !was_grounded && final_ground_line_is_sloped_ledge)
                ? 1u
                : 0u;
        const uint8_t suppress_cliff_horizontal_ledge_locked_final_land =
            // Same source owner as the floor-sweep suppression above, kept as a final publication
            // guard because several mpColl approximation paths can accept the restored ledge floor.
            // Horizontal ledge floors should not publish LandingFallSpecial while the direct
            // reseed's locked zero-bottom handoff is still earlier than the callback-local source
            // floor contact. Keep this to off-end ledge snaps and shallow in-span final snaps; once
            // the accepted in-span floor is deeper than the entered EscapeAir ECB-bottom extent plus
            // mpColl's vertical ECB unit, the normal EscapeAir_Coll floor handoff owns
            // LandingFallSpecial. Sloped ledges retain the generated low-floor owner, and later lock
            // phases on flat ledges continue through the normal landing path.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpCollInterpolateECB,mpColl_80044628_Floor,mpColl_80044838_Floor}
            (cliff_ledge_floor_owner_active &&
             batch->state.cliff_ledge_floor_segment_seeded != NULL &&
             batch->state.cliff_ledge_floor_segment_seeded[idx] != 0u &&
             action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             ground_id == batch->state.cliff_ledge_floor_segment_id[idx] &&
             batch->state.coll_desired_ecb_bottom_rel_y[idx] > k_floor_y_bias &&
             final_ground_line_idx >= 0 &&
             !floor_line_is_generated_stage_slope(batch, bi, g, final_ground_line_idx) &&
             (!final_ledge_x_in_bounds ||
              final_landing_lift < (escapeair_entry_bottom_rel0 + k_ecb_vertical_unit)) &&
             ecb_lock_timer_seed > 1u)
                ? 1u
                : 0u;
        if (suppress_escapeair_transformed_remap_land || suppress_specialairhi_platform_land ||
            suppress_fallspecial_first_sustained_current_ecb_land ||
            suppress_fallspecial_same_floor_early_final_land ||
            suppress_fallspecial_platform_final_without_source_bottom ||
            suppress_fall_same_floor_early_final_land ||
            suppress_specialhi_transformed_platform_land ||
            suppress_specialhi_understage_hard_floor_land ||
            suppress_specialhi_from_below_hard_floor_land ||
            suppress_specialairhi_floor_angle_land ||
            suppress_airborne_transformed_platform_pre_handoff_land ||
            suppress_attackair_transformed_platform_ecb_only_final_land ||
            suppress_attackair_transformed_platform_floor_skip_final_land ||
            suppress_attackair_offspan_hard_floor_edge_final_land ||
            suppress_attackair_transformed_platform_below_final_land ||
            suppress_jumpaerial_transformed_platform_fastfall_land ||
            suppress_jumpaerial_static_platform_from_below_final_land ||
            suppress_fall_transformed_platform_fastfall_land ||
            suppress_fall_loop_wrap_stage_object_floor_to_hard_floor_land ||
            suppress_sustained_escapeair_same_platform_lock_land ||
            suppress_sustained_escapeair_same_ledge_lock_land ||
            suppress_locked_escapeair_missing_bottom_owner_land ||
            suppress_locked_desired_platform_without_bottom_sweep ||
            suppress_locked_desired_nonplatform_without_bottom_sweep ||
            suppress_kneebend_escapeair_slope_final_land ||
            suppress_jumpaerial_escapeair_high_lift_ledge_final_land ||
            suppress_jumpaerial_escapeair_static_platform_overstep_final_land ||
            damage_active_hitlag_root_below_bottom_above_floor_owner ||
            damage_active_hitlag_downward_sdi_airborne_owner ||
            suppress_cliff_horizontal_ledge_locked_final_land) {
          uint16_t reject_ground_id =
              suppress_specialairhi_platform_land ? ground_id : batch->state.ground_id[idx];
          float reject_pos_y = batch->state.pos_y[idx];
          if (suppress_specialairhi_platform_land && batch->state.floor_skip_segment_id != NULL) {
            batch->state.floor_skip_segment_id[idx] = ground_id;
          } else if (suppress_fallspecial_first_sustained_current_ecb_land ||
                     suppress_fallspecial_same_floor_early_final_land ||
                     suppress_fallspecial_platform_final_without_source_bottom ||
                     suppress_fall_same_floor_early_final_land) {
            reject_pos_y = y;
          } else if (suppress_specialhi_understage_hard_floor_land) {
            // Keep the rejected root outside the same live ECB neighborhood; otherwise the next frame
            // can re-accept the same inside-stage floor.
            reject_pos_y = contact_y -
                           specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                           k_floor_y_bias;
          } else if (suppress_specialhi_from_below_hard_floor_land) {
            reject_pos_y = y;
          } else if (suppress_specialairhi_floor_angle_land) {
            reject_pos_y = y;
          } else if (suppress_airborne_transformed_platform_pre_handoff_land) {
            reject_pos_y = y;
          } else if (suppress_attackair_transformed_platform_ecb_only_final_land ||
                     suppress_attackair_transformed_platform_floor_skip_final_land ||
                     suppress_attackair_offspan_hard_floor_edge_final_land ||
                     suppress_attackair_transformed_platform_below_final_land) {
            if (suppress_attackair_transformed_platform_ecb_only_final_land) {
              publish_attackair_transformed_platform_floor_skip_from_sweep(
                  batch, idx, bi, g, stage_id, final_ground_line_idx, x, prev_y, y);
            } else if (suppress_attackair_transformed_platform_floor_skip_final_land &&
                       batch->state.floor_skip_segment_id != NULL) {
              batch->state.floor_skip_segment_id[idx] = 0xFFFFu;
            }
            reject_pos_y = y;
          } else if (suppress_jumpaerial_escapeair_high_lift_ledge_final_land) {
            reject_pos_y = cur_bottom_y - cur_bot.rel_y;
          } else if (suppress_jumpaerial_escapeair_static_platform_overstep_final_land) {
            reject_pos_y = cur_bottom_y - cur_bot.rel_y;
          } else if (suppress_jumpaerial_static_platform_from_below_final_land) {
            reject_pos_y = y;
          } else if (suppress_jumpaerial_transformed_platform_fastfall_land ||
                     suppress_fall_transformed_platform_fastfall_land) {
            reject_pos_y = y;
          } else if (suppress_fall_loop_wrap_stage_object_floor_to_hard_floor_land) {
            reject_pos_y = y;
          } else if (suppress_sustained_escapeair_same_platform_lock_land) {
            reject_pos_y = cur_bottom_y - cur_bot.rel_y;
          } else if (suppress_sustained_escapeair_same_ledge_lock_land) {
            reject_pos_y = cur_bottom_y - cur_bot.rel_y;
          } else if (suppress_locked_escapeair_missing_bottom_owner_land) {
            reject_pos_y = y;
          } else if (suppress_locked_desired_platform_without_bottom_sweep) {
            reject_pos_y = y;
          } else if (suppress_locked_desired_nonplatform_without_bottom_sweep) {
            reject_pos_y = y;
          } else if (suppress_kneebend_escapeair_slope_final_land) {
            reject_pos_y = y;
          } else if (damage_active_hitlag_downward_sdi_airborne_owner ||
                     damage_active_hitlag_root_below_bottom_above_floor_owner) {
            reject_pos_y = y;
          } else if (suppress_cliff_horizontal_ledge_locked_final_land) {
            reject_pos_y = y;
          }
          mpcoll_reject_floor_publication_to_current_bottom(
              &mpcoll_ctx, &on_ground, &ground_id, &contact_x, &contact_y, reject_ground_id,
              reject_pos_y, cur_bottom_x, cur_bottom_y);
        }
      }

      if (on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          ecb_lock_timer_seed != 0u && batch->state.action_frame[idx] == 4 &&
          prefer_line_idx >= 0 && !g->lines[(size_t)prefer_line_idx].is_platform &&
          !g->lines[(size_t)prefer_line_idx].is_ledge &&
          fabsf(cur_bottom_x - prev_bottom_x) <= (float)k_floor_horiz_dy_thresh &&
          batch->state.prev_pos_y[idx] < 0.0f && y <= -fabsf(batch->state.speed_y_self[idx])) {
        // Sustained EscapeAir frame-start below-floor lock window:
        // EscapeAir_Coll delegates through ft_80082C74 while CollData_X130_Locked is still active.
        // When the frame-start cur_pos was already below the carried hard floor, source
        // mpColl_80043754/mpCollInterpolateECB preserves the airborne EscapeAir row for this
        // callback instead of treating the already-penetrating root as a fresh floor crossing.
        // Above->below crossings in the same visible window continue through ft_80082C74 and enter
        // LandingFallSpecial normally.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
        // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
        mpcoll_reject_floor_publication_to_current_bottom(&mpcoll_ctx, &on_ground, &ground_id,
                                                          &contact_x, &contact_y, seed_ground_id, y,
                                                          cur_bottom_x, cur_bottom_y);
      }
      const float ledge_drop_skip_lift = contact_y - y;
      const float ledge_drop_skip_large_projection =
          fabsf(batch->state.speed_y_self[idx]) +
          mpcoll_floor_projection_lift_allowance(&cur_ecb_points);
      if (on_ground && batch->state.ledge_drop_floor_skip_segment_id != NULL &&
          batch->state.ledge_drop_floor_skip_segment_id[idx] != 0xFFFFu &&
          action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && contact_y > k_floor_y_bias &&
          ground_id == batch->state.ledge_drop_floor_skip_segment_id[idx] &&
          stage_collision_floor_line_is_platform(stage_id, ground_id) &&
          ledge_drop_skip_lift > ledge_drop_skip_large_projection) {
        // Ledge-drop floor skip is a hidden CollData owner. Some transformed/platform floor
        // projection paths can resolve through remap helpers after the ordinary skip check; reject
        // only oversized same-floor EscapeAir lifts above stage-floor height at final writeback.
        // Ordinary ledge-airdodge landings on adjacent Yoshi floor segments can still carry the
        // replay-visible platform id, and non-EscapeAir recovery landings are owned by their own
        // callbacks. Source ledge release sets x2064_ledgeCooldown but does not call
        // mpUpdateFloorSkip, so do not make that a broad platform-pass lane. Rejection must also
        // undo the local floor snap: keeping the projected Y creates an airborne ledgedash teleport
        // to the skipped platform.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffWait.c::ftCo_8009A9AC
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044628_Floor}
        mpcoll_reject_floor_publication_to_current_bottom(&mpcoll_ctx, &on_ground, &ground_id,
                                                          &contact_x, &contact_y, seed_ground_id, y,
                                                          cur_bottom_x, cur_bottom_y);
      }
      if (on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          stage_collision_floor_line_is_platform(stage_id, ground_id) &&
          !escapeair_stale_platform_root_handoff_hit &&
          !escapeair_fresh_jump_height_platform_handoff_hit &&
          batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
          batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u &&
          (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) > (contact_y + k_floor_y_bias)) {
        // Final mpColl floor precondition for locked EscapeAir platform snaps:
        // root projection is legal only after the callback's current desired ECB bottom has reached
        // the platform floor. Reject platform writeback where the pre-snap callback root plus
        // desired bottom is still above the accepted platform line.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
        mpcoll_reject_floor_publication_to_current_bottom(&mpcoll_ctx, &on_ground, &ground_id,
                                                          &contact_x, &contact_y, seed_ground_id, y,
                                                          cur_bottom_x, cur_bottom_y);
      }
      if (on_ground && action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI &&
          raw_current_floor_line_idx >= 0 && msl_char_params(char_id) != NULL &&
          batch->state.action_frame[idx] <=
              (int16_t)msl_char_params(char_id)->firefox_bound_delay_frames &&
          !(prev_bottom_y > (contact_y + k_floor_y_bias) &&
            cur_bottom_y <= (contact_y + k_floor_y_bias)) &&
          !stage_collision_floor_line_is_platform(stage_id, seed_ground_id) &&
          !floor_x_within_line_bounds(batch, bi, g, raw_current_floor_line_idx, prev_x) &&
          !stage_collision_floor_line_is_platform(stage_id, ground_id)) {
        // SpecialAirHi_Coll's floor handoff comes from the callback-local mpColl floor result, not
        // an endpoint-clamped projection from a stale carried floor.index. Once the launch root has
        // already left the carried hard-floor span before the x6C bounce gate, source
        // ft_CheckGroundAndLedge does not publish a same-floor rebound by sweeping back through the
        // old segment endpoint. Once the source bound counter reaches x6C, later rows can rebound
        // normally from the refreshed floor owner.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
        //   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound}
        // refs/melee/src/melee/ft/chara/ftFox/types.h::x6C_FOX_FIREFOX_BOUNCE_VAR
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
        mpcoll_reject_floor_publication_to_current_bottom(&mpcoll_ctx, &on_ground, &ground_id,
                                                          &contact_x, &contact_y, seed_ground_id, y,
                                                          cur_bottom_x, cur_bottom_y);
      }
      if (on_ground && batch->state.coll_floor_result_valid[idx] == 0u) {
        // Source mpColl keeps the accepted floor result local to the callback before the wrapper
        // consumes it. Mirror that result for ordinary direct floor hits too; the 4A908 retry above
        // is the retained source-clear consumer that requires this scratch lane.
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044628_Floor}
        mpcoll_record_callback_floor_result(&mpcoll_ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                                            ground_id, contact_x, contact_y, floor_nx, floor_ny);
      } else if (!on_ground && batch->state.coll_floor_result_valid[idx] != 0u) {
        mpcoll_discard_callback_floor_result(&mpcoll_ctx);
      }

      if (on_ground && is_common_fallspecial_action(action_id) &&
          stage_id == (uint32_t)MSL_STAGE_FINAL_DESTINATION_LOCAL &&
          batch->state.ground_id[idx] == 1u && batch->state.seed_prev_action_id[idx] == action_id &&
          batch->state.seed_prev_action_frame[idx] <= 5 && batch->state.action_frame[idx] <= 6 &&
          batch->state.speed_y_self[idx] < 0.0f && batch->state.fall_fast[idx] != 0u &&
          ground_id == seed_ground_id && batch->state.floor_sweep_prev_pos_y[idx] > contact_y &&
          y < contact_y) {
        // Last publication guard for same carried FD main-floor early FallSpecial root crossings.
        // Some projection paths publish after the local sweep guards above; source still leaves
        // these rows airborne until an adjacent floor/seam or later callback owns LandingFallSpecial.
        // data/stages/bin/grnla.bin::MSLSTG01 floor segment 1
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
        //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
        mpcoll_reject_floor_publication(&mpcoll_ctx, &on_ground, &ground_id, &contact_x, &contact_y,
                                        ground_id, y, contact_x, contact_y);
      }
      if (damage_active_hitlag_downward_sdi_airborne_owner ||
          damage_active_hitlag_root_below_bottom_above_floor_owner) {
        // Last publication guard for the same active-hitlag owner above. Some generic
        // floor paths can keep `ground_or_air` airborne while still lifting the root to the floor
        // bias; source keeps the current hitlag-callback root until the damage collision owner
        // resolves the floor contact.
        mpcoll_reject_floor_publication_to_current_bottom(&mpcoll_ctx, &on_ground, &ground_id,
                                                          &contact_x, &contact_y, seed_ground_id, y,
                                                          cur_bottom_x, cur_bottom_y);
        mpcoll_discard_callback_floor_result(&mpcoll_ctx);
      }
      const MslMpcollFloorPublication floor_publication = {
          .on_ground = on_ground,
          .airborne_ground_id = ground_id,
          .contact =
              {
                  .ground_id = ground_id,
                  .contact_x = contact_x,
                  .contact_y = contact_y,
                  .normal_x = floor_nx,
                  .normal_y = floor_ny,
              },
          .cur_bottom_x = cur_bottom_x,
          .cur_bottom_y = cur_bottom_y,
      };
      mpcoll_commit_final_floor_state(&mpcoll_ctx, &floor_publication);
      // Promote CollData ECB lifetime for rollout. Source interpolation copies ecb to prev_ecb
      // before stepping toward desired_ecb; after this single-step callback pass, carry the resolved
      // desired ECB as the next current ECB.
      // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
      if (have_state_cur_ecb) {
        mpcoll_store_prev_ecb_points(batch, idx, &state_cur_ecb_points);
      } else {
        mpcoll_store_prev_ecb_points(batch, idx, &prev_ecb_points);
      }
      MslEcbWorldPoints stored_desired_ecb_points = desired_ecb_points;
      const uint8_t nonfastfall_fall_generic_lock_bottom =
          (uint8_t)(action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] == 0u &&
                    batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 1u);
      uint8_t stored_locked_desired_bottom_owner =
          (use_locked_desired_ecb_bottom && ecb_lock_active &&
           !nonfastfall_fall_generic_lock_bottom)
              ? (batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 2u
                     ? 2u
                     : (batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 3u ? 3u : 1u))
              : 0u;
      if (!stored_locked_desired_bottom_owner && ecb_lock_active &&
          ((action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
            ((stage_id == (uint32_t)MSL_STAGE_FOUNTAIN_OF_DREAMS_LOCAL &&
              batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 1u) ||
             batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 2u)) ||
           (action_id == (uint16_t)MSL_ACT_FALL && batch->state.fall_fast[idx] != 0u) ||
           msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_COMMON_AIR_COLL)) &&
          batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
          batch->state.coll_desired_ecb_bottom_locked_owner[idx] != 0u) {
        // Source `mpColl_LoadECB_inline` preserves desired_ecb.bottom while CollData_X130_Locked is
        // active. The lite floor pass keeps Jump/JumpAerial floor ownership on its existing
        // callback-phase slice above, but sustained EscapeAir continuations, including
        // replay-seeded owner-1 CollData_X130 rows, and subsequent airborne wall/ceiling passes
        // still consume the same source CollData desired ECB until the lock expires. Carry the
        // preserved desired bottom without changing the already-completed floor decision.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80045B74_LeftWall}
        msl_ecb_world_points_preserve_desired_bottom_rel_y(
            &stored_desired_ecb_points, x, y, batch->state.coll_desired_ecb_bottom_rel_y[idx]);
        stored_locked_desired_bottom_owner =
            (batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 2u)
                ? 2u
                : (batch->state.coll_desired_ecb_bottom_locked_owner[idx] == 3u ? 3u : 1u);
      }
      mpcoll_store_current_ecb_points(batch, idx, &cur_ecb_points);
      mpcoll_store_desired_ecb_points(batch, idx, &stored_desired_ecb_points);
      const float unlocked_fall_pose_bottom_rel =
          (action_id == (uint16_t)MSL_ACT_FALL && ecb_lock_timer <= 1u)
              ? mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_cur, 0u)
              : 0.0f;
      if (!batch->state.on_ground[idx] && action_id == (uint16_t)MSL_ACT_FALL &&
          ecb_lock_timer <= 1u &&
          batch->state.coll_desired_ecb_bottom_rel_y[idx] <= k_floor_y_bias &&
          unlocked_fall_pose_bottom_rel > k_floor_y_bias) {
        // CollData_X130_Locked is cleared by Fighter_procMap before the Fall_Coll map callback.
        // On the expiry frame, mpColl_LoadECB_inline stops preserving desired_ecb.bottom=0 and
        // reloads the ordinary Fall ECB pose. Runtime rollouts must promote that unlocked
        // callback-local ECB lifetime just like teacher-forced reseeds do; otherwise the next
        // fastfall frame can keep a stale zero-bottom ECB and publish a false Landing.
        // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
        MslEcbWorldPoints unlocked_current_ecb = {0};
        MslEcbWorldPoints unlocked_desired_ecb = {0};
        msl_ecb_world_points_sample(&unlocked_current_ecb, char_id, anim, ecb_frame_prev,
                                    facing_dir_for_ecb, prev_x, prev_y, 0u);
        msl_ecb_world_points_sample(&unlocked_desired_ecb, char_id, anim, ecb_frame_cur,
                                    facing_dir_for_ecb, x, y, 0u);
        mpcoll_store_prev_ecb_points(batch, idx, &unlocked_current_ecb);
        mpcoll_store_current_ecb_points(batch, idx, &unlocked_current_ecb);
        mpcoll_store_desired_ecb_points(batch, idx, &unlocked_desired_ecb);
        batch->state.ecb_lock_timer[idx] = 0u;
      }
      batch->state.coll_prev_ecb_bottom_valid[idx] = 1u;
      batch->state.coll_ecb_bottom_valid[idx] = 1u;
      batch->state.coll_desired_ecb_bottom_valid[idx] = 1u;
      batch->state.coll_desired_ecb_bottom_locked_owner[idx] = stored_locked_desired_bottom_owner;
    }
  }
}
