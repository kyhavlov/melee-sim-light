#pragma once

#include "batch_internal.h"

void combat_resolve(MslBatch* batch);
void combat_processhit_consume(MslBatch* batch);

// Apply a single item->fighter BODY hit using decomp-shaped damage/hitlag/hitstun/state-entry math.
//
// Intended for simple projectiles (e.g. Fox/Falco blaster lasers) that resolve outside the
// fighter-vs-fighter hitbox pass.
void combat_apply_item_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                           uint16_t item_attack_id, uint16_t item_attack_instance, float damage,
                           uint16_t angle, uint16_t kbg, uint16_t wsk, uint16_t bkb,
                           uint8_t defender_hurt_height);

// Apply an item->fighter SHIELD hit (shield HP depletion + GuardSetOff + defender hitlag).
//
// Intended for simple projectiles (e.g. Fox/Falco blaster lasers) where the item itself would
// normally take hitlag/deflection in Melee; we apply only defender-side effects.
void combat_apply_item_shield_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                  uint16_t item_attack_id, uint16_t item_attack_instance,
                                  float damage, int8_t hitbox_shield_damage);

// Debug/testing helper: run combat pass-1 selection (BODY-only, shield-safe, with rehit
// suppression) and write selected contacts into `out_contacts`.
//
// Deterministic ordering:
// attacker 0..num_players-1, defender 0..num_players-1 (skip attacker==defender),
// with at most 1 record per (attacker, defender) per call.
int combat_debug_select_body_hits(MslBatch* batch, int batch_index,
                                  MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                  uint16_t* out_count);
