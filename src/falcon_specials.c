#include "falcon_specials.h"

#include <math.h>

#include "action.h"
#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "batch_internal.h"
#include "buttons.h"
#include "char_params.h"
#include "char_registry.h"
#include "coll_env_flags.h"
#include "common_params.h"
#include "grab_flow.h"
#include "ids.h"
#include "locomotion.h"
#include "move_tables.h"
#include "msl_math.h"

// Falcon char-special action ids (347..363; MSLMSO01-derived, ftCa_* Anim callbacks). The
// 341..346 item-swing states are common-owned and never enter through this module.
enum {
  FC_ACT_SPECIAL_N = 347u,
  FC_ACT_SPECIAL_AIR_N = 348u,
  FC_ACT_SPECIAL_S_START = 349u,
  FC_ACT_SPECIAL_S = 350u,
  FC_ACT_SPECIAL_AIR_S_START = 351u,
  FC_ACT_SPECIAL_AIR_S = 352u,
  FC_ACT_SPECIAL_HI = 353u,
  FC_ACT_SPECIAL_AIR_HI = 354u,
  FC_ACT_SPECIAL_HI_CATCH = 355u,
  FC_ACT_SPECIAL_HI_THROW = 356u,
  FC_ACT_SPECIAL_LW = 357u,
  FC_ACT_SPECIAL_LW_END = 358u,
  FC_ACT_SPECIAL_AIR_LW = 359u,
  FC_ACT_SPECIAL_AIR_LW_END = 360u,
  FC_ACT_SPECIAL_AIR_LW_END_AIR = 361u,
  FC_ACT_SPECIAL_LW_END_AIR = 362u,
  FC_ACT_SPECIAL_HI_THROW1 = 363u,
};

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static inline float fc_stick_unit(int8_t v) { return (float)v * (1.0f / 80.0f); }

static inline float fc_apply_deadzone(float v, float dz) { return (fabsf(v) < dz) ? 0.0f : v; }

static inline uint8_t fc_anim_finished(uint8_t char_id, uint16_t msid, float anim_frame_f32) {
  const float end = msl_anim_end_frame(char_id, msid);
  return (end > 0.0f && msl_anim_frame_sanitize_f32(anim_frame_f32) >= end) ? 1u : 0u;
}

static inline void fc_enter_rate(MslBatch* batch, size_t idx, uint16_t action_id, float start_frame,
                                 float rate) {
  batch->state.action_id[idx] = action_id;
  batch->state.animation_index[idx] = (uint32_t)falcon_special_submotion(action_id);
  msl_anim_timebase_enter(batch, idx, start_frame, rate);
}

static inline void fc_enter(MslBatch* batch, size_t idx, uint16_t action_id, float start_frame) {
  fc_enter_rate(batch, idx, action_id, start_frame, 1.0f);
}

// Falcon Kick on-hit friction lane, with an uninitialized-seed guard: replay reseeds of
// mid-kick rows have no friction lane yet (seed reconstruction is a Phase 5 item); a raw 0
// would zero the kick velocity, so treat 0 as the entry value 1.0.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_Special_Inline_Friction
static inline float fc_speciallw_friction(const MslBatch* batch, size_t idx) {
  const float f = batch->state.falcon_speciallw_friction[idx];
  return (f > 0.0f) ? f : 1.0f;
}

static inline void fc_reset_cmds(MslBatch* batch, size_t idx) {
  batch->state.special_cmd0[idx] = 0u;
  batch->state.special_cmd1[idx] = 0u;
  batch->state.special_cmd2[idx] = 0u;
}

static inline float fc_facing_dir(const MslBatch* batch, size_t idx) {
  return batch->state.facing[idx] ? 1.0f : -1.0f;
}

static uint8_t fc_try_enter_air_b_special_from_fall_iasa(MslBatch* batch, const MslCommonParams* c,
                                                         const MslCharParams* ch, size_t idx);
static uint8_t fc_try_run_grounded_wait_iasa_after_ft_8008A2BC(MslBatch* batch,
                                                               const MslCommonParams* c,
                                                               const MslCharParams* ch, size_t idx,
                                                               uint16_t source_action);

// ---------------------------------------------------------------------------
// Entries (decomp: ftCa_Special*_Enter)
// ---------------------------------------------------------------------------

