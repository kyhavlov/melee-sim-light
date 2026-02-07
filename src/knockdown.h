#pragma once

#include "batch_internal.h"

// Knockdown / downed state machine (DownBound/DownWait/DownStand/DownAttack).
//
// Decomp sources:
// - DownBound/DownWait: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c
// - DownStand: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownStand.c
// - DownAttack: refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownAttack.c
// - Damage landing -> DownBound: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll

// Per-frame knockdown updates that happen before physics integration:
// - per-state friction (ft_80084F3C)
// - anim-end transitions
// - IASA-style input transitions (DownWait -> Stand/Attack)
void knockdown_update_pre_physics(MslBatch* batch);

// Post-collision knockdown updates:
// - landing transitions from tumble-style damage into DownBound (Damage*_Coll paths)
// - grounded->air fallback for downed states (Down*_Coll paths)
void knockdown_update_post_collision(MslBatch* batch);

// Post-combat knockdown updates:
// - finalize DamageAir anim-end exits after combat has had a chance to overwrite state via hits.
void knockdown_update_post_combat(MslBatch* batch);
