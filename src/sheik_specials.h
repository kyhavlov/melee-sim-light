#pragma once

#include <stddef.h>
#include <stdint.h>

#include "batch_internal.h"

// Sheik (ftSk_*) character specials. Numeric action ids overlap other characters, so callers must
// gate on char_id == MSL_CHAR_ID_SHEIK before interpreting this range.
// Decomp sources:
// - refs/melee/src/melee/ft/chara/ftSeak/forward.h
// - refs/melee/src/melee/ft/chara/ftSeak/ftSk_Special{N,S,Hi,Lw}.c

enum {
  MSL_ACT_SK_SPECIAL_N_START = 341,
  MSL_ACT_SK_SPECIAL_N_LOOP = 342,
  MSL_ACT_SK_SPECIAL_N_CANCEL = 343,
  MSL_ACT_SK_SPECIAL_N_END = 344,
  MSL_ACT_SK_SPECIAL_AIR_N_START = 345,
  MSL_ACT_SK_SPECIAL_AIR_N_LOOP = 346,
  MSL_ACT_SK_SPECIAL_AIR_N_CANCEL = 347,
  MSL_ACT_SK_SPECIAL_AIR_N_END = 348,
  MSL_ACT_SK_SPECIAL_S_START = 349,
  MSL_ACT_SK_SPECIAL_S = 350,
  MSL_ACT_SK_SPECIAL_S_END = 351,
  MSL_ACT_SK_SPECIAL_AIR_S_START = 352,
  MSL_ACT_SK_SPECIAL_AIR_S = 353,
  MSL_ACT_SK_SPECIAL_AIR_S_END = 354,
  MSL_ACT_SK_SPECIAL_HI_START_0 = 355,
  MSL_ACT_SK_SPECIAL_HI_START_1 = 356,
  MSL_ACT_SK_SPECIAL_HI = 357,
  MSL_ACT_SK_SPECIAL_AIR_HI_START_0 = 358,
  MSL_ACT_SK_SPECIAL_AIR_HI_START_1 = 359,
  MSL_ACT_SK_SPECIAL_AIR_HI = 360,
  MSL_ACT_SK_SPECIAL_LW = 361,
  MSL_ACT_SK_SPECIAL_LW_2 = 362,
  MSL_ACT_SK_SPECIAL_AIR_LW = 363,
  MSL_ACT_SK_SPECIAL_AIR_LW_2 = 364,
};

static inline uint8_t sheik_action_is_special(uint16_t action_id) {
  return (uint8_t)(action_id >= (uint16_t)MSL_ACT_SK_SPECIAL_N_START &&
                   action_id <= (uint16_t)MSL_ACT_SK_SPECIAL_AIR_LW_2);
}

void sheik_specials_update_pre_physics(MslBatch* batch);
void sheik_specials_update_accessory4_phase(MslBatch* batch);
uint8_t sheik_specials_phys(MslBatch* batch, size_t idx);
uint8_t sheik_special_try_landing_iasa(MslBatch* batch, size_t idx);
uint8_t sheik_special_try_vanish_travel_wallceil_end(MslBatch* batch, size_t idx);
uint8_t sheik_special_vanish_air_start1_platform_pass_active(const MslBatch* batch, size_t idx);
uint8_t sheik_special_try_air_to_ground_swap(MslBatch* batch, size_t idx);
uint8_t sheik_special_try_ground_to_air_swap(MslBatch* batch, size_t idx);
