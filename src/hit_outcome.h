#pragma once

#include <stdint.h>

// -------------------------
// Decomp-backed outcome tags
// -------------------------
//
// This header defines decomp/asm-backed "hit type" codes used when adding victims to a HitCapsule
// rehit suppression list via:
// - lbColl_80008688 (HitCapsule.victims_1)
// - lbColl_80008820 (HitCapsule.victims_2)
//
// Decomp signature (GALE01):
//   bool lbColl_80008688(HitCapsule* capsule, int type, void* victim)
//   bool lbColl_80008820(HitCapsule* capsule, int type, void* victim)
// refs/melee/src/melee/lb/lbcollision.c
//
// The callsites in ft collision define the "type" argument values used for different collision
// outcomes. We use these values as stable tags for "what kind of hit happened", so that higher
// level systems (staling, counters, etc.) can decide what to update without baking in replay
// heuristics.
//
// Outcome typing table (source: asm/decomp only):
//
// | Type | Meaning (callsite context)                 | Decomp / ASM source pointer |
// |------|--------------------------------------------|-----------------------------|
// | 0    | BODY hitbox→hurt (damaging)                | refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8 (inlineB0(...,0,lbColl_*)) |
// | 1    | SHIELD contact (GuardSetOff / shield dmg)  | refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC (ftColl_80076808(...,1,...)) |
// | 3    | HITBOX↔HITBOX interaction (clank-like)      | refs/melee/src/melee/ft/ftcoll.c::ftColl_8007699C (inlineA0/inlineA1 use type=3) |
//
// IMPORTANT: This table is intentionally partial today.
// - lbColl_* has other "refresh timer" types {2,4,5,7,8} that control victim cooldown refresh in
//   HitCapsule hitlists. refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 / ::lbColl_80008820
// - We do not yet have a complete, decomp-backed mapping from all collision outcomes → these type
//   values, so do not treat the enum below as exhaustive.
//
// Corresponding GALE01 asm callsites (subset; same semantics):
// - type=0: refs/melee/build/GALE01/asm/melee/ft/ftcoll.s (lbColl_80008688 @ 0x80077234, lbColl_80008820 @ 0x80077090)
// - type=1: refs/melee/build/GALE01/asm/melee/ft/ftcoll.s (lbColl_80008688 @ 0x80076D08)
// - type=3: refs/melee/build/GALE01/asm/melee/ft/ftcoll.s (lbColl_80008688 @ 0x80076A9C, 0x80076BE8, 0x80077A70)
//
// Notes:
// - These type codes are NOT "hit element" (electric/fire/etc) and not "hit status" (invuln).
// - lbColl_* uses the type to decide whether to refresh per-victim cooldown timers (x40_b4).
//   For example, lbColl_80008688 refreshes for types {2,4,5,7,8} but not for {0,1,3}.
//   refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
typedef enum MslLbCollHitType {
  MSL_LBCOLL_HITTYPE_BODY = 0,
  MSL_LBCOLL_HITTYPE_SHIELD = 1,
  MSL_LBCOLL_HITTYPE_CLANK = 3,
} MslLbCollHitType;

// Higher-level "outcome class" used for deciding which subsystems to update.
typedef enum MslHitOutcomeClass {
  // BODY (damaging) hitbox→hurtbox outcomes that should update damage-attribution driven systems.
  MSL_HIT_OUTCOME_DAMAGING = 0,
  // Shield contact outcomes (do not update staling queue in GALE01).
  MSL_HIT_OUTCOME_SHIELD = 1,
  // Hitbox↔hitbox (clank-like) outcomes.
  MSL_HIT_OUTCOME_CLANK = 2,
  // Unknown / not yet classified.
  MSL_HIT_OUTCOME_OTHER = 3,
} MslHitOutcomeClass;

static inline MslHitOutcomeClass msl_hit_outcome_class_from_lb_type(int lb_type) {
  switch (lb_type) {
    case MSL_LBCOLL_HITTYPE_BODY:
      return MSL_HIT_OUTCOME_DAMAGING;
    case MSL_LBCOLL_HITTYPE_SHIELD:
      return MSL_HIT_OUTCOME_SHIELD;
    case MSL_LBCOLL_HITTYPE_CLANK:
      return MSL_HIT_OUTCOME_CLANK;
    default:
      return MSL_HIT_OUTCOME_OTHER;
  }
}
