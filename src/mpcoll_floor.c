#include "mpcoll_floor.h"

#include <float.h>
#include <math.h>

#include "action.h"
#include "action_ids.h"
#include "attack_id_tables.h"
#include "buttons.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "damage_terminal_owner.h"
#include "input_axis.h"
#include "match_flow.h"
#include "motion_state_owners.h"
#include "mpcoll_ecb_pose.h"
#include "mpcoll_floor_skip.h"
#include "mpcoll_wall_ceil.h"
#include "msl_math.h"
#include "sheik_specials.h"
#include "specialhi_pose.h"
#include "stage_collision.h"
#include "state_flags.h"
#include "throw_flow.h"

// Decomp constants shared by the split floor owner. These mirror the constants kept in
// mpcoll_ground.c for coordinator code that still owns later callback logic.
static const float k_floor_x_end_clamp = 0.1f;
static const float k_floor_y_bias = 0.0001f;
static const float k_floor_ed5c_min_dist = 0.001f;
static const float k_floor_horiz_dy_thresh = 0.0001f;
static const float k_floor_ed5c_extend = 1.0f;
static const float k_mpcoll_substep_max_delta = 6.0f;
static const float k_floor_edge_wall_probe_x_offset = 1.0f;
static const float k_floor_edge_wall_probe_y_offset = 1.0f;
static const float k_ecb_vertical_unit = 1.0f;
static const float k_transformed_platform_skip_lookup_slop = 2.0f * 1.0f;

