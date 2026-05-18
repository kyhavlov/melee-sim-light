#pragma once

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "batch_internal.h"
#include "common_params.h"

enum {
  MSL_GUARD_STATE_FLAGS_2218_REFLECTING = 0x10,
  MSL_GUARD_STATE_FLAGS_221B_IS_SHIELD_ACTIVE = 0x80,
  MSL_GUARD_STATE_FLAGS_221C_B3 = 0x10,
  MSL_GUARD_STATE_FLAGS_221C_B2 = 0x20,
  MSL_GUARD_STATE_FLAGS_221C_B1 = 0x40,
};

typedef struct MslGuardReflectOwner {
  uint8_t is_guard_reflect;
  uint8_t no_submotion;
  uint8_t frozen_snapshot;
  uint8_t locomotion_entry_snapshot;
  uint8_t shield_entry_no_submotion;
  uint8_t reflectdesc_only;
  uint8_t active_x14_no_guardon_blocks_body;
  uint8_t final_x14_live_x18_blocks_body;
} MslGuardReflectOwner;

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

static inline uint8_t msl_guard_x10_visible_guardon_init_u8(const MslCommonParams* c) {
  // Slippi no-submotion GuardOn/GuardReflect snapshots expose the post-first-hold-tick value.
  // Same-frame GuardOn -> GuardSetOff contacts use msl_guard_x10_raw_init_u8() instead.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_800925A4}
  uint16_t t = msl_guard_x10_raw_init_u8(c);
  if (t > 0u) {
    t = (uint16_t)(t - 1u);
  }
  return (uint8_t)t;
}

static inline uint8_t msl_guard_lifecycle_action_has_shield_callback(uint16_t action_id) {
  return msl_action_is_live_shield_family(action_id);
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

static inline uint8_t msl_guard_reflect_entry_uses_guardon_pose_source(uint16_t action_id) {
  // Narrow proven source boundary: Dash/Landing -> GuardReflect enter through ftCo_80091A4C and
  // can use the immediate GuardOn current-pose ShieldDesc lane. Walk -> GuardReflect has a
  // replay-real ShieldBounced keepalive control and stays on the normal descriptor source until
  // its callback phase is separately proved.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4}
  return (action_id == (uint16_t)MSL_ACT_DASH || action_id == (uint16_t)MSL_ACT_LANDING) ? 1u : 0u;
}

static inline uint8_t msl_guard_reflect_prev_action_is_locomotion_source(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_WAIT:
    case (uint16_t)MSL_ACT_WALK_SLOW:
    case (uint16_t)MSL_ACT_WALK_MIDDLE:
    case (uint16_t)MSL_ACT_WALK_FAST:
    case (uint16_t)MSL_ACT_TURN:
    case (uint16_t)MSL_ACT_DASH:
    case (uint16_t)MSL_ACT_RUN:
    case (uint16_t)MSL_ACT_RUN_DIRECT:
    case (uint16_t)MSL_ACT_SQUAT:
    case (uint16_t)MSL_ACT_SQUAT_WAIT:
    case (uint16_t)MSL_ACT_SQUAT_RV:
    case (uint16_t)MSL_ACT_LANDING:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t msl_guard_reflect_is_frozen_snapshot(const MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.action_id[idx] != (uint16_t)MSL_ACT_GUARD_REFLECT) {
    return 0u;
  }
  // Frozen no-submotion replay snapshots before GuardReflect_Anim / ftCo_80093BC0 ownership.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
  enum { MSL_GUARD_REFLECT_FROZEN_ACTION_FRAME_MAX = -2 };
  return (batch->state.action_frame[idx] <= MSL_GUARD_REFLECT_FROZEN_ACTION_FRAME_MAX) ? 1u : 0u;
}

static inline uint8_t msl_guard_reflect_is_no_submotion(const MslBatch* batch, size_t idx) {
  return (batch != NULL && batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.action_frame[idx] < 0 && batch->state.animation_index[idx] == UINT32_MAX)
             ? 1u
             : 0u;
}

static inline uint8_t msl_guard_reflect_is_locomotion_entry_snapshot(const MslBatch* batch,
                                                                     size_t idx) {
  if (!msl_guard_reflect_is_no_submotion(batch, idx) || batch->state.action_frame[idx] != -1) {
    return 0u;
  }
  const uint16_t seed_prev_action = batch->state.seed_prev_action_id[idx];
  return (msl_guard_reflect_prev_action_is_locomotion_source(seed_prev_action) &&
          seed_prev_action != (uint16_t)MSL_ACT_GUARD_ON &&
          seed_prev_action != (uint16_t)MSL_ACT_GUARD &&
          seed_prev_action != (uint16_t)MSL_ACT_GUARD_REFLECT &&
          seed_prev_action != (uint16_t)MSL_ACT_GUARD_SET_OFF)
             ? 1u
             : 0u;
}

static inline uint8_t msl_guard_reflect_shield_entry_no_submotion(const MslBatch* batch,
                                                                  size_t idx) {
  return (msl_guard_reflect_is_no_submotion(batch, idx) &&
          batch->state.guard_reflect_timer_x14[idx] != 0u &&
          !msl_guard_reflect_is_locomotion_entry_snapshot(batch, idx))
             ? 1u
             : 0u;
}

static inline uint8_t msl_guard_reflect_x14_expired_this_callback_from_guardon(
    const MslBatch* batch, size_t idx) {
  return (batch != NULL && batch->state.guard_reflect_timer_x14_seed[idx] == 1u &&
          batch->state.guard_reflect_timer_x14[idx] == 0u &&
          batch->state.guard_reflect_origin_guardon[idx] != 0u)
             ? 1u
             : 0u;
}

static inline uint8_t msl_guard_reflect_reflectdesc_only(const MslBatch* batch, size_t idx) {
  if (!msl_guard_reflect_is_no_submotion(batch, idx)) {
    return 0u;
  }
  // Guard-origin GuardReflect entry creates ReflectDesc only; direct locomotion entry creates
  // ShieldDesc first. The x14_seed==1 -> x14==0 callback boundary has already recreated ShieldDesc.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
  return ((batch->state.guard_reflect_timer_x14_seed[idx] != 0u ||
           batch->state.guard_reflect_timer_x14[idx] != 0u) &&
          !msl_guard_reflect_x14_expired_this_callback_from_guardon(batch, idx) &&
          !msl_guard_reflect_is_locomotion_entry_snapshot(batch, idx) &&
          batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] == 0u)
             ? 1u
             : 0u;
}

