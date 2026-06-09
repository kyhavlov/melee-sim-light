#pragma once

#include <stdint.h>

#include "action_ids.h"
#include "common_params.h"

enum {
  MSL_DEADUPSTAR_STARTUP_EFFECT_PREFIX_MAX_ACTION_FRAME = 8,
  MSL_DEADUPSTAR_ACTIVE_EFFECT_PREFIX_MAX_ACTION_FRAME = 27,
  MSL_DEADUPSTAR_EFFECT_PREFIX_PHASE1_TAIL_FRAMES = 15,
};

static inline uint8_t msl_deadupstar_effect_prefix_phase_tail_active(
    uint8_t match_flow_timer, const MslCommonParams* common) {
  if (common == NULL) {
    return 0u;
  }
  const uint16_t live_tail = (uint16_t)common->dead_up_star_phase2_frames +
                             (uint16_t)MSL_DEADUPSTAR_EFFECT_PREFIX_PHASE1_TAIL_FRAMES;
  return (match_flow_timer > common->dead_up_star_phase2_frames &&
          (uint16_t)match_flow_timer <= live_tail)
             ? 1u
             : 0u;
}

static inline uint8_t msl_deadupstar_startup_effect_prefix_active(uint16_t action_id,
                                                                  int16_t action_frame,
                                                                  uint8_t match_flow_timer,
                                                                  const MslCommonParams* common) {
  if (common == NULL || action_id != (uint16_t)MSL_ACT_DEAD_UP_STAR) {
    return 0u;
  }
  if (match_flow_timer <= (uint8_t)common->dead_up_star_phase2_frames) {
    return 0u;
  }
  if (msl_deadupstar_effect_prefix_phase_tail_active(match_flow_timer, common) == 0u) {
    return 0u;
  }
  // DeadUpStar entry/early Anim owns a short-lived async visual generator before fighter Wait_Anim
  // can sample getAnimID. Source spawns effect kind 0x42D -> generator 0x121 from
  // ftCo_DeadUpStar_Anim; the generator bytecode/data is not extracted yet, so runtime admits only
  // this named source-live startup window in the final phase-1 tail before phase2. Earlier loop
  // passes, such as MAJ's timer-82 frame-4 row, must not carry the prefix.
  // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D40B8,ftCo_DeadUpStar_Anim}
  // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch case 0x42D
  // refs/melee/src/melee/ef/eflib.c::efLib_CreateGenerator case 0x121
  // refs/melee/src/sysdolphin/baselib/particle.c
  return action_frame <= MSL_DEADUPSTAR_STARTUP_EFFECT_PREFIX_MAX_ACTION_FRAME ? 1u : 0u;
}

static inline uint8_t msl_deadupstar_active_effect_prefix_before_wait(
    uint16_t action_id, int16_t action_frame, uint8_t match_flow_timer,
    const MslCommonParams* common) {
  if (common == NULL || action_id != (uint16_t)MSL_ACT_DEAD_UP_STAR) {
    return 0u;
  }
  if (msl_deadupstar_effect_prefix_phase_tail_active(match_flow_timer, common) == 0u) {
    return 0u;
  }
  // DeadUpStar's async effect kind 0x42D maps to generator 0x121. Startup frames are already owned
  // by the entry/early-Anim creation prefix above, plus the bounded earlier-player two-consume
  // prefix in locomotion_consume_deadupstar_effect_prefix_before_wait. The active-tail owner covers
  // the live generator tick that can still precede Wait getAnimID in the same player callback pass.
  // Runtime observes the other player's post-Anim frame at the Wait decision: TVR seed frame 26 is
  // runtime frame 27 and consumes one generator step, BHH seed frame 28 is runtime frame 29 and
  // remains the stale-late negative, and MAJ seed frame 4/timer 82 is outside the final phase-1
  // effect tail.
  // refs/melee/src/melee/ft/ft_0D31.c::ftCo_DeadUpStar_Anim
  // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch case 0x42D
  // refs/melee/src/melee/ef/eflib.c::efLib_CreateGenerator case 0x121
  // refs/melee/src/sysdolphin/baselib/particle.c
  return (action_frame <= MSL_DEADUPSTAR_ACTIVE_EFFECT_PREFIX_MAX_ACTION_FRAME) ? 1u : 0u;
}
