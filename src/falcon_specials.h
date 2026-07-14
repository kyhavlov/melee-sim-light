#pragma once

#include <stdint.h>

#include "api.h"
#include "motion_state_owners.h"

// Captain Falcon (ftCa_*) character specials: Falcon Punch, Raptor Boost, Falcon Dive,
// Falcon Kick. Decomp-first port of refs/melee/src/melee/ft/chara/ftCaptain/ftCa_Special{N,S,Hi,Lw}.c.
// The live fighter-script cursor owns movescript hitboxes, timing, and command state;
// this module carries only state, counters, transitions, physics, and combat hooks.
//
// All four special families are live. Source-specific combat effects enter through the hooks below;
// collision admission and ProcessHit branch priority remain owned by combat.c.

// True for the falcon char-special action id range. 341..346 are the common item-swing
// states (ftCa_MS_SwordSwing4..LipstickSwing4); the specials proper are 347..363. Callers
// must gate on char_id == MSL_CHAR_ID_FALCON: the numeric range is shared with other
// characters' specials.
static inline uint8_t falcon_action_is_special(uint16_t action_id) {
  return (uint8_t)(action_id >= 347u && action_id <= 363u);
}

// Falcon action -> submotion identity is generated from ftCa_Init_MotionStateTable. In particular,
// SpecialAirLwEndAir/SpecialLwEndAir do not follow an arithmetic action-id offset, so the runtime
// must consume MSLMSO01 instead of preserving a second hand-coded map.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_Init.c::ftCa_Init_MotionStateTable
static inline uint16_t falcon_special_submotion(uint16_t action_id) {
  return msl_motion_state_submotion_id((uint8_t)MSL_CHAR_ID_FALCON, action_id);
}

// Entry dispatch (B-press routing) + per-action Anim/IASA/transition logic. Runs in the
// action phase alongside the other char-special modules.
void falcon_specials_update_pre_physics(MslBatch* batch);

// Replay-reseed reconstruction of the hidden Falcon Dive lanes: mv.ca.specialhi.vel is
// recovered from the seeded (replay-visible) self velocity minus the seeded frame's TransN
// delta (SpecialHi_Phys defines self_vel = TransN_delta + mv.vel each frame), and the
// attacker x221B_b7 attach-mode flag from the seeded hold linkage. Runs from the batch seed
// path after grab_attachment_reseed_init.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::ftCa_SpecialHi_Phys
void falcon_specials_reseed_init(MslBatch* batch, int batch_index);

// Per-player physics for falcon special actions. Returns 1 when this module owned the
// player's self-velocity update this frame (the generic physics path must then skip its own).
uint8_t falcon_specials_phys(MslBatch* batch, size_t idx);

// Collision-callback ground/air handling. Return 1 when this module owned the transition
// (frame-preserving variant swap, same-action kick phase flip, kick landing entry, or the
// SpecialAirLwEnd -> Fall handoff). Callers own the ordinary grounding/floor-loss bundle; the
// destination-specific Fall/FallSpecial paths apply their bundles inside the helper.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_Special{N,Lw}.c (Coll handlers)
uint8_t falcon_special_try_air_to_ground_swap(MslBatch* batch, size_t idx);
uint8_t falcon_special_try_ground_to_air_swap(MslBatch* batch, size_t idx);

// Falcon-specific Fighter_ProcessHit packet ledger. Contact producers only note their source lanes;
// combat_resolve consumes the aggregate once, after every fighter/item contact, so x18A0/x19A4/
// incoming-damage priority is independent of attacker/defender iteration order.
// The existing per-frame byte is an internal bitset and is cleared by the consume point.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialHi_800E400C
// refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
void falcon_specials_processhit_note_dealt_x1914(MslBatch* batch, size_t idx);
void falcon_specials_processhit_note_higher_priority(MslBatch* batch, size_t idx);
void falcon_specials_processhit_consume(MslBatch* batch);

// Falcon Kick wall rebound + Raptor Boost wall stop (post-collision: script cmd0 window +
// wall contact in the facing direction).
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialLw.c::ftCa_SpecialLw_Coll
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialSStart_Coll
uint8_t falcon_special_try_speciallw_wall_rebound(MslBatch* batch, size_t idx);

// Raptor Boost inert-hitbox detection (fp->unk_gobj -> hurtbox_detect_cb -> OnDetect Start->Hit).
// BODY and shield hooks are called only after their normal ftColl admission/narrowphase owners have
// accepted the contact; the ProcessHit consumer applies the source branch ladder afterward.
// refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialS.c::ftCa_SpecialS_OnDetect
void falcon_specials_on_inert_body_contact(MslBatch* batch, size_t a_idx);
uint8_t falcon_specials_item_kind_eligible(uint16_t item_kind);
void falcon_specials_on_inert_shield_contact(MslBatch* batch, size_t a_idx);
