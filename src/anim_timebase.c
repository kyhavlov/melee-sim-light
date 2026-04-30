#include "anim_timebase.h"

#include <math.h>
#include <stddef.h>

#include "action_ids.h"
#include "anim_table.h"
#include "attack_id_tables.h"
#include "buttons.h"
#include "char_params.h"
#include "combat.h"
#include "common_params.h"
#include "motion_state_owners.h"
#include "move_tables.h"

enum { Ft_MF_KeepFastFall = 1 << 0 };

static inline uint8_t anim_timebase_try_rebound_anim_speed_from_ground_vel(const MslCommonParams* c,
                                                                           const MslCharParams* ch,
                                                                           float ground_speed_x,
                                                                           float* out_rate) {
  if (c == NULL || ch == NULL || out_rate == NULL) {
    return 0u;
  }
  const float rebound_speed_abs = fabsf(ground_speed_x);
  if (!(c->rebound_ground_x0_mul > 0.0f) || rebound_speed_abs <= c->rebound_ground_x0_base) {
    return 0u;
  }
  const float rebound_x191c =
      (rebound_speed_abs - c->rebound_ground_x0_base) / c->rebound_ground_x0_mul;
  if (!(rebound_x191c > 0.0f)) {
    return 0u;
  }
  // Rebound anim-speed ownership:
  // - ftCo_80099D9C stores `mv.co.rebound.anim_start = (fp->co_attrs.x9C + 0.1f) / fp->dmg.x191C`.
  // - ReboundStop_Anim immediately enters Rebound, and Rebound's first timeline advance should
  //   already use that callback-owned rate on frame-0 seeds.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{ftCo_80099D9C,ftCo_80099E44}
  // refs/melee/src/melee/ft/ftcoll.c::{inlineA0,inlineA1}
  *out_rate = (ch->rebound_anim_numerator_frames + 0.1f) / rebound_x191c;
  return 1u;
}

static inline int anim_timebase_throw_index_from_action(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_THROW_F:
      return 0;
    case (uint16_t)MSL_ACT_THROW_B:
      return 1;
    case (uint16_t)MSL_ACT_THROW_HI:
      return 2;
    case (uint16_t)MSL_ACT_THROW_LW:
      return 3;
    default:
      return -1;
  }
}

static inline uint16_t anim_timebase_throw_action_from_thrown(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_THROWN_F:
      return (uint16_t)MSL_ACT_THROW_F;
    case (uint16_t)MSL_ACT_THROWN_B:
      return (uint16_t)MSL_ACT_THROW_B;
    case (uint16_t)MSL_ACT_THROWN_HI:
      return (uint16_t)MSL_ACT_THROW_HI;
    case (uint16_t)MSL_ACT_THROWN_LW:
    case (uint16_t)MSL_ACT_THROWN_LW_WOMEN:
      return (uint16_t)MSL_ACT_THROW_LW;
    default:
      return 0xFFFFu;
  }
}

static inline int32_t anim_timebase_throw_rate_fp_from_pair(uint8_t owner_char_id,
                                                            uint8_t victim_char_id,
                                                            uint16_t throw_action) {
  const int throw_index = anim_timebase_throw_index_from_action(throw_action);
  if (throw_index < 0) {
    return 0;
  }
  const MslCommonParams* c = msl_common_params();
  const MslCharParams* owner_ch = msl_char_params(owner_char_id);
  const MslCharParams* victim_ch = msl_char_params(victim_char_id);
  if (owner_ch == NULL || victim_ch == NULL || c == NULL) {
    return 0;
  }
  float rate = 1.0f;
  if ((owner_ch->weight_independent_throws_mask & (uint8_t)(1u << throw_index)) == 0u) {
    if (!(victim_ch->weight > 0.0f) || !(c->throw_anim_speed_weight_mul > 0.0f)) {
      return 0;
    }
    rate = 1.0f / (victim_ch->weight * c->throw_anim_speed_weight_mul);
  }
  if (!(rate > 0.0f)) {
    return 0;
  }
  return msl_q16_16_from_f32(rate);
}

static inline uint8_t anim_timebase_attached_non_low_throw_pair_active(const MslBatch* batch,
                                                                       int bi, int p, size_t idx,
                                                                       int num_players) {
  if (batch == NULL || batch->state.throw_anim_rate_fp_q16_16[idx] <= 0) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (a == (uint16_t)MSL_ACT_THROW_B || a == (uint16_t)MSL_ACT_THROW_HI) {
    const uint8_t victim_p = batch->state.attached_victim_port[idx];
    if (victim_p == 0xFFu || (int)victim_p >= num_players || (int)victim_p == p) {
      return 0u;
    }
    const size_t vidx = msl_idx_player(bi, (int)victim_p);
    return (batch->state.action_id[vidx] == (uint16_t)(a + 20u) &&
            batch->state.grab_owner_port[vidx] == (uint8_t)p)
               ? 1u
               : 0u;
  }
  if (a == (uint16_t)MSL_ACT_THROWN_B || a == (uint16_t)MSL_ACT_THROWN_HI) {
    uint8_t owner_p = batch->state.grab_owner_port[idx];
    if (owner_p == 0xFFu || (int)owner_p >= num_players || (int)owner_p == p) {
      for (int candidate = 0; candidate < num_players; candidate++) {
        const size_t cidx = msl_idx_player(bi, candidate);
        if (batch->state.attached_victim_port[cidx] == (uint8_t)p) {
          owner_p = (uint8_t)candidate;
          break;
        }
      }
    }
    if (owner_p == 0xFFu || (int)owner_p >= num_players || (int)owner_p == p) {
      return 0u;
    }
    const size_t oidx = msl_idx_player(bi, (int)owner_p);
    const uint16_t owner_action = anim_timebase_throw_action_from_thrown(a);
    return (owner_action != 0xFFFFu && batch->state.action_id[oidx] == owner_action &&
            batch->state.attached_victim_port[oidx] == (uint8_t)p)
               ? 1u
               : 0u;
  }
  return 0u;
}