static inline float cross2(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

uint8_t is_damage_collision_landing_action(uint16_t a) {
  // Damage collision callbacks can resolve grounded contact while hitstun remains active:
  // - ftCo_Damage_Coll
  // - ftCo_DamageFly_Coll
  // - ftCo_DownDamage_Coll (air path calls ft_80081DD4 before downed follow-up handling)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_DownDamage_Coll
  return msl_damage_owner_is_damage_collision_landing_action(a);
}

uint8_t is_damage_fly_collision_action(uint16_t a) {
  return msl_damage_owner_is_damagefly_collision_action(a);
}

uint8_t is_damage_ground_collision_action(uint16_t a) {
  return msl_damage_owner_is_damage_ground_action(a);
}

uint8_t is_common_damage_ground_pose_ecb_action(uint16_t a) {
  // Common DamageHi/N/Lw actions share `ftCo_Damage_Coll` and use the generated Damage pose ECB
  // after CollData_X130 unlock. Keep this explicit because the post-unlock loaded-pose owner is a
  // narrower callback-local state than the broad Damage collision class.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 DAMAGE_GROUND
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

uint8_t is_capture_lw_allow_ground_to_air_collision_action(uint16_t a) {
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

uint8_t is_attackair_action(uint16_t a) {
  return msl_motion_state_common_class3_has_fast(a, MSL_MS_CLASS3_PHASE4_ATTACK_AIR_COLL);
}

uint8_t is_spacie_air_special_floor_collision_action(uint8_t char_id, uint16_t a) {
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
  switch (msl_motion_state_fx_special_kind(char_id, a)) {
    case MSL_FX_KIND_SPECIAL_AIR_LW_LOOP:
    case MSL_FX_KIND_SPECIAL_AIR_LW_END:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t is_spacie_ground_sideb_allow_ground_to_air_callback(uint8_t char_id,
                                                                          uint16_t action_id) {
  // Table-backed MotionState callback owner:
  // grounded Fox/Falco Side-B Start/Main collision callbacks call ft_80082708, which routes to
  // mpColl_8004B108 -> mpColl_80043754 -> mpColl_8004ACE4. Use the generated callback identity
  // rather than a local action-id list so this owner stays tied to MSLMSO01 provenance.
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class FX_SPECIALS_GROUND_B108_COLL
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialSStart_Coll,ftFx_SpecialS_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
  return msl_motion_state_class_has(char_id, action_id, MSL_MS_CLASS_FX_SPECIALS_GROUND_B108_COLL);
}

uint8_t is_common_fallspecial_action(uint16_t a) {
  switch (a) {
    case MSL_ACT_FALL_SPECIAL:
    case MSL_ACT_FALL_SPECIAL_F:
    case MSL_ACT_FALL_SPECIAL_B:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t mpcoll_ground_prev_action_is_fall_iasa_escapeair_source(uint16_t action_id) {
  // Fall-family IASA can enter EscapeAir before the same frame's map callback. The entered
  // EscapeAir_Coll consumes the frame-start CollData root/ECB through ft_80082C74/mpColl_800471F8.
  // This helper intentionally does not include JumpAerial/KneeBend: those owners have distinct
  // floor-producer lanes and should not inherit Fall's ledge-endpoint floor handoff.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::{
  //   ftCo_80099A58,ftCo_EscapeAir_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_800471F8}
  switch (action_id) {
    case MSL_ACT_FALL:
    case MSL_ACT_FALL_F:
    case MSL_ACT_FALL_B:
    case MSL_ACT_FALL_AERIAL:
    case MSL_ACT_FALL_AERIAL_F:
    case MSL_ACT_FALL_AERIAL_B:
      return 1u;
    default:
      return 0u;
  }
}

uint8_t mpcoll_ground_escapeair_fall_iasa_source_owner(const MslBatch* batch, size_t idx) {
  if (mpcoll_ground_prev_action_is_fall_iasa_escapeair_source(batch->state.prev_action_id[idx])) {
    return 1u;
  }
  // Teacher-forced one-step rows can start directly in EscapeAir after the Fall IASA transition,
  // so `cache_prev_action_state` sees EscapeAir instead of the frame-start Fall action. Accept the
  // explicit seed snapshot only together with the live runtime floor-sweep provenance required by
  // the caller; seeded action history alone is not source authority.
  return mpcoll_ground_prev_action_is_fall_iasa_escapeair_source(
      batch->state.seed_prev_action_id[idx]);
}

uint8_t is_spacie_specialhi_end_fallspecial_source(uint8_t char_id, uint16_t a) {
  switch (msl_motion_state_fx_special_kind(char_id, a)) {
    case MSL_FX_KIND_SPECIAL_HI_FALL:
    case MSL_FX_KIND_SPECIAL_HI_BOUND:
      return 1u;
    default:
      return 0u;
  }
}

uint8_t is_spacie_sideb_air_end_fallspecial_source(uint8_t char_id, uint16_t a) {
  // Fox/Falco aerial Side-B end is a distinct `ft_CheckGroundAndLedge` source path into
  // FallSpecial/LandingFallSpecial. Sustained same-action FallSpecial rows still keep the older
  // first-sustained hard-floor delay below; do not use those rows as evidence for the just-entered
  // Side-B destination callback.
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class FT_CHECK_GROUND_LEDGE_AIR_COLL
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
  return (uint8_t)(msl_motion_state_class_has(char_id, a,
                                              MSL_MS_CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL) &&
                   msl_motion_state_submotion_id(char_id, a) ==
                       (uint16_t)MSL_SM_FX_SPECIAL_AIR_S_END);
}

uint8_t action_uses_ftco_80096cc8_floor_callback(uint16_t a) {
  // MSLMSO01 marks the collision callbacks that pass ftCo_80096CC8 into the floor check. That
  // callback accepts hard floors, accepts passable platforms only above p_ftCommonData->x25C, and
  // rejects held-down platform pass-through. Keep this owner table-backed instead of duplicating
  // common-air/CliffJump2/PassiveWall/PassiveCeil action ids here.
  // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 class FT80083090_PLATFORM_PASS_COLL
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083090_inline,ft_800831CC,ft_800835B0}
  return msl_motion_state_common_class_has_fast(a, MSL_MS_CLASS_FT80083090_PLATFORM_PASS_COLL);
}

uint8_t damage_hitlag_exit_carry_source_is_thrown_needle(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // Thrown-Needle Damage-hitlag ECB carry:
  // A same-volley Needle can hit on the frame after Fighter_8006A1BC decrements visible hitlag to
  // zero but before the next Damage_Coll callback can rebuild CollData from the Damage pose. The
  // source item BODY pass still tests the frozen Damage ECB/hurtcaps for the current item contact.
  // This source state belongs to the victim's Damage hitlag CollData packet, not to a still-live
  // item slot; Needle DmgDealt can destroy/bounce the article before the exit-carry consumer runs.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_ProcessHit_8006D1EC}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgDealt
  return (batch->state.coll_damage_hitlag_ecb_source_kind[idx] ==
          (uint8_t)MSL_DAMAGE_HITLAG_ECB_SOURCE_THROWN_NEEDLE)
             ? 1u
             : 0u;
}

uint8_t is_just_entered_specialairn_end_from_loop(uint8_t char_id, uint16_t action_id,
                                                  uint16_t prev_action_id, int16_t action_frame) {
  return (msl_motion_state_fx_special_kind(char_id, action_id) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_END &&
          msl_motion_state_fx_special_kind(char_id, prev_action_id) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_LOOP &&
          action_frame == 0)
             ? 1u
             : 0u;
}

uint8_t damage_hitlag_floorhug_attempts_downward_sdi(const MslBatch* batch, size_t idx,
                                                     const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  // Collision-owner gate for active-hitlag Damage/DamageFly rows:
  // - ftCo_Damage_OnEveryHitlag reads the current stick after input processing and mutates
  //   `fp->cur_pos` before the motion-state collision callback runs.
  // - ft_80081DD4 then routes `allow_sdi` rows through mpColl_800477E0, where
  //   mpColl_80044628_Floor / mpColl_80044948_Floor can raise FloorPush|FloorHug while keeping
  //   the fighter airborne (`CollisionFlagAir_StayAirborne`).
  // - Restrict this stay-airborne floor correction owner to rows where OnEveryHitlag can actually
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
  if (batch->state.damage_allow_sdi[idx] == 0u) {
    return 0u;
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
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221A_INDEX;
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

uint8_t action_uses_active_hitlag_downward_sdi_floorhug(uint16_t action_id, const MslBatch* batch,
                                                        size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // Active-hitlag OnEveryHitlag -> mpColl floorhug ownership is shared by the common damage
  // collision family. DamageAir1/2/3 still call `ftCo_Damage_Coll`, and `ft_80081DD4` routes the
  // allow-SDI path through `mpColl_800477E0`, whose floor pass can raise FloorPush|FloorHug while
  // leaving ground_or_air airborne. The caller keeps this bounded to rows with a fresh consumed
  // downward SDI input and a current hard-floor/bottom-sweep owner; soft platforms and ledges stay
  // rejected by the later source preconditions.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{ftCo_8009F184,ftCo_DownDamage_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
  if (msl_motion_state_common_class3_has_fast(action_id, MSL_MS_CLASS3_PHASE4_DAMAGE_COMMON_COLL) ||
      is_damage_fly_collision_action(action_id) || action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
      action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) {
    return 1u;
  }
  return (batch->state.phantom_damage_pending_x1898[idx] > 0.0f) ? 1u : 0u;
}

uint8_t mpcoll_damageair_action(uint16_t action_id) {
  return msl_damage_owner_is_damage_air_action(action_id);
}

uint8_t mpcoll_damage_active_hitlag_stay_airborne_floor_owner(
    const MslBatch* batch, size_t idx, uint16_t action_id, uint8_t prefer_line_valid,
    uint8_t prefer_line_is_platform, uint8_t prefer_line_is_ledge, uint8_t prefer_line_is_slope,
    uint8_t prefer_line_has_platform_transform, uint8_t prefer_line_is_fighter_solid,
    uint8_t prefer_line_is_terminal_cardinal_hard_floor, float prefer_line_root_y,
    float source_prev_root_y, uint8_t downdamage_x_axis_fresh_sdi_edge) {
  if (batch == NULL || batch->state.hitlag_pre_timer[idx] == 0u || batch->state.hitlag[idx] == 0u ||
      batch->state.damage_hitlag_downward_sdi_consumed[idx] == 0u) {
    return 0u;
  }

  // Active-hitlag SDI/floor handoff:
  // - ftCo_Damage_OnEveryHitlag mutates cur_pos before the motion-state collision callback.
  // - ft_80081DD4's allow-SDI path can keep a below-floor SDI root airborne via
  //   mpColl_800477E0 / mpColl_80044948_Floor while hitlag remains frozen.
  // - DownDamageU/D re-enter that common callback through ftCo_8009F184, but floor-adjacent rows
  //   with a fresh x670 horizontal edge publish the horizontal displacement without treating the y
  //   component as a below-floor stay-airborne owner; the later DownDamage_Coll path owns floor
  //   contact. Rows whose fresh edge is vertical remain on the below-floor active-hitlag owner.
  // - The retained non-DownDamage slice is common DamageAir* active hitlag on a carried terminal
  //   cardinal hard-floor chain: source routes all three DamageAir motions through Damage_Coll /
  //   mpColl_800477E0 after consumed downward OnEveryHitlag displacement, and
  //   mpColl_80044948_Floor can publish FloorPush|FloorHug while preserving airborne state once the
  //   callback-current hard floor is the active CollData floor. Keep this keyed to MSLSTG01 line
  //   graph metadata instead of stage id. Stadium-style connected lip graphs, soft platforms,
  //   ledge floors, generated slopes, and transformed stage-object floors still need their
  //   explicit bottom-sweep/source-owner proof; visible root-below-floor state alone is not enough.
  //
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_8008DCE0,ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
  //   ftCo_8009F184,ftCo_DownDamage_Phys,ftCo_DownDamage_Coll}
  // refs/melee/src/melee/ft/fighter.c::Fighter_Spaghetti_8006AD10
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
  if (action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
      action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D) {
    return downdamage_x_axis_fresh_sdi_edge ? 0u : 1u;
  }

  if (mpcoll_damageair_action(action_id) && prefer_line_valid && prefer_line_is_fighter_solid &&
      !prefer_line_is_platform && !prefer_line_is_ledge && !prefer_line_is_slope &&
      !prefer_line_has_platform_transform && prefer_line_is_terminal_cardinal_hard_floor &&
      isfinite(prefer_line_root_y) && isfinite(source_prev_root_y) &&
      source_prev_root_y > (prefer_line_root_y + k_floor_y_bias) &&
      batch->state.pos_y[idx] < (prefer_line_root_y - k_floor_y_bias)) {
    return 1u;
  }
  return 0u;
}

uint8_t grounded_damage_hitlag_allows_downward_floor_projection(const MslBatch* batch, size_t idx,
                                                                uint16_t action_id) {
  if (batch == NULL) {
    return 0u;
  }
  // Grounded damage collision owner:
  // - `ftCo_Damage_OnEveryHitlag` and `ftCo_Damage_OnExitHitlag` can move `fp->cur_pos.y`
  //   before collision.
  // - grounded `ftCo_Damage_Coll` then routes through `ft_800848DC -> ft_80082708 ->
  //   mpColl_8004B108`, which keeps the row floor-owned.
  // - Keep the downward floor projection on these hitlag/post-hitlag grounded damage rows so the
  //   generic grounded anti-snap clamp does not strand a "grounded" fighter several units above
  //   the stage after SDI/ASDI.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_OnExitHitlag,ftCo_Damage_Coll
  // }
  // refs/melee/src/melee/ft/ft_081B.c::{ft_800848DC,ft_80082708}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  if (!is_damage_collision_landing_action(action_id)) {
    return 0u;
  }
  return (batch->state.on_ground[idx] != 0u && batch->state.hitlag_pre_timer[idx] != 0u) ? 1u : 0u;
}

void stay_airborne_floor_projection_point(float fighter_x, float fighter_y, float bottom_x,
                                          float bottom_y, float* proj_x_out, float* proj_y_out) {
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

uint8_t mpcoll_replay_rollout_advanced_past_reseed(const MslBatch* batch, int bi) {
  if (batch == NULL || batch->replay_rollout_reseeded == NULL ||
      batch->replay_rollout_reseeded[bi] == 0u || batch->replay_rollout_seed_frame_id == NULL) {
    return 0u;
  }
  return (batch->state.frame_id[bi] != batch->replay_rollout_seed_frame_id[bi]) ? 1u : 0u;
}

MslMpcollSourcePhases mpcoll_source_phases_for_motion_state(
    uint8_t char_id, uint16_t action_id, uint8_t ft_check_ground_ledge_uses_no_ledge_path) {
  const uint32_t class_bits = msl_motion_state_class_bits(char_id, action_id);
  const uint32_t class2_bits = msl_motion_state_class2_bits(char_id, action_id);
  uint32_t class3_bits = 0u;
  if ((class_bits &
       (MSL_MS_CLASS_ATTACK_AIR | MSL_MS_CLASS_ESCAPE_AIR_COLL | MSL_MS_CLASS_DAMAGE_COMMON_COLL |
        MSL_MS_CLASS_DAMAGE_FLY_COLL | MSL_MS_CLASS_DAMAGE_FALL_COLL)) != 0u) {
    class3_bits = msl_motion_state_class3_bits(char_id, action_id);
  }
  MslMpcollSourcePhases phases = 0u;
  if ((class2_bits & MSL_MS_CLASS2_COMMON_AIRBORNE_COLL) != 0u) {
    // Phase 3 common airborne owner. Excluded airborne families may still consume the same low-level
    // phase bits below, but they must enter through their broad source-owner class, not this narrow
    // common owner.
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80083090,ft_800831CC,ft_800835B0}
    if ((class_bits & MSL_MS_CLASS_FT80083090_PLATFORM_PASS_COLL) != 0u) {
      phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_PLATFORM_PASS;
    }
    if ((class_bits & MSL_MS_CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL) != 0u) {
      // ft_CheckGroundAndLedge is a branch-shaped owner, not a fixed mpColl wrapper:
      // ledgeCooldown/x2224_b2 uses mpColl_800471F8, otherwise source sets facing and calls
      // mpColl_800473CC, whose inline0(flags=4) floor producer keeps ground_or_air airborne.
      // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_800473CC}
      phases |= (MslMpcollSourcePhases)(ft_check_ground_ledge_uses_no_ledge_path
                                            ? MSL_MPCOLL_PHASE_AIR_471F8
                                            : MSL_MPCOLL_PHASE_AIR_473CC);
    }
  } else {
    if ((class_bits & MSL_MS_CLASS_FT80083090_PLATFORM_PASS_COLL) != 0u) {
      phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_PLATFORM_PASS;
    }
    if ((class3_bits &
         (MSL_MS_CLASS3_PHASE4_ATTACK_AIR_COLL | MSL_MS_CLASS3_PHASE4_ESCAPE_AIR_COLL)) != 0u) {
      phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_AIR_471F8;
    } else if ((class_bits & MSL_MS_CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL) != 0u) {
      // Same branch as above for non-common FT_CHECK callbacks.
      phases |= (MslMpcollSourcePhases)(ft_check_ground_ledge_uses_no_ledge_path
                                            ? MSL_MPCOLL_PHASE_AIR_471F8
                                            : MSL_MPCOLL_PHASE_AIR_473CC);
    } else if ((class_bits & MSL_MS_CLASS_FT80081D0C_AIR_COLL) != 0u) {
      // Retained non-Phase-4 wrapper owners. These callbacks share the low-level airborne mpColl
      // wrapper but are not selected by the narrow Phase 4 owner word above.
      phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_AIR_471F8;
    }
  }
  if ((class2_bits & MSL_MS_CLASS2_COMMON_GROUNDED_COLL) != 0u) {
    // Phase 3 common grounded owner.
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC,ft_80084104}
    phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_GROUNDED_4ACE4;
  } else if ((class_bits & MSL_MS_CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL) != 0u) {
    phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_GROUNDED_4ACE4;
  }
  if ((class2_bits & MSL_MS_CLASS2_COMMON_GROUNDED_B108_COLL) != 0u) {
    // Phase 3 common ft_80083F88/mpColl_8004B108 owner.
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_GROUND_B108;
  } else if ((class_bits & (MSL_MS_CLASS_FT80083F88_GROUND_TO_AIR_COLL |
                            MSL_MS_CLASS_FX_SPECIALS_GROUND_B108_COLL)) != 0u) {
    phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_GROUND_B108;
  }
  if ((class3_bits &
       (MSL_MS_CLASS3_PHASE4_DAMAGE_FLY_COLL | MSL_MS_CLASS3_PHASE4_DAMAGE_FALL_COLL)) != 0u) {
    phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_AIR_473CC;
  }
  if ((class3_bits & MSL_MS_CLASS3_PHASE4_DAMAGE_COMMON_COLL) != 0u) {
    phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_AIR_477E0;
  }
  if ((class2_bits & MSL_MS_CLASS2_COMMON_GROUNDED_B2DC_COLL) != 0u) {
    // Phase 3 common ft_800827A0/mpColl_8004B2DC endpoint owner.
    // refs/melee/src/melee/ft/ft_081B.c::{ft_800827A0,ft_80084104}
    phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_EDGE_SNAP;
  } else if ((class_bits & MSL_MS_CLASS_FT800827A0_EDGE_SNAP_COLL) != 0u) {
    phases |= (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_EDGE_SNAP;
  }
  return phases;
}

uint8_t mpcoll_ft_check_ground_ledge_uses_no_ledge_path(const MslBatch* batch, size_t idx) {
  // Source checks `fp->x2064_ledgeCooldown != 0 || fp->x2224_b2` before selecting the no-ledge
  // mpColl_800471F8 path. The replay seed exposes the same fp+0x2224 bit as `dmg_x2224_b2`; use
  // that source lane here rather than approximating it with character/action ids.
  // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
  return (uint8_t)(batch != NULL && (batch->state.ledge_cooldown[idx] != 0u ||
                                     batch->state.dmg_x2224_b2[idx] != 0u));
}

uint8_t mpcoll_source_phases_has(MslMpcollSourcePhases phases, MslMpcollSourcePhase phase) {
  return (uint8_t)((phases & (MslMpcollSourcePhases)phase) != 0u);
}

uint8_t mpcoll_vanish_ft_check_jobj_ecb_owner(uint8_t char_id, uint16_t action_id,
                                              MslMpcollSourcePhases source_phases) {
  const MslCharParams* chp = msl_char_params_fast(char_id);
  if (chp == NULL || !(chp->sheik_vanish_landing_lag_frames > 0.0f)) {
    return 0u;
  }
  if (!mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_473CC) ||
      msl_motion_state_class_has(char_id, action_id, MSL_MS_CLASS_FT_CHECK_GROUND_LEDGE_AIR_COLL) ==
          0u ||
      msl_motion_state_fx_special_kind(char_id, action_id) != (uint8_t)MSL_FX_KIND_NONE) {
    return 0u;
  }
  // Vanish end/start Coll callbacks call `ft_CheckGroundAndLedge`, which snapshots the
  // callback-local root into CollData and uses `mpColl_800473CC` when ledgeCooldown/x2224_b2 are
  // clear. With a fixed ECB whose bottom is above the root, source `mpColl_80044838_Floor` uses the
  // live JObj ECB before floor checks. This is a Vanish-special source owner (ftSeak/ftZelda
  // attributes), not the Fox/Falco SpecialHi JObj/XRotN owner and not a generic character-id rule.
  // data/characters/{sheik,zelda}.json::sheik_vanish_landing_lag_frames
  // data/motion_state/owners/{sheik,zelda}.bin::MSLMSO01 FT_CHECK_GROUND_LEDGE_AIR_COLL
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
  //   ftSk_SpecialAirHiStart_0_Coll,ftSk_SpecialAirHiStart_1_Coll,ftSk_SpecialAirHi_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::ft_CheckGroundAndLedge
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_800473CC}
  return 1u;
}

uint8_t mpcoll_source_phases_preserve_grounded_floor(MslMpcollSourcePhases phases) {
  const MslMpcollSourcePhases preserving = (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_GROUNDED_4ACE4 |
                                           (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_GROUND_B108 |
                                           (MslMpcollSourcePhases)MSL_MPCOLL_PHASE_EDGE_SNAP;
  return (uint8_t)((phases & preserving) != 0u);
}

uint8_t floor_line_y_at_x_for_env(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                  int line_idx, float x, float* y_out);
uint8_t floor_x_within_line_bounds(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                   int line_idx, float x);

uint8_t mpcoll_floor_sweep_prev_root_is_source_owned(const MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.floor_sweep_prev_source_owned[idx] == 0u ||
      !isfinite(batch->state.floor_sweep_prev_pos_x[idx]) ||
      !isfinite(batch->state.floor_sweep_prev_pos_y[idx])) {
    return 0u;
  }
  // Reseed without a source-owned floor-sweep packet initializes floor_sweep_prev_pos to the
  // current root in api.c and clears this provenance bit. The bit is set only by an explicit seed
  // mpCollPrev endpoint or by natural post-frame promotion.
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  return 1u;
}

uint8_t mpcoll_floor_sweep_prev_root_is_runtime_owned(const MslBatch* batch, size_t idx) {
  if (!mpcoll_floor_sweep_prev_root_is_source_owned(batch, idx) ||
      batch->state.floor_sweep_prev_runtime_owned[idx] == 0u) {
    return 0u;
  }
  // Runtime-only floor-sweep authority:
  // Source map callbacks may consume CollData.prev/cur roots produced by the previous callback.
  // Teacher-forced reseed can reconstruct a one-step source endpoint, but that is not evidence
  // that the current live callback ran the hard-floor producer. New hard-body rescue lanes consume
  // only this runtime-produced subset so stale replay-visible platform/root state cannot publish.
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754}
  // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
  return 1u;
}

void mpcoll_colldata_state_load(const MslMpcollContext* ctx, MslMpcollCollDataState* out,
                                MslMpcollSourcePhases source_phases) {
  if (ctx == NULL || ctx->batch == NULL || out == NULL) {
    return;
  }
  const MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  // Minimal callback-local CollData view currently consumed by the ordered substrate. The backing
  // SoA lanes already hold root/ECB/env/surface state; copy additional fields here only when a
  // routed wrapper consumes them, so normal callbacks do not pay for unused ECB materialization.
  // refs/melee/src/melee/lb/types.h::CollData
  // refs/melee/src/melee/mp/mpcoll.c::{mpCollPrev,mpColl_80043754,mpCollInterpolateECB}
  *out = (MslMpcollCollDataState){
      .floor_index = batch->state.ground_id[idx],
      .floor_skip_segment_id = platform_floor_skip_segment_id(batch, idx, ctx->stage_id),
      .source_phases = source_phases,
  };
}

void mpcoll_materialize_floor_publication_result(const MslMpcollContext* ctx,
                                                 const MslMpcollFloorPublication* publication) {
  if (ctx == NULL || publication == NULL || publication->on_ground == 0u ||
      mpcoll_callback_floor_result_valid(ctx)) {
    return;
  }
  // All grounded publications should enter final writeback through the same callback-local CollData
  // floor scratch. Producers that really ran the source ECB-bottom sweep set BOTTOM_SWEEP
  // explicitly; untagged direct publications stay tagged as DIRECT_PUBLICATION so late guards
  // cannot mistake mixed mpLib root/endpoint projections for a proven mpColl_80044628_Floor hit.
  // Candidate collection owns the mode/provenance at production time; publication guards may still
  // reject it, and final writeback consumes or discards that one packet.
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80043754,mpColl_80044628_Floor,mpColl_80044838_Floor,mpColl_8004A908_Floor}
  const uint8_t mode = publication->result_mode != (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE
                           ? publication->result_mode
                           : (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION;
  mpcoll_record_callback_floor_result_with_mode(
      ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, mode, publication->contact.ground_id,
      publication->contact.contact_x, publication->contact.contact_y, publication->contact.normal_x,
      publication->contact.normal_y);
}

uint8_t mpcoll_materialize_active_damage_hitlag_stay_airborne_floor(
    const MslMpcollContext* ctx, MslMpcollFloorPublication* publication) {
  const uint8_t active_hitlag_owner =
      (ctx != NULL && ctx->batch != NULL && ctx->batch->state.hitlag[ctx->idx] != 0u &&
       is_damage_collision_landing_action(ctx->action_id) &&
       action_uses_active_hitlag_downward_sdi_floorhug(ctx->action_id, ctx->batch, ctx->idx))
          ? 1u
          : 0u;
  const uint8_t damage_post_hitlag_owner =
      (ctx != NULL && ctx->batch != NULL && ctx->batch->state.hitlag[ctx->idx] == 0u &&
       (msl_damage_owner_is_damage_air_action(ctx->action_id) ||
        msl_damage_owner_is_damage_ground_action(ctx->action_id)) &&
       ctx->batch->state.hitstun[ctx->idx] != 0u)
          ? 1u
          : 0u;
  if (ctx == NULL || ctx->batch == NULL || ctx->floor_graph == NULL || publication == NULL ||
      publication->on_ground != 0u || (!active_hitlag_owner && !damage_post_hitlag_owner) ||
      ctx->prefer_floor_line_idx < 0) {
    return 0u;
  }
  const uint8_t active_hitlag_floor_authority =
      (active_hitlag_owner &&
       ctx->batch->state.coll_damage_hitlag_floor_contact_runtime[ctx->idx] != 0u &&
       (ctx->batch->state.coll_env_flags[ctx->idx] & (uint32_t)MSL_COLLIDE_FLOOR_MASK) != 0u)
          ? 1u
          : 0u;
  const MslStageFloorLine* line = &ctx->floor_graph->lines[(size_t)ctx->prefer_floor_line_idx];
  if (line->is_platform || line->is_ledge) {
    return 0u;
  }
  float floor_y = 0.0f;
  if (!floor_line_y_at_x_for_env(ctx->batch, ctx->bi, ctx->floor_graph, ctx->prefer_floor_line_idx,
                                 ctx->batch->state.pos_x[ctx->idx], &floor_y) ||
      ctx->batch->state.pos_y[ctx->idx] >= (floor_y - k_floor_y_bias)) {
    return 0u;
  }
  const uint8_t common_damage_post_unlock_pose_bottom_above_floor =
      // Same post-unlock DamageHi/N/Lw loaded-pose owner as the runtime floor-sweep path: after
      // CollData_X130 unlock, stale zero-bottom current ECB state is not enough to prove a
      // stay-airborne FloorHug contact. The generated Damage pose bottom must reach the carried
      // hard floor before `mpColl_80044948_Floor` can project the root while leaving ground_or_air
      // airborne. Require runtime floor-sweep provenance so one-step replay seeds with only a
      // serialized CollData endpoint do not borrow a live callback owner.
      // refs/melee/src/melee/ft/fighter.c::Fighter_procMap
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
      // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpColl_80044948_Floor}
      (damage_post_hitlag_owner && is_common_damage_ground_pose_ecb_action(ctx->action_id) &&
       mpcoll_floor_sweep_prev_root_is_runtime_owned(ctx->batch, ctx->idx) &&
       (ctx->batch->state.pos_y[ctx->idx] +
        mpcoll_pose_ecb_bottom_rel_y(ctx->char_id, ctx->anim, ctx->ecb_frame, 0u)) >
           (floor_y + k_floor_y_bias))
          ? 1u
          : 0u;
  const uint8_t damage_carried_hard_floor_authority =
      (damage_post_hitlag_owner && ctx->batch->state.ground_id[ctx->idx] != 0xFFFFu &&
       line->segment_i == ctx->batch->state.ground_id[ctx->idx] &&
       !common_damage_post_unlock_pose_bottom_above_floor &&
       mpcoll_floor_sweep_prev_root_is_runtime_owned(ctx->batch, ctx->idx) &&
       floor_x_within_line_bounds(ctx->batch, ctx->bi, ctx->floor_graph, ctx->prefer_floor_line_idx,
                                  ctx->batch->state.pos_x[ctx->idx]) &&
       floor_x_within_line_bounds(ctx->batch, ctx->bi, ctx->floor_graph, ctx->prefer_floor_line_idx,
                                  ctx->batch->state.floor_sweep_prev_pos_x[ctx->idx]) &&
       ctx->loaded_ecb != NULL && ctx->loaded_ecb->current != NULL &&
       ctx->loaded_ecb->current->bottom_y <= (floor_y + k_floor_y_bias))
          ? 1u
          : 0u;
  if (!active_hitlag_floor_authority && !damage_carried_hard_floor_authority) {
    return 0u;
  }
  // Damage stay-airborne writeback:
  // source `mpColl_80044628_Floor` has already produced floor/contact/env state for this Damage
  // segment, and `mpColl_80044948_Floor` may project `CollData.cur_pos` while
  // `CollisionFlagAir_StayAirborne` leaves ground_or_air airborne. Active hitlag consumes the
  // runtime-produced floor/env packet while the fighter is frozen. Sustained Damage hitstun
  // consumes the carried hard-floor `CollData.floor.index` through the same ft_80081DD4 owner when
  // the callback-loaded previous-root/current-ECB floor packet proves that floor. This lets source
  // project a root that the previous frame already left below the floor without allowing restored
  // visible `ground_id` plus an ECB below the floor to publish by itself. Keep that as an airborne
  // STAY_AIRBORNE floor-result packet so final writeback consumes the same CollData-shaped path as
  // other floor contacts instead of patching root/contact after publication.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll,ftCo_Damage_IASA}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_800477E0,mpColl_80044628_Floor,mpColl_80044948_Floor}
  ctx->batch->state.pos_y[ctx->idx] = floor_y + k_floor_y_bias;
  publication->airborne_ground_id = line->segment_i;
  publication->contact.ground_id = line->segment_i;
  publication->contact.contact_x = ctx->batch->state.pos_x[ctx->idx];
  publication->contact.contact_y = floor_y;
  publication->contact.normal_x = 0.0f;
  publication->contact.normal_y = 1.0f;
  publication->result_mode = (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAY_AIRBORNE_PROJECTION;
  mpcoll_record_callback_floor_result_with_mode(
      ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE,
      (uint8_t)MSL_MPCOLL_FLOOR_MODE_STAY_AIRBORNE_PROJECTION, line->segment_i,
      publication->contact.contact_x, publication->contact.contact_y, publication->contact.normal_x,
      publication->contact.normal_y);
  return 1u;
}

uint8_t mpcoll_source_phases_allow_floor_edge_snap(MslMpcollSourcePhases phases) {
  // Decomp: `mpColl_8004A45C_Floor` endpoint snap is used by `mpColl_8004B2DC`,
  // reached through `ft_800827A0` directly or through wrappers such as `ft_80084104` and
  // `ft_800841B8`. MSLMSO01 extracts that callback owner into a generated class, replacing the
  // older local action-id list. DownBound/DownWait/DownStand remain excluded because they route
  // through `ft_80082708 -> mpColl_8004B108` instead.
  // refs/melee/src/melee/ft/ft_081B.c::{ft_800827A0,ft_80084104,ft_800841B8,ft_80082708}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004A45C_Floor,mpColl_8004B108}
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class FT800827A0_EDGE_SNAP_COLL)
  return mpcoll_source_phases_has(phases, MSL_MPCOLL_PHASE_EDGE_SNAP);
}

uint8_t action_is_down_bound(uint16_t a) {
  return (uint8_t)(a == (uint16_t)MSL_ACT_DOWN_BOUND_U || a == (uint16_t)MSL_ACT_DOWN_BOUND_D);
}

uint8_t action_uses_landing_floor_release_coll(uint16_t a) {
  // Landing-family collision callbacks route through ft_80084280 -> mpColl_8004B4B0
  // (inline2 flags=1), which uses the current floor-id release helper (mpColl_8004A678_Floor)
  // rather than the generic edge-snap helper path (mpColl_8004A45C_Floor).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_Coll
  // refs/melee/src/melee/ft/ftmotionstates.c (ftCo_MS_LandingFallSpecial coll_cb)
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B4B0,mpColl_8004A678_Floor,mpColl_8004A45C_Floor}
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 classes LANDING_COLL/LANDING_AIR_COLL)
  return (uint8_t)(a == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL ||
                   msl_motion_state_common_class_has_fast(a, MSL_MS_CLASS_LANDING_COLL) ||
                   msl_motion_state_common_class_has_fast(a, MSL_MS_CLASS_LANDING_AIR_COLL));
}

uint8_t floor_lines_connected(const MslStageFloorGraph* g, int a, int b) {
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

uint8_t stage_floor_graph_has_height_platform_transform(uint32_t stage_id,
                                                        const MslStageFloorGraph* g) {
  (void)g;
  return stage_collision_stage_has_height_platform_transform(stage_id);
}

uint8_t stage_height_platform_line_has_current_source(const MslBatch* batch, int bi,
                                                      uint32_t stage_id, uint16_t segment_i) {
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

uint8_t stage_height_platform_line_has_same_step_contact_source(const MslBatch* batch, int bi,
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

uint8_t stage_height_platform_line_has_live_scheduler_source(const MslBatch* batch, int bi,
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
  const size_t pidx = (size_t)bi * 2u + (size_t)platform_id;
  return (uint8_t)(batch->state.stage_fod_platform_scheduler_valid[pidx] ||
                   (batch->state.stage_fod_platform_velocity_valid[pidx] &&
                    fabsf(batch->state.stage_fod_platform_velocity[pidx]) > 1.0e-6f));
}

MslStageFloorLine floor_line_world_for_env(const MslBatch* batch, int bi,
                                           const MslStageFloorGraph* g, int line_idx) {
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

uint8_t floor_x_within_line_bounds(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                   int line_idx, float x) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  const float min_x = (l.x0 < l.x1) ? l.x0 : l.x1;
  const float max_x = (l.x0 > l.x1) ? l.x0 : l.x1;
  return (uint8_t)((x >= (min_x - k_floor_x_end_clamp) && x <= (max_x + k_floor_x_end_clamp)) ? 1u
                                                                                              : 0u);
}

uint8_t floor_x_within_line_segment_strict(const MslBatch* batch, int bi,
                                           const MslStageFloorGraph* g, int line_idx, float x) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  const float min_x = (l.x0 < l.x1) ? l.x0 : l.x1;
  const float max_x = (l.x0 > l.x1) ? l.x0 : l.x1;
  return (uint8_t)((x >= min_x && x <= max_x) ? 1u : 0u);
}

uint8_t floor_line_admitted_by_source_callback(const MslBatch* batch, size_t idx,
                                               const MslStageFloorGraph* g, uint32_t stage_id,
                                               int line_idx, uint16_t skip_platform_segment_i,
                                               const MslCommonParams* c);

uint8_t floor_line_y_at_x_for_env(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                  int line_idx, float x, float* y_out) {
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

uint8_t floor_line_normal_for_env(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                  int line_idx, float* nx_out, float* ny_out) {
  if (nx_out == NULL || ny_out == NULL || g == NULL || line_idx < 0 ||
      (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  float nx = -(l.y1 - l.y0);
  float ny = l.x1 - l.x0;
  if (!msl_psvec2_normalize(nx, ny, &nx, &ny)) {
    return 0u;
  }
  *nx_out = nx;
  *ny_out = ny;
  return 1u;
}

uint8_t active_damage_hard_floor_projection_source_accepted(const MslBatch* batch, size_t idx,
                                                            int bi, const MslStageFloorGraph* g,
                                                            int out_line_idx, float cur_bottom_x,
                                                            float cur_bottom_y,
                                                            uint8_t carried_source_floor_contact,
                                                            const MslCommonParams* c) {
  if (batch == NULL || g == NULL || c == NULL || out_line_idx < 0 ||
      (size_t)out_line_idx >= g->line_count || g->lines[(size_t)out_line_idx].is_platform ||
      g->lines[(size_t)out_line_idx].is_ledge) {
    return 1u;
  }
  // Source acceptance boundary for active-hitlag Damage stay-airborne root projection:
  // `mpColl_80044948_Floor` may project from the root when the loaded ECB bottom is positive, but
  // only after the Damage callback's floor pass has source authority for the current floor.
  //
  // Fresh `ftCo_Damage_OnEveryHitlag` displacement still has live x670/x671 SDI state and is the
  // source of the current below-floor root. Once that live SDI window is gone, require the loaded
  // current ECB bottom itself to be floor-owned; this blocks stale root-only projection rows where
  // the root is below the floor but `mpColl_80044628_Floor` would not have accepted bottom contact.
  //
  // Throw-release Damage entry is admitted through the same live Damage callback lifetime before
  // the stale-window check reaches this predicate; see `active_damage_thrown_release_floor_owner`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
  if (is_damage_fly_collision_action(batch->state.action_id[idx])) {
    float bottom_floor_y = 0.0f;
    return (uint8_t)((floor_line_y_at_x_for_env(batch, bi, g, out_line_idx, cur_bottom_x,
                                                &bottom_floor_y) &&
                      cur_bottom_y <= (bottom_floor_y + k_floor_y_bias))
                         ? 1u
                         : 0u);
  }
  if (batch->state.tilt_timer_y_frame_start[idx] < c->sdi_tilt_max_frames) {
    return 1u;
  }
  if (carried_source_floor_contact != 0u) {
    return 1u;
  }

  float bottom_floor_y = 0.0f;
  return (uint8_t)((floor_line_y_at_x_for_env(batch, bi, g, out_line_idx, cur_bottom_x,
                                              &bottom_floor_y) &&
                    cur_bottom_y <= (bottom_floor_y + k_floor_y_bias))
                       ? 1u
                       : 0u);
}

uint8_t active_damage_hitlag_ledge_edge_floorhug_owner(const MslBatch* batch, size_t idx, int bi,
                                                       const MslStageFloorGraph* g,
                                                       uint32_t stage_id, int line_idx,
                                                       float projection_x, const MslCommonParams* c,
                                                       float* edge_x_out, float* edge_y_out) {
  if (batch == NULL || g == NULL || c == NULL || edge_x_out == NULL || edge_y_out == NULL ||
      stage_id != (uint32_t)MSL_STAGE_ID_POKEMON_STADIUM || line_idx < 0 ||
      (size_t)line_idx >= g->line_count || batch->state.hitlag[idx] == 0u ||
      !is_damage_fly_collision_action(batch->state.action_id[idx]) ||
      batch->state.damage_hitlag_downward_sdi_consumed[idx] == 0u) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  if (line->fighter_solid == 0u || line->is_platform || line->is_ledge == 0u ||
      line->platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE) {
    return 0u;
  }

  const MslStageFloorLine world = floor_line_world_for_env(batch, bi, g, line_idx);
  if (fabsf(world.y0 - world.y1) > k_floor_horiz_dy_thresh) {
    return 0u;
  }
  const float edge_scale_x = 1.0f + (c->pokemon_stadium_x34_scale_z / 3.0f);
  const float mid_x = 0.5f * (world.x0 + world.x1);
  MslStageRawLineKind raw_kind = MSL_STAGE_RAW_LINE_UNKNOWN;
  uint16_t raw_segment = 0xFFFFu;
  if (projection_x <= mid_x &&
      stage_collision_raw_line_prev_non_kind(stage_id, line->segment_i, MSL_STAGE_RAW_LINE_FLOOR,
                                             &raw_kind, &raw_segment) &&
      (raw_kind == MSL_STAGE_RAW_LINE_LEFT_WALL || raw_kind == MSL_STAGE_RAW_LINE_RIGHT_WALL)) {
    // Active DamageFly hitlag ledge-edge FloorHug:
    // `ftCo_Damage_OnEveryHitlag` mutates `cur_pos` before `ftCo_DamageFly_Coll`, then
    // `ft_80081DD4 -> mpColl_800477E0 -> mpColl_80044948_Floor` handles stay-airborne floorhug.
    // If the carried floor projection falls off a floor whose previous raw line is a wall, source
    // snaps `cur_pos.x` to the floor's left endpoint and keeps the fighter airborne. MSLSTG01 keeps
    // the ISO raw wall kind (`left_wall`/`right_wall`) rather than source's call-site side label, so
    // the owner requires generated wall adjacency but does not depend on that naming convention.
    // Keep this bounded to generated ledge floor/wall adjacency plus the live consumed-SDI DamageFly
    // callback; ordinary ledge floors without this source callback remain rejected by the generic
    // ledge suppression below.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    //   ftCo_Damage_OnEveryHitlag,ftCo_DamageFly_Coll}
    // refs/melee/src/melee/ft/ft_081B.c::ft_80081DD4
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800477E0,mpColl_80044948_Floor}
    // data/stages/bin/*.bin::MSLSTG01 floor raw_prev/raw_next wall adjacency
    (void)raw_segment;
    *edge_x_out = world.x0 * edge_scale_x;
    *edge_y_out = world.y0;
    return 1u;
  }
  return 0u;
}

uint8_t grounded_height_platform_reproject(const MslBatch* batch, int bi,
                                           const MslStageFloorGraph* g, uint32_t stage_id,
                                           size_t idx, uint16_t action_id, uint16_t action_frame,
                                           int current_line_idx, int* out_line_idx,
                                           float* out_y_corr) {
  if (batch == NULL || g == NULL || g->lines == NULL || out_line_idx == NULL ||
      out_y_corr == NULL || current_line_idx < 0 || (size_t)current_line_idx >= g->line_count ||
      !action_uses_landing_floor_release_coll(action_id)) {
    return 0u;
  }
  const uint8_t landing_entry_transition =
      (uint8_t)(batch->state.seed_prev_action_id[idx] != action_id);
  if (action_frame > 1u && landing_entry_transition == 0u) {
    return 0u;
  }
  // grIzumi updates FoD height-platform JObjs before fighter map callbacks. If a one-step seed
  // starts on the static main floor but the sparse seed lane proves same-step platform contact,
  // Landing_Coll/ft_80084280 consumes the transformed platform floor for this callback instead of
  // stale CollData.floor.index. LandingAir entry can reach this map callback after animation has
  // advanced the visible action frame, so source entry ownership is the action-transition lane, not
  // only post-animation `action_frame <= 1`. Free-running Landing callbacks may also consume a live
  // grIzumi scheduler/velocity floor when the transformed line is within the current CollData ECB
  // lift envelope. Sparse seed-only heights without same-step contact or live scheduler/velocity
  // proof still keep the carried floor.
  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
  // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  int best_line_idx = -1;
  float best_lift = 0.0f;
  const float live_lift_allowance =
      fabsf(batch->state.coll_desired_ecb_bottom_rel_y[idx]) + k_ecb_vertical_unit;
  for (size_t li = 0; li < g->line_count; li++) {
    const uint16_t segment_i = g->lines[li].segment_i;
    if (!stage_collision_floor_line_has_height_platform_transform(stage_id, segment_i) ||
        !stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, segment_i) ||
        !floor_x_within_line_bounds(batch, bi, g, (int)li, batch->state.pos_x[idx])) {
      continue;
    }
    const uint8_t same_step_source =
        stage_height_platform_line_has_same_step_contact_source(batch, bi, stage_id, segment_i);
    const uint8_t live_scheduler_source =
        stage_height_platform_line_has_live_scheduler_source(batch, bi, stage_id, segment_i);
    float line_y = 0.0f;
    if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, batch->state.pos_x[idx], &line_y)) {
      continue;
    }
    const float lift = (line_y + k_floor_y_bias) - batch->state.pos_y[idx];
    if (lift < 0.0f) {
      continue;
    }
    const uint8_t current_line_has_height_transform =
        stage_collision_floor_line_has_height_platform_transform(
            stage_id, g->lines[(size_t)current_line_idx].segment_i);
    const float landing_entry_vertical_reach =
        fabsf(batch->state.pos_y[idx] - batch->state.floor_sweep_prev_pos_y[idx]) +
        k_ecb_vertical_unit;
    const uint8_t landing_entry_live_release_retry =
        (uint8_t)(landing_entry_transition != 0u && live_scheduler_source != 0u &&
                  current_line_has_height_transform == 0u && lift <= landing_entry_vertical_reach);
    if (!same_step_source && (!live_scheduler_source ||
                              (!landing_entry_live_release_retry && lift > live_lift_allowance) ||
                              current_line_has_height_transform)) {
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

uint8_t hidden_height_platform_remaps_to_solid_floor(const MslBatch* batch, int bi,
                                                     const MslStageFloorGraph* g, uint32_t stage_id,
                                                     int current_line_idx, float root_x,
                                                     float root_y, int* out_line_idx) {
  if (batch == NULL || g == NULL || g->lines == NULL || out_line_idx == NULL ||
      current_line_idx < 0 || (size_t)current_line_idx >= g->line_count) {
    return 0u;
  }
  const uint16_t current_segment = g->lines[(size_t)current_line_idx].segment_i;
  if (!stage_collision_floor_line_has_height_platform_transform(stage_id, current_segment) ||
      (!stage_height_platform_line_has_current_source(batch, bi, stage_id, current_segment) &&
       !stage_height_platform_line_has_live_scheduler_source(batch, bi, stage_id,
                                                             current_segment))) {
    return 0u;
  }
  float platform_y = 0.0f;
  if (!floor_line_y_at_x_for_env(batch, bi, g, current_line_idx, root_x, &platform_y) ||
      platform_y >= k_floor_y_bias) {
    return 0u;
  }

  int best_line_idx = -1;
  float best_y = -FLT_MAX;
  for (size_t li = 0; li < g->line_count; li++) {
    const MslStageFloorLine* line = &g->lines[li];
    if ((int)li == current_line_idx || line->is_platform || line->is_ledge ||
        stage_collision_floor_line_has_platform_transform(stage_id, line->segment_i) ||
        !stage_collision_floor_line_is_runtime_fighter_solid(stage_id, line->segment_i)) {
      continue;
    }
    float line_y = 0.0f;
    if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, root_x, &line_y)) {
      continue;
    }
    if (line_y <= platform_y + k_floor_y_bias || line_y > root_y + k_ecb_vertical_unit) {
      continue;
    }
    if (best_line_idx < 0 || line_y > best_y ||
        (line_y == best_y && line->segment_i < g->lines[(size_t)best_line_idx].segment_i)) {
      best_line_idx = (int)li;
      best_y = line_y;
    }
  }
  if (best_line_idx < 0) {
    return 0u;
  }
  // Height-platform stage objects hide by moving the source JObj line to its generated hidden
  // target below the stage. A grounded fighter whose CollData.floor still names that height
  // platform should continue through the ordinary solid floor under the root instead of projecting
  // onto the hidden platform line.
  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // data/stages/bin/*.bin::MSLSTG01 platform_motions.hidden_target_height
  *out_line_idx = best_line_idx;
  return 1u;
}

uint8_t floor_line_x_near_endpoint_for_env(const MslBatch* batch, int bi,
                                           const MslStageFloorGraph* g, int line_idx, float x,
                                           float max_dist) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count || max_dist < 0.0f) {
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  const float d0 = fabsf(x - l.x0);
  const float d1 = fabsf(x - l.x1);
  return (d0 <= max_dist || d1 <= max_dist) ? 1u : 0u;
}

uint8_t floor_find_low_raw_floor_contact(const MslBatch* batch, size_t idx, int bi,
                                         const MslStageFloorGraph* g, uint32_t stage_id, float x,
                                         float y, float max_lift, uint16_t skip_platform_segment_i,
                                         const MslCommonParams* c, int* out_line_idx, float* out_y,
                                         float* out_nx, float* out_ny) {
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
    if (!msl_psvec2_normalize(nx, ny, &nx, &ny)) {
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
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class2 COMMON_GROUNDED_COLL;
  // retained later-owner class GROUNDED_STAGE_OBJECT_CARRY_COLL)
  if (action_uses_landing_floor_release_coll(action_id) && action_frame <= 1u) {
    return 0u;
  }
  if (msl_motion_state_common_class2_has_fast(action_id, MSL_MS_CLASS2_COMMON_GROUNDED_COLL)) {
    return 1u;
  }
  if (msl_motion_state_common_class2_has_fast(action_id, MSL_MS_CLASS2_COMMON_GROUNDED_B2DC_COLL) ||
      msl_motion_state_common_class2_has_fast(action_id, MSL_MS_CLASS2_COMMON_GROUNDED_B4B0_COLL)) {
    // Phase-3 grounded floor persistence also covers `ft_800827A0` users such as EscapeF/B/N:
    // the callback writes CollData.cur_pos back after `mpColl_8004B2DC`, whose floor traversal
    // consumes mpLib_8004DD90_Floor's signed correction when the current floor remains valid.
    // This is still bounded to generated-slope/platform correction by the callers below.
    // refs/melee/src/melee/ft/ft_081B.c::{ft_800827A0,ft_80084104}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B2DC,mpColl_8004B4B0}
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class2 COMMON_GROUNDED_B2DC/B4B0_COLL)
    return 1u;
  }
  if (action_id == (uint16_t)MSL_ACT_ESCAPE_F || action_id == (uint16_t)MSL_ACT_ESCAPE_B ||
      action_id == (uint16_t)MSL_ACT_ESCAPE_N) {
    // The current MSLMSO01 class2 table does not expose EscapeF/B/N as common grounded B2DC, but
    // source does: EscapeF/B call `ftCo_Escape_Coll`, EscapeN calls `ftCo_EscapeN_Coll`, and both
    // delegate to `ft_80084104 -> ft_800827A0 -> mpColl_8004B2DC` before writing CollData.cur_pos
    // back to the fighter. Keep this as a named source owner until the extractor promotes the
    // class bit, rather than using a replay-row or stage predicate.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Escape.c::{
    //   ftCo_Escape_Coll,ftCo_EscapeN_Coll}
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80084104,ft_800827A0}
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B2DC
    return 1u;
  }
  return msl_motion_state_common_class_has_fast(action_id,
                                                MSL_MS_CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL);
}

uint8_t grounded_action_allows_height_platform_y_correction(uint16_t action_id,
                                                            uint16_t action_frame,
                                                            uint8_t char_id) {
  if (grounded_action_allows_platform_carry_y_correction(action_id, action_frame)) {
    return 1u;
  }
  // KneeBend, neutral Passive, and grounded Fox/Falco SpecialN route through
  // ft_80083F88 -> ft_80082708 -> mpColl_8004B108. That source path still writes CollData.cur_pos
  // back to the fighter when the persisted floor remains valid, so a moving grIzumi platform must
  // be allowed to apply the signed mpLib_8004DD90_Floor correction while the fighter is still
  // grounded. Use MSLMSO01 for the callback owner, a named neutral Passive source action, and the
  // decomp-derived MotionState move_id table for the grounded SpecialN distinction instead of
  // carrying a local SpecialN action range.
  //
  // Do not admit the whole generated FT80083F88 class here: downed and PassiveStand callbacks
  // share that helper but route through separate downed/getup owners and should not inherit this
  // moving-platform carry. Neutral Passive is admitted explicitly because ftCo_Passive_Coll itself
  // is only a thin ft_80083F88 wrapper, so its live CollData floor remains the B108 source owner.
  //
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_Passive_Coll
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //   ftFx_SpecialNStart_Coll,ftFx_SpecialNLoop_Coll,ftFx_SpecialNEnd_Coll}
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
  // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class2 COMMON_GROUNDED_B108_COLL)
  // data/attack_id/move_id/{fox,falco}.bin (MotionState.move_id == FtMoveId_SpecialN)
  if (msl_motion_state_common_class2_has_fast(action_id, MSL_MS_CLASS2_COMMON_GROUNDED_B108_COLL)) {
    return 1u;
  }
  if (action_id == (uint16_t)MSL_ACT_PASSIVE) {
    return 1u;
  }
  if (!msl_motion_state_class_has(char_id, action_id, MSL_MS_CLASS_FT80083F88_GROUND_TO_AIR_COLL)) {
    return 0u;
  }
  enum { MSL_FT_MOVE_ID_SPECIAL_N = 18u };
  return (uint8_t)(attack_id_move_id_from_action(char_id, action_id) ==
                   (uint16_t)MSL_FT_MOVE_ID_SPECIAL_N);
}

static inline uint8_t grounded_action_allows_b108_generated_slope_y_correction(uint16_t action_id,
                                                                               uint8_t char_id) {
  // Source/table-backed B108 floor persistence owner:
  // ft_80083F88 delegates through ft_80082708 -> mpColl_8004B108, whose CollData.cur_pos
  // writeback consumes mpLib_8004DD90_Floor's signed correction. Keep this separate from
  // transformed-platform carry: downed/passive callbacks should not inherit moving-platform
  // deltas, but they still follow generated legal-stage slopes while their current floor remains
  // valid.
  //
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
  // refs/melee/src/melee/mp/{mpcoll.c::mpColl_8004B108,mplib.c::mpLib_8004DD90_Floor}
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class FT80083F88_GROUND_TO_AIR_COLL)
  if (msl_motion_state_common_class2_has_fast(action_id, MSL_MS_CLASS2_COMMON_GROUNDED_B108_COLL)) {
    return 1u;
  }
  if (msl_motion_state_common_class_has_fast(action_id,
                                             MSL_MS_CLASS_FT80083F88_GROUND_TO_AIR_COLL)) {
    return 1u;
  }
  return msl_motion_state_class_has(char_id, action_id, MSL_MS_CLASS_FX_SPECIALS_GROUND_B108_COLL);
}

uint8_t grounded_action_allows_stage_object_platform_carry(uint16_t action_id) {
  // Table-backed MotionState collision owner. The generated class groups grounded callbacks that
  // preserve an existing floor attachment through common map-collision owners and should inherit a
  // moving stage object's transform before projection. DownBound/DownWait/DownStand/DownSpot,
  // Passive, and DownDamage callbacks are intentionally excluded by the extractor because they
  // route through separate downed/damage owners.
  //
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80084280,ft_800844EC,ft_80084104,ft_800845B4}
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class2 COMMON_GROUNDED_COLL;
  // retained later-owner class GROUNDED_STAGE_OBJECT_CARRY_COLL)
  if (msl_motion_state_common_class2_has_fast(action_id, MSL_MS_CLASS2_COMMON_GROUNDED_COLL)) {
    return 1u;
  }
  return msl_motion_state_common_class_has_fast(action_id,
                                                MSL_MS_CLASS_GROUNDED_STAGE_OBJECT_CARRY_COLL);
}

int mpcoll_find_current_randall_floor_line_at_root(const MslBatch* batch, int bi,
                                                   const MslStageFloorGraph* g, float root_x,
                                                   float root_y) {
  if (batch == NULL || bi < 0 || g == NULL || g->lines == NULL) {
    return -1;
  }
  const uint32_t stage_id = batch->state.stage_id[bi];
  const int randall_idx = stage_collision_randall_floor_line_index(stage_id);
  if (randall_idx < 0 || (size_t)randall_idx >= g->line_count) {
    return -1;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)randall_idx];
  if (!line->is_platform || !line->fighter_solid ||
      !stage_collision_floor_line_has_randall_platform_transform(stage_id, line->segment_i)) {
    return -1;
  }
  const MslStageFloorLine world = floor_line_world_for_env(batch, bi, g, randall_idx);
  const float min_x = (world.x0 < world.x1) ? world.x0 : world.x1;
  const float max_x = (world.x0 > world.x1) ? world.x0 : world.x1;
  if (root_x >= (min_x - k_floor_x_end_clamp) && root_x <= (max_x + k_floor_x_end_clamp) &&
      fabsf(root_y - (world.y0 + k_floor_y_bias)) <= (2.0f * k_floor_y_bias)) {
    return randall_idx;
  }

  float platform_dx = 0.0f;
  float platform_dy = 0.0f;
  if (!stage_collision_floor_line_motion_delta(batch, bi, line, &platform_dx, &platform_dy)) {
    return -1;
  }
  const float prev_min_x = min_x - platform_dx;
  const float prev_max_x = max_x - platform_dx;
  const float sweep_min_x = ((prev_min_x < min_x) ? prev_min_x : min_x) - k_floor_x_end_clamp;
  const float sweep_max_x = ((prev_max_x > max_x) ? prev_max_x : max_x) + k_floor_x_end_clamp;
  const float prev_y = world.y0 - platform_dy;
  const float sweep_min_y = ((prev_y < world.y0) ? prev_y : world.y0) - (2.0f * k_floor_y_bias);
  const float sweep_max_y = ((prev_y > world.y0) ? prev_y : world.y0) + (2.0f * k_floor_y_bias);
  if (root_x >= sweep_min_x && root_x <= sweep_max_x && root_y >= (sweep_min_y + k_floor_y_bias) &&
      root_y <= (sweep_max_y + k_floor_y_bias)) {
    return randall_idx;
  }
  return -1;
}

uint8_t grounded_entry_same_step_height_platform_admits_line(const MslBatch* batch, int bi,
                                                             size_t idx, uint32_t stage_id,
                                                             uint16_t action_id,
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

uint8_t floor_line_is_generated_stage_slope(const MslBatch* batch, int bi,
                                            const MslStageFloorGraph* g, int line_idx) {
  if (batch == NULL || bi < 0 || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  if (g->lines[(size_t)line_idx].fighter_solid == 0u) {
    return 0u;
  }
  const MslStageFloorLine l = floor_line_world_for_env(batch, bi, g, line_idx);
  return (uint8_t)(l.y0 != l.y1);
}

uint8_t floor_line_is_generated_sloped_ledge(const MslBatch* batch, int bi,
                                             const MslStageFloorGraph* g, int line_idx) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count ||
      g->lines[(size_t)line_idx].is_ledge == 0u) {
    return 0u;
  }
  return floor_line_is_generated_stage_slope(batch, bi, g, line_idx);
}

uint8_t floor_line_is_terminal_cardinal_hard_floor(const MslBatch* batch, int bi,
                                                   const MslStageFloorGraph* g, uint32_t stage_id,
                                                   int line_idx) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  if (line->fighter_solid == 0u || line->is_platform || line->is_ledge ||
      stage_collision_floor_line_has_platform_transform(stage_id, line->segment_i) ||
      floor_line_is_generated_stage_slope(batch, bi, g, line_idx)) {
    return 0u;
  }
  // This is the extracted line-graph shape for the source-proven cardinal-floor DamageAir active
  // hitlag owner: a static hard-floor chain with terminal ledge endpoints and no passable/moving
  // floor support elsewhere in the stage graph. Stages with platforms or transformed stage-object
  // floors need an explicit bottom-sweep/source-owner proof instead of borrowing this carried-floor
  // branch.
  // data/stages/bin/*.bin::MSLSTG01 line flags, links, and platform transform metadata
  for (size_t i = 0; i < g->line_count; i++) {
    if (g->lines[i].fighter_solid != 0u &&
        (g->lines[i].is_platform ||
         stage_collision_floor_line_has_platform_transform(stage_id, g->lines[i].segment_i))) {
      return 0u;
    }
  }

  const int prev_idx = line->prev;
  const int next_idx = line->next;
  if (prev_idx < 0 || next_idx < 0 || (size_t)prev_idx >= g->line_count ||
      (size_t)next_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine* prev = &g->lines[(size_t)prev_idx];
  const MslStageFloorLine* next = &g->lines[(size_t)next_idx];
  return (uint8_t)((prev->is_ledge && prev->prev < 0 && next->is_ledge && next->next < 0) ? 1u
                                                                                          : 0u);
}

uint8_t fallspecial_sustained_same_terminal_cardinal_floor_delay(
    const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    uint16_t action_id, uint16_t seed_ground_id, uint16_t ground_id, int ground_line_idx,
    float contact_y, float root_y) {
  if (batch == NULL || g == NULL || !is_common_fallspecial_action(action_id) ||
      ground_id != seed_ground_id || seed_ground_id == 0xFFFFu ||
      batch->state.seed_prev_action_id[idx] != action_id ||
      batch->state.seed_prev_action_frame[idx] > 5 || batch->state.action_frame[idx] < 5 ||
      batch->state.action_frame[idx] > 6 || batch->state.speed_y_self[idx] >= 0.0f ||
      batch->state.fall_fast[idx] == 0u || batch->state.floor_sweep_prev_pos_y[idx] <= contact_y ||
      root_y >= contact_y) {
    return 0u;
  }

  int line_idx = ground_line_idx;
  if (line_idx < 0 || (size_t)line_idx >= g->line_count ||
      g->lines[(size_t)line_idx].segment_i != ground_id) {
    line_idx = stage_collision_floor_line_index(stage_id, ground_id);
  }
  // Sustained fastfall FallSpecial can carry CollData.floor.index through one same-floor root
  // crossing before the source bottom/edge floor phase publishes LandingFallSpecial. Keep this as
  // a narrow sustained-action source policy; freshly entered aerial Side-B end -> FallSpecial
  // reaches the same ftCo_80096CC8 hard-floor owner earlier and must not reuse this delay.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::{
  //   ftCo_FallSpecial_Coll,ftCo_80096CC8,ftCo_80096D28}
  // refs/melee/src/melee/ft/ft_081B.c::ft_80083090
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80047E14,mpColl_80044628_Floor}
  return floor_line_is_terminal_cardinal_hard_floor(batch, bi, g, stage_id, line_idx);
}