static void fc_enter_specialn(MslBatch* batch, const MslCharParams* ch, size_t idx,
                              uint8_t on_ground) {
  // ftCa_SpecialN_Enter / ftCa_SpecialAirN_Enter: clear cmd vars + throw flags and change
  // motion state at frame 0. Unlike Marth's Shield Breaker there is NO entry velocity write;
  // the grounded wind-up is anim-root-motion-owned and the aerial variant keeps its drift
  // until the script's cmd lanes take over.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::{
  //   ftCa_SpecialN_Enter,ftCa_SpecialAirN_Enter}
  (void)ch;
  fc_reset_cmds(batch, idx);
  fc_enter(batch, idx, on_ground ? (uint16_t)FC_ACT_SPECIAL_N : (uint16_t)FC_ACT_SPECIAL_AIR_N,
           0.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

static void fc_enter_speciallw(MslBatch* batch, const MslCharParams* ch, size_t idx,
                               uint8_t on_ground) {
  // ftCa_SpecialLw_Enter / ftCa_SpecialAirLw_Enter: clear cmd vars + throw flags. Only the
  // GROUNDED entry initializes mv.ca.speciallw (x0 hit count, friction 1.0) and installs the
  // deal_dmg_cb slowdown; the friction lanes are only consumed by SpecialLw/SpecialLwEnd,
  // both reachable exclusively from the grounded family.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
  //   ftCa_SpecialLw_Enter,ftCa_SpecialAirLw_Enter}
  (void)ch;
  fc_reset_cmds(batch, idx);
  if (on_ground) {
    batch->state.falcon_speciallw_hits[idx] = 0u;
    batch->state.falcon_speciallw_friction[idx] = 1.0f;
  }
  fc_enter(batch, idx, on_ground ? (uint16_t)FC_ACT_SPECIAL_LW : (uint16_t)FC_ACT_SPECIAL_AIR_LW,
           0.0f);
  msl_anim_timebase_tick_once(batch, idx);
}

// ---------------------------------------------------------------------------
// Anim-end exits
// ---------------------------------------------------------------------------

static void fc_exit_to_wait_or_fall(MslBatch* batch, const MslCommonParams* c,
                                    const MslCharParams* ch, size_t idx) {
  if (batch->state.on_ground[idx]) {
    // Grounded falcon special Anim callbacks exit through ft_8008A2BC -> Wait; the destination
    // Wait_IASA runs in the same Fighter_procUpdate pass.
    // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
    const uint16_t source_action = batch->state.action_id[idx];
    batch->state.action_id[idx] = (uint16_t)MSL_ACT_WAIT;
    batch->state.animation_index[idx] = (uint32_t)MSL_SM_WAIT1_0;
    msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
    (void)fc_try_run_grounded_wait_iasa_after_ft_8008A2BC(batch, c, ch, idx, source_action);
  } else {
    // Aerial falcon special Anim callbacks exit through ftCo_Fall_Enter; the destination Fall
    // IASA runs in the same proc, so a same-frame B/jump edge can overwrite Fall immediately.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialAirN_Anim
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::{ftCo_Fall_Enter,ftCo_Fall_IASA_Inner}
    msl_locomotion_enter_fall_via_ftco_fall_enter(batch, ch, idx);
    if (fc_try_enter_air_b_special_from_fall_iasa(batch, c, ch, idx)) {
      return;
    }
    (void)msl_locomotion_run_fall_iasa_non_special_tail(batch, c, ch, idx);
  }
}

// ---------------------------------------------------------------------------
// Per-action update (anim/IASA/transitions); runs in the action phase
// ---------------------------------------------------------------------------

// ftCaptain_SpecialN_GetAngleVel: |stick.y| clamped to [range_y_neg, range_y_pos], rebased to
// zero at range_y_neg, sign restored from stick.y, scaled to angle_diff degrees across the
// clamp span, in radians. stickGetDir(y, 0) == |y|.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCaptain_SpecialN_GetAngleVel
// refs/melee/src/melee/ft/inlines.h::stickGetDir
static float fc_specialn_angle_rad(const MslBatch* batch, const MslCharParams* ch, size_t idx) {
  const float raw_y = fc_stick_unit(batch->state.input_main_y[idx]);
  const float max = ch->falcon_specialn_stick_range_y_pos;
  const float min = ch->falcon_specialn_stick_range_y_neg;
  if (!(max > min)) {
    return 0.0f;
  }
  float sy = fabsf(raw_y);
  if (sy > max) {
    sy = max;
  }
  sy -= min;
  if (sy < 0.0f) {
    sy = 0.0f;
  }
  if (raw_y < 0.0f) {
    sy = -sy;
  }
  return (3.14159265359f / 180.0f) * (sy * ch->falcon_specialn_angle_diff / (max - min));
}

static void fc_update_player(MslBatch* batch, const MslCommonParams* c, const MslCharParams* ch,
                             size_t idx) {
  const uint16_t a = batch->state.action_id[idx];
  const uint8_t cid = batch->state.char_id[idx];
  const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  const uint16_t msid = falcon_special_submotion(a);

  switch (a) {
    // ---- Falcon Punch -----------------------------------------------------
    case FC_ACT_SPECIAL_N:
      // ftCa_SpecialN_IASA is empty; the script's allow_interrupt event at frame 65 has no
      // consumer for this action (the grounded punch is not interruptible). Exit at anim end
      // through ft_8008A2BC.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::{
      //   ftCa_SpecialN_IASA,ftCa_SpecialN_Anim}
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    case FC_ACT_SPECIAL_AIR_N: {
      // ftCa_SpecialAirN_IASA: consume the script's cmd_vars[0] pulse (frame 50) once and
      // apply the one-shot punch velocity impulse. special_cmd0 is the consumed-once latch
      // (source sets fp->cmd_vars[0]=0 on consume); it survives ground<->air swaps because
      // the swap transition flags carry cmd state (Ft_MF_UpdateCmd).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialAirN_IASA
      // data/moves/falcon.json::specials_by_msid.302 set_cmd_var(idx=0)@50
      const uint8_t cmd0 = move_tables_special_cmd_var_value_at_frame(cid, msid, 0u, frame);
      if (cmd0 != 0u && batch->state.special_cmd0[idx] == 0u) {
        batch->state.special_cmd0[idx] = 1u;
        const float ang = fc_specialn_angle_rad(batch, ch, idx);
        batch->state.speed_y_self[idx] = ch->falcon_specialn_vel_x * sinf(ang);
        batch->state.speed_air_x_self[idx] =
            ch->falcon_specialn_vel_x * (fc_facing_dir(batch, idx) * cosf(ang));
      }
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    }
    // ---- Falcon Kick ------------------------------------------------------
    case FC_ACT_SPECIAL_LW:
      // ftCa_SpecialLw_Anim: travel anim end -> grounded SpecialLwEnd at the ground-lag anim
      // rate, or airborne SpecialLwEndAir; both reset cmd vars + throw flags.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
      //   ftCa_SpecialLw_Anim,ftCa_SpecialLw_Anim_inline}
      if (fc_anim_finished(cid, msid, frame)) {
        fc_reset_cmds(batch, idx);
        if (batch->state.on_ground[idx]) {
          fc_enter_rate(batch, idx, (uint16_t)FC_ACT_SPECIAL_LW_END, 0.0f,
                        ch->falcon_speciallw_ground_lag_mul > 0.0f
                            ? ch->falcon_speciallw_ground_lag_mul
                            : 1.0f);
        } else {
          fc_enter(batch, idx, (uint16_t)FC_ACT_SPECIAL_LW_END_AIR, 0.0f);
        }
        msl_anim_timebase_tick_once(batch, idx);
      }
      break;
    case FC_ACT_SPECIAL_LW_END:
    case FC_ACT_SPECIAL_LW_END_AIR:
      // ftCa_SpecialLwEnd_Anim / ftCa_SpecialLwEndAir_Anim: anim end -> ftCommon_8007D92C
      // (grounded: ft_8008A2BC Wait tail; airborne: ftCo_Fall_Enter).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    case FC_ACT_SPECIAL_AIR_LW:
      // ftCa_SpecialAirLw_Anim: anim end -> clear cmds + ftCommon_8007D5D4 + SpecialAirLwEndAir.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialAirLw_Anim
      if (fc_anim_finished(cid, msid, frame)) {
        fc_reset_cmds(batch, idx);
        fc_enter(batch, idx, (uint16_t)FC_ACT_SPECIAL_AIR_LW_END_AIR, 0.0f);
        msl_anim_timebase_tick_once(batch, idx);
      }
      break;
    case FC_ACT_SPECIAL_AIR_LW_END:
      // ftCa_SpecialAirLwEnd_Anim: landing-skid anim end -> ft_8008A2BC Wait tail.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialAirLwEnd_Anim
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    case FC_ACT_SPECIAL_AIR_LW_END_AIR:
    case FC_ACT_SPECIAL_HI_THROW1:
      // ftCa_SpecialAirLwEndAir_Anim / ftCa_SpecialHiThrow1_Anim: anim end -> ftCo_Fall_Enter
      // (the Wait branch below only fires on a transient grounded row whose Coll transition is
      // already owed).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c
      if (fc_anim_finished(cid, msid, frame)) {
        fc_exit_to_wait_or_fall(batch, c, ch, idx);
      }
      break;
    default:
      // SpecialS/Hi families are not ported yet; they are unreachable from this module's
      // dispatcher (their zones no-op below) and replay-seeded rows keep the fail-closed
      // generic handling documented in agent_docs/FALCON_PLAN.md.
      break;
  }
}

// ---------------------------------------------------------------------------
// Physics (decomp: ftCa_Special*_Phys)
// ---------------------------------------------------------------------------

// ft_80084F3C: grounded friction with the common high-speed multiplier when |gr_vel| exceeds
// walk_max. refs/melee/src/melee/ft/ft_084E.c::ft_80084F3C
static void fc_ground_friction_f3c(MslBatch* batch, const MslCharParams* ch, size_t idx) {
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
static void fc_ground_anim_vel_fa8(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                   uint16_t msid, float frame) {
  if (msl_anim_uses_root_motion(batch->state.char_id[idx], msid)) {
    float t_cur[3];
    float t_prev[3];
    const uint16_t f_cur = msl_anim_frame_floor_u16(frame);
    const uint16_t f_prev = (f_cur > 0u) ? (uint16_t)(f_cur - 1u) : 0u;
    if (anim_pose_get_transn(batch->state.char_id[idx], msid, f_cur, t_cur) == 0 &&
        anim_pose_get_transn(batch->state.char_id[idx], msid, f_prev, t_prev) == 0) {
      const float facing = fc_facing_dir(batch, idx);
      const float dz = (t_cur[2] - t_prev[2]) * ch->model_scaling;
      batch->state.speed_ground_x_self[idx] = dz * facing;
      return;
    }
  }
  fc_ground_friction_f3c(batch, ch, idx);
}

static void fc_fall_step(MslBatch* batch, size_t idx, float grav, float terminal) {
  float vy = batch->state.speed_y_self[idx];
  vy -= grav;
  if (vy < -terminal) {
    vy = -terminal;
  }
  batch->state.speed_y_self[idx] = vy;
}

static void fc_air_friction_step(MslBatch* batch, size_t idx, float friction) {
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

// ft_80085134: self_vel = per-frame TransN offset delta (z*facing, y) — the airborne
// anim-root-motion owner (Falcon Kick travel/backflip trajectories are animation-owned).
// refs/melee/src/melee/ft/ft_084E.c::ft_80085134
static void fc_air_anim_vel_85134(MslBatch* batch, const MslCharParams* ch, size_t idx,
                                  uint16_t msid, float frame) {
  float t_cur[3];
  float t_prev[3];
  const uint16_t f_cur = msl_anim_frame_floor_u16(frame);
  const uint16_t f_prev = (f_cur > 0u) ? (uint16_t)(f_cur - 1u) : 0u;
  if (anim_pose_get_transn(batch->state.char_id[idx], msid, f_cur, t_cur) != 0 ||
      anim_pose_get_transn(batch->state.char_id[idx], msid, f_prev, t_prev) != 0) {
    return;
  }
  const float facing = fc_facing_dir(batch, idx);
  batch->state.speed_air_x_self[idx] = (t_cur[2] - t_prev[2]) * ch->model_scaling * facing;
  batch->state.speed_y_self[idx] = (t_cur[1] - t_prev[1]) * ch->model_scaling;
}

uint8_t falcon_specials_phys(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (!falcon_action_is_special(a)) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return 0u;
  }
  const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;
  const uint16_t msid = falcon_special_submotion(a);
  const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);

  switch (a) {
    case FC_ACT_SPECIAL_N:
      if (!on_ground) {
        return 0u;  // transient pre-swap frame: generic air handling
      }
      // ftCa_SpecialN_Phys: doPhys (gfx pulses only) + ft_80084FA8 (anim root motion owns
      // the wind-up/step forward; friction otherwise).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialN_Phys
      fc_ground_anim_vel_fa8(batch, ch, idx, msid, frame);
      return 1u;
    case FC_ACT_SPECIAL_AIR_N: {
      if (on_ground) {
        return 0u;  // transient pre-swap frame: generic grounded handling
      }
      // ftCa_SpecialAirN_Phys switches on the script-owned cmd_vars[1]:
      //   0 -> ft_80084EEC (ordinary gravity + air friction; no drift)
      //   1 -> self_vel *= specialn_vel_mul per frame (post-impulse decay, frames 50..64)
      //   2 -> ft_80084DB0 (common fall + fastfall + drift; handled by the generic air path,
      //        admitted via the falcon branch in msl_action_allows_fastfall)
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialAirN_Phys
      // refs/melee/src/melee/ft/ft_084E.c::ft_80084EEC
      // data/moves/falcon.json::specials_by_msid.302 set_cmd_var(idx=1)@{50,65}
      const uint8_t cmd1 =
          move_tables_special_cmd_var_u8_value_at_frame(batch->state.char_id[idx], msid, 1u, frame);
      switch (cmd1) {
        case 0u:
          fc_fall_step(batch, idx, ch->grav, ch->terminal_vel);
          fc_air_friction_step(batch, idx, ch->aerial_friction);
          return 1u;
        case 1u:
          batch->state.speed_y_self[idx] *= ch->falcon_specialn_vel_mul;
          batch->state.speed_air_x_self[idx] *= ch->falcon_specialn_vel_mul;
          return 1u;
        default:
          // cmd1 == 2: hand ownership to the generic ft_80084DB0-equivalent air path.
          return 0u;
      }
    }
    // ---- Falcon Kick air phases (grounded kick phys is the physics.c grounded chain) ------
    case FC_ACT_SPECIAL_LW: {
      if (on_ground) {
        return 0u;  // grounded travel: physics.c grounded chain (85088 + friction scale)
      }
      // ftCa_SpecialLw_Phys air phase: ftPartSetRotZ (cosmetic) + ft_80085134, then
      // Inline_Friction scales the frame's self_vel by the on-hit friction lane (85134
      // rewrites self_vel from TransN each frame, so a per-frame scale does not compound).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Phys
      fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      const float f = fc_speciallw_friction(batch, idx);
      batch->state.speed_air_x_self[idx] *= f;
      batch->state.speed_y_self[idx] *= f;
      return 1u;
    }
    case FC_ACT_SPECIAL_LW_END: {
      if (on_ground) {
        return 0u;  // grounded skid: physics.c grounded chain (cmd2 traction / F3C + scale)
      }
      // ftCa_SpecialLwEnd_Phys air phase: ft_80084EEC then Inline_Friction. self_vel persists
      // across frames here, so the per-frame friction scale compounds exactly as source.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLwEnd_Phys
      fc_fall_step(batch, idx, ch->grav, ch->terminal_vel);
      fc_air_friction_step(batch, idx, ch->aerial_friction);
      const float f = fc_speciallw_friction(batch, idx);
      batch->state.speed_air_x_self[idx] *= f;
      batch->state.speed_y_self[idx] *= f;
      return 1u;
    }
    case FC_ACT_SPECIAL_AIR_LW:
      if (on_ground) {
        return 0u;
      }
      // ftCa_SpecialAirLw_Phys: ft_80085134 only (the dive trajectory is animation-owned; no
      // gravity, no friction lane).
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialAirLw_Phys
      fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      return 1u;
    case FC_ACT_SPECIAL_AIR_LW_END_AIR:
      if (on_ground) {
        return 0u;
      }
      // ftCa_SpecialAirLwEndAir_Phys: ft_80084EEC.
      fc_fall_step(batch, idx, ch->grav, ch->terminal_vel);
      fc_air_friction_step(batch, idx, ch->aerial_friction);
      return 1u;
    case FC_ACT_SPECIAL_LW_END_AIR: {
      if (on_ground) {
        return 0u;  // grounded: physics.c grounded chain (85088)
      }
      // ftCa_SpecialLwEndAir_Phys air branch: cmd0 ? ft_80084EEC : ft_80085134.
      // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLwEndAir_Phys
      // data/moves/falcon.json::specials_by_msid.315 set_cmd_var(idx=0)@7
      const uint8_t cmd0 =
          move_tables_special_cmd_var_u8_value_at_frame(batch->state.char_id[idx], msid, 0u, frame);
      if (cmd0 != 0u) {
        fc_fall_step(batch, idx, ch->grav, ch->terminal_vel);
        fc_air_friction_step(batch, idx, ch->aerial_friction);
      } else {
        fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      }
      return 1u;
    }
    case FC_ACT_SPECIAL_HI_THROW1:
      if (on_ground) {
        return 0u;
      }
      // ftCa_SpecialHiThrow1_Phys: ft_80085134 (backflip trajectory is animation-owned).
      fc_air_anim_vel_85134(batch, ch, idx, msid, frame);
      return 1u;
    default:
      return 0u;
  }
}

// ---------------------------------------------------------------------------
// Falcon Kick on-hit slowdown + wall rebound
// ---------------------------------------------------------------------------

void falcon_speciallw_on_deal_dmg_x1914(MslBatch* batch, size_t a_idx) {
  // deal_dmg_cb (ftCa_SpecialHi_800E400C), installed by the grounded Falcon Kick entry and
  // fired by Fighter_ProcessHit once per frame with dealt damage (fp->dmg.x1914; shield hits
  // take the x19A4 branch first and never reach it). Action 357 is reachable only from the
  // grounded entry (the air kick lands into SpecialAirLwEnd), so the action gate is
  // equivalent to the callback-pointer lifetime.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
  //   ftCa_SpecialLw_Enter,ftCa_SpecialHi_800E400C}
  // refs/melee/src/melee/ft/fighter.c (fp->deal_dmg_cb under fp->dmg.x1914)
  if (batch == NULL || batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_FALCON ||
      batch->state.action_id[a_idx] != (uint16_t)FC_ACT_SPECIAL_LW) {
    return;
  }
  if (batch->state.falcon_speciallw_dealt_x1914_frame[a_idx] != 0u) {
    return;
  }
  batch->state.falcon_speciallw_dealt_x1914_frame[a_idx] = 1u;
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[a_idx]);
  if (ch == NULL) {
    return;
  }
  if ((int32_t)batch->state.falcon_speciallw_hits[a_idx] <= ch->falcon_speciallw_unk2) {
    batch->state.falcon_speciallw_hits[a_idx] =
        (uint8_t)(batch->state.falcon_speciallw_hits[a_idx] + 1u);
    batch->state.falcon_speciallw_friction[a_idx] =
        fc_speciallw_friction(batch, a_idx) * ch->falcon_speciallw_on_hit_spd_modifier;
  }
}

uint8_t falcon_special_try_speciallw_wall_rebound(MslBatch* batch, size_t idx) {
  // ftCa_SpecialLw_Coll rebound: while the script's cmd_vars[0] window is live (frame 15+), a
  // wall hug in the facing direction clears cmd/throw state, goes airborne (ftCommon_8007D5D4)
  // and enters SpecialHiThrow1 (the backflip; a Falcon Kick state despite the name). The block
  // runs after the grounded/air phase handling, so both phases of the travel action rebound.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Coll
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON ||
      batch->state.action_id[idx] != (uint16_t)FC_ACT_SPECIAL_LW) {
    return 0u;
  }
  const uint16_t msid = falcon_special_submotion((uint16_t)FC_ACT_SPECIAL_LW);
  const float frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  if (move_tables_special_cmd_var_u8_value_at_frame(batch->state.char_id[idx], msid, 0u, frame) ==
      0u) {
    return 0u;
  }
  const uint32_t env = batch->state.coll_env_flags[idx];
  const uint8_t facing_right = (batch->state.facing[idx] != 0u) ? 1u : 0u;
  const uint8_t wall_in_front =
      facing_right ? ((env & (uint32_t)MSL_COLLIDE_LEFT_WALL_HUG) != 0u ? 1u : 0u)
                   : ((env & (uint32_t)MSL_COLLIDE_RIGHT_WALL_HUG) != 0u ? 1u : 0u);
  if (!wall_in_front) {
    return 0u;
  }
  fc_reset_cmds(batch, idx);
  // ftCommon_8007D5D4: go airborne without a jump refresh (Coll-phase entry: the new state's
  // callbacks first run next frame, so no same-frame timebase tick).
  batch->state.on_ground[idx] = 0u;
  batch->state.ground_id[idx] = 0xFFFFu;
  batch->state.speed_air_x_self[idx] = batch->state.speed_ground_x_self[idx];
  batch->state.speed_ground_x_self[idx] = 0.0f;
  fc_enter(batch, idx, (uint16_t)FC_ACT_SPECIAL_HI_THROW1, 0.0f);
  return 1u;
}

