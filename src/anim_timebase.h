#pragma once

#include <math.h>
#include <stdint.h>

#include "attack_id_tables.h"
#include "batch_internal.h"
#include "action_ids.h"
#include "anim_table.h"

// Deterministic animation/script timebase helpers.
//
// Decomp shape (GALE01):
// - fp->cur_anim_frame (float) is the movescript/AS timebase recorded by Slippi as `state_age`.
// - fp->frame_speed_mul (float) is the animation rate (ftAnim_SetAnimRate / ftAnim_8006F0FC).
// - Fighter_ChangeMotionState sets:
//     fp->frame_speed_mul = anim_speed;
//     fp->cur_anim_frame = anim_start - fp->frame_speed_mul;
//   but then calls ftAnim_8006E9B4, which overwrites fp->cur_anim_frame from the current AObj
//   frame (ftAnim_8006F3DC). This means post-frame `state_age` is shaped by the AObj curr_frame
//   on entry (typically anim_start for anim_start==0.0f).
//   refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
//   refs/melee/src/melee/ft/ftanim.c::ftAnim_8006E9B4
//   refs/melee/src/melee/ft/ftanim.c::ftAnim_8006F3DC
// - Animation advance is gated during hitlag:
//   Fighter_8006A360 wraps ftAnim_8006EBA4 in `if (!fp->x2219_b5)`.
//   refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
//
// We represent these as signed Q16.16 fixed-point for deterministic fractional carry on the hot path.

enum { MSL_Q16_16_ONE = 1 << 16 };

static inline int32_t msl_q16_16_clamp_i32(double x) {
  if (x >= 2147483647.0) {
    return INT32_MAX;
  }
  if (x <= -2147483648.0) {
    return INT32_MIN;
  }
  return (int32_t)x;
}

static inline int32_t msl_q16_16_from_f32(float x) {
  if (!isfinite(x)) {
    return 0;
  }
  // Round-to-nearest when quantizing float -> fixed-point so that seeded and computed rates don't
  // systematically bias downward (important for anim-end and window thresholds).
  //
  // Note: action_frame is derived separately via floor(x / 65536), matching `floor(cur_anim_frame)`.
  const double scaled = (double)x * (double)MSL_Q16_16_ONE;
  const double quantized = (scaled >= 0.0) ? floor(scaled + 0.5) : ceil(scaled - 0.5);
  return msl_q16_16_clamp_i32(quantized);
}

static inline float msl_f32_from_q16_16(int32_t x) {
  return (float)x * (1.0f / (float)MSL_Q16_16_ONE);
}

static inline int16_t msl_floor_i16_from_q16_16(int32_t x) {
  // Compute floor(x / 65536) in a way that is well-defined for negative values.
  int32_t q = 0;
  if (x >= 0) {
    q = x / (int32_t)MSL_Q16_16_ONE;
  } else {
    const int32_t neg = -x;
    q = -(int32_t)((neg + ((int32_t)MSL_Q16_16_ONE - 1)) / (int32_t)MSL_Q16_16_ONE);
  }
  if (q > (int32_t)INT16_MAX) {
    return INT16_MAX;
  }
  if (q < (int32_t)INT16_MIN) {
    return INT16_MIN;
  }
  return (int16_t)q;
}

static inline void msl_anim_timebase_recompute_derived(MslBatch* batch, size_t idx) {
  batch->state.anim_frame_f32[idx] = msl_f32_from_q16_16(batch->state.anim_frame_fp_q16_16[idx]);
  batch->state.action_frame[idx] =
      msl_floor_i16_from_q16_16(batch->state.anim_frame_fp_q16_16[idx]);
}

