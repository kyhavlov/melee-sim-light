#include "marth_specials.h"

#include <math.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "batch_internal.h"
#include "buttons.h"
#include "char_params.h"
#include "char_registry.h"
#include "common_params.h"
#include "ids.h"
#include "locomotion.h"
#include "move_tables.h"
#include "msl_math.h"
#include "state_flags.h"

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static inline float ms_stick_unit(int8_t v) { return (float)v * (1.0f / 80.0f); }

static inline float ms_apply_deadzone(float v, float dz) { return (fabsf(v) < dz) ? 0.0f : v; }

static inline uint8_t ms_anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  return (end > 0.0f && msl_anim_frame_sanitize_f32(anim_frame_f32) >= end) ? 1u : 0u;
}

static inline void ms_enter(MslBatch* batch, size_t idx, uint16_t action_id, float start_frame) {
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] = (uint32_t)marth_special_submotion(action_id);
  msl_anim_timebase_enter(batch, idx, start_frame, 1.0f);
}

static inline void ms_reset_cmds(MslBatch* batch, size_t idx) {
  batch->state.special_cmd0[idx] = 0u;
  batch->state.special_cmd1[idx] = 0u;
  batch->state.special_cmd2[idx] = 0u;
}

static inline float ms_facing_dir(const MslBatch* batch, size_t idx) {
  return batch->state.facing[idx] ? 1.0f : -1.0f;
}

// ---------------------------------------------------------------------------
// Entries (decomp: ftMs_Special*_Enter)
// ---------------------------------------------------------------------------

