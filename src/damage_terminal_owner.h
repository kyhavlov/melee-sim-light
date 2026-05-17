#pragma once

#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "api.h"
#include "motion_state_owners.h"
#include "move_tables.h"

enum { MSL_DAMAGE_OWNER_CHAR_FOX = 1, MSL_DAMAGE_OWNER_CHAR_FALCO = 22 };
enum { MSL_DAMAGE_OWNER_FOX_DYNAMIC_TAIL_PART_ID = 18 };

static inline uint8_t msl_damage_owner_is_damagefly_action(uint16_t action_id) {
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_DAMAGE_FLY);
}

static inline uint8_t msl_damage_owner_is_damage_or_firefox_launch_action(uint16_t action_id) {
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
    case MSL_ACT_FX_SPECIAL_HI:
    case MSL_ACT_FX_SPECIAL_AIR_HI:
      return 1u;
    default:
      return 0u;
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
  return (char_id == (uint8_t)MSL_DAMAGE_OWNER_CHAR_FOX &&
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
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW || hb_id != 0u) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.hitstun[d_idx] == 0u || expected_hitlag == 0u ||
      batch->state.hitstun[d_idx] > expected_hitlag) {
    return 0u;
  }
  // Active-hitstun DamageFlyTop uses the same Fox dynamic tail-chain owner as the DamageFlyLw high
  // part bridge. Falco's corresponding cap is not part 18 and stays on ordinary BODY selection.
  // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
  // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  // data/hurtcaps/fox.bin cap12 -> FtPart 18
  // data/anims/fox.dyn.bin (SSDYNN01 collision-owner index, part 18 in root 17 chain)
  return (batch->state.hitlag[d_idx] == 0u) ? 1u : 0u;
}

