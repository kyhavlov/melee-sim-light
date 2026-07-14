#pragma once

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "ids.h"
#include "api.h"
#include "motion_state_owners.h"
#include "state_flags.h"

enum {
  MSL_DAMAGE_OWNER_FOX_DYNAMIC_TAIL_PART_ID = 18,
  MSL_DAMAGE_OWNER_DAMAGEFLYTOP_XROTN_HURTCAP_SLOT = 12,
};

static inline uint8_t msl_damage_owner_is_damagefly_action(uint16_t action_id) {
  return msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_DAMAGE_FLY);
}

static inline uint8_t msl_damage_owner_is_damage_air_action(uint16_t action_id) {
  return msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_DAMAGE_AIR);
}

static inline uint8_t msl_damage_owner_is_damage_ground_action(uint16_t action_id) {
  return msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_DAMAGE_GROUND);
}

static inline uint8_t msl_damage_owner_is_damage_collision_landing_action(uint16_t action_id) {
  const uint8_t handler = msl_motion_state_coll_handler_kind((uint8_t)MSL_CHAR_ID_FOX, action_id);
  return (uint8_t)(handler == (uint8_t)MSL_COLL_HANDLER_DAMAGE_COMMON ||
                   handler == (uint8_t)MSL_COLL_HANDLER_DAMAGE_FLY ||
                   handler == (uint8_t)MSL_COLL_HANDLER_DAMAGE_FALL ||
                   handler == (uint8_t)MSL_COLL_HANDLER_DOWN_DAMAGE ||
                   handler == (uint8_t)MSL_COLL_HANDLER_DOWN_REFLECT);
}

static inline uint8_t msl_damage_owner_is_damagefly_collision_action(uint16_t action_id) {
  return msl_motion_state_common_coll_handler_is(action_id, MSL_COLL_HANDLER_DAMAGE_FLY);
}

static inline uint8_t msl_damage_owner_allows_sdi_action(uint16_t action_id) {
  // Damage SDI owner is motion-state data plus the source-exception DownDamage pair:
  // Damage Hi/N/Lw, DamageAir, DamageFly/FlyReflect, and DamageFall use Damage/DamageFly/
  // DamageFall callbacks; DownDamageU/D re-enter the damage hitlag callback through
  // ftCo_8009F184 before their downed collision callback.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_OnEveryHitlag,ftCo_Damage_Coll,ftCo_DamageFly_Coll}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_Coll
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
  //   ftCo_8009F184,ftCo_DownDamage_Coll}
  return (uint8_t)(msl_damage_owner_is_damage_collision_landing_action(action_id) ||
                   action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U ||
                   action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_D);
}

static inline uint8_t msl_damage_owner_is_damage_or_firefox_launch_action(uint8_t char_id,
                                                                          uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_DAMAGE_HI_1:
    case MSL_ACT_DAMAGE_HI_2:
    case MSL_ACT_DAMAGE_HI_3:
    case MSL_ACT_DAMAGE_N_1:
    case MSL_ACT_DAMAGE_N_2:
    case MSL_ACT_DAMAGE_N_3:
    case MSL_ACT_DAMAGE_LW_1:
    case MSL_ACT_DAMAGE_LW_2:
    case MSL_ACT_DAMAGE_LW_3:
    case MSL_ACT_DAMAGE_AIR_1:
    case MSL_ACT_DAMAGE_AIR_2:
    case MSL_ACT_DAMAGE_AIR_3:
    case MSL_ACT_DAMAGE_FLY_HI:
    case MSL_ACT_DAMAGE_FLY_N:
    case MSL_ACT_DAMAGE_FLY_LW:
    case MSL_ACT_DAMAGE_FLY_TOP:
    case MSL_ACT_DAMAGE_FLY_ROLL:
    case MSL_ACT_FLY_REFLECT_WALL:
    case MSL_ACT_FLY_REFLECT_CEIL:
      return 1u;
    default: {
      // Firefox/Firebird launch ownership from the extracted MotionState row identity
      // (the shared 341..372 range stays kind 0 for other characters).
      const uint8_t fx_kind = msl_motion_state_fx_special_kind_fast(char_id, action_id);
      return (uint8_t)(fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI ||
                       fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI);
    }
  }
}

