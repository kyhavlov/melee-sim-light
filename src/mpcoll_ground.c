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

static inline uint8_t action_allows_floor_edge_snap(uint16_t a) {
  // Decomp: mpColl_8004A45C_Floor (edge snap) is used by mpColl_8004B2DC (flags=2), which is
  // called by ft_800827A0. Multiple grounded motion states use ft_80084104 (which calls
  // ft_800827A0) as their collision callback, including:
  // - Down* (except DownBound, which uses ft_80082708 -> mpColl_8004B108)
  // - EscapeF/EscapeB/EscapeN (rolls / spotdodge)
  // - Grounded attacks (Attack11..AttackLw4), including AttackDash and AttackS4S.
  // - Common AppealSR/SL through ftCo_AppealS_Coll -> ft_80084104.
  // - Fox/Falco grounded SpecialSEnd, whose collision callback uses ft_800827A0 after the main
  //   Side-B travel phase has already converted through ft_80082708 when floor is lost.
  // - PassiveStandF/B tech-roll grounded continuation.
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
    case MSL_ACT_DOWN_WAIT_U:
    case MSL_ACT_DOWN_STAND_U:
    case MSL_ACT_DOWN_ATTACK_U:
    case MSL_ACT_DOWN_FOWARD_U:
    case MSL_ACT_DOWN_BACK_U:
    case MSL_ACT_DOWN_WAIT_D:
    case MSL_ACT_DOWN_STAND_D:
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

static inline uint8_t floor_x_within_line_bounds(const MslStageFloorGraph* g, int line_idx,
                                                 float x) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine* l = &g->lines[(size_t)line_idx];
  const float min_x = (l->x0 < l->x1) ? l->x0 : l->x1;
  const float max_x = (l->x0 > l->x1) ? l->x0 : l->x1;
  return (uint8_t)((x >= (min_x - k_floor_x_end_clamp) && x <= (max_x + k_floor_x_end_clamp)) ? 1u
                                                                                              : 0u);
}