static inline int32_t anim_timebase_non_low_throw_rate_snap_delta(const MslBatch* batch, int bi,
                                                                  int p, size_t idx,
                                                                  int num_players, int32_t cur_fp,
                                                                  int32_t rem) {
  if (anim_timebase_attached_non_low_throw_pair_active(batch, bi, p, idx, num_players)) {
    return (rem == (int32_t)MSL_Q16_16_ONE - 1) ? 1 : 0;
  }
  if (batch == NULL || batch->state.throw_anim_rate_fp_q16_16[idx] <= 0) {
    return 0;
  }
  const uint16_t action = batch->state.action_id[idx];
  if (action != (uint16_t)MSL_ACT_THROW_B && action != (uint16_t)MSL_ACT_THROW_HI) {
    return 0;
  }
  if (rem < (int32_t)MSL_Q16_16_ONE - 8) {
    return 0;
  }
  // Post-release thrower rate lifetime:
  // - ftCo_800DD398 enters ThrowB/ThrowHi with the victim-weight throw anim rate.
  // - ftCo_800DD724 consuming set_throw_flags(0) detaches/applies the throw hit, but it does not
  //   call ftAnim_SetAnimRate(1.0f). The thrower keeps the same AObj rate until a later callback or
  //   command-owned freeze changes it.
  // - The stored throw rate is carried only from a source-owned attached episode. Normal one-step
  //   reseed rows after detach have this lane cleared, so this cannot become a broad post-release
  //   replay shortcut. The 8-LSB bound is a fixed-point representation guard for repeated 4/3-ish
  //   throw rates; it is applied only at extracted projectile/throw events or after the extracted
  //   projectile pulse family has completed and the throw script remains in an active command band.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{
  //   ftCo_800DD398,ftCo_800DD724,fn_800DD568,fn_800DD5EC
  // }
  if (batch->state.frame_speed_mul_fp_q16_16[idx] != batch->state.throw_anim_rate_fp_q16_16[idx]) {
    return 0;
  }
  const int32_t delta_to_integer = (int32_t)MSL_Q16_16_ONE - rem;
  if (delta_to_integer <= 0 || delta_to_integer > 8) {
    return 0;
  }
  const int32_t prev_fp = cur_fp - batch->state.frame_speed_mul_fp_q16_16[idx];
  const float prev_frame = msl_f32_from_q16_16(prev_fp);
  const float snapped_frame = msl_f32_from_q16_16(cur_fp + delta_to_integer);
  const uint8_t char_id = batch->state.char_id[idx];
  if (move_tables_throw_should_flip_facing(char_id, action, prev_frame, snapped_frame)) {
    return delta_to_integer;
  }
  if (move_tables_throw_release_hit_idx(char_id, action, snapped_frame, NULL) &&
      !move_tables_throw_release_hit_idx(char_id, action, prev_frame, NULL)) {
    return delta_to_integer;
  }
  int16_t crossed_pulse_af = -1;
  if (move_tables_throw_crossed_projectile_pulse_frame(char_id, action, prev_frame, snapped_frame,
                                                       &crossed_pulse_af)) {
    uint8_t pulse_ordinal = 0u;
    const uint8_t has_ordinal = move_tables_throw_projectile_pulse_ordinal(
        char_id, action, crossed_pulse_af, &pulse_ordinal);
    if (action == (uint16_t)MSL_ACT_THROW_HI && has_ordinal && pulse_ordinal == 2u) {
      const uint8_t source_port0 = batch->state.source_port0[idx];
      const uint8_t prior_pulse_frame = batch->state.throw_pulse_crossed_prev_frame[idx];
      uint8_t first_pulse_hit_provenance_active = 0u;
      if (prior_pulse_frame != 0u && (int16_t)prior_pulse_frame < crossed_pulse_af) {
        for (int vp = 0; vp < num_players; vp++) {
          if (vp == p) {
            continue;
          }
          const size_t v_idx = msl_idx_player(bi, vp);
          if (batch->state.hitstun[v_idx] > 0u && batch->state.last_hit_by[v_idx] == source_port0 &&
              batch->state.instance_hit_by[v_idx] == batch->state.instance_id[idx]) {
            first_pulse_hit_provenance_active = 1u;
            break;
          }
        }
      }
      if (first_pulse_hit_provenance_active) {
        // ThrowHi mirror mid-pulse callback boundary:
        // - Existing source-shaped item logic keeps frame-20 rows from serializing the second
        //   article one callback early when the first state1 shot is already carrying victim
        //   hitstun/source provenance from this thrower's raw source port and current instance,
        //   and the previous replay step already recorded an earlier ThrowHi pulse crossing.
        // - Preserve that boundary here too; the fixed-point snap must not manufacture an earlier
        //   command crossing than ftAction/ftFx_Throw_Anim exposes.
        // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        return 0;
      }
    }
    return delta_to_integer;
  }
  int16_t last_pulse_af = -1;
  const uint8_t past_last_projectile_pulse =
      (move_tables_throw_projectile_last_pulse_frame(char_id, action, &last_pulse_af) &&
       prev_frame > (float)last_pulse_af)
          ? 1u
          : 0u;
  if (past_last_projectile_pulse && move_tables_throw_cmd1_active(char_id, action, snapped_frame)) {
    return delta_to_integer;
  }
  return 0;
}

static inline uint8_t anim_timebase_apply_aobj_loop(MslBatch* batch, size_t idx) {
  // Fighter AObj loop semantics (AOBJ_LOOP) apply a deterministic rewind+wrap when
  // `end_frame <= curr_frame`.
  //
  // Decomp:
  // - Motion-state animation init sets AOBJ_LOOP when fp->x594_b1_loop is set.
  //   refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBE8
  // - AOBJ_LOOP wrap is implemented by sysdolphin's HSD_AObjInterpretAnim.
  //   refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
  //
  // We model the wrapped `curr_frame` on the sim's deterministic Q16.16 timebase so that Slippi
  // `state_age`/`action_frame` match on looping locomotion timelines (e.g. Run).
  if (batch == NULL) {
    return 0;
  }
  const uint32_t anim_u32 = batch->state.animation_index[idx];
  if (anim_u32 > 0xFFFFu) {
    return 0;
  }
  const uint16_t smid = (uint16_t)anim_u32;
  if (!msl_anim_is_looping(batch->state.char_id[idx], smid)) {
    return 0;
  }
  const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], smid);
  if (!(end_frame > 0.0f)) {
    return 0;
  }
  const int32_t end_fp = msl_q16_16_from_f32(end_frame);
  if (end_fp <= 0) {
    return 0;
  }
  int32_t cur_fp = batch->state.anim_frame_fp_q16_16[idx];
  if (cur_fp < 0) {
    return 0;
  }
  if (cur_fp >= end_fp) {
    // Deterministic modulo wrap. Decomp: HSD_AObjLoadDesc sets rewind_frame=0.0F by default.
    // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjLoadDesc
    cur_fp = cur_fp % end_fp;
    batch->state.anim_frame_fp_q16_16[idx] = cur_fp;
    return 1;
  }
  return 0;
}

