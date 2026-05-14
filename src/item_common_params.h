#pragma once

// Init-time loader for ItemCommonData constants used by item collision.
//
// Source of truth: `data/items/item_common.json` (ISO-derived from `_iso/ItCo.dat`).
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
} MslItemCommonParams;

int item_common_params_init(void);
const MslItemCommonParams* msl_item_common_params(void);