static inline uint8_t msl_damage_owner_is_downed_damage_contact_action(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_DOWN_BOUND_U:
    case (uint16_t)MSL_ACT_DOWN_WAIT_U:
    case (uint16_t)MSL_ACT_DOWN_DAMAGE_U:
    case (uint16_t)MSL_ACT_DOWN_BOUND_D:
    case (uint16_t)MSL_ACT_DOWN_WAIT_D:
    case (uint16_t)MSL_ACT_DOWN_DAMAGE_D:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint16_t msl_damage_owner_down_damage_action_from_source(uint16_t action_id) {
  // ftCo_8009F184 selects DownDamageU only from DownWaitU; other downed source motions route to
  // DownDamageD.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_8009F184
  return (action_id == (uint16_t)MSL_ACT_DOWN_WAIT_U) ? (uint16_t)MSL_ACT_DOWN_DAMAGE_U
                                                      : (uint16_t)MSL_ACT_DOWN_DAMAGE_D;
}

static inline uint32_t msl_damage_owner_down_damage_submotion_from_action(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U) ? (uint32_t)MSL_SM_DOWN_DAMAGE_U
                                                        : (uint32_t)MSL_SM_DOWN_DAMAGE_D;
}

static inline uint8_t msl_damage_owner_is_fox_dynamic_tail_part(uint8_t char_id,
                                                                uint16_t bone_part_id) {
  return (char_id == (uint8_t)MSL_CHAR_ID_FOX &&
          bone_part_id == (uint16_t)MSL_DAMAGE_OWNER_FOX_DYNAMIC_TAIL_PART_ID)
             ? 1u
             : 0u;
}

static inline uint8_t msl_damage_owner_terminal_state_blocks_enable_edge_body(
    const MslBatch* batch, size_t hb_i, size_t a_idx, size_t d_idx, uint16_t cap_bone_part_id,
    uint8_t cap_valid) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.hitbox_enable_edge[hb_i] == 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  if (batch->state.prev_action_id[a_idx] != batch->state.action_id[a_idx]) {
    return 0u;
  }

  const uint16_t d_action = batch->state.action_id[d_idx];
  const uint16_t d_prev = batch->state.prev_action_id[d_idx];
  const uint8_t terminal_damagefly_tail =
      cap_valid != 0u && batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_HI3 &&
      msl_damage_owner_is_fox_dynamic_tail_part(batch->state.char_id[d_idx], cap_bone_part_id) &&
      msl_damage_owner_is_damagefly_action(d_action) && batch->state.instance_hit_by[d_idx] != 0u;
  const uint8_t terminal_fall_from_damage =
      d_action == (uint16_t)MSL_ACT_FALL &&
      (d_prev == (uint16_t)MSL_ACT_DAMAGE_FALL || msl_damage_owner_is_damagefly_action(d_prev));

  // DamageFly/DamageFall terminal rows remain under the damage callback episode after hitstun reaches
  // zero. Same-action create-edge BODY candidates must wait for the next collision frame on the
  // source-owned terminal slice, while already-live hitboxes and same-frame action entries stay on the
  // ordinary ftColl path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_8008F744,ftCo_DamageFly_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DamageFall.c::ftCo_DamageFall_IASA
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
  return (uint8_t)(terminal_damagefly_tail || terminal_fall_from_damage);
}