uint8_t floor_line_is_terminal_cardinal_ledge_floor(const MslBatch* batch, int bi,
                                                    const MslStageFloorGraph* g, uint32_t stage_id,
                                                    int line_idx) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  if (line->fighter_solid == 0u || !line->is_ledge || line->is_platform ||
      stage_collision_floor_line_has_platform_transform(stage_id, line->segment_i)) {
    return 0u;
  }
  // Terminal ledge-floor companion to `floor_line_is_terminal_cardinal_hard_floor`: the line is a
  // fighter-solid ledge segment whose only floor neighbor is the static cardinal hard-floor chain.
  // Stages with platform/transformed support elsewhere stay out of this root-crossing guard.
  // data/stages/bin/*.bin::MSLSTG01 line flags, links, and platform transform metadata
  const int prev_idx = line->prev;
  const int next_idx = line->next;
  if (prev_idx >= 0 && (size_t)prev_idx < g->line_count &&
      floor_line_is_terminal_cardinal_hard_floor(batch, bi, g, stage_id, prev_idx)) {
    return 1u;
  }
  if (next_idx >= 0 && (size_t)next_idx < g->line_count &&
      floor_line_is_terminal_cardinal_hard_floor(batch, bi, g, stage_id, next_idx)) {
    return 1u;
  }
  return 0u;
}