static void floor_ed5c_endpoints(const MslStageFloorGraph* g, int line_idx, float* x0_out,
                                 float* y0_out, float* x1_out, float* y1_out) {
  // Decomp: mpLib_8004ED5C expands endpoints by 1 unit for connected endpoints.
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
  const MslStageFloorLine* l = &g->lines[line_idx];
  float x0 = l->x0;
  float y0 = l->y0;
  float x1 = l->x1;
  float y1 = l->y1;

  float dist = 0.0f;
  uint8_t have_dist = 0;
  if (l->has_prev_link) {
    const float dx = x0 - x1;
    const float dy = y0 - y1;
    dist = sqrtf(dx * dx + dy * dy);
    have_dist = 1;
    if (dist > k_floor_ed5c_min_dist) {
      x0 += (dx / dist) * k_floor_ed5c_extend;
      y0 += (dy / dist) * k_floor_ed5c_extend;
    }
  }
  if (l->has_next_link) {
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

static int floor_dd90_project(const MslStageFloorGraph* g, int line_idx, float x_in, float y_in,
                              float* y_out, float* nx_out, float* ny_out) {
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
    const MslStageFloorLine* l = &g->lines[cur];
    x0 = l->x0;
    x1 = l->x1;
    x = x_in;
    if (x < x0) {
      if (dir != 1) {
        const int prev = (int)l->prev;
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
        const int next = (int)l->next;
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

  const MslStageFloorLine* out = &g->lines[cur];
  const float y0 = out->y0;
  const float y1 = out->y1;

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
    // This sim does not carry CollData.floor_skip as a seed lane yet, so keep the skip scoped to
    // the source-owned Pass episode and the platform ground_id carried by CollData.floor.index.
    // Deletion path: promote floor_skip itself as an explicit hidden CollData seed/internal lane so
    // the skip lifetime no longer has to be represented by Pass/previous-Pass action ownership.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::{ftCo_8009A184,ftCo_8009A228}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpUpdateFloorSkip}
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

static inline uint8_t floor_chain_endpoints(const MslStageFloorGraph* g, int line_idx,
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
    *out_left_x = g->lines[left_i].x0;
  }
  if (out_left_y) {
    *out_left_y = g->lines[left_i].y0;
  }
  if (out_right_x) {
    *out_right_x = g->lines[right_i].x1;
  }
  if (out_right_y) {
    *out_right_y = g->lines[right_i].y1;
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
  if (!floor_chain_endpoints(fg, line_idx, &left_x0, &left_y0, &right_x1, &right_y1)) {
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

static uint8_t floor_sweep_check(const MslStageFloorGraph* g, uint32_t stage_id, float ax, float ay,
                                 float bx, float by, uint16_t skip_platform_segment_i,
                                 int prefer_line_idx, int* out_line_idx, float* out_ix,
                                 float* out_iy, float* out_nx, float* out_ny) {
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
    if (!floor_line_is_runtime_fighter_solid(g, stage_id, (int)li)) {
      continue;
    }
    if (floor_line_is_skipped_platform(g, (int)li, skip_platform_segment_i)) {
      continue;
    }
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    floor_ed5c_endpoints(g, (int)li, &x0, &y0, &x1, &y1);

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

uint8_t mpcoll_800477e0_floor_mask_probe(const MslBatch* batch, size_t idx,
                                         MslMpcollFloorMaskResult* out) {
  if (batch == NULL) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
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
  if (cur_bot.y <= prev_bot.y &&
      floor_sweep_check(g, stage_id, prev_bot.x, prev_bot.y, cur_bot.x, cur_bot.y,
                        skip_platform_segment_i, -1, &line_idx, &ix, &iy, NULL, NULL)) {
    float y_corr = 0.0f;
    const int out_line_idx = floor_dd90_project(g, line_idx, ix, cur_bot.y, &y_corr, NULL, NULL);
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
          floor_dd90_project(g, (int)li, cur_bot.x, cur_bot.y, &y_corr, NULL, NULL);
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
  const uint32_t stage_id = batch->state.stage_id[idx / (size_t)MSL_MAX_PLAYERS];
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
    out_line_idx = floor_dd90_project(g, line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
                                      &y_corr, NULL, NULL);
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
    out_line_idx = floor_dd90_project(g, (int)li, batch->state.pos_x[idx], batch->state.pos_y[idx],
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

      // Decomp: Fighter_procMap decrements fp->ecb_lock before calling coll_cb/map callbacks and
      // clears CollData_X130_Locked when the countdown reaches 0.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_UnlockECB
      //
      // This simulator stores only the countdown (`ecb_lock_timer`) and uses it as the lock gate.
      // Grounded snapshots should not carry a stale lock.
      const uint8_t ecb_lock_timer_seed = batch->state.ecb_lock_timer[idx];
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
      uint8_t lock_bottom_to_prev_frame = 0u;
      const uint8_t spacie_air_special_floor_owner =
          is_spacie_air_special_floor_collision_action(action_id) && prev_action_id == action_id;
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
          (ecb_lock_active || damage_collision_uses_seeded_lock_bottom) &&
          (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR ||
           is_damage_collision_landing_action(action_id) || spacie_air_special_floor_owner)) {
        // ECB lock-bottom semantics while CollData_X130_Locked is active:
        // - ftCommon_8007D5D4 sets fp->ecb_lock and CollData_X130_Locked on ground->air transitions.
        // - mpColl_LoadECB_inline preserves desired_ecb.bottom while locked.
        // - EscapeAir, Damage/DamageFly, and the Fox/Falco aerial special callbacks above can
        //   resolve grounded contact during this lock window.
        // - Shine narrows this to frame-start SpecialAirLwLoop/End owners; otherwise
        //   SpecialAirLwStart_Anim can change to Loop before collision and incorrectly inherit
        //   the loop/end floor-contact policy on the startup handoff frame.
        // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_inline
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
        //   ftFx_SpecialAirLwStart_Anim,ftFx_SpecialAirLwLoop_Coll,ftFx_SpecialAirLwEnd_Coll}
        lock_bottom_to_zero = 1u;
      } else if (!lock_bottom_to_zero && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
                 batch->state.action_frame[idx] >= 0 && batch->state.action_frame[idx] <= 10 &&
                 batch->state.speed_y_self[idx] <= 0.0f && prev_action_id != action_id) {
        // Decomp shape: mpColl tracks both `ecb` and `prev_ecb`, and floor checks can resolve from a
        // swept bottom segment while EscapeAir is descending. This lite sim does not carry mpColl's
        // full interpolation state, so use the previous ECB frame as a deterministic approximation
        // for fresh EscapeAir entries. Sustained no-lock EscapeAir rows keep current ECB sampling so
        // the approximation does not synthesize an extra LandingFallSpecial frame.
        // refs/melee/src/melee/mp/mpcoll.c::mpCollInterpolateECB
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
        lock_bottom_to_prev_frame = 1u;
      }
      const uint16_t ecb_frame_cur =
          lock_bottom_to_prev_frame ? ecb_frame_prev : ecb_frame_bias_next;
      msl_ecb_bottom_world_point_sample(&cur_bot, char_id, anim, ecb_frame_cur, x, y,
                                        lock_bottom_to_zero);
      msl_ecb_bottom_world_point_sample(&prev_bot, char_id, anim, ecb_frame_prev, prev_x, prev_y,
                                        lock_bottom_to_zero);

      const float cur_bottom_x = cur_bot.x;
      const float cur_bottom_y = cur_bot.y;
      const float prev_bottom_x = prev_bot.x;
      const float prev_bottom_y = prev_bot.y;

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
      if (ground_id != 0xFFFFu) {
        prefer_line_idx = stage_collision_floor_line_index(stage_id, ground_id);
        if (!floor_line_is_runtime_fighter_solid(g, stage_id, prefer_line_idx)) {
          prefer_line_idx = -1;
        }
      }
      const uint16_t skip_platform_segment_i = platform_floor_skip_segment_id(batch, idx, stage_id);

      if (was_grounded && prefer_line_idx >= 0) {
        float y_corr = 0.0f;
        const int out_line_idx = floor_dd90_project(g, prefer_line_idx, cur_bottom_x, cur_bottom_y,
                                                    &y_corr, &floor_nx, &floor_ny);
        if (out_line_idx >= 0) {
          const uint8_t keep_grounded_damage_hitlag_floor_snap =
              grounded_damage_hitlag_allows_downward_floor_projection(batch, idx, action_id);
          const uint8_t keep_capture_lw_floor_snap =
              is_capture_lw_allow_ground_to_air_collision_action(action_id);
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
              !keep_capture_lw_floor_snap) {
            y_corr = 0.0f;
          }
          int resolved_line_idx = out_line_idx;
          if (prefer_line_idx >= 0 && out_line_idx != prefer_line_idx &&
              floor_lines_connected(g, prefer_line_idx, out_line_idx)) {
            // Decomp-shaped tie-break:
            // mpLib_8004DD90_Floor traverses connected prev/next floor links immediately when the
            // bottom point crosses an endpoint. Keep the pre-entry floor.index only for the
            // Dash-entry callback slice that has replay-real coverage; ordinary Damage/Landing/etc.
            // rows should accept the traversed line id.
            // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Coll
            const uint8_t dash_entry_keep_prev_line =
                (action_id == (uint16_t)MSL_ACT_DASH && prev_action_id != (uint16_t)MSL_ACT_DASH)
                    ? 1u
                    : 0u;
            if (dash_entry_keep_prev_line) {
              const MslStageFloorLine* pl = &g->lines[(size_t)prefer_line_idx];
              const float min_x = (pl->x0 < pl->x1) ? pl->x0 : pl->x1;
              const float max_x = (pl->x0 > pl->x1) ? pl->x0 : pl->x1;
              if (cur_bottom_x >= (min_x - k_floor_x_end_clamp) &&
                  cur_bottom_x <= (max_x + k_floor_x_end_clamp)) {
                resolved_line_idx = prefer_line_idx;
              }
            }
          }
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
          const MslStageFloorLine* l = &g->lines[(size_t)prefer_line_idx];
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
                int out_line_idx2 = floor_dd90_project(g, prefer_line_idx, left_x, left_y, NULL,
                                                       &floor_nx, &floor_ny);
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
                int out_line_idx2 = floor_dd90_project(g, prefer_line_idx, right_x, right_y, NULL,
                                                       &floor_nx, &floor_ny);
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
                floor_sweep_check(g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x,
                                  cur_bottom_y, skip_platform_segment_i, prefer_line_idx,
                                  &hit_line_idx, &ix, &iy, &floor_nx, &floor_ny)) {
              // Decomp: desired_ecb.bottom.x is always 0.0, so clamping the ECB bottom contact X
              // corresponds to clamping the fighter position X.
              // refs/melee/src/melee/mp/mpcoll.c::mpColl_LoadECB_JObj
              batch->state.pos_x[idx] += (ix - cur_bottom_x);

              float y_corr2 = 0.0f;
              const int out_line_idx2 =
                  floor_dd90_project(g, hit_line_idx, ix, cur_bottom_y, &y_corr2, NULL, NULL);
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
        const uint8_t escapeair_locked =
            (action_id == (uint16_t)MSL_ACT_ESCAPE_AIR && ecb_lock_active) ? 1u : 0u;
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
          const int out_line_idx =
              floor_dd90_project(g, prefer_line_idx, proj_x, proj_y, &y_corr, &floor_nx, &floor_ny);
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
          const int out_line_idx =
              floor_dd90_project(g, prefer_line_idx, proj_x, proj_y, &y_corr, &floor_nx, &floor_ny);
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
                floor_dd90_project(g, prefer_line_idx, batch->state.pos_x[idx],
                                   batch->state.pos_y[idx], &root_y_corr, &floor_nx, &floor_ny);
            if (root_line_idx >= 0 && root_y_corr >= 0.0f &&
                !g->lines[(size_t)root_line_idx].is_ledge &&
                floor_x_within_line_bounds(g, root_line_idx, batch->state.pos_x[idx])) {
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
              g, prefer_line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx],
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
              floor_dd90_project(g, prefer_line_idx, batch->state.pos_x[idx],
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
              floor_dd90_project(g, prefer_line_idx, batch->state.pos_x[idx],
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
        uint8_t deep_lock_penetration = 0u;
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
          const int out_line_idx = floor_dd90_project(g, prefer_line_idx, cur_bottom_x,
                                                      cur_bottom_y, &y_corr, &floor_nx, &floor_ny);
          if (out_line_idx >= 0 && y_corr >= 0.0f && y_corr <= bottom_rel0) {
            batch->state.pos_y[idx] += y_corr;
            on_ground = 1;
            ground_id = g->lines[(size_t)out_line_idx].segment_i;
            contact_x = cur_bottom_x;
            contact_y = cur_bottom_y + y_corr;
          }
        }
        if (!on_ground && escapeair_locked && prefer_line_idx >= 0) {
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
            const int out_line_idx = floor_dd90_project(
                g, prefer_line_idx, cur_bottom_x, cur_bottom_y, &y_corr, &floor_nx, &floor_ny);
            const float ledge_projection_depth_limit = bottom_rel0 + fabsf(y - prev_y);
            const uint8_t is_ledge_floor = g->lines[(size_t)prefer_line_idx].is_ledge;
            const uint8_t ledge_x_in_bounds =
                floor_x_within_line_bounds(g, prefer_line_idx, batch->state.pos_x[idx]);
            const uint8_t ledge_escapeair_phase_owner =
                (is_ledge_floor && ledge_x_in_bounds &&
                 (batch->state.action_frame[idx] <= 3 ||
                  (prev_y <= k_floor_y_bias && batch->state.action_frame[idx] >= 5)))
                    ? 1u
                    : 0u;
            if (out_line_idx >= 0 && y_corr >= 0.0f &&
                (!is_ledge_floor ||
                 (ledge_escapeair_phase_owner && y_corr <= ledge_projection_depth_limit))) {
              batch->state.pos_y[idx] += y_corr;
              on_ground = 1;
              ground_id = g->lines[(size_t)out_line_idx].segment_i;
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
              floor_dd90_project(g, prefer_line_idx, batch->state.pos_x[idx],
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
              floor_dd90_project(g, prefer_line_idx, batch->state.pos_x[idx],
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
              floor_dd90_project(g, prefer_line_idx, batch->state.pos_x[idx],
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
            floor_sweep_check(g, stage_id, prev_bottom_x, prev_bottom_y, cur_bottom_x, cur_bottom_y,
                              skip_platform_segment_i, prefer_line_idx, &hit_line_idx, &ix, &iy,
                              &floor_nx, &floor_ny)) {
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
          const uint8_t suppress_locked_ledge_land =
              (escapeair_locked && !deep_lock_penetration && hit_line_idx >= 0 &&
               g->lines[(size_t)hit_line_idx].is_ledge && !escapeair_sustained_floor_handoff &&
               !escapeair_kneebend_entry_floor_handoff && !escapeair_jump_entry_floor_handoff)
                  ? 1u
                  : 0u;
          const uint8_t suppress_locked_vertical_af3_land =
              (escapeair_locked && !deep_lock_penetration && hit_line_idx >= 0 &&
               !g->lines[(size_t)hit_line_idx].is_ledge &&
               // Decomp-shaped interpolation gap: in the early EscapeAir lock window, vertical-only
               // sweep crossings can report a floor hit one frame earlier than replay references.
               // Keep this specific case airborne and let subsequent collision frames resolve.
               batch->state.action_frame[idx] == 3 &&
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
          if (suppress_locked_ledge_land || suppress_locked_vertical_af3_land ||
              suppress_escapeair_no_lock_vertical_af3_land || suppress_fallspecial_entry_af3_land ||
              suppress_escapeair_anim_end_fallspecial_fastfall_land ||
              suppress_fall_ledge_floor_first_root_crossing ||
              escapeair_sustained_floorhug_airborne ||
              escapeair_fresh_horizontal_floorhug_airborne || specialhi_bound_entry_airborne ||
              suppress_damageflyroll_shallow_land || suppress_damageair_attackair_entry_land) {
            floor_write_edge_suppression_flags(batch, idx, stage_id, g, hit_line_idx, char_id, anim,
                                               ecb_frame, was_grounded);
          } else {
            float proj_x = 0.0f;
            float proj_y = 0.0f;
            stay_airborne_floor_projection_point(batch->state.pos_x[idx], batch->state.pos_y[idx],
                                                 cur_bottom_x, cur_bottom_y, &proj_x, &proj_y);
            float y_corr = 0.0f;
            const int out_line_idx =
                floor_dd90_project(g, hit_line_idx, proj_x, proj_y, &y_corr, NULL, NULL);
            if (out_line_idx >= 0) {
              batch->state.pos_y[idx] += y_corr;
              if (suppress_active_damage_hitlag_land) {
                batch->state.coll_env_flags[idx] |= (uint32_t)MSL_COLLIDE_FLOOR_MASK;
                ground_id = g->lines[(size_t)out_line_idx].segment_i;
                contact_x = proj_x;
                contact_y = proj_y + y_corr;
              } else {
                on_ground = 1;
                ground_id = g->lines[(size_t)out_line_idx].segment_i;
                contact_x = ix;
                contact_y = iy;
              }
            } else {
              // Sweep saw a floor segment, but projection failed (often an off-end / edge case).
              // DownDamage_Coll's ft_80081DD4 floor path can still resolve from the root point
              // when post-hitlag common Damage Phys has moved the ECB bottom past the normal
              // projection point. Keep that fallback bounded to the same downward-KB owner used by
              // the resting-contact path below; other actions keep edge-suppression diagnostics.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
              //   ftCo_DownDamage_Phys,ftCo_DownDamage_Coll}
              // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
              if (downdamage_active_hitlag_resting_floor_contact) {
                float root_y_corr = 0.0f;
                const int root_line_idx =
                    floor_dd90_project(g, prefer_line_idx, batch->state.pos_x[idx],
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
        } else if (prefer_line_idx >= 0 && !escapeair_sustained_floorhug_airborne &&
                   !escapeair_fresh_horizontal_floorhug_airborne &&
                   !specialhi_bound_entry_airborne &&
                   (batch->state.speed_y_self[idx] == 0.0f ||
                    downdamage_active_hitlag_resting_floor_contact) &&
                   batch->state.hitlag[idx] == 0 &&
                   (batch->state.hitstun[idx] == 0 ||
                    downdamage_active_hitlag_resting_floor_contact)) {
          // Decomp: mpLib_8004DD90_Floor can resolve a resting contact even when no crossing sweep is
          // reported (e.g. vy==0 and the ECB bottom is already on the surface).
          // Gate this to "already on the surface" to avoid snapping to the floor from far below.
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          float y_corr = 0.0f;
          const int out_line_idx = floor_dd90_project(g, prefer_line_idx, cur_bottom_x,
                                                      cur_bottom_y, &y_corr, &floor_nx, &floor_ny);
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
                floor_dd90_project(g, prefer_line_idx, batch->state.pos_x[idx],
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

      batch->state.on_ground[idx] = on_ground;
      if (on_ground) {
        if (action_id == (uint16_t)MSL_ACT_DASH && prev_action_id != (uint16_t)MSL_ACT_DASH &&
            seed_ground_id != 0xFFFFu) {
          // Dash entry floor-index ownership:
          // keep the pre-entry floor.index on the entry frame to avoid an early seam handoff.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Coll
          // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
          ground_id = seed_ground_id;
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

        // Slippi post-frames: on_ground can become true while `speed_y_self` remains negative for
        // one frame on landing, then resets to 0 on the subsequent grounded frame.
        if (was_grounded) {
          batch->state.speed_y_self[idx] = 0.0f;
        }
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
          const int out_line_idx =
              floor_dd90_project(g, prefer_line_idx, cur_bottom_x, cur_bottom_y, NULL, NULL, NULL);
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
    }
  }
}
