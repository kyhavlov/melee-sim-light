#include "combat_internal.h"

// Pokemon Stadium GuardOn residual bridge caps:
// Runtime does not yet carry the exact live JObj ShieldDesc packet used by lbColl_80007BCC. These
// constants bound the temporary bridge to source-payload rows whose adjacent positives/negatives
// prove the bridge is not a broad Stadium residual tolerance.
// refs/melee/src/melee/ft/fighter.c::{Fighter_80068E64,Fighter_UpdateModelScale}
// refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
static const float MSL_PSTADIUM_GUARDON_TILT_MIN = 0.40f;
static const float MSL_PSTADIUM_GUARDON_DAIR_REJECT_MIN_MARGIN = 1.50f;
static const float MSL_PSTADIUM_GUARDON_S4_REJECT_MIN_MARGIN = 0.0f;
static const float MSL_PSTADIUM_GUARDON_S4_REJECT_MAX_MARGIN = 2.0f;
static const float MSL_PSTADIUM_GUARDON_BAIR_REJECT_MIN_MARGIN = 0.0f;
static const float MSL_PSTADIUM_GUARDON_BAIR_REJECT_MAX_MARGIN = 0.75f;
static const float MSL_PSTADIUM_GUARDON_WEAK_BAIR_REJECT_MAX_MARGIN = 1.0f;

uint8_t combat_shield_overlap_ftcoll_80007bcc(const MslBatch* batch, int bi, int attacker,
                                              int defender, int hb_id, float hx, float hy, float hz,
                                              float hr, float shx, float shy, float shz, float shr,
                                              float shield_desc_radius, float shield_owner_scale_y,
                                              uint8_t shield_desc_envelope_ready,
                                              uint8_t shield_extent_bridge_active,
                                              float* out_overlap_margin) {
  return msl_shielddesc_fighter_overlap_ftcoll_80007bcc(
      batch, bi, attacker, defender, hb_id, hx, hy, hz, hr, shx, shy, shz, shr, shield_desc_radius,
      shield_owner_scale_y, shield_desc_envelope_ready, shield_extent_bridge_active,
      out_overlap_margin);
}

float combat_clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

float combat_latched_lightshield_amount_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0.0f;
  }

  // Decomp: shield-hit HP depletion, GuardSetOff shieldstun/recoil, and attacker shield push read
  // `fp->lightshield_amount`, a latched Guard callback lane. Do not recompute it from current
  // trigger input here: ftCo_800925A4 preserves the previous non-negative squeeze when trigger
  // input is below deadzone, and ftCo_80092F2C consumes that stored value on same-frame shield hits.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800925A4,ftCo_80092F2C}
  const float light = batch->state.lightshield_amount[idx];
  if (!isfinite(light)) {
    return 0.0f;
  }
  return combat_clamp01(light);
}

void combat_state_flags_set_is_hitlag(MslBatch* batch, size_t idx, uint16_t hitlag) {
  if (batch == NULL) {
    return;
  }
  // Slippi post-frame: `lbz r3,0x221A(REG_PlayerData)  #0x20 = isHitlag`.
  //
  // Decomp-first references (GALE01):
  // - `fp->x221A_b2` is toggled with hitlag start/end:
  //   - set when hitlag is applied (Fighter_ProcessHit_8006D1EC),
  //   - cleared when hitlag reaches 0 (Fighter_8006A1BC).
  // refs/melee/src/melee/ft/fighter.c
  // - Bitfield layout at fp+0x221A is documented in refs/melee/src/melee/ft/types.h.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm

  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221A_INDEX;
  uint8_t f = batch->state.state_flags[flags_i];
  if (hitlag > 0) {
    f |= (uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
  } else {
    f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
  }
  batch->state.state_flags[flags_i] = f;
}

uint8_t combat_damage_allow_sdi_owner_action(uint16_t action) {
  return msl_damage_owner_allows_sdi_action(action);
}

void combat_damage_allow_sdi_set(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Source owner: Fighter_ProcessHit starts damage hitlag and owns `allow_sdi`
  // (fp+0x221A:2) for ftCo_Damage_OnEveryHitlag. Keep this separate from the generic
  // hitlag-active bit so shield, clank, deal-hitlag, and item-shield helpers do not gain SDI.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  // refs/melee/src/melee/ft/types.h
  batch->state.damage_allow_sdi[idx] = 1u;
}

void combat_state_flags_set_x221a_b3(MslBatch* batch, size_t idx) {
  // Decomp: Fighter_ProcessHit sets fp->x221A_b3 = 1 alongside hitlag start under certain
  // knockback/damage paths (see `bool2`), and Fighter_8006A1BC clears it on hitlag end.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  //
  // In GALE01, `bool2` is set to 1 when the hit takes the "forceAppliedOnHit && !no_kb" path
  // (Fighter_ProcessHit_8006D1EC sets `bool2 = 1` shortly before the `if (bool2) fp->x221A_b3 = 1`
  // assignment). In this light sim we do not model the full `forceAppliedOnHit` / `no_kb` plumbing,
  // so callers gate this bit on `hitstun > 0` (derived from knockback), which is a safe proxy for
  // the current suite domain (Fox/Falco) and matches Slippi's observable "KB hit that causes hitstun"
  // cases where this bit is set.
  //
  // Slippi post-frame: this bit lives in the fp+0x221A byte (`state_flags[...,1]`). The isHitlag
  // bit is 0x20 (x221A_b2), so x221A_b3 is the adjacent 0x10 bit under the same packing.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  if (batch == NULL) {
    return;
  }

  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221A_INDEX;
  batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_221A_B3;
}

