#pragma once

#include <stdint.h>

#include "action_ids.h"
#include "common_params.h"

enum {
  MSL_DEADUPSTAR_STARTUP_EFFECT_PREFIX_MAX_ACTION_FRAME = 8,
};

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
  // DeadUpStar entry/early Anim owns a short-lived async visual generator before fighter Wait_Anim
  // can sample getAnimID. Source spawns effect kind 0x42D -> generator 0x121 from
  // ftCo_DeadUpStar_Anim; the generator bytecode/data is not extracted yet, so runtime admits only
  // this named source-live startup window. Stale later phase-1 rows must not carry the prefix.
  // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D40B8,ftCo_DeadUpStar_Anim}
  // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch case 0x42D
  // refs/melee/src/melee/ef/eflib.c::efLib_CreateGenerator case 0x121
  // refs/melee/src/sysdolphin/baselib/particle.c
  return action_frame <= MSL_DEADUPSTAR_STARTUP_EFFECT_PREFIX_MAX_ACTION_FRAME ? 1u : 0u;
}