static inline int8_t platform_pass_current_stick_y(const MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.input_main_y == NULL) {
    return 0;
  }
  return batch->state.input_main_y[idx];
}

static inline int8_t platform_pass_current_raw_stick_y(const MslBatch* batch, size_t idx) {
  enum {
    MSL_LOCAL_UCF_PADBUF_SIZE = 4u,
    MSL_LOCAL_UCF_PADBUF_MASK = MSL_LOCAL_UCF_PADBUF_SIZE - 1u
  };
  if (batch == NULL || batch->state.ucf_padbuf_index == NULL ||
      batch->state.ucf_padbuf_stick_y == NULL) {
    return platform_pass_current_stick_y(batch, idx);
  }
  const uint8_t base = batch->state.ucf_padbuf_index[idx];
  const uint8_t slot = (uint8_t)(base & (uint8_t)MSL_LOCAL_UCF_PADBUF_MASK);
  return batch->state.ucf_padbuf_stick_y[idx * (size_t)MSL_LOCAL_UCF_PADBUF_SIZE + (size_t)slot];
}

uint8_t platform_pass_input_below_raw_threshold(const MslBatch* batch, size_t idx,
                                                const MslCommonParams* c) {
  if (c == NULL) {
    return 0u;
  }
  // Existing transformed-platform pass-through seed lanes are derived from Slippi's raw signed
  // stick byte before the simulator's 80-unit UCF clamp scale. Keep these legacy floor-skip and
  // suppression owners on the raw pad-buffer scale; source-callback paths that require
  // callback-visible `fp->input.lstick.y` use `platform_pass_input_below_current_threshold`
  // explicitly.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096CC8
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_procMap}
  const int threshold_i8 = (int)floorf(c->platform_air_land_stick_y_threshold * 127.0f);
  const int8_t stick_y_i8 = platform_pass_current_raw_stick_y(batch, idx);
  return ((int)stick_y_i8 <= threshold_i8) ? 1u : 0u;
}

void publish_common_air_transformed_platform_skip_from_root_crossing(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    MslMpcollSourcePhases source_phases, const MslCommonParams* c) {
  if (batch == NULL || c == NULL || g == NULL || g->lines == NULL ||
      batch->state.floor_skip_segment_id == NULL ||
      batch->state.floor_skip_segment_id[idx] != 0xFFFFu || batch->state.on_ground[idx] != 0u) {
    return;
  }
  if (!mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_PLATFORM_PASS)) {
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
      msl_mpcoll_update_floor_skip(batch, idx, segment_i);
      return;
    }
  }
}

void publish_attackair_transformed_platform_skip_from_root_crossing(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    MslMpcollSourcePhases source_phases, const MslCommonParams* c) {
  if (batch == NULL || c == NULL || g == NULL || g->lines == NULL ||
      batch->state.floor_skip_segment_id == NULL ||
      batch->state.floor_skip_segment_id[idx] != 0xFFFFu || batch->state.on_ground[idx] != 0u) {
    return;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (!mpcoll_source_phases_has(source_phases, MSL_MPCOLL_PHASE_AIR_471F8) ||
      !is_attackair_action(action_id) || batch->state.prev_action_id[idx] != action_id) {
    return;
  }
  const uint16_t smid = msl_motion_state_submotion_id(batch->state.char_id[idx], action_id);
  if (smid != (uint16_t)MSL_SM_ATTACK_AIR_N && smid != (uint16_t)MSL_SM_ATTACK_AIR_LW &&
      smid != (uint16_t)MSL_SM_ATTACK_AIR_HI) {
    return;
  }
  const int16_t first_create_frame =
      move_tables_attackair_first_create_hitbox_frame(batch->state.char_id[idx], action_id);
  const int16_t second_create_frame =
      move_tables_attackair_second_create_hitbox_frame(batch->state.char_id[idx], action_id);
  const uint8_t attackairlw_live_publication_window =
      (action_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW && first_create_frame >= 0 &&
       second_create_frame >= 0 && batch->state.action_frame[idx] >= (uint16_t)first_create_frame &&
       batch->state.action_frame[idx] <= (uint16_t)second_create_frame)
          ? 1u
          : 0u;
  if (attackairlw_live_publication_window &&
      (batch->state.floor_sweep_prev_runtime_owned[idx] != 0u ||
       batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] != 0u)) {
    return;
  }
  const int8_t stick_y_i8 = platform_pass_current_stick_y(batch, idx);
  if (stick_i8_to_unit(stick_y_i8) > c->platform_air_land_stick_y_threshold ||
      stick_i8_to_unit(batch->state.prev_input_main_y[idx]) >
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
        stage_height_platform_line_has_current_source(batch, bi, stage_id, segment_i) ||
        stage_height_platform_line_has_live_scheduler_source(batch, bi, stage_id, segment_i) ||
        segment_i == batch->state.ground_id[idx]) {
      continue;
    }
    if (!floor_x_within_line_segment_strict(batch, bi, g, (int)li, x)) {
      continue;
    }
    float line_y = 0.0f;
    if (!floor_line_y_at_x_for_env(batch, bi, g, (int)li, x, &line_y)) {
      continue;
    }
    if (prev_y > (line_y + k_floor_y_bias) && y < (line_y - k_ecb_vertical_unit)) {
      // Sustained AttackAir_Coll goes through ft_80082C74 -> mpColl_800471F8 rather than the
      // common ftCo_80096CC8 callback, but source CollData still preserves the floor-skip owner
      // once a down-held transformed soft platform has been passed. This is the pre-floor-hit
      // publication half of the generated FoD seed lane for the extracted AttackAirN/Lw/Hi
      // platform-owner submotions: the in-span root has crossed below the live height-platform
      // line, the callback is still sustained AttackAir, and no direct/contact source owns the
      // platform for the current frame. Live scheduler velocity proves the platform pose but is not
      // an accepted AttackAir floor producer. Source still admits platform-pass rejection near the
      // FoD platform endpoints when the root is inside the extracted line span; current-source
      // contacts and AttackAirB/F rows keep the ordinary AttackAir landing path through
      // platform_floor_skip_segment_id().
      // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 submotion_id
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
      // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpUpdateFloorSkip}
      // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
      msl_mpcoll_update_floor_skip(batch, idx, segment_i);
      return;
    }
  }
}

uint8_t jumpaerial_terminal_fastfall_descent(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const MslCharParams* chp = msl_char_params_fast(batch->state.char_id[idx]);
  if (chp == NULL || !isfinite(chp->terminal_vel) || !(chp->terminal_vel > 0.0f)) {
    return 0u;
  }
  return (uint8_t)(batch->state.speed_y_self[idx] <=
                   -(chp->terminal_vel - k_floor_horiz_dy_thresh));
}

float specialhi_understage_floor_reject_clearance(const MslEcbWorldPoints* prev_ecb) {
  if (prev_ecb == NULL || !isfinite(prev_ecb->bottom_rel_y)) {
    return k_ecb_vertical_unit;
  }
  // Decomp ECB ownership gives the floor/ceiling callbacks a live CollData ECB span. Use the
  // previous bottom-to-root extent plus mpColl's minimum vertical ECB unit as the shallow-penetration
  // allowance instead of a stage/replay-local depth constant.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80042384,mpColl_LoadECB_JObj}
  return fabsf(prev_ecb->bottom_rel_y) + k_ecb_vertical_unit;
}

uint8_t specialhi_understage_floor_clip_action(uint8_t char_id, uint16_t action_id) {
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id);
  return (uint8_t)(fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI ||
                   fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL);
}

uint8_t specialhi_floor_candidate_starts_below_source_floor(uint8_t char_id, uint16_t action_id,
                                                            float prev_root_y, float prev_bottom_y,
                                                            float cur_bottom_y, float floor_y,
                                                            float speed_y_self) {
  if (!specialhi_understage_floor_clip_action(char_id, action_id) || speed_y_self >= 0.0f) {
    return 0u;
  }
  const float floor_top = floor_y + k_floor_y_bias;
  return (prev_root_y < floor_top && prev_bottom_y < floor_top && cur_bottom_y < floor_top) ? 1u
                                                                                            : 0u;
}