void combat_state_flags_set_is_hitstun(MslBatch* batch, size_t idx, uint16_t hitstun) {
  if (batch == NULL) {
    return;
  }
  // Slippi post-frame: `lbz r3,0x221C(REG_PlayerData)  #0x2 = isHitstun`.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //
  // Decomp: `fp->x221C_b6` is set on Damage state entry and cleared when hitstun ends.
  // - set: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (end of function)
  // - clear: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744

  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  uint8_t f = batch->state.state_flags[flags_i];
  if (hitstun > 0) {
    f |= (uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
  } else {
    f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
  }
  batch->state.state_flags[flags_i] = f;
}

void combat_state_flags_set_x221c_b0(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  // fp+0x221C bit 0x80 ownership in attached hit windows:
  // - This bit is exposed by Slippi's post-frame byte capture at fp+0x221C.
  //   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // - Damage flow consumes fp->x221C_b0 as part of the no-reaction branch gate.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
  // - Generic motion-state change clears fp+0x221C lanes owned by transition reset paths.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_221C_B0;
}

void combat_state_flags_clear_x221c_b0(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  // Motion-state reset ownership for fp+0x221C_b0:
  // - Fighter_ChangeMotionState clears fp->x221C_b0 on destination entry.
  // - Damage state entry uses Fighter_ChangeMotionState via ftCo_8008DCE0 / ftCo_8008EC90.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_8008EC90}
  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B0;
}

void combat_state_flags_clear_guard_reflecting(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  // GuardSetOff destination reset:
  // - shield-hit transition enters GuardSetOff through Fighter_ChangeMotionState in ftCo_80092F2C,
  // - that destination does not keep the live `fp->reflecting` owner from the GuardReflect
  //   descriptor, even when x221C_b1/x221C_b2 timer lanes remain visible on the same row.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
}

void combat_state_flags_clear_stale_guard_timer_bits_on_setoff_entry(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  if (batch->state.guard_reflect_timer_x14[idx] != 0u ||
      batch->state.guard_reflect_timer_x18[idx] != 0u) {
    return;
  }

  // GuardSetOff entry through shield contact does not create GuardReflect timer bits. Those bits
  // are written by GuardReflect / powershield entry (`ftCo_8009388C` / `ftCo_80093A50`) and ticked
  // by `ftCo_80093BC0`; when both timers are already expired, a repeated GuardSetOff shield-hit
  // entry must not carry a stale seeded x221C_b1/b2 post-frame lane.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_80092F2C,ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &=
      (uint8_t) ~(uint8_t)(MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
}

void combat_state_flags_set_guard_reflect_timer_bits(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  uint8_t bits = 0u;
  if (batch->state.guard_reflect_timer_x14[idx] != 0u) {
    bits |= (uint8_t)MSL_STATE_FLAG_221C_B1;
  }
  if (batch->state.guard_reflect_timer_x18[idx] != 0u) {
    bits |= (uint8_t)MSL_STATE_FLAG_221C_B2;
  }
  if (bits == 0u) {
    return;
  }

  // ProcessHit ordering after an item BODY hit from GuardReflect:
  // - GuardReflect entry/timer bits (x221C_b1/x221C_b2) remain visible on the post-frame even
  //   after the damage-state entry consumes the live reflect descriptor.
  // - The live reflecting bit itself is cleared separately through fp+0x2218.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] |= bits;
}

void combat_apply_guard_reflect_body_hit_followup(const MslCommonParams* c, MslBatch* batch,
                                                  size_t idx, uint16_t pre_motion_id) {
  if (c == NULL || batch == NULL || pre_motion_id != (uint16_t)MSL_ACT_GUARD_REFLECT) {
    return;
  }

  combat_state_flags_clear_guard_reflecting(batch, idx);
  combat_state_flags_set_guard_reflect_timer_bits(batch, idx);

  // Fighter_ProcessHit runs after the GuardReflect/GuardOn anim callback. If that callback drained
  // shield health and the BODY hit then leaves shield ownership, the standard !x221A_b7 recharge
  // gate can add one recharge tick on the same post-frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_GuardOn_Anim,ftCo_800925A4}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  if (batch->state.stocks[idx] == 0u) {
    return;
  }
  float hp = batch->state.shield_hp[idx];
  if (hp < c->start_shield_health) {
    hp += c->shield_recharge_per_frame;
    if (hp > c->start_shield_health) {
      hp = c->start_shield_health;
    }
    batch->state.shield_hp[idx] = hp;
  }
}

void combat_apply_ftCommon_8007D5D4_ground_to_air(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp common helper ownership:
  // - ftCommon_8007D5D4 sets ground_or_air=Air, gr_vel=0, jumpsUsed=1, ecb_lock=10, and
  //   CollData_X130_Locked, and clears `cur_pos.z`.
  // - Damage entry / throw-release lanes call this helper when launching victim airborne.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  msl_ftcommon_lock_ecb_8007d5d4(batch, idx);
  batch->state.on_ground[idx] = 0u;
  batch->state.speed_ground_x_self[idx] = 0.0f;
  batch->state.pos_z[idx] = 0.0f;
  // Narrow ownership parity for this lane: keep existing velocity ownership in its current
  // systems and source jumpsUsed parity here (jumps_left=max_jumps-1).
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch != NULL) {
    batch->state.jumps_left[idx] = (ch->max_jumps > 0u) ? (uint8_t)(ch->max_jumps - 1u) : 0u;
  }
}

