#include "puff_specials.h"

#include <math.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "ids.h"
#include "locomotion.h"
#include "move_tables.h"
#include "stage_collision.h"

// ---------------------------------------------------------------------------
// Multi-jump ladder (ftPr_MS_JumpAerialF1..F5)
// ---------------------------------------------------------------------------

static inline uint8_t pr_anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  return (end > 0.0f && msl_anim_frame_sanitize_f32(anim_frame_f32) >= end) ? 1u : 0u;
}

static inline float pr_stick_unit(int8_t raw) {
  return (float)raw / 80.0f;
}

static inline float pr_apply_deadzone(float v, float dz) {
  return (v > -dz && v < dz) ? 0.0f : v;
}

static inline float pr_facing_dir(const MslBatch* batch, size_t idx) {
  return batch->state.facing[idx] ? 1.0f : -1.0f;
}


static uint8_t puff_b_entry_admissible(const MslBatch* batch, size_t idx, uint16_t a);
static void pr_enter_pound(MslBatch* batch, size_t idx, uint8_t on_ground);
static void pr_enter_rest(MslBatch* batch, size_t idx, uint8_t on_ground);
static void pr_enter_sing(MslBatch* batch, size_t idx, uint8_t on_ground);
static void pr_enter_rollout(MslBatch* batch, const MslCharParams* ch, size_t idx,
                             uint8_t on_ground);
static void pr_rollout_update(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                              size_t idx, uint16_t a);
static void pr_rollout_end(MslBatch* batch, const MslCharParams* ch, size_t idx, uint8_t is_air);
static inline uint8_t pr_action_is_sing(uint16_t a);
static inline uint8_t pr_action_is_sing_grounded(uint16_t a);
static inline uint8_t pr_action_is_rest(uint16_t a);
static inline uint8_t pr_action_is_rest_grounded(uint16_t a);

void puff_mjump_turn_tick(MslBatch* batch, const MslCharParams* ch, size_t idx) {
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ft_800CB6EC
  if (batch == NULL || batch->state.puff_mjump_turn_timer[idx] == 0u) {
    return;
  }
  batch->state.puff_mjump_turn_timer[idx]--;
  if (ch != NULL && ch->puff_mjump_turn_frames > 0 &&
      batch->state.puff_mjump_turn_timer[idx] == (uint8_t)(ch->puff_mjump_turn_frames / 2)) {
    batch->state.facing[idx] = batch->state.facing[idx] ? 0u : 1u;
    batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
  }
}