uint8_t specialairhi_floor_contact_angle_continues_launch(uint16_t action_id, uint8_t char_id,
                                                          float floor_normal_x,
                                                          float floor_normal_y, float speed_x_self,
                                                          float speed_y_self) {
  if (msl_motion_state_fx_special_kind(char_id, action_id) != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) {
    return 0u;
  }
  const MslCharParams* chp = msl_char_params_fast(char_id);
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

float mpcoll_floor_projection_lift_allowance(const MslEcbWorldPoints* cur_ecb) {
  if (cur_ecb == NULL || !isfinite(cur_ecb->bottom_rel_y)) {
    return k_ecb_vertical_unit;
  }
  // Floor projection owners should be bounded by the live CollData ECB bottom-to-root extent, not a
  // replay-local literal. Source mpColl loads the ECB for the callback before floor projection and
  // keeps a minimum vertical unit while tightening the span.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80042384}
  return fabsf(cur_ecb->bottom_rel_y) + k_ecb_vertical_unit;
}

uint8_t grounded_persistence_allows_signed_dd90_y_correction(
    const MslBatch* batch, int bi, const MslStageFloorGraph* g, int current_line_idx,
    int projected_line_idx, size_t idx, uint16_t action_id, uint16_t action_frame) {
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
  if (action_uses_landing_floor_release_coll(action_id)) {
    // Every already-grounded Landing/LandingAir/LandingFallSpecial callback reaches
    // `ft_80084280 -> mpColl_8004B4B0`. Its first `mpColl_800488F4` success applies DD90's signed
    // correction immediately, including frame-0/1 ECB growth and connected slope-to-flat
    // traversal. The airborne landing handoff is owned by a different callback path before this
    // grounded-persistence helper is reached, so action age is not an admission predicate here.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Coll
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_Coll
    // refs/melee/src/melee/ft/ft_081B.c::ft_80084280
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B4B0,mpColl_800488F4}
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    return 1u;
  }
  if (generated_slope_owner && grounded_action_allows_b108_generated_slope_y_correction(
                                   action_id, batch->state.char_id[idx])) {
    // Yoshi's Story exposes generated hard-floor slopes where `ft_80083F88 -> mpColl_8004B108`
    // callbacks keep grounded CollData.cur_pos pinned to the current floor as overlap nudge or
    // ground motion moves the root along the slope. This admits only signed DD90 slope projection,
    // not the moving-platform carry deliberately excluded from downed/passive owners above.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c::ftCo_Passive_Coll
    // refs/melee/src/melee/ft/ft_081B.c::{ft_80083F88,ft_80082708}
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    return 1u;
  }
  if (grounded_action_allows_height_platform_y_correction(action_id, action_frame,
                                                          batch->state.char_id[idx]) &&
      generated_slope_owner) {
    // Yoshi's Story and FoD both expose admitted sloped floor segments in MSLSTG01. Grounded
    // persistence should keep the fighter attached through source graph traversal, including
    // slope-to-flat endpoint handoffs, rather than preserving a stale root height over raised lip
    // segments. KneeBend uses ft_80083F88 -> ft_80082708 -> mpColl_8004B108, so it consumes the
    // same signed DD90 floor correction on generated slopes as on moving FoD height platforms.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Coll
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    return 1u;
  }
  if (!grounded_action_allows_height_platform_y_correction(action_id, action_frame,
                                                           batch->state.char_id[idx])) {
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

void floor_ed5c_endpoints(const MslBatch* batch, int bi, const MslStageFloorGraph* g, int line_idx,
                          float* x0_out, float* y0_out, float* x1_out, float* y1_out) {
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

uint8_t floor_line_y_at_x_ed5c_for_env(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
                                       int line_idx, float x, float* y_out) {
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

int msl_mplib_8004dd90_floor(const MslBatch* batch, int bi, const MslStageFloorGraph* g,
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
    if (!msl_psvec2_normalize(nx, ny, &nx, &ny)) {
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

uint8_t mpcoll_grounded_final_root_flat_seam_remap(const MslMpcollContext* ctx,
                                                   MslMpcollFloorContact* contact) {
  if (ctx == NULL || ctx->batch == NULL || ctx->floor_graph == NULL || contact == NULL ||
      !ctx->was_grounded) {
    return 0u;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  const int line_idx = stage_collision_floor_line_index(ctx->stage_id, contact->ground_id);
  if (line_idx < 0 || (size_t)line_idx >= ctx->floor_graph->line_count) {
    return 0u;
  }

  const MslStageFloorLine line =
      floor_line_world_for_env(batch, ctx->bi, ctx->floor_graph, line_idx);
  if (line.is_platform || line.platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE ||
      fabsf(line.y1 - line.y0) > k_floor_horiz_dy_thresh ||
      floor_x_within_line_segment_strict(batch, ctx->bi, ctx->floor_graph, line_idx,
                                         batch->state.pos_x[idx])) {
    return 0u;
  }
  if (is_capture_lw_allow_ground_to_air_collision_action(ctx->action_id) &&
      batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_STAND_B &&
      batch->state.action_frame[idx] <= 2 && line.is_ledge &&
      !stage_collision_floor_line_is_sloped(ctx->stage_id, line.segment_i)) {
    const uint8_t owner_p = batch->state.grab_owner_port[idx];
    if (owner_p < batch->config.num_players) {
      const size_t oidx = msl_idx_player(ctx->bi, (int)owner_p);
      if (batch->state.action_id[oidx] == (uint16_t)MSL_ACT_CATCH_DASH_PULL &&
          batch->state.on_ground[oidx] != 0u && batch->state.ground_id[oidx] != line.segment_i &&
          line.joint_id == 0) {
        // Low-capture flat ledge carry:
        // generic grounded final-root seam refresh follows `mpLib_8004DD90_Floor` to the adjacent
        // main floor after the attached victim root is pulled across the endpoint. Source
        // first PassiveStandB -> CapturePulledLw callback keeps the original flat ledge
        // `CollData.floor.index` when the victim is attached to an owner on another floor; sloped
        // same-floor ledges remain on the floor-loss PulledHi path in grab_attachment.c.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
        //   ftCo_CapturePulledLw_Phys,ftCo_CapturePulledLw_Coll}
        // refs/melee/src/melee/ft/ft_081B.c::ft_8008403C
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
        // data/stages/bin/*.bin::MSLSTG01 segment.{ledge,endpoints}
        return 0u;
      }
    }
  }

  float y_corr = 0.0f;
  float nx = 0.0f;
  float ny = 1.0f;
  const int root_line_idx =
      msl_mplib_8004dd90_floor(batch, ctx->bi, ctx->floor_graph, line_idx, batch->state.pos_x[idx],
                               batch->state.pos_y[idx], &y_corr, &nx, &ny);
  if (root_line_idx < 0 || root_line_idx == line_idx ||
      (size_t)root_line_idx >= ctx->floor_graph->line_count ||
      !floor_lines_connected(ctx->floor_graph, line_idx, root_line_idx)) {
    return 0u;
  }

  const MslStageFloorLine root_line =
      floor_line_world_for_env(batch, ctx->bi, ctx->floor_graph, root_line_idx);
  if (root_line.is_platform ||
      root_line.platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE ||
      fabsf(root_line.y1 - root_line.y0) > k_floor_horiz_dy_thresh ||
      fabsf(root_line.y0 - line.y0) > k_floor_y_bias) {
    return 0u;
  }

  // Grounded mpColl's final floor.index belongs to CollData.cur_pos/root after the full
  // mpColl_8004ACE4 substep sequence, not only the first accepted ECB-bottom projection. Source
  // can apply grounded squeeze/overlap corrections before final floor writeback; at flat connected
  // legal-stage seams, mpLib_8004DD90_Floor then returns the adjacent floor line once the final
  // root is past the persisted segment endpoint.
  // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_8004ACE4}
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // data/stages/bin/*.bin::MSLSTG01 floor prev/next links
  contact->ground_id = ctx->floor_graph->lines[(size_t)root_line_idx].segment_i;
  contact->contact_x = batch->state.pos_x[idx];
  contact->contact_y = batch->state.pos_y[idx] + y_corr - k_floor_y_bias;
  contact->normal_x = nx;
  contact->normal_y = ny;
  return 1u;
}

static uint8_t mpcoll_refresh_grounded_root_flat_seam(MslBatch* batch, int bi, size_t idx,
                                                      uint32_t stage_id,
                                                      const MslStageFloorGraph* g) {
  if (batch == NULL || g == NULL || batch->state.on_ground[idx] == 0u ||
      batch->state.ground_id[idx] == 0xFFFFu) {
    return 0u;
  }
  const int line_idx = stage_collision_floor_line_index(stage_id, batch->state.ground_id[idx]);
  if (line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  const MslStageFloorLine line = floor_line_world_for_env(batch, bi, g, line_idx);
  if (line.is_platform || line.platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE ||
      fabsf(line.y1 - line.y0) > k_floor_horiz_dy_thresh ||
      floor_x_within_line_segment_strict(batch, bi, g, line_idx, batch->state.pos_x[idx])) {
    return 0u;
  }

  float y_corr = 0.0f;
  float nx = 0.0f;
  float ny = 1.0f;
  const int root_line_idx = msl_mplib_8004dd90_floor(
      batch, bi, g, line_idx, batch->state.pos_x[idx], batch->state.pos_y[idx], &y_corr, &nx, &ny);
  if (root_line_idx < 0 || root_line_idx == line_idx || (size_t)root_line_idx >= g->line_count ||
      !floor_lines_connected(g, line_idx, root_line_idx)) {
    return 0u;
  }

  const MslStageFloorLine root_line = floor_line_world_for_env(batch, bi, g, root_line_idx);
  if (root_line.is_platform ||
      root_line.platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE ||
      fabsf(root_line.y1 - root_line.y0) > k_floor_horiz_dy_thresh ||
      fabsf(root_line.y0 - line.y0) > k_floor_y_bias) {
    return 0u;
  }

  // Source stage displacement can move grounded `cur_pos` after the main mpColl floor pass while
  // leaving CollData.floor on the same connected legal-stage floor chain. Refresh the final
  // floor.index from mpLib_8004DD90_Floor so post-frame state names the floor under the root after
  // that displacement, rather than the earlier ECB-bottom projection.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_GetWindOffsetVec
  // refs/melee/src/melee/gr/groldpupupu.c::fn_802112F4
  // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
  // data/stages/bin/*.bin::MSLSTG01 floor prev/next links
  batch->state.ground_id[idx] = g->lines[(size_t)root_line_idx].segment_i;
  batch->state.ground_contact_x[idx] = batch->state.pos_x[idx];
  batch->state.ground_contact_y[idx] = batch->state.pos_y[idx] + y_corr - k_floor_y_bias;
  batch->state.ground_normal_x[idx] = nx;
  batch->state.ground_normal_y[idx] = ny;
  if (batch->state.coll_floor_result_valid[idx] != 0u) {
    batch->state.coll_floor_result_segment_id[idx] = batch->state.ground_id[idx];
    batch->state.coll_floor_result_contact_x[idx] = batch->state.ground_contact_x[idx];
    batch->state.coll_floor_result_contact_y[idx] = batch->state.ground_contact_y[idx];
    batch->state.coll_floor_result_normal_x[idx] = nx;
    batch->state.coll_floor_result_normal_y[idx] = ny;
  }
  return 1u;
}

void mpcoll_ground_refresh_grounded_root_floor_index(MslBatch* batch, int batch_index,
                                                     int player_index) {
  if (batch == NULL || batch_index < 0 || batch_index >= batch->batch_size || player_index < 0 ||
      player_index >= (int)batch->config.num_players) {
    return;
  }
  const uint32_t stage_id = batch->state.stage_id[(size_t)batch_index];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  if (g == NULL) {
    return;
  }
  const size_t idx = msl_idx_player(batch_index, player_index);
  (void)mpcoll_refresh_grounded_root_flat_seam(batch, batch_index, idx, stage_id, g);
}

static inline float mpcoll_absmaxf(float a, float b) {
  const float aa = fabsf(a);
  const float bb = fabsf(b);
  return (aa > bb) ? aa : bb;
}

void mpcoll_penultimate_interpolated_ecb(MslEcbWorldPoints* out,
                                         const MslEcbWorldPoints* step_count_start_ecb,
                                         const MslEcbWorldPoints* interpolation_start_ecb,
                                         const MslEcbWorldPoints* cur_ecb, float last_x,
                                         float last_y, float cur_x, float cur_y) {
  if (out == NULL || step_count_start_ecb == NULL || interpolation_start_ecb == NULL ||
      cur_ecb == NULL) {
    return;
  }
  float max_delta = mpcoll_absmaxf(cur_x - last_x, cur_y - last_y);
  max_delta =
      fmaxf(max_delta, mpcoll_absmaxf(cur_ecb->left_rel_x - step_count_start_ecb->left_rel_x,
                                      cur_ecb->right_rel_x - step_count_start_ecb->right_rel_x));
  max_delta =
      fmaxf(max_delta, mpcoll_absmaxf(cur_ecb->top_rel_y - step_count_start_ecb->top_rel_y,
                                      cur_ecb->side_rel_y - step_count_start_ecb->side_rel_y));
  if (!(max_delta > k_mpcoll_substep_max_delta)) {
    *out = *step_count_start_ecb;
    return;
  }

  // Source `mpColl_80043754` computes the substep count from the pre-interpolation ecb delta. Then
  // `mpCollInterpolateECB` copies that ecb into prev_ecb, optionally restores x64_ecb when b6 is
  // set, and interpolates from the restored/current ecb toward desired_ecb.
  // refs/melee/src/melee/mp/mpcoll.c::{
  //   mpColl_80043754,mpCollInterpolateECB,mpCollSqueezeHorizontal,mpCollSqueezeVertical}
  const int steps = ((int)(max_delta / k_mpcoll_substep_max_delta)) + 1;
  if (steps <= 1) {
    *out = *step_count_start_ecb;
    return;
  }
  const float t = (float)(steps - 1) / (float)steps;
  mpcoll_ecb_world_points_from_rel(
      out, cur_x, cur_y,
      interpolation_start_ecb->bottom_rel_y +
          (cur_ecb->bottom_rel_y - interpolation_start_ecb->bottom_rel_y) * t,
      interpolation_start_ecb->top_rel_y +
          (cur_ecb->top_rel_y - interpolation_start_ecb->top_rel_y) * t,
      interpolation_start_ecb->left_rel_x +
          (cur_ecb->left_rel_x - interpolation_start_ecb->left_rel_x) * t,
      interpolation_start_ecb->right_rel_x +
          (cur_ecb->right_rel_x - interpolation_start_ecb->right_rel_x) * t,
      interpolation_start_ecb->side_rel_y +
          (cur_ecb->side_rel_y - interpolation_start_ecb->side_rel_y) * t,
      cur_ecb->frame_u16);
}

uint8_t grounded_sideb_substep_floor_loss(
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

uint16_t platform_floor_skip_segment_id(const MslBatch* batch, size_t idx, uint32_t stage_id) {
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
    if (is_attackair_action(batch->state.action_id[idx]) &&
        stage_collision_floor_line_has_height_platform_transform(stage_id, skip) &&
        stage_height_platform_line_has_current_source(batch, (int)(idx / (size_t)MSL_MAX_PLAYERS),
                                                      stage_id, skip)) {
      // AttackAir_Coll routes through ft_80082C74 -> mpColl_800471F8, not the common
      // ftCo_80096CC8 platform-pass callback. A replay-prefix seed can still carry the previous
      // CollData.floor_skip for the same FoD height platform after grIzumi/mpLib has accepted a
      // current same-step/direct/ground-contact floor result. Do not let that stale skip reject the
      // current-source AttackAir floor publication; no-current rows still retain the explicit
      // floor_skip/pass-through owner.
      // agent_docs/DATA_CONTRACT.md::FoD platform height source mask
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
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
    // One-step seed path: replay rows do not carry hidden CollData.floor_skip, so first
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

uint8_t action_consumes_cliff_ledge_floor_owner(uint8_t char_id, uint16_t action_id) {
  // EscapeAir_Coll is the Fox/Falco cliff-exit consumer covered by the current RL1 surface:
  // ftCo_EscapeAir_Coll -> ft_80082C74 -> ft_80081D0C -> mpColl_800471F8. The hidden floor line is
  // selected through the shared CollData floor-owner path below only while source ledge-release
  // cooldown is live; this predicate is extracted MotionState callback ownership, not a replay
  // outcome or local action-id slice.
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class3 PHASE4_ESCAPE_AIR_COLL)
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_800471F8
  return msl_motion_state_class3_has(char_id, action_id, MSL_MS_CLASS3_PHASE4_ESCAPE_AIR_COLL);
}

MslMpcollCarriedCliffLedgeFloorAuthority mpcoll_carried_cliff_ledge_floor_authority(
    const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint8_t char_id,
    uint16_t action_id, int candidate_line_idx, int raw_current_floor_line_idx, float root_x,
    float root_y, float candidate_floor_y, uint8_t ecb_lock_timer_seed,
    uint8_t callback_bottom_root_accepted) {
  MslMpcollCarriedCliffLedgeFloorAuthority a = {0};
  if (batch == NULL || g == NULL || batch->state.cliff_ledge_floor_segment_id == NULL ||
      batch->state.cliff_ledge_floor_segment_id[idx] == 0xFFFFu || candidate_line_idx < 0 ||
      (size_t)candidate_line_idx >= g->line_count) {
    return a;
  }

  const uint16_t carried_floor_id = batch->state.cliff_ledge_floor_segment_id[idx];
  a.carried_floor_valid = 1u;
  a.candidate_matches = (g->lines[(size_t)candidate_line_idx].is_ledge &&
                         g->lines[(size_t)candidate_line_idx].segment_i == carried_floor_id)
                            ? 1u
                            : 0u;
  a.owner_live = (action_consumes_cliff_ledge_floor_owner(char_id, action_id) &&
                  batch->state.ledge_cooldown[idx] != 0u)
                     ? 1u
                     : 0u;
  a.strict_span =
      floor_x_within_line_segment_strict(batch, bi, g, candidate_line_idx, root_x) ? 1u : 0u;
  a.producer_start_span =
      (isfinite(batch->state.floor_sweep_prev_pos_x[idx]) &&
       floor_x_within_line_segment_strict(batch, bi, g, candidate_line_idx,
                                          batch->state.floor_sweep_prev_pos_x[idx]))
          ? 1u
          : 0u;
  a.callback_bottom_root_accepted = callback_bottom_root_accepted ? 1u : 0u;
  a.current_floor_matches =
      (raw_current_floor_line_idx >= 0 && (size_t)raw_current_floor_line_idx < g->line_count &&
       g->lines[(size_t)raw_current_floor_line_idx].segment_i == carried_floor_id)
          ? 1u
          : 0u;
  const uint8_t raw_current_floor_is_platform =
      (raw_current_floor_line_idx >= 0 && (size_t)raw_current_floor_line_idx < g->line_count &&
       g->lines[(size_t)raw_current_floor_line_idx].is_platform)
          ? 1u
          : 0u;

  const uint8_t jumpaerial_entry_provenance =
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B)
          ? 1u
          : 0u;
  const uint8_t desired_bottom_owner = msl_escapeair_locked_bottom_owner_normalize(
      batch->state.coll_desired_ecb_bottom_locked_owner[idx]);
  const uint8_t desired_bottom_owner_live_jumpaerial =
      msl_escapeair_locked_bottom_owner_is_live_jumpaerial(desired_bottom_owner);
  const uint8_t desired_bottom_owner_seeded =
      msl_escapeair_locked_bottom_owner_is_seeded(desired_bottom_owner);
  const uint8_t desired_bottom_source_valid =
      (batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
       (desired_bottom_owner != (uint8_t)MSL_ESCAPEAIR_LOCKED_BOTTOM_OWNER_NONE ||
        jumpaerial_entry_provenance) &&
       fabsf(batch->state.coll_desired_ecb_bottom_rel_y[idx]) > k_floor_y_bias)
          ? 1u
          : 0u;
  const float desired_bottom_cur_y = root_y + batch->state.coll_desired_ecb_bottom_rel_y[idx];
  a.desired_bottom_reaches_carried_floor =
      (desired_bottom_source_valid && desired_bottom_cur_y <= (candidate_floor_y + k_floor_y_bias))
          ? 1u
          : 0u;
  float desired_bottom_prev_floor_y = 0.0f;
  uint8_t desired_bottom_prev_floor_valid =
      (isfinite(batch->state.floor_sweep_prev_pos_x[idx]) &&
       floor_line_y_at_x_ed5c_for_env(batch, bi, g, candidate_line_idx,
                                      batch->state.floor_sweep_prev_pos_x[idx],
                                      &desired_bottom_prev_floor_y))
          ? 1u
          : 0u;
  if (!desired_bottom_prev_floor_valid && isfinite(batch->state.floor_sweep_prev_pos_y[idx])) {
    // mpColl_80044628_Floor first proves the current floor candidate, then root projection can use
    // that accepted plane when the previous bottom sample is just outside mpLib_8004ED5C's endpoint
    // lookup. This keeps the source producer on bottom/root crossing instead of a previous-root
    // strict-span gate.
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004ED5C
    desired_bottom_prev_floor_y = candidate_floor_y;
    desired_bottom_prev_floor_valid = 1u;
  }
  const float desired_bottom_prev_y =
      batch->state.floor_sweep_prev_pos_y[idx] + batch->state.coll_desired_ecb_bottom_rel_y[idx];
  a.desired_bottom_crosses_carried_floor =
      (desired_bottom_source_valid && desired_bottom_prev_floor_valid &&
       desired_bottom_prev_y > (desired_bottom_prev_floor_y + k_floor_y_bias) &&
       desired_bottom_cur_y <= (candidate_floor_y + k_floor_y_bias))
          ? 1u
          : 0u;
  const uint8_t restored_current_floor_entry_provenance =
      (jumpaerial_entry_provenance && batch->state.seed_prev_action_frame[idx] >= 3 &&
       batch->state.action_frame[idx] <= 2 && a.current_floor_matches &&
       !desired_bottom_source_valid)
          ? 1u
          : 0u;
  const uint8_t flags_2218 =
      batch->state
          .state_flags[idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX];
  const uint8_t allow_interrupt_escapeair_source =
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
       (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) != 0u)
          ? 1u
          : 0u;
  const uint8_t allow_interrupt_source =
      ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) != 0u) ? 1u : 0u;
  const uint8_t locked_stale_owner_requires_producer_start_span =
      (((flags_2218 & (uint8_t)(MSL_STATE_FLAG_2218_B1 | MSL_STATE_FLAG_2218_B2)) != 0u) &&
       (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) == 0u &&
       (batch->state.stage_id == NULL ||
        (!stage_collision_stage_has_height_platform_transform(batch->state.stage_id[bi]) &&
         !stage_collision_stage_has_only_static_cardinal_hard_floors(batch->state.stage_id[bi]))))
          ? 1u
          : 0u;
  const uint8_t height_stage_early_b1_remap_without_source_bit =
      (batch->state.stage_id != NULL &&
       stage_collision_stage_has_height_platform_transform(batch->state.stage_id[bi]) &&
       (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_B1) != 0u &&
       (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR) == 0u &&
       !a.current_floor_matches)
          ? 1u
          : 0u;
  const uint8_t producer_start_span_source_owned =
      ((!locked_stale_owner_requires_producer_start_span || a.producer_start_span) &&
       !height_stage_early_b1_remap_without_source_bit)
          ? 1u
          : 0u;
  const uint8_t current_floor_lock_still_source_owned =
      (!a.current_floor_matches || ecb_lock_timer_seed > 1u || allow_interrupt_escapeair_source)
          ? 1u
          : 0u;
  const uint8_t jumpaerial_current_floor_source_window =
      (!jumpaerial_entry_provenance || !a.current_floor_matches ||
       batch->state.seed_prev_action_frame[idx] <= 1 || allow_interrupt_source ||
       ecb_lock_timer_seed >= 5u)
          ? 1u
          : 0u;
  const MslCommonParams* common = msl_common_params();
  const uint8_t jumpaerial_first_cliff_cooldown_phase =
      (common != NULL && jumpaerial_entry_provenance && a.current_floor_matches &&
       batch->state.ledge_cooldown[idx] >=
           (uint8_t)(common->ledge_cooldown_frames > 5u ? common->ledge_cooldown_frames - 5u : 0u))
          ? 1u
          : 0u;
  a.live_bottom_sweep_authority =
      (a.callback_bottom_root_accepted && producer_start_span_source_owned &&
       !jumpaerial_entry_provenance && current_floor_lock_still_source_owned)
          ? 1u
          : 0u;
  a.live_desired_bottom_authority =
      (desired_bottom_source_valid && jumpaerial_current_floor_source_window &&
       current_floor_lock_still_source_owned &&
       (((desired_bottom_owner_seeded || desired_bottom_owner_live_jumpaerial) &&
         (a.callback_bottom_root_accepted ||
          (a.desired_bottom_crosses_carried_floor &&
           (a.current_floor_matches ||
            batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR ||
            (a.producer_start_span && !raw_current_floor_is_platform &&
             !height_stage_early_b1_remap_without_source_bit)))) &&
         (a.current_floor_matches ||
          batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR ||
          !height_stage_early_b1_remap_without_source_bit)) ||
        ((desired_bottom_owner_seeded || desired_bottom_owner_live_jumpaerial) &&
         !a.current_floor_matches && allow_interrupt_escapeair_source) ||
        (desired_bottom_owner_live_jumpaerial && !a.current_floor_matches &&
         (batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_ESCAPE_AIR ||
          !height_stage_early_b1_remap_without_source_bit || allow_interrupt_escapeair_source) &&
         (a.desired_bottom_reaches_carried_floor || allow_interrupt_escapeair_source)) ||
        (jumpaerial_entry_provenance && !a.current_floor_matches &&
         a.desired_bottom_reaches_carried_floor)))
          ? 1u
          : 0u;
  a.live_current_floor_continuation_authority =
      (a.candidate_matches && a.owner_live && a.strict_span && ecb_lock_timer_seed != 0u &&
       producer_start_span_source_owned && a.current_floor_matches &&
       !restored_current_floor_entry_provenance && current_floor_lock_still_source_owned &&
       ((desired_bottom_owner_live_jumpaerial && desired_bottom_source_valid &&
         (a.desired_bottom_reaches_carried_floor || !jumpaerial_entry_provenance ||
          (batch->state.seed_prev_action_frame[idx] >= 5 && ecb_lock_timer_seed > 2u))) ||
        (desired_bottom_owner_seeded && desired_bottom_source_valid &&
         a.desired_bottom_crosses_carried_floor) ||
        a.live_bottom_sweep_authority || a.live_desired_bottom_authority ||
        (jumpaerial_entry_provenance && !jumpaerial_first_cliff_cooldown_phase &&
         a.desired_bottom_reaches_carried_floor &&
         (batch->state.seed_prev_action_frame[idx] <= 1 || allow_interrupt_source))))
          ? 1u
          : 0u;
  if (!a.live_current_floor_continuation_authority && a.candidate_matches && a.owner_live &&
      a.strict_span && a.current_floor_matches && ecb_lock_timer_seed == 0u &&
      a.callback_bottom_root_accepted) {
    // With no CollData_X130 lock active, a callback floor result on the current carried ledge is
    // live floor-producer state, not restored locked provenance.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    a.live_current_floor_continuation_authority = 1u;
  }
  a.current_floor_source_owned = a.live_current_floor_continuation_authority;
  a.source_authority = (a.candidate_matches && a.owner_live && a.strict_span &&
                        (a.live_bottom_sweep_authority || a.live_desired_bottom_authority ||
                         a.live_current_floor_continuation_authority))
                           ? 1u
                           : 0u;
  a.restored_only =
      (a.candidate_matches && a.owner_live && a.strict_span && !a.source_authority) ? 1u : 0u;
  return a;
}

MslAttackAirPlatformEcbOwner attackair_platform_ecb_owner(uint8_t char_id, uint16_t action_id) {
  MslAttackAirPlatformEcbOwner owner = {0u, 0u, 0u};
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
  if (msl_motion_state_class3_has(char_id, action_id, MSL_MS_CLASS3_PHASE4_ATTACK_AIR_COLL) == 0u) {
    return owner;
  }
  const uint16_t smid = msl_motion_state_submotion_id(char_id, action_id);
  if (smid == (uint16_t)MSL_SM_ATTACK_AIR_N || smid == (uint16_t)MSL_SM_ATTACK_AIR_LW) {
    owner.shallow = 1u;
    owner.first_phase = 1u;
    owner.late = 1u;
  } else if (smid == (uint16_t)MSL_SM_ATTACK_AIR_HI) {
    owner.first_phase = 1u;
  }
  return owner;
}

uint8_t action_uses_shallow_attackair_platform_ecb_owner(uint8_t char_id, uint16_t action_id) {
  return attackair_platform_ecb_owner(char_id, action_id).shallow;
}

uint8_t action_uses_late_attackair_platform_ecb_owner(uint8_t char_id, uint16_t action_id) {
  return attackair_platform_ecb_owner(char_id, action_id).late;
}

MslMpcollFinalFloorLineState mpcoll_final_floor_line_state(const MslBatch* batch, int bi,
                                                           const MslStageFloorGraph* g,
                                                           uint32_t stage_id, uint16_t ground_id,
                                                           float root_x) {
  MslMpcollFinalFloorLineState out = {
      .line_idx = stage_collision_floor_line_index(stage_id, ground_id),
      .line = NULL,
      .has_platform_transform = 0u,
      .has_height_platform_transform = 0u,
      .height_same_step_contact = 0u,
      .height_live_scheduler_source = 0u,
      .height_current_source = 0u,
      .line_y_valid = 0u,
      .line_y = 0.0f,
  };
  if (g == NULL || out.line_idx < 0 || (size_t)out.line_idx >= g->line_count) {
    return out;
  }

  out.line = &g->lines[(size_t)out.line_idx];
  out.has_platform_transform =
      (out.line->platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE) ? 1u : 0u;
  out.has_height_platform_transform =
      (out.line->platform_transform_kind == MSL_STAGE_PLATFORM_TRANSFORM_HEIGHT) ? 1u : 0u;
  if (out.has_height_platform_transform) {
    const uint8_t source_trusted =
        stage_collision_floor_line_height_platform_state_is_source_trusted(batch, bi, ground_id);
    if (source_trusted) {
      out.height_same_step_contact =
          stage_height_platform_line_has_same_step_contact_source(batch, bi, stage_id, ground_id);
    }
    out.height_live_scheduler_source =
        stage_height_platform_line_has_live_scheduler_source(batch, bi, stage_id, ground_id);
    out.height_current_source =
        stage_height_platform_line_has_current_source(batch, bi, stage_id, ground_id);
  }
  out.line_y_valid =
      floor_line_y_at_x_for_env(batch, bi, g, out.line_idx, root_x, &out.line_y) ? 1u : 0u;
  return out;
}

uint8_t mpcoll_attackairlw_air471f8_live_platform_publication_owner(
    const MslBatch* batch, size_t idx, const MslMpcollFinalFloorLineState* floor,
    uint16_t action_id, int16_t first_create_frame, int16_t second_create_frame,
    uint16_t skip_platform_segment_i) {
  if (batch == NULL || floor == NULL || floor->line == NULL) {
    return 0u;
  }
  // AttackAirLw_Coll reaches final AIR_471F8 publication after the floor producer has accepted the
  // transformed platform line. This is source authority from the callback-local floor producer, not
  // carried `ground_id`: old ECB-only/floor-skip suppression must not reclassify the accepted floor.
  //
  // Bound this to the script interval after the authored first create_hitbox callback edge and
  // before/at the authored second create_hitbox command; later DAir frames retain the existing
  // transformed-platform pass/floor-skip owner. The generated first-create frame is stored in the
  // callback-visible action-frame coordinate used by the other move-table windows.
  //
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4
  // refs/melee/src/melee/ft/ft_081B.c::{ft_80082C74,ft_80081D0C}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
  // data/scripts/<char>.bin::MSLFTSC1 AttackAirLw create_hitbox events
  return (uint8_t)((action_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
                    batch->state.action_id[idx] == (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
                    floor->line_idx >= 0 && floor->has_platform_transform &&
                    first_create_frame >= 0 &&
                    batch->state.action_frame[idx] >= (uint16_t)first_create_frame &&
                    second_create_frame >= 0 &&
                    batch->state.action_frame[idx] <= (uint16_t)second_create_frame &&
                    !(skip_platform_segment_i != 0xFFFFu &&
                      floor->line->segment_i == skip_platform_segment_i) &&
                    batch->state.floor_sweep_prev_source_owned[idx] != 0u &&
                    mpcoll_floor_sweep_prev_root_is_runtime_owned(batch, idx))
                       ? 1u
                       : 0u);
}

uint8_t action_uses_sideb_air_ft_check_ground_and_ledge_floor_coll(uint8_t char_id,
                                                                   uint16_t action_id) {
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

uint8_t floor_line_is_skipped_platform(uint32_t stage_id, const MslStageFloorGraph* g, int line_idx,
                                       uint16_t skip_segment_i) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count || skip_segment_i == 0xFFFFu) {
    return 0u;
  }
  const MslStageFloorLine* line = &g->lines[(size_t)line_idx];
  return (uint8_t)(line->segment_i == skip_segment_i &&
                   (line->is_platform || stage_collision_floor_line_has_height_platform_transform(
                                             stage_id, line->segment_i)));
}

static inline void publish_attackair_transformed_platform_floor_skip(
    MslBatch* batch, size_t idx, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslCommonParams* c, uint8_t force_source_owned, int line_idx) {
  if (batch == NULL || g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return;
  }
  const uint8_t carried_skip = floor_line_is_skipped_platform(
      stage_id, g, line_idx, batch->state.floor_skip_segment_id[idx]);
  const uint8_t continuous_downheld =
      (c != NULL && batch->state.floor_sweep_prev_runtime_owned[idx] == 0u &&
       batch->state.coll_floor_probe_raw_bottom_sweep_hit[idx] == 0u &&
       platform_pass_input_below_raw_threshold(batch, idx, c) &&
       stick_i8_to_unit(batch->state.prev_input_main_y[idx]) <=
           c->platform_air_land_stick_y_threshold)
          ? 1u
          : 0u;
  if (!carried_skip && !continuous_downheld && force_source_owned == 0u) {
    return;
  }
  // AttackAir_Coll itself does not pass ftCo_80096CC8, but the retained transformed-platform owner
  // can preserve an already-carried CollData.floor_skip or publish the first skip for a continuous
  // down-held AttackAir platform-pass episode. Released/no-skip rows must not synthesize a new
  // floor_skip from an AttackAir ECB-only rejection.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Coll
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor,mpUpdateFloorSkip}
  msl_mpcoll_update_floor_skip(batch, idx, g->lines[(size_t)line_idx].segment_i);
}

void publish_attackair_transformed_platform_floor_skip_from_sweep(
    MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g, uint32_t stage_id,
    const MslCommonParams* c, uint8_t force_source_owned, int line_idx, float x, float prev_y,
    float y) {
  if (batch == NULL || g == NULL || batch->state.floor_skip_segment_id == NULL) {
    return;
  }
  if (line_idx >= 0 && (size_t)line_idx < g->line_count &&
      stage_collision_floor_line_has_height_platform_transform(
          stage_id, g->lines[(size_t)line_idx].segment_i)) {
    publish_attackair_transformed_platform_floor_skip(batch, idx, g, stage_id, c,
                                                      force_source_owned, line_idx);
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
      publish_attackair_transformed_platform_floor_skip(batch, idx, g, stage_id, c,
                                                        force_source_owned, (int)li);
      return;
    }
  }
}

uint8_t floor_line_is_runtime_fighter_solid(const MslStageFloorGraph* g, uint32_t stage_id,
                                            int line_idx) {
  (void)stage_id;
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0u;
  }
  return g->lines[(size_t)line_idx].fighter_solid ? 1u : 0u;
}

uint8_t carried_floor_line_is_live_yoshi_shyguy_support(const MslBatch* batch, int bi,
                                                        const MslStageFloorGraph* g,
                                                        uint32_t stage_id, int line_idx) {
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

uint8_t floor_line_admitted_by_source_callback(const MslBatch* batch, size_t idx,
                                               const MslStageFloorGraph* g, uint32_t stage_id,
                                               int line_idx, uint16_t skip_platform_segment_i,
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

uint8_t floor_intersect_horiz(float x0, float y0, float x1, float ax, float ay, float bx, float by,
                              float* ix_out, float* iy_out) {
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

uint8_t wall_blocks_floor_edge_probe(const MslStageWallGraph* wg, float ax, float ay, float bx,
                                     float by) {
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

uint32_t floor_edge_suppression_flags(MslBatch* batch, size_t idx, uint32_t stage_id,
                                      const MslStageFloorGraph* fg, int line_idx, uint8_t char_id,
                                      uint32_t anim, uint16_t ecb_frame, uint8_t was_grounded,
                                      const MslEcbWorldPoints* loaded_current_ecb) {
  if (batch == NULL || fg == NULL) {
    return 0u;
  }
  if (line_idx < 0 || (size_t)line_idx >= fg->line_count) {
    return 0u;
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
    return 0u;
  }

  uint32_t flags = 0u;
  const float left_x = (left_x0 < right_x1) ? left_x0 : right_x1;
  const float right_x = (left_x0 < right_x1) ? right_x1 : left_x0;
  const float left_y = (left_x0 < right_x1) ? left_y0 : right_y1;
  const float right_y = (left_x0 < right_x1) ? right_y1 : left_y0;

  const float fighter_x = batch->state.pos_x[idx];
  if (fighter_x <= left_x) {
    const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
    MslEcbWorldPoints ecb = {0};
    if (loaded_current_ecb != NULL) {
      ecb = *loaded_current_ecb;
    } else {
      msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, fighter_x,
                                  batch->state.pos_y[idx], was_grounded);
    }
    const float probe_ax = left_x + k_floor_edge_wall_probe_x_offset;
    const float probe_ay = left_y + k_floor_edge_wall_probe_y_offset;
    const float probe_bx = left_x + (ecb.right_rel_x /* bottom.x == 0 */);
    const float probe_by = left_y + (ecb.side_rel_y - ecb.bottom_rel_y);
    const MslStageWallGraph* lwg = stage_collision_get_left_wall_graph(stage_id);
    if (!wall_blocks_floor_edge_probe(lwg, probe_ax, probe_ay, probe_bx, probe_by)) {
      flags |= (uint32_t)MSL_COLLIDE_RIGHT_EDGE;
      // Decomp: mpColl_8004A678_Floor also sets Collide_Edge when snapping to floor endpoints.
      // In this sim, set Collide_Edge whenever any edge suppression bit is set as a cheap parity
      // win and to future-proof other gates.
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A678_Floor
      flags |= (uint32_t)MSL_COLLIDE_EDGE;
    }
  } else if (fighter_x >= right_x) {
    const float fd = batch->state.facing[idx] ? 1.0f : -1.0f;
    MslEcbWorldPoints ecb = {0};
    if (loaded_current_ecb != NULL) {
      ecb = *loaded_current_ecb;
    } else {
      msl_ecb_world_points_sample(&ecb, char_id, anim, ecb_frame, fd, fighter_x,
                                  batch->state.pos_y[idx], was_grounded);
    }
    const float probe_ax = right_x - k_floor_edge_wall_probe_x_offset;
    const float probe_ay = right_y + k_floor_edge_wall_probe_y_offset;
    const float probe_bx = right_x + (ecb.left_rel_x /* bottom.x == 0 */);
    const float probe_by = right_y + (ecb.side_rel_y - ecb.bottom_rel_y);
    const MslStageWallGraph* rwg = stage_collision_get_right_wall_graph(stage_id);
    if (!wall_blocks_floor_edge_probe(rwg, probe_ax, probe_ay, probe_bx, probe_by)) {
      flags |= (uint32_t)MSL_COLLIDE_LEFT_EDGE;
      // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004A678_Floor
      flags |= (uint32_t)MSL_COLLIDE_EDGE;
    }
  }
  return flags;
}

void mpcoll_commit_grounded_floor_contact(const MslMpcollContext* ctx,
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
  uint32_t env_flags = (uint32_t)MSL_COLLIDE_FLOOR_MASK;
  env_flags |= floor_edge_suppression_flags(
      batch, idx, ctx->stage_id, ctx->floor_graph,
      stage_collision_floor_line_index(ctx->stage_id, contact->ground_id), ctx->char_id, ctx->anim,
      ctx->ecb_frame, was_grounded, ctx->loaded_ecb != NULL ? ctx->loaded_ecb->current : NULL);
  batch->state.coll_env_flags[idx] |= env_flags;
  batch->state.ground_id[idx] = contact->ground_id;
  batch->state.ground_normal_x[idx] = contact->normal_x;
  batch->state.ground_normal_y[idx] = contact->normal_y;
  batch->state.ground_contact_x[idx] = contact->contact_x;
  batch->state.ground_contact_y[idx] = contact->contact_y;
}

static inline void mpcoll_commit_airborne_floor_state(const MslMpcollContext* ctx,
                                                      uint16_t ground_id,
                                                      const MslMpcollFloorContact* contact) {
  if (ctx == NULL || ctx->batch == NULL) {
    return;
  }
  // Airborne CollData usually keeps only floor.index provenance and clears live contact
  // publication. Active Damage hitlag stay-airborne is the source exception:
  // mpColl_80044628_Floor/mpColl_80044948_Floor can leave ground_or_air airborne while preserving
  // callback-current floor/contact state for later frozen hitlag callbacks.
  // refs/melee/src/melee/lb/types.h::CollData
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044948_Floor}
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  batch->state.ground_id[idx] = ground_id;
  if (contact != NULL) {
    batch->state.ground_normal_x[idx] = contact->normal_x;
    batch->state.ground_normal_y[idx] = contact->normal_y;
    batch->state.ground_contact_x[idx] = contact->contact_x;
    batch->state.ground_contact_y[idx] = contact->contact_y;
  } else {
    batch->state.ground_normal_x[idx] = 0.0f;
    batch->state.ground_normal_y[idx] = 1.0f;
    batch->state.ground_contact_x[idx] = 0.0f;
    batch->state.ground_contact_y[idx] = 0.0f;
  }
}

static inline void mpcoll_reject_floor_publication(const MslMpcollContext* ctx,
                                                   MslMpcollFloorPublication* publication,
                                                   uint16_t airborne_ground_id,
                                                   float restored_pos_y, float restored_contact_x,
                                                   float restored_contact_y) {
  if (ctx == NULL || ctx->batch == NULL || publication == NULL) {
    return;
  }
  // Final wrapper-owned rejection of an already-built floor result. Source callbacks can collect a
  // candidate floor and still leave ground_or_air airborne when the callback-local publication
  // preconditions are not met; preserve that shape by changing only the final publication state.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044628_Floor,mpColl_80044838_Floor}
  publication->on_ground = 0u;
  publication->airborne_ground_id = airborne_ground_id;
  publication->contact.ground_id = airborne_ground_id;
  ctx->batch->state.pos_y[ctx->idx] = restored_pos_y;
  publication->contact.contact_x = restored_contact_x;
  publication->contact.contact_y = restored_contact_y;
}

static inline void mpcoll_reject_floor_publication_to_current_bottom(
    const MslMpcollContext* ctx, MslMpcollFloorPublication* publication,
    uint16_t airborne_ground_id, float restored_pos_y, float cur_bottom_x, float cur_bottom_y) {
  mpcoll_reject_floor_publication(ctx, publication, airborne_ground_id, restored_pos_y,
                                  cur_bottom_x, cur_bottom_y);
}

void mpcoll_apply_final_floor_rejection_bits(const MslMpcollContext* ctx,
                                             MslMpcollFloorPublication* publication,
                                             MslMpcollFloorRejectPacket packet, float y,
                                             float cur_bottom_x, float cur_bottom_y,
                                             float cur_bot_rel_y,
                                             const MslEcbWorldPoints* prev_ecb_points,
                                             int final_ground_line_idx, float x, float prev_y) {
  const uint64_t bits = packet.bits;
  if (ctx == NULL || ctx->batch == NULL || publication == NULL || bits == 0u) {
    return;
  }
  // Final publication rejection is ordered because several guards can be true on the same
  // carried/projected floor candidate. Preserve the old source-shaped priority in one
  // writeback owner instead of scattering side effects across the tail.
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044628_Floor,mpColl_80044838_Floor}
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  const uint8_t specialairhi_platform =
      (uint8_t)((packet.side_effects &
                 (uint32_t)MSL_MPCOLL_FLOOR_REJECT_SIDE_FLOOR_SKIP_TO_CONTACT) != 0u);
  const uint8_t sheik_vanish_start1_platform_pass =
      (uint8_t)((bits & MSL_MPCOLL_REJECT_SHEIK_VANISH_START1_PLATFORM_PASS) != 0u);
  uint16_t reject_ground_id = (specialairhi_platform || sheik_vanish_start1_platform_pass)
                                  ? publication->contact.ground_id
                                  : batch->state.ground_id[idx];
  float reject_pos_y = batch->state.pos_y[idx];

  if (specialairhi_platform) {
    msl_mpcoll_update_floor_skip(batch, idx, publication->contact.ground_id);
  }
  if ((packet.side_effects &
       (uint32_t)MSL_MPCOLL_FLOOR_REJECT_SIDE_ATTACKAIR_PUBLISH_SKIP_FROM_SWEEP) != 0u) {
    const uint8_t force_attackair_source_skip =
        (bits & MSL_MPCOLL_REJECT_ATTACKAIR_TRANSFORMED_PLATFORM_ECB_ONLY) != 0u ? 1u : 0u;
    publish_attackair_transformed_platform_floor_skip_from_sweep(
        batch, idx, ctx->bi, ctx->floor_graph, ctx->stage_id, msl_common_params(),
        force_attackair_source_skip, final_ground_line_idx, x, prev_y, y);
  }
  if ((packet.side_effects & (uint32_t)MSL_MPCOLL_FLOOR_REJECT_SIDE_ATTACKAIR_CLEAR_FLOOR_SKIP) !=
      0u) {
    msl_mpcoll_clear_floor_skip(batch, idx);
  }

  switch ((MslMpcollFloorRejectRestore)packet.restore) {
    case MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_XY:
    case MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_Y:
      reject_pos_y = y;
      break;
    case MSL_MPCOLL_FLOOR_REJECT_RESTORE_SPECIALHI_UNDERSTAGE_CLEARANCE:
      if (prev_ecb_points != NULL) {
        // Keep the rejected root outside the same live ECB neighborhood; otherwise the next frame
        // can re-accept the same inside-stage floor.
        reject_pos_y = publication->contact.contact_y -
                       specialhi_understage_floor_reject_clearance(prev_ecb_points) -
                       k_floor_y_bias;
      }
      break;
    case MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_BOTTOM_TO_ROOT_REL:
      reject_pos_y = cur_bottom_y - cur_bot_rel_y;
      break;
    case MSL_MPCOLL_FLOOR_REJECT_RESTORE_KEEP_CURRENT:
    case MSL_MPCOLL_FLOOR_REJECT_RESTORE_NONE:
    default:
      break;
  }

  mpcoll_reject_floor_publication_to_current_bottom(ctx, publication, reject_ground_id,
                                                    reject_pos_y, cur_bottom_x, cur_bottom_y);
  if (packet.restore == (uint8_t)MSL_MPCOLL_FLOOR_REJECT_RESTORE_CURRENT_ROOT_XY) {
    batch->state.pos_x[idx] = x;
  }
}

void mpcoll_apply_late_floor_publication_guards(
    const MslMpcollContext* ctx, MslMpcollFloorPublication* publication,
    uint8_t ecb_lock_timer_seed, int prefer_line_idx, uint16_t seed_ground_id, float y,
    float prev_x, float prev_bottom_x, float prev_bottom_y, float cur_bottom_x, float cur_bottom_y,
    const MslEcbWorldPoints* cur_ecb_points, int raw_current_floor_line_idx,
    uint8_t escapeair_stale_platform_root_handoff_hit,
    uint8_t escapeair_fresh_jump_height_platform_handoff_hit,
    uint8_t damage_active_hitlag_downward_sdi_airborne_owner,
    uint8_t damage_active_hitlag_root_below_bottom_above_floor_owner) {
  if (ctx == NULL || ctx->batch == NULL || ctx->floor_graph == NULL || publication == NULL) {
    return;
  }
  MslBatch* batch = ctx->batch;
  const size_t idx = ctx->idx;
  const uint16_t action_id = ctx->action_id;

  const int publication_floor_line_idx =
      publication->contact.ground_id != 0xFFFFu
          ? stage_collision_floor_line_index(ctx->stage_id, publication->contact.ground_id)
          : -1;
  const int seed_floor_line_idx =
      seed_ground_id != 0xFFFFu ? stage_collision_floor_line_index(ctx->stage_id, seed_ground_id)
                                : -1;
  if (publication->on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      ecb_lock_timer_seed == 0u && batch->state.action_frame[idx] <= 1 &&
      (batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
      (batch->state.input_buttons[idx] & (uint16_t)(MSL_BUTTON_L | MSL_BUTTON_R | MSL_BUTTON_Z)) !=
          0u &&
      publication->contact.ground_id != seed_ground_id &&
      !floor_lines_connected(ctx->floor_graph, publication_floor_line_idx, seed_floor_line_idx) &&
      floor_line_is_generated_sloped_ledge(batch, ctx->bi, ctx->floor_graph,
                                           publication_floor_line_idx)) {
    // JumpAerial IASA can enter EscapeAir before the map callback. The entry callback may collect a
    // generated Yoshi sloped ledge candidate, but source does not remap an unlinked carried floor
    // (for example a side platform) onto that slope until the following EscapeAir_Coll owner. If
    // the carried floor already names that sloped ledge or is connected through the generated floor
    // graph, the entry callback can publish it normally. Flat non-platform ledges/hard floors are
    // admitted by the earlier no-lock bottom-sweep publication path.
    // data/stages/bin/grst.bin::MSLSTG01 sloped ledge floor segments
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044628_Floor}
    mpcoll_reject_floor_publication_to_current_bottom(ctx, publication, seed_ground_id, y,
                                                      cur_bottom_x, cur_bottom_y);
  }

  if (publication->on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      ecb_lock_timer_seed != 0u && batch->state.action_frame[idx] == 4 && prefer_line_idx >= 0 &&
      !ctx->floor_graph->lines[(size_t)prefer_line_idx].is_platform &&
      !ctx->floor_graph->lines[(size_t)prefer_line_idx].is_ledge &&
      fabsf(cur_bottom_x - prev_bottom_x) <= (float)k_floor_horiz_dy_thresh &&
      batch->state.prev_pos_y[idx] < 0.0f && y <= -fabsf(batch->state.speed_y_self[idx])) {
    // EscapeAir_Coll under CollData_X130_Locked preserves airborne state when frame-start cur_pos
    // was already below the carried hard floor.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_80043754}
    mpcoll_reject_floor_publication_to_current_bottom(ctx, publication, seed_ground_id, y,
                                                      cur_bottom_x, cur_bottom_y);
  }

  const float ledge_drop_skip_lift = publication->contact.contact_y - y;
  const float ledge_drop_skip_large_projection =
      fabsf(batch->state.speed_y_self[idx]) +
      mpcoll_floor_projection_lift_allowance(cur_ecb_points);
  if (publication->on_ground && batch->state.ledge_drop_floor_skip_segment_id != NULL &&
      batch->state.ledge_drop_floor_skip_segment_id[idx] != 0xFFFFu &&
      action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      publication->contact.contact_y > k_floor_y_bias &&
      publication->contact.ground_id == batch->state.ledge_drop_floor_skip_segment_id[idx] &&
      stage_collision_floor_line_is_platform(ctx->stage_id, publication->contact.ground_id) &&
      !escapeair_fresh_jump_height_platform_handoff_hit &&
      ledge_drop_skip_lift > ledge_drop_skip_large_projection) {
    // Ledge-drop floor skip is a hidden CollData owner; reject oversized same-platform EscapeAir
    // lifts that bypass the ordinary floor-skip check.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_8009AAFC
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_JObj,mpColl_80044628_Floor}
    mpcoll_reject_floor_publication_to_current_bottom(ctx, publication, seed_ground_id, y,
                                                      cur_bottom_x, cur_bottom_y);
  }

  if (publication->on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      stage_collision_floor_line_is_platform(ctx->stage_id, publication->contact.ground_id) &&
      batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
      batch->state.seed_prev_action_frame[idx] >= 3 && batch->state.action_frame[idx] <= 2 &&
      publication->contact.ground_id == seed_ground_id && batch->state.speed_y_self[idx] >= 0.0f) {
    // Fresh KneeBend -> Jump -> EscapeAir can reach EscapeAir_Coll on the same callback, but
    // mpColl_80044838_Floor only owns same-platform publication when the entered EscapeAir step is
    // actually descending into that platform. A horizontal airdodge already resting on Dream Land's
    // elevated platform keeps EscapeAir airborne; downward airdodge handoffs remain admitted above.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Jump.c::ftCo_Jump_IASA
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800471F8,mpColl_80044838_Floor}
    mpcoll_reject_floor_publication_to_current_bottom(ctx, publication, seed_ground_id, y,
                                                      cur_bottom_x, cur_bottom_y);
  }

  if (publication->on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      stage_collision_floor_line_is_platform(ctx->stage_id, publication->contact.ground_id) &&
      batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      ecb_lock_timer_seed >= 4u && batch->state.action_frame[idx] <= 7 &&
      publication->contact.ground_id == seed_ground_id && batch->state.speed_y_self[idx] >= 0.0f &&
      fabsf(y - publication->contact.contact_y) <= (2.0f * k_floor_horiz_dy_thresh)) {
    // Sustained horizontal EscapeAir keeps the same early-lock floorhug owner as the entry frame:
    // without a descending current/desired ECB bottom, `EscapeAir_Coll` does not materialize the
    // carried Dream Land platform as LandingFallSpecial during the lock countdown. Later/deeper
    // downward platform rows remain on the retained ft_80082C74 floor-publication owners.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/ft/ft_081B.c::ft_80082C74
    // refs/melee/src/melee/mp/mpcoll.c::{mpCollInterpolateECB,mpColl_800471F8}
    mpcoll_reject_floor_publication_to_current_bottom(ctx, publication, seed_ground_id, y,
                                                      cur_bottom_x, cur_bottom_y);
  }

  if (publication->on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      stage_collision_floor_line_is_platform(ctx->stage_id, publication->contact.ground_id) &&
      !escapeair_stale_platform_root_handoff_hit &&
      !escapeair_fresh_jump_height_platform_handoff_hit &&
      batch->state.coll_desired_ecb_bottom_valid[idx] != 0u &&
      msl_escapeair_locked_bottom_owner_any(
          batch->state.coll_desired_ecb_bottom_locked_owner[idx]) &&
      (y + batch->state.coll_desired_ecb_bottom_rel_y[idx]) >
          (publication->contact.contact_y + k_floor_y_bias)) {
    // Locked EscapeAir platform root projection is legal only after desired ECB bottom reaches the
    // platform floor.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    mpcoll_reject_floor_publication_to_current_bottom(ctx, publication, seed_ground_id, y,
                                                      cur_bottom_x, cur_bottom_y);
  }

  if (publication->on_ground && action_id == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_AIR &&
      ecb_lock_timer_seed == 0u &&
      stage_collision_floor_line_is_platform(ctx->stage_id, publication->contact.ground_id) &&
      stage_collision_floor_line_has_platform_transform(ctx->stage_id,
                                                        publication->contact.ground_id) &&
      batch->state.speed_y_self[idx] >= 0.0f && !escapeair_stale_platform_root_handoff_hit &&
      !escapeair_fresh_jump_height_platform_handoff_hit && batch->state.action_frame[idx] <= 4) {
    // Sustained no-lock EscapeAir transformed-platform rows no longer have CollData_X130
    // provenance to publish a carried root projection when the current EscapeAir bottom sweep is
    // not descending into the platform. Fresh JumpAerial -> EscapeAir handoffs are passed in
    // through `escapeair_fresh_jump_height_platform_handoff_hit`; descending sustained EscapeAir
    // rows can still land through the ordinary current bottom sweep.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_EscapeAir_Coll
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80044628_Floor,mpColl_80044838_Floor}
    mpcoll_reject_floor_publication_to_current_bottom(ctx, publication, seed_ground_id, y,
                                                      cur_bottom_x, cur_bottom_y);
  }

  if (publication->on_ground &&
      (msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id) ==
       (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) &&
      raw_current_floor_line_idx >= 0 && msl_char_params_fast(ctx->char_id) != NULL &&
      batch->state.action_frame[idx] <=
          (int16_t)msl_char_params_fast(ctx->char_id)->firefox_bound_delay_frames &&
      !(prev_bottom_y > (publication->contact.contact_y + k_floor_y_bias) &&
        cur_bottom_y <= (publication->contact.contact_y + k_floor_y_bias)) &&
      !stage_collision_floor_line_is_platform(ctx->stage_id, seed_ground_id) &&
      !floor_x_within_line_bounds(batch, ctx->bi, ctx->floor_graph, raw_current_floor_line_idx,
                                  prev_x) &&
      !stage_collision_floor_line_is_platform(ctx->stage_id, publication->contact.ground_id)) {
    // SpecialAirHi_Coll floor handoff comes from callback-local mpColl floor result, not an
    // endpoint-clamped projection from a stale carried floor.index.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
    //   ftFx_SpecialAirHi_Coll,ftFox_SpecialHi_IsBound}
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_800473CC,mpColl_80044628_Floor}
    mpcoll_reject_floor_publication_to_current_bottom(ctx, publication, seed_ground_id, y,
                                                      cur_bottom_x, cur_bottom_y);
  }

  if (damage_active_hitlag_downward_sdi_airborne_owner ||
      damage_active_hitlag_root_below_bottom_above_floor_owner) {
    // Active-hitlag damage floor candidates keep the current hitlag-callback root until the damage
    // collision owner resolves floor contact.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
    //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll,ftCo_DamageFly_Coll}
    mpcoll_reject_floor_publication_to_current_bottom(ctx, publication, seed_ground_id, y,
                                                      cur_bottom_x, cur_bottom_y);
    mpcoll_discard_callback_floor_result(ctx);
  }
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
  if (*ground_id_io != 0xFFFFu &&
      stage_collision_floor_line_has_height_platform_transform(ctx->stage_id, *ground_id_io) &&
      stage_collision_floor_line_height_platform_state_is_source_trusted(batch, ctx->bi,
                                                                         *ground_id_io)) {
    const int carry_line_idx = stage_collision_floor_line_index(ctx->stage_id, *ground_id_io);
    if (carry_line_idx >= 0 && (size_t)carry_line_idx < g->line_count &&
        floor_x_within_line_segment_strict(batch, ctx->bi, g, carry_line_idx,
                                           batch->state.pos_x[ctx->idx])) {
      float platform_dx = 0.0f;
      float platform_dy = 0.0f;
      float platform_line_y = 0.0f;
      if (stage_collision_floor_line_motion_delta(batch, ctx->bi, &g->lines[(size_t)carry_line_idx],
                                                  &platform_dx, &platform_dy) &&
          floor_line_y_at_x_for_env(batch, ctx->bi, g, carry_line_idx, batch->state.pos_x[ctx->idx],
                                    &platform_line_y) &&
          fabsf((batch->state.pos_y[ctx->idx] + platform_dy) -
                (platform_line_y + k_floor_y_bias)) <= (2.0f * k_floor_y_bias)) {
        // DownBound_Coll uses the allow-ground-to-air floor owner
        // (`ft_80082708 -> mpColl_8004B108`): the fighter can remain airborne while
        // CollData.floor/cur_pos follow a moving grIzumi side platform. Apply that carry at the
        // final airborne CollData writeback, after floor publication has been decided, so the
        // carried root does not create an artificial hard-floor sweep in the same callback.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
        // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
        // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
        // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
        batch->state.pos_x[ctx->idx] += platform_dx;
        batch->state.pos_y[ctx->idx] += platform_dy;
      }
    }
  }
  const MslCharParams* chp = msl_char_params_fast(ctx->char_id);
  const float ledge_snap_height =
      (chp != NULL && isfinite(chp->ledge_snap_height) && chp->ledge_snap_height > 0.0f)
          ? (chp->ledge_snap_height * batch->state.fighter_scale_y[ctx->idx])
          : k_ecb_vertical_unit;
  int best_height_platform_idx = -1;
  float best_height_platform_y = 0.0f;
  float best_height_platform_dy = 0.0f;
  for (size_t li = 0; li < g->line_count; li++) {
    const uint16_t segment_i = g->lines[li].segment_i;
    if (!stage_collision_floor_line_has_height_platform_transform(ctx->stage_id, segment_i) ||
        !stage_collision_floor_line_height_platform_state_is_source_trusted(batch, ctx->bi,
                                                                            segment_i) ||
        !floor_x_within_line_segment_strict(batch, ctx->bi, g, (int)li,
                                            batch->state.pos_x[ctx->idx])) {
      continue;
    }
    float line_y = 0.0f;
    if (!floor_line_y_at_x_for_env(batch, ctx->bi, g, (int)li, batch->state.pos_x[ctx->idx],
                                   &line_y)) {
      continue;
    }
    const float dy = (line_y + k_floor_y_bias) - batch->state.pos_y[ctx->idx];
    if (!(dy > 0.0f && dy <= ledge_snap_height)) {
      continue;
    }
    if (best_height_platform_idx < 0 || dy < best_height_platform_dy ||
        (dy == best_height_platform_dy &&
         segment_i < g->lines[(size_t)best_height_platform_idx].segment_i)) {
      best_height_platform_idx = (int)li;
      best_height_platform_y = line_y;
      best_height_platform_dy = dy;
    }
  }
  if (best_height_platform_idx >= 0) {
    // DownBound_Coll uses ft_80082708 -> mpColl_8004B108, which loads the current ECB and runs the
    // allow-ground-to-air floor path. Source can update CollData.floor.index/cur_pos from a
    // source-trusted FoD height-platform line while still returning GA_Air to the DownBound
    // callback. Keep this to generated height-transform lines with live/sparse source authority and
    // the character ledge-snap vertical neighborhood.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
    // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
    // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
    // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
    // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
    *ground_id_io = g->lines[(size_t)best_height_platform_idx].segment_i;
    batch->state.pos_y[ctx->idx] = best_height_platform_y + k_floor_y_bias;
    return 1u;
  }

  if (*ground_id_io != 0xFFFFu &&
      stage_collision_floor_line_has_height_platform_transform(ctx->stage_id, *ground_id_io) &&
      stage_collision_floor_line_height_platform_state_is_source_trusted(batch, ctx->bi,
                                                                         *ground_id_io)) {
    const int carried_line_idx = stage_collision_floor_line_index(ctx->stage_id, *ground_id_io);
    float carried_line_y = 0.0f;
    if (carried_line_idx >= 0 &&
        floor_line_y_at_x_for_env(batch, ctx->bi, g, carried_line_idx, batch->state.pos_x[ctx->idx],
                                  &carried_line_y) &&
        carried_line_y < -k_floor_y_bias) {
      int best_hard_idx = -1;
      float best_hard_y = 0.0f;
      float best_hard_dy = 0.0f;
      for (size_t li = 0; li < g->line_count; li++) {
        const MslStageFloorLine* line = &g->lines[li];
        if (!line->fighter_solid || line->is_platform || line->is_ledge ||
            stage_collision_floor_line_has_platform_transform(ctx->stage_id, line->segment_i) ||
            !floor_x_within_line_bounds(batch, ctx->bi, g, (int)li, batch->state.pos_x[ctx->idx])) {
          continue;
        }
        float line_y = 0.0f;
        if (!floor_line_y_at_x_for_env(batch, ctx->bi, g, (int)li, batch->state.pos_x[ctx->idx],
                                       &line_y)) {
          continue;
        }
        const float dy = batch->state.pos_y[ctx->idx] - line_y;
        if (!(dy >= -k_floor_y_bias && dy <= k_ecb_vertical_unit)) {
          continue;
        }
        if (best_hard_idx < 0 || dy < best_hard_dy ||
            (dy == best_hard_dy && line->segment_i < g->lines[(size_t)best_hard_idx].segment_i)) {
          best_hard_idx = (int)li;
          best_hard_y = line_y;
          best_hard_dy = dy;
        }
      }
      if (best_hard_idx >= 0) {
        // DownBound_Coll's ft_80082708 -> mpColl_8004B108 floor owner keeps ground_or_air
        // airborne while updating CollData.floor. When a FoD side platform reaches grIzumi's
        // hidden target, MSLSTG01 places the generated height line below the main floor; source
        // mpLib can then refresh the persisted DownBound floor to the exposed hard floor without
        // entering Fall. The source floor query admits endpoint-adjacent floor bounds, so keep this
        // to source-trusted hidden height-platform floors and nearby non-platform hard floors
        // instead of requiring a strict in-span root.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::ftCo_DownBound_Coll
        // refs/melee/src/melee/ft/ft_081B.c::ft_80082708
        // refs/melee/src/melee/mp/mpcoll.c::mpColl_8004B108
        // refs/melee/src/melee/mp/mplib.c::mpLib_8004DD90_Floor
        // refs/melee/src/melee/gr/grizumi.c::grIzumi_801CC358
        // data/stages/bin/griz.bin::MSLSTG01 platform_transforms(kind=height)
        *ground_id_io = g->lines[(size_t)best_hard_idx].segment_i;
        batch->state.pos_y[ctx->idx] = best_hard_y + k_floor_y_bias;
        return 1u;
      }
    }
  }

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