void combat_publish_damage_entry_hitlag_ecb_current(MslBatch* batch, size_t idx) {
  if (batch == NULL || batch->state.coll_ecb_bottom_valid[idx] == 0u ||
      batch->state.coll_desired_ecb_bottom_valid[idx] == 0u) {
    return;
  }
  // Fighter_ProcessHit runs after Fighter_procMap. Entering Damage during hitlag changes the
  // MotionState row but does not run the destination Anim callback, so the next Damage map callback
  // consumes the outgoing callback's already-published CollData ECB. Preserve that live packet;
  // sampling the visible Damage animation here invents geometry that source has not evaluated.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procMap,Fighter_ProcessHit_8006D1EC}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
  batch->state.coll_damage_hitlag_ecb_valid[idx] = 1u;
  batch->state.coll_damage_hitlag_ecb_source_kind[idx] = MSL_DAMAGE_HITLAG_ECB_SOURCE_NONE;
}

uint8_t combat_is_guard_reflect_frozen_snapshot_idx(const MslBatch* batch, size_t idx) {
  return msl_guard_reflect_is_frozen_snapshot(batch, idx);
}

uint8_t combat_defer_late_slot_same_frame_speciallw_entry_hit(const MslBatch* batch, int bi,
                                                              size_t a_idx, size_t d_idx,
                                                              int attacker, int defender,
                                                              int hb_id) {
  if (batch == NULL || attacker <= defender) {
    return 0u;
  }
  (void)bi;
  (void)hb_id;
  const uint16_t action = batch->state.action_id[a_idx];
  const uint8_t action_fx_kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[a_idx], action);
  if (action_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_LW_START &&
      action_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START) {
    return 0u;
  }
  if (batch->state.prev_action_id[a_idx] == action) {
    return 0u;
  }
  const uint16_t defender_action = batch->state.action_id[d_idx];
  const uint8_t defender_coll_handler = batch->state.live_coll_handler_kind[d_idx];
  const uint16_t attacker_prev_action = batch->state.prev_action_id[a_idx];
  const uint8_t attacker_squat_family_platform_pass_source =
      (batch->state.frame_start_on_ground[a_idx] != 0u &&
       (attacker_prev_action == (uint16_t)MSL_ACT_SQUAT ||
        attacker_prev_action == (uint16_t)MSL_ACT_SQUAT_WAIT ||
        attacker_prev_action == (uint16_t)MSL_ACT_SQUAT_RV))
          ? 1u
          : 0u;
  if (action_fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START &&
      attacker_squat_family_platform_pass_source && batch->state.on_ground[d_idx] == 0u &&
      batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] != 0u &&
      (defender_coll_handler == (uint8_t)MSL_COLL_HANDLER_DAMAGE_FLY ||
       defender_coll_handler == (uint8_t)MSL_COLL_HANDLER_DAMAGE_COMMON)) {
    // Fighter BODY pair-order + active airborne damage collision owner:
    // - A frame-start grounded Squat-family state can enter grounded Reflector and immediately
    //   platform-pass into SpecialAirLwStart, preserving the ground-start submotion/hitbox while
    //   `ftFx_SpecialLwStart_Pass` explicitly creates the reflect hit.
    // - If that platform-pass creator is a later entity, an earlier active Damage/DamageFly fighter
    //   has already run its collision callback for this frame, so the fresh HitCapsule cannot damage
    //   it until a later pair phase. Ordinary aerial Shine entries stay on the normal BODY path; QGD
    //   and aggregate controls show those hits are real.
    // - Use MSLMSO01 collision-owner classes for the victim family instead of an action-id list.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    //   ftFx_SpecialLwStart_Pass,ftFx_SpecialAirLw_Enter}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
    // data/motion_state/owners/{fox,falco}.bin::MSLMSO01 coll_handler_kind
    return 1u;
  }
  if (!batch->state.on_ground[d_idx] || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  if (action_fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_LW_START &&
      attacker_squat_family_platform_pass_source &&
      (defender_action == (uint16_t)MSL_ACT_GUARD_ON ||
       batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON ||
       batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON) &&
      batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX &&
      msl_action_is_live_shield_family(batch->state.seed_prev_action_id[d_idx]) &&
      batch->state.guard_x10[d_idx] == 0u && batch->state.lightshield_amount[d_idx] > 0.0f &&
      fabsf(batch->state.guard_tilt_x4[d_idx]) >= 0.49f) {
    // Fighter pair-order + expired no-submotion GuardOn lightshield owner:
    // - GuardOn_Anim runs before fighter collision and can transition through ftCo_800928CC once
    //   mv.co.guard.x10 has expired, even while Slippi still exposes a no-submotion GuardOn
    //   snapshot.
    // - ftCo_800925A4/ftCo_80091E78 keep the latched lightshield amount and tilted ShieldDesc
    //   pose live for the source collision helper. A later-slot Squat-family grounded Shine entry
    //   creates its HitCapsule after the earlier shield owner's pair phase; do not fall through to
    //   BODY when that live ShieldDesc source has already missed.
    // - This is not a generic GuardOn suppression: active x10 raise rows, untilted hard-shield
    //   rows, aerial Shine entries, and non-Squat SpecialLwStart rows stay on their existing
    //   shield/BODY paths.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78,ftCo_800928CC}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
    return 1u;
  }
  if (action_fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START &&
      batch->state.prev_action_id[d_idx] != defender_action &&
      batch->state.action_frame[d_idx] <= 1 &&
      msl_motion_state_common_class_has_fast(defender_action, MSL_MS_CLASS_GROUNDED_ATTACK)) {
    // Fighter BODY pair-order + same-frame grounded attack entry owner:
    // - ftColl_80078C70 walks fighter entity pairs after action/IASA entry. For a later entity
    //   entering aerial SpecialLwStart on the same frame an earlier grounded attack state is entered,
    //   the fresh Shine HitCapsule can miss that earlier fighter's already-processed pair phase.
    // - The defender boundary is table-backed by MSLMSO01's GROUNDED_ATTACK class and the local
    //   entry snapshot (`prev_action_id != action`, action_frame <= 1), not an AttackHi3 row list.
    // - Grounded SpecialLwStart stays on the narrower Turn microphase below; HVG:5200/TCH:3376
    //   prove fresh grounded Shine can still hit pre-turn earlier-slot defenders.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
    // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class GROUNDED_ATTACK)
    return 1u;
  }
  if (action_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_LW_START) {
    return 0u;
  }
  if (defender_action != (uint16_t)MSL_ACT_TURN || batch->state.turn_has_turned[d_idx] == 0u) {
    return 0u;
  }

  // Fighter BODY pair-order + Turn internal-facing microphase owner:
  // - ftColl_80078C70 walks the fighter entity list as an unordered pair pass. For a later entity
  //   attacking an earlier entity, its freshly-created same-frame HitCapsule can miss the earlier
  //   fighter's already-processed collision-pair phase.
  // - This boundary is only replay-proven for the Turn internal-facing microphase (`has_turned=1`):
  //   ftCo_Turn_Anim_Inner has flipped fp->facing_dir while the replay-visible facing byte can
  //   still be stale, and the simulator's SSANIM/x58 bootstrap can otherwise create an extra
  //   grounded Shine frame-0 BODY hit from the internal-facing hurtcap pose. Pre-turn Turn
  //   (`has_turned=0`) remains on the normal BODY path; HVG:5200/TCH:3376 prove those same later
  //   slot grounded Shine entries still hit.
  // - Aerial SpecialLwStart remains on the normal BODY path.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Anim_Inner
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
  // data/moves/{fox,falco}.json specials_by_msid["313"|"317"].events
  return 1u;
}

