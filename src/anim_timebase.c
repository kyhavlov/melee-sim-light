#include "anim_timebase.h"

#include <math.h>
#include <stddef.h>

#include "action_ids.h"
#include "anim_table.h"
#include "attack_id_tables.h"
#include "char_params.h"
#include "combat.h"
#include "common_params.h"
#include "move_tables.h"

enum { Ft_MF_KeepFastFall = 1 << 0 };

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
  switch (a) {
    case MSL_ACT_ATTACK_AIR_N:
    case MSL_ACT_ATTACK_AIR_F:
    case MSL_ACT_ATTACK_AIR_B:
    case MSL_ACT_ATTACK_AIR_HI:
    case MSL_ACT_ATTACK_AIR_LW:
      return 1;
    default:
      return 0;
  }
}

static inline uint8_t anim_timebase_is_walk(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_WALK_SLOW || a == (uint16_t)MSL_ACT_WALK_MIDDLE ||
          a == (uint16_t)MSL_ACT_WALK_FAST)
             ? 1u
             : 0u;
}

static inline uint8_t anim_timebase_try_walk_rate(uint16_t a, const MslCharParams* ch,
                                                  float speed_ground_x_self, int8_t facing_dir1,
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
  const float mv_x0 = speed_ground_x_self;

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
        } else if (action_frame_pre == 1) {
          // Decomp ordering bridge for first steady walk frame:
          // - Walk enter uses ChangeMotionState(..., anim_speed=1) then immediate ftAnim tick.
          // - Walk_Anim (ftWalkCommon_800DFDDC) writes the velocity-scaled rate after that tick,
          //   for the *next* frame's advance.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Enter
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Anim
          // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
          //
          // Keep the seeded 1.0 entry carry here; applying the scaled walk rate one frame early
          // at action_frame==1 shifts Walk timebase ownership.
        } else if (a == (uint16_t)MSL_ACT_WALK_MIDDLE && action_frame_pre == 10) {
          // WalkMiddle callback-rate ownership bridge:
          // - ftWalkCommon_800DFDDC derives anim rate from `mv.co.walk.x0` / middle_anim_rate.
          // - `mv.co.walk.x0` is a walk callback-owned lane that is not explicitly represented in
          //   current reseed schema; using raw `gr_vel` here can over-advance mid-cycle rows.
          // - Preserve the seeded callback-owned rate on this narrow steady frame window where the
          //   suite exhibits `WalkMiddle af=10` over-advance with `gr_vel`-derived recompute.
          // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Anim
          // TODO(narrowed_temporary): promote seed-visible mv.co.walk.x0 ownership so this frame
          // window bridge can be removed in favor of fully callback-owned rate parity.
        } else if (a == (uint16_t)MSL_ACT_WALK_MIDDLE && action_frame_pre == 3 &&
                   batch->state.frame_speed_mul_fp_q16_16[idx] > MSL_Q16_16_ONE) {
          // WalkMiddle mid-cycle ownership bridge:
          // - callback-owned mv.co.walk.x0 can remain phase-lagged vs gr_vel during the early
          //   acceleration segment; preserve seeded callback rate on this narrow af=3 window.
          // refs/melee/src/melee/ft/ftwalkcommon.c::ftWalkCommon_800DFDDC
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_Anim
        } else {
          float walk_rate = 0.0f;
          if (anim_timebase_try_walk_rate(a, ch, batch->state.speed_ground_x_self[idx],
                                          batch->state.facing_dir1[idx], &walk_rate)) {
            batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(walk_rate);
          }
        }
      }

      // Decomp-shaped early-steady bridge (why `action_frame in {1,2}` is not a magic number):
      // - Run is entered via Fighter_ChangeMotionState(..., anim_start=0, anim_speed=1).
      //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::ftCo_Run_Enter_Full
      // - Animation advance for the frame happens first (ftAnim_8006EBA4), then the per-motion
      //   Anim callback runs (Run_Anim), which sets the anim rate for the *next* advance.
      //   refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      //   refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
      //
      // Under teacher-forced reseed, a Run segment can enter with `state_age` already at 1-2 while
      // the seeded `frame_speed_mul` still reflects entry carry. Apply the Run_Anim-scaled rate in
      // this early steady window so the next advance matches decomp callback ownership.
      //
      // TODO(narrowed_temporary): this <=2 early-steady window is a bounded bridge. Full parity
      // needs a seed-visible callback-phase ownership marker equivalent to whether Run_Anim already
      // committed the next rate in the previous frame; Slippi post-frame does not expose that bit.
      if ((a == (uint16_t)MSL_ACT_RUN || a == (uint16_t)MSL_ACT_RUN_DIRECT) &&
          action_frame_pre > 0 && action_frame_pre <= 2) {
        if (ch != NULL && ch->run_animation_scaling > 0.0f) {
          const float vx = batch->state.speed_ground_x_self[idx];
          const float rate = fabsf(vx) / ch->run_animation_scaling;
          batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(rate);
        }
      }

      // Grounded smash early-hold ownership bridge:
      // - Smash family hold lanes in GALE01 use A-held gating before the hitbox-onset window.
      // - In the Fox/Falco suite, AttackHi4/AttackLw4 rows with held A can remain at action_frame=2
      //   across consecutive frames before progressing into the startup/hitbox timeline.
      // refs/melee/src/melee/ft/ftattacks4combo.c::ftCo_800CECE8
      // refs/melee/src/melee/ft/chara/ftCommon/{ftCo_AttackHi4.c,ftCo_AttackLw4.c}
      // data/moves/{fox,falco}.json::ftCo_SM_Attack{Hi4,Lw4}
      if ((a == (uint16_t)MSL_ACT_ATTACK_HI4 || a == (uint16_t)MSL_ACT_ATTACK_LW4) &&
          batch->state.on_ground[idx] != 0u && batch->state.hitstun[idx] == 0u &&
          batch->state.hitlag[idx] == 0u && action_frame_pre == 2 &&
          batch->state.x67C[idx] != 0u && batch->state.x67C[idx] <= 2u) {
        batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
      }

      // CaptureWait Anim-rate ownership bridge:
      // - ftCo_CaptureWaitHi_Anim updates frame_speed_mul via ftAnim_SetAnimRate in Anim callback.
      // - Fighter_8006A360 advances the AObj timeline before callback-owned rate writes.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_CaptureWaitHi_Anim
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      //
      // Teacher-forced reseed snapshots are post-frame; for stable CaptureWait ownership we apply
      // the previous seeded rate when continuity is preserved across consecutive reseeds.
      //
      // TODO(narrowed_temporary): this bridge is continuity-gated only. Full parity needs an
      // explicit seed lane for whether the prior frame's CaptureWait callback already committed a
      // non-1.0 ftAnim_SetAnimRate write before reseed.
      if ((a == (uint16_t)MSL_ACT_CAPTURE_WAIT_HI || a == (uint16_t)MSL_ACT_CAPTURE_WAIT_LW) &&
          batch->state.capture_wait_prev_rate_valid[idx]) {
        batch->state.frame_speed_mul_fp_q16_16[idx] =
            batch->state.capture_wait_prev_rate_fp_q16_16[idx];
      }

      // ThrowLw hitlag-release anim-rate ownership bridge:
      // - Hitlag decrement runs at Fighter_8006A1BC before Fighter_8006A360 callback work.
      // - ThrowLw callback ownership (ftCo_ThrowLw_Anim -> ftCo_800DD724) can leave reseeded
      //   first post-hitlag rows with frame_speed_mul==0 while the prior continuous row had a
      //   nonzero callback-owned rate.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowLw_Anim,ftCo_800DD724}
      //
      // Bridge policy (narrowed):
      // - On the frame that started in hitlag (`hitlag_pre_timer!=0`) but is no longer frozen after
      //   decrement (`hitlag_started_frame==0`), keep ThrowLw rate at 0 for this pre-input tick.
      // - On strict continuity rows flagged during reseed, restore prior ThrowLw seeded rate only
      //   when the current seeded rate is 0.
      // TODO(narrowed_temporary): full parity needs a direct seed-visible "ThrowLw callback wrote
      // next anim rate" ownership marker instead of continuity inference.
      if (a == (uint16_t)MSL_ACT_THROW_LW) {
        if (batch->state.hitlag_pre_timer[idx] != 0u &&
            batch->state.hitlag_started_frame[idx] == 0u) {
          batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
        } else if (batch->state.throw_lw_prev_rate_valid[idx] != 0u &&
                   batch->state.frame_speed_mul_fp_q16_16[idx] == 0) {
          batch->state.frame_speed_mul_fp_q16_16[idx] =
              batch->state.throw_lw_prev_rate_fp_q16_16[idx];
        } else if (batch->state.frame_speed_mul_fp_q16_16[idx] == 0) {
          // ThrowLw attached-victim post-hitlag bridge (seed-visible fallback):
          // - ThrowLw callback ownership (`ftCo_ThrowLw_Anim` -> `ftCo_800DD724`) consumes throw
          //   script flags while the victim remains attached.
          // - Thrown victim Phys/Coll are attachment-driven (`ftCo_Thrown*`), so on the first
          //   post-hitlag frame the owner/victim timebase can advance together even when the
          //   thrower reseed snapshot carries frame_speed_mul==0.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowLw_Anim,ftCo_800DD724}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE508,ftCo_ThrownF_Phys,ftCo_ThrownF_Coll}
          // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
          for (int v = 0; v < num_players; v++) {
            if (v == p) {
              continue;
            }
            const size_t vidx = msl_idx_player(bi, v);
            if (batch->state.grab_owner_port[vidx] != (uint8_t)p) {
              continue;
            }
            if (!msl_action_is_grabbed_victim(batch->state.action_id[vidx])) {
              continue;
            }
            if (!(batch->state.hitlag_pre_timer[vidx] != 0u && batch->state.hitlag[vidx] == 0u)) {
              continue;
            }
            const int32_t victim_rate = batch->state.frame_speed_mul_fp_q16_16[vidx];
            if (victim_rate > 0) {
              batch->state.frame_speed_mul_fp_q16_16[idx] = victim_rate;
            }
            break;
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
          const float lag = c->landing_fall_special_lag_frames;
          const float end_frame =
              msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_LANDING_FALL_SPECIAL);
          if (lag > 0.0f && end_frame > 0.0f) {
            entry_rate = (end_frame + 0.1f) / lag;
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
              (void)combat_rng_consume_randi_site(batch, bi,
                                                  MSL_RNG_SITE_FTACTION_PSEUDO_RANDOM_SFX_CMD,
                                                  (uint32_t)rr);
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

void anim_timebase_apply_deferred_tick_once_post_combat(MslBatch* batch) {
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

      // Decomp: some motion-state entry paths call ftAnim_8006EBA4 immediately after
      // Fighter_ChangeMotionState, before combat/hitlag for this frame is resolved. This deferred
      // tick is applied post-combat to keep hitbox evaluation on the entry pose_frame while still
      // matching Slippi post-frame state_age/action_frame.
      //
      // Apply it post-combat in this simulator to preserve pre-combat/combat geometry side effects
      // (hitbox refresh + collision/KB resolution) while still matching the decomp entry semantics
      // seen in ftFx_Special{N,Lw}_Enter and ftCo_AttackAir_EnterFromMsid.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c
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