static void ms_enter_specialn(MslBatch* batch, const MslCharParams* ch, size_t idx,
                              uint8_t on_ground) {
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialN.c::{ftMs_SpecialN_Enter,
  //   ftMs_SpecialAirN_Enter}
  if (ch->specialn_entry_vel_divisor > 0.0f) {
    if (on_ground) {
      batch->state.speed_ground_x_self[idx] /= ch->specialn_entry_vel_divisor;
    } else {
      batch->state.speed_air_x_self[idx] /= ch->specialn_entry_vel_divisor;
      if (batch->state.speed_y_self[idx] <= 0.0f) {
        batch->state.speed_y_self[idx] = 0.0f;
      }
    }
  }
  ms_reset_cmds(batch, idx);
  batch->state.specialn_charge_frames[idx] = 0u;
  ms_enter(
      batch, idx,
      on_ground ? (uint16_t)MSL_ACT_MS_SPECIAL_N_START : (uint16_t)MSL_ACT_MS_SPECIAL_AIR_N_START,
      0.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void ms_enter_specials(MslBatch* batch, const MslCharParams* ch, size_t idx,
                              uint8_t on_ground) {
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialS.c::{ftMs_SpecialS_Enter,
  //   ftMs_SpecialAirS_Enter}
  if (on_ground) {
    batch->state.speed_y_self[idx] = 0.0f;
  } else {
    if (ch->specials_air_entry_vel_x_divisor > 0.0f) {
      batch->state.speed_air_x_self[idx] /= ch->specials_air_entry_vel_x_divisor;
    }
    if (batch->state.specials_air_used[idx] == 0u) {
      // First air side-special of this airtime gets the vertical hop (fv.ms.x222C gate).
      batch->state.specials_air_used[idx] = 1u;
      batch->state.speed_y_self[idx] = ch->specials_air_entry_vel_y;
    } else {
      batch->state.speed_y_self[idx] = 0.0f;
    }
  }
  ms_reset_cmds(batch, idx);
  ms_enter(batch, idx,
           on_ground ? (uint16_t)MSL_ACT_MS_SPECIAL_S1 : (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S1, 0.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void ms_enter_specialhi(MslBatch* batch, const MslCharParams* ch, size_t idx,
                               uint8_t on_ground) {
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::{ftMs_SpecialHi_Enter,
  //   ftMs_SpecialAirHi_Enter}
  ms_reset_cmds(batch, idx);
  batch->state.special_stick_angle[idx] = 0.0f;
  if (!on_ground) {
    batch->state.speed_y_self[idx] = 0.0f;
    batch->state.speed_air_x_self[idx] *= ch->specialhi_air_entry_vel_x_mul;
  }
  ms_enter(batch, idx,
           on_ground ? (uint16_t)MSL_ACT_MS_SPECIAL_HI : (uint16_t)MSL_ACT_MS_SPECIAL_AIR_HI, 0.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void ms_enter_speciallw(MslBatch* batch, const MslCharParams* ch, size_t idx,
                               uint8_t on_ground) {
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{ftMs_SpecialLw_Enter,
  //   ftMs_SpecialAirLw_Enter}
  if (on_ground) {
    batch->state.speed_y_self[idx] = 0.0f;
  } else {
    if (ch->speciallw_air_entry_vel_x_divisor > 0.0f) {
      batch->state.speed_air_x_self[idx] /= ch->speciallw_air_entry_vel_x_divisor;
    }
    batch->state.speed_y_self[idx] = 0.0f;
  }
  ms_reset_cmds(batch, idx);
  batch->state.speciallw_countered_damage[idx] = 0u;
  batch->state.speciallw_counter_window[idx] = 0u;
  ms_enter(batch, idx,
           on_ground ? (uint16_t)MSL_ACT_MS_SPECIAL_LW : (uint16_t)MSL_ACT_MS_SPECIAL_AIR_LW, 0.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

// ---------------------------------------------------------------------------
// Dancing Blade chaining (decomp: ftMs_SpecialS.c stage advance fns)
// ---------------------------------------------------------------------------

// Stage-advance target for the NEXT stage from the current action + stick.
// Stage 2: up -> S2Hi else S2Lw (ftMs_SpecialS_80137A9C: no mid variant).
// Stages 3/4: up -> Hi, down -> Lw, else S (ftMs_SpecialS_80137E0C / 80138148).
static uint16_t ms_db_next_action(const MslBatch* batch, const MslCommonParams* c, size_t idx,
                                  uint16_t a) {
  const float stick_y = ms_stick_unit(batch->state.input_main_y[idx]);
  const float up_thresh = c->special_stick_y_threshold;
  const uint8_t air = (a >= (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S1) ? 1u : 0u;
  switch (a) {
    case MSL_ACT_MS_SPECIAL_S1:
    case MSL_ACT_MS_SPECIAL_AIR_S1:
      if (stick_y > up_thresh) {
        return air ? (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S2_HI : (uint16_t)MSL_ACT_MS_SPECIAL_S2_HI;
      }
      return air ? (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S2_LW : (uint16_t)MSL_ACT_MS_SPECIAL_S2_LW;
    case MSL_ACT_MS_SPECIAL_S2_HI:
    case MSL_ACT_MS_SPECIAL_S2_LW:
    case MSL_ACT_MS_SPECIAL_AIR_S2_HI:
    case MSL_ACT_MS_SPECIAL_AIR_S2_LW:
      if (stick_y > up_thresh) {
        return air ? (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S3_HI : (uint16_t)MSL_ACT_MS_SPECIAL_S3_HI;
      }
      if (stick_y < -up_thresh) {
        return air ? (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S3_LW : (uint16_t)MSL_ACT_MS_SPECIAL_S3_LW;
      }
      return air ? (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S3_S : (uint16_t)MSL_ACT_MS_SPECIAL_S3_S;
    case MSL_ACT_MS_SPECIAL_S3_HI:
    case MSL_ACT_MS_SPECIAL_S3_S:
    case MSL_ACT_MS_SPECIAL_S3_LW:
    case MSL_ACT_MS_SPECIAL_AIR_S3_HI:
    case MSL_ACT_MS_SPECIAL_AIR_S3_S:
    case MSL_ACT_MS_SPECIAL_AIR_S3_LW:
      if (stick_y > up_thresh) {
        return air ? (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S4_HI : (uint16_t)MSL_ACT_MS_SPECIAL_S4_HI;
      }
      if (stick_y < -up_thresh) {
        return air ? (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S4_LW : (uint16_t)MSL_ACT_MS_SPECIAL_S4_LW;
      }
      return air ? (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S4_S : (uint16_t)MSL_ACT_MS_SPECIAL_S4_S;
    default:
      return 0u;
  }
}

static inline uint8_t ms_db_is_stage(uint16_t a) {
  return (uint8_t)((a >= (uint16_t)MSL_ACT_MS_SPECIAL_S1 &&
                    a <= (uint16_t)MSL_ACT_MS_SPECIAL_AIR_S4_LW));
}

static inline uint8_t ms_db_is_final_stage(uint16_t a) {
  switch (a) {
    case MSL_ACT_MS_SPECIAL_S4_HI:
    case MSL_ACT_MS_SPECIAL_S4_S:
    case MSL_ACT_MS_SPECIAL_S4_LW:
    case MSL_ACT_MS_SPECIAL_AIR_S4_HI:
    case MSL_ACT_MS_SPECIAL_AIR_S4_S:
    case MSL_ACT_MS_SPECIAL_AIR_S4_LW:
      return 1u;
    default:
      return 0u;
  }
}

// ---------------------------------------------------------------------------
// Anim-end exits
// ---------------------------------------------------------------------------

static void ms_exit_to_wait_or_fall(MslBatch* batch, size_t idx) {
  if (batch->state.on_ground[idx]) {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
  } else {
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_FALL;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_FALL;
  }
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static void ms_enter_fall_special_from_specialhi(MslBatch* batch, const MslCharParams* ch,
                                                 size_t idx) {
  // Decomp: ftMs_Special(Air)Hi_Anim end -> ftCo_80096900(gobj, 0, 1, 0, x28, x2C):
  // FallSpecial with custom freefall mobility and LandingFallSpecial lag.
  msl_locomotion_enter_fall_special_via_ftco_80096900(batch, idx, ch->specialhi_landing_lag_frames,
                                                      /*allow_interrupt=*/0u);
  batch->state.fallspecial_mobility_mul[idx] = ch->specialhi_freefall_mobility_mul;
}

// ---------------------------------------------------------------------------
// Per-action update (anim/IASA/transitions); runs in the action phase
// ---------------------------------------------------------------------------

static void ms_update_player(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                             size_t idx) {
  const uint16_t a = batch->state.action_id[idx];
  const uint8_t cid = batch->state.char_id[idx];
  const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  const uint16_t msid = marth_special_submotion(a);
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  const uint16_t held = batch->state.input_buttons[idx];
  enum { AB = (uint16_t)(MSL_BUTTON_A | MSL_BUTTON_B) };

  switch (a) {
    // ---- Shield Breaker -------------------------------------------------
    case MSL_ACT_MS_SPECIAL_N_START:
    case MSL_ACT_MS_SPECIAL_AIR_N_START:
      if (ms_anim_finished(cid, msid, frame)) {
        // doStartAnim -> Loop at frame 0 (ftMs_SpecialN_80136E74/EAC).
        ms_enter(batch, idx,
                 (a == (uint16_t)MSL_ACT_MS_SPECIAL_N_START)
                     ? (uint16_t)MSL_ACT_MS_SPECIAL_N_LOOP
                     : (uint16_t)MSL_ACT_MS_SPECIAL_AIR_N_LOOP,
                 0.0f);
      }
      break;
    case MSL_ACT_MS_SPECIAL_N_LOOP:
    case MSL_ACT_MS_SPECIAL_AIR_N_LOOP: {
      const uint8_t grounded_family = (a == (uint16_t)MSL_ACT_MS_SPECIAL_N_LOOP) ? 1u : 0u;
      // doLoopAnim: cur_frame++ each held frame; force the full-charge release past max.
      uint16_t cf = batch->state.specialn_charge_frames[idx];
      if (cf < 0xFFFEu) {
        cf++;
      }
      batch->state.specialn_charge_frames[idx] = cf;
      const int32_t max_frames = ch->specialn_charge_max_seconds * 30;
      if (max_frames > 0 && (int32_t)cf > max_frames) {
        // Full charge: cmd0=1 selects the End1 (shield-breaker) release anim.
        batch->state.special_cmd0[idx] = 1u;
        ms_enter(batch, idx,
                 grounded_family ? (uint16_t)MSL_ACT_MS_SPECIAL_N_END1
                                 : (uint16_t)MSL_ACT_MS_SPECIAL_AIR_N_END1,
                 0.0f);
        break;
      }
      // doLoopIasa: release on B let go -> End0 with charge-scaled damage.
      if ((held & (uint16_t)MSL_BUTTON_B) == 0u) {
        batch->state.special_cmd0[idx] = 0u;
        ms_enter(batch, idx,
                 grounded_family ? (uint16_t)MSL_ACT_MS_SPECIAL_N_END0
                                 : (uint16_t)MSL_ACT_MS_SPECIAL_AIR_N_END0,
                 0.0f);
      }
      break;
    }
    case MSL_ACT_MS_SPECIAL_N_END0:
    case MSL_ACT_MS_SPECIAL_AIR_N_END0:
    case MSL_ACT_MS_SPECIAL_N_END1:
    case MSL_ACT_MS_SPECIAL_AIR_N_END1:
      // The End0 charge-damage override is applied by the combat hitbox refresh hook
      // (marth_specialn_end_damage_override) so it tracks live HitCapsules exactly.
      if (ms_anim_finished(cid, msid, frame)) {
        ms_exit_to_wait_or_fall(batch, idx);
      }
      break;

    // ---- Dancing Blade ---------------------------------------------------
    default:
      if (ms_db_is_stage(a)) {
        // IASA chain (per-stage): cmd0 window from the stage movescript; cmd1 = pressed-early
        // lockout (decomp ftMs_SpecialS*_IASA).
        const uint8_t window = move_tables_special_cmd_var_value_at_frame(cid, msid, 0u, frame);
        if (!ms_db_is_final_stage(a)) {
          if (window) {
            if (batch->state.special_cmd1[idx] == 0u && (pressed & AB) != 0u) {
              const uint16_t next = ms_db_next_action(batch, c, idx, a);
              if (next != 0u) {
                batch->state.special_cmd1[idx] = 0u;
                ms_enter(batch, idx, next, 0.0f);
                msl_anim_timebase_tick_once(batch, idx);
                break;
              }
            }
          } else if ((pressed & AB) != 0u) {
            batch->state.special_cmd1[idx] = 1u;
          }
        }
        if (ms_anim_finished(cid, msid, frame)) {
          ms_exit_to_wait_or_fall(batch, idx);
        }
        break;
      }

      // ---- Dolphin Slash -------------------------------------------------
      if (a == (uint16_t)MSL_ACT_MS_SPECIAL_HI || a == (uint16_t)MSL_ACT_MS_SPECIAL_AIR_HI) {
        // Pre-launch IASA: stick X tilts the launch angle (ftMs_SpecialHi_IASA).
        const uint8_t launched = move_tables_special_cmd_var_value_at_frame(cid, msid, 0u, frame);
        {
          const float sx = ms_stick_unit(batch->state.input_main_x[idx]);
          const float ax = fabsf(sx);
          if (!launched && ax > ch->specialhi_angle_stick_threshold &&
              ch->specialhi_angle_stick_threshold < 1.0f) {
            // Launch-angle tilt accumulates only while cmd_vars[0] is clear.
            float deg =
                ch->specialhi_angle_max_degrees * ((ax - ch->specialhi_angle_stick_threshold) /
                                                   (1.0f - ch->specialhi_angle_stick_threshold));
            float rad = deg * (3.14159265359f / 180.0f);
            rad = (sx > 0.0f) ? -rad : rad;
            if (fabsf(rad) > fabsf(batch->state.special_stick_angle[idx])) {
              batch->state.special_stick_angle[idx] = rad;
            }
          }
          // B-reverse: ftCheckThrowB3 runs UNCONDITIONALLY in the IASA (not under cmd0==0);
          // the script's set_throw_flags pulse (frame 6, the same frame cmd0 sets) is
          // consume-once, modeled as a single-frame window at the pulse crossing.
          // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::ftMs_SpecialHi_IASA
          // refs/melee/src/melee/ft/inlines.h::ftCheckThrowB3
          if (move_tables_special_throw_flags_window(cid, msid, frame) &&
              !move_tables_special_throw_flags_window(cid, msid, frame - 1.0f) &&
              ax > ch->specialhi_breverse_stick_threshold) {
            batch->state.facing[idx] = (uint8_t)(sx > 0.0f);
          }
        }
        if (ms_anim_finished(cid, msid, frame)) {
          if (batch->state.on_ground[idx]) {
            // Grounded anim end (ground contact resolved the same frame): the landing owner is
            // LandingFallSpecial with x2C, not an (unexitable) grounded FallSpecial.
            // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialHi.c::ftMs_SpecialHi_80138884
            msl_locomotion_enter_fall_special_via_ftco_80096900(
                batch, idx, ch->specialhi_landing_lag_frames, /*allow_interrupt=*/0u);
            batch->state.action_id[idx] = (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL;
            batch->state.animation_index[idx] = (uint32_t)MSL_SM_LANDING_FALL_SPECIAL;
            msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
          } else {
            ms_enter_fall_special_from_specialhi(batch, ch, idx);
          }
        }
        break;
      }

      // ---- Counter ---------------------------------------------------------
      if (a == (uint16_t)MSL_ACT_MS_SPECIAL_LW || a == (uint16_t)MSL_ACT_MS_SPECIAL_AIR_LW) {
        // Window state machine (ftMs_SpecialLw_Anim): script cmd1 drives the intercept
        // descriptor lifetime; the armed value (2) persists until the script closes the window.
        const uint8_t script_cmd1 =
            move_tables_special_cmd_var_value_at_frame(cid, msid, 1u, frame);
        if (script_cmd1 && batch->state.speciallw_counter_window[idx] == 0u) {
          batch->state.speciallw_counter_window[idx] = 2u;  // armed
        } else if (!script_cmd1 && batch->state.speciallw_counter_window[idx] != 0u) {
          batch->state.speciallw_counter_window[idx] = 0u;
        }
        if (ms_anim_finished(cid, msid, frame)) {
          batch->state.speciallw_counter_window[idx] = 0u;
          ms_exit_to_wait_or_fall(batch, idx);
        }
        break;
      }
      if (a == (uint16_t)MSL_ACT_MS_SPECIAL_LW_HIT ||
          a == (uint16_t)MSL_ACT_MS_SPECIAL_AIR_LW_HIT) {
        // CounterAttack: hitboxes/damage come from the movescript (Marth keeps authored
        // damage; the speciallw_countered_damage override is the FTKIND_EMBLEM/Roy path).
        if (ms_anim_finished(cid, msid, frame)) {
          ms_exit_to_wait_or_fall(batch, idx);
        }
        break;
      }
      break;
  }
}

// ---------------------------------------------------------------------------
// Physics (decomp: ftMs_Special*_Phys)
// ---------------------------------------------------------------------------

// ft_80084F3C: grounded friction with the common high-speed multiplier when |gr_vel| exceeds
// walk_max. refs/melee/src/melee/ft/ft_084E.c::ft_80084F3C
static void ms_ground_friction_f3c(MslBatch* batch, const MslCharParams* ch, size_t idx) {
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

// ft_80084FA8 -> ft_80085030: when the animation owns root motion, gr_vel is driven to the
// anim's per-frame TransN z-delta (facing-aligned); otherwise the F3C friction applies.
// refs/melee/src/melee/ft/ft_084E.c::{ft_80084FA8,ft_80085030}
static void ms_ground_anim_vel_fa8(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                   uint16_t msid, float frame) {
  if (msl_anim_uses_root_motion(batch->state.char_id[idx], msid)) {
    float t_cur[3];
    float t_prev[3];
    const uint16_t f_cur = msl_anim_frame_floor_u16(frame);
    const uint16_t f_prev = (f_cur > 0u) ? (uint16_t)(f_cur - 1u) : 0u;
    if (anim_pose_get_transn(batch->state.char_id[idx], msid, f_cur, t_cur) == 0 &&
        anim_pose_get_transn(batch->state.char_id[idx], msid, f_prev, t_prev) == 0) {
      const float facing = ms_facing_dir(batch, idx);
      const float dz = (t_cur[2] - t_prev[2]) * ch->model_scaling;
      batch->state.speed_ground_x_self[idx] = dz * facing;
      return;
    }
  }
  ms_ground_friction_f3c(batch, ch, idx);
}

static void ms_fall_step(MslBatch* batch, size_t idx, float grav, float terminal) {
  float vy = batch->state.speed_y_self[idx];
  vy -= grav;
  if (vy < -terminal) {
    vy = -terminal;
  }
  batch->state.speed_y_self[idx] = vy;
}

static void ms_air_friction_step(MslBatch* batch, size_t idx, float friction) {
  float vx = batch->state.speed_air_x_self[idx];
  if (vx > 0.0f) {
    vx -= friction;
    if (vx < 0.0f) {
      vx = 0.0f;
    }
  } else if (vx < 0.0f) {
    vx += friction;
    if (vx > 0.0f) {
      vx = 0.0f;
    }
  }
  batch->state.speed_air_x_self[idx] = vx;
}

static void ms_air_drift_step(MslBatch* batch, const MslCharParams* ch, size_t idx,
                              float mobility_mul) {
  // ftCommon_8007D344(fp, 0, air_drift_stick_mul * mul, air_drift_max * mul)
  const MslCommonParams* c = msl_common_params();
  const float sx = ms_apply_deadzone(ms_stick_unit(batch->state.input_main_x[idx]),
                                     c != NULL ? c->lstick_deadzone_x : 0.2625f);
  const float accel = sx * ch->air_drift_stick_mul * mobility_mul;
  const float cap = ch->air_drift_max * mobility_mul;
  float vx = batch->state.speed_air_x_self[idx] + accel;
  if (vx > cap) {
    vx = cap;
  } else if (vx < -cap) {
    vx = -cap;
  }
  batch->state.speed_air_x_self[idx] = vx;
}

// Dolphin Slash launch: self_vel = rotate(transN per-frame offset (z*facing, y), lstick_angle).
// refs/melee/src/melee/ft/ft_084E.c::ft_80085154
static void ms_specialhi_launch_vel(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                    uint16_t msid, float frame) {
  float t_cur[3];
  float t_prev[3];
  const uint16_t f_cur = msl_anim_frame_floor_u16(frame);
  const uint16_t f_prev = (f_cur > 0u) ? (uint16_t)(f_cur - 1u) : 0u;
  if (anim_pose_get_transn(batch->state.char_id[idx], msid, f_cur, t_cur) != 0 ||
      anim_pose_get_transn(batch->state.char_id[idx], msid, f_prev, t_prev) != 0) {
    return;
  }
  const float off_y = (t_cur[1] - t_prev[1]) * ch->model_scaling;
  const float off_z = (t_cur[2] - t_prev[2]) * ch->model_scaling;
  const float facing = ms_facing_dir(batch, idx);
  const float ang = batch->state.special_stick_angle[idx];
  const float ca = cosf(ang);
  const float sa = sinf(ang);
  const float fz = off_z * facing;
  float vx = (fz * ca) - (off_y * sa);
  const float vy = (fz * sa) + (off_y * ca);
  // Facing alignment: if the launch X opposes facing, mirror it (decomp sign fixup).
  if ((vx < 0.0f && facing > 0.0f) || (vx > 0.0f && facing < 0.0f)) {
    vx = -vx;
  }
  batch->state.speed_air_x_self[idx] = vx;
  batch->state.speed_y_self[idx] = vy;
}

uint8_t marth_specials_phys(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_MARTH) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (!marth_action_is_special(a)) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
  if (ch == NULL) {
    return 0u;
  }
  const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;
  const uint16_t msid = marth_special_submotion(a);
  const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);

  switch (a) {
    case MSL_ACT_MS_SPECIAL_N_START:
    case MSL_ACT_MS_SPECIAL_N_LOOP:
    case MSL_ACT_MS_SPECIAL_N_END0:
    case MSL_ACT_MS_SPECIAL_N_END1:
      if (on_ground) {
        // ftCommon_ApplyFrictionGround(fp, specialn_start_friction)
        float v = batch->state.speed_ground_x_self[idx];
        const float f = ch->specialn_start_friction;
        if (v > 0.0f) {
          v = (v > f) ? v - f : 0.0f;
        } else if (v < 0.0f) {
          v = (v < -f) ? v + f : 0.0f;
        }
        batch->state.speed_ground_x_self[idx] = v;
        return 1u;
      }
      ms_fall_step(batch, idx, ch->grav, ch->terminal_vel);
      ms_air_friction_step(batch, idx, ch->specialn_start_friction);
      return 1u;
    case MSL_ACT_MS_SPECIAL_AIR_N_START:
    case MSL_ACT_MS_SPECIAL_AIR_N_LOOP:
    case MSL_ACT_MS_SPECIAL_AIR_N_END0:
    case MSL_ACT_MS_SPECIAL_AIR_N_END1:
      if (on_ground) {
        return 0u;  // landed mid-SB: generic grounded handling
      }
      ms_fall_step(batch, idx, ch->grav, ch->terminal_vel);
      ms_air_friction_step(batch, idx, ch->specialn_start_friction);
      return 1u;
    case MSL_ACT_MS_SPECIAL_HI: {
      // Ground-origin Dolphin Slash: once airborne, anim-driven launch until descending,
      // then post-launch fall + reduced drift (ftMs_SpecialHi_Phys).
      if (on_ground) {
        // Pre-launch grounded frames: ft_80084FA8 (anim root-motion owner; the launch wind-up
        // root motion replaces carried gr_vel rather than sliding on it).
        ms_ground_anim_vel_fa8(batch, ch, idx, msid, frame);
        return 1u;
      }
      if (batch->state.special_cmd2[idx] == 0u) {
        ms_specialhi_launch_vel(batch, ch, idx, msid, frame);
        if (batch->state.speed_y_self[idx] < 0.0f) {
          batch->state.special_cmd2[idx] = 1u;
        }
        return 1u;
      }
      // ftMs_SpecialHi_Coll sequencing: the first descending collision pass only arms
      // cmd_vars[1]; the cliffcatch wrapper runs from the second. This phys branch first runs
      // on the second descending frame (cmd2 was set by the previous frame's launch branch),
      // so arming here gives the ledge admission vanilla's one-frame delay.
      batch->state.special_cmd1[idx] = 1u;
      ms_fall_step(batch, idx, ch->specialhi_fall_accel, ch->specialhi_terminal_vel);
      ms_air_drift_step(batch, ch, idx, ch->specialhi_freefall_mobility_mul);
      return 1u;
    }
    case MSL_ACT_MS_SPECIAL_AIR_HI: {
      const uint8_t launched =
          move_tables_special_cmd_var_value_at_frame(batch->state.char_id[idx], msid, 0u, frame);
      if (!launched) {
        ms_fall_step(batch, idx, ch->grav, ch->terminal_vel);
        return 1u;
      }
      if (batch->state.special_cmd2[idx] == 0u) {
        ms_specialhi_launch_vel(batch, ch, idx, msid, frame);
        batch->state.speed_air_x_self[idx] *= ch->specialhi_launch_decay_mul;
        batch->state.speed_y_self[idx] *= ch->specialhi_launch_decay_mul;
        if (batch->state.speed_y_self[idx] < 0.0f) {
          batch->state.special_cmd2[idx] = 1u;
        }
        return 1u;
      }
      // See the grounded-origin branch: arm cmd_vars[1] from the second descending frame.
      batch->state.special_cmd1[idx] = 1u;
      ms_fall_step(batch, idx, ch->specialhi_fall_accel, ch->specialhi_terminal_vel);
      ms_air_drift_step(batch, ch, idx, ch->specialhi_freefall_mobility_mul);
      return 1u;
    }
    case MSL_ACT_MS_SPECIAL_LW:
    case MSL_ACT_MS_SPECIAL_LW_HIT:
      if (on_ground) {
        // ftMs_SpecialLw_Phys / ftMs_SpecialLwHit_Phys: ft_80084F3C.
        ms_ground_friction_f3c(batch, ch, idx);
        return 1u;
      }
      ms_fall_step(batch, idx, ch->speciallw_fall_accel, ch->speciallw_terminal_vel);
      ms_air_friction_step(batch, idx, ch->speciallw_air_friction);
      return 1u;
    case MSL_ACT_MS_SPECIAL_AIR_LW:
    case MSL_ACT_MS_SPECIAL_AIR_LW_HIT:
      if (on_ground) {
        return 0u;
      }
      ms_fall_step(batch, idx, ch->speciallw_fall_accel, ch->speciallw_terminal_vel);
      ms_air_friction_step(batch, idx, ch->speciallw_air_friction);
      return 1u;
    default:
      if (ms_db_is_stage(a)) {
        if (on_ground) {
          // ftMs_SpecialAirS1/S2/S4_Phys grounded branch: ft_80084F3C (friction);
          // ftMs_SpecialS3_Phys grounded branch: ft_80084FA8 (anim root-motion owner).
          if (a == (uint16_t)MSL_ACT_MS_SPECIAL_S3_HI || a == (uint16_t)MSL_ACT_MS_SPECIAL_S3_S ||
              a == (uint16_t)MSL_ACT_MS_SPECIAL_S3_LW) {
            ms_ground_anim_vel_fa8(batch, ch, idx, msid, frame);
          } else {
            ms_ground_friction_f3c(batch, ch, idx);
          }
          return 1u;
        }
        ms_fall_step(batch, idx, ch->specials_fall_accel, ch->specials_terminal_vel);
        ms_air_friction_step(batch, idx, ch->specials_air_friction);
        return 1u;
      }
      return 0u;
  }
}

// ---------------------------------------------------------------------------
// Entry dispatch + update loop
// ---------------------------------------------------------------------------

// Grounded/aerial B-special admission, per action and per direction, mirroring the ftCo IASA
// dispatch chains (each grounded action's IASA calls a specific subset of
// ftCo_SpecialS_CheckInput / ftCo_Attack100_CheckInput (up) / ftCo_800D6824 (neutral) /
// ftCo_800D68C0 (down)).
// refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Wait.c,ftCo_Walk.c,ftCo_Squat.c,
//   ftCo_SquatWait.c,ftCo_Run.c,ftCo_Dash.c,ftCo_RunBrake.c,ftCo_TurnRun.c,ftCo_Turn.c,
//   ftCo_KneeBend.c,ftCo_Ottotto.c,ftCo_Landing.c}
enum {
  MS_B_SIDE = 1u << 0,
  MS_B_UP = 1u << 1,
  MS_B_NEUTRAL = 1u << 2,
  MS_B_DOWN = 1u << 3,
  MS_B_ALL = 0xFu,
};

static uint8_t ms_b_entry_mask(const MslBatch* batch, size_t idx, uint16_t a, uint8_t on_ground) {
  if (on_ground) {
    switch (a) {
      // Full chain: SpecialS -> Attack100(up) -> D6824(neutral) -> D68C0(down).
      // RunDirect_IASA and OttottoWait_IASA (which delegates to Ottotto_IASA) run the same
      // full chain as Run/Ottotto.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunDirect.c::ftCo_RunDirect_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Ottotto.c::ftCo_OttottoWait_IASA
      case MSL_ACT_WAIT:
      case MSL_ACT_WALK_SLOW:
      case MSL_ACT_WALK_MIDDLE:
      case MSL_ACT_WALK_FAST:
      case MSL_ACT_SQUAT:
      case MSL_ACT_RUN:
      case MSL_ACT_RUN_DIRECT:
      case MSL_ACT_OTTOTTO:
      case MSL_ACT_OTTOTTO_WAIT:
        return MS_B_ALL;
      case MSL_ACT_LANDING: {
        // Landing_IASA runs the full chain only past the landing-lag gate.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
        const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
        const float cur = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
        return (ch != NULL && cur >= (float)ch->landing_lag_frames) ? (uint8_t)MS_B_ALL : 0u;
      }
      // SquatWait_IASA / SquatRv_IASA: D68C0(down) -> Attack100(up); no SpecialS, no D6824.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SquatRv.c::ftCo_SquatRv_IASA
      case MSL_ACT_SQUAT_WAIT:
      case MSL_ACT_SQUAT_RV:
        return (uint8_t)(MS_B_UP | MS_B_DOWN);
      // Turn_IASA: SpecialS -> D68C0(down) -> Attack100(up); no D6824(neutral).
      case MSL_ACT_TURN:
        return (uint8_t)(MS_B_SIDE | MS_B_UP | MS_B_DOWN);
      // Dash_IASA: SpecialS only.
      case MSL_ACT_DASH:
        return (uint8_t)MS_B_SIDE;
      // KneeBend_IASA: Attack100(up) first (jump-cancel up-special). The IASA first runs on
      // the frame AFTER entry (callbacks already ran for the entering frame), so block the
      // entry frame itself.
      case MSL_ACT_KNEE_BEND:
        return (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND) ? (uint8_t)MS_B_UP
                                                                                 : 0u;
      // GuardOff_IASA runs the full chain only while mv.co.guard.x1C is armed (powershield
      // contact window); the engine lane already exists for the spacie dispatcher.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOff_IASA,ftCo_80094138}
      case MSL_ACT_GUARD_OFF:
        return (batch->state.guard_special_enable_timer_x1c[idx] != 0u) ? (uint8_t)MS_B_ALL : 0u;
      // Grounded attack IASA -> specials, gated on fp->allow_interrupt (the script-owned
      // allow_interrupt event via move_tables):
      // - AttackS4_IASA runs the full special chain directly.
      // - Attack13/AttackDash/AttackS3*/AttackHi3/AttackHi4/AttackLw4 delegate to
      //   ftCo_Wait_IASA (full chain) once allow_interrupt is set.
      // - Attack11/Attack12/AttackLw3/Attack100* run attack/locomotion checks only - NO
      //   special dispatch (excluded below by default).
      // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackS4.c,ftCo_Attack1.c,
      //   ftCo_AttackDash.c,ftCo_AttackS3.c,ftCo_AttackHi3.c,ftCo_AttackHi4.c,
      //   ftCo_AttackLw4.c,ftCo_AttackLw3.c}
      case MSL_ACT_ATTACK_13:
      case MSL_ACT_ATTACK_DASH:
      case MSL_ACT_ATTACK_S3_HI:
      case MSL_ACT_ATTACK_S3_HI_S:
      case MSL_ACT_ATTACK_S3_S:
      case MSL_ACT_ATTACK_S3_LW_S:
      case MSL_ACT_ATTACK_S3_LW:
      case MSL_ACT_ATTACK_HI3:
      case MSL_ACT_ATTACK_S4_HI:
      case MSL_ACT_ATTACK_S4_HI_S:
      case MSL_ACT_ATTACK_S4_S:
      case MSL_ACT_ATTACK_S4_LW_S:
      case MSL_ACT_ATTACK_S4_LW:
      case MSL_ACT_ATTACK_HI4:
      case MSL_ACT_ATTACK_LW4:
        return move_tables_grounded_attack_allow_interrupt(
                   batch->state.char_id[idx], a,
                   msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]))
                   ? (uint8_t)MS_B_ALL
                   : 0u;
      // Grounded Damage_IASA delegates to ftCo_Wait_IASA once the hitstun scalar clears
      // (x221C_b6); the dispatcher's hitstun gate already enforces the scalar.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_IASA
      case MSL_ACT_DAMAGE_HI_1:
      case MSL_ACT_DAMAGE_HI_1 + 1:
      case MSL_ACT_DAMAGE_HI_1 + 2:
      case MSL_ACT_DAMAGE_N_1:
      case MSL_ACT_DAMAGE_N_1 + 1:
      case MSL_ACT_DAMAGE_N_1 + 2:
      case MSL_ACT_DAMAGE_LW_1:
      case MSL_ACT_DAMAGE_LW_1 + 1:
      case MSL_ACT_DAMAGE_LW_1 + 2:
        return MS_B_ALL;
      // AppealS_IASA runs the full chain gated on fp->allow_interrupt, but no extracted
      // MSLFTSC1 allow_interrupt event exists for the Appeal msids (engine-wide retained
      // policy: Appeal rows stay anim-end-only until the data owns the event), so Appeal
      // stays blocked here intentionally.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AppealS.c::ftCo_AppealS_IASA
      // RunBrake/TurnRun IASA: jump/dash checks only, no special dispatch. Exception: on
      // the brake ENTRY frame the B edge belongs to the same frame's Run_IASA /
      // RunDirect_IASA (both run the full chain BEFORE the brake transition in source
      // order, and the locomotion brake transition accepts either source); the sim's
      // locomotion brakes before this dispatcher runs, so honor the pre-brake chain.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunDirect.c::ftCo_RunDirect_IASA
      case MSL_ACT_RUN_BRAKE:
        return (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN ||
                batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN_DIRECT)
                   ? (uint8_t)MS_B_ALL
                   : 0u;
      default:
        return 0u;
    }
  }
  // Aerial: ftCo_SpecialAir_CheckInput resolves all four; reachable from air locomotion,
  // Pass, and post-hitstun DamageFall (the dispatcher's hitstun gate).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
  // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Fall.c,ftCo_Jump.c,ftCo_JumpAerial.c,
  //   ftCo_Pass.c,ftCo_DamageFall.c}
  //
  // RESOLVED (x686/x68B input-history lanes landed): the aerial up-special is admitted on
  // up+B PRESENCE with the x68B >= x1C freshness gate (ftCo_800D69C4); grounded on presence
  // alone (ftCo_Attack100_CheckInput). See the dispatch block below.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D69C4
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
      return MS_B_ALL;
    // Airborne Damage/DamageFly IASA delegate into Fall_IASA_Inner / DamageFall_IASA once
    // the hitstun scalar clears (the dispatcher's hitstun gate); same split the spacie
    // dispatcher models.
    // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_Damage.c,ftCo_DamageFall.c,ftCo_Fall.c}
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_1 + 1:
    case MSL_ACT_DAMAGE_AIR_1 + 2:
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
      return MS_B_ALL;
    // PassiveWall_IASA runs ftCo_SpecialAir_CheckInput; the walltech timer blocks the
    // window exactly as the spacie dispatcher models.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Passive.c (PassiveWall)
    case MSL_ACT_PASSIVE_WALL:
    case MSL_ACT_PASSIVE_WALL_JUMP:
      return (batch->state.passivewall_timer[idx] != 0u) ? 0u : (uint8_t)MS_B_ALL;
    // Out-of-scope (engine substrate): BuryJump/CaptureJump (no capture-escape jump
    // machinery), ItemParasol/ItemScope/ItemScrew (no held items).
    default:
      return 0u;
  }
}

void marth_specials_update_pre_physics(MslBatch* batch) {
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
      if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_MARTH ||
          batch->state.stocks[idx] == 0u) {
        continue;
      }
      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }
      const uint16_t a = batch->state.action_id[idx];
      const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;

      // Air side-special freshness resets on grounding (fv.ms.x222C cleared by the
      // air->ground stage transitions and ordinary landings).
      if (on_ground) {
        batch->state.specials_air_used[idx] = 0u;
      }

      if (marth_action_is_special(a)) {
        ms_update_player(batch, c, ch, idx);
        continue;
      }

      // B-press dispatch (mirrors the spacie dispatcher ordering; Marth has no article
      // machinery so all four directions live here).
      if (batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u) {
        continue;
      }
      const uint16_t pressed = batch->state.input_buttons_pressed[idx];
      const uint8_t b_edge = ((pressed & (uint16_t)MSL_BUTTON_B) != 0u) ? 1u : 0u;
      // Source up-special admission is PRESENCE-based, not edge-based: grounded
      // ftCo_Attack100_CheckInput fires on x686 == 0 (up+B present this frame, including a
      // held B with a late up-flick); aerial ftCo_800D69C4 adds the x68B >= x1C freshness
      // gate (x68B carries the gap before the current press period only on its first frame,
      // so a mashed second up+B within the window is rejected).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
      //   ftCo_Attack100_CheckInput,ftCo_800D69C4,ftCo_800D6928}
      const uint8_t up_b_present = (batch->state.x686[idx] == 0u) ? 1u : 0u;
      const uint8_t air_up_b_fresh =
          (uint8_t)(up_b_present && batch->state.x68B[idx] >= c->tech_lr_debounce_frames);
      if (!b_edge && !up_b_present) {
        continue;
      }
      const uint8_t mask = ms_b_entry_mask(batch, idx, a, on_ground);
      if (mask == 0u) {
        continue;
      }
      const float sx =
          ms_apply_deadzone(ms_stick_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
      const float sy =
          ms_apply_deadzone(ms_stick_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
      const float ax = fabsf(sx);
      // Resolution order mirrors the per-action IASA chain order: grounded
      // SpecialS -> Attack100(up) -> D6824(neutral) -> D68C0(down) (ftCo_Wait_IASA); aerial
      // Up -> Down -> Side -> Neutral (ftCo_SpecialAir_CheckInput). The source neutral/down
      // helpers gate on the x689/x687 input counters rather than rereading the raw stick;
      // the stick-zone reconstruction below is outcome-equivalent because the zones are
      // mutually exclusive, but the check order matches the source chain regardless.
      // Directions absent from the action's chain do not enter.
      if (on_ground) {
        if ((mask & MS_B_SIDE) != 0u && b_edge && ax >= c->special_stick_x_threshold_side) {
          if ((sx > 0.0f) != (batch->state.facing[idx] != 0u)) {
            batch->state.facing[idx] = (uint8_t)(sx > 0.0f);
          }
          ms_enter_specials(batch, ch, idx, 1u);
        } else if ((mask & MS_B_UP) != 0u && up_b_present) {
          ms_enter_specialhi(batch, ch, idx, 1u);
        } else if ((mask & MS_B_NEUTRAL) != 0u && b_edge &&
                   ax < c->special_stick_x_threshold_side && sy < c->special_stick_y_threshold &&
                   sy > -c->special_stick_y_threshold) {
          ms_enter_specialn(batch, ch, idx, 1u);
        } else if ((mask & MS_B_DOWN) != 0u && b_edge && sy <= -c->special_stick_y_threshold) {
          ms_enter_speciallw(batch, ch, idx, 1u);
        }
      } else {
        if ((mask & MS_B_UP) != 0u && air_up_b_fresh) {
          ms_enter_specialhi(batch, ch, idx, 0u);
        } else if ((mask & MS_B_DOWN) != 0u && b_edge && sy <= -c->special_stick_y_threshold) {
          ms_enter_speciallw(batch, ch, idx, 0u);
        } else if ((mask & MS_B_SIDE) != 0u && b_edge && ax >= c->special_stick_x_threshold_side) {
          if ((sx > 0.0f) != (batch->state.facing[idx] != 0u)) {
            batch->state.facing[idx] = (uint8_t)(sx > 0.0f);
          }
          ms_enter_specials(batch, ch, idx, 0u);
        } else if ((mask & MS_B_NEUTRAL) != 0u && b_edge &&
                   ax < c->special_stick_x_threshold_side && sy < c->special_stick_y_threshold) {
          ms_enter_specialn(batch, ch, idx, 0u);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Ground <-> air variant swaps (collision callbacks; preserve animation frame)
// ---------------------------------------------------------------------------

// Decomp swap pairs (ground id <-> air id), all entered at fp->cur_anim_frame:
// - Shield Breaker: 341..344 <-> 345..348 (ftMs_SpecialN_80136A1C/80136A7C/80136DB4/80136E14...)
// - Dancing Blade stages: 349..357 <-> 358..366 (ftMs_SpecialS_801376E8/80137748/80137CBC/80137D60)
// - Counter: 369<->371, 370<->372 (ftMs_SpecialLw_80138D38/80138DD0/80139080/801390E0)
// Dolphin Slash (367/368) has no swap pair; its collision handling is the cliffcatch/landing
// path (ledge.c + the LandingFallSpecial owner).
static uint16_t marth_special_air_variant(uint16_t a) {
  if (a >= 341u && a <= 344u) {
    return (uint16_t)(a + 4u);
  }
  if (a >= 349u && a <= 357u) {
    return (uint16_t)(a + 9u);
  }
  if (a == 369u || a == 370u) {
    return (uint16_t)(a + 2u);
  }
  return 0u;
}

static uint16_t marth_special_ground_variant(uint16_t a) {
  if (a >= 345u && a <= 348u) {
    return (uint16_t)(a - 4u);
  }
  if (a >= 358u && a <= 366u) {
    return (uint16_t)(a - 9u);
  }
  if (a == 371u || a == 372u) {
    return (uint16_t)(a - 2u);
  }
  return 0u;
}

static void ms_swap_preserving_frame(MslBatch* batch, size_t idx, uint16_t next_action) {
  const float cur = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  batch->state.action_id[idx] = next_action;
  batch->state.animation_index[idx] = (uint32_t)marth_special_submotion(next_action);
  msl_anim_timebase_enter(batch, idx, cur, 1.0f);
  // ChangeMotionState without Ft_MF_Unk24 clears fp->x221C_u16_y; opcode-52 levels whose
  // source event is at or before the preserved entry frame stay cleared until the script
  // crosses its next event (state_flags.c consumer).
  batch->state.x221c_y_event_floor[idx] = (uint16_t)(msl_anim_frame_floor_u16(cur) + 1u);
}

uint8_t marth_special_try_air_to_ground_swap(MslBatch* batch, size_t idx) {
  if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_MARTH) {
    return 0u;
  }
  const uint16_t next = marth_special_ground_variant(batch->state.action_id[idx]);
  if (next == 0u) {
    return 0u;
  }
  // ftCommon_8007D7FC grounding bundle equivalents are applied by the caller's landing path
  // (gr_vel sync, jumps refresh); the swap owns action/anim only. fv.ms.x222C clears on
  // grounding via the per-frame specials_air_used reset.
  ms_swap_preserving_frame(batch, idx, next);
  batch->state.specials_air_used[idx] = 0u;
  return 1u;
}

uint8_t marth_special_try_ground_to_air_swap(MslBatch* batch, size_t idx) {
  if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_MARTH) {
    return 0u;
  }
  const uint16_t next = marth_special_air_variant(batch->state.action_id[idx]);
  if (next == 0u) {
    return 0u;
  }
  // ftCommon_8007D5D4 (lose ground jump / no-ECB window) equivalents are owned by the caller's
  // floor-loss path.
  ms_swap_preserving_frame(batch, idx, next);
  return 1u;
}
