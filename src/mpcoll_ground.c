#include "mpcoll_ground.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action.h"
#include "action_ids.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "match_flow.h"
#include "mpcoll_ecb_points.h"
#include "motion_state_owners.h"
#include "mpcoll_wall_ceil.h"
#include "state_flags.h"
#include "stage_collision.h"
#include "input_axis.h"

static inline uint8_t mpcoll_is_pending_throw_release_victim(const MslBatch* batch, int bi, int p) {
  if (batch == NULL) {
    return 0u;
  }
  const int num_players = (int)batch->config.num_players;
  if (p < 0 || p >= num_players) {
    return 0u;
  }
  for (int owner = 0; owner < num_players; owner++) {
    if (owner == p) {
      continue;
    }
    const size_t oidx = msl_idx_player(bi, owner);
    if (batch->state.throw_pending_victim_port[oidx] == (uint8_t)p &&
        batch->state.throw_pending_hit_idx[oidx] != 0xFFu) {
      // Shared release owner:
      // - ftCo_800DD724 is the common ThrowF/B/Hi/Lw release consume path.
      // - The victim is detached from the owner link before later damage/item resolution, but that
      //   same frame is still owned by throw release rather than generic fighter mpColl.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
      return 1u;
    }
  }
  return 0u;
}

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

enum {
  MSL_MPCOLL_FLOOR_RESULT_NONE = 0u,
  MSL_MPCOLL_FLOOR_RESULT_DIRECT = 1u,
  MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY = 2u,
};

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
  const float raw_sx = stick_i8_to_unit(batch->state.input_main_x[idx]);
  const float raw_sy = stick_i8_to_unit(batch->state.input_main_y[idx]);
  const float sy =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  if (batch->state.damage_hitlag_downward_sdi_consumed[idx] != 0u) {
    return 1u;
  }
  const float prev_sx = stick_i8_to_unit(batch->state.prev_input_main_x[idx]);
  const float prev_sy = stick_i8_to_unit(batch->state.prev_input_main_y[idx]);
  const float mag_sq = raw_sx * raw_sx + raw_sy * raw_sy;
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
  const float prev_mag_sq = prev_sx * prev_sx + prev_sy * prev_sy;
  const uint8_t first_active_radius_crossing =
      (batch->state.action_frame[idx] == 1 && batch->state.tilt_timer_x[idx] == 254u &&
       batch->state.tilt_timer_y[idx] == 254u &&
       batch->state.seed_prev_action_id[idx] != action_id &&
       mag_sq >= c->sdi_radius * c->sdi_radius && prev_mag_sq < c->sdi_radius * c->sdi_radius)
          ? 1u
          : 0u;
  return (uint8_t)((timer_window_owner || first_active_radius_crossing) ? 1u : 0u);
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

static inline void mpcoll_clear_callback_floor_result(MslBatch* batch, size_t idx, float prev_x,
                                                      float prev_y, float cur_x, float cur_y) {
  batch->state.coll_floor_result_valid[idx] = 0u;
  batch->state.coll_floor_result_source[idx] = (uint8_t)MSL_MPCOLL_FLOOR_RESULT_NONE;
  batch->state.coll_floor_result_segment_id[idx] = 0xFFFFu;
  batch->state.coll_floor_result_contact_x[idx] = 0.0f;
  batch->state.coll_floor_result_contact_y[idx] = 0.0f;
  batch->state.coll_floor_result_normal_x[idx] = 0.0f;
  batch->state.coll_floor_result_normal_y[idx] = 1.0f;
  batch->state.coll_substep_prev_pos_x[idx] = prev_x;
  batch->state.coll_substep_prev_pos_y[idx] = prev_y;
  batch->state.coll_substep_cur_pos_x[idx] = cur_x;
  batch->state.coll_substep_cur_pos_y[idx] = cur_y;
}

static inline void mpcoll_record_callback_floor_result(MslBatch* batch, size_t idx, uint8_t source,
                                                       uint16_t segment_id, float contact_x,
                                                       float contact_y, float normal_x,
                                                       float normal_y) {
  batch->state.coll_floor_result_valid[idx] = 1u;
  batch->state.coll_floor_result_source[idx] = source;
  batch->state.coll_floor_result_segment_id[idx] = segment_id;
  batch->state.coll_floor_result_contact_x[idx] = contact_x;
  batch->state.coll_floor_result_contact_y[idx] = contact_y;
  batch->state.coll_floor_result_normal_x[idx] = normal_x;
  batch->state.coll_floor_result_normal_y[idx] = normal_y;
}

