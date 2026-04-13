#pragma once

#include <stddef.h>

#include "batch_internal.h"

void combat_resolve(MslBatch* batch);
void combat_processhit_consume(MslBatch* batch);
uint8_t combat_is_powershield_active_idx(const MslBatch* batch, size_t idx);
void combat_rng_trace_begin_frame(MslBatch* batch);
void combat_rng_trace_end_frame(MslBatch* batch);
float combat_rng_consume_randf_site(MslBatch* batch, int bi, uint16_t site_id);
int32_t combat_rng_consume_randi_site(MslBatch* batch, int bi, uint16_t site_id, int32_t max_val);

typedef struct MslThrowHitboxParams MslThrowHitboxParams;

typedef enum MslItemHitResult {
  MSL_ITEM_HIT_NONE = 0,
  // A real BODY hit was applied and the item should be consumed/despawned (e.g. laser on BODY hit).
  MSL_ITEM_HIT_APPLIED_CONSUME_ITEM = 1,
  // A real BODY hit was applied and the item should remain alive (e.g. Illusion/Phantasm body hit).
  MSL_ITEM_HIT_APPLIED_DONT_CONSUME = 2,
  // A collision-confirmed BODY hit was suppressed (e.g. attached Thrown*/Capture* victim),
  // and the item should NOT be consumed/despawned.
  MSL_ITEM_HIT_SUPPRESSED_DONT_CONSUME = 3,
} MslItemHitResult;

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
MslItemHitResult combat_apply_item_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                       uint16_t item_attack_id, uint16_t item_attack_instance,
                                       uint16_t item_instance_id, uint16_t item_type,
                                       uint8_t item_state, float damage, uint16_t angle,
                                       uint16_t kbg, uint16_t wsk, uint16_t bkb,
                                       uint8_t defender_hurt_height, uint8_t element);

// Apply an item->fighter SHIELD hit (shield HP depletion + GuardSetOff + defender hitlag).
//
// Intended for simple projectiles (e.g. Fox/Falco blaster lasers) where the item itself would
// normally take hitlag/deflection in Melee; we apply only defender-side effects.
void combat_apply_item_shield_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                  uint16_t item_attack_id, uint16_t item_attack_instance,
                                  float damage, int8_t hitbox_shield_damage, uint8_t hit_element,
                                  float item_pos_x);

// Debug/testing helper: run combat pass-1 selection (BODY-only, shield-safe, with rehit
// suppression) and write selected contacts into `out_contacts`.
//
// Deterministic ordering:
// attacker 0..num_players-1, defender 0..num_players-1 (skip attacker==defender),
// with at most 1 record per (attacker, defender) per call.
int combat_debug_select_body_hits(MslBatch* batch, int batch_index,
                                  MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                  uint16_t* out_count);

// Debug/testing helper: emit shield-candidate gate decisions for one batch row.
//
// This mirrors the fighter-vs-fighter shield path ownership/order in:
// - refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
// - refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
int combat_debug_shield_candidate_decisions(MslBatch* batch, int batch_index,
                                            MslDebugShieldCandidateDecision* out_rows,
                                            uint16_t max_rows, uint16_t* out_count);