static inline void anim_timebase_apply_capture_loop(MslBatch* batch, size_t idx) {
  // CapturePulled*/CaptureWait*/CaptureDamage* victims use looping HSD AObj timelines in the suite
  // (most notably the CaptureDamage* loop with end_frame=20). Vanilla wraps `cur_anim_frame` under
  // the AObj's loop mode during ftAnim_8006EBA4 / HSD_AObjInterpretAnim.
  //
  // Decomp tie-down: these motion states consult their AObj timebase via fp->cur_anim_frame and
  // read bone world translations during Phys (lb_8000B1CC), so the wrapped timebase affects the
  // per-frame capture delta in ftCo_Attack100.c::fn_800DAD18.
  //
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::fn_800DAD18
  if (batch == NULL) {
    return;
  }
  if (!msl_action_is_capture_pulled_wait_damage_victim(batch->state.action_id[idx])) {
    return;
  }
  const uint32_t anim_u32 = batch->state.animation_index[idx];
  if (anim_u32 > 0xFFFFu) {
    return;
  }
  const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)anim_u32);
  if (!(end_frame > 0.0f)) {
    return;
  }
  const int32_t end_fp = msl_q16_16_from_f32(end_frame);
  if (end_fp <= 0) {
    return;
  }
  int32_t cur_fp = batch->state.anim_frame_fp_q16_16[idx];
  if (cur_fp < 0) {
    return;
  }
  if (cur_fp >= end_fp) {
    // Deterministic modulo wrap (Q16.16), matching the observed Slippi `state_age` wrap behavior on
    // looping capture timelines.
    cur_fp = cur_fp % end_fp;
    batch->state.anim_frame_fp_q16_16[idx] = cur_fp;
  }
}

static inline uint8_t anim_timebase_is_attackair(uint16_t a) {
  // Generated from decomp MotionState callback symbols ftCo_AttackAir_* for the five common
  // aerial attacks.
  // refs/melee/src/melee/ft/ftmotionstates.c::ftData_MotionStateList
  return msl_motion_state_common_class_has(a, MSL_MS_CLASS_ATTACK_AIR);
}

static inline uint8_t anim_timebase_is_walk(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_WALK_SLOW || a == (uint16_t)MSL_ACT_WALK_MIDDLE ||
          a == (uint16_t)MSL_ACT_WALK_FAST)
             ? 1u
             : 0u;
}

static inline uint16_t anim_timebase_walk_action_from_speed(const MslCommonParams* c,
                                                            const MslCharParams* ch, float gr_vel) {
  if (c == NULL || ch == NULL) {
    return (uint16_t)MSL_ACT_WALK_SLOW;
  }
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_GetWalkType
  const float v = fabsf(gr_vel);
  if (v >= (c->walk_fast_vel_mul * ch->walk_max_vel)) {
    return (uint16_t)MSL_ACT_WALK_FAST;
  }
  if (v >= (c->walk_mid_vel_mul * ch->walk_max_vel)) {
    return (uint16_t)MSL_ACT_WALK_MIDDLE;
  }
  return (uint16_t)MSL_ACT_WALK_SLOW;
}

static inline uint8_t anim_timebase_try_walk_rate_from_source_vel(uint16_t a,
                                                                  const MslCharParams* ch,
                                                                  float walk_anim_source_vel,
                                                                  int8_t facing_dir1,
                                                                  float* out_rate) {
  if (ch == NULL || out_rate == NULL) {
    return 0u;
  }

  float denom = 0.0f;
  switch (a) {
    case (uint16_t)MSL_ACT_WALK_SLOW:
      denom = ch->slow_walk_max;
      break;
    case (uint16_t)MSL_ACT_WALK_MIDDLE:
      denom = ch->mid_walk_point;
      break;
    case (uint16_t)MSL_ACT_WALK_FAST:
      denom = ch->fast_walk_min;
      break;
    default:
      return 0u;
  }
  if (!(denom > 0.0f)) {
    return 0u;
  }

  const float facing_dir = (facing_dir1 < 0) ? -1.0f : 1.0f;
  const float mv_x0 = walk_anim_source_vel;

  // Decomp: ftWalkCommon_800DFDDC sets walk anim_rate from motion velocity:
  // - if mv_x0 * facing_dir <= 0, anim_rate = 0,
  // - else anim_rate = ABS(mv_x0) / {slow_walk_max, mid_walk_point, fast_walk_min}.
  // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
  // refs/melee/src/melee/ft/types.h::ftCo_DatAttrs
  if (mv_x0 * facing_dir <= 0.0f) {
    *out_rate = 0.0f;
  } else {
    *out_rate = fabsf(mv_x0) / denom;
  }

  return 1u;
}

static inline uint8_t anim_timebase_try_run_rate_from_source_vel(const MslCharParams* ch,
                                                                 float run_anim_source_vel,
                                                                 int8_t facing_dir1,
                                                                 float* out_rate) {
  if (ch == NULL || out_rate == NULL || !(ch->run_animation_scaling > 0.0f)) {
    return 0u;
  }
  const float facing_dir = (facing_dir1 < 0) ? -1.0f : 1.0f;
  const float vel = run_anim_source_vel;
  // Decomp: ftCo_Run_Anim sets anim_rate=0 when `vel` is not forward-facing, otherwise
  // ABS(vel) / run_animation_scaling.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
  if (vel * facing_dir <= 0.0f) {
    *out_rate = 0.0f;
  } else {
    *out_rate = fabsf(vel) / ch->run_animation_scaling;
  }
  return 1u;
}