static inline uint8_t mpcoll_maybe_project_specialhi_air_launch_platform_pass(
    const MslMpcollContext* ctx, uint16_t ground_id) {
  if (ctx == NULL || ctx->batch == NULL || ctx->floor_graph == NULL ||
      msl_motion_state_fx_special_kind(ctx->char_id, ctx->action_id) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI ||
      ctx->batch->state.action_frame[ctx->idx] > 0 ||
      (msl_motion_state_fx_special_kind(ctx->char_id,
                                        ctx->batch->state.seed_prev_action_id[ctx->idx]) !=
           (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD &&
       msl_motion_state_fx_special_kind(ctx->char_id,
                                        ctx->batch->state.seed_prev_action_id[ctx->idx]) !=
           (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD_AIR) ||
      ground_id == 0xFFFFu || !stage_collision_floor_line_is_platform(ctx->stage_id, ground_id)) {
    return 0u;
  }
  const int line_idx = stage_collision_floor_line_index(ctx->stage_id, ground_id);
  if (line_idx < 0 ||
      !floor_x_within_line_segment_strict(ctx->batch, ctx->bi, ctx->floor_graph, line_idx,
                                          ctx->batch->state.pos_x[ctx->idx])) {
    return 0u;
  }
  float line_y = 0.0f;
  const uint8_t line_y_valid =
      floor_line_y_at_x_for_env(ctx->batch, ctx->bi, ctx->floor_graph, line_idx,
                                ctx->batch->state.pos_x[ctx->idx], &line_y)
          ? 1u
          : 0u;
  float projected_y = line_y + k_floor_y_bias;
  if (isfinite(ctx->batch->state.floor_sweep_prev_pos_y[ctx->idx])) {
    projected_y = ctx->batch->state.floor_sweep_prev_pos_y[ctx->idx];
  } else if (!line_y_valid) {
    return 0u;
  }
  const float lift = projected_y - ctx->batch->state.pos_y[ctx->idx];
  if (!(lift >= 0.0f &&
        lift <= (fabsf(ctx->batch->state.speed_y_self[ctx->idx]) + k_ecb_vertical_unit))) {
    return 0u;
  }
  // SpecialHiHold's grounded anim-end fallback can enter aerial launch through
  // ftCommon_8007D60C when `ftCo_8009A134` consumes a platform pass-through. Source carries the
  // old CollData.floor.index/cur_pos into the same-frame SpecialAirHi collision callback; the
  // platform pass-through floor check may keep that carried cur_pos Y while leaving ground_or_air
  // airborne.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D60C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Pass.c::ftCo_8009A134
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialHiHold_Anim,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Coll}
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B108,mpColl_80044628_Floor}
  ctx->batch->state.pos_y[ctx->idx] = projected_y;
  return 1u;
}

