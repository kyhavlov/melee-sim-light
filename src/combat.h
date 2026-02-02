#pragma once

#include "batch_internal.h"

void combat_resolve(MslBatch* batch);
void combat_processhit_consume(MslBatch* batch);

typedef struct MslThrowHitboxParams MslThrowHitboxParams;

// Apply a throw hit (Throw* -> Thrown* victim), using extracted set_throw_hitbox params and the
// same decomp-shaped percent/hitlag/KB/hitstun/state-entry machinery as combat_resolve.
//
// Returns 1 if defender damage/KB/state entry is applied; 0 if the hit is suppressed by
// invincibility/intangibility gating (attacker hitlag may still apply).
uint8_t combat_apply_throw_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                               const MslThrowHitboxParams* p);

// Apply a single item->fighter BODY hit using decomp-shaped damage/hitlag/hitstun/state-entry math.
//
// Intended for simple projectiles (e.g. Fox/Falco blaster lasers) that resolve outside the
// fighter-vs-fighter hitbox pass.
void combat_apply_item_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                           uint16_t item_attack_id, uint16_t item_attack_instance,
                           uint16_t item_instance_id, uint16_t item_type, float damage,
                           uint16_t angle, uint16_t kbg, uint16_t wsk, uint16_t bkb,
                           uint8_t defender_hurt_height, uint8_t element);

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