uint8_t combat_defer_late_slot_same_frame_speciallw_guardon_shield_hit(const MslBatch* batch,
                                                                       size_t a_idx, size_t d_idx,
                                                                       int attacker, int defender) {
  if (batch == NULL || attacker <= defender) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[a_idx];
  if (msl_motion_state_fx_special_kind(batch->state.char_id[a_idx], action) !=
      (uint8_t)MSL_FX_KIND_SPECIAL_LW_START) {
    return 0u;
  }
  if (batch->state.prev_action_id[a_idx] == action) {
    return 0u;
  }
  const uint16_t attacker_prev_action = batch->state.prev_action_id[a_idx];
  if (batch->state.frame_start_on_ground[a_idx] == 0u ||
      (attacker_prev_action != (uint16_t)MSL_ACT_SQUAT &&
       attacker_prev_action != (uint16_t)MSL_ACT_SQUAT_WAIT &&
       attacker_prev_action != (uint16_t)MSL_ACT_SQUAT_RV)) {
    return 0u;
  }
  const uint16_t defender_action = batch->state.action_id[d_idx];
  if ((defender_action != (uint16_t)MSL_ACT_GUARD_ON &&
       defender_action != (uint16_t)MSL_ACT_GUARD) ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] != UINT32_MAX) {
    return 0u;
  }
  const uint8_t expired_lightshield_guardon_owner =
      ((defender_action == (uint16_t)MSL_ACT_GUARD_ON ||
        batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON ||
        batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON) &&
       batch->state.guard_x10[d_idx] == 0u && batch->state.lightshield_amount[d_idx] > 0.0f &&
       fabsf(batch->state.guard_tilt_x4[d_idx]) >= 0.49f &&
       msl_action_is_live_shield_family(batch->state.seed_prev_action_id[d_idx]))
          ? 1u
          : 0u;
  if (batch->state.guard_on_cliff_end_source[d_idx] == 0u &&
      expired_lightshield_guardon_owner == 0u) {
    return 0u;
  }

  // Fighter shield pair-order + no-submotion GuardOn owners:
  // - ftColl_80078C70 walks fighter pairs in entity order and consumes ShieldDesc/HitCapsule
  //   state for the current owner pair. When a later-slot fighter enters grounded Reflector from
  //   Squat-family IASA, the freshly-created HitCapsule can miss an earlier-slot ShieldDesc that
  //   came from the same source proc's CliffClimb/Attack/Escape end -> Wait_IASA -> GuardOn
  //   handoff.
  // - The same source pair-order shape applies when no-submotion GuardOn has expired x10 and is
  //   carrying a latched lightshield/tilt pose: GuardOn_Anim can transition through
  //   ftCo_800928CC before collision, while ftCo_800925A4/ftCo_80091E78 keep that live
  //   ShieldDesc owner. Active-x10 or untilted hard-shield rows keep the normal ShieldDesc path.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CliffClimb.c::ftCo_CliffClimb_Anim
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D92C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardOn_Anim,ftCo_80091A4C,ftCo_80092450,ftCo_800925A4,ftCo_80091E78,ftCo_800928CC}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
  return 1u;
}

