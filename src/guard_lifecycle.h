#pragma once

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "batch_internal.h"
#include "common_params.h"
#include "state_flags.h"

static inline uint8_t msl_guard_reflect_timer_x14_init(const MslCommonParams* c) {
  // Decomp: GuardReflect reflect window uses p_ftCommonData->x2A4, decremented by
  // ftCo_80093BC0 until it drops below zero. Runtime stores the same timer with a +1 bias.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80093BC0}
  if (c == NULL) {
    return 0u;
  }
  uint16_t t = (uint16_t)c->powershield_reflect_frames;
  t = (uint16_t)(t + 1u);
  return (uint8_t)(t > 255u ? 255u : t);
}

static inline uint8_t msl_guard_reflect_timer_x18_init(const MslCommonParams* c) {
  // Decomp: GuardReflect powershield-active window uses p_ftCommonData->x2B4, decremented by
  // ftCo_80093BC0 until it drops below zero. Runtime stores the same timer with a +1 bias.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80093BC0}
  if (c == NULL) {
    return 0u;
  }
  uint16_t t = (uint16_t)c->powershield_reflect_total_frames;
  t = (uint16_t)(t + 1u);
  return (uint8_t)(t > 255u ? 255u : t);
}

static inline uint8_t msl_guard_reflect_timer_after_anim_tick(uint8_t seed_timer,
                                                              uint8_t hitlag_started) {
  if (hitlag_started != 0u || seed_timer == 0u) {
    return seed_timer;
  }
  return (uint8_t)(seed_timer - 1u);
}

static inline uint8_t msl_guard_x10_raw_init_u8(const MslCommonParams* c) {
  // Decomp: mv.co.guard.x10 is initialized from p_ftCommonData->x268.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800921DC
  if (c == NULL || !(c->guard_x10_init_frames > 0.0f)) {
    return 0u;
  }
  const uint16_t t = (uint16_t)c->guard_x10_init_frames;
  return (uint8_t)(t > 255u ? 255u : t);
}

static inline uint8_t msl_guard_lifecycle_action_has_shield_callback(uint16_t action_id) {
  return msl_action_is_live_shield_family(action_id);
}

static inline uint8_t msl_guard_lifecycle_blocks_shield_recharge(const MslBatch* batch,
                                                                 size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // Shield recharge owner:
  // - Fighter_ProcessHit_8006D1EC runs:
  //     if (!fp->x221A_b7 && fp->shield_health < start) fp->shield_health += x27C;
  // - The gate is the live descriptor bit, not action or replay animation shape.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221A_INDEX;
  return (batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_221A_B7) != 0u ? 1u : 0u;
}

static inline void msl_guard_lifecycle_apply_shield_recharge(MslBatch* batch,
                                                             const MslCommonParams* c, size_t idx) {
  if (batch == NULL || c == NULL || batch->state.stocks[idx] == 0u) {
    return;
  }
  float hp = batch->state.shield_hp[idx];
  if (hp < c->start_shield_health) {
    hp += c->shield_recharge_per_frame;
    if (hp > c->start_shield_health) {
      hp = c->start_shield_health;
    }
    batch->state.shield_hp[idx] = hp;
  }
}

static inline uint8_t msl_guard_lifecycle_action_uses_guard_shield(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD:
    case MSL_ACT_GUARD_OFF:
    case MSL_ACT_GUARD_SET_OFF:
    case MSL_ACT_GUARD_REFLECT:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t msl_guard_lifecycle_action_updates_tilt(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD:
    case MSL_ACT_GUARD_REFLECT:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t msl_guard_action_entered_this_frame(const MslBatch* batch, size_t idx,
                                                          uint16_t action_id) {
  if (batch == NULL) {
    return 0u;
  }
  // Fighter_procUpdate runs only the frame-start MotionState's input callback. A destination entry
  // is therefore identified by the actual frame-start/live MotionState boundary, not a second
  // mutable "entered this frame" latch owned by each caller.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  return (uint8_t)(batch->state.action_id[idx] == action_id &&
                   batch->state.frame_start_action_id[idx] != action_id);
}

static inline uint16_t msl_guard_frame_source_action(const MslBatch* batch, size_t idx) {
  return batch != NULL ? batch->state.frame_start_action_id[idx] : 0u;
}

static inline void msl_guard_set_shield_desc_active(MslBatch* batch, size_t idx, uint8_t active) {
  if (batch == NULL) {
    return;
  }
  const size_t flags_221a_i =
      idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221A_INDEX;
  const size_t flags_221b_i =
      idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
  if (active != 0u) {
    batch->state.state_flags[flags_221a_i] |= (uint8_t)MSL_STATE_FLAG_221A_B7;
    batch->state.state_flags[flags_221b_i] |= (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
    batch->state.state_flags[flags_221b_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_B1;
  } else {
    batch->state.state_flags[flags_221a_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_B7;
    batch->state.state_flags[flags_221b_i] &=
        (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
  }
}

static inline uint8_t msl_guard_reflect_has_reflectdesc_only(const MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.action_id[idx] != (uint16_t)MSL_ACT_GUARD_REFLECT ||
      batch->state.guard_reflect_timer_x14[idx] == 0u) {
    return 0u;
  }
  // ftCo_8009388C installs ReflectDesc after clearing the ordinary ShieldDesc; ftCo_80093A50
  // recreates ShieldDesc on direct locomotion entry. The live x221B_b0 bit is the source identity.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_80093A50}
  const size_t flags_i = idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
  return (batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) == 0u
             ? 1u
             : 0u;
}
