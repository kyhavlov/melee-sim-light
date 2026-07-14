#pragma once

#include <stdint.h>

#include "batch_internal.h"

typedef enum MslGuardShieldContactSource {
  MSL_GUARD_CONTACT_FIGHTER = 0,
  MSL_GUARD_CONTACT_ITEM = 1,
} MslGuardShieldContactSource;

typedef struct MslGuardShieldContact {
  int max_int_damage;
  int shield_damage_taken;
  float source_pos_x;
  uint8_t hit_element;
  MslGuardShieldContactSource source;
} MslGuardShieldContact;

typedef struct MslGuardShieldContactResult {
  float lightshield_amount;
  uint16_t defender_motion_id;
  uint16_t defender_hitlag;
} MslGuardShieldContactResult;

// Consume the defender-side shield contact packet and enter GuardSetOff. Fighter and item
// collision have distinct x19A0 accumulation rules, but Fighter_ProcessHit consumes both through
// the same ftCo_80092F2C owner.
// refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80077688}
// refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
// refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
uint8_t fighter_guard_apply_shield_contact(MslBatch* batch, int batch_index, int defender,
                                           const MslGuardShieldContact* contact,
                                           MslGuardShieldContactResult* result);