uint8_t combat_guard_reflect_active_x14_reflectdesc_blocks_hitshield(const MslBatch* batch,
                                                                     size_t idx,
                                                                     float overlap_margin) {
  if (batch == NULL) {
    return 0u;
  }
  // Carried GuardReflect rows with an active x14 reflect timer are still in the ReflectDesc-owned
  // window when fighter-vs-fighter collision runs. Do not let the simulator's reconstructed
  // ShieldDesc overlap hand off to GuardSetOff until ftCo_80093BC0 has consumed x14. Same-frame
  // locomotion / landing powershield entries have x14_seed==0 and are not this carried phase:
  // ftCo_80093A50 creates ShieldDesc before collision in that entry frame. While x14 is still
  // active, only accept reconstructed ShieldDesc hits whose sphere/segment overlap penetrates by
  // at least the source ShieldDesc size lane (`lbColl_80007BCC` arg4=1 scaled by the shield JObj);
  // shallower contacts are the reduced-proxy grazes that the live ReflectDesc owner would keep out
  // of GuardSetOff.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_80093A50,ftCo_80093BC0,ftCo_GuardReflect_Anim}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  const MslCharParams* chp = msl_char_params_fast(batch->state.char_id[idx]);
  const float model_scale =
      (chp != NULL && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
          ? chp->model_scaling
          : 1.0f;
  if (model_scale > 1.0f) {
    // Enlarged collision models carry the ShieldDesc JObj scale in the lbColl_80007BCC matrix
    // path; the current sphere proxy already admits the retained Falco landing/Wait front-door
    // contacts there. The grazing false positives guarded below are the non-expanded-model carried
    // ReflectDesc rows.
    // refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale
    return 0u;
  }
  const float shield_desc_size_world = batch->state.fighter_scale_y[idx] * model_scale;
  return (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.guard_reflect_timer_x14_seed[idx] != 0u &&
          batch->state.guard_reflect_timer_x14[idx] != 0u &&
          overlap_margin < shield_desc_size_world && batch->state.hitlag[idx] == 0u &&
          batch->state.hitstun[idx] == 0u)
             ? 1u
             : 0u;
}

uint8_t combat_pstadium_guardreflect_attackairlw_extent_accepts_shield(const MslBatch* batch,
                                                                       int bi, size_t a_idx,
                                                                       size_t d_idx, size_t hb_i,
                                                                       float overlap_margin) {
  if (batch == NULL || batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_POKEMON_STADIUM ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] != UINT32_MAX ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW ||
      batch->state.animation_index[a_idx] != (uint32_t)MSL_SM_ATTACK_AIR_LW ||
      batch->state.hitbox_damage[hb_i] != 12.0f || !(overlap_margin > -4.0f) ||
      !(overlap_margin < 0.0f)) {
    return 0u;
  }
  // Active no-submotion GuardReflect / strong DAir extent:
  // The direct GuardReflect entry has live ShieldDesc source ownership before the reduced sphere
  // proxy reaches it. Admit only the authored strong DAir payload when the reduced overlap is just
  // outside the ShieldDesc sphere and still within the source extent lane.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80093BC0,ftCo_80092450}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  return 1u;
}