static inline uint8_t anim_timebase_try_landing_air_rate(uint16_t a, const MslCharParams* ch,
                                                         const MslCommonParams* c, uint8_t char_id,
                                                         uint8_t lr_press_timer, uint8_t l_cancel,
                                                         float* out_rate) {
  if (ch == NULL || c == NULL || out_rate == NULL) {
    return 0;
  }

  uint16_t smid = 0;
  uint8_t lag_frames = 0;
  switch (a) {
    case MSL_ACT_LANDING_AIR_N:
      smid = (uint16_t)MSL_SM_LANDING_AIR_N;
      lag_frames = ch->landing_airn_lag_frames;
      break;
    case MSL_ACT_LANDING_AIR_F:
      smid = (uint16_t)MSL_SM_LANDING_AIR_F;
      lag_frames = ch->landing_airf_lag_frames;
      break;
    case MSL_ACT_LANDING_AIR_B:
      smid = (uint16_t)MSL_SM_LANDING_AIR_B;
      lag_frames = ch->landing_airb_lag_frames;
      break;
    case MSL_ACT_LANDING_AIR_HI:
      smid = (uint16_t)MSL_SM_LANDING_AIR_HI;
      lag_frames = ch->landing_airhi_lag_frames;
      break;
    case MSL_ACT_LANDING_AIR_LW:
      smid = (uint16_t)MSL_SM_LANDING_AIR_LW;
      lag_frames = ch->landing_airlw_lag_frames;
      break;
    default:
      return 0;
  }

  float lag = (float)lag_frames;
  // Decomp: LandingAir lag is divided (integer truncation, min 1) when fp->x67F < p_ftCommonData->xE4.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithLag
  //
  // Seed-bridge note:
  // - Slippi does not expose fp->x67F directly.
  // - Post-frame `l_cancel==1` is emitted by the exact same x67F < xE4 check on LandingAir* entry.
  //   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // So on entry-shaped reseeds we treat `l_cancel==1` as authoritative for the divide branch.
  const uint8_t did_lcancel =
      (lr_press_timer < c->lcancel_window_frames || l_cancel == 1u) ? 1u : 0u;
  if (lag > 0.0f && did_lcancel) {
    const float div_lag = lag / c->lcancel_lag_div;
    int int_lag = (int)div_lag;
    if (int_lag == 0) {
      int_lag = 1;
    }
    lag = (float)int_lag;
  }

  if (!(lag > 0.0f)) {
    return 0;
  }
  const float end_frame = msl_anim_end_frame(char_id, smid);
  if (!(end_frame > 0.0f)) {
    return 0;
  }
  // Decomp: ftAnim_SetAnimRate((ftAnim_8006F484(gobj) + 0.1f) / lag).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::ftCo_LandingAir_EnterWithMsidLag
  *out_rate = (end_frame + 0.1f) / lag;
  return 1;
}