// ---------------------------------------------------------------------------
// Entry dispatch (B-press routing)
// ---------------------------------------------------------------------------

// Grounded/aerial B-special admission per common action, mirroring the ftCo IASA dispatch
// chains. This mask logic is common-action-owned (identical decomp chains to the Marth port);
// see marth_specials.c::ms_b_entry_mask for the per-case decomp anchors.
enum {
  FC_B_SIDE = 1u << 0,
  FC_B_UP = 1u << 1,
  FC_B_NEUTRAL = 1u << 2,
  FC_B_DOWN = 1u << 3,
  FC_B_ALL = 0xFu,
};

static uint8_t fc_b_entry_mask(const MslBatch* batch, size_t idx, uint16_t a, uint8_t on_ground) {
  if (on_ground) {
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
        return FC_B_ALL;
      case MSL_ACT_LANDING: {
        const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
        const float cur = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
        return (ch != NULL && cur >= (float)ch->landing_lag_frames) ? (uint8_t)FC_B_ALL : 0u;
      }
      case MSL_ACT_SQUAT_WAIT:
      case MSL_ACT_SQUAT_RV:
        return (uint8_t)(FC_B_UP | FC_B_DOWN);
      case MSL_ACT_TURN:
        return (uint8_t)(FC_B_SIDE | FC_B_UP | FC_B_DOWN);
      case MSL_ACT_DASH:
        return (uint8_t)FC_B_SIDE;
      case MSL_ACT_KNEE_BEND:
        return (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_KNEE_BEND) ? (uint8_t)FC_B_UP
                                                                                 : 0u;
      case MSL_ACT_GUARD_OFF:
        return (batch->state.guard_special_enable_timer_x1c[idx] != 0u) ? (uint8_t)FC_B_ALL : 0u;
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
                   ? (uint8_t)FC_B_ALL
                   : 0u;
      case MSL_ACT_DAMAGE_HI_1:
      case MSL_ACT_DAMAGE_HI_1 + 1:
      case MSL_ACT_DAMAGE_HI_1 + 2:
      case MSL_ACT_DAMAGE_N_1:
      case MSL_ACT_DAMAGE_N_1 + 1:
      case MSL_ACT_DAMAGE_N_1 + 2:
      case MSL_ACT_DAMAGE_LW_1:
      case MSL_ACT_DAMAGE_LW_1 + 1:
      case MSL_ACT_DAMAGE_LW_1 + 2:
        return FC_B_ALL;
      case MSL_ACT_RUN_BRAKE:
        return (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN ||
                batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_RUN_DIRECT)
                   ? (uint8_t)FC_B_ALL
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
      return FC_B_ALL;
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_1 + 1:
    case MSL_ACT_DAMAGE_AIR_1 + 2:
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
      return FC_B_ALL;
    case MSL_ACT_PASSIVE_WALL:
    case MSL_ACT_PASSIVE_WALL_JUMP:
      return (batch->state.passivewall_timer[idx] != 0u) ? 0u : (uint8_t)FC_B_ALL;
    default:
      return 0u;
  }
}