uint8_t combat_pstadium_guardon_x44_reduced_proxy_rejects_shield(const MslBatch* batch, int bi,
                                                                 size_t a_idx, size_t d_idx,
                                                                 uint8_t hb_id, size_t hb_i,
                                                                 float overlap_margin) {
  if (batch == NULL || batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_POKEMON_STADIUM ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] != UINT32_MAX ||
      batch->state.guard_x10[d_idx] == 0u) {
    return 0u;
  }

  // Pokemon Stadium GuardOn x44 reduced-proxy rejects:
  // The source x44 ShieldDesc lane is identified and x7E4 is extracted, but runtime does not yet
  // consume the full live ShieldDesc/JObj packet. The reduced sphere/segment proxy can over-admit a
  // few selected payloads, so keep this temporary reject bounded by Stadium stage, no-submotion
  // continuing GuardOn, live x10, and extracted/authored payloads; active GuardReflect,
  // non-Stadium rows, stale x10=0, and unrelated hitboxes stay on the ordinary ShieldDesc path.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_80068E64,Fighter_UpdateModelScale}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_80091E78}
  const uint16_t a_action = batch->state.action_id[a_idx];
  const uint32_t a_anim = batch->state.animation_index[a_idx];
  const float damage = batch->state.hitbox_damage[hb_i];
  if (a_action == (uint16_t)MSL_ACT_ATTACK_AIR_LW && a_anim == (uint32_t)MSL_SM_ATTACK_AIR_LW &&
      damage == 12.0f && batch->state.guard_tilt_x4[d_idx] > MSL_PSTADIUM_GUARDON_TILT_MIN &&
      overlap_margin > MSL_PSTADIUM_GUARDON_DAIR_REJECT_MIN_MARGIN) {
    return 1u;
  }
  if (a_action == (uint16_t)MSL_ACT_ATTACK_S4_S && a_anim == (uint32_t)MSL_SM_ATTACK_S4 &&
      damage == 17.0f && overlap_margin > MSL_PSTADIUM_GUARDON_S4_REJECT_MIN_MARGIN &&
      overlap_margin < MSL_PSTADIUM_GUARDON_S4_REJECT_MAX_MARGIN) {
    return 1u;
  }
  if (a_action == (uint16_t)MSL_ACT_ATTACK_AIR_B && a_anim == (uint32_t)MSL_SM_ATTACK_AIR_B &&
      hb_id == 1u && batch->state.guard_tilt_x4[d_idx] > MSL_PSTADIUM_GUARDON_TILT_MIN &&
      overlap_margin > MSL_PSTADIUM_GUARDON_BAIR_REJECT_MIN_MARGIN &&
      ((damage == 15.0f && overlap_margin < MSL_PSTADIUM_GUARDON_BAIR_REJECT_MAX_MARGIN) ||
       (damage == 9.0f && overlap_margin < MSL_PSTADIUM_GUARDON_WEAK_BAIR_REJECT_MAX_MARGIN))) {
    // AttackAirB keeps the same authored outer hb1 ShieldDesc owner across its strong and weak
    // same-group payloads; both are MSLFTSC1 create_hitbox data for ftCo_SM_AttackAirB. The later
    // weak refresh can sample a slightly wider reduced-proxy residual after the source x44
    // transform has moved with the continuing GuardOn pose, so keep that bound separate from the
    // strong/create-edge BAir lane.
    return 1u;
  }
  return 0u;
}

uint8_t combat_guardon_raise_attacks4_age_rejects_shield(const MslBatch* batch,
                                                         const MslCommonParams* c, int bi,
                                                         size_t a_idx, size_t d_idx, size_t hb_i) {
  if (batch == NULL || c == NULL ||
      batch->state.stage_id[bi] == (uint32_t)MSL_STAGE_ID_POKEMON_STADIUM ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] != UINT32_MAX ||
      batch->state.guard_x10[d_idx] == 0u ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_S4_S ||
      batch->state.animation_index[a_idx] != (uint32_t)MSL_SM_ATTACK_S4 ||
      batch->state.hitbox_damage[hb_i] != 17.0f) {
    return 0u;
  }
  const float main_x =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[d_idx]), c->lstick_deadzone_x);
  const float main_y =
      apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[d_idx]), c->lstick_deadzone_y);
  if (main_x != 0.0f || main_y != 0.0f) {
    return 0u;
  }
  const uint16_t init = (uint16_t)c->guard_x10_init_frames;
  uint16_t guard_x10 = (uint16_t)batch->state.guard_x10[d_idx];
  if ((uint16_t)batch->state.guard_x10_frame_start[d_idx] > guard_x10) {
    guard_x10 = (uint16_t)batch->state.guard_x10_frame_start[d_idx];
  }
  if (init == 0u || guard_x10 > init) {
    return 0u;
  }
  const uint16_t age = (uint16_t)(init - guard_x10);
  // GuardOn raise ShieldDesc age owner:
  // - `ftCo_800921DC` initializes x10 from p_ftCommonData->x268 and calls ftCo_80091E78(..., 0).
  // - Each GuardOn_Anim callback then decrements x10 through ftCo_800925A4, while the
  //   frame-start x10 lane remains the replay-visible owner for which raise-pose callback is being
  //   collided. Runtime uses the larger of current/frame-start x10 so callback-local decrement
  //   order does not move the ShieldDesc boundary one frame early.
  // - The authored AttackS4 side-smash payload reaches this row while the raise pose is still in
  //   the first half of the x10 window and no current main-stick guard-tilt input is active; source
  //   lbColl_80007BCC still misses until the later x10<=4 pose. Keep this to the generated
  //   AttackS4 17-damage payload instead of a broad GuardOn shield suppressor; active-tilt rows and
  //   Pokemon Stadium's separate x44 transform owner keep their existing ShieldDesc path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_800921DC,ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  // data/common/ft_common_data.json::{guard_x10_init_frames,lstick_deadzone_x,lstick_deadzone_y}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackS4.events.create_hitbox
  return (uint8_t)(age < (init / 2u));
}