void anim_timebase_update_pre_input(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t a = batch->state.action_id[idx];

      // Hitlag freezes animation advancement (decomp gate is fp->x2219_b5).
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      if (batch->state.hitlag_started_frame[idx] != 0) {
        // Keep derived fields coherent even when frozen.
        msl_anim_timebase_recompute_derived(batch, idx);
        continue;
      }

      // Decomp: Run animation rate is scaled from current ground velocity:
      // `ftAnim_SetAnimRate(fp, ABS(fp->gr_vel) / fp->co_attrs.run_animation_scaling)`.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
      // refs/melee/src/melee/ft/ftanim.c::ftAnim_SetAnimRate
      //
      // Source of truth for the per-character scaling:
      // - ISO-extracted `data/characters/{fox,falco}.json` `run_animation_scaling`.
      const int16_t action_frame_pre = batch->state.action_frame[idx];
      const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);

      // PassiveWall / PassiveWallJump startup hold:
      // - ftCo_800C1E64 seeds `fp->mv.co.passivewall.timer = p_ftCommonData->x760`.
      // - ftCo_PassiveWall_Anim decrements that hidden timer each non-hitlag frame and keeps the
      //   animation frozen until it reaches 0, at which point motion/anim advance begins.
      // - Replay-visible action_frame stays at 0 through the frozen startup, so action_frame alone
      //   cannot distinguish "still held" from "ready to launch".
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{ftCo_800C1E64,ftCo_PassiveWall_Anim}
      if ((a == (uint16_t)MSL_ACT_PASSIVE_WALL || a == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP) &&
          batch->state.passivewall_timer[idx] != 0u) {
        batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
      } else if ((a == (uint16_t)MSL_ACT_PASSIVE_WALL ||
                  a == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP) &&
                 action_frame_pre == 0) {
        // First post-hold PassiveWall frame:
        // - when ftCo_PassiveWall_Anim decrements timer->0, it either calls inlineA0
        //   (ChangeMotionState(..., anim_speed=1)) or ftAnim_SetAnimRate(gobj, 1).
        // - the next seeded timer==0 / action_frame==0 snapshot therefore advances at rate 1.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::{inlineA0,ftCo_PassiveWall_Anim}
        batch->state.frame_speed_mul_fp_q16_16[idx] = MSL_Q16_16_ONE;
      }

      // Decomp: Walk Anim callback (ftCo_Walk_Anim -> ftWalkCommon_800DFDDC) updates anim rate
      // from current walk velocity and facing.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Anim
      // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
      //
      // Entry ordering (not a magic threshold):
      // - Walk entry uses Fighter_ChangeMotionState(..., anim_start=0, anim_speed=1).
      // - The first ftAnim tick (0->1) happens before Walk_Anim writes the velocity-scaled rate.
      // - Therefore the entry frame (`action_frame==0`) must advance at 1.0; scaled walk rate
      //   applies from the next steady frame onward.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Anim
      if (anim_timebase_is_walk(a)) {
        // Entry-only guard: apply 1.0 only on true walk motion-state entry.
        const uint8_t walk_entry = (batch->state.prev_action_id[idx] != a) ? 1u : 0u;
        if (walk_entry) {
          batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(1.0f);
          batch->state.walk_anim_source_vel[idx] = batch->state.speed_ground_x_self[idx];
        } else if (action_frame_pre == 1) {
          // Decomp ordering bridge for first steady walk frame:
          // - Walk enter uses ChangeMotionState(..., anim_speed=1) then immediate ftAnim tick.
          // - Walk_Anim (ftWalkCommon_800DFDDC) writes the velocity-scaled rate after that tick,
          //   for the *next* frame's advance.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Enter
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Anim
          // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
          //
          // Keep the seeded/source 1.0 entry carry here; applying the scaled walk rate one frame
          // early at action_frame==1 shifts Walk timebase ownership. Runtime rollouts promote the
          // frame-start source action into seed_prev_action_id, which preserves this same
          // first-steady Walk entry owner after a same-frame Damage_IASA/Wait_IASA walk enter. A
          // zero carry at action_frame==1 is also an entry-clamp artifact, not a valid Walk_Anim
          // rate: source entry used anim_speed=1 and Walk_Anim has not yet supplied the consumed
          // rate for this tick.
          if (batch->state.seed_prev_action_id[idx] != a ||
              batch->state.frame_speed_mul_fp_q16_16[idx] == 0) {
            batch->state.frame_speed_mul_fp_q16_16[idx] = MSL_Q16_16_ONE;
          }
        } else {
          // Walk callback-source ownership:
          // - ftCo_Walk_Anim runs after ftAnim advance and writes the rate used by the next frame.
          // - ftWalkCommon_800DFDDC selects `mv_x0` from either `fp->mv.co.walk.x0` or `fp->gr_vel`
          //   before converting it to `frame_speed_mul`.
          // - Under one-step reseed, Slippi exposes the post-callback rate but not that hidden
          //   source, so `walk_anim_source_vel` is the minimum explicit carry for the same owner.
          // - Walk type-change rows have one more hidden branch (`ft_GetGroundFrictionMultiplier`)
          //   deciding whether this tick consumes hidden `mv.co.walk.x0` or current `gr_vel`; the
          //   narrow retarget lane carries that source only for those replay-reseeded rows.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Anim
          // refs/melee/src/melee/ft/ftwalkcommon.c::{ftWalkCommon_800DFDDC,ftWalkCommon_800DFEC8}
          float source_vel = batch->state.walk_anim_source_vel[idx];
          const uint16_t speed_walk_action =
              anim_timebase_walk_action_from_speed(c, ch, batch->state.speed_ground_x_self[idx]);
          if (speed_walk_action != a && batch->state.walk_retarget_tick_source_vel[idx] != 0.0f) {
            source_vel = batch->state.walk_retarget_tick_source_vel[idx];
          }
          float walk_rate = 0.0f;
          if (anim_timebase_try_walk_rate_from_source_vel(
                  a, ch, source_vel, batch->state.facing_dir1[idx], &walk_rate)) {
            batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(walk_rate);
          }
        }
      }

      // Run callback-source ownership:
      // - ftCo_Run_Anim runs after ftAnim advance and writes the rate used by the next frame.
      // - Under one-step reseed, Slippi exposes that post-callback rate one row after the anim tick
      //   that consumed it, so `run_anim_source_vel` is the explicit replay-facing hidden-owner lane.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Anim
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunDirect.c::ftCo_RunDirect_Anim
      if ((a == (uint16_t)MSL_ACT_RUN || a == (uint16_t)MSL_ACT_RUN_DIRECT) &&
          action_frame_pre > 0) {
        float source_vel = batch->state.run_anim_source_vel[idx];
        if (source_vel != 0.0f) {
          float rate = 0.0f;
          if (anim_timebase_try_run_rate_from_source_vel(ch, source_vel,
                                                         batch->state.facing_dir1[idx], &rate)) {
            batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(rate);
          }
        }
      }

      // Grounded smash early-hold replay bridge:
      // - The live current-sim owner is opcode 56 / `smash_attrs` in input.c
      //   (`ftAction_80073008` -> `ftCo_800DEE84` / `ftCo_800DF0D0`).
      // - Teacher-forced reseed can still start mid-hold on AttackHi4/AttackLw4 af=2 rows without
      //   the prior frame's live smash_attrs lifecycle, so keep this narrow replay-real bridge for
      //   those seeded rows.
      // - Decomp input ownership: `fp->input.x668` is updated in Fighter_procUpdate (prio3), so
      //   prio1 Anim callbacks use prior-frame input state.
      // refs/melee/src/melee/ft/ftattacks4combo.c::ftCo_800CECE8
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80073008
      // refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEE84,ftCo_800DF0D0}
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackHi4.c,ftCo_AttackLw4.c}
      // data/moves/{fox,falco}.json::ftCo_SM_Attack{Hi4,Lw4}
      if ((a == (uint16_t)MSL_ACT_ATTACK_S4_S || a == (uint16_t)MSL_ACT_ATTACK_HI4 ||
           a == (uint16_t)MSL_ACT_ATTACK_LW4) &&
          batch->state.on_ground[idx] != 0u && batch->state.hitstun[idx] == 0u &&
          batch->state.hitlag[idx] == 0u &&
          ((a == (uint16_t)MSL_ACT_ATTACK_S4_S && action_frame_pre == 7) ||
           (a != (uint16_t)MSL_ACT_ATTACK_S4_S && action_frame_pre == 2))) {
        const uint8_t smash_hold_timer_max =
            (a == (uint16_t)MSL_ACT_ATTACK_S4_S)
                ? 60u
                : ((a == (uint16_t)MSL_ACT_ATTACK_HI4) ? c->attack_hi4_tilt_max_frames
                                                       : c->attack_lw4_tilt_max_frames);
        const uint8_t pre_input_a_held =
            ((batch->state.input_buttons[idx] & (uint16_t)MSL_BUTTON_A) != 0u) ? 1u : 0u;
        if (pre_input_a_held != 0u && batch->state.x67C[idx] <= smash_hold_timer_max) {
          batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
        } else if (pre_input_a_held == 0u && batch->state.x67C[idx] > 0u &&
                   batch->state.frame_speed_mul_fp_q16_16[idx] == 0) {
          // Release bridge:
          // - when prior-frame A is no longer held, clear stale seeded hold-rate carry and resume
          //   default 1.0 on the next advance.
          // Decomp/data refs:
          // - AttackHi4/AttackLw4 input checks use p_ftCommonData->xD0/xD8 as their action-specific
          //   tilt windows for the hold admission side.
          // - Fresh held-A admission on the visible af=2 row is legal in live play (`x67C == 0`),
          //   but rows with prevA==0 still resume on the next step once A is no longer held.
          // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackHi4.c,ftCo_AttackLw4.c}
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
          // data/common/ft_common_data.json::{attack_hi4_tilt_max_frames,attack_lw4_tilt_max_frames}
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
          batch->state.frame_speed_mul_fp_q16_16[idx] = MSL_Q16_16_ONE;
        }
      }

      if (a == (uint16_t)MSL_ACT_REBOUND && action_frame_pre == 0) {
        const int32_t hidden_rebound_rate_fp = batch->state.rebound_anim_rate_fp_q16_16[idx];
        if (hidden_rebound_rate_fp > 0) {
          // Rebound callback-source ownership:
          // - ftCo_80099D9C stores `mv.co.rebound.anim_start` from `dmg.x191C`.
          // - ftCo_80099E44 consumes that hidden rate when entering Rebound.
          // - On first-Rebound replay seeds, `mv.co.rebound.x0` may already be consumed by
          //   Rebound_Phys, so the ground-velocity reconstruction is only a fallback.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
          //   ftCo_80099D9C,ftCo_80099E44,ftCo_Rebound_Phys}
          batch->state.frame_speed_mul_fp_q16_16[idx] = hidden_rebound_rate_fp;
        } else {
          float rebound_rate = 0.0f;
          if (anim_timebase_try_rebound_anim_speed_from_ground_vel(
                  c, ch, batch->state.speed_ground_x_self[idx], &rebound_rate)) {
            batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(rebound_rate);
          }
        }
      }

      // Attached ThrowHi/ThrownHi first-steady rate ownership:
      // - ftCo_800DD4B0 computes one shared throw anim_speed from the victim weight.
      // - ftCo_800DD398 enters both thrower and victim with that speed and immediately calls
      //   ftAnim_8006EBA4 in the owner callback, so the first attached post-entry snapshot has
      //   action_frame 1, and attached pre-release throw frames continue advancing on the shared
      //   throw rate until the throw script changes the rate/flags.
      // - This keeps rollout-started ThrowHi release timing aligned without broadening release
      //   gates; the victim/owner link and attached pre-release action phase are the source
      //   predicate.
      // - Keep this on ThrowHi/ThrownHi: ThrowF/B/Lw have their own release / hitlag pulse slices,
      //   and broadening this rate restore changed unrelated rollout first-break ownership.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD4B0,ftCo_800DD398}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE3FC
      if ((a == (uint16_t)MSL_ACT_THROW_HI || a == (uint16_t)MSL_ACT_THROWN_HI) &&
          action_frame_pre >= 1 && batch->state.hitlag[idx] == 0u) {
        if (msl_action_is_throw_owner(a)) {
          const uint8_t victim_p = batch->state.attached_victim_port[idx];
          if (victim_p != 0xFFu && (int)victim_p < num_players && (int)victim_p != p) {
            const size_t vidx = msl_idx_player(bi, (int)victim_p);
            if (msl_action_is_thrown_victim(batch->state.action_id[vidx])) {
              int32_t throw_rate_fp = batch->state.throw_anim_rate_fp_q16_16[idx];
              if (throw_rate_fp <= 0) {
                throw_rate_fp = anim_timebase_throw_rate_fp_from_pair(
                    batch->state.char_id[idx], batch->state.char_id[vidx], a);
              }
              if (throw_rate_fp > 0) {
                batch->state.frame_speed_mul_fp_q16_16[idx] = throw_rate_fp;
              }
            }
          }
        } else {
          uint8_t owner_p = batch->state.grab_owner_port[idx];
          if (owner_p == 0xFFu || (int)owner_p >= num_players || (int)owner_p == p) {
            for (int candidate = 0; candidate < num_players; candidate++) {
              const size_t cidx = msl_idx_player(bi, candidate);
              if (batch->state.attached_victim_port[cidx] == (uint8_t)p) {
                owner_p = (uint8_t)candidate;
                break;
              }
            }
          }
          if (owner_p != 0xFFu && (int)owner_p < num_players && (int)owner_p != p) {
            const size_t oidx = msl_idx_player(bi, (int)owner_p);
            if (msl_action_is_throw_owner(batch->state.action_id[oidx]) &&
                batch->state.attached_victim_port[oidx] == (uint8_t)p) {
              const uint16_t throw_action = anim_timebase_throw_action_from_thrown(a);
              int32_t throw_rate_fp = batch->state.throw_anim_rate_fp_q16_16[idx];
              if (throw_rate_fp <= 0 && throw_action != 0xFFFFu) {
                throw_rate_fp = anim_timebase_throw_rate_fp_from_pair(
                    batch->state.char_id[oidx], batch->state.char_id[idx], throw_action);
              }
              if (throw_rate_fp > 0) {
                batch->state.frame_speed_mul_fp_q16_16[idx] = throw_rate_fp;
              }
            }
          }
        }
      }

      // ThrowLw attached pulse/post-hitlag anim-rate ownership:
      // - Throw entry computes one shared throw anim-speed via ftCo_800DD4B0, and ftCo_800DD398
      //   installs it onto both thrower and thrown victim.
      // - The remaining non-shared slice is specifically ThrowLw's throw-side pulse family:
      //   ftCo_ThrowLw_Anim runs ftFx_Throw_Anim while the victim remains attached under
      //   ftCo_800DE508, and on the first post-hitlag pre-input row replay can carry a zeroed
      //   thrower frame_speed_mul snapshot even though the ThrowLw callback resumes the shared
      //   throw anim-speed for the attached pulse-25 window.
      // - This is not generic throw substrate: broadening it to ThrowF/ThrowHi changes real
      //   release-time ownership outside the ftFx_Throw_Anim family.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD4B0,ftCo_800DD398,ftCo_ThrowLw_Anim,ftCo_800DD724}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
      if (a == (uint16_t)MSL_ACT_THROW_LW) {
        if (batch->state.hitlag_pre_timer[idx] != 0u &&
            batch->state.hitlag_started_frame[idx] == 0u) {
          batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
        } else if (batch->state.frame_speed_mul_fp_q16_16[idx] == 0) {
          const int32_t throw_rate_fp = batch->state.throw_anim_rate_fp_q16_16[idx];
          if (throw_rate_fp > 0) {
            const uint8_t victim_p = batch->state.attached_victim_port[idx];
            if (victim_p != 0xFFu && (int)victim_p < num_players && (int)victim_p != p) {
              const size_t vidx = msl_idx_player(bi, (int)victim_p);
              if (msl_action_is_grabbed_victim(batch->state.action_id[vidx]) &&
                  batch->state.hitlag_pre_timer[vidx] != 0u && batch->state.hitlag[vidx] == 0u) {
                batch->state.frame_speed_mul_fp_q16_16[idx] = throw_rate_fp;
              }
            }
          }
        }
      }

      //
      // Decomp: AttackAir entry always uses anim_speed=1.0f (KeepFastFall only affects fastfall).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
      //
      // Decomp ordering rationale for `action_frame_pre == 1` (not a magic constant):
      // - ChangeMotionState enters at anim_start=0, anim_speed=1.
      // - The first ftAnim tick advances frame 0->1 before motion Anim callback-style rate updates
      //   are visible to the next tick.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
      // So frame 1 is the first "steady" reseed frame where stale carry-over rates must be reset.
      if (anim_timebase_is_attackair(a) && action_frame_pre == 1) {
        batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(1.0f);
      }

      // Decomp entry-rate corrections for landing states:
      // - LandingAir*: rate = (end_frame + 0.1f) / lag (with x67F L-cancel lag divide branch)
      // - LandingFallSpecial: anim_speed = (0.1f + fp->x2EC) / landing_lag
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
      //
      // Apply on action_frame==0 (entry-shaped snapshots) so one-step reseeds do not depend on
      // prior-frame hidden rates.
      if (action_frame_pre == 0 && c != NULL) {
        float entry_rate = 0.0f;
        if (a == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL) {
          float lag = c->landing_fall_special_lag_frames;
          const uint16_t source_prev_action = (action_frame_pre == 0)
                                                  ? batch->state.seed_prev_action_id[idx]
                                                  : batch->state.prev_action_id[idx];
          const uint8_t source_is_fallspecial =
              (source_prev_action == (uint16_t)MSL_ACT_FALL_SPECIAL ||
               source_prev_action == (uint16_t)MSL_ACT_FALL_SPECIAL_F ||
               source_prev_action == (uint16_t)MSL_ACT_FALL_SPECIAL_B)
                  ? 1u
                  : 0u;
          if (source_prev_action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END) {
            // Decomp source split for LandingFallSpecial entry rate:
            // - EscapeAir_Coll enters ftCo_LandingFallSpecial_Enter(..., p_ftCommonData->x344).
            // - Fox/Falco Illusion end collision enters ftCo_LandingFallSpecial_Enter(...,
            //   da->x50_FOX_ILLUSION_LANDING_LAG).
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialAirSEnd_Coll
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_LandingFallSpecial_Enter
            // data/characters/{fox,falco}.json: illusion_landing_lag_frames
            lag = (float)ch->illusion_landing_lag_frames;
          }
          if (!source_is_fallspecial) {
            const float end_frame = msl_anim_end_frame(batch->state.char_id[idx],
                                                       (uint16_t)MSL_SM_LANDING_FALL_SPECIAL);
            if (lag > 0.0f && end_frame > 0.0f) {
              entry_rate = (end_frame + 0.1f) / lag;
            }
          }
        } else if (anim_timebase_try_landing_air_rate(a, ch, c, batch->state.char_id[idx],
                                                      batch->state.lr_press_timer[idx],
                                                      batch->state.l_cancel[idx], &entry_rate)) {
          // rate already written to entry_rate by helper.
        }
        if (entry_rate > 0.0f) {
          batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(entry_rate);
        }
      }

      // Slippi parity (no-submotion snapshots):
      // Slippi can report `animation_index==0xFFFFFFFF` with `state_age==-1` (i.e.
      // `anim_frame_f32==-1`, `action_frame==-1`). Preserve that frozen (-1) timebase even if a
      // nonzero `frame_speed_mul` is seeded.
      if (batch->state.animation_index[idx] == 0xFFFFFFFFu &&
          batch->state.anim_frame_fp_q16_16[idx] < 0) {
        msl_anim_timebase_recompute_derived(batch, idx);
        continue;
      }

      batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
      {
        // Deterministic fixed-point representation guard for source float throw rates:
        // Fox/Falco attached back/up throws can use a data-backed 4/3 shared rate. Repeated Q16.16
        // rounded advances can land exactly one LSB below an integer (for example 3.999984),
        // delaying set_throw_flags / throw-side script-frame checks even though the source float
        // timebase crosses the integer. During the attached window this mirrors the shared
        // ThrowB/Hi source rate directly; after detach, keep it only on extracted throw command
        // windows/events because ordinary fractional frames can legitimately remain just below an
        // integer in Slippi.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{
        //   ftCo_800DD4B0,ftCo_800DD398,ftCo_800DD724
        // }
        const int32_t cur_fp = batch->state.anim_frame_fp_q16_16[idx];
        if (cur_fp > 0) {
          const int32_t rem = cur_fp % (int32_t)MSL_Q16_16_ONE;
          const int32_t throw_snap_delta = anim_timebase_non_low_throw_rate_snap_delta(
              batch, bi, p, idx, num_players, cur_fp, rem);
          if (throw_snap_delta > 0) {
            batch->state.anim_frame_fp_q16_16[idx] = cur_fp + throw_snap_delta;
          }
        }
      }
      const uint8_t did_wrap = anim_timebase_apply_aobj_loop(batch, idx);
      anim_timebase_apply_capture_loop(batch, idx);

      // Non-looping timelines clamp at end_frame and stop advancing.
      //
      // Decomp:
      // - ftAnim_8006EBA4 advances the underlying HSD AObj timeline.
      // - HSD_AObjInterpretAnim clamps curr_frame at end_frame when not looping.
      // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
      // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
      //
      // Scope guard: only apply this clamp to non-looping tracks (AOBJ_LOOP==0 in extracted
      // `data/anims/*.tracks.bin`). Looping tracks continue to use the deterministic modulo wrap
      // in anim_timebase_apply_aobj_loop() exactly as before.
      if (!did_wrap) {
        const uint32_t anim_u32 = batch->state.animation_index[idx];
        if (anim_u32 <= 0xFFFFu &&
            !msl_anim_is_looping(batch->state.char_id[idx], (uint16_t)anim_u32)) {
          const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)anim_u32);
          if (end_frame > 0.0f) {
            const int32_t end_fp = msl_q16_16_from_f32(end_frame);
            if (end_fp > 0) {
              int32_t cur_fp = batch->state.anim_frame_fp_q16_16[idx];
              if (cur_fp > end_fp) {
                cur_fp = end_fp;
                batch->state.anim_frame_fp_q16_16[idx] = cur_fp;
              }
              if (cur_fp >= end_fp) {
                batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
              }
            }
          }
        }
      }

      msl_anim_timebase_recompute_derived(batch, idx);

      // Action-script pseudo-random SFX command RNG lane:
      // - Command opcode 38 (`ftAction_80071FC8`) consumes one HSD_Randi(random_range) when the
      //   event executes during command-script interpretation.
      // - Command scripts are interpreted on the anim callback timeline under Fighter_8006A360.
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80071FC8
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
      //
      // Runtime mapping:
      // - Extracted event pulses are sourced from `data/moves/{fox,falco}.json`
      //   `specials_by_msid["<msid>"].events` with kind `pseudo_random_sfx`.
      // - Consume each crossed pulse once using the same frame-crossing policy as other script
      //   pulse lanes.
      // - `MSL_RNG_DISABLE_PSEUDO_RANDOM_SFX_CMD=1` is a debug kill-switch for A/B ablations.
      if (!batch->debug_rng_disable_pseudo_random_sfx_cmd && action_frame_pre >= 0) {
        const uint32_t smid_u32 = batch->state.animation_index[idx];
        const int16_t action_frame_cur = batch->state.action_frame[idx];
        if (smid_u32 <= 0xFFFFu && action_frame_cur >= action_frame_pre) {
          enum { MSL_PSEUDO_SFX_PULSE_MAX = 16 };
          uint8_t random_ranges[MSL_PSEUDO_SFX_PULSE_MAX] = {0};
          const uint8_t pulse_n = move_tables_special_pseudo_random_sfx_ranges_crossed(
              batch->state.char_id[idx], (uint16_t)smid_u32, (float)action_frame_pre,
              (float)action_frame_cur, random_ranges, (uint8_t)MSL_PSEUDO_SFX_PULSE_MAX);
          for (uint8_t ri = 0; ri < pulse_n; ri++) {
            const uint8_t rr = random_ranges[ri];
            if (rr > 0u) {
              (void)combat_rng_consume_randi_site(
                  batch, bi, MSL_RNG_SITE_FTACTION_PSEUDO_RANDOM_SFX_CMD, (uint32_t)rr);
            }
          }
        }
      }

      // Decomp: Fighter_ChangeMotionState clears `fp->fall_fast` unless KeepFastFall is requested.
      // refs/melee/src/melee/ft/fighter.c (KeepFastFall gate inside ChangeMotionState).
      //
      // Some suite-present special-move timelines restart their motion state on anim-end by
      // calling Fighter_ChangeMotionState (even if the action_id is unchanged), which clears
      // `fp->fall_fast` unless KeepFastFall is present in the call flags.
      //
      // Decomp example (GALE01):
      // - ftFx_SpecialAirNLoop_Anim restarts ftFx_MS_SpecialAirNLoop with
      //   (Ft_MF_SkipAttackCount | Ft_MF_SkipModel | Ft_MF_KeepGfx), i.e. without KeepFastFall.
      //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
      //
      // We do not model per-state Anim callbacks yet, but we can observe restart-like boundaries
      // via a wrap in the derived `action_frame` and clear fastfall in those cases.
      //
      // IMPORTANT: Do not clear `fall_fast` generically on any AObj loop wrap. Many common motion
      // states (including Fall) use AOBJ_LOOP and wrap `cur_anim_frame` without invoking
      // Fighter_ChangeMotionState; fastfall persists across those wraps in decomp.
      if (did_wrap && action_frame_pre >= 0 && batch->state.action_frame[idx] < action_frame_pre) {
        const uint16_t a = batch->state.action_id[idx];
        // Wait anim variant selection consumes HSD_Randi(100) when ftCo_8008A7A8 needs a new
        // idle sub-animation at end-of-anim.
        // refs/melee/src/melee/ft/ftwaitanim.c::{ftCo_8008A7A8,getAnimID}
        // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
        if (a == (uint16_t)MSL_ACT_WAIT) {
          (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_FTWAIT_ANIM_VARIANT, 100);
        }
        if (a == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_N_LOOP) {
          batch->state.fall_fast[idx] = 0;
        }
      }
    }
  }
}