void puff_specials_update_pre_physics(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.stocks[idx] == 0u || batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }
      const uint8_t cid = batch->state.char_id[idx];
      const uint16_t a = batch->state.action_id[idx];
      const MslCharParams* ch = msl_char_params_fast(cid);
      if (ch == NULL || cid != (uint8_t)MSL_CHAR_ID_PUFF) {
        continue;
      }

      // ---- Rest / Sing Anim callbacks ------------------------------------
      if (pr_action_is_rest(a) || pr_action_is_sing(a)) {
        // Grounded end -> ft_8008A2BC Wait; air end -> ftCo_Fall_Enter. Same-proc destination
        // IASA runs like the Pound exits below.
        // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialLw.c::{ftPr_SpecialLw_Anim,
        //   ftPr_SpecialAirLw_Anim}
        if (pr_anim_finished(cid, puff_special_submotion(a), batch->state.anim_frame_f32[idx])) {
          if (pr_action_is_rest_grounded(a) || pr_action_is_sing_grounded(a)) {
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
            const float sx = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_x[idx]),
                                               c->lstick_deadzone_x);
            const float sy = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_y[idx]),
                                               c->lstick_deadzone_y);
            (void)locomotion_wait_iasa_locomotion_subset_try_enter(
                batch, c, ch, idx, batch->state.input_buttons[idx],
                batch->state.input_buttons_pressed[idx], sx, sy, batch->state.tilt_timer_x[idx],
                batch->state.tilt_timer_y[idx], pr_facing_dir(batch, idx), (uint16_t)MSL_ACT_WAIT);
          } else {
            msl_locomotion_enter_fall_via_ftco_fall_enter(batch, ch, idx);
            (void)msl_locomotion_run_fall_iasa_non_special_tail(batch, c, ch, idx);
          }
        }
        continue;
      }

      // ---- Pound Anim callbacks -------------------------------------------
      if (a == (uint16_t)MSL_ACT_PR_SPECIAL_S) {
        // ftPr_SpecialS_Anim: anim end -> ft_8008A2BC Wait (destination Wait IASA runs in the
        // same proc via the locomotion subset helper).
        // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialS.c::ftPr_SpecialS_Anim
        if (pr_anim_finished(cid, puff_special_submotion(a), batch->state.anim_frame_f32[idx])) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          const float sx = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_x[idx]),
                                             c->lstick_deadzone_x);
          const float sy = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_y[idx]),
                                             c->lstick_deadzone_y);
          (void)locomotion_wait_iasa_locomotion_subset_try_enter(
              batch, c, ch, idx, batch->state.input_buttons[idx],
              batch->state.input_buttons_pressed[idx], sx, sy, batch->state.tilt_timer_x[idx],
              batch->state.tilt_timer_y[idx], pr_facing_dir(batch, idx), (uint16_t)MSL_ACT_WAIT);
        }
        continue;
      }
      if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_S) {
        // ftPr_SpecialAirS_Anim: anim end -> ftCo_Fall_Enter (+ same-proc Fall IASA).
        // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialS.c::ftPr_SpecialAirS_Anim
        if (pr_anim_finished(cid, puff_special_submotion(a), batch->state.anim_frame_f32[idx])) {
          msl_locomotion_enter_fall_via_ftco_fall_enter(batch, ch, idx);
          (void)msl_locomotion_run_fall_iasa_non_special_tail(batch, c, ch, idx);
        }
        continue;
      }

      // ---- Rollout family (346..362): stateful machine, all anims frozen or short ------
      if (puff_action_is_rollout(cid, a)) {
        pr_rollout_update(batch, c, ch, idx, a);
        continue;
      }

      // ---- B dispatch (all four specials live) -----------------------------
      if (a < 341u || puff_action_is_multijump(cid, a)) {
        if (batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] == 0u &&
            (batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) != 0u &&
            puff_b_entry_admissible(batch, idx, a)) {
          const float sx = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_x[idx]),
                                             c->lstick_deadzone_x);
          const float sy = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_y[idx]),
                                             c->lstick_deadzone_y);
          const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;
          // Zone resolution mirrors the falcon dispatcher's chain subset: side, then up, then
          // down, then the remaining neutral band.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
          if (sy < c->special_stick_y_threshold && sy > -c->special_stick_y_threshold &&
              fabsf(sx) >= c->special_stick_x_threshold_side) {
            // Side-B facing update before the Enter (both grounded and aerial dispatchers).
            if ((sx > 0.0f) != (batch->state.facing[idx] != 0u)) {
              batch->state.facing[idx] = (uint8_t)(sx > 0.0f);
              batch->state.facing_dir1[idx] = batch->state.facing[idx] ? 1 : -1;
            }
            if (on_ground) {
              // Grounded Side-B entry damps gr_vel by the common xB8 multiplier before Enter.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialS.c::doEnter
              batch->state.speed_ground_x_self[idx] *= ch->side_special_ground_entry_vel_mul;
            }
            pr_enter_pound(batch, idx, on_ground);
            continue;
          }
          // Up-B zone -> Sing.
          if (sy >= c->special_stick_y_threshold) {
            pr_enter_sing(batch, idx, on_ground);
            continue;
          }
          // Down-B zone -> Rest. Aerial chain checks the down zone before side; grounded
          // Wait-IASA reaches SpecialLw after the side/neutral owners decline, so a stick in
          // the down zone (which the side branch above already excluded via its sy guard) is
          // unambiguous in both chains.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
          if (sy <= -c->special_stick_y_threshold) {
            pr_enter_rest(batch, idx, on_ground);
            continue;
          }
          // Neutral zone -> Rollout (ftPr_SpecialN/ftPr_SpecialAirN Enter).
          // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::{ftPr_SpecialN_Enter,
          //   ftPr_SpecialAirN_Enter}
          pr_enter_rollout(batch, ch, idx, on_ground);
          continue;
        }
        if (!puff_action_is_multijump(cid, a)) {
          continue;
        }
      }

      if (!puff_action_is_multijump(cid, a)) {
        continue;
      }

      // ftCo_JumpAerialF1_Anim runs ft_800CB6EC every frame. The entry itself
      // (ftCo_800D74A4) already applied one tick, so skip the tick on entry rows
      // (prev_action_id != a covers both live entries earlier this action phase and
      // teacher-forced seed rows whose source entry tick already happened pre-serialize).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_800D74A4,
      //   ftCo_JumpAerialF1_Anim}
      if (batch->state.prev_action_id[idx] == a) {
        puff_mjump_turn_tick(batch, ch, idx);
      }

      // Anim end: jumps exhausted -> FallAerial (Ft_MF_None clears fastfall), else Fall.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_JumpAerialF1_Anim
      const uint16_t msid = puff_special_submotion(a);
      if (pr_anim_finished(cid, msid, batch->state.anim_frame_f32[idx])) {
        if (batch->state.jumps_left[idx] == 0u) {
          batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL_AERIAL;
          batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL_AERIAL;
          msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          batch->state.fall_fast[idx] = 0u;
        } else {
          msl_locomotion_enter_fall_via_ftco_fall_enter(batch, ch, idx);
        }
        // The destination Fall/FallAerial IASA runs in the same Fighter_procUpdate; a held
        // chain input can consume the next ladder jump immediately (the tail's jump entry
        // forks back into the multi-jump admission for this char).
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallAerial.c::ftCo_FallAerial_IASA
        (void)msl_locomotion_run_fall_iasa_non_special_tail(batch, c, ch, idx);
        continue;
      }

      // ftCo_JumpAerial_IASA (shared by the ladder states): aerial attacks, EscapeAir, and the
      // jump chain. B rows are left unconsumed for the future puff specials owner; the jump
      // entry inside the tail routes through the multi-jump admission (chain window gated on
      // the script cmd0 pulse inside locomotion's fork).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
      if (batch->state.hitstun[idx] == 0u &&
          (batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) == 0u) {
        (void)msl_locomotion_run_fall_iasa_non_special_tail(batch, c, ch, idx);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Pound (ftPr_MS_SpecialS 363 / ftPr_MS_SpecialAirS 364)
// ---------------------------------------------------------------------------

// calcAngleRadians: |stick_y| windowed to [range_y_neg, range_y_pos] maps to up to angle_diff
// degrees, sign from stick_y.
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialS.c::calcAngleRadians
static float pr_pound_angle_rad(const MslBatch* batch, const MslCharParams* ch, size_t idx) {
  const float raw_sy = pr_stick_unit(batch->state.input_main_y[idx]);
  float ay = fabsf(raw_sy);
  if (ay > ch->puff_pound_stick_range_y_pos) {
    ay = ch->puff_pound_stick_range_y_pos;
  }
  ay -= ch->puff_pound_stick_range_y_neg;
  if (ay < 0.0f) {
    ay = 0.0f;
  }
  if (raw_sy < 0.0f) {
    ay = -ay;
  }
  const float degrees =
      (ay * ch->puff_pound_angle_diff) /
      (ch->puff_pound_stick_range_y_pos - ch->puff_pound_stick_range_y_neg);
  return degrees * (3.14159265359f / 180.0f);
}

// Side-B admission per current action (the ftCo_*_IASA chains that reach
// ftCo_Special{,Air}_CheckInput). Adapted from the falcon dispatcher's decomp-anchored state
// families, plus the puff multi-jump ladder whose shared JumpAerial IASA checks SpecialAir
// first. Squat/KneeBend rows (up/down-only zones in the source chains) stay excluded until the
// Sing/Rest owners land.
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_IASA_Inner
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ftCo_JumpAerial_IASA
static uint8_t puff_b_entry_admissible(const MslBatch* batch, size_t idx, uint16_t a) {
  if (puff_action_is_multijump(batch->state.char_id[idx], a)) {
    return 1u;
  }
  if (batch->state.on_ground[idx]) {
    switch (a) {
      case MSL_ACT_WAIT:
      case MSL_ACT_WALK_SLOW:
      case MSL_ACT_WALK_MIDDLE:
      case MSL_ACT_WALK_FAST:
      case MSL_ACT_SQUAT:
      case MSL_ACT_RUN:
      case MSL_ACT_RUN_DIRECT:
      case MSL_ACT_OTTOTTO:
      case MSL_ACT_OTTOTTO_WAIT:
      case MSL_ACT_TURN:
      case MSL_ACT_DASH:
        return 1u;
      case MSL_ACT_LANDING: {
        const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
        const float cur = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
        return (ch != NULL && cur >= (float)ch->landing_lag_frames) ? 1u : 0u;
      }
      case MSL_ACT_RUN_BRAKE:
        return (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN ||
                batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN_DIRECT)
                   ? 1u
                   : 0u;
      default:
        return 0u;
    }
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
    case MSL_ACT_PASS:
    case MSL_ACT_DAMAGE_FALL:
      return 1u;
    default:
      return 0u;
  }
}

// Rest (ftPr_SpecialLw 369/371 grounded, 370/372 air; L/R variant by facing). The frame-0
// full invincibility, the frame-1 hitbox, and the frame-27 vulnerability restore all ride the
// generic script machinery (set_hit_status/create_hitbox in msids 323..326); the entry
// accessory4 self-clears on its first call and stays unmodeled.
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialLw.c
static void pr_enter_rest(MslBatch* batch, size_t idx, uint8_t on_ground) {
  const uint8_t facing_right = batch->state.facing[idx] ? 1u : 0u;
  const uint16_t act =
      on_ground
          ? (facing_right ? (uint16_t)MSL_ACT_PR_SPECIAL_LW_R : (uint16_t)MSL_ACT_PR_SPECIAL_LW_L)
          : (facing_right ? (uint16_t)MSL_ACT_PR_SPECIAL_AIR_LW_R
                          : (uint16_t)MSL_ACT_PR_SPECIAL_AIR_LW_L);
  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = (uint32_t)puff_special_submotion(act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
  batch->state.special_cmd0[idx] = 0u;
  batch->state.special_cmd1[idx] = 0u;
  batch->state.special_cmd2[idx] = 0u;
}

// Sing (ftPr_SpecialHi 365/367 grounded, 366/368 air; L/R variant by facing). Same state
// shape as Rest: grounded F3C friction / air 84EEC phys, Wait / Fall exits, frame-preserving
// phase flips. The element-6 sleep hitbox (frames 28..126 with size rekeys) rides the generic
// script machinery; the VICTIM DamageSong family is the remaining consumer (documented gap
// until the sleep-victim machine lands).
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialHi.c
static void pr_enter_sing(MslBatch* batch, size_t idx, uint8_t on_ground) {
  const uint8_t facing_right = batch->state.facing[idx] ? 1u : 0u;
  const uint16_t act =
      on_ground
          ? (facing_right ? (uint16_t)MSL_ACT_PR_SPECIAL_HI_R : (uint16_t)MSL_ACT_PR_SPECIAL_HI_L)
          : (facing_right ? (uint16_t)MSL_ACT_PR_SPECIAL_AIR_HI_R
                          : (uint16_t)MSL_ACT_PR_SPECIAL_AIR_HI_L);
  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = (uint32_t)puff_special_submotion(act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
  batch->state.special_cmd0[idx] = 0u;
  batch->state.special_cmd1[idx] = 0u;
  batch->state.special_cmd2[idx] = 0u;
}

static inline uint8_t pr_action_is_sing(uint16_t a) {
  return (uint8_t)(a >= (uint16_t)MSL_ACT_PR_SPECIAL_HI_L &&
                   a <= (uint16_t)MSL_ACT_PR_SPECIAL_AIR_HI_R);
}

static inline uint8_t pr_action_is_sing_grounded(uint16_t a) {
  return (uint8_t)(a == (uint16_t)MSL_ACT_PR_SPECIAL_HI_L ||
                   a == (uint16_t)MSL_ACT_PR_SPECIAL_HI_R);
}

static inline uint8_t pr_action_is_rest(uint16_t a) {
  return (uint8_t)(a == (uint16_t)MSL_ACT_PR_SPECIAL_LW_L ||
                   a == (uint16_t)MSL_ACT_PR_SPECIAL_LW_R ||
                   a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_LW_L ||
                   a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_LW_R);
}

static inline uint8_t pr_action_is_rest_grounded(uint16_t a) {
  return (uint8_t)(a == (uint16_t)MSL_ACT_PR_SPECIAL_LW_L ||
                   a == (uint16_t)MSL_ACT_PR_SPECIAL_LW_R);
}

static void pr_enter_pound(MslBatch* batch, size_t idx, uint8_t on_ground) {
  // ftPr_Special(Air)S_Enter: change motion state at frame 0 and clear cmd_vars[0..3].
  // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialS.c::{ftPr_SpecialS_Enter,
  //   ftPr_SpecialAirS_Enter}
  const uint16_t act =
      on_ground ? (uint16_t)MSL_ACT_PR_SPECIAL_S : (uint16_t)MSL_ACT_PR_SPECIAL_AIR_S;
  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = (uint32_t)puff_special_submotion(act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
  batch->state.special_cmd0[idx] = 0u;
  batch->state.special_cmd1[idx] = 0u;
  batch->state.special_cmd2[idx] = 0u;
}

// ---------------------------------------------------------------------------
// Rollout (ftPr_MS_SpecialN* 346..362; ftPr_SpecialN.c)
// ---------------------------------------------------------------------------
//
// State model: the whole charge/roll family runs with the animation FROZEN (every internal
// Fighter_ChangeMotionState passes anim rate 0 and the loop states hold frame 0), so exits are
// purely stateful: charge (mv x2C), turn budget (mv x0), roll angle (mv x14), velocity, stick,
// B-release, and on-hit. Only the Start and End bookends animate at rate 1.
// Validated against replay traces: state_age stays 0 across 348/350/351/362 rows.
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c

static const float PR_DEG_TO_RAD = 0.017453292f;  // ftPr_SpecialAirNChargeRelease_Anim literal
static const float PR_HALF_PI = 1.57079632679f;

// mv.pr.specialn.x14 normalization (normalizeAndSetRollAngle; the JObj rotation is cosmetic,
// the scalar gates the Release end).
static inline float pr_rollout_normalize_angle(float ang) {
  while (ang < 0.0f) {
    ang += 2.0f * 3.14159265359f;
  }
  while (ang > 2.0f * 3.14159265359f) {
    ang -= 2.0f * 3.14159265359f;
  }
  return ang;
}

static inline void pr_rollout_set_action(MslBatch* batch, size_t idx, uint16_t act) {
  batch->state.action_id[idx] = act;
  batch->state.animation_index[idx] = (uint32_t)puff_special_submotion(act);
}

static void pr_enter_rollout(MslBatch* batch, const MslCharParams* ch, size_t idx,
                             uint8_t on_ground) {
  // ftPr_Special(Air)N_Enter + ftPr_SpecialS_8013DC64: StartR/L (by facing) at frame 0 rate 1,
  // cmd_vars cleared, x34.x latches facing, x0 = turn budget (da->x34), x2C = charge (da->xA0),
  // x1C = slope/turn accel lane (da->x44 ground / da->x54 air), gr_vel = 0; grounded entry also
  // zeroes vy. The air entry's x74_anim_vel.y = da->x3C is anim-velocity plumbing left
  // unmodeled (cosmetic ascent blend).
  // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::{ftPr_SpecialN_Enter,
  //   ftPr_SpecialAirN_Enter,ftPr_SpecialS_8013DC64}
  const uint8_t facing_right = batch->state.facing[idx] ? 1u : 0u;
  const uint16_t act = on_ground ? (facing_right ? (uint16_t)MSL_ACT_PR_SPECIAL_N_START_R
                                                 : (uint16_t)MSL_ACT_PR_SPECIAL_N_START_L)
                                 : (facing_right ? (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_R
                                                 : (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_L);
  pr_rollout_set_action(batch, idx, act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  msl_anim_timebase_tick_once(batch, idx);
  batch->state.special_cmd0[idx] = 0u;
  batch->state.special_cmd1[idx] = 0u;
  batch->state.special_cmd2[idx] = 0u;
  batch->state.puff_rollout_dir[idx] = facing_right ? 1 : -1;
  batch->state.puff_rollout_turn_budget[idx] = (int16_t)ch->puff_rollout_turn_budget_frames;
  batch->state.puff_rollout_charge[idx] = ch->puff_rollout_charge_init;
  batch->state.puff_rollout_angle[idx] = 0.0f;
  batch->state.puff_rollout_pre_turn_vel[idx] = 0.0f;
  batch->state.puff_rollout_facing_restore[idx] = 0;
  batch->state.puff_rollout_slope_lane[idx] =
      on_ground ? ch->puff_rollout_slope_lane_ground : ch->puff_rollout_slope_lane_air;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  if (on_ground) {
    batch->state.speed_y_self[idx] = 0.0f;
  }
}

// ftPr_SpecialS_8013DA24: End variant chosen by the roll direction (x34.x), velocity scaled by
// x90 (ground gr_vel / air self_vel.x) and x94 (air vy), then ftPr_SpecialS_8013D658 consumes
// the pending facing latch. anim_start = 0, rate 1 for every state-machine exit.
static void pr_rollout_end(MslBatch* batch, const MslCharParams* ch, size_t idx, uint8_t is_air) {
  const int8_t dir = batch->state.puff_rollout_dir[idx];
  const int8_t latch = batch->state.puff_rollout_facing_restore[idx];
  const uint16_t act = is_air ? (dir == 1 ? (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_END_R
                                          : (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_END_L)
                              : (dir == 1 ? (uint16_t)MSL_ACT_PR_SPECIAL_N_END_R
                                          : (uint16_t)MSL_ACT_PR_SPECIAL_N_END_L);
  pr_rollout_set_action(batch, idx, act);
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
  if (!is_air) {
    batch->state.speed_ground_x_self[idx] *= ch->puff_rollout_end_vel_x_mul;
    batch->state.speed_y_self[idx] = 0.0f;
  } else {
    batch->state.speed_air_x_self[idx] *= ch->puff_rollout_end_vel_x_mul;
    batch->state.speed_y_self[idx] *= ch->puff_rollout_end_vel_y_mul;
    batch->state.speed_ground_x_self[idx] = 0.0f;
  }
  const int8_t facing_dir1 = (latch != 0) ? latch : dir;
  batch->state.facing[idx] = (uint8_t)(facing_dir1 > 0);
  batch->state.facing_dir1[idx] = facing_dir1;
  batch->state.puff_rollout_facing_restore[idx] = 0;
}

static void pr_rollout_update(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                              size_t idx, uint16_t a) {
  const uint8_t cid = batch->state.char_id[idx];
  const float dir = (float)batch->state.puff_rollout_dir[idx];
  switch (a) {
    case MSL_ACT_PR_SPECIAL_N_START_R:
    case MSL_ACT_PR_SPECIAL_N_START_L:
    case MSL_ACT_PR_SPECIAL_AIR_N_START_R:
    case MSL_ACT_PR_SPECIAL_AIR_N_START_L: {
      // ftPr_Special(Air)NStart_Anim: anim end -> charge loop held at frame 0 with anim rate 0
      // and gr_vel = self_vel.x = facing * 0.0001f.
      batch->state.puff_rollout_facing_restore[idx] = 0;
      if (pr_anim_finished(cid, puff_special_submotion(a), batch->state.anim_frame_f32[idx])) {
        const uint8_t ground = (uint8_t)(a == (uint16_t)MSL_ACT_PR_SPECIAL_N_START_R ||
                                         a == (uint16_t)MSL_ACT_PR_SPECIAL_N_START_L);
        pr_rollout_set_action(batch, idx,
                              ground ? (uint16_t)MSL_ACT_PR_SPECIAL_N_LOOP
                                     : (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_LOOP);
        msl_anim_timebase_enter(batch, idx, 0.0f, 0.0f);
        const float fdir = pr_facing_dir(batch, idx);
        batch->state.speed_air_x_self[idx] = fdir * 0.0001f;
        if (ground) {
          // gr_vel = facing * 1e-4 here is transient: the Loop Phys re-zeroes it this frame.
          batch->state.speed_ground_x_self[idx] = fdir * 0.0001f;
        }
      }
      return;
    }
    case MSL_ACT_PR_SPECIAL_N_LOOP:
    case MSL_ACT_PR_SPECIAL_N_FULL:
    case MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_LOOP:
    case MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_FULL: {
      // ftPr_Special(Air)N{Loop,ChargeFull}_Anim: charge x2C += xA8 capped at xA4 (Loop -> Full
      // at the cap; the ftCo_800BFFD0 colanim pulse and the x30 full flag are cosmetic), roll
      // angle accumulates charge * deg2rad(xAC).
      batch->state.puff_rollout_facing_restore[idx] = 0;
      float charge = batch->state.puff_rollout_charge[idx] + ch->puff_rollout_charge_rate;
      if (charge >= ch->puff_rollout_charge_max) {
        charge = ch->puff_rollout_charge_max;
        if (a == (uint16_t)MSL_ACT_PR_SPECIAL_N_LOOP) {
          pr_rollout_set_action(batch, idx, (uint16_t)MSL_ACT_PR_SPECIAL_N_FULL);
        } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_LOOP) {
          pr_rollout_set_action(batch, idx, (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_FULL);
        }
      }
      batch->state.puff_rollout_charge[idx] = charge;
      batch->state.puff_rollout_angle[idx] = pr_rollout_normalize_angle(
          batch->state.puff_rollout_angle[idx] +
          dir * (charge * (PR_DEG_TO_RAD * ch->puff_rollout_loop_roll_rate_deg)));
      // ftPr_Special(Air)N{Loop,Full}_IASA: releasing B fires the roll at the preserved frame
      // (still 0, rate 0) with vel = x34.x * (xC0 * (x2C - xA0)); the Phys recomputes the
      // (x2C - xB8) formula the same frame, so the entry value is transient.
      if ((batch->state.input_buttons[idx] & (uint16_t)MSL_BUTTON_B) == 0u) {
        const uint16_t now = batch->state.action_id[idx];
        const uint8_t ground = (uint8_t)(now == (uint16_t)MSL_ACT_PR_SPECIAL_N_LOOP ||
                                         now == (uint16_t)MSL_ACT_PR_SPECIAL_N_FULL);
        const float vel =
            dir * (ch->puff_rollout_release_vel_scale * (charge - ch->puff_rollout_charge_init));
        pr_rollout_set_action(batch, idx,
                              ground ? (uint16_t)MSL_ACT_PR_SPECIAL_N_RELEASE
                                     : (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE);
        if (ground) {
          batch->state.speed_ground_x_self[idx] = vel;
        } else {
          batch->state.speed_air_x_self[idx] = vel;
        }
      }
      return;
    }
    case MSL_ACT_PR_SPECIAL_N_RELEASE:
    case MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE: {
      // ftPr_SpecialNRelease_Anim / ftPr_SpecialAirNChargeRelease_Anim: roll-angle advance and
      // turn-budget decrement; once the budget is spent the roll ends on the model-upright pi
      // crossing (angle passes pi inside (pi/2, 3pi/2), crossing direction matched to the
      // angular velocity sign).
      batch->state.puff_rollout_facing_restore[idx] = 0;
      const uint8_t is_air = (uint8_t)(a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE);
      const float charge = batch->state.puff_rollout_charge[idx];
      const float delta =
          is_air ? (float)(0.2 * ch->puff_rollout_release_roll_rate * dir) *
                       (PR_DEG_TO_RAD * charge * ch->puff_rollout_roll_rate_scale)
                 : PR_DEG_TO_RAD * charge * (float)(0.2 * ch->puff_rollout_release_roll_rate * dir);
      const float old_ang = batch->state.puff_rollout_angle[idx];
      const float ang = pr_rollout_normalize_angle(old_ang + delta);
      batch->state.puff_rollout_angle[idx] = ang;
      const int16_t budget = (int16_t)(batch->state.puff_rollout_turn_budget[idx] - 1);
      batch->state.puff_rollout_turn_budget[idx] = budget;
      if (budget <= 0) {
        if (PR_HALF_PI < ang && ang < 3.0f * PR_HALF_PI) {
          const float pi = 3.14159265359f;
          if ((delta > 0.0f && ang > pi && old_ang < pi) ||
              (delta <= 0.0f && ang < pi && old_ang > pi)) {
            batch->state.puff_rollout_turn_budget[idx] = 0;
            pr_rollout_end(batch, ch, idx, is_air);
            return;
          }
        }
      }
      // ftPr_SpecialNRelease_IASA (ground only): stick held opposite the roll beyond x68 ->
      // SpecialNTurn at the preserved frame. Latches the post-turn facing (mv facing_dir),
      // stashes the pre-turn gr_vel (x10) and the -0.05 * gr_vel turn accel lane (x1C).
      if (!is_air) {
        const float sx = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_x[idx]),
                                           c->lstick_deadzone_x);
        if (fabsf(sx) > ch->puff_rollout_turn_stick_threshold) {
          const float sdir = (sx >= 0.0f) ? 1.0f : -1.0f;
          if (dir != sdir) {
            batch->state.puff_rollout_facing_restore[idx] = (int8_t)sdir;
            batch->state.puff_rollout_dir[idx] = (int8_t)-sdir;
            pr_rollout_set_action(batch, idx, (uint16_t)MSL_ACT_PR_SPECIAL_N_TURN);
            const float gr_vel = batch->state.speed_ground_x_self[idx];
            batch->state.puff_rollout_pre_turn_vel[idx] = gr_vel;
            batch->state.puff_rollout_slope_lane[idx] = -0.05f * gr_vel;
          }
        }
      }
      return;
    }
    case MSL_ACT_PR_SPECIAL_N_TURN:
    case MSL_ACT_PR_SPECIAL_AIR_N_START_TURN: {
      // ftPr_SpecialNTurn_Anim / ftPr_SpecialAirNStartTurn_Anim: fixed-rate unroll (the air
      // variant scales by xBC) and budget decrement; exhaustion flips the roll direction and
      // ends the move. The source's air exhaustion passes is_air=false and relies on the new
      // grounded End's Coll swapping to the air End the same frame when no floor is present;
      // model that composite directly (vy zeroed by the grounded handoff, then the air tail).
      const uint8_t is_air_turn = (uint8_t)(a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_TURN);
      float ang_delta = (float)(0.2 * ch->puff_rollout_turn_roll_rate) * -dir;
      if (is_air_turn) {
        ang_delta *= ch->puff_rollout_roll_rate_scale;
      }
      batch->state.puff_rollout_angle[idx] =
          pr_rollout_normalize_angle(batch->state.puff_rollout_angle[idx] + ang_delta);
      const int16_t budget = (int16_t)(batch->state.puff_rollout_turn_budget[idx] - 1);
      batch->state.puff_rollout_turn_budget[idx] = budget;
      if (budget <= 0) {
        batch->state.puff_rollout_turn_budget[idx] = 0;
        batch->state.puff_rollout_dir[idx] = (int8_t)-batch->state.puff_rollout_dir[idx];
        if (batch->state.on_ground[idx]) {
          pr_rollout_end(batch, ch, idx, 0u);
        } else {
          // DA24(ground) zeroes vy, then ftPr_SpecialNEnd_Coll's same-frame floor-loss runs the
          // DA24(air) tail (self_vel.x *= x90, vy *= x94 which is 0, gr_vel = 0).
          batch->state.speed_y_self[idx] = 0.0f;
          pr_rollout_end(batch, ch, idx, 1u);
        }
      }
      return;
    }
    case MSL_ACT_PR_SPECIAL_N_END_R:
    case MSL_ACT_PR_SPECIAL_N_END_L: {
      // ftPr_SpecialNEnd_Anim: anim end -> facing restore (latch already consumed by the DA24
      // handoff) + ft_8008A2BC Wait with the same-proc Wait IASA subset (Pound exit shape).
      batch->state.puff_rollout_facing_restore[idx] = 0;
      if (pr_anim_finished(cid, puff_special_submotion(a), batch->state.anim_frame_f32[idx])) {
        batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
        batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
        msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
        const float sx = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_x[idx]),
                                           c->lstick_deadzone_x);
        const float sy = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_y[idx]),
                                           c->lstick_deadzone_y);
        (void)locomotion_wait_iasa_locomotion_subset_try_enter(
            batch, c, ch, idx, batch->state.input_buttons[idx],
            batch->state.input_buttons_pressed[idx], sx, sy, batch->state.tilt_timer_x[idx],
            batch->state.tilt_timer_y[idx], pr_facing_dir(batch, idx), (uint16_t)MSL_ACT_WAIT);
      }
      return;
    }
    case MSL_ACT_PR_SPECIAL_AIR_N_END_R:
    case MSL_ACT_PR_SPECIAL_AIR_N_END_L: {
      // ftPr_SpecialAirNEnd_Anim: anim end -> xD8 == 0 ? Fall : ftCo_80096900 freefall with
      // landing lag xD8.
      batch->state.puff_rollout_facing_restore[idx] = 0;
      if (pr_anim_finished(cid, puff_special_submotion(a), batch->state.anim_frame_f32[idx])) {
        if (ch->puff_rollout_landing_lag > 0.0f) {
          msl_locomotion_enter_fall_special_via_ftco_80096900(batch, idx, /*fallspecial_xc=*/0u,
                                                              ch->puff_rollout_landing_lag,
                                                              /*allow_interrupt=*/0u);
        } else {
          msl_locomotion_enter_fall_via_ftco_fall_enter(batch, ch, idx);
          (void)msl_locomotion_run_fall_iasa_non_special_tail(batch, c, ch, idx);
        }
      }
      return;
    }
    case MSL_ACT_PR_SPECIAL_N_HIT:
      // ftPr_SpecialNHit_Anim: cosmetic roll-angle advance only; no anim-end exit (rate 0).
      // Exits are the landing Coll (LandingFallSpecial with lag xD8) and blast zones.
      return;
    default:
      return;
  }
}

uint8_t puff_specials_phys(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t cid = batch->state.char_id[idx];
  if (cid != (uint8_t)MSL_CHAR_ID_PUFF) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  const MslCharParams* ch = msl_char_params_fast(cid);
  if (ch == NULL) {
    return 0u;
  }
  const uint16_t msid = puff_special_submotion(a);
  const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  switch (a) {
    case MSL_ACT_PR_SPECIAL_S:
      if (batch->state.on_ground[idx] == 0u) {
        return 0u;  // transient pre-swap frame: generic air handling
      }
      // ftPr_SpecialS_Phys -> ft_80084FA8: anim root motion owns the lunge; friction otherwise.
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialS.c::ftPr_SpecialS_Phys
      // refs/melee/src/melee/ft/ft_084E.c::{ft_80084FA8,ft_80085030}
      if (msl_anim_uses_root_motion(cid, msid)) {
        float t_cur[3];
        float t_prev[3];
        const uint16_t f_cur = msl_anim_frame_floor_u16(frame);
        const uint16_t f_prev = (f_cur > 0u) ? (uint16_t)(f_cur - 1u) : 0u;
        if (anim_pose_get_transn(cid, msid, f_cur, t_cur) == 0 &&
            anim_pose_get_transn(cid, msid, f_prev, t_prev) == 0) {
          const float dz = (t_cur[2] - t_prev[2]) * ch->model_scaling;
          batch->state.speed_ground_x_self[idx] = dz * pr_facing_dir(batch, idx);
          return 1u;
        }
      }
      {
        // ft_80084FA8 friction branch (gr_friction with the common high-speed multiplier).
        const MslCommonParams* c = msl_common_params();
        float friction = ch->gr_friction;
        const float v = batch->state.speed_ground_x_self[idx];
        if (fabsf(v) > ch->walk_max_vel && c != NULL) {
          friction *= c->high_speed_friction_mul;
        }
        float nv = v;
        if (nv > 0.0f) {
          nv = (nv > friction) ? nv - friction : 0.0f;
        } else if (nv < 0.0f) {
          nv = (nv < -friction) ? nv + friction : 0.0f;
        }
        batch->state.speed_ground_x_self[idx] = nv;
      }
      return 1u;
    case MSL_ACT_PR_SPECIAL_AIR_S: {
      if (batch->state.on_ground[idx] != 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftPr_SpecialAirS_Phys: consume the cmd_vars[0] pulse once (frame 12) — the stick-angled
      // impulse — then switch on cmd_vars[1]:
      //   0 -> ft_80084EEC (gravity + air friction, no drift)
      //   1 -> self_vel *= puff_pound_vel_decay per frame (gravity suspended, frames 12..39)
      //   2 -> ft_80084DB0 (common fall + fastfall + drift; generic air path owns it, admitted
      //        via the puff branch in msl_action_allows_fastfall)
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialS.c::ftPr_SpecialAirS_Phys
      // data/moves/puff.json::specials_by_msid.318 set_cmd_var pulses @12 (0->1, 1->1), @40 (1->2)
      const uint8_t cmd0 = move_tables_special_cmd_var_value_at_frame(cid, msid, 0u, frame);
      if (cmd0 != 0u && batch->state.special_cmd0[idx] == 0u) {
        batch->state.special_cmd0[idx] = 1u;
        const float ang = pr_pound_angle_rad(batch, ch, idx);
        batch->state.speed_y_self[idx] = ch->puff_pound_vel * sinf(ang);
        batch->state.speed_air_x_self[idx] =
            ch->puff_pound_vel * (pr_facing_dir(batch, idx) * cosf(ang));
      }
      const uint8_t cmd1 = move_tables_special_cmd_var_u8_value_at_frame(cid, msid, 1u, frame);
      if (cmd1 == 0u) {
        // ft_80084EEC: gravity + terminal clamp + air friction.
        float vy = batch->state.speed_y_self[idx] - ch->grav;
        if (vy < -ch->terminal_vel) {
          vy = -ch->terminal_vel;
        }
        batch->state.speed_y_self[idx] = vy;
        float vx = batch->state.speed_air_x_self[idx];
        if (vx > 0.0f) {
          vx = (vx > ch->aerial_friction) ? vx - ch->aerial_friction : 0.0f;
        } else if (vx < 0.0f) {
          vx = (vx < -ch->aerial_friction) ? vx + ch->aerial_friction : 0.0f;
        }
        batch->state.speed_air_x_self[idx] = vx;
        return 1u;
      }
      if (cmd1 == 1u) {
        batch->state.speed_y_self[idx] *= ch->puff_pound_vel_decay;
        batch->state.speed_air_x_self[idx] *= ch->puff_pound_vel_decay;
        return 1u;
      }
      // cmd1 == 2: hand ownership to the generic common-air path (ft_80084DB0).
      return 0u;
    }
    case MSL_ACT_PR_SPECIAL_HI_L:
    case MSL_ACT_PR_SPECIAL_HI_R:
    case MSL_ACT_PR_SPECIAL_LW_L:
    case MSL_ACT_PR_SPECIAL_LW_R:
      if (batch->state.on_ground[idx] == 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftPr_SpecialLw_Phys -> ft_80084F3C (plain grounded friction with the high-speed mul).
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialLw.c::ftPr_SpecialLw_Phys
      {
        const MslCommonParams* c = msl_common_params();
        float friction = ch->gr_friction;
        const float v = batch->state.speed_ground_x_self[idx];
        if (fabsf(v) > ch->walk_max_vel && c != NULL) {
          friction *= c->high_speed_friction_mul;
        }
        float nv = v;
        if (nv > 0.0f) {
          nv = (nv > friction) ? nv - friction : 0.0f;
        } else if (nv < 0.0f) {
          nv = (nv < -friction) ? nv + friction : 0.0f;
        }
        batch->state.speed_ground_x_self[idx] = nv;
      }
      return 1u;
    case MSL_ACT_PR_SPECIAL_AIR_HI_L:
    case MSL_ACT_PR_SPECIAL_AIR_HI_R:
    case MSL_ACT_PR_SPECIAL_AIR_LW_L:
    case MSL_ACT_PR_SPECIAL_AIR_LW_R: {
      if (batch->state.on_ground[idx] != 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftPr_SpecialAirLw_Phys -> ft_80084EEC (gravity + terminal + air friction; no drift).
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialLw.c::ftPr_SpecialAirLw_Phys
      float vy = batch->state.speed_y_self[idx] - ch->grav;
      if (vy < -ch->terminal_vel) {
        vy = -ch->terminal_vel;
      }
      batch->state.speed_y_self[idx] = vy;
      float vx = batch->state.speed_air_x_self[idx];
      if (vx > 0.0f) {
        vx = (vx > ch->aerial_friction) ? vx - ch->aerial_friction : 0.0f;
      } else if (vx < 0.0f) {
        vx = (vx < -ch->aerial_friction) ? vx + ch->aerial_friction : 0.0f;
      }
      batch->state.speed_air_x_self[idx] = vx;
      return 1u;
    }

    // ---- Rollout ---------------------------------------------------------
    case MSL_ACT_PR_SPECIAL_N_START_R:
    case MSL_ACT_PR_SPECIAL_N_START_L:
    case MSL_ACT_PR_SPECIAL_N_LOOP:
    case MSL_ACT_PR_SPECIAL_N_FULL:
      if (batch->state.on_ground[idx] == 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftPr_SpecialNStart_Phys (shared by Loop/Full): gr_vel = 0, self_vel = (facing * 1e-4,
      // 0). self_vel.x is written directly WITHOUT ftCommon_ApplyGroundMovement — the generic
      // grounded projection is gated off for these rows (puff_rollout_ground_charge_state).
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialNStart_Phys
      batch->state.speed_ground_x_self[idx] = 0.0f;
      batch->state.speed_air_x_self[idx] = pr_facing_dir(batch, idx) * 0.0001f;
      batch->state.speed_y_self[idx] = 0.0f;
      return 1u;
    case MSL_ACT_PR_SPECIAL_N_RELEASE: {
      if (batch->state.on_ground[idx] == 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftPr_SpecialNRelease_Phys: slope-influenced charge-scaled roll speed with the double
      // clamp (x4C then x50), charge decay xB4/frame, and the min-charge end (x2C < xB8).
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialNRelease_Phys
      const float dir = (float)batch->state.puff_rollout_dir[idx];
      const float nx = batch->state.ground_normal_x[idx];
      float charge = batch->state.puff_rollout_charge[idx];
      const float base = dir * (ch->puff_rollout_release_vel_scale *
                                (charge - ch->puff_rollout_charge_min_rolling));
      const float infl = ch->puff_rollout_slope_influence * (base * fabsf(nx));
      float v = (nx > 0.0f) ? base + infl : base - infl;
      float clamp = ch->puff_rollout_vel_clamp_a;
      if (fabsf(v) > clamp) {
        v = (v < 0.0f) ? -clamp : clamp;
      }
      clamp = ch->puff_rollout_vel_clamp_b;
      if (fabsf(v) > clamp) {
        v = (v < 0.0f) ? -clamp : clamp;
      }
      batch->state.speed_ground_x_self[idx] = v;
      charge -= ch->puff_rollout_charge_decay;
      batch->state.puff_rollout_charge[idx] = charge;
      if (charge < ch->puff_rollout_charge_min_rolling) {
        pr_rollout_end(batch, ch, idx, 0u);
      }
      return 1u;
    }
    case MSL_ACT_PR_SPECIAL_N_TURN: {
      if (batch->state.on_ground[idx] == 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftPr_SpecialNTurn_Phys: gr_vel += xC4 * material_mul * (x1C +/- slope influence);
      // exits back to Release once the velocity crosses zero past |x10 * xD0|, consuming the
      // facing latch into both facing and the roll direction (setFacingDir).
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::{ftPr_SpecialNTurn_Phys,
      //   setFacingDir}
      // refs/melee/src/melee/mp/mplib.c::mpLib_800569EC (material mul; MSLSTG01 segment field)
      const size_t bi = idx / (size_t)MSL_MAX_PLAYERS;
      float mul = batch->state.ground_friction_mul[idx];
      if (!(mul > 0.0f)) {
        mul = 1.0f;
      }
      const uint16_t ground_id = batch->state.ground_id[idx];
      if (ground_id != 0xFFFFu) {
        const float stage_mul =
            stage_collision_floor_ground_friction_mul(batch->state.stage_id[bi], ground_id);
        if (stage_mul > 0.0f) {
          mul = stage_mul;
        }
      }
      const float scale = ch->puff_rollout_turn_accel * mul;
      const float x1c = batch->state.puff_rollout_slope_lane[idx];
      const float nx = batch->state.ground_normal_x[idx];
      const float infl = ch->puff_rollout_slope_influence * (x1c * fabsf(nx));
      float v = batch->state.speed_ground_x_self[idx] +
                scale * ((nx > 0.0f) ? x1c + infl : x1c - infl);
      batch->state.speed_ground_x_self[idx] = v;
      const float pre = batch->state.puff_rollout_pre_turn_vel[idx];
      if (((pre > 0.0f && v < 0.0f) || (pre <= 0.0f && v > 0.0f)) &&
          fabsf(v) >= fabsf(pre * ch->puff_rollout_turn_exit_vel_ratio)) {
        pr_rollout_set_action(batch, idx, (uint16_t)MSL_ACT_PR_SPECIAL_N_RELEASE);
        const int8_t latch = batch->state.puff_rollout_facing_restore[idx];
        if (latch != 0) {
          batch->state.puff_rollout_dir[idx] = latch;
          batch->state.facing[idx] = (uint8_t)(latch > 0);
          batch->state.facing_dir1[idx] = latch;
        }
        batch->state.puff_rollout_facing_restore[idx] = 0;
      }
      return 1u;
    }
    case MSL_ACT_PR_SPECIAL_N_END_R:
    case MSL_ACT_PR_SPECIAL_N_END_L: {
      if (batch->state.on_ground[idx] == 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftPr_SpecialNEnd_Phys: plain ftCommon_ApplyFrictionGround (no high-speed multiplier);
      // ftCommon_ApplyGroundMovement scales the friction step by the floor material mul when
      // it is below 1.
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialNEnd_Phys
      // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_ApplyFrictionGround,
      //   ftCommon_ApplyGroundMovement}
      const size_t bi = idx / (size_t)MSL_MAX_PLAYERS;
      float mul = batch->state.ground_friction_mul[idx];
      if (!(mul > 0.0f)) {
        mul = 1.0f;
      }
      const uint16_t ground_id = batch->state.ground_id[idx];
      if (ground_id != 0xFFFFu) {
        const float stage_mul =
            stage_collision_floor_ground_friction_mul(batch->state.stage_id[bi], ground_id);
        if (stage_mul > 0.0f) {
          mul = stage_mul;
        }
      }
      float friction = ch->gr_friction;
      if (mul < 1.0f) {
        friction *= mul;
      }
      float v = batch->state.speed_ground_x_self[idx];
      if (v > 0.0f) {
        v = (v > friction) ? v - friction : 0.0f;
      } else if (v < 0.0f) {
        v = (v < -friction) ? v + friction : 0.0f;
      }
      batch->state.speed_ground_x_self[idx] = v;
      return 1u;
    }
    case MSL_ACT_PR_SPECIAL_AIR_N_START_R:
    case MSL_ACT_PR_SPECIAL_AIR_N_START_L:
    case MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_LOOP:
    case MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_FULL:
    case MSL_ACT_PR_SPECIAL_AIR_N_END_R:
    case MSL_ACT_PR_SPECIAL_AIR_N_END_L: {
      if (batch->state.on_ground[idx] != 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftCommon_Fall(da->x3C, da->x40): rollout gravity + terminal, no drift, no fastfall.
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c (air Phys callbacks)
      float vy = batch->state.speed_y_self[idx] - ch->puff_rollout_air_grav;
      if (vy < -ch->puff_rollout_air_terminal_vel) {
        vy = -ch->puff_rollout_air_terminal_vel;
      }
      batch->state.speed_y_self[idx] = vy;
      return 1u;
    }
    case MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE: {
      if (batch->state.on_ground[idx] != 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftPr_SpecialAirNChargeRelease_Phys: charge-scaled speed with the x58 decel toward the
      // x5C floor, rollout Fall, charge decay, min-charge end (air).
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::
      //   ftPr_SpecialAirNChargeRelease_Phys
      const float dir = (float)batch->state.puff_rollout_dir[idx];
      float charge = batch->state.puff_rollout_charge[idx];
      float v = dir * (ch->puff_rollout_release_vel_scale *
                       (charge - ch->puff_rollout_charge_min_rolling));
      v -= (v > 0.0f) ? ch->puff_rollout_air_decel : -ch->puff_rollout_air_decel;
      if (fabsf(v) < ch->puff_rollout_air_min_vel) {
        v = (v < 0.0f) ? -ch->puff_rollout_air_min_vel : ch->puff_rollout_air_min_vel;
      }
      batch->state.speed_air_x_self[idx] = v;
      float vy = batch->state.speed_y_self[idx] - ch->puff_rollout_air_grav;
      if (vy < -ch->puff_rollout_air_terminal_vel) {
        vy = -ch->puff_rollout_air_terminal_vel;
      }
      batch->state.speed_y_self[idx] = vy;
      charge -= ch->puff_rollout_charge_decay;
      batch->state.puff_rollout_charge[idx] = charge;
      if (charge < ch->puff_rollout_charge_min_rolling) {
        pr_rollout_end(batch, ch, idx, 1u);
      }
      return 1u;
    }
    case MSL_ACT_PR_SPECIAL_AIR_N_START_TURN: {
      if (batch->state.on_ground[idx] != 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftPr_SpecialAirNStartTurn_Phys: x58 decel toward the x5C floor + rollout Fall.
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialAirNStartTurn_Phys
      float v = batch->state.speed_air_x_self[idx];
      v -= (v > 0.0f) ? ch->puff_rollout_air_decel : -ch->puff_rollout_air_decel;
      if (fabsf(v) < ch->puff_rollout_air_min_vel) {
        v = (v < 0.0f) ? -ch->puff_rollout_air_min_vel : ch->puff_rollout_air_min_vel;
      }
      batch->state.speed_air_x_self[idx] = v;
      float vy = batch->state.speed_y_self[idx] - ch->puff_rollout_air_grav;
      if (vy < -ch->puff_rollout_air_terminal_vel) {
        vy = -ch->puff_rollout_air_terminal_vel;
      }
      batch->state.speed_y_self[idx] = vy;
      return 1u;
    }
    case MSL_ACT_PR_SPECIAL_N_HIT: {
      if (batch->state.on_ground[idx] != 0u) {
        return 0u;  // transient pre-swap frame
      }
      // ftPr_SpecialNHit_Phys: common drift (ftCommon_8007D268) only once vy has reached the
      // rollout terminal, then rollout Fall.
      // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialNHit_Phys
      // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_8007D268,ftCommon_8007D28C,
      //   ftCommon_8007D174}
      if (batch->state.speed_y_self[idx] <= -ch->puff_rollout_air_terminal_vel) {
        const MslCommonParams* c = msl_common_params();
        const float lsx =
            (c != NULL) ? pr_apply_deadzone(pr_stick_unit(batch->state.input_main_x[idx]),
                                            c->lstick_deadzone_x)
                        : 0.0f;
        float vel = batch->state.speed_air_x_self[idx];
        const float target = lsx * ch->air_drift_max;
        if (target == 0.0f) {
          // ftCommon_ApplyFrictionAir
          const float f = ch->aerial_friction;
          if (vel > 0.0f) {
            vel = (vel > f) ? vel - f : 0.0f;
          } else if (vel < 0.0f) {
            vel = (vel < -f) ? vel + f : 0.0f;
          }
        } else {
          float accel = lsx * ch->air_drift_stick_mul +
                        ((lsx > 0.0f) ? ch->aerial_drift_base : -ch->aerial_drift_base);
          if (!(vel * accel < 0.0f)) {
            if (accel > 0.0f) {
              if (vel + accel > target) {
                accel = -ch->aerial_friction;
                if (vel + accel < target) {
                  accel = target - vel;
                }
                if (vel + accel > ch->air_max_horizontal_velocity) {
                  accel = ch->air_max_horizontal_velocity - vel;
                }
              }
            } else if (vel + accel < target) {
              accel = ch->aerial_friction;
              if (vel + accel > target) {
                accel = target - vel;
              }
              if (vel + accel < -ch->air_max_horizontal_velocity) {
                accel = -ch->air_max_horizontal_velocity - vel;
              }
            }
          }
          vel += accel;
        }
        batch->state.speed_air_x_self[idx] = vel;
      }
      float vy = batch->state.speed_y_self[idx] - ch->puff_rollout_air_grav;
      if (vy < -ch->puff_rollout_air_terminal_vel) {
        vy = -ch->puff_rollout_air_terminal_vel;
      }
      batch->state.speed_y_self[idx] = vy;
      return 1u;
    }
    default:
      return 0u;
  }
}

void puff_rollout_on_deal_dmg(MslBatch* batch, size_t a_idx) {
  // ftPr_SpecialS_8013D764 (deal_dmg_cb, live in Release/Turn/AirChargeRelease/AirStartTurn):
  // budget x0 -= x38, facing = x34.x, remove hitboxes -> SpecialNHit at the preserved frame
  // (rate 0). Ground: self_vel.x = gr_vel * specialn_vel.x + ftCommon_8007D5D4 (airborne,
  // gr_vel = 0, one air jump consumed); air: self_vel.x *= specialn_vel.x. Always
  // self_vel.y = specialn_vel.y; the facing latch clears. The action gate makes a second call
  // in the same combat pass a no-op (NHit is not in the family).
  // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialS_8013D764
  if (batch == NULL || batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_PUFF) {
    return;
  }
  const uint16_t a = batch->state.action_id[a_idx];
  if (a != (uint16_t)MSL_ACT_PR_SPECIAL_N_RELEASE && a != (uint16_t)MSL_ACT_PR_SPECIAL_N_TURN &&
      a != (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE &&
      a != (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_TURN) {
    return;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[a_idx]);
  if (ch == NULL) {
    return;
  }
  batch->state.puff_rollout_turn_budget[a_idx] =
      (int16_t)(batch->state.puff_rollout_turn_budget[a_idx] -
                (int16_t)ch->puff_rollout_turn_budget_hit_cost);
  const int8_t dir = batch->state.puff_rollout_dir[a_idx];
  batch->state.facing[a_idx] = (uint8_t)(dir > 0);
  batch->state.facing_dir1[a_idx] = dir;
  pr_rollout_set_action(batch, a_idx, (uint16_t)MSL_ACT_PR_SPECIAL_N_HIT);
  msl_anim_timebase_enter(batch, a_idx, 0.0f, 0.0f);
  if (batch->state.on_ground[a_idx] != 0u) {
    batch->state.speed_air_x_self[a_idx] =
        batch->state.speed_ground_x_self[a_idx] * ch->puff_rollout_hit_vel_x_mul;
    // ftCommon_8007D5D4 airborne flip (jumpsUsed = 1; replay-confirmed jumps_left drops by
    // one on the hit row). ground_id stays: the CollData floor owner persists in the lanes.
    batch->state.on_ground[a_idx] = 0u;
    if (ch->max_jumps > 0u && batch->state.jumps_left[a_idx] > 0u) {
      batch->state.jumps_left[a_idx] = (uint8_t)(ch->max_jumps - 1u);
    }
  } else {
    batch->state.speed_air_x_self[a_idx] *= ch->puff_rollout_hit_vel_x_mul;
  }
  batch->state.speed_ground_x_self[a_idx] = 0.0f;
  batch->state.speed_y_self[a_idx] = ch->puff_rollout_hit_vel_y;
  batch->state.puff_rollout_facing_restore[a_idx] = 0;
}

void puff_rollout_hitbox_speed_damage_refresh(MslBatch* batch) {
  // ftPr_SpecialS_8013D8E4, run every Release/AirChargeRelease frame after the script hitbox
  // refresh: below da->xCC the roll hitboxes disable (and re-enable above it); otherwise the
  // per-frame damage override is (s32)(x84 * (x80 + |vel|)), min 1. Hitlag frames recompute the
  // same values from the frozen velocity, so no hitlag gate is needed.
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF ||
          batch->state.stocks[idx] == 0u) {
        continue;
      }
      const uint16_t a = batch->state.action_id[idx];
      if (a != (uint16_t)MSL_ACT_PR_SPECIAL_N_RELEASE &&
          a != (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE) {
        continue;
      }
      const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }
      const float speed = batch->state.on_ground[idx]
                              ? fabsf(batch->state.speed_ground_x_self[idx])
                              : fabsf(batch->state.speed_air_x_self[idx]);
      const uint8_t count = batch->state.hitbox_count[idx];
      const size_t base = idx * (size_t)MSL_MAX_HITBOXES;
      // The grounded Release script creates three capsules but sets x42_b5 = 0 (no fighter
      // interaction) on ids 1 and 2 via set_hitbox_interaction; only hb0 hits fighters. The
      // event kind is not carried by the MSLHITB1 artifact, so gate them here (approximation:
      // their item interaction is dropped too).
      // data/moves/puff.json::specials_by_msid.304 set_hitbox_interaction(idx=1/2, type=0, 0)
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80071708
      // refs/melee/src/melee/ft/ftcoll.c (x42_b5 gate in fighter hit checks)
      if (a == (uint16_t)MSL_ACT_PR_SPECIAL_N_RELEASE) {
        for (uint8_t i = 1; i < count && i < 3u; i++) {
          batch->state.hitbox_enabled[base + i] = 0u;
        }
      }
      if (speed < ch->puff_rollout_min_damage_speed) {
        for (uint8_t i = 0; i < count; i++) {
          batch->state.hitbox_enabled[base + i] = 0u;
        }
      } else {
        int dmg = (int)(ch->puff_rollout_damage_scale * (ch->puff_rollout_damage_base + speed));
        if (dmg < 1) {
          dmg = 1;
        }
        for (uint8_t i = 0; i < count; i++) {
          if (batch->state.hitbox_enabled[base + i] != 0u) {
            batch->state.hitbox_damage[base + i] = (float)dmg;
          }
        }
      }
    }
  }
}

uint8_t puff_rollout_try_wall_bounce(MslBatch* batch, size_t idx) {
  // ftPr_SpecialNRelease_Coll / ftPr_SpecialAirNChargeRelease_Coll wall hit: rolling right
  // (x34.x == 1) checks the left-facing wall bits (0x3F: the wall on the fighter's right),
  // rolling left checks 0xFC0. On hit: charge *= xD4 (clamped >= 0), velocity reverses scaled
  // by xD4; the ground variant re-derives the roll direction from the reversed gr_vel while
  // the air variant flips it. (The x18 |speed| lane is derived from the velocity here.)
  // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::{ftPr_SpecialNRelease_Coll,
  //   ftPr_SpecialAirNChargeRelease_Coll}
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (a != (uint16_t)MSL_ACT_PR_SPECIAL_N_RELEASE &&
      a != (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return 0u;
  }
  const int8_t dir = batch->state.puff_rollout_dir[idx];
  const uint32_t mask = (dir == 1) ? (uint32_t)MSL_COLLIDE_LEFT_WALL_MASK
                                   : (uint32_t)MSL_COLLIDE_RIGHT_WALL_MASK;
  if ((batch->state.coll_env_flags[idx] & mask) == 0u) {
    return 0u;
  }
  const float decay = ch->puff_rollout_wall_bounce_decay;
  float charge = batch->state.puff_rollout_charge[idx] * decay;
  if (charge < 0.0f) {
    charge = 0.0f;
  }
  batch->state.puff_rollout_charge[idx] = charge;
  if (a == (uint16_t)MSL_ACT_PR_SPECIAL_N_RELEASE) {
    const float gr = -batch->state.speed_ground_x_self[idx] * decay;
    batch->state.speed_ground_x_self[idx] = gr;
    batch->state.speed_air_x_self[idx] = -batch->state.speed_air_x_self[idx] * decay;
    batch->state.puff_rollout_dir[idx] = (gr >= 0.0f) ? 1 : -1;  // SIGNF(gr_vel)
  } else {
    batch->state.speed_air_x_self[idx] = -batch->state.speed_air_x_self[idx] * decay;
    batch->state.puff_rollout_dir[idx] = (int8_t)-dir;
  }
  return 1u;
}

uint8_t puff_rollout_try_floor_loss_swap(MslBatch* batch, size_t idx) {
  // Grounded rollout floor loss -> air variant at the preserved frame. ftCommon_8007D5D4
  // zeroes gr_vel WITHOUT transferring it into self_vel (the air Phys recomputes the roll
  // speed from charge); grounded End additionally runs the ftPr_SpecialS_8013DA24(air) tail
  // on the projection-synced self velocity.
  // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c (grounded *_Coll floor-loss paths)
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return 0u;
  }
  uint16_t dest = 0u;
  switch (a) {
    case MSL_ACT_PR_SPECIAL_N_START_R:
      dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_R;
      break;
    case MSL_ACT_PR_SPECIAL_N_START_L:
      dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_L;
      break;
    case MSL_ACT_PR_SPECIAL_N_LOOP:
      dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_LOOP;
      break;
    case MSL_ACT_PR_SPECIAL_N_FULL:
      dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_FULL;
      break;
    case MSL_ACT_PR_SPECIAL_N_RELEASE:
      // ftPr_SpecialNRelease_Coll floor loss also re-seeds the x1C lane with the air value.
      dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE;
      batch->state.puff_rollout_slope_lane[idx] = ch->puff_rollout_slope_lane_air;
      break;
    case MSL_ACT_PR_SPECIAL_N_TURN:
      dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_TURN;
      break;
    case MSL_ACT_PR_SPECIAL_N_END_R:
    case MSL_ACT_PR_SPECIAL_N_END_L:
      // ftPr_SpecialNEnd_Coll -> 8013DA24(air) at the preserved frame: self_vel.x *= x90,
      // self_vel.y *= x94 (self velocity was projection-synced from gr_vel while grounded).
      dest = (a == (uint16_t)MSL_ACT_PR_SPECIAL_N_END_R)
                 ? (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_END_R
                 : (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_END_L;
      batch->state.speed_air_x_self[idx] *= ch->puff_rollout_end_vel_x_mul;
      batch->state.speed_y_self[idx] *= ch->puff_rollout_end_vel_y_mul;
      break;
    default:
      return 0u;
  }
  pr_rollout_set_action(batch, idx, dest);
  batch->state.speed_ground_x_self[idx] = 0.0f;
  return 1u;
}

uint8_t puff_rollout_air_release_land_or_bounce(MslBatch* batch, size_t idx) {
  // ftPr_SpecialAirNChargeRelease_Coll ground contact. vy' = |vy * x78|:
  // - vy' < x7C: grounded Release at the preserved frame with gr_vel = self_vel.x = x18 *
  //   x34.x, vy = 0, x1C = x44 (return 1; the caller applies the grounding bundle).
  // - else BOUNCE: vy = +vy' with an optional stick re-aim beyond x68; the fighter stays
  //   airborne on the collision-corrected root (return 2). Unreachable from the rollout fall
  //   itself (terminal x40 * x78 < x7C) but live for external vertical velocity.
  // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialAirNChargeRelease_Coll
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  const MslCommonParams* c = msl_common_params();
  if (ch == NULL || c == NULL) {
    return 0u;
  }
  const float vy_land = fabsf(batch->state.speed_y_self[idx] * ch->puff_rollout_landing_vy_scale);
  if (vy_land < ch->puff_rollout_bounce_vy_threshold) {
    const float dir = (float)batch->state.puff_rollout_dir[idx];
    pr_rollout_set_action(batch, idx, (uint16_t)MSL_ACT_PR_SPECIAL_N_RELEASE);
    batch->state.speed_air_x_self[idx] = fabsf(batch->state.speed_air_x_self[idx]) * dir;
    batch->state.speed_y_self[idx] = 0.0f;
    batch->state.puff_rollout_slope_lane[idx] = ch->puff_rollout_slope_lane_ground;
    return 1u;
  }
  batch->state.speed_y_self[idx] = vy_land;
  const float sx =
      pr_apply_deadzone(pr_stick_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  if (fabsf(sx) > ch->puff_rollout_turn_stick_threshold) {
    const float sdir = (sx >= 0.0f) ? 1.0f : -1.0f;
    batch->state.puff_rollout_dir[idx] = (int8_t)sdir;
    batch->state.speed_air_x_self[idx] = fabsf(batch->state.speed_air_x_self[idx]) * sdir;
  }
  return 2u;
}

uint8_t puff_rollout_turn_coll_edge_snap(const MslBatch* batch, size_t idx) {
  // |gr_vel| <= x74 selects the ft_80082978 wrapper whose floor-miss branch runs
  // mpColl_8004A45C_Floor (endpoint snap, stays grounded); above it ft_80082888's flags=0
  // branch loses the floor and the Coll swaps to SpecialAirNStartTurn.
  // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialNTurn_Coll
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_8004B21C,mpColl_8004B3F0,mpColl_8004A45C_Floor}
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_PR_SPECIAL_N_TURN) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return 0u;
  }
  return (uint8_t)(fabsf(batch->state.speed_ground_x_self[idx]) <=
                   ch->puff_rollout_turn_coll_vel_threshold);
}

void puff_rollout_on_cliff_catch(MslBatch* batch, size_t idx) {
  // ftPr_SpecialAirNChargeRelease_Coll's catch inline: consume the mv facing latch, then the
  // second ftCliffCommon_80081370 call re-asserts stage-side facing, so the generic catch's
  // facing stands; only the latch consumption is observable (scale/rot resets are cosmetic).
  // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialAirNChargeRelease_Coll
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF) {
    return;
  }
  batch->state.puff_rollout_facing_restore[idx] = 0;
}

// Ground <-> air phase flips preserve the animation frame (13D590/13D5F0).
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialS.c::{ftPr_SpecialS_8013D590,
//   ftPr_SpecialS_8013D5F0}
uint8_t puff_special_try_ground_to_air_swap(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  uint16_t dest = 0u;
  if (a == (uint16_t)MSL_ACT_PR_SPECIAL_S) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_S;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_LW_L) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_LW_L;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_LW_R) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_LW_R;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_HI_L) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_HI_L;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_HI_R) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_HI_R;
  } else {
    return 0u;
  }
  batch->state.action_id[idx] = dest;
  batch->state.animation_index[idx] = (uint32_t)puff_special_submotion(dest);
  return 1u;
}

uint8_t puff_special_try_air_to_ground_swap(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  uint16_t dest = 0u;
  if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_S) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_S;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_LW_L) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_LW_L;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_LW_R) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_LW_R;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_HI_L) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_HI_L;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_HI_R) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_HI_R;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_R) {
    // Rollout air-to-ground landings (7D7FC bundle is caller-owned). AirChargeRelease is NOT
    // here — its landing/bounce fork (puff_rollout_air_release_land_or_bounce) has its own
    // locomotion branch.
    // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c (air *_Coll landing paths)
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_N_START_R;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_L) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_N_START_L;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_LOOP) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_N_LOOP;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_FULL) {
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_N_FULL;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_TURN) {
    // ftPr_SpecialAirNStartTurn_Coll: gr_vel = self_vel.x (caller bundle), vy/z zeroed.
    dest = (uint16_t)MSL_ACT_PR_SPECIAL_N_TURN;
    batch->state.speed_y_self[idx] = 0.0f;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_END_R ||
             a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_END_L) {
    // ftPr_SpecialAirNEnd_Coll -> 8013DA24(ground) at the preserved frame: gr_vel *= x90
    // (applied on the air lane before the caller's ground = air copy), vy = 0.
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    if (ch != NULL) {
      batch->state.speed_air_x_self[idx] *= ch->puff_rollout_end_vel_x_mul;
    }
    batch->state.speed_y_self[idx] = 0.0f;
    dest = (a == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_END_R) ? (uint16_t)MSL_ACT_PR_SPECIAL_N_END_R
                                                           : (uint16_t)MSL_ACT_PR_SPECIAL_N_END_L;
  } else if (a == (uint16_t)MSL_ACT_PR_SPECIAL_N_HIT) {
    // ftPr_SpecialNHit_Coll landing: facing restore (latch consume) then xD8 == 0 ? normal
    // landing : ftCo_LandingFallSpecial_Enter with lag xD8 (falcon Raptor Boost shape: fixed
    // submotion rate (end + 0.1) / lag).
    // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c::ftPr_SpecialNHit_Coll
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    if (ch == NULL) {
      return 0u;
    }
    const int8_t latch = batch->state.puff_rollout_facing_restore[idx];
    if (latch != 0) {
      batch->state.facing[idx] = (uint8_t)(latch > 0);
      batch->state.facing_dir1[idx] = latch;
    }
    batch->state.puff_rollout_facing_restore[idx] = 0;
    if (ch->puff_rollout_landing_lag > 0.0f) {
      const float lag = ch->puff_rollout_landing_lag;
      batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
      batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING_FALL_SPECIAL;
      const float ef =
          msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_LANDING_FALL_SPECIAL);
      msl_anim_timebase_enter(batch, idx, 0.0f,
                              (lag > 0.0f && ef > 0.0f) ? ((ef + 0.1f) / lag) : 1.0f);
      batch->state.fallspecial_landing_lag[idx] = lag;
      batch->state.landing_fallspecial_allow_interrupt[idx] = 0u;
      return 1u;
    }
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    return 1u;
  } else {
    return 0u;
  }
  batch->state.action_id[idx] = dest;
  batch->state.animation_index[idx] = (uint32_t)puff_special_submotion(dest);
  return 1u;
}

void puff_specials_reseed_init(MslBatch* batch, int batch_index) {
  if (batch == NULL || batch_index < 0 || batch_index >= batch->batch_size) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(batch_index, p);
    if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF) {
      continue;
    }
    // Pound's cmd_vars[0] impulse pulse is consume-once (the phys zeroes it): reconstruct the
    // latch from whether the script lane already reads latched at the seeded frame (same model
    // as falcon_specials_reseed_init).
    // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialS.c::ftPr_SpecialAirS_Phys
    if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_PR_SPECIAL_AIR_S) {
      batch->state.special_cmd0[idx] = move_tables_special_cmd_var_value_at_frame(
          batch->state.char_id[idx],
          puff_special_submotion((uint16_t)MSL_ACT_PR_SPECIAL_AIR_S), 0u,
          msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]));
    }
    // The turnaround window counter (mv.co.jumpaerial.x0) is hidden per-action state the replay
    // does not carry. Reseed starts with no armed window: a reversed-jump seed row inside the
    // ~turn_frames entry window will miss the pending mid-window facing flip (documented
    // approximation; the flip itself is replay-visible one row later and self-corrects).
    // Reconstructing the ladder from the CURRENT stick was tried and measured worse on the
    // specials suite: entry-time arming is not derivable from the seeded row (released-stick
    // armed windows miss, and post-entry stick reversals false-arm — 8 net new facing rows).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D74A4
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ft_800CB6EC
    batch->state.puff_mjump_turn_timer[idx] = 0u;

    // ---- Rollout lane reconstruction --------------------------------------
    // The whole roll family runs with the anim frozen at frame 0 (state_age carries nothing),
    // so the hidden lanes reconstruct from the seeded velocity where the Phys pins it to the
    // charge formula, and fall back to documented approximations elsewhere:
    // - charge x2C: Release rows invert v = dir * xC0 * (x2C - xB8) * (1 +/- xC8|nx|); air
    //   rows add back the x58 decel. Full rows are exactly xA4; Loop rows are hidden (the
    //   elapsed hold does not reach the replay) and reseed at the xA0 entry value.
    // - turn budget x0: hidden mid-roll; reseeds at the da->x34 init, so a seeded row cannot
    //   predict the natural end-of-roll row (documented shared debt, ~1 row per full rollout).
    // - roll angle x14: only gates the end once the budget is spent; with the budget reseeded
    //   at init it stays inert, reseed 0.
    // - x10/x1C (Turn): pre-turn vel approximates from the seeded gr_vel — exact before the
    //   zero crossing, and dir * 4x the crossed velocity after it (the true value is >= 4x by
    //   the un-met x10 * xD0 exit test), which fires the exit within ~a frame of source.
    // refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialN.c
    const uint16_t a = batch->state.action_id[idx];
    if (puff_action_is_rollout(batch->state.char_id[idx], a)) {
      const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }
      const int8_t facing_dir1 = batch->state.facing[idx] ? 1 : -1;
      int8_t dir = facing_dir1;
      int8_t latch = 0;
      float charge = ch->puff_rollout_charge_init;
      float pre_turn = 0.0f;
      float slope_lane = batch->state.on_ground[idx] ? ch->puff_rollout_slope_lane_ground
                                                     : ch->puff_rollout_slope_lane_air;
      switch (a) {
        case MSL_ACT_PR_SPECIAL_N_START_R:
        case MSL_ACT_PR_SPECIAL_AIR_N_START_R:
          dir = 1;
          break;
        case MSL_ACT_PR_SPECIAL_N_START_L:
        case MSL_ACT_PR_SPECIAL_AIR_N_START_L:
          dir = -1;
          break;
        case MSL_ACT_PR_SPECIAL_N_FULL:
        case MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_FULL:
          charge = ch->puff_rollout_charge_max;
          break;
        case MSL_ACT_PR_SPECIAL_N_RELEASE: {
          const float v = batch->state.speed_ground_x_self[idx];
          if (v != 0.0f) {
            dir = (v > 0.0f) ? 1 : -1;
          }
          // v = base * f with f = 1 +/- xC8 * |nx| (sign by the floor normal side; the
          // dir-signed influence term factors out of |v|).
          const float nx = batch->state.ground_normal_x[idx];
          float f =
              1.0f + ((nx > 0.0f) ? 1.0f : -1.0f) * ch->puff_rollout_slope_influence * fabsf(nx);
          if (!(f > 0.0f)) {
            f = 1.0f;
          }
          if (ch->puff_rollout_release_vel_scale > 0.0f) {
            if (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_PR_SPECIAL_N_TURN) {
              // Turn-exit row: the seeded velocity is the exit-threshold value (~x10 * xD0),
              // not the charge formula; the next Phys snaps back to the charge speed. Invert
              // the exit threshold instead (|x10| ~= |v| / xD0).
              const float pre_abs = (ch->puff_rollout_turn_exit_vel_ratio > 0.0f)
                                        ? fabsf(v) / ch->puff_rollout_turn_exit_vel_ratio
                                        : fabsf(v);
              charge = pre_abs / (ch->puff_rollout_release_vel_scale * f) +
                       ch->puff_rollout_charge_min_rolling;
            } else {
              charge = fabsf(v) / (ch->puff_rollout_release_vel_scale * f) +
                       ch->puff_rollout_charge_min_rolling;
            }
          }
          break;
        }
        case MSL_ACT_PR_SPECIAL_AIR_N_CHARGE_RELEASE: {
          const float v = batch->state.speed_air_x_self[idx];
          if (v != 0.0f) {
            dir = (v > 0.0f) ? 1 : -1;
          }
          if (ch->puff_rollout_release_vel_scale > 0.0f) {
            charge = (fabsf(v) + ch->puff_rollout_air_decel) / ch->puff_rollout_release_vel_scale +
                     ch->puff_rollout_charge_min_rolling;
          }
          break;
        }
        case MSL_ACT_PR_SPECIAL_N_TURN:
        case MSL_ACT_PR_SPECIAL_AIR_N_START_TURN: {
          // dir stays the pre-turn roll direction (== facing until the exit's setFacingDir);
          // the latch carries the post-turn facing. The true x10 is hidden; before the zero
          // crossing pre = v underestimates it proportionally, which keeps the decel shape and
          // (since the step scales with pre) can never flip the sign in one step — the exit
          // stays un-predicted, matching the ref on every row except the true exit row. After
          // the crossing, pre = 8 * |v| keeps |v'| below the xD0 exit threshold for the same
          // one-miss behavior (any single-row estimate either always or never exits).
          latch = (int8_t)-dir;
          const float v = batch->state.speed_ground_x_self[idx];
          if ((v > 0.0f) == (dir > 0) && v != 0.0f) {
            pre_turn = v;
          } else {
            pre_turn = (float)dir * (8.0f * fabsf(v));
          }
          slope_lane = -0.05f * pre_turn;
          if (ch->puff_rollout_release_vel_scale > 0.0f) {
            charge = fabsf(pre_turn) / ch->puff_rollout_release_vel_scale +
                     ch->puff_rollout_charge_min_rolling;
          }
          break;
        }
        default:
          break;
      }
      if (charge > ch->puff_rollout_charge_max) {
        charge = ch->puff_rollout_charge_max;
      }
      if (charge < 0.0f) {
        charge = 0.0f;
      }
      batch->state.puff_rollout_dir[idx] = dir;
      batch->state.puff_rollout_facing_restore[idx] = latch;
      batch->state.puff_rollout_charge[idx] = charge;
      batch->state.puff_rollout_turn_budget[idx] = (int16_t)ch->puff_rollout_turn_budget_frames;
      batch->state.puff_rollout_angle[idx] = 0.0f;
      batch->state.puff_rollout_pre_turn_vel[idx] = pre_turn;
      batch->state.puff_rollout_slope_lane[idx] = slope_lane;
      // The charge/roll family holds its anim frozen (every internal ChangeMotionState passes
      // rate 0). Replay-derived frame_speed lanes cannot see that on state-entry rows (the
      // builder defaults them to 1), so force the frozen rate here; Start/End animate at 1.
      if (a != (uint16_t)MSL_ACT_PR_SPECIAL_N_START_R &&
          a != (uint16_t)MSL_ACT_PR_SPECIAL_N_START_L &&
          a != (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_R &&
          a != (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_START_L &&
          a != (uint16_t)MSL_ACT_PR_SPECIAL_N_END_R && a != (uint16_t)MSL_ACT_PR_SPECIAL_N_END_L &&
          a != (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_END_R &&
          a != (uint16_t)MSL_ACT_PR_SPECIAL_AIR_N_END_L) {
        batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
      }
    }
  }
}