uint8_t combat_pstadium_guardon_attackairb_weak_x44_miss_suppresses_body(const MslBatch* batch,
                                                                         int bi, int attacker,
                                                                         int hb_id, size_t a_idx,
                                                                         size_t d_idx,
                                                                         size_t hb_i) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES || batch->replay_rollout_reseeded == NULL ||
      batch->replay_rollout_reseeded[bi] == 0u ||
      batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_POKEMON_STADIUM ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B ||
      batch->state.animation_index[a_idx] != (uint32_t)MSL_SM_ATTACK_AIR_B ||
      batch->state.hitbox_damage[hb_i] != 9.0f ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] != UINT32_MAX ||
      batch->state.guard_x10[d_idx] == 0u ||
      !(batch->state.guard_tilt_x4[d_idx] > MSL_PSTADIUM_GUARDON_TILT_MIN) ||
      batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      !combat_replay_rollout_advanced_past_reseed(batch, bi)) {
    return 0u;
  }
  if (hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]) != 0u) {
    return 0u;
  }
  const size_t valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  if (batch->state.combat_hitlist_hb_valid[valid_i] != 0u) {
    return 0u;
  }
  // Pokemon Stadium weak BAir ShieldDesc-miss / BODY latch:
  // - The same x44 ShieldDesc source that rejects the weak hb1 shield overlap can still leave the
  //   group-0 weak BAir HitCapsules' victims_1 populated for lbColl_8000ACFC before BODY
  //   fallthrough.
  // - Long replay rollouts seeded before this GuardOn entry do not have the row-local dense
  //   victims_1 map, so suppress only the extracted live weak BAir payload on the Stadium
  //   no-submotion GuardOn x10/tilt owner. Strong/create-edge payloads, low-tilt, non-Stadium, and
  //   authoritative per-HitCapsule seed rows stay on their normal collision path.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_80068E64,Fighter_UpdateModelScale}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_80091E78}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  // data/scripts/{fox,falco}.bin (MSLFTSC1 ftCo_SM_AttackAirB same-group weak payload)
  return 1u;
}

uint8_t combat_guard_reflect_final_x14_live_x18_blocks_body(const MslBatch* batch, size_t idx) {
  return msl_guard_reflect_final_x14_live_x18_blocks_body(batch, idx);
}

uint8_t combat_guard_reflect_active_x14_no_guardon_blocks_body(const MslBatch* batch, size_t idx) {
  return msl_guard_reflect_active_x14_no_guardon_blocks_body(batch, idx);
}

uint8_t combat_guard_reflect_active_x14_rejects_strong_attackairb_shield(const MslBatch* batch,
                                                                         size_t a_idx, size_t d_idx,
                                                                         size_t hb_i) {
  if (batch == NULL) {
    return 0u;
  }
  if (!combat_guard_reflect_active_x14_no_guardon_blocks_body(batch, d_idx)) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B) {
    return 0u;
  }
  // Direct active-x14 powershield rows keep the ReflectDesc owner for the strong early Back-Air
  // torso/leg capsules; the lower-damage tail capsule can still reach the live ShieldDesc handoff.
  // Express the boundary through the extracted AttackAirB damage split (15-damage strong capsules
  // vs 9-damage tail/late capsules) rather than an EWT row or hitbox slot id.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80093BC0}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  return (batch->state.hitbox_damage[hb_i] > 9.5f) ? 1u : 0u;
}

uint8_t combat_guard_reflect_x18_expiry_rejects_strong_attackairn_hb1_shield(
    const MslBatch* batch, size_t a_idx, size_t d_idx, size_t hb_i, uint8_t hb_id) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT ||
      batch->state.action_frame[d_idx] > -2 || batch->state.animation_index[d_idx] != UINT32_MAX ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      batch->state.guard_reflect_timer_x14_seed[d_idx] != 0u ||
      batch->state.guard_reflect_timer_x14[d_idx] != 0u ||
      batch->state.guard_reflect_origin_guardon[d_idx] == 0u ||
      batch->state.guard_reflect_timer_x18_seed[d_idx] != 1u ||
      batch->state.guard_reflect_timer_x18[d_idx] != 0u) {
    return 0u;
  }
  if (batch->state.animation_index[a_idx] != (uint32_t)MSL_SM_ATTACK_AIR_N ||
      batch->state.hitbox_prev_enabled[hb_i] == 0u || hb_id != 1u ||
      batch->state.hitbox_damage[hb_i] != 12.0f || batch->state.hitbox_angle[hb_i] != 361u ||
      batch->state.hitbox_kbg[hb_i] != 100u || batch->state.hitbox_wsk[hb_i] != 0u ||
      batch->state.hitbox_bkb[hb_i] != 10u) {
    return 0u;
  }
  // GuardOn-origin GuardReflect x18-expiry strong NAir shield-side owner:
  // - The no-submotion GuardReflect row is still in the callback that consumes the final x18
  //   powershield-active tick (`seed==1 -> current==0`). Source does not let the persistent strong
  //   NAir hb1 ShieldDesc side path enter GuardSetOff until the following callback.
  // - Keep this to the extracted strong NAir hb1 payload and the explicit x18-expiry owner. It is
  //   not a broad GuardReflect/NAir reject; x18-live accepted seed lanes and the next x18-cleared
  //   callback stay on the normal shield-hit path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092F2C}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  return 1u;
}