void mpcoll_commit_final_floor_state(const MslMpcollContext* ctx,
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
    const uint8_t has_callback_floor_contact =
        mpcoll_floor_contact_from_callback_result(ctx, &contact);
    const uint8_t final_root_remapped = mpcoll_grounded_final_root_flat_seam_remap(ctx, &contact);
    if (!has_callback_floor_contact || final_root_remapped) {
      // Source mpColl keeps the accepted floor result local to the callback before the wrapper
      // consumes it. Mirror ordinary direct floor hits here so every grounded publication reaches
      // final writeback through the same CollData-shaped scratch lane as explicit 4A908 retries.
      // Untagged producers are deliberately DIRECT_PUBLICATION, not BOTTOM_SWEEP: exact
      // bottom-sweep candidates must be tagged when produced.
      // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044628_Floor}
      const uint8_t mode = publication->result_mode != (uint8_t)MSL_MPCOLL_FLOOR_MODE_NONE
                               ? publication->result_mode
                               : (uint8_t)MSL_MPCOLL_FLOOR_MODE_DIRECT_PUBLICATION;
      mpcoll_record_callback_floor_result_with_mode(
          ctx, (uint8_t)MSL_MPCOLL_FLOOR_RESULT_DIRECT, mode, contact.ground_id, contact.contact_x,
          contact.contact_y, contact.normal_x, contact.normal_y);
    }
    mpcoll_commit_grounded_floor_contact(ctx, &contact, ctx->was_grounded);
    return;
  }

  MslMpcollFloorContact airborne_contact = {0};
  MslMpcollFloorContact* airborne_contact_ptr = NULL;
  if (mpcoll_callback_floor_result_valid(ctx) &&
      ctx->batch->state.coll_floor_result_source[idx] ==
          (uint8_t)MSL_MPCOLL_FLOOR_RESULT_STAY_AIRBORNE &&
      mpcoll_floor_contact_from_callback_result(ctx, &airborne_contact)) {
    airborne_contact_ptr = &airborne_contact;
  }
  if (mpcoll_callback_floor_result_valid(ctx)) {
    mpcoll_discard_callback_floor_result(ctx);
  }
  uint16_t ground_id = publication->airborne_ground_id;
  (void)mpcoll_maybe_project_specialhi_air_launch_platform_pass(ctx, ground_id);
  (void)mpcoll_maybe_refresh_downbound_airborne_floor_index(ctx, publication->cur_bottom_x,
                                                            publication->cur_bottom_y, &ground_id);
  mpcoll_commit_airborne_floor_state(ctx, ground_id, airborne_contact_ptr);
}