// Direction resolution shared by the dispatcher paths. Only Neutral-B (Falcon Punch) enters
// today; Side/Up/Down zones are recognized so their inputs stay consumed-by-nothing (the sim
// must not fall through to a different special) until SpecialS/Hi/Lw port.
static uint8_t fc_resolve_and_enter(MslBatch* batch, const MslCommonParams* c,
                                    const MslCharParams* ch, size_t idx, uint8_t mask,
                                    uint8_t on_ground, uint8_t b_edge, uint8_t up_b_present) {
  const float sx =
      fc_apply_deadzone(fc_stick_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float sy =
      fc_apply_deadzone(fc_stick_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float ax = fabsf(sx);
  if (on_ground) {
    // Grounded chain order: SpecialS -> Attack100(up) -> D6824(neutral) -> D68C0(down).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
    if ((mask & FC_B_SIDE) != 0u && b_edge && ax >= c->special_stick_x_threshold_side) {
      return 0u;  // Raptor Boost: not ported yet
    }
    if ((mask & FC_B_UP) != 0u && up_b_present) {
      return 0u;  // Falcon Dive: not ported yet
    }
    if ((mask & FC_B_NEUTRAL) != 0u && b_edge && ax < c->special_stick_x_threshold_side &&
        sy < c->special_stick_y_threshold && sy > -c->special_stick_y_threshold) {
      fc_enter_specialn(batch, ch, idx, 1u);
      return 1u;
    }
    if ((mask & FC_B_DOWN) != 0u && b_edge && sy <= -c->special_stick_y_threshold) {
      fc_enter_speciallw(batch, ch, idx, 1u);
      return 1u;
    }
    return 0u;
  }
  // Aerial chain order: Up -> Down -> Side -> Neutral.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_SpecialAir.c::ftCo_SpecialAir_CheckInput
  if ((mask & FC_B_UP) != 0u && b_edge && sy >= c->special_stick_y_threshold) {
    return 0u;  // Falcon Dive: not ported yet
  }
  if ((mask & FC_B_DOWN) != 0u && b_edge && sy <= -c->special_stick_y_threshold) {
    fc_enter_speciallw(batch, ch, idx, 0u);
    return 1u;
  }
  if ((mask & FC_B_SIDE) != 0u && b_edge && ax >= c->special_stick_x_threshold_side) {
    return 0u;  // Raptor Boost: not ported yet
  }
  if ((mask & FC_B_NEUTRAL) != 0u && b_edge && ax < c->special_stick_x_threshold_side &&
      sy < c->special_stick_y_threshold) {
    fc_enter_specialn(batch, ch, idx, 0u);
    return 1u;
  }
  return 0u;
}

static uint8_t fc_try_enter_grounded_b_special_from_wait_iasa(MslBatch* batch,
                                                              const MslCommonParams* c,
                                                              const MslCharParams* ch, size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL || batch->state.on_ground[idx] == 0u ||
      batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u) {
    return 0u;
  }
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  const uint8_t b_edge = ((pressed & (uint16_t)MSL_BUTTON_B) != 0u) ? 1u : 0u;
  const uint8_t up_b_present = (batch->state.x686[idx] == 0u) ? 1u : 0u;
  if (!b_edge && !up_b_present) {
    return 0u;
  }
  const uint8_t mask = fc_b_entry_mask(batch, idx, (uint16_t)MSL_ACT_WAIT, 1u);
  if (mask == 0u) {
    return 0u;
  }
  return fc_resolve_and_enter(batch, c, ch, idx, mask, 1u, b_edge, up_b_present);
}

static uint8_t fc_try_run_grounded_wait_iasa_after_ft_8008A2BC(MslBatch* batch,
                                                               const MslCommonParams* c,
                                                               const MslCharParams* ch, size_t idx,
                                                               uint16_t source_action) {
  if (batch == NULL || c == NULL || ch == NULL ||
      batch->state.action_id[idx] != (uint16_t)MSL_ACT_WAIT || batch->state.on_ground[idx] == 0u) {
    return 0u;
  }

  const uint16_t buttons = batch->state.input_buttons[idx];
  const uint16_t buttons_pressed = batch->state.input_buttons_pressed[idx];
  const float stick_x =
      fc_apply_deadzone(fc_stick_unit(batch->state.input_main_x[idx]), c->lstick_deadzone_x);
  const float stick_y =
      fc_apply_deadzone(fc_stick_unit(batch->state.input_main_y[idx]), c->lstick_deadzone_y);
  const float facing_dir = fc_facing_dir(batch, idx);
  const uint8_t tilt_timer_x = batch->state.tilt_timer_x[idx];
  const uint8_t tilt_timer_y = batch->state.tilt_timer_y[idx];

  // Same source owner as the Marth/Sheik ports: ft_8008A2BC enters Wait through ft_8008A348 and
  // the destination Wait_IASA runs in the same Fighter_procUpdate pass, ordered
  // specials -> catch -> grounded attacks -> spotdodge-before-guard -> guard -> locomotion.
  // refs/melee/src/melee/ft/ft_0892.c::{ft_8008A2BC,ft_8008A348}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  if (fc_try_enter_grounded_b_special_from_wait_iasa(batch, c, ch, idx)) {
    return 1u;
  }
  if (grab_flow_try_enter_catch_from_iasa(batch, c, idx)) {
    return 1u;
  }
  if (locomotion_grounded_a_attack_try_enter_from_wait_iasa(batch, c, idx, buttons_pressed, stick_x,
                                                            stick_y, tilt_timer_x, tilt_timer_y,
                                                            facing_dir)) {
    return 1u;
  }
  if (wait_iasa_try_enter_spotdodge_before_guard_hsd_lr(batch, c, idx)) {
    return 1u;
  }
  guard_update_grounded(batch, c, idx, /*allow_entry=*/1u);
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_WAIT) {
    return 1u;
  }
  return locomotion_wait_iasa_locomotion_subset_try_enter(
      batch, c, ch, idx, buttons, buttons_pressed, stick_x, stick_y, tilt_timer_x, tilt_timer_y,
      facing_dir, source_action);
}