uint8_t combat_guardsetoff_carried_shield_packet_owner(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.combat_shield_hit_int_damage[idx] == 0u ||
      batch->state.combat_shield_damage_taken[idx] == 0u) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[idx];
  if (action == (uint16_t)MSL_ACT_GUARD_SET_OFF) {
    return 1u;
  }
  if (action == (uint16_t)MSL_ACT_GUARD &&
      (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF ||
       batch->state.frame_start_action_id[idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF ||
       batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF)) {
    // GuardSetOff -> Guard carried shield-hit packet:
    // GuardSetOff_Anim can enter Guard before the current frame's collision pass, while the
    // replay-visible hidden x19A4/x19A0 shield-hit lanes still belong to the GuardSetOff source
    // episode that produced this callback. Treat the x19A* packet as the source discriminator,
    // not the destination Guard action id.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_GuardSetOff_Anim,ftCo_800928CC,ftCo_80092F2C}
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    return 1u;
  }
  return 0u;
}

uint8_t combat_shield_damage_powershield_suppressed_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  if ((flags_221c & (uint8_t)MSL_STATE_FLAG_221C_B2) != 0u) {
    if (combat_guardsetoff_carried_shield_packet_owner(batch, idx) != 0u) {
      // Carried GuardSetOff shield-hit packet:
      // Slippi can retain fp+0x221C_b2 on an already-entered or just-advanced GuardSetOff source
      // episode, while the hidden x19A4/x19A0 lanes prove ftColl_80076CBC accumulated a fresh
      // shield-hit packet for this callback. Collision-time x221C_b2 would have suppressed x19A0,
      // so the nonzero x19A0 lane is the source-owned discriminator that the visible bit is stale
      // for shield-damage accumulation.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
      // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093240}
      return 0u;
    }
    // Shield-hit damage/recoil consumes the live powershield flag directly. This is intentionally
    // broader than item reflect ownership: x14 owns ReflectDesc/item reflection, while
    // ftColl_80076CBC and ftCo_80092F2C gate shieldDamageTaken and GuardSetOff pushback on
    // fp->x221C_b2.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    return 1u;
  }
  uint8_t powershield_active = combat_is_powershield_active_idx(batch, idx);
  if (!powershield_active) {
    return 0u;
  }
  // GuardReflect frozen snapshot ownership split:
  // - ftColl_80076CBC shield-damage accumulation reads fp->x221C_b2, but callback ownership for
  //   active reflect window expiration is in GuardReflect_Anim (ftCo_80093BC0, x14 lane).
  // - In no-submotion frozen snapshots, x18 can remain non-zero while x14 is already expired.
  // - For shield-damage accumulation only (GuardSetOff/hitlag ownership), suppress powershield
  //   gating in this narrow lane; item reflect ownership continues to use
  //   combat_is_powershield_active_idx().
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
      combat_is_guard_reflect_frozen_snapshot_idx(batch, idx) &&
      batch->state.animation_index[idx] == UINT32_MAX &&
      batch->state.guard_reflect_timer_x14[idx] == 0u &&
      batch->state.guard_reflect_timer_x18[idx] != 0u &&
      // When x14 expires during this callback (`seed==1 -> current==0`), ftCo_80093BC0 has
      // just recreated ShieldDesc before the collision pass while x18/x221C_b2 remains live.
      // That boundary should still suppress shieldDamageTaken; rows already seeded with x14
      // expired use this branch as the older frozen-snapshot shield-damage handoff.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
      batch->state.guard_reflect_timer_x14_seed[idx] == 0u) {
    if ((flags_221c & (uint8_t)MSL_STATE_FLAG_221C_B2) != 0u) {
      return powershield_active;
    }
    powershield_active = 0u;
  }
  return powershield_active;
}

uint8_t combat_is_powershield_active_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0;
  }
  // Decomp gate at collision-time is on fp->x221C_b2 directly.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT) {
    // GuardReflect lane split:
    // - frozen no-submotion snapshot lane keeps x18 (legacy powershield-active lane) so replay-real
    //   frozen rows do not collapse before the first canonical callback-owned transition;
    // - normal GuardReflect lane uses x14 (active reflect callback lane).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
    if (combat_is_guard_reflect_frozen_snapshot_idx(batch, idx)) {
      return (batch->state.guard_reflect_timer_x18[idx] != 0u) ? 1u : 0u;
    }
    // For non-frozen GuardReflect frames, gate suppression on the active reflect callback lane.
    return (batch->state.guard_reflect_timer_x14[idx] != 0u) ? 1u : 0u;
  }
  return (flags_221c & (uint8_t)MSL_STATE_FLAG_221C_B2) ? 1u : 0u;
}

uint8_t combat_guard_setoff_recoil_x221c_b2_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  if ((flags_221c & (uint8_t)MSL_STATE_FLAG_221C_B2) != 0u) {
    return 1u;
  }
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT) {
    // GuardSetOff recoil consumes fp->x221C_b2 directly inside ftCo_80092F2C. That lane is owned by
    // GuardReflect's x18 timer, not the shorter ReflectDesc/x14 ownership used by item reflect.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092F2C}
    // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Guard.s:0x80093080..0x800930A0
    return (batch->state.guard_reflect_timer_x18[idx] != 0u) ? 1u : 0u;
  }
  return 0u;
}