static inline int msl_damage_owner_local_slot_from_source_port0(const MslBatch* batch, int bi,
                                                                int num_players,
                                                                uint8_t source_port0) {
  if (batch == NULL || bi < 0) {
    return -1;
  }
  for (int p = 0; p < num_players; p++) {
    const size_t idx = (size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p;
    if (batch->state.source_port0[idx] == source_port0) {
      return p;
    }
  }
  return -1;
}

static inline uint8_t msl_damage_owner_replay_rollout_advanced_under_rng_owner(
    const MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const size_t bi = d_idx / (size_t)MSL_MAX_PLAYERS;
  const uint8_t clock_owner = (batch->rollout_clock_rng_owned != NULL)
                                  ? batch->rollout_clock_rng_owned[bi]
                                  : (uint8_t)MSL_ROLLOUT_CLOCK_NONE;
  const uint8_t replay_rollout =
      (batch->replay_rollout_reseeded != NULL && batch->replay_rollout_reseeded[bi] != 0u) ? 1u
                                                                                           : 0u;
  const uint8_t advanced_past_reseed =
      (batch->replay_rollout_seed_frame_id != NULL &&
       batch->state.frame_id[bi] != batch->replay_rollout_seed_frame_id[bi])
          ? 1u
          : 0u;
  return (replay_rollout && advanced_past_reseed && clock_owner != (uint8_t)MSL_ROLLOUT_CLOCK_NONE)
             ? 1u
             : 0u;
}

static inline uint8_t msl_damage_owner_damageflyroll_pre_action_allows_gate(const MslBatch* batch,
                                                                            size_t d_idx,
                                                                            uint16_t action_id) {
  if (batch == NULL) {
    return 0u;
  }
  // ftCo_8008DCE0 block_33 evaluates the DamageFlyRoll RNG gate during severe airborne
  // Fighter_ProcessHit entry. Visible motion state alone is insufficient for exact replay reseeds
  // when hidden Fighter_8006CDA4 pre-gate RNG consumers are not exposed, so exact rows use explicit
  // seed lanes while free-running rollout may use an owned RNG clock after the reseed frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/sysdolphin/baselib/random.c::{HSD_Randi,HSD_Randf}
  switch (action_id) {
    case (uint16_t)MSL_ACT_DAMAGE_FALL:
    case (uint16_t)MSL_ACT_FALL:
    case (uint16_t)MSL_ACT_RUN:
    case (uint16_t)MSL_ACT_JUMP_AERIAL_F:
    case (uint16_t)MSL_ACT_JUMP_AERIAL_B:
    case (uint16_t)MSL_ACT_LANDING_AIR_LW:
    case (uint16_t)MSL_ACT_ATTACK_HI4:
    case (uint16_t)MSL_ACT_ATTACK_LW3:
    case (uint16_t)MSL_ACT_FX_SPECIAL_LW_END:
    case (uint16_t)MSL_ACT_ATTACK_AIR_LW:
      return 1u;
    case (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI:
      // Exact replay rows keep the HSD_Randf phase seed-owned; free-running rollout after reseed can
      // use the owned RNG clock for this source-eligible pre-action.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
      return msl_damage_owner_replay_rollout_advanced_under_rng_owner(batch, d_idx);
    case (uint16_t)MSL_ACT_DAMAGE_FLY_N:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_LW: {
      if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u) {
        return 1u;
      }
      if (batch->state.hitlag[d_idx] != 0u && batch->state.hitstun[d_idx] != 0u) {
        const size_t bi = d_idx / (size_t)MSL_MAX_PLAYERS;
        const int num_players = (int)batch->config.num_players;
        const int attacker = msl_damage_owner_local_slot_from_source_port0(
            batch, (int)bi, num_players, batch->state.last_hit_by[d_idx]);
        if (attacker >= 0 && (size_t)attacker != (d_idx % (size_t)MSL_MAX_PLAYERS)) {
          const size_t a_idx = bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker;
          if (batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_HI &&
              batch->state.instance_hit_by[d_idx] == batch->state.instance_id[a_idx]) {
            return 1u;
          }
        }
      }
      return msl_damage_owner_replay_rollout_advanced_under_rng_owner(batch, d_idx);
    }
    case (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL: {
      const size_t bi = d_idx / (size_t)MSL_MAX_PLAYERS;
      const int num_players = (int)batch->config.num_players;
      const int attacker = msl_damage_owner_local_slot_from_source_port0(
          batch, (int)bi, num_players, batch->state.last_hit_by[d_idx]);
      if (attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
        return 0u;
      }
      const size_t a_idx = bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker;
      if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B) {
        return 0u;
      }
      const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
      for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
        if (batch->state.hitbox_enable_edge[hb_base + (size_t)hb] != 0u) {
          return 1u;
        }
      }
      return 0u;
    }
    case (uint16_t)MSL_ACT_DAMAGE_FLY_TOP: {
      const size_t bi = d_idx / (size_t)MSL_MAX_PLAYERS;
      const int num_players = (int)batch->config.num_players;
      const int attacker = msl_damage_owner_local_slot_from_source_port0(
          batch, (int)bi, num_players, batch->state.last_hit_by[d_idx]);
      if (attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
        return 0u;
      }
      const size_t a_idx = bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker;
      const uint16_t a_action = batch->state.action_id[a_idx];
      const int16_t a_af = batch->state.action_frame[a_idx];
      const int16_t attacklw4_create_frame = move_tables_grounded_attack_first_create_hitbox_frame(
          batch->state.char_id[a_idx], a_action);
      if (a_action == (uint16_t)MSL_ACT_ATTACK_LW4 && attacklw4_create_frame >= 0 &&
          a_af == attacklw4_create_frame) {
        return 1u;
      }
      if (a_action == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
          (a_af >= 6 || batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u)) {
        return 1u;
      }
      return 0u;
    }
    case (uint16_t)MSL_ACT_ATTACK_AIR_B: {
      const int16_t pre_af = batch->state.action_frame[d_idx];
      return (pre_af >= 5 || batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u)
                 ? 1u
                 : 0u;
    }
    case (uint16_t)MSL_ACT_ATTACK_AIR_N:
      return (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u) ? 1u : 0u;
    case (uint16_t)MSL_ACT_CATCH:
    case (uint16_t)MSL_ACT_CATCH_PULL:
    case (uint16_t)MSL_ACT_CATCH_DASH:
    case (uint16_t)MSL_ACT_CATCH_DASH_PULL:
    case (uint16_t)MSL_ACT_CATCH_WAIT:
    case (uint16_t)MSL_ACT_CATCH_ATTACK:
    case (uint16_t)MSL_ACT_CATCH_CUT:
      if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u) {
        return 1u;
      }
      return msl_damage_owner_replay_rollout_advanced_under_rng_owner(batch, d_idx);
    default:
      return 0u;
  }
}

static inline uint8_t msl_damage_owner_damageflyroll_jumpaerial_attackairb_carry(
    const MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t pre_action = batch->state.action_id[d_idx];
  if (pre_action != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
      pre_action != (uint16_t)MSL_ACT_JUMP_AERIAL_B) {
    return 0u;
  }
  const size_t bi = d_idx / (size_t)MSL_MAX_PLAYERS;
  const int num_players = (int)batch->config.num_players;
  const int attacker = msl_damage_owner_local_slot_from_source_port0(
      batch, (int)bi, num_players, batch->state.last_hit_by[d_idx]);
  if (attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  const size_t a_idx = bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker;
  return (batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_B) ? 1u : 0u;
}