void anim_timebase_apply_deferred_tick_once_pre_collision(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (!batch->state.anim_defer_tick_once[idx]) {
        continue;
      }
      batch->state.anim_defer_tick_once[idx] = 0u;

      // Decomp: these motion-state entry paths call ftAnim_8006EBA4 immediately after
      // Fighter_ChangeMotionState, before fighter collision primitives are refreshed and before
      // combat/hitlag for this frame is resolved. Apply the one-shot tick in the pre-collision slot
      // so lb_8000B1CC / ftColl consumers sample the same entry pose that becomes replay-visible at
      // t+1.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
      // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
      // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
      //
      // IMPORTANT: do not gate this on `hitlag_started_frame`; hitlag can be started by combat later
      // in the frame, after the decomp tick already occurred.
      batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
      const uint8_t did_wrap = anim_timebase_apply_aobj_loop(batch, idx);
      anim_timebase_apply_capture_loop(batch, idx);

      if (!did_wrap) {
        const uint32_t anim_u32 = batch->state.animation_index[idx];
        if (anim_u32 <= 0xFFFFu &&
            !msl_anim_is_looping(batch->state.char_id[idx], (uint16_t)anim_u32)) {
          const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)anim_u32);
          if (end_frame > 0.0f) {
            const int32_t end_fp = msl_q16_16_from_f32(end_frame);
            if (end_fp > 0) {
              int32_t cur_fp = batch->state.anim_frame_fp_q16_16[idx];
              if (cur_fp > end_fp) {
                cur_fp = end_fp;
                batch->state.anim_frame_fp_q16_16[idx] = cur_fp;
              }
              if (cur_fp >= end_fp) {
                batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
              }
            }
          }
        }
      }

      msl_anim_timebase_recompute_derived(batch, idx);
    }
  }
}

void anim_timebase_apply_deferred_tick_once_post_combat(MslBatch* batch) {
  // Combat can enter a fresh damage or special motion after the pre-collision deferred-tick owner
  // has already run. Apply any newly requested ftAnim_8006EBA4-equivalent tick here so the
  // replay-visible post-frame action_frame matches the motion state just installed by collision.
  // Pre-collision entrants have already consumed their flag and will not double-tick.
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  anim_timebase_apply_deferred_tick_once_pre_collision(batch);
}