static inline uint8_t msl_guard_reflect_active_x14_no_guardon_blocks_body(const MslBatch* batch,
                                                                          size_t idx) {
  if (!msl_guard_reflect_is_no_submotion(batch, idx)) {
    return 0u;
  }
  // Active-x14 no-submotion rows without GuardOn provenance are ReflectDesc-owned until
  // ftCo_80093BC0 reaches the expiry/recreate phase.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_8009388C,ftCo_80093A50,ftCo_GuardReflect_Anim,ftCo_80093BC0}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  return (batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] == 0u &&
          batch->state.guard_reflect_timer_x14[idx] != 0u &&
          batch->state.guard_reflect_origin_guardon[idx] == 0u &&
          batch->state.prev_action_id[idx] != (uint16_t)MSL_ACT_GUARD_ON &&
          batch->state.seed_prev_action_id[idx] != (uint16_t)MSL_ACT_GUARD_ON)
             ? 1u
             : 0u;
}

static inline uint8_t msl_guard_reflect_final_x14_live_x18_blocks_body(const MslBatch* batch,
                                                                       size_t idx) {
  if (batch == NULL || batch->state.action_id[idx] != (uint16_t)MSL_ACT_GUARD_REFLECT) {
    return 0u;
  }
  // Final-x14 GuardReflect still has the x18 powershield-active owner for this callback; do not let
  // no-submotion shield/BODY fallback consume the row until the next callback can expire x18.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092F2C}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC,ftColl_80076ED8}
  return (batch->state.action_frame[idx] == -1 && batch->state.animation_index[idx] == UINT32_MAX &&
          batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] == 0u &&
          batch->state.guard_reflect_timer_x14_seed[idx] == 0u &&
          batch->state.guard_reflect_timer_x14[idx] == 0u &&
          batch->state.guard_reflect_origin_guardon[idx] == 0u &&
          batch->state.guard_reflect_timer_x18_seed[idx] > 1u &&
          batch->state.guard_reflect_timer_x18[idx] != 0u)
             ? 1u
             : 0u;
}

static inline MslGuardReflectOwner msl_guard_reflect_owner_resolve(const MslBatch* batch,
                                                                   size_t idx) {
  MslGuardReflectOwner owner = {0};
  if (batch == NULL) {
    return owner;
  }
  owner.is_guard_reflect =
      (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT) ? 1u : 0u;
  if (!owner.is_guard_reflect) {
    return owner;
  }

  owner.no_submotion = msl_guard_reflect_is_no_submotion(batch, idx);
  owner.frozen_snapshot = msl_guard_reflect_is_frozen_snapshot(batch, idx);
  owner.locomotion_entry_snapshot = msl_guard_reflect_is_locomotion_entry_snapshot(batch, idx);
  owner.shield_entry_no_submotion = msl_guard_reflect_shield_entry_no_submotion(batch, idx);
  owner.reflectdesc_only = msl_guard_reflect_reflectdesc_only(batch, idx);
  owner.active_x14_no_guardon_blocks_body =
      msl_guard_reflect_active_x14_no_guardon_blocks_body(batch, idx);
  owner.final_x14_live_x18_blocks_body =
      msl_guard_reflect_final_x14_live_x18_blocks_body(batch, idx);
  return owner;
}
