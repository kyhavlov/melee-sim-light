#pragma once

#include <stdint.h>

#include "batch_internal.h"
#include "item_article_params.h"

static inline uint8_t item_type_is_illusion_article(uint16_t type) {
  // MSLITAR1 side_special_illusion_itkind, loaded at init:
  // refs/melee/src/melee/it/forward.h::ItemKind
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_OnLoad
  // refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_OnLoad
  return item_article_params_is_illusion_item_type(type);
}

void items_update(MslBatch* batch);

// Runtime collision-demand predicate for consumers that need fighter hurtcap geometry before
// items_update() runs. This shares the item-kind source of truth with item-vs-fighter collision:
// supported blaster shots from data/items/lasers.bin plus Fox/Falco side-special ghost articles.
uint8_t items_row_has_fighter_collision_demand(const MslBatch* batch, int bi);

// Post-combat cleanup for item lanes that are owned by motion-state exits caused by combat
// transitions in the same frame.
void items_update_post_combat(MslBatch* batch);

// Fighter-driven item spawns (blaster guns + shots) that should use the pre-physics fighter pose/pos
// snapshot for the frame.
//
// Decomp ordering (GALE01):
// - Fighter anim callbacks run at proc prio 1 (Fighter_8006A360), before Fighter_procUpdate (prio 4).
// - Fox/Falco blaster loop anim callback spawns shots from cmd_vars[2] via it_8029C6A4.
// refs/melee/src/melee/ft/fighter.c::Fighter_Create_Inline2
// refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNLoop_Anim
// refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Anim
void items_spawn_pre_physics(MslBatch* batch);
