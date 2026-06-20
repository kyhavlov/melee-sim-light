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
void items_update_sheik_chain_accessory_phase(MslBatch* batch);
void items_spawn_fighter_anim_phase(MslBatch* batch);
uint8_t items_spawn_sheik_held_needle_article(MslBatch* batch, size_t owner_idx);
void items_sheik_needle_damage_callback(MslBatch* batch, int batch_index, int owner,
                                        uint16_t pre_damage_action, uint8_t source_on_ground);
uint8_t items_spawn_sheik_chain_article(MslBatch* batch, size_t owner_idx);
uint8_t items_set_sheik_chain_article_state(MslBatch* batch, size_t owner_idx, uint8_t state);
uint8_t items_activate_sheik_chain_hitcaps_on_entry(MslBatch* batch, size_t owner_idx);
uint8_t items_destroy_sheik_chain_article(MslBatch* batch, size_t owner_idx);
// World-space position of Sheik Chain fighter HitCapsule `hitbox_id` (0..3), taken from the solved
// Verlet link the it_802BCB88 stride map assigns to it. Returns 1 and writes out_x/out_y/out_z if a
// live, solved Chain article is owned by `fighter_idx`; else 0 (caller keeps the script/bone
// position).
// refs/melee/src/melee/it/items/itseakchain.c::{it_802BC080,it_802BCB88}
uint8_t sheik_chain_hitbox_world_pos(const MslBatch* batch, size_t fighter_idx, uint8_t hitbox_id,
                                     float* out_x, float* out_y, float* out_z);
uint8_t sheik_chain_hitbox_stale_damage_mul(const MslBatch* batch, size_t fighter_idx,
                                            float* out_mul);
uint8_t sheik_chain_hitbox_reset_prev_active(const MslBatch* batch, size_t fighter_idx);
void sheik_chain_clear_hitbox_reset_prev(MslBatch* batch, size_t fighter_idx);
void items_reseed_clear_sheik_chain_hidden_slot(MslBatch* batch, size_t item_idx);
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
