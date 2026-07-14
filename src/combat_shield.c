#include "combat_internal.h"

uint8_t combat_shield_overlap_ftcoll_80007bcc(const MslBatch* batch, int bi, int attacker,
                                              int defender, int hb_id, float* out_overlap_margin) {
  return msl_shielddesc_fighter_overlap_ftcoll_80007bcc(batch, bi, attacker, defender, hb_id,
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

uint8_t combat_shield_damage_powershield_suppressed_idx(const MslBatch* batch, size_t idx) {
  // Fighter shield contact suppresses shieldDamageTaken on fp->x221C_b2. The same live bit owns
  // GuardSetOff recoil below; replay action history and descriptor timers are not substitutes for
  // the source field.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  return combat_is_powershield_active_idx(batch, idx);
}

uint8_t combat_is_powershield_active_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // Collision-time shield damage, item shield recoil, and GuardSetOff pushback all consume
  // fp->x221C_b2 directly. Timer expiry owns the bit in ftCo_80093BC0; consumers do not infer it
  // from action, animation, or replay snapshot shape.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093BC0}
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  return (flags_221c & (uint8_t)MSL_STATE_FLAG_221C_B2) ? 1u : 0u;
}

uint8_t combat_guard_setoff_recoil_x221c_b2_idx(const MslBatch* batch, size_t idx) {
  return combat_is_powershield_active_idx(batch, idx);
}