static uint8_t fc_aerial_up_b_uses_presence_gate(uint16_t action_id) {
  switch (action_id) {
    // Airborne Damage/DamageFly doIasa calls ftCo_800D69C4 (x686/x68B presence-owned Up-B).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::doIasa
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_1 + 1:
    case MSL_ACT_DAMAGE_AIR_1 + 2:
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
      return 1u;
    default:
      return 0u;
  }
}

static uint8_t fc_try_enter_air_b_special_from_fall_iasa(MslBatch* batch, const MslCommonParams* c,
                                                         const MslCharParams* ch, size_t idx) {
  if (batch == NULL || c == NULL || ch == NULL || batch->state.on_ground[idx] != 0u ||
      batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u) {
    return 0u;
  }
  const uint16_t pressed = batch->state.input_buttons_pressed[idx];
  const uint8_t b_edge = ((pressed & (uint16_t)MSL_BUTTON_B) != 0u) ? 1u : 0u;
  if (!b_edge) {
    return 0u;
  }
  const uint8_t mask = fc_b_entry_mask(batch, idx, (uint16_t)MSL_ACT_FALL, 0u);
  if (mask == 0u) {
    return 0u;
  }
  return fc_resolve_and_enter(batch, c, ch, idx, mask, 0u, b_edge, 0u);
}

