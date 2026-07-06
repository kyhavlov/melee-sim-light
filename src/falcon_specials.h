#pragma once

#include <stdint.h>

#include "api.h"

// Captain Falcon (ftCa_*) character specials: Falcon Punch, Raptor Boost, Falcon Dive,
// Falcon Kick. Decomp-first port of refs/melee/src/melee/ft/chara/ftCaptain/ftCa_Special{N,S,Hi,Lw}.c.
// Movescripts own hitboxes/timing/windows (data/moves + data/scripts via move_tables);
// this module carries only state, counters, transitions, physics, and combat hooks.
//
// Port progress: SpecialN (Falcon Punch) is live; SpecialS/Hi/Lw dispatch zones are
// recognized but intentionally no-op until their decomp ports land (agent_docs/FALCON_PLAN.md).

// True for the falcon char-special action id range. 341..346 are the common item-swing
// states (ftCa_MS_SwordSwing4..LipstickSwing4); the specials proper are 347..363. Callers
// must gate on char_id == MSL_CHAR_ID_FALCON: the numeric range is shared with other
// characters' specials.
static inline uint8_t falcon_action_is_special(uint16_t action_id) {
  return (uint8_t)(action_id >= 347u && action_id <= 363u);
}

// Falcon special submotion ids from the generated MotionState table (MSLMSO01): actions
// 347..360 are a fixed offset (-46) onto msids 301..314; the LwEndAir pair crosses over
// (MS order AirLwEndAir(361)/LwEndAir(362) vs SM order LwEndAir(315)/AirLwEndAir(316));
// SpecialHiThrow1(363) -> 317.
// refs/melee/src/melee/ft/chara/ftCaptain/forward.h (ftCaptain_MotionState / ftCa_Submotion)
static inline uint16_t falcon_special_submotion(uint16_t action_id) {
  switch (action_id) {
    case 361u:
      return 316u;  // SpecialAirLwEndAir
    case 362u:
      return 315u;  // SpecialLwEndAir
    default:
      return (uint16_t)(action_id - 46u);
  }
}

// Entry dispatch (B-press routing) + per-action Anim/IASA/transition logic. Runs in the
// action phase alongside the other char-special modules.
void falcon_specials_update_pre_physics(MslBatch* batch);

// Per-player physics for falcon special actions. Returns 1 when this module owned the
// player's self-velocity update this frame (the generic physics path must then skip its own).
uint8_t falcon_specials_phys(MslBatch* batch, size_t idx);

// Collision-callback ground/air handling. Return 1 when this module owned the transition
// (frame-preserving variant swap, same-action kick phase flip, or kick landing entry);
// callers own the grounding/floor-loss bundle.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_Special{N,Lw}.c (Coll handlers)
uint8_t falcon_special_try_air_to_ground_swap(MslBatch* batch, size_t idx);
uint8_t falcon_special_try_ground_to_air_swap(MslBatch* batch, size_t idx);

// Falcon Kick deal_dmg_cb slowdown (once per frame with dealt damage; combat x1914 attacker
// paths call this; shield hits are excluded by source branch order).
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialHi_800E400C
void falcon_speciallw_on_deal_dmg_x1914(MslBatch* batch, size_t a_idx);

// Falcon Kick wall rebound (post-collision: script cmd0 window + wall hug in the facing
// direction -> airborne SpecialHiThrow1 backflip).
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Coll
uint8_t falcon_special_try_speciallw_wall_rebound(MslBatch* batch, size_t idx);