static inline void msl_anim_timebase_seed(MslBatch* batch, size_t idx, float cur_anim_frame_f32,
                                          float frame_speed_mul_f32) {
  if (batch == NULL) {
    return;
  }
  int32_t anim_q = msl_q16_16_from_f32(cur_anim_frame_f32);
  const int32_t speed_q = msl_q16_16_from_f32(frame_speed_mul_f32);

  // Seed phase alignment (decomp-shaped action_frame ownership):
  // - action_frame progression is floor(cur_anim_frame) after per-frame anim advance.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  //
  // Q16.16 quantization can shift `cur_anim_frame` by ~1 LSB around integer boundaries and flip
  // the next-frame floor by one. Clamp seeded q16 into the interval that preserves
  // floor(seed_cur + seed_speed) computed from the seeded float snapshot.
  if (isfinite(cur_anim_frame_f32) && isfinite(frame_speed_mul_f32)) {
    const int32_t expect_next_af = (int32_t)floorf(cur_anim_frame_f32 + frame_speed_mul_f32);
    const int64_t lo = (int64_t)expect_next_af * (int64_t)MSL_Q16_16_ONE - (int64_t)speed_q;
    const int64_t hi =
        (int64_t)(expect_next_af + 1) * (int64_t)MSL_Q16_16_ONE - 1LL - (int64_t)speed_q;
    if ((int64_t)anim_q < lo) {
      anim_q = (lo < (int64_t)INT32_MIN)   ? INT32_MIN
               : (lo > (int64_t)INT32_MAX) ? INT32_MAX
                                           : (int32_t)lo;
    } else if ((int64_t)anim_q > hi) {
      anim_q = (hi < (int64_t)INT32_MIN)   ? INT32_MIN
               : (hi > (int64_t)INT32_MAX) ? INT32_MAX
                                           : (int32_t)hi;
    }
  }
  batch->state.anim_frame_fp_q16_16[idx] = anim_q;
  batch->state.frame_speed_mul_fp_q16_16[idx] = speed_q;
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline void msl_anim_timebase_enter_raw(MslBatch* batch, size_t idx, float anim_start_f32,
                                               float anim_speed_f32) {
  // Mirror Fighter_ChangeMotionState's post-entry cur_anim_frame shape.
  //
  // Decomp:
  // - fp->frame_speed_mul = anim_speed;
  // - fp->cur_anim_frame = anim_start - fp->frame_speed_mul;
  // - ftAnim_8006E9B4(gobj) overwrites fp->cur_anim_frame from ftAnim_8006F3DC (AObj curr_frame).
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006E9B4
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006F3DC
  if (batch == NULL) {
    return;
  }
  const int32_t start_fp = msl_q16_16_from_f32(anim_start_f32);
  const int32_t speed_fp = msl_q16_16_from_f32(anim_speed_f32);
  // Simulator scheduling invariant:
  // entering a new motion state supersedes any prior state's deferred one-shot anim tick request.
  // Without this clear, an earlier same-frame `MSL_ANIM_ENTER_TICK_DEFER_POST_COMBAT` can leak
  // across a later ChangeMotionState and double-advance the new action's frame counter.
  batch->state.anim_defer_tick_once[idx] = 0u;
  batch->state.frame_speed_mul_fp_q16_16[idx] = speed_fp;
  batch->state.anim_frame_fp_q16_16[idx] = start_fp;
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline void msl_anim_timebase_tick_once(MslBatch* batch, size_t idx) {
  // Only call this for decomp-anchored motion-state entry paths that perform an immediate
  // same-frame animation advance on entry:
  // - explicit ftAnim_8006EBA4 after Fighter_ChangeMotionState (e.g. Dash / Turn), or
  // - Fighter_ChangeMotionState entry paths where same-call ftAnim_8006E9B4 advancement must be
  //   represented by this simulator's timebase layer.
  //
  // Decomp examples:
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_Enter
  // - refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter
  // - refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // - refs/melee/src/melee/ft/ftanim.c::ftAnim_8006E9B4
  if (batch == NULL) {
    return;
  }
  // Decomp: Fighter_8006A360 gates ftAnim_8006EBA4 on !hitlag (fp->x2219_b5).
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  if (batch->state.hitlag_started_frame[idx] != 0) {
    msl_anim_timebase_recompute_derived(batch, idx);
    return;
  }
  batch->state.anim_frame_fp_q16_16[idx] += batch->state.frame_speed_mul_fp_q16_16[idx];
  const uint32_t anim_u32 = batch->state.animation_index[idx];
  if (anim_u32 <= 0xFFFFu) {
    const uint16_t smid = (uint16_t)anim_u32;
    const float end_frame = msl_anim_end_frame(batch->state.char_id[idx], smid);
    if (end_frame > 0.0f) {
      const int32_t end_fp = msl_q16_16_from_f32(end_frame);
      if (end_fp > 0 && batch->state.anim_frame_fp_q16_16[idx] >= end_fp) {
        if (msl_anim_is_looping(batch->state.char_id[idx], smid)) {
          batch->state.anim_frame_fp_q16_16[idx] %= end_fp;
        } else {
          batch->state.anim_frame_fp_q16_16[idx] = end_fp;
          batch->state.frame_speed_mul_fp_q16_16[idx] = 0;
        }
      }
    }
  }
  msl_anim_timebase_recompute_derived(batch, idx);
}

static inline void msl_anim_timebase_defer_tick_once(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  batch->state.anim_defer_tick_once[idx] = 1u;
}

typedef enum MslAnimEnterTickPolicy {
  MSL_ANIM_ENTER_TICK_NONE = 0,
  MSL_ANIM_ENTER_TICK_IMMEDIATE = 1,
  MSL_ANIM_ENTER_TICK_DEFER_POST_COMBAT = 2,
} MslAnimEnterTickPolicy;

static inline void msl_anim_timebase_apply_enter_tick_policy(MslBatch* batch, size_t idx,
                                                             MslAnimEnterTickPolicy policy) {
  if (batch == NULL) {
    return;
  }
  if (policy == MSL_ANIM_ENTER_TICK_IMMEDIATE) {
    msl_anim_timebase_tick_once(batch, idx);
  } else if (policy == MSL_ANIM_ENTER_TICK_DEFER_POST_COMBAT) {
    msl_anim_timebase_defer_tick_once(batch, idx);
  }
}

static inline void msl_motion_state_start_source_clear_timer_x18c8(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.on_ground[idx] == 0u ||
      batch->state.source_clear_timer_x18c8[idx] != 0u || batch->state.last_hit_by[idx] == 6u) {
    return;
  }

  const uint16_t a = batch->state.action_id[idx];
  const uint32_t motion_word =
      attack_id_motion_state_word_from_action(batch->state.char_id[idx], a);
  // Big-endian MotionState bitfield packing: x9_b0 is bit 23 and x9_b1 is bit 22 in the
  // extracted +0x8 word. Fighter_ChangeMotionState seeds dmg.x18C8 from p_ftCommonData->x814
  // when entering a grounded x9_b1 state while the countdown is inactive.
  // Runtime stores the +1-biased seed representation. The modeled landing/collision entries that
  // reach this helper run after timers_update_post_anim(), so the first decrement is on the next
  // simulated frame.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_8006A360}
  // refs/melee/src/melee/ft/types.h::MotionState (x9_b1), fp->dmg.x18C8
  // data/attack_id/move_id/{fox,falco}.bin::motion_state_word
  enum { MSL_SOURCE_CLEAR_X18C8_INIT_FRAMES_X814 = 60u };
  if ((motion_word & (uint32_t)(1u << 22)) != 0u) {
    batch->state.source_clear_timer_x18c8[idx] = (uint8_t)MSL_SOURCE_CLEAR_X18C8_INIT_FRAMES_X814;
    batch->state.source_clear_owner_set_phase[idx] = 1u;
  }
}

// Forward decl: fighter attack identity update on motion-state change.
// Defined in src/attack_identity.c.
void attack_identity_on_motion_state_change_ft_800890D0(MslBatch* batch, size_t idx);

// Forward decl: fighter action-state instance_id update on motion-state change.
// Defined in src/instance_id.c.
void instance_id_on_motion_state_change_ft_800895E0(MslBatch* batch, size_t idx);

static inline void msl_motion_state_enter_side_effects(MslBatch* batch, size_t idx) {
  // Decomp: Fighter_ChangeMotionState updates motion-state-owned identity/bookkeeping after
  // installing the destination motion. Some simulator paths intentionally delay only the animation
  // timebase/pose commit for collision parity; they still need these side effects at source-time.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
  if (batch != NULL) {
    // Decomp: Fighter_UnkInitReset and Fighter_ChangeMotionState copy fp->facing_dir into
    // fp->facing_dir1 on motion-state entry. Root-motion helpers such as ft_80085030 consume
    // facing_dir1, so rollout entries must not inherit a stale prior state's sign.
    // refs/melee/src/melee/ft/fighter.c::{Fighter_UnkInitReset,Fighter_ChangeMotionState}
    batch->state.facing_dir1[idx] = batch->state.facing[idx] ? (int8_t)1 : (int8_t)-1;
  }

  // Decomp: Fighter_ChangeMotionState clears `fp->fall_fast` when (flags & Ft_MF_KeepFastFall)==0.
  // refs/melee/src/melee/ft/fighter.c (see KeepFastFall gate).
  // refs/melee/src/melee/ft/forward.h (Ft_MF_KeepFastFall = 1<<0).
  //
  // This simulator does not plumb the per-transition `flags` argument explicitly; approximate
  // using the ISO-extracted per-action x4_flags table (same source used for move identity).
  enum { Ft_MF_KeepFastFall = 1 << 0 };
  if (batch != NULL) {
    const uint16_t a = batch->state.action_id[idx];
    // Decomp: AttackAir enters with Ft_MF_KeepFastFall unconditionally.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_EnterFromMsid
    if (!(a == (uint16_t)MSL_ACT_ATTACK_AIR_N || a == (uint16_t)MSL_ACT_ATTACK_AIR_F ||
          a == (uint16_t)MSL_ACT_ATTACK_AIR_B || a == (uint16_t)MSL_ACT_ATTACK_AIR_HI ||
          a == (uint16_t)MSL_ACT_ATTACK_AIR_LW)) {
      const uint32_t x4_flags = attack_id_x4_flags_from_action(batch->state.char_id[idx], a);
      if ((x4_flags & (uint32_t)Ft_MF_KeepFastFall) == 0u) {
        batch->state.fall_fast[idx] = 0;
      }
    }

    // Decomp: smash_attrs is motion-owned transient state. Fighter_ChangeMotionState enters a new
    // motion with smash charge lifecycle cleared unless a later command script seeds it again.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    // refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEEA8,ftCo_800DEE84}
    batch->state.kb_smashcharge_active[idx] = 0u;
    batch->state.smash_charge_state[idx] = 0u;
    batch->state.smash_charge_frames[idx] = 0u;
    batch->state.smash_charge_hold_frames_max[idx] = 0u;
    batch->state.smash_charge_saved_rate_fp_q16_16[idx] = 0;
    // Common Fall/FallAerial/FallSpecial entry initializes mv.co.*.x4=0 and the selected smid to
    // the neutral submotion. Clearing on all motion entries prevents stale blend pose from
    // surviving into non-Fall states; the Anim callback reconstructs source x4/smid only while a
    // Fall-family state is live.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Enter
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallAerial.c::ftCo_FallAerial_Enter
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::inline0
    batch->state.common_fall_blend_x4[idx] = 0.0f;
    batch->state.common_fall_blend_msid[idx] = 0u;
    // Decomp: Squat_Enter clears mv.co.squat.x0 before the platform-pass helper can arm it.
    // Clearing on all motion entries prevents stale armed countdowns from surviving out of Squat.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_Enter
    batch->state.squat_pass_x0[idx] = 0u;
    batch->state.squat_pass_x4[idx] = 0u;
  }

  attack_identity_on_motion_state_change_ft_800890D0(batch, idx);

  instance_id_on_motion_state_change_ft_800895E0(batch, idx);

  msl_motion_state_start_source_clear_timer_x18c8(batch, idx);
}

static inline void msl_anim_timebase_enter(MslBatch* batch, size_t idx, float anim_start_f32,
                                           float anim_speed_f32) {
  // Decomp: Fighter_ChangeMotionState sets the anim timebase then invokes motion-state identity
  // updates (ft_800890D0 / ft_800895E0). This helper models that full "enter motion state" bundle.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
  // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
  msl_anim_timebase_enter_raw(batch, idx, anim_start_f32, anim_speed_f32);
  msl_motion_state_enter_side_effects(batch, idx);
}

static inline void msl_anim_timebase_enter_with_policy(MslBatch* batch, size_t idx,
                                                       float anim_start_f32, float anim_speed_f32,
                                                       MslAnimEnterTickPolicy policy) {
  msl_anim_timebase_enter(batch, idx, anim_start_f32, anim_speed_f32);
  msl_anim_timebase_apply_enter_tick_policy(batch, idx, policy);
}

// Pure animation timebase reset without invoking Fighter_ChangeMotionState side-effects.
//
// Used by debug/test helpers that want to manipulate the anim clock without updating identity
// (attack_id/attack_instance, instance_id).
static inline void msl_anim_timebase_restart(MslBatch* batch, size_t idx, float anim_start_f32,
                                             float anim_speed_f32) {
  msl_anim_timebase_enter_raw(batch, idx, anim_start_f32, anim_speed_f32);
}

static inline void msl_anim_timebase_set_rate(MslBatch* batch, size_t idx, float anim_rate_f32) {
  if (batch == NULL) {
    return;
  }
  batch->state.frame_speed_mul_fp_q16_16[idx] = msl_q16_16_from_f32(anim_rate_f32);
}

void anim_timebase_update_pre_input(MslBatch* batch);
void anim_timebase_seed_common_fall_blend(MslBatch* batch, size_t idx, int16_t action_frame);

// Apply deferred "tick once" requests (see MslState::anim_defer_tick_once).
void anim_timebase_apply_deferred_tick_once_pre_collision(MslBatch* batch);
void anim_timebase_apply_deferred_tick_once_post_combat(MslBatch* batch);