void falcon_specials_update_pre_physics(MslBatch* batch) {
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
      if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON ||
          batch->state.stocks[idx] == 0u) {
        continue;
      }
      const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
      if (ch == NULL) {
        continue;
      }
      const uint16_t a = batch->state.action_id[idx];
      const uint8_t on_ground = batch->state.on_ground[idx] ? 1u : 0u;

      // deal_dmg_cb once-per-frame window: cleared here (action phase), set by combat later in
      // the same frame (Fighter_ProcessHit fires the cb once per frame with dealt damage).
      batch->state.falcon_speciallw_dealt_x1914_frame[idx] = 0u;

      if (falcon_action_is_special(a)) {
        fc_update_player(batch, c, ch, idx);
        continue;
      }

      if (batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 0u) {
        continue;
      }
      const uint16_t pressed = batch->state.input_buttons_pressed[idx];
      const uint8_t b_edge = ((pressed & (uint16_t)MSL_BUTTON_B) != 0u) ? 1u : 0u;
      const uint8_t up_b_present = (batch->state.x686[idx] == 0u) ? 1u : 0u;
      const uint8_t air_up_b_fresh =
          (uint8_t)(up_b_present && batch->state.x68B[idx] >= c->tech_lr_debounce_frames);
      const uint8_t air_up_b_presence_gate =
          (uint8_t)((!on_ground) && fc_aerial_up_b_uses_presence_gate(a));
      if (on_ground) {
        if (!b_edge && !up_b_present) {
          continue;
        }
      } else if (!b_edge && !(air_up_b_presence_gate && up_b_present)) {
        continue;
      }
      const uint8_t mask = fc_b_entry_mask(batch, idx, a, on_ground);
      if (mask == 0u) {
        continue;
      }
      if (on_ground) {
        (void)fc_resolve_and_enter(batch, c, ch, idx, mask, 1u, b_edge, up_b_present);
      } else {
        // The presence-gated aerial Up-B path only matters for the (unported) Falcon Dive;
        // keep the ordinary aerial resolver, which requires a fresh B edge for all zones.
        (void)air_up_b_fresh;
        (void)fc_resolve_and_enter(batch, c, ch, idx, mask, 0u, b_edge, 0u);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Ground <-> air variant swaps (collision callbacks; preserve animation frame)
// ---------------------------------------------------------------------------

// Decomp collision handling per family:
// - Falcon Punch: frame-preserving swap 347 <-> 348 (ftCa_SpecialN_Coll / ftCa_SpecialAirN_Coll).
// - Falcon Kick travel/end (357/358/362): SAME-action ground<->air phase flips
//   (ftCommon_8007D5D4 / ftCommon_8007D7FC inside ftCa_SpecialLw{,End,EndAir}_Coll) — the
//   handlers below return 1 without changing the action so the caller applies the bundle.
// - Air kick + backflip descent (359/361): landing enters SpecialAirLwEnd at frame 0 with the
//   speciallw_landing_lag_mul anim rate (ftCa_SpecialAirLw_Coll / ftCa_SpecialAirLwEndAir_Coll
//   doColl). SpecialHiThrow1 landing uses the generic ftCo_AirCatchHit_Coll basic-Landing path.
static uint16_t falcon_special_air_variant(uint16_t a) {
  if (a == (uint16_t)FC_ACT_SPECIAL_N) {
    return (uint16_t)FC_ACT_SPECIAL_AIR_N;
  }
  return 0u;
}

static uint16_t falcon_special_ground_variant(uint16_t a) {
  if (a == (uint16_t)FC_ACT_SPECIAL_AIR_N) {
    return (uint16_t)FC_ACT_SPECIAL_N;
  }
  return 0u;
}

static inline uint8_t falcon_speciallw_action_phase_flips(uint16_t a) {
  return (uint8_t)(a == (uint16_t)FC_ACT_SPECIAL_LW || a == (uint16_t)FC_ACT_SPECIAL_LW_END ||
                   a == (uint16_t)FC_ACT_SPECIAL_LW_END_AIR);
}

static void fc_swap_preserving_frame(MslBatch* batch, size_t idx, uint16_t next_action) {
  const float cur = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  batch->state.action_id[idx] = next_action;
  batch->state.animation_index[idx] = (uint32_t)falcon_special_submotion(next_action);
  msl_anim_timebase_enter(batch, idx, cur, 1.0f);
  // ChangeMotionState without Ft_MF_Unk24 clears fp->x221C_u16_y; opcode-52 levels whose source
  // event is at or before the preserved entry frame stay cleared until the script crosses its
  // next event (state_flags.c consumer). The grounded punch script has set_state_flags events
  // at frames 52/77.
  batch->state.x221c_y_event_floor[idx] = (uint16_t)(msl_anim_frame_floor_u16(cur) + 1u);
}

uint8_t falcon_special_try_air_to_ground_swap(MslBatch* batch, size_t idx) {
  if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (falcon_speciallw_action_phase_flips(a)) {
    // Falcon Kick travel/end landing: ftCommon_8007D7FC phase flip only — same action, same
    // frame; the caller's grounding bundle applies.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
    //   ftCa_SpecialLw_Coll,ftCa_SpecialLwEnd_Coll,ftCa_SpecialLwEndAir_Coll}
    return 1u;
  }
  if (a == (uint16_t)FC_ACT_SPECIAL_AIR_LW || a == (uint16_t)FC_ACT_SPECIAL_AIR_LW_END_AIR) {
    // Air kick / backflip-descent landing: clear cmds + SpecialAirLwEnd from frame 0 at the
    // landing-lag anim rate (doColl).
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
    //   ftCa_SpecialAirLw_Coll,ftCa_SpecialAirLwEndAir_Coll}
    const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
    fc_reset_cmds(batch, idx);
    fc_enter_rate(batch, idx, (uint16_t)FC_ACT_SPECIAL_AIR_LW_END, 0.0f,
                  (ch != NULL && ch->falcon_speciallw_landing_lag_mul > 0.0f)
                      ? ch->falcon_speciallw_landing_lag_mul
                      : 1.0f);
    return 1u;
  }
  const uint16_t next = falcon_special_ground_variant(a);
  if (next == 0u) {
    return 0u;
  }
  // ftCa_SpecialAirN_Coll: ft_80081D0C ground contact -> ftCommon_8007D7FC + grounded variant
  // at the preserved frame. The grounding bundle (gr_vel sync, jumps refresh) is caller-owned.
  // The special_cmd0 impulse-consumed latch intentionally survives (Ft_MF_UpdateCmd carries
  // cmd state).
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialAirN_Coll
  fc_swap_preserving_frame(batch, idx, next);
  return 1u;
}

uint8_t falcon_special_try_ground_to_air_swap(MslBatch* batch, size_t idx) {
  if (batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_FALCON) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (falcon_speciallw_action_phase_flips(a)) {
    // Falcon Kick travel/end floor loss: ftCommon_8007D5D4 phase flip only — same action,
    // same frame; the caller syncs speed_air from gr_vel.
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::{
    //   ftCa_SpecialLw_Coll,ftCa_SpecialLwEnd_Coll,ftCa_SpecialLwEndAir_Coll}
    return 1u;
  }
  const uint16_t next = falcon_special_air_variant(a);
  if (next == 0u) {
    return 0u;
  }
  // ftCa_SpecialN_Coll: floor loss (!ft_800827A0) -> ftCommon_8007D5D4 + aerial variant at the
  // preserved frame + ftCommon_ClampAirDrift (caller-owned floor-loss bundle).
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialN.c::ftCa_SpecialN_Coll
  fc_swap_preserving_frame(batch, idx, next);
  return 1u;
}