static inline uint8_t msl_damage_owner_damageflylw_dynamic_high_part_rejects_body(
    const MslBatch* batch, size_t a_idx, size_t d_idx, uint8_t hb_id, uint16_t cap_bone_part_id,
    uint8_t cap_valid) {
  if (batch == NULL || cap_valid == 0u) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_HI3 || hb_id != 1u) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_LW ||
      batch->state.hitstun[d_idx] == 0u) {
    return 0u;
  }
  // Active-hitstun DamageFlyLw can still expose Fox's live dynamic tail-chain JObj state before the
  // static collision snapshot catches up. Keep the bridge bounded to the source-owned part-18 chain.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  // data/anims/fox.dyn.bin (SSDYNN01 collision-owner index, part 18 in root 17 chain)
  return msl_damage_owner_is_fox_dynamic_tail_part(batch->state.char_id[d_idx], cap_bone_part_id);
}

static inline uint8_t msl_damage_owner_attackairlw_damageflytop_fox_tail_rejects_body(
    const MslBatch* batch, size_t a_idx, size_t d_idx, uint8_t hb_id, uint16_t cap_bone_part_id,
    uint8_t cap_valid, uint16_t expected_hitlag) {
  if (batch == NULL || cap_valid == 0u) {
    return 0u;
  }
  if (!msl_damage_owner_is_fox_dynamic_tail_part(batch->state.char_id[d_idx], cap_bone_part_id)) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW || hb_id > 1u) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.hitstun[d_idx] == 0u || expected_hitlag == 0u) {
    return 0u;
  }
  if (hb_id == 0u && batch->state.hitstun[d_idx] > expected_hitlag) {
    return 0u;
  }
  if (hb_id == 1u && batch->state.hitstun[d_idx] <= expected_hitlag) {
    return 0u;
  }
  // Active-hitstun DamageFlyTop uses the same Fox dynamic tail-chain owner as the DamageFlyLw high
  // part bridge. Falco AttackAirLw hb0/hb1 are the paired same-group DAir body capsules; hb0's
  // replay-proven reject is terminal, while hb1's dynamic-tail reject applies before the next
  // same-source hitlag horizon and releases once hitstun decays to that horizon. Falco's
  // corresponding victim cap is not part 18 and stays on ordinary BODY selection.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  // data/hurtcaps/fox.bin cap12 -> FtPart 18
  // data/anims/fox.dyn.bin (SSDYNN01 collision-owner index, part 18 in root 17 chain)
  return (batch->state.hitlag[d_idx] == 0u) ? 1u : 0u;
}

static inline uint8_t msl_damage_owner_attackhi4_damageflytop_xrotn_rejects_body(
    const MslBatch* batch, size_t hb_i, size_t a_idx, size_t d_idx, uint8_t hb_id, uint8_t cap_id) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_HI4 || hb_id > 1u ||
      cap_id != (uint8_t)MSL_DAMAGE_OWNER_DAMAGEFLYTOP_XROTN_HURTCAP_SLOT ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      batch->state.instance_hit_by[d_idx] == 0u ||
      batch->state.instance_hit_by[d_idx] == batch->state.instance_id[a_idx]) {
    return 0u;
  }
  // Terminal DamageFlyTop / strong AttackHi4 XRotN rejection:
  // Fox strong UpSmash's hb0/hb1 create payload is extracted as 18 damage, angle 80, KBG 112,
  // BKB 30.
  // A terminal DamageFlyTop victim can keep a stale dynamic XRotN/tail part matrix after its damage
  // callback episode has ended; do not turn that matrix-only overlap into a new BODY hit unless the
  // victim source instance already belongs to this attacker. The predicate is payload + generated
  // selected cap12/XRotN provenance, not character pair or replay row.
  // data/moves/fox.json::moves.ftCo_SM_AttackHi4.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap12 -> XRotN upper-body dynamic part
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  return (uint8_t)(batch->state.hitbox_damage[hb_i] == 18.0f &&
                   batch->state.hitbox_angle[hb_i] == 80u &&
                   batch->state.hitbox_kbg[hb_i] == 112u && batch->state.hitbox_bkb[hb_i] == 30u);
}