uint8_t msl_mpcheck_floor(const MslBatch* batch, size_t idx, int bi, const MslStageFloorGraph* g,
                          uint32_t stage_id, float ax, float ay, float bx, float by,
                          uint16_t skip_platform_segment_i, int prefer_line_idx, int skip_line_idx,
                          const MslCommonParams* c, int* out_line_idx, float* out_ix, float* out_iy,
                          float* out_nx, float* out_ny) {
  // Decomp: mpCheckFloor iterates floor lines, intersects segment A->B with each, and chooses the
  // closest intersection to A (min dist^2), with stage-defined deterministic ordering on ties.
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
  if (g == NULL || out_line_idx == NULL) {
    return 0;
  }
  const int16_t joint_id_skip = (batch != NULL && batch->state.mpcoll_joint_id_skip != NULL)
                                    ? batch->state.mpcoll_joint_id_skip[idx]
                                    : -1;
  const int16_t joint_id_only = (batch != NULL && batch->state.mpcoll_joint_id_only != NULL)
                                    ? batch->state.mpcoll_joint_id_only[idx]
                                    : -1;

  if (prefer_line_idx < 0 && skip_line_idx < 0 &&
      !stage_collision_stage_has_deferred_static_floor_transform(stage_id)) {
    const uint8_t platform_callback_admits_floor =
        (batch == NULL || c == NULL ||
         !action_uses_ftco_80096cc8_floor_callback(batch->state.action_id[idx]) ||
         stick_i8_to_unit(batch->state.input_main_y[idx]) > c->platform_air_land_stick_y_threshold)
            ? 1u
            : 0u;
    if (platform_callback_admits_floor) {
      MslStageQueryHit hit = {0};
      const uint8_t static_hit =
          stage_collision_static_query(stage_id, (uint32_t)MSL_STAGE_QUERY_FLOOR, ax, ay, bx, by,
                                       skip_platform_segment_i, joint_id_skip, joint_id_only, &hit);
      if (static_hit) {
        const int hit_line_idx = hit.line_idx;
        if (hit_line_idx >= 0 && (size_t)hit_line_idx < g->line_count &&
            floor_line_admitted_by_source_callback(batch, idx, g, stage_id, hit_line_idx,
                                                   skip_platform_segment_i, c)) {
          // Static Phase-1 substrate now owns the source-shaped mpCheckFloor sweep for
          // no-preference floor producer calls. Dynamic/deferred platform transforms and
          // persisted-floor preference still fall through to the graph path below.
          // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
          // refs/melee/src/melee/mp/mpcoll.c::mpColl_80044628_Floor
          *out_line_idx = hit_line_idx;
          if (out_ix) {
            *out_ix = hit.x;
          }
          if (out_iy) {
            *out_iy = hit.y;
          }
          if (out_nx) {
            *out_nx = hit.normal_x;
          }
          if (out_ny) {
            *out_ny = hit.normal_y;
          }
          return 1u;
        }
      } else {
        return 0u;
      }
    }
  }

  uint8_t found = 0;
  const uint8_t use_joint_filter = (joint_id_skip >= 0 || joint_id_only >= 0) ? 1u : 0u;
  float best_dist2 = FLT_MAX;
  int best_idx = -1;
  int best_pref = -1;
  float best_ix = 0.0f, best_iy = 0.0f;
  float best_nx = 0.0f, best_ny = 1.0f;

  for (size_t li = 0; li < g->line_count; li++) {
    const MslStageFloorLine* line = &g->lines[li];
    if (skip_line_idx >= 0 && (int)li == skip_line_idx) {
      continue;
    }
    if (use_joint_filter && ((joint_id_skip >= 0 && line->joint_id == joint_id_skip) ||
                             (joint_id_only >= 0 && line->joint_id != joint_id_only))) {
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
      // Source sloped-line branch: mpLineIntersection's 0.1 half-space slop (prev not far
      // below the line, cur not far above), endpoint-clamped, NO direction gate - the
      // ay >= by descent gate belongs to the horizontal branch only.
      // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLineIntersection}
      hit = msl_mplib_line_intersection(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy);
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
      if (!msl_psvec2_normalize(nx, ny, &nx, &ny)) {
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

uint8_t msl_mpcheck_hard_floor(const MslBatch* batch, size_t idx, int bi,
                               const MslStageFloorGraph* g, uint32_t stage_id, float ax, float ay,
                               float bx, float by, int prefer_line_idx, int skip_line_idx,
                               uint8_t admit_ledge, int* out_line_idx, float* out_ix, float* out_iy,
                               float* out_nx, float* out_ny) {
  if (g == NULL || out_line_idx == NULL) {
    return 0u;
  }
  const int16_t joint_id_skip = (batch != NULL && batch->state.mpcoll_joint_id_skip != NULL)
                                    ? batch->state.mpcoll_joint_id_skip[idx]
                                    : -1;
  const int16_t joint_id_only = (batch != NULL && batch->state.mpcoll_joint_id_only != NULL)
                                    ? batch->state.mpcoll_joint_id_only[idx]
                                    : -1;

  // Hard-floor substep producer:
  // `mpColl_80043754` can leave a transformed/static platform candidate airborne, then continue the
  // callback-local floor producer on a later interpolation point. This helper is not a broad root
  // clamp: it keeps the same mpCheckFloor intersection ordering but filters the candidate set to
  // fighter-solid non-platform hard floors before projection. It is used only after a real current
  // ECB-bottom segment crosses the floor.
  //
  // data/stages/bin/*.bin::MSLSTG01 fighter_solid/is_platform/platform_transform metadata
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_80043754,mpColl_80044628_Floor}
  // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
  uint8_t found = 0u;
  const uint8_t use_joint_filter = (joint_id_skip >= 0 || joint_id_only >= 0) ? 1u : 0u;
  float best_dist2 = FLT_MAX;
  int best_idx = -1;
  int best_pref = -1;
  float best_ix = 0.0f;
  float best_iy = 0.0f;
  float best_nx = 0.0f;
  float best_ny = 1.0f;
  for (size_t li = 0; li < g->line_count; li++) {
    const MslStageFloorLine* line = &g->lines[li];
    if (skip_line_idx >= 0 && (int)li == skip_line_idx) {
      continue;
    }
    if (use_joint_filter && ((joint_id_skip >= 0 && line->joint_id == joint_id_skip) ||
                             (joint_id_only >= 0 && line->joint_id != joint_id_only))) {
      continue;
    }
    // Source mpCheckFloor has NO ledge-line filter (a ledge-grabbable strip is an ordinary
    // landable floor); the exclusion below is retained scoping for consumers whose post-hit
    // ledge rejection depends on the next-nearest non-ledge line being returned. Producers
    // that want the source set pass admit_ledge.
    // refs/melee/src/melee/mp/mplib.c::mpCheckFloor
    if (!floor_line_is_runtime_fighter_solid(g, stage_id, (int)li) || line->is_platform ||
        (!admit_ledge && line->is_ledge) ||
        line->platform_transform_kind != MSL_STAGE_PLATFORM_TRANSFORM_NONE) {
      continue;
    }

    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    floor_ed5c_endpoints(batch, bi, g, (int)li, &x0, &y0, &x1, &y1);
    float ix = 0.0f;
    float iy = 0.0f;
    const float dy = y0 - y1;
    // Sloped branch: source mpLineIntersection shape (see msl_mpcheck_floor).
    // refs/melee/src/melee/mp/mplib.c::{mpCheckFloor,mpLineIntersection}
    const uint8_t hit = (fabsf(dy) > k_floor_horiz_dy_thresh)
                            ? msl_mplib_line_intersection(x0, y0, x1, y1, ax, ay, bx, by, &ix, &iy)
                            : floor_intersect_horiz(x0, y0, x1, ax, ay, bx, by, &ix, &iy);
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
    if (!found || dist2 < best_dist2 || (dist2 == best_dist2 && pref > best_pref) ||
        (dist2 == best_dist2 && pref == best_pref &&
         line->segment_i < g->lines[(size_t)best_idx].segment_i)) {
      found = 1u;
      best_dist2 = dist2;
      best_idx = (int)li;
      best_pref = pref;
      best_ix = ix;
      best_iy = iy;
      float nx = -(y1 - y0);
      float ny = x1 - x0;
      if (!msl_psvec2_normalize(nx, ny, &nx, &ny)) {
        nx = 0.0f;
        ny = 1.0f;
      }
      best_nx = nx;
      best_ny = ny;
    }
  }

  if (!found) {
    return 0u;
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
  return 1u;
}

uint16_t mpcoll_probe_segment_for_line(const MslStageFloorGraph* g, int line_idx) {
  if (g == NULL || line_idx < 0 || (size_t)line_idx >= g->line_count) {
    return 0xFFFFu;
  }
  return g->lines[(size_t)line_idx].segment_i;
}
