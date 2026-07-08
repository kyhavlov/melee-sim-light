#include "puff_specials.h"

#include <math.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "common_params.h"
#include "ids.h"
#include "locomotion.h"
#include "move_tables.h"

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

      // ---- B dispatch (side-B only until Rollout/Sing/Rest land) ----------
      if (a < 341u || puff_action_is_multijump(cid, a)) {
        if (batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] == 0u &&
            (batch->state.input_buttons_pressed[idx] & (uint16_t)MSL_BUTTON_B) != 0u &&
            puff_b_entry_admissible(batch, idx, a)) {
          const float sx = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_x[idx]),
                                             c->lstick_deadzone_x);
          const float sy = pr_apply_deadzone(pr_stick_unit(batch->state.input_main_y[idx]),
                                             c->lstick_deadzone_y);
          const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;
          // Zone resolution mirrors the falcon dispatcher's chain subset: up/down zones are NOT
          // consumed yet (Sing/Rest owners land later; leave those rows untouched so the replay
          // rows stay visibly unconsumed rather than wrongly entering Pound).
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
    default:
      return 0u;
  }
}

// Ground <-> air phase flips preserve the animation frame (13D590/13D5F0).
// refs/melee/src/melee/ft/chara/ftPurin/ftPr_SpecialS.c::{ftPr_SpecialS_8013D590,
//   ftPr_SpecialS_8013D5F0}
uint8_t puff_special_try_ground_to_air_swap(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF) {
    return 0u;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_PR_SPECIAL_S) {
    return 0u;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_PR_SPECIAL_AIR_S;
  batch->state.animation_index[idx] =
      (uint32_t)puff_special_submotion((uint16_t)MSL_ACT_PR_SPECIAL_AIR_S);
  return 1u;
}

uint8_t puff_special_try_air_to_ground_swap(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_PUFF) {
    return 0u;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_PR_SPECIAL_AIR_S) {
    return 0u;
  }
  batch->state.action_id[idx] = (uint16_t)MSL_ACT_PR_SPECIAL_S;
  batch->state.animation_index[idx] =
      (uint32_t)puff_special_submotion((uint16_t)MSL_ACT_PR_SPECIAL_S);
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
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_JumpAerial.c::ft_800CB6EC
    batch->state.puff_mjump_turn_timer[idx] = 0u;
  }
}
