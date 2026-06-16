#pragma once

#include <stddef.h>
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

// Item proc phases, named after the GALE01 item GObj procs registered in
// refs/melee/src/melee/it/item.c::Item_8026862C.
//
// Supported RL 1.0 item families route through explicit source owners:
// - fighter-owned article spawns run from the fighter Anim callback phase via
//   items_spawn_fighter_anim_phase();
// - supported article motion/collision runs in items_update_collision_phase();
// - item post-hit callbacks run in items_update_post_combat().
// Full generic GObj priority for unsupported item kinds is intentionally outside the current
// item-core gameplay scope.
void items_update_pre_fighter_anim_phase(MslBatch* batch);
void items_spawn_fighter_anim_phase(MslBatch* batch);
uint8_t items_spawn_sheik_chain_article(MslBatch* batch, size_t owner_idx);
uint8_t items_destroy_sheik_chain_article(MslBatch* batch, size_t owner_idx);
uint8_t items_spawn_sheik_vanish_smoke_article(MslBatch* batch, size_t owner_idx);
void items_update_collision_phase(MslBatch* batch);

// Runtime collision-demand predicate for consumers that need fighter hurtcap geometry before
// item-vs-fighter collision runs. This shares the item-kind source of truth with item-vs-fighter
// collision: supported blaster shots from data/items/lasers.bin plus Fox/Falco side-special ghost
// articles.
uint8_t items_row_has_fighter_collision_demand(const MslBatch* batch, int bi);

// Post-combat cleanup for item lanes that are owned by motion-state exits caused by combat
// transitions in the same frame.
void items_update_post_combat(MslBatch* batch);

// Compatibility names for older call sites while the phase split lands.
static inline void items_update(MslBatch* batch) { items_update_collision_phase(batch); }
static inline void items_spawn_pre_physics(MslBatch* batch) {
  items_spawn_fighter_anim_phase(batch);
}
