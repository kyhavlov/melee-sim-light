#pragma once

#include <stddef.h>

#include "batch_internal.h"

void combat_resolve(MslBatch* batch);
void combat_processhit_resolve(MslBatch* batch);
int combat_processhit_pending_init(MslBatch* batch);
void combat_processhit_pending_free(MslBatch* batch);
void combat_processhit_pending_begin(MslBatch* batch);
uint8_t combat_is_powershield_active_idx(const MslBatch* batch, size_t idx);
// Publish the fighter x1914 dealt-damage path: attacker hitlag plus any installed deal_dmg_cb.
// refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
void combat_apply_deal_hitlag_raw_damage(MslBatch* batch, size_t idx, int damage);
int combat_get_env_dmg(float dmg);
int combat_hitbox_collision_env_damage(const MslBatch* batch, size_t idx, size_t hb_i);
void combat_apply_deal_hitlag_hitbox_damage(MslBatch* batch, size_t idx, size_t hb_i);
void combat_apply_min_hitlag_frames(MslBatch* batch, size_t idx, uint16_t hitlag_frames);
void combat_rng_trace_begin_frame(MslBatch* batch);
void combat_rng_trace_end_frame(MslBatch* batch);
void combat_rng_use_next_replay_frame_seed_if_unconsumed(MslBatch* batch, int bi);
void combat_rng_set_replay_frame_seed_if_unconsumed(MslBatch* batch, int bi, uint32_t seed);
float combat_rng_consume_randf_site(MslBatch* batch, int bi, uint16_t site_id);
int32_t combat_rng_consume_randi_site(MslBatch* batch, int bi, uint16_t site_id, int32_t max_val);
void combat_rng_consume_step_site(MslBatch* batch, int bi, uint16_t site_id);

typedef struct MslThrowHitboxParams {
  float damage;
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t element;
  uint8_t sfx_kind;
  uint8_t sfx_severity;
} MslThrowHitboxParams;

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
uint8_t combat_apply_throw_hit_falcon_dive_release(MslBatch* batch, int batch_index, int attacker,
                                                   int defender, const MslThrowHitboxParams* p);
uint8_t combat_apply_falcon_dive_capture_break_hit(MslBatch* batch, int batch_index, int falcon,
                                                   int victim);

// Apply a single item->fighter BODY hit using decomp-shaped damage/hitlag/hitstun/state-entry math.
//
// Intended for simple projectiles (e.g. Fox/Falco blaster lasers) that resolve outside the
// fighter-vs-fighter hitbox pass.
MslItemHitResult combat_apply_item_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                       uint16_t item_attack_id, uint16_t item_attack_instance,
                                       uint16_t item_instance_id, uint16_t item_type,
                                       uint8_t item_state, float damage, uint16_t angle,
                                       uint16_t kbg, uint16_t wsk, uint16_t bkb,
                                       uint8_t defender_hurt_height, uint8_t element,
                                       float stale_mult_override, float item_pos_x,
                                       float item_vel_x, uint8_t item_damage_facing_owner_valid);

// Apply an item/fighter phantom BODY contact: victim hitlag and source attribution only; no
// percent, KB, damage-state entry, stale queue, or item consume.
void combat_apply_item_phantom_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                   uint16_t item_attack_id, uint16_t item_instance_id, float damage,
                                   uint8_t element);

// Apply the source attribution/victim-ring side of an item phantom BODY contact when source
// no-damage guards suppress hitlag/damage-state entry.
void combat_apply_item_phantom_attribution(MslBatch* batch, int batch_index, int attacker,
                                           int defender, uint16_t item_instance_id);

// Apply an item->fighter SHIELD hit (shield HP depletion + GuardSetOff + defender hitlag).
//
// Intended for simple projectiles (e.g. Fox/Falco blaster lasers) where the item itself would
// normally take hitlag/deflection in Melee; we apply only defender-side effects.
void combat_apply_item_shield_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                  uint16_t item_attack_id, uint16_t item_attack_instance,
                                  float damage, int8_t hitbox_shield_damage, uint8_t hit_element,
                                  float item_pos_x);

// Shared lbColl_8000805C narrowphase for any fighter or item HitCapsule segment against the
// priority-9 published fighter HurtCapsule/JObj matrix.
// refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
uint8_t combat_hurtcap_segment_overlap_lbcoll(const MslBatch* batch, int batch_index, int defender,
                                              int cap_id, float hit_start_x, float hit_start_y,
                                              float hit_start_z, float hit_end_x, float hit_end_y,
                                              float hit_end_z, float hit_radius,
                                              uint8_t flatten_hurt_z, float* out_overlap_amount,
                                              uint8_t* out_evaluated);

// Debug/testing helper: run combat pass-1 selection (BODY-only, shield-safe, with rehit
// suppression) and write selected contacts into `out_contacts`.
//
// Deterministic ordering:
// attacker 0..num_players-1, defender 0..num_players-1 (skip attacker==defender),
// with at most 1 record per (attacker, defender) per call.
int combat_debug_select_body_hits(MslBatch* batch, int batch_index,
                                  MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                  uint16_t* out_count);

// Debug-only: compute the current matrix-radius BODY overlap helper for any fighter hitbox/hurtcap
// pair. This is instrumentation for Dolphin lbColl_8000805C/80006E58 comparisons; it is not a
// gameplay admission bridge.
int combat_debug_body_matrix_overlap(const MslBatch* batch, int batch_index, int attacker,
                                     int hb_id, int defender, int cap_id, float* out_overlap);