static inline uint8_t action_allows_floor_edge_snap(uint16_t a) {
  // Decomp: mpColl_8004A45C_Floor (edge snap) is used by mpColl_8004B2DC (flags=2), which is
  // called by ft_800827A0. Motion states that use ft_80084104 (ft_800827A0) as their collision
  // callback include:
  // - DownAttack, DownForward, DownBack (DownRoll)
  // - EscapeF/EscapeB/EscapeN (rolls / spotdodge)
  // - Grounded attacks (Attack11..AttackLw4), including AttackDash and AttackS4S.
  // - Common AppealSR/SL through ftCo_AppealS_Coll -> ft_80084104.
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
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_AppealS_Coll
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

static inline MslStageFloorLine floor_line_world_for_env(const MslBatch* batch, int bi,
                                                         const MslStageFloorGraph* g,
                                                         int line_idx) {
  MslStageFloorLine out = {0};
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return out;
  }
  (void)stage_collision_floor_line_world(batch, bi, &g->lines[(size_t)line_idx], &out);
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

static inline uint8_t grounded_action_allows_platform_carry_y_correction(uint16_t action_id) {
  // Source-shaped owner split:
  // - stable grounded locomotion/guard/rooted-attack callbacks keep CollData.floor.index
  //   attached and call the common grounded floor persistence path;
  // - Landing/LandingAir callbacks own their root/ECB offset during landing entry and should not
  //   borrow the platform-carry root projection exception;
  // - damage/knockdown/passive floor-contact callbacks own their own projection/transition and
  //   must not borrow this platform-carry exception.
  //
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::ftCo_AttackDash_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackS4.c::ftCo_AttackS4_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Coll,ftCo_Guard_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084104,ft_800827A0}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B2DC
  switch (action_id) {
    case MSL_ACT_WAIT:
    case MSL_ACT_WALK_SLOW:
    case MSL_ACT_WALK_MIDDLE:
    case MSL_ACT_WALK_FAST:
    case MSL_ACT_TURN:
    case MSL_ACT_TURN_RUN:
    case MSL_ACT_DASH:
    case MSL_ACT_RUN:
    case MSL_ACT_RUN_DIRECT:
    case MSL_ACT_RUN_BRAKE:
    case MSL_ACT_SQUAT:
    case MSL_ACT_SQUAT_WAIT:
    case MSL_ACT_SQUAT_RV:
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD:
    case MSL_ACT_GUARD_OFF:
    case MSL_ACT_GUARD_SET_OFF:
    case MSL_ACT_GUARD_REFLECT:
      return 1u;
    default:
      break;
  }
  return (uint8_t)(action_id >= (uint16_t)MSL_ACT_ATTACK_11 &&
                   action_id <= (uint16_t)MSL_ACT_ATTACK_LW4);
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
    int projected_line_idx, uint16_t action_id) {
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
  if (grounded_action_allows_platform_carry_y_correction(action_id) &&
      (floor_line_is_generated_stage_slope(batch, bi, g, current_line_idx) ||
       floor_line_is_generated_stage_slope(batch, bi, g, projected_line_idx))) {
    // Yoshi's Story and FoD both expose admitted sloped floor segments in MSLSTG01. Grounded
    // persistence should keep the fighter attached through source graph traversal, including
    // slope-to-flat endpoint handoffs, rather than preserving a stale root height over raised lip
    // segments.
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    return 1u;
  }
  if (!grounded_action_allows_platform_carry_y_correction(action_id)) {
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

static int floor_dd90_project(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                              int line_idx, float x_in, float y_in, float* y_out, float* nx_out,
                              float* ny_out) {
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
    return stage_collision_floor_line_is_platform(stage_id, skip) ? skip : 0xFFFFu;
  }
  const uint16_t ground_id = batch->state.ground_id[idx];
  if (ground_id == 0xFFFFu || !stage_collision_floor_line_is_platform(stage_id, ground_id)) {
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

static inline uint8_t floor_line_is_skipped_platform(const MslStageFloorGraph* g, int line_idx,
                                                     uint16_t skip_segment_i) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count || skip_segment_i == 0xFFFFu) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  return (uint8_t)(line->is_platform && line->segment_i == skip_segment_i);
}

static inline uint8_t floor_line_is_runtime_fighter_solid(const MslStageFloorGraph* g,
                                                          uint32_t stage_id, int line_idx) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  return stage_collision_floor_line_is_runtime_fighter_solid(stage_id,
                                                             g->lines[(size_t)line_idx].segment_i);
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
  if (floor_line_is_skipped_platform(g, line_idx, skip_platform_segment_i)) {
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

static uint8_t floor_sweep_check(const MslBatch* batch, size_t idx, int bi,
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

static uint8_t floor_snap_to_line_edge_from_bottom(
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
  int out_line_idx =
      floor_dd90_project(batch, bi, g, line_idx, edge_x, edge_y, NULL, floor_nx_out, floor_ny_out);
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

static uint8_t floor_44628_wall_adjacent_fallback(
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
        floor_line_is_skipped_platform(g, line_idx, skip_platform_segment_i)) {
      continue;
    }
    float y_corr = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    const int projected_line_idx =
        floor_dd90_project(batch, bi, g, line_idx, cur_bottom_x, cur_bottom_y, &y_corr, &nx, &ny);
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
    if (!floor_sweep_check(batch, idx, bi, g, stage_id, prev_bottom_x, start_y[pass], cur_bottom_x,
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
  const int projected_line_idx = floor_dd90_project(
      batch, bi, g, accepted_line_idx, cur_bottom_x, cur_bottom_y, &y_corr,
      floor_nx_out != NULL ? floor_nx_out : &nx, floor_ny_out != NULL ? floor_ny_out : &ny);
  if (projected_line_idx < 0) {
    // Source immediately follows an accepted mpColl_8004A908_Floor with
    // mpColl_80044838_Floor, which endpoint-snaps the accepted floor even for hard floors.
    // Keep hard-floor endpoint snap scoped to this accepted disconnected-floor retry so ordinary
    // hard-floor off-end handling is not broadened.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004A908_Floor,mpColl_80044838_Floor}
    if (!floor_snap_to_line_edge_from_bottom(batch, bi, g, accepted_line_idx, cur_bottom_x,
                                             cur_bottom_y, 1u, ground_id_out, contact_x_out,
                                             contact_y_out, floor_nx_out, floor_ny_out)) {
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

static uint8_t escapeair_locked_platform_root_projection(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint16_t skip_platform_segment_i, uint8_t ecb_lock_timer_seed, float start_pose_bottom_rel_y,
    float current_pose_bottom_rel_y, float current_pose_top_rel_y, uint16_t* ground_id_out,
    float* contact_x_out, float* contact_y_out, float* floor_nx_out, float* floor_ny_out) {
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
    if (floor_line_is_skipped_platform(g, (int)li, skip_platform_segment_i)) {
      continue;
    }

    float y_corr = 0.0f;
    float nx = 0.0f;
    float ny = 1.0f;
    const int out_line_idx =
        floor_dd90_project(batch, bi, g, (int)li, root_x, root_y, &y_corr, &nx, &ny);
    if (out_line_idx < 0 || (size_t)out_line_idx >= g->line_count ||
        !g->lines[(size_t)out_line_idx].is_platform) {
      continue;
    }
    if (!floor_line_is_runtime_fighter_solid(g, stage_id, out_line_idx)) {
      continue;
    }
    if (floor_line_is_skipped_platform(g, out_line_idx, skip_platform_segment_i)) {
      continue;
    }
    float start_y_corr = 0.0f;
    const int start_line_idx =
        floor_dd90_project(batch, bi, g, out_line_idx, root_x, batch->state.prev_pos_y[idx],
                           &start_y_corr, NULL, NULL);
    if (start_line_idx < 0 ||
        g->lines[(size_t)start_line_idx].segment_i != g->lines[(size_t)out_line_idx].segment_i) {
      continue;
    }
    const uint8_t start_root_depth_owner =
        (start_y_corr >= min_lift && start_y_corr <= max_lift) ? 1u : 0u;
    const uint8_t current_root_depth_owner =
        // Same source callback owner as the start-root gate, but for rows where the platform line is
        // inside the current post-Phys EscapeAir ECB envelope while CollData_X130_Locked is still in
        // the handoff phase. Earlier lock frames remain with the interpolation-gap suppression path;
        // later same-platform pass-through frames are owned by the final floor publication guard.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
        (batch->state.action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
         batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
         ecb_lock_timer_seed == 5u && y_corr >= current_pose_bottom_rel_y &&
         y_corr <= current_pose_top_rel_y)
            ? 1u
            : 0u;
    if (!start_root_depth_owner && !current_root_depth_owner) {
      continue;
    }
    if (!found || y_corr < best_lift ||
        (y_corr == best_lift &&
         g->lines[(size_t)out_line_idx].segment_i < g->lines[(size_t)best_line_idx].segment_i)) {
      found = 1u;
      best_lift = y_corr;
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
      floor_sweep_check(batch, idx, bi, g, stage_id, prev_bot.x, prev_bot.y, cur_bot.x, cur_bot.y,
                        skip_platform_segment_i, -1, -1, c, &line_idx, &ix, &iy, NULL, NULL)) {
    float y_corr = 0.0f;
    const int out_line_idx =
        floor_dd90_project(batch, bi, g, line_idx, ix, cur_bot.y, &y_corr, NULL, NULL);
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
      const int out_line_idx =
          floor_dd90_project(batch, bi, g, (int)li, cur_bot.x, cur_bot.y, &y_corr, NULL, NULL);
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
  if (ground_id != 0xFFFFu) {
    line_idx = stage_collision_floor_line_index(stage_id, ground_id);
    if (!floor_line_is_runtime_fighter_solid(g, stage_id, line_idx)) {
      line_idx = -1;
    }
  }

  float y_corr = 0.0f;
  int out_line_idx = -1;
  if (line_idx >= 0) {
    out_line_idx = floor_dd90_project(batch, bi, g, line_idx, batch->state.pos_x[idx],
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
    out_line_idx = floor_dd90_project(batch, bi, g, (int)li, batch->state.pos_x[idx],
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

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t action_id = batch->state.action_id[idx];
      const uint16_t prev_action_id = batch->state.prev_action_id[idx];

      // Decomp: Fighter_procMap runs every frame (not gated by hitlag), and collision callbacks
      // such as ftCo_DamageFly_Coll internally select hitlag-specific mpColl paths when needed.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4

      // Match-flow actions use dedicated (or NULL) collision callbacks in decomp; this lite sim
      // skips the generic stage collision pass until those paths are implemented.
      // refs: src/match_flow.c::match_flow_should_stage_collide
      if (!match_flow_should_stage_collide(action_id)) {
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
      if (mpcoll_is_pending_throw_release_victim(batch, bi, p)) {
        continue;
      }

      const uint8_t was_grounded = batch->state.prev_on_ground[idx] ? 1u : 0u;
      const uint8_t ecb_lock_timer_seed = batch->state.ecb_lock_timer[idx];
      if (was_grounded && batch->state.ground_id[idx] != 0xFFFFu &&
          grounded_action_allows_platform_carry_y_correction(action_id)) {
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
      const uint8_t char_id = batch->state.char_id[idx];
      const uint32_t anim = batch->state.animation_index[idx];
      const uint16_t ecb_frame =
          msl_ecb_frame_u16_from_anim_frame(batch->state.anim_frame_f32[idx]);
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
      const float prev_x = batch->state.floor_sweep_prev_pos_x[idx];
      const float prev_y = batch->state.floor_sweep_prev_pos_y[idx];
      mpcoll_clear_callback_floor_result(batch, idx, prev_x, prev_y, x, y);

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
      const uint8_t spacie_air_special_floor_owner =
          is_spacie_air_special_floor_collision_action(action_id) && prev_action_id == action_id;
      const uint8_t common_air_collision_uses_locked_ecb_bottom =
          (ecb_lock_active && (is_attackair_action(action_id) ||
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
      if (!lock_bottom_to_zero && !damagefly_release_entry_uses_pose_bottom &&
          ((action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active) ||
           (is_damage_collision_landing_action(action_id) &&
            (ecb_lock_active || damage_collision_uses_seeded_lock_bottom)) ||
           (spacie_air_special_floor_owner && ecb_lock_active) ||
           (common_air_collision_uses_locked_ecb_bottom && ecb_lock_active))) {
        // ECB lock-bottom semantics while CollData_X130_Locked is active:
        // - ftCommon_8007D5D4 sets fp->ecb_lock and CollData_X130_Locked on ground->air transitions.
        // - mpColl_LoadECB_inline preserves desired_ecb.bottom while locked.
        // - EscapeAir, AttackAir, Damage/DamageFly, and the Fox/Falco aerial special callbacks above
        //   can also resolve grounded contact during this lock window.
        // - Shine narrows this to frame-start SpecialAirLwLoop/End owners; otherwise
        //   SpecialAirLwStart_Anim can change to Loop before collision and incorrectly inherit
        //   the loop/end floor-contact policy on the startup handoff frame.
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
        //   ftFx_SpecialAirNStart_Coll,ftFx_SpecialAirNLoop_Coll,ftFx_SpecialAirNEnd_Coll}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
        //   ftFx_SpecialAirLwStart_Anim,ftFx_SpecialAirLwLoop_Coll,ftFx_SpecialAirLwEnd_Coll}
        lock_bottom_to_zero = 1u;
      }
      uint8_t lock_bottom_to_prev_frame = 0u;
      if (!lock_bottom_to_zero && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
          batch->state.action_frame[idx] >= 0 && batch->state.action_frame[idx] <= 10 &&
          batch->state.speed_y_self[idx] <= 0.0f && prev_action_id != action_id) {
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
          mpcoll_pose_ecb_bottom_rel_y(char_id, anim, ecb_frame_cur, lock_bottom_to_zero);
      const float facing_dir_for_ecb = batch->state.facing[idx] ? 1.0f : -1.0f;
      MslEcbWorldPoints desired_ecb_points = {0};
      msl_ecb_world_points_sample(&desired_ecb_points, char_id, anim, ecb_frame_cur,
                                  facing_dir_for_ecb, x, y, lock_bottom_to_zero);
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
              : escapeair_jumpaerial_prev_ecb_lifetime;
      const float state_cur_ecb_rel =
          have_state_cur_ecb ? state_cur_ecb_points.bottom_rel_y : pose_prev_ecb_rel;
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
      const float prev_bottom_x = prev_bot.x;
      const float prev_bottom_y = prev_bot.y;
      MslEcbWorldPoints cur_ecb_points = desired_ecb_points;
      MslEcbWorldPoints prev_ecb_points = {0};
      if (use_hidden_ecb_lifetime && have_state_cur_ecb) {
        prev_ecb_points = state_cur_ecb_points;
      } else {
        msl_ecb_world_points_sample(&prev_ecb_points, char_id, anim, ecb_frame_prev,
                                    facing_dir_for_ecb, prev_x, prev_y, was_grounded);
      }
      const float prev_side_mid_y = prev_y + (0.5f * (prev_ecb_points.top_rel_y + prev_ecb_rel));

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

      int prefer_line_idx = -1;
      int raw_current_floor_line_idx = -1;
      if (ground_id != 0xFFFFu) {
        raw_current_floor_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
        prefer_line_idx = raw_current_floor_line_idx;
        if (!floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx)) {
          prefer_line_idx = -1;
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
      const uint16_t skip_platform_segment_i = platform_floor_skip_segment_id(batch, idx, stage_id);
      const uint8_t escapeair_locked =
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active) ? 1u : 0u;
      uint8_t deep_lock_penetration = 0u;

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
        mpcoll_grounded_wall_ceil_ordered_begin(batch, idx, &prev_ecb_points, &cur_ecb_points,
                                                &ordered_wall_ceil);
        cur_ecb_points = ordered_wall_ceil.cur_ecb_after;
        cur_bottom_x = cur_ecb_points.bottom_x;
        cur_bottom_y = cur_ecb_points.bottom_y;
      }

      if (was_grounded && prefer_line_idx >= 0) {
        float y_corr = 0.0f;
        const int out_line_idx = floor_dd90_project(batch, bi, g, prefer_line_idx, cur_bottom_x,
                                                    cur_bottom_y, &y_corr, &floor_nx, &floor_ny);
        if (out_line_idx >= 0) {
          const uint8_t keep_grounded_damage_hitlag_floor_snap =
              grounded_damage_hitlag_allows_downward_floor_projection(batch, idx, action_id);
          const uint8_t keep_capture_lw_floor_snap =
              is_capture_lw_allow_ground_to_air_collision_action(action_id);
          const uint8_t keep_slope_or_platform_floor_snap =
              grounded_persistence_allows_signed_dd90_y_correction(batch, bi, g, prefer_line_idx,
                                                                   out_line_idx, action_id);
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
              !keep_capture_lw_floor_snap && !keep_slope_or_platform_floor_snap) {
            y_corr = 0.0f;
          }
          int resolved_line_idx = out_line_idx;
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
                int out_line_idx2 = floor_dd90_project(batch, bi, g, prefer_line_idx, left_x,
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
                int out_line_idx2 = floor_dd90_project(batch, bi, g, prefer_line_idx, right_x,
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
            if (!landing_release_skip_floor_sweep && can_sweep &&
                floor_sweep_check(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                                  cur_bottom_x, cur_bottom_y, skip_platform_segment_i,
                                  prefer_line_idx, prefer_line_idx, c, &hit_line_idx, &ix, &iy,
                                  &floor_nx, &floor_ny)) {
              const uint8_t hit_line_is_platform =
                  (hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_platform) ? 1u : 0u;
              const uint8_t hit_line_has_platform_transform =
                  (hit_line_idx >= 0 && stage_collision_floor_line_has_platform_transform(
                                            stage_id, g->lines[(size_t)hit_line_idx].segment_i))
                      ? 1u
                      : 0u;
              // Decomp: desired_ecb.bottom.x is always 0.0, so clamping the ECB bottom contact X
              // corresponds to clamping the fighter position X.
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
              float y_corr2 = 0.0f;
              const int out_line_idx2 = floor_dd90_project(batch, bi, g, hit_line_idx, ix,
                                                           cur_bottom_y, &y_corr2, NULL, NULL);
              const uint16_t resolved_segment_i = (out_line_idx2 >= 0)
                                                      ? g->lines[(size_t)out_line_idx2].segment_i
                                                      : g->lines[(size_t)hit_line_idx].segment_i;
              const uint8_t resolved_line_has_platform_transform =
                  stage_collision_floor_line_has_platform_transform(stage_id, resolved_segment_i);
              const uint8_t resolved_line_is_platform =
                  (out_line_idx2 >= 0 && g->lines[(size_t)out_line_idx2].is_platform) ? 1u : 0u;
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
              const uint8_t specialhi_fall_understage_hard_floor_clip =
                  // SpecialHiFall_Coll reaches ft_CheckGroundAndLedge only after the launch/fall
                  // collision substrate has kept CollData outside the stage shell. If the previous
                  // root is still below the accepted hard floor by more than the live ECB
                  // neighborhood, this is not a legitimate top-surface landing; it is the sim having
                  // missed the earlier wall/ceiling owner and then accepting the floor from inside
                  // the stage on descent.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
                  (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL && !hit_line_is_platform &&
                   !resolved_line_is_platform &&
                   prev_y < (iy - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                             k_floor_y_bias))
                      ? 1u
                      : 0u;
              const float damage_root_down_delta = prev_y - y;
              const uint8_t damage_root_tracks_self_descent =
                  (damage_root_down_delta >=
                   (fabsf(batch->state.speed_y_self[idx]) - k_floor_horiz_dy_thresh))
                      ? 1u
                      : 0u;
              const uint8_t suppress_damage_transformed_platform_ecb_only_land =
                  // Damage/DamageFly platform contact is owned by ftCo_Damage*_Coll -> ft_80081DD4
                  // and mpCheckFloor's live CollData ECB sweep. Do not key this on residual KB sign:
                  // legal-stage platform landings can retain positive attack-Y while the ECB bottom
                  // descends through the floor. The retained rejection is the FoD transformed-platform
                  // boundary where the platform hit is dominated by ECB-bottom interpolation rather
                  // than root descent.
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
                  // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpCheckFloor}
                  ((hit_line_has_platform_transform || resolved_line_has_platform_transform) &&
                   is_damage_collision_landing_action(action_id) &&
                   batch->state.hitstun[idx] != 0u && batch->state.speed_y_attack[idx] > 0.0f &&
                   !damage_root_tracks_self_descent)
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
                         specialhi_fall_understage_hard_floor_clip ||
                         suppress_damage_transformed_platform_ecb_only_land ||
                         suppress_fallspecial_b_transformed_platform_skip) {
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
                       floor_44628_wall_adjacent_fallback(
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
        const uint8_t damage_hitlag_exit_projection_owner =
            (is_damage_collision_landing_action(action_id) &&
             batch->state.hitlag_pre_timer[idx] != 0u && batch->state.hitlag[idx] == 0u &&
             batch->state.damage_hitlag_floorhug_latch[idx] != 0u)
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
             prefer_line_idx >= 0 && damage_hitlag_floorhug_attempts_downward_sdi(batch, idx, c))
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
        const uint8_t escapeair_jump_platform_root_owner =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active &&
             (prev_action_id == (uint16_t)MSL_ACT_JUMP_F ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_B ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              prev_action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B))
                ? 1u
                : 0u;
        const int escapeair_callback_pose_frame =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.seed_prev_action_frame[idx] >= 0)
                ? ((int)batch->state.seed_prev_action_frame[idx] + 1)
                : batch->state.action_frame[idx];
        if (!on_ground && escapeair_locked &&
            (ecb_lock_timer >= 4u || escapeair_jump_platform_root_owner) &&
            (batch->state.speed_y_self[idx] < 0.0f || escapeair_jump_platform_root_owner) &&
            escapeair_locked_platform_root_projection(
                batch, idx, bi, g, stage_id, skip_platform_segment_i, ecb_lock_timer_seed,
                msl_ecb_bottom_rel_y(char_id, anim, (int)ecb_frame_cur),
                msl_ecb_bottom_rel_y(char_id, anim, escapeair_callback_pose_frame),
                msl_ecb_top_rel_y(char_id, anim, escapeair_callback_pose_frame), &ground_id,
                &contact_x, &contact_y, &floor_nx, &floor_ny)) {
          on_ground = 1u;
        }
        if (!on_ground && damage_hitlag_exit_projection_owner && prefer_line_idx >= 0) {
          // Damage hitlag-exit callback ownership:
          // - Damage entry writes `post_hitlag_cb = ftCo_Damage_OnExitHitlag`.
          // - Fighter_8006D10C invokes that callback on hitlag exit before Damage collision callback.
          // - DamageFly_Coll / Damage_Coll then resolve grounded contact via ft_80081DD4.
          // - In the non-allow_sdi air callback path, mpColl_80046904 uses
          //   mpColl_80044838_Floor(ignore_bottom=true) when `ecb.bottom.y > 0`, so the landing
          //   snap projects from root pos rather than the ECB bottom.
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
          const int out_line_idx = floor_dd90_project(batch, bi, g, prefer_line_idx, proj_x, proj_y,
                                                      &y_corr, &floor_nx, &floor_ny);
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
        if (!on_ground && (active_damage_hitlag_stay_airborne_floor_owner ||
                           active_damage_thrown_release_floor_owner)) {
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
          const int out_line_idx = floor_dd90_project(batch, bi, g, prefer_line_idx, proj_x, proj_y,
                                                      &y_corr, &floor_nx, &floor_ny);
          if (out_line_idx >= 0 && y_corr >= 0.0f &&
              // Keep this bridge off ledge floor segments. mpColl edge/ledge suppression is owned
              // by separate floor-edge helpers, and replay-real AGN 4839/4840 stay airborne below
              // the FD ledge floor despite diagonal down input during DamageFlyTop hitlag.
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A45C_Floor
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046904
              !g->lines[(size_t)out_line_idx].is_ledge) {
            batch->state.pos_y[idx] += y_corr;
            batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_FLOOR_MASK;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = proj_x;
            contact_y = proj_y + y_corr;
            active_damage_hitlag_airborne_floor_contact = 1u;
            batch->state.damage_hitlag_floorhug_latch[idx] = 1u;
          } else if (prefer_line_idx >= 0 && !g->lines[(size_t)prefer_line_idx].is_ledge) {
            // Same active-hitlag owner, root fallback:
            // mpColl_80044948_Floor can use the fighter root when the loaded ECB bottom is not the
            // usable floor-contact point. Keep the fallback inside the already-proven
            // OnEveryHitlag/downward-SDI owner and require the persisted floor line to be a
            // non-ledge segment containing the root X. Ledge rows remain excluded by the control
            // above.
            // refs/melee/src/melee/mp/mpcoll.c::{
            //   mpColl_80044948_Floor,mpColl_80046904}
            float root_y_corr = 0.0f;
            const int root_line_idx =
                floor_dd90_project(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                   batch->state.pos_y[idx], &root_y_corr, &floor_nx, &floor_ny);
            if (root_line_idx >= 0 && root_y_corr >= 0.0f &&
                !g->lines[(size_t)root_line_idx].is_ledge &&
                floor_x_within_line_bounds(batch, bi, g, root_line_idx, batch->state.pos_x[idx])) {
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

          damageflyroll_root_proj_line_idx = floor_dd90_project(
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
          // Keep this bridge limited to rows with frame-start prev-ECB-bottom penetration and
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
              floor_dd90_project(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
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
              floor_dd90_project(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
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
          const int out_line_idx = floor_dd90_project(batch, bi, g, prefer_line_idx, cur_bottom_x,
                                                      cur_bottom_y, &y_corr, &floor_nx, &floor_ny);
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
                floor_dd90_project(batch, bi, g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                   &y_corr, &floor_nx, &floor_ny);
            const float ledge_projection_depth_limit = bottom_rel0 + fabsf(y - prev_y);
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
                       !suppress_jumpaerial_entry_shallow_ledge_projection &&
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
              floor_dd90_project(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                 batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          const float max_lift =
              fabsf(batch->state.speed_y_self[idx]) + (2.0f * k_ecb_vertical_unit);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
          }
        }
        const uint8_t escapeair_anim_end_fallspecial_fastfall_entry =
            (action_id == (uint16_t)MSL_ACT_FALL_SPECIAL &&
             prev_action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
             batch->state.action_frame[idx] <= 0 && batch->state.fall_fast[idx] != 0u &&
             batch->state.speed_y_self[idx] < 0.0f)
                ? 1u
                : 0u;
        if (!on_ground && is_common_fallspecial_action(action_id) && prefer_line_idx >= 0 &&
            batch->state.speed_y_self[idx] < 0.0f && batch->state.prev_action_frame[idx] >= 4 &&
            !escapeair_anim_end_fallspecial_fastfall_entry) {
          // FallSpecial_Coll uses ft_80083090 and enters LandingFallSpecial through
          // ftCo_80096D28 when the floor callback accepts. One-step reseeds lack mpColl's full
          // previous ECB segment after the freefall entry, so use a root projection for shallow
          // same-floor penetration on the post-entry landing window. Cap the projection by the
          // current downward velocity to avoid pulling blast-zone FallSpecial rows up to the stage.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
          //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80083090
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float y_corr = 0.0f;
          const int out_line_idx =
              floor_dd90_project(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                 batch->state.pos_y[idx], &y_corr, &floor_nx, &floor_ny);
          const float max_lift =
              fabsf(batch->state.speed_y_self[idx]) + (2.0f * k_ecb_vertical_unit);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= max_lift) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = batch->state.pos_x[idx];
            contact_y = batch->state.pos_y[idx];
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
              floor_dd90_project(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
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
        if (!on_ground && !active_damage_hitlag_airborne_floor_contact && can_sweep &&
            floor_sweep_check(batch, idx, bi, g, stage_id, prev_bottom_x, prev_bottom_y,
                              cur_bottom_x, cur_bottom_y, skip_platform_segment_i, prefer_line_idx,
                              -1, c, &hit_line_idx, &ix, &iy, &floor_nx, &floor_ny)) {
          const uint8_t hit_line_is_platform =
              (hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_platform) ? 1u : 0u;
          const uint8_t hit_line_is_slope =
              floor_line_is_generated_stage_slope(batch, bi, g, hit_line_idx);
          const uint8_t hit_line_is_platform_or_slope =
              (uint8_t)((hit_line_is_platform || hit_line_is_slope) ? 1u : 0u);
          const uint8_t hit_line_has_platform_transform =
              (hit_line_idx >= 0 && stage_collision_floor_line_has_platform_transform(
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
                ecb_lock_timer >= 5u))
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
          const uint8_t escapeair_locked_deep_platform_floor_handoff =
              (escapeair_locked && hit_line_idx >= 0 &&
               (hit_line_is_platform_or_slope || g->lines[(size_t)hit_line_idx].is_platform) &&
               hit_line_x_in_strict_segment && batch->state.action_frame[idx] == 3 &&
               (iy - cur_bottom_y) >= msl_ecb_bottom_rel_y(char_id, anim, 0))
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_ledge_land =
              (escapeair_locked && !deep_lock_penetration && hit_line_idx >= 0 &&
               g->lines[(size_t)hit_line_idx].is_ledge && !escapeair_sustained_floor_handoff &&
               !escapeair_kneebend_entry_floor_handoff && !escapeair_jump_entry_floor_handoff)
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
              (escapeair_locked && !deep_lock_penetration &&
               !escapeair_locked_deep_platform_floor_handoff && hit_line_idx >= 0 &&
               !g->lines[(size_t)hit_line_idx].is_ledge &&
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
          const uint8_t suppress_escapeair_jump_entry_platform_from_below =
              // JumpAerialF -> EscapeAir entry can carry a locked previous bottom into a static
              // soft-platform line before the source EscapeAir_Coll callback publishes stable floor
              // ownership. Keep this off transformed platforms (covered by explicit positive locks)
              // and JumpAerialB rows, which land through the ordinary callback in replay-real locks.
              // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
              (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && hit_line_is_platform &&
               !hit_line_has_platform_transform &&
               batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
               batch->state.action_frame[idx] <= 3 && ecb_lock_active &&
               batch->state.seed_prev_action_frame[idx] <= 6)
                  ? 1u
                  : 0u;
          const uint8_t suppress_jumpaerial_escapeair_entry_land =
              (escapeair_fresh_jumpaerial_entry_lock && hit_line_is_platform_or_slope) ? 1u : 0u;
          const uint8_t suppress_fallspecial_entry_af3_land =
              (is_common_fallspecial_action(action_id) &&
               batch->state.prev_action_frame[idx] == 3 && batch->state.speed_y_self[idx] < 0.0f)
                  ? 1u
                  : 0u;
          const uint8_t suppress_escapeair_anim_end_fallspecial_fastfall_land =
              // EscapeAir_Anim can enter FallSpecial through ftCo_80096900 after the EscapeAir
              // timeline ends. That entry uses Ft_MF_KeepFastFall and publishes the FallSpecial
              // entry row with the pre-map fastfall descent before the following FallSpecial_Coll
              // frame owns LandingFallSpecial.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Anim
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
              //   inline0,ftCo_80096900,ftCo_FallSpecial_Coll}
              escapeair_anim_end_fallspecial_fastfall_entry;
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
              // root-crossing frame and let the next below-floor Fall_Coll frame land. Do not key
              // this on character id: Fox/Falco controls are distinguished by extracted
              // ftCo_DatAttrs::model_scaling in `data/characters/*.json`.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
              // refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale
              // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_LoadECB_inline,mpCollInterpolateECB,mpColl_80044628_Floor}
              (fall_ledge_floor_uses_expanded_collision_model &&
               action_id == (uint16_t)MSL_ACT_FALL && prev_action_id == (uint16_t)MSL_ACT_FALL &&
               hit_line_idx >= 0 && g->lines[(size_t)hit_line_idx].is_ledge &&
               batch->state.action_frame[idx] == 3 && batch->state.speed_y_self[idx] < 0.0f &&
               prev_y > k_floor_y_bias && batch->state.pos_y[idx] < 0.0f)
                  ? 1u
                  : 0u;
          const uint8_t suppress_damageflyroll_shallow_land =
              (damageflyroll_iasa_lockout && !damageflyroll_deep_side_penetration &&
               damageflyroll_root_proj_ready &&
               damageflyroll_root_proj_y_corr < damageflyroll_side_y_thresh)
                  ? 1u
                  : 0u;
          // Active-hitlag damage rows can receive Damage_OnEveryHitlag SDI/ASDI before collision,
          // but the Damage motion state remains airborne until the hitlag-exit/collision handoff.
          // Resolve root penetration for visual/collision stability without setting
          // ground_or_air=Ground during the frozen frame.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
          //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll,ftCo_DamageFly_Coll}
          // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
          // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpCheckFloor,mpColl_800477E0}
          const uint8_t suppress_active_damage_hitlag_land =
              (batch->state.hitlag[idx] != 0u && is_damage_collision_landing_action(action_id))
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
               (ecb_lock_active ||
                (prev_action_id == action_id &&
                 g->lines[(size_t)hit_line_idx].segment_i != batch->state.ground_id[idx])))
                  ? 1u
                  : 0u;
          uint8_t carried_platform_id = 0xFFu;
          const uint8_t carried_moving_platform_transform =
              (batch->state.ground_id[idx] != 0xFFFFu &&
               stage_collision_floor_line_platform_transform_id(
                   stage_id, batch->state.ground_id[idx], &carried_platform_id) &&
               carried_platform_id < 2u)
                  ? 1u
                  : 0u;
          const uint8_t carried_transformed_platform_skip =
              // Backward-compatible seed path for hidden CollData.floor_skip after EscapeAir
              // continues a transformed-platform pass-through. Runtime writes the explicit
              // floor_skip lane when Pass/shield-drop owns the episode. Replay/eval rows seed
              // hidden floor_skip explicitly; this fallback only covers the early EscapeAir carried
              // same-platform continuation before the runtime lane is observable.
              // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{
              //   ftCo_8009A184,ftCo_8009A228}
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
              (hit_line_is_platform && hit_line_has_platform_transform && c != NULL &&
               batch->state.on_ground[idx] == 0u &&
               stick_i8_to_unit(batch->state.input_main_y[idx]) <=
                   c->platform_air_land_stick_y_threshold &&
               ((action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && carried_moving_platform_transform &&
                 stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
                     c->platform_air_land_stick_y_threshold &&
                 batch->state.action_frame[idx] <= 3)))
                  ? 1u
                  : 0u;
          float proj_x = 0.0f;
          float proj_y = 0.0f;
          stay_airborne_floor_projection_point(batch->state.pos_x[idx], batch->state.pos_y[idx],
                                               cur_bottom_x, cur_bottom_y, &proj_x, &proj_y);
          float y_corr = 0.0f;
          const int out_line_idx =
              floor_dd90_project(batch, bi, g, hit_line_idx, proj_x, proj_y, &y_corr, NULL, NULL);
          const uint16_t projected_segment_i =
              (out_line_idx >= 0) ? g->lines[(size_t)out_line_idx].segment_i : 0xFFFFu;
          const uint8_t projected_line_has_platform_transform =
              stage_collision_floor_line_has_platform_transform(stage_id, projected_segment_i);
          const uint8_t suppress_downheld_transformed_platform_land =
              // Soft-platform pass-through belongs to the collision callbacks that actually pass
              // ftCo_80096CC8. AttackAir_Coll, EscapeAir_Coll, and DamageFly_Coll use separate floor
              // callbacks, so held down must not make active aerials, air dodges, or damage/tumble
              // fall through FoD's transformed platform lines.
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
               carried_transformed_platform_skip)
                  ? 1u
                  : 0u;
          const float damage_root_down_delta = prev_y - y;
          const uint8_t damage_root_tracks_self_descent =
              (damage_root_down_delta >=
               (fabsf(batch->state.speed_y_self[idx]) - k_floor_horiz_dy_thresh))
                  ? 1u
                  : 0u;
          const uint8_t suppress_damage_transformed_platform_ecb_only_land =
              // Damage/DamageFly transformed-platform contact consumes the same live CollData ECB
              // sweep as hard floors. Positive attack-Y is not a pass-through predicate by itself;
              // suppress only the table-backed transformed-platform boundary where the apparent
              // crossing is ECB-bottom interpolation rather than root descent.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpCheckFloor}
              ((hit_line_has_platform_transform || projected_line_has_platform_transform) &&
               is_damage_collision_landing_action(action_id) && batch->state.hitstun[idx] != 0u &&
               batch->state.speed_y_attack[idx] > 0.0f && !damage_root_tracks_self_descent)
                  ? 1u
                  : 0u;
          if (suppress_locked_ledge_land || suppress_locked_off_end_platform_land ||
              suppress_locked_seed6_platform_land || suppress_locked_vertical_af3_land ||
              suppress_escapeair_no_lock_vertical_af3_land ||
              suppress_seeded_escapeair_first_locked_land || suppress_fallspecial_entry_af3_land ||
              suppress_escapeair_jump_entry_platform_from_below ||
              suppress_jumpaerial_escapeair_entry_land ||
              suppress_escapeair_anim_end_fallspecial_fastfall_land ||
              suppress_fall_ledge_floor_first_root_crossing ||
              escapeair_sustained_floorhug_airborne ||
              escapeair_fresh_horizontal_floorhug_airborne || specialhi_bound_entry_airborne ||
              suppress_damageflyroll_shallow_land || suppress_damageair_attackair_entry_land ||
              suppress_locomotion_attackair_entry_platform_land ||
              suppress_escapeair_entry_locked_platform_land ||
              suppress_downheld_transformed_platform_land ||
              suppress_damage_transformed_platform_ecb_only_land) {
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
                  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
                  // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
                  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
                  (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                   batch->state.action_frame[idx] <= 4 && resolved_line_has_platform_transform &&
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
                  // Same hard-floor guard as the direct sweep path: a SpecialHiFall projection
                  // whose previous root is already below the accepted hard floor by more than the
                  // live ECB neighborhood is recovering from a missed underside collision, not source
                  // `ft_CheckGroundAndLedge` ownership.
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
                  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
                  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
                  (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL &&
                   !stage_collision_floor_line_is_platform(stage_id, resolved_segment_i) &&
                   prev_y <
                       (proj_y - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                        k_floor_y_bias))
                      ? 1u
                      : 0u;
              const float escapeair_entry_bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
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
                         suppress_projected_jumpaerial_escapeair_shallow_ledge_land ||
                         suppress_projected_specialhi_transformed_platform_land ||
                         suppress_projected_specialhi_understage_hard_floor_clip) {
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
                  floor_snap_to_line_edge_from_bottom(batch, bi, g, hit_line_idx, cur_bottom_x,
                                                      cur_bottom_y, 0u, &ground_id, &contact_x,
                                                      &contact_y, &floor_nx, &floor_ny)) {
                batch->state.pos_x[idx] += (contact_x - cur_bottom_x);
                batch->state.pos_y[idx] += (contact_y - cur_bottom_y);
                on_ground = 1;
              } else if (downdamage_active_hitlag_resting_floor_contact) {
                float root_y_corr = 0.0f;
                const int root_line_idx =
                    floor_dd90_project(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                       batch->state.pos_y[idx], &root_y_corr, &floor_nx, &floor_ny);
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
                floor_dd90_project(batch, bi, g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
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
              const int root_line_idx =
                  floor_dd90_project(batch, bi, g, prefer_line_idx, batch->state.pos_x[idx],
                                     batch->state.pos_y[idx], &root_y_corr, &floor_nx, &floor_ny);
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
        (void)mpcoll_grounded_ceiling_ordered_retry(batch, idx, &prev_ecb_points, &post_floor_ecb,
                                                    &ordered_wall_ceil);
        cur_ecb_points = ordered_wall_ceil.cur_ecb_after;
        cur_bottom_x = cur_ecb_points.bottom_x;
        cur_bottom_y = cur_ecb_points.bottom_y;
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
          mpcoll_record_callback_floor_result(batch, idx,
                                              (uint8_t)MSL_MPCOLL_FLOOR_RESULT_GROUNDED_4A908_RETRY,
                                              ground_id, contact_x, contact_y, floor_nx, floor_ny);
          on_ground = 1u;
        }
      }

      if (on_ground) {
        const uint8_t resolved_line_has_platform_transform =
            stage_collision_floor_line_has_platform_transform(stage_id, ground_id);
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
             ground_id != batch->state.ground_id[idx] &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_F &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_B &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
             batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_JUMP_AERIAL_B)
                ? 1u
                : 0u;
        const uint8_t suppress_specialhi_transformed_platform_land =
            // Fox/Falco up-special collision callbacks own transformed-platform bound/landing
            // timing. Keep locked SpecialAirHi and remapped SpecialHiFall transformed-platform
            // contacts airborne here; hard floors and same-floor followups retain the normal result.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialHi_Coll,ftFx_SpecialHiFall_Coll,ftFx_SpecialHiBound_Coll}
            (resolved_line_has_platform_transform &&
             ((action_id == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI && ecb_lock_active) ||
              (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL &&
               ground_id != batch->state.ground_id[idx])))
                ? 1u
                : 0u;
        const uint8_t suppress_specialhi_understage_hard_floor_land =
            // Final writeback guard for SpecialHiFall hard-floor clips from inside the stage. Some
            // floor result paths can set `on_ground` after the direct/projection suppression sites;
            // if the previous root was below the accepted hard floor by more than the live ECB
            // neighborhood, source collision should have consumed the stage underside before
            // `ft_CheckGroundAndLedge` could publish a landing.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
            //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Coll}
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044C74_Ceiling}
            (action_id == (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             prev_y < (contact_y - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                       k_floor_y_bias))
                ? 1u
                : 0u;
        const uint8_t suppress_jump_transformed_platform_pre_handoff_land =
            // Jump_Coll routes through ft_800835B0 with the soft-platform callback. During upward
            // JumpF/JumpB motion, transformed-platform sweeps can observe the platform before the
            // source callback publishes the landing handoff; descending rows land normally.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            (resolved_line_has_platform_transform &&
             (action_id == (uint16_t)MSL_ACT_JUMP_F || action_id == (uint16_t)MSL_ACT_JUMP_B) &&
             batch->state.speed_y_self[idx] > k_floor_horiz_dy_thresh)
                ? 1u
                : 0u;
        const uint8_t suppress_jumpaerial_transformed_platform_fastfall_land =
            // Sustained JumpAerial_Coll uses the same ft_800835B0 -> mpColl_80047E14 callback
            // family, but transformed-platform fastfall rows can observe a platform sweep before
            // the callback-local floor result is published as a landing. Keep this to same-action
            // fastfall rows on transformed platform lines; fresh JumpAerial entry and hard-floor
            // contacts remain on the normal landing path.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_80084DB0
            // refs/melee/src/melee/ft/ft_081B.c::ft_800835B0
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80047E14}
            (resolved_line_has_platform_transform &&
             (action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              action_id == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             prev_action_id == action_id && batch->state.fall_fast[idx] != 0u &&
             jumpaerial_terminal_fastfall_descent(batch, idx))
                ? 1u
                : 0u;
        const uint8_t suppress_fall_transformed_platform_fastfall_land =
            // Fall_Coll routes through ft_800831CC -> mpColl_80047E14 with the same
            // ftCo_80096CC8 platform callback as Jump/JumpAerial. FoD transformed-platform rows can
            // see a remapped platform sweep before the source callback publishes a landing result;
            // preserve the airborne Fall frame for sustained same-action fastfall contacts only.
            // Fresh Fall entries and hard-floor contacts still use the normal landing path.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Coll
            // refs/melee/src/melee/ft/ft_081B.c::ft_800831CC
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80047E14}
            (resolved_line_has_platform_transform && action_id == (uint16_t)MSL_ACT_FALL &&
             prev_action_id == action_id && batch->state.fall_fast[idx] != 0u)
                ? 1u
                : 0u;
        const float final_landing_lift = contact_y - cur_bottom_y;
        const float escapeair_entry_bottom_rel0 = msl_ecb_bottom_rel_y(char_id, anim, 0);
        const int final_ground_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
        const uint8_t final_ground_line_is_ledge =
            (final_ground_line_idx >= 0 && (size_t)final_ground_line_idx < g->line_count &&
             g->lines[(size_t)final_ground_line_idx].is_ledge)
                ? 1u
                : 0u;
        const int seed_ground_line_idx_for_final =
            stage_collision_floor_line_index(stage_id, seed_ground_id);
        const uint8_t seed_ground_line_is_ledge =
            (seed_ground_line_idx_for_final >= 0 &&
             (size_t)seed_ground_line_idx_for_final < g->line_count &&
             g->lines[(size_t)seed_ground_line_idx_for_final].is_ledge)
                ? 1u
                : 0u;
        const uint8_t suppress_jumpaerial_escapeair_shallow_ledge_final_land =
            // Final guard for fresh JumpAerial -> EscapeAir ledge remaps that can be accepted by
            // multiple projection paths above. Source only publishes LandingFallSpecial once the
            // accepted ledge snap is deep enough for the entered EscapeAir ECB; shallower
            // persisted-floor remaps remain airborne.
            // data/moves/{fox,falco}.json::ftCo_SM_EscapeAir ECB frame 0 bottom
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
            // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && batch->state.action_frame[idx] <= 3 &&
             (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
              batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
             batch->state.seed_prev_action_frame[idx] <= 4 && ground_id != seed_ground_id &&
             !seed_ground_line_is_ledge &&
             !stage_collision_floor_line_is_platform(stage_id, ground_id) &&
             final_ground_line_is_ledge && final_landing_lift >= 0.0f &&
             final_landing_lift < (escapeair_entry_bottom_rel0 - k_floor_y_bias))
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
        if (suppress_escapeair_transformed_remap_land ||
            suppress_specialhi_transformed_platform_land ||
            suppress_specialhi_understage_hard_floor_land ||
            suppress_jump_transformed_platform_pre_handoff_land ||
            suppress_jumpaerial_transformed_platform_fastfall_land ||
            suppress_fall_transformed_platform_fastfall_land ||
            suppress_sustained_escapeair_same_platform_lock_land ||
            suppress_jumpaerial_escapeair_shallow_ledge_final_land) {
          if (suppress_specialhi_understage_hard_floor_land) {
            // Keep the rejected root outside the same live ECB neighborhood; otherwise the next frame
            // can re-accept the same inside-stage floor.
            batch->state.pos_y[idx] =
                contact_y - specialhi_understage_floor_reject_clearance(&prev_ecb_points) -
                k_floor_y_bias;
          } else if (suppress_jumpaerial_escapeair_shallow_ledge_final_land) {
            batch->state.pos_y[idx] = cur_bottom_y - cur_bot.rel_y;
          } else if (suppress_sustained_escapeair_same_platform_lock_land) {
            batch->state.pos_y[idx] = cur_bottom_y - cur_bot.rel_y;
          }
          on_ground = 0u;
          ground_id = batch->state.ground_id[idx];
          contact_x = cur_bottom_x;
          contact_y = cur_bottom_y;
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
        on_ground = 0u;
        ground_id = seed_ground_id;
        batch->state.pos_y[idx] = y;
        contact_x = cur_bottom_x;
        contact_y = cur_bottom_y;
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
        on_ground = 0u;
        ground_id = seed_ground_id;
        batch->state.pos_y[idx] = y;
        contact_x = cur_bottom_x;
        contact_y = cur_bottom_y;
      }

      if (on_ground && batch->state.coll_floor_result_valid[idx] == 0u) {
        // Source mpColl keeps the accepted floor result local to the callback before the wrapper
        // consumes it. Mirror that result for ordinary direct floor hits too; the 4A908 retry above
        // is the retained source-clear consumer that requires this scratch lane.
        // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044628_Floor}
        mpcoll_record_callback_floor_result(batch, idx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT,
                                            ground_id, contact_x, contact_y, floor_nx, floor_ny);
      } else if (!on_ground && batch->state.coll_floor_result_valid[idx] != 0u) {
        batch->state.coll_floor_result_valid[idx] = 0u;
        batch->state.coll_floor_result_source[idx] = (uint8_t)MSL_MPCOLL_FLOOR_RESULT_NONE;
        batch->state.coll_floor_result_segment_id[idx] = 0xFFFFu;
      }

      batch->state.on_ground[idx] = on_ground;
      if (on_ground) {
        if (batch->state.coll_floor_result_valid[idx] != 0u) {
          ground_id = batch->state.coll_floor_result_segment_id[idx];
          contact_x = batch->state.coll_floor_result_contact_x[idx];
          contact_y = batch->state.coll_floor_result_contact_y[idx];
          floor_nx = batch->state.coll_floor_result_normal_x[idx];
          floor_ny = batch->state.coll_floor_result_normal_y[idx];
        }
        // Decomp: floor collision sets Collide_FloorPush (+ sometimes FloorHug).
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_80046F78
        batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_FLOOR_MASK;

        floor_write_edge_suppression_flags(batch, idx, stage_id, g,
                                           stage_collision_floor_line_index(stage_id, ground_id),
                                           char_id, anim, ecb_frame, was_grounded);

        batch->state.ground_id[idx] = ground_id;
        batch->state.ground_normal_x[idx] = floor_nx;
        batch->state.ground_normal_y[idx] = floor_ny;
        batch->state.ground_contact_x[idx] = contact_x;
        batch->state.ground_contact_y[idx] = contact_y;

      } else {
        if (action_is_down_bound(action_id) && prefer_line_idx >= 0) {
          // DownBound_Coll uses the allow-ground-to-air mpColl path: the fighter can remain
          // airborne while CollData's floor.index updates across connected floor seams. Preserve
          // airborne `ground_or_air`, but refresh the persisted floor id when DD90 projection can
          // resolve the current bottom point.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
          // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          const int out_line_idx = floor_dd90_project(batch, bi, g, prefer_line_idx, cur_bottom_x,
                                                      cur_bottom_y, NULL, NULL, NULL);
          if (out_line_idx >= 0) {
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
          }
        }
        // Keep ground_id stable while airborne (CollData floor.index persists while in air).
        // refs/melee/src/melee/lb/types.h::CollData
        batch->state.ground_id[idx] = ground_id;
        batch->state.ground_normal_x[idx] = 0.0f;
        batch->state.ground_normal_y[idx] = 1.0f;
        batch->state.ground_contact_x[idx] = 0.0f;
        batch->state.ground_contact_y[idx] = 0.0f;
      }
      // Promote CollData ECB lifetime for rollout. Source interpolation copies ecb to prev_ecb
      // before stepping toward desired_ecb; after this single-step callback pass, carry the resolved
      // desired ECB as the next current ECB.
      // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
      if (have_state_cur_ecb) {
        mpcoll_store_prev_ecb_points(batch, idx, &state_cur_ecb_points);
      } else {
        mpcoll_store_prev_ecb_points(batch, idx, &prev_ecb_points);
      }
      mpcoll_store_current_ecb_points(batch, idx, &cur_ecb_points);
      mpcoll_store_desired_ecb_points(batch, idx, &desired_ecb_points);
      batch->state.coll_prev_ecb_bottom_valid[idx] = 1u;
      batch->state.coll_ecb_bottom_valid[idx] = 1u;
      batch->state.coll_desired_ecb_bottom_valid[idx] = 1u;
    }
  }
}
