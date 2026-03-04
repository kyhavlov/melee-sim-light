#pragma once

#include "batch_internal.h"

void items_update(MslBatch* batch);

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
