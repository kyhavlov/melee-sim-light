#pragma once

// Init-time loader for ItemCommonData constants used by item collision.
//
// Source of truth: `data/items/item_common.json` (ISO-derived from `MSL_DATA_DIR/raw/ItCo.dat`).
// Extractor: `tools/extraction/extract_item_common_data.py`.
//
// IMPORTANT: item_common_params_init() may do IO/allocations; call only during batch init.
// The per-frame hot path must remain alloc-free.

typedef struct MslItemCommonParams {
  // ftColl_8007A06C item-damage facing owner:
  // if abs(item->x40_vel.x) < x78, use item position; otherwise use item velocity sign.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
  // refs/melee/src/melee/it/types.h::ItemCommonData::x78_float
  float item_damage_facing_velocity_threshold;
  // Item_80269DC8 shield-bounce angle predicate:
  // item->xC54 < deg_to_rad(90 + it_804D6D28->unk_degrees)
  // refs/melee/src/melee/it/item.c::Item_80269DC8
  // refs/melee/src/melee/it/types.h::ItemCommonData::unk_degrees
  float shield_bounce_extra_degrees;
  float shield_bounce_threshold_radians;
  // Item hitlag scalar:
  // refs/melee/src/melee/it/it_26B1.c::it_8026B424
  // refs/melee/src/melee/it/types.h::ItemCommonData::{xB8,xBC}
  float item_hitlag_damage_mul;
  float item_hitlag_base;
  // it_80275158 sets xD48_halfLifeTimer = lifetime * ItemCommonData::x4C_float; the Needle
  // it_2725_Logic109_Reflected callback then assigns xD44_lifeTimer = xD48_halfLifeTimer, so a
  // reflected state-0 Needle's remaining life becomes spawn_life * this fraction.
  // refs/melee/src/melee/it/it_2725.c::{it_80275158,it_2725_Logic109_Reflected}
  // refs/melee/src/melee/it/types.h::ItemCommonData::x4C_float
  float reflect_half_life_fraction;
} MslItemCommonParams;

int item_common_params_init(void);
const MslItemCommonParams* msl_item_common_params(void);
