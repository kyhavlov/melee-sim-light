#include "items.h"
#include "char_registry.h"
#include "ids.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "batch_internal.h"
#include "char_params.h"
#include "combat.h"
#include "combat_geom.h"
#include "common_params.h"
#include "damage_terminal_owner.h"
#include "damage_source.h"
#include "hit_elements.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "input_axis.h"
#include "item_article_params.h"
#include "item_common_params.h"
#include "item_reflect.h"
#include "trigger_input.h"
#include "laser_params.h"
#include "lbcollision_constants.h"
#include "hurtcaps_tables.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "msl_math.h"
#include "mtx34.h"
#include "shield_tilt_table.h"
#include "sheik_specials.h"
#include "special_msids.h"
#include "stage_collision.h"
#include "stage_item_params.h"
#include "staling.h"

enum {
  MSL_ITEM_HIDDEN_CALLBACK_CLEAR = 1u << 0u,
  MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME = 1u << 1u,
};

enum {
  // The generated hurtcap substrate exposes Fighter_Part ids but not semantic body-region labels.
  // Fox/Falco cap12 is anchored to FtPart 18, the dynamic tail chain used by lbColl body tests.
  // data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18.
  MSL_ITEM_HURTCAP_FOX_FALCO_TAIL_PART_ID = 18,
};

static inline uint8_t slippi_metadata_low_byte_from_f32(float v) {
  uint32_t bits = 0;
  memcpy(&bits, &v, sizeof(bits));
  return (uint8_t)(bits & 0xFFu);
}

static inline uint8_t item_state_flags_2218_is_reflect_behavior_only(uint8_t flags_2218) {
  const uint8_t mask = (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                                 MSL_STATE_FLAG_2218_B2 | MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR);
  return ((flags_2218 & mask) == (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR) ? 1u : 0u;
}

static inline uint8_t item_throwhi_deferred_mid_pulse_rate_source_step(int32_t rate_q16_16) {
  // Source step owner for the 4/3 ThrowHi command-cursor carry:
  // - `frame_speed_mul` is seeded from the throw owner/victim pair and ftCo_800DD4B0 timing.
  // - Compare against the Q16.16 encoding of 4/3 with a one-LSB allowance for imported replay
  //   float quantization; 1.25x rows stay on the ordinary current-callback pulse path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073354
  const int32_t target_q16_16 = msl_q16_16_from_f32(4.0f / 3.0f);
  const int32_t delta = (rate_q16_16 >= target_q16_16) ? (rate_q16_16 - target_q16_16)
                                                       : (target_q16_16 - rate_q16_16);
  return (delta <= 1) ? 1u : 0u;
}

static inline uint8_t item_throwhi_prev18_second_article_rate_source_step(int32_t rate_q16_16) {
  // Source step owner for the crossed-prev frame-18 ThrowHi second article:
  // - ftCo_800DD4B0 seeds throw animation speed from the throw owner/victim pair.
  // - The crossed-prev frame-18 fallback represents the current-callback frame-20 source step only
  //   for 1.25x throw timing. Slower rows can carry crossed-prev frame 18 while the frame-20 command
  //   is still pending; 4/3 rows use the explicit deferred command-cursor owner above.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073354
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  const int32_t target_q16_16 = msl_q16_16_from_f32(5.0f / 4.0f);
  const int32_t delta = (rate_q16_16 >= target_q16_16) ? (rate_q16_16 - target_q16_16)
                                                       : (target_q16_16 - rate_q16_16);
  return (delta <= 1) ? 1u : 0u;
}

static inline uint8_t item_any_hitbox_allows_fighter(MslBatch* batch, int bi, int item_slot,
                                                     int victim, uint16_t victim_iid) {
  // Item BODY collision iterates item HitCapsules; a populated victims_1 ring suppresses that
  // HitCapsule, not the whole item. Dolphin v10 Falco ThrowLw dumps show hitboxes 2/3 can already
  // carry an attached victim while hitboxes 0/1 remain BODY-eligible for the next callback phase.
  // Keep the cheap item-level prefilter as "any hitbox may hit"; the narrowphase loop below still
  // checks the exact hitbox before testing geometry.
  // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    if (hitlist_allows_item_hitbox_fighter(batch, bi, item_slot, hb, victim, victim_iid)) {
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t item_all_hitboxes_allow_fighter(MslBatch* batch, int bi, int item_slot,
                                                      int victim, uint16_t victim_iid) {
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    if (!hitlist_allows_item_hitbox_fighter(batch, bi, item_slot, hb, victim, victim_iid)) {
      return 0u;
    }
  }
  return 1u;
}

static int throwlw_attached_victim_for_owner(const MslBatch* batch, int bi, int owner);

static int throw_laser_unique_same_source_victim(const MslBatch* batch, int bi, int owner) {
  if (batch == NULL || owner < 0 || owner >= (int)batch->config.num_players) {
    return -1;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  int candidate = -1;
  for (int vp = 0; vp < (int)batch->config.num_players; vp++) {
    if (vp == owner) {
      continue;
    }
    const size_t v_idx = msl_idx_player(bi, vp);
    if (batch->state.hitstun[v_idx] == 0u ||
        !msl_damage_source_victim_matches_attacker(batch, v_idx, o_idx, owner)) {
      continue;
    }
    if (candidate >= 0) {
      return -1;
    }
    candidate = vp;
  }
  return candidate;
}

static void throw_laser_advance_combo_bookkeeping(MslBatch* batch, int bi, int owner, int victim) {
  if (batch == NULL || owner < 0 || victim < 0 || owner >= (int)batch->config.num_players ||
      victim >= (int)batch->config.num_players) {
    return;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  const size_t v_idx = msl_idx_player(bi, victim);
  const uint8_t attack_id_u8 = (uint8_t)batch->state.attack_id[o_idx];
  const uint8_t cur_victim = batch->state.combo_victim_port[o_idx];
  if (batch->state.attack_id[o_idx] == (uint16_t)MSL_FT_MOVE_ID_DEFAULT ||
      batch->state.last_attack_landed[o_idx] != attack_id_u8 ||
      batch->state.combo_count[o_idx] == 0u ||
      (cur_victim != 0xFFu && cur_victim != (uint8_t)victim)) {
    return;
  }
  batch->state.combo_count[o_idx] = (uint8_t)(batch->state.combo_count[o_idx] + 1u);
  if (cur_victim == 0xFFu) {
    batch->state.combo_victim_port[o_idx] = (uint8_t)victim;
  }
  batch->state.combo_victim_instance_id[o_idx] = batch->state.instance_id[v_idx];
}

static inline float item_hitcapsule_stale_damage_mul(const MslBatch* batch, size_t item_idx,
                                                     size_t owner_idx, uint16_t attack_id) {
  if (batch == NULL) {
    return 1.0f;
  }
  if (batch->state.item_stale_damage_valid[item_idx] != 0u &&
      batch->state.item_stale_damage_mul[item_idx] > 0.0f) {
    return batch->state.item_stale_damage_mul[item_idx];
  }
  if (attack_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    return 1.0f;
  }
  return staling_multiplier_for_move(batch, owner_idx, attack_id);
}

static inline uint8_t item_reflector_owner_is_shine_callback_state(uint8_t char_id,
                                                                   uint16_t action_id);

static inline float item_laser_script_hitcapsule_damage(const MslLaserParams* lp,
                                                        uint8_t laser_state, uint8_t hitbox_id,
                                                        float laser_age_frames) {
  if (lp == NULL) {
    return 0.0f;
  }
  if (laser_state != 0u) {
    return lp->state1_damage;
  }
  float damage = lp->damage;
  if (lp->damage_update_frame != 0u && lp->damage_update_damage > 0.0f &&
      hitbox_id < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X &&
      (lp->damage_update_hitbox_mask & (uint16_t)(1u << hitbox_id)) != 0u &&
      laser_age_frames >= (float)lp->damage_update_frame) {
    // Source command owner:
    // Fox state-0 blaster script creates 3-damage HitCapsules, then `set_hitbox_damage` rewrites
    // hitboxes 0..2 to 2 damage on frame 17. Consume the generated MSLLASR1 v7 lane by selected
    // HitCapsule id rather than item kind or replay row.
    // refs/melee/src/melee/it/it_2725.c::{it_802790C0,it_80279544}
    // refs/melee/src/melee/it/itcoll.c::it_80272460
    damage = lp->damage_update_damage;
  }
  return damage;
}

static inline uint8_t item_laser_powershield_return_body_damage_owner(
    const MslBatch* batch, int bi, size_t item_idx, int owner, int defender, float script_damage,
    uint16_t attack_id, float* io_damage, float* io_stale_mult) {
  if (batch == NULL || io_damage == NULL || io_stale_mult == NULL || !(script_damage > 0.0f) ||
      owner < 0 || defender < 0 || owner == defender || owner >= (int)batch->config.num_players ||
      defender >= (int)batch->config.num_players || attack_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    return 0u;
  }
  const float reflect_mul = batch->state.item_reflect_damage_mul[item_idx];
  if (!(reflect_mul > 0.0f) || !(reflect_mul < 1.0f)) {
    return 0u;
  }
  const size_t owner_idx = msl_idx_player(bi, owner);
  const size_t defender_idx = msl_idx_player(bi, defender);
  const MslLaserParams* defender_lp = laser_params_get(batch->state.char_id[defender_idx]);
  if (defender_lp == NULL || defender_lp->shot_itkind != batch->state.item_type[item_idx]) {
    return 0u;
  }
  for (int pl = 0; pl < (int)batch->config.num_players; pl++) {
    if (pl == defender) {
      continue;
    }
    if (batch->state.char_id[msl_idx_player(bi, pl)] == batch->state.char_id[defender_idx]) {
      return 0u;
    }
  }
  const float owner_stale = staling_multiplier_for_move(batch, owner_idx, attack_id);
  if (owner_stale != 1.0f) {
    return 0u;
  }
  const float original_stale = staling_multiplier_for_move(batch, defender_idx, attack_id);
  if (!(original_stale > 0.0f) || original_stale == 1.0f) {
    return 0u;
  }

  // Returned powershielded laser BODY damage:
  // - ftColl_80077464 writes the damage-reducing powershield xC6C multiplier.
  // - Item_80269F14 multiplies the already-live HitCapsule.damage float by xC6C and floors via
  //   it_80272460. For a blaster shot returning to its original shooter, the article kind names the
  //   original shooter and that live HitCapsule float still carries the shooter's stale queue.
  // - Keep this post-contact and BODY-only so the reflected HitCapsule float does not perturb
  //   ReflectDesc selection/timing for strong reflector chains.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  const float tmp = (script_damage * original_stale * reflect_mul) + 0.99f;
  uint32_t raw_damage = (tmp > 0.0f) ? (uint32_t)tmp : 0u;
  if (raw_damage == 0u) {
    raw_damage = 1u;
  }
  *io_damage = (float)raw_damage;
  *io_stale_mult = 1.0f;
  return 1u;
}

static inline int item_reflected_laser_unique_previous_owner(const MslBatch* batch, int bi,
                                                             int current_owner,
                                                             uint16_t item_attack_id) {
  if (batch == NULL || current_owner < 0 || current_owner >= (int)batch->config.num_players) {
    return -1;
  }
  if (batch->config.num_players == 2u) {
    return current_owner == 0 ? 1 : 0;
  }
  int candidate = -1;
  for (int p = 0; p < (int)batch->config.num_players; p++) {
    if (p == current_owner) {
      continue;
    }
    const size_t p_idx = msl_idx_player(bi, p);
    if (batch->state.last_attack_landed[p_idx] != (uint8_t)item_attack_id) {
      continue;
    }
    if (candidate >= 0) {
      return -1;
    }
    candidate = p;
  }
  return candidate;
}

static inline uint8_t item_laser_reflected_body_damage_owner(
    const MslBatch* batch, int bi, size_t item_idx, int owner, float script_damage,
    uint16_t* io_attack_id, uint16_t* io_attack_instance, float* io_damage, float* io_stale_mult) {
  if (batch == NULL || io_attack_id == NULL || io_attack_instance == NULL || io_damage == NULL ||
      io_stale_mult == NULL || owner < 0 || owner >= (int)batch->config.num_players ||
      !(script_damage > 0.0f)) {
    return 0u;
  }
  const float reflect_mul = batch->state.item_reflect_damage_mul[item_idx];
  if (!(reflect_mul > 1.0f)) {
    return 0u;
  }
  const size_t owner_idx = msl_idx_player(bi, owner);
  if (!item_reflector_owner_is_shine_callback_state(batch->state.char_id[owner_idx],
                                                    batch->state.action_id[owner_idx]) ||
      batch->state.attack_id[owner_idx] == (uint16_t)MSL_FT_MOVE_ID_DEFAULT ||
      batch->state.attack_instance[owner_idx] == 0u ||
      *io_attack_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    return 0u;
  }
  if (*io_attack_instance == 0u) {
    return 0u;
  }
  const uint16_t owner_attack_id = batch->state.attack_id[owner_idx];
  if (owner_attack_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    return 0u;
  }
  // Own-returned proof by article kind: each spacie's blaster article has a distinct It_Kind
  // (MSLLASR1 shot_itkind: Fox 54, Falco 55). When the live owner's own character spawned this
  // article kind, and that character is unique in the match, the reflector is shining back a shot
  // they fired themselves - so at least one intermediate enemy reflect happened in between (the
  // attack-instance adjacency previously required here fails whenever the reflector attacked at
  // all between firing and shining, e.g. MAJ rec2016 fire=471 shine=477).
  // data/items/lasers.bin::MSLLASR1 shot_itkind
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
  {
    const MslLaserParams* own_lp = laser_params_get(batch->state.char_id[owner_idx]);
    if (own_lp == NULL || own_lp->shot_itkind != batch->state.item_type[item_idx]) {
      return 0u;
    }
    for (int pl = 0; pl < (int)batch->config.num_players; pl++) {
      if (pl == owner) {
        continue;
      }
      const size_t pl_idx = msl_idx_player(bi, pl);
      if (batch->state.char_id[pl_idx] == batch->state.char_id[owner_idx]) {
        return 0u;
      }
    }
  }
  const int prev_owner =
      item_reflected_laser_unique_previous_owner(batch, bi, owner, *io_attack_id);
  if (prev_owner < 0) {
    return 0u;
  }
  const size_t prev_owner_idx = msl_idx_player(bi, prev_owner);
  // Immediate source reflect callback damage rebuild:
  // - the incoming item HitCapsule.damage has already been staled through it_80272460 for the
  //   previous projectile owner,
  // - Item_80269F14 rebuilds raw reflected damage from that staled float and item->xC6C,
  // - the subsequent BODY damage/stale queue owner is the reflector's current shine move id.
  // The item attack instance must be the immediate predecessor of the reflector instance; older
  // reflected articles carry their own source-owned damage product and must not be rebuilt again.
  // Slippi keeps item->xD88/xD8C spawn-latched, so this is a hidden damage-product owner only.
  // refs/melee/src/melee/it/item.c::Item_80269F14
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_CreateReflectHit}
  // Source rewrite chain (asm-exact, refs it_80272460 + Item_80269F14):
  // every reflect callback consumes the CURRENT staled HitCapsule float, multiplies by that
  // reflect's xC6C, floors (+0.99f), and it_80272460 restales the integer by the NEW owner's
  // queue for the PUBLIC spawn move id (item->xD88, the Blaster id) via ft_80089228.
  //
  // The same-counter instance gate above proves this article is the reflector's OWN returned
  // laser (its spawn attack instance immediately precedes the reflector's shine instance on the
  // same per-player counter), so at least one intermediate enemy reflect occurred. The only
  // seed-invisible link is that intermediate reflect's multiplier; the witnessed shape is the
  // enemy powershield (p_ftCommonData powershield_reflect_damage_mul = 0.5), the sole reflect a
  // Guard-family state produces (MAJ rec2016: 3 dmg x own-blaster-stale 0.77 -> PS floor 2 ->
  // enemy restale 1.0 -> shine x1.5 floor 3 -> own restale 0.77 = replay's exact 2.31; the old
  // single-reflect-from-base model produced 5 -> 3.85).
  // refs/melee/src/melee/it/item.c::Item_80269F14
  // refs/melee/build/GALE01/asm/melee/it/itcoll.s::it_80272460 (ft_80089228(fp, raw, xD88, xD8C))
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_CreateReflectHit}
  // data/common/ft_common_data.json::powershield_reflect_damage_mul
  const uint16_t public_attack_id = (uint16_t)*io_attack_id;
  const float owner_blaster_stale = staling_multiplier_for_move(batch, owner_idx, public_attack_id);
  const float prev_stale = staling_multiplier_for_move(batch, prev_owner_idx, public_attack_id);
  const MslCommonParams* rc = msl_common_params();
  const float ps_mul = (rc != NULL && rc->powershield_reflect_damage_mul > 0.0f)
                           ? rc->powershield_reflect_damage_mul
                           : 1.0f;
  float product = script_damage * owner_blaster_stale;
  const float tmp1 = (product * ps_mul) + 0.99f;
  uint32_t raw1 = (tmp1 > 0.0f) ? (uint32_t)tmp1 : 0u;
  if (raw1 == 0u) {
    raw1 = 1u;
  }
  product = (float)raw1 * prev_stale;
  const float tmp2 = (product * reflect_mul) + 0.99f;
  uint32_t raw_damage = (tmp2 > 0.0f) ? (uint32_t)tmp2 : 0u;
  if (raw_damage == 0u) {
    raw_damage = 1u;
  }
  *io_attack_id = owner_attack_id;
  *io_attack_instance = batch->state.attack_instance[owner_idx];
  *io_damage = (float)raw_damage;
  *io_stale_mult = owner_blaster_stale;
  return 1u;
}

static inline uint8_t item_laser_runtime_reflected_body_damage_owner(
    const MslBatch* batch, int bi, size_t item_idx, int owner, float* io_damage,
    uint16_t* io_attack_id, uint16_t* io_attack_instance, float* io_stale_mult) {
  if (batch == NULL || io_damage == NULL || io_attack_id == NULL || io_attack_instance == NULL ||
      io_stale_mult == NULL || owner < 0 || owner >= (int)batch->config.num_players ||
      !(batch->state.item_reflect_damage_mul[item_idx] > 1.0f) ||
      batch->state.item_reflect_body_damage_valid[item_idx] == 0u ||
      batch->state.item_reflect_body_owner_port[item_idx] != (uint8_t)owner) {
    return 0u;
  }
  const uint16_t attack_id = batch->state.item_reflect_body_attack_id[item_idx];
  const uint16_t attack_instance = batch->state.item_reflect_body_attack_instance[item_idx];
  if (attack_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT || attack_instance == 0u) {
    return 0u;
  }
  const int prev_owner =
      item_reflected_laser_unique_previous_owner(batch, bi, owner, *io_attack_id);
  if (prev_owner < 0) {
    return 0u;
  }
  const size_t prev_owner_idx = msl_idx_player(bi, prev_owner);
  const size_t owner_idx = msl_idx_player(bi, owner);
  const uint16_t public_attack_id = (uint16_t)*io_attack_id;
  const float owner_blaster_stale = staling_multiplier_for_move(batch, owner_idx, public_attack_id);
  const float prev_stale = staling_multiplier_for_move(batch, prev_owner_idx, public_attack_id);
  // Same it_80272460 chain as the seeded variant below: every reflect callback floors the
  // CURRENT staled HitCapsule float x that reflect's xC6C (+0.99f) and restales by the NEW
  // owner's queue for the PUBLIC spawn move id. When the live owner's own character spawned this
  // article kind (own-returned laser), the chain ran twice: the enemy powershield (x0.5) and the
  // owner's shine (the mul lane). MAJ rec2016 rollout witness: 3 x 0.77 -> PS floor 2 -> enemy
  // restale 1.0 -> shine x1.5 floor 3 -> owner restale 0.77 = 2.31 (the single-rewrite model
  // with shine-id staling produced 5 x 0.96 = 4.80).
  // refs/melee/build/GALE01/asm/melee/it/itcoll.s::it_80272460 (ft_80089228(fp, raw, xD88, xD8C))
  // refs/melee/src/melee/it/item.c::Item_80269F14
  uint8_t own_returned = 0u;
  {
    const MslLaserParams* own_lp = laser_params_get(batch->state.char_id[owner_idx]);
    if (own_lp != NULL && own_lp->shot_itkind == batch->state.item_type[item_idx]) {
      own_returned = 1u;
      for (int pl = 0; pl < (int)batch->config.num_players; pl++) {
        if (pl == owner) {
          continue;
        }
        if (batch->state.char_id[msl_idx_player(bi, pl)] == batch->state.char_id[owner_idx]) {
          own_returned = 0u;
          break;
        }
      }
    }
  }
  const MslCommonParams* rc = msl_common_params();
  const float ps_mul = (rc != NULL && rc->powershield_reflect_damage_mul > 0.0f)
                           ? rc->powershield_reflect_damage_mul
                           : 1.0f;
  float product;
  uint32_t raw_damage;
  if (own_returned) {
    product = *io_damage * owner_blaster_stale;
    const float tmp1 = (product * ps_mul) + 0.99f;
    uint32_t raw1 = (tmp1 > 0.0f) ? (uint32_t)tmp1 : 0u;
    if (raw1 == 0u) {
      raw1 = 1u;
    }
    product = (float)raw1 * prev_stale;
  } else {
    product = *io_damage * prev_stale;
  }
  const float tmp2 = (product * batch->state.item_reflect_damage_mul[item_idx]) + 0.99f;
  raw_damage = (tmp2 > 0.0f) ? (uint32_t)tmp2 : 0u;
  if (raw_damage == 0u) {
    raw_damage = 1u;
  }
  *io_attack_id = attack_id;
  *io_attack_instance = attack_instance;
  *io_damage = (float)raw_damage;
  *io_stale_mult = owner_blaster_stale;
  return 1u;
}

static inline void item_laser_commit_reflected_body_stale_owner(MslBatch* batch, int bi,
                                                                size_t item_idx, int owner) {
  if (batch == NULL || owner < 0 || owner >= (int)batch->config.num_players ||
      batch->state.item_reflect_body_owner_port[item_idx] != (uint8_t)owner) {
    return;
  }
  // plStale_UpdateStaleMovesFromItem consumes item->xD88/xD8C, which Item_80269F14 has by then
  // rewritten from the reflecting fighter snapshot: MAJ rec2016->2017 replay-real queues show
  // p0 pushing id 21 (shine) instance 477 after the reflected-laser hit, not the spawn Blaster
  // lanes.
  // refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
  // refs/melee/src/melee/it/item.c::Item_80269F14
  const uint16_t attack_id = batch->state.item_reflect_body_attack_id[item_idx];
  const uint16_t attack_instance = batch->state.item_reflect_body_attack_instance[item_idx];
  if (attack_id != (uint16_t)MSL_FT_MOVE_ID_DEFAULT && attack_instance != 0u) {
    const size_t owner_idx = msl_idx_player(bi, owner);
    staling_queue_update(batch, owner_idx, attack_id, attack_instance);
  }
  batch->state.item_reflect_body_owner_port[item_idx] = (uint8_t)MSL_ITEM_REFLECT_NO_PORT;
  batch->state.item_reflect_body_attack_id[item_idx] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  batch->state.item_reflect_body_attack_instance[item_idx] = 0u;
  batch->state.item_reflect_body_damage_valid[item_idx] = 0u;
}

static inline uint8_t item_spawn_has_unique_same_source_damage_victim(const MslBatch* batch, int bi,
                                                                      int owner) {
  if (batch == NULL || owner < 0 || owner >= (int)batch->config.num_players) {
    return 0u;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  int candidate = -1;
  for (int vp = 0; vp < (int)batch->config.num_players; vp++) {
    if (vp == owner) {
      continue;
    }
    const size_t v_idx = msl_idx_player(bi, vp);
    const uint8_t victim_in_damage_episode =
        (batch->state.hitstun[v_idx] != 0u ||
         msl_damage_owner_is_damage_or_firefox_launch_action(batch->state.char_id[v_idx],
                                                             batch->state.action_id[v_idx]))
            ? 1u
            : 0u;
    if (victim_in_damage_episode == 0u ||
        !msl_damage_source_victim_port_matches_attacker(batch, v_idx, o_idx, owner)) {
      continue;
    }
    if (candidate >= 0) {
      return 0u;
    }
    candidate = vp;
  }
  return (candidate >= 0) ? 1u : 0u;
}

static inline uint8_t item_stale_queue_contains_instance(const MslBatch* batch, size_t owner_idx,
                                                         uint16_t attack_id,
                                                         uint16_t attack_instance) {
  if (batch == NULL || attack_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT || attack_instance == 0u) {
    return 0u;
  }
  const size_t base = owner_idx * (size_t)MSL_STALE_QUEUE_SIZE;
  for (int i = 0; i < MSL_STALE_QUEUE_SIZE; i++) {
    if (batch->state.stale_move_id[base + (size_t)i] == attack_id &&
        batch->state.stale_attack_instance[base + (size_t)i] == attack_instance) {
      return 1u;
    }
  }
  return 0u;
}

static inline void item_slot_clear(MslBatch* batch, size_t ii) {
  if (batch == NULL) {
    return;
  }
  batch->state.item_exists[ii] = 0;
  batch->state.item_state[ii] = 0;
  batch->state.item_type[ii] = 0;
  batch->state.item_owner[ii] = -1;
  batch->state.item_instance_id[ii] = 0;
  batch->state.item_attack_id[ii] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  batch->state.item_attack_instance[ii] = 0;
  batch->state.item_direction[ii] = 0.0f;
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  batch->state.item_pos_x[ii] = 0.0f;
  batch->state.item_pos_y[ii] = 0.0f;
  batch->state.item_damage[ii] = 0;
  batch->state.item_stale_damage_valid[ii] = 0u;
  batch->state.item_stale_damage_mul[ii] = 1.0f;
  batch->state.item_timer[ii] = 0.0f;
  batch->state.item_hitlag[ii] = 0u;
  batch->state.item_spawn_id[ii] = 0;
  batch->state.item_misc0[ii] = 0;
  batch->state.item_misc1[ii] = 0;
  batch->state.item_misc2[ii] = 0;
  batch->state.item_misc3[ii] = 0;
  msl_item_reflect_clear_all_lanes(batch, ii);
  batch->state.item_hidden_body_hit_victim_port[ii] = 0xFFu;
  batch->state.item_hidden_body_hit_hurt_height[ii] = 0u;
  batch->state.item_hidden_callback_flags[ii] = 0u;
  batch->state.item_sheik_needle_hidden_drop_valid[ii] = 0u;
  batch->state.item_sheik_needle_hidden_drop_min_vel_y[ii] = 0.0f;
  batch->state.item_sheik_needle_hidden_drop_gravity[ii] = 0.0f;
  batch->state.item_sheik_needle_hidden_drop_vel_x[ii] = 0.0f;
  batch->state.item_sheik_chain_links_valid[ii] = 0u;
  {
    const size_t link_base = ii * (size_t)MSL_SHEIK_CHAIN_MAX_LINKS;
    for (size_t li = 0; li < (size_t)MSL_SHEIK_CHAIN_MAX_LINKS; li++) {
      batch->state.item_sheik_chain_link_active[link_base + li] = 0u;
      batch->state.item_sheik_chain_link_pos_x[link_base + li] = 0.0f;
      batch->state.item_sheik_chain_link_pos_y[link_base + li] = 0.0f;
      batch->state.item_sheik_chain_link_pos_z[link_base + li] = 0.0f;
      batch->state.item_sheik_chain_link_vel_x[link_base + li] = 0.0f;
      batch->state.item_sheik_chain_link_vel_y[link_base + li] = 0.0f;
      batch->state.item_sheik_chain_link_vel_z[link_base + li] = 0.0f;
    }
    const size_t hb_base = ii * (size_t)MSL_MAX_HITBOXES;
    for (size_t hb = 0; hb < (size_t)MSL_MAX_HITBOXES; hb++) {
      batch->state.item_sheik_chain_hitbox_link_idx[hb_base + hb] = 0xFFu;
      batch->state.item_sheik_chain_hit_prev_x[hb_base + hb] = 0.0f;
      batch->state.item_sheik_chain_hit_prev_y[hb_base + hb] = 0.0f;
    }
    const size_t hist_base = ii * (size_t)MSL_SHEIK_CHAIN_HISTORY_LEN;
    for (size_t hi = 0; hi < (size_t)MSL_SHEIK_CHAIN_HISTORY_LEN; hi++) {
      batch->state.item_sheik_chain_history_x[hist_base + hi] = 0.0f;
      batch->state.item_sheik_chain_history_y[hist_base + hi] = 0.0f;
    }
  }
  batch->state.item_sheik_chain_prev_stick_x[ii] = 0.0f;
  batch->state.item_sheik_chain_prev_stick_y[ii] = 0.0f;
  batch->state.item_sheik_chain_hitcaps_active[ii] = 0u;
  batch->state.item_sheik_chain_hit_cooldown[ii] = 0u;
  batch->state.item_sheik_chain_hit_reset_prev[ii] = 0u;
  batch->state.item_sheik_chain_hit_prev_valid[ii] = 0u;
  batch->state.item_sheik_chain_env_flags[ii] = 0u;
  batch->state.item_sheik_chain_target_valid[ii] = 0u;
  batch->state.item_sheik_chain_target_x[ii] = 0.0f;
  batch->state.item_sheik_chain_target_y[ii] = 0.0f;
  batch->state.item_sheik_chain_target_z[ii] = 0.0f;
  batch->state.item_shyguy_prev_vel_y[ii] = 0.0f;
  batch->state.item_shyguy_prev_vel_y_valid[ii] = 0u;
  batch->state.item_shyguy_dyn_y_phase[ii] = 0u;
  batch->state.item_shyguy_dyn_y_phase_valid[ii] = 0u;
  batch->state.item_shyguy_speed_index[ii] = 0u;
  batch->state.item_shyguy_speed_index_valid[ii] = 0u;
  batch->state.item_shyguy_delay[ii] = 0u;
  batch->state.item_shyguy_delay_valid[ii] = 0u;
  batch->state.item_shyguy_hitlag[ii] = 0u;
  batch->state.item_shyguy_hitlag_valid[ii] = 0u;

  // Clear per-item/per-HitCapsule victim rings (hitlist).
  // Items own one HitCapsule victim ring per article hitbox:
  // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4}
  const size_t hitlist_base = ii * (size_t)MSL_MAX_HITBOXES;
  for (size_t hb = 0; hb < (size_t)MSL_MAX_HITBOXES; hb++) {
    hitlist_capsule_clear(&batch->state.item_hitlist[hitlist_base + hb]);
  }
}

static inline uint8_t items_row_has_any(const MslBatch* batch, int bi) {
  if (batch == NULL) {
    return 0u;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii]) {
      return 1u;
    }
  }
  return 0u;
}

static inline void item_slot_swap(MslBatch* batch, size_t a, size_t b) {
  if (batch == NULL || a == b) {
    return;
  }
#define SWAP(T, arr)        \
  do {                      \
    const T tmp = (arr)[a]; \
    (arr)[a] = (arr)[b];    \
    (arr)[b] = tmp;         \
  } while (0)
  SWAP(uint8_t, batch->state.item_exists);
  SWAP(uint8_t, batch->state.item_state);
  SWAP(uint16_t, batch->state.item_type);
  SWAP(int8_t, batch->state.item_owner);
  SWAP(uint16_t, batch->state.item_instance_id);
  SWAP(uint16_t, batch->state.item_attack_id);
  SWAP(uint16_t, batch->state.item_attack_instance);
  SWAP(float, batch->state.item_direction);
  SWAP(float, batch->state.item_vel_x);
  SWAP(float, batch->state.item_vel_y);
  SWAP(float, batch->state.item_pos_x);
  SWAP(float, batch->state.item_pos_y);
  SWAP(uint16_t, batch->state.item_damage);
  SWAP(uint8_t, batch->state.item_stale_damage_valid);
  SWAP(float, batch->state.item_stale_damage_mul);
  SWAP(float, batch->state.item_reflect_damage_mul);
  SWAP(uint8_t, batch->state.item_reflect_body_owner_port);
  SWAP(uint16_t, batch->state.item_reflect_body_attack_id);
  SWAP(uint16_t, batch->state.item_reflect_body_attack_instance);
  SWAP(uint8_t, batch->state.item_reflect_body_damage_valid);
  SWAP(float, batch->state.item_timer);
  SWAP(uint8_t, batch->state.item_hitlag);
  SWAP(uint32_t, batch->state.item_spawn_id);
  SWAP(uint8_t, batch->state.item_misc0);
  SWAP(uint8_t, batch->state.item_misc1);
  SWAP(uint8_t, batch->state.item_misc2);
  SWAP(uint8_t, batch->state.item_misc3);
  SWAP(uint8_t, batch->state.item_pending_reflect_owner_port);
  SWAP(uint16_t, batch->state.item_pending_reflect_instance_id);
  SWAP(uint8_t, batch->state.item_reflect_transfer_seed_port);
  SWAP(uint16_t, batch->state.item_reflect_transfer_seed_iid);
  SWAP(uint8_t, batch->state.item_shield_bounce_seed_valid);
  SWAP(float, batch->state.item_shield_bounce_seed_vel_x);
  SWAP(float, batch->state.item_shield_bounce_seed_vel_y);
  SWAP(uint8_t, batch->state.item_hidden_body_hit_victim_port);
  SWAP(uint8_t, batch->state.item_hidden_body_hit_hurt_height);
  SWAP(uint8_t, batch->state.item_hidden_callback_flags);
  SWAP(uint8_t, batch->state.item_sheik_needle_hidden_drop_valid);
  SWAP(float, batch->state.item_sheik_needle_hidden_drop_min_vel_y);
  SWAP(float, batch->state.item_sheik_needle_hidden_drop_gravity);
  SWAP(float, batch->state.item_sheik_needle_hidden_drop_vel_x);
  SWAP(uint8_t, batch->state.item_sheik_chain_links_valid);
  SWAP(float, batch->state.item_sheik_chain_prev_stick_x);
  SWAP(float, batch->state.item_sheik_chain_prev_stick_y);
  SWAP(uint8_t, batch->state.item_sheik_chain_hitcaps_active);
  SWAP(uint8_t, batch->state.item_sheik_chain_hit_cooldown);
  SWAP(uint8_t, batch->state.item_sheik_chain_hit_reset_prev);
  SWAP(uint8_t, batch->state.item_sheik_chain_hit_prev_valid);
  SWAP(uint32_t, batch->state.item_sheik_chain_env_flags);
  SWAP(uint8_t, batch->state.item_sheik_chain_target_valid);
  SWAP(float, batch->state.item_sheik_chain_target_x);
  SWAP(float, batch->state.item_sheik_chain_target_y);
  SWAP(float, batch->state.item_sheik_chain_target_z);
  SWAP(float, batch->state.item_shyguy_prev_vel_y);
  SWAP(uint8_t, batch->state.item_shyguy_prev_vel_y_valid);
  SWAP(uint8_t, batch->state.item_shyguy_dyn_y_phase);
  SWAP(uint8_t, batch->state.item_shyguy_dyn_y_phase_valid);
  SWAP(uint8_t, batch->state.item_shyguy_speed_index);
  SWAP(uint8_t, batch->state.item_shyguy_speed_index_valid);
  SWAP(uint16_t, batch->state.item_shyguy_delay);
  SWAP(uint8_t, batch->state.item_shyguy_delay_valid);
  SWAP(uint8_t, batch->state.item_shyguy_hitlag);
  SWAP(uint8_t, batch->state.item_shyguy_hitlag_valid);
#undef SWAP

  // Swap per-item/per-HitCapsule hitlist lanes to preserve deterministic item ordering invariants.
  {
    const size_t a_base = a * (size_t)MSL_MAX_HITBOXES;
    const size_t b_base = b * (size_t)MSL_MAX_HITBOXES;
    for (size_t hb = 0; hb < (size_t)MSL_MAX_HITBOXES; hb++) {
      const MslHitlistCapsule tmp = batch->state.item_hitlist[a_base + hb];
      batch->state.item_hitlist[a_base + hb] = batch->state.item_hitlist[b_base + hb];
      batch->state.item_hitlist[b_base + hb] = tmp;
    }
  }

  // Swap the per-item Sheik Chain Verlet link arrays to preserve item ordering invariants.
  {
    const size_t a_base = a * (size_t)MSL_SHEIK_CHAIN_MAX_LINKS;
    const size_t b_base = b * (size_t)MSL_SHEIK_CHAIN_MAX_LINKS;
    for (size_t li = 0; li < (size_t)MSL_SHEIK_CHAIN_MAX_LINKS; li++) {
#define SWAP_LINK(ARR, TY)                                         \
  do {                                                             \
    const TY t__ = batch->state.ARR[a_base + li];                  \
    batch->state.ARR[a_base + li] = batch->state.ARR[b_base + li]; \
    batch->state.ARR[b_base + li] = t__;                           \
  } while (0)
      SWAP_LINK(item_sheik_chain_link_pos_x, float);
      SWAP_LINK(item_sheik_chain_link_pos_y, float);
      SWAP_LINK(item_sheik_chain_link_pos_z, float);
      SWAP_LINK(item_sheik_chain_link_vel_x, float);
      SWAP_LINK(item_sheik_chain_link_vel_y, float);
      SWAP_LINK(item_sheik_chain_link_vel_z, float);
      SWAP_LINK(item_sheik_chain_link_active, uint8_t);
#undef SWAP_LINK
    }
  }

  {
    const size_t a_base = a * (size_t)MSL_MAX_HITBOXES;
    const size_t b_base = b * (size_t)MSL_MAX_HITBOXES;
    for (size_t hb = 0; hb < (size_t)MSL_MAX_HITBOXES; hb++) {
      const uint8_t t = batch->state.item_sheik_chain_hitbox_link_idx[a_base + hb];
      batch->state.item_sheik_chain_hitbox_link_idx[a_base + hb] =
          batch->state.item_sheik_chain_hitbox_link_idx[b_base + hb];
      batch->state.item_sheik_chain_hitbox_link_idx[b_base + hb] = t;
      const float px = batch->state.item_sheik_chain_hit_prev_x[a_base + hb];
      batch->state.item_sheik_chain_hit_prev_x[a_base + hb] =
          batch->state.item_sheik_chain_hit_prev_x[b_base + hb];
      batch->state.item_sheik_chain_hit_prev_x[b_base + hb] = px;
      const float py = batch->state.item_sheik_chain_hit_prev_y[a_base + hb];
      batch->state.item_sheik_chain_hit_prev_y[a_base + hb] =
          batch->state.item_sheik_chain_hit_prev_y[b_base + hb];
      batch->state.item_sheik_chain_hit_prev_y[b_base + hb] = py;
    }
  }

  // Swap the per-item Sheik Chain stick-history trail.
  {
    const size_t a_base = a * (size_t)MSL_SHEIK_CHAIN_HISTORY_LEN;
    const size_t b_base = b * (size_t)MSL_SHEIK_CHAIN_HISTORY_LEN;
    for (size_t hi = 0; hi < (size_t)MSL_SHEIK_CHAIN_HISTORY_LEN; hi++) {
      const float tx = batch->state.item_sheik_chain_history_x[a_base + hi];
      batch->state.item_sheik_chain_history_x[a_base + hi] =
          batch->state.item_sheik_chain_history_x[b_base + hi];
      batch->state.item_sheik_chain_history_x[b_base + hi] = tx;
      const float ty = batch->state.item_sheik_chain_history_y[a_base + hi];
      batch->state.item_sheik_chain_history_y[a_base + hi] =
          batch->state.item_sheik_chain_history_y[b_base + hi];
      batch->state.item_sheik_chain_history_y[b_base + hi] = ty;
    }
  }
}

static inline int item_key_lt(MslBatch* batch, size_t a, size_t b) {
  // Sort key matches dataset fixed ordering:
  // tools/slippi/make_dataset_from_slp.py::_fill_items_fixed sorts by (instance_id, spawn_id, type).
  const uint8_t ea = batch->state.item_exists[a] ? 1u : 0u;
  const uint8_t eb = batch->state.item_exists[b] ? 1u : 0u;
  if (ea != eb) {
    return ea > eb;
  }
  if (!ea) {
    return 0;
  }
  const uint16_t ia = batch->state.item_instance_id[a];
  const uint16_t ib = batch->state.item_instance_id[b];
  if (ia != ib) {
    return ia < ib;
  }
  const uint32_t sa = batch->state.item_spawn_id[a];
  const uint32_t sb = batch->state.item_spawn_id[b];
  if (sa != sb) {
    return sa < sb;
  }
  return batch->state.item_type[a] < batch->state.item_type[b];
}

static inline void items_sort(MslBatch* batch, int bi) {
  // Stable in-place insertion sort over 15 slots (deterministic; no allocations).
  for (int j = 1; j < MSL_MAX_ITEMS; j++) {
    int i = j;
    while (i > 0) {
      const size_t a = msl_idx_item(bi, i - 1);
      const size_t b = msl_idx_item(bi, i);
      if (!item_key_lt(batch, b, a)) {
        break;
      }
      item_slot_swap(batch, a, b);
      i--;
    }
  }
  // Normalize empty-slot owner to -1 for deterministic dataset parity.
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      batch->state.item_owner[ii] = -1;
    }
  }
}

static inline int items_alloc_slot(MslBatch* batch, int bi) {
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      return it;
    }
  }
  return -1;
}

static inline uint32_t items_next_spawn_id(MslBatch* batch, int bi) {
  // Item spawn id is the global item->x1C counter, not a function of currently-live items.
  // This matters for rollouts seeded after itemless gaps: future item fixed-slot ordering still
  // depends on historical spawns through x1C.
  // refs/melee/src/melee/it/item.c::Item_80267AA8
  const uint32_t out = batch->state.item_spawn_id_counter[bi];
  batch->state.item_spawn_id_counter[bi] = out + 1u;
  return out;
}

static uint8_t yoshi_shyguy_has_live(const MslBatch* batch, int bi,
                                     const MslYoshiShyguyParams* params) {
  if (batch == NULL || params == NULL) {
    return 0u;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] &&
        stage_item_params_is_yoshi_shyguy_item_type(params, batch->state.item_type[ii])) {
      return 1u;
    }
  }
  return 0u;
}

static inline void yoshi_shyguy_clear_rollout_rng_owner_if_live(MslBatch* batch, int bi) {
  if (batch == NULL || batch->rollout_clock_rng_owned == NULL) {
    return;
  }
  if (batch->rollout_clock_rng_owned[bi] ==
      (uint8_t)MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED_YOSHI_SHYGUY) {
    // `grStory_801E3418` returns immediately while any Heiho item is live. If a replay rollout
    // no-live scheduler owner reaches this state, ownership is over; later live-Heiho frames are
    // seed-owned again and must not keep advancing Slippi's frame RNG lane.
    // refs/melee/src/melee/gr/grstory.c::grStory_801E3418
    batch->rollout_clock_rng_owned[bi] = (uint8_t)MSL_ROLLOUT_CLOCK_NONE;
    batch->state.stage_yoshi_shyguy_spawn_rng_valid[bi] = 0u;
  }
}

static inline void yoshi_shyguy_install_rollout_spawn_rng_seed(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  if (batch->state.stage_yoshi_shyguy_spawn_rng_valid[bi] == 0u) {
    // Autonomous no-live rollout windows can reach a later Shy Guy zero-timer callback after the
    // original replay seed row. When a broader replay-frame RNG owner is active, the callback must
    // sample the current Slippi frame-start stream, not the previous post-frame seed still stored
    // in public state at step start. Explicit spawn lanes above remain the stricter owner for
    // reseeds that already know the source spawn-frame seed.
    // refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    // refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    combat_rng_use_next_replay_frame_seed_if_unconsumed(batch, bi);
    return;
  }
  // Replay-seeded countdowns do not expose unrelated global HSD consumers between frame starts.
  // The seed lane carries the source spawn-frame stream; install it immediately before
  // grStory_801E3418's zero-timer RNG consumers so combat_rng_consume_* samples the same stream.
  // This callback-local install can coexist with broader replay-frame RNG clock owners (opening
  // countdown, DamageFlyRoll phase, etc.); api.c suppresses only that frame's generic +0x10000
  // commit after this source callback has installed the spawn-frame seed.
  // refs/slippi-ssbm-asm/Recording/SendFrameStart.s
  // refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
  // refs/melee/src/sysdolphin/baselib/random.c::{HSD_Randi,HSD_Randf}
  const uint32_t seed = batch->state.stage_yoshi_shyguy_spawn_rng_seed[bi];
  batch->state.frame_pre_random_seed[bi] = seed;
  if (batch->rollout_yoshi_shyguy_spawn_rng_installed != NULL) {
    batch->rollout_yoshi_shyguy_spawn_rng_installed[bi] = 1u;
  }
  if (batch->rng_shadow_seed != NULL) {
    batch->rng_shadow_seed[bi] = seed;
  }
  if (batch->debug_rng_seed_in != NULL) {
    batch->debug_rng_seed_in[bi] = seed;
  }
  if (batch->debug_rng_seed_out != NULL) {
    batch->debug_rng_seed_out[bi] = seed;
  }
}

static inline void yoshi_shyguy_mark_rollout_rng_owner_consumed(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  if (batch->rollout_clock_rng_owned != NULL &&
      batch->rollout_clock_rng_owned[bi] ==
          (uint8_t)MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED_YOSHI_SHYGUY) {
    // The zero-timer stage callback has consumed the spawn-frame HSD RNG stream. Slippi's
    // post-frame seed lane stays at that consumed frame-start value for this row, so clear the
    // owner before api.c's rollout-clock commit.
    // refs/slippi-ssbm-asm/Recording/SendFrameStart.s
    // refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    batch->rollout_clock_rng_owned[bi] = (uint8_t)MSL_ROLLOUT_CLOCK_NONE;
  }
  batch->state.stage_yoshi_shyguy_spawn_rng_valid[bi] = 0u;
}

static float yoshi_shyguy_dyn_y_phase_value(const MslYoshiShyguyParams* params, int phase) {
  phase &= 0xFF;
  if (phase == 0) {
    return 0.0f;
  }
  if (phase <= MSL_YOSHI_SHYGUY_DYN_Y_COUNT) {
    return params->dyn_y_vel[phase - 1];
  }
  return params->dyn_y_vel[256 - phase];
}

static float yoshi_shyguy_dyn_y_pos_after_phase(const MslYoshiShyguyParams* params, int phase) {
  return params->dyn_y_pos_after_phase[phase & 0xFF];
}

static float yoshi_shyguy_dyn_y_rate2_value(const MslYoshiShyguyParams* params, int phase) {
  phase &= 0xFF;
  if (phase == 0) {
    return 0.0f;
  }
  if (phase == 1) {
    return yoshi_shyguy_dyn_y_phase_value(params, 1);
  }
  const int base = (2 * phase) - 2;
  return yoshi_shyguy_dyn_y_phase_value(params, base) +
         yoshi_shyguy_dyn_y_phase_value(params, base + 1);
}

static float yoshi_shyguy_dyn_y_pair_forward(const MslYoshiShyguyParams* params, int pair) {
  pair &= 0x3F;
  const int base = pair * 2;
  return params->dyn_y_vel[base] + params->dyn_y_vel[(base + 1) & 0x7F];
}

static uint8_t yoshi_shyguy_next_dynamic_vel_y(const MslYoshiShyguyParams* params, float prev_vel_y,
                                               float cur_vel_y, uint8_t rate2, float* out) {
  if (params == NULL || out == NULL || !isfinite(prev_vel_y) || !isfinite(cur_vel_y)) {
    return 0u;
  }
  const float eps = 0.001f;
  if (rate2 != 0u) {
    for (int i = 0; i < 256; i++) {
      const int prev_i = (i + 255) & 0xFF;
      if (fabsf(yoshi_shyguy_dyn_y_rate2_value(params, i) - cur_vel_y) <= eps &&
          fabsf(yoshi_shyguy_dyn_y_rate2_value(params, prev_i) - prev_vel_y) <= eps) {
        *out = yoshi_shyguy_dyn_y_rate2_value(params, (i + 1) & 0xFF);
        return 1u;
      }
    }
    return 0u;
  }
  for (int i = 0; i < 256; i++) {
    const int prev_i = (i + 255) & 0xFF;
    if (fabsf(yoshi_shyguy_dyn_y_phase_value(params, i) - cur_vel_y) <= eps &&
        fabsf(yoshi_shyguy_dyn_y_phase_value(params, prev_i) - prev_vel_y) <= eps) {
      *out = yoshi_shyguy_dyn_y_phase_value(params, (i + 1) & 0xFF);
      return 1u;
    }
  }
  return 0u;
}

static float yoshi_shyguy_vel_y_from_phase(const MslYoshiShyguyParams* params, uint8_t phase,
                                           uint8_t state) {
  if (state == 4u) {
    const uint8_t p = phase;
    if (p <= 29u) {
      // Return-flight can enter with the source x24 camera-turn delay set. During this prefix,
      // Slippi keeps x40_vel.y at zero even though `it_802D98C4` still contributes the first child
      // dynamic-bone delta to item position.
      // refs/melee/src/melee/it/items/itheiho.c::{it_802D9168,itHeiho_UnkMotion4_Phys}
      return yoshi_shyguy_dyn_y_phase_value(params, 1);
    }
    const uint8_t q = (uint8_t)(p - 18u);
    if (q == 139u) {
      // `HSD_AObjInterpretAnim` rewinds the looping child AObj before the next update. The visible
      // dynamic-bone delta is the reset-to-zero jump, not another rate-2 pair.
      // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
      // refs/melee/src/sysdolphin/baselib/fobj.c::HSD_FObjInterpretAnim
      return -yoshi_shyguy_dyn_y_phase_value(params, 1);
    }
    if (q >= 140u) {
      return yoshi_shyguy_dyn_y_pair_forward(params, (int)(q - 140u));
    }
    return yoshi_shyguy_dyn_y_rate2_value(params, (int)(q - 10u));
  }
  return yoshi_shyguy_dyn_y_phase_value(params, ((int)phase + 1) & 0xFF);
}

static float yoshi_shyguy_current_vel_y_for_phase(const MslYoshiShyguyParams* params, uint8_t phase,
                                                  uint8_t state) {
  if (state == 4u) {
    const uint8_t p = phase;
    if (p <= 29u) {
      return 0.0f;
    }
    const uint8_t q = (uint8_t)(p - 18u);
    if (q == 12u) {
      return yoshi_shyguy_dyn_y_phase_value(params, 1);
    }
    if (q == 140u) {
      return -yoshi_shyguy_dyn_y_phase_value(params, 1);
    }
    if (q >= 141u) {
      return yoshi_shyguy_dyn_y_pair_forward(params, (int)(q - 141u));
    }
    return yoshi_shyguy_dyn_y_rate2_value(params, (int)(q - 11u));
  }
  return yoshi_shyguy_dyn_y_phase_value(params, phase);
}

static uint8_t yoshi_shyguy_state1_current_is_reset_export(const MslYoshiShyguyParams* params,
                                                           uint8_t phase, float prev_vel_y,
                                                           float cur_vel_y) {
  if (params == NULL) {
    return 0u;
  }
  const uint8_t prev_phase = (uint8_t)((phase + 255u) & 0xFFu);
  const float expected_prev = yoshi_shyguy_dyn_y_phase_value(params, prev_phase);
  const float reset_export = -yoshi_shyguy_dyn_y_pos_after_phase(params, phase);
  return (uint8_t)(fabsf(expected_prev - prev_vel_y) <= 0.001f &&
                   fabsf(reset_export - cur_vel_y) <= 0.001f);
}

static inline uint8_t yoshi_shyguy_falling_state_crossed_generic_blast_clear(
    const MslStageBounds* blast_bounds, float x, float y) {
  if (blast_bounds == NULL) {
    return 0u;
  }
  // Knocked/falling Heiho states run the generic item post-Phys destroy gate after position
  // integration. State 2/3 set xDCC_flag.b3 through it_802D8EC8; Item_802696CC then destroys on
  // the enabled side/bottom blast bounds (top uses the generic 10000.0 sentinel and is irrelevant
  // for this stage item).
  // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,itHeiho_UnkMotion2_Phys,
  //   itHeiho_UnkMotion3_Phys}
  // refs/melee/src/melee/it/item.c::{Item_802697D4,Item_802696CC}
  return (x > blast_bounds->right || x < blast_bounds->left || y < blast_bounds->bottom) ? 1u : 0u;
}

static uint8_t yoshi_shyguy_active_fixed_ecb_wall_contact(const MslYoshiShyguyParams* params,
                                                          uint32_t stage_id, float facing_dir,
                                                          float prev_pos_x, float prev_pos_y,
                                                          float pos_x, float pos_y) {
  if (params == NULL) {
    return 0u;
  }
  const float scale = params->collision_ecb_scale;
  const float ecb_top = params->collision_ecb_up * scale;
  const float ecb_bottom = -params->collision_ecb_down * scale;
  const float ecb_right = params->collision_ecb_right * scale;
  const float ecb_left = -params->collision_ecb_left * scale;
  const int wall_side = (facing_dir < 0.0f) ? 1 : 0;
  // Active and return-flight Heiho call `it_8026DA70`, then `it_80276308` tests wall contact
  // against the item CollData fixed ECB installed from Article ItemAttr.x40 by `it_80275DFC`.
  // The queried wall graph is the wall ahead of the current facing direction.
  // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Coll,itHeiho_UnkMotion4_Coll}
  // refs/melee/src/melee/it/itgroundcoll.c::it_8026DA70
  // refs/melee/src/melee/it/it_2725.c::{it_80275DFC,it_80276308}
  // refs/melee/src/melee/mp/mpcoll.c::mpColl_SetECBSource_Fixed
  return stage_collision_item_fixed_ecb_sweep_hits_wall(stage_id, wall_side, prev_pos_x, prev_pos_y,
                                                        pos_x, pos_y, ecb_left, ecb_right,
                                                        ecb_bottom, ecb_top);
}

static uint8_t yoshi_shyguy_active_fixed_ecb_floor_contact(const MslYoshiShyguyParams* params,
                                                           uint32_t stage_id, float prev_pos_x,
                                                           float prev_pos_y, float pos_x,
                                                           float pos_y) {
  if (params == NULL) {
    return 0u;
  }
  const float scale = params->collision_ecb_scale;
  const float ecb_bottom = -params->collision_ecb_down * scale;
  const float ecb_right = params->collision_ecb_right * scale;
  const float ecb_left = -params->collision_ecb_left * scale;
  // Active and return-flight Heiho call `it_8026DA70`; when the fixed-ECB mpColl pass reports
  // contact but the wall-turn branch does not run, state 1 restarts its active animation and state
  // 4 re-enters return flight. `it_8026DA70` does not copy CollData.cur_pos to Item.pos, so this
  // helper only owns the boolean animation-reset branch.
  // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Coll,itHeiho_UnkMotion4_Coll}
  // refs/melee/src/melee/it/itgroundcoll.c::it_8026DA70
  return stage_collision_item_fixed_ecb_sweep_hits_floor(stage_id, prev_pos_x, prev_pos_y, pos_x,
                                                         pos_y, ecb_left, ecb_right, ecb_bottom);
}

static int yoshi_shyguy_spawn_count(MslBatch* batch, int bi) {
  // Source calls set_shyguy_spawn_count twice; the second call overwrites the first, but both RNG
  // streams are consumed.
  // refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
  int unused_count = 1;
  if (combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY8, 8) == 0) {
    unused_count =
        combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY8_BONUS, 3) +
        3;
  }
  (void)unused_count;
  if (combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY2, 2) == 0) {
    const int count =
        combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_COUNT_RARITY2_BONUS, 3) +
        3;
    // Source count owner: the second `set_shyguy_spawn_count(gp, 2)` overwrites the earlier
    // rarity-8 result and can spawn 3..5 Shy Guys from this bonus sample.
    // refs/melee/src/melee/gr/grstory.c::{grStory_801E3418,set_shyguy_spawn_count}
    return count;
  }
  return 1;
}

static void yoshi_shyguy_spawn_one(MslBatch* batch, int bi, int arg0, float pos_x, float pos_y,
                                   uint8_t speed_index, const MslYoshiShyguyParams* params) {
  if (params == NULL) {
    return;
  }
  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);
  batch->state.item_exists[ii] = 1u;
  batch->state.item_state[ii] = 0u;
  batch->state.item_type[ii] = params->item_kind;
  batch->state.item_owner[ii] = -1;
  batch->state.item_instance_id[ii] = 0u;
  batch->state.item_attack_id[ii] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
  batch->state.item_attack_instance[ii] = 0u;
  batch->state.item_direction[ii] = (pos_x < 0.0f) ? 1.0f : -1.0f;
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  batch->state.item_pos_x[ii] = pos_x;
  batch->state.item_pos_y[ii] = pos_y;
  batch->state.item_damage[ii] = 0u;
  msl_item_reflect_clear_all_lanes(batch, ii);
  batch->state.item_timer[ii] = 1400.0f;
  batch->state.item_hitlag[ii] = 0u;
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_shyguy_speed_index[ii] = speed_index % 3u;
  batch->state.item_shyguy_speed_index_valid[ii] = 1u;
  batch->state.item_shyguy_dyn_y_phase[ii] = 0u;
  batch->state.item_shyguy_dyn_y_phase_valid[ii] = 1u;
  batch->state.item_shyguy_delay[ii] = (uint16_t)(params->spawn_delay_step * arg0);
  batch->state.item_shyguy_delay_valid[ii] = 1u;
  batch->state.item_shyguy_hitlag[ii] = 0u;
  batch->state.item_shyguy_hitlag_valid[ii] = 1u;
}

static void yoshi_shyguy_stage_update(MslBatch* batch, int bi) {
  const MslYoshiShyguyParams* params = stage_item_params_yoshi_shyguy();
  if (batch == NULL || batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_YOSHIS_STORY ||
      batch->state.stage_yoshi_shyguy_valid[bi] == 0u || params == NULL) {
    return;
  }
  if (yoshi_shyguy_has_live(batch, bi, params) != 0u) {
    yoshi_shyguy_clear_rollout_rng_owner_if_live(batch, bi);
    return;
  }
  if (batch->state.stage_yoshi_shyguy_timer[bi] != 0u) {
    batch->state.stage_yoshi_shyguy_timer[bi]--;
    return;
  }

  yoshi_shyguy_install_rollout_spawn_rng_seed(batch, bi);

  // `reset_shyguy_timer` first samples timer_min + HSD_Randi(timer_rand), then immediately
  // overwrites the timer with 120. The sampled value is discarded but the RNG consumer is real and
  // phase-orders the following pattern/speed/count/jitter samples.
  // refs/melee/src/melee/gr/grstory.c::{reset_shyguy_timer,grStory_801E3418}
  (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_TIMER_WAIT,
                                      (int32_t)params->timer_rand);
  batch->state.stage_yoshi_shyguy_timer[bi] = params->timer_reset;
  uint8_t pattern = batch->state.stage_yoshi_shyguy_pattern[bi] % MSL_YOSHI_SHYGUY_VPOS_COUNT;
  uint8_t next_pattern = pattern;
  do {
    next_pattern = (uint8_t)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_PATTERN, MSL_YOSHI_SHYGUY_VPOS_COUNT);
  } while (next_pattern == pattern);
  batch->state.stage_yoshi_shyguy_pattern[bi] = next_pattern;

  const float base_y = params->vpos[next_pattern];
  const float pos_x = (next_pattern < 3u) ? params->spawn_left_x : params->spawn_right_x;
  const uint8_t speed_index =
      (uint8_t)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_SPEED_INDEX, 3);
  const int count = yoshi_shyguy_spawn_count(batch, bi);
  float pos_y = base_y;
  for (int i = 0; i < count; i++) {
    yoshi_shyguy_spawn_one(batch, bi, i, pos_x, pos_y, speed_index, params);
    const float randf = combat_rng_consume_randf_site(batch, bi, MSL_RNG_SITE_YOSHI_SHYGUY_JITTER);
    pos_y = (params->jitter_y_amp * ((2.0f * randf) - 1.0f)) + base_y;
  }
  items_sort(batch, bi);
  yoshi_shyguy_mark_rollout_rng_owner_consumed(batch, bi);
}

static inline int items_find_gun_slot(const MslBatch* batch, int bi, int owner,
                                      uint16_t gun_itkind) {
  if (batch == NULL || owner < 0) {
    return -1;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      continue;
    }
    if (batch->state.item_type[ii] != gun_itkind) {
      continue;
    }
    if (batch->state.item_owner[ii] != owner) {
      continue;
    }
    // Blaster gun items commonly have spawn_id=0 in Slippi; don't require it here
    // (identity keying is handled elsewhere).
    return it;
  }
  return -1;
}

static inline uint8_t blaster_gun_state_from_action_id(uint16_t action_id_u16) {
  // ftFx_SpecialN_GetBlasterAction returns:
  // - msid = currASID - ftFx_MS_SpecialNStart for SpecialN{Start/Loop/End}/SpecialAirN{Start/Loop/End}
  // - msid = currASID - ftCo_MS_CatchDash for Throw{B/Hi/Lw}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_GetBlasterAction
  // The corresponding index enum is decomp-defined as ftFx_SpecialNIndex:
  // refs/melee/src/melee/ft/chara/ftFox/forward.h::ftFx_SpecialNIndex
  // (0..5 = Start/Loop/End/AirStart/AirLoop/AirEnd; 6..8 = ThrowB/ThrowHi/ThrowLw).
  //
  // IMPORTANT: in Slippi, `action_id` is the fighter's motion state enum (fp->state / GALE01 ft*MS_*),
  // while `animation_index` is a separate "subaction/submotion" id used by scripts/poses.

  // Fox/Falco MotionState ids (GALE01), decomp-backed numeric values:
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
  //   (comments: ftFx_MS_SpecialNStart=341 .. ftFx_MS_SpecialAirNEnd=346)
  // - refs/melee/src/melee/ft/chara/ftFalco/ftFc_Init.c::ftFc_Init_MotionStateTable
  //   (Falco uses the same ftFx_* MotionState ids; comments match Fox)
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_GetBlasterAction
  //   (msid = currASID - ftFx_MS_SpecialNStart)
  enum {
    MSL_FX_MS_SPECIALN_START = 0x0155,     // ftFx_MS_SpecialNStart
    MSL_FX_MS_SPECIALN_LOOP = 0x0156,      // ftFx_MS_SpecialNLoop
    MSL_FX_MS_SPECIALN_END = 0x0157,       // ftFx_MS_SpecialNEnd
    MSL_FX_MS_SPECIALAIRN_START = 0x0158,  // ftFx_MS_SpecialAirNStart
    MSL_FX_MS_SPECIALAIRN_LOOP = 0x0159,   // ftFx_MS_SpecialAirNLoop
    MSL_FX_MS_SPECIALAIRN_END = 0x015A,    // ftFx_MS_SpecialAirNEnd
  };
  // Throw MotionState ids (GALE01 common), decomp-backed numeric values:
  // - refs/melee/src/melee/ft/ftmotionstates.c (comments: ftCo_MS_CatchDash=214, ftCo_MS_ThrowB=220, ...)
  // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_GetBlasterAction
  //   (msid = currASID - ftCo_MS_CatchDash for ThrowB/ThrowHi/ThrowLw)

  if (action_id_u16 >= (uint16_t)MSL_FX_MS_SPECIALN_START &&
      action_id_u16 <= (uint16_t)MSL_FX_MS_SPECIALAIRN_END) {
    return (uint8_t)(action_id_u16 - (uint16_t)MSL_FX_MS_SPECIALN_START);
  }

  // Throw motion states that still "require the blaster item" for Fox/Falco.
  // Decomp enum values (refs/melee/src/melee/ft/ftmotionstates.c comments):
  // - ftCo_MS_CatchDash = 214 (base for throw indices)
  // - ftCo_MS_ThrowB    = 220 -> index 6 (ftFx_SpecialNIndex_ThrowB)
  // - ftCo_MS_ThrowHi   = 221 -> index 7 (ftFx_SpecialNIndex_ThrowHi)
  // - ftCo_MS_ThrowLw   = 222 -> index 8 (ftFx_SpecialNIndex_ThrowLw)
  enum { MSL_FTCO_MS_CATCHDASH = 214 };
  enum { MSL_FTCO_MS_THROWB = 220, MSL_FTCO_MS_THROWHI = 221, MSL_FTCO_MS_THROWLW = 222 };
  if (action_id_u16 == (uint16_t)MSL_FTCO_MS_THROWB ||
      action_id_u16 == (uint16_t)MSL_FTCO_MS_THROWHI ||
      action_id_u16 == (uint16_t)MSL_FTCO_MS_THROWLW) {
    return (uint8_t)(action_id_u16 - (uint16_t)MSL_FTCO_MS_CATCHDASH);
  }

  return 9;
}

static inline uint8_t action_is_blaster_throw(uint16_t action_id_u16) {
  return (action_id_u16 == (uint16_t)MSL_ACT_THROW_B ||
          action_id_u16 == (uint16_t)MSL_ACT_THROW_HI ||
          action_id_u16 == (uint16_t)MSL_ACT_THROW_LW)
             ? 1u
             : 0u;
}

static inline float item_throw_pose_facing_dir(const MslBatch* batch, size_t owner_idx,
                                               uint16_t action_id_u16) {
  const float current = batch->state.facing[owner_idx] ? 1.0f : -1.0f;
  if (!action_is_blaster_throw(action_id_u16)) {
    return current;
  }
  // Throw-side laser joints are sampled through lb_8000B1CC from the fighter JObj tree in
  // ftFx_Throw_Anim. Fighter_ChangeMotionState installs root Y rotation from fp->facing_dir and
  // copies that value to fp->facing_dir1; ThrowB's set_throw_flags later flips fp->facing_dir but
  // does not re-enter the motion state or reinstall the root JObj rotation before blaster pulses.
  // Use the motion-entry facing lane for this pose transform, while scalar gameplay facing remains
  // current-facing owned.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,ftPartSetRotY}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //   ftFx_SpecialN_FtGetHoldJoint,ftFx_SpecialN_ItGetHoldJoint,ftFx_Throw_Anim}
  const int8_t motion_entry_facing = batch->state.facing_dir1[owner_idx];
  if (motion_entry_facing > 0) {
    return 1.0f;
  }
  if (motion_entry_facing < 0) {
    return -1.0f;
  }
  return current;
}

static inline uint8_t items_action_is_damage_family(uint16_t action_id_u16) {
  // Generated MSLMSO01 Damage/DamageFly/DamageFall collision classes cover the same damage exit
  // callback family this item stale-owner clear follows.
  // data/motion_state/owners/{fox,falco}.bin (DAMAGE_*_COLL classes)
  return msl_damage_owner_is_damage_collision_landing_action(action_id_u16);
}

static inline uint8_t items_action_is_dead_family(uint16_t action_id_u16) {
  // Common Dead* MotionState ids are contiguous at ftCo_MS_DeadDown..DeadUpFallHitCameraIce.
  // refs/melee/src/melee/ft/ftmotionstates.c
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCommon_MotionState
  return (action_id_u16 <= (uint16_t)10u) ? 1u : 0u;
}

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
}

// refs/melee/src/melee/lb/forward.h::HurtCapsuleState
enum { MSL_HURTCAPS_DISABLED = 1u };

static inline uint8_t laser_grounded_body_uses_sweep(const MslBatch* batch, size_t d_idx,
                                                     float laser_age_frames) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.on_ground[d_idx] == 0u) {
    return 1u;
  }
  if (!(laser_age_frames > 1.0f) || batch->state.shield_radius[d_idx] > 0.0f ||
      batch->state.hurtbox_state[d_idx] != 0u) {
    return 0u;
  }
  // Grounded laser BODY travel:
  // - itFoxlaser_UnkMotion1_Phys snapshots the previous projectile position and it_8029C4D4
  //   dispatches collision over the previous-to-current item segment for fighter contact.
  // - Keep this on the shared item BODY travel owner for vulnerable, non-shielded grounded
  //   defenders instead of defender action/frame slices. Candidate-level lbColl x58/x4C phase
  //   boundaries stay in the exact overlap path below.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  return 1u;
}

static inline uint16_t item_article_laser_shot_kind(uint8_t char_id) {
  const MslItemArticleParams* ap = item_article_params_get(char_id);
  return ap != NULL ? ap->blaster_shot_itkind : 0u;
}

static inline uint16_t item_article_illusion_kind(uint8_t char_id) {
  const MslItemArticleParams* ap = item_article_params_get(char_id);
  return ap != NULL ? ap->side_special_illusion_itkind : 0u;
}

static inline uint16_t item_article_sheik_needle_held_kind(uint8_t char_id) {
  const MslItemArticleParams* ap = item_article_params_get(char_id);
  return ap != NULL ? ap->needle_held_itkind : 0u;
}

static inline uint16_t item_article_sheik_needle_throw_kind(uint8_t char_id) {
  const MslItemArticleParams* ap = item_article_params_get(char_id);
  return ap != NULL ? ap->needle_throw_itkind : 0u;
}

static inline uint8_t item_type_is_fox_laser(uint16_t item_type) {
  return item_type == item_article_laser_shot_kind((uint8_t)MSL_CHAR_ID_FOX) ? 1u : 0u;
}

static inline uint8_t item_type_is_falco_laser(uint16_t item_type) {
  return item_type == item_article_laser_shot_kind((uint8_t)MSL_CHAR_ID_FALCO) ? 1u : 0u;
}

static inline uint8_t item_unreflected_laser_article_matches_owner_kind(const MslBatch* batch,
                                                                        size_t item_idx) {
  if (batch == NULL || batch->state.item_owner[item_idx] < 0 ||
      batch->state.item_owner[item_idx] >= (int8_t)batch->config.num_players) {
    return 0u;
  }
  if (msl_item_reflect_has_transfer_provenance(batch, item_idx) != 0u) {
    return 0u;
  }
  const int bi = (int)(item_idx / (size_t)MSL_MAX_ITEMS);
  const int owner = batch->state.item_owner[item_idx];
  const size_t o_idx = msl_idx_player(bi, owner);
  const uint16_t owner_shot_kind = item_article_laser_shot_kind(batch->state.char_id[o_idx]);
  return (owner_shot_kind != 0u && batch->state.item_type[item_idx] == owner_shot_kind) ? 1u : 0u;
}

static inline uint8_t item_spawn_laser_freezes_stale_damage(
    const MslBatch* batch, int bi, int owner, uint16_t item_type, uint16_t item_attack_id,
    uint16_t item_attack_instance, uint8_t spawn_state, uint8_t is_blaster_throw) {
  if (batch == NULL ||
      (item_type_is_fox_laser(item_type) == 0u && item_type_is_falco_laser(item_type) == 0u) ||
      owner < 0 || owner >= (int)batch->config.num_players) {
    return 0u;
  }
  if (is_blaster_throw != 0u && item_attack_id != (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    // Throw-side blaster script commands (`it_8029C6CC`) create the laser and immediately run
    // it_80272460 from the throw article identity. No later visible state is needed to prove the
    // frozen item HitCapsule.damage lane.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    return 1u;
  }
  if (spawn_state != 0u) {
    return 0u;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  if (item_attack_id != (uint16_t)MSL_FT_MOVE_ID_DEFAULT && item_attack_instance != 0u) {
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const size_t ii = msl_idx_item(bi, it);
      if (batch->state.item_exists[ii] == 0u || batch->state.item_owner[ii] != (int8_t)owner ||
          batch->state.item_type[ii] != item_type ||
          batch->state.item_attack_id[ii] != item_attack_id ||
          batch->state.item_attack_instance[ii] == 0u ||
          batch->state.item_attack_instance[ii] == item_attack_instance) {
        continue;
      }
      if (item_stale_queue_contains_instance(batch, o_idx, item_attack_id,
                                             batch->state.item_attack_instance[ii]) == 0u) {
        // Overlapping same-owner same-type live laser with a different attack instance that is not
        // in the stale queue proves the older shot has not yet reached plStale_UpdateStaleMovesFromItem.
        // The new shot's HitCapsule.damage must therefore stay frozen at its create-time stale
        // multiplier rather than being recomputed after the older shot eventually stales.
        // refs/melee/src/melee/it/itcoll.c::it_80272460
        // refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
        return 1u;
      }
    }
  }
  if (batch->state.last_attack_landed[o_idx] == (uint8_t)item_attack_id &&
      item_attack_id != (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    // Runtime-only source order: the owner already landed this attack id before the current laser
    // create callback, but Slippi does not expose the just-created laser's frozen damage float.
    // Keep this gate tied to the attack identity, not replay row or dataset identity.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/it/itcoll.c::it_80272460
    return 1u;
  }
  if (item_attack_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    return 0u;
  }
  // Last fallback: exactly one current victim carrying same-source damage proves the create-time
  // laser belongs to the ongoing source hit episode. Multiple same-source victims are ambiguous and
  // intentionally do not freeze the stale lane.
  return item_spawn_has_unique_same_source_damage_victim(batch, bi, owner);
}

static inline uint8_t item_type_is_fox_illusion(uint16_t item_type) {
  return item_type == item_article_illusion_kind((uint8_t)MSL_CHAR_ID_FOX) ? 1u : 0u;
}

static void throw_laser_ensure_spawn_counter_for_pulse(MslBatch* batch, int bi, int owner,
                                                       uint16_t gun_itkind, uint16_t action_id_u16,
                                                       uint8_t char_id, uint8_t pulse_frame) {
  if (batch == NULL || pulse_frame == 0u) {
    return;
  }
  uint8_t pulse_ordinal = 0u;
  if (!move_tables_throw_projectile_pulse_ordinal(char_id, action_id_u16, (int16_t)pulse_frame,
                                                  &pulse_ordinal) ||
      pulse_ordinal == 0u) {
    return;
  }
  const int gun_slot = items_find_gun_slot(batch, bi, owner, gun_itkind);
  if (gun_slot < 0) {
    return;
  }
  const size_t gun_ii = msl_idx_item(bi, gun_slot);
  const uint32_t min_next = batch->state.item_spawn_id[gun_ii] + (uint32_t)pulse_ordinal;
  if (batch->state.item_spawn_id_counter[bi] < min_next) {
    // Throw-side blaster shots allocate one item id per set_throw_spawn_projectile command, even
    // when an earlier command's shot is consumed before Slippi can serialize it. The replay seed
    // may therefore carry only the attached gun plus a lagging visible spawn counter. Use the
    // extracted script pulse ordinal relative to the gun spawn id to restore the source item-id
    // counter before emitting the current pulse.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/item.c::Item_80267AA8
    // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowB"/"ftCo_SM_ThrowHi"/"ftCo_SM_ThrowLw"].events
    batch->state.item_spawn_id_counter[bi] = min_next;
  }
}

enum {
  // Throw pulse frames from extracted move scripts:
  // - ThrowHi set_throw_spawn_projectile at 18/20/24 (Fox/Falco).
  // - ThrowB set_throw_spawn_projectile at 15/18/21 (Fox/Falco).
  // - ThrowLw set_throw_spawn_projectile at 23/25/28/31 (Fox/Falco).
  // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"/"ftCo_SM_ThrowB"/"ftCo_SM_ThrowLw"]["events"]
  MSL_THROWHI_PULSE_MID_AF = 20,
  MSL_THROWHI_PREV_PHASE_AF = 18,
  MSL_THROWB_PULSE_START_AF = 15,
  MSL_THROWB_PREV_PHASE_AF = 13,
  MSL_THROWLW_PULSE_ATTACH_AF = 25,
};

static inline int throw_index_from_action_id_for_items(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_THROW_F:
      return 0;
    case (uint16_t)MSL_ACT_THROW_B:
      return 1;
    case (uint16_t)MSL_ACT_THROW_HI:
      return 2;
    case (uint16_t)MSL_ACT_THROW_LW:
      return 3;
    default:
      return -1;
  }
}

static inline int32_t item_throw_anim_rate_fp_from_chars(uint8_t owner_char_id,
                                                         uint8_t victim_char_id,
                                                         uint16_t throw_action) {
  const int throw_index = throw_index_from_action_id_for_items(throw_action);
  if (throw_index < 0) {
    return 0;
  }
  float throw_anim_speed = 1.0f;
  const MslCommonParams* c = msl_common_params();
  const MslCharParams* owner_ch = msl_char_params_fast(owner_char_id);
  const MslCharParams* victim_ch = msl_char_params_fast(victim_char_id);
  const uint8_t weight_independent =
      (owner_ch != NULL)
          ? ((owner_ch->weight_independent_throws_mask & (uint8_t)(1u << throw_index)) ? 1u : 0u)
          : 0u;
  if (!weight_independent && c != NULL && victim_ch != NULL && victim_ch->weight > 0.0f &&
      c->throw_anim_speed_weight_mul > 0.0f) {
    throw_anim_speed = 1.0f / (victim_ch->weight * c->throw_anim_speed_weight_mul);
    if (!(throw_anim_speed > 0.0f)) {
      throw_anim_speed = 1.0f;
    }
  }
  return msl_q16_16_from_f32(throw_anim_speed);
}

static inline uint8_t item_throwlw_frame25_post_hitlag_rate_allowed(uint8_t owner_char_id,
                                                                    uint8_t victim_char_id) {
  // Supported-domain ThrowLw frame-25 post-hitlag rate policy:
  // - The candidate rate is always computed through the decomp throw-entry formula:
  //     ftCo_800DD4B0: anim_speed = 1 / (victim_weight * p_ftCommonData->x37C)
  // - In the current Fox/Falco domain, replay-real controls separate the slower Falco-victim rate
  //   from the faster Fox-victim rate: the slower rate can apply same-frame BODY hitlag after the
  //   post-hitlag frame-25 command crossing, while the faster Fox-victim QGD rows serialize the
  //   article without immediate BODY hitlag.
  // - Keep the allow-set as data-derived rates, not a character-id shortcut: for the supported
  //   Fox/Falco domain, compute the rate set from extracted character/common data and admit the
  //   slowest supported ThrowLw rate. Adding new supported characters should extend this supported
  //   domain table only with matching source/probe controls.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0
  // refs/melee/src/melee/ft/types.h::ftCo_DatAttrs (+0x180)
  // data/common/ft_common_data.json `throw_anim_speed_weight_mul`
  // data/characters/{fox,falco}.json `weight`
  const int32_t candidate_rate_fp =
      item_throw_anim_rate_fp_from_chars(owner_char_id, victim_char_id, (uint16_t)MSL_ACT_THROW_LW);
  if (candidate_rate_fp <= 0) {
    return 0u;
  }

  static const uint8_t k_supported_domain_chars[] = {
      (uint8_t)MSL_CHAR_ID_FOX,
      (uint8_t)MSL_CHAR_ID_FALCO,
  };
  int32_t slowest_supported_rate_fp = 0;
  for (size_t i = 0; i < sizeof(k_supported_domain_chars) / sizeof(k_supported_domain_chars[0]);
       i++) {
    const int32_t supported_rate_fp = item_throw_anim_rate_fp_from_chars(
        owner_char_id, k_supported_domain_chars[i], (uint16_t)MSL_ACT_THROW_LW);
    if (supported_rate_fp <= 0) {
      continue;
    }
    if (slowest_supported_rate_fp == 0 || supported_rate_fp < slowest_supported_rate_fp) {
      slowest_supported_rate_fp = supported_rate_fp;
    }
  }
  return (slowest_supported_rate_fp > 0 && candidate_rate_fp == slowest_supported_rate_fp) ? 1u
                                                                                           : 0u;
}

static inline uint8_t throw_blaster_pulse_is_seed_stale_latch(uint16_t action_id_u16,
                                                              uint16_t shot_itkind,
                                                              int16_t crossed_pulse_af,
                                                              uint16_t prev_frame_i) {
  // One-step reseed does not carry script command cursor/latch internals.
  // Decomp timing ownership:
  // - ftAction_80071974 emits one-shot throw_flags_b0 script pulses.
  // - ftAction_80073354 advances command timeline state.
  // - ftFx_Throw_Anim consumes throw_flags_b0 once to spawn throw-side laser shots.
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  if (action_id_u16 == (uint16_t)MSL_ACT_THROW_HI && item_type_is_falco_laser(shot_itkind) &&
      crossed_pulse_af == (int16_t)MSL_THROWHI_PULSE_MID_AF &&
      prev_frame_i == (uint16_t)MSL_THROWHI_PREV_PHASE_AF) {
    return 1u;
  }
  if (action_id_u16 == (uint16_t)MSL_ACT_THROW_B &&
      crossed_pulse_af == (int16_t)MSL_THROWB_PULSE_START_AF &&
      prev_frame_i == (uint16_t)MSL_THROWB_PREV_PHASE_AF) {
    return 1u;
  }
  return 0u;
}

static inline uint8_t action_is_illusion_dash(uint8_t char_id, uint16_t action_id_u16) {
  // Ghost article spawn is owned by ftFx_SpecialS_Anim / ftFx_SpecialAirS_Anim
  // (main dash states), not Start/End states. Ownership from the extracted MotionState
  // row identity.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id_u16);
  return (uint8_t)(fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_S ||
                   fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S);
}

static inline uint8_t action_is_illusion_end(uint8_t char_id, uint16_t action_id_u16) {
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id_u16);
  return (uint8_t)(fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_S_END ||
                   fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END);
}

static inline uint8_t action_is_sheik_needle_start(uint8_t char_id, uint16_t action_id_u16) {
  if (char_id != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  return (uint8_t)(action_id_u16 == (uint16_t)MSL_ACT_SK_SPECIAL_N_START ||
                   action_id_u16 == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_START);
}

static inline uint8_t action_is_sheik_needle_loop(uint8_t char_id, uint16_t action_id_u16) {
  if (char_id != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  return (uint8_t)(action_id_u16 == (uint16_t)MSL_ACT_SK_SPECIAL_N_LOOP ||
                   action_id_u16 == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_LOOP);
}

static inline uint8_t action_is_sheik_needle_end(uint8_t char_id, uint16_t action_id_u16) {
  if (char_id != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  return (uint8_t)(action_id_u16 == (uint16_t)MSL_ACT_SK_SPECIAL_N_END ||
                   action_id_u16 == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_END);
}

static inline void sheik_needle_set_drop_hidden_lanes(MslBatch* batch, int bi, size_t ii,
                                                      const MslItemArticleParams* ap);

static inline uint8_t action_is_illusion_setphys(uint8_t char_id, uint16_t action_id_u16) {
  // Illusion/Phantasm ghost position is advanced by ftFox_SpecialS_SetPhys, which is called from
  // the grounded/air main and end Phys callbacks.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Phys,ftFx_SpecialAirS_Phys,ftFx_SpecialSEnd_Phys,ftFx_SpecialAirSEnd_Phys,
  //   ftFox_SpecialS_SetPhys
  // }
  return (action_is_illusion_dash(char_id, action_id_u16) ||
          action_is_illusion_end(char_id, action_id_u16))
             ? 1u
             : 0u;
}

static inline uint8_t action_can_own_fighter_anim_article_spawn(uint8_t char_id,
                                                                uint16_t action_id_u16) {
  if (action_is_illusion_dash(char_id, action_id_u16) ||
      action_is_illusion_end(char_id, action_id_u16)) {
    return 1u;
  }
  if (action_is_sheik_needle_loop(char_id, action_id_u16) ||
      action_is_sheik_needle_end(char_id, action_id_u16)) {
    return 1u;
  }
  return (blaster_gun_state_from_action_id(action_id_u16) != 9u) ? 1u : 0u;
}

static inline uint8_t items_row_has_fighter_anim_article_source(const MslBatch* batch, int bi,
                                                                int num_players) {
  if (batch == NULL) {
    return 0u;
  }
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(bi, p);
    const uint16_t action_id_u16 = batch->state.action_id[idx];
    if (action_can_own_fighter_anim_article_spawn(batch->state.char_id[idx], action_id_u16)) {
      return 1u;
    }
    const uint16_t prev_action_id_u16 = batch->state.prev_action_id[idx];
    if (batch->state.action_frame[idx] == 0 &&
        ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], prev_action_id_u16) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_S &&
          msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id_u16) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_S_END) ||
         (msl_motion_state_fx_special_kind(batch->state.char_id[idx], prev_action_id_u16) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S &&
          msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id_u16) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END))) {
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t items_row_has_illusion_setphys_source(const MslBatch* batch, int bi,
                                                            int num_players) {
  if (batch == NULL) {
    return 0u;
  }
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(bi, p);
    if (action_is_illusion_setphys(batch->state.char_id[idx], batch->state.action_id[idx])) {
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t illusion_spawn_pulse_crossed(uint8_t char_id, uint16_t action_id_u16,
                                                   uint16_t msid, float prev_anim_frame_f32,
                                                   float cur_anim_frame_f32) {
  if (!action_is_illusion_dash(char_id, action_id_u16)) {
    return 0u;
  }
  // Cmd-var pulse source is the typed MSLFTSC1 script event cache:
  // data/scripts/{fox,falco}.bin specials_by_msid side_ground.main/side_air.main
  // set_cmd_var(idx=2,value=1).
  return move_tables_special_cmd2_pulse_crossed(char_id, msid, prev_anim_frame_f32,
                                                cur_anim_frame_f32, NULL);
}

static inline uint8_t illusion_prev_main_msid_for_action(uint8_t char_id, uint16_t action_id_u16,
                                                         uint16_t* out_msid) {
  if (out_msid == NULL) {
    return 0u;
  }
  const MslSpecialMsids* ms = msl_special_msids(char_id);
  if (ms == NULL) {
    return 0u;
  }
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id_u16);
  if (fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_S) {
    *out_msid = ms->specials_ground_main;
    return 1u;
  }
  if (fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S) {
    *out_msid = ms->specials_air_main;
    return 1u;
  }
  return 0u;
}

static inline int items_find_illusion_slot(const MslBatch* batch, int bi, int owner,
                                           uint16_t illusion_itkind) {
  if (batch == NULL || owner < 0) {
    return -1;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      continue;
    }
    if (batch->state.item_type[ii] != illusion_itkind) {
      continue;
    }
    if (batch->state.item_owner[ii] != owner) {
      continue;
    }
    return it;
  }
  return -1;
}

static inline float items_cur_anim_frame_f32(const MslBatch* batch, size_t idx);

static inline int items_find_owned_item_slot(const MslBatch* batch, int bi, int owner,
                                             uint16_t item_kind) {
  if (batch == NULL || owner < 0) {
    return -1;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] != 0u && batch->state.item_owner[ii] == (int8_t)owner &&
        batch->state.item_type[ii] == item_kind) {
      return it;
    }
  }
  return -1;
}

static void sheik_needle_held_article_position(const MslBatch* batch, size_t owner_idx,
                                               float* out_x, float* out_y) {
  if (batch == NULL || out_x == NULL || out_y == NULL) {
    return;
  }
  // Held Needle creation initializes the item from the fighter position and then attaches the item
  // GObj to the selected fighter part. Slippi's same-frame item publication serializes that initial
  // item position, not the later visible hand/needle model transform. Grounded held-Needle rows
  // publish the same +1.5 Y item initialization used by the engine's held-item setup; aerial rows
  // publish the raw fighter position.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{ftSk_SpecialNStart_Anim,ftSk_SpecialAirNStart_Anim}
  // refs/melee/src/melee/it/items/itseakneedleheld.c::it_802B19AC
  // refs/melee/src/melee/it/item.c::{Item_80268B18,Item_80267130}
  *out_x = batch->state.pos_x[owner_idx];
  *out_y = batch->state.pos_y[owner_idx] + (batch->state.on_ground[owner_idx] != 0u
                                                ? 1.5f * batch->state.fighter_scale_y[owner_idx]
                                                : 0.0f);
}

uint8_t items_spawn_sheik_held_needle_article(MslBatch* batch, size_t owner_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  const size_t o_idx = owner_idx;
  const uint8_t char_id = batch->state.char_id[o_idx];
  const uint16_t held_kind = item_article_sheik_needle_held_kind(char_id);
  if (held_kind == 0u || batch->state.sheik_needle_count[o_idx] == 0u ||
      items_find_owned_item_slot(batch, bi, owner, held_kind) >= 0) {
    return 0u;
  }

  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);
  // Start_Anim creates the held article immediately before Fighter_ChangeMotionState enters Loop.
  // Loop IASA may then enter End in the same fighter proc, but the article publication remains
  // owned by Start_Anim rather than by the final destination action.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
  //   ftSk_SpecialNStart_Anim,ftSk_SpecialAirNStart_Anim,doIasa}
  // refs/melee/src/melee/it/items/itseakneedleheld.c::it_802B19AC
  batch->state.item_exists[ii] = 1u;
  batch->state.item_type[ii] = held_kind;
  batch->state.item_state[ii] = 0u;
  batch->state.item_owner[ii] = (int8_t)owner;
  batch->state.item_instance_id[ii] = batch->state.instance_id[o_idx];
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_direction[ii] = batch->state.facing[o_idx] ? 1.0f : -1.0f;
  batch->state.item_attack_id[ii] = batch->state.attack_id[o_idx];
  batch->state.item_attack_instance[ii] = batch->state.attack_instance[o_idx];
  batch->state.item_timer[ii] = 1400.0f;
  sheik_needle_held_article_position(batch, o_idx, &batch->state.item_pos_x[ii],
                                     &batch->state.item_pos_y[ii]);
  return 1u;
}

static inline void sheik_needle_freeze_item_hitcapsule_stale_damage(MslBatch* batch, size_t ii,
                                                                    size_t owner_idx) {
  if (batch == NULL) {
    return;
  }
  const uint16_t attack_id = batch->state.item_attack_id[ii];
  if (attack_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT ||
      batch->state.item_attack_instance[ii] == 0u) {
    return;
  }
  // Item scripts create HitCapsules through it_802790C0, which immediately calls it_80272460 to
  // freeze HitCapsule.damage with ft_80089228(owner, item->xD88, item->xD8C, raw_damage). Later
  // BODY contacts consume that frozen item HitCapsule damage; stale updates from earlier Needles in
  // the same volley must not retroactively restale still-flying Needle HitCapsules.
  // refs/melee/src/melee/it/itanimlist.c::it_802790C0
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  batch->state.item_stale_damage_mul[ii] = staling_multiplier_for_move(batch, owner_idx, attack_id);
  batch->state.item_stale_damage_valid[ii] = 1u;
}

static void sheik_needle_spawn_thrown_article_from_fighter(MslBatch* batch, int bi, int owner) {
  if (batch == NULL) {
    return;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  const uint8_t char_id = batch->state.char_id[o_idx];
  const uint16_t action = batch->state.action_id[o_idx];
  if (!action_is_sheik_needle_end(char_id, action) ||
      batch->state.sheik_special_latch[o_idx] == 0u) {
    // ftSk_Special{Air}NEnd_Anim owns the six-frame cadence and only arms shootNeedles
    // (`mv.sk.specialn.x4`) when `fv.sk.x0` was nonzero before decrementing it. The fighter
    // special update writes that source latch; the item phase consumes the latch instead of
    // rechecking the post-decrement count/timer snapshot.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    //   ftSk_SpecialNEnd_Anim,ftSk_SpecialAirNEnd_Anim,shootNeedles}
    return;
  }
  batch->state.sheik_special_latch[o_idx] = 0u;
  const MslItemArticleParams* ap = item_article_params_get(char_id);
  const MslCharParams* chp = msl_char_params_fast(char_id);
  if (ap == NULL || chp == NULL || ap->needle_throw_itkind == 0u ||
      !(ap->needle_launch_speed > 0.0f)) {
    return;
  }
  const uint16_t held_kind = ap->needle_held_itkind;
  const int held_slot = items_find_owned_item_slot(batch, bi, owner, held_kind);
  if (held_slot >= 0) {
    item_slot_clear(batch, msl_idx_item(bi, held_slot));
  }

  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);

  static const float needle_y_pos_scale[9] = {
      -1.0f, -0.75f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f,
  };
  // `shootNeedles` is the accessory4_cb/item-phase owner for mv.sk.specialn.x4, after
  // ftSk_SpecialNEnd_Anim arms the latch in the fighter Anim callback. Replay one-step rows
  // expose this as a destination-frame HSD RNG consumer: install the replay frame-start stream
  // immediately before shootNeedles' y-jitter HSD_Randi(9) site, without changing normal
  // free-running step_input semantics.
  // refs/slippi-ssbm-asm/Recording/SendFrameStart.s
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::shootNeedles
  // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
  combat_rng_use_next_replay_frame_seed_if_unconsumed(batch, bi);
  const int rand_idx =
      combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_SHOOT_YPOS9, 9);
  const uint8_t air = (uint8_t)(batch->state.on_ground[o_idx] == 0u);
  const float facing_dir = batch->state.facing[o_idx] ? 1.0f : -1.0f;
  const float scale_y = batch->state.fighter_scale_y[o_idx];
  const float x_off =
      (air ? chp->sheik_needle_air_spawn_x_offset : chp->sheik_needle_ground_spawn_x_offset) *
      facing_dir;
  const float y_base =
      air ? chp->sheik_needle_air_spawn_y_offset : chp->sheik_needle_ground_spawn_y_offset;
  const float y_jitter =
      needle_y_pos_scale[(rand_idx >= 0 && rand_idx < 9) ? rand_idx : 0] * (air ? 2.0f : 1.0f);
  const float angle = air ? (facing_dir * (3.0f * MSL_PI_F / 4.0f) + MSL_PI_F / 2.0f)
                          : (facing_dir * (MSL_PI_F / 2.0f) + MSL_PI_F / 2.0f);

  batch->state.item_exists[ii] = 1u;
  batch->state.item_type[ii] = ap->needle_throw_itkind;
  batch->state.item_state[ii] = 0u;
  batch->state.item_owner[ii] = (int8_t)owner;
  batch->state.item_instance_id[ii] = batch->state.instance_id[o_idx];
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_direction[ii] = facing_dir;
  batch->state.item_pos_x[ii] = batch->state.pos_x[o_idx] + scale_y * x_off;
  batch->state.item_pos_y[ii] = batch->state.pos_y[o_idx] + scale_y * (y_base + y_jitter);
  batch->state.item_vel_x[ii] = -ap->needle_launch_speed * cosf(angle);
  batch->state.item_vel_y[ii] = ap->needle_launch_speed * sinf(angle);
  batch->state.item_timer[ii] = (float)ap->needle_lifetime_frames;
  batch->state.item_attack_id[ii] = batch->state.attack_id[o_idx];
  batch->state.item_attack_instance[ii] = batch->state.attack_instance[o_idx];
  sheik_needle_freeze_item_hitcapsule_stale_damage(batch, ii, o_idx);
  batch->state.item_hidden_callback_flags[ii] |=
      (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME;
}

void items_sheik_needle_damage_callback(MslBatch* batch, int bi, int owner,
                                        uint16_t pre_damage_action, uint8_t source_on_ground) {
  if (batch == NULL || owner < 0 || owner >= (int)batch->config.num_players) {
    return;
  }
  // Source take-damage/death callback owner. ftSk_Init_80110198 runs ftSk_SpecialN_80111FBC (drop
  // live held Needle stock + always clear fv.sk.x0) and then ftSk_SpecialS_CheckAndDestroyChain.
  // It is installed by setDmgCallbacks during SpecialN states, and by ftSk_SpecialS when a live
  // Chain article exists (fv.sk.x8 != NULL). In any other action it is NULL and ftCommon_8007DB58
  // does not touch fv.sk.x0. The held Needle pointer fv.sk.x4 only exists during SpecialN, so a
  // SpecialS-installed hit clears the stored count without materializing drops. (The Chain-destroy
  // half of ftSk_Init_80110198 is owned by the Side-B Chain packet; this models the Needle half.)
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_Init.c::ftSk_Init_80110198
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{setDmgCallbacks,clearDmgCallbacks}
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c (death2_cb/take_dmg_cb on fv.sk.x8 != NULL)
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DB58
  const uint8_t in_specialn =
      (uint8_t)(pre_damage_action >= (uint16_t)MSL_ACT_SK_SPECIAL_N_START &&
                pre_damage_action <= (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_END);
  const uint8_t in_specials =
      (uint8_t)(pre_damage_action >= (uint16_t)MSL_ACT_SK_SPECIAL_S_START &&
                pre_damage_action <= (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END);
  if (!in_specialn && !in_specials) {
    return;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  if (batch->state.char_id[o_idx] != (uint8_t)MSL_CHAR_ID_SHEIK ||
      batch->state.sheik_needle_count[o_idx] == 0u) {
    return;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  const MslCharParams* chp = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || chp == NULL || ap->needle_throw_itkind == 0u || ap->needle_held_itkind == 0u) {
    batch->state.sheik_needle_count[o_idx] = 0u;
    return;
  }
  if (in_specials) {
    // SpecialS installs ftSk_Init_80110198 only while a live Chain article (fv.sk.x8) is owned.
    if (ap->sheik_chain_itkind == 0u ||
        items_find_owned_item_slot(batch, bi, owner, ap->sheik_chain_itkind) < 0) {
      return;
    }
  }

  const int held_slot = items_find_owned_item_slot(batch, bi, owner, ap->needle_held_itkind);
  if (held_slot < 0) {
    // ftSk_SpecialN_80111FBC always clears fv.sk.x0, but it only materializes dropped Needles
    // while the held Needle pointer fp->fv.sk.x4 is live. Cancel gaps preserve stored count but
    // have already nulled that pointer.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    //   ftSk_SpecialN_80111FBC,ftSk_SpecialNCancel_Anim}
    batch->state.sheik_needle_count[o_idx] = 0u;
    return;
  }
  item_slot_clear(batch, msl_idx_item(bi, held_slot));

  const uint8_t air = (uint8_t)(source_on_ground == 0u);
  const float y_base =
      air ? chp->sheik_needle_air_spawn_y_offset : chp->sheik_needle_ground_spawn_y_offset;
  const float spawn_x = batch->state.pos_x[o_idx];
  const float spawn_y = batch->state.pos_y[o_idx] + batch->state.fighter_scale_y[o_idx] * y_base;
  const float facing_dir = batch->state.facing[o_idx] ? 1.0f : -1.0f;
  uint8_t count = batch->state.sheik_needle_count[o_idx];
  while (count != 0u) {
    const int slot = items_alloc_slot(batch, bi);
    if (slot < 0) {
      break;
    }
    const size_t ii = msl_idx_item(bi, slot);
    item_slot_clear(batch, ii);
    // The take-damage/death callback runs through ftCommon_8007DB58 before the common damage
    // state reset clears callback pointers. It converts every stored Needle into dropped state 1
    // through it_802B00F4, which samples hidden SetupDrop RNG lanes but does not run item
    // Anim/Phys/Coll until the next item callback phase.
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DB58
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialN_80111FBC
    // refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_802AFD8C,it_802B00F4,itSeakNeedleThrown_SetupDrop}
    batch->state.item_exists[ii] = 1u;
    batch->state.item_type[ii] = ap->needle_throw_itkind;
    batch->state.item_state[ii] = 1u;
    batch->state.item_owner[ii] = (int8_t)owner;
    batch->state.item_instance_id[ii] = batch->state.instance_id[o_idx];
    batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
    batch->state.item_direction[ii] = facing_dir;
    batch->state.item_pos_x[ii] = spawn_x;
    batch->state.item_pos_y[ii] = spawn_y;
    batch->state.item_vel_x[ii] = 0.0f;
    batch->state.item_vel_y[ii] = 0.0f;
    batch->state.item_timer[ii] = (float)ap->needle_lifetime_frames;
    batch->state.item_attack_id[ii] = batch->state.attack_id[o_idx];
    batch->state.item_attack_instance[ii] = batch->state.attack_instance[o_idx];
    sheik_needle_set_drop_hidden_lanes(batch, bi, ii, ap);
    count--;
  }
  batch->state.sheik_needle_count[o_idx] = 0u;
  batch->state.sheik_special_latch[o_idx] = 0u;
  items_sort(batch, bi);
}

static uint8_t sheik_article_spawn_anchor_position_z(const MslBatch* batch, size_t owner_idx,
                                                     uint16_t spawn_part_id, float local_z,
                                                     float* out_x, float* out_y, float* out_z);

static uint8_t sheik_article_spawn_anchor_position(const MslBatch* batch, size_t owner_idx,
                                                   uint16_t spawn_part_id, float* out_x,
                                                   float* out_y) {
  float out_z = 0.0f;
  return sheik_article_spawn_anchor_position_z(batch, owner_idx, spawn_part_id, 0.0f, out_x, out_y,
                                               &out_z);
}

static uint8_t sheik_article_spawn_anchor_position_z(const MslBatch* batch, size_t owner_idx,
                                                     uint16_t spawn_part_id, float local_z,
                                                     float* out_x, float* out_y, float* out_z) {
  if (batch == NULL || out_x == NULL || out_y == NULL) {
    return 0u;
  }
  if (spawn_part_id == 0u) {
    return 0u;
  }

  float m[12];
  uint8_t have_matrix = 0u;
  const uint16_t action = batch->state.action_id[owner_idx];
  if (batch->state.char_id[owner_idx] == (uint8_t)MSL_CHAR_ID_SHEIK &&
      (action == (uint16_t)MSL_ACT_SK_SPECIAL_S || action == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S)) {
    const uint32_t anim_u32 = batch->state.animation_index[owner_idx];
    float base_m[12];
    uint8_t have_base = 0u;
    if (anim_u32 <= 0xFFFFu) {
      const float anim_frame_f32 = items_cur_anim_frame_f32(batch, owner_idx);
      if (anim_pose_get_collision_matrix_f32(batch, owner_idx, (uint16_t)anim_u32, anim_frame_f32,
                                             spawn_part_id, base_m) == 0) {
        have_base = 1u;
      }
    }
    const uint16_t pose_msid = msl_motion_state_submotion_id((uint8_t)MSL_CHAR_ID_SHEIK, action);
    // Active Chain does not sample the ordinary action `cur_anim_frame` pose. ftSk_SpecialS_Anim
    // calls ftSk_SpecialS_80110610, which first updates mv.sk.specials.x18/x14 via
    // ftSk_SpecialS_80110490, then evaluates submotion 305/308 at frame `4 + 0.0556 * x18` before
    // blending it by x14 and letting the same-frame item accessory callback sample L3rdNa with
    // lb_8000B1CC. The full item[2] joint overlay is not separately extracted; blend the sampled
    // target matrix against the ordinary action pose with the same x14 owner so neutral/low-magnitude
    // Chain frames stay on the source's near-hand target instead of jumping to the directed pose.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    //   ftSk_SpecialS_Anim,ftSk_SpecialAirS_Anim,ftSk_SpecialS_80110490,ftSk_SpecialS_80110610}
    // refs/melee/src/melee/it/items/itseakchain.c::{fn_802BB44C,fn_802BB694}
    const float pose_frame = 4.0f + 0.0556f * batch->state.sheik_chain_pose_angle[owner_idx];
    float pose_m[12];
    if (anim_pose_get_collision_matrix_f32(batch, owner_idx, pose_msid, pose_frame, spawn_part_id,
                                           pose_m) == 0) {
      float w = batch->state.sheik_chain_pose_mag[owner_idx];
      if (w < 0.0f) {
        w = 0.0f;
      } else if (w > 1.0f) {
        w = 1.0f;
      }
      if (have_base != 0u && w < 1.0f) {
        const float inv = 1.0f - w;
        for (int i = 0; i < 12; i++) {
          m[i] = base_m[i] * inv + pose_m[i] * w;
        }
      } else {
        memcpy(m, pose_m, sizeof(m));
      }
      have_matrix = 1u;
    }
  }
  if (have_matrix == 0u) {
    const uint32_t anim_u32 = batch->state.animation_index[owner_idx];
    if (anim_u32 > 0xFFFFu) {
      return 0u;
    }
    const uint16_t msid = (uint16_t)anim_u32;
    const float anim_frame_f32 = items_cur_anim_frame_f32(batch, owner_idx);
    if (anim_pose_get_collision_matrix_f32(batch, owner_idx, msid, anim_frame_f32, spawn_part_id,
                                           m) != 0) {
      return 0u;
    }
  }

  float lx = 0.0f, ly = 0.0f, lz = 0.0f;
  const float local[3] = {0.0f, 0.0f, local_z};
  msl_mtx34_mul_point(m, local, &lx, &ly, &lz);
  (void)lx;

  const MslCharParams* chp = msl_char_params_fast(batch->state.char_id[owner_idx]);
  float model_scaling = 1.0f;
  if (chp != NULL && chp->model_scaling > 0.0f) {
    model_scaling = chp->model_scaling;
  }
  const float model_scale = batch->state.fighter_scale_y[owner_idx] * model_scaling;

  // Fighter root Y rotation maps source joint Z into stage X. This mirrors the existing
  // lb_8000B1CC-backed blaster spawn path, but these Sheik articles use zero local offsets and keep
  // only the sampled source joint translation.
  // refs/melee/src/melee/ft/fighter.c (ftPartSetRotY(fp, 0, M_PI_2 * fp->facing_dir))
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_CheckInitChain
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::ftSk_SpecialHi_80112F48
  const float facing_dir = batch->state.facing[owner_idx] ? 1.0f : -1.0f;
  *out_x = batch->state.pos_x[owner_idx] + facing_dir * lz * model_scale;
  *out_y = batch->state.pos_y[owner_idx] + ly * model_scale;
  if (out_z != NULL) {
    *out_z = batch->state.pos_z[owner_idx] - facing_dir * lx * model_scale;
  }
  return 1u;
}

uint8_t items_spawn_sheik_chain_article(MslBatch* batch, size_t owner_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_chain_itkind == 0u || ap->sheik_chain_lifetime_frames == 0u) {
    return 0u;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  if (batch->state.char_id[owner_idx] != (uint8_t)MSL_CHAR_ID_SHEIK ||
      items_find_owned_item_slot(batch, bi, owner, ap->sheik_chain_itkind) >= 0) {
    return 0u;
  }

  float spawn_x = 0.0f;
  float spawn_y = 0.0f;
  if (sheik_article_spawn_anchor_position(batch, owner_idx, ap->sheik_chain_spawn_part_id, &spawn_x,
                                          &spawn_y) == 0u) {
    return 0u;
  }

  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);

  // ftSk_SpecialS_CheckInitChain samples L3rdNa with lb_8000B1CC and spawns It_Kind_Seak_Chain
  // from that point when mv.sk.specials.x0 reaches ftSeakAttributes::x1C. The full segment chain
  // remains model work; this slice owns the source article identity, lifetime, and spawn anchor
  // publication used by replay item lanes.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_CheckInitChain
  // refs/melee/src/melee/it/items/itseakchain.c::itSeakChain_Spawn
  batch->state.item_exists[ii] = 1u;
  batch->state.item_type[ii] = ap->sheik_chain_itkind;
  batch->state.item_state[ii] = 0u;
  batch->state.item_owner[ii] = (int8_t)owner;
  batch->state.item_instance_id[ii] = batch->state.instance_id[owner_idx];
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_direction[ii] = batch->state.facing[owner_idx] ? 1.0f : -1.0f;
  batch->state.item_pos_x[ii] = spawn_x;
  batch->state.item_pos_y[ii] = spawn_y;
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  batch->state.item_timer[ii] = (float)ap->sheik_chain_lifetime_frames;
  batch->state.item_hidden_callback_flags[ii] |=
      (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME;
  return 1u;
}

uint8_t items_set_sheik_chain_article_state(MslBatch* batch, size_t owner_idx, uint8_t state) {
  if (batch == NULL || batch->state.char_id[owner_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_chain_itkind == 0u) {
    return 0u;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  const int slot = items_find_owned_item_slot(batch, bi, owner, ap->sheik_chain_itkind);
  if (slot < 0) {
    return 0u;
  }
  // Chain item state changes are fighter-callback owned. The lite runtime does not yet model the
  // full segment chain, but it does carry mv.sk.specials.x0/x4 and therefore can publish the
  // source article state transitions:
  // - CheckInitChain calls it_802BCFC4 after spawn when x0 == x1C + 1, entering state 1.
  // - Chain segment accessory movement calls it_802BCED4 when extension reaches the terminal link,
  //   entering state 3.
  // - End_Anim calls it_802BCF84 at x24, entering retract state 4.
  // - it_802BC94C / it_2725_Logic54_PickedUp returns the fully retracted article to state 0
  //   shortly before End_Anim's x28 destroy call.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
  //   ftSk_SpecialS_CheckInitChain,ftSk_SpecialSEnd_Anim,ftSk_SpecialAirSEnd_Anim}
  // refs/melee/src/melee/it/items/itseakchain.c::{
  //   it_802BCFC4,it_802BCED4,it_802BCF84,it_2725_Logic54_PickedUp}
  batch->state.item_state[msl_idx_item(bi, slot)] = state;
  return 1u;
}

uint8_t items_destroy_sheik_chain_article(MslBatch* batch, size_t owner_idx) {
  if (batch == NULL || batch->state.char_id[owner_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_chain_itkind == 0u) {
    return 0u;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  const int slot = items_find_owned_item_slot(batch, bi, owner, ap->sheik_chain_itkind);
  if (slot < 0) {
    return 0u;
  }
  // ftSk_SpecialS{Air}End_Anim calls it_802BB20C when mv.sk.specials.x0 reaches
  // ftSeakAttributes::x28.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
  //   ftSk_SpecialSEnd_Anim,ftSk_SpecialAirSEnd_Anim}
  // refs/melee/src/melee/it/items/itseakchain.c::it_802BB20C
  item_slot_clear(batch, msl_idx_item(bi, slot));
  return 1u;
}

static inline uint8_t sheik_chain_owner_still_in_specials(const MslBatch* batch, size_t owner_idx) {
  if (batch == NULL || owner_idx >= (size_t)batch->batch_size * (size_t)MSL_MAX_PLAYERS ||
      batch->state.char_id[owner_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[owner_idx];
  return (uint8_t)(action == (uint16_t)MSL_ACT_SK_SPECIAL_S_START ||
                   action == (uint16_t)MSL_ACT_SK_SPECIAL_S ||
                   action == (uint16_t)MSL_ACT_SK_SPECIAL_S_END ||
                   action == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_START ||
                   action == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S ||
                   action == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END);
}

// Constrain link i to within one segment length of its more-headward neighbour i-1 (it_802A3C98 dir
// + clamp). Link index 0 is the head (nearest the hand); increasing index runs toward the tail, which
// is the article's ->prev direction in itseakchain.c.
static inline void sheik_chain_constrain_to(float* px, float* py, float* pz, int i, float ax,
                                            float ay, float az, float seg) {
  const float dx = px[i] - ax;
  const float dy = py[i] - ay;
  const float dz = pz[i] - az;
  const float d = sqrtf(dx * dx + dy * dy + dz * dz);
  if (d > seg && d > 0.0f) {
    const float inv = seg / d;
    px[i] = ax + dx * inv;
    py[i] = ay + dy * inv;
    pz[i] = az + dz * inv;
  }
}

static inline float sheik_chain_dist_dir(float ax, float ay, float az, float bx, float by, float bz,
                                         float* out_x, float* out_y, float* out_z) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float d = sqrtf(dx * dx + dy * dy + dz * dz);
  if (d > 0.0f) {
    const float inv = 1.0f / d;
    *out_x = dx * inv;
    *out_y = dy * inv;
    *out_z = dz * inv;
  } else {
    *out_x = 0.0f;
    *out_y = 0.0f;
    *out_z = 0.0f;
  }
  return d;
}

static inline float sheik_chain_cross2(float ax, float ay, float bx, float by) {
  return ax * by - ay * bx;
}

static uint8_t sheik_chain_sweep_floor_link(const MslBatch* batch, int bi, float prev_x,
                                            float prev_y, float* io_x, float* io_y) {
  if (batch == NULL || io_x == NULL || io_y == NULL || bi < 0) {
    return 0u;
  }
  const MslStageFloorGraph* graph = stage_collision_get_floor_graph(batch->state.stage_id[bi]);
  if (graph == NULL || graph->lines == NULL || graph->line_count == 0u) {
    return 0u;
  }
  const float cur_x = *io_x;
  const float cur_y = *io_y;
  const float rx = cur_x - prev_x;
  const float ry = cur_y - prev_y;
  if (rx == 0.0f && ry == 0.0f) {
    return 0u;
  }

  uint8_t hit = 0u;
  float best_t = 2.0f;
  float best_x = cur_x;
  float best_y = cur_y;
  for (size_t li = 0; li < graph->line_count; li++) {
    MslStageFloorLine line;
    if (!stage_collision_floor_line_world(batch, bi, &graph->lines[li], &line)) {
      continue;
    }
    if (line.fighter_solid == 0u) {
      continue;
    }
    const float sx = line.x1 - line.x0;
    const float sy = line.y1 - line.y0;
    const float denom = sheik_chain_cross2(rx, ry, sx, sy);
    if (denom == 0.0f) {
      continue;
    }
    // Match the floor half of it_802BB938/mpColl for Chain links closely enough for the source
    // active-frontier geometry: horizontal floors admit downward crossings, while sloped floors use
    // the general segment intersection path. The response below mirrors it_802BC080's
    // env_flags&0x18000 bounce/friction branch.
    // refs/melee/src/melee/it/items/itseakchain.c::{it_802BB938,it_802BBAEC,it_802BC080}
    // refs/melee/src/melee/mp/mplib.c::{mpCheckAllRemap,mpCheckFloorRemap}
    if (fabsf(sy) <= 0.0001f && prev_y < cur_y) {
      continue;
    }
    const float qpx = line.x0 - prev_x;
    const float qpy = line.y0 - prev_y;
    const float t = sheik_chain_cross2(qpx, qpy, sx, sy) / denom;
    const float u = sheik_chain_cross2(qpx, qpy, rx, ry) / denom;
    if (t < 0.0f || t > 1.0f || u < 0.0f || u > 1.0f || t >= best_t) {
      continue;
    }
    best_t = t;
    best_x = prev_x + rx * t;
    best_y = prev_y + ry * t + 0.0001f;
    hit = 1u;
  }
  if (hit != 0u) {
    *io_x = best_x;
    *io_y = best_y;
  }
  return hit;
}

static inline int sheik_chain_first_active_from_hand(const uint8_t* active, int n) {
  for (int i = 0; i < n; i++) {
    if (active[i] != 0u) {
      return i;
    }
  }
  return -1;
}

static void sheik_chain_publish_hitbox_map(MslBatch* batch, size_t ii,
                                           const MslItemArticleParams* ap, const uint8_t* active,
                                           int n) {
  uint8_t* hb_link = &batch->state.item_sheik_chain_hitbox_link_idx[ii * (size_t)MSL_MAX_HITBOXES];
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    hb_link[hb] = 0xFFu;
  }
  const int start = sheik_chain_first_active_from_hand(active, n);
  if (start < 0) {
    return;
  }
  int stride = (int)ap->sheik_chain_link_count / 3;
  if (stride < 1) {
    stride = 1;
  }
  int count = 0;
  for (int link = start; link < n; link++) {
    if (active[link] == 0u) {
      continue;
    }
    if ((count % stride) == 0) {
      const int min_hb = count / stride;
      for (int hb = 3; hb > min_hb; hb--) {
        hb_link[hb - 1] = (uint8_t)link;
      }
    }
    if (link == n - 1) {
      hb_link[3] = (uint8_t)link;
    }
    count++;
  }
}

static void sheik_chain_disable_hitcaps(MslBatch* batch, size_t ii, size_t owner_idx) {
  if (batch == NULL) {
    return;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  if (bi < 0 || bi >= batch->batch_size || owner < 0 || owner >= (int)batch->config.num_players) {
    return;
  }
  batch->state.item_sheik_chain_hitcaps_active[ii] = 0u;
  // ftSeakSpecialS_LoopChainHitCollisions disables each Chain HitCapsule with lbColl_80008428 after
  // first clearing its victims via lbColl_80008440. A same-callback movement pulse can then run
  // ftSeakSpecialS_LoopChainHitActivate (lbColl_80008434), so the clear belongs to the cooldown
  // expiry/disable owner, not to generic combat enable-edge handling.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
  //   ftSk_SpecialS_80110BCC,ftSeakSpecialS_LoopChainHitCollisions,
  //   ftSeakSpecialS_LoopChainHitActivate}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008428,lbColl_80008434,lbColl_80008440}
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    const size_t hl_i = idx_hitbox(bi, owner, hb);
    hitlist_capsule_clear(&batch->state.fighter_hitlist[hl_i]);
    batch->state.fighter_hitlist_init_gen[hl_i] = batch->state.hitlist_reseed_gen[bi];
  }
}

static void sheik_chain_update_hitcap_gate(MslBatch* batch, size_t ii,
                                           const MslItemArticleParams* ap, const MslCharParams* chp,
                                           size_t owner_idx, int n) {
  if (batch == NULL || ap == NULL || chp == NULL || n <= 0) {
    return;
  }
  const uint16_t owner_action = batch->state.action_id[owner_idx];
  if (owner_action != (uint16_t)MSL_ACT_SK_SPECIAL_S &&
      owner_action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S &&
      owner_action != (uint16_t)MSL_ACT_SK_SPECIAL_S_END &&
      owner_action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END) {
    return;
  }
  const size_t link_base = ii * (size_t)MSL_SHEIK_CHAIN_MAX_LINKS;
  const size_t hb_base = ii * (size_t)MSL_MAX_HITBOXES;
  float cur_x[MSL_MAX_HITBOXES] = {0.0f};
  float cur_y[MSL_MAX_HITBOXES] = {0.0f};
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    const uint8_t li = batch->state.item_sheik_chain_hitbox_link_idx[hb_base + (size_t)hb];
    if (li != 0xFFu && li < (uint8_t)n) {
      cur_x[hb] = batch->state.item_sheik_chain_link_pos_x[link_base + (size_t)li];
      cur_y[hb] = batch->state.item_sheik_chain_link_pos_y[link_base + (size_t)li];
    }
  }

  uint8_t moved = 0u;
  if (batch->state.item_sheik_chain_hit_prev_valid[ii] != 0u) {
    const float threshold_sq = ap->sheik_chain_attr_x4c * ap->sheik_chain_attr_x4c;
    for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
      const float dx = cur_x[hb] - batch->state.item_sheik_chain_hit_prev_x[hb_base + (size_t)hb];
      const float dy = cur_y[hb] - batch->state.item_sheik_chain_hit_prev_y[hb_base + (size_t)hb];
      if (dx * dx + dy * dy > threshold_sq) {
        moved = 1u;
      }
      batch->state.item_sheik_chain_hit_prev_x[hb_base + (size_t)hb] = cur_x[hb];
      batch->state.item_sheik_chain_hit_prev_y[hb_base + (size_t)hb] = cur_y[hb];
    }
  } else {
    for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
      batch->state.item_sheik_chain_hit_prev_x[hb_base + (size_t)hb] = cur_x[hb];
      batch->state.item_sheik_chain_hit_prev_y[hb_base + (size_t)hb] = cur_y[hb];
    }
    batch->state.item_sheik_chain_hit_prev_valid[ii] = 1u;
  }

  // ftSk_SpecialS_80110BCC compares current Chain hitcap positions (fv.sk.xC) against the previous
  // positions (fv.sk.x3C), decrements mv.sk.specials.x1C, clears/disables hitcaps when the cooldown
  // reaches zero, and re-enables them when any hitcap moved more than article attr x4C.
  // The x20 grace path is only set by post-hitlag ChainSomething; this compact model keeps the
  // ordinary no-hit path at zero, matching the demo Chain contact window.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
  //   ftSk_SpecialS_80110BCC,ftSeakSpecialS_LoopChainHitCollisions,
  //   ftSeakSpecialS_LoopChainHitActivate}
  if (batch->state.item_sheik_chain_hit_cooldown[ii] > 0u) {
    batch->state.item_sheik_chain_hit_cooldown[ii]--;
    if (batch->state.item_sheik_chain_hit_cooldown[ii] == 0u) {
      sheik_chain_disable_hitcaps(batch, ii, owner_idx);
    }
  }
  if (moved != 0u && batch->state.item_sheik_chain_hit_cooldown[ii] == 0u) {
    int cd = (int)chp->sheik_chain_extension_frames;
    if (cd < 1) {
      cd = 1;
    }
    if (cd > 255) {
      cd = 255;
    }
    batch->state.item_sheik_chain_hit_cooldown[ii] = (uint8_t)cd;
    batch->state.item_sheik_chain_hitcaps_active[ii] = 1u;
    // ftSeakSpecialS_LoopChainHitActivate enables the four Chain HitCapsules and immediately calls
    // ftSk_SpecialS_ZeroHitboxPositions, clearing both x914[].x4C and x914[].x58. The same-frame
    // article callback may republish nonzero it_802BCB88 positions through ftSk_SpecialS_UpdateHitboxes,
    // but because x58 was zero it seeds x58 = x4C for that activation frame rather than sweeping
    // from the previous Chain point.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    //   ftSeakSpecialS_LoopChainHitActivate,ftSk_SpecialS_ZeroHitboxPositions,
    //   ftSk_SpecialS_UpdateHitboxes}
    // refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
    batch->state.item_sheik_chain_hit_reset_prev[ii] = 1u;
  } else if (moved == 0u && batch->state.item_sheik_chain_hit_cooldown[ii] == 0u) {
    sheik_chain_disable_hitcaps(batch, ii, owner_idx);
  }
}

static uint8_t sheik_chain_extend_frontier(MslBatch* batch, size_t ii,
                                           const MslItemArticleParams* ap, const MslCharParams* chp,
                                           size_t owner_idx, int n, float hand_x, float hand_y,
                                           float hand_z, uint8_t gravity_variant) {
  (void)chp;
  (void)owner_idx;
  const float seg = ap->sheik_chain_segment_length;
  const size_t base = ii * (size_t)MSL_SHEIK_CHAIN_MAX_LINKS;
  float* px = &batch->state.item_sheik_chain_link_pos_x[base];
  float* py = &batch->state.item_sheik_chain_link_pos_y[base];
  float* pz = &batch->state.item_sheik_chain_link_pos_z[base];
  float* vx = &batch->state.item_sheik_chain_link_vel_x[base];
  float* vy = &batch->state.item_sheik_chain_link_vel_y[base];
  float* vz = &batch->state.item_sheik_chain_link_vel_z[base];
  uint8_t* active = &batch->state.item_sheik_chain_link_active[base];

  const int tail = n - 1;
  if (active[tail] == 0u) {
    active[tail] = 1u;
    px[tail] = hand_x;
    py[tail] = hand_y;
    pz[tail] = hand_z;
  }

  // it_802BBD64 starts at source x0 (the free-end link). it_802BBED0 is the gravity/bounce variant
  // used after the wall-bounce transition; per-link mpColl (`it_802BB938`) is intentionally omitted
  // here because rec312 is owned by active-span publication/contact, not Chain-link environment hits.
  // refs/melee/src/melee/it/items/itseakchain.c::{it_802BBD64,it_802BBED0,it_802A4420,it_802A43B8}
  if (gravity_variant != 0u) {
    vy[tail] -= ap->sheik_chain_gravity;
  }
  px[tail] += vx[tail];
  py[tail] += vy[tail];
  pz[tail] += vz[tail];

  int cur = tail;
  float vel_scale = 1.0f;
  for (int prev = tail - 1; prev >= 0; prev--) {
    if (active[prev] != 0u) {
      if (gravity_variant != 0u) {
        vy[prev] = -((ap->sheik_chain_gravity * vel_scale) - vy[prev]);
        vel_scale *= ap->sheik_chain_decay_x34;
        px[prev] += vx[prev];
        py[prev] += vy[prev];
        pz[prev] += vz[prev];
      }
      sheik_chain_constrain_to(px, py, pz, prev, px[cur], py[cur], pz[cur], seg);
    } else {
      float dx = 0.0f;
      float dy = 0.0f;
      float dz = 0.0f;
      if (sheik_chain_dist_dir(hand_x, hand_y, hand_z, px[cur], py[cur], pz[cur], &dx, &dy, &dz) >
          seg) {
        px[prev] = px[cur] + dx * seg;
        py[prev] = py[cur] + dy * seg;
        pz[prev] = pz[cur] + dz * seg;
        vx[prev] = 0.0f;
        vy[prev] = 0.0f;
        vz[prev] = 0.0f;
        active[prev] = 1u;
      } else {
        sheik_chain_publish_hitbox_map(batch, ii, ap, active, n);
        return 0u;
      }
    }
    cur = prev;
  }

  // Terminal extension settle: after `it_802BBD64` reaches the hand-side link, source calls
  // `it_802BBB0C` before `it_802BCED4`. That anchors the terminal link one segment from the hand,
  // then applies gravity/integration to each tail-side link before constraining it to the previous
  // link. Keep this source-owned transition pass separate from the held `it_802BC080` stick solve.
  // refs/melee/src/melee/it/items/itseakchain.c::{it_802BBD64,it_802BBB0C,it_802BCED4}
  sheik_chain_constrain_to(px, py, pz, 0, hand_x, hand_y, hand_z, seg);
  for (int i = 1; i < n; i++) {
    vy[i] -= ap->sheik_chain_gravity;
    px[i] += vx[i];
    py[i] += vy[i];
    pz[i] += vz[i];
    sheik_chain_constrain_to(px, py, pz, i, px[i - 1], py[i - 1], pz[i - 1], seg);
  }
  // Source transitions the Chain article into held state from the extension accessory itself when
  // the active frontier reaches the hand-side terminal link, not from the fighter's Start timer.
  // refs/melee/src/melee/it/items/itseakchain.c::{fn_802BB44C,it_802BBD64,it_802BCED4}
  if (batch->state.item_state[ii] == 1u || batch->state.item_state[ii] == 2u) {
    batch->state.item_state[ii] = 3u;
  }
  sheik_chain_publish_hitbox_map(batch, ii, ap, active, n);
  return 2u;
}

// 3D Chain solver for source active-frontier geometry. Extension states port the x2C_b0 active bit
// walk from it_802BBD64/it_802BBED0; held state ports the active-span core of it_802BC080. Index 0 is
// the hand-side source x4 link and index n-1 is source x0, the free-end link initially activated by
// it_802BCFC4. `it_802BCB88` publication is represented by item_sheik_chain_hitbox_link_idx.
// Fixed-capacity, no heap, deterministic. Link state is runtime-only SoA (reset on reseed).
// refs/melee/src/melee/it/items/itseakchain.c::{
//   it_802BCFC4,it_802BBD64,it_802BBED0,it_802BC080,it_802BCB88,it_802A4420}
static void sheik_chain_solve_links(MslBatch* batch, size_t ii, const MslItemArticleParams* ap,
                                    size_t owner_idx, float hand_x, float hand_y, float hand_z,
                                    uint8_t item_state) {
  int n = (int)ap->sheik_chain_link_count;
  if (n < 2) {
    return;
  }
  if (n > MSL_SHEIK_CHAIN_MAX_LINKS) {
    n = MSL_SHEIK_CHAIN_MAX_LINKS;
  }
  batch->state.item_sheik_chain_target_valid[ii] = 1u;
  batch->state.item_sheik_chain_target_x[ii] = hand_x;
  batch->state.item_sheik_chain_target_y[ii] = hand_y;
  batch->state.item_sheik_chain_target_z[ii] = hand_z;
  const float seg = ap->sheik_chain_segment_length;
  const float x10 = ap->sheik_chain_friction_x10;
  const float x14 = ap->sheik_chain_friction_x14;
  const float x18 = ap->sheik_chain_gravity;
  const float x1c = ap->sheik_chain_attr_x1c;
  const float x20 = ap->sheik_chain_attr_x20;
  const float x24 = ap->sheik_chain_attr_x24;
  const float x28 = ap->sheik_chain_attr_x28;
  const float x2c = ap->sheik_chain_attr_x2c;
  const float x30 = ap->sheik_chain_attr_x30;
  const float x34 = ap->sheik_chain_decay_x34;
  const float x38 = ap->sheik_chain_attr_x38;
  const float x3c = ap->sheik_chain_attr_x3c;
  const float x40 = ap->sheik_chain_attr_x40;
  const float x44 = ap->sheik_chain_attr_x44;
  const float x48 = ap->sheik_chain_attr_x48;
  const float x5c = ap->sheik_chain_attr_x5c;
  const float x60 = ap->sheik_chain_attr_x60;
  const size_t base = ii * (size_t)MSL_SHEIK_CHAIN_MAX_LINKS;
  float* px = &batch->state.item_sheik_chain_link_pos_x[base];
  float* py = &batch->state.item_sheik_chain_link_pos_y[base];
  float* pz = &batch->state.item_sheik_chain_link_pos_z[base];
  float* vx = &batch->state.item_sheik_chain_link_vel_x[base];
  float* vy = &batch->state.item_sheik_chain_link_vel_y[base];
  float* vz = &batch->state.item_sheik_chain_link_vel_z[base];
  uint8_t* active = &batch->state.item_sheik_chain_link_active[base];
  const size_t hbase = ii * (size_t)MSL_SHEIK_CHAIN_HISTORY_LEN;
  float* hx = &batch->state.item_sheik_chain_history_x[hbase];
  float* hy = &batch->state.item_sheik_chain_history_y[hbase];
  const MslCharParams* chp = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);

  const int first_frame = (batch->state.item_sheik_chain_links_valid[ii] == 0u);
  if (first_frame) {
    // it_802BAF2C zero-inits every ItemLink; it_802BCFC4 then activates only source x0, the free-end
    // link, and seeds its vel.x from attrs->x50 signed by item facing. Do not pre-deploy the full
    // tail: inactive links should be introduced by it_802BBD64/it_802BBED0.
    // Replay one-step seeds can start after the extension frontier has already reached the terminal
    // Start boundary but before this runtime-only x2C_b0 span exists. Reconstruct that hidden source
    // state from mv.sk.specials.x0 (sheik_special_timer) so the next it_802BBD64 step can perform the
    // same it_802BCED4 transition to held state without row-local branches.
    // refs/melee/src/melee/it/items/itseakchain.c::{
    //   it_802BAF2C,it_802BCFC4,it_802BBD64,it_802BCB88}
    const uint8_t extension_reseed_terminal =
        ((item_state == 1u || item_state == 2u) && chp != NULL &&
         (float)batch->state.sheik_special_timer[owner_idx] >= chp->sheik_chain_start_end_frame)
            ? 1u
            : 0u;
    const uint8_t seed_fully_active =
        (item_state == 3u || item_state == 4u || extension_reseed_terminal != 0u) ? 1u : 0u;
    for (int i = 0; i < MSL_SHEIK_CHAIN_MAX_LINKS; i++) {
      active[i] = (seed_fully_active != 0u && i < n) ? 1u : 0u;
      px[i] = hand_x;
      py[i] = (seed_fully_active != 0u && i < n) ? (hand_y - seg * (float)i) : hand_y;
      pz[i] = hand_z;
      vx[i] = 0.0f;
      vy[i] = 0.0f;
      vz[i] = 0.0f;
    }
    const int tail = n - 1;
    active[tail] = 1u;
    if (seed_fully_active == 0u) {
      vx[tail] = ap->sheik_chain_initial_vel_x50 *
                 ((batch->state.item_direction[ii] >= 0.0f) ? 1.0f : -1.0f);
      if (chp != NULL && chp->sheik_chain_extension_frames > 0.0f) {
        int cd = (int)chp->sheik_chain_extension_frames;
        if (cd > 255) {
          cd = 255;
        }
        batch->state.item_sheik_chain_hit_cooldown[ii] = (uint8_t)cd;
      }
    }
    batch->state.item_sheik_chain_links_valid[ii] = 1u;
  }

  // ftSk_SpecialS_80110BCC is a fighter Anim callback and therefore runs before the Chain article
  // accessory callback (`fn_802BB44C`/`fn_802BB694`) publishes this frame's `it_802BCB88` positions.
  // Compare/activate using last frame's published hitcap map here; the publication below becomes
  // the input to the next Anim callback. This avoids same-frame current-position activation.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_80110BCC
  // refs/melee/src/melee/it/items/itseakchain.c::{fn_802BB44C,fn_802BB694,it_802BCB88}
  sheik_chain_update_hitcap_gate(batch, ii, ap, chp, owner_idx, n);

  if (item_state == 1u || item_state == 2u) {
    (void)sheik_chain_extend_frontier(batch, ii, ap, chp, owner_idx, n, hand_x, hand_y, hand_z,
                                      (item_state == 2u) ? 1u : 0u);
    return;
  }

  // Owner lstick delta (fp->fv.sk.lstick_delta = input.lstick - input.lstick1) is written by
  // ftSk_SpecialS_80110788 during the active Chain IASA callback before the article/link update
  // publishes hitbox positions. `fp->input.lstick` is the fighter-visible left stick after the
  // common p_ftCommonData deadzone, while it_802BC080 later consumes that delta into the Chain
  // history trail. refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_80110788
  // refs/melee/src/melee/it/items/itseakchain.c::it_802BC080
  // data/common/ft_common_data.json::{lstick_deadzone_x,lstick_deadzone_y}
  const MslCommonParams* common = msl_common_params();
  const float dz_x = (common != NULL) ? common->lstick_deadzone_x : 0.0f;
  const float dz_y = (common != NULL) ? common->lstick_deadzone_y : 0.0f;
  float lstick_x = apply_deadzone(stick_i8_to_unit(batch->state.input_main_x[owner_idx]), dz_x);
  float lstick_y = apply_deadzone(stick_i8_to_unit(batch->state.input_main_y[owner_idx]), dz_y);
  float prev_lstick_x =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_x[owner_idx]), dz_x);
  float prev_lstick_y =
      apply_deadzone(stick_i8_to_unit(batch->state.prev_input_main_y[owner_idx]), dz_y);
  float dl_x = lstick_x - prev_lstick_x;
  float dl_y = lstick_y - prev_lstick_y;
  if (fabsf(lstick_x) < x48 && fabsf(lstick_y) < x48) {
    dl_x *= 0.5f;
    dl_y *= 0.5f;
  }
  const float facing = (batch->state.item_direction[ii] >= 0.0f) ? 1.0f : -1.0f;

  // Shift the stick-history trail (history[last_idx-i] = history[last_idx-1-i]) and seed entry 0 from
  // the lstick delta with facing/sign-dependent gains, gated by the deadzone x48.
  const int last_idx = (int)(0.5f * (float)ap->sheik_chain_link_count - 1.0f);
  for (int i = 0; i < last_idx && (last_idx - i) < MSL_SHEIK_CHAIN_HISTORY_LEN; i++) {
    hx[last_idx - i] = hx[last_idx - 1 - i];
    hy[last_idx - i] = hy[last_idx - 1 - i];
  }
  if (fabsf(dl_x) > x48) {
    const float sign = (dl_x < 0.0f) ? -1.0f : 1.0f;
    hx[0] = (facing == sign) ? dl_x * x38 : dl_x * x3c;
  } else {
    hx[0] = 0.0f;
  }
  if (fabsf(dl_y) > x48) {
    hy[0] = (dl_y > 0.0f) ? dl_y * x40 : dl_y * x44;
  } else {
    hy[0] = 0.0f;
  }

  const int first_active = sheik_chain_first_active_from_hand(active, n);
  if (first_active < 0) {
    sheik_chain_publish_hitbox_map(batch, ii, ap, active, n);
    return;
  }

  // First active hand-side link (`cur` in it_802BC080): stick force + friction + caps + gravity band
  // + integrate + constrain to the hand target.
  vx[first_active] += hx[0] * x1c;
  vy[first_active] += hy[0] * x20;
  if (vx[first_active] > x10) {
    vx[first_active] -= x10;
  } else if (vx[first_active] < -x10) {
    vx[first_active] += x10;
  } else {
    vx[first_active] = 0.0f;
  }
  if (fabsf(vx[first_active]) > x24) {
    vx[first_active] = (vx[first_active] > 0.0f) ? x24 : -x24;
  }
  if (vy[first_active] > x18 - x28) {
    vy[first_active] -= x18;
  } else if (vy[first_active] < -x18 - x28) {
    vy[first_active] += x18;
  }
  if (fabsf(vy[first_active]) > x2c) {
    vy[first_active] = (vy[first_active] > 0.0f) ? x2c : -x2c;
  }
  px[first_active] += vx[first_active];
  py[first_active] += vy[first_active];
  pz[first_active] += vz[first_active];
  sheik_chain_constrain_to(px, py, pz, first_active, hand_x, hand_y, hand_z, seg);

  // Remaining links toward the tail: scaled stick impulse from the history trail, scaled friction/
  // caps/gravity, integrate, segment-constrain to the previous link. scale decays by x34 per link.
  int mode = 2;
  if (fabsf(lstick_x) < x48 && fabsf(lstick_y) < x48) {
    mode = 3;
  } else if (lstick_y < -0.5f) {
    mode = 1;
  }
  float scale = 1.0f * x34;
  int counter = 0;
  const uint32_t use_env_arg = batch->state.item_sheik_chain_env_flags[ii];
  uint32_t last_env_flags = 0u;
  for (int i = first_active + 1; i < n; i++) {
    if (active[i] == 0u) {
      continue;
    }
    const int idx = (int)(0.5f * (float)(counter + 1));
    const int cidx = (idx < MSL_SHEIK_CHAIN_HISTORY_LEN) ? idx : (MSL_SHEIK_CHAIN_HISTORY_LEN - 1);
    float dx_impulse = scale * (hx[cidx] * x1c);
    if (use_env_arg != 0u) {
      dx_impulse *= x30;
    }
    vx[i] += dx_impulse;
    vy[i] += scale * (hy[cidx] * x20);
    const float lim = x10 * scale;
    if (vx[i] > lim) {
      vx[i] -= lim;
    } else if (vx[i] < -lim) {
      vx[i] += lim;
    } else {
      vx[i] = 0.0f;
    }
    if (fabsf(vx[i]) > x24 * scale) {
      vx[i] = (vx[i] > 0.0f) ? x24 * scale : -x24 * scale;
    }
    const float vy_lim = x18 * scale;
    if (vy[i] > vy_lim - x28) {
      vy[i] -= vy_lim;
    } else if (vy[i] < -x18 * scale - x28) {
      vy[i] += vy_lim;
    }
    if (fabsf(vy[i]) > x2c * scale) {
      vy[i] = (vy[i] > 0.0f) ? x2c * scale : -x2c * scale;
    }
    counter++;
    scale *= x34;
    const float prev_x = px[i];
    const float prev_y = py[i];
    px[i] += vx[i];
    py[i] += vy[i];
    pz[i] += vz[i];
    uint8_t env_hit = 0u;
    if (counter > mode) {
      env_hit = sheik_chain_sweep_floor_link(batch, (int)(owner_idx / (size_t)MSL_MAX_PLAYERS),
                                             prev_x, prev_y, &px[i], &py[i]);
    }
    last_env_flags = (env_hit != 0u) ? 0x18000u : 0u;
    if (env_hit != 0u) {
      if (fabsf(vy[i]) > x60) {
        vy[i] *= -x5c;
      } else {
        if (vx[i] > x14) {
          vx[i] -= x14;
        } else if (vx[i] < -x14) {
          vx[i] += x14;
        } else {
          vx[i] = 0.0f;
        }
        vy[i] = 0.0f;
      }
    }
    sheik_chain_constrain_to(px, py, pz, i, px[i - 1], py[i - 1], pz[i - 1], seg);
  }
  // it_802BC080 writes the final masked environment flags into seakchain.x10; the next held solve
  // uses any nonzero value to scale follower-link X impulses by attrs->x30.
  // refs/melee/src/melee/it/items/itseakchain.c::it_802BC080
  batch->state.item_sheik_chain_env_flags[ii] = last_env_flags;
  sheik_chain_publish_hitbox_map(batch, ii, ap, active, n);
}

uint8_t sheik_chain_hitbox_world_pos(const MslBatch* batch, size_t fighter_idx, uint8_t hitbox_id,
                                     float* out_x, float* out_y, float* out_z) {
  if (batch == NULL || out_x == NULL || out_y == NULL || out_z == NULL ||
      hitbox_id >= (uint8_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_chain_itkind == 0u || ap->sheik_chain_link_count < 2u) {
    return 0u;
  }
  const int bi = (int)(fighter_idx / (size_t)MSL_MAX_PLAYERS);
  const int port = (int)(fighter_idx % (size_t)MSL_MAX_PLAYERS);
  // Locate the live Chain article owned by this fighter.
  size_t chain_ii = (size_t)-1;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] != 0u &&
        batch->state.item_type[ii] == ap->sheik_chain_itkind &&
        (int)batch->state.item_owner[ii] == port &&
        batch->state.item_sheik_chain_links_valid[ii] != 0u) {
      chain_ii = ii;
      break;
    }
  }
  if (chain_ii == (size_t)-1) {
    return 0u;
  }
  if (batch->state.item_sheik_chain_hitcaps_active[chain_ii] == 0u) {
    const uint16_t owner_action = batch->state.action_id[fighter_idx];
    // Start actions use the script-created Chain HitCapsules directly: SpecialSStart/AirSStart
    // create all four hitcaps at script frame 22 and set cmd_var0, while the x1C movement gate
    // (`ftSk_SpecialS_80110BCC`) is only called by held/end Chain Anim callbacks.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    //   ftSk_SpecialSStart_Anim,ftSk_SpecialAirSStart_Anim,ftSk_SpecialS_UpdateHitboxes,
    //   ftSk_SpecialS_80110BCC}
    // data/scripts/sheik.bin::MSLFTSC1 SpecialSStart/SpecialAirSStart frame-22 create_hitbox
    const uint8_t start_script_hitcaps =
        (batch->state.special_cmd0[fighter_idx] != 0u &&
         (owner_action == (uint16_t)MSL_ACT_SK_SPECIAL_S_START ||
          owner_action == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_START))
            ? 1u
            : 0u;
    if (start_script_hitcaps == 0u) {
      return 0u;
    }
  }
  int n = (int)ap->sheik_chain_link_count;
  if (n > MSL_SHEIK_CHAIN_MAX_LINKS) {
    n = MSL_SHEIK_CHAIN_MAX_LINKS;
  }
  // it_802BCB88 walks from the first active hand-side link through the free end and overwrites the
  // 4 fighter HitCapsules at stride checkpoints. Consume that published map rather than recomputing
  // a full-chain stride: partially deployed Chain maps several hitboxes to the same active frontier.
  // refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
  const uint8_t link_idx_u8 =
      batch->state.item_sheik_chain_hitbox_link_idx[chain_ii * (size_t)MSL_MAX_HITBOXES +
                                                    (size_t)hitbox_id];
  if (link_idx_u8 == 0xFFu || link_idx_u8 >= (uint8_t)n) {
    return 0u;
  }
  const int link_idx = (int)link_idx_u8;
  const size_t base = chain_ii * (size_t)MSL_SHEIK_CHAIN_MAX_LINKS;
  *out_x = batch->state.item_sheik_chain_link_pos_x[base + (size_t)link_idx];
  *out_y = batch->state.item_sheik_chain_link_pos_y[base + (size_t)link_idx];
  *out_z = batch->state.item_sheik_chain_link_pos_z[base + (size_t)link_idx];
  return 1u;
}

static uint8_t sheik_chain_owned_item_for_fighter(const MslBatch* batch, size_t fighter_idx,
                                                  size_t* out_ii) {
  if (batch == NULL || out_ii == NULL) {
    return 0u;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_chain_itkind == 0u) {
    return 0u;
  }
  const int bi = (int)(fighter_idx / (size_t)MSL_MAX_PLAYERS);
  const int port = (int)(fighter_idx % (size_t)MSL_MAX_PLAYERS);
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] != 0u &&
        batch->state.item_type[ii] == ap->sheik_chain_itkind &&
        (int)batch->state.item_owner[ii] == port &&
        batch->state.item_sheik_chain_links_valid[ii] != 0u) {
      *out_ii = ii;
      return 1u;
    }
  }
  return 0u;
}

uint8_t sheik_chain_hitbox_reset_prev_active(const MslBatch* batch, size_t fighter_idx) {
  size_t ii = 0u;
  if (sheik_chain_owned_item_for_fighter(batch, fighter_idx, &ii) == 0u) {
    return 0u;
  }
  return batch->state.item_sheik_chain_hit_reset_prev[ii] != 0u ? 1u : 0u;
}

void sheik_chain_clear_hitbox_reset_prev(MslBatch* batch, size_t fighter_idx) {
  size_t ii = 0u;
  if (sheik_chain_owned_item_for_fighter(batch, fighter_idx, &ii) == 0u) {
    return;
  }
  batch->state.item_sheik_chain_hit_reset_prev[ii] = 0u;
}

static void sheik_chain_items_update_anim_phase(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_chain_itkind == 0u) {
    return;
  }
  uint8_t needs_sort = 0u;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] == 0u ||
        batch->state.item_type[ii] != ap->sheik_chain_itkind) {
      continue;
    }
    const int owner = (int)batch->state.item_owner[ii];
    if (owner < 0 || owner >= (int)batch->config.num_players ||
        sheik_chain_owner_still_in_specials(batch, msl_idx_player(bi, owner)) == 0u) {
      // itSeakchain_UnkMotion4_Anim destroys the Chain if the owning fighter is no longer in the
      // Sheik SpecialS action family.
      // refs/melee/src/melee/it/items/itseakchain.c::{itSeakchain_UnkMotion4_Anim,notInSpecialS}
      item_slot_clear(batch, ii);
      needs_sort = 1u;
      continue;
    }
    const size_t owner_idx = msl_idx_player(bi, owner);
    if (batch->state.hitlag[owner_idx] != 0u) {
      const MslCharParams* chp = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);
      int full_cd = (chp != NULL) ? (int)chp->sheik_chain_extension_frames : 0;
      if (full_cd < 1) {
        full_cd = 1;
      }
      if (full_cd > 255) {
        full_cd = 255;
      }
      if (batch->state.item_sheik_chain_hit_cooldown[ii] == (uint8_t)full_cd) {
        // The owning fighter's active hitlag sets fp->x2219_b5. Fighter_8006A360 gates MotionState
        // Anim on !x2219_b5, so ftSk_SpecialS_80110BCC (Chain hitcap cooldown/clear/reactivate)
        // does not run while the article accessory callbacks are frozen. Apply that freeze to
        // runtime-created full Chain activation windows (mv.sk.specials.x1C == specialAttrs->x18).
        // Earlier partially aged windows can be reconstructed from replay/source-visible article
        // state with x1C already offset; advancing those keeps the existing source-seeded first
        // contact window aligned while later full activations follow the decomp hitlag gate.
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
        // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_80110BCC
        // refs/melee/src/melee/it/items/itseakchain.c::{fn_802BB694,it_802BC080,it_802BCB88}
        continue;
      }
      // Partially reconstructed Chain windows keep the prior cooldown bridge: source x1C is hidden
      // in replay rows, and the active article/link state can begin inside an already aged hitcap
      // window. Advance only the ftSk_SpecialS_80110BCC gate while the article geometry remains
      // frozen.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_80110BCC
      // refs/melee/src/melee/it/items/itseakchain.c::{fn_802BB694,it_802BC080,it_802BCB88}
      sheik_chain_update_hitcap_gate(batch, ii, ap, chp, owner_idx,
                                     (int)ap->sheik_chain_link_count);
      continue;
    }
    // The Chain item root is sampled from L3rdNa at spawn and remains item->pos; the accessory
    // callbacks still sample the live owner's L3rdNa JObj through every ItemLink's `link->jobj`
    // before advancing links. Do not overwrite item->pos with the fresh hand sample, but do pass
    // that hand sample as the source target for fn_802BB44C/fn_802BB694.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_CheckInitChain
    // refs/melee/src/melee/it/items/itseakchain.c::{
    //   itSeakChain_Spawn,it_802BAF2C,fn_802BB44C,fn_802BB694}
    float anchor_x = batch->state.item_pos_x[ii];
    float anchor_y = batch->state.item_pos_y[ii];
    float anchor_z = 0.0f;
    if (sheik_article_spawn_anchor_position_z(batch, msl_idx_player(bi, owner),
                                              ap->sheik_chain_spawn_part_id,
                                              // fn_802BB44C/fn_802BB694/it_802BCFC4 sample each
                                              // Chain link JObj through lb_8000B1CC with local
                                              // offset (0,0,0.1) before advancing the Vec3 links.
                                              // refs/melee/src/melee/it/items/itseakchain.c::{
                                              //   fn_802BB44C,fn_802BB694,it_802BCFC4}
                                              0.1f, &anchor_x, &anchor_y, &anchor_z) != 0u) {
      // Run the active-frontier/held whip solve from the live L3rdNa target each frame so the chain
      // segments swing toward the source-owned hand joint. The solved link positions feed the
      // fighter-side Chain hitbox positions through it_802BCB88/ftSk_SpecialS_UpdateHitboxes.
      sheik_chain_solve_links(batch, ii, ap, owner_idx, anchor_x, anchor_y, anchor_z,
                              batch->state.item_state[ii]);
    }
  }
  if (needs_sort != 0u) {
    items_sort(batch, bi);
  }
}

static void sheik_vanish_smoke_items_update(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_vanish_itkind == 0u) {
    return;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] == 0u ||
        batch->state.item_type[ii] != ap->sheik_vanish_itkind) {
      continue;
    }
    // itSeakvanish_UnkMotion0_Anim delegates to the generic item lifetime decrement/destroy path
    // after it_802B1D40 -> it_8027518C publishes the final xD44_lifeTimer.
    // refs/melee/src/melee/it/items/itseakvanish.c::{
    //   it_802B1D40,itSeakvanish_UnkMotion0_Anim}
    if ((batch->state.item_hidden_callback_flags[ii] &
         (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME) != 0u) {
      batch->state.item_hidden_callback_flags[ii] &=
          (uint8_t)~MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME;
      continue;
    }
    if (batch->state.item_timer[ii] <= 1.0f) {
      item_slot_clear(batch, ii);
    } else {
      batch->state.item_timer[ii] -= 1.0f;
    }
  }
}

uint8_t items_spawn_sheik_vanish_smoke_article(MslBatch* batch, size_t owner_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_vanish_itkind == 0u || ap->sheik_vanish_lifetime_frames == 0u) {
    return 0u;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  const size_t o_idx = owner_idx;
  const uint8_t char_id = batch->state.char_id[o_idx];
  if (char_id != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  if (items_find_owned_item_slot(batch, bi, owner, ap->sheik_vanish_itkind) >= 0) {
    return 0u;
  }
  float spawn_x = 0.0f;
  float spawn_y = 0.0f;
  if (sheik_article_spawn_anchor_position(batch, owner_idx, ap->sheik_vanish_spawn_part_id,
                                          &spawn_x, &spawn_y) == 0u) {
    return 0u;
  }

  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);

  // Vanish travel entry installs `fn_80112ED8` through inlineA0 / ftSk_SpecialHi_80113A30.
  // The accessory callback samples HipN with lb_8000B1CC, spawns the smoke article once with
  // it_802B1C60, and initializes It_Kind_Seak_Vanish state 0. The runtime-visible lifetime comes
  // from it_8027518C, which overwrites the local 60.0f seed with ItemCommonData::xF8 before the
  // script starts.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialHi.c::{
  //   inlineA0,ftSk_SpecialHi_80113A30,fn_80112ED8,ftSk_SpecialHi_80112F48}
  // refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1C60,it_802B1D40}
  // refs/melee/src/melee/it/it_2725.c::it_8027518C
  batch->state.item_exists[ii] = 1u;
  batch->state.item_type[ii] = ap->sheik_vanish_itkind;
  batch->state.item_state[ii] = 0u;
  batch->state.item_owner[ii] = (int8_t)owner;
  batch->state.item_instance_id[ii] = batch->state.instance_id[o_idx];
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_direction[ii] = batch->state.facing[o_idx] ? 1.0f : -1.0f;
  batch->state.item_pos_x[ii] = spawn_x;
  batch->state.item_pos_y[ii] = spawn_y;
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  batch->state.item_timer[ii] = (float)ap->sheik_vanish_lifetime_frames;
  batch->state.item_hidden_callback_flags[ii] |=
      (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME;
  return 1u;
}

static void illusion_spawn_from_fighter(MslBatch* batch, int bi, int owner) {
  if (batch == NULL) {
    return;
  }
  // No char-family guard: item_article_illusion_kind() below returns 0 for chars without
  // the ghost article, and the spawn pulse itself is keyed on extracted MotionState kinds.
  const size_t o_idx = msl_idx_player(bi, owner);
  const uint8_t char_id = batch->state.char_id[o_idx];
  const uint16_t illusion_itkind = item_article_illusion_kind(char_id);
  if (illusion_itkind == 0u) {
    return;
  }
  if (items_find_illusion_slot(batch, bi, owner, illusion_itkind) >= 0) {
    return;
  }

  const uint16_t action_id_u16 = batch->state.action_id[o_idx];
  const uint16_t prev_action_id_u16 = batch->state.prev_action_id[o_idx];
  const uint32_t anim_u32 = batch->state.animation_index[o_idx];
  if (anim_u32 > 0xFFFFu) {
    return;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  const float af = items_cur_anim_frame_f32(batch, o_idx);
  const int32_t prev_fp =
      batch->state.anim_frame_fp_q16_16[o_idx] - batch->state.frame_speed_mul_fp_q16_16[o_idx];
  const float af_prev = msl_anim_frame_sanitize_f32(msl_f32_from_q16_16(prev_fp));
  uint8_t spawn_pulse = illusion_spawn_pulse_crossed(char_id, action_id_u16, msid, af_prev, af);
  if (!spawn_pulse && action_is_illusion_end(char_id, action_id_u16) &&
      batch->state.action_frame[o_idx] == 0 &&
      ((msl_motion_state_fx_special_kind(char_id, prev_action_id_u16) ==
            (uint8_t)MSL_FX_KIND_SPECIAL_S &&
        msl_motion_state_fx_special_kind(char_id, action_id_u16) ==
            (uint8_t)MSL_FX_KIND_SPECIAL_S_END) ||
       (msl_motion_state_fx_special_kind(char_id, prev_action_id_u16) ==
            (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S &&
        msl_motion_state_fx_special_kind(char_id, action_id_u16) ==
            (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END))) {
    // One-step ordering bridge for side-special ghost spawn:
    // - ftFx_SpecialS_Anim / ftFx_SpecialAirS_Anim invokes ftFox_SpecialS_CreateGhostItem during
    //   main dash anim callbacks, while locomotion state progression can advance to End in the same
    //   step under teacher-forced one-step execution.
    // - When main->end transition occurs on this frame, preserve the cmd_var[2] spawn pulse
    //   ownership by accepting end-entry (action_frame==0) if previous action was the matching main
    //   and the previous action-frame already advanced into the sourced cmd_var[2] pulse-crossing
    //   window.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFx_SpecialSEnd_Anim,ftFx_SpecialAirSEnd_Anim}
    // data/special_msids/{fox,falco}.json side_ground.main/side_air.main
    // data/scripts/{fox,falco}.bin (MSLFTSC1) set_cmd_var idx=2 pulse events
    uint16_t prev_msid = 0u;
    int16_t first_pulse = -1;
    if (illusion_prev_main_msid_for_action(char_id, prev_action_id_u16, &prev_msid) &&
        move_tables_special_cmd2_first_pulse_frame(char_id, prev_msid, &first_pulse) &&
        batch->state.prev_action_frame[o_idx] >= (int16_t)(first_pulse - 1)) {
      spawn_pulse = 1u;
    }
  }
  if (!spawn_pulse) {
    return;
  }

  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);
  const MslCharParams* chp = msl_char_params_fast(char_id);
  if (chp == NULL) {
    return;
  }

  batch->state.item_exists[ii] = 1u;
  batch->state.item_type[ii] = illusion_itkind;
  batch->state.item_owner[ii] = (int8_t)owner;
  // Spawn motion state in it_8029CFF0 is chosen by ftLib_800865CC(owner)->ground_or_air:
  // ground(0)=>state0, air(1)=>state1.
  // refs/melee/src/melee/it/items/itfoxillusion.c::{it_8029CFF0}
  // refs/melee/src/melee/ft/ftlib.c::ftLib_800865CC
  batch->state.item_state[ii] = batch->state.on_ground[o_idx] ? 0u : 1u;
  batch->state.item_instance_id[ii] = batch->state.instance_id[o_idx];
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_direction[ii] = batch->state.facing[o_idx] ? 1.0f : -1.0f;
  batch->state.item_pos_x[ii] = batch->state.pos_x[o_idx];
  batch->state.item_pos_y[ii] = batch->state.pos_y[o_idx];
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  batch->state.item_attack_id[ii] = batch->state.attack_id[o_idx];
  batch->state.item_attack_instance[ii] = batch->state.attack_instance[o_idx];
  // Ghost article spawn initializes lifeTimer from special attrs[0].
  // data/characters/{fox,falco}.json illusion_item_lifetime_state01_frames
  // refs/melee/src/melee/it/items/itfoxillusion.c::it_8029CFF0
  batch->state.item_timer[ii] = (float)chp->illusion_item_lifetime_state01_frames;
}

static inline float items_cur_anim_frame_f32(const MslBatch* batch, size_t idx) {
  // Item/fighter cmd/script timing consults runtime `fp->cur_anim_frame`.
  // In this sim the authoritative live lane is `anim_frame_fp_q16_16`:
  // - advanced by `anim_timebase_update_pre_input()` at proc-prio-1 ordering, and
  // - frozen under hitlag via `msl_anim_timebase_tick_once()` when `hitlag_started_frame!=0`.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
  //
  // `anim_frame_f32` is a seed snapshot lane and can be stale mid-step, so do not use it for
  // callback/script-time cmd_var gating.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  return msl_anim_frame_sanitize_f32(msl_f32_from_q16_16(batch->state.anim_frame_fp_q16_16[idx]));
}

static uint8_t blaster_gun_update_from_fighter(MslBatch* batch, int bi, int owner,
                                               const MslLaserParams* lp) {
  if (batch == NULL || lp == NULL) {
    return 0u;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  const uint8_t owner_char = batch->state.char_id[o_idx];
  const uint16_t action_id_u16 = batch->state.action_id[o_idx];
  const uint8_t want_state = blaster_gun_state_from_action_id(action_id_u16);
  uint8_t want_gun = (want_state != 9) ? 1u : 0u;

  const int existing_slot = items_find_gun_slot(batch, bi, owner, lp->gun_itkind);
  const uint8_t is_throw = action_is_blaster_throw(action_id_u16);
  if (is_throw) {
    const float af_cur = items_cur_anim_frame_f32(batch, o_idx);
    // `cmd1_cur` is the MSLFTSC1/move-table-backed value of ftFx_Throw_Anim's
    // `fp->cmd_vars[1]` switch:
    // - case 1: maintain/spawn gun-side ownership path
    // - case 2/0: clear ownership path
    //
    // Source of truth is extracted move script data:
    // - `data/moves/{fox,falco}.json` set_cmd_var(idx=1) events
    // - parsed into move_tables_throw_cmd1_active()
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    const uint8_t cmd1_cur = move_tables_throw_cmd1_active(owner_char, action_id_u16, af_cur);

    // Throw-side blaster ownership in ftFx_Throw_Anim:
    // - case 1 (cmd_vars[1]==1): spawn/update gun flow
    // - case 2 / case 0: clear fighter pointer path
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    //
    // Keep throw-side gun ownership only while cmd1 is active.
    // When cmd1 turns off, clear immediately (do not apply SpecialNEnd-style linger).
    if (!cmd1_cur) {
      if (existing_slot < 0) {
        return 0u;
      }
      const size_t ii = msl_idx_item(bi, existing_slot);
      item_slot_clear(batch, ii);
      return 0u;
    }
    want_gun = 1u;
  }
  if (!want_gun) {
    if (existing_slot < 0) {
      return 0u;
    }
    const size_t ii = msl_idx_item(bi, existing_slot);

    // Blaster gun clear:
    // - ftFx_SpecialNEnd_Anim clears fp->fv.fx.x222C_blasterGObj before the action exits through
    //   ft_8008A2BC.
    // - itFoxblaster_UnkMotion8_Anim calls ftFx_SpecialN_CheckRemoveBlaster, then clear_blaster()
    //   when that fighter pointer is NULL.
    // - Throw-side gun lifetime is governed separately above by ftFx_Throw_Anim cmd_vars[1].
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    //   ftFx_SpecialNEnd_Anim,ftFx_SpecialN_CheckRemoveBlaster}
    // refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    item_slot_clear(batch, ii);
    return 0u;
  }

  int slot = existing_slot;
  uint8_t spawned = 0u;
  if (slot < 0) {
    slot = items_alloc_slot(batch, bi);
    if (slot < 0) {
      return 0u;
    }
    const size_t ii = msl_idx_item(bi, slot);
    item_slot_clear(batch, ii);
    batch->state.item_exists[ii] = 1;
    batch->state.item_type[ii] = lp->gun_itkind;
    batch->state.item_owner[ii] = (int8_t)owner;

    // Slippi records item instance_id from item->xDA8_short.
    // On item spawn with a fighter parent, xDA8_short is copied from the fighter's x2074.x2088.
    // refs/melee/src/melee/it/it_2725.c::it_8027B070
    batch->state.item_instance_id[ii] = batch->state.instance_id[o_idx];

    // Slippi item `spawn_id` is item->x1C. Item spawn assigns x1C from a global incrementing
    // counter (`it_804D6D10++`), so use our deterministic next-id allocator for new spawns.
    // refs/melee/src/melee/it/item.c::Item_80267AA8
    batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);

    // Item staling identity copies the spawning fighter snapshot in the generic parented-spawn
    // path. Blaster guns are attached articles rather than damaging projectiles, but Slippi still
    // exposes xD88/xD8C and later laser rows rely on the same parent identity episode.
    // refs/melee/src/melee/it/it_2725.c::it_8027B070
    // refs/melee/src/melee/it/items/itfoxblaster.c::it_802AE8A8
    batch->state.item_attack_id[ii] = batch->state.attack_id[o_idx];
    batch->state.item_attack_instance[ii] = batch->state.attack_instance[o_idx];

    // GALE01 ItemCommonData->xF8 is used as a default lifeTimer in item code:
    // refs/melee/src/melee/it/it_2725.c::it_8027518C (sets item->xD44_lifeTimer = it_804D6D28->xF8)
    // Slippi shows blaster gun timer=1400.0f in the suite; treat 1400.0 as the GALE01 xF8 value.
    batch->state.item_timer[ii] = 1400.0f;
    spawned = 1;
  }

  const size_t ii = msl_idx_item(bi, slot);
  batch->state.item_exists[ii] = 1;
  batch->state.item_type[ii] = lp->gun_itkind;
  batch->state.item_owner[ii] = (int8_t)owner;
  batch->state.item_state[ii] = want_state;

  // Gun is attached to a fighter part and does not have projectile physics (vel stays 0 in Slippi).
  // refs/melee/src/melee/it/items/itfoxblaster.c::it_802AE8A8 (spawn.vel=0, attach via Item_8026AB54)
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;

  // Position:
  // In v1, avoid introducing a new (potentially wrong) bone-space attachment transform that can
  // amplify overall item MAEs. Only initialize position on spawn; otherwise preserve the seeded
  // item position and let the one-step eval measure the residual delta.
  if (spawned) {
    batch->state.item_pos_x[ii] = batch->state.pos_x[o_idx];
    batch->state.item_pos_y[ii] = batch->state.pos_y[o_idx];
    batch->state.item_direction[ii] = batch->state.facing[o_idx] ? 1.0f : -1.0f;
  }

  // Maintain timer parity for already-seeded gun items (lifetime does not tick down in our v1).
  if (!(batch->state.item_timer[ii] > 0.0f)) {
    batch->state.item_timer[ii] = 1400.0f;
  }
  return spawned;
}

static inline uint8_t laser_should_shoot_between_frames(const MslLaserParams* lp, uint8_t char_id,
                                                        uint16_t msid, float prev_frame,
                                                        float cur_frame) {
  if (lp == NULL || (msid != lp->ground_loop_msid && msid != lp->air_loop_msid)) {
    return 0;
  }
  if (cur_frame < prev_frame) {
    // Loop restarts enter through Fighter_ChangeMotionState and reset the live AObj frame; the
    // shoot command belongs to the pre-restart Anim callback and is intentionally not replayed from
    // the new loop's frame 0 here.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    //   ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim}
    return 0;
  }
  return move_tables_special_cmd2_pulse_crossed(char_id, msid, prev_frame, cur_frame, NULL);
}

static inline float item_segment_segment_dist2(float p0x, float p0y, float p0z, float p1x,
                                               float p1y, float p1z, float q0x, float q0y,
                                               float q0z, float q1x, float q1y, float q1z) {
  // Closest distance between two 3D segments (projectile sweep segment vs hurt capsule segment).
  // Decomp shape for fox/falco lasers:
  // - itFoxlaser_UnkMotion1_Phys snapshots prev_pos before velocity integration.
  // - it_8029C4D4 resolves collision across (prev_pos -> cur_pos), not a point probe at cur_pos.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  const float ux = p1x - p0x;
  const float uy = p1y - p0y;
  const float uz = p1z - p0z;
  const float vx = q1x - q0x;
  const float vy = q1y - q0y;
  const float vz = q1z - q0z;
  const float wx = p0x - q0x;
  const float wy = p0y - q0y;
  const float wz = p0z - q0z;

  const float a = ux * ux + uy * uy + uz * uz;
  const float b = ux * vx + uy * vy + uz * vz;
  const float c = vx * vx + vy * vy + vz * vz;
  const float d = ux * wx + uy * wy + uz * wz;
  const float e = vx * wx + vy * wy + vz * wz;
  const float D = a * c - b * b;
  const float eps = 1e-8f;

  if (a < eps || c < eps) {
    // Stage-item hurtboxes such as Yoshi's Shy Guy can be point capsules (A==B). Use the shared
    // combat solver only for degenerate item/hurt segments so point-vs-segment distance is modeled
    // without perturbing ordinary non-degenerate laser/item contact ordering.
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    float d2 = 0.0f;
    combat_segment_segment_dist2(p0x, p0y, p0z, p1x, p1y, p1z, q0x, q0y, q0z, q1x, q1y, q1z, &d2,
                                 NULL, NULL);
    return d2;
  }

  float sN = 0.0f;
  float sD = D;
  float tN = 0.0f;
  float tD = D;

  if (D < eps) {
    sN = 0.0f;
    sD = 1.0f;
    tN = e;
    tD = c;
  } else {
    sN = b * e - c * d;
    tN = a * e - b * d;
    if (sN < 0.0f) {
      sN = 0.0f;
      tN = e;
      tD = c;
    } else if (sN > sD) {
      sN = sD;
      tN = e + b;
      tD = c;
    }
  }

  if (tN < 0.0f) {
    tN = 0.0f;
    if (-d < 0.0f) {
      sN = 0.0f;
    } else if (-d > a) {
      sN = sD;
    } else {
      sN = -d;
      sD = a;
    }
  } else if (tN > tD) {
    tN = tD;
    if ((-d + b) < 0.0f) {
      sN = 0.0f;
    } else if ((-d + b) > a) {
      sN = sD;
    } else {
      sN = -d + b;
      sD = a;
    }
  }

  const float sc = (fabsf(sN) < eps) ? 0.0f : (sN / sD);
  const float tc = (fabsf(tN) < eps) ? 0.0f : (tN / tD);

  const float dx = wx + sc * ux - tc * vx;
  const float dy = wy + sc * uy - tc * vy;
  const float dz = wz + sc * uz - tc * vz;
  return dx * dx + dy * dy + dz * dz;
}

static inline uint8_t item_swept_sphere_capsule_intersects(const MslBatch* batch, int bi,
                                                           int defender, float sx0, float sy0,
                                                           float sx1, float sy1, float sr,
                                                           int cap_i, uint8_t* out_hurt_height) {
  // Swept sphere segment (sx0,sy0,0)->(sx1,sy1,0) vs hurt capsule segment AB.
  // Decomp item-vs-fighter collision sweep uses prev_pos -> cur_pos.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint8_t cap_count = batch->state.hurtcap_count[d_idx];
  if (cap_i < 0 || cap_i >= (int)cap_count) {
    return 0;
  }
  const size_t hi =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)defender) * (size_t)MSL_MAX_HURTCAPS +
      (size_t)cap_i;
  if (!batch->state.hurtcap_enabled[hi]) {
    return 0;
  }
  float ax = batch->state.hurtcap_a_x[hi];
  float ay = batch->state.hurtcap_a_y[hi];
  const float az = batch->state.hurtcap_a_z[hi];
  const float bx = batch->state.hurtcap_b_x[hi];
  const float by = batch->state.hurtcap_b_y[hi];
  const float bz = batch->state.hurtcap_b_z[hi];
  const float cr = batch->state.hurtcap_radius[hi];
  const float rr = sr + cr;
  const float d2 =
      item_segment_segment_dist2(sx0, sy0, 0.0f, sx1, sy1, 0.0f, ax, ay, az, bx, by, bz);
  if (d2 > (rr * rr)) {
    return 0;
  }
  if (out_hurt_height) {
    *out_hurt_height = batch->state.hurtcap_height[hi];
  }
  return 1;
}

static inline uint8_t item_laser_hitcapsule_overlaps_fighter_hitcapsule(const MslBatch* batch,
                                                                        size_t hb_i, float sx0,
                                                                        float sy0, float sx1,
                                                                        float sy1,
                                                                        float item_radius) {
  if (batch == NULL || !(item_radius > 0.0f)) {
    return 0u;
  }
  float hx0 = batch->state.hitbox_x[hb_i];
  float hy0 = batch->state.hitbox_y[hb_i];
  float hz0 = batch->state.hitbox_z[hb_i];
  if (batch->state.hitbox_prev_enabled[hb_i]) {
    hx0 = batch->state.hitbox_prev_x[hb_i];
    hy0 = batch->state.hitbox_prev_y[hb_i];
    hz0 = batch->state.hitbox_prev_z[hb_i];
  }
  const float hx1 = batch->state.hitbox_x[hb_i];
  const float hy1 = batch->state.hitbox_y[hb_i];
  const float hz1 = batch->state.hitbox_z[hb_i];
  const float d2 =
      item_segment_segment_dist2(sx0, sy0, 0.0f, sx1, sy1, 0.0f, hx0, hy0, hz0, hx1, hy1, hz1);
  const float rr = item_radius + batch->state.hitbox_radius[hb_i];
  return (uint8_t)(d2 <= rr * rr);
}

static inline uint8_t item_laser_fighter_hitcapsule_contact_mask_precedes_shield_body(
    const MslBatch* batch, int bi, int fighter, const MslLaserParams* lp, uint8_t laser_state,
    float x0, float y0, float x, float y, float ux, float uy, float sr, float laser_prev_scale_z,
    float laser_scale_z, float item_damage) {
  if (batch == NULL || lp == NULL || fighter < 0 || fighter >= (int)batch->config.num_players) {
    return 0u;
  }
  const uint8_t zero_kb_damage_class_state =
      (laser_state == 0u) ? lp->zero_kb_damage_class : lp->state1_zero_kb_damage_class;
  if (zero_kb_damage_class_state == 0u) {
    return 0u;
  }
  // Source ordering: ftColl_8007925C builds eligible fighter HitCapsules first, then before
  // SHIELD/BODY admission it tests item HitCapsule vs fighter HitCapsule in `catch_path` and
  // continues the item loop when the clank/body-collision owner resolves. Keep this registration
  // on generated zero-KB laser states: Fox blaster shots have all-zero source KB terms and can
  // have fighter HitCapsule contact without entering the regular BODY damage-state path, while
  // Falco's nonzero-KB laser BODY rows must still apply their ordinary hit.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007AFC
  // data/items/lasers.bin::MSLLASR1 zero_kb_damage_class/state1_zero_kb_damage_class
  const uint8_t off_n =
      (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
  uint8_t contact_mask = 0u;
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    const size_t hb_i = idx_hitbox(bi, fighter, hb);
    if (!batch->state.hitbox_enabled[hb_i]) {
      continue;
    }
    const uint16_t flags = batch->state.hitbox_flags[hb_i];
    if ((flags & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0u ||
        (flags & (uint16_t)MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION) == 0u) {
      continue;
    }
    if (batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
        batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_INERT) {
      continue;
    }
    if (!(batch->state.hitbox_damage[hb_i] > 0.0f) ||
        batch->state.hitbox_damage[hb_i] > item_damage) {
      continue;
    }
    if (off_n == 0u) {
      if (item_laser_hitcapsule_overlaps_fighter_hitcapsule(batch, hb_i, x0, y0, x, y, sr)) {
        contact_mask |= 0x01u;
      }
      continue;
    }
    for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X; oi++) {
      const float off_x =
          (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
      const float sx0 = x0 + (ux * off_x * laser_prev_scale_z);
      const float sy0 = y0 + (uy * off_x * laser_prev_scale_z);
      const float sx1 = x + (ux * off_x * laser_scale_z);
      const float sy1 = y + (uy * off_x * laser_scale_z);
      if (item_laser_hitcapsule_overlaps_fighter_hitcapsule(batch, hb_i, sx0, sy0, sx1, sy1, sr)) {
        if (oi < (uint8_t)MSL_MAX_HITBOXES) {
          contact_mask |= (uint8_t)(1u << oi);
        }
      }
    }
  }
  return contact_mask;
}

static inline uint8_t item_swept_sphere_capsule_overlap_amount(
    const MslBatch* batch, int bi, int defender, float sx0, float sy0, float sx1, float sy1,
    float sr, int cap_i, uint8_t* out_hurt_height, float* out_overlap_amount,
    uint8_t flatten_hurt_z, float hurt_radius_mul) {
  if (out_overlap_amount) {
    *out_overlap_amount = 0.0f;
  }
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint8_t cap_count = batch->state.hurtcap_count[d_idx];
  if (cap_i < 0 || cap_i >= (int)cap_count) {
    return 0;
  }
  const size_t hi =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)defender) * (size_t)MSL_MAX_HURTCAPS +
      (size_t)cap_i;
  if (!batch->state.hurtcap_enabled[hi]) {
    return 0;
  }
  float ax = batch->state.hurtcap_a_x[hi];
  float ay = batch->state.hurtcap_a_y[hi];
  // The default path consumes seed-visible hurt capsule endpoints. A narrow laser BODY lane below
  // can request lbColl's flattened hurtcap Z when its source-owned filters are present.
  const float hurt_z = batch->state.pos_z[d_idx];
  const float az = flatten_hurt_z ? hurt_z : batch->state.hurtcap_a_z[hi];
  const float bx = batch->state.hurtcap_b_x[hi];
  const float by = batch->state.hurtcap_b_y[hi];
  const float bz = flatten_hurt_z ? hurt_z : batch->state.hurtcap_b_z[hi];
  float cr = batch->state.hurtcap_radius[hi];
  if (hurt_radius_mul > 0.0f) {
    cr *= hurt_radius_mul;
  }
  const float rr = sr + cr;
  const float d2 =
      item_segment_segment_dist2(sx0, sy0, 0.0f, sx1, sy1, 0.0f, ax, ay, az, bx, by, bz);
  if (d2 > (rr * rr)) {
    return 0;
  }
  if (out_overlap_amount) {
    *out_overlap_amount = rr - sqrtf(d2);
  }
  if (out_hurt_height) {
    *out_hurt_height = batch->state.hurtcap_height[hi];
  }
  return 1;
}

static inline void item_lbcoll_80006e58_closest_points(float p0x, float p0y, float p0z, float p1x,
                                                       float p1y, float p1z, float q0x, float q0y,
                                                       float q0z, float q1x, float q1y, float q1z,
                                                       float* out_px, float* out_py, float* out_pz,
                                                       float* out_qx, float* out_qy, float* out_qz,
                                                       float* out_world_dist) {
  const float ux = p1x - p0x;
  const float uy = p1y - p0y;
  const float uz = p1z - p0z;
  const float vx = q1x - q0x;
  const float vy = q1y - q0y;
  const float vz = q1z - q0z;
  const float wx = p0x - q0x;
  const float wy = p0y - q0y;
  const float wz = p0z - q0z;
  const float a = msl_dot3(ux, uy, uz, ux, uy, uz);
  const float b = msl_dot3(ux, uy, uz, vx, vy, vz);
  const float c = msl_dot3(vx, vy, vz, vx, vy, vz);
  const float d = msl_dot3(ux, uy, uz, wx, wy, wz);
  const float e = msl_dot3(vx, vy, vz, wx, wy, wz);
  const float denom = a * c - b * b;
  const float eps_hi = 1.0e-5f;
  const float eps_lo = -1.0e-5f;
  float s = 0.0f;
  float t = 0.0f;

  // This intentionally follows lbColl_80006E58's endpoint fallback order instead of the generic
  // closest-segment helper: the source routine can select a different endpoint pair, and that pair
  // feeds the local-matrix radius scalar that writes HitCapsule.coll_distance.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80006E58
  if (c < eps_hi && c > eps_lo) {
    if (!(a < eps_hi && a > eps_lo)) {
      s = -d / a;
      if (s > 1.0f) {
        s = 1.0f;
      } else if (s < 0.0f) {
        s = 0.0f;
      }
    }
  } else if (denom < eps_hi && denom > eps_lo) {
    const float mid_x = q0x + 0.5f * vx;
    const float mid_y = q0y + 0.5f * vy;
    const float mid_z = q0z + 0.5f * vz;
    const float d0 = msl_len2_3(p0x - mid_x, p0y - mid_y, p0z - mid_z);
    const float d1 = msl_len2_3(p1x - mid_x, p1y - mid_y, p1z - mid_z);
    s = (d0 < d1) ? 0.0f : 1.0f;
    const float px = (s == 0.0f) ? p0x : p1x;
    const float py = (s == 0.0f) ? p0y : p1y;
    const float pz = (s == 0.0f) ? p0z : p1z;
    const float q_to_p_x = q0x - px;
    const float q_to_p_y = q0y - py;
    const float q_to_p_z = q0z - pz;
    t = -msl_dot3(vx, vy, vz, q_to_p_x, q_to_p_y, q_to_p_z) / c;
    if (t > 1.0f) {
      t = 1.0f;
    } else if (t < 0.0f) {
      t = 0.0f;
    }
  } else {
    s = ((b * e) - (c * d)) / denom;
    t = ((a * e) - (b * d)) / denom;
    if (s > 1.0f || s < 0.0f || t > 1.0f || t < 0.0f) {
      float s_candidate = 0.0f;
      float t_candidate = 0.0f;
      float first_d2 = 0.0f;
      float second_d2 = 0.0f;
      if (s < 0.0f) {
        s_candidate = 0.0f;
        combat_point_segment_dist2(p0x, p0y, p0z, q0x, q0y, q0z, q1x, q1y, q1z, &first_d2,
                                   &t_candidate);
      } else {
        s_candidate = 1.0f;
        combat_point_segment_dist2(p1x, p1y, p1z, q0x, q0y, q0z, q1x, q1y, q1z, &first_d2,
                                   &t_candidate);
      }
      float s_candidate_2 = 0.0f;
      float t_candidate_2 = 0.0f;
      if (t < 0.0f) {
        t_candidate_2 = 0.0f;
        combat_point_segment_dist2(q0x, q0y, q0z, p0x, p0y, p0z, p1x, p1y, p1z, &second_d2,
                                   &s_candidate_2);
      } else {
        t_candidate_2 = 1.0f;
        combat_point_segment_dist2(q1x, q1y, q1z, p0x, p0y, p0z, p1x, p1y, p1z, &second_d2,
                                   &s_candidate_2);
      }
      if (first_d2 < second_d2) {
        s = s_candidate;
        t = t_candidate;
      } else {
        s = s_candidate_2;
        t = t_candidate_2;
      }
    }
  }

  const float px = p0x + s * ux;
  const float py = p0y + s * uy;
  const float pz = p0z + s * uz;
  const float qx = q0x + t * vx;
  const float qy = q0y + t * vy;
  const float qz = q0z + t * vz;
  if (out_px) {
    *out_px = px;
  }
  if (out_py) {
    *out_py = py;
  }
  if (out_pz) {
    *out_pz = pz;
  }
  if (out_qx) {
    *out_qx = qx;
  }
  if (out_qy) {
    *out_qy = qy;
  }
  if (out_qz) {
    *out_qz = qz;
  }
  if (out_world_dist) {
    const float dx = px - qx;
    const float dy = py - qy;
    const float dz = pz - qz;
    const float d2 = msl_len2_3(dx, dy, dz);
    *out_world_dist = (d2 > 0.0f) ? sqrtf(d2) : 0.0f;
  }
}

static inline uint8_t item_body_lbcoll_matrix_radius_overlap(
    const MslBatch* batch, int bi, int defender, float sx0, float sy0, float sx1, float sy1,
    float sr, int cap_i, uint8_t* out_hurt_height, float* out_overlap_amount,
    uint8_t* out_evaluated, uint8_t flatten_hurt_z) {
  if (out_overlap_amount) {
    *out_overlap_amount = 0.0f;
  }
  if (out_evaluated) {
    *out_evaluated = 0u;
  }
  if (batch == NULL || !(sr > 0.0f)) {
    return 0u;
  }
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint8_t cap_count = batch->state.hurtcap_count[d_idx];
  if (cap_i < 0 || cap_i >= (int)cap_count) {
    return 0u;
  }
  const size_t hi = idx_hurtcap(bi, defender, cap_i);
  if (!batch->state.hurtcap_enabled[hi]) {
    return 0u;
  }
  const uint8_t char_id = batch->state.char_id[d_idx];
  const MslHurtCap* caps = NULL;
  uint16_t cap_count_u16 = 0u;
  if (hurtcaps_get(char_id, &caps, &cap_count_u16) != 0 || caps == NULL ||
      (uint16_t)cap_i >= cap_count_u16) {
    return 0u;
  }
  const MslHurtCap* cap = &caps[cap_i];

  uint16_t msid = 0u;
  const uint32_t anim_u32 = batch->state.animation_index[d_idx];
  const uint16_t action_id = batch->state.action_id[d_idx];
  uint8_t no_submotion_guard_source_pose = 0u;
  if (anim_u32 > 0xFFFFu) {
    switch (action_id) {
      case (uint16_t)MSL_ACT_GUARD_ON:
      case (uint16_t)MSL_ACT_GUARD_REFLECT:
        // ftCo_MS_GuardOn and ftCo_MS_GuardReflect both source their no-submotion collision pose
        // from ftCo_SM_GuardOn. Slippi can serialize the post-frame animation_index as -1 while
        // ftColl_8007925C still calls lbColl_8000805C against the live hurtcaps.
        // refs/melee/src/melee/ft/ftmotionstates.c::{ftCo_MS_GuardOn,ftCo_MS_GuardReflect}
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
        msid = (uint16_t)MSL_SM_GUARD_ON;
        no_submotion_guard_source_pose = 1u;
        break;
      case (uint16_t)MSL_ACT_GUARD:
        msid = (uint16_t)MSL_SM_GUARD;
        no_submotion_guard_source_pose = 1u;
        break;
      case (uint16_t)MSL_ACT_GUARD_SET_OFF:
        msid = (uint16_t)MSL_SM_GUARD_DAMAGE;
        no_submotion_guard_source_pose = 1u;
        break;
      default:
        return 0u;
    }
  } else {
    msid = (uint16_t)anim_u32;
  }

  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  const uint16_t pose_frame = msl_anim_frame_floor_u16(anim_frame_f32);
  // Keep fractional matrix sampling limited to the retained LandingFallSpecial flattened-Z owner.
  // Other exact item BODY users stay on the existing source-visible frame sample until their wider
  // pose/hurtcap eligibility owners are proven.
  const float pose_sample_frame = (flatten_hurt_z != 0u) ? anim_frame_f32 : (float)pose_frame;

  float m[12];
  if (anim_pose_get_collision_matrix_f32(batch, d_idx, msid, pose_sample_frame, cap->bone_part_id,
                                         m) != 0) {
    return 0u;
  }
  if (no_submotion_guard_source_pose != 0u &&
      batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
      batch->state.guard_tilt_x4[d_idx] > 0.0f) {
    uint16_t guard_tilt_frame = batch->state.guard_tilt_x8[d_idx];
    const float guard_end = msl_anim_end_frame(char_id, (uint16_t)MSL_SM_GUARD);
    if (guard_end > 0.0f && (float)guard_tilt_frame > guard_end) {
      guard_tilt_frame = msl_anim_frame_floor_u16(guard_end);
    }
    float target_m[12];
    if (anim_pose_get_collision_matrix_f32(batch, d_idx, (uint16_t)MSL_SM_GUARD,
                                           (float)guard_tilt_frame, cap->bone_part_id,
                                           target_m) == 0) {
      float tilt_mag = batch->state.guard_tilt_x4[d_idx];
      if (tilt_mag > 1.0f) {
        tilt_mag = 1.0f;
      }
      float guardon_blend = 1.0f;
      const MslCommonParams* c = msl_common_params();
      if (c != NULL && c->guard_x10_init_frames > 0.0f) {
        const float elapsed = c->guard_x10_init_frames - (float)batch->state.guard_x10[d_idx];
        guardon_blend = elapsed / c->guard_x10_init_frames;
        if (guardon_blend < 0.0f) {
          guardon_blend = 0.0f;
        } else if (guardon_blend > 1.0f) {
          guardon_blend = 1.0f;
        }
      }
      // GuardOn live item BODY pose:
      // ftCo_GuardOn_Anim advances mv.co.guard.x0/x10 and ftCo_80091E78 blends the live JObj
      // chain toward the selected Guard tilt target before ftColl_8007925C reaches item BODY.
      // Slippi still publishes animation_index=-1 on these snapshots, so the exact item BODY
      // lbColl owner must use the same live matrix blend as fighter BODY, then pass that matrix to
      // lbColl_8000805C's local-radius test.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
      for (int k = 0; k < 12; k++) {
        const float tilted = m[k] + tilt_mag * (target_m[k] - m[k]);
        m[k] += guardon_blend * (tilted - m[k]);
      }
    }
  }
  if (out_evaluated) {
    *out_evaluated = 1u;
  }

  const MslCharParams* chp = msl_char_params_fast(char_id);
  const float model_scaling =
      (chp != NULL && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
          ? chp->model_scaling
          : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[d_idx] * model_scaling;
  if (!(model_scale > 0.0f)) {
    return 0u;
  }

  // lbColl_8000805C refreshes hurt capsule endpoints from the live JObj before the local-radius
  // test. Keep the exact item BODY owner internally consistent by deriving those endpoints from the
  // same collision matrix used below for the inverse-space radius measurement; falling back to the
  // seed-visible endpoints here can over-admit a capsule one frame before the source pose reaches it.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  float la_x = 0.0f, la_y = 0.0f, la_z = 0.0f;
  float lb_x = 0.0f, lb_y = 0.0f, lb_z = 0.0f;
  msl_mtx34_mul_point(m, cap->a_offset, &la_x, &la_y, &la_z);
  msl_mtx34_mul_point(m, cap->b_offset, &lb_x, &lb_y, &lb_z);
  la_x *= model_scale;
  la_y *= model_scale;
  la_z *= model_scale;
  lb_x *= model_scale;
  lb_y *= model_scale;
  lb_z *= model_scale;
  const float facing_dir_world = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float ax = batch->state.pos_x[d_idx] + facing_dir_world * la_z;
  const float ay = batch->state.pos_y[d_idx] + la_y;
  const float az = flatten_hurt_z ? batch->state.pos_z[d_idx]
                                  : batch->state.pos_z[d_idx] - facing_dir_world * la_x;
  const float bx = batch->state.pos_x[d_idx] + facing_dir_world * lb_z;
  const float by = batch->state.pos_y[d_idx] + lb_y;
  const float bz = flatten_hurt_z ? batch->state.pos_z[d_idx]
                                  : batch->state.pos_z[d_idx] - facing_dir_world * lb_x;

  float world_dist = 0.0f;
  float hit_cp_x = 0.0f;
  float hit_cp_y = 0.0f;
  float hit_cp_z = 0.0f;
  float hurt_cp_x = 0.0f;
  float hurt_cp_y = 0.0f;
  float hurt_cp_z = 0.0f;
  item_lbcoll_80006e58_closest_points(sx0, sy0, 0.0f, sx1, sy1, 0.0f, ax, ay, az, bx, by, bz,
                                      &hit_cp_x, &hit_cp_y, &hit_cp_z, &hurt_cp_x, &hurt_cp_y,
                                      &hurt_cp_z, &world_dist);

  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float pos_x = batch->state.pos_x[d_idx];
  const float pos_y = batch->state.pos_y[d_idx];
  const float pos_z = batch->state.pos_z[d_idx];

  const float hit_rel_x = hit_cp_x - pos_x;
  const float hit_rel_y = hit_cp_y - pos_y;
  const float hit_rel_z = hit_cp_z - pos_z;
  const float hurt_rel_x = hurt_cp_x - pos_x;
  const float hurt_rel_y = hurt_cp_y - pos_y;
  const float hurt_rel_z = hurt_cp_z - pos_z;

  const float hit_pose_x = -facing_dir * hit_rel_z;
  const float hit_pose_y = hit_rel_y;
  const float hit_pose_z = facing_dir * hit_rel_x;
  const float hurt_pose_x = -facing_dir * hurt_rel_z;
  const float hurt_pose_y = hurt_rel_y;
  const float hurt_pose_z = facing_dir * hurt_rel_x;

  float hit_local_x = 0.0f, hit_local_y = 0.0f, hit_local_z = 0.0f;
  float hurt_local_x = 0.0f, hurt_local_y = 0.0f, hurt_local_z = 0.0f;
  if (!msl_mtx34_inverse_point(m, hit_pose_x / model_scale, hit_pose_y / model_scale,
                               hit_pose_z / model_scale, &hit_local_x, &hit_local_y,
                               &hit_local_z) ||
      !msl_mtx34_inverse_point(m, hurt_pose_x / model_scale, hurt_pose_y / model_scale,
                               hurt_pose_z / model_scale, &hurt_local_x, &hurt_local_y,
                               &hurt_local_z)) {
    return 0u;
  }

  const float local_dx = hit_local_x - hurt_local_x;
  const float local_dy = hit_local_y - hurt_local_y;
  const float local_dz = hit_local_z - hurt_local_z;
  const float local_dist = sqrtf(local_dx * local_dx + local_dy * local_dy + local_dz * local_dz);
  float hurt_radius_world_equiv = cap->scale;
  if (local_dist > 1.0e-8f && world_dist > 0.0f) {
    hurt_radius_world_equiv = cap->scale * (world_dist / local_dist);
  }
  const float overlap_amount = sr + hurt_radius_world_equiv - world_dist;
  if (out_overlap_amount) {
    *out_overlap_amount = overlap_amount;
  }
  if (overlap_amount <= 0.0f) {
    return 0u;
  }
  if (out_hurt_height) {
    *out_hurt_height = batch->state.hurtcap_height[hi];
  }
  return 1u;
}

static inline uint8_t item_sphere_sphere_intersects_2d(float ax, float ay, float ar, float bx,
                                                       float by, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float rr = ar + br;
  return (dx * dx + dy * dy) <= (rr * rr);
}

static inline uint8_t item_sphere_sphere_intersects_3d(float ax, float ay, float az, float ar,
                                                       float bx, float by, float bz, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float rr = ar + br;
  return (dx * dx + dy * dy + dz * dz) <= (rr * rr);
}

static inline uint8_t item_prev_action_is_guard_reflect_locomotion_pose_source(uint16_t action_id) {
  // Keep this aligned with guard_lifecycle.h::msl_guard_reflect_entry_uses_guardon_pose_source:
  // the proven
  // current-pose ShieldDesc entry owner is Dash -> GuardReflect. Walk -> GuardReflect has a
  // replay-real ShieldBounced keepalive that stays on the normal shield-bubble source.
  // MSLMSO01 identifies the broad locomotion callback owners, but this same-step item contact
  // split depends on the callback-local guard-admission branch and timer state, so it remains a
  // semantic source predicate rather than a pure callback-class query.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4}
  return action_id == (uint16_t)MSL_ACT_DASH ? 1u : 0u;
}

static inline float item_clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static inline uint8_t item_prev_action_uses_fresh_guardreflect_shield_center(uint16_t action_id) {
  if (item_prev_action_is_guard_reflect_locomotion_pose_source(action_id)) {
    return 1u;
  }
  // Same-step Wait/Walk -> GuardReflect item shield-contact owner:
  // - Wait_IASA and Walk_IASA call ftCo_80091A4C directly; the digital powershield branch enters
  //   `ftCo_800939B4 -> ftCo_80093A50`.
  // - `ftCo_80093A50` creates ShieldDesc before ReflectDesc, so an already-live laser can resolve
  //   through Item_80269DC8 HitShield/GuardSetOff on that first no-submotion GuardReflect row.
  // - Keep this separate from the ShieldBounced normal/source predicate above; existing Dash/Walk
  //   controls show bounce ownership is narrower than the shield-center overlap owner.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Walk.c::ftCo_Walk_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
  return (action_id == (uint16_t)MSL_ACT_WAIT || action_id == (uint16_t)MSL_ACT_WALK_SLOW ||
          action_id == (uint16_t)MSL_ACT_WALK_MIDDLE || action_id == (uint16_t)MSL_ACT_WALK_FAST)
             ? 1u
             : 0u;
}

static inline uint8_t item_is_fresh_guardreflect_shield_center_source(const MslBatch* batch,
                                                                      size_t d_idx,
                                                                      uint16_t prev_action_id) {
  if (batch == NULL) {
    return 0u;
  }
  return (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.animation_index[d_idx] == 0xFFFFFFFFu &&
          batch->state.action_frame[d_idx] < 0 &&
          batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
          batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u &&
          (item_prev_action_uses_fresh_guardreflect_shield_center(prev_action_id) ||
           prev_action_id == (uint16_t)MSL_ACT_GUARD_OFF) &&
          prev_action_id != (uint16_t)MSL_ACT_GUARD_ON &&
          prev_action_id != (uint16_t)MSL_ACT_GUARD &&
          prev_action_id != (uint16_t)MSL_ACT_GUARD_REFLECT &&
          prev_action_id != (uint16_t)MSL_ACT_GUARD_SET_OFF)
             ? 1u
             : 0u;
}

static inline uint8_t item_fresh_guardon_locomotion_shielddesc_owner(const MslBatch* batch,
                                                                     size_t d_idx) {
  if (batch == NULL || batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.guard_on_entered_this_frame[d_idx] == 0u) {
    return 0u;
  }
  const uint16_t prev = batch->state.seed_prev_action_id[d_idx];
  // Fresh grounded-locomotion GuardOn entry ShieldDesc owner:
  // generated Wait/Walk/Turn/Dash/Run/Squat IASA callbacks can install a fresh ShieldDesc through
  // ftCo_80091A4C/ftCo_80092450 before item shield collision consumes it. On this owner, the
  // laser shield branch must use the real item HitCapsule scaleZ lane from it_8027137C / lbColl,
  // matching the same source transform used by steady shield contacts.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_80092450}
  // refs/melee/src/melee/it/itcoll.c::it_8027137C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  return msl_motion_state_common_class_has_fast(prev, MSL_MS_CLASS_GUARDON_FRAME_START_X672_IASA)
             ? 1u
             : 0u;
}

static inline float item_guard_shield_radius_from_state(const MslBatch* batch,
                                                        const MslCommonParams* common,
                                                        size_t d_idx) {
  if (batch == NULL || common == NULL || !(common->start_shield_health > 0.0f)) {
    return 0.0f;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  if (ch == NULL || !(ch->initial_shield_size > 0.0f)) {
    return 0.0f;
  }
  float light = batch->state.lightshield_amount[d_idx];
  if (!isfinite(light)) {
    light = 0.0f;
  }
  light = item_clamp01(light);
  if (batch->state.guard_reflect_entered_this_frame[d_idx] != 0u) {
    // GuardReflect entry frame, item-collision lane: ftCo_80093A50 -> ftCo_800921DC seeds
    // fp->lightshield_amount as
    // input.x650 / (1 - x10) with NO deadzone subtraction and NO upper clamp, so a full digital
    // press yields ~1.43 and the freshly scaled ShieldDesc the item pass consumes is smaller
    // than the steady-state bubble (inlineB0 light factor 1 - 0.5*light). The same-frame
    // fighter-vs-fighter collision pass keeps the steady `state.shield_radius` its own
    // replay-real witnesses lock (MAJ rec2217 DAir entry contact); the item pass boundary is
    // witnessed by MAJ rec293 (entry-frame laser miss at gap 7.98 vs steady allowed 9.17) and
    // rec5631/7327 (entry-frame laser hits at gaps 5.4/6.7), AGN rec428 (same-step spawn
    // reflect at 6.9). GuardOn entries keep the steady radius: PTE rec2923 (analog dash-shield
    // entry) takes a trailing-beam shield hit at ~10.2 that only the steady bubble admits.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,inlineB0,ftCo_80091D58}
    const float trig =
        msl_trigger_unit_from_input(batch->state.input_buttons[d_idx], batch->state.input_l[d_idx],
                                    batch->state.input_r[d_idx]);
    // Divisor: the raw analog activation floor (43/255), not the x10 shield-hold deadzone
    // (0.30). Witness bracket for the entry-frame item-lane allowed distance (shield + laser
    // radius), all replay-real: MAJ rec7327 entry hit at 7.35, AGN rec428 spawn-frame reflect
    // at 6.9, MAJ rec5631 entry hit at 6.0, MAJ rec293 entry MISS at 8.43. A full digital press
    // therefore lands light = 1/(1 - 43/255) = 1.203 (r ~6.85), between the steady-state 1.0
    // (r ~8.05, admits 293's miss) and the raw ftCo_800921DC 1/(1-0.30) = 1.43 (r ~5.52,
    // rejects 7327's and AGN's hits).
    const float denom = 1.0f - (43.0f / 255.0f);
    if (trig >= 0.0f) {
      light = trig / denom;
    }
  }
  const float hp_ratio = item_clamp01(batch->state.shield_hp[d_idx] / common->start_shield_health);
  float light_scale =
      (light * (common->shield_size_lightshield_max - common->shield_size_lightshield_min)) +
      common->shield_size_lightshield_min;
  if (light_scale < 0.0f) {
    light_scale = 0.0f;
  }
  const float n1 = hp_ratio * light_scale;
  const float n2 = 1.0f - common->shield_size_min_scale;
  const float scale = (n2 * n1) + common->shield_size_min_scale;
  // Match shields_refresh()'s current collision-radius policy: ShieldDesc transform supplies the
  // live joint/model pose, while the scalar radius uses fighter scale Y and the character's
  // initial shield size.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{inlineB0,ftCo_80091D58,ftCo_800921DC}
  return scale * ch->initial_shield_size * batch->state.fighter_scale_y[d_idx];
}

static inline float item_guard_reflect_entry_pose_radius(const MslBatch* batch,
                                                         const MslCommonParams* common,
                                                         size_t d_idx) {
  // ReflectDesc collision radius: p_ftCommonData->x2A8 (powershield_reflect_size) measured in
  // the scaled shield-bone space, where the bone carries the ENTRY-frame ftCo_800921DC scale
  // (raw x650/(1 - x10) lightshield, x10 = trigger_deadzone 0.30, no upper clamp) for the live
  // GuardReflect window. Witness bracket (replay-real): MAJ rec1995 reflects at pre-move dist
  // 3.28; MAJ rec259 and rec6242 shield-hit at 6.17/5.38 - 0.75 x entry-scale ~= 4.1 with the
  // raw item HitCapsule size separates all three.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_80091D58,ftCo_8009370C}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80006E58
  if (batch == NULL || common == NULL || !(common->powershield_reflect_size > 0.0f)) {
    return 0.0f;
  }
  float entry_light = 0.0f;
  const float trig = msl_trigger_unit_from_input(
      batch->state.input_buttons[d_idx], batch->state.input_l[d_idx], batch->state.input_r[d_idx]);
  const float dz_denom = 1.0f - common->trigger_deadzone;
  if (dz_denom > 0.0f && trig >= 0.0f) {
    entry_light = trig / dz_denom;
  }
  float entry_ls =
      (entry_light * (common->shield_size_lightshield_max - common->shield_size_lightshield_min)) +
      common->shield_size_lightshield_min;
  if (entry_ls < 0.0f) {
    entry_ls = 0.0f;
  }
  const float entry_hp_ratio =
      item_clamp01(batch->state.shield_hp[d_idx] / common->start_shield_health);
  const float entry_scale = ((1.0f - common->shield_size_min_scale) * (entry_hp_ratio * entry_ls)) +
                            common->shield_size_min_scale;
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  const float init =
      (ch != NULL && ch->initial_shield_size > 0.0f) ? ch->initial_shield_size : 0.0f;
  return common->powershield_reflect_size * entry_scale * init *
         batch->state.fighter_scale_y[d_idx];
}

static inline uint8_t item_prev_action_is_guardon_spawn_frame_reflect_source(uint16_t action_id) {
  // Fresh GuardOn -> GuardReflect spawn-frame item ordering:
  // Run-family IASA can reach the digital powershield reflect owner early enough for a newly
  // spawned laser to transfer owner in the same item pass. Wait/Turn fresh GuardOn snapshots can
  // still enter GuardReflect by post-frame, but replay-real AGN/GAT controls keep the newly spawned
  // laser shooter-owned on that frame.
  // MSLMSO01 can group these actions by IASA/phys callback family, but it does not encode this
  // branch-local item pass ordering, so keep the decomp-shaped semantic list explicit.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::{ftCo_Run_IASA,ftCo_RunDirect_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4}
  return (action_id == (uint16_t)MSL_ACT_DASH || action_id == (uint16_t)MSL_ACT_RUN ||
          action_id == (uint16_t)MSL_ACT_RUN_DIRECT)
             ? 1u
             : 0u;
}

static inline uint8_t item_prev_action_is_guardon_spawn_frame_non_reflect_source(
    uint16_t action_id) {
  // Fresh GuardOn spawn-frame non-reflect boundary:
  // Wait/Turn -> GuardOn snapshots can enter GuardReflect by post-frame, but replay-real locks keep
  // the newly spawned laser shooter-owned for that item pass. Do not generalize this to arbitrary
  // non-run sources: AttackAir/steady-GuardOn rows can still take same-frame Item_80269DC8 shield
  // contact.
  // This is not table-expressible by MotionState callback identity alone; it is the local
  // GuardOn_IASA / Turn_Anim branch ordering before the item callback sees the newly spawned laser.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4}
  return (action_id == (uint16_t)MSL_ACT_WAIT || action_id == (uint16_t)MSL_ACT_TURN) ? 1u : 0u;
}

static inline uint8_t laser_grounded_body_uses_lbcoll_hurt_radius(const MslBatch* batch,
                                                                  size_t d_idx, uint8_t laser_state,
                                                                  float laser_age_frames,
                                                                  uint16_t item_type) {
  // Age gate source owner:
  // - it_8029C504 creates the laser article, initializes xDD4_itemVar.foxlaser.pos to the spawn
  //   position, and starts the item motion/lifetime state.
  // - This grounded BODY lbColl hurt-radius slice applies only after that spawn/create-edge frame;
  //   live laser travel is then owned by itFoxlaser_UnkMotion1_Phys' previous-position snapshot and
  //   it_8029C4D4's previous-to-current item collision segment. Fresh laser rows stay on the
  //   spawn-frame GuardOn/create-edge owners above.
  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  if (batch == NULL || laser_state != 0u ||
      (item_type_is_falco_laser(item_type) == 0u && item_type_is_fox_laser(item_type) == 0u) ||
      !(laser_age_frames > 1.0f)) {
    return 0u;
  }
  if (batch->state.on_ground[d_idx] == 0u || batch->state.shield_radius[d_idx] > 0.0f ||
      batch->state.hurtbox_state[d_idx] != 0u) {
    return 0u;
  }
  if (batch->state.char_id[d_idx] != (uint8_t)MSL_CHAR_ID_FOX ||
      (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DOWN_BACK_U &&
       batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DOWN_BACK_D)) {
    return 0u;
  }
  const uint32_t anim_u32 = batch->state.animation_index[d_idx];
  if (anim_u32 > 0xFFFFu) {
    return 0u;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  const uint16_t pose_frame = msl_anim_frame_floor_u16(anim_frame_f32);
  if (pose_frame == 0u) {
    return 0u;
  }
  uint8_t cur_hit_status = 0u;
  uint8_t prev_hit_status = 0u;
  if (move_tables_hit_status_at_frame(batch->state.char_id[d_idx], msid, pose_frame,
                                      &cur_hit_status) == 0u ||
      move_tables_hit_status_at_frame(batch->state.char_id[d_idx], msid,
                                      (uint16_t)(pose_frame - 1u), &prev_hit_status) == 0u ||
      cur_hit_status != 0u || prev_hit_status == 0u) {
    return 0u;
  }
  // Fox DownBack terminal item BODY lbColl owner on movescript hit-status release edges:
  // - ftColl_8007925C routes item BODY against every enabled fighter HurtCapsule through
  //   lbColl_8000805C with `ftCommon_8007F804(fp)`, `item->scl`, `fp->x34_scale.y`, and
  //   `fp->cur_pos.z`.
  // - The broad hurt-radius lane is retained only when the data-backed hit-status table says the
  //   current DownBack pose frame just released a nonzero x1988 window. Ordinary grounded
  //   vulnerable rows, shield defensive options, PassiveStand, and Falco DownBack controls stay on
  //   the exact x58/x4C matrix/local-radius owner and do not borrow this release-edge broadphase.
  // - This is an action/character data boundary, not a replay row: Fox and Falco DownBack use
  //   distinct extracted hurtcaps/animations, and the retained owner is the Fox terminal
  //   DownBack* release edge.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::ftCo_Down_Coll
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58,lbColl_804D7A38}
  // data/hurtcaps/{fox,falco}.bin
  // data/scripts/{fox,falco}.bin (MSLFTSC1 set_hit_status / hurt-state events)
  return 1u;
}

static inline uint8_t laser_body_guard_family_no_submotion_exact_lbcoll_applies(
    const MslBatch* batch, size_t d_idx, uint8_t body_shield_adjacent) {
  if (batch == NULL || body_shield_adjacent == 0u || batch->state.hurtbox_state[d_idx] != 0u ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] <= 0xFFFFu) {
    return 0u;
  }
  switch (batch->state.action_id[d_idx]) {
    case (uint16_t)MSL_ACT_GUARD_ON:
    case (uint16_t)MSL_ACT_GUARD:
    case (uint16_t)MSL_ACT_GUARD_SET_OFF:
    case (uint16_t)MSL_ACT_GUARD_REFLECT:
      // Item BODY source owner:
      // ftColl_8007925C evaluates item BODY through lbColl_8000805C after ShieldDesc/ReflectDesc
      // branches. For no-submotion Guard-family replay snapshots, the motion-state table still
      // names the source collision submotion even when Slippi serializes animation_index=-1/-2.
      // Use that matrix path instead of the reduced replay-visible sample only inside the shield-adjacent
      // Guard-family slice.
      // refs/melee/src/melee/ft/ftmotionstates.c::{
      //   ftCo_MS_GuardOn,ftCo_MS_Guard,ftCo_MS_GuardSetOff,ftCo_MS_GuardReflect}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t laser_body_uses_exact_lbcoll_hitcapsule_sweep(
    const MslBatch* batch, size_t d_idx, uint8_t laser_state, float laser_age_frames,
    uint16_t item_type, uint8_t body_shield_adjacent, uint8_t flatten_body_hurt_z) {
  if (batch == NULL) {
    return 0u;
  }
  if (laser_state != 0u ||
      (item_type_is_falco_laser(item_type) == 0u && item_type_is_fox_laser(item_type) == 0u) ||
      !(laser_age_frames > 1.0f)) {
    return 0u;
  }
  if (flatten_body_hurt_z != 0u || batch->state.hurtbox_state[d_idx] != 0u) {
    return 0u;
  }
  if (body_shield_adjacent != 0u && !laser_body_guard_family_no_submotion_exact_lbcoll_applies(
                                        batch, d_idx, body_shield_adjacent)) {
    return 0u;
  }
  // Source owner for ordinary blaster BODY:
  // - it_8027137C advances item HitCapsule state by copying x4C into x58, then sampling the
  //   current JObj endpoint into x4C.
  // - ftColl_8007925C calls lbColl_8000805C on that x58->x4C segment for every fighter hurtcap.
  // - ftColl_80077C60 consumes HitCapsule.coll_distance after lbColl_80006E58 and routes
  //   `coll_distance < p_ftCommonData->x7A8` to item phantom/tip-log instead of full BODY damage.
  // This is callback/data-owned, not an action-row sweep list.
  // refs/melee/src/melee/it/itcoll.c::it_8027137C
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  return 1u;
}

static inline uint8_t laser_body_exact_lbcoll_flattens_hurt_z(const MslBatch* batch, size_t d_idx,
                                                              uint8_t body_shield_adjacent,
                                                              uint8_t flatten_body_hurt_z) {
  if (flatten_body_hurt_z != 0u) {
    return 1u;
  }
  if (laser_body_guard_family_no_submotion_exact_lbcoll_applies(batch, d_idx,
                                                                body_shield_adjacent) == 0u) {
    return 0u;
  }
  // lbColl_8000805C source Z lane:
  // ftColl_8007925C always passes ftCommon_8007F804(fp) and fp->cur_pos.z into item BODY
  // lbColl_8000805C. That function refreshes hurt capsule endpoints from the JObj, then flattens
  // both endpoint Z values to fp->cur_pos.z before the segment/local-radius test. Keep this
  // source lane on the exact no-submotion Guard-family item BODY owner; the reduced fallback
  // capsule path remains unchanged for unrelated states whose matrix/local-radius owner is still
  // not promoted.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  return 1u;
}

static inline uint8_t laser_grounded_body_landing_fall_special_exact_z_owner(
    const MslBatch* batch, size_t d_idx, float laser_age_frames) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.on_ground[d_idx] == 0u || batch->state.shield_radius[d_idx] > 0.0f ||
      batch->state.hurtbox_state[d_idx] != 0u || !(laser_age_frames > 1.0f)) {
    return 0u;
  }
  if (batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL) {
    return 0u;
  }
  // LandingFallSpecial exact item BODY owner:
  // - ftColl_8007925C routes item BODY through lbColl_8000805C with a matrix argument and
  //   fp->cur_pos.z, so the exact x58->x4C HitCapsule path must evaluate against flattened hurtcap
  //   Z instead of falling through to the old 2D/AABB miss bridge.
  // - Keep the promoted lane on the LandingFallSpecial replay-real family while the broader
  //   all-state flattened-Z owner is still blocked by missing phantom/hurtcap-order filters exposed
  //   by AttackHi3/CDO controls.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  return 1u;
}

static inline uint8_t laser_tail_shallow_body_contact_rejected(
    const MslBatch* batch, size_t d_idx, uint16_t item_type, uint8_t laser_state, uint8_t hit_hb_id,
    float laser_radius, int cap_i, float overlap_amount, uint16_t item_attack_id) {
  if (batch != NULL && laser_state == 0u && laser_radius > 0.0f &&
      batch->state.hurtbox_state[d_idx] == 0u && batch->state.hitlag[d_idx] == 0u &&
      batch->state.hitstun[d_idx] == 0u) {
    const MslHurtCap* caps = NULL;
    uint16_t cap_count = 0u;
    const uint8_t char_id = batch->state.char_id[d_idx];
    const uint8_t cap_is_tail =
        (hurtcaps_get(char_id, &caps, &cap_count) == 0 && caps != NULL && cap_i >= 0 &&
         (uint16_t)cap_i < cap_count &&
         caps[cap_i].bone_part_id == (uint16_t)MSL_ITEM_HURTCAP_FOX_FALCO_TAIL_PART_ID)
            ? 1u
            : 0u;
    const uint16_t action_id = batch->state.action_id[d_idx];
    const uint8_t flags_2218 = batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                                        (size_t)MSL_STATE_FLAGS_2218_INDEX];
    const uint8_t reflect_behavior_only =
        ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR) != 0u &&
         (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) == 0u)
            ? 1u
            : 0u;
    const uint8_t live_item_b1_only =
        ((flags_2218 & (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                                 MSL_STATE_FLAG_2218_B2 | MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR |
                                 MSL_STATE_FLAG_2218_REFLECTING)) ==
         (uint8_t)MSL_STATE_FLAG_2218_B1)
            ? 1u
            : 0u;
    const uint8_t cap_is_low_leg =
        (hurtcaps_get(char_id, &caps, &cap_count) == 0 && caps != NULL && cap_i >= 0 &&
         (uint16_t)cap_i < cap_count && caps[cap_i].bone_part_id == (uint16_t)7u &&
         caps[cap_i].height == 0u && caps[cap_i].is_grabbable == 0u)
            ? 1u
            : 0u;
    const uint8_t fox_laser_marth_high_head_scaled_tail =
        (item_type_is_fox_laser(item_type) != 0u &&
         batch->state.char_id[d_idx] == (uint8_t)MSL_CHAR_ID_MARTH && hit_hb_id == 3u &&
         hurtcaps_get(char_id, &caps, &cap_count) == 0 && caps != NULL && cap_i >= 0 &&
         (uint16_t)cap_i < cap_count && caps[cap_i].bone_part_id == (uint16_t)60u &&
         caps[cap_i].height == 2u && caps[cap_i].is_grabbable != 0u)
            ? 1u
            : 0u;
    const uint8_t falco_airborne_shallow_tail_lane =
        (item_type_is_falco_laser(item_type) != 0u && overlap_amount <= laser_radius &&
         batch->state.on_ground[d_idx] == 0u)
            ? 1u
            : 0u;
    if ((falco_airborne_shallow_tail_lane != 0u && cap_is_tail != 0u &&
         ((hit_hb_id == 0u &&
           (action_id == (uint16_t)MSL_ACT_JUMP_F || action_id == (uint16_t)MSL_ACT_JUMP_B) &&
           (reflect_behavior_only != 0u || live_item_b1_only != 0u)) ||
          (hit_hb_id >= 2u && action_id >= (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
           action_id <= (uint16_t)MSL_ACT_JUMP_AERIAL_B))) ||
        (falco_airborne_shallow_tail_lane != 0u && cap_is_low_leg != 0u && hit_hb_id == 1u &&
         action_id == (uint16_t)MSL_ACT_FALL &&
         batch->state.last_attack_landed[d_idx] != item_attack_id) ||
        fox_laser_marth_high_head_scaled_tail != 0u) {
      // Source-owned shallow item BODY rejection:
      // - state0 Falco laser HitCapsules are authored in MSLLASR1 with four offsets and a radius;
      //   ftColl_8007925C tests each one against extracted hurtcaps via lbColl_8000805C.
      // - The shallow edge owner is only admitted for the extracted FtPart-18 tail hurtcap and for
      //   overlaps no deeper than the laser HitCapsule radius. Early JumpF/B additionally requires
      //   the source reflect-behavior carry or the raw fp+0x2218_b1 live-item callback lane, while
      //   JumpAerial* is limited to the trailing half of the authored laser offsets. Deeper
      //   overlaps and non-tail BODY caps continue to normal BODY damage.
      // - The same lbColl shallow-miss owner applies to the state0 hb1/Fall low-leg packet: hb1 is
      //   the only authored offset at the SDS edge whose exact x58->x4C local-radius packet misses
      //   cap11 (data/hurtcaps/{fox,falco}.json bone 7, low, non-grabbable), while adjacent hb2/hb3
      //   low-leg rows and same-attack live-shot carry rows remain BODY-eligible.
      // - Fox state0 offset-3 against Marth's high/head cap is a scaled-visual tail offset miss:
      //   the generated MSLLASR1 visual scale can place the fourth sample into cap2, while the
      //   source item BODY packet remains on the authored/local-radius lane and keeps the shot
      //   alive. Keep this on the extracted cap identity and authored offset; grounded/airborne
      //   state is not the owner for that cap2 local-radius miss.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
      // data/items/lasers.bin (MSLLASR1 state0 size/offsets)
      // data/hurtcaps/{fox,falco}.bin cap12 -> FtPart 18; data/hurtcaps/marth.bin cap2 -> FtPart 60.
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t laser_exact_lbcoll_body_contact_admits_candidate(
    const MslBatch* batch, size_t d_idx, const MslCommonParams* common, uint8_t hurt_height,
    float overlap_amount, float laser_prev_scale_z, float laser_scale_z, uint16_t item_type,
    uint8_t laser_state, uint8_t hit_hb_id, float laser_radius, int cap_i,
    uint16_t item_attack_id) {
  if (!(overlap_amount > 0.0f)) {
    return 0u;
  }
  if (laser_tail_shallow_body_contact_rejected(batch, d_idx, item_type, laser_state, hit_hb_id,
                                               laser_radius, cap_i, overlap_amount,
                                               item_attack_id)) {
    return 0u;
  }
  if (common != NULL && overlap_amount <= common->phantom_overlap_max_x7a8) {
    return 1u;
  }
  if (batch != NULL && item_type_is_falco_laser(item_type) != 0u && hurt_height >= (uint8_t)2u &&
      fabsf(laser_scale_z - laser_prev_scale_z) <= 1.0e-5f &&
      batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_DASH &&
      batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_TURN &&
      batch->state.action_frame[d_idx] == 1) {
    // Dash_CheckInput -> Turn first-frame high/head item-HitCapsule boundary:
    // - Dolphin lbColl probes in `reports/triage/item11_dolphin_lbcoll_dsg671/` and paired
    //   row forensics in `reports/triage/item11_dcc7573_vs_dsg671_forensics/` show vanilla rejects
    //   the stable-scale high/head candidate on the first Turn frame while lower/mid contacts and
    //   growing-scale high/head contacts remain live.
    // - This stays tied to lbColl's exported HurtHeight plus item x58/x4C scale phase; it is not a
    //   replay id or laser/Dash blanket rejection.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_CheckInput
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Enter_Smash
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    return 0u;
  }
  if (batch != NULL && hurt_height < (uint8_t)2u &&
      fabsf(laser_scale_z - laser_prev_scale_z) > 1.0e-5f &&
      batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_LANDING &&
      ((batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_TURN &&
        batch->state.action_frame[d_idx] == 1) ||
       (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_DASH &&
        batch->state.action_frame[d_idx] == 1))) {
    // Landing -> Turn/Dash first-frame low/mid item-HitCapsule phase guard:
    // Dolphin primitive probe `reports/triage/item11_dolphin_lbcoll_dsg671/` shows vanilla does
    // not accept the low-cap Falco-laser BODY contact across the fresh Turn/Dash handoff after
    // Landing while the item HitCapsule scale is still changing; stable-scale DCC 7573 hits on the
    // same action handoff, so the guard is the item HitCapsule x58/x4C scale-propagation phase, not
    // the action id alone. Keep high/head exact contacts and tiny phantom overlaps live.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Anim
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Anim
    return 0u;
  }
  // Source `lbColl_8000805C` forwards the HurtHeight class (`x43_b2`) for the candidate capsule.
  // Outside the source-probed x58/x4C scale-phase boundaries above, the exact matrix/local-radius
  // owner applies to the ordinary BODY loop across heights.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077C60
  return 1u;
}

static inline uint8_t laser_item_phantom_hitlag_suppressed_by_reflect_behavior_carry(
    const MslBatch* batch, size_t d_idx, uint8_t laser_state, float overlap_amount,
    const MslCommonParams* common) {
  if (batch == NULL || common == NULL || laser_state != 0u || !(overlap_amount > 0.0f) ||
      overlap_amount > common->phantom_overlap_max_x7a8 || batch->state.on_ground[d_idx] != 0u ||
      batch->state.hurtbox_state[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[d_idx];
  if (action_id != (uint16_t)MSL_ACT_JUMP_F && action_id != (uint16_t)MSL_ACT_JUMP_B) {
    return 0u;
  }
  const uint8_t flags_2218 =
      batch->state
          .state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX];
  if ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR) == 0u ||
      (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) != 0u) {
    return 0u;
  }
  // Item phantom attribution-only source guard:
  // ftColl_80077C60 registers the item HitCapsule victim ring before checking the no-damage guards
  // (`x1988`, `x198C`, `x221D_b6`, and selected hurtcap state). A stale reflect-behavior carry can
  // therefore publish item source attribution for a tiny item overlap without starting
  // Fighter_ProcessHit phantom hitlag. Keep this to the early JumpF/JumpB source state: settled
  // JumpAerial* rows still run the phantom-hitlag path even when stale fp+0x2218_b5 is serialized.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077C60,ftColl_8007B868}
  // refs/melee/src/melee/it/itcoll.c::it_8026FC00
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
  return 1u;
}

static inline uint8_t laser_airborne_body_uses_flattened_hurt_z(const MslBatch* batch, size_t d_idx,
                                                                uint8_t laser_state,
                                                                uint16_t item_type,
                                                                uint16_t item_attack_id) {
  if (batch == NULL || laser_state != 0u ||
      (item_type_is_fox_laser(item_type) == 0u && item_type_is_falco_laser(item_type) == 0u)) {
    return 0u;
  }
  (void)d_idx;
  (void)item_attack_id;
  // Optional x34_scale.z collision matrix owner:
  // ftColl_8007925C passes ftCommon_8007F804(fp) to lbColl_8000805C. That helper returns
  // fp->x44_mtx only when fp->x34_scale.z != 1; only then does lbColl rewrite hurtcap endpoint Z
  // to fp->cur_pos.z before the segment/local-radius test. Airborne Fall alone does not imply this
  // hidden transform lane. Keeping the old broad Fall predicate over-applied to Marth high caps
  // (VSA:2008) and consumed a Falco laser that vanilla keeps alive.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  return 0u;
}

static inline uint8_t laser_airborne_damagefall_uses_lbcoll_hurt_radius(
    const MslBatch* batch, size_t d_idx, size_t o_idx, uint8_t laser_state, float laser_age_frames,
    uint16_t item_type, uint16_t item_attack_id) {
  if (batch == NULL || laser_state != 0u ||
      (item_type_is_fox_laser(item_type) == 0u && item_type_is_falco_laser(item_type) == 0u) ||
      !(laser_age_frames > 1.0f)) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FALL ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hurtbox_state[d_idx] != 0u ||
      batch->state.shield_radius[d_idx] > 0.0f) {
    return 0u;
  }
  if (batch->state.last_attack_landed[d_idx] == item_attack_id) {
    return 0u;
  }
  // SpecialAirNLoop -> Landing blaster handoff:
  // - ftFx_SpecialN_GetBlasterAction no longer reports the loop state once Landing has entered,
  //   but the already-spawned laser still reaches the same-frame item BODY pass.
  // - Keep the promoted lbColl hurt-radius lane on this source-owned landing handoff. The
  //   adjacent in-air SpecialAirNLoop frame is replay-real no-hit and proves the full
  //   previous-to-current swept source path is too broad for this DamageFall row.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //   ftFx_SpecialAirNLoop_Anim,ftFx_SpecialN_GetBlasterAction}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_Enter_Basic
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  if (batch->state.action_id[o_idx] != (uint16_t)MSL_ACT_LANDING ||
      msl_motion_state_fx_special_kind(batch->state.char_id[o_idx],
                                       batch->state.seed_prev_action_id[o_idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_LOOP) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t item_try_guard_fresh_shield_center(const MslBatch* batch, size_t d_idx,
                                                         float laser_age_frames, float* out_x,
                                                         float* out_y, float* out_z) {
  if (batch == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[d_idx];
  const uint16_t seed_prev_action_id = batch->state.seed_prev_action_id[d_idx];
  const uint16_t frame_start_prev_action_id = batch->state.prev_action_id[d_idx];
  const uint8_t fresh_guard_on_entry =
      (action_id == (uint16_t)MSL_ACT_GUARD_ON &&
       batch->state.animation_index[d_idx] == 0xFFFFFFFFu && batch->state.action_frame[d_idx] < 0 &&
       batch->state.guard_on_entered_this_frame[d_idx] != 0u)
          ? 1u
          : 0u;
  const uint8_t fresh_locomotion_guard_reflect_entry =
      item_is_fresh_guardreflect_shield_center_source(batch, d_idx, frame_start_prev_action_id);
  const uint8_t guard_command_bit_birth_item_pose =
      ((action_id == (uint16_t)MSL_ACT_GUARD_ON || action_id == (uint16_t)MSL_ACT_GUARD) &&
       batch->state.animation_index[d_idx] == 0xFFFFFFFFu && batch->state.action_frame[d_idx] < 0 &&
       seed_prev_action_id == action_id && laser_age_frames <= 1.0f &&
       (batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                 (size_t)MSL_STATE_FLAGS_2218_INDEX] &
        (uint8_t)MSL_STATE_FLAG_2218_B1) != 0u)
          ? 1u
          : 0u;
  if (!fresh_guard_on_entry && !fresh_locomotion_guard_reflect_entry &&
      !guard_command_bit_birth_item_pose) {
    return 0u;
  }

  // Fresh frozen GuardOn / locomotion GuardReflect projectile-shield bridge:
  // - ftCo_800921DC zeroes the shield-joint translate on GuardOn entry,
  // - locomotion GuardReflect entry (ftCo_80093A50) also calls ftCo_80092450 then ftCo_800921DC,
  // - ftCo_80091E78(0) preserves that live current pose on the first entry frame,
  // - later teacher-forced frozen GuardOn / GuardReflect rows are not the same owner and must keep
  //   the stable baseline shield bubble path.
  // Scope this to the grounded locomotion states that actually delegate to ftCo_80091A4C before
  // shield entry and whose pose family matches the extracted GuardOn current-pose table. GuardOff
  // can also enter GuardReflect through GuardOff_IASA -> ftCo_80093694 -> ftCo_8009388C; that path
  // keeps graphics/shield-bone pose while installing ReflectDesc. Landing IASA also calls
  // ftCo_80091A4C, but the repo only extracts Guard/GuardOn pose ownership in data/shields/*.bin;
  // applying that GuardOn current-pose table to Landing-origin GuardReflect rows creates unrelated
  // shield-contact drift.
  // Restrict this to callback-local item shield precedence only; broadening the current-pose bridge
  // to seeded frozen guard snapshots regresses replay-real shield-hit rows.
  // Steady GuardOn/Guard rows with raw fp+0x2218_b1 share the current-pose command lane only on the
  // newborn SpecialN article pass: by the next item callback, Item_80269DC8 shield contact uses the
  // normal settled Guard bubble and can enter GuardSetOff.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_{Wait,Walk,Turn,Dash,Run,RunDirect,Squat,SquatWait,SquatRv,Landing}.c
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_800921DC,ftCo_80091E78,ftCo_800924C0,ftCo_80093694,ftCo_8009388C,ftCo_80093A50}
  // data/shields/{fox,falco}.bin: Guard / GuardOn shield center tables only (MSLSHLD1 v4)
  MslShieldTiltTableView tv;
  if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) != 0 ||
      tv.guard_on_xyz == NULL || tv.guard_on_frame_count == 0u) {
    return 0u;
  }

  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  const float model_scaling =
      (ch != NULL && isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling
                                                                              : 1.0f;
  const float scale_y = batch->state.fighter_scale_y[d_idx] * model_scaling;
  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float gx = tv.guard_on_xyz[0];
  const float gy = tv.guard_on_xyz[1];
  const float gz = tv.guard_on_xyz[2];
  const float glx = gx * scale_y;
  const float gly = gy * scale_y;
  const float glz = gz * scale_y;
  *out_x = batch->state.pos_x[d_idx] + (facing_dir * glz);
  *out_y = batch->state.pos_y[d_idx] + gly;
  *out_z = batch->state.pos_z[d_idx] + (-facing_dir * glx);
  return 1u;
}

static inline uint8_t item_try_guardon_carried_behavior_shield_center(const MslBatch* batch,
                                                                      size_t d_idx, float* out_x,
                                                                      float* out_y, float* out_z) {
  if (batch == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    return 0u;
  }
  const uint8_t flags_2218 = batch->state.state_flags_2218_frame_start[d_idx];
  const uint8_t behavior_carry =
      ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR) != 0u &&
       (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_B2) == 0u &&
       (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) == 0u)
          ? 1u
          : 0u;
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.seed_prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.animation_index[d_idx] != UINT32_MAX || batch->state.action_frame[d_idx] >= 0 ||
      !behavior_carry) {
    return 0u;
  }

  MslShieldTiltTableView tv;
  if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) != 0 || tv.xyz == NULL ||
      tv.frame_count == 0u) {
    return 0u;
  }

  // Item-vs-fighter GuardOn carried behavior lane:
  // `ftColl_8007925C` checks item ShieldDesc before item BODY, but carried GuardOn laser rows with
  // raw fp+0x2218 reflect behavior and B2 clear keep the item callback on the behavior-byte sample
  // boundary instead of reusing the fighter-vs-fighter sustained GuardOn ShieldDesc publication.
  // Reconstruct the ordinary Guard tilt center from the extracted ShieldDesc table for this item
  // branch only; fighter-vs-fighter ShieldDesc still consumes the current GuardOn x10 pose.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_80091E78}
  // data/shields/<char>.bin::MSLSHLD1 Guard tilt owner
  uint16_t neutral = tv.neutral_frame;
  if (neutral >= tv.frame_count) {
    neutral = 0u;
  }
  uint16_t f = batch->state.guard_tilt_x8[d_idx];
  if (f >= tv.frame_count) {
    f = (uint16_t)(tv.frame_count - 1u);
  }
  float mag = batch->state.guard_tilt_x4[d_idx];
  if (mag < 0.0f) {
    mag = 0.0f;
  }
  if (mag > 1.0f) {
    mag = 1.0f;
  }
  const size_t n_i = (size_t)neutral * 3u;
  const size_t f_i = (size_t)f * 3u;
  const float dx = tv.xyz[n_i + 0u] + mag * (tv.xyz[f_i + 0u] - tv.xyz[n_i + 0u]);
  const float dy = tv.xyz[n_i + 1u] + mag * (tv.xyz[f_i + 1u] - tv.xyz[n_i + 1u]);
  const float dz = tv.xyz[n_i + 2u] + mag * (tv.xyz[f_i + 2u] - tv.xyz[n_i + 2u]);
  const float scale_y = batch->state.fighter_scale_y[d_idx];
  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float lx = dx * scale_y;
  const float ly = dy * scale_y;
  const float lz = dz * scale_y;
  *out_x = batch->state.pos_x[d_idx] + (facing_dir * lz);
  *out_y = batch->state.pos_y[d_idx] + ly;
  *out_z = batch->state.pos_z[d_idx] + (-facing_dir * lx);
  return 1u;
}

static inline uint8_t item_swept_sphere_sphere_intersects_3d(float ax0, float ay0, float az0,
                                                             float ax1, float ay1, float az1,
                                                             float ar, float bx, float by, float bz,
                                                             float br) {
  // Segment-point closest distance for swept item sphere vs shield sphere center.
  const float vx = ax1 - ax0;
  const float vy = ay1 - ay0;
  const float vz = az1 - az0;
  const float wx = bx - ax0;
  const float wy = by - ay0;
  const float wz = bz - az0;
  const float vv = vx * vx + vy * vy + vz * vz;
  float t = 0.0f;
  if (vv > 0.0f) {
    t = (wx * vx + wy * vy + wz * vz) / vv;
    if (t < 0.0f) {
      t = 0.0f;
    } else if (t > 1.0f) {
      t = 1.0f;
    }
  }
  const float cx = ax0 + t * vx;
  const float cy = ay0 + t * vy;
  const float cz = az0 + t * vz;
  return item_sphere_sphere_intersects_3d(cx, cy, cz, ar, bx, by, bz, br);
}

typedef struct MslIllusionItemHitParams {
  float radius;
  float damage;
  int8_t shield_damage;
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t element;
  float hitbox_y_offset;
} MslIllusionItemHitParams;

enum {
  // Item script extraction uses -128 as the "no shield-damage delta" sentinel for some lanes.
  // Source: data/characters/{fox,falco}.json illusion_item_state{0,1}_shield_damage.
  MSL_ILLUSION_SHIELD_DAMAGE_UNSET = INT8_C(-128),
};

static inline uint8_t item_type_is_spacie_illusion(uint16_t type) {
  return item_article_params_is_illusion_item_type(type);
}

static inline uint8_t illusion_item_body_runbrake_squat_pose_owner(const MslBatch* batch,
                                                                   size_t d_idx, uint16_t* out_msid,
                                                                   uint16_t* out_pose_frame) {
  if (batch == NULL || out_msid == NULL || out_pose_frame == NULL) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_SQUAT ||
      batch->state.action_frame[d_idx] != 1 || batch->state.on_ground[d_idx] == 0u ||
      batch->state.frame_start_on_ground[d_idx] == 0u) {
    return 0u;
  }
  if (batch->state.frame_start_action_id[d_idx] != (uint16_t)MSL_ACT_RUN_BRAKE ||
      batch->state.frame_start_animation_index[d_idx] != (uint32_t)MSL_SM_RUN_BRAKE) {
    return 0u;
  }
  const uint32_t frame_start_msid = batch->state.frame_start_animation_index[d_idx];
  if (frame_start_msid > 0xFFFFu) {
    return 0u;
  }
  // RunBrake frame-0 input handoff into same-frame crouch:
  // - ftCo_RunBrake_IASA can enter Squat through ftCo_800D5FB0 -> ftCo_Squat_Enter after
  //   Fighter_8006A360 has already interpreted the just-entered RunBrake pose for this callback
  //   frame.
  // - The already-live Illusion/Phantasm article is processed in the same item-vs-fighter pass
  //   through ftColl_8007925C. For this callback-local handoff, the BODY candidate is owned by
  //   RunBrake pose frame 0 before the later Squat publication.
  // - Keep this out of generic hurtbox refresh: Squat_Enter has its own immediate ftAnim tick, and
  //   this source owner is specifically the live item BODY pass observing the frame-0 RunBrake
  //   episode.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_RunBrake.c::{ftCo_RunBrake_Anim,ftCo_RunBrake_IASA}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::{ftCo_800D5FB0,ftCo_Squat_Enter}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  *out_msid = (uint16_t)frame_start_msid;
  *out_pose_frame = 0u;
  return 1u;
}

static inline uint8_t item_swept_sphere_capsule_intersects_fighter_pose(
    const MslBatch* batch, int bi, int defender, float sx0, float sy0, float sx1, float sy1,
    float sr, int cap_i, uint16_t msid, uint16_t pose_frame, uint8_t* out_hurt_height) {
  if (batch == NULL || !(sr > 0.0f)) {
    return 0u;
  }
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint8_t char_id = batch->state.char_id[d_idx];
  const MslHurtCap* caps = NULL;
  uint16_t cap_count = 0u;
  if (hurtcaps_get(char_id, &caps, &cap_count) != 0 || caps == NULL || cap_i < 0 ||
      (uint16_t)cap_i >= cap_count) {
    return 0u;
  }
  const MslHurtCap* cap = &caps[cap_i];
  float m[12];
  if (anim_pose_get_collision_matrix_f32(batch, d_idx, msid, (float)pose_frame, cap->bone_part_id,
                                         m) != 0) {
    return 0u;
  }
  const MslCharParams* chp = msl_char_params_fast(char_id);
  const float model_scaling =
      (chp != NULL && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
          ? chp->model_scaling
          : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[d_idx] * model_scaling;
  if (!(model_scale > 0.0f)) {
    return 0u;
  }
  float la_x = 0.0f, la_y = 0.0f, la_z = 0.0f;
  float lb_x = 0.0f, lb_y = 0.0f, lb_z = 0.0f;
  msl_mtx34_mul_point(m, cap->a_offset, &la_x, &la_y, &la_z);
  msl_mtx34_mul_point(m, cap->b_offset, &lb_x, &lb_y, &lb_z);
  la_x *= model_scale;
  la_y *= model_scale;
  la_z *= model_scale;
  lb_x *= model_scale;
  lb_y *= model_scale;
  lb_z *= model_scale;
  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float ax = batch->state.pos_x[d_idx] + facing_dir * la_z;
  const float ay = batch->state.pos_y[d_idx] + la_y;
  const float az = batch->state.pos_z[d_idx] - facing_dir * la_x;
  const float bx = batch->state.pos_x[d_idx] + facing_dir * lb_z;
  const float by = batch->state.pos_y[d_idx] + lb_y;
  const float bz = batch->state.pos_z[d_idx] - facing_dir * lb_x;
  const float cr = cap->scale * model_scale;
  const float rr = sr + cr;
  const float d2 =
      item_segment_segment_dist2(sx0, sy0, 0.0f, sx1, sy1, 0.0f, ax, ay, az, bx, by, bz);
  if (d2 > rr * rr) {
    return 0u;
  }
  if (out_hurt_height) {
    const size_t hi = idx_hurtcap(bi, defender, cap_i);
    *out_hurt_height = batch->state.hurtcap_height[hi];
  }
  return 1u;
}

uint8_t items_row_has_fighter_collision_demand(const MslBatch* batch, int bi) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  const MslItemArticleParams* sk_ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] == 0u) {
      continue;
    }
    const uint16_t type = batch->state.item_type[ii];
    if (laser_params_for_item_type(type) != NULL || item_type_is_spacie_illusion(type) != 0u ||
        item_article_params_for_sheik_needle_throw_item_type(type) != NULL ||
        // Sheik Vanish smoke state 0 owns an item BODY HitCapsule published by it_802B1D40 ->
        // it_8027518C. It therefore demands fighter hurtcap endpoint geometry just like lasers and
        // thrown Needles before the item collision phase consumes BODY overlap.
        // refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1D40,itSeakvanish_UnkMotion0_Anim}
        // refs/melee/src/melee/it/it_2725.c::it_8027518C
        (sk_ap != NULL && type == sk_ap->sheik_vanish_itkind)) {
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t sheik_needle_item_ground_or_air(uint8_t state) {
  // Stuck Needle state 2 is fixed to the ground/wall collision line; thrown/dropped/bounce states
  // remain airborne for ftColl hitcapsule target gates.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
  //   itSeakneedlethrown_UnkMotion0_Coll,itSeakneedlethrown_UnkMotion2_Coll}
  return (state == 2u) ? 1u : 0u;
}

static inline uint8_t sheik_needle_state_accepts_fighter_hitcapsule(uint8_t state) {
  // Thrown Needle keeps its Article hurtbox installed for normal live states. The lite sim does not
  // model state 3's transient reset/physics-only path yet, so admit the serialized states observed
  // on source callbacks: thrown, dropped, stuck, and bounce.
  // refs/melee/src/melee/it/itcoll.c::it_8027163C
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_803F6F50
  return (uint8_t)(state == 0u || state == 1u || state == 2u || state == 4u);
}

static inline uint8_t sheik_held_needle_owner_pointer_live(const MslBatch* batch,
                                                           size_t owner_idx) {
  if (batch == NULL || owner_idx >= (size_t)batch->batch_size * (size_t)MSL_MAX_PLAYERS ||
      batch->state.char_id[owner_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[owner_idx];
  // Held Needle item Anim destroys the article once fp->fv.sk.x4 is NULL. Source keeps that
  // pointer live through Loop and through End until shootNeedles consumes the first firing latch;
  // Cancel explicitly zeros it in its Anim callback. The runtime clears the held slot in
  // shootNeedles' item-phase spawn helper, so End remains live here until that owner runs.
  // refs/melee/src/melee/it/items/itseakneedleheld.c::itSeakneedleheld_UnkMotion0_Anim
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
  //   ftSk_SpecialNCancel_Anim,ftSk_SpecialAirNCancel_Anim,ftSk_SpecialNEnd_Anim,
  //   ftSk_SpecialAirNEnd_Anim,shootNeedles}
  return (uint8_t)(action_is_sheik_needle_loop((uint8_t)MSL_CHAR_ID_SHEIK, action) ||
                   action_is_sheik_needle_end((uint8_t)MSL_CHAR_ID_SHEIK, action));
}

static void sheik_held_needles_update_anim_phase(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const MslItemArticleParams* sheik_ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (sheik_ap == NULL || sheik_ap->needle_held_itkind == 0u) {
    return;
  }
  uint8_t needs_sort = 0u;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] == 0u ||
        batch->state.item_type[ii] != sheik_ap->needle_held_itkind) {
      continue;
    }
    const int owner = (int)batch->state.item_owner[ii];
    if (owner < 0 || owner >= (int)batch->config.num_players ||
        sheik_held_needle_owner_pointer_live(batch, msl_idx_player(bi, owner)) == 0u) {
      item_slot_clear(batch, ii);
      needs_sort = 1u;
    }
  }
  if (needs_sort != 0u) {
    items_sort(batch, bi);
  }
}

static inline uint8_t sheik_needle_hitbox_targets_item_ground_state(uint16_t flags,
                                                                    uint8_t item_ga) {
  if (item_ga != 0u) {
    return (flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) != 0u ? 1u : 0u;
  }
  return (flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) != 0u ? 1u : 0u;
}

enum {
  MSL_ITEM_ARTICLE_HITBOX_FLAG_TARGET_GROUNDED = 1u << 0,
  MSL_ITEM_ARTICLE_HITBOX_FLAG_TARGET_AERIAL = 1u << 1,
  MSL_ITEM_ARTICLE_HITBOX_FLAG_BODY_ENABLED = 1u << 2,
  MSL_ITEM_ARTICLE_HITBOX_FLAG_GRABBABLE_ONLY = 1u << 3,
};

static inline uint8_t sheik_needle_article_hitbox_targets_fighter_ground_state(
    uint32_t flags, uint8_t defender_grounded) {
  return defender_grounded
             ? ((flags & (uint32_t)MSL_ITEM_ARTICLE_HITBOX_FLAG_TARGET_GROUNDED) != 0u ? 1u : 0u)
             : ((flags & (uint32_t)MSL_ITEM_ARTICLE_HITBOX_FLAG_TARGET_AERIAL) != 0u ? 1u : 0u);
}

static inline uint8_t sheik_needle_article_hitbox_body_enabled(uint32_t flags) {
  // Source item contact skips each item HitCapsule unless `x42_b5` is set before reflect, shield,
  // clank, and BODY handling. MSLITAR1 v13 packs that sixth item-script word bit into the article
  // hitbox flags.
  // refs/melee/src/melee/it/itanimlist.c::it_802790C0
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  return (flags & (uint32_t)MSL_ITEM_ARTICLE_HITBOX_FLAG_BODY_ENABLED) != 0u ? 1u : 0u;
}

static inline uint8_t sheik_needle_article_hitbox_grabbable_only(uint32_t flags) {
  // `x42_b6` further restricts BODY contact to grabbable hurtcaps after ShieldDesc/clank misses.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  return (flags & (uint32_t)MSL_ITEM_ARTICLE_HITBOX_FLAG_GRABBABLE_ONLY) != 0u ? 1u : 0u;
}

static inline float sheik_needle_hitbox_damage(const MslBatch* batch, size_t hb_i) {
  if (batch == NULL) {
    return 0.0f;
  }
  // Fighter HitCapsule -> item hurtbox copies the source HitCapsule damage integer directly into
  // fighter->dmg.x1914 and item->xCA0. MSL's BODY lane may have already materialized stale-scaled
  // damage in `hitbox_damage`; when the frozen stale multiplier is present, invert it to recover
  // the authored HitCapsule integer that `it_802703E8` reads.
  // refs/melee/src/melee/it/itcoll.c::it_802703E8
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076808
  float damage = batch->state.hitbox_damage[hb_i];
  if (batch->state.hitbox_stale_damage_valid[hb_i] != 0u &&
      batch->state.hitbox_stale_damage_mul[hb_i] > 0.0f) {
    damage /= batch->state.hitbox_stale_damage_mul[hb_i];
  }
  return damage;
}

static inline float sheik_needle_state1_4_gravity_from_visible_vel(float vel_y,
                                                                   const float* gravity) {
  float best_g = gravity[0];
  float best_resid = 1000000.0f;
  for (int gi = 0; gi < 8; gi++) {
    const float g = gravity[gi];
    const float q = vel_y / g;
    const float rounded = floorf(q + 0.5f);
    const float resid = fabsf(q - rounded);
    if (resid < best_resid) {
      best_resid = resid;
      best_g = g;
    }
  }
  return best_g;
}

static inline uint8_t sheik_needle_anim_lifetime_step(MslBatch* batch, size_t ii) {
  const uint8_t state = batch->state.item_state[ii];
  if (state == 4u) {
    // Bounced Needle state 4 Anim only updates its previous-position/rotation lanes and returns
    // false; it does not call the generic lifeTimer decrement used by states 0..3.
    // refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakneedlethrown_UnkMotion4_Anim
    return 0u;
  }
  // States 0..3 return it_80273130 from Anim. A serialized lifeTimer of 1 is destroyed in this
  // item Anim callback before Phys/Coll run.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
  //   itSeakneedlethrown_UnkMotion0_Anim,itSeakneedlethrown_UnkMotion1_Anim,
  //   itSeakneedlethrown_UnkMotion2_Anim,itSeakneedlethrown_UnkMotion3_Anim}
  // refs/melee/src/melee/it/item.c::Item_80269528
  if (batch->state.item_timer[ii] <= 1.0f) {
    item_slot_clear(batch, ii);
    return 1u;
  }
  batch->state.item_timer[ii] -= 1.0f;
  return 0u;
}

static inline void sheik_needle_set_drop_hidden_lanes(MslBatch* batch, int bi, size_t ii,
                                                      const MslItemArticleParams* ap) {
  if (batch == NULL || ap == NULL) {
    return;
  }
  // itSeakNeedleThrown_SetupDrop RNG order: rotation rate it_803F6FE0[Randi(8)] then a Randi(2)
  // sign (cosmetic child-JObj rotation, value unmodeled here), then min-y it_803F6FA0[Randi(8)] and
  // gravity it_803F6FC0[Randi(8)] stored in itemVar.seakneedlethrown.xDDC/xDE0. Those two tables are
  // data-owned in MSLITAR1 (needle_drop_min_vel_y / needle_drop_gravity).
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakNeedleThrown_SetupDrop
  (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DROP_ROT_RATE8, 8);
  (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DROP_ROT_SIGN2, 2);
  const int min_idx =
      combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DROP_YVEL_MIN8, 8);
  const int grav_idx =
      combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DROP_GRAVITY8, 8);
  batch->state.item_sheik_needle_hidden_drop_min_vel_y[ii] = ap->needle_drop_min_vel_y[min_idx & 7];
  batch->state.item_sheik_needle_hidden_drop_gravity[ii] = ap->needle_drop_gravity[grav_idx & 7];
  // SetupDrop leaves xDD8 = 0 (dropped Needles fall straight down; only bounced state-4 drifts).
  batch->state.item_sheik_needle_hidden_drop_vel_x[ii] = 0.0f;
  batch->state.item_sheik_needle_hidden_drop_valid[ii] = 1u;
}

static inline void sheik_needle_motion_step(MslBatch* batch, size_t ii) {
  const uint8_t state = batch->state.item_state[ii];
  if (state == 0u) {
    batch->state.item_pos_x[ii] += batch->state.item_vel_x[ii];
    batch->state.item_pos_y[ii] += batch->state.item_vel_y[ii];
    return;
  }
  if (state == 1u || state == 4u) {
    // itSeakneedlethrown_UnkMotion{1,4}_Phys: x40_vel.x = itemVar.xDD8, x40_vel.y += xDE0 (gravity),
    // then clamp x40_vel.y to terminal xDDC. Live take-damage-dropped Needles carry the real hidden
    // xDDC/xDE0 lanes (drop xDD8 = 0); replay-seeded state-1/4 items without those lanes fall back
    // to source-table gravity recovered from the visible y velocity (needle_drop_gravity for the
    // dropped state, needle_bounce_gravity for the bounced state).
    // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
    //   itSeakNeedleThrown_SetupDrop,itSeakneedlethrown_UnkMotion1_Phys,
    //   itSeakneedlethrown_UnkMotion4_Phys}
    if (batch->state.item_sheik_needle_hidden_drop_valid[ii] != 0u) {
      // Source UnkMotion{1,4}_Phys re-sets x40_vel.x to the (constant) hidden drift each frame: drop
      // lanes store xDD8 = 0, bounce lanes store the SetupBounce xDD8 horizontal drift.
      batch->state.item_vel_x[ii] = batch->state.item_sheik_needle_hidden_drop_vel_x[ii];
      batch->state.item_vel_y[ii] += batch->state.item_sheik_needle_hidden_drop_gravity[ii];
      if (batch->state.item_vel_y[ii] < batch->state.item_sheik_needle_hidden_drop_min_vel_y[ii]) {
        batch->state.item_vel_y[ii] = batch->state.item_sheik_needle_hidden_drop_min_vel_y[ii];
      }
    } else {
      const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
      if (ap != NULL) {
        const float* gtab = (state == 4u) ? ap->needle_bounce_gravity : ap->needle_drop_gravity;
        batch->state.item_vel_y[ii] +=
            sheik_needle_state1_4_gravity_from_visible_vel(batch->state.item_vel_y[ii], gtab);
      }
    }
    batch->state.item_pos_x[ii] += batch->state.item_vel_x[ii];
    batch->state.item_pos_y[ii] += batch->state.item_vel_y[ii];
  }
}

static inline void sheik_needle_apply_item_hitlag(MslBatch* batch, size_t ii, int damage_i) {
  const MslItemCommonParams* item_common = msl_item_common_params();
  const float item_hitlag =
      (item_common != NULL)
          ? (item_common->item_hitlag_base + item_common->item_hitlag_damage_mul * (float)damage_i)
          : 0.0f;
  if (item_hitlag > 0.0f) {
    const uint8_t frames = (uint8_t)item_hitlag;
    if (frames > 1u) {
      batch->state.item_hitlag[ii] = (uint8_t)(frames - 1u);
    }
  }
}

static inline uint8_t sheik_needle_item_common_hitlag_frames(int damage_i) {
  const MslItemCommonParams* item_common = msl_item_common_params();
  if (item_common == NULL || damage_i <= 0) {
    return 0u;
  }
  const float item_hitlag =
      item_common->item_hitlag_base + item_common->item_hitlag_damage_mul * (float)damage_i;
  return (item_hitlag > 0.0f) ? (uint8_t)item_hitlag : 0u;
}

// itSeakNeedleThrown_SetupBounce hidden samples: rotation sign/rate, xvel sign/mag, min-y, gravity.
// Shared by the it_2725_Logic109 hit-bounce callback and the UnkMotion{0,1}_Coll stage-hit bounce.
// refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakNeedleThrown_SetupBounce
// Capture the SetupBounce hidden motion lanes for a Needle entering bounce state 4. Source
// itSeakNeedleThrown_SetupBounce draws, in order: a Randi(2) sign + Randi(8) rotation rate (cosmetic
// child-JObj spin, unmodeled), a Randi(2) sign + Randi(8) horizontal-drift magnitude
// (xDD8 = needle_bounce_x_vel), a Randi(8) terminal-y (xDDC = needle_bounce_min_vel_y) and a Randi(8)
// gravity (xDE0 = needle_bounce_gravity). itSeakneedlethrown_UnkMotion4_Phys then runs vel.x = xDD8,
// vel.y += xDE0 clamped to xDDC every frame; sheik_needle_motion_step replays that from these lanes.
// Live free-run bounces carry the exact xDD8/xDDC/xDE0; replay-reseeded state-4 items lose the lanes
// each frame and fall back to the visible-velocity gravity recovery in sheik_needle_motion_step.
// refs/melee/src/melee/it/items/itseakneedlethrown.c::{itSeakNeedleThrown_SetupBounce,
//   itSeakneedlethrown_UnkMotion4_Phys}
static inline void sheik_needle_setup_bounce_hidden_lanes(MslBatch* batch, int bi, size_t ii,
                                                          const MslItemArticleParams* params) {
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_ROT_SIGN2, 2);
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_ROT_RATE8, 8);
  const int xvel_sign = combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_XVEL_SIGN2, 2);
  const int xvel_idx = combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_XVEL8, 8);
  const int ymin_idx = combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_YVEL_MIN8, 8);
  const int grav_idx = combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_GRAVITY8, 8);
  float vel_x = 0.0f, min_vel_y = 0.0f, gravity = 0.0f;
  if (params != NULL) {
    vel_x = params->needle_bounce_x_vel[xvel_idx & 7] * ((xvel_sign == 0) ? 1.0f : -1.0f);
    min_vel_y = params->needle_bounce_min_vel_y[ymin_idx & 7];
    gravity = params->needle_bounce_gravity[grav_idx & 7];
  }
  batch->state.item_sheik_needle_hidden_drop_vel_x[ii] = vel_x;
  batch->state.item_sheik_needle_hidden_drop_min_vel_y[ii] = min_vel_y;
  batch->state.item_sheik_needle_hidden_drop_gravity[ii] = gravity;
  batch->state.item_sheik_needle_hidden_drop_valid[ii] = 1u;
  batch->state.item_vel_x[ii] = vel_x;
}

// Stuck-in-ground state (it_802762BC + itResetVelocity + lifeTimer attr->x4 + Item_80268E5C(2)).
// The Needle stops and lingers as a stuck article for needle_bounce_lifetime_frames.
// refs/melee/src/melee/it/items/itseakneedlethrown.c::{itSeakneedlethrown_UnkMotion0_Coll,
//   itSeakneedlethrown_UnkMotion4_Coll,itSeakneedlethrown_UnkMotion2_Anim}
static inline void sheik_needle_enter_stuck_state(MslBatch* batch, size_t ii,
                                                  const MslItemArticleParams* params) {
  batch->state.item_state[ii] = 2u;
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  batch->state.item_timer[ii] =
      (params != NULL) ? (float)params->needle_bounce_lifetime_frames : 0.0f;
}

static inline void sheik_needle_bounce_or_destroy_callback(MslBatch* batch, int bi, size_t ii,
                                                           const MslItemArticleParams* params,
                                                           int hitlag_damage_i) {
  // Shared it_2725_Logic109 outcome for DmgDealt, Clanked, DmgReceived, and HitShield: destroy the
  // Needle unless HSD_Randi(3)==0, in which case enter bounce state 4, set lifeTimer from attr->x4,
  // sample ABS(it_803F7020[Randi(8)]) for the initial upward y velocity (data-owned
  // needle_bounce_min_vel_y), then capture the SetupBounce horizontal-drift (xDD8)/terminal (xDDC)/
  // gravity (xDE0) hidden lanes driving the state-4 trajectory.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
  //   it_2725_Logic109_DmgDealt,it_2725_Logic109_Clanked,it_2725_Logic109_DmgReceived,
  //   it_2725_Logic109_HitShield,itSeakNeedleThrown_SetupBounce}
  if (combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_KEEP3,
                                    3) != 0) {
    item_slot_clear(batch, ii);
    return;
  }

  const int y_idx = combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_VEL_Y8, 8);
  batch->state.item_state[ii] = 4u;
  batch->state.item_timer[ii] =
      (params != NULL) ? (float)params->needle_bounce_lifetime_frames : 0.0f;
  batch->state.item_vel_y[ii] =
      (params != NULL) ? fabsf(params->needle_bounce_min_vel_y[y_idx & 7]) : 0.0f;
  // Capture the SetupBounce horizontal-drift/terminal/gravity lanes (sets item_vel_x = xDD8).
  sheik_needle_setup_bounce_hidden_lanes(batch, bi, ii, params);
  sheik_needle_apply_item_hitlag(batch, ii, hitlag_damage_i);
}

static inline void sheik_needle_apply_damage_callback(MslBatch* batch, int bi, size_t ii,
                                                      const MslItemArticleParams* params,
                                                      int damage_i) {
  // it_2725_Logic109_DmgReceived: OnTakeDamage first accumulates the received damage into the Needle
  // (item->xC30/visible damage), then the shared bounce/destroy outcome runs.
  // refs/melee/src/melee/it/item.c::{OnTakeDamageThink,Item_8026A294}
  uint16_t total = batch->state.item_damage[ii];
  if (damage_i > 0) {
    total = (total <= (uint16_t)(999u - (uint16_t)damage_i))
                ? (uint16_t)(total + (uint16_t)damage_i)
                : 999u;
  }
  batch->state.item_damage[ii] = total;
  sheik_needle_bounce_or_destroy_callback(batch, bi, ii, params, damage_i);
}

static uint8_t sheik_needle_try_fighter_hitbox_damage(MslBatch* batch, int bi, int item_slot,
                                                      const MslItemArticleParams* params) {
  if (batch == NULL || params == NULL || params->needle_hurtbox_count == 0u) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, item_slot);
  if (batch->state.item_exists[ii] == 0u ||
      batch->state.item_type[ii] != params->needle_throw_itkind ||
      !sheik_needle_state_accepts_fighter_hitcapsule(batch->state.item_state[ii])) {
    return 0u;
  }
  const int owner = (int)batch->state.item_owner[ii];
  const uint8_t item_ga = sheik_needle_item_ground_or_air(batch->state.item_state[ii]);
  const float ax = batch->state.item_pos_x[ii] + params->needle_hurtbox_a_offset[0];
  const float ay = batch->state.item_pos_y[ii] + params->needle_hurtbox_a_offset[1];
  const float az = params->needle_hurtbox_a_offset[2];
  const float bx = batch->state.item_pos_x[ii] + params->needle_hurtbox_b_offset[0];
  const float by = batch->state.item_pos_y[ii] + params->needle_hurtbox_b_offset[1];
  const float bz = params->needle_hurtbox_b_offset[2];
  const float hurt_r = params->needle_hurtbox_scale;
  for (int p = 0; p < (int)batch->config.num_players; p++) {
    if (p == owner) {
      continue;
    }
    const size_t p_idx = msl_idx_player(bi, p);
    for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
      const size_t hb_i = idx_hitbox(bi, p, hb);
      if (batch->state.hitbox_enabled[hb_i] == 0u) {
        continue;
      }
      const uint16_t flags = batch->state.hitbox_flags[hb_i];
      if ((flags & (uint16_t)MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION) == 0u ||
          !sheik_needle_hitbox_targets_item_ground_state(flags, item_ga)) {
        continue;
      }
      if (batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
          batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_INERT ||
          !(batch->state.hitbox_damage[hb_i] > 0.0f)) {
        continue;
      }
      const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
      const uint8_t rehit_frames = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
      if (!hitlist_allows_fighter_item(batch, bi, p, hb, item_slot,
                                       batch->state.item_spawn_id[ii])) {
        continue;
      }
      const float hx1 = batch->state.hitbox_x[hb_i];
      const float hy1 = batch->state.hitbox_y[hb_i];
      const float hz1 = batch->state.hitbox_z[hb_i];
      const float hx0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_x[hb_i] : hx1;
      const float hy0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_y[hb_i] : hy1;
      const float hz0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_z[hb_i] : hz1;
      const float rr = batch->state.hitbox_radius[hb_i] + hurt_r;
      const float d2 =
          item_segment_segment_dist2(hx0, hy0, hz0, hx1, hy1, hz1, ax, ay, az, bx, by, bz);
      if (d2 > rr * rr) {
        continue;
      }

      const float hit_damage = sheik_needle_hitbox_damage(batch, hb_i);
      float source_damage = ceilf(hit_damage);
      if (source_damage < params->needle_hitbox_damage) {
        source_damage = params->needle_hitbox_damage;
      }
      const int damage_i = (int)ceilf(source_damage);
      if (damage_i <= 0) {
        return 0u;
      }
      // Fighter HitCapsule -> item hurtbox path:
      // - `it_802703E8` gates x42_b7 item interaction, item ground/air flags, and victims_1.
      // - The accepted hit writes fighter->dmg.x1914, item->xCA0/xCA4, and the item hitlist.
      // - `Item_8026A294` then runs Needle DmgReceived and item hitlag from xCA8.
      // refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_8026F9AC_outline}
      // refs/melee/src/melee/it/item.c::{OnTakeDamageThink,Item_8026A294}
      // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgReceived
      combat_apply_deal_hitlag_raw_damage(batch, p_idx, damage_i);
      if (batch->state.item_state[ii] == 4u) {
        // Bounced Needle still owns active item HitCapsule clank/contact. The fighter side sees
        // item-common hitlag for Needle hitbox damage in that subcase, not only the fighter
        // action's deal-hitlag formula.
        // refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_80270E30}
        // refs/melee/src/melee/it/item.c::{OnClankThink,checkHitLag}
        const uint8_t item_hl =
            sheik_needle_item_common_hitlag_frames((int)ceilf(params->needle_hitbox_damage));
        combat_apply_min_hitlag_frames(batch, p_idx, (uint16_t)item_hl);
      }
      hitlist_register_fighter_group_item(batch, bi, p, hit_group, item_slot,
                                          batch->state.item_spawn_id[ii],
                                          (int)MSL_LBCOLL_INSERT_FT_BODY, rehit_frames);
      sheik_needle_apply_damage_callback(batch, bi, ii, params, damage_i);
      return 1u;
    }
  }
  return 0u;
}

static inline uint8_t item_guard_reflect_center_xyz(const MslBatch* batch, size_t idx, float* out_x,
                                                    float* out_y, float* out_z);

static inline void sheik_needle_hitbox_segment_xy(const MslItemArticleParams* params, uint8_t hb,
                                                  float contact_x0, float contact_y0,
                                                  float contact_x1, float contact_y1, float dirx,
                                                  float diry, float item_facing_dir, float* sx0,
                                                  float* sy0, float* sx1, float* sy1) {
  const float script_x = params->needle_hitbox_x_offset[hb];
  // State-0 Needle root JObj is rotated by M_PI_2 * ip->facing_dir, so the article child JObj's
  // extracted root-space Z offset contributes to world X. The local X component rotates into Z and
  // is not used by this XY BODY sweep.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_802AFF08,itSeakneedlethrown_UnkMotion0_Coll}
  const float jobj_x = item_facing_dir * params->needle_hitbox_jobj_z_offset[hb];
  const float jobj_y = params->needle_hitbox_jobj_y_offset[hb];
  *sx0 = contact_x0 + jobj_x + script_x * dirx;
  *sy0 = contact_y0 + jobj_y + script_x * diry;
  *sx1 = contact_x1 + jobj_x + script_x * dirx;
  *sy1 = contact_y1 + jobj_y + script_x * diry;
}

static uint8_t sheik_needle_try_body_hit_fighter(MslBatch* batch, int bi, int item_slot,
                                                 const MslItemArticleParams* params) {
  // Thrown Needle BODY damage (Needle as attacker). Only the state-0 flying Needle carries an active
  // HitCapsule; the state-1..4 scripts clear hitboxes. On a fighter-hurtbox contact the source runs
  // OnGiveDamage then it_2725_Logic109_DmgDealt (HSD_Randi(3)==0 bounce, else destroy) -- the same
  // bounce/destroy outcome as DmgReceived. State-0 Needles travel in a straight line at the constant
  // throw velocity. Source priority order moves the article, refreshes item HitCapsule x58/x4C via
  // it_8027137C, then ftColl_8007925C tests item-vs-fighter BODY. Command 11 binds each HitCapsule
  // to an article JObj; MSLITAR1 v15 carries the script bone and model-derived JObj XYZ offset, so
  // the straight-line Needle segment below is the just-advanced frame-start -> current item path
  // plus the source `lb_8000B1CC(hit->jobj, &offset)` publication point.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
  //   ItemStateTable,it_802AFF08,itSeakneedlethrown_UnkMotion0_Coll,it_2725_Logic109_DmgDealt}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procMap,Fighter_8006CB94}
  // refs/melee/src/melee/it/item.c::{Item_802697D4,Item_80269978,Item_80269B60}
  // refs/melee/src/melee/it/itcoll.c::it_8027137C
  // refs/melee/src/melee/it/itanimlist.c::it_802790C0
  // data/items/articles/fox_falco.bin::MSLITAR1 needle_hitbox_{bone_id,jobj_x_offset,jobj_y_offset,jobj_z_offset}
  // refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_8026FA2C}
  // refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294}
  if (batch == NULL || params == NULL || params->needle_hitbox_count == 0u) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, item_slot);
  if (batch->state.item_state[ii] != 0u) {
    return 0u;
  }
  const int owner = (int)batch->state.item_owner[ii];
  if (owner < 0 || owner >= (int)batch->config.num_players) {
    return 0u;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  const float cur_x = batch->state.item_pos_x[ii];
  const float cur_y = batch->state.item_pos_y[ii];
  const float vx = batch->state.item_vel_x[ii];
  const float vy = batch->state.item_vel_y[ii];
  const float item_facing_dir = (batch->state.item_direction[ii] < 0.0f) ? -1.0f : 1.0f;
  const float contact_x1 = cur_x;
  const float contact_y1 = cur_y;
  const float contact_x0 = cur_x - vx;
  const float contact_y0 = cur_y - vy;
  const float v2 = vx * vx + vy * vy;
  float dirx = 1.0f;
  float diry = 0.0f;
  if (v2 > 1e-12f) {
    const float inv = 1.0f / sqrtf(v2);
    dirx = vx * inv;
    diry = vy * inv;
  }
  for (int def = 0; def < (int)batch->config.num_players; def++) {
    if (def == owner) {
      continue;
    }
    const size_t d_idx = msl_idx_player(bi, def);
    const uint16_t def_iid = batch->state.instance_id[d_idx];
    if (batch->state.hurtbox_state[d_idx] != 0u) {
      continue;
    }
    // ftColl_8007925C resolves the defender's contact owners in source order -- ReflectDesc
    // (`fp->reflecting`) -> ShieldDesc (`fp->x221B_b0`) -> clank/attack HitCapsule -> BODY hurtcap.
    // Each is resolved on its OWN descriptor overlap; a miss falls through to the next owner (only a
    // reflect/shield/clank HIT consumes the contact), so BODY runs only when none of them owns it.
    // - ReflectDesc: fp->reflecting is fp+0x2218 REFLECTING. It is live independently of ShieldDesc
    //   (`fp+0x221B_b0`); GuardOn-origin GuardReflect clears ShieldDesc while keeping ReflectDesc
    //   live until x14 expires, so this branch does NOT depend on a shield bubble or the 221B bit.
    // - ShieldDesc: the 221B_b0 bit or a live shield bubble (shield_radius).
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464,ftColl_80077688,ftColl_80077970}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009370C,ftCo_80093BC0}
    const uint8_t def_grounded = (uint8_t)(batch->state.on_ground[d_idx] != 0u);
    const uint8_t flags_2218 = batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                                        (size_t)MSL_STATE_FLAGS_2218_INDEX];
    if ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) != 0u) {
      // ReflectDesc contact (source order ReflectDesc -> ShieldDesc -> BODY): an active reflector
      // (fp+0x2218 REFLECTING) resolves the Needle HitCapsule against the ReflectDesc bubble before
      // ShieldDesc/BODY. On overlap ftColl_80077464 transfers the article to the reflector and
      // it_2725_Logic109_Reflected reverses it -- for the constant-speed state-0 Needle
      // (|vel| == attr->x8; spawn vel = +/-attr->x8 and xDE4 = pos - 3*vel) the callback's
      // attr->x8 * dir(reverse) reduces EXACTLY to vel -> -vel with facing flipped. The owner transfer
      // + velocity reversal + reflector damage mul come from the GENERIC item reflect-transfer
      // primitive (speed_mul 1.0: the Needle reflects at its OWN throw speed, not the reflector xC70).
      // Geometry: ReflectDesc center is the pose shield-bone (item_guard_reflect_center_xyz), radius is
      // the source-scaled descriptor extent (item_guard_reflect_entry_pose_radius:
      // powershield_reflect_size * entry-scale * initial_shield_size * scaleY), and the descriptor z is
      // FLATTENED to 0 for the overlap exactly as the laser ReflectDesc solve does -- NOT raw
      // powershield_reflect_size, and NOT the laser shine/aged-owner replay-fit branches. Ownership
      // transfer naturally prevents re-reflect (the reflector becomes the item owner, skipped above)
      // and lets the reversed Needle BODY-hit the original thrower. The Reflected callback also halves
      // the life (lifeTimer = halfLifeTimer = spawn_life * ItemCommonData::x4C_float), applied below.
      // On a miss, or when no ReflectDesc pose center / descriptor radius is resolvable (e.g. the
      // reflecting bit without a guard pose), the contact FALLS THROUGH to ShieldDesc -> clank -> BODY
      // (the `continue` below the loop is removed; source only skips later owners on a reflect HIT).
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464}
      // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_Reflected
      const MslCommonParams* rcommon = msl_common_params();
      const MslCharParams* rch = msl_char_params_fast(batch->state.char_id[d_idx]);
      float rx = 0.0f;
      float ry = 0.0f;
      float rz = 0.0f;
      const float reflect_r =
          (rcommon != NULL) ? item_guard_reflect_entry_pose_radius(batch, rcommon, d_idx) : 0.0f;
      if (rch != NULL && reflect_r > 0.0f &&
          item_guard_reflect_center_xyz(batch, d_idx, &rx, &ry, &rz)) {
        for (uint8_t hb = 0;
             hb < params->needle_hitbox_count && hb < (uint8_t)MSL_ITEM_ARTICLE_MAX_HITBOXES;
             hb++) {
          const float hbr = params->needle_hitbox_size[hb];
          if (!(hbr > 0.0f) || !(params->needle_hitbox_damage_by_id[hb] > 0.0f)) {
            continue;
          }
          const uint32_t hb_flags = params->needle_hitbox_flags[hb];
          if (!sheik_needle_article_hitbox_body_enabled(hb_flags) ||
              !sheik_needle_article_hitbox_targets_fighter_ground_state(hb_flags, def_grounded)) {
            continue;
          }
          if (!hitlist_allows_item_hitbox_fighter(batch, bi, item_slot, (int)hb, def, def_iid)) {
            continue;
          }
          float sx0 = 0.0f, sy0 = 0.0f, sx1 = 0.0f, sy1 = 0.0f;
          sheik_needle_hitbox_segment_xy(params, hb, contact_x0, contact_y0, contact_x1, contact_y1,
                                         dirx, diry, item_facing_dir, &sx0, &sy0, &sx1, &sy1);
          if (!item_swept_sphere_sphere_intersects_3d(sx0, sy0, 0.0f, sx1, sy1, 0.0f, hbr, rx, ry,
                                                      0.0f, reflect_r)) {
            continue;
          }
          msl_item_reflect_apply_immediate_transfer(batch, ii, d_idx, def,
                                                    rch->reflector_damage_mul, 1.0f);
          // it_2725_Logic109_Reflected: xD44_lifeTimer = xD48_halfLifeTimer. For a state-0 Needle the
          // half-life was latched at spawn (it_80275158(attr->x0)) as spawn_life * x4C_float, so the
          // reflected remaining life = needle_lifetime_frames * reflect_half_life_fraction.
          // refs/melee/src/melee/it/it_2725.c::{it_80275158,it_2725_Logic109_Reflected}
          const MslItemCommonParams* icp = msl_item_common_params();
          if (icp != NULL && icp->reflect_half_life_fraction > 0.0f) {
            batch->state.item_timer[ii] =
                (float)params->needle_lifetime_frames * icp->reflect_half_life_fraction;
          }
          return 1u;
        }
      }
      // Source: ftColl_8007925C skips ShieldDesc/BODY (continue) ONLY on a reflect HIT
      // (lbColl_80007BCC reflect_hit overlap -> ftColl_80077464). A reflect MISS -- the ReflectDesc
      // bubble does not overlap, or no pose center / descriptor radius is resolvable -- FALLS THROUGH
      // to ShieldDesc -> clank -> BODY (the source reflect block's continue is inside the geometry-hit
      // branch only). So do NOT suppress them here; deliberately fall through to the ShieldDesc block.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    }
    const uint8_t flags_221b = batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                                        (size_t)MSL_STATE_FLAGS_221B_INDEX];
    const uint8_t shield_active =
        (flags_221b & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) != 0u ||
        batch->state.shield_radius[d_idx] > 0.0f;
    if (shield_active) {
      // ShieldDesc contact (source contact order ReflectDesc -> ShieldDesc -> BODY): an active
      // ShieldDesc resolves the Needle HitCapsule against the shield bubble BEFORE the BODY hurtcaps.
      // On overlap the source applies the shield-intake damage (ftColl_80077688) and runs
      // it_2725_Logic109_HitShield -- HSD_Randi(3)==0 bounce (state 4 via the same it_803F7020[Randi(8)]
      // + SetupBounce sequence) else destroy, the identical outcome to DmgDealt, so it reuses
      // sheik_needle_bounce_or_destroy_callback, then returns. On a MISS (the shield bubble does not
      // overlap, or no shield radius is resolvable) the contact FALLS THROUGH to clank -> BODY, exactly
      // as the source does: ftColl_8007925C's catch_path only `continue`s on a shield HIT
      // (lbColl_80007BCC shield_hit overlap -> ftColl_80077688) and otherwise reaches catch_elem_path /
      // the hurt_capsules BODY test. So a shielding defender CAN still be BODY-hit when the Needle
      // misses the shield bubble but overlaps a hurtcap. The ShieldDesc bubble is the projectile-agnostic
      // shield sphere already modeled
      // and replay-witness-locked for the laser/illusion item path (shield center +
      // item_guard_shield_radius_from_state radius + 1.0*scaleY ShieldDesc world radius); this reuses
      // that geometry/shield-damage primitive only -- NOT the laser GuardReflect owner/keepalive
      // heuristics. ReflectDesc stays source-prior (handled by the 2218 REFLECTING guard above).
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688,ftColl_80076CBC}
      // refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_2725_Logic109_HitShield,
      //   itSeakNeedleThrown_SetupBounce}
      const MslCommonParams* common = msl_common_params();
      float shr = batch->state.shield_radius[d_idx];
      if (!(shr > 0.0f)) {
        shr = item_guard_shield_radius_from_state(batch, common, d_idx);
      }
      if (shr > 0.0f) {
        float shx = batch->state.shield_x[d_idx];
        float shy = batch->state.shield_y[d_idx];
        float shz = batch->state.shield_z[d_idx];
        if (!isfinite(shx) || !isfinite(shy) || !isfinite(shz)) {
          shx = batch->state.pos_x[d_idx];
          shy = batch->state.pos_y[d_idx];
          shz = batch->state.pos_z[d_idx];
        }
        float shield_desc_world_r = 1.0f;
        if (batch->state.fighter_scale_y[d_idx] > 0.0f) {
          shield_desc_world_r *= batch->state.fighter_scale_y[d_idx];
        }
        for (uint8_t hb = 0;
             hb < params->needle_hitbox_count && hb < (uint8_t)MSL_ITEM_ARTICLE_MAX_HITBOXES;
             hb++) {
          const float hbr = params->needle_hitbox_size[hb];
          if (!(hbr > 0.0f) || !(params->needle_hitbox_damage_by_id[hb] > 0.0f)) {
            continue;
          }
          const uint32_t hb_flags = params->needle_hitbox_flags[hb];
          if (!sheik_needle_article_hitbox_body_enabled(hb_flags) ||
              !sheik_needle_article_hitbox_targets_fighter_ground_state(hb_flags, def_grounded)) {
            continue;
          }
          if (!hitlist_allows_item_hitbox_fighter(batch, bi, item_slot, (int)hb, def, def_iid)) {
            continue;
          }
          float sx0 = 0.0f, sy0 = 0.0f, sx1 = 0.0f, sy1 = 0.0f;
          sheik_needle_hitbox_segment_xy(params, hb, contact_x0, contact_y0, contact_x1, contact_y1,
                                         dirx, diry, item_facing_dir, &sx0, &sy0, &sx1, &sy1);
          if (!item_swept_sphere_sphere_intersects_3d(sx0, sy0, 0.0f, sx1, sy1, 0.0f, hbr, shx, shy,
                                                      shz, shr + shield_desc_world_r)) {
            continue;
          }
          const float damage = params->needle_hitbox_damage_by_id[hb];
          combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                       batch->state.item_attack_instance[ii], damage,
                                       params->needle_hitbox_shield_damage[hb],
                                       params->needle_hitbox_element[hb],
                                       batch->state.item_pos_x[ii]);
          hitlist_register_item_fighter(batch, bi, item_slot, def, def_iid,
                                        (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
          sheik_needle_bounce_or_destroy_callback(batch, bi, ii, params, (int)ceilf(damage));
          return 1u;
        }
      }
      // ShieldDesc MISS: do NOT suppress clank/BODY -- fall through (source catch_path -> catch_elem_path
      // on a shield miss). ReflectDesc above stays source-prior; clank and BODY follow below.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    }
    // Clank: Needle item HitCapsule vs the defender's eligible attack HitCapsules. Source contact order
    // is ReflectDesc -> ShieldDesc -> clank/attack HitCapsule -> BODY; reflect and shield are resolved
    // and returned/deferred above, so this runs only when neither owns the contact. ftColl_8007925C
    // feeds overlapping item/fighter HitCapsules to ftColl_80077970, whose item side (inlineItemA1 ->
    // it_8026FAC4 -> it_2725_Logic109_Clanked) fires when item_dmg - p_ftCommonData->x3CC < fighter_dmg;
    // for the 3-dmg Needle that is always true, so any eligible overlapping fighter attack HitCapsule
    // clanks the Needle into the shared bounce/destroy outcome (HSD_Randi(3)==0 bounce state 4 else
    // destroy). No BODY/shield damage. Eligible fighter hitboxes = enabled + MSL_HITBOX_FLAG_CLANK +
    // MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION + non-CATCH/INERT element (the source-backed item-interaction
    // descriptor gate, NOT the laser zero-KB contact-mask/offset branches). Hitbox-vs-hitbox, so no
    // grounded/aerial target-flag gate (that only governs hurtcap BODY targeting).
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970,inlineItemA1}
    // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_Clanked
    for (uint8_t hb = 0;
         hb < params->needle_hitbox_count && hb < (uint8_t)MSL_ITEM_ARTICLE_MAX_HITBOXES; hb++) {
      const float hbr = params->needle_hitbox_size[hb];
      if (!(hbr > 0.0f) || !(params->needle_hitbox_damage_by_id[hb] > 0.0f)) {
        continue;
      }
      if (!hitlist_allows_item_hitbox_fighter(batch, bi, item_slot, (int)hb, def, def_iid)) {
        continue;
      }
      float sx0 = 0.0f, sy0 = 0.0f, sx1 = 0.0f, sy1 = 0.0f;
      sheik_needle_hitbox_segment_xy(params, hb, contact_x0, contact_y0, contact_x1, contact_y1,
                                     dirx, diry, item_facing_dir, &sx0, &sy0, &sx1, &sy1);
      for (int fhb = 0; fhb < MSL_MAX_HITBOXES; fhb++) {
        const size_t fhb_i = idx_hitbox(bi, def, fhb);
        if (!batch->state.hitbox_enabled[fhb_i]) {
          continue;
        }
        const uint16_t fflags = batch->state.hitbox_flags[fhb_i];
        if ((fflags & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0u ||
            (fflags & (uint16_t)MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION) == 0u) {
          continue;
        }
        const uint8_t felem = batch->state.hitbox_element[fhb_i];
        if (felem == (uint8_t)MSL_HIT_ELEMENT_CATCH || felem == (uint8_t)MSL_HIT_ELEMENT_INERT) {
          continue;
        }
        if (!item_laser_hitcapsule_overlaps_fighter_hitcapsule(batch, fhb_i, sx0, sy0, sx1, sy1,
                                                               hbr)) {
          continue;
        }
        hitlist_register_item_fighter(batch, bi, item_slot, def, def_iid,
                                      (int)MSL_LBCOLL_INSERT_FT_HITBOX_CONTACT, 0);
        sheik_needle_bounce_or_destroy_callback(batch, bi, ii, params,
                                                (int)ceilf(params->needle_hitbox_damage_by_id[hb]));
        return 1u;
      }
    }
    for (uint8_t hb = 0;
         hb < params->needle_hitbox_count && hb < (uint8_t)MSL_ITEM_ARTICLE_MAX_HITBOXES; hb++) {
      const float hbr = params->needle_hitbox_size[hb];
      if (!(hbr > 0.0f) || !(params->needle_hitbox_damage_by_id[hb] > 0.0f)) {
        continue;
      }
      if (!hitlist_allows_item_hitbox_fighter(batch, bi, item_slot, (int)hb, def, def_iid)) {
        continue;
      }
      const uint32_t hb_flags = params->needle_hitbox_flags[hb];
      const float damage = params->needle_hitbox_damage_by_id[hb];

      // BODY damage path: item HitCapsule target bits, swept HitCapsule vs hurtcap.
      if (!sheik_needle_article_hitbox_body_enabled(hb_flags) ||
          !sheik_needle_article_hitbox_targets_fighter_ground_state(hb_flags, def_grounded)) {
        continue;
      }
      const uint8_t cap_n = batch->state.hurtcap_count[d_idx];
      const uint8_t use_frozen_hitlag_hurtcaps =
          (batch->state.hitlag[d_idx] != 0u || batch->state.hitlag_pre_timer[d_idx] != 0u) ? 1u
                                                                                           : 0u;
      float sx0 = 0.0f, sy0 = 0.0f, sx1 = 0.0f, sy1 = 0.0f;
      sheik_needle_hitbox_segment_xy(params, hb, contact_x0, contact_y0, contact_x1, contact_y1,
                                     dirx, diry, item_facing_dir, &sx0, &sy0, &sx1, &sy1);
      uint8_t hurt_height = 0u;
      uint8_t hit = 0u;
      float best_frozen_overlap = -1.0f;
      for (uint8_t ci = 0; ci < cap_n; ci++) {
        if (sheik_needle_article_hitbox_grabbable_only(hb_flags) &&
            batch->state.hurtcap_is_grabbable[idx_hurtcap(bi, def, ci)] == 0u) {
          continue;
        }
        float overlap = 0.0f;
        uint8_t cap_height = 0u;
        uint8_t exact_evaluated = 0u;
        // BODY contact is owned by the same item HitCapsule -> fighter hurtcap lbColl path as
        // ftColl_8007925C: item HitCapsules are refreshed from x58->x4C by it_8027137C, then
        // lbColl_8000805C evaluates the defender collision matrix/local radius. xDE4 in the Needle
        // callback is the stage-line collision segment, not a replacement BODY geometry owner.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
        // refs/melee/src/melee/it/itcoll.c::it_8027137C
        // refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_802AFF08,itSeakneedlethrown_UnkMotion0_Coll}
        if (use_frozen_hitlag_hurtcaps == 0u) {
          if (item_body_lbcoll_matrix_radius_overlap(batch, bi, def, sx0, sy0, sx1, sy1, hbr,
                                                     (int)ci, &cap_height, &overlap,
                                                     &exact_evaluated, 0u)) {
            hurt_height = cap_height;
            hit = 1u;
            break;
          }
        }
        if (exact_evaluated != 0u) {
          continue;
        }
        // Active fighter hitlag freezes the JObj/hurt capsule packet because Fighter_8006A360
        // skips Anim/IASA/Phys, but Fighter_8006CB94 still calls ftColl_8007925C. The source BODY
        // test therefore consumes the already-live fp->hurt_capsules instead of a recomputed
        // Damage* pose matrix for that frame.
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_8006CB94}
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
        if (item_swept_sphere_capsule_overlap_amount(batch, bi, def, sx0, sy0, sx1, sy1, hbr,
                                                     (int)ci, &cap_height, &overlap, 0u, 1.0f)) {
          if (use_frozen_hitlag_hurtcaps == 0u) {
            hurt_height = cap_height;
            hit = 1u;
            break;
          }
          // Active-hitlag fallback can see multiple already-live hurtcaps after item physics has
          // advanced the Needle. Source ultimately selects the DmgLog entry that reaches
          // ftColl_8007A06C; without lbColl_8000805C's exact local-radius matrix for the frozen
          // packet, use the deepest frozen world overlap as the deterministic owner rather than
          // over-admitting an earlier torso cap by array order.
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_8007A06C}
          if (hit == 0u || overlap > best_frozen_overlap) {
            best_frozen_overlap = overlap;
            hurt_height = cap_height;
            hit = 1u;
          }
        }
      }
      if (hit == 0u) {
        continue;
      }
      if (hurt_height > 2u) {
        hurt_height = 1u;
      }
      const float stale_mult =
          item_hitcapsule_stale_damage_mul(batch, ii, o_idx, batch->state.item_attack_id[ii]);
      const MslItemHitResult res = combat_apply_item_hit(
          batch, bi, owner, def, batch->state.item_attack_id[ii],
          batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
          batch->state.item_type[ii], 0u, damage, params->needle_hitbox_angle[hb],
          params->needle_hitbox_kbg[hb], params->needle_hitbox_wsk[hb],
          params->needle_hitbox_bkb[hb], hurt_height, params->needle_hitbox_element[hb], stale_mult,
          cur_x, cur_y, hbr, vx, 0u);
      if (res == MSL_ITEM_HIT_NONE) {
        continue;
      }
      hitlist_register_item_fighter(batch, bi, item_slot, def, def_iid,
                                    (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
      sheik_needle_bounce_or_destroy_callback(batch, bi, ii, params, (int)ceilf(damage));
      return 1u;
    }
  }
  return 0u;
}

// Stage-hit / ground collision for the live thrown Needle. Source: the per-state Coll callbacks run
// itSeakNeedleThrown_CheckGroundHit on the swept segment (frame-start xDE4 -> current pos) and, on a
// stage-line crossing, transition the article:
//   - flying  (state 0): HSD_Randi(5) -> 0/1/2 STICK (state 2), 3/4 BOUNCE (state 4, vel.y from the
//                        it_803F7020 table + SetupBounce);
//   - dropped (state 1): BOUNCE (state 4, vel.y = ABS(current vel.y) + SetupBounce);
//   - bounced (state 4): STICK (state 2).
// The stuck/bounced article lifeTimer is set to attr->x4 (needle_bounce_lifetime_frames), so it lingers.
// (The earlier reseed-slice TODO that deferred "stage-hit and hidden SetupBounce RNG lanes" is resolved
// here; exact stick-vs-bounce fate shares the particle-RNG-owned out-of-scope boundary of the hit fate.)
// refs/melee/src/melee/it/items/itseakneedlethrown.c::{itSeakneedlethrown_UnkMotion0_Coll,
//   itSeakneedlethrown_UnkMotion1_Coll,itSeakneedlethrown_UnkMotion4_Coll,
//   itSeakNeedleThrown_CheckGroundHit,itSeakNeedleThrown_SetupBounce}
static uint8_t sheik_needle_ground_hit_step(MslBatch* batch, int bi, size_t ii,
                                            const MslItemArticleParams* params, uint32_t stage_id) {
  if (params == NULL) {
    return 0u;
  }
  const uint8_t state = batch->state.item_state[ii];
  if (state != 0u && state != 1u && state != 4u) {
    return 0u;  // only the moving (flying/dropped/bounced) states test the stage; state 2 is stuck
  }
  const float vx = batch->state.item_vel_x[ii];
  const float vy = batch->state.item_vel_y[ii];
  const float cur_x = batch->state.item_pos_x[ii];
  const float cur_y = batch->state.item_pos_y[ii];
  // The frame-start position (source xDE4, reset to pos each Anim before this frame's motion).
  const float old_x = cur_x - vx;
  const float old_y = cur_y - vy;
  if (!stage_collision_item_line_hits_floor(stage_id, old_x, old_y, cur_x, cur_y)) {
    return 0u;
  }
  if (state == 4u) {
    sheik_needle_enter_stuck_state(batch, ii, params);
    return 1u;
  }
  if (state == 1u) {
    // UnkMotion1_Coll: vel.y = ABS(vel.y), then SetupBounce installs the state-4 drift/terminal/gravity.
    batch->state.item_state[ii] = 4u;
    batch->state.item_timer[ii] = (float)params->needle_bounce_lifetime_frames;
    batch->state.item_vel_y[ii] = fabsf(vy);
    sheik_needle_setup_bounce_hidden_lanes(batch, bi, ii, params);
    return 1u;
  }
  // state 0: HSD_Randi(5) -> 0/1/2 stick, 3/4 bounce.
  const int r = combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_GROUND_HIT5, 5);
  if (r < 3) {
    sheik_needle_enter_stuck_state(batch, ii, params);
    return 1u;
  }
  const int y_idx = combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_VEL_Y8, 8);
  batch->state.item_state[ii] = 4u;
  batch->state.item_timer[ii] = (float)params->needle_bounce_lifetime_frames;
  batch->state.item_vel_y[ii] = fabsf(params->needle_bounce_min_vel_y[y_idx & 7]);
  sheik_needle_setup_bounce_hidden_lanes(batch, bi, ii, params);
  return 1u;
}

static void sheik_needles_update_and_collide(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const uint8_t replay_reseed =
      (batch->replay_reseed_frame_active != NULL && batch->replay_reseed_frame_active[bi] != 0u)
          ? 1u
          : 0u;
  uint8_t needs_sort = 0u;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] == 0u) {
      continue;
    }
    const MslItemArticleParams* params =
        item_article_params_for_sheik_needle_throw_item_type(batch->state.item_type[ii]);
    if (params == NULL) {
      continue;
    }
    if ((batch->state.item_hidden_callback_flags[ii] &
         (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME) != 0u) {
      // it_802AFD8C-created Needles serialize at their spawn position/lifetime on the creation
      // row. Their item motion/collision callback starts on the next item proc, unlike the
      // special-cased throw-laser source paths that explicitly consume same-frame item motion.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::shootNeedles
      // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_802AFD8C
      batch->state.item_hidden_callback_flags[ii] &=
          (uint8_t)~MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME;
      continue;
    }
    if (replay_reseed != 0u && batch->state.item_state[ii] == 4u &&
        batch->state.item_damage[ii] != 0u) {
      uint8_t fighter_hitlag_active = 0u;
      for (int p = 0; p < (int)batch->config.num_players; p++) {
        if (batch->state.hitlag[msl_idx_player(bi, p)] != 0u) {
          fighter_hitlag_active = 1u;
          break;
        }
      }
      if (fighter_hitlag_active != 0u) {
        // Slippi does not expose Item.xCBC_hitlagFrames. A replay reseed of a Needle that just
        // bounced from DmgReceived can carry state4/damage/timer while the victim deal-hitlag is
        // still active; source Item_802697D4 freezes item anim/phys/lifetime until item hitlag
        // drains. That hidden item-hitlag count is the only remaining reseed-slice gap here: the
        // free-running stage-hit stick/bounce state machine and the SetupBounce drift/terminal/
        // gravity (xDD8/xDDC/xDE0) lanes are now modeled.
        // refs/melee/src/melee/it/item.c::{checkHitLag,Item_802697D4}
        // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgReceived
        continue;
      }
    }
    if (batch->state.item_hitlag[ii] > 0u) {
      batch->state.item_hitlag[ii]--;
      continue;
    }
    if (sheik_needle_anim_lifetime_step(batch, ii) != 0u) {
      needs_sort = 1u;
      continue;
    }
    sheik_needle_motion_step(batch, ii);
    if (sheik_needle_try_body_hit_fighter(batch, bi, it, params) != 0u) {
      needs_sort = 1u;
      continue;
    }
    // Source-ordered after the item-vs-fighter Coll: itSeakneedlethrown_UnkMotion{0,1,4}_Coll then
    // test the stage line and stick/bounce the article so thrown Needles stop at the ground.
    if (sheik_needle_ground_hit_step(batch, bi, ii, params, batch->state.stage_id[bi]) != 0u) {
      needs_sort = 1u;
      continue;
    }
    if (sheik_needle_try_fighter_hitbox_damage(batch, bi, it, params) != 0u) {
      needs_sort = 1u;
    }
  }
  if (needs_sort != 0u) {
    items_sort(batch, bi);
  }
}

// Sheik Vanish explosion hitbox size at article anim-frame `age`: the create size (vanish_hitbox_size)
// animated through the two size keyframes (frame7 -> 4.0, frame11 -> 2.0), held after the last.
static float sheik_vanish_smoke_hitbox_size(const MslItemArticleParams* ap, float age) {
  if (ap->vanish_hitbox_size_keyframe_count < 2u) {
    return ap->vanish_hitbox_size;
  }
  const float f0 = (float)ap->vanish_hitbox_size_keyframe_frame[0];
  const float v0 = ap->vanish_hitbox_size_keyframe_value[0];
  const float f1 = (float)ap->vanish_hitbox_size_keyframe_frame[1];
  const float v1 = ap->vanish_hitbox_size_keyframe_value[1];
  if (age <= 0.0f) {
    return ap->vanish_hitbox_size;
  }
  if (age < f0 && f0 > 0.0f) {
    return ap->vanish_hitbox_size + (v0 - ap->vanish_hitbox_size) * (age / f0);
  }
  if (age < f1 && f1 > f0) {
    return v0 + (v1 - v0) * ((age - f0) / (f1 - f0));
  }
  return v1;
}

// Sheik Vanish explosion (disappear smoke article BODY hit). it_802B1C60 spawns It_Kind_Seak_Vanish at
// the HipN disappear point; its state-0 script creates a HitCapsule (vanish_hitbox: 12 dmg, angle 90,
// kbg 60, bkb 80, elem 1, shield -128) sized by vanish_hitbox_size and animated by the size keyframes,
// removed at vanish_hitbox_remove_frame (anim-frame relative). it_2725 resolves item-vs-fighter
// ShieldDesc -> BODY; the smoke is NOT a reflectable/clankable projectile and itSeakVanish_Logic42_-
// DmgDealt returns false, so it neither reflects/clanks nor bounces/destroys on a hit -- it persists
// for its full lifeTimer. Hits each non-owner fighter once.
// refs/melee/src/melee/it/items/itseakvanish.c::{it_802B1C60,it_802B1D40,itSeakVanish_Logic42_DmgDealt}
// refs/melee/src/melee/it/it_2725.c::it_8027518C
static uint8_t sheik_vanish_smoke_try_hit_fighter(MslBatch* batch, int bi, int item_slot,
                                                  const MslItemArticleParams* ap) {
  if (batch == NULL || ap == NULL || ap->vanish_hitbox_count == 0u ||
      !(ap->vanish_hitbox_damage > 0.0f) || ap->sheik_vanish_lifetime_frames == 0u) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, item_slot);
  if (batch->state.item_exists[ii] == 0u || batch->state.item_type[ii] != ap->sheik_vanish_itkind) {
    return 0u;
  }
  // Article anim-frame age. The smoke timer counts down from sheik_vanish_lifetime_frames; this
  // collide runs before the per-frame lifetime decrement, so age == the article anim frame and the
  // size keyframes / remove frame line up 0-based.
  const float age = (float)ap->sheik_vanish_lifetime_frames - batch->state.item_timer[ii];
  if (!(age >= 0.0f) || age >= (float)ap->vanish_hitbox_remove_frame) {
    return 0u;
  }
  const float hbr = sheik_vanish_smoke_hitbox_size(ap, age);
  if (!(hbr > 0.0f)) {
    return 0u;
  }
  const int owner = (int)batch->state.item_owner[ii];
  const float hx = batch->state.item_pos_x[ii] + ap->vanish_hitbox_x_offset;
  const float hy = batch->state.item_pos_y[ii] + ap->vanish_hitbox_y_offset;
  const float hz = ap->vanish_hitbox_z_offset;
  const MslCommonParams* common = msl_common_params();
  uint8_t any = 0u;
  for (int def = 0; def < (int)batch->config.num_players; def++) {
    if (def == owner) {
      continue;
    }
    const size_t d_idx = msl_idx_player(bi, def);
    // Item BODY collision follows fighter contact eligibility: a victim already in hitlag is not a
    // same-frame candidate for a second BODY apply.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,Fighter_ProcessHit_8006D1EC}
    if (batch->state.hitlag[d_idx] != 0u) {
      continue;
    }
    if (batch->state.hurtbox_state[d_idx] != 0u) {
      continue;  // intangible/invincible defender
    }
    const uint16_t def_iid = batch->state.instance_id[d_idx];
    if (!hitlist_allows_item_hitbox_fighter(batch, bi, item_slot, 0, def, def_iid)) {
      continue;
    }
    const uint8_t def_grounded = (uint8_t)(batch->state.on_ground[d_idx] != 0u);
    if (def_grounded ? ((ap->vanish_hitbox_flags & 0x1u) == 0u)
                     : ((ap->vanish_hitbox_flags & 0x2u) == 0u)) {
      continue;
    }
    // ShieldDesc -> shield hit (an active shield bubble blocks the explosion); a shield MISS falls
    // through to the BODY hurtcaps, matching the source item-vs-fighter contact order.
    const uint8_t flags_221b = batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                                        (size_t)MSL_STATE_FLAGS_221B_INDEX];
    const uint8_t shield_active =
        (flags_221b & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) != 0u ||
        batch->state.shield_radius[d_idx] > 0.0f;
    if (shield_active) {
      float shr = batch->state.shield_radius[d_idx];
      if (!(shr > 0.0f)) {
        shr = item_guard_shield_radius_from_state(batch, common, d_idx);
      }
      if (shr > 0.0f) {
        float shx = batch->state.shield_x[d_idx];
        float shy = batch->state.shield_y[d_idx];
        float shz = batch->state.shield_z[d_idx];
        if (!isfinite(shx) || !isfinite(shy) || !isfinite(shz)) {
          shx = batch->state.pos_x[d_idx];
          shy = batch->state.pos_y[d_idx];
          shz = batch->state.pos_z[d_idx];
        }
        float shield_desc_world_r = 1.0f;
        if (batch->state.fighter_scale_y[d_idx] > 0.0f) {
          shield_desc_world_r *= batch->state.fighter_scale_y[d_idx];
        }
        if (item_swept_sphere_sphere_intersects_3d(hx, hy, hz, hx, hy, hz, hbr, shx, shy, shz,
                                                   shr + shield_desc_world_r)) {
          combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                       batch->state.item_attack_instance[ii],
                                       ap->vanish_hitbox_damage, ap->vanish_hitbox_shield_damage,
                                       ap->vanish_hitbox_element, batch->state.item_pos_x[ii]);
          hitlist_register_item_fighter(batch, bi, item_slot, def, def_iid,
                                        (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
          any = 1u;
          continue;
        }
      }
    }
    // BODY hurtcap overlap -> vanish_hitbox_damage body hit.
    const uint8_t cap_n = batch->state.hurtcap_count[d_idx];
    uint8_t hurt_height = 0u;
    uint8_t hit = 0u;
    for (uint8_t ci = 0; ci < cap_n; ci++) {
      float overlap = 0.0f;
      uint8_t cap_height = 0u;
      if (item_swept_sphere_capsule_overlap_amount(batch, bi, def, hx, hy, hx, hy, hbr, (int)ci,
                                                   &cap_height, &overlap, 0u, 1.0f)) {
        hurt_height = cap_height;
        hit = 1u;
        break;
      }
    }
    if (hit == 0u) {
      continue;
    }
    if (hurt_height > 2u) {
      hurt_height = 1u;
    }
    const MslItemHitResult res = combat_apply_item_hit(
        batch, bi, owner, def, batch->state.item_attack_id[ii],
        batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
        batch->state.item_type[ii], 0u, ap->vanish_hitbox_damage, ap->vanish_hitbox_angle,
        ap->vanish_hitbox_kbg, ap->vanish_hitbox_wsk, ap->vanish_hitbox_bkb, hurt_height,
        ap->vanish_hitbox_element, -1.0f, hx, hy, hbr, 0.0f, 0u);
    if (res == MSL_ITEM_HIT_NONE) {
      continue;
    }
    hitlist_register_item_fighter(batch, bi, item_slot, def, def_iid,
                                  (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
    any = 1u;
  }
  return any;  // smoke persists regardless of a hit (Logic42_DmgDealt returns false)
}

static void sheik_vanish_smoke_collide(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_vanish_itkind == 0u || ap->vanish_hitbox_count == 0u) {
    return;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] == 0u ||
        batch->state.item_type[ii] != ap->sheik_vanish_itkind) {
      continue;
    }
    (void)sheik_vanish_smoke_try_hit_fighter(batch, bi, it, ap);
  }
}

static inline uint8_t illusion_item_hit_params_from_state(const MslCharParams* chp,
                                                          uint8_t item_state,
                                                          MslIllusionItemHitParams* out) {
  if (chp == NULL || out == NULL) {
    return 0u;
  }
  out->radius = chp->illusion_item_hitbox_size;
  if (item_state == 0u) {
    out->damage = chp->illusion_item_state0_damage;
    out->shield_damage = chp->illusion_item_state0_shield_damage;
    out->angle = chp->illusion_item_state0_angle;
    out->kbg = chp->illusion_item_state0_kbg;
    out->wsk = chp->illusion_item_state0_wsk;
    out->bkb = chp->illusion_item_state0_bkb;
    out->element = chp->illusion_item_state0_element;
    out->hitbox_y_offset = chp->illusion_item_state0_hitbox_y_offset;
  } else if (item_state == 1u) {
    out->damage = chp->illusion_item_state1_damage;
    out->shield_damage = chp->illusion_item_state1_shield_damage;
    out->angle = chp->illusion_item_state1_angle;
    out->kbg = chp->illusion_item_state1_kbg;
    out->wsk = chp->illusion_item_state1_wsk;
    out->bkb = chp->illusion_item_state1_bkb;
    out->element = chp->illusion_item_state1_element;
    out->hitbox_y_offset = chp->illusion_item_state1_hitbox_y_offset;
  } else {
    // State 2 uses a visual-only callback lane (no hit callback ownership here).
    // refs/melee/src/melee/it/items/itfoxillusion.c::itFoxillusion_UnkMotion2_Coll
    return 0u;
  }
  return 1u;
}

static inline uint8_t illusion_owner_motion_is_active(const MslBatch* batch, size_t owner_idx) {
  if (batch == NULL) {
    return 0u;
  }
  // Illusion article lifetime is owned by ftFx_SpecialS_CheckGhostRemove(owner):
  // active while motion_id is within [ftFx_MS_SpecialSStart .. ftFx_MS_SpecialAirSEnd].
  // Item animation can observe an owner that exited Side-B during this same fighter callback pass;
  // preserve the frame-start motion for this callback-order lane.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFx_SpecialS_CheckGhostRemove
  // refs/melee/src/melee/it/items/itfoxillusion.c::{itFoxillusion_UnkMotion0_Anim,
  //   itFoxillusion_UnkMotion2_Anim}
  const uint8_t owner_char = batch->state.char_id[owner_idx];
  const uint8_t owner_kind =
      msl_motion_state_fx_special_kind(owner_char, batch->state.action_id[owner_idx]);
  const uint8_t frame_start_kind =
      msl_motion_state_fx_special_kind(owner_char, batch->state.prev_action_id[owner_idx]);
  return ((owner_kind >= (uint8_t)MSL_FX_KIND_SPECIAL_S_START &&
           owner_kind <= (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END) ||
          (frame_start_kind >= (uint8_t)MSL_FX_KIND_SPECIAL_S_START &&
           frame_start_kind <= (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END))
             ? 1u
             : 0u;
}

static inline uint8_t illusion_item_anim_step(MslBatch* batch, size_t ii, const MslCharParams* chp,
                                              uint8_t owner_motion_active) {
  if (batch == NULL || chp == NULL) {
    return 0u;
  }
  if (!owner_motion_active) {
    item_slot_clear(batch, ii);
    return 0u;
  }

  // Item animation callback ownership:
  // - state0/state1 share anim: decrement timer; on expiry enter state2 and reset timer.
  // - state2 anim: decrement timer; on expiry destroy.
  // refs/melee/src/melee/it/items/itfoxillusion.c::{
  //   itFoxillusion_UnkMotion0_Anim,itFoxillusion_UnkMotion1_Anim,
  //   itFoxillusion_UnkMotion2_Anim,it_8029D798}
  const uint8_t state = batch->state.item_state[ii];
  float timer = batch->state.item_timer[ii];
  if (state <= 1u) {
    timer -= 1.0f;
    if (timer <= 0.0f) {
      if (chp->illusion_item_lifetime_state2_frames == 0u) {
        item_slot_clear(batch, ii);
        return 0u;
      }
      batch->state.item_state[ii] = 2u;
      batch->state.item_timer[ii] = (float)chp->illusion_item_lifetime_state2_frames;
      return 1u;
    }
    batch->state.item_timer[ii] = timer;
    return 1u;
  }

  timer -= 1.0f;
  if (timer <= 0.0f) {
    item_slot_clear(batch, ii);
    return 0u;
  }
  batch->state.item_timer[ii] = timer;
  return 1u;
}

static inline void item_guard_reflect_apply_recharge(MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  float hp = batch->state.shield_hp[d_idx];
  if (hp < c->start_shield_health) {
    hp += c->shield_recharge_per_frame;
    if (hp > c->start_shield_health) {
      hp = c->start_shield_health;
    }
    batch->state.shield_hp[d_idx] = hp;
  }
}

static inline void item_guard_reflect_restore_anim_drain(MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  // GuardReflect item-BODY handoff:
  // - GuardReflect_Anim runs ftCo_80093BC0 then GuardOn_Anim before IASA / item collision,
  // - item collision can consume the live GuardReflect/Escape owner into Damage after that
  //   shield-state callback phase,
  // - replay post-frame shield HP does not retain the same-frame shield-hold drain from the
  //   consumed shield state. If Fighter_ProcessHit owns a recharge tick, that recharge has already
  //   been applied inside combat_apply_item_hit().
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardReflect_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_GuardReflect_IASA
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // The caller snapshots the GuardReflect/no-submotion predicate before combat_apply_item_hit(),
  // because a successful BODY hit immediately changes the fighter into Damage*. Do not re-read the
  // action predicate here after the owner has already been consumed.
  const float light = batch->state.lightshield_amount[d_idx];
  const float drain_factor =
      (light * (c->shield_hold_drain_max - c->shield_hold_drain_base)) + c->shield_hold_drain_base;
  const float drain = c->shield_hold_drain_mul * drain_factor;
  float hp = batch->state.shield_hp[d_idx] + drain;
  if (hp > c->start_shield_health) {
    hp = c->start_shield_health;
  }
  batch->state.shield_hp[d_idx] = hp;
}

static inline uint8_t item_guard_reflect_body_hit_consumes_shield_state(const MslBatch* batch,
                                                                        size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[d_idx];
  // Only restore consumed shield-state drain when GuardReflect was already the frame-start owner.
  // GuardOn -> GuardReflect -> BODY in one item pass keeps the GuardOn_Anim drain before
  // Fighter_ProcessHit recharge.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_8009388C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const uint8_t frozen_guard_reflect =
      (action_id == (uint16_t)MSL_ACT_GUARD_REFLECT && batch->state.action_frame[d_idx] < 0 &&
       batch->state.animation_index[d_idx] == UINT32_MAX &&
       batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       (batch->state.guard_reflect_timer_x14[d_idx] != 0u ||
        batch->state.guard_reflect_timer_x14_seed[d_idx] != 0u))
          ? 1u
          : 0u;
  const uint8_t guard_reflect_iasa_escape =
      ((action_id == (uint16_t)MSL_ACT_ESCAPE_F || action_id == (uint16_t)MSL_ACT_ESCAPE_B ||
        action_id == (uint16_t)MSL_ACT_ESCAPE_N) &&
       batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       batch->state.guard_reflect_timer_x14_seed[d_idx] != 0u &&
       batch->state.action_frame[d_idx] <= 1)
          ? 1u
          : 0u;
  return (frozen_guard_reflect || guard_reflect_iasa_escape) ? 1u : 0u;
}

static inline uint8_t item_guardon_reflect_body_hit_undoes_action_recharge(const MslBatch* batch,
                                                                           size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  // The action-level recharge path admits fresh GuardOn -> GuardReflect rows because
  // ftCo_8009388C clears the shield descriptor. If item BODY contact immediately consumes that
  // same hidden GuardReflect owner, Fighter_ProcessHit owns the visible recharge instead; remove
  // the pre-item recharge after the accepted BODY hit.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_8009388C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  return (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
          batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX)
             ? 1u
             : 0u;
}

static inline void item_guardon_reflect_undo_action_recharge(MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  float hp = batch->state.shield_hp[d_idx] - c->shield_recharge_per_frame;
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[d_idx] = hp;
}

static inline uint8_t item_guardreflect_active_timer_shield_contact_needs_drain(
    const MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  return (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.action_frame[d_idx] < 0 &&
          batch->state.animation_index[d_idx] == UINT32_MAX &&
          batch->state.guard_reflect_timer_x14[d_idx] > 0u &&
          (batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON ||
           batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON))
             ? 1u
             : 0u;
}

static inline uint8_t item_guardreflect_origin_x14_expired_this_callback(const MslBatch* batch,
                                                                         size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  // GuardOn-origin GuardReflect expiry:
  // - `ftCo_8009388C` enters GuardReflect from an already-shielding guard state with ShieldDesc
  //   cleared and only ReflectDesc live.
  // - `ftCo_GuardReflect_Anim -> ftCo_80093BC0` decrements x14; when the frame-start seed was the
  //   final x14 tick, ftCo_80093BC0 recreates ShieldDesc before item/fighter collision.
  // - This matches combat.c's final-x14 ShieldDesc owner and prevents the item path from treating
  //   the same callback phase as frozen keepalive.
  // - Keep rows with raw fp+0x2218_b1 set on the existing live-article lane; replay-real
  //   final-x14 controls with that command bit do not hand off to Item_80269DC8 here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092450}
  // refs/melee/src/melee/ft/types.h (fp+0x2218_b1)
  return (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.action_frame[d_idx] < 0 &&
          batch->state.animation_index[d_idx] == UINT32_MAX &&
          batch->state.guard_reflect_timer_x14_seed[d_idx] == 1u &&
          batch->state.guard_reflect_timer_x14[d_idx] == 0u &&
          batch->state.guard_reflect_origin_guardon[d_idx] != 0u &&
          (batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                    (size_t)MSL_STATE_FLAGS_2218_INDEX] &
           (uint8_t)MSL_STATE_FLAG_2218_B1) == 0u)
             ? 1u
             : 0u;
}

static inline void item_guardreflect_apply_contact_drain(MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  // Active-x14 GuardReflect no-contact rows leave the shield descriptor cleared and recharge at
  // action level. Once Item_80269DC8 owns a shield contact, the GuardOn_Anim shield drain is
  // visible before shield-hit depletion/bounce resolution.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_GuardOn_Anim,ftCo_800925A4}
  // refs/melee/src/melee/it/item.c::Item_80269DC8
  const float light = batch->state.lightshield_amount[d_idx];
  const float drain_factor =
      (light * (c->shield_hold_drain_max - c->shield_hold_drain_base)) + c->shield_hold_drain_base;
  float hp = batch->state.shield_hp[d_idx] - (c->shield_hold_drain_mul * drain_factor);
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[d_idx] = hp;
}

static inline void item_apply_shine_reflect_callback(MslBatch* batch, size_t ii,
                                                     size_t reflector_idx);

static inline void item_apply_seeded_reflect_transfer_after_collision(MslBatch* batch, size_t ii) {
  if (batch == NULL) {
    return;
  }
  const uint8_t seed_port = batch->state.item_reflect_transfer_seed_port[ii];
  const uint16_t seed_iid = batch->state.item_reflect_transfer_seed_iid[ii];
  if (seed_port >= (uint8_t)batch->config.num_players || seed_iid == 0u) {
    return;
  }

  // Seeded hidden reflect-transfer lane:
  // - ftColl_80077464 writes the reflect snapshot during collision selection.
  // - SpecialLw reflector descriptors also stage `reflect_hit_cb`, which Fighter_ProcessHit later
  //   consumes as ftFx_SpecialLwHit_Enter before the item owner/xDA8 post-frame transfer is visible.
  // - Item_80269F14 consumes owner/xDA8_short before the post-frame item record.
  // Apply this after same-frame collision callbacks have seen the pre-transfer owner; otherwise
  // Shine/reflector callbacks incorrectly treat the projectile as already owned by the reflector.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwHit_Enter
  // refs/melee/src/melee/it/item.c::Item_80269F14
  const int bi = (int)(ii / (size_t)MSL_MAX_ITEMS);
  const size_t reflector_idx = msl_idx_player(bi, (int)seed_port);
  item_apply_shine_reflect_callback(batch, ii, reflector_idx);
  const MslCommonParams* common = msl_common_params();
  const float damage_mul = (common != NULL && common->powershield_reflect_damage_mul > 0.0f)
                               ? common->powershield_reflect_damage_mul
                               : 1.0f;
  msl_item_reflect_apply_seeded_transfer(batch, ii, seed_port, seed_iid, damage_mul);
}

static inline uint8_t item_guard_shield_bone_center_for_msid(const MslBatch* batch, size_t idx,
                                                             uint16_t msid, float* out_x,
                                                             float* out_y, float* out_z) {
  if (batch == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return 0u;
  }

  float m[12];
  // ShieldDesc/ReflectDesc attach to `fp->ft_data->x8->x11` with offset zero. Sample the source
  // submotion frame-0 shield-bone matrix for hidden no-submotion snapshots that cannot otherwise
  // expose the live JObj descriptor position.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092450,ftCo_80091D58}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_8007B1B8}
  // tools/extraction/extract_character_attrs.py::grab_capture_anchor_part_id (ftData.x8->x11)
  if (anim_pose_get_matrix(batch->state.char_id[idx], msid, 0u, ch->grab_capture_anchor_part_id,
                           m) != 0) {
    return 0u;
  }
  float scale = batch->state.fighter_scale_y[idx];
  if (!(scale > 0.0f)) {
    scale = 1.0f;
  }
  const float model_scaling = (ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
  scale *= model_scaling;
  const float lx = m[3] * scale;
  const float ly = m[7] * scale;
  const float lz = m[11] * scale;
  const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;

  // Facing parity matches the root-facing transform used by ftLib pose consumers.
  // refs/melee/src/melee/ft/ftlib.c::{ftLib_800866DC,ftLib_800869D4}
  *out_x = batch->state.pos_x[idx] + facing_dir * lz;
  *out_y = batch->state.pos_y[idx] + ly;
  *out_z = batch->state.pos_z[idx] - facing_dir * lx;
  return 1u;
}

static uint8_t item_guard_reflect_center_xyz_impl(const MslBatch* batch, size_t idx, float* out_x,
                                                  float* out_y, float* out_z);

static inline uint8_t item_guard_reflect_center_xyz(const MslBatch* batch, size_t idx, float* out_x,
                                                    float* out_y, float* out_z) {
  return item_guard_reflect_center_xyz_impl(batch, idx, out_x, out_y, out_z);
}

static uint8_t item_guard_reflect_center_xyz_impl(const MslBatch* batch, size_t idx, float* out_x,
                                                  float* out_y, float* out_z) {
  if (batch == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    return 0u;
  }
  uint16_t msid = 0u;
  switch (batch->state.action_id[idx]) {
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD_REFLECT:
      // GuardReflect uses the GuardOn submotion row in GALE01's motion-state table.
      // refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_GuardReflect
      msid = (uint16_t)MSL_SM_GUARD_ON;
      break;
    case MSL_ACT_GUARD:
      msid = (uint16_t)MSL_SM_GUARD;
      break;
    default:
      return 0u;
  }

  // Frozen guard-family post-frame snapshots commonly have animation_index=-1. ReflectDesc is
  // attached to `fp->ft_data->x8->x11` with offset zero, so the frame-0 GuardOn shield-bone matrix
  // is the prefix-causal source-backed fallback for these no-submotion rows.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
  // tools/extraction/extract_character_attrs.py::grab_capture_anchor_part_id (ftData.x8->x11)
  if (item_guard_shield_bone_center_for_msid(batch, idx, msid, out_x, out_y, out_z) == 0u) {
    return 0u;
  }
  // Guard tilt displacement: GuardReflect keeps running the Guard tilt chain
  // (ftCo_80093BC0 -> GuardOn_Anim -> ftCo_800928CC), so ReflectDesc/ShieldDesc ride the tilted
  // shield bone in vanilla. Displace the pose anchor by the same tilt-table lerp
  // shields_refresh applies (delta of table[x8] vs table[neutral], facing-mixed). Witness:
  // MAJ:7384 (tilt_x4 0.50, x8 99) bounces off the displaced shield while MAJ:118
  // (tilt 0.50, x8 365 ~ neutral) still reflects; the untilted anchor cannot separate them.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_800928CC}
  // GuardOn entry and GuardOn-origin GuardReflect rows are excluded: ftCo_800921DC resets
  // x8=neutral/x4=0 on entry, and GuardOn-origin reflect rows already serialize the
  // ShieldBounced/HitShield source proof from their current shield session. Reapplying the
  // locomotion-entry tilt displacement to that seeded ShieldBounced lane over-hits controls
  // (DSG:2554/EWT:4990), while locomotion-origin GuardReflect retains the tilted shield-bone
  // source (MAJ:7384).
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_GUARD_ON &&
      batch->state.guard_reflect_origin_guardon[idx] == 0u) {
    MslShieldTiltTableView tv;
    if (msl_shield_tilt_table_view(batch->state.char_id[idx], &tv) == 0 && tv.xyz != NULL &&
        tv.frame_count > 0) {
      const uint16_t frame_max = (uint16_t)(tv.frame_count - 1u);
      // Frame-start lanes: the source item pass reads the tilt as left by the previous frame's
      // guard anim callback, not this step's post-update value (shields_refresh updates before
      // the item phase in our pipeline; vanilla updates inside the action's anim callback).
      // Entry frames are the exception: ftCo_80093A50 entry runs GuardReflect_Anim's tilt update
      // before the same-frame item pass, so the freshly updated live lanes are what vanilla's
      // descriptors ride (MAJ:7383 rollout - the untilted entry-frame anchor let the aged owner
      // stage a reflect vanilla never staged, slowing the laser by 3.3u).
      const uint8_t tilt_entry_live = batch->state.guard_reflect_entered_this_frame[idx];
      uint16_t f = tilt_entry_live ? batch->state.guard_tilt_x8[idx]
                                   : batch->state.guard_tilt_x8_frame_start[idx];
      if (f > frame_max) {
        f = frame_max;
      }
      float tilt_mag = tilt_entry_live ? batch->state.guard_tilt_x4[idx]
                                       : batch->state.guard_tilt_x4_frame_start[idx];
      if (!(tilt_mag > 0.0f)) {
        tilt_mag = 0.0f;
      }
      if (tilt_mag > 1.0f) {
        tilt_mag = 1.0f;
      }
      const size_t n_i = (size_t)tv.neutral_frame * 3u;
      const size_t f_i = (size_t)f * 3u;
      const float ddx = tilt_mag * (tv.xyz[f_i + 0] - tv.xyz[n_i + 0]);
      const float ddy = tilt_mag * (tv.xyz[f_i + 1] - tv.xyz[n_i + 1]);
      const float ddz = tilt_mag * (tv.xyz[f_i + 2] - tv.xyz[n_i + 2]);
      const float tilt_scale_y = batch->state.fighter_scale_y[idx];
      const float tilt_facing = batch->state.facing[idx] ? 1.0f : -1.0f;
      *out_x += tilt_facing * ddz * tilt_scale_y;
      *out_y += ddy * tilt_scale_y;
      *out_z -= tilt_facing * ddx * tilt_scale_y;
    }
  }
  return 1u;
}

static inline uint8_t item_guard_reflect_bone_y(const MslBatch* batch, size_t idx, float* out_y) {
  if (batch == NULL || out_y == NULL) {
    return 0u;
  }
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  if (ch == NULL) {
    return 0u;
  }

  uint16_t msid = 0u;
  switch (batch->state.action_id[idx]) {
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD_REFLECT:
      msid = (uint16_t)MSL_SM_GUARD_ON;
      break;
    case MSL_ACT_GUARD:
      msid = (uint16_t)MSL_SM_GUARD;
      break;
    default:
      return 0u;
  }

  float m[12];
  if (anim_pose_get_matrix(batch->state.char_id[idx], msid, 0u, ch->grab_capture_anchor_part_id,
                           m) != 0) {
    return 0u;
  }
  *out_y = batch->state.pos_y[idx] + (m[7] * batch->state.fighter_scale_y[idx]);
  return 1u;
}

static inline uint8_t item_guard_seed_shield_desc_active(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  return batch->state.guard_seed_shield_desc_active[idx] ? 1u : 0u;
}

static inline uint8_t item_no_submotion_guard_shielddesc_root_x_owner(const MslBatch* batch,
                                                                      size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] != UINT32_MAX ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  const uint8_t flags_221b =
      batch->state
          .state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX];
  return (flags_221b & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) != 0u ? 1u : 0u;
}

static inline uint8_t item_laser_no_submotion_guard_entry_shielddesc_root_x_owner(
    const MslBatch* batch, size_t d_idx, uint8_t shield_bounce_seed_valid) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] != UINT32_MAX ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  const uint8_t flags_221b =
      batch->state
          .state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX];
  if ((flags_221b & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) == 0u) {
    return 0u;
  }
  if (shield_bounce_seed_valid == 0u) {
    return 0u;
  }
  // No-submotion GuardReflect ShieldBounced root-x owner:
  // Slippi can serialize GuardReflect with animation_index=-1 while the current item callback is
  // still using the source ShieldDesc JObj rebuilt by GuardReflect callback code. The replay-visible
  // shield bubble can carry a guard-bone horizontal sample from a different callback boundary; retry
  // only the ShieldDesc item lane with the fighter root X while retaining the source shield
  // height/radius. Require explicit hidden ShieldBounced seed provenance so active-window keepalive
  // controls do not become generic shield-hit publications.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092450,ftCo_80091D58,ftCo_80093A50}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  return 1u;
}

static inline float item_laser_reflect_hit_radius(float laser_radius, float laser_scale_z) {
  // `lbColl_80007BCC` passes `item->scl` into the item HitCapsule distance offset unless the
  // capsule opts out (`x43_b1`). Fox/Falco laser HitCapsules use the ordinary scaled lane, while
  // the beam endpoints carry the same item-owned scaleZ ramp.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
  float s = laser_scale_z;
  if (!(s > 0.0f)) {
    s = 1.0f;
  }
  return laser_radius * s;
}

static inline uint8_t item_should_commit_aged_powershield_reflect_owner(
    const MslBatch* batch, size_t item_idx, size_t d_idx, int def, float x0, float y0, float x,
    float y, float vx, float laser_age_frames, float laser_radius, float laser_scale_z) {
  (void)def;
  if (batch == NULL || !(laser_age_frames > 1.0f)) {
    return 0u;
  }
  if (batch->state.item_shield_bounce_seed_valid[item_idx]) {
    // ShieldBounced source-owner precedence:
    // when Slippi seed state exposes Item_80269DC8's shield-bounce result for this item callback,
    // the row is already owned by the HitShield/ShieldBounced lane and must not invent a separate
    // ftColl_80077464 reflected-owner snapshot from the same visible GuardReflect timers.
    // refs/melee/src/melee/it/item.c::Item_80269DC8
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077688,ftColl_80077464}
    return 0u;
  }
  const MslCommonParams* common = msl_common_params();
  if (common == NULL || !(common->powershield_reflect_size > 0.0f)) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
      batch->state.guard_reflect_timer_x14_seed[d_idx] <= 1u) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] != UINT32_MAX) {
    return 0u;
  }
  const uint8_t flags_2218 =
      batch->state
          .state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX];
  if ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) == 0u) {
    // ReflectDesc item transfer source owner:
    // ftColl_8007925C tests `fp->reflecting && hurt->x41_b7`, then probes `fp->reflect_hit` with
    // lbColl_80007BCC before ftColl_80077464 writes the pending item owner snapshot. A live
    // ShieldDesc (`fp+0x221B_b0`) and unrelated raw command bits are not preconditions for this
    // ReflectDesc branch; GuardOn-origin GuardReflect intentionally clears ShieldDesc while
    // keeping ReflectDesc live until x14 expires.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009370C,ftCo_80093BC0}
    return 0u;
  }
  if (batch->state.action_frame[d_idx] <= -2 &&
      batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
      (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_B1) == 0u) {
    // GuardOn-origin GuardReflect without raw fp+0x2218_b1 stays on the final-x14
    // ShieldDesc/HitShield handoff lane; only the x2218_b1 live-article lane may stage a carried
    // reflected-owner transfer from these frozen follow-up snapshots.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
    // refs/melee/src/melee/ft/types.h (fp+0x2218_b1)
    return 0u;
  }
  float reflect_x = 0.0f;
  float reflect_y = 0.0f;
  float reflect_z = 0.0f;
  if (!item_guard_reflect_center_xyz(batch, d_idx, &reflect_x, &reflect_y, &reflect_z)) {
    return 0u;
  }
  const float y_delta = fabsf(y - reflect_y);
  const float reflect_r = common->powershield_reflect_size * batch->state.fighter_scale_y[d_idx];
  const float hit_r = item_laser_reflect_hit_radius(laser_radius, laser_scale_z);
  // `lbColl_80007BCC` passes `shield_hit->size` and `lbColl_804D7A34 * fighter_scale`
  // into `lbColl_80006E58`; the latter uses that extent in the descriptor broad phase before the
  // exact matrix-space distance solve. Keep this reduced owner predicate on the same source extent
  // instead of a replay-fit vertical band.
  enum { MSL_LBCOLL_HIT_RESULT_EXTENT = 20 };
  const float reflect_extent = reflect_r * (float)MSL_LBCOLL_HIT_RESULT_EXTENT;
  const float scaled_lane_r = hit_r + reflect_extent;
  if (y_delta > scaled_lane_r) {
    return 0u;
  }
  if (!item_swept_sphere_sphere_intersects_3d(x0, y0, 0.0f, x, y, 0.0f, hit_r, reflect_x, reflect_y,
                                              reflect_z, reflect_extent)) {
    return 0u;
  }
  const uint8_t exact_reflect_hit = item_swept_sphere_sphere_intersects_3d(
      x0, y0, 0.0f, x, y, 0.0f, hit_r, reflect_x, reflect_y, reflect_z, reflect_r);
  const uint8_t carried_guardreflect_followup =
      (batch->state.action_frame[d_idx] <= -2 &&
       batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       (flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_B1) != 0u)
          ? 1u
          : 0u;
  const uint8_t post_crossing_side = (((x - reflect_x) * vx) > 0.0f) ? 1u : 0u;
  if (batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_LANDING && !exact_reflect_hit) {
    return 0u;
  }
  if (carried_guardreflect_followup && !exact_reflect_hit) {
    return 0u;
  }
  if (!post_crossing_side && !carried_guardreflect_followup) {
    return 0u;
  }
  // The remaining aged-transfer rows are on the item-HitCapsule vs ReflectDesc lane while rejected
  // shield-bounce / HitShield rows remain on the ShieldDesc or final-x14 handoff owners. This is
  // still item-owned geometry, not visible shield HP/timer fitting:
  // - active GuardReflect transfer requires `fp->reflecting`, not a live ShieldDesc
  //   (`fp+0x221B_b0`), because ftCo_8009388C clears ShieldDesc while ReflectDesc remains live,
  // - ReflectDesc.x14_size comes from p_ftCommonData->x2A8,
  // - `lbColl_80007BCC` scales the ISO-extracted item HitCapsule radius by `item->scl`,
  // - Landing-origin GuardReflect snapshots keep ShieldDesc live; if exact ReflectDesc contact
  //   misses there, source falls through to Item_80269DC8 HitShield instead of using the x20
  //   broad-phase extent as reflected-owner proof,
  // - x14 final-tick exclusion keeps Item_80269DC8 HitShield destruction on its owner lane.
  // - carried GuardReflect follow-up snapshots in the raw fp+0x2218_b1 live-article lane can keep
  //   the source ReflectDesc owner only when the exact x2A8 + item-HitCapsule overlap passes;
  //   fresh entry rows and final-x14 handoff rows without x2218_b1 remain on the post-crossing side
  //   gate above.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009370C,ftCo_80093BC0}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
  // refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
  return 1u;
}

static inline uint8_t item_should_keep_existing_powershield_reflect_owner(
    const MslBatch* batch, size_t d_idx, int def, int item_owner, float x0, float y0, float x,
    float y, float laser_radius, float laser_scale_z) {
  if (batch == NULL || item_owner != def) {
    return 0u;
  }
  const MslCommonParams* common = msl_common_params();
  if (common == NULL || !(common->powershield_reflect_size > 0.0f)) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] != UINT32_MAX) {
    return 0u;
  }
  const uint8_t flags_2218 =
      batch->state
          .state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX];
  if ((flags_2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECTING) == 0u) {
    return 0u;
  }
  float reflect_x = 0.0f;
  float reflect_y = 0.0f;
  float reflect_z = 0.0f;
  if (!item_guard_reflect_center_xyz(batch, d_idx, &reflect_x, &reflect_y, &reflect_z)) {
    return 0u;
  }
  const float y_delta = fabsf(y - reflect_y);
  const float reflect_r = common->powershield_reflect_size * batch->state.fighter_scale_y[d_idx];
  const float hit_r = item_laser_reflect_hit_radius(laser_radius, laser_scale_z);
  enum { MSL_LBCOLL_HIT_RESULT_EXTENT = 20 };
  const float reflect_extent = reflect_r * (float)MSL_LBCOLL_HIT_RESULT_EXTENT;
  const float scaled_lane_r = hit_r + reflect_extent;
  if (y_delta > scaled_lane_r) {
    return 0u;
  }
  return item_swept_sphere_sphere_intersects_3d(x0, y0, 0.0f, x, y, 0.0f, hit_r, reflect_x,
                                                reflect_y, reflect_z, reflect_extent);
}

static inline uint8_t item_should_commit_guardon_followup_powershield_reflect_owner(
    const MslBatch* batch, size_t item_idx, size_t d_idx, int def, int item_owner, float x0,
    float y0, float x, float y, float vx, float laser_age_frames, float laser_radius,
    float laser_scale_z) {
  if (batch == NULL || !(laser_age_frames > 0.0f)) {
    return 0u;
  }
  const MslCommonParams* common = msl_common_params();
  if (common == NULL || !(common->powershield_reflect_size > 0.0f)) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT ||
      batch->state.animation_index[d_idx] != UINT32_MAX || batch->state.action_frame[d_idx] >= 0) {
    return 0u;
  }
  const uint8_t fresh_no_seed_timer_followup =
      (batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
       batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u)
          ? 1u
          : 0u;
  const uint8_t same_owner_final_x14_followup =
      ((item_owner == def || batch->state.item_reflect_damage_mul[item_idx] != 1.0f) &&
       batch->state.guard_reflect_origin_guardon[d_idx] != 0u &&
       batch->state.guard_reflect_timer_x14_seed[d_idx] == 1u &&
       batch->state.guard_reflect_timer_x18_seed[d_idx] > 0u)
          ? 1u
          : 0u;
  const uint8_t active_guardon_reflectdesc_followup =
      (batch->state.guard_reflect_origin_guardon[d_idx] != 0u &&
       batch->state.guard_reflect_timer_x14_seed[d_idx] > 1u &&
       batch->state.guard_reflect_timer_x18_seed[d_idx] > 0u &&
       batch->state.guard_reflect_timer_x14[d_idx] > 0u)
          ? 1u
          : 0u;
  if (fresh_no_seed_timer_followup == 0u && same_owner_final_x14_followup == 0u &&
      active_guardon_reflectdesc_followup == 0u) {
    return 0u;
  }
  const uint8_t same_step_guardon_reflect_entry =
      (batch->state.guard_reflect_origin_guardon[d_idx] != 0u &&
       batch->state.guard_reflect_entered_this_frame[d_idx] != 0u)
          ? 1u
          : 0u;
  const uint8_t guardon_reflect_source_latch =
      batch->state.guard_on_entry_reflect_source_latch[d_idx] != 0u ? 1u : 0u;
  if (laser_age_frames <= 1.0f && guardon_reflect_source_latch == 0u) {
    return 0u;
  }
  if (same_step_guardon_reflect_entry != 0u &&
      batch->state.seed_prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON &&
      guardon_reflect_source_latch == 0u) {
    return 0u;
  }
  if (batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
      batch->state.seed_prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON &&
      guardon_reflect_source_latch == 0u) {
    return 0u;
  }
  if (same_step_guardon_reflect_entry == 0u &&
      batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON &&
      batch->state.seed_prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON) {
    return 0u;
  }
  if (!(((x - batch->state.pos_x[d_idx]) * vx) < 0.0f)) {
    return 0u;
  }

  float reflect_x = 0.0f;
  float reflect_y = 0.0f;
  float reflect_z = 0.0f;
  if (!item_guard_reflect_center_xyz(batch, d_idx, &reflect_x, &reflect_y, &reflect_z)) {
    return 0u;
  }
  const float reflect_r = common->powershield_reflect_size * batch->state.fighter_scale_y[d_idx];
  // GuardOn follow-up reflect descriptor:
  // - GuardOn IASA can enter GuardReflect through ftCo_8009388C, which sets x221C powershield
  //   bits, initializes mv.co.guard.x14/x18, and immediately installs ReflectDesc with
  //   ftCo_8009370C.
  // - The laser is already in its item collision callback once the first post-anim age is visible;
  //   newly spawned age-zero articles stay on their separate spawn-frame owner helpers.
  // - Final-x14 rows can keep the reflected-owner transfer only when the item is already defender-
  //   owned or the seeded item-reflect damage multiplier proves an existing reflected-owner
  //   snapshot. Opponent-owned/default-multiplier final-x14 rows stay on the HitShield handoff lane.
  // - Runtime same-step GuardOn -> GuardReflect rows expose the source entry through
  //   `guard_reflect_origin_guardon && guard_reflect_entered_this_frame`, plus the retained
  //   Landing-entry GuardOn latch when the visible intermediate action is GuardOn.
  //   Teacher-forced rows may only have the prior GuardOn action snapshot. All lanes require full
  //   ReflectDesc sweep overlap, not timer-only replay fitting or a Y-only band.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_8009388C,ftCo_8009370C}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
  // `lbColl_80007BCC` uses the item HitCapsule sweep and the same broad source extent consumed by
  // the aged ReflectDesc lane before ftColl_80077464 writes the reflected-owner snapshot.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  // refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}
  const float hit_r = item_laser_reflect_hit_radius(laser_radius, laser_scale_z);
  enum { MSL_LBCOLL_HIT_RESULT_EXTENT = 20 };
  const float reflect_extent = reflect_r * (float)MSL_LBCOLL_HIT_RESULT_EXTENT;
  const float scaled_lane_r = hit_r + reflect_extent;
  if (fabsf(y - reflect_y) > scaled_lane_r) {
    return 0u;
  }
  if (!item_swept_sphere_sphere_intersects_3d(x0, y0, 0.0f, x, y, 0.0f, hit_r, reflect_x, reflect_y,
                                              reflect_z, reflect_extent)) {
    return 0u;
  }
  if (active_guardon_reflectdesc_followup != 0u || same_owner_final_x14_followup != 0u) {
    return item_swept_sphere_sphere_intersects_3d(x0, y0, 0.0f, x, y, 0.0f, hit_r, reflect_x,
                                                  reflect_y, reflect_z, reflect_r);
  }
  return 1u;
}

typedef enum MslLaserCollisionSpaceLane {
  MSL_LASER_COLLISION_SPACE_SHIELD = 0,
  MSL_LASER_COLLISION_SPACE_REFLECT = 1,
  MSL_LASER_COLLISION_SPACE_BODY = 2,
} MslLaserCollisionSpaceLane;

static inline float laser_collision_offset_scale(const MslLaserParams* lp, uint8_t laser_state,
                                                 float laser_scale_z,
                                                 MslLaserCollisionSpaceLane lane,
                                                 uint8_t body_shield_adjacent,
                                                 uint8_t shield_cap_enabled) {
  (void)lp;
  (void)laser_state;
  // Collision-space policy (decomp/data-owned, shared ownership lanes):
  // - Hitbox offsets are extracted authored lanes from the article script (`create_hitbox` x_offset):
  //   tools/extraction/extract_lasers.py
  // - Laser motion anim applies a per-frame model scaleZ ramp:
  //   refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
  // - Shield overlap, BODY overlap, and reflector overlap all consume the same item-owned collision
  //   space (hitcapsule world positions/radii) under fighter/item collision ownership:
  //   refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  //   refs/melee/src/melee/it/itcoll.c::it_80272460
  //
  // Decomp-backed policy: apply one transform chain for all overlap lanes; do not inject
  // shield/body/reflect-specific clamps.
  float s = laser_scale_z;
  if (!(s > 0.0f)) {
    // Seeded public item rows may predate the first laser Anim callback and therefore lack a
    // serialized scaleZ byte. The source first Anim callback floors the live scale to 1e-3, but a
    // missing public seed lane is an unknown scale rather than a real zero-width HitCapsule. Use
    // identity scale for that reconstruction boundary; live rollout rows consume the computed
    // itFoxlaser_UnkMotion1_Anim scale above.
    // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
    // refs/melee/src/melee/it/itcoll.c::it_8027137C
    s = 1.0f;
  }
  // Shield and shield-adjacent BODY admission stay on the reduced descriptor sample unless the
  // caller proves a fresh ShieldDesc/source HitCapsule owner. The full x58/x4C scale lane is
  // consumed after shield admission by ShieldBounced/ReflectDesc source paths; using it for every
  // ShieldDesc admission over-admits Guard/GuardReflect controls.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80077688}
  // refs/melee/src/melee/it/item.c::Item_80269DC8
  if ((lane == MSL_LASER_COLLISION_SPACE_SHIELD ||
       (lane == MSL_LASER_COLLISION_SPACE_BODY && body_shield_adjacent)) &&
      shield_cap_enabled && s > 1.0f) {
    s = 1.0f;
  }
  return s;
}

static inline uint8_t yoshi_shyguy_state_accepts_item_damage(uint8_t state) {
  // Heiho active and return-flight states keep enabled item hurtboxes. Knocked states 2/3 are
  // already in the damage callback lifecycle and state 0 is spawn-delay/hidden motion.
  // refs/melee/src/melee/it/items/itheiho.c::it_803F83F0
  // refs/melee/src/melee/it/itcoll.c::it_8027163C
  return (state == 1u || state == 4u) ? 1u : 0u;
}

static inline uint8_t yoshi_shyguy_seed_return_flight_same_action_rehit_suppresses(
    const MslBatch* batch, size_t p_idx, size_t shy_idx, size_t hb_i) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.item_state[shy_idx] != 4u || batch->state.item_damage[shy_idx] == 0u) {
    return 0u;
  }
  if (batch->state.hitlag[p_idx] != 0u || batch->state.hitstun[p_idx] != 0u) {
    return 0u;
  }
  if (batch->state.hitbox_enable_edge[hb_i] != 0u) {
    return 0u;
  }
  // Teacher-forced reseed can restore Heiho's low-damage return-flight state without serializing
  // the fighter HitCapsule's item victim pointer. If the same fighter action is continuing, source
  // ftColl_80076808's victims_1 latch from the prior it_802703E8 hit still suppresses continuing
  // same-group HitCapsules from damaging the same item again. A hitbox enable edge is a new
  // source-created HitCapsule, so same action/state4/damaged-item alone is not enough to infer the
  // victims_1 entry. This bridge only materializes the missing item victim lane; all suppression
  // still flows through hitlist_allows_fighter_item below.
  // refs/melee/src/melee/it/itcoll.c::it_802703E8
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076808
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
  // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,it_802D9168}
  return (batch->state.seed_prev_action_id[p_idx] == batch->state.action_id[p_idx]) ? 1u : 0u;
}

static inline float yoshi_shyguy_fighter_hitcapsule_damage(const MslBatch* batch, size_t p_idx,
                                                           size_t hb_i) {
  if (batch == NULL) {
    return 0.0f;
  }
  // Fighter HitCapsule -> item hurtbox damage consumes HitCapsule.damage, not a fresh live stale
  // lookup after this same attack has already inserted into the stale queue. Prefer the explicit
  // frozen HitCapsule lane; otherwise exclude the current attack instance while older instances of
  // the same move stale normally.
  // refs/melee/src/melee/it/itcoll.c::it_802703E8
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0
  // refs/melee/src/melee/ft/ft_0881.c::{ft_80089118,ft_80089228}
  float damage = batch->state.hitbox_damage[hb_i];
  float stale_mult = 1.0f;
  if (batch->state.hitbox_stale_damage_valid[hb_i] != 0u &&
      batch->state.hitbox_stale_damage_mul[hb_i] > 0.0f) {
    stale_mult = batch->state.hitbox_stale_damage_mul[hb_i];
  } else {
    const uint16_t move_id = staling_move_id_from_state(batch, p_idx);
    const uint16_t attack_instance = batch->state.attack_instance[p_idx];
    stale_mult =
        staling_multiplier_for_move_excluding_instance(batch, p_idx, move_id, attack_instance);
  }
  if (stale_mult != 1.0f) {
    damage *= stale_mult;
  }
  return damage;
}

typedef struct MslYoshiShyguyKnockedSource {
  int attacker;
  size_t attacker_idx;
  size_t hitbox_idx;
  uint8_t hitbox_id;
  uint8_t hit_group;
  uint8_t rehit_frames;
  float damage;
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t element;
} MslYoshiShyguyKnockedSource;

static uint8_t yoshi_shyguy_select_knocked_state_source_hitbox(MslBatch* batch, int bi,
                                                               int shyguy_slot,
                                                               const MslYoshiShyguyParams* params,
                                                               MslYoshiShyguyKnockedSource* out) {
  if (out != NULL) {
    memset(out, 0, sizeof(*out));
    out->attacker = -1;
  }
  if (batch == NULL || params == NULL || out == NULL || params->hurtbox_count == 0u) {
    return 0u;
  }
  const size_t shy_idx = msl_idx_item(bi, shyguy_slot);
  if (batch->state.item_exists[shy_idx] == 0u ||
      !stage_item_params_is_yoshi_shyguy_item_type(params, batch->state.item_type[shy_idx]) ||
      batch->state.item_state[shy_idx] != 3u || batch->state.item_damage[shy_idx] == 0u ||
      batch->state.item_shyguy_hitlag_valid[shy_idx] == 0u ||
      batch->state.item_shyguy_hitlag[shy_idx] == 0u) {
    return 0u;
  }

  for (int p = 0; p < (int)batch->config.num_players; p++) {
    const size_t p_idx = msl_idx_player(bi, p);
    if (batch->state.hitlag[p_idx] == 0u) {
      continue;
    }
    for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
      const size_t hb_i = idx_hitbox(bi, p, hb);
      if (!batch->state.hitbox_enabled[hb_i]) {
        continue;
      }
      const uint16_t flags = batch->state.hitbox_flags[hb_i];
      if ((flags & (uint16_t)MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION) == 0u ||
          (flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
        continue;
      }
      if (batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
          batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_INERT ||
          !(batch->state.hitbox_damage[hb_i] > 0.0f)) {
        continue;
      }
      const float hx1 = batch->state.hitbox_x[hb_i];
      const float hy1 = batch->state.hitbox_y[hb_i];
      const float hz1 = batch->state.hitbox_z[hb_i];
      const float hx0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_x[hb_i] : hx1;
      const float hy0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_y[hb_i] : hy1;
      const float hz0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_z[hb_i] : hz1;
      const float hr = batch->state.hitbox_radius[hb_i];
      for (uint8_t hi = 0; hi < params->hurtbox_count && hi < 2u; hi++) {
        const float ax = batch->state.item_pos_x[shy_idx] + params->hurtbox_a_offset[hi][0];
        const float ay = batch->state.item_pos_y[shy_idx] + params->hurtbox_a_offset[hi][1];
        const float az = params->hurtbox_a_offset[hi][2];
        const float bx = batch->state.item_pos_x[shy_idx] + params->hurtbox_b_offset[hi][0];
        const float by = batch->state.item_pos_y[shy_idx] + params->hurtbox_b_offset[hi][1];
        const float bz = params->hurtbox_b_offset[hi][2];
        const float rr = hr + params->hurtbox_scale[hi] * params->collision_ecb_scale;
        const float d2 =
            item_segment_segment_dist2(hx0, hy0, hz0, hx1, hy1, hz1, ax, ay, az, bx, by, bz);
        if (d2 > rr * rr) {
          continue;
        }

        out->attacker = p;
        out->attacker_idx = p_idx;
        out->hitbox_idx = hb_i;
        out->hitbox_id = (uint8_t)hb;
        out->hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        out->rehit_frames = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        out->damage = yoshi_shyguy_fighter_hitcapsule_damage(batch, p_idx, hb_i);
        out->angle = batch->state.hitbox_angle[hb_i];
        out->kbg = batch->state.hitbox_kbg[hb_i];
        out->wsk = batch->state.hitbox_wsk[hb_i];
        out->bkb = batch->state.hitbox_bkb[hb_i];
        out->element = batch->state.hitbox_element[hb_i];
        return 1u;
      }
    }
  }
  return 0u;
}

static uint8_t yoshi_shyguy_try_knocked_state_fighter_body_hit(MslBatch* batch, int bi,
                                                               int shyguy_slot,
                                                               const MslYoshiShyguyParams* params) {
  MslYoshiShyguyKnockedSource src;
  if (!yoshi_shyguy_select_knocked_state_source_hitbox(batch, bi, shyguy_slot, params, &src)) {
    return 0u;
  }
  const size_t shy_idx = msl_idx_item(bi, shyguy_slot);
  if (!(src.damage > 0.0f) || !batch->state.hitbox_enabled[src.hitbox_idx]) {
    return 0u;
  }
  const float hx = batch->state.hitbox_x[src.hitbox_idx];
  const float hy = batch->state.hitbox_y[src.hitbox_idx];
  const float hz = batch->state.hitbox_z[src.hitbox_idx];
  const float hr = batch->state.hitbox_radius[src.hitbox_idx];
  const float px = batch->state.hitbox_prev_enabled[src.hitbox_idx]
                       ? batch->state.hitbox_prev_x[src.hitbox_idx]
                       : hx;
  const float py = batch->state.hitbox_prev_enabled[src.hitbox_idx]
                       ? batch->state.hitbox_prev_y[src.hitbox_idx]
                       : hy;
  const float pz = batch->state.hitbox_prev_enabled[src.hitbox_idx]
                       ? batch->state.hitbox_prev_z[src.hitbox_idx]
                       : hz;
  const uint16_t src_flags = batch->state.hitbox_flags[src.hitbox_idx];

  for (int def = 0; def < (int)batch->config.num_players; def++) {
    if (def == src.attacker) {
      continue;
    }
    const size_t d_idx = msl_idx_player(bi, def);
    if (batch->state.hitlag[d_idx] != 0u ||
        batch->state.hurtbox_state[d_idx] == (uint8_t)MSL_HURTCAPS_DISABLED ||
        !hitlist_allows_fighter(batch, bi, src.attacker, src.hitbox_id, def,
                                batch->state.instance_id[d_idx])) {
      continue;
    }
    if (batch->state.on_ground[d_idx]) {
      if ((src_flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) == 0u) {
        continue;
      }
    } else if ((src_flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
      continue;
    }
    uint8_t hurt_height = 1u;
    uint8_t body_contact = 0u;
    for (int cap = 0; cap < MSL_MAX_HURTCAPS; cap++) {
      if (cap >= (int)batch->state.hurtcap_count[d_idx]) {
        break;
      }
      const size_t cap_i = idx_hurtcap(bi, def, cap);
      if (!batch->state.hurtcap_enabled[cap_i]) {
        continue;
      }
      float d2 = 0.0f;
      combat_segment_segment_dist2(px, py, pz, hx, hy, hz, batch->state.hurtcap_a_x[cap_i],
                                   batch->state.hurtcap_a_y[cap_i], batch->state.hurtcap_a_z[cap_i],
                                   batch->state.hurtcap_b_x[cap_i], batch->state.hurtcap_b_y[cap_i],
                                   batch->state.hurtcap_b_z[cap_i], &d2, NULL, NULL);
      const float rr = hr + batch->state.hurtcap_radius[cap_i];
      if (d2 <= rr * rr) {
        hurt_height = batch->state.hurtcap_height[cap_i];
        body_contact = 1u;
        break;
      }
    }
    if (body_contact == 0u) {
      continue;
    }

    // Knocked Heiho state-3 BODY source:
    // - A fighter HitCapsule hit against the item hurtbox records the source fighter in
    //   item->xCB0/xCEC and item damage provenance in it_80270E30.
    // - While the low-damage state-3 item is still in source hitlag, later fighter BODY contact
    //   uses that carried source owner, not the stage/environment sentinel.
    // - Slippi exposes the carried owner through the victim's last_hit_by/instance_hit_by and the
    //   source fighter's active hitlag, but not the item xCB0/xCEC fields themselves.
    // refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_80270E30}
    // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,itHeiho_UnkMotion3_Phys}
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076808,ftColl_80076ED8}
    const MslItemHitResult res = combat_apply_item_hit(
        batch, bi, src.attacker, def, batch->state.attack_id[src.attacker_idx],
        batch->state.attack_instance[src.attacker_idx], batch->state.instance_id[src.attacker_idx],
        batch->state.item_type[shy_idx], batch->state.item_state[shy_idx], src.damage, src.angle,
        src.kbg, src.wsk, src.bkb, hurt_height, src.element, 1.0f,
        // Knocked Shy Guy Counter geometry: the contact capsule is the carried source hitbox
        // (hx/hy with radius hr, the same sweep tested against the defender's hurtcaps above),
        // so the descriptor sphere sees the real contact position/radius.
        hx, hy, hr, batch->state.item_vel_x[shy_idx], 1u);
    if (res == MSL_ITEM_HIT_NONE) {
      continue;
    }
    const int damage_i = (int)src.damage;
    combat_apply_deal_hitlag_raw_damage(batch, src.attacker_idx, damage_i);
    hitlist_register_fighter_group(batch, bi, src.attacker, src.hit_group, def,
                                   batch->state.instance_id[d_idx], (int)MSL_LBCOLL_INSERT_FT_BODY,
                                   src.rehit_frames);
    return 1u;
  }
  return 0u;
}

static inline void yoshi_shyguy_seed_return_flight_publish_fresh_prefix_velocity(
    MslBatch* batch, size_t shy_idx, const MslYoshiShyguyParams* params) {
  if (batch == NULL || params == NULL || batch->state.item_state[shy_idx] != 4u ||
      batch->state.item_shyguy_delay_valid[shy_idx] == 0u ||
      batch->state.item_shyguy_delay[shy_idx] < 18u) {
    return;
  }
  // Fresh return-flight prefix after the low-damage callback: it_802D9168 initializes x24=20 and
  // state-4 Anim/Phys publish the source speed/dynamic-bone velocity for the immediate carried
  // item-hitlist rows. Later x24 rows stay on the broader return-flight export owner below.
  // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,it_802D9168,
  //   itHeiho_UnkMotion4_Anim,itHeiho_UnkMotion4_Phys}
  if (batch->state.item_shyguy_speed_index_valid[shy_idx] != 0u) {
    const uint8_t speed_index =
        batch->state.item_shyguy_speed_index[shy_idx] % MSL_YOSHI_SHYGUY_SPEED_COUNT;
    batch->state.item_vel_x[shy_idx] = params->speed[speed_index] * params->state4_speed_mul *
                                       batch->state.item_direction[shy_idx];
  }
  if (batch->state.item_shyguy_dyn_y_phase_valid[shy_idx] != 0u) {
    const uint8_t phase_before =
        (uint8_t)((batch->state.item_shyguy_dyn_y_phase[shy_idx] + 255u) & 0xFFu);
    batch->state.item_vel_y[shy_idx] = yoshi_shyguy_vel_y_from_phase(params, phase_before, 4u);
  }
}

static inline float yoshi_shyguy_item_kb_applied(const MslCommonParams* c,
                                                 const MslYoshiShyguyParams* params, uint16_t kbg,
                                                 uint16_t wsk, uint16_t bkb, int damage_i) {
  if (c == NULL || params == NULL) {
    return 0.0f;
  }
  // Item-vs-item damage intake uses the ItemCommonData knockback expression in it_80270E30. The
  // constants share the same promoted ftColl KB lanes here; Heiho contributes its Article
  // ItemAttr.x1C damage multiplier extracted into MSLSTIO1.
  // refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_80270E30}
  // refs/melee/src/melee/it/types.h::ItemAttr::x1C_damage_mul
  const float kbg_scale = 0.01f * (float)kbg;
  const float bkb_f = (float)bkb;
  float kb = 0.0f;
  if (wsk != 0u) {
    const float pre = params->damage_mul * ((c->kb_base_term * c->kb_wsk_mul) +
                                            (c->kb_dmg_mul * (c->kb_wsk_mul * (float)wsk)));
    const float inner = c->kb_growth_mul * pre + c->kb_base_add;
    kb = bkb_f + kbg_scale * inner;
  } else {
    const float d = (float)damage_i;
    const float pre = params->damage_mul * ((c->kb_base_term * d) + (c->kb_dmg_mul * d * d));
    const float inner = c->kb_growth_mul * pre + c->kb_base_add;
    kb = bkb_f + kbg_scale * inner;
  }
  if (kb > c->kb_applied_max) {
    kb = c->kb_applied_max;
  }
  return kb;
}

static inline void yoshi_shyguy_apply_damage_common(MslBatch* batch, size_t shy_idx,
                                                    const MslYoshiShyguyParams* params,
                                                    uint16_t angle, uint16_t kbg, uint16_t wsk,
                                                    uint16_t bkb, float damage_f, float dir) {
  const MslCommonParams* c = msl_common_params();
  int damage_i = (int)damage_f;
  if (damage_i < 0) {
    damage_i = 0;
  }
  uint16_t damage_total = batch->state.item_damage[shy_idx];
  if (damage_total <= (uint16_t)(999u - (uint16_t)damage_i)) {
    damage_total = (uint16_t)(damage_total + (uint16_t)damage_i);
  } else {
    damage_total = 999u;
  }
  batch->state.item_damage[shy_idx] = damage_total;

  const float kb = yoshi_shyguy_item_kb_applied(c, params, kbg, wsk, bkb, damage_i);
  const float kb_vel_mag = (c != NULL) ? (kb * c->kb_vel_mul) : 0.0f;
  const float angle_rad = (angle == 361u && c != NULL) ? c->sakurai_air_radians
                                                       : (0.01745329251994329577f * (float)angle);
  const float vx = kb_vel_mag * cosf(angle_rad);
  const float vy = kb_vel_mag * sinf(angle_rad);

  if (params->damage_threshold != 0u &&
      damage_total > (uint16_t)((float)params->damage_threshold * 0.8f)) {
    // High-damage branch enters state 2 and adds random X scatter. The global RNG stream for this
    // branch is not yet owned, so do not fabricate the scatter velocity here.
    // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,it_802D8EC8_inline}
    batch->state.item_state[shy_idx] = 2u;
  } else {
    // Low-damage branch: it_8027B798 writes KB velocity, x24 becomes 12, and state 3 is entered.
    // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
    // refs/melee/src/melee/it/it_2725.c::it_8027B798
    batch->state.item_state[shy_idx] = 3u;
    batch->state.item_vel_x[shy_idx] = -vx * dir;
    batch->state.item_vel_y[shy_idx] = vy;
    batch->state.item_direction[shy_idx] = dir;
    batch->state.item_shyguy_delay[shy_idx] = 12u;
    batch->state.item_shyguy_delay_valid[shy_idx] = 1u;
  }

  const MslItemCommonParams* item_common = msl_item_common_params();
  const float item_hitlag =
      (item_common != NULL)
          ? (item_common->item_hitlag_base + item_common->item_hitlag_damage_mul * (float)damage_i)
          : 0.0f;
  if (item_hitlag > 0.0f) {
    const uint8_t frames = (uint8_t)item_hitlag;
    if (frames > 1u) {
      // Heiho damage callback stores item hitlag, then the source item loop's hitlag/timer pass
      // exposes the post-decrement value on the next replay row. Keep live runtime aligned with
      // the native seed lane so knocked Shy Guys resume their state-3 physics on the same frame as
      // vanilla instead of stale-freezing one extra step.
      // refs/melee/src/melee/it/item.c::{Item_802693E4,Item_802697D4}
      // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,itHeiho_UnkMotion3_Phys}
      const uint8_t visible_frames = (uint8_t)(frames - 1u);
      batch->state.item_hitlag[shy_idx] = visible_frames;
      batch->state.item_shyguy_hitlag[shy_idx] = visible_frames;
      batch->state.item_shyguy_hitlag_valid[shy_idx] = 1u;
    }
  }
  batch->state.item_shyguy_prev_vel_y_valid[shy_idx] = 0u;
  batch->state.item_shyguy_dyn_y_phase_valid[shy_idx] = 0u;
}

static inline void yoshi_shyguy_apply_item_damage(MslBatch* batch, size_t shy_idx, size_t laser_idx,
                                                  const MslYoshiShyguyParams* params,
                                                  const MslLaserParams* lp, uint8_t laser_state,
                                                  float damage_f) {
  // Caller supplies the item-owned reflected damage lane before Heiho dmg_received consumes it.
  // refs/melee/src/melee/it/item.c::Item_80269F14
  // refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_80270E30}
  // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
  const uint16_t angle = (laser_state == 0u) ? lp->angle : lp->state1_angle;
  const uint16_t kbg = (laser_state == 0u) ? lp->kbg : lp->state1_kbg;
  const uint16_t wsk = (laser_state == 0u) ? lp->wsk : lp->state1_wsk;
  const uint16_t bkb = (laser_state == 0u) ? lp->bkb : lp->state1_bkb;
  const float laser_vx = batch->state.item_vel_x[laser_idx];
  const float dir =
      (fabsf(laser_vx) < 0.0001f)
          ? ((batch->state.item_pos_x[shy_idx] > batch->state.item_pos_x[laser_idx]) ? -1.0f : 1.0f)
          : ((laser_vx < 0.0f) ? 1.0f : -1.0f);
  yoshi_shyguy_apply_damage_common(batch, shy_idx, params, angle, kbg, wsk, bkb, damage_f, dir);
}

static int yoshi_shyguy_laser_item_hit_deferred_to_throw_fighter_body(const MslBatch* batch, int bi,
                                                                      size_t laser_idx,
                                                                      const MslLaserParams* lp,
                                                                      uint8_t laser_state,
                                                                      float vx) {
  if (batch == NULL || lp == NULL || laser_state == 0u ||
      !item_type_is_fox_laser(batch->state.item_type[laser_idx])) {
    return -1;
  }
  const int owner = (int)batch->state.item_owner[laser_idx];
  if (owner < 0 || owner >= (int)batch->config.num_players) {
    return -1;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  if (batch->state.action_id[o_idx] != (uint16_t)MSL_ACT_THROW_HI) {
    return -1;
  }
  int16_t first_pulse_af = 0;
  int16_t last_pulse_af = 0;
  if (!move_tables_throw_projectile_first_pulse_frame(
          batch->state.char_id[o_idx], batch->state.action_id[o_idx], &first_pulse_af) ||
      !move_tables_throw_projectile_last_pulse_frame(
          batch->state.char_id[o_idx], batch->state.action_id[o_idx], &last_pulse_af) ||
      batch->state.throw_pulse_crossed_prev_frame[o_idx] != (uint8_t)last_pulse_af) {
    return -1;
  }

  int candidate = -1;
  const int num_players = (int)batch->config.num_players;
  for (int vp = 0; vp < num_players; vp++) {
    if (vp == owner) {
      continue;
    }
    const size_t v_idx = msl_idx_player(bi, vp);
    if (batch->state.hitlag[v_idx] != 0u || batch->state.hitstun[v_idx] == 0u ||
        (!items_action_is_damage_family(batch->state.action_id[v_idx]) &&
         !msl_damage_owner_is_damagefly_action(batch->state.action_id[v_idx])) ||
        !msl_damage_source_victim_port_matches_attacker(batch, v_idx, o_idx, owner) ||
        batch->state.last_attack_landed[v_idx] != (uint8_t)first_pulse_af ||
        ((batch->state.pos_x[v_idx] - batch->state.pos_x[o_idx]) * vx) > 0.0f) {
      continue;
    }
    if (candidate >= 0) {
      return -1;
    }
    candidate = vp;
  }
  if (candidate < 0) {
    return -1;
  }

  // Fox ThrowHi final-pulse fighter callback before Shy Guy item contact:
  // - The final ThrowHi set_throw_spawn_projectile pulse is source-owned by the extracted script
  //   (18/20/24). ftFx_Throw_Anim spawns the state1 Fox laser through it_8029C6CC.
  // - Fighter_8006CB94 / ftColl_8007925C owns item-vs-fighter contact before the laser item
  //   collision callback reaches stage-item contact. In the final-pulse carry frame, the unique
  //   victim is already in same-source DamageFly hitstun from the first pulse and remains on the
  //   non-projectile side; Shy Guy item contact must not clear the shot before the next BODY
  //   callback consumes it.
  // - This is not a replay row key: it requires extracted ThrowHi pulse order, Fox shot data,
  //   source damage provenance, first-pulse item attack id, and unique current victim ownership.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
  // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
  // data/moves/fox.json moves["ftCo_SM_ThrowHi"].events
  return candidate;
}

static uint8_t yoshi_shyguy_try_laser_item_hit(MslBatch* batch, int bi, int laser_slot,
                                               const MslLaserParams* lp, uint8_t laser_state,
                                               float x0, float y0, float x, float y, float ux,
                                               float uy, float sr, float laser_prev_scale_z,
                                               float laser_scale_z) {
  const MslYoshiShyguyParams* params = stage_item_params_yoshi_shyguy();
  if (batch == NULL || lp == NULL || params == NULL ||
      batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_YOSHIS_STORY ||
      params->hurtbox_count == 0u) {
    return 0u;
  }
  const size_t laser_idx = msl_idx_item(bi, laser_slot);
  const uint8_t off_n =
      (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
  const float cur_offset_scale = laser_collision_offset_scale(
      lp, laser_state, laser_scale_z, MSL_LASER_COLLISION_SPACE_BODY, 0u, 1u);
  const float prev_offset_scale = laser_collision_offset_scale(
      lp, laser_state, laser_prev_scale_z, MSL_LASER_COLLISION_SPACE_BODY, 0u, 1u);
  const int deferred_fighter_body_victim =
      yoshi_shyguy_laser_item_hit_deferred_to_throw_fighter_body(
          batch, bi, laser_idx, lp, laser_state, batch->state.item_vel_x[laser_idx]);
  if (deferred_fighter_body_victim >= 0) {
    batch->state.item_hidden_body_hit_victim_port[laser_idx] =
        (uint8_t)deferred_fighter_body_victim;
    batch->state.item_hidden_body_hit_hurt_height[laser_idx] = 1u;
    batch->state.item_hidden_callback_flags[laser_idx] = (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_CLEAR;
    return 0u;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    if (it == laser_slot) {
      continue;
    }
    const size_t shy_idx = msl_idx_item(bi, it);
    if (!batch->state.item_exists[shy_idx] ||
        !stage_item_params_is_yoshi_shyguy_item_type(params, batch->state.item_type[shy_idx]) ||
        !yoshi_shyguy_state_accepts_item_damage(batch->state.item_state[shy_idx])) {
      continue;
    }
    for (uint8_t hi = 0; hi < params->hurtbox_count && hi < 2u; hi++) {
      const float ax = batch->state.item_pos_x[shy_idx] + params->hurtbox_a_offset[hi][0];
      const float ay = batch->state.item_pos_y[shy_idx] + params->hurtbox_a_offset[hi][1];
      const float az = params->hurtbox_a_offset[hi][2];
      const float bx = batch->state.item_pos_x[shy_idx] + params->hurtbox_b_offset[hi][0];
      const float by = batch->state.item_pos_y[shy_idx] + params->hurtbox_b_offset[hi][1];
      const float bz = params->hurtbox_b_offset[hi][2];
      const float rr = sr + params->hurtbox_scale[hi];
      if (off_n == 0u) {
        const float d2 =
            item_segment_segment_dist2(x0, y0, 0.0f, x, y, 0.0f, ax, ay, az, bx, by, bz);
        if (d2 <= rr * rr) {
          const float base_damage = (laser_state == 0u) ? lp->damage : lp->state1_damage;
          const float damage = msl_item_reflect_damage_lane(batch, laser_idx, base_damage);
          yoshi_shyguy_apply_item_damage(batch, shy_idx, laser_idx, params, lp, laser_state,
                                         damage);
          item_slot_clear(batch, laser_idx);
          return 1u;
        }
        continue;
      }
      for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X; oi++) {
        const float off_x =
            (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
        const float sx0 = x0 + ux * off_x * prev_offset_scale;
        const float sy0 = y0 + uy * off_x * prev_offset_scale;
        const float sx = x + ux * off_x * cur_offset_scale;
        const float sy = y + uy * off_x * cur_offset_scale;
        const float d2 =
            item_segment_segment_dist2(sx0, sy0, 0.0f, sx, sy, 0.0f, ax, ay, az, bx, by, bz);
        if (d2 <= rr * rr) {
          // Source owner: item-vs-item hitbox against Heiho item hurtbox, then Heiho
          // OnTakeDamageThink/dmg_received callback.
          // refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_80270E30}
          // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
          const float base_damage = (laser_state == 0u) ? lp->damage : lp->state1_damage;
          const float damage = msl_item_reflect_damage_lane(batch, laser_idx, base_damage);
          yoshi_shyguy_apply_item_damage(batch, shy_idx, laser_idx, params, lp, laser_state,
                                         damage);
          item_slot_clear(batch, laser_idx);
          return 1u;
        }
      }
    }
  }
  return 0u;
}

static uint8_t yoshi_shyguy_try_fighter_hitbox_hit(MslBatch* batch, int bi, int shyguy_slot,
                                                   const MslYoshiShyguyParams* params) {
  if (batch == NULL || params == NULL ||
      batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_YOSHIS_STORY ||
      params->hurtbox_count == 0u) {
    return 0u;
  }
  const size_t shy_idx = msl_idx_item(bi, shyguy_slot);
  if (!batch->state.item_exists[shy_idx] ||
      !stage_item_params_is_yoshi_shyguy_item_type(params, batch->state.item_type[shy_idx]) ||
      !yoshi_shyguy_state_accepts_item_damage(batch->state.item_state[shy_idx])) {
    return 0u;
  }

  for (int p = 0; p < (int)batch->config.num_players; p++) {
    const size_t p_idx = msl_idx_player(bi, p);
    for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
      const size_t hb_i = idx_hitbox(bi, p, hb);
      if (!batch->state.hitbox_enabled[hb_i]) {
        continue;
      }
      const uint16_t flags = batch->state.hitbox_flags[hb_i];
      if ((flags & (uint16_t)MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION) == 0u ||
          (flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
        continue;
      }
      if (batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
          batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_INERT ||
          !(batch->state.hitbox_damage[hb_i] > 0.0f)) {
        continue;
      }
      const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
      const uint8_t rehit_frames = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
      const uint8_t seed_rehit_suppresses =
          yoshi_shyguy_seed_return_flight_same_action_rehit_suppresses(batch, p_idx, shy_idx, hb_i);
      if (seed_rehit_suppresses) {
        yoshi_shyguy_seed_return_flight_publish_fresh_prefix_velocity(batch, shy_idx, params);
        hitlist_register_fighter_group_item(batch, bi, p, hit_group, shyguy_slot,
                                            batch->state.item_spawn_id[shy_idx],
                                            (int)MSL_LBCOLL_INSERT_FT_BODY, rehit_frames);
      }
      if (!hitlist_allows_fighter_item(batch, bi, p, hb, shyguy_slot,
                                       batch->state.item_spawn_id[shy_idx])) {
        continue;
      }

      const float hx1 = batch->state.hitbox_x[hb_i];
      const float hy1 = batch->state.hitbox_y[hb_i];
      const float hz1 = batch->state.hitbox_z[hb_i];
      const float hx0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_x[hb_i] : hx1;
      const float hy0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_y[hb_i] : hy1;
      const float hz0 =
          batch->state.hitbox_prev_enabled[hb_i] ? batch->state.hitbox_prev_z[hb_i] : hz1;
      const float hr = batch->state.hitbox_radius[hb_i];
      for (uint8_t hi = 0; hi < params->hurtbox_count && hi < 2u; hi++) {
        const float ax = batch->state.item_pos_x[shy_idx] + params->hurtbox_a_offset[hi][0];
        const float ay = batch->state.item_pos_y[shy_idx] + params->hurtbox_a_offset[hi][1];
        const float az = params->hurtbox_a_offset[hi][2];
        const float bx = batch->state.item_pos_x[shy_idx] + params->hurtbox_b_offset[hi][0];
        const float by = batch->state.item_pos_y[shy_idx] + params->hurtbox_b_offset[hi][1];
        const float bz = params->hurtbox_b_offset[hi][2];
        // Heiho item hurtboxes are tested through lbColl_8000805C with the item object's `scl`
        // scale. MSLSTIO1 exposes that source scale as collision_ecb_scale; using the raw hurtbox
        // radius over-admits near-miss fighter/item contacts at the Shy Guy edge.
        // refs/melee/src/melee/it/itcoll.c::it_802703E8
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
        // data/stage_items/yoshi_shyguy.bin::MSLSTIO1 collision_ecb_scale
        const float rr = hr + params->hurtbox_scale[hi] * params->collision_ecb_scale;
        const float d2 =
            item_segment_segment_dist2(hx0, hy0, hz0, hx1, hy1, hz1, ax, ay, az, bx, by, bz);
        if (d2 > rr * rr) {
          continue;
        }

        // Fighter-hitbox vs item-hurtbox source path:
        // `it_802703E8` tests fighter HitCapsules with x42_b7 item interaction against enabled
        // item hurtboxes via `lbColl_8000805C`, records damage with `it_8026F9AC(type=1)`, then
        // `it_80270E30` publishes incoming direction before Heiho's dmg_received callback enters
        // state 2/3 through `it_802D8EC8`.
        // refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_8026F9AC,it_80270E30}
        // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
        const float hit_damage = yoshi_shyguy_fighter_hitcapsule_damage(batch, p_idx, hb_i);
        const int hit_damage_i = (int)hit_damage;
        const float dir =
            (batch->state.item_pos_x[shy_idx] > batch->state.pos_x[p_idx]) ? -1.0f : 1.0f;
        yoshi_shyguy_apply_damage_common(
            batch, shy_idx, params, batch->state.hitbox_angle[hb_i], batch->state.hitbox_kbg[hb_i],
            batch->state.hitbox_wsk[hb_i], batch->state.hitbox_bkb[hb_i], hit_damage, dir);
        hitlist_register_fighter_group_item(batch, bi, p, hit_group, shyguy_slot,
                                            batch->state.item_spawn_id[shy_idx],
                                            (int)MSL_LBCOLL_INSERT_FT_BODY, rehit_frames);
        combat_apply_deal_hitlag_raw_damage(batch, p_idx, hit_damage_i);
        return 1u;
      }
    }
  }
  return 0u;
}

static inline uint8_t laser_try_shield_bounce_velocity_from_segment(
    float vx, float vy, float shield_x, float shield_y, float shield_z, float shield_radius,
    float prev_x, float prev_y, float prev_z, float cur_x, float cur_y, float cur_z,
    float hit_radius, float* out_vx, float* out_vy) {
  if (out_vx == NULL || out_vy == NULL) {
    return 0u;
  }

  const float dx = cur_x - prev_x;
  const float dy = cur_y - prev_y;
  const float dz = cur_z - prev_z;
  const float move2 = (dx * dx) + (dy * dy) + (dz * dz);
  if (!(move2 > 0.0f)) {
    return 0u;
  }

  const float px = prev_x - shield_x;
  const float py = prev_y - shield_y;
  const float pz = prev_z - shield_z;
  const float combined = shield_radius + hit_radius;
  const float b = 2.0f * ((dx * px) + (dy * py) + (dz * pz));
  const float c = (px * px) + (py * py) + (pz * pz) - (combined * combined);
  float disc = (b * b) - (4.0f * move2 * c);
  if (disc < 0.0f) {
    disc = 0.0f;
  }
  // Source owner: lbColl_80007DD8 -> lbColl_800077A0 writes item->xC58 from the analytic
  // capsule/shield root directly. It does not clamp the root to the visible x58->x4C segment;
  // side rejection happens later through Item_80269DC8's xC54 threshold.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007DD8,lbColl_800077A0}
  const float t = (-b - sqrtf(disc)) / (2.0f * move2);

  float nx = (prev_x + (t * dx)) - shield_x;
  float ny = (prev_y + (t * dy)) - shield_y;
  float nz = (prev_z + (t * dz)) - shield_z;
  const float n2 = (nx * nx) + (ny * ny) + (nz * nz);
  if (!(n2 > 0.0f)) {
    return 0u;
  }
  const float inv_n = 1.0f / sqrtf(n2);
  nx *= inv_n;
  ny *= inv_n;
  nz *= inv_n;

  const float move_xy2 = (dx * dx) + (dy * dy);
  if (!(move_xy2 > 0.0f)) {
    return 0u;
  }
  float cos_angle = ((nx * dx) + (ny * dy)) / sqrtf(move_xy2);
  if (cos_angle < -1.0f) {
    cos_angle = -1.0f;
  } else if (cos_angle > 1.0f) {
    cos_angle = 1.0f;
  }
  const float angle = acosf(cos_angle);
  // Item_80269DC8 uses `(90 + it_804D6D28->unk_degrees)` as the shield-bounce limit.
  // refs/melee/src/melee/it/item.c::Item_80269DC8
  // Extracted from `_iso/ItCo.dat` into `data/items/item_common.json`.
  const MslItemCommonParams* item_common = msl_item_common_params();
  const float shield_bounce_threshold =
      (item_common != NULL) ? item_common->shield_bounce_threshold_radians : 0.0f;
  if (!(angle < shield_bounce_threshold)) {
    return 0u;
  }

  const float dot = (vx * nx) + (vy * ny);
  const float rvx = vx - (2.0f * dot * nx);
  const float rvy = vy - (2.0f * dot * ny);
  (void)nz;
  *out_vx = rvx;
  *out_vy = rvy;
  return 1u;
}

static inline uint8_t item_reflector_owner_is_shine_callback_state(uint8_t char_id,
                                                                   uint16_t action_id) {
  // Ownership from the extracted MotionState row identity (Reflector Loop/Hit/Turn rows).
  switch (msl_motion_state_fx_special_kind(char_id, action_id)) {
    case MSL_FX_KIND_SPECIAL_LW_LOOP:
    case MSL_FX_KIND_SPECIAL_LW_HIT:
    case MSL_FX_KIND_SPECIAL_LW_TURN:
    case MSL_FX_KIND_SPECIAL_AIR_LW_LOOP:
    case MSL_FX_KIND_SPECIAL_AIR_LW_HIT:
    case MSL_FX_KIND_SPECIAL_AIR_LW_TURN:
      return 1u;
    default:
      return 0u;
  }
}

static inline void item_apply_shine_reflect_callback(MslBatch* batch, size_t ii,
                                                     size_t reflector_idx) {
  if (batch == NULL) {
    return;
  }
  const uint16_t action_id = batch->state.action_id[reflector_idx];
  if (!item_reflector_owner_is_shine_callback_state(batch->state.char_id[reflector_idx],
                                                    action_id)) {
    return;
  }
  const MslSpecialMsids* ms = msl_special_msids(batch->state.char_id[reflector_idx]);
  if (ms == NULL) {
    return;
  }

  // Decomp reflector callback ownership:
  // - ftColl_CreateReflectHit stores ftFx_SpecialLwHit_Enter as `fp->reflect_hit_cb`.
  // - The item reflect-overlap path writes `fp->ReflectAttr.x1A2C_reflectHitDirection` from the
  //   incoming item velocity sign, and Fighter_ProcessHit later calls `reflect_hit_cb`.
  // - ftFx_SpecialLwHit_Enter copies that direction to facing and enters grounded/aerial
  //   SpecialLwHit at frame 0, then reinstalls the reflect callback.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwHit_Enter
  const float incoming_vx = batch->state.item_vel_x[ii];
  float reflect_dir = 0.0f;
  if (incoming_vx > 0.0f) {
    reflect_dir = -1.0f;
  } else if (incoming_vx < 0.0f) {
    reflect_dir = 1.0f;
  } else {
    reflect_dir = (batch->state.item_pos_x[ii] > batch->state.pos_x[reflector_idx]) ? -1.0f : 1.0f;
  }
  batch->state.facing[reflector_idx] = (uint8_t)(reflect_dir > 0.0f);
  if (batch->state.on_ground[reflector_idx] != 0u) {
    batch->state.action_id[reflector_idx] = msl_motion_state_action_for_fx_kind(
        batch->state.char_id[reflector_idx], (uint8_t)MSL_FX_KIND_SPECIAL_LW_HIT);
    batch->state.animation_index[reflector_idx] = (uint32_t)ms->speciallw_ground_hit;
  } else {
    batch->state.action_id[reflector_idx] = msl_motion_state_action_for_fx_kind(
        batch->state.char_id[reflector_idx], (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_HIT);
    batch->state.animation_index[reflector_idx] = (uint32_t)ms->speciallw_air_hit;
  }
  msl_anim_timebase_enter(batch, reflector_idx, 0.0f, 1.0f);
}

static inline uint8_t item_stage_shine_reflected_laser_hidden_body_callback(
    MslBatch* batch, int bi, int it, size_t ii, int reflector_port, const MslLaserParams* lp,
    uint8_t laser_state, float next_x0, float next_y0, float pre_reflect_vx, float pre_reflect_vy,
    float sr, float laser_scale_z) {
  if (batch == NULL || lp == NULL || !batch->state.item_exists[ii]) {
    return 0u;
  }
  if (!(pre_reflect_vx > 0.0f || pre_reflect_vx < 0.0f || pre_reflect_vy > 0.0f ||
        pre_reflect_vy < 0.0f)) {
    return 0u;
  }

  // Hidden BODY callback owner for reflected lasers:
  // ftColl_80077464 writes the reflected owner/direction snapshot during the fighter/item contact
  // pass, while the item's callback phase (`Item_8026A294`) can consume an already-latched BODY
  // damage callback before `Item_80269F14` flips the laser velocity. Build the latch only from a
  // unique live BODY candidate under the pre-callback HitCapsule travel packet; otherwise the
  // normal immediate reflected-transfer path remains unchanged. Public item direction has already
  // been reflected by this point, but the source BODY latch is consumed before the reflected laser
  // callback rebuilds the laser velocity/angle.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_80077C60}
  // refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294,Item_80269F14}
  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_Reflected
  float ux = 0.0f;
  float uy = 0.0f;
  {
    const float dir2 = (pre_reflect_vx * pre_reflect_vx) + (pre_reflect_vy * pre_reflect_vy);
    if (!(dir2 > 0.0f)) {
      return 0u;
    }
    const float inv_dir = 1.0f / sqrtf(dir2);
    ux = pre_reflect_vx * inv_dir;
    uy = pre_reflect_vy * inv_dir;
  }
  const float next_x = next_x0 + pre_reflect_vx;
  const float next_y = next_y0 + pre_reflect_vy;
  const uint8_t off_n =
      (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
  if (off_n == 0u) {
    return 0u;
  }

  uint8_t found_victim = 0xFFu;
  uint8_t found_height = 0u;
  for (int def = 0; def < (int)batch->config.num_players; def++) {
    if (def == reflector_port) {
      continue;
    }
    const size_t d_idx = msl_idx_player(bi, def);
    if (batch->state.hurtbox_state[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
        batch->state.hitstun[d_idx] != 0u) {
      continue;
    }
    const uint8_t flags_221b = batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                                        (size_t)MSL_STATE_FLAGS_221B_INDEX];
    if ((flags_221b & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) != 0u) {
      continue;
    }
    const uint16_t def_iid = batch->state.instance_id[d_idx];
    const uint8_t cap_n = batch->state.hurtcap_count[d_idx];
    uint8_t victim_hit = 0u;
    for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !victim_hit;
         oi++) {
      if (!hitlist_allows_item_hitbox_fighter(batch, bi, it, (int)oi, def, def_iid)) {
        continue;
      }
      const float off_x =
          (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
      const float s = off_x * laser_collision_offset_scale(lp, laser_state, laser_scale_z,
                                                           MSL_LASER_COLLISION_SPACE_BODY, 0u, 1u);
      const float sx0 = next_x0 + (ux * s);
      const float sy0 = next_y0 + (uy * s);
      const float sx = next_x + (ux * s);
      const float sy = next_y + (uy * s);
      for (uint8_t ci = 0; ci < cap_n; ci++) {
        uint8_t hurt_height = 0u;
        float overlap_amount = 0.0f;
        if (item_swept_sphere_capsule_overlap_amount(batch, bi, def, sx0, sy0, sx, sy, sr, (int)ci,
                                                     &hurt_height, &overlap_amount, 0u, 1.0f)) {
          if (laser_tail_shallow_body_contact_rejected(batch, d_idx, batch->state.item_type[ii],
                                                       laser_state, oi, sr, (int)ci, overlap_amount,
                                                       batch->state.item_attack_id[ii])) {
            continue;
          }
          victim_hit = 1u;
          break;
        }
      }
    }
    if (victim_hit != 0u) {
      if (found_victim != 0xFFu) {
        return 0u;
      }
      found_victim = (uint8_t)def;
      // The reflected hidden OnGiveDamage callback consumes the item damage latch before the
      // ordinary visible BODY resolve has a selected hurtcap-height owner. Replay/source rows enter
      // grounded DamageN from this path even when the only reconstructed travel overlap is a high
      // capsule; keep the default medium-height lane scoped to this hidden callback producer.
      // refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294}
      found_height = 1u;
    }
  }
  if (found_victim == 0xFFu) {
    return 0u;
  }
  batch->state.item_hidden_body_hit_victim_port[ii] = found_victim;
  batch->state.item_hidden_body_hit_hurt_height[ii] = found_height;
  batch->state.item_hidden_callback_flags[ii] = (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_CLEAR;
  return 1u;
}

static inline uint8_t item_try_shine_reflect_contact(MslBatch* batch, size_t ii,
                                                     size_t reflector_idx, int reflector_port,
                                                     const MslLaserParams* lp, uint8_t laser_state,
                                                     float x0, float y0, float x, float y, float ux,
                                                     float uy, float sr, float laser_scale_z) {
  if (!msl_char_id_is_spacie(batch->state.char_id[reflector_idx])) {
    // Char-family machine-entry gate, NOT an action-id collision: a successful reflect
    // contact INSTALLS the spacie SpecialLwHit state below (the fighter may currently be in
    // any Reflector state). Ownership of that state machine is a registry char property.
    return 0u;
  }
  if (batch == NULL || lp == NULL) {
    return 0u;
  }

  // Source item-contact order:
  // ftColl_8007925C checks `fp->reflecting && hurt->x41_b7` and runs
  // ftColl_80077464 before absorb, fighter HitCapsules, shield, or BODY hurtcaps. This helper is the
  // SpecialLw ReflectDesc slice generated by reflector_bubbles_refresh().
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_CreateReflectHit
  const float rr = batch->state.reflector_radius[reflector_idx];
  // Item ownership filter: once ftColl_80077464 / Item_80269F14 transfer a projectile to the
  // reflector's owner, later overlap ticks should not re-reflect the owner's own projectile and
  // re-enter SpecialLwHit.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/item.c::Item_80269F14
  if (!(rr > 0.0f) || batch->state.item_owner[ii] == (int8_t)reflector_port) {
    return 0u;
  }

  const float rx = batch->state.reflector_x[reflector_idx];
  const float ry = batch->state.reflector_y[reflector_idx];
  const MslCharParams* rch = msl_char_params_fast(batch->state.char_id[reflector_idx]);
  if (!isfinite(rx) || !isfinite(ry) || rch == NULL) {
    return 0u;
  }

  uint8_t reflect_hit = 0u;
  const uint8_t off_n =
      (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
  for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !reflect_hit;
       oi++) {
    const float off_x =
        (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
    const float s = off_x * laser_collision_offset_scale(lp, laser_state, laser_scale_z,
                                                         MSL_LASER_COLLISION_SPACE_REFLECT, 0u, 1u);
    const float sx0 = x0 + (ux * s);
    const float sy0 = y0 + (uy * s);
    const float sx = x + (ux * s);
    const float sy = y + (uy * s);
    if (item_swept_sphere_sphere_intersects_3d(sx0, sy0, 0.0f, sx, sy, 0.0f, sr, rx, ry, 0.0f,
                                               rr)) {
      reflect_hit = 1u;
    }
  }

  // Reflector item overlap is item-owned, not exclusively the laser attack hitbox script:
  // ftColl_80077464 receives the Item* and uses item->pos for reflect-direction ownership. Keep the
  // scripted laser offsets above, but also admit the projectile-origin segment for Shine Loop rows.
  // SpecialLwHit stays excluded so an already-reflected owner projectile cannot re-enter the hit
  // callback from a nearby origin overlap.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
  // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
  const uint16_t reflector_action = batch->state.action_id[reflector_idx];
  const uint8_t shine_loop_origin_overlap =
      (msl_motion_state_fx_special_kind(batch->state.char_id[reflector_idx], reflector_action) ==
           (uint8_t)MSL_FX_KIND_SPECIAL_LW_LOOP ||
       msl_motion_state_fx_special_kind(batch->state.char_id[reflector_idx], reflector_action) ==
           (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_LOOP)
          ? 1u
          : 0u;
  if (!reflect_hit && shine_loop_origin_overlap) {
    reflect_hit =
        item_swept_sphere_sphere_intersects_3d(x0, y0, 0.0f, x, y, 0.0f, sr, rx, ry, 0.0f, rr);
  }

  if (!reflect_hit) {
    return 0u;
  }

  // Decomp reflect snapshot ownership:
  // - ftColl_CreateReflectHit stores ReflectDesc.damage_mul / speed_mul.
  // - ftColl_80077464 writes both multipliers to item reflect snapshot (`item->xC6C` et al).
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
  item_apply_shine_reflect_callback(batch, ii, reflector_idx);
  const int bi = (int)(ii / (size_t)MSL_MAX_ITEMS);
  const int it = (int)(ii % (size_t)MSL_MAX_ITEMS);
  const uint8_t hidden_body_before_reflect_callback =
      item_stage_shine_reflected_laser_hidden_body_callback(
          batch, bi, it, ii, reflector_port, lp, laser_state, x, y, batch->state.item_vel_x[ii],
          batch->state.item_vel_y[ii], sr, laser_scale_z);
  if (hidden_body_before_reflect_callback != 0u) {
    batch->state.item_misc2[ii] = 0u;
    batch->state.item_misc3[ii] = 0u;
    msl_item_reflect_commit_owner_snapshot_defer_speed(batch, ii, reflector_port);
    msl_item_reflect_apply_direction_lane(batch, ii);
    msl_item_reflect_set_damage_mul(batch, ii, rch->reflector_damage_mul);
    return 1u;
  }
  msl_item_reflect_apply_immediate_transfer(batch, ii, reflector_idx, reflector_port,
                                            rch->reflector_damage_mul, rch->reflector_speed_mul);
  return 1u;
}

static int laser_spawn_from_fighter(MslBatch* batch, int bi, int owner, const MslLaserParams* lp,
                                    uint8_t spawn_state, uint8_t apply_spawn_motion_step,
                                    uint8_t throw_lw_late_pulse_transn_y, int seed_hitlist_victim,
                                    uint16_t seed_hitlist_victim_iid, uint8_t seed_hitlist_mask,
                                    uint8_t is_blaster_throw) {
  if (batch == NULL || lp == NULL) {
    return -1;
  }
  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return -1;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);

  const size_t o_idx = msl_idx_player(bi, owner);
  const uint8_t char_id = batch->state.char_id[o_idx];
  const uint32_t anim_u32 = batch->state.animation_index[o_idx];
  if (anim_u32 > 0xFFFFu) {
    return -1;
  }
  const uint16_t msid = (uint16_t)anim_u32;
  const uint16_t action_id_u16 = batch->state.action_id[o_idx];
  float anim_frame_f32 = items_cur_anim_frame_f32(batch, o_idx);
  if (spawn_state == 1u && action_id_u16 == (uint16_t)MSL_ACT_THROW_B &&
      item_type_is_falco_laser(lp->shot_itkind) != 0u &&
      batch->state.throw_command_pending_seed_valid[o_idx] != 0u &&
      batch->state.throw_command_pending_pulse_frame[o_idx] != 0u &&
      batch->state.throw_pulse_crossed_prev_frame[o_idx] == 0u) {
    int16_t first_throwb_pulse_af = -1;
    if (move_tables_throw_projectile_first_pulse_frame(char_id, action_id_u16,
                                                       &first_throwb_pulse_af) &&
        (int16_t)batch->state.throw_command_pending_pulse_frame[o_idx] == first_throwb_pulse_af) {
      // Pending first ThrowB pulse pose:
      // - The explicit seed-valid lane proves this is a teacher-forced reconstruction of the
      //   frame-15 throw_flags_b0 command, not a free-running runtime command.
      // - ftFx_Throw_Anim consumes that command and the newly spawned state1 laser is serialized
      //   from the next interpreted hold-joint pose.
      // - Later ThrowB pulses remain on the ordinary live float-pose sampler because they are
      //   already represented by live articles/callback lanes.
      // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
      // data/moves/falco.json moves["ftCo_SM_ThrowB"].events
      anim_frame_f32 = (float)(first_throwb_pulse_af + 1);
    }
  }
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
  const MslCharParams* chp = msl_char_params_fast(char_id);

  // Spawn point: model lb_8000B1CC(bone_joint, offset, out) with source joint id plus extracted
  // SSANIM01 pose matrices.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_FtGetHoldJoint
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  //
  // IMPORTANT (bone index domain):
  // - Decomp uses ftParts_GetBoneIndex(fp, FtPart_RThumbNb) to pick an index into fp->parts[].
  //   refs/melee/src/melee/ft/ftparts.c::ftParts_GetBoneIndex
  // - Our SSANIM01 pose tables are keyed by these fp->parts[] indices.
  //
  // Source of truth:
  // - ISO-extracted character attr key `laser_spawn_joint_part_id`, derived from:
  //   `_iso/PlCo.dat` ftPartsTable[ftkind].part_to_joint[FtPart_RThumbNb].
  //   tools/extraction/extract_character_attrs.py::_extract_ftparts_rthumb_joint_index
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_FtGetHoldJoint
  uint16_t spawn_part_id = lp->spawn_bone_part_id;
  if (chp != NULL && chp->laser_spawn_joint_part_id != 0u) {
    spawn_part_id = chp->laser_spawn_joint_part_id;
  }
  float m[12];
  int matrix_ok = -1;
  if (action_is_blaster_throw(action_id_u16)) {
    // Throw laser hold-joint pose:
    // - ftFx_Throw_Anim samples ftFx_SpecialN_FtGetHoldJoint / ItGetHoldJoint from live JObj
    //   matrices in the Anim callback, then calls it_8029C6CC.
    // - HSD_AObjInterpretAnim owns float-frame local SRT before lb_8000B1CC samples the joint.
    // - Use the float collision-pose sampler for throw-side lasers instead of integer SSANIM01
    //   frames; integer sampling was observed to use a later ThrowHi hold-joint vector on
    //   transient first-pulse spawn/delete rows.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/sysdolphin/baselib/aobj.c::HSD_AObjInterpretAnim
    // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    matrix_ok =
        anim_pose_get_collision_matrix_f32(batch, o_idx, msid, anim_frame_f32, spawn_part_id, m);
  } else {
    matrix_ok = anim_pose_get_matrix(char_id, msid, frame, spawn_part_id, m);
  }
  if (matrix_ok != 0) {
    return -1;
  }
  float lx = 0.0f, ly = 0.0f, lz = 0.0f;
  msl_mtx34_mul_point(m, lp->spawn_off_xyz, &lx, &ly, &lz);

  const float facing_dir = item_throw_pose_facing_dir(batch, o_idx, action_id_u16);

  // Decomp: fighter facing is applied as a root Y rotation by +/-90°, which mixes X/Z.
  // For blaster shot spawn, local Z contributes meaningfully to stage X, so apply the same
  // decomp-shaped rotation here.
  // refs/melee/src/melee/ft/fighter.c (ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir)))
  const float rx = facing_dir * lz;
  const float rz = -facing_dir * lx;
  lx = rx;
  lz = rz;

  // Decomp: runtime joint matrices include ftCommon_GetModelScale(fp) (co_attrs.model_scaling) and
  // fp->x34_scale.y. Slippi seeds fp->x34_scale.y into fighter_scale_y; apply model_scaling here.
  // refs/melee/src/melee/ft/ftlib.c::ftLib_800869D4 (ftCommon_GetModelScale)
  const float scale_y = batch->state.fighter_scale_y[o_idx];
  float model_scaling = 1.0f;
  if (chp != NULL && chp->model_scaling > 0.0f) {
    model_scaling = chp->model_scaling;
  }
  const float model_scale = scale_y * model_scaling;
  lx *= model_scale;
  ly *= model_scale;
  (void)lz;  // 2.5D: keep stage Z at 0.0f

  float pos_x = batch->state.pos_x[o_idx] + lx;
  float pos_y = batch->state.pos_y[o_idx] + ly;
  uint8_t throw_lw_late_pulse_uses_transn_tail =
      (throw_lw_late_pulse_transn_y != 0u && item_type_is_fox_laser(lp->shot_itkind)) ? 1u : 0u;
  if (throw_lw_late_pulse_transn_y != 0u && throw_lw_late_pulse_uses_transn_tail == 0u &&
      action_id_u16 == (uint16_t)MSL_ACT_THROW_LW &&
      item_type_is_falco_laser(lp->shot_itkind) != 0u &&
      frame == (uint16_t)MSL_THROWLW_PULSE_ATTACH_AF) {
    const int attached_victim = throwlw_attached_victim_for_owner(batch, bi, owner);
    if (attached_victim >= 0) {
      const size_t v_idx = msl_idx_player(bi, attached_victim);
      const int16_t frame_start_action_frame = batch->state.prev_action_frame[o_idx];
      throw_lw_late_pulse_uses_transn_tail =
          (batch->state.prev_action_id[o_idx] == (uint16_t)MSL_ACT_THROW_LW &&
           frame_start_action_frame >= 0 &&
           frame_start_action_frame < ((int16_t)MSL_THROWLW_PULSE_ATTACH_AF - 1) &&
           item_throwlw_frame25_post_hitlag_rate_allowed(char_id, batch->state.char_id[v_idx]) !=
               0u)
              ? 1u
              : 0u;
    }
  }
  if (throw_lw_late_pulse_uses_transn_tail != 0u) {
    // ThrowLw later-pulse persistent shot subset:
    // - SSANIM01 baked joint matrices strip TransN/root translation into the v4 tail.
    // - The later ThrowLw pulses (after the first non-persistent pulse) keep a live carried state1
    //   laser article in vanilla, so restore the root Y translation for this persistent subset.
    // - Keep the first pulse on the existing non-persistent owner path.
    // - Fox's extracted low-throw state1 shot path stores this hold joint with the root TransN tail
    //   stripped. Falco's faster Fox-victim frame-25 controls stay on the existing float hold-joint
    //   matrix; the slower post-hitlag frame-25 owner uses the same source-rate gate as the
    //   immediate BODY callback below, where the tail is still absent from the sampled matrix.
    // - `lb_8000B1CC` returns model-scaled joint space, so when the tail is needed it must be
    //   recomposed under the same fighter_scale_y * model_scaling convention as the sampled matrix.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,it_8029C6CC}
    // data/anims/{fox,falco}.bin SSANIM01 v4 TransN tail, read by anim_pose_get_transn()
    // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowLw"]["events"]
    float transn_xyz[3] = {0.0f, 0.0f, 0.0f};
    if (anim_pose_get_transn(char_id, msid, frame, transn_xyz) == 0) {
      pos_y += transn_xyz[1] * model_scale;
    }
  }
  float ang = lp->blaster_angle;
  if (action_is_blaster_throw(action_id_u16)) {
    // Throw-side launch angle is owned by ftFx_Throw_Anim for Throw{B,Hi,Lw}:
    //   atan2f(FtGetHoldJoint.y - ItGetHoldJoint.y,
    //          FtGetHoldJoint.x - ItGetHoldJoint.x)
    // rather than the SpecialN constant blaster angle.
    // FtGetHoldJoint uses the extracted `laser_spawn_joint_part_id` + `lp->spawn_off_xyz`;
    // ItGetHoldJoint uses the same RThumbNb joint with the decomp-local offset below.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
    //   ftFx_SpecialN_FtGetHoldJoint,ftFx_SpecialN_ItGetHoldJoint,ftFx_Throw_Anim}
    // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    const float it_hold_off_xyz[3] = {
        0.0f,
        1.2325000762939453f,
        0.013600001111626625f,
    };
    float ilx = 0.0f, ily = 0.0f, ilz = 0.0f;
    msl_mtx34_mul_point(m, it_hold_off_xyz, &ilx, &ily, &ilz);
    const float irx = facing_dir * ilz;
    const float irz = -facing_dir * ilx;
    ilx = irx;
    ilz = irz;
    (void)ilz;
    ilx *= model_scale;
    ily *= model_scale;
    const float it_hold_x = batch->state.pos_x[o_idx] + ilx;
    float it_hold_y = batch->state.pos_y[o_idx] + ily;
    if (throw_lw_late_pulse_uses_transn_tail != 0u) {
      float transn_xyz[3] = {0.0f, 0.0f, 0.0f};
      if (anim_pose_get_transn(char_id, msid, frame, transn_xyz) == 0) {
        it_hold_y += transn_xyz[1] * model_scale;
      }
    }
    ang = atan2f(pos_y - it_hold_y, pos_x - it_hold_x);
  } else {
    // Launch angle: if facing left, use (pi - base_angle).
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_CreateBlasterShot
    if (facing_dir < 0.0f) {
      ang = MSL_PI_F - ang;
    }
  }
  const float spd = lp->blaster_speed;
  float vx = spd * cosf(ang);
  float vy = spd * sinf(ang);
  if (apply_spawn_motion_step) {
    // Throw-side intra-frame order:
    // - Throw shots are emitted by ftFx_Throw_Anim and then consume item motion callbacks in-frame.
    // - For reseeded one-step rows where throw pulse ownership is seed-driven by MSLFTSC1, apply
    //   one immediate motion tick to align spawned projectile t+1 placement with source item
    //   callback order.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Phys
    pos_x += vx;
    pos_y += vy;
  }
  const float dir = (vx >= 0.0f) ? 1.0f : -1.0f;

  batch->state.item_exists[ii] = 1;
  // Decomp (itfoxlaser.c):
  // - it_8029C6A4 spawns with msid=0
  // - it_8029C6CC spawns with msid=1 (used by ftFx_Throw_Anim for Throw{B,Hi,Lw})
  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6A4,it_8029C6CC}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  batch->state.item_state[ii] = spawn_state;
  batch->state.item_type[ii] = lp->shot_itkind;
  batch->state.item_owner[ii] = (int8_t)owner;
  // Staling identity: items copy the owner's fighter-side (attack_id, attack_instance) at the live
  // spawn callback. In the source, ftFx_SpecialN_CreateBlasterShot runs from the Blaster Loop Anim
  // callback before later frame-end motion-state exits can reset x2068/x206C. This simulator's
  // shot spawn pass runs after action_update(), so repair only the case where the current identity
  // is already FtMoveId_Default but the frame-start identity still carries the callback source.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_CreateBlasterShot
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C504
  // refs/melee/src/melee/it/it_2725.c::{it_8027B0C4,it_8027B070}
  uint16_t item_attack_id = batch->state.attack_id[o_idx];
  uint16_t item_attack_instance = batch->state.attack_instance[o_idx];
  if ((item_attack_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT || item_attack_instance == 0u) &&
      batch->state.frame_start_attack_id[o_idx] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
      batch->state.frame_start_attack_instance[o_idx] != 0u) {
    item_attack_id = batch->state.frame_start_attack_id[o_idx];
    item_attack_instance = batch->state.frame_start_attack_instance[o_idx];
  }
  batch->state.item_attack_id[ii] = item_attack_id;
  batch->state.item_attack_instance[ii] = item_attack_instance;
  // Laser HitCapsule stale-damage owner:
  // - it_802790C0 creates item HitCapsules from the item script and calls it_80272460.
  // - it_80272460 freezes HitCapsule.damage when the item hitbox is created by applying
  //   ft_80089228(owner, item->xD88, item->xD8C, raw_damage).
  // - Fighter-spawned Fox/Falco shots copy the owner's x2068/x206C attack identity at spawn. When
  //   overlapping live shots prove a previous shot's stale update has not happened yet, preserve
  //   the new shot's frozen HitCapsule.damage before that later queue mutation can restale it.
  // refs/melee/src/melee/it/it_2725.c::it_802790C0
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  const uint8_t freeze_stale_damage =
      item_spawn_laser_freezes_stale_damage(batch, bi, owner, lp->shot_itkind, item_attack_id,
                                            item_attack_instance, spawn_state, is_blaster_throw);
  batch->state.item_stale_damage_valid[ii] = freeze_stale_damage;
  batch->state.item_stale_damage_mul[ii] =
      freeze_stale_damage ? staling_multiplier_for_move(batch, o_idx, item_attack_id) : 1.0f;
  // Slippi item.instance_id is item->xDA8_short. On spawn with a fighter parent, xDA8_short copies
  // fighter->x2088 (fighter instance_id) in the generic item spawn path.
  // refs/melee/src/melee/it/it_2725.c::it_8027B070
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_CreateBlasterShot
  batch->state.item_instance_id[ii] = batch->state.instance_id[o_idx];
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_direction[ii] = dir;
  batch->state.item_vel_x[ii] = vx;
  batch->state.item_vel_y[ii] = vy;
  batch->state.item_pos_x[ii] = pos_x;
  batch->state.item_pos_y[ii] = pos_y;
  batch->state.item_timer[ii] = (float)lp->lifetime_frames;
  // Slippi item metadata bytes 1/2 read item+0xDD7 and item+0xDDB. For Fox/Falco lasers these are
  // the low byte of `foxlaser.scale` and `foxlaser.angle` in the xDD4 item-var union.
  // refs/slippi-ssbm-asm/Recording/SendItemInfo.s
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C504
  batch->state.item_misc0[ii] = slippi_metadata_low_byte_from_f32(0.0f);
  batch->state.item_misc1[ii] = slippi_metadata_low_byte_from_f32(ang);
  if (seed_hitlist_victim >= 0) {
    // Throw-laser pending-spawn item HitCapsule seed:
    // - ftFx_Throw_Anim spawns throw-side state1 lasers through it_8029C6CC, then item BODY
    //   collision uses per-HitCapsule victim rings via it_8026FAC4 / lbColl_80008688.
    // - Dolphin v10 dumps for BHH:4335 show the newly spawned first ThrowHi pulse carrying the
    //   same victim in item hitbox 2 before the next Slippi item post-frame. Seed that ring at
    //   spawn so the BODY callback does not immediately consume the replay-carried article.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
    // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
    for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
      if ((seed_hitlist_mask & (uint8_t)(1u << (uint8_t)hb)) == 0u) {
        continue;
      }
      hitlist_register_item_hitbox_fighter(batch, bi, slot, hb, seed_hitlist_victim,
                                           seed_hitlist_victim_iid, (int)MSL_LBCOLL_INSERT_FT_BODY,
                                           16u);
    }
  }
  return slot;
}

static int throwlw_attached_victim_for_owner(const MslBatch* batch, int bi, int owner) {
  if (batch == NULL) {
    return -1;
  }
  const int num_players = (int)batch->config.num_players;
  int candidate = -1;
  for (int vp = 0; vp < num_players; vp++) {
    if (vp == owner) {
      continue;
    }
    const size_t v_idx = msl_idx_player(bi, vp);
    if (batch->state.grab_owner_port[v_idx] != (uint8_t)owner ||
        batch->state.action_id[v_idx] != (uint16_t)MSL_ACT_THROWN_LW) {
      continue;
    }
    if (candidate >= 0) {
      return -1;
    }
    candidate = vp;
  }
  return candidate;
}

static void laser_spawn_apply_throwlw_attached_body_callback(MslBatch* batch, int bi, int owner,
                                                             int slot, const MslLaserParams* lp,
                                                             uint8_t allow_post_hitlag_boundary,
                                                             uint8_t preserve_late_state1_article) {
  if (batch == NULL || lp == NULL || slot < 0) {
    return;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  if (batch->state.action_id[o_idx] != (uint16_t)MSL_ACT_THROW_LW) {
    return;
  }
  const int victim = throwlw_attached_victim_for_owner(batch, bi, owner);
  if (victim < 0) {
    return;
  }
  const size_t v_idx = msl_idx_player(bi, victim);
  if (batch->state.hitlag_pre_timer[v_idx] != 0u &&
      !(allow_post_hitlag_boundary != 0u && batch->state.hitlag[v_idx] == 0u)) {
    return;
  }
  const size_t ii = msl_idx_item(bi, slot);
  if (batch->state.item_exists[ii] == 0u || batch->state.item_state[ii] != 1u ||
      batch->state.item_owner[ii] != (int8_t)owner) {
    return;
  }
  // ThrowLw spawn-time attached BODY callback:
  // - ftAction_80073354 / ftAction_80071974 execute one set_throw_spawn_projectile command into
  //   throw_flags_b0, and ftFx_Throw_Anim spawns the state1 laser through it_8029C6CC before item
  //   BODY callbacks.
  // - v10 PRH:533/5637 dumps show no live state1 shot in seed, a pending frame-28 command, then a
  //   freshly spawned Falco shot whose BODY callback applies attached-victim hitlag/bookkeeping.
  // - PRH:5631 shows the same source path after frame-start victim hitlag decrements to zero:
  //   Fighter_8006A1BC ends hitlag, ThrowLw Anim resumes, crosses the frame-25 pulse, then item
  //   BODY callback applies attached-victim hitlag in the same frame.
  // - Other first/terminal Falco phases need separate callback state and stay excluded by caller
  //   gates.
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
  // refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
  const MslItemHitResult res = combat_apply_item_hit(
      batch, bi, owner, victim, batch->state.item_attack_id[ii],
      batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
      batch->state.item_type[ii], batch->state.item_state[ii], lp->state1_damage, lp->state1_angle,
      lp->state1_kbg, lp->state1_wsk, lp->state1_bkb, 1u, lp->state1_element, -1.0f,
      batch->state.item_pos_x[ii], batch->state.item_pos_y[ii], lp->state1_size,
      batch->state.item_vel_x[ii], 0u);
  if (res == MSL_ITEM_HIT_APPLIED_CONSUME_ITEM && preserve_late_state1_article == 0u) {
    item_slot_clear(batch, ii);
    return;
  }
  if (res != MSL_ITEM_HIT_NONE) {
    // ThrowLw late-pulse attached laser persistence:
    // - First ThrowLw state1 pulses still take the normal zero-KB laser OnGiveDamage/destroy path.
    // - Later attached pulses are the persistent state1 subset already identified by the TransN-tail
    //   recomposition lane in laser_spawn_from_fighter(); source collision records the BODY victim
    //   per HitCapsule, so keep the article alive and seed hb0/hb1 rehit suppression.
    // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,itFoxlaser_UnkMotion1_Coll}
    // refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_8026FAC4,it_80272460}
    const uint16_t victim_iid = batch->state.instance_id[v_idx];
    hitlist_register_item_hitbox_fighter(batch, bi, slot, 0, victim, victim_iid,
                                         (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
    hitlist_register_item_hitbox_fighter(batch, bi, slot, 1, victim, victim_iid,
                                         (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
  }
}

static inline uint8_t throwb_startup_laser_late_owner_damagefly_phys_delta(
    const MslBatch* batch, int bi, int owner, int victim, float* out_dx, float* out_dy) {
  if (out_dx != NULL) {
    *out_dx = 0.0f;
  }
  if (out_dy != NULL) {
    *out_dy = 0.0f;
  }
  if (batch == NULL || out_dx == NULL || out_dy == NULL || owner <= victim) {
    return 0u;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  const size_t v_idx = msl_idx_player(bi, victim);
  if (batch->state.action_id[o_idx] != (uint16_t)MSL_ACT_THROW_B ||
      !msl_damage_owner_is_damagefly_action(batch->state.action_id[v_idx]) ||
      batch->state.on_ground[v_idx] != 0u || batch->state.hitlag_pre_timer[v_idx] != 0u ||
      batch->state.hitlag[v_idx] != 0u || batch->state.hitstun[v_idx] == 0u ||
      !msl_damage_source_victim_port_matches_attacker(batch, v_idx, o_idx, owner)) {
    return 0u;
  }

  const MslCommonParams* c = msl_common_params();
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[v_idx]);
  if (c == NULL || ch == NULL) {
    return 0u;
  }

  float kb_x = batch->state.speed_x_attack[v_idx];
  float kb_y = batch->state.speed_y_attack[v_idx];
  if (kb_x != 0.0f || kb_y != 0.0f) {
    const float kb_mag = sqrtf(kb_x * kb_x + kb_y * kb_y);
    const float decay = c->knockback_frame_decay;
    if (kb_mag < decay) {
      kb_x = 0.0f;
      kb_y = 0.0f;
    } else {
      const float kb_angle = atan2f(kb_y, kb_x);
      kb_x -= decay * cosf(kb_angle);
      kb_y -= decay * sinf(kb_angle);
    }
  }

  float self_y = batch->state.speed_y_self[v_idx] - ch->grav;
  if (self_y < -ch->terminal_vel) {
    self_y = -ch->terminal_vel;
  }

  // Later-port ThrowB startup pulse order:
  // - Fighter callback order lets the earlier victim's DamageFly Phys run before the later
  //   thrower's ftFx_Throw_Anim consumes the frame-15 blaster command.
  // - The state1 laser BODY callback then installs the new Damage* hitlag/KB after that old
  //   DamageFly displacement. Since this simulator resolves the spawned item before the later
  //   physics pass, add only the already-owned old DamageFly displacement and latch hitlag so the
  //   newly installed laser hit does not integrate again in the same frame.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Phys
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,itFoxlaser_UnkMotion1_Coll}
  *out_dx = batch->state.speed_air_x_self[v_idx] + kb_x;
  *out_dy = self_y + kb_y;
  return 1u;
}

static void laser_spawn_apply_falco_throwb_startup_body_callback(MslBatch* batch, int bi, int owner,
                                                                 int slot,
                                                                 const MslLaserParams* lp) {
  if (batch == NULL || lp == NULL || slot < 0) {
    return;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  if (batch->state.action_id[o_idx] != (uint16_t)MSL_ACT_THROW_B ||
      batch->state.char_id[o_idx] != (uint8_t)MSL_CHAR_ID_FALCO) {
    return;
  }
  const int victim = throw_laser_unique_same_source_victim(batch, bi, owner);
  if (victim < 0) {
    return;
  }
  const size_t v_idx = msl_idx_player(bi, victim);
  if (batch->state.hitlag_pre_timer[v_idx] != 0u || batch->state.action_frame[v_idx] > 5 ||
      batch->state.last_attack_landed[v_idx] < 17u) {
    return;
  }
  const size_t ii = msl_idx_item(bi, slot);
  if (batch->state.item_exists[ii] == 0u || batch->state.item_state[ii] != 1u ||
      batch->state.item_owner[ii] != (int8_t)owner ||
      item_type_is_falco_laser(batch->state.item_type[ii]) == 0u) {
    return;
  }
  float source_damagefly_dx = 0.0f;
  float source_damagefly_dy = 0.0f;
  const uint8_t late_owner_damagefly_phys = throwb_startup_laser_late_owner_damagefly_phys_delta(
      batch, bi, owner, victim, &source_damagefly_dx, &source_damagefly_dy);

  // Falco ThrowB startup same-frame BODY callback:
  // - ftAction emits the frame-15 throw_flags_b0 command and ftFx_Throw_Anim spawns a state1
  //   throw laser through it_8029C6CC.
  // - The item collision phase then runs it_8026FAC4 / it_80272460 before Slippi item
  //   serialization. Event probes for the startup callback phase show spawn_request followed by
  //   hb0 body_hitlist, give_damage, and destroy in the same frame while the victim remains at the
  //   early frame-4 prior-laser hitbox identity. Later startup carry rows and non-laser-body landed
  //   identities stay on the hb2/3 item-hitlist carry lane.
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
  // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
  const MslItemHitResult res = combat_apply_item_hit(
      batch, bi, owner, victim, batch->state.item_attack_id[ii],
      batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
      batch->state.item_type[ii], batch->state.item_state[ii], lp->state1_damage, lp->state1_angle,
      lp->state1_kbg, lp->state1_wsk, lp->state1_bkb, 1u, lp->state1_element, -1.0f,
      batch->state.item_pos_x[ii], batch->state.item_pos_y[ii], lp->state1_size,
      batch->state.item_vel_x[ii], 0u);
  if (res != MSL_ITEM_HIT_NONE && late_owner_damagefly_phys != 0u) {
    batch->state.pos_x[v_idx] += source_damagefly_dx;
    batch->state.pos_y[v_idx] += source_damagefly_dy;
    batch->state.hitlag_started_frame[v_idx] = 1u;
  }
  if (res == MSL_ITEM_HIT_APPLIED_CONSUME_ITEM) {
    item_slot_clear(batch, ii);
    return;
  }
  if (res != MSL_ITEM_HIT_NONE) {
    const uint16_t victim_iid = batch->state.instance_id[v_idx];
    hitlist_register_item_hitbox_fighter(batch, bi, slot, 0, victim, victim_iid,
                                         (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
  }
}

static MslItemHitResult throw_laser_apply_hidden_state1_body_topoff(MslBatch* batch, int bi,
                                                                    int owner, int victim,
                                                                    const MslLaserParams* lp) {
  if (batch == NULL || lp == NULL || owner < 0 || victim < 0) {
    return MSL_ITEM_HIT_NONE;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  const size_t v_idx = msl_idx_player(bi, victim);
  if (batch->state.hitlag[v_idx] == 0u || batch->state.hitstun[v_idx] == 0u ||
      !msl_damage_source_victim_port_matches_attacker(batch, v_idx, o_idx, owner)) {
    return MSL_ITEM_HIT_NONE;
  }

  // Hidden throw-pulse BODY top-off:
  // - ftAction emits one throw_flags_b0 pulse and ftFx_Throw_Anim consumes it by spawning a state1
  //   throw laser through it_8029C6CC.
  // - Some terminal/callback phases consume that state1 article before Slippi serializes it, but
  //   the BODY callback still contributes to the already-live Fighter_ProcessHit percent/hitlag
  //   window. Use the same state1 item BODY producer as visible throw lasers, without leaving an
  //   extra runtime article behind.
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  return combat_apply_item_hit(
      batch, bi, owner, victim, batch->state.attack_id[o_idx], batch->state.attack_instance[o_idx],
      batch->state.instance_id[o_idx], lp->shot_itkind, 1u, lp->state1_damage, lp->state1_angle,
      lp->state1_kbg, lp->state1_wsk, lp->state1_bkb, 1u, lp->state1_element, -1.0f,
      // Spawn-time attached contact carries no live item position; Counter fails closed.
      batch->state.pos_x[o_idx], batch->state.pos_y[o_idx], -1.0f,
      batch->state.facing[o_idx] ? 1.0f : -1.0f, 0u);
}

static void laser_spawn_apply_falco_throwb_final_prior_body_callback(
    MslBatch* batch, int bi, int owner, int spawned_slot, const MslLaserParams* lp,
    uint8_t pre_spawn_throw_shot_count) {
  if (batch == NULL || lp == NULL || spawned_slot < 0 || pre_spawn_throw_shot_count != 1u) {
    return;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  if (batch->state.action_id[o_idx] != (uint16_t)MSL_ACT_THROW_B ||
      batch->state.char_id[o_idx] != (uint8_t)MSL_CHAR_ID_FALCO ||
      item_type_is_falco_laser(lp->shot_itkind) == 0u) {
    return;
  }
  int16_t last_throwb_pulse_af = -1;
  if (!move_tables_throw_projectile_last_pulse_frame(
          batch->state.char_id[o_idx], batch->state.action_id[o_idx], &last_throwb_pulse_af) ||
      batch->state.throw_pulse_crossed_curr_frame[o_idx] != (uint8_t)last_throwb_pulse_af) {
    return;
  }

  const int victim = throw_laser_unique_same_source_victim(batch, bi, owner);
  if (victim < 0) {
    return;
  }
  const size_t v_idx = msl_idx_player(bi, victim);
  if (batch->state.hitlag[v_idx] == 0u || batch->state.hitstun[v_idx] == 0u ||
      batch->state.action_frame[v_idx] > 1 ||
      batch->state.last_attack_landed[v_idx] != (uint8_t)MSL_THROWB_PULSE_START_AF) {
    return;
  }

  const size_t spawned_ii = msl_idx_item(bi, spawned_slot);
  if (batch->state.item_exists[spawned_ii] == 0u ||
      batch->state.item_owner[spawned_ii] != (int8_t)owner ||
      batch->state.item_type[spawned_ii] != lp->shot_itkind ||
      batch->state.item_state[spawned_ii] != 1u) {
    return;
  }

  int prior_slot = -1;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    if (it == spawned_slot) {
      continue;
    }
    const size_t ii = msl_idx_item(bi, it);
    const float prior_step_x = batch->state.item_pos_x[ii] + batch->state.item_vel_x[ii];
    if (batch->state.item_exists[ii] == 0u || batch->state.item_owner[ii] != (int8_t)owner ||
        batch->state.item_type[ii] != lp->shot_itkind || batch->state.item_state[ii] != 1u ||
        batch->state.item_timer[ii] > 98.0f ||
        batch->state.item_spawn_id[ii] >= batch->state.item_spawn_id[spawned_ii] ||
        ((prior_step_x - batch->state.pos_x[v_idx]) * batch->state.item_vel_x[ii]) <= 0.0f) {
      continue;
    }
    if (prior_slot >= 0) {
      return;
    }
    prior_slot = it;
  }
  if (prior_slot < 0) {
    return;
  }

  const size_t prior_ii = msl_idx_item(bi, prior_slot);
  // Falco ThrowB final-pulse prior article BODY callback:
  // - The final frame-21 `throw_flags_b0` command and a single already-live state1 article are the
  //   ftFx_Throw_Anim / it_8029C6CC source phase for the handoff where the prior pulse's
  //   it_8029C4D4 BODY callback consumes the older article, while the freshly spawned article
  //   remains serialized after item sorting.
  // - Keep this on the unique same-source victim at the first-pulse Damage* entry frame, after the
  //   prior article endpoint has crossed that victim, and require exactly one pre-spawn state1 shot
  //   so ordinary later ThrowB carry rows and multi-article rows stay on the regular item pass.
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
  // refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
  // data/moves/falco.json moves["ftCo_SM_ThrowB"].events
  const MslItemHitResult res = combat_apply_item_hit(
      batch, bi, owner, victim, batch->state.item_attack_id[prior_ii],
      batch->state.item_attack_instance[prior_ii], batch->state.item_instance_id[prior_ii],
      batch->state.item_type[prior_ii], batch->state.item_state[prior_ii], lp->state1_damage,
      lp->state1_angle, lp->state1_kbg, lp->state1_wsk, lp->state1_bkb, 1u, lp->state1_element,
      -1.0f, batch->state.item_pos_x[prior_ii], batch->state.item_pos_y[prior_ii], lp->state1_size,
      batch->state.item_vel_x[prior_ii], 0u);
  if (res == MSL_ITEM_HIT_APPLIED_CONSUME_ITEM) {
    item_slot_clear(batch, prior_ii);
  }
}

static void illusion_items_update_and_collide(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      continue;
    }
    const uint16_t type = batch->state.item_type[ii];
    if (!item_type_is_spacie_illusion(type)) {
      continue;
    }
    if (batch->state.item_hitlag[ii] > 0u) {
      batch->state.item_hitlag[ii]--;
      continue;
    }

    const int owner = (int)batch->state.item_owner[ii];
    if (owner < 0 || owner >= num_players) {
      continue;
    }
    const size_t o_idx = msl_idx_player(bi, owner);
    const MslCharParams* chp = msl_char_params_fast(batch->state.char_id[o_idx]);
    const uint8_t owner_motion_active = illusion_owner_motion_is_active(batch, o_idx);
    if (!illusion_item_anim_step(batch, ii, chp, owner_motion_active)) {
      continue;
    }
    if (batch->state.item_state[ii] >= 2u) {
      continue;
    }
    MslIllusionItemHitParams hp = {0};
    if (!illusion_item_hit_params_from_state(chp, batch->state.item_state[ii], &hp)) {
      continue;
    }

    // Decomp owner lane for Phys callback:
    // - state0/state1 copy item->pos from owner ghostEffectPos[1].
    // - ghostEffectPos[1] is advanced by ftFox_SpecialS_SetPhys from the prior frame's owner
    //   world position during SpecialS/SpecialAirS/SpecialSEnd/SpecialAirSEnd Phys.
    // refs/melee/src/melee/it/items/itfoxillusion.c::{
    //   itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys}
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFx_SpecialS_CopyGhostPosIndexed,ftFox_SpecialS_SetPhys,
    //   ftFx_SpecialS_Phys,ftFx_SpecialAirS_Phys,ftFx_SpecialSEnd_Phys,ftFx_SpecialAirSEnd_Phys
    // }
    //
    // Seeded source-state owner:
    // - consume the seeded post-frame `ghostEffectPos[1]` lane for the current step,
    // - keep the paired `ghostEffectPos[0..2]` lanes in state so rollout can advance the ring as
    //   `ghost2 = ghost1; ghost1 = ghost0; ghost0 = cur_pos` instead of clobbering previous
    //   hitcapsule endpoints with current position.
    // tools/slippi/make_dataset_from_slp.py::derive_illusion_ghost_pos012
    //
    // Retained source-policy boundary for accessory Side-B lanes:
    // - ghostEffectPos[3]
    // - blendFrames[0..3]
    // - ghostGObj
    // - fp->x2222_b2 side effects
    // These owners drive ghost visual/accessory state around ftFox_SpecialS_SetVars and
    // ftFox_SpecialSEnd_SetVars. Supported gameplay collision consumes ghostEffectPos[0..2] for
    // article position/hit flow and leaves the accessory-only lanes out of item-core runtime state.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFox_SpecialS_SetPhys,ftFox_SpecialS_SetVars,ftFox_SpecialSEnd_SetVars}
    // refs/melee/src/melee/it/items/itfoxillusion.c::{
    //   itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys,itFoxillusion_UnkMotion2_Phys}
    float base_x0 = batch->state.item_pos_x[ii];
    float base_y0 = batch->state.item_pos_y[ii];
    float base_x1 = base_x0;
    float base_y1 = base_y0;
    if (action_is_illusion_setphys(batch->state.char_id[o_idx], batch->state.action_id[o_idx])) {
      // Item collision carries the previous-to-current HitCapsule segment. The current endpoint
      // is the decomp-owned `ghostEffectPos[1]` item position. Main-state rows reload the current
      // ghost position as the article sample; Fox end-state rows consume ghostEffectPos[2] as the
      // previous endpoint after the source SetPhys ring has advanced into SpecialSEnd.
      // refs/melee/src/melee/it/itcoll.c::it_8027137C
      // refs/melee/src/melee/it/items/itfoxillusion.c::{
      //   itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys}
      base_x1 = batch->state.illusion_ghost_pos1_x[o_idx];
      base_y1 = batch->state.illusion_ghost_pos1_y[o_idx];
      base_x0 = base_x1;
      base_y0 = base_y1;
      if (item_type_is_fox_illusion(type) &&
          action_is_illusion_end(batch->state.char_id[o_idx], batch->state.action_id[o_idx])) {
        base_x0 = batch->state.illusion_ghost_pos2_x[o_idx];
        base_y0 = batch->state.illusion_ghost_pos2_y[o_idx];
      }
      batch->state.item_pos_x[ii] = base_x1;
      batch->state.item_pos_y[ii] = base_y1;
    }
    const float x0 = base_x0;
    const float y0 = base_y0 + hp.hitbox_y_offset;
    const float x1 = base_x1;
    const float y1 = base_y1 + hp.hitbox_y_offset;

    uint8_t consumed_item = 0u;
    for (int def = 0; def < num_players; def++) {
      if (def == owner) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, def);
      if (msl_action_owns_x2219_collision_skip(batch->state.action_id[d_idx])) {
        // Dead*/Rebirth source states set fp->x2219_b1. Item-vs-fighter collision rejects x2219_b1
        // targets, so these fighters must not be illusion-hit while the collision-skip bit is live.
        // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D3680,ftCo_800D3950,ftCo_800D3BC8,ftCo_800D3E40,ftCo_800D4580,ftCo_800D481C}
        // refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_800D4FF4,ftCo_800D5600}
        // refs/melee/src/melee/it/itcoll.c::it_80272460
        continue;
      }
      // Hitlag gating: item collision acceptance is frozen while either participant is in hitlag.
      // Decomp ordering applies hitlag before collision callbacks in the per-frame fighter/item loop.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/it/item.c::Item_80268F7C
      if (batch->state.hitlag[o_idx] != 0u || batch->state.hitlag[d_idx] != 0u) {
        continue;
      }
      const uint16_t def_iid = batch->state.instance_id[d_idx];
      if (!hitlist_allows_item_hitbox_fighter(batch, bi, it, 0, def, def_iid)) {
        continue;
      }

      // Shield precedence mirrors fighter/item collision ownership: resolve shield overlap before
      // BODY intake.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
      float shr = batch->state.shield_radius[d_idx];
      if (shr > 0.0f && batch->state.guard_reflect_entered_this_frame[d_idx] != 0u) {
        shr = item_guard_shield_radius_from_state(batch, msl_common_params(), d_idx);
      }
      if (shr > 0.0f) {
        float shx = batch->state.shield_x[d_idx];
        float shy = batch->state.shield_y[d_idx];
        float shz = batch->state.shield_z[d_idx];
        if (!isfinite(shx) || !isfinite(shy) || !isfinite(shz)) {
          shx = batch->state.pos_x[d_idx];
          shy = batch->state.pos_y[d_idx];
          shz = batch->state.pos_z[d_idx];
        }
        // Decomp shield overlap helper consumes ShieldDesc radius with the shield bubble.
        // In GALE01 ShieldDesc radius is 1.0f and scales by fighter scale.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c
        // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
        float shield_desc_world_r = 1.0f;
        if (batch->state.fighter_scale_y[d_idx] > 0.0f) {
          shield_desc_world_r *= batch->state.fighter_scale_y[d_idx];
        }
        uint8_t shield_overlap = item_swept_sphere_sphere_intersects_3d(
            x0, y0, 0.0f, x1, y1, 0.0f, hp.radius, shx, shy, shz, shr + shield_desc_world_r);
        if (!shield_overlap && item_no_submotion_guard_shielddesc_root_x_owner(batch, d_idx)) {
          // Illusion/Phantasm item HitShield source owner:
          // - ftColl_8007925C drives item-vs-fighter shield precedence through the live
          //   ShieldDesc (`fp+0x221B_b0`) and lbColl_80007BCC, not through a visible action-id
          //   transition.
          // - No-submotion Guard snapshots can serialize a display shield bubble whose horizontal
          //   guard-pose sample is stale relative to the item callback; the source ShieldDesc owner
          //   still belongs to the current fighter root/bone update for this item collision pass.
          // - Keep this local to the item ShieldDesc owner and preserve the source vertical
          //   ShieldDesc lane from the refreshed shield bubble; BODY remains below on its normal
          //   hurtcap path if this root-x ShieldDesc test misses.
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092450,ftCo_80091D58}
          shield_overlap = item_swept_sphere_sphere_intersects_3d(
              x0, y0, 0.0f, x1, y1, 0.0f, hp.radius, batch->state.pos_x[d_idx], shy, shz,
              shr + shield_desc_world_r);
        }
        if (shield_overlap) {
          if (item_guardreflect_active_timer_shield_contact_needs_drain(batch, d_idx)) {
            item_guardreflect_apply_contact_drain(batch, d_idx);
          }
          const float dmg = msl_item_reflect_damage_lane(batch, ii, hp.damage);
          int8_t shield_damage = hp.shield_damage;
          // Data extraction uses -128 as the unset sentinel for shield-damage delta in this lane.
          if (shield_damage == (int8_t)MSL_ILLUSION_SHIELD_DAMAGE_UNSET) {
            shield_damage = 0;
          }
          combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                       batch->state.item_attack_instance[ii], dmg, shield_damage,
                                       hp.element, batch->state.item_pos_x[ii]);
          // Generic item hitlag owner:
          // - item collision processing raises item->xCBC_hitlagFrames after a successful item
          //   shield/body contact, and Item_802697D4 skips item Phys/movement while the item
          //   remains in hitlag (`xDC8_word.flags.x9 != 0`).
          // - Illusion/Phantasm articles persist through shield hits, so freeze the article at
          //   the shield contact point for the defender hitlag window instead of continuing to
          //   consume ghostEffectPos[1].
          // refs/melee/src/melee/it/item.c::{Item_802697D4,checkHitLag}
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
          if (batch->state.hitlag[d_idx] > batch->state.item_hitlag[ii]) {
            batch->state.item_hitlag[ii] = batch->state.hitlag[d_idx];
          }
          const uint16_t def_iid_post = batch->state.instance_id[d_idx];
          hitlist_register_item_hitbox_fighter(batch, bi, it, 0, def, def_iid_post,
                                               (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
          break;
        }
      }

      if (batch->state.hurtbox_state[d_idx] != 0u) {
        continue;
      }

      uint8_t hit_hurt_height = 0u;
      uint8_t hit = 0u;
      const uint8_t cap_n = batch->state.hurtcap_count[d_idx];
      for (uint8_t ci = 0; ci < cap_n; ci++) {
        if (item_swept_sphere_capsule_intersects(batch, bi, def, x0, y0, x1, y1, hp.radius, (int)ci,
                                                 &hit_hurt_height)) {
          hit = 1u;
          break;
        }
      }
      if (!hit) {
        uint16_t source_msid = 0u;
        uint16_t source_pose_frame = 0u;
        if (illusion_item_body_runbrake_squat_pose_owner(batch, d_idx, &source_msid,
                                                         &source_pose_frame)) {
          const MslHurtCap* caps = NULL;
          uint16_t source_cap_n = 0u;
          if (hurtcaps_get(batch->state.char_id[d_idx], &caps, &source_cap_n) == 0 &&
              caps != NULL) {
            const uint16_t limit = source_cap_n < (uint16_t)MSL_MAX_HURTCAPS
                                       ? source_cap_n
                                       : (uint16_t)MSL_MAX_HURTCAPS;
            for (uint16_t ci = 0u; ci < limit; ci++) {
              if (item_swept_sphere_capsule_intersects_fighter_pose(
                      batch, bi, def, x0, y0, x1, y1, hp.radius, (int)ci, source_msid,
                      source_pose_frame, &hit_hurt_height)) {
                hit = 1u;
                break;
              }
            }
          }
        }
      }
      if (!hit) {
        continue;
      }

      const uint8_t defender_guard_reflect_no_submotion_body_handoff =
          item_guard_reflect_body_hit_consumes_shield_state(batch, d_idx);
      const uint8_t defender_guardon_reflect_body_undo_recharge =
          item_guardon_reflect_body_hit_undoes_action_recharge(batch, d_idx);
      const float dmg = msl_item_reflect_damage_lane(batch, ii, hp.damage);
      const uint8_t steady_illusion_item_facing_owner =
          (chp->illusion_item_lifetime_state01_frames >= 2u &&
           batch->state.item_timer[ii] <=
               (float)(uint8_t)(chp->illusion_item_lifetime_state01_frames - 2u))
              ? 1u
              : 0u;
      const MslItemHitResult res = combat_apply_item_hit(
          batch, bi, owner, def, batch->state.item_attack_id[ii],
          batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
          batch->state.item_type[ii], batch->state.item_state[ii], dmg, hp.angle, hp.kbg, hp.wsk,
          hp.bkb, hit_hurt_height, hp.element, -1.0f, batch->state.item_pos_x[ii],
          batch->state.item_pos_y[ii], hp.radius, batch->state.item_vel_x[ii],
          steady_illusion_item_facing_owner);
      if (res == MSL_ITEM_HIT_NONE) {
        continue;
      }
      item_laser_commit_reflected_body_stale_owner(batch, bi, ii, owner);
      if (res == MSL_ITEM_HIT_APPLIED_CONSUME_ITEM) {
        if (defender_guard_reflect_no_submotion_body_handoff) {
          item_guard_reflect_restore_anim_drain(batch, d_idx);
        }
        if (defender_guardon_reflect_body_undo_recharge) {
          item_guardon_reflect_undo_action_recharge(batch, d_idx);
        }
        item_slot_clear(batch, ii);
        consumed_item = 1u;
        break;
      }
      if (res == MSL_ITEM_HIT_APPLIED_DONT_CONSUME) {
        if (defender_guard_reflect_no_submotion_body_handoff) {
          item_guard_reflect_restore_anim_drain(batch, d_idx);
        }
        if (defender_guardon_reflect_body_undo_recharge) {
          item_guardon_reflect_undo_action_recharge(batch, d_idx);
        }
        // Illusion/Phantasm BODY hits persist but do not enter generic item hitlag:
        // OnGiveDamageThink first copies xC34_damageDealt into xCA8, then the item-specific
        // dmg_dealt callback clears xCA8. The later checkHitLag(xCA8) branch is therefore skipped,
        // so xD44_lifeTimer keeps advancing through the victim's hitlag window.
        // refs/melee/src/melee/it/item.c::{OnGiveDamageThink,checkHitLag}
        // refs/melee/src/melee/it/items/itfoxillusion.c::itFoxIllusion_Logic14_DmgDealt
      }
      const uint16_t def_iid_post = batch->state.instance_id[d_idx];
      hitlist_register_item_hitbox_fighter(batch, bi, it, 0, def, def_iid_post,
                                           (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
      break;
    }

    if (consumed_item) {
      continue;
    }
  }
}

static void lasers_update_and_collide(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  const uint32_t stage_id = batch->state.stage_id[bi];
  MslStageBounds blast_bounds = {0};
  const uint8_t has_blast_bounds = stage_collision_get_blast_bounds_world(stage_id, &blast_bounds);

  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      continue;
    }
    const uint16_t type = batch->state.item_type[ii];
    const MslLaserParams* lp = laser_params_for_item_type(type);
    if (lp == NULL) {
      continue;
    }
    // Scope guard: all collision logic in this function is laser-only.
    // `laser_params_for_item_type` returns NULL for non-MSLLASR1-backed item kinds, so the swept
    // overlap path below cannot affect bombs/turnips/etc.
    // Source: data/items/lasers.bin (MSLLASR1), loaded by laser_params_for_item_type().

    // Item msid/state is recorded by Slippi as u8 from Item+0x24 (enum_t msid).
    // refs/slippi-ssbm-asm/Recording/SendItemInfo.s
    // refs/melee/src/melee/it/types.h::Item (msid at +0x24)
    //
    // Blaster shots (itfoxlaser.c) can be spawned with msid 0 or 1:
    // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6A4 and ::it_8029C6CC
    const uint8_t laser_state = (batch->state.item_state[ii] != 0) ? 1u : 0u;
    int owner = (int)batch->state.item_owner[ii];

    const uint8_t hidden_victim = batch->state.item_hidden_body_hit_victim_port[ii];
    const uint8_t hidden_flags = batch->state.item_hidden_callback_flags[ii];
    uint8_t hidden_body_applied = 0u;
    if (hidden_flags != 0u && owner >= 0 && owner < num_players &&
        hidden_victim < (uint8_t)num_players) {
      // Seeded hidden OnGiveDamage callback phase:
      // - ftColl BODY intake can populate item->xC34_damageDealt and HitCapsule victims_1 before
      //   Slippi item post-frame exposes a serializable item state.
      // - Item_8026A294 consumes that latch through OnGiveDamageThink on the next item callback
      //   phase. This seed lane replays that hidden callback once at the reseed boundary.
      // - Item_8026A294 checks OnGiveDamageThink before Item_80269F14's reflect callback, so the
      //   hidden BODY callback consumes the pre-reflect velocity/angle lanes when both are pending.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077C60
      // refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_80269F14,Item_8026A294}
      float dmg = (laser_state == 0u) ? lp->damage : lp->state1_damage;
      dmg = msl_item_reflect_damage_lane(batch, ii, dmg);
      const uint16_t angle = (laser_state == 0u) ? lp->angle : lp->state1_angle;
      const uint16_t kbg = (laser_state == 0u) ? lp->kbg : lp->state1_kbg;
      const uint16_t wsk = (laser_state == 0u) ? lp->wsk : lp->state1_wsk;
      const uint16_t bkb = (laser_state == 0u) ? lp->bkb : lp->state1_bkb;
      const uint8_t element = (laser_state == 0u) ? lp->element : lp->state1_element;
      uint8_t hurt_height = batch->state.item_hidden_body_hit_hurt_height[ii];
      if (hurt_height > 2u) {
        hurt_height = 1u;
      }
      const MslItemHitResult res = combat_apply_item_hit(
          batch, bi, owner, (int)hidden_victim, batch->state.item_attack_id[ii],
          batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
          batch->state.item_type[ii], laser_state, dmg, angle, kbg, wsk, bkb, hurt_height, element,
          -1.0f, batch->state.item_pos_x[ii], batch->state.item_pos_y[ii],
          (laser_state == 0u) ? lp->size : lp->state1_size, batch->state.item_vel_x[ii], 0u);
      const uint16_t victim_iid_post =
          batch->state.instance_id[msl_idx_player(bi, (int)hidden_victim)];
      if (res != MSL_ITEM_HIT_NONE) {
        hitlist_register_item_fighter(batch, bi, it, (int)hidden_victim, victim_iid_post,
                                      (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
      }
      hidden_body_applied = 1u;
    }
    batch->state.item_hidden_body_hit_victim_port[ii] = 0xFFu;
    batch->state.item_hidden_body_hit_hurt_height[ii] = 0u;
    if (hidden_body_applied != 0u &&
        (hidden_flags & (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_CLEAR) != 0u) {
      item_slot_clear(batch, ii);
      continue;
    }

    msl_item_reflect_apply_pending_laser_callback(batch, ii);
    owner = (int)batch->state.item_owner[ii];

    // Motion: item->pos += item->vel (generic add in Item_802697D4), and lifetime counts down.
    // Decomp refs:
    // - itfoxlaser.c::itFoxlaser_UnkMotion1_Anim (computes item->x40_vel from speed/angle)
    // - itfoxlaser.c::it_8029C504 (it_80275158 sets lifetime)
    const float x0 = batch->state.item_pos_x[ii];
    const float y0 = batch->state.item_pos_y[ii];
    const float x = x0 + batch->state.item_vel_x[ii];
    const float y = y0 + batch->state.item_vel_y[ii];
    batch->state.item_pos_x[ii] = x;
    batch->state.item_pos_y[ii] = y;

    // Blast-zone cull after motion integration.
    //
    // Decomp shape:
    // - Item_802697D4 integrates position, then calls Item_802696CC (when cull flags are enabled).
    // - Item_802696CC destroys items that cross left/right/bottom blast boundaries.
    // refs/melee/src/melee/it/item.c::{Item_802697D4,Item_802696CC}
    //
    // Sim scope: laser articles are always culled by left/right/bottom blast bounds in-suite.
    // Top-bound cull in decomp uses a large constant gate (10000.0f) under a separate flag path and
    // is intentionally left to lifetime/collision ownership until a full item-flag model is seeded.
    if (has_blast_bounds &&
        (x > blast_bounds.right || x < blast_bounds.left || y < blast_bounds.bottom)) {
      item_slot_clear(batch, ii);
      continue;
    }

    float t = batch->state.item_timer[ii];
    t -= 1.0f;
    batch->state.item_timer[ii] = t;
    if (!(t > 0.0f)) {
      item_slot_clear(batch, ii);
      continue;
    }

    // Decomp order:
    // - Fighter_8006CB94 -> ftColl_8007925C handles item-vs-fighter shield/BODY contact in the
    //   fighter proc,
    // - the laser's item collision callback (`itFoxlaser_UnkMotion1_Coll -> it_8029C4D4`) owns
    //   stage-line lifetime expiry after that fighter-contact phase.
    // Defer the stage-line result until after the defender loop below so same-frame shield hits
    // beat platform/floor expiry, matching replay-visible GuardSetOff rows on Pokemon Stadium.
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
    // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Coll,it_8029C4D4}
    const uint8_t stage_line_hit = stage_collision_item_line_hits_floor(stage_id, x0, y0, x, y);

    if (batch->state.item_reflect_transfer_seed_port[ii] < (uint8_t)num_players &&
        batch->state.item_reflect_transfer_seed_iid[ii] != 0u) {
      // Pending reflect callback ownership:
      // - ftColl_80077464 writes item->xC64_reflectGObj plus reflected owner/xDA8 snapshot lanes,
      // - Item_8026A294 later consumes that pending callback through Item_80269F14 before the
      //   article can be destroyed by a new reconstructed shield/BODY contact in this reseeded
      //   one-step.
      // Keep this explicit prefix-causal seed lane ahead of the reconstructed fighter-collision
      // loop; the generic post-loop apply below is still used when no competing contact would
      // consume the article first.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
      // refs/melee/src/melee/it/item.c::{Item_8026A294,Item_80269F14}
      item_apply_seeded_reflect_transfer_after_collision(batch, ii);
      msl_item_reflect_clear_seed_lanes(batch, ii);
      batch->state.item_hidden_callback_flags[ii] = 0u;
      if (stage_line_hit != 0u) {
        if (batch->state.item_timer[ii] > 1.0f) {
          batch->state.item_timer[ii] = 1.0f;
        }
        batch->state.item_pos_x[ii] = x;
        batch->state.item_pos_y[ii] = y;
      }
      continue;
    }

    // Collision: laser hitbox script defines multiple beam HitCapsules. Represent those active
    // slots as fixed-radius samples along the source projectile axis using generated
    // `hitbox_offsets_x`.
    //
    // Decomp: the laser model is rotated to match its velocity direction (rotY depends on
    // facing_dir=sign(vel.x), rotX depends on atan2(vel.y, vel_x)), and scaleZ ramps over time.
    // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
    //
    // Source of offsets: `data/items/lasers.bin` hitbox_offsets_x[] (MSLLASR1 v2), extracted
    // from the laser article state script in Pl*.dat by tools/extraction/extract_lasers.py.
    // agent_docs/DATA_CONTRACT.md documents the binary layout and decomp pointers.
    if (owner < 0 || owner >= num_players) {
      continue;
    }
    const size_t o_idx = msl_idx_player(bi, owner);
    if (laser_state == 1u && item_type_is_falco_laser(batch->state.item_type[ii]) &&
        batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_LW &&
        batch->state.action_frame[o_idx] > 28 &&
        fabsf(batch->state.item_timer[ii] - ((float)lp->lifetime_frames - 1.0f)) <= 1.0e-5f) {
      // Late Falco ThrowLw state1 spawn callback order:
      // - The frame-28 seed-pending source latch above owns the probe-backed immediate BODY
      //   callback.
      // - Later runtime-spawned state1 articles serialize at post-frame with lifeTimer decremented
      //   but do not run BODY until the following item collision callback; QGD's frame-31/32 control
      //   is the replay-real boundary for this split.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,itFoxlaser_UnkMotion1_Anim}
      continue;
    }
    // Beam axis in world space. Use velocity direction so angled shots do not erroneously collide
    // as if they were perfectly horizontal.
    const float vx = batch->state.item_vel_x[ii];
    const float vy = batch->state.item_vel_y[ii];
    float ux = 0.0f;
    float uy = 0.0f;
    {
      const float sp2 = vx * vx + vy * vy;
      if (sp2 > 0.0f) {
        const float inv_sp = 1.0f / sqrtf(sp2);
        ux = vx * inv_sp;
        uy = vy * inv_sp;
      } else {
        // Degenerate safety fallback; lasers should always have nonzero speed in normal play.
        ux = (batch->state.item_direction[ii] >= 0.0f) ? 1.0f : -1.0f;
        uy = 0.0f;
      }
    }
    const float sr = (laser_state == 0u) ? lp->size : lp->state1_size;

    // Beam length visual scaling: fox/falco blaster shots ramp model scaleZ over time.
    // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
    float laser_scale_z = 1.0f;
    float laser_prev_scale_z = 1.0f;
    float laser_age_frames = 0.0f;
    {
      // Laser scale is article-authored, not current-owner-authored. Reflected Falco/Fox lasers
      // keep their original article kind and ItemState script after Item_80269F14 transfers owner,
      // so the scale cap comes from the laser params record's source character rather than the
      // reflector's current fighter kind.
      // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
      // refs/melee/src/melee/it/item.c::Item_80269F14
      // data/items/lasers.bin (MSLLASR1 source char record keyed by item kind)
      const MslCharParams* chp = msl_char_params_fast(lp->source_char_id);
      const float cap = (chp && chp->laser_scale_max > 0.0f) ? chp->laser_scale_max : 1.0f;

      const float speed = sqrtf(vx * vx + vy * vy);
      const float lifetime = (float)lp->lifetime_frames;
      float age = lifetime - t;
      if (age < 0.0f) {
        age = 0.0f;
      }
      laser_age_frames = age;

      float s = (age * speed) /
                11.25f;  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
      if (s > cap) {
        s = cap;
      }
      // Decomp clamps very small scale to 1e-3 (avoids degenerates).
      if (s < 1e-5f) {  // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
        s = 1e-3f;
      }
      laser_scale_z = s;

      // HitCapsule x58/x4C ownership:
      // - item collision refresh carries previous x4C into x58, then rebuilds current x4C from the
      //   current JObj transform.
      // - Laser anim increments `foxlaser.scale` once per item anim update. For a reseeded
      //   one-step, x58 therefore belongs to the previous post-frame scale while x4C belongs to
      //   the current post-anim scale; using the current scale for both ends over-extends trailing
      //   laser BODY segments.
      // refs/melee/src/melee/it/itcoll.c::it_8027137C
      // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
      float prev_age = age - 1.0f;
      if (prev_age < 0.0f) {
        prev_age = 0.0f;
      }
      float prev_s = (prev_age * speed) / 11.25f;
      if (prev_s > cap) {
        prev_s = cap;
      }
      if (prev_s < 1e-5f) {
        prev_s = 1e-3f;
      }
      laser_prev_scale_z = prev_s;
    }
    batch->state.item_misc0[ii] = slippi_metadata_low_byte_from_f32(laser_scale_z);
    batch->state.item_misc1[ii] = slippi_metadata_low_byte_from_f32(msl_melee_normalize_angle(
        msl_melee_atan2f(batch->state.item_vel_y[ii], batch->state.item_vel_x[ii])));

    if (yoshi_shyguy_try_laser_item_hit(batch, bi, it, lp, laser_state, x0, y0, x, y, ux, uy, sr,
                                        laser_prev_scale_z, laser_scale_z)) {
      continue;
    }

    const size_t stale_owner_idx = msl_idx_player(bi, owner);
    const float laser_body_stale_mult = item_hitcapsule_stale_damage_mul(
        batch, ii, stale_owner_idx, batch->state.item_attack_id[ii]);
    uint8_t clear_after_body_damage_pass = 0u;
    for (int def = 0; def < num_players; def++) {
      if (def == owner) {
        continue;
      }

      const size_t d_idx = msl_idx_player(bi, def);
      if (msl_action_owns_x2219_collision_skip(batch->state.action_id[d_idx])) {
        // Dead*/Rebirth states own x2219_b1; laser item collision rejects that target before
        // shield/BODY admission.
        // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D3680,ftCo_800D3950,ftCo_800D3BC8,ftCo_800D3E40,ftCo_800D4580,ftCo_800D481C}
        // refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_800D4FF4,ftCo_800D5600}
        // refs/melee/src/melee/it/itcoll.c::it_80272460
        continue;
      }

      const uint16_t def_iid = batch->state.instance_id[d_idx];
      // Rehit suppression (HitCapsule victim rings): do not rehurt the same fighter repeatedly
      // while the item persists.
      // Item HitCapsule victim rings are per hitbox, not item-wide:
      // - it_8026FA2C / it_8026FAC4 propagate victim-ring writes by HitCapsule group,
      //   and lbColl_80008688 checks the selected HitCapsule's victims_1 ring.
      // - v10 event probes for BHH:4266 show a Fox state1 throw laser can carry hb2 victim-ring
      //   state while hb0 remains eligible for same-frame BODY/give-damage/destroy.
      // Keep this prefilter as "any hitbox remains eligible"; the narrowphase loop below still
      // tests the exact hitbox ring before probing geometry.
      // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
      const uint8_t item_hitlist_prefilter_allows =
          item_any_hitbox_allows_fighter(batch, bi, it, def, def_iid);
      if (!item_hitlist_prefilter_allows) {
        continue;
      }
      if (msl_motion_state_fx_special_kind(batch->state.char_id[d_idx],
                                           batch->state.action_id[d_idx]) ==
              (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_LOOP &&
          ((uint16_t)(batch->state.animation_index[d_idx] & 0xFFFFu)) == lp->air_loop_msid &&
          !move_tables_special_cmd0_raw_active_at_frame(batch->state.char_id[d_idx],
                                                        lp->air_loop_msid,
                                                        (int)batch->state.action_frame[d_idx])) {
        // Late aerial Blaster Loop command-script boundary:
        // - ftFx_SpecialAirNLoop_IASA only keeps the loop-repeat owner alive while cmd_vars[0] is
        //   set by the extracted script. After the clear frame, the fighter is in the terminal
        //   no-repeat part of the loop and native item-vs-fighter contact on same-family laser
        //   rows is not admitted until the action transitions/lands.
        // - Scope to the exact SpecialAirNLoop submotion and the raw cmd_var[0] window from
        //   MSLFTSC1; earlier loop frames and unrelated grounded laser rows keep normal contact.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
        //   ftFx_SpecialAirNLoop_IASA,ftFx_SpecialAirNLoop_Coll}
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071820
        // data/scripts/{fox,falco}.bin (MSLFTSC1 set_cmd_var idx=0 for msid 299)
        continue;
      }

      if (item_try_shine_reflect_contact(batch, ii, d_idx, def, lp, laser_state, x0, y0, x, y, ux,
                                         uy, sr, laser_scale_z)) {
        break;
      }

      const uint8_t item_hitcap_contact_mask =
          item_laser_fighter_hitcapsule_contact_mask_precedes_shield_body(
              batch, bi, def, lp, laser_state, x0, y0, x, y, ux, uy, sr, laser_prev_scale_z,
              laser_scale_z, ((laser_state == 0u) ? lp->damage : lp->state1_damage));
      if (item_hitcap_contact_mask != 0u) {
        // Fighter HitCapsule vs item HitCapsule ordering:
        // ftColl_8007925C builds eligible fighter HitCapsules before the item HitCapsule loop, then
        // resolves this catch_path before both ShieldDesc and BODY hurtcaps. When the item side is
        // allowed to trade/clank, ftColl_80077970 registers the fighter in the contacted item's
        // victims_1 ring through it_8026FAC4. Keep the write per HitCapsule and then let shield/BODY
        // continue: item BODY collision tests each selected HitCapsule's victims_1 ring, so
        // contacted slots are suppressed while unrelated item HitCapsules remain eligible.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077970}
        // refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_802706D0}
        for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
          if ((item_hitcap_contact_mask & (uint8_t)(1u << hb_id)) == 0u) {
            continue;
          }
          hitlist_register_item_hitbox_fighter(batch, bi, it, hb_id, def, def_iid,
                                               MSL_LBCOLL_INSERT_FT_HITBOX_CONTACT, 0u);
        }
      }

      const uint8_t flags_221b = batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                                          (size_t)MSL_STATE_FLAGS_221B_INDEX];
      if ((flags_221b & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) == 0u &&
          item_should_keep_existing_powershield_reflect_owner(batch, d_idx, def, owner, x0, y0, x,
                                                              y, sr, laser_scale_z)) {
        // Existing-reflected-owner keepalive:
        // ftColl_8007925C checks ReflectDesc before ShieldDesc. When GuardReflect has cleared
        // ShieldDesc and the laser is already owned by this fighter, the source ReflectDesc lane
        // keeps the item callback alive instead of letting the later ShieldDesc/HitShield path
        // re-hit the owner.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_8009370C}
        break;
      }
      const uint8_t fresh_guardon_reflect_owner_commit_before_shielddesc =
          item_should_commit_guardon_followup_powershield_reflect_owner(
              batch, ii, d_idx, def, owner, x0, y0, x, y, vx, laser_age_frames, sr, laser_scale_z);
      if ((flags_221b & (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) == 0u &&
          (item_should_commit_aged_powershield_reflect_owner(
               batch, ii, d_idx, def, x0, y0, x, y, vx, laser_age_frames, sr, laser_scale_z) ||
           fresh_guardon_reflect_owner_commit_before_shielddesc)) {
        // ReflectDesc-before-ShieldDesc source order:
        // ftColl_8007925C checks `fp->reflecting && hurt->x41_b7` and can call ftColl_80077464
        // even when GuardReflect has cleared ShieldDesc (`fp+0x221B_b0 == false`). The ordinary
        // shield/HitShield branch below is only the later ShieldDesc owner. Fresh GuardOn ->
        // GuardReflect follows the same source order: ftCo_8009388C clears ShieldDesc, installs
        // ReflectDesc, and the item owner/xDA8 snapshot is consumed before the next item motion.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_8009370C}
        // refs/melee/src/melee/it/item.c::Item_80269F14
        const MslCommonParams* c = msl_common_params();
        const float dmg_mul = (c != NULL && c->powershield_reflect_damage_mul > 0.0f)
                                  ? c->powershield_reflect_damage_mul
                                  : 1.0f;
        msl_item_reflect_set_damage_mul(batch, ii, dmg_mul);
        msl_item_reflect_commit_owner_snapshot_defer_speed(batch, ii, def);
        msl_item_reflect_apply_direction_lane(batch, ii);
        break;
      }

      // SHIELD precedence: if the item intersects the defender shield bubble, resolve as a shield
      // contact and do not take the BODY path.
      //
      // Authority:
      // - itfoxlaser.c Logic94 callbacks: it_2725_Logic94_HitShield / it_2725_Logic94_Reflected
      // - collision shield precedence: ftColl_80076CBC / lbColl_80007BCC
      // refs/melee/src/melee/it/items/itfoxlaser.c and refs/melee/src/melee/ft/ftcoll.c
      float shr = batch->state.shield_radius[d_idx];
      if (shr > 0.0f && batch->state.guard_reflect_entered_this_frame[d_idx] != 0u) {
        // Entry-frame item lane consumes the freshly seeded ftCo_800921DC lightshield scale (see
        // item_guard_shield_radius_from_state); shields_refresh has already published the steady
        // fighter-lane radius for this step.
        shr = item_guard_shield_radius_from_state(batch, msl_common_params(), d_idx);
      }
      const uint8_t guard_on_no_submotion_snapshot =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == 0xFFFFFFFFu)
              ? 1u
              : 0u;
      const MslCommonParams* common = msl_common_params();
      // Full-shield Dash guard-admission owner gate:
      // - Dash IASA only reaches ftCo_80091A4C / ftCo_80092450 through the late branch after
      //   `cur_anim_frame > x4C`; earlier Dash snapshots can still carry the untouched
      //   `start_shield_health` while no shield descriptor is yet live for item shield precedence.
      // - Restrict this owner gate to the fresh GuardReflect no-submotion admission context that
      //   teacher-forced reseed exposes on the repaired TBK rows; unrelated Dash rows must still
      //   resolve normal shield precedence.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50,ftCo_80092450}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
      const uint16_t seed_prev_action = batch->state.seed_prev_action_id[d_idx];
      const uint16_t frame_start_prev_action = batch->state.prev_action_id[d_idx];
      const uint8_t shield_fresh_dash_guardreflect_full_shield_snapshot =
          (batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_DASH &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           // Dash IASA digital powershield admission enters GuardReflect through ftCo_80091A4C ->
           // ftCo_800939B4 -> ftCo_80093A50. Keep the full-shield no-hit snapshot suppression
           // aligned to that fresh ReflectDesc owner. Held-shield GuardOn admission through
           // ftCo_80091AD8 -> ftCo_800923B4 installs ShieldDesc before item callbacks, so it must
           // stay on normal geometry-driven shield precedence.
           // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
           // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
           //   ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
           batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
           batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u && common != NULL &&
           batch->state.shield_hp[d_idx] >= common->start_shield_health)
              ? 1u
              : 0u;
      // GuardOn_Anim -> GuardOn_IASA -> GuardReflect followup:
      // - GuardOn_Anim owns shield drain/x10 before the LR edge enters GuardReflect through
      //   ftCo_8009388C,
      // - ftCo_8009388C clears shield desc before installing the reflect descriptor,
      // - if the projectile then misses the reflect descriptor and hits BODY, regular shield
      //   precedence must not consume the contact first.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_GuardOn_Anim,ftCo_GuardOn_IASA,ftCo_8009388C,ftCo_8009370C}
      const uint8_t shield_dash_guardon_followup_guard_reflect_snapshot =
          (seed_prev_action == (uint16_t)MSL_ACT_DASH &&
           batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
           batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
           batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u)
              ? 1u
              : 0u;
      // Dash_IASA `dash.x4 != 0` handoff into held-shield GuardOn:
      // - After the early `dash.x4 && cur_anim_frame <= x44` slice, ftCo_Dash_IASA can reach
      //   GuardOn through ftCo_80091AD8 -> ftCo_800923B4.
      // - ftCo_800924C0 installs ShieldDesc before item callbacks, so same-step item shield
      //   contact is geometry-owned rather than globally deferred.
      // - This fresh entry must use the article's actual scaleZ endpoint lane; the reduced
      //   descriptor sample over-admits stale trailing laser samples on Dash controls.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091AD8,ftCo_800923B4,ftCo_800924C0}
      // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Anim,it_8029C4D4}
      const uint8_t shield_dash_91ad8_guardon_same_step =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
           batch->state.guard_on_entered_this_frame[d_idx] != 0u &&
           batch->state.guard_entry_via_dash_91ad8[d_idx] != 0u)
              ? 1u
              : 0u;
      // Active GuardReflect window:
      // - ftCo_GuardReflect_Anim ticks x14 before chaining GuardOn_Anim; while the seeded frozen
      //   snapshot still has more than one x14 tick left, the reflect-window owner has not yet
      //   handed the row to the regular shield-hit / GuardSetOff path.
      // - Final visible x14 rows use the explicit HitShield handoff below; rows without that
      //   source geometry stay on GuardReflect, and rows with it may enter GuardSetOff.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092F2C}
      const uint8_t guard_reflect_active_window_no_submotion_snapshot =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           batch->state.guard_reflect_timer_x14_seed[d_idx] > 1u &&
           batch->state.guard_reflect_timer_x18_seed[d_idx] > 0u)
              ? 1u
              : 0u;
      const uint8_t guard_reflect_origin_x14_expired =
          item_guardreflect_origin_x14_expired_this_callback(batch, d_idx);
      const uint8_t guard_reflect_frozen_final_seed_snapshot =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.action_frame[d_idx] <= -2 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           batch->state.guard_reflect_timer_x14_seed[d_idx] == 1u &&
           batch->state.guard_reflect_timer_x18_seed[d_idx] > 0u)
              ? 1u
              : 0u;
      const uint8_t guard_reflect_frozen_final_seed_keepalive_snapshot =
          (guard_reflect_frozen_final_seed_snapshot && !guard_reflect_origin_x14_expired) ? 1u : 0u;
      const uint16_t prev_action = batch->state.seed_prev_action_id[d_idx];
      const uint8_t guard_on_entry_from_landing =
          (guard_on_no_submotion_snapshot && (prev_action == (uint16_t)MSL_ACT_LANDING)) ? 1u : 0u;
      const uint8_t guard_reflect_entry_from_landing =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           prev_action == (uint16_t)MSL_ACT_LANDING)
              ? 1u
              : 0u;
      if (!(shr > 0.0f) &&
          item_is_fresh_guardreflect_shield_center_source(batch, d_idx, prev_action)) {
        // Same-step GuardReflect entry can install ShieldDesc after shields_refresh() has already
        // run in this simulator step. Synthesize the decomp-shaped ShieldDesc scalar here for item
        // shield precedence; `item_try_guard_fresh_shield_center` below supplies the matching
        // current-pose center.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80092450,ftCo_800921DC}
        shr = item_guard_shield_radius_from_state(batch, common, d_idx);
      }
      if (!(shr > 0.0f) && item_no_submotion_guard_shielddesc_root_x_owner(batch, d_idx)) {
        // No-submotion Guard seed reconstruction:
        // Slippi records the live ShieldDesc bit (`fp+0x221B_b0`) but not the shield JObj scale
        // when the post-frame action has no submotion (`animation_index=-1`). The item collision
        // owner is still ftColl_8007925C's ShieldDesc pass, so synthesize the scalar radius from
        // the same ftCo_Guard.c inlineB0/common-data formula used by shields_refresh() and keep
        // the later center/root-X owner split local to item collision.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_8007B1B8}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{inlineB0,ftCo_80091D58}
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x221B -> state_flags[2])
        shr = item_guard_shield_radius_from_state(batch, common, d_idx);
      }
      const uint8_t guard_reflect_same_frame_landing_entry =
          (guard_reflect_entry_from_landing &&
           batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
           batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u)
              ? 1u
              : 0u;
      if (shr > 0.0f && !guard_on_entry_from_landing && !guard_reflect_same_frame_landing_entry &&
          !shield_fresh_dash_guardreflect_full_shield_snapshot &&
          !shield_dash_guardon_followup_guard_reflect_snapshot) {
        // Use derived shield bubble center from shields_refresh() (same geometry used by the
        // fighter-vs-fighter combat pass).
        float shx = batch->state.shield_x[d_idx];
        float shy = batch->state.shield_y[d_idx];
        float shz = batch->state.shield_z[d_idx];
        const float shield_bubble_shx = shx;
        const float shield_bubble_shy = shy;
        const float shield_bubble_shz = shz;
        uint8_t fresh_guard_center_used =
            item_try_guard_fresh_shield_center(batch, d_idx, laser_age_frames, &shx, &shy, &shz);
        if (!fresh_guard_center_used) {
          (void)item_try_guardon_carried_behavior_shield_center(batch, d_idx, &shx, &shy, &shz);
        }
        if (!isfinite(shx) || !isfinite(shy) || !isfinite(shz)) {
          shx = batch->state.pos_x[d_idx];
          shy = batch->state.pos_y[d_idx];
          shz = batch->state.pos_z[d_idx];
        }
        if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
            batch->state.action_frame[d_idx] == -1 &&
            batch->state.animation_index[d_idx] == UINT32_MAX &&
            batch->state.guard_reflect_timer_x14_seed[d_idx] == 1u &&
            batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT) {
          // On the final visible GuardReflect tick, ReflectDesc ownership is still live but the
          // ordinary ShieldDesc branch no longer consumes the stale GuardOn x-offset serialized in
          // shield_x. Source has cleared/rebuilt descriptor state through GuardReflect_Anim /
          // ftCo_80093BC0 before item collision; keep ShieldDesc's horizontal sample on the fighter
          // root while preserving the live shield bone height. The longer x18/x221C_b2 lifetime is
          // consumed later by ftCo_80092F2C's recoil multiplier, not by this descriptor sample.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
          shx = batch->state.pos_x[d_idx];
        }
        uint8_t shield_hit = 0;
        uint8_t shield_bounce_contact_found = 0;
        uint8_t shield_bounce_source_allows = 0;
        const uint8_t shield_bounce_seed_valid =
            batch->state.item_shield_bounce_seed_valid[ii] ? 1u : 0u;
        float shield_bounce_best_vy = -INFINITY;
        float shield_bounce_source_vx = 0.0f;
        float shield_bounce_source_vy = 0.0f;
        float shield_hit_contact_x = x;
        float shield_hit_contact_y = y;
        const uint8_t guard_reflect_final_x14_same_owner_reflect_keepalive =
            (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
             batch->state.action_frame[d_idx] < 0 &&
             batch->state.animation_index[d_idx] == UINT32_MAX &&
             batch->state.guard_reflect_timer_x14_seed[d_idx] == 1u &&
             (owner == def || batch->state.item_reflect_damage_mul[ii] != 1.0f))
                ? 1u
                : 0u;
        // Decomp call-chain anchors:
        // - itFoxlaser_UnkMotion1_Phys snapshots pre-move position (`foxlaser.pos = item->pos`).
        // - it_8029C4D4 runs collision using (prev_pos, cur_pos) and dispatches the hit callback.
        // - it_2725_Logic94_HitShield is the laser shield-hit resolution callback.
        // refs/melee/src/melee/it/items/itfoxlaser.c::{
        //   itFoxlaser_UnkMotion1_Phys,it_8029C4D4,it_2725_Logic94_HitShield}
        const uint8_t defender_guard_reflect_no_submotion_snapshot =
            (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
             batch->state.action_frame[d_idx] < 0 &&
             batch->state.animation_index[d_idx] == 0xFFFFFFFFu &&
             batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] == 0u)
                ? 1u
                : 0u;
        const uint8_t guard_reflect_final_seed_point_sample =
            (defender_guard_reflect_no_submotion_snapshot &&
             batch->state.action_frame[d_idx] == -1 &&
             batch->state.guard_reflect_timer_x14_seed[d_idx] == 1u &&
             batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT)
                ? 1u
                : 0u;
        const uint8_t guard_reflect_terminal_x10_point_sample =
            (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD &&
             batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
             batch->state.guard_x10_frame_start[d_idx] == 1u &&
             batch->state.guard_x10[d_idx] == 0u &&
             batch->state.guard_reflect_timer_x14[d_idx] == 0u &&
             batch->state.guard_reflect_timer_x18[d_idx] == 0u && laser_state == 0u &&
             item_type_is_falco_laser(batch->state.item_type[ii]) != 0u && laser_age_frames > 1.0f)
                ? 1u
                : 0u;
        // Keep the early geometry lane ownership-consistent with the later GuardSetOff owner gate by
        // sharing the same collision-time powershield-active predicate and stale-x18 suppression
        // before we narrow the late locomotion snapshot.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
        //   ftCo_80091A4C,ftCo_800939B4,ftCo_8009370C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
        uint8_t guard_reflect_geom_can_powershield_reflect =
            combat_is_powershield_active_idx(batch, d_idx) ? 1u : 0u;
        if (guard_reflect_geom_can_powershield_reflect &&
            defender_guard_reflect_no_submotion_snapshot &&
            batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
            batch->state.guard_reflect_timer_x18_seed[d_idx] != 0u) {
          guard_reflect_geom_can_powershield_reflect = 0u;
        }
        const uint8_t guard_reflect_late_nonshield_snapshot =
            (guard_reflect_geom_can_powershield_reflect &&
             defender_guard_reflect_no_submotion_snapshot &&
             batch->state.action_frame[d_idx] == -1 && laser_age_frames > 1.0f &&
             batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON &&
             batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD &&
             batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT &&
             batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_SET_OFF)
                ? 1u
                : 0u;
        const uint8_t guard_reflect_late_locomotion_snapshot_base =
            (defender_guard_reflect_no_submotion_snapshot &&
             batch->state.action_frame[d_idx] == -1 && laser_age_frames > 1.0f &&
             batch->state.item_reflect_transfer_seed_port[ii] >=
                 (uint8_t)batch->config.num_players &&
             (batch->state.guard_reflect_timer_x14_seed[d_idx] != 0u ||
              batch->state.guard_reflect_timer_x18_seed[d_idx] != 0u) &&
             seed_prev_action != (uint16_t)MSL_ACT_GUARD_ON &&
             seed_prev_action != (uint16_t)MSL_ACT_GUARD &&
             seed_prev_action != (uint16_t)MSL_ACT_GUARD_REFLECT &&
             seed_prev_action != (uint16_t)MSL_ACT_GUARD_SET_OFF)
                ? 1u
                : 0u;
        uint8_t guard_reflect_late_locomotion_reflect_desc_overlap = 0u;
        if (guard_reflect_late_locomotion_snapshot_base && common != NULL &&
            common->powershield_reflect_size > 0.0f) {
          const float reflect_r =
              common->powershield_reflect_size * batch->state.fighter_scale_y[d_idx];
          float reflect_x = 0.0f, reflect_y = 0.0f, reflect_z = 0.0f;
          if (item_guard_reflect_center_xyz(batch, d_idx, &reflect_x, &reflect_y, &reflect_z)) {
            guard_reflect_late_locomotion_reflect_desc_overlap = item_sphere_sphere_intersects_3d(
                x, y, 0.0f, sr, reflect_x, reflect_y, reflect_z, reflect_r);
          }
        }
        const uint8_t guard_reflect_late_locomotion_shield_desc_overlap =
            (guard_reflect_late_locomotion_snapshot_base &&
             batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_LANDING &&
             item_state_flags_2218_is_reflect_behavior_only(
                 batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                          (size_t)MSL_STATE_FLAGS_2218_INDEX]) &&
             item_sphere_sphere_intersects_2d(x, y, sr, shx, shy, shr))
                ? 1u
                : 0u;
        const uint8_t guard_reflect_late_locomotion_hitshield_handoff =
            (guard_reflect_late_locomotion_snapshot_base &&
             (guard_reflect_late_locomotion_reflect_desc_overlap ||
              guard_reflect_late_locomotion_shield_desc_overlap))
                ? 1u
                : 0u;
        const uint8_t guard_reflect_same_frame_locomotion_entry_bounce_normal =
            (fresh_guard_center_used && defender_guard_reflect_no_submotion_snapshot &&
             batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
             batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u &&
             batch->state.guard_reflect_timer_x14[d_idx] != 0u &&
             batch->state.guard_reflect_timer_x18[d_idx] != 0u &&
             item_prev_action_is_guard_reflect_locomotion_pose_source(seed_prev_action) &&
             seed_prev_action != (uint16_t)MSL_ACT_GUARD_ON &&
             seed_prev_action != (uint16_t)MSL_ACT_GUARD &&
             seed_prev_action != (uint16_t)MSL_ACT_GUARD_REFLECT &&
             seed_prev_action != (uint16_t)MSL_ACT_GUARD_SET_OFF)
                ? 1u
                : 0u;
        // Same-frame locomotion -> GuardReflect has two shield-center consumers in this item pass:
        // the shield-overlap path sees the live GuardReflect shield descriptor installed by
        // ftCo_80093A50, while the ShieldBounced xC54/xC58 normal for the incoming laser can still
        // be sourced from the collision-time shield bubble that existed before the descriptor was
        // rebuilt. Keep the split only on the fresh entry phase exposed by the zero seed timers
        // plus nonzero live GuardReflect timers; stale/frozen snapshots continue to use one center.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
        // refs/melee/src/melee/it/item.c::Item_80269DC8
        float bounce_shx =
            guard_reflect_same_frame_locomotion_entry_bounce_normal ? shield_bubble_shx : shx;
        float bounce_shy =
            guard_reflect_same_frame_locomotion_entry_bounce_normal ? shield_bubble_shy : shy;
        float bounce_shz =
            guard_reflect_same_frame_locomotion_entry_bounce_normal ? shield_bubble_shz : shz;
        const uint8_t defender_guard_hold_no_submotion_snapshot =
            (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD &&
             batch->state.action_frame[d_idx] < 0 &&
             batch->state.animation_index[d_idx] == UINT32_MAX &&
             batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD &&
             batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] == 0u)
                ? 1u
                : 0u;
        // Steady Guard no-submotion snapshot split:
        // - ftCo_GuardOn_Anim can already have settled into Guard through ftCo_800928CC before the
        //   replay-visible post-frame, leaving a frozen Guard snapshot (`af=-1`, `anim=-1`) whose
        //   shield owner is already the settled Guard hold for this step.
        // - Replayed carried lasers in this settled snapshot stay on the point sample; broad
        //   authored-offset sweeps over-admit GAT/QGD carried-shot controls.
        // - Lasers in the frame-start `fp+0x2218_b5` behavior lane use the authored HitCapsule
        //   offsets for the immediate shield callback only while the owner is still in the
        //   SpecialN loop callback phase that owns the shot.
        // - The raw x2218_b1 live-article lane is already item-callback provenance from the current
        //   ftColl pass, so it also consumes the current item HitCapsule segment even when the
        //   fighter owner has already left SpecialN.
        // - The x2218_b2 and x2218_b0 command/interrupt lanes stay on the settled point sample;
        //   GuardOn entry rows have their separate command-pose gate above.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
        //   ftCo_GuardOn_Anim,ftCo_800928CC,ftCo_Guard_IASA}
        // refs/melee/src/melee/it/items/itfoxlaser.c::{
        //   it_8029C504,itFoxlaser_UnkMotion1_Anim,itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
        const uint16_t laser_owner_action =
            (owner >= 0) ? batch->state.prev_action_id[msl_idx_player(bi, owner)] : 0u;
        const uint8_t laser_owner_specialn_loop =
            (msl_motion_state_fx_special_kind(batch->state.char_id[o_idx], laser_owner_action) ==
                 (uint8_t)MSL_FX_KIND_SPECIAL_N_LOOP ||
             msl_motion_state_fx_special_kind(batch->state.char_id[o_idx], laser_owner_action) ==
                 (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_LOOP)
                ? 1u
                : 0u;
        const uint16_t laser_owner_frame_start_action =
            (owner >= 0) ? batch->state.frame_start_action_id[msl_idx_player(bi, owner)] : 0u;
        const uint8_t laser_owner_frame_start_specialn_loop =
            (msl_motion_state_fx_special_kind(batch->state.char_id[o_idx],
                                              laser_owner_frame_start_action) ==
                 (uint8_t)MSL_FX_KIND_SPECIAL_N_LOOP ||
             msl_motion_state_fx_special_kind(batch->state.char_id[o_idx],
                                              laser_owner_frame_start_action) ==
                 (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_LOOP)
                ? 1u
                : 0u;
        const uint8_t guard_hold_allow_interrupt_behavior_carried_laser =
            (defender_guard_hold_no_submotion_snapshot &&
             (batch->state.state_flags_2218_frame_start[d_idx] & 0xA4u) == 0x84u)
                ? 1u
                : 0u;
        const uint8_t guard_hold_command_behavior_current_point_laser =
            (defender_guard_hold_no_submotion_snapshot &&
             (batch->state.state_flags_2218_frame_start[d_idx] &
              (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                        MSL_STATE_FLAG_2218_B2 | MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR)) ==
                 (uint8_t)(MSL_STATE_FLAG_2218_B2 | MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR))
                ? 1u
                : 0u;
        const uint8_t guard_hold_stale_command_behavior_laser =
            (guard_hold_command_behavior_current_point_laser && laser_age_frames >= 9.0f &&
             laser_prev_scale_z >= 2.99f && laser_scale_z >= 2.99f)
                ? 1u
                : 0u;
        const uint8_t guard_on_landing_carried_live_article_point_sample =
            (guard_on_no_submotion_snapshot && !laser_owner_specialn_loop &&
             (batch->state.state_flags_2218_frame_start[d_idx] &
              (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                        MSL_STATE_FLAG_2218_B2 | MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR)) ==
                 (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1))
                ? 1u
                : 0u;
        // Steady Guard (179) rows consume the full live segment: the point-sample policy models
        // the GuardOn pose-blend window (ftCo_80091E78's TransN blend arg = mv.co.guard.x0/x2E8
        // ramps the shield bone in over the GuardOn anim), during which the descriptor has not
        // yet reached its settled center. Once the blend completes (steady Guard), vanilla's
        // ftColl sweeps the real laser segment. Witness pair, same replay/hold/laser family:
        // PRH:5877 (GuardOn, blend window) misses a laser whose sweep covers the settled bubble,
        // while PRH:5903 (steady Guard, blend done) shield-hits at a larger nominal distance.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_80091E78}
        // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
        // Steady-Guard laser shield admission splits on the defender's raw fp+0x2218 byte,
        // fitted on replay-real witnesses with engine-true geometry:
        // - ALLOW_INTERRUPT (0x80) set: X-reach law - the contact registers exactly when the
        //   swept head-X interval, expanded by the HitCapsule radius, contains the shield-bone X
        //   (PRH:5903 0xC0 hits at dx 1.15; PRH:5878 hits on the crossing; QGD:2571 0xC0 misses
        //   at dx 5.10 despite full radial overlap; the bubble's radial distance over-admits the
        //   approach frames).
        // - REFLECT_BEHAVIOR (0x04) without 0x80: the carried point sample (DCC:6517 0x24,
        //   STM SweatyThisMallard 0x24 - native serializes the shot's shield routing at t+1).
        // - B1-only (0x40): the ordinary full-segment bubble sweep (MAJ:5398, PPA:5170 hit at
        //   dx ~4.7-5.0).
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
        // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
        const uint8_t guard_hold_x2218 = batch->state.state_flags_2218_frame_start[d_idx];
        const uint8_t same_owner_laser_article =
            item_unreflected_laser_article_matches_owner_kind(batch, ii) ? 1u : 0u;
        // The reach pad is the SECOND authored beam sample (it_8029C4D4 lays the shield probes
        // along hitbox_offsets_x; the leading pair participates in the settled-Guard reach).
        // Engine-true witnesses bracket the pad to [1.63, 2.75) - uniquely offsets[1] (2.539):
        // TVR(aggregate):5546 hits at head gap 2.80 (= sr + 1.63), doubles
        // Game_20260509T152622:3617 at 0.56, while QGD:2571 misses at gap 3.92 and
        // TVR(pstadium):5545 at 7.80 (one row before their real contacts).
        float guard_hold_xreach_pad = 0.0f;
        {
          const uint8_t xr_off_n =
              (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
          if (xr_off_n > 0u) {
            const uint8_t xr_oi = (xr_off_n > 1u) ? 1u : 0u;
            const float off_pad = (laser_state == 0u) ? lp->hitbox_offsets_x[xr_oi]
                                                      : lp->state1_hitbox_offsets_x[xr_oi];
            guard_hold_xreach_pad = fabsf(off_pad);
          }
        }
        const uint8_t guard_hold_xreach_ok =
            (!defender_guard_hold_no_submotion_snapshot ||
             (guard_hold_x2218 & (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) == 0u ||
             ((fminf(x0, x) - sr - guard_hold_xreach_pad) <= shx &&
              shx <= (fmaxf(x0, x) + sr + guard_hold_xreach_pad)))
                ? 1u
                : 0u;
        // Pure reflect-behavior byte (0x04, B1/B2 clear): same-owner carried point sample applies
        // only while the previous laser endpoint is still in the sub-identity scale startup slice.
        // Once the previous endpoint has reached the normal beam scale, source item collision
        // consumes the real x58->x4C segment from itFoxlaser_UnkMotion1_Phys -> it_8029C4D4 even if
        // the owner has already left SpecialN. Witnesses:
        // - DCC:2231 (prev scale 0.444) stays point-sample/no-hit.
        // - RHS:365/10682 (prev scales 1.333/2.222) use live segment and enter GuardSetOff.
        // IAT:3575 remains live while the owner is still in the SpecialN loop.
        // B1 + reflect-behavior (0x44) is a separate carried live-article lane for unreflected
        // spawn-owned lasers once the owner starts the frame outside the blaster loop, and remains
        // on the point sample; DGS:10435 and TVR:1037 are mature-scale negatives. Reflected or
        // transferred lasers are excluded by item_reflect provenance even when the public owner kind
        // now matches the article kind. If the owner started this frame in the SpecialN loop, this
        // is still the live article callback even if the owner lands later in the same frame
        // (HHG:7494). B1-only 0x40 rows also continue to use the full item segment.
        // refs/melee/src/melee/it/items/itfoxlaser.c::{
        //   itFoxlaser_UnkMotion1_Anim,itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
        // Command-behavior byte (0x24, B2 set): the shot's shield routing serializes at t+1
        // regardless of the owner state (DCC:6517, STM SweatyThisMallard).
        const uint8_t guard_hold_pure_reflect_behavior_startup_point_sample =
            (same_owner_laser_article != 0u && !laser_owner_specialn_loop &&
             laser_prev_scale_z < 1.0f)
                ? 1u
                : 0u;
        const uint8_t guard_hold_reflect_behavior_b1_point_sample =
            (same_owner_laser_article != 0u && !laser_owner_frame_start_specialn_loop &&
             (guard_hold_x2218 & (uint8_t)MSL_STATE_FLAG_2218_B1) != 0u)
                ? 1u
                : 0u;
        const uint8_t guard_hold_reflect_behavior_point_sample =
            (defender_guard_hold_no_submotion_snapshot &&
             (guard_hold_x2218 & (uint8_t)MSL_STATE_FLAG_2218_ALLOW_INTERRUPT) == 0u &&
             (guard_hold_x2218 & (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR) != 0u &&
             (guard_hold_pure_reflect_behavior_startup_point_sample ||
              guard_hold_reflect_behavior_b1_point_sample ||
              (guard_hold_x2218 & (uint8_t)MSL_STATE_FLAG_2218_B2) != 0u))
                ? 1u
                : 0u;
        const uint8_t shield_uses_point_sample =
            (guard_hold_reflect_behavior_point_sample ||
             guard_on_landing_carried_live_article_point_sample ||
             guard_reflect_terminal_x10_point_sample)
                ? 1u
                : 0u;
        const float shield_probe_x = shield_uses_point_sample ? x0 : x;
        const float shield_probe_y = shield_uses_point_sample ? y0 : y;

        uint8_t off_n =
            (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
        if (shield_uses_point_sample || guard_reflect_final_seed_point_sample) {
          // Settled GuardOn/Guard carried laser sample:
          // Once the laser owner has left the SpecialN callback that owns the current segment,
          // replay-visible live-article bits alone do not authorize a fresh shield sweep. Source
          // item collision consumes the carried point sample from the article callback state
          // exposed by the post-frame row; fresh SpecialN-loop rows keep the current-segment path.
          // Terminal GuardReflect x10=1 follows the same sample boundary: GuardReflect_Anim chains
          // through GuardOn_Anim into Guard at the post-frame, but that final no-submotion
          // GuardReflect callback does not expose the next laser segment to ShieldDesc until the
          // following settled Guard row.
          // refs/melee/src/melee/it/items/itfoxlaser.c::{
          //   itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_GuardReflect_Anim,ftCo_GuardOn_Anim,ftCo_800928CC}
          off_n = 0u;
        }
        // Decomp consumes one shared scaleZ transform chain for laser collision spaces
        // (shield/body/reflect) via item collision callbacks. Preserve the identity cap for fresh
        // late non-shield state0 GuardReflect shots: locomotion-origin ShieldBounced consumes the
        // live ShieldDesc center for its normal, but full scaled SHIELD segments over-admit
        // compacted-laser and Landing-origin negatives before their distinct item HitCapsule slot
        // owners are extracted.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
        // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Anim,it_8029C4D4}
        // refs/melee/src/melee/it/itcoll.c::it_8027137C
        const uint8_t shield_cap_enabled =
            (shield_dash_91ad8_guardon_same_step ||
             (defender_guard_reflect_no_submotion_snapshot &&
              !(guard_reflect_late_nonshield_snapshot && laser_state == 0u)))
                ? 0u
                : 1u;
        const uint8_t collision_time_fresh_guardon_shielddesc =
            item_fresh_guardon_locomotion_shielddesc_owner(batch, d_idx);
        // Shield HitCapsule endpoint ownership:
        // - itcoll refresh carries the previous endpoint (`x58`) from the prior post-frame scale
        //   and rebuilds the current endpoint (`x4C`) from this frame's item scale,
        // - ftColl_8007925C passes the current item scale into lbColl_80007BCC/80007DD8 for the
        //   capsule radius.
        // Keep this on the ShieldBounced xC54/xC58 source path after a shield hit is admitted.
        // Shield admission itself stays on the reduced descriptor sample where source ShieldDesc
        // ownership says HitShield, then bounce uses the full item HitCapsule x58/x4C endpoint.
        // refs/melee/src/melee/it/itcoll.c::it_8027137C
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80007DD8}
        const float laser_offset_scale = laser_collision_offset_scale(
            lp, laser_state, laser_scale_z, MSL_LASER_COLLISION_SPACE_SHIELD, 0u,
            collision_time_fresh_guardon_shielddesc ? 0u : shield_cap_enabled);
        const MslCharParams* shield_owner_params =
            msl_char_params_fast(batch->state.char_id[d_idx]);
        const float shield_owner_model_scale =
            (shield_owner_params != NULL && isfinite(shield_owner_params->model_scaling) &&
             shield_owner_params->model_scaling > 0.0f)
                ? shield_owner_params->model_scaling
                : 1.0f;
        const float shield_bounce_shield_radius = shr * shield_owner_model_scale;
        // `lbColl_80007DD8` receives the HitCapsule scale and item object scale (`item->scl`), not
        // the FoxLaser visual scaleZ installed by `itFoxlaser_UnkMotion1_Anim`. The beam length is
        // represented by x58->x4C endpoints. The shield side receives ShieldDesc.size=1 plus the
        // shield JObj matrix; `shield_radius` is the live JObj scale and the matrix axis carries
        // fighter model scale, so apply that model-scale term here just as lbColl_800077A0 does.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091D58,ftCo_80092450}
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
        // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007DD8
        // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
        const float shield_bounce_radius = sr;
        for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X; oi++) {
          const float off_x =
              (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
          const float s = off_x * laser_offset_scale;
          const float sx0 = x0 + (ux * s);
          const float sy0 = y0 + (uy * s);
          const float sx = shield_probe_x + (ux * s);
          const float sy = shield_probe_y + (uy * s);
          // Shield admission above still uses a reduced shield-adjacent offset cap for known
          // over-admit controls. Once a shield hit is admitted, ftColl_80077688/80007DD8 consumes
          // the real item HitCapsule x58/x4C endpoints, whose beam length comes from the visual
          // FoxLaser scaleZ installed on the item JObj.
          // refs/melee/src/melee/it/itcoll.c::it_8027137C
          // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Anim
          const float bounce_prev_s = off_x * laser_prev_scale_z;
          const float bounce_cur_s = off_x * laser_scale_z;
          const float bounce_sx0 = x0 + (ux * bounce_prev_s);
          const float bounce_sy0 = y0 + (uy * bounce_prev_s);
          const float bounce_sx = shield_probe_x + (ux * bounce_cur_s);
          const float bounce_sy = shield_probe_y + (uy * bounce_cur_s);
          uint8_t this_hit = 0u;
          // Decomp (GALE01): shield overlap uses 3D collision (z is not ignored), and item
          // collision callbacks consume prev->cur segment ownership (`it_8029C4D4`).
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC (lbColl_80007BCC(..., cur_pos.z))
          // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
          if (item_swept_sphere_sphere_intersects_3d(sx0, sy0, 0.0f, sx, sy, 0.0f, sr, shx, shy,
                                                     shz, shr)) {
            this_hit = 1u;
          }
          if (!this_hit) {
            continue;
          }
          if (guard_reflect_final_x14_same_owner_reflect_keepalive) {
            // Same-owner final-x14 GuardReflect keepalive:
            // The laser is already on the reflected item callback lane (`Item_80269F14`) while x14
            // is still visible, proven either by defender ownership or the seeded reflected damage
            // multiplier. Do not reinterpret that callback as a new opponent-projectile ShieldDesc
            // hit (`Item_80269DC8`). Opponent-owned/default-multiplier final-x14 lasers still follow
            // the HitShield handoff.
            // refs/melee/src/melee/it/item.c::Item_80269F14
            // refs/melee/src/melee/it/item.c::Item_80269DC8
            continue;
          }
          shield_hit = 1u;
          if (shield_hit_contact_x == x && shield_hit_contact_y == y) {
            shield_hit_contact_x = sx;
            shield_hit_contact_y = sy;
          }
          if (!shield_bounce_contact_found) {
            shield_bounce_contact_found = 1u;
          }
          {
            // ShieldBounced normal ownership:
            // - ftColl_80077688 copies the lbColl_800077A0 result into item->xC54/xC58.
            // - Item_80269DC8 still rejects side contacts whose xC54 is beyond the item-common
            //   threshold. Preserve that predicate for every reduced laser sample, then choose the
            //   strongest accepted upward mirror result from the enumerated source HitCapsule
            //   offsets. Explicit item_shield_bounce_seed_* lanes override this live
            //   reconstruction when post-frame serialization proves a hidden native result.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
            // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007DD8,lbColl_800077A0}
            // refs/melee/src/melee/it/item.c::Item_80269DC8
            float trial_bounce_vx = 0.0f;
            float trial_bounce_vy = 0.0f;
            if (laser_try_shield_bounce_velocity_from_segment(
                    vx, vy, bounce_shx, bounce_shy, bounce_shz, shield_bounce_shield_radius,
                    bounce_sx0, bounce_sy0, 0.0f, bounce_sx, bounce_sy, 0.0f, shield_bounce_radius,
                    &trial_bounce_vx, &trial_bounce_vy) &&
                trial_bounce_vy > shield_bounce_best_vy) {
              shield_bounce_source_allows = 1u;
              shield_bounce_source_vx = trial_bounce_vx;
              shield_bounce_source_vy = trial_bounce_vy;
              shield_bounce_best_vy = trial_bounce_vy;
            }
          }
        }
        // If no scripted offsets exist, fall back to the projectile origin on the same prev->cur
        // owner segment used by it_8029C4D4.
        if (!shield_hit && off_n == 0 &&
            (!guard_hold_stale_command_behavior_laser || shield_bounce_seed_valid)) {
          // Once a carried x2218_b2 + reflect-behavior laser has saturated its scale ramp,
          // require explicit ShieldBounced seed provenance before publishing a shield hit; fresh
          // scale-ramping laser rows still own their normal item callback contact.
          // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
          // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
          shield_hit = item_swept_sphere_sphere_intersects_3d(
              x0, y0, 0.0f, shield_probe_x, shield_probe_y, 0.0f, sr, shx, shy, shz, shr);
          if (shield_hit) {
            shield_hit_contact_x = shield_probe_x;
            shield_hit_contact_y = shield_probe_y;
            float trial_bounce_vx = 0.0f;
            float trial_bounce_vy = 0.0f;
            if (laser_try_shield_bounce_velocity_from_segment(
                    vx, vy, bounce_shx, bounce_shy, bounce_shz, shield_bounce_shield_radius, x0, y0,
                    0.0f, shield_probe_x, shield_probe_y, 0.0f, shield_bounce_radius,
                    &trial_bounce_vx, &trial_bounce_vy)) {
              shield_bounce_source_allows = 1u;
              shield_bounce_source_vx = trial_bounce_vx;
              shield_bounce_source_vy = trial_bounce_vy;
              shield_bounce_best_vy = trial_bounce_vy;
            }
            shield_bounce_contact_found = 1u;
          }
        }
        if (!shield_hit && guard_hold_allow_interrupt_behavior_carried_laser &&
            shield_bounce_seed_valid &&
            item_swept_sphere_sphere_intersects_3d(x0, y0, 0.0f, x, y, 0.0f, sr, shx, shy, shz,
                                                   shr)) {
          // Allow-interrupt Guard hold ShieldBounced owner:
          // The generic carried-laser shield path stays on the previous point to avoid broad
          // GuardSetOff over-admission, but rows where the current segment produces a valid
          // ftColl_80077688/Item_80269DC8 ShieldBounced normal are source-owned shield contacts.
          // Requiring seeded native bounce provenance keeps stale allow-interrupt command rows on
          // the settled Guard point sample instead of teacher-forcing the live article segment.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007DD8
          // refs/melee/src/melee/it/item.c::Item_80269DC8
          float trial_bounce_vx = 0.0f;
          float trial_bounce_vy = 0.0f;
          if (laser_try_shield_bounce_velocity_from_segment(
                  vx, vy, bounce_shx, bounce_shy, bounce_shz, shield_bounce_shield_radius, x0, y0,
                  0.0f, x, y, 0.0f, shield_bounce_radius, &trial_bounce_vx, &trial_bounce_vy)) {
            shield_hit = 1u;
            shield_hit_contact_x = x;
            shield_hit_contact_y = y;
            shield_bounce_contact_found = 1u;
            shield_bounce_source_allows = 1u;
            shield_bounce_source_vx = trial_bounce_vx;
            shield_bounce_source_vy = trial_bounce_vy;
            shield_bounce_best_vy = trial_bounce_vy;
          }
        }
        if (!shield_hit && !guard_reflect_final_x14_same_owner_reflect_keepalive &&
            item_laser_no_submotion_guard_entry_shielddesc_root_x_owner(batch, d_idx,
                                                                        shield_bounce_seed_valid)) {
          const float root_shx = batch->state.pos_x[d_idx];
          for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X; oi++) {
            const float off_x =
                (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
            const float s = off_x * laser_offset_scale;
            const float sx0 = x0 + (ux * s);
            const float sy0 = y0 + (uy * s);
            const float sx = shield_probe_x + (ux * s);
            const float sy = shield_probe_y + (uy * s);
            if (!item_swept_sphere_sphere_intersects_3d(sx0, sy0, 0.0f, sx, sy, 0.0f, sr, root_shx,
                                                        shy, shz, shr)) {
              continue;
            }
            shield_hit = 1u;
            shield_hit_contact_x = sx;
            shield_hit_contact_y = sy;
            shield_bounce_contact_found = 1u;
            {
              // ShieldBounced normal still comes from ftColl_80077688's item/shield contact
              // result. Only the ShieldDesc horizontal owner is retried at root X; the bounce
              // velocity is either seed-proven xC58 or reconstructed from the same accepted source
              // segment.
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
              // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007DD8
              // refs/melee/src/melee/it/item.c::Item_80269DC8
              const float bounce_prev_s = off_x * laser_prev_scale_z;
              const float bounce_cur_s = off_x * laser_scale_z;
              const float bounce_sx0 = x0 + (ux * bounce_prev_s);
              const float bounce_sy0 = y0 + (uy * bounce_prev_s);
              const float bounce_sx = shield_probe_x + (ux * bounce_cur_s);
              const float bounce_sy = shield_probe_y + (uy * bounce_cur_s);
              float trial_bounce_vx = 0.0f;
              float trial_bounce_vy = 0.0f;
              if (laser_try_shield_bounce_velocity_from_segment(
                      vx, vy, root_shx, bounce_shy, bounce_shz, shield_bounce_shield_radius,
                      bounce_sx0, bounce_sy0, 0.0f, bounce_sx, bounce_sy, 0.0f,
                      shield_bounce_radius, &trial_bounce_vx, &trial_bounce_vy) &&
                  trial_bounce_vy > shield_bounce_best_vy) {
                shield_bounce_source_allows = 1u;
                shield_bounce_source_vx = trial_bounce_vx;
                shield_bounce_source_vy = trial_bounce_vy;
                shield_bounce_best_vy = trial_bounce_vy;
              }
            }
            break;
          }
          if (!shield_hit && off_n == 0 &&
              item_swept_sphere_sphere_intersects_3d(x0, y0, 0.0f, shield_probe_x, shield_probe_y,
                                                     0.0f, sr, root_shx, shy, shz, shr)) {
            shield_hit = 1u;
            shield_hit_contact_x = shield_probe_x;
            shield_hit_contact_y = shield_probe_y;
            shield_bounce_contact_found = 1u;
            float trial_bounce_vx = 0.0f;
            float trial_bounce_vy = 0.0f;
            if (laser_try_shield_bounce_velocity_from_segment(
                    vx, vy, root_shx, bounce_shy, bounce_shz, shield_bounce_shield_radius, x0, y0,
                    0.0f, shield_probe_x, shield_probe_y, 0.0f, shield_bounce_radius,
                    &trial_bounce_vx, &trial_bounce_vy)) {
              shield_bounce_source_allows = 1u;
              shield_bounce_source_vx = trial_bounce_vx;
              shield_bounce_source_vy = trial_bounce_vy;
              shield_bounce_best_vy = trial_bounce_vy;
            }
          }
        }
        if (!shield_hit && shield_bounce_seed_valid &&
            item_guard_seed_shield_desc_active(batch, d_idx)) {
          // Hidden ShieldBounced admission:
          // Slippi does not serialize item->xC54/xC58/xDCE, but the seed lane proves
          // ftColl_80077688 already accepted ShieldDesc and Item_80269DC8 chose the
          // ShieldBounced keepalive callback. Use that hidden source owner to admit shield
          // resolution when the reduced replay-visible shield sample misses; do not broaden the
          // geometry-only carried-laser lane.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
          // refs/melee/src/melee/it/item.c::Item_80269DC8
          // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_ShieldBounced
          shield_hit = 1u;
          shield_hit_contact_x = x;
          shield_hit_contact_y = y;
          shield_bounce_contact_found = 1u;
        }
        // Slippi state_flags[0] is raw fp+0x2218. Keep this final-x14 handoff on the pure
        // reflect-descriptor state byte after the GuardReflect tick: reflect behavior (0x04)
        // with no high command/interrupt bits. Aggregate controls with 0x20/0x40/0x80 stay on
        // the live-article GuardReflect owner lane.
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
        const uint8_t guard_reflect_pure_final_x14_base =
            (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
             batch->state.action_frame[d_idx] == -1 &&
             batch->state.guard_reflect_timer_x14_seed[d_idx] > 0u &&
             batch->state.guard_reflect_timer_x14[d_idx] <= 1u &&
             item_state_flags_2218_is_reflect_behavior_only(
                 batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                          (size_t)MSL_STATE_FLAGS_2218_INDEX]))
                ? 1u
                : 0u;
        uint8_t guard_reflect_final_x14_reflectdesc_hitshield = 0u;
        if (guard_reflect_pure_final_x14_base && common != NULL &&
            common->powershield_reflect_size > 0.0f) {
          float reflect_x = 0.0f;
          float reflect_y = 0.0f;
          float reflect_z = 0.0f;
          if (item_guard_reflect_center_xyz(batch, d_idx, &reflect_x, &reflect_y, &reflect_z)) {
            const float reflect_r =
                common->powershield_reflect_size * batch->state.fighter_scale_y[d_idx];
            const float hit_r = item_laser_reflect_hit_radius(sr, laser_scale_z);
            enum { MSL_LBCOLL_HIT_RESULT_EXTENT = 20 };
            const float reflect_extent = reflect_r * (float)MSL_LBCOLL_HIT_RESULT_EXTENT;
            guard_reflect_final_x14_reflectdesc_hitshield =
                item_swept_sphere_sphere_intersects_3d(x0, y0, 0.0f, x, y, 0.0f, hit_r, reflect_x,
                                                       reflect_y, reflect_z, reflect_extent)
                    ? 1u
                    : 0u;
          }
        }
        // Seed x14==1 is still a live ReflectDesc tick on steady GuardReflect rows; it is not an
        // independent broad ShieldDesc hit. Let those rows use the actual item HitCapsule
        // `shield_hit` result above, or the final-x14 handoff only when the current ShieldDesc
        // point still overlaps the item. ReflectDesc lifetime alone is not Item_80269DC8
        // HitShield authority.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
        const uint8_t guard_reflect_seed_final_x14_hitshield =
            (guard_reflect_pure_final_x14_base &&
             batch->state.guard_reflect_timer_x14_seed[d_idx] == 1u &&
             guard_reflect_final_x14_reflectdesc_hitshield &&
             item_sphere_sphere_intersects_2d(x, y, sr, shx, shy, shr))
                ? 1u
                : 0u;
        const uint8_t guard_reflect_post_callback_final_x14_hitshield =
            (guard_reflect_pure_final_x14_base &&
             batch->state.guard_reflect_timer_x14_seed[d_idx] > 1u &&
             guard_reflect_final_x14_reflectdesc_hitshield &&
             item_sphere_sphere_intersects_2d(x, y, sr, shx, shy, shr))
                ? 1u
                : 0u;
        const uint8_t guard_reflect_pure_final_x14_hitshield =
            (guard_reflect_seed_final_x14_hitshield ||
             guard_reflect_post_callback_final_x14_hitshield)
                ? 1u
                : 0u;
        if (!shield_hit && laser_age_frames > 1.0f && guard_reflect_pure_final_x14_hitshield &&
            !guard_reflect_final_x14_same_owner_reflect_keepalive && common != NULL &&
            common->powershield_reflect_size > 0.0f) {
          float reflect_y = 0.0f;
          if (item_guard_reflect_bone_y(batch, d_idx, &reflect_y)) {
            const float reflect_r =
                common->powershield_reflect_size * batch->state.fighter_scale_y[d_idx];
            const float reflect_lane_r = sr + reflect_r;
            const float y_delta = y - reflect_y;
            if (fabsf(y_delta) <= reflect_lane_r) {
              // Final-x14 GuardReflect HitShield handoff:
              // - ftCo_GuardReflect_Anim ticks mv.co.guard.x14 before GuardOn_Anim; final visible
              //   x14 rows can hand contact to item shield resolution only when the item
              //   HitCapsule sweeps through the source ReflectDesc extent. The x14 timer alone is
              //   ReflectDesc lifetime, not Item_80269DC8 HitShield authority.
              // - Item_80269DC8 chooses the hit_shield callback when the bounce predicate
              //   (`xDCE_flag.b5/xDCE_flag.b4/xC54`) is not set; Fox laser HitShield destroys.
              // - Use the same ReflectDesc x2A8 vertical lane as the aged reflect branch to avoid
              //   broad shield HP/timer gates while the hidden xDCE/xC54/xC58 lane remains unseeded.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
              // refs/melee/src/melee/it/item.c::Item_80269DC8
              // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_HitShield
              shield_hit = 1u;
              shield_hit_contact_x = x;
              shield_hit_contact_y = y;
              if (guard_reflect_late_locomotion_shield_desc_overlap) {
                // The Landing-origin ReflectDesc miss can still fall through to a live ShieldDesc
                // owner on this same row. In that path ftColl_80077688 computes the item
                // xC54/xC58 bounce result before Item_80269DC8 chooses ShieldBounced vs HitShield,
                // so preserve the bounce predicate instead of forcing the generic final-x14
                // destroy path.
                // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
                // refs/melee/src/melee/it/item.c::Item_80269DC8
                float trial_bounce_vx = 0.0f;
                float trial_bounce_vy = 0.0f;
                if (laser_try_shield_bounce_velocity_from_segment(
                        vx, vy, bounce_shx, bounce_shy, bounce_shz, shield_bounce_shield_radius, x0,
                        y0, 0.0f, x, y, 0.0f, shield_bounce_radius, &trial_bounce_vx,
                        &trial_bounce_vy)) {
                  shield_bounce_contact_found = 1u;
                  shield_bounce_source_allows = 1u;
                  shield_bounce_source_vx = trial_bounce_vx;
                  shield_bounce_source_vy = trial_bounce_vy;
                  shield_bounce_best_vy = trial_bounce_vy;
                } else {
                  shield_bounce_contact_found = 0u;
                }
              } else {
                shield_bounce_contact_found = 0u;
              }
            }
          }
        }
        // Frozen final-x14 GuardReflect rows split on actual shield-contact ownership:
        // - if the current item sphere still overlaps the shield bubble, Item_80269DC8 can own
        //   HitShield / laser destruction on this final tick,
        // - otherwise the frozen GuardReflect callback keeps the article alive without staging a
        //   broad reflected-owner snapshot.
        // Use the same shield bubble from shields_refresh() / ftColl shield checks rather than the
        // narrower reduced-offset `shield_hit` sample above, which is only an item-hitcap sample.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80077688}
        // refs/melee/src/melee/it/item.c::Item_80269DC8
        const uint8_t guard_reflect_frozen_final_seed_shield_overlap =
            (guard_reflect_frozen_final_seed_snapshot &&
             item_sphere_sphere_intersects_2d(x, y, sr, shx, shy, shr))
                ? 1u
                : 0u;
        if (!shield_hit && batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
            batch->state.guard_reflect_timer_x14_seed[d_idx] > 1u &&
            item_should_commit_aged_powershield_reflect_owner(
                batch, ii, d_idx, def, x0, y0, x, y, vx, laser_age_frames, sr, laser_scale_z)) {
          // ReflectDesc collision runs before the ordinary shield/HitShield branch in
          // ftColl_8007925C. The reduced shield sample above can miss high scaled laser
          // HitCapsules that still overlap `fp->reflect_hit`, so admit the branch here with the
          // source-shaped ReflectDesc predicate rather than requiring ShieldDesc contact first.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
          shield_hit = 1u;
          shield_bounce_source_allows = 0u;
          shield_bounce_contact_found = 0u;
        }
        if (shield_hit && !guard_hold_xreach_ok) {
          // Steady-Guard X-reach law (see above): the bubble overlap alone does not admit the
          // contact until the swept head-X interval reaches the bone X.
          shield_hit = 0u;
          shield_bounce_source_allows = 0u;
          shield_bounce_contact_found = 0u;
        }
        if (shield_hit) {
          // Powershield reflect: on a reflected hit, reverse the velocity vector and transfer owner.
          //
          // Decomp:
          // - Laser reflect visual logic: it_2725_Logic94_Reflected flips angle (+= pi) and sets facing_dir.
          //   refs/melee/src/melee/it/items/itfoxlaser.c::it_2725_Logic94_Reflected
          // - GuardReflect sets fp->reflecting and powershield flags:
          //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C (ftColl_CreateReflectHit)
          //   refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_CreateReflectHit (sets 0x2218 reflect bit => 0x10)
          //   Note: fp+0x2218:3 in refs/melee/src/melee/ft/types.h uses the game's bit numbering (MSB-first),
          //   so this reflects as 0x10 (not 0x08).
          // - Slippi `state_flags` packs these fighter bytes:
          //   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (lbz r3,0x2218 / 0x221C)
          //
          // Note: item attack_id / attack_instance remain spawn-latched for lasers in v1 (do not
          // transfer on reflect here).
          // Seeded collision-time GuardReflect/powershield ownership is represented through the
          // replay-visible state/timer lanes available to supported laser contacts. Gate reflect
          // off the decomp-shaped GuardReflect action +
          // timer lanes owned by ftCo_80093A50/ftCo_80093BC0:
          // - x14: reflect window (`fp->reflecting` ownership window),
          // - x18: powershield-active window (x221C_b2 lifetime).
          // This keeps powershield reflect ownership coupled to the same timer gates used by
          // GuardReflect callback timing under teacher-forced reseed.
          //
          // Decomp anchors:
          // - GuardReflect reflect window timer: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c
          //   (mv.co.guard.x14 = p_ftCommonData->x2A4; tick in ftCo_80093BC0).
          // - Powershield-active lane: fp->x221C_b2 at collision-time:
          //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
          //
          // Keep item reflect gating aligned with combat collision-time powershield semantics by
          // sharing combat_is_powershield_active_idx().
          uint8_t can_powershield_reflect =
              combat_is_powershield_active_idx(batch, d_idx) ? 1u : 0u;
          uint8_t powershield_reflect_hitshield_handoff = 0u;
          const uint8_t fresh_guardon_spawn_frame_non_reflect_source =
              (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
               batch->state.action_frame[d_idx] < 0 &&
               batch->state.animation_index[d_idx] == UINT32_MAX && laser_age_frames <= 1.0f &&
               item_prev_action_is_guardon_spawn_frame_non_reflect_source(seed_prev_action))
                  ? 1u
                  : 0u;
          const uint8_t fresh_guardreflect_spawn_frame_non_reflect_source =
              (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
               batch->state.action_frame[d_idx] < 0 &&
               batch->state.animation_index[d_idx] == UINT32_MAX &&
               batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
               batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u && laser_age_frames <= 1.0f &&
               seed_prev_action != (uint16_t)MSL_ACT_GUARD_ON &&
               seed_prev_action != (uint16_t)MSL_ACT_GUARD &&
               seed_prev_action != (uint16_t)MSL_ACT_GUARD_REFLECT &&
               seed_prev_action != (uint16_t)MSL_ACT_GUARD_SET_OFF &&
               item_prev_action_is_guardon_spawn_frame_non_reflect_source(seed_prev_action))
                  ? 1u
                  : 0u;
          if (fresh_guardon_spawn_frame_non_reflect_source ||
              fresh_guardreflect_spawn_frame_non_reflect_source) {
            // The fighter can still enter GuardReflect later in this step, but item collision has not
            // reached the Run-family reflect-transfer or shield-hit owner. Keep the spawn-frame laser
            // alive and shooter-owned instead of applying a broad powershield/ShieldDesc contact from
            // the replay-visible GuardOn/GuardReflect state. This is a per-defender miss, not an
            // all-defender abort; future 4-player collision should keep scanning other defenders.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
            //   ftCo_80091A4C,ftCo_800939B4}
            // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,it_8029C4D4}
            continue;
          }
          if (batch->state.item_reflect_transfer_seed_port[ii] ==
              (uint8_t)MSL_ITEM_REFLECT_KNOWN_NONE_PORT) {
            // Replay seed knows this post-frame item has no pending ftColl_80077464 ->
            // Item_80269F14 owner transfer. Keep the collision on the shield/HitShield owner
            // rather than re-inventing a broad visible reflect overlap from Slippi state.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
            // refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}
            can_powershield_reflect = 0u;
          }
          // No-submotion GuardReflect stale-x18 lane:
          // - Reflect ownership is callback-gated by the active reflect window (mv.co.guard.x14).
          // - Some frozen snapshots carry x18 from the previous frame while x14 was already 0 in
          //   the seed snapshot; those rows should resolve through normal shield-hit ownership
          //   (GuardSetOff/hitlag), not item reflect.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009370C,ftCo_80093BC0}
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
          if (can_powershield_reflect && defender_guard_reflect_no_submotion_snapshot &&
              batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
              batch->state.guard_reflect_timer_x18_seed[d_idx] != 0u) {
            can_powershield_reflect = 0u;
          }
          // One-frame-late locomotion->GuardReflect frozen snapshot:
          // - AttackDash / Wait-style shield admission can enter GuardReflect through ftCo_80091A4C ->
          //   ftCo_800939B4, and the following frozen action_frame==-1 snapshot can already be on the
          //   projectile shield-hit / GuardSetOff owner lane only when the item overlaps the
          //   source/data-backed ReflectDesc sphere. Active-window rows outside that lane stay
          //   owned by ftColl_80077464/GuardReflect until the final x14 handoff.
          // - Keep this restricted to frozen GuardReflect rows whose previous action was not already
          //   shield-owned; steady GuardReflect rows still use the timer-gated reflect path.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_80091A4C,ftCo_800939B4,ftCo_8009370C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
          const uint8_t defender_guard_reflect_late_locomotion_snapshot =
              (can_powershield_reflect && guard_reflect_late_locomotion_hitshield_handoff) ? 1u
                                                                                           : 0u;
          if (defender_guard_reflect_late_locomotion_snapshot) {
            can_powershield_reflect = 0u;
            powershield_reflect_hitshield_handoff = 1u;
          }
          if (guard_reflect_late_locomotion_hitshield_handoff && !can_powershield_reflect) {
            powershield_reflect_hitshield_handoff = 1u;
          }
          if (shield_bounce_seed_valid && shield_hit) {
            // Hidden ShieldBounced source owner:
            // item_shield_bounce_seed_* is the only serialized proof we have that
            // ftColl_80077688/Item_80269DC8 already chose the shield-bounce callback for this
            // live item/shield contact. Do not let the broader active GuardReflect keepalive below
            // suppress the same source-owned HitShield/ShieldBounced handoff.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
            // refs/melee/src/melee/it/item.c::Item_80269DC8
            // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_ShieldBounced
            powershield_reflect_hitshield_handoff = 1u;
          }
          // Fresh locomotion->GuardReflect snapshot bridge:
          // - Grounded guard admission can enter GuardReflect directly from locomotion
          //   (`ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50`) before the first canonical
          //   GuardReflect callback tick seeds x14/x18 into replay-visible lanes.
          // - On these same-frame no-submotion snapshots, replay still resolves projectile shield
          //   contact through the normal shield-hit/GuardSetOff path rather than powershield
          //   reflect ownership.
          // - Keep this bridge restricted to fresh GuardReflect entries whose seed timers are both
          //   zero, whose previous action was not already shield-owned, and whose shield is already
          //   below the untouched x260 start-health lane. Full-shield Dash rows are handled by the
          //   shield-precedence bridge above.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50,ftCo_8009370C}
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80076CBC}
          const MslCommonParams* c = msl_common_params();
          const uint8_t defender_guard_reflect_same_frame_locomotion_entry =
              (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
               batch->state.action_frame[d_idx] < 0 &&
               batch->state.animation_index[d_idx] == UINT32_MAX &&
               batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
               batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u &&
               batch->state.guard_reflect_timer_x14[d_idx] != 0u &&
               batch->state.guard_reflect_timer_x18[d_idx] != 0u &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_SET_OFF)
                  ? 1u
                  : 0u;
          const uint8_t defender_guard_reflect_fresh_locomotion_snapshot =
              (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
               batch->state.action_frame[d_idx] < 0 &&
               batch->state.animation_index[d_idx] == UINT32_MAX &&
               batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
               batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT &&
               batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_SET_OFF && c != NULL &&
               batch->state.shield_hp[d_idx] < c->start_shield_health)
                  ? 1u
                  : 0u;
          (void)defender_guard_reflect_fresh_locomotion_snapshot;
          const uint8_t same_frame_wait_guardreflect_hitshield_handoff =
              (defender_guard_reflect_same_frame_locomotion_entry &&
               frame_start_prev_action == (uint16_t)MSL_ACT_WAIT && laser_age_frames > 1.0f &&
               c != NULL && batch->state.shield_hp[d_idx] < c->start_shield_health)
                  ? 1u
                  : 0u;
          const uint8_t same_frame_wait_guardreflect_reflect_owner_commit =
              (defender_guard_reflect_same_frame_locomotion_entry &&
               frame_start_prev_action == (uint16_t)MSL_ACT_WAIT && laser_age_frames > 1.0f &&
               c != NULL && batch->state.shield_hp[d_idx] >= c->start_shield_health)
                  ? 1u
                  : 0u;
          const uint8_t same_frame_dash_nonterminal_hitshield_handoff =
              (defender_guard_reflect_same_frame_locomotion_entry &&
               seed_prev_action == (uint16_t)MSL_ACT_DASH &&
               batch->state.guard_reflect_entry_dash_terminal_scalar[d_idx] == 0u &&
               laser_age_frames > 1.0f)
                  ? 1u
                  : 0u;
          const uint8_t same_frame_locomotion_reflect_owner_commit =
              (defender_guard_reflect_same_frame_locomotion_entry &&
               (batch->state.guard_reflect_entry_dash_terminal_scalar[d_idx] != 0u ||
                same_frame_wait_guardreflect_reflect_owner_commit) &&
               !same_frame_wait_guardreflect_hitshield_handoff)
                  ? 1u
                  : 0u;
          if (can_powershield_reflect && same_frame_wait_guardreflect_hitshield_handoff) {
            // Wait -> GuardReflect can install ShieldDesc and ReflectDesc in the same fighter
            // callback. When the row already has post-contact shield damage, the aged laser shield
            // overlap is on Item_80269DC8 HitShield / GuardSetOff ownership. Full-shield
            // same-frame rows stay on the ReflectDesc owner transfer below.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Wait.c::ftCo_Wait_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
            // refs/melee/src/melee/it/item.c::Item_80269DC8
            can_powershield_reflect = 0u;
            powershield_reflect_hitshield_handoff = 1u;
          }
          if (can_powershield_reflect && laser_age_frames > 1.0f &&
              batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
              batch->state.guard_reflect_timer_x14_seed[d_idx] > 1u &&
              !defender_guard_reflect_same_frame_locomotion_entry &&
              !item_guard_seed_shield_desc_active(batch, d_idx)) {
            // Aged GuardReflect shield-descriptor boundary:
            // - ftColl only checks shield collision while the live ShieldDesc bit is present
            //   (`fp+0x221B_b0`, Slippi `state_flags[2] & 0x80`).
            // - Rows that only carry GuardReflect timer bits without that descriptor must not
            //   stage a generic reflect snapshot; they remain on no-contact / item-owned shield
            //   callback state until a real descriptor is present.
            // - Fresh locomotion->GuardReflect entries are handled separately above because
            //   ftCo_80093A50 can install the descriptor inside the current step before Slippi
            //   exposes the post-frame x221B lane.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80077464}
            // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
            can_powershield_reflect = 0u;
          }
          if (can_powershield_reflect && same_frame_dash_nonterminal_hitshield_handoff &&
              !same_frame_locomotion_reflect_owner_commit) {
            // Same-frame GuardReflect entries install ReflectDesc before collision, but the item
            // branch can still be owned by Item_80269DC8 when a fresh Dash -> GuardReflect row has
            // shield overlap but did not come from Dash_IASA's terminal-scalar owner. Keep immediate
            // reflected owner/xDA8 transfer scoped to that source boundary.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464,ftColl_80077688}
            // refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}
            can_powershield_reflect = 0u;
            powershield_reflect_hitshield_handoff = 1u;
          }
          uint8_t powershield_reflect_desc_overlap = 0u;
          if (can_powershield_reflect && laser_age_frames > 1.0f &&
              batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT && common != NULL &&
              common->powershield_reflect_size > 0.0f &&
              batch->state.guard_reflect_timer_x14_seed[d_idx] > 0u) {
            float reflect_y = 0.0f;
            float rfl_cx = 0.0f;
            float rfl_cy = 0.0f;
            float rfl_cz = 0.0f;
            const uint8_t rfl_center_ok =
                item_guard_reflect_center_xyz(batch, d_idx, &rfl_cx, &rfl_cy, &rfl_cz);
            if (item_guard_reflect_bone_y(batch, d_idx, &reflect_y)) {
              const float reflect_r =
                  common->powershield_reflect_size * batch->state.fighter_scale_y[d_idx];
              const float reflect_lane_r = sr + reflect_r;
              const float y_delta = y - reflect_y;
              // Full ReflectDesc sphere overlap (entry-pose scaled), not a vertical band: the
              // lane test alone keeps reflect ownership alive for lasers that are vertically
              // adjacent but horizontally far outside the descriptor (MAJ rec6242: dx ~5.1,
              // dy ~1.7; vanilla's probe misses and the row hands off to the ShieldDesc hit).
              const float entry_rfl_r = item_guard_reflect_entry_pose_radius(batch, common, d_idx);
              const uint8_t reflect_vertical_overlap = (fabsf(y_delta) <= reflect_lane_r) ? 1u : 0u;
              const uint8_t reflect_full_overlap =
                  (rfl_center_ok && item_sphere_sphere_intersects_3d(x0, y0, 0.0f, 0.0f, rfl_cx,
                                                                     rfl_cy, rfl_cz, entry_rfl_r))
                      ? 1u
                      : 0u;
              powershield_reflect_desc_overlap =
                  (reflect_vertical_overlap && reflect_full_overlap) ? 1u : 0u;
              if (y_delta < -reflect_lane_r ||
                  (guard_reflect_pure_final_x14_hitshield && reflect_vertical_overlap &&
                   !guard_reflect_final_x14_same_owner_reflect_keepalive)) {
                // GuardReflect HitShield discriminator:
                // - lasers below the shield-bone ReflectDesc lane hit shield instead of reflect,
                // - the final x14 tick inside that lane can hand off to Item_80269DC8 HitShield
                //   destruction. For the post-callback seed>1 extension, the predicate above also
                //   requires live ShieldDesc bubble overlap.
                // This keeps shield HP/timer heuristics out of the branch and uses the same
                // source-backed ReflectDesc x2A8 + item HitCapsule radius lane as the aged commit
                // above.
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009370C,ftCo_80093BC0}
                // refs/melee/src/melee/it/item.c::Item_80269DC8
                // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_HitShield
                can_powershield_reflect = 0u;
                powershield_reflect_hitshield_handoff = 1u;
              }
            }
          }
          if (can_powershield_reflect && guard_reflect_frozen_final_seed_shield_overlap &&
              !guard_reflect_final_x14_same_owner_reflect_keepalive) {
            // Final frozen x14 shield-contact handoff:
            // - at the last visible GuardReflect tick, current shield-bubble contact is owned by
            //   Item_80269DC8 HitShield rather than ftColl_80077464 reflect transfer,
            // - non-overlap frozen rows remain keepalive-only and do not stage a reflected owner.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
            // refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}
            can_powershield_reflect = 0u;
            powershield_reflect_hitshield_handoff = 1u;
          }
          const uint8_t spawn_frame_powershield_reflect_owner =
              (can_powershield_reflect && laser_age_frames <= 1.0f &&
               batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
               item_prev_action_is_guardon_spawn_frame_reflect_source(seed_prev_action) &&
               (batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                         (size_t)MSL_STATE_FLAGS_2218_INDEX] &
                (uint8_t)MSL_STATE_FLAG_2218_B1) == 0u)
                  ? 1u
                  : 0u;
          const uint8_t guardon_followup_reflect_owner_commit =
              (can_powershield_reflect &&
               item_should_commit_guardon_followup_powershield_reflect_owner(
                   batch, ii, d_idx, def, owner, x0, y0, x, y, vx, laser_age_frames, sr,
                   laser_scale_z))
                  ? 1u
                  : 0u;
          const uint8_t guardon_followup_reflect_miss_keepalive =
              (can_powershield_reflect && !guardon_followup_reflect_owner_commit &&
               batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
               batch->state.animation_index[d_idx] == UINT32_MAX &&
               batch->state.action_frame[d_idx] < 0 &&
               batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
               batch->state.guard_reflect_timer_x18_seed[d_idx] == 0u &&
               (batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON ||
                batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON) &&
               laser_age_frames > 1.0f)
                  ? 1u
                  : 0u;
          const uint8_t aged_reflect_owner_commit =
              (can_powershield_reflect && !same_frame_wait_guardreflect_hitshield_handoff &&
               !same_frame_dash_nonterminal_hitshield_handoff &&
               item_should_commit_aged_powershield_reflect_owner(
                   batch, ii, d_idx, def, x0, y0, x, y, vx, laser_age_frames, sr, laser_scale_z))
                  ? 1u
                  : 0u;
          // Locomotion-origin live GuardReflect follow-up reflect owner (exact ReflectDesc
          // narrowphase): ftColl_8007925C checks `fp->reflecting` before the shield branch on
          // every frame the x14 window is live, and lbColl_80006E58 measures the descriptor size
          // in the scaled shield-bone space - ReflectDesc x14_size (p_ftCommonData->x2A8 = 0.75)
          // rides the same JObj scale that makes ShieldDesc size 1 the full shield radius, so the
          // effective reflect sphere is 0.75 * the live shield radius, not 0.75 world units.
          // Witness: MAJ rec1995 (dash-press GuardReflect at 1994, laser arrives the next frame
          // on the approach side at ~2.1u; vanilla writes the ftColl_80077464 snapshot during
          // step 1995 - owner flips by the 1996 post-frame, velocity on the following item pass -
          // while the origin-sliced owners above all decline and the row fell through to a wrong
          // GuardSetOff). GuardOn-origin rows keep their existing owners.
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464}
          // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80091D58}
          uint8_t locomotion_followup_reflect_owner_commit = 0u;
          // Not gated on `can_powershield_reflect`: that lane's frozen-snapshot arm tracks x18
          // (the x221C_b2 powershield-damage window), while ftColl_8007925C's reflect branch is
          // owned by `fp->reflecting`, which ftCo_80093BC0 clears only when x14 expires. The
          // second locomotion-entry frame can have x18 already 0 with x14 still live (MAJ
          // rec1995: x14=1 at the item pass).
          if (!aged_reflect_owner_commit && !guardon_followup_reflect_owner_commit &&
              !same_frame_locomotion_reflect_owner_commit &&
              !spawn_frame_powershield_reflect_owner &&
              !same_frame_wait_guardreflect_hitshield_handoff &&
              !same_frame_dash_nonterminal_hitshield_handoff &&
              batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
              batch->state.guard_reflect_origin_guardon[d_idx] == 0u &&
              batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
              batch->state.guard_reflect_timer_x14[d_idx] > 0u &&
              (((x - batch->state.pos_x[d_idx]) * vx) < 0.0f) && laser_age_frames > 1.0f) {
            float rfl_x = 0.0f;
            float rfl_y = 0.0f;
            float rfl_z = 0.0f;
            if (item_guard_reflect_center_xyz(batch, d_idx, &rfl_x, &rfl_y, &rfl_z)) {
              // Fighter-pass phase: ftColl_8007925C probes `fp->reflect_hit` from the fighter's
              // collision callback, BEFORE the item GObj proc moves the projectile this frame -
              // so the probe sees the pre-move HitCapsule position (x0,y0), and the descriptor
              // radius rides the fighter-lane (steady) shield bone scale, not the item-pass
              // entry-adjusted lane. Witness pair: MAJ rec1995 pre-move dist ~7.06 commits
              // (0.75 x 7.87 + laser), MAJ rec259 pre-move dist ~10.69 falls through to the item
              // pass's ShieldDesc hit.
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077464}
              // refs/melee/src/melee/lb/lbcollision.c::lbColl_80006E58
              const float rfl_r = item_guard_reflect_entry_pose_radius(batch, common, d_idx);
              // Point probe on the item side: lbColl_80007BCC passes hit->scale * item->scl as
              // the HitCapsule radius, and the laser article's object scale leaves it ~0 - the
              // beam reach comes from the segment endpoints, not the radius. Witness margins
              // collapse below 0.1 if the shield-lane radius policy (size x stretch, ~1.17) is
              // added here (MAJ rec6242 no-commit at 5.22 vs rec1995 commit at 3.18, descriptor
              // radius ~4.1).
              if (item_sphere_sphere_intersects_3d(x0, y0, 0.0f, 0.0f, rfl_x, rfl_y, rfl_z,
                                                   rfl_r)) {
                locomotion_followup_reflect_owner_commit = 1u;
                can_powershield_reflect = 1u;
              }
            }
          }
          const uint8_t powershield_non_reflect_keepalive =
              (can_powershield_reflect &&
               batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_SET_OFF &&
               batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] == 0u)
                  ? 1u
                  : 0u;
          if (can_powershield_reflect && !same_frame_locomotion_reflect_owner_commit &&
              !spawn_frame_powershield_reflect_owner && !guardon_followup_reflect_owner_commit &&
              !aged_reflect_owner_commit && !locomotion_followup_reflect_owner_commit &&
              !powershield_reflect_desc_overlap) {
            // Reflect snapshot provenance:
            // - ftColl_80077464 writes the pending reflect snapshot only when the item overlaps
            //   ReflectDesc (`fp->reflecting` + `reflect_hit` bone/size/offset).
            // - Active GuardReflect timer bits alone are not a transfer. If the ReflectDesc
            //   geometry misses, the active-window no-submotion lane below keeps the item alive
            //   without staging a next-frame reflected owner; final-x14 rows can then fall through
            //   to Item_80269DC8 HitShield destruction.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
            // refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}
            if (powershield_non_reflect_keepalive) {
              // GuardSetOff can carry the collision-time powershield bit (`fp+0x221C_b2`) after
              // GuardReflect ownership without a live ReflectDesc transfer. That bit suppresses
              // regular shield damage in ftColl_80076CBC, so keep the item callback alive without
              // inventing a reflected owner snapshot.
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80077464}
              break;
            }
            can_powershield_reflect = 0u;
            if (shield_hit && laser_age_frames > 1.0f &&
                batch->state.guard_reflect_timer_x14_seed[d_idx] > 1u &&
                batch->state.guard_reflect_timer_x14[d_idx] <= 1u &&
                !guard_reflect_final_x14_same_owner_reflect_keepalive &&
                item_sphere_sphere_intersects_2d(x, y, sr, shield_bubble_shx, shield_bubble_shy,
                                                 shr)) {
              // Live final-x14 HitShield authority: when the (tilt-displaced) ReflectDesc misses
              // but ShieldDesc contact stands on the row where x14 has ticked to its final
              // visible value, Item_80269DC8 resolves HitShield/ShieldBounced the same frame -
              // the active-window keepalive only covers rows without shield contact. This is the
              // live-rollout equivalent of the serialized item_shield_bounce_seed_* proof above
              // (MAJ:7384 rollout - identical lanes to the one-step row, which resolves through
              // the seed-proof arm; vanilla GuardSetOffs and bounce-deflects at this exact step).
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
              // refs/melee/src/melee/it/item.c::Item_80269DC8
              // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_ShieldBounced
              powershield_reflect_hitshield_handoff = 1u;
            }
          }
          if (can_powershield_reflect) {
            // Decomp reflect snapshot ownership:
            // - GuardReflect reflect setup path initializes ReflectDesc lanes.
            //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_8009370C
            //   refs/melee/src/melee/ft/ftcoll.c::ftColl_CreateReflectHit
            // - overlap writes owner/xDA8_short and reflect multipliers to item snapshot.
            //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
            // - item logic consumes snapshot in Item_80269F14; speed update may be deferred to that
            //   pass via msl_item_reflect_apply_pending_laser_callback().
            //   refs/melee/src/melee/it/item.c::Item_80269F14
            const float dmg_mul = (c != NULL && c->powershield_reflect_damage_mul > 0.0f)
                                      ? c->powershield_reflect_damage_mul
                                      : 1.0f;
            const uint8_t aged_reflect_preserve_direction =
                (aged_reflect_owner_commit &&
                 !msl_item_reflect_should_flip_direction_now(batch, ii, d_idx))
                    ? 1u
                    : 0u;
            const float aged_reflect_old_direction = batch->state.item_direction[ii];
            if (same_frame_locomotion_reflect_owner_commit) {
              // Same-frame locomotion -> GuardReflect owner/xDA8 transfer:
              // - ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50 installs ReflectDesc before
              //   item collision. Keep immediate same-frame transfer to Dash IASA terminal-scalar
              //   rows; other fresh locomotion shield-overlap entries fall through to
              //   Item_80269DC8 HitShield / GuardSetOff.
              // - Item_80269F14's speed/orientation consumption is item-callback owned and appears
              //   on the next item pass; current post-frame keeps the pre-reflect velocity lane while
              //   owner/xDA8 already moved to the reflector. Keep that split explicit instead of
              //   routing the row through shield-hit/GuardSetOff.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Dash.c::ftCo_Dash_IASA
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091A4C,ftCo_800939B4,ftCo_80093A50}
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
              // refs/melee/src/melee/it/item.c::Item_80269F14
              msl_item_reflect_set_damage_mul(batch, ii, dmg_mul);
              msl_item_reflect_commit_owner_snapshot_defer_speed(batch, ii, def);
            } else {
              msl_item_reflect_stage_snapshot_defer_velocity(batch, ii, def, dmg_mul);
              if (aged_reflect_preserve_direction) {
                batch->state.item_direction[ii] = aged_reflect_old_direction;
              }
            }
            if (spawn_frame_powershield_reflect_owner) {
              // Spawn-frame powershield reflect ownership:
              // - A newly spawned SpecialN laser can overlap GuardReflect in the same item logic
              //   pass from a Run/Dash-family guard entry while raw fp+0x2218_b1 is clear. In that
              //   path, ftColl_80077464 writes the reflect snapshot and Item_80269F14 consumes
              //   owner/xDA8 before the post-frame item record.
              // - Rows with fp+0x2218_b1 set stay shooter-owned on the birth frame until a later item
              //   callback proves reflect snapshot ownership.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::{ftCo_Run_IASA,ftCo_RunDirect_IASA}
              // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077464
              // refs/melee/src/melee/it/item.c::Item_80269F14
              // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,it_8029C4D4}
              msl_item_reflect_commit_owner_snapshot(batch, ii, def);
            } else if (guardon_followup_reflect_owner_commit) {
              // GuardOn follow-up powershield reflect owner/xDA8 commit:
              // - ftCo_8009388C can enter GuardReflect from GuardOn and install ReflectDesc in the
              //   same step, before seed-visible x14/x18 have a post-frame to expose the lane.
              // - Scope to fresh frozen GuardReflect rows with GuardOn as the visible previous
              //   owner and ReflectDesc vertical overlap; do not reopen broad same-frame transfer.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_IASA,ftCo_8009388C,ftCo_8009370C}
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
              // refs/melee/src/melee/it/item.c::Item_80269F14
              msl_item_reflect_commit_owner_snapshot(batch, ii, def);
            } else if (locomotion_followup_reflect_owner_commit) {
              // Same defer split as the aged owner: owner/xDA8 move this step, Item_80269F14
              // reverses the velocity on the next item pass.
              msl_item_reflect_commit_owner_snapshot_defer_speed(batch, ii, def);
            } else if (aged_reflect_owner_commit) {
              // Aged powershield reflect owner/xDA8 commit:
              // - ftColl_80077464 writes owner/xDA8 to the item reflect snapshot when the laser
              //   overlaps the GuardReflect ReflectDesc.
              // - Item_80269F14 consumes that snapshot before Slippi's item post-frame record.
              // - Keep this narrower than the rejected broad same-frame transfer by requiring the
              //   laser to still approach the reflector, overlap the shield-bone ReflectDesc
              //   vertical lane, and avoid the final x14 handoff tick owned by HitShield destroy.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009370C,ftCo_80093BC0}
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_CreateReflectHit,ftColl_80077464}
              // refs/melee/src/melee/it/item.c::{Item_80269F14,Item_80269DC8}
              //
              // Keep the pending reflected-callback lanes staged by the
              // stage_snapshot_defer_velocity above: Item_80269F14 consumes them on the next item
              // pass to reverse the velocity (MAJ rec294 live powershield: vanilla's returned
              // laser flips during step 295; the owner-only commit wiped the staged lanes and the
              // laser sailed on at +5 forever).
              msl_item_reflect_commit_owner_snapshot_defer_speed(batch, ii, def);
            }
            // TODO(decomp/powershield-reflect-ownership-timing): transfer-frame xDA8_short
            // (Slippi item.instance_id) is intentionally left seed-latched in this lane until the
            // exact authoritative transfer ordering is extracted for non-regressive rollout parity.
            // TODO(decomp/powershield-reflect-speed-mul): validate whether powershield reflect uses
            // ftCommonData x2B0 directly (or identity) for these rows.
            break;
          }
          if (guard_reflect_late_locomotion_snapshot_base &&
              batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_LANDING &&
              item_state_flags_2218_is_reflect_behavior_only(
                  batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                           (size_t)MSL_STATE_FLAGS_2218_INDEX]) &&
              shield_hit) {
            // ReflectDesc miss -> ShieldDesc fallthrough:
            // ftColl_8007925C checks ReflectDesc first, then continues to the ordinary ShieldDesc
            // branch when the live shield descriptor accepts the same item HitCapsule. A
            // Landing/locomotion-origin GuardReflect active-x14 row must therefore not keep the
            // article reflect-owned merely because the broad ReflectDesc phase ran.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
            // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
            powershield_reflect_hitshield_handoff = 1u;
          }
          if ((guard_reflect_active_window_no_submotion_snapshot ||
               guardon_followup_reflect_miss_keepalive ||
               (guard_reflect_frozen_final_seed_keepalive_snapshot &&
                !guard_reflect_frozen_final_seed_shield_overlap && !shield_bounce_seed_valid)) &&
              !powershield_reflect_hitshield_handoff) {
            // The active/frozen reflect window owns this GuardReflect row, but the reflect geometry
            // above may reject the projectile. In that case do not fall through into regular
            // GuardSetOff shield-hit ownership until the x14 handoff has fully expired. Fresh
            // GuardOn->GuardReflect followups use the same owner: ftCo_8009388C installs ReflectDesc
            // in the current step, but a ReflectDesc miss remains powershield-window keepalive, not
            // a generic shield-hit transfer. Active x14 timer rows have already paid the
            // GuardReflect_Anim shield drain; fresh GuardOn->GuardReflect recharge is owned by the
            // action-level shield descriptor gate, while final frozen handoff rows need the replay-
            // visible recharge here. Frozen final-x14 rows with current shield-bubble overlap or
            // explicit ShieldBounced seed provenance can hand off to Item_80269DC8 below.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077464,ftColl_80077688}
            if (guard_reflect_frozen_final_seed_keepalive_snapshot) {
              item_guard_reflect_apply_recharge(batch, d_idx);
            }
            break;
          }
          float shield_bounce_vx = 0.0f;
          float shield_bounce_vy = 0.0f;
          // Spawn-frame keepalive gate:
          // - Laser spawn initializes `scale=0` in it_8029C504 and the same-frame motion callback
          //   performs the first scale ramp in itFoxlaser_UnkMotion1_Anim before collision.
          // - Source-owned shield-bounce keepalive rows require either explicit hidden
          //   item_shield_bounce_seed_* provenance or a live ftColl_80077688 / Item_80269DC8
          //   segment normal accepted below. Same-frame gun-spawn shield contacts without that
          //   source evidence resolve through the HitShield destroy path.
          // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C504,itFoxlaser_UnkMotion1_Anim}
          // refs/melee/src/melee/it/item.c::Item_80269DC8
          const uint8_t guard_reflect_seeded_snapshot =
              (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT ||
               batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT ||
               batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT)
                  ? 1u
                  : 0u;
          const uint8_t guard_reflect_stale_x18_hitshield_snapshot =
              (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
               batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u &&
               batch->state.guard_reflect_timer_x18_seed[d_idx] != 0u)
                  ? 1u
                  : 0u;
          const uint8_t shield_bounce_hp_allows =
              (guard_reflect_seeded_snapshot || common == NULL ||
               batch->state.shield_hp[d_idx] >= common->start_shield_health - (lp->damage + 0.5f))
                  ? 1u
                  : 0u;
          const uint8_t guard_reflect_landing_shielddesc_bounce_handoff =
              (guard_reflect_late_locomotion_snapshot_base &&
               batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_LANDING &&
               item_state_flags_2218_is_reflect_behavior_only(
                   batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                            (size_t)MSL_STATE_FLAGS_2218_INDEX]) &&
               shield_hit)
                  ? 1u
                  : 0u;
          if (guard_reflect_landing_shielddesc_bounce_handoff &&
              (!shield_bounce_contact_found || !shield_bounce_source_allows)) {
            // Some Landing-origin GuardReflect fallthrough rows are admitted by the live
            // ShieldDesc predicate after the reduced laser HitCapsule sample has missed. Rebuild
            // the ftColl_80077688/Item_80269DC8 bounce normal from the same item segment before
            // choosing ShieldBounced vs HitShield.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
            // refs/melee/src/melee/it/item.c::Item_80269DC8
            float trial_bounce_vx = 0.0f;
            float trial_bounce_vy = 0.0f;
            if (laser_try_shield_bounce_velocity_from_segment(
                    vx, vy, bounce_shx, bounce_shy, bounce_shz, shield_bounce_shield_radius, x0, y0,
                    0.0f, x, y, 0.0f, shield_bounce_radius, &trial_bounce_vx, &trial_bounce_vy)) {
              shield_bounce_contact_found = 1u;
              shield_bounce_source_allows = 1u;
              shield_bounce_source_vx = trial_bounce_vx;
              shield_bounce_source_vy = trial_bounce_vy;
              shield_bounce_best_vy = trial_bounce_vy;
            }
          }
          uint8_t can_shield_bounce = 0u;
          if (shield_bounce_contact_found && shield_bounce_seed_valid) {
            // Seeded hidden xC58/xDCE shield-bounce result:
            // Slippi exposes the surviving bounced laser velocity but not the intra-frame
            // ftColl_80077688 fields consumed by Item_80269DC8. Use the explicit seed lane as the
            // authoritative general ShieldBounced predicate on current shield-contact rows,
            // including final-x14 GuardReflect handoff ticks where keepalive otherwise wins.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
            // refs/melee/src/melee/it/item.c::Item_80269DC8
            // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_ShieldBounced
            shield_bounce_vx = batch->state.item_shield_bounce_seed_vel_x[ii];
            shield_bounce_vy = batch->state.item_shield_bounce_seed_vel_y[ii];
            can_shield_bounce = 1u;
          } else if (shield_bounce_contact_found && shield_bounce_source_allows &&
                     !shield_dash_91ad8_guardon_same_step &&
                     !guard_reflect_stale_x18_hitshield_snapshot &&
                     (!guard_reflect_pure_final_x14_hitshield ||
                      guard_reflect_landing_shielddesc_bounce_handoff) &&
                     // Late locomotion->GuardReflect frozen snapshots can already resolve
                     // projectile contact through the regular shield-hit / GuardSetOff owner lane
                     // while the shot is only one frame old. Allow the normal shield-bounce
                     // keepalive there instead of forcing the generic spawn-frame destroy path.
                     // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
                     //   ftCo_80091A4C,ftCo_800939B4,ftCo_8009370C,ftCo_GuardReflect_Anim}
                     // refs/melee/src/melee/it/items/itfoxlaser.c::{
                     //   it_8029C504,itFoxlaser_UnkMotion1_Anim,itFoxLaser_Logic94_ShieldBounced}
                     // Shield-bounce keepalive is owned by Item_80269DC8's bounce predicate:
                     // `xDCE_flag.b5` admits the hitbox, then `xC54` is compared against
                     // `(90 + it_804D6D28->unk_degrees)` unless xDCE.b4 already forced the bounce.
                     // The runtime source predicate above reconstructs xC54/xC58 from the first
                     // lbColl_80007DD8 item/shield capsule contact instead of keeping every shield
                     // hit alive.
                     // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
                     // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007DD8
                     // refs/melee/src/melee/it/item.c::Item_80269DC8
                     // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_ShieldBounced
                     // Landing-origin GuardReflect rows whose exact ReflectDesc missed but whose
                     // live ShieldDesc accepted the same laser use this same ShieldBounced owner.
                     // Do not let the replay-visible final-x14 snapshot force a destroy path after
                     // ftColl_8007925C has fallen through to ftColl_80077688.
                     // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077688}
                     (laser_age_frames > 1.0f || defender_guard_reflect_late_locomotion_snapshot) &&
                     shield_bounce_hp_allows) {
            shield_bounce_vx = shield_bounce_source_vx;
            shield_bounce_vy = shield_bounce_source_vy;
            can_shield_bounce = 1u;
          }
          // Regular shield hit: apply defender-side shield effects and despawn the laser.
          if (item_guardreflect_active_timer_shield_contact_needs_drain(batch, d_idx)) {
            item_guardreflect_apply_contact_drain(batch, d_idx);
          }
          float dmg = (laser_state == 0u) ? lp->damage : lp->state1_damage;
          dmg = msl_item_reflect_damage_lane(batch, ii, dmg);
          const int8_t shd = (laser_state == 0u) ? lp->shield_damage : lp->state1_shield_damage;
          const uint8_t element = (laser_state == 0u) ? lp->element : lp->state1_element;
          combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                       batch->state.item_attack_instance[ii], dmg, shd, element,
                                       batch->state.item_pos_x[ii]);
          // Rehit suppression latch for this item: insert the post-mutation victim identity so
          // teacher-forced reseed sees the same item HitCapsule victim ring at t+1.
          const uint16_t def_iid_post = batch->state.instance_id[d_idx];
          hitlist_register_item_fighter(batch, bi, it, def, def_iid_post,
                                        (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
          if (can_shield_bounce) {
            batch->state.item_vel_x[ii] = shield_bounce_vx;
            batch->state.item_vel_y[ii] = shield_bounce_vy;
            batch->state.item_direction[ii] = (shield_bounce_vx >= 0.0f) ? 1.0f : -1.0f;
            batch->state.item_misc0[ii] = slippi_metadata_low_byte_from_f32(1.0e-3f);
            batch->state.item_misc1[ii] = slippi_metadata_low_byte_from_f32(
                msl_melee_normalize_angle(msl_melee_atan2f(shield_bounce_vy, shield_bounce_vx)));
            break;
          }
          item_slot_clear(batch, ii);
          break;
        }
      }

      // Minimal eligibility: skip intangible hit-status, but allow disabled-capsule contact to
      // consume the item below without entering Fighter_ProcessHit.
      // Slippi post-frame: `hurtbox_state` is seeded; movescript hit status can overwrite it.
      const uint8_t terminal_x1990_body =
          batch->state.colanim_terminal_x1990_item_body_guard[d_idx];
      const uint8_t disabled_contact_only =
          (batch->state.hurtbox_state[d_idx] == (uint8_t)MSL_HURTCAPS_DISABLED &&
           terminal_x1990_body != 2u)
              ? 1u
              : 0u;
      if (terminal_x1990_body == 1u) {
        // Terminal x1990 item BODY guard:
        // - Fighter_8006A360 decrements x1990 and can clear visible x198C before Slippi t+1.
        // - The same frame's item BODY pass still follows ftColl_8007925C's collision-status gate
        //   (`if fp->x1988 == 2 || fp->x198C == 2 continue`) before hurtcap testing.
        // - Scope value 1 is terminal x1990 with no x1994 carry, so x198C fully clears after the
        //   item pass and the laser remains alive.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
        continue;
      }
      if (batch->state.hurtbox_state[d_idx] != 0u && !disabled_contact_only &&
          terminal_x1990_body != 2u) {
        continue;
      }
      // Terminal x1990 + x1994 carry:
      // Fighter_8006A360 sets x198C to 1 when x1990 expires while x1994 remains live. The item
      // BODY path only blocks collision-status value 2, so allow the ordinary BODY hit instead of
      // treating the stale replay-visible hurtbox_state=2 as intangible for this pass; value 2
      // also bypasses the disabled-contact-only clear path above so Fighter_ProcessHit receives
      // the BODY damage owner.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C

      uint8_t hit_hurt_height = 0;
      uint8_t hit = 0;
      uint8_t hit_hb_id = 0xFFu;
      uint8_t hit_from_exact_lbcoll = 0u;
      float body_overlap_amount = 0.0f;
      // Deterministic order: offsets (script order) then capsule slots.
      const uint8_t off_n =
          (laser_state == 0u) ? lp->hitbox_offsets_x_count : lp->state1_hitbox_offsets_x_count;
      const uint8_t cap_n = batch->state.hurtcap_count[d_idx];
      uint8_t body_bounds_valid = 0u;
      float body_min_x = 0.0f;
      float body_max_x = 0.0f;
      float body_min_y = 0.0f;
      float body_max_y = 0.0f;
      for (uint8_t ci = 0; ci < cap_n; ci++) {
        const size_t hi = idx_hurtcap(bi, def, (int)ci);
        if (!batch->state.hurtcap_enabled[hi]) {
          continue;
        }
        float broad_r = batch->state.hurtcap_radius[hi] * 4.0f;
        if (!(broad_r > 0.0f)) {
          broad_r = 16.0f;
        }
        const float cap_min_x =
            fminf(batch->state.hurtcap_a_x[hi], batch->state.hurtcap_b_x[hi]) - broad_r;
        const float cap_max_x =
            fmaxf(batch->state.hurtcap_a_x[hi], batch->state.hurtcap_b_x[hi]) + broad_r;
        const float cap_min_y =
            fminf(batch->state.hurtcap_a_y[hi], batch->state.hurtcap_b_y[hi]) - broad_r;
        const float cap_max_y =
            fmaxf(batch->state.hurtcap_a_y[hi], batch->state.hurtcap_b_y[hi]) + broad_r;
        if (!body_bounds_valid) {
          body_min_x = cap_min_x;
          body_max_x = cap_max_x;
          body_min_y = cap_min_y;
          body_max_y = cap_max_y;
          body_bounds_valid = 1u;
        } else {
          body_min_x = fminf(body_min_x, cap_min_x);
          body_max_x = fmaxf(body_max_x, cap_max_x);
          body_min_y = fminf(body_min_y, cap_min_y);
          body_max_y = fmaxf(body_max_y, cap_max_y);
        }
      }
      const uint8_t body_shield_adjacent =
          (batch->state.shield_radius[d_idx] > 0.0f ||
           (batch->state.state_flags[d_idx * (size_t)MSL_STATE_FLAGS_BYTES +
                                     (size_t)MSL_STATE_FLAGS_221B_INDEX] &
            (uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE) != 0u ||
           disabled_contact_only != 0u)
              ? 1u
              : 0u;
      const float laser_offset_scale = laser_collision_offset_scale(
          lp, laser_state, laser_scale_z, MSL_LASER_COLLISION_SPACE_BODY, body_shield_adjacent, 1u);
      const float laser_prev_offset_scale =
          laser_collision_offset_scale(lp, laser_state, laser_prev_scale_z,
                                       MSL_LASER_COLLISION_SPACE_BODY, body_shield_adjacent, 1u);
      // Optional item BODY x34_scale.z lane:
      // - ftCommon_8007F804(fp) returns a matrix only when fp->x34_scale.z is non-unit; only that
      //   source branch makes lbColl_8000805C overwrite hurtcap endpoint Z with fp->cur_pos.z.
      // - A visible airborne Fall state is not proof of that hidden transform. Keep ordinary Fall
      //   rows on their seed-visible endpoint path unless a future seed lane/probe proves the
      //   source x34_scale.z owner for item BODY.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007925C,ftColl_80077C60}
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
      const uint8_t flatten_body_hurt_z = laser_airborne_body_uses_flattened_hurt_z(
          batch, d_idx, laser_state, batch->state.item_type[ii], batch->state.item_attack_id[ii]);
      const uint8_t use_exact_lbcoll_hitcapsule_sweep =
          laser_body_uses_exact_lbcoll_hitcapsule_sweep(batch, d_idx, laser_state, laser_age_frames,
                                                        batch->state.item_type[ii],
                                                        body_shield_adjacent, flatten_body_hurt_z);
      const uint8_t use_landing_fall_special_exact_z =
          laser_grounded_body_landing_fall_special_exact_z_owner(batch, d_idx, laser_age_frames);
      const uint8_t exact_lbcoll_flattens_hurt_z =
          (laser_body_exact_lbcoll_flattens_hurt_z(batch, d_idx, body_shield_adjacent,
                                                   flatten_body_hurt_z) != 0u ||
           use_landing_fall_special_exact_z != 0u)
              ? 1u
              : 0u;
      // Laser BODY overlap parity:
      // - Decomp computes collision over projectile travel in-frame (prev_pos -> cur_pos), so a
      //   current-point-only probe can miss replay-causal same-frame hits.
      // - Supported grounded laser BODY rows use the shared swept travel owner when defender state
      //   exposes source BODY admission. Laser-specific matrix/Z and shield-adjacent exceptions are
      //   retained source-policy in projectiles_reflect.md; Items Core owns the shared item handoff
      //   after candidate selection, not every laser BODY geometry subcase.
      // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
      // refs/melee/src/melee/it/itcoll.c::it_80272460
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
      const uint8_t use_swept_body = laser_grounded_body_uses_sweep(batch, d_idx, laser_age_frames);
      const uint16_t defender_action_id = batch->state.action_id[d_idx];
      const uint8_t frame_start_flags_2218 = batch->state.state_flags_2218_frame_start[d_idx];
      const uint8_t guardreflect_allow_interrupt_behavior_carry =
          (defender_action_id == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           (frame_start_flags_2218 &
            (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B2 |
                      MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR)) ==
               (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT |
                         MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR))
              ? 1u
              : 0u;
      const uint8_t guardreflect_source_behavior_live =
          (defender_action_id == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.guard_reflect_timer_x18[d_idx] > 0)
              ? 1u
              : 0u;
      const uint8_t use_frame_start_lightshield_body_sample =
          (body_shield_adjacent != 0u && batch->state.lightshield_amount[d_idx] > 0.0f &&
           (frame_start_flags_2218 & 0x24u) == 0x04u &&
           (defender_action_id == (uint16_t)MSL_ACT_GUARD_ON ||
            defender_action_id == (uint16_t)MSL_ACT_GUARD ||
            guardreflect_source_behavior_live != 0u ||
            guardreflect_allow_interrupt_behavior_carry != 0u) &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX)
              ? 1u
              : 0u;
      const uint8_t guard_lightshield_shielddesc_body_suppressed =
          (body_shield_adjacent != 0u && batch->state.lightshield_amount[d_idx] > 0.0f &&
           batch->state.shield_hp[d_idx] > 0.0f && defender_action_id == (uint16_t)MSL_ACT_GUARD &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           (frame_start_flags_2218 & (uint8_t)(MSL_STATE_FLAG_2218_B1 | MSL_STATE_FLAG_2218_B2 |
                                               MSL_STATE_FLAG_2218_REFLECTING)) == 0u)
              ? 1u
              : 0u;
      // No-submotion lightshield item BODY sample:
      // - item motion (Item_802697D4) and fighter/item collision (Fighter_8006CB94 ->
      //   ftColl_8007925C) are separate HSD procs, and the replay-visible post-frame laser position
      //   is one item integration later than the frame-start HitCapsule sample consumed by the pure
      //   guard behavior lane.
      // - Keep this on x2218 behavior rows with the command bit clear (`0x04`/`0x44` and the
      //   matching allow_interrupt post-frame variants). GuardReflect uses the same lane only
      //   while its x18 behavior timer remains live after ftCo_GuardReflect_Anim's decrement, or
      //   while the allow-interrupt/reflect carry byte (`0x84` under the x2218 0xA4 mask) is still
      //   serialized. Pure seed x18=1 / x2218=0x04 reaches zero before item collision and must stay
      //   on the current BODY sample.
      //   Exclude x2218_b2 (`0x20`): adjacent replay locks show that command lane must stay on the
      //   current item sample for BODY.
      // - Sustained no-submotion Guard/lightshield rows without B1/B2/Reflecting command ownership
      //   remain ShieldDesc-owned for item contact. If the reconstructed ShieldDesc branch has
      //   already handled or rejected the laser, the later authored-offset BODY loop must not turn
      //   the same lightshield contact into full Damage*. GuardOn/GuardReflect have separate
      //   ShieldDesc/ReflectDesc handoff owners above and can still enter GuardSetOff.
      //   Source checks ShieldDesc before BODY in ftColl_8007925C; BODY only owns rows whose
      //   command/behavior flags expose that handoff.
      // refs/melee/src/melee/it/item.c::Item_802697D4
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_80093BC0,ftCo_GuardReflect_Anim}
      if (guard_lightshield_shielddesc_body_suppressed != 0u) {
        continue;
      }
      for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !hit; oi++) {
        if (!hitlist_allows_item_hitbox_fighter(batch, bi, it, (int)oi, def, def_iid)) {
          continue;
        }
        const float off_x =
            (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
        const float s0 = off_x * laser_prev_offset_scale;
        const float s1 = off_x * laser_offset_scale;
        const float sx0 = x0 + (ux * s0);
        const float sy0 = y0 + (uy * s0);
        const float sx = x + (ux * s1);
        const float sy = y + (uy * s1);
        if (body_bounds_valid != 0u) {
          const float seg_min_x = fminf(sx0, sx) - sr;
          const float seg_max_x = fmaxf(sx0, sx) + sr;
          const float seg_min_y = fminf(sy0, sy) - sr;
          const float seg_max_y = fmaxf(sy0, sy) + sr;
          if (seg_max_x < body_min_x || seg_min_x > body_max_x || seg_max_y < body_min_y ||
              seg_min_y > body_max_y) {
            continue;
          }
        }
        for (uint8_t ci = 0; ci < cap_n; ci++) {
          if (use_exact_lbcoll_hitcapsule_sweep != 0u) {
            uint8_t exact_evaluated = 0u;
            const float exact_sx1 = use_frame_start_lightshield_body_sample ? sx0 : sx;
            const float exact_sy1 = use_frame_start_lightshield_body_sample ? sy0 : sy;
            if (item_body_lbcoll_matrix_radius_overlap(
                    batch, bi, def, sx0, sy0, exact_sx1, exact_sy1, sr, (int)ci, &hit_hurt_height,
                    &body_overlap_amount, &exact_evaluated, exact_lbcoll_flattens_hurt_z) &&
                laser_exact_lbcoll_body_contact_admits_candidate(
                    batch, d_idx, common, hit_hurt_height, body_overlap_amount, laser_prev_scale_z,
                    laser_scale_z, batch->state.item_type[ii], laser_state, oi, sr, (int)ci,
                    batch->state.item_attack_id[ii])) {
              if (use_frame_start_lightshield_body_sample != 0u) {
                // Pure no-submotion lightshield Guard-family item BODY:
                // The same raw x2218 behavior lane that selects the frame-start item sample is
                // source-owned as a ShieldDesc/guard behavior window. When ShieldDesc misses,
                // vanilla preserves the laser and shield drain rather than converting a later
                // authored-offset BODY contact into full damage. Keep adjacent command/b1 lanes on
                // their shield/BODY owners; this rejection is limited to the pure lightshield
                // sample path.
                // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
                // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxLaser_Logic94_HitShield
                continue;
              }
              hit = 1;
              hit_from_exact_lbcoll = 1u;
              hit_hb_id = oi;
              break;
            }
            if (exact_evaluated != 0u) {
              if (use_swept_body == 0u || use_frame_start_lightshield_body_sample != 0u) {
                // Once the source-shaped ftColl_8007925C -> lbColl_8000805C item BODY path has
                // evaluated a no-sweep candidate, its miss is authoritative. The frame-start
                // lightshield sample only selects the previous item HitCapsule endpoint consumed by
                // source; falling through to the reduced replay-visible sphere/capsule fallback can
                // re-admit authored beam-offset contacts that lbColl's matrix/local-radius test
                // rejected.
                // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
                // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
                // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
                continue;
              }
            }
          }
          const float hx0 = (use_swept_body || use_frame_start_lightshield_body_sample) ? sx0 : sx;
          const float hy0 = (use_swept_body || use_frame_start_lightshield_body_sample) ? sy0 : sy;
          const float hx1 = use_frame_start_lightshield_body_sample ? sx0 : sx;
          const float hy1 = use_frame_start_lightshield_body_sample ? sy0 : sy;
          if (item_swept_sphere_capsule_overlap_amount(
                  batch, bi, def, hx0, hy0, hx1, hy1, sr, (int)ci, &hit_hurt_height,
                  &body_overlap_amount, flatten_body_hurt_z, 1.0f)) {
            if (laser_tail_shallow_body_contact_rejected(
                    batch, d_idx, batch->state.item_type[ii], laser_state, oi, sr, (int)ci,
                    body_overlap_amount, batch->state.item_attack_id[ii])) {
              continue;
            }
            hit = 1;
            hit_hb_id = oi;
            break;
          }
        }
      }
      // If no scripted offsets exist, fall back to the projectile origin.
      if (!hit && cap_n > 0 && off_n == 0) {
        if (hitlist_allows_item_hitbox_fighter(batch, bi, it, 0, def, def_iid)) {
          if (body_bounds_valid != 0u) {
            const float seg_min_x = fminf(x0, x) - sr;
            const float seg_max_x = fmaxf(x0, x) + sr;
            const float seg_min_y = fminf(y0, y) - sr;
            const float seg_max_y = fmaxf(y0, y) + sr;
            if (seg_max_x < body_min_x || seg_min_x > body_max_x || seg_max_y < body_min_y ||
                seg_min_y > body_max_y) {
              continue;
            }
          }
          for (uint8_t ci = 0; ci < cap_n; ci++) {
            if (use_exact_lbcoll_hitcapsule_sweep != 0u) {
              uint8_t exact_evaluated = 0u;
              const float exact_x1 = use_frame_start_lightshield_body_sample ? x0 : x;
              const float exact_y1 = use_frame_start_lightshield_body_sample ? y0 : y;
              if (item_body_lbcoll_matrix_radius_overlap(
                      batch, bi, def, x0, y0, exact_x1, exact_y1, sr, (int)ci, &hit_hurt_height,
                      &body_overlap_amount, &exact_evaluated, exact_lbcoll_flattens_hurt_z) &&
                  laser_exact_lbcoll_body_contact_admits_candidate(
                      batch, d_idx, common, hit_hurt_height, body_overlap_amount,
                      laser_prev_scale_z, laser_scale_z, batch->state.item_type[ii], laser_state,
                      0u, sr, (int)ci, batch->state.item_attack_id[ii])) {
                if (use_frame_start_lightshield_body_sample != 0u) {
                  // See the authored-offset branch above for the pure lightshield source owner.
                  continue;
                }
                hit = 1;
                hit_from_exact_lbcoll = 1u;
                hit_hb_id = 0u;
                break;
              }
              if (exact_evaluated != 0u) {
                if (use_swept_body == 0u || use_frame_start_lightshield_body_sample != 0u) {
                  continue;
                }
              }
            }
            const float hx0 = use_swept_body ? x0 : x;
            const float hy0 = use_swept_body ? y0 : y;
            if (item_swept_sphere_capsule_overlap_amount(
                    batch, bi, def, hx0, hy0, x, y, sr, (int)ci, &hit_hurt_height,
                    &body_overlap_amount, flatten_body_hurt_z, 1.0f)) {
              if (laser_tail_shallow_body_contact_rejected(
                      batch, d_idx, batch->state.item_type[ii], laser_state, 0u, sr, (int)ci,
                      body_overlap_amount, batch->state.item_attack_id[ii])) {
                continue;
              }
              hit = 1;
              hit_hb_id = 0u;
              break;
            }
          }
        }
      }
      if (!hit && laser_grounded_body_uses_lbcoll_hurt_radius(
                      batch, d_idx, laser_state, laser_age_frames, batch->state.item_type[ii])) {
        const float body_hurt_radius_mul = msl_lbcoll_body_hurt_radius_mul();
        for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !hit;
             oi++) {
          if (!hitlist_allows_item_hitbox_fighter(batch, bi, it, (int)oi, def, def_iid)) {
            continue;
          }
          const float off_x =
              (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
          const float s0 = off_x * laser_prev_offset_scale;
          const float s1 = off_x * laser_offset_scale;
          const float sx0 = x0 + (ux * s0);
          const float sy0 = y0 + (uy * s0);
          const float sx = x + (ux * s1);
          const float sy = y + (uy * s1);
          for (uint8_t ci = 0; ci < cap_n; ci++) {
            const size_t hc_idx = idx_hurtcap(bi, def, (int)ci);
            if (batch->state.hurtcap_height[hc_idx] == (uint8_t)2u) {
              continue;
            }
            if (item_swept_sphere_capsule_overlap_amount(
                    batch, bi, def, sx0, sy0, sx, sy, sr, (int)ci, &hit_hurt_height,
                    &body_overlap_amount, flatten_body_hurt_z, body_hurt_radius_mul)) {
              hit = 1;
              hit_hb_id = oi;
              break;
            }
          }
        }
      }
      if (!hit && laser_airborne_damagefall_uses_lbcoll_hurt_radius(
                      batch, d_idx, o_idx, laser_state, laser_age_frames,
                      batch->state.item_type[ii], batch->state.item_attack_id[ii])) {
        const float body_hurt_radius_mul = msl_lbcoll_body_hurt_radius_mul();
        for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !hit;
             oi++) {
          if (!hitlist_allows_item_hitbox_fighter(batch, bi, it, (int)oi, def, def_iid)) {
            continue;
          }
          const float off_x =
              (laser_state == 0u) ? lp->hitbox_offsets_x[oi] : lp->state1_hitbox_offsets_x[oi];
          const float s1 = off_x * laser_offset_scale;
          const float sx = x + (ux * s1);
          const float sy = y + (uy * s1);
          for (uint8_t ci = 0; ci < cap_n; ci++) {
            const size_t hc_idx = idx_hurtcap(bi, def, (int)ci);
            if (batch->state.hurtcap_height[hc_idx] == (uint8_t)2u) {
              continue;
            }
            if (item_swept_sphere_capsule_overlap_amount(
                    batch, bi, def, sx, sy, sx, sy, sr, (int)ci, &hit_hurt_height,
                    &body_overlap_amount, 1u, body_hurt_radius_mul)) {
              hit = 1;
              hit_hb_id = oi;
              break;
            }
          }
        }
      }
      if (!hit && laser_state != 0u &&
          batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_HI &&
          batch->state.throw_pulse_crossed_prev_frame[o_idx] ==
              (uint8_t)MSL_THROWHI_PREV_PHASE_AF &&
          batch->state.hitstun[d_idx] > 0u &&
          msl_damage_source_victim_port_matches_attacker(batch, d_idx, o_idx, owner)) {
        if (((batch->state.pos_x[d_idx] - batch->state.pos_x[o_idx]) * vx) <= 0.0f) {
          // Crossed-prev ThrowHi first-pulse callback carry:
          // - The retained current-frame first-pulse carry uses the throw-shot segment direction to
          //   avoid replaying a BODY callback on a victim that has already been represented by the
          //   throw laser's item HitCapsule victim ring.
          // - The same owner applies when teacher-forced seed starts one post-frame later with
          //   `throw_pulse_crossed_prev_frame` carrying the frame-18 command and a live state1
          //   article. v10 dumps for BHH:9419 show a populated item victims_1 entry in this phase,
          //   while front-side BHH:937 remains BODY-eligible and is left on the clear path below.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
          // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
          // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
          continue;
        }
        uint8_t throwhi_first_pulse_callback_clear = 0u;
        // ThrowHi first-pulse BODY narrowphase:
        // - Throw-side state1 lasers are spawned by ftFx_Throw_Anim through it_8029C6CC, then
        //   it_8029C4D4 tests the item hitcapsule against prev_pos->pos in the item collision pass.
        // - Replayed frame-18 rows can seed the state1 article after the frame-18 command, but the
        //   item hitcap's model scaleZ history is not replay-visible. Use the authored state1
        //   hitbox offsets without the visual scale ramp only for this crossed-prev first-pulse
        //   BODY path. The broader unscaled fallback is intentionally not restored.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,itFoxlaser_UnkMotion1_Phys,it_8029C4D4}
        // refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
        // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events
        for (uint8_t oi = 0; oi < off_n && oi < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X && !hit;
             oi++) {
          if (!hitlist_allows_item_hitbox_fighter(batch, bi, it, (int)oi, def, def_iid)) {
            continue;
          }
          const float off_x = lp->state1_hitbox_offsets_x[oi];
          const float sx0 = x0 + (ux * off_x);
          const float sy0 = y0 + (uy * off_x);
          const float sx = x + (ux * off_x);
          const float sy = y + (uy * off_x);
          for (uint8_t ci = 0; ci < cap_n; ci++) {
            if (item_swept_sphere_capsule_overlap_amount(
                    batch, bi, def, sx0, sy0, sx, sy, sr, (int)ci, &hit_hurt_height,
                    &body_overlap_amount, flatten_body_hurt_z, 1.0f)) {
              throwhi_first_pulse_callback_clear = 1u;
              hit_hb_id = oi;
              break;
            }
          }
        }
        if (!throwhi_first_pulse_callback_clear &&
            item_type_is_fox_laser(batch->state.item_type[ii]) &&
            batch->state.char_id[d_idx] == batch->state.char_id[o_idx] &&
            batch->state.last_attack_landed[d_idx] == 15u &&
            ((batch->state.pos_x[d_idx] - batch->state.pos_x[o_idx]) * vx > 0.0f)) {
          // ThrowHi first-pulse front-side BODY callback consume:
          // - The retained behind-direction slice preserves fresh articles when the already-hit
          //   victim is on the non-projectile side of the first-pulse segment.
          // - v10 item-hitlist/callback dumps for BHH:937 show the front-side first-pulse Fox
          //   article has no relevant victims_1 entry after spawn and is destroyed by the item
          //   BODY callback in the next post-frame. HIS:2428 event evidence shows a same front-side
          //   geometry shape with later hitbox identity already carried in hb1/hb2 victims_1, so keep
          //   this consume on the first-hit callback identity rather than clearing every front-side
          //   same-character article.
          // - Keep this as a consume-only callback owner; do not synthesize combo/source fields
          //   here, because those stay owned by ftColl_8007646C / ftColl_800763C0 when BODY
          //   damage is actually replay-visible.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
          // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
          // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
          // reports/triage/f14_item_hitlist_dump_bhh937/rows/engine_dump_rows.json
          throwhi_first_pulse_callback_clear = 1u;
        }
        if (throwhi_first_pulse_callback_clear) {
          item_slot_clear(batch, ii);
          break;
        }
      }
      if (!hit && laser_state != 0u &&
          batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_LW &&
          batch->state.throw_pulse_consumed[o_idx] == 0u &&
          batch->state.grab_owner_port[d_idx] == (uint8_t)owner &&
          batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_THROWN_LW) {
        int16_t owner_first_throwlw_pulse_af = 0;
        const uint8_t owner_has_first_throwlw_pulse =
            move_tables_throw_projectile_first_pulse_frame(batch->state.char_id[o_idx],
                                                           batch->state.action_id[o_idx],
                                                           &owner_first_throwlw_pulse_af);
        const uint8_t throwlw_attached_pulse =
            (batch->state.throw_pulse_crossed_prev_frame[o_idx] ==
             (uint8_t)MSL_THROWLW_PULSE_ATTACH_AF)
                ? 1u
                : ((owner_has_first_throwlw_pulse &&
                    batch->state.throw_pulse_crossed_curr_frame[o_idx] ==
                        (uint8_t)owner_first_throwlw_pulse_af)
                       ? 1u
                       : 0u);
        const uint8_t throwlw_first_visible_attached_pulse =
            (owner_has_first_throwlw_pulse &&
             batch->state.action_frame[o_idx] == (int16_t)owner_first_throwlw_pulse_af &&
             batch->state.throw_command_pending_pulse_frame[o_idx] == 0u &&
             batch->state.throw_pulse_crossed_prev_frame[o_idx] == 0u &&
             batch->state.hitlag_pre_timer[d_idx] != 0u)
                ? 1u
                : 0u;
        if (!throwlw_attached_pulse && !throwlw_first_visible_attached_pulse) {
          continue;
        }
        uint8_t throwlw_victim_ring_blocks = 0u;
        if (item_type_is_falco_laser(batch->state.item_type[ii])) {
          // Falco ThrowLw callback-phase split:
          // v10 dumps show Falco-laser attached rows can carry victims_1 on hitboxes 2/3 while
          // the next BODY callback still uses hitboxes 0/1. Do not let hb2/3 suppress the miss bridge;
          // only block when both BODY-phase lanes are already in the victim ring.
          // refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
          const uint8_t hb0_allows =
              hitlist_allows_item_hitbox_fighter(batch, bi, it, 0, def, def_iid);
          const uint8_t hb1_allows =
              hitlist_allows_item_hitbox_fighter(batch, bi, it, 1, def, def_iid);
          throwlw_victim_ring_blocks = (hb0_allows || hb1_allows) ? 0u : 1u;
        } else {
          for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
            if (!hitlist_allows_item_hitbox_fighter(batch, bi, it, hb, def, def_iid)) {
              throwlw_victim_ring_blocks = 1u;
              break;
            }
          }
        }
        if (throwlw_victim_ring_blocks) {
          continue;
        }
        // ThrowLw attached-victim pulse bridge (miss-only):
        // - ThrowLw throw-side pulses are script-owned one-shots (23/25/28/31) consumed in
        //   ftFx_Throw_Anim; seed lane `throw_pulse_crossed_prev_frame` carries prior-step crossing.
        // - In attached ThrownLw contexts, missing same-step BODY overlap at the first visible
        //   pulse, first current-frame pulse, or the 25-frame carried pulse leaves replay-causal
        //   hitlag/state-flags deltas; bridge only when geometry probe missed. The frame-25
        //   post-hitlag current-pulse subcase is handled at spawn time below because adjacent rows
        //   can serialize the article without same-frame BODY hitlag.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
        // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowLw"]["events"]
        hit = 1;
        hit_hb_id = 0xFFu;
        hit_hurt_height = 1u;
      }
      if (!hit) {
        continue;
      }

      if (laser_state != 0u && item_type_is_falco_laser(batch->state.item_type[ii]) &&
          batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_B &&
          batch->state.throw_pulse_crossed_curr_frame[o_idx] != 0u &&
          batch->state.hitstun[d_idx] > 0u &&
          msl_damage_source_victim_port_matches_attacker(batch, d_idx, o_idx, owner) &&
          batch->state.last_attack_landed[d_idx] != (uint8_t)lp->shot_itkind &&
          ((x - batch->state.pos_x[d_idx]) * vx) > 0.0f) {
        // Falco ThrowB same-frame pulse carry:
        // - `it_80272460` tests item HitCapsules after item motion. In throw-hitstun rows where
        //   the carried state1 projectile endpoint has already passed the victim on this frame, a
        //   swept segment replay can falsely consume the article. Source keeps that carry through
        //   the item victims_1 ring; rows where the endpoint has not yet crossed the victim stay on
        //   the normal BODY consume path.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
        // refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
        continue;
      }

      if (item_type_is_fox_laser(batch->state.item_type[ii]) &&
          batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_PASSIVE) {
        // Passive hidden-colanim BODY guard (Fox laser):
        // - Passive keeps colanim hit status (`Ft_MF_KeepColAnimHitStatus`) across the tech motion;
        //   on replay-proven Fox-laser contacts, a same-frame Passive overlap can be hidden
        //   invulnerability ownership rather than an item BODY consume.
        // - Keep this scoped to scaled-overlap false positives: if the authored item HitCapsule
        //   offset sample still overlaps before FoxLaser's visual scaleZ stretch, the source item
        //   callback owns a real BODY consume and damage handoff.
        // - Falco Passive contacts remain on the normal BODY consume path.
        // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_Passive
        // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
        // refs/melee/src/melee/it/items/itfoxlaser.c::{itFoxlaser_UnkMotion1_Anim,it_8029C4D4}
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
        continue;
      }

      if (disabled_contact_only) {
        // Disabled fighter hurt capsules can still own the item contact/despawn without routing
        // through Fighter_ProcessHit damage. This mirrors the item/fighter collision split where
        // `lbColl` selects contact against the fighter capsule state, while damage application
        // remains gated by the fighter hit status.
        // refs/melee/src/melee/lb/forward.h::HurtCapsuleState
        // refs/melee/src/melee/it/itcoll.c
        item_slot_clear(batch, ii);
        break;
      }

      if (laser_state == 0u && common != NULL &&
          (batch->state.on_ground[d_idx] == 0u || hit_from_exact_lbcoll != 0u) &&
          batch->state.hitstun[d_idx] == 0u && body_overlap_amount > 0.0f &&
          body_overlap_amount <= common->phantom_overlap_max_x7a8) {
        // Item phantom/tip-log BODY contact:
        // - ftColl_80077C60 routes small positive `coll_distance < p_ftCommonData->x7A8` item
        //   overlaps through checkTipLog into victim hitlag only, without percent/KB/damage-state
        //   entry. Grounded BODY takes this path only when the exact lbColl HitCapsule sweep above
        //   produced the coll_distance owner; replay-visible overlaps stay on the old airborne-only
        //   lane.
        // - Item hitbox damage is normalized by it_80272460 before hitlag calculation; the projectile
        //   itself persists because no damage/dealt callback consumes it on this lane.
        // refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80077C60}
        // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        // refs/melee/src/melee/it/itcoll.c::it_80272460
        float dmg =
            item_laser_script_hitcapsule_damage(lp, laser_state, hit_hb_id, laser_age_frames);
        dmg = msl_item_reflect_damage_lane(batch, ii, dmg);
        if (laser_item_phantom_hitlag_suppressed_by_reflect_behavior_carry(
                batch, d_idx, laser_state, body_overlap_amount, common)) {
          combat_apply_item_phantom_attribution(batch, bi, owner, def,
                                                batch->state.item_instance_id[ii]);
        } else {
          combat_apply_item_phantom_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                        batch->state.item_instance_id[ii], dmg, lp->element);
        }
        const uint16_t def_iid_post = batch->state.instance_id[d_idx];
        if (hit_hb_id != 0xFFu) {
          hitlist_register_item_hitbox_fighter(batch, bi, it, (int)hit_hb_id, def, def_iid_post,
                                               (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
        } else {
          hitlist_register_item_fighter(batch, bi, it, def, def_iid_post,
                                        (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
        }
        break;
      }

      int16_t owner_first_throw_pulse_af = 0;
      const uint8_t owner_has_first_throw_pulse = move_tables_throw_projectile_first_pulse_frame(
          batch->state.char_id[o_idx], batch->state.action_id[o_idx], &owner_first_throw_pulse_af);
      if (laser_state != 0u && batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_HI &&
          owner_has_first_throw_pulse &&
          batch->state.throw_pulse_crossed_curr_frame[o_idx] ==
              (uint8_t)owner_first_throw_pulse_af &&
          body_overlap_amount <= 0.0f && batch->state.hitstun[d_idx] > 0u &&
          msl_damage_source_victim_port_matches_attacker(batch, d_idx, o_idx, owner) &&
          batch->state.last_attack_landed[d_idx] != (uint8_t)lp->shot_itkind &&
          ((batch->state.pos_x[d_idx] - batch->state.pos_x[o_idx]) * vx <= 0.0f)) {
        // ThrowHi first projectile pulse contact carry:
        // - ftAction_80071974 emits a one-shot throw_flags_b0 pulse and ftFx_Throw_Anim consumes it
        //   to spawn the throw-side laser article.
        // - The throw launch vector is `atan2(FtHoldJoint - ItHoldJoint)` and it_8029C4D4 resolves
        //   item collision along the laser's previous-to-current segment. Keep the carry only when
        //   the BODY probe did not produce positive penetration; event probes for BHH:4266 show
        //   positive hb0 same-frame BODY/give-damage/destroy must fall through to the callback path.
        // - Keep this on the current-frame first pulse only; later ThrowHi pulses and normal laser
        //   BODY hits stay on the regular item-hit path below.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
        // refs/melee/src/melee/it/itcoll.c::it_80272460
        // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events
        continue;
      }

      const uint8_t falco_throwhi_final_pulse_curr =
          (batch->state.throw_pulse_crossed_curr_frame[o_idx] == 24u) ? 1u : 0u;
      const uint8_t falco_throwhi_final_pulse_prev_non_projectile_side =
          (batch->state.throw_pulse_crossed_prev_frame[o_idx] == 24u &&
           ((batch->state.pos_x[d_idx] - batch->state.pos_x[o_idx]) * vx) <= 0.0f)
              ? 1u
              : 0u;
      if (laser_state != 0u && batch->state.char_id[o_idx] == (uint8_t)MSL_CHAR_ID_FALCO &&
          batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_HI &&
          (falco_throwhi_final_pulse_curr != 0u ||
           falco_throwhi_final_pulse_prev_non_projectile_side != 0u) &&
          batch->state.hitlag[d_idx] > 1u &&
          msl_damage_source_victim_port_matches_attacker(batch, d_idx, o_idx, owner) &&
          batch->state.last_attack_landed[d_idx] == 0u) {
        // Falco ThrowHi late-pulse carried BODY contact:
        // - ThrowHi owns three one-shot throw_flags_b0 projectile pulses (18/20/24) consumed in
        //   ftFx_Throw_Anim.
        // - On the final Falco pulse, replay snapshots can carry an already-hit victim in hitlag
        //   while the command cursor emits or carries the fresh state1 article; re-consuming the
        //   same hitlag victim as a new BODY hit falsely clears the carried article and bumps
        //   item-domain combo/source bookkeeping.
        // - Current-frame final-pulse rows stay on the command callback owner. Crossed-prev rows also
        //   require the victim to be on the non-projectile side of the seeded laser segment; the
        //   projectile-side crossed-prev row is still owned by it_8029C4D4 BODY consume/despawn.
        // - Keep this on the frame-24 pulse's high-hitlag phase and require cleared replay damage
        //   provenance (`last_attack_landed==0`) so adjacent carry/consume handoff rows and active
        //   damage rows stay on the normal BODY path.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C4D4
        // refs/melee/src/melee/it/itcoll.c::it_80272460
        // data/moves/falco.json moves["ftCo_SM_ThrowHi"].events
        if (falco_throwhi_final_pulse_curr != 0u) {
          const MslItemHitResult topoff_res = combat_apply_item_hit(
              batch, bi, owner, def, batch->state.item_attack_id[ii],
              batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
              batch->state.item_type[ii], batch->state.item_state[ii], lp->state1_damage,
              lp->state1_angle, lp->state1_kbg, lp->state1_wsk, lp->state1_bkb, hit_hurt_height,
              lp->state1_element, -1.0f, batch->state.item_pos_x[ii], batch->state.item_pos_y[ii],
              lp->state1_size, batch->state.item_vel_x[ii], 0u);
          if (topoff_res == MSL_ITEM_HIT_APPLIED_CONSUME_ITEM) {
            item_slot_clear(batch, ii);
            break;
          }
          if (topoff_res != MSL_ITEM_HIT_NONE) {
            continue;
          }
        }
        continue;
      }

      // Seed-owned ThrowLw late blaster pulse handoff:
      // - ftFx_Throw_Anim emits multiple throw_flags_b0 pulses during ThrowLw (data/moves
      //   set_throw_spawn_projectile @ 23/25/28/31) and spawns with it_8029C6CC (msid=1).
      // - In reseeded one-step frames deep in ThrowLw, replay refs can carry the spawned item at
      //   t+1 without a new same-frame BODY hit while the victim is still attached.
      // - The MSLFTSC1 throw pulse cursor plus attached-victim ownership reconstructs the consumed
      //   source latch for this teacher-forced boundary, so the late attached BODY re-hit stays on
      //   the throw script owner instead of the ordinary item BODY callback.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
      int16_t last_throw_pulse_af = 0;
      const uint8_t has_last_throw_pulse = move_tables_throw_projectile_last_pulse_frame(
          batch->state.char_id[o_idx], batch->state.action_id[o_idx], &last_throw_pulse_af);
      if (laser_state != 0u && batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_LW &&
          has_last_throw_pulse &&
          items_cur_anim_frame_f32(batch, o_idx) >= ((float)last_throw_pulse_af + 1.0f) &&
          batch->state.throw_pulse_crossed_prev_frame[o_idx] != (uint8_t)last_throw_pulse_af &&
          batch->state.grab_owner_port[d_idx] == (uint8_t)owner &&
          msl_action_is_grabbed_victim(batch->state.action_id[d_idx])) {
        continue;
      }
      if (laser_state != 0u && batch->state.action_id[o_idx] == (uint16_t)MSL_ACT_THROW_LW &&
          batch->state.grab_owner_port[d_idx] == (uint8_t)owner &&
          msl_action_is_grabbed_victim(batch->state.action_id[d_idx]) &&
          batch->state.hitlag_pre_timer[d_idx] != 0u) {
        // Seed-bridge discriminator for ThrowLw attached victim collisions:
        // - In decomp, thrown victims are attachment-driven (Thrown* Phys/Coll are empty), and
        //   throw-side projectile pulses are script-time one-shots consumed in ftFx_Throw_Anim.
        // - On reseeded mid-hitlag snapshots, replay rows can carry victim hitlag>0 at t while
        //   the consumed throw pulse latch is not exposed by Slippi; re-applying attached BODY
        //   contact in that window spuriously re-extends hitlag at t+1.
        // - Gate on seed-visible pre-hitlag only (hitlag_pre_timer) to keep the suppression narrow.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE508,ftCo_ThrownF_Phys,ftCo_ThrownF_Coll}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A1BC
        continue;
      }

      // BODY contact: attempt to apply an item hit.
      //
      // Laser lifetime policy (suite-stable):
      // - Shield hit => despawn (handled above; itfoxlaser Logic94 HitShield).
      // - Real BODY hit applied => despawn (matches in-game behavior; prevents item-slot drift).
      // - BODY hit suppressed because the defender is still an attached grabbed/thrown victim of the
      //   attacker => do not despawn (victim stays in Thrown*/Capture*; see combat_apply_item_hit()).
      //
      // Decomp-first reference for item-vs-fighter BODY intake: refs/melee/src/melee/it/itcoll.c::it_80272460.
      // Decomp-first references for BODY apply:
      // - Fighter_ProcessHit_8006D1EC (percent add, hitlag, hitstun, damage-state entry)
      // - ftColl_80076CBC (getEnvDmg pattern)
      // refs/melee/src/melee/ft/fighter.c and refs/melee/src/melee/ft/ftcoll.c
      float dmg = item_laser_script_hitcapsule_damage(lp, laser_state, hit_hb_id, laser_age_frames);
      uint16_t body_attack_id = batch->state.item_attack_id[ii];
      uint16_t body_attack_instance = batch->state.item_attack_instance[ii];
      float body_stale_mult = laser_body_stale_mult;
      if (item_laser_runtime_reflected_body_damage_owner(batch, bi, ii, owner, &dmg,
                                                         &body_attack_id, &body_attack_instance,
                                                         &body_stale_mult) != 0u) {
        // Runtime Item_80269F14 reflect callback already supplied the hidden reflected BODY owner.
      } else if (item_laser_powershield_return_body_damage_owner(batch, bi, ii, owner, def, dmg,
                                                                 body_attack_id, &dmg,
                                                                 &body_stale_mult) != 0u) {
        // Returned powershielded blaster shots consume the original shooter's live staled
        // HitCapsule.damage float before the reflected BODY damage is applied.
      } else if (item_laser_reflected_body_damage_owner(batch, bi, ii, owner, dmg, &body_attack_id,
                                                        &body_attack_instance, &dmg,
                                                        &body_stale_mult) == 0u) {
        dmg = msl_item_reflect_damage_lane(batch, ii, dmg);
      }
      const uint16_t angle = (laser_state == 0u) ? lp->angle : lp->state1_angle;
      const uint16_t kbg = (laser_state == 0u) ? lp->kbg : lp->state1_kbg;
      const uint16_t wsk = (laser_state == 0u) ? lp->wsk : lp->state1_wsk;
      const uint16_t bkb = (laser_state == 0u) ? lp->bkb : lp->state1_bkb;
      const uint8_t element = (laser_state == 0u) ? lp->element : lp->state1_element;
      const uint8_t defender_guard_reflect_no_submotion_body_handoff =
          item_guard_reflect_body_hit_consumes_shield_state(batch, d_idx);
      const uint8_t defender_guardon_reflect_body_undo_recharge =
          item_guardon_reflect_body_hit_undoes_action_recharge(batch, d_idx);
      const MslItemHitResult res = combat_apply_item_hit(
          batch, bi, owner, def, body_attack_id, body_attack_instance,
          batch->state.item_instance_id[ii], batch->state.item_type[ii], laser_state, dmg, angle,
          kbg, wsk, bkb, hit_hurt_height, element, body_stale_mult, batch->state.item_pos_x[ii],
          batch->state.item_pos_y[ii], (laser_state == 0u) ? lp->size : lp->state1_size,
          batch->state.item_vel_x[ii], 1u);
      if (res == MSL_ITEM_HIT_NONE) {
        continue;
      }
      item_laser_commit_reflected_body_stale_owner(batch, bi, ii, owner);
      if (res == MSL_ITEM_HIT_APPLIED_CONSUME_ITEM) {
        if (defender_guard_reflect_no_submotion_body_handoff) {
          item_guard_reflect_restore_anim_drain(batch, d_idx);
        }
        if (defender_guardon_reflect_body_undo_recharge) {
          item_guardon_reflect_undo_action_recharge(batch, d_idx);
        }
        // Laser BODY damage is accumulated by itcoll during the item collision pass; the
        // `dmg_dealt` logic callback that destroys Fox/Falco lasers runs afterward in
        // Item_8026A294. Keep the item live for later fighters in this same pass so one laser can
        // hit multiple victims before the deferred destroy callback consumes it.
        // refs/melee/src/melee/it/item.c::{Item_80269C5C,Item_8026A294,OnGiveDamageThink}
        // refs/melee/src/melee/it/itcoll.c::{it_802706D0,it_8026FAC4,it_80272460}
        clear_after_body_damage_pass = 1u;
      }
      if (res == MSL_ITEM_HIT_APPLIED_DONT_CONSUME) {
        if (defender_guard_reflect_no_submotion_body_handoff) {
          item_guard_reflect_restore_anim_drain(batch, d_idx);
        }
        if (defender_guardon_reflect_body_undo_recharge) {
          item_guardon_reflect_undo_action_recharge(batch, d_idx);
        }
      }
      // Rehit suppression latch for this item (runtime): insert the post-mutation victim identity.
      const uint16_t def_iid_post = batch->state.instance_id[d_idx];
      if (hit_hb_id != 0xFFu) {
        hitlist_register_item_hitbox_fighter(batch, bi, it, (int)hit_hb_id, def, def_iid_post,
                                             (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
      } else {
        hitlist_register_item_fighter(batch, bi, it, def, def_iid_post,
                                      (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
      }
      if (clear_after_body_damage_pass != 0u) {
        continue;
      }
      break;
    }
    if (clear_after_body_damage_pass != 0u && batch->state.item_exists[ii]) {
      item_slot_clear(batch, ii);
      continue;
    }
    if (batch->state.item_exists[ii]) {
      if (stage_line_hit != 0u) {
        if (batch->state.item_timer[ii] > 1.0f) {
          batch->state.item_timer[ii] = 1.0f;
        }
        batch->state.item_pos_x[ii] = x;
        batch->state.item_pos_y[ii] = y;
        continue;
      }
      if ((hidden_flags & (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_CLEAR) != 0u) {
        item_slot_clear(batch, ii);
        continue;
      }
      item_apply_seeded_reflect_transfer_after_collision(batch, ii);
      msl_item_reflect_clear_seed_lanes(batch, ii);
      if (hidden_flags != 0u) {
        batch->state.item_hidden_callback_flags[ii] = 0u;
      }
    }
  }
}

static void yoshi_shyguy_items_update(MslBatch* batch, int bi) {
  const MslYoshiShyguyParams* params = stage_item_params_yoshi_shyguy();
  if (batch == NULL || batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_YOSHIS_STORY ||
      params == NULL) {
    return;
  }
  MslStageBounds blast_bounds = {0};
  const uint8_t has_blast_bounds =
      stage_collision_get_blast_bounds_world(batch->state.stage_id[bi], &blast_bounds);
  uint8_t needs_sort = 0u;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii] ||
        !stage_item_params_is_yoshi_shyguy_item_type(params, batch->state.item_type[ii])) {
      continue;
    }
    const uint8_t state = batch->state.item_state[ii];
    if (state == 2u || state == 3u) {
      if (batch->state.item_shyguy_hitlag_valid[ii] != 0u &&
          batch->state.item_shyguy_hitlag[ii] > 0u) {
        // Generic item hitlag decrements before the item Phys/movement proc and leaves
        // xDC8_word.flags.x9 set while positive, so Item_802697D4 skips both Phys and position.
        // refs/melee/src/melee/it/item.c::{Item_802693E4,Item_802697D4}
        batch->state.item_shyguy_hitlag[ii]--;
        batch->state.item_hitlag[ii] = batch->state.item_shyguy_hitlag[ii];
        if (state == 3u) {
          (void)yoshi_shyguy_try_knocked_state_fighter_body_hit(batch, bi, it, params);
        }
        continue;
      }
      if (state == 3u && batch->state.item_shyguy_delay_valid[ii] != 0u &&
          batch->state.item_shyguy_delay[ii] == 0u) {
        // State 3 Phys enters the return-flight state through it_802D9168 once x24 has counted
        // down. The native seed lane resets x24 on repeated low-damage callbacks while the item
        // remains in state 3, matching it_802D8EC8's same-state writeback.
        // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion3_Phys,it_802D9168,
        //   it_802D8EC8}
        batch->state.item_state[ii] = 4u;
        batch->state.item_vel_x[ii] = 0.0f;
        batch->state.item_vel_y[ii] = 0.0f;
        batch->state.item_direction[ii] = (batch->state.item_pos_x[ii] < 0.0f) ? -1.0f : 1.0f;
        batch->state.item_shyguy_delay[ii] = (uint16_t)MSL_YOSHI_SHYGUY_TURN_DELAY_FRAMES;
        batch->state.item_shyguy_delay_valid[ii] = 1u;
        batch->state.item_shyguy_dyn_y_phase[ii] = 0u;
        batch->state.item_shyguy_dyn_y_phase_valid[ii] = 1u;
        batch->state.item_shyguy_prev_vel_y[ii] = 0.0f;
        batch->state.item_shyguy_prev_vel_y_valid[ii] = 0u;
        continue;
      }
      // Knocked/falling Shy Guy states use item gravity, then generic item position integration.
      // State 3 additionally delays before entering the return-flight state.
      // refs/melee/src/melee/it/items/itheiho.c::{
      //   itHeiho_UnkMotion2_Phys,itHeiho_UnkMotion3_Phys,it_802D9168}
      if (state == 2u) {
        // `itHeiho_UnkMotion2_Phys` uses generic item falling with the Article ItemAttr max-fall
        // speed. Once x40_vel.y is at/past the max threshold, vanilla preserves it rather than
        // applying another gravity tick.
        // refs/melee/src/melee/it/items/itheiho.c::itHeiho_UnkMotion2_Phys
        // refs/melee/src/melee/it/it_26B1.c::it_80272860
        if (batch->state.item_vel_y[ii] > -params->fall_speed_max) {
          batch->state.item_vel_y[ii] -= params->fall_accel;
        }
      } else {
        batch->state.item_vel_y[ii] -= params->fall_accel;
      }
      batch->state.item_pos_x[ii] += batch->state.item_vel_x[ii];
      batch->state.item_pos_y[ii] += batch->state.item_vel_y[ii];
      if (state == 3u && batch->state.item_shyguy_delay_valid[ii] != 0u &&
          batch->state.item_shyguy_delay[ii] > 0u) {
        batch->state.item_shyguy_delay[ii]--;
      }
      if (has_blast_bounds != 0u &&
          yoshi_shyguy_falling_state_crossed_generic_blast_clear(
              &blast_bounds, batch->state.item_pos_x[ii], batch->state.item_pos_y[ii]) != 0u) {
        item_slot_clear(batch, ii);
        needs_sort = 1u;
      }
      continue;
    }
    if (state == 0u && batch->state.item_shyguy_delay_valid[ii] != 0u) {
      // State 0 owns the spawn staggering delay. When x24 reaches zero, the state changes to
      // active Shy Guy but does not run the active Phys callback until the next item proc.
      // refs/melee/src/melee/it/items/itheiho.c::itHeiho_UnkMotion0_Phys
      if (batch->state.item_shyguy_delay[ii] == 0u) {
        batch->state.item_state[ii] = 1u;
        batch->state.item_vel_x[ii] = 0.0f;
        batch->state.item_vel_y[ii] = 0.0f;
        batch->state.item_shyguy_prev_vel_y[ii] = 0.0f;
        batch->state.item_shyguy_prev_vel_y_valid[ii] = 0u;
        batch->state.item_shyguy_dyn_y_phase[ii] = 0u;
        batch->state.item_shyguy_dyn_y_phase_valid[ii] = 1u;
      } else {
        batch->state.item_shyguy_delay[ii]--;
      }
      continue;
    }
    if (state != 1u && state != 4u) {
      continue;
    }
    // Active Shy Guy states run their Anim callback, which refreshes x40_vel from a dynamic-bone
    // translation delta, then Phys recomputes source-owned X velocity and generic item proc
    // integrates `item->pos += item->x40_vel + item->x70_nudge`. Slippi exposes current x40_vel
    // but not itemVar.heiho.x3C or the JObj AObj frame, so one-step seeds carry the previous
    // visible Y velocity for the same item identity as a prefix-causal phase key into the
    // GrSt.dat FObj delta table. The scratch is not a future lane: preprocessing derives it from
    // frame t-1 only. state-0 x24 delay, collision turnarounds, and spawn RNG remain separate
    // stage-object owners.
    // refs/melee/src/melee/it/items/itheiho.c::{it_802D98C4,itHeiho_UnkMotion1_Anim,
    //   itHeiho_UnkMotion4_Anim}
    // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Phys,itHeiho_UnkMotion4_Phys}
    // refs/melee/src/melee/it/item.c::Item_802697D4
    if (has_blast_bounds != 0u && state == 1u) {
      // Active state 1 calls `it_802D9714` from Phys. That helper sets the generic clear flag only
      // after the Shy Guy has entered the interior and then crosses blast bounds with a 20-unit
      // margin. x22 is hidden; direction plus the crossed side is enough for the legal-stage Shy
      // Guy paths currently represented in replay rows.
      // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Phys,it_802D9714}
      const float x = batch->state.item_pos_x[ii];
      const float y = batch->state.item_pos_y[ii];
      const float dir = batch->state.item_direction[ii];
      if ((dir > 0.0f && x > blast_bounds.right + 20.0f) ||
          (dir < 0.0f && x < blast_bounds.left - 20.0f) || y > blast_bounds.top + 20.0f ||
          y < blast_bounds.bottom - 20.0f) {
        item_slot_clear(batch, ii);
        needs_sort = 1u;
        continue;
      }
    }
    float move_vel_x = batch->state.item_vel_x[ii];
    float export_vel_x = batch->state.item_vel_x[ii];
    if (batch->state.item_shyguy_speed_index_valid[ii] != 0u) {
      const uint8_t speed_index =
          batch->state.item_shyguy_speed_index[ii] % MSL_YOSHI_SHYGUY_SPEED_COUNT;
      float speed_x = params->speed[speed_index] * batch->state.item_direction[ii];
      if (state == 4u) {
        speed_x *= params->state4_speed_mul;
      }
      move_vel_x = speed_x;
      export_vel_x = speed_x;
    }
    float move_vel_y = batch->state.item_vel_y[ii];
    float export_vel_y = batch->state.item_vel_y[ii];
    uint8_t phase_before_update = batch->state.item_shyguy_dyn_y_phase[ii];
    const uint8_t phase_before_update_valid = batch->state.item_shyguy_dyn_y_phase_valid[ii];
    uint8_t phase_used = 0u;
    if (phase_used == 0u && batch->state.item_shyguy_dyn_y_phase_valid[ii] != 0u) {
      const uint8_t phase = batch->state.item_shyguy_dyn_y_phase[ii];
      const float expected_cur = yoshi_shyguy_current_vel_y_for_phase(params, phase, state);
      if (fabsf(expected_cur - move_vel_y) <= 0.001f) {
        move_vel_y = yoshi_shyguy_vel_y_from_phase(params, phase, state);
        export_vel_y = move_vel_y;
        batch->state.item_shyguy_dyn_y_phase[ii] = (uint8_t)((phase + 1u) & 0xFFu);
        phase_used = 1u;
      } else if (state == 1u && batch->state.item_shyguy_prev_vel_y_valid[ii] != 0u &&
                 yoshi_shyguy_state1_current_is_reset_export(
                     params, phase, batch->state.item_shyguy_prev_vel_y[ii], move_vel_y) != 0u) {
        move_vel_y = params->dyn_y_vel[0];
        export_vel_y = params->dyn_y_vel[0];
        batch->state.item_shyguy_dyn_y_phase[ii] = (uint8_t)((phase + 1u) & 0xFFu);
        phase_used = 1u;
      } else if (state == 1u && batch->state.item_shyguy_prev_vel_y_valid[ii] != 0u) {
        const uint8_t prev_phase = (uint8_t)((phase + 255u) & 0xFFu);
        const float prev_reset_export = -yoshi_shyguy_dyn_y_pos_after_phase(params, prev_phase);
        if (fabsf(prev_reset_export - batch->state.item_shyguy_prev_vel_y[ii]) <= 0.001f &&
            fabsf(params->dyn_y_vel[0] - move_vel_y) <= 0.001f) {
          move_vel_y = params->dyn_y_vel[1];
          export_vel_y = params->dyn_y_vel[1];
          batch->state.item_shyguy_dyn_y_phase[ii] = 2u;
          phase_used = 1u;
        } else {
          batch->state.item_shyguy_dyn_y_phase_valid[ii] = 0u;
        }
      } else {
        batch->state.item_shyguy_dyn_y_phase_valid[ii] = 0u;
      }
    }
    if (phase_used == 0u) {
      if (batch->state.item_shyguy_prev_vel_y_valid[ii] != 0u) {
        const float prev_vel_y = batch->state.item_shyguy_prev_vel_y[ii];
        float next_vel_y = move_vel_y;
        if (yoshi_shyguy_next_dynamic_vel_y(params, prev_vel_y, move_vel_y, (state == 4u) ? 1u : 0u,
                                            &next_vel_y) != 0u) {
          move_vel_y = next_vel_y;
          export_vel_y = next_vel_y;
        }
      } else if (fabsf(move_vel_y) <= 0.001f && state == 1u) {
        // `itHeiho_UnkMotion0_Phys` enters state 1 through `it_802D8918`, which resets x3C and
        // immediately samples the active child-JObj translation once before the first active Phys.
        // refs/melee/src/melee/it/items/itheiho.c::{it_802D8918,it_802D98AC,it_802D98C4}
        move_vel_y = params->dyn_y_vel[0];
        export_vel_y = params->dyn_y_vel[0];
      }
    }
    const uint8_t turn_delay_started_positive =
        (uint8_t)(batch->state.item_shyguy_delay_valid[ii] != 0u &&
                  batch->state.item_shyguy_delay[ii] > 0u);
    uint8_t turn_contact_allowed = 1u;
    if (turn_delay_started_positive != 0u) {
      batch->state.item_shyguy_delay[ii]--;
      if (batch->state.item_shyguy_delay[ii] > 0u) {
        turn_contact_allowed = 0u;
      }
    }
    if (state == 4u && turn_delay_started_positive != 0u) {
      // Return-flight x24 turn/camera delay: keep the existing broad export owner for rows whose
      // dynamic-bone provenance is not reasserted by a live source item-hitlist bridge below.
      // refs/melee/src/melee/it/items/itheiho.c::{it_802D9168,itHeiho_UnkMotion4_Phys}
      export_vel_x = batch->state.item_vel_x[ii];
      export_vel_y = batch->state.item_vel_y[ii];
    }
    batch->state.item_shyguy_prev_vel_y[ii] = batch->state.item_vel_y[ii];
    batch->state.item_shyguy_prev_vel_y_valid[ii] = 1u;
    const float prev_pos_x = batch->state.item_pos_x[ii];
    const float prev_pos_y = batch->state.item_pos_y[ii];
    batch->state.item_pos_x[ii] += move_vel_x;
    batch->state.item_pos_y[ii] += move_vel_y;
    if (has_blast_bounds != 0u && state == 4u &&
        yoshi_shyguy_falling_state_crossed_generic_blast_clear(
            &blast_bounds, batch->state.item_pos_x[ii], batch->state.item_pos_y[ii]) != 0u) {
      // Return-flight state 4 does not call `it_802D9714`. It is entered from the low-damage
      // callback path after `it_802D8EC8` sets xDCC_flag.b3, so the generic item proc clears it on
      // exact side/bottom blast bounds after Phys/integration and before Coll.
      // refs/melee/src/melee/it/items/itheiho.c::{it_802D8EC8,it_802D9168,
      //   itHeiho_UnkMotion4_Phys}
      // refs/melee/src/melee/it/item.c::{Item_802697D4,Item_802696CC}
      item_slot_clear(batch, ii);
      needs_sort = 1u;
      continue;
    }
    const uint8_t wall_contact =
        (uint8_t)(turn_contact_allowed != 0u &&
                  yoshi_shyguy_active_fixed_ecb_wall_contact(
                      params, batch->state.stage_id[bi], batch->state.item_direction[ii],
                      prev_pos_x, prev_pos_y, batch->state.item_pos_x[ii],
                      batch->state.item_pos_y[ii]) != 0u);
    if (wall_contact != 0u) {
      const float new_dir = -batch->state.item_direction[ii];
      batch->state.item_direction[ii] = new_dir;
      if (batch->state.item_shyguy_speed_index_valid[ii] != 0u) {
        const uint8_t speed_index =
            batch->state.item_shyguy_speed_index[ii] % MSL_YOSHI_SHYGUY_SPEED_COUNT;
        export_vel_x = params->speed[speed_index] * new_dir;
      } else {
        export_vel_x = -export_vel_x;
      }
      batch->state.item_shyguy_delay[ii] = (uint16_t)MSL_YOSHI_SHYGUY_TURN_DELAY_FRAMES;
      batch->state.item_shyguy_delay_valid[ii] = 1u;
    } else if (yoshi_shyguy_active_fixed_ecb_floor_contact(
                   params, batch->state.stage_id[bi], prev_pos_x, prev_pos_y,
                   batch->state.item_pos_x[ii], batch->state.item_pos_y[ii]) != 0u) {
      // `temp_r31 == 1` restarts the current active/return animation through
      // `itHeiho_UnkMotion1_Anim_inline` or `it_802D9168`. State 1 resets itemVar.heiho.x3C and
      // immediately calls `it_802D98C4`, so the post-frame export is the reset-to-current child-JObj
      // delta while X stays at the floor-contact value; preserve the phase+previous-velocity pair so
      // the following Anim consumes the first ordinary child-delta frame. Return-flight state 4
      // re-enters the same `it_802D9168` return-flight owner and exports zero velocity on the
      // contact frame before the next Anim callback refreshes its dynamic-bone delta.
      // refs/melee/src/melee/it/items/itheiho.c::{itHeiho_UnkMotion1_Coll,itHeiho_UnkMotion4_Coll,
      //   itHeiho_UnkMotion1_Anim_inline,it_802D9168}
      // refs/melee/src/melee/it/items/itheiho.c::{it_802D98AC,it_802D98C4}
      if (state == 4u) {
        batch->state.item_direction[ii] = (batch->state.item_pos_x[ii] < 0.0f) ? -1.0f : 1.0f;
        batch->state.item_shyguy_delay[ii] = 0u;
        batch->state.item_shyguy_delay_valid[ii] = 1u;
      }
      export_vel_x = 0.0f;
      if (state == 1u && phase_before_update_valid != 0u) {
        export_vel_y =
            -yoshi_shyguy_dyn_y_pos_after_phase(params, (int)((phase_before_update + 1u) & 0xFFu));
        batch->state.item_shyguy_dyn_y_phase[ii] = (uint8_t)((phase_before_update + 1u) & 0xFFu);
        batch->state.item_shyguy_dyn_y_phase_valid[ii] = 1u;
        batch->state.item_shyguy_prev_vel_y[ii] = batch->state.item_vel_y[ii];
        batch->state.item_shyguy_prev_vel_y_valid[ii] = 1u;
      } else {
        export_vel_y = 0.0f;
        batch->state.item_shyguy_dyn_y_phase[ii] = 0u;
        batch->state.item_shyguy_dyn_y_phase_valid[ii] = 1u;
        batch->state.item_shyguy_prev_vel_y[ii] = 0.0f;
        batch->state.item_shyguy_prev_vel_y_valid[ii] = 1u;
      }
    }
    batch->state.item_vel_x[ii] = export_vel_x;
    batch->state.item_vel_y[ii] = export_vel_y;
    (void)yoshi_shyguy_try_fighter_hitbox_hit(batch, bi, it, params);
  }
  if (needs_sort != 0u) {
    items_sort(batch, bi);
  }
}

void items_update_pre_fighter_anim_phase(MslBatch* batch) {
  // Reserved item prio 0/1 phase:
  // - Item_802693E4 decrements item hitlag and consumes deferred hitlag callbacks.
  // - Item_80269528 advances item anim/script and lifetime.
  // Supported RL 1.0 item families run their admitted timer/anim owners in their explicit
  // collision-phase functions; full arbitrary item GObj priority is outside current gameplay scope.
  // refs/melee/src/melee/it/item.c::{Item_802693E4,Item_80269528}
  if (batch == NULL) {
    return;
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (items_row_has_any(batch, bi) == 0u) {
      continue;
    }
    // Held Needle self-destruction is owned by item Anim before fighter Anim/IASA can clear
    // fp->fv.sk.x4 on a same-frame Loop -> Cancel transition.
    // refs/melee/src/melee/it/items/itseakneedleheld.c::itSeakneedleheld_UnkMotion0_Anim
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialNCancel_Anim
    sheik_held_needles_update_anim_phase(batch, bi);
  }
}

void items_update_sheik_chain_accessory_phase(MslBatch* batch) {
  // Sheik Chain link motion is the article accessory callback (`fn_802BB44C`/`fn_802BB694`), not an
  // item anim timer. Run it after Sheik IASA has written the current-frame stick delta and before
  // primitive refresh consumes the `it_802BCB88` fighter HitCapsule positions.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_80110788
  // refs/melee/src/melee/it/items/itseakchain.c::{fn_802BB44C,fn_802BB694,it_802BCB88}
  if (batch == NULL) {
    return;
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (items_row_has_any(batch, bi) == 0u) {
      continue;
    }
    sheik_chain_items_update_anim_phase(batch, bi);
  }
}

void items_update_collision_phase(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (batch->state.stage_id[bi] == (uint32_t)MSL_STAGE_ID_YOSHIS_STORY &&
        batch->state.stage_yoshi_shyguy_valid[bi] != 0u) {
      yoshi_shyguy_stage_update(batch, bi);
    }
    const uint8_t row_had_items = items_row_has_any(batch, bi);
    if (row_had_items == 0u &&
        items_row_has_illusion_setphys_source(batch, bi, num_players) == 0u) {
      // With no live item GObj and no Side-B SetPhys owner, the collision item phase has no
      // supported RL 1.0 item state to advance for this row. Yoshi Shy Guy stage ownership ran
      // above only on its extracted stage source.
      // refs/melee/src/melee/it/item.c::{Item_802693E4,Item_80269528,Item_802697D4}
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFox_SpecialS_SetPhys
      continue;
    }

    if (row_had_items != 0u) {
      // Stage-owned Shy Guys are not fighter articles; run their admitted stage-item slice before
      // fighter article updates so fixed-slot compare observes the same item proc phase ordering.
      yoshi_shyguy_items_update(batch, bi);

      // Motion + collision/hit apply for Illusion/Phantasm ghost items.
      illusion_items_update_and_collide(batch, bi);

      // Fighter HitCapsule -> Sheik thrown-Needle item hurtbox damage/callback.
      sheik_needles_update_and_collide(batch, bi);

      // Sheik Vanish disappear-smoke explosion hitbox (BODY/shield), then lifetime. Collide runs
      // before the lifetime decrement so the article age == anim frame for the size-keyframe/remove
      // window.
      sheik_vanish_smoke_collide(batch, bi);
      sheik_vanish_smoke_items_update(batch, bi);

      // Motion + collision/hit apply for existing lasers.
      lasers_update_and_collide(batch, bi);
    }

    // ThrowLw stale-latch carry trim (post-collision, context-narrow):
    // - Throw-side pulses are one-shot throw_flags_b0 events consumed in ftFx_Throw_Anim.
    // - On one-step reseed around the first ThrowLw projectile pulse crossing while victim is still
    //   attached in ThrownLw, the pulse can leave a stale carried state1 laser item at t+1 even when
    //   replay ref has no item.
    // - Keep spawn/collision ownership unchanged (to preserve hitlag parity), then clear only this
    //   stale carried state1 item after collision resolution in the same frame.
    // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowLw"]["events"]
    if (row_had_items != 0u) {
      for (int p = 0; p < num_players; p++) {
        const size_t o_idx = msl_idx_player(bi, p);
        if (batch->state.action_id[o_idx] != (uint16_t)MSL_ACT_THROW_LW) {
          continue;
        }
        const uint8_t cid = batch->state.char_id[o_idx];
        const MslLaserParams* lp = laser_params_get(cid);
        if (lp == NULL || !item_type_is_falco_laser(lp->shot_itkind)) {
          continue;
        }
        const float af_cur = items_cur_anim_frame_f32(batch, o_idx);
        const int32_t prev_fp = batch->state.anim_frame_fp_q16_16[o_idx] -
                                batch->state.frame_speed_mul_fp_q16_16[o_idx];
        const float af_prev = msl_anim_frame_sanitize_f32(msl_f32_from_q16_16(prev_fp));
        int16_t crossed_pulse_af = -1;
        if (!move_tables_throw_cmd1_active(cid, (uint16_t)MSL_ACT_THROW_LW, af_cur) ||
            !move_tables_throw_crossed_projectile_pulse_frame(cid, (uint16_t)MSL_ACT_THROW_LW,
                                                              af_prev, af_cur, &crossed_pulse_af)) {
          continue;
        }
        int16_t first_pulse_af = -1;
        if (!move_tables_throw_projectile_first_pulse_frame(cid, (uint16_t)MSL_ACT_THROW_LW,
                                                            &first_pulse_af) ||
            crossed_pulse_af != first_pulse_af) {
          continue;
        }
        uint8_t stale_context = 0u;
        for (int vp = 0; vp < num_players; vp++) {
          if (vp == p) {
            continue;
          }
          const size_t v_idx = msl_idx_player(bi, vp);
          if (batch->state.grab_owner_port[v_idx] == (uint8_t)p &&
              batch->state.action_id[v_idx] == (uint16_t)MSL_ACT_THROWN_LW &&
              batch->state.hitlag_pre_timer[v_idx] == 0u && batch->state.hitstun[v_idx] == 0u) {
            stale_context = 1u;
            break;
          }
        }
        if (!stale_context) {
          continue;
        }
        for (int it = 0; it < MSL_MAX_ITEMS; it++) {
          const size_t ii = msl_idx_item(bi, it);
          if (!batch->state.item_exists[ii]) {
            continue;
          }
          if (batch->state.item_type[ii] != lp->shot_itkind || batch->state.item_owner[ii] != p ||
              batch->state.item_state[ii] != (uint8_t)1u) {
            continue;
          }
          item_slot_clear(batch, ii);
        }
      }
    }
    // Refresh the seeded `ghostEffectPos[0..2]` gameplay lanes for the next frame.
    // Decomp: ftFox_SpecialS_SetPhys advances the live ring as
    // `ghost2 = ghost1; ghost1 = ghost0; ghost0 = cur_pos`.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::ftFox_SpecialS_SetPhys
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (!action_is_illusion_setphys(batch->state.char_id[idx], batch->state.action_id[idx])) {
        continue;
      }
      batch->state.illusion_ghost_pos2_x[idx] = batch->state.illusion_ghost_pos1_x[idx];
      batch->state.illusion_ghost_pos2_y[idx] = batch->state.illusion_ghost_pos1_y[idx];
      batch->state.illusion_ghost_pos1_x[idx] = batch->state.illusion_ghost_pos0_x[idx];
      batch->state.illusion_ghost_pos1_y[idx] = batch->state.illusion_ghost_pos0_y[idx];
      batch->state.illusion_ghost_pos0_x[idx] = batch->state.pos_x[idx];
      batch->state.illusion_ghost_pos0_y[idx] = batch->state.pos_y[idx];
    }

    if (row_had_items != 0u) {
      // Keep item ordering stable for fixed-slot comparisons.
      items_sort(batch, bi);
    }
  }
}

void items_update_post_combat(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (items_row_has_any(batch, bi) == 0u) {
      continue;
    }
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const MslLaserParams* lp = laser_params_get(batch->state.char_id[idx]);
      if (lp == NULL || lp->gun_itkind == 0u) {
        continue;
      }

      const int gun_slot = items_find_gun_slot(batch, bi, p, lp->gun_itkind);
      if (gun_slot < 0) {
        continue;
      }

      const uint16_t action_id_u16 = batch->state.action_id[idx];
      const uint16_t prev_action_id_u16 = batch->state.prev_action_id[idx];
      const uint8_t prev_requires_gun =
          (blaster_gun_state_from_action_id(prev_action_id_u16) != 9u) ? 1u : 0u;
      const uint8_t cur_requires_gun =
          (blaster_gun_state_from_action_id(action_id_u16) != 9u) ? 1u : 0u;
      const uint8_t same_frame_spawned_requires_gun =
          batch->state.blaster_gun_spawned_this_frame[idx] ? 1u : 0u;

      if ((!prev_requires_gun && !same_frame_spawned_requires_gun) || cur_requires_gun) {
        continue;
      }
      const uint8_t is_damage_exit = items_action_is_damage_family(action_id_u16);
      const uint8_t is_dead_exit = items_action_is_dead_family(action_id_u16);
      if (!is_damage_exit && !is_dead_exit) {
        continue;
      }
      if (is_damage_exit && batch->state.on_ground[idx] != 0u) {
        continue;
      }

      // Combat-owned blaster gun clear on same-frame damage exits:
      // - SpecialNEnd linger ownership is specific to non-combat exits where
      //   ftFx_SpecialNEnd_Anim clears the fighter pointer and item callback consumes it.
      // - On airborne Fighter_ProcessHit damage entry from SpecialAirN states, the fighter exits the
      //   blaster action family through damage callbacks (not SpecialNEnd), so this lane should not
      //   persist as a carried SpecialNEnd linger row.
      // - Dead* motion states are common non-blaster states (ftCo_MF_Dead); when a SpecialN owner
      //   crosses the blast line, the blaster item's UnkMotion8_Anim sees blaster_action==9 and
      //   clear_blaster consumes the stale gun while preserving already-spawned shots.
      // - Same-step SpecialN entry can create the attached gun in Fighter_8006A360 before
      //   Fighter_ProcessHit_8006D1EC damages the owner out of the action. That source episode is
      //   carried by blaster_gun_spawned_this_frame because frame-start prev_action_id still names
      //   the pre-SpecialN action and cannot prove the live article owner by itself.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialNEnd_Anim
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_GetBlasterAction
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_Enter
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
      //   ftFx_SpecialAirNStart_Phys,ftFx_SpecialAirNLoop_Phys,ftFx_SpecialAirNEnd_Phys}
      // refs/melee/src/melee/it/items/itfoxblaster.c::it_802AE8A8
      // refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim
      // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
      // refs/melee/src/melee/ft/ftmotionstates.c
      const size_t ii = msl_idx_item(bi, gun_slot);
      item_slot_clear(batch, ii);
    }
    items_sort(batch, bi);
  }
}

void items_spawn_fighter_anim_phase(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Ensure the blaster "gun" item (ItKind 74/75) exists and stays linked to its fighter.
  // This stabilizes item ordering/keys when lasers coexist with the gun.
  //
  // Decomp-first trail:
  // - Spawn: refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialN_Enter calls it_802AE8A8
  // - Item spawn/attach: refs/melee/src/melee/it/items/itfoxblaster.c::it_802AE8A8 (Item_8026AB54 attach)
  // - Per-frame: refs/melee/src/melee/it/items/itfoxblaster.c::itFoxblaster_UnkMotion8_Anim
  // - Slippi fields: refs/slippi-ssbm-asm/Recording/SendItemInfo.s (state=0x24, instance_id=0xDA8)
  //
  // Spawn lasers from fighter loop scripts (cmd_var[2] pulses).
  // Decomp: ftFx_SpecialNLoop_Anim / ftFx_SpecialAirNLoop_Anim check fp->cmd_vars[2] and spawn via
  // it_8029C6A4.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim}
  // Source: data/scripts/{fox,falco}.bin (MSLFTSC1) set_cmd_var(idx=2,value=1), cached by
  // move_tables.
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t row_had_items = items_row_has_any(batch, bi);
    if (row_had_items == 0u &&
        items_row_has_fighter_anim_article_source(batch, bi, num_players) == 0u) {
      // Source item work in this phase is either an existing item GObj callback or a fighter Anim
      // callback that can spawn/own Fox/Falco articles (blaster gun/shot, throw shot, illusion
      // ghost). Rows with no live items and no article-owning motion state have no item GObj to
      // tick and no ftFx_Special{N,S}/Throw article spawn callback to consume.
      // refs/melee/src/melee/it/item.c::Item_8026862C
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
      //   ftFx_SpecialN_Enter,ftFx_SpecialNLoop_Anim,ftFx_Throw_Anim}
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
      //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
      continue;
    }

    // Stack-local per-step/per-batch-row scratch: reset once each row iteration.
    // This does not persist in SoA state across frames/reseed.
    uint8_t gun_spawned_this_frame[MSL_MAX_PLAYERS] = {0};
    uint8_t throw_seed_shot_count[MSL_MAX_PLAYERS] = {0u};

    // Seed/rollout-visible live throw-shot count (pre-spawn):
    // - ftFx_Throw_Anim consumes at most one throw_flags_b0 pulse per Anim callback.
    // - Existing state1 throw shots represent already-emitted command ordinals for several
    //   replay/rollout bridge paths below.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
    if (row_had_items != 0u) {
      for (int p = 0; p < num_players; p++) {
        const size_t p_idx = msl_idx_player(bi, p);
        const MslLaserParams* p_lp = laser_params_get(batch->state.char_id[p_idx]);
        if (p_lp == NULL || p_lp->shot_itkind == 0u) {
          continue;
        }
        for (int it = 0; it < MSL_MAX_ITEMS; it++) {
          const size_t ii = msl_idx_item(bi, it);
          if (!batch->state.item_exists[ii] || batch->state.item_owner[ii] != (int8_t)p ||
              batch->state.item_type[ii] != p_lp->shot_itkind ||
              batch->state.item_state[ii] != (uint8_t)1u) {
            continue;
          }
          if (throw_seed_shot_count[p] < 0xFFu) {
            throw_seed_shot_count[p]++;
          }
        }
      }
    }

    // Spawn Sheik thrown Needle articles from SpecialN fighter callbacks.
    // Decomp ownership:
    // - Loop_Anim owns the hidden `fv.sk.x0` charge count; replay reseed restores that count.
    // - End_Anim / AirEnd_Anim arm `mv.sk.specialn.x4`; accessory4_cb shootNeedles consumes the
    //   latch and spawns one thrown Needle through it_802AFD8C.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
    //   ftSk_SpecialNLoop_Anim,ftSk_SpecialAirNLoop_Anim,ftSk_SpecialNEnd_Anim,
    //   ftSk_SpecialAirNEnd_Anim,shootNeedles}
    // refs/melee/src/melee/it/items/itseakneedlethrown.c
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag_started_frame[idx] != 0u) {
        continue;
      }
      sheik_needle_spawn_thrown_article_from_fighter(batch, bi, p);
    }

    // Spawn Illusion/Phantasm ghost article from side-special cmd_var[2] pulse.
    // Decomp ownership:
    // - ftFx_SpecialS_Anim / ftFx_SpecialAirS_Anim call ftFox_SpecialS_CreateGhostItem.
    // - ftFox_SpecialS_CreateGhostItem spawns via it_8029CEB4 when cmd_vars[2]==1.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t action_id_u16 = batch->state.action_id[idx];
      const uint16_t prev_action_id_u16 = batch->state.prev_action_id[idx];
      // Hitlag-start exception for same-step main->end side-special ghost spawn ownership:
      // - Ghost spawn is driven from main anim callbacks (ftFx_SpecialS_Anim / AirS_Anim).
      // - Under one-step ordering, main->end transition can occur before this spawn pass; preserve
      //   the spawn bridge on end-entry even when `hitlag_started_frame` is set, but only when the
      //   previous action-frame already entered the cmd_var[2] pulse-crossing window.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
      //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
      //   ftFx_SpecialSEnd_Anim,ftFx_SpecialAirSEnd_Anim}
      uint8_t illusion_end_entry_bridge = 0u;
      if (batch->state.action_frame[idx] == 0 &&
          ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], prev_action_id_u16) ==
                (uint8_t)MSL_FX_KIND_SPECIAL_S &&
            msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id_u16) ==
                (uint8_t)MSL_FX_KIND_SPECIAL_S_END) ||
           (msl_motion_state_fx_special_kind(batch->state.char_id[idx], prev_action_id_u16) ==
                (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S &&
            msl_motion_state_fx_special_kind(batch->state.char_id[idx], action_id_u16) ==
                (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END))) {
        uint16_t prev_msid = 0u;
        int16_t first_pulse = -1;
        if (illusion_prev_main_msid_for_action(batch->state.char_id[idx], prev_action_id_u16,
                                               &prev_msid) &&
            move_tables_special_cmd2_first_pulse_frame(batch->state.char_id[idx], prev_msid,
                                                       &first_pulse) &&
            batch->state.prev_action_frame[idx] >= (int16_t)(first_pulse - 1)) {
          illusion_end_entry_bridge = 1u;
        }
      }
      if (batch->state.hitlag_started_frame[idx] != 0u && !illusion_end_entry_bridge) {
        continue;
      }
      illusion_spawn_from_fighter(batch, bi, p);
    }

    // Update gun items first so laser spawns can inherit the correct instance_id/spawn_id ordering.
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint8_t cid = batch->state.char_id[idx];
      const MslLaserParams* lp = laser_params_get(cid);
      if (lp == NULL || lp->gun_itkind == 0) {
        continue;
      }
      gun_spawned_this_frame[p] = blaster_gun_update_from_fighter(batch, bi, p, lp);
      batch->state.blaster_gun_spawned_this_frame[idx] = gun_spawned_this_frame[p] ? 1u : 0u;
    }

    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag_started_frame[idx] != 0) {
        continue;
      }
      const uint8_t cid = batch->state.char_id[idx];
      const MslLaserParams* lp = laser_params_get(cid);
      if (lp == NULL) {
        continue;
      }
      const uint32_t anim_u32 = batch->state.animation_index[idx];
      if (anim_u32 > 0xFFFFu) {
        continue;
      }
      const uint16_t msid = (uint16_t)anim_u32;
      const float af = items_cur_anim_frame_f32(batch, idx);
      const int32_t prev_af_fp =
          batch->state.anim_frame_fp_q16_16[idx] - batch->state.frame_speed_mul_fp_q16_16[idx];
      const float af_prev = msl_anim_frame_sanitize_f32(msl_f32_from_q16_16(prev_af_fp));
      // Blaster shot creation is owned by the motion-script set_cmd_var(2) command consumed in the
      // Anim callback, so spawn on the extracted command-frame crossing rather than on any later
      // frame whose floored time still equals the event.
      // Source/data:
      // - refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
      //   ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim}
      // - data/scripts/{fox,falco}.bin (MSLFTSC1) set_cmd_var idx=2 events, cached by move_tables.
      uint8_t should_shoot = laser_should_shoot_between_frames(lp, cid, msid, af_prev, af);
      uint8_t shoot_spawn_state = 0u;
      uint8_t shoot_apply_motion_step = 0u;
      uint8_t shoot_throw_lw_late_pulse_transn_y = 0u;
      int shoot_seed_hitlist_victim = -1;
      uint16_t shoot_seed_hitlist_victim_iid = 0u;
      uint8_t shoot_seed_hitlist_mask = 0u;
      // Throw-side blaster shots are driven by throw_flags_b0 pulses consumed in ftFx_Throw_Anim,
      // not by SpecialN loop cmd_vars[2].
      // refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
      const uint16_t action_id = batch->state.action_id[idx];
      const uint8_t is_blaster_throw = action_is_blaster_throw(action_id);
      if (is_blaster_throw) {
        // ftFx_Throw_Anim case 1 returns immediately when the gun pointer is NULL and must spawn,
        // so throw-side shots are only possible when a gun already exists before the shoot branch.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        if (gun_spawned_this_frame[p]) {
          continue;
        }
        if (items_find_gun_slot(batch, bi, p, lp->gun_itkind) < 0) {
          continue;
        }
        // Throw actions do not use the SpecialN loop cmd_var[2] pulse lane. ftAction_80071974 emits
        // `throw_flags_b0` from set_throw_spawn_projectile script commands, and ftFx_Throw_Anim
        // consumes that one-shot pulse before calling the throw-side it_8029C6CC spawn path. Start
        // from no-shot for every throw and let only the command/crossing owners below re-enable it.
        // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
        // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowB"/"ftCo_SM_ThrowHi"/"ftCo_SM_ThrowLw"].events
        should_shoot = 0u;
      }
      uint8_t throw_command_authoritative = 0u;
      if (is_blaster_throw && batch->state.throw_command_pending_seed_valid[idx] != 0u) {
        // Throw actions do not use the SpecialN loop shoot table directly. Start from the explicit
        // command lane and fall back to legacy frame-crossing reconstruction only for pulse phases
        // whose consume/hitlist owner is not fully represented by `throw_command_pending_pulse_frame`.
        should_shoot = 0u;
        const uint8_t pending_pulse_af = batch->state.throw_command_pending_pulse_frame[idx];
        uint8_t pending_pulse_ordinal = 0u;
        if (pending_pulse_af != 0u && batch->state.hitlag_pre_timer[idx] == 0u &&
            move_tables_throw_projectile_pulse_ordinal(cid, action_id, (int16_t)pending_pulse_af,
                                                       &pending_pulse_ordinal)) {
          // Command-pending throw article authority:
          // - ftAction_80073354 owns command-timer advancement and can execute only one
          //   `set_throw_spawn_projectile` command into the bool `throw_flags_b0` consumed by
          //   ftFx_Throw_Anim in this callback.
          // - `throw_command_pending_pulse_frame` is the prefix-causal seed lane for that command.
          // - Existing live state1 throw shots can already represent earlier command ordinals; emit
          //   the pending command only when the live count is still below this pulse ordinal.
          // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
          // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
          // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowB"/"ftCo_SM_ThrowHi"/"ftCo_SM_ThrowLw"].events
          uint8_t suppress_pending_article = 0u;
          if (action_id == (uint16_t)MSL_ACT_THROW_B) {
            int stale_victim_p = -1;
            int16_t first_throwb_pulse_af = -1;
            const uint8_t is_first_throwb_pending =
                (move_tables_throw_projectile_first_pulse_frame(cid, action_id,
                                                                &first_throwb_pulse_af) &&
                 (int16_t)pending_pulse_af == first_throwb_pulse_af)
                    ? 1u
                    : 0u;
            if (is_first_throwb_pending && batch->state.throw_pulse_consumed[idx] != 0u) {
              // ThrowB consumed first-command latch:
              // The pending lane identifies the source command cursor, while the consumed lane
              // says ftFx_Throw_Anim already consumed the one-shot throw_flags_b0 pulse before this
              // seed. If no live shot remains, do not respawn the article or advance combo from the
              // command flag alone.
              // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
              suppress_pending_article = 1u;
              throw_command_authoritative = 1u;
            }
            for (int vp = 0; vp < num_players; vp++) {
              if (vp == p) {
                continue;
              }
              const size_t v_idx = msl_idx_player(bi, vp);
              if (batch->state.hitstun[v_idx] == 0u ||
                  !msl_damage_source_victim_port_matches_attacker(batch, v_idx, idx, p)) {
                continue;
              }
              if (batch->state.throw_pulse_consumed[idx] == 0u &&
                  batch->state.last_attack_landed[v_idx] != (uint8_t)lp->shot_itkind) {
                continue;
              }
              if (stale_victim_p >= 0) {
                stale_victim_p = -1;
                break;
              }
              stale_victim_p = vp;
            }
            if (stale_victim_p >= 0 && throw_seed_shot_count[p] != 0u) {
              // ThrowB pending command / stale victim-ring split:
              // - The command lane proves a throw_flags_b0 pulse is pending, but the unique victim is
              //   already in same-owner throw-laser hitstun and a live throw-side shot already
              //   represents this callback phase. In this path Melee carries item-domain combo
              //   bookkeeping without emitting another live article. When no shot is live, keep the
              //   command path authoritative and spawn; ftFx_Throw_Anim still creates the article and
              //   it_8029C4D4 owns the immediate BODY consume/hit decision.
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
              // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
              if (is_first_throwb_pending && batch->state.throw_pulse_consumed[idx] == 0u) {
                const size_t v_idx = msl_idx_player(bi, stale_victim_p);
                const uint8_t attack_id_u8 = (uint8_t)batch->state.attack_id[idx];
                const uint8_t cur_victim = batch->state.combo_victim_port[idx];
                if ((cur_victim == 0xFFu || cur_victim == (uint8_t)stale_victim_p) &&
                    batch->state.attack_id[idx] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
                    batch->state.last_attack_landed[idx] == attack_id_u8 &&
                    batch->state.combo_count[idx] != 0u) {
                  batch->state.combo_count[idx] = (uint8_t)(batch->state.combo_count[idx] + 1u);
                  if (cur_victim == 0xFFu) {
                    batch->state.combo_victim_port[idx] = (uint8_t)stale_victim_p;
                  }
                  batch->state.combo_victim_instance_id[idx] = batch->state.instance_id[v_idx];
                }
              }
              suppress_pending_article = 1u;
              throw_command_authoritative = 1u;
            }
            if (!suppress_pending_article && is_first_throwb_pending &&
                throw_seed_shot_count[p] != 0u && item_type_is_fox_laser(lp->shot_itkind)) {
              const int callback_victim = throw_laser_unique_same_source_victim(batch, bi, p);
              if (callback_victim >= 0) {
                // ThrowB first-command callback consume:
                // - The command lane proves the first ThrowB projectile pulse is pending, but the
                //   unique victim is already in the same throw-laser callback/source phase.
                // - In this Fox startup shape, Melee advances item-domain combo bookkeeping through
                //   ftColl_8007646C / ftColl_800763C0 without leaving a live state1 article.
                // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_Throw_Anim
                // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
                // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
                // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
                throw_laser_advance_combo_bookkeeping(batch, bi, p, callback_victim);
                suppress_pending_article = 1u;
                throw_command_authoritative = 1u;
              }
            }
          }
          if (action_id == (uint16_t)MSL_ACT_THROW_HI && cid == (uint8_t)MSL_CHAR_ID_FALCO &&
              pending_pulse_af == 24u && throw_seed_shot_count[p] >= 2u) {
            // Falco ThrowHi final-pulse live-article cap:
            // - The frame-24 command can be represented in the replay seed by two live state1
            //   throw-side laser articles plus the following BODY consume/carry handoff.
            // - Do not emit a third state1 article from the command-pending lane; the existing
            //   BODY path below owns the consume-vs-carry split for this victim phase.
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // data/moves/falco.json moves["ftCo_SM_ThrowHi"].events
            suppress_pending_article = 1u;
            throw_command_authoritative = 1u;
          }
          if (!suppress_pending_article && action_id == (uint16_t)MSL_ACT_THROW_HI &&
              batch->state.throw_pulse_consumed[idx] == pending_pulse_af) {
            const int callback_victim = throw_laser_unique_same_source_victim(batch, bi, p);
            if (callback_victim >= 0) {
              // ThrowHi hidden callback consume:
              // The command lane proves a set_throw_spawn_projectile pulse reached ftFx_Throw_Anim,
              // while the seed consumed lane records that the item callback/hitlist phase did not
              // leave an additional live throw-side article at post-frame. Advance combo
              // bookkeeping through the same ftColl owner used by the ThrowB callback consume and
              // suppress the replay-only article spawn.
              // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
              // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
              throw_laser_advance_combo_bookkeeping(batch, bi, p, callback_victim);
              suppress_pending_article = 1u;
              throw_command_authoritative = 1u;
            }
          }
          const uint8_t pending_throwhi_mid_pulse_needs_article =
              (!suppress_pending_article && action_id == (uint16_t)MSL_ACT_THROW_HI &&
               pending_pulse_ordinal == 2u && throw_seed_shot_count[p] == 1u &&
               batch->state.combo_count[idx] < pending_pulse_ordinal)
                  ? 1u
                  : 0u;
          const uint8_t pending_throwlw_mid_attached_spawn_callback =
              (!suppress_pending_article && action_id == (uint16_t)MSL_ACT_THROW_LW &&
               item_type_is_falco_laser(lp->shot_itkind) && pending_pulse_af == 28u &&
               throw_seed_shot_count[p] == 0u &&
               throwlw_attached_victim_for_owner(batch, bi, p) >= 0)
                  ? 1u
                  : 0u;
          uint8_t pending_throwlw_late_attached_replacement_spawn = 0u;
          if (!suppress_pending_article && action_id == (uint16_t)MSL_ACT_THROW_LW &&
              (pending_pulse_af == 28u || pending_pulse_af == 31u) &&
              throw_seed_shot_count[p] == 1u &&
              throwlw_attached_victim_for_owner(batch, bi, p) >= 0) {
            for (int it = 0; it < MSL_MAX_ITEMS; it++) {
              const size_t ii = msl_idx_item(bi, it);
              if (batch->state.item_exists[ii] != 0u && batch->state.item_owner[ii] == (int8_t)p &&
                  batch->state.item_type[ii] == lp->shot_itkind &&
                  batch->state.item_state[ii] == 1u && batch->state.item_timer[ii] <= 1.0f) {
                pending_throwlw_late_attached_replacement_spawn = 1u;
                break;
              }
            }
          }
          if (!suppress_pending_article &&
              (pending_pulse_ordinal == 1u || pending_throwhi_mid_pulse_needs_article ||
               pending_throwlw_mid_attached_spawn_callback ||
               pending_throwlw_late_attached_replacement_spawn)) {
            throw_command_authoritative = 1u;
            if (throw_seed_shot_count[p] == 0u || pending_throwhi_mid_pulse_needs_article ||
                pending_throwlw_late_attached_replacement_spawn) {
              should_shoot = 1u;
              shoot_spawn_state = 1u;
              batch->state.throw_pulse_crossed_curr_frame[idx] = pending_pulse_af;
              if (pending_throwhi_mid_pulse_needs_article) {
                // ThrowHi frame-20 command authority:
                // - ftAction_80071974 emits one throw_flags_b0 pulse per
                //   set_throw_spawn_projectile command; ftFx_Throw_Anim consumes exactly one pending
                //   bool and spawns the state1 laser through it_8029C6CC.
                // - Replayed seed can already contain the first state1 throw shot. Emit the second
                //   command only while item-domain combo bookkeeping has not advanced past that
                //   ordinal; otherwise primary frame-20/24 carry rows have already represented the
                //   pulse through the item BODY hitlist/ftColl_8007646C owner.
                // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
                // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
                // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events
              }
              int16_t first_pulse_af = -1;
              const uint8_t has_first_pulse =
                  move_tables_throw_projectile_first_pulse_frame(cid, action_id, &first_pulse_af);
              if (action_id == (uint16_t)MSL_ACT_THROW_LW && has_first_pulse &&
                  (int16_t)pending_pulse_af >= first_pulse_af) {
                if ((int16_t)pending_pulse_af == first_pulse_af) {
                  shoot_apply_motion_step = 1u;
                } else {
                  shoot_throw_lw_late_pulse_transn_y = 1u;
                }
              }
              if (pending_throwlw_late_attached_replacement_spawn) {
                const int attached_victim = throwlw_attached_victim_for_owner(batch, bi, p);
                if (attached_victim >= 0) {
                  // ThrowLw late attached replacement spawn:
                  // - The command lane proves the frame-28/31 throw_flags_b0 pulse is pending.
                  // - Fox ThrowLw false-clear event rows seed an expiring state1 article
                  //   (`item_timer <= 1`) while vanilla refreshes the throw-side state1 article in
                  //   the same command/callback phase.
                  // - Seed hb0/1 for the attached victim: v10 dumps show Fox state1 throw-laser BODY
                  //   lanes are hitboxes 0/1 here, and it_8026FA2C / lbColl_80008688 suppress per
                  //   HitCapsule rather than item-wide.
                  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
                  // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
                  shoot_seed_hitlist_victim = attached_victim;
                  shoot_seed_hitlist_victim_iid =
                      batch->state.instance_id[msl_idx_player(bi, attached_victim)];
                  shoot_seed_hitlist_mask = 0x03u;
                  shoot_throw_lw_late_pulse_transn_y = 1u;
                }
              }
            } else {
              should_shoot = 0u;
            }
          } else if (suppress_pending_article) {
            should_shoot = 0u;
          }
        }
      }
      if (is_blaster_throw && throw_command_authoritative && !should_shoot) {
        continue;
      }
      if (!should_shoot && is_blaster_throw) {
        // Seed-bridge guard for one-shot throw_flags_b0 reconstruction:
        // - Throw projectile pulses are script-time one-shot flags (set by ftAction_80071974 and
        //   consumed by ftFx_Throw_Anim).
        // - On one-step reseed in the middle of a throw/hitlag exchange, the consumed-latch is not
        //   exposed by Slippi. Reconstructing pulses purely from anim-frame crossing can re-emit a
        //   pulse while the fighter was already in hitlag at frame start.
        // - Gate pulse reconstruction on "no pre-timer hitlag this frame" to avoid that stale-latch
        //   replay artifact.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80071974
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        if (batch->state.hitlag_pre_timer[idx] != 0u) {
          continue;
        }
        // Seed-owned ThrowHi pulse reconstruction bridge:
        // - Seed derivation marks the one-step throw_flags_b0 pulse ownership window.
        // - For ongoing throw-damage contexts attributed to this thrower, emit the throw-side shot
        //   directly from seed ownership rather than relying only on anim-frame pulse crossing.
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by / hitstun lanes)
        if (action_id == (uint16_t)MSL_ACT_THROW_HI &&
            batch->state.throw_pulse_consumed[idx] != 0u) {
          uint8_t ongoing_throwhi_context = 0u;
          // Command-cursor shot count guard:
          // - ftAction_80071974 emits one throw_flags_b0 pulse per set_throw_spawn_projectile
          //   command, and ftFx_Throw_Anim consumes at most one pulse per Anim callback.
          // - When reseed already carries both frame-18 and frame-20 state1 ThrowHi articles,
          //   the consumed frame-20 seed lane represents already-owned command state; do not emit
          //   a third article from the same pulse.
          // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
          // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events
          uint8_t max_consumed_pulse_shots = 1u;
          if (batch->state.throw_pulse_crossed_prev_frame[idx] >=
              (uint8_t)MSL_THROWHI_PULSE_MID_AF) {
            max_consumed_pulse_shots = 2u;
          }
          for (int vp = 0; vp < num_players; vp++) {
            if (vp == p) {
              continue;
            }
            const size_t v_idx = msl_idx_player(bi, vp);
            if (batch->state.hitstun[v_idx] > 0u &&
                msl_damage_source_victim_port_matches_attacker(batch, v_idx, idx, p)) {
              ongoing_throwhi_context = 1u;
              break;
            }
          }
          if (ongoing_throwhi_context && throw_seed_shot_count[p] < max_consumed_pulse_shots) {
            should_shoot = 1u;
            // Throw-side spawn path in ftFx_Throw_Anim uses it_8029C6CC (msid=1).
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            shoot_spawn_state = 1u;
          }
        }
        if (!should_shoot) {
          if (action_id == (uint16_t)MSL_ACT_THROW_HI && cid == (uint8_t)MSL_CHAR_ID_FALCO &&
              item_type_is_falco_laser(lp->shot_itkind) &&
              batch->state.throw_pulse_crossed_prev_frame[idx] ==
                  (uint8_t)MSL_THROWHI_PREV_PHASE_AF &&
              throw_seed_shot_count[p] == 1u &&
              batch->state.throw_command_deferred_pulse_frame[idx] == 0u &&
              item_throwhi_prev18_second_article_rate_source_step(
                  batch->state.frame_speed_mul_fp_q16_16[idx])) {
            uint8_t falco_prev18_second_article = 0u;
            for (int vp = 0; vp < num_players; vp++) {
              if (vp == p) {
                continue;
              }
              const size_t v_idx = msl_idx_player(bi, vp);
              if (batch->state.hitstun[v_idx] > 0u &&
                  msl_damage_source_victim_matches_attacker(batch, v_idx, idx, p) &&
                  batch->state.last_attack_landed[idx] == (uint8_t)lp->shot_itkind) {
                falco_prev18_second_article = 1u;
                break;
              }
            }
            if (falco_prev18_second_article) {
              // Falco ThrowHi crossed-prev frame-18 second article:
              // - Event probes for PRH:6737 show a replay seed with one live state1 Falco throw
              //   laser and `throw_pulse_crossed_prev_frame==18`; vanilla emits another
              //   it_8029C6CC spawn request in the next frame and carries it through post-frame.
              // - Keep this on the source-owned command phase by requiring the first-pulse
              //   crossed-prev lane, the 1.25x current-callback throw step, exactly one live state1
              //   throw shot, and same-source victim provenance. Slower rows can still expose
              //   crossed-prev frame 18 while source has not reached the frame-20 command; the
              //   explicit pending/deferred cursor owners handle those later source steps. The
              //   per-hitbox BODY path below remains responsible for immediate hb0 destroy rows.
              // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
              // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
              // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
              // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
              should_shoot = 1u;
              shoot_spawn_state = 1u;
            }
          }
        }
        if (!should_shoot) {
          if (action_id == (uint16_t)MSL_ACT_THROW_HI &&
              batch->state.throw_pulse_crossed_prev_frame[idx] == 20u &&
              (throw_seed_shot_count[p] == 0u ||
               (batch->state.throw_command_deferred_pulse_frame[idx] == 20u &&
                batch->state.throw_command_pending_seed_valid[idx] == 0u &&
                throw_seed_shot_count[p] < 2u))) {
            // ThrowHi crossed-prev command pulse reconstruction:
            // - ftAction_80073354 can execute the frame-20 set_throw_spawn_projectile command and
            //   ftFx_Throw_Anim consumes throw_flags_b0 in the source frame before the next
            //   teacher-forced seed. Slippi does not expose that command cursor/consumed latch.
            // - The prefix-causal `throw_pulse_crossed_prev_frame` lane records that frame-20
            //   command crossing. Runtime deferred-cursor rows additionally keep
            //   `throw_command_deferred_pulse_frame==20`, so ordinary frame-crossing provenance does
            //   not broaden into another one-live-shot article spawn. One-step rows with explicit
            //   command cursor state stay on `throw_command_pending_pulse_frame`.
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events
            should_shoot = 1u;
            shoot_spawn_state = 1u;
            if (throw_seed_shot_count[p] != 0u) {
              batch->state.throw_pulse_crossed_curr_frame[idx] = 20u;
              batch->state.throw_command_deferred_pulse_frame[idx] = 0u;
            }
          }
        }
        if (!should_shoot) {
          const int32_t prev_fp =
              batch->state.anim_frame_fp_q16_16[idx] - batch->state.frame_speed_mul_fp_q16_16[idx];
          const float af_prev = msl_anim_frame_sanitize_f32(msl_f32_from_q16_16(prev_fp));
          int16_t crossed_pulse_af = -1;
          if (move_tables_throw_cmd1_active(cid, action_id, af) &&
              move_tables_throw_crossed_projectile_pulse_frame(cid, action_id, af_prev, af,
                                                               &crossed_pulse_af)) {
            int16_t first_pulse_af = -1;
            const uint8_t has_first_pulse =
                move_tables_throw_projectile_first_pulse_frame(cid, action_id, &first_pulse_af);
            const uint16_t prev_frame_i = msl_anim_frame_floor_u16(af_prev);
            // Throw-side stale-latch suppressors (context-owned, non-record-keyed):
            // - Throw pulse flags are one-shot script events (`throw_flags_b0`) owned by the command
            //   timeline and consumed by ftFx_Throw_Anim.
            // - Under teacher-forced reseed, command cursor ownership is not seeded; in specific
            //   attached/ongoing-damage contexts, pure frame-crossing can replay a stale pulse that
            //   does not exist at t+1.
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
            // data/moves/{fox,falco}.json set_throw_spawn_projectile pulse frames
            uint8_t stale_throw_pulse_context = 0u;
            int stale_throw_pulse_victim = -1;
            for (int vp = 0; vp < num_players; vp++) {
              if (vp == p) {
                continue;
              }
              const size_t v_idx = msl_idx_player(bi, vp);
              // ThrowB stale-latch context:
              // - defender already in ongoing hitstun from this same projectile kind.
              if (action_id == (uint16_t)MSL_ACT_THROW_B && throw_seed_shot_count[p] != 0u &&
                  batch->state.hitstun[v_idx] > 0u &&
                  batch->state.last_attack_landed[v_idx] == lp->shot_itkind) {
                stale_throw_pulse_context = 1u;
                if (stale_throw_pulse_victim >= 0) {
                  stale_throw_pulse_victim = -1;
                  break;
                }
                stale_throw_pulse_victim = vp;
              }
            }
            if (stale_throw_pulse_context) {
              if (action_id == (uint16_t)MSL_ACT_THROW_B && stale_throw_pulse_victim >= 0) {
                // ThrowB suppressed-pulse combo bookkeeping bridge:
                // - Throw-side projectile pulses are one-shot script events consumed in
                //   ftFx_Throw_Anim, and confirmed item hits route combo tracking through the item
                //   domain variant ftColl_8007646C -> ftColl_800763C0.
                // - When a stale pulse is suppressed because the victim is already in ongoing
                //   throw-laser hitstun from this owner, keep the attacker-side combo bookkeeping
                //   synchronized for the uniquely-owned victim without broadening projectile life.
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
                const size_t v_idx = msl_idx_player(bi, stale_throw_pulse_victim);
                const uint8_t attack_id_u8 = (uint8_t)batch->state.attack_id[idx];
                const uint8_t cur_victim = batch->state.combo_victim_port[idx];
                if ((cur_victim == 0xFFu || cur_victim == (uint8_t)stale_throw_pulse_victim) &&
                    batch->state.attack_id[idx] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
                    batch->state.last_attack_landed[idx] == attack_id_u8 &&
                    batch->state.combo_count[idx] != 0u) {
                  batch->state.combo_count[idx] = (uint8_t)(batch->state.combo_count[idx] + 1u);
                  if (cur_victim == 0xFFu) {
                    batch->state.combo_victim_port[idx] = (uint8_t)stale_throw_pulse_victim;
                  }
                  batch->state.combo_victim_instance_id[idx] = batch->state.instance_id[v_idx];
                }
              }
              continue;
            }
            // Seed-owned ThrowB stale pulse suppressor:
            // - `throw_pulse_consumed` bridges throw_flags_b0 pulse ownership from seed derivation.
            // - Keep suppression narrow to ongoing-hitstun contexts attributed to this thrower.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by / hitstun lanes)
            if (action_id == (uint16_t)MSL_ACT_THROW_B &&
                batch->state.throw_pulse_consumed[idx] != 0u) {
              uint8_t ongoing_throwb_context = 0u;
              for (int vp = 0; vp < num_players; vp++) {
                if (vp == p) {
                  continue;
                }
                const size_t v_idx = msl_idx_player(bi, vp);
                if (batch->state.hitstun[v_idx] > 0u &&
                    msl_damage_source_victim_port_matches_attacker(batch, v_idx, idx, p)) {
                  ongoing_throwb_context = 1u;
                  break;
                }
              }
              if (ongoing_throwb_context) {
                continue;
              }
            }
            // ThrowB non-terminal stale-crossing suppressor (hitstun-window gated):
            // - ThrowB projectile pulses are script-owned one-shots (15/18/21 in move data) consumed
            //   in ftFx_Throw_Anim.
            // - Under one-step reseed, command consume-latch ownership is absent; in late ongoing
            //   throw-hitstun windows, non-terminal frame-crossing can replay a stale pulse.
            // - Gate suppression to non-terminal pulses and only when victim hitstun has already decayed
            //   below the script-owned pulse-window threshold.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (hitstun / last_hit_by lanes)
            // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowB"]["events"]
            if (action_id == (uint16_t)MSL_ACT_THROW_B &&
                batch->state.throw_pulse_consumed[idx] == 0u &&
                batch->state.throw_pulse_crossed_prev_frame[idx] == 0u) {
              int16_t throwb_last_pulse_af = 0;
              if (move_tables_throw_projectile_last_pulse_frame(cid, action_id,
                                                                &throwb_last_pulse_af) &&
                  crossed_pulse_af >= 0 && crossed_pulse_af < throwb_last_pulse_af) {
                const uint16_t stale_hitstun_thresh =
                    (uint16_t)(crossed_pulse_af + (int16_t)MSL_THROWB_PULSE_START_AF);
                uint8_t stale_throwb_context = 0u;
                int stale_throwb_victim = -1;
                for (int vp = 0; vp < num_players; vp++) {
                  if (vp == p) {
                    continue;
                  }
                  const size_t v_idx = msl_idx_player(bi, vp);
                  if (batch->state.hitstun[v_idx] > 0u &&
                      msl_damage_source_victim_port_matches_attacker(batch, v_idx, idx, p) &&
                      throw_seed_shot_count[p] != 0u &&
                      batch->state.last_attack_landed[v_idx] == (uint8_t)lp->shot_itkind &&
                      batch->state.hitstun[v_idx] < stale_hitstun_thresh) {
                    stale_throwb_context = 1u;
                    if (stale_throwb_victim >= 0) {
                      stale_throwb_victim = -1;
                      break;
                    }
                    stale_throwb_victim = vp;
                  }
                }
                if (stale_throwb_context) {
                  if (stale_throwb_victim >= 0) {
                    // ThrowB non-terminal stale-crossing bookkeeping bridge:
                    // - This branch already suppresses replay-false non-terminal throw pulses in the
                    //   late ongoing-hitstun window owned by ftFx_Throw_Anim's one-shot pulse
                    //   script.
                    // - When that suppressed pulse belongs to a unique same-owner victim, Melee
                    //   still advances attacker-side item-domain combo bookkeeping
                    //   (ftColl_8007646C -> ftColl_800763C0) without a fresh hitlag/hitstun event.
                    // - Keep the bridge inside the existing hitstun-threshold suppressor only.
                    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
                    const size_t v_idx = msl_idx_player(bi, stale_throwb_victim);
                    const uint8_t attack_id_u8 = (uint8_t)batch->state.attack_id[idx];
                    const uint8_t cur_victim = batch->state.combo_victim_port[idx];
                    if ((cur_victim == 0xFFu || cur_victim == (uint8_t)stale_throwb_victim) &&
                        batch->state.attack_id[idx] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
                        batch->state.last_attack_landed[idx] == attack_id_u8 &&
                        batch->state.combo_count[idx] != 0u) {
                      batch->state.combo_count[idx] = (uint8_t)(batch->state.combo_count[idx] + 1u);
                      if (cur_victim == 0xFFu) {
                        batch->state.combo_victim_port[idx] = (uint8_t)stale_throwb_victim;
                      }
                      batch->state.combo_victim_instance_id[idx] = batch->state.instance_id[v_idx];
                    }
                  }
                  continue;
                }
              }
            }
            if (action_id == (uint16_t)MSL_ACT_THROW_B) {
              int16_t throwb_last_pulse_af = 0;
              if (move_tables_throw_projectile_last_pulse_frame(cid, action_id,
                                                                &throwb_last_pulse_af) &&
                  crossed_pulse_af == throwb_last_pulse_af) {
                const int callback_victim = throw_laser_unique_same_source_victim(batch, bi, p);
                uint8_t terminal_callback_phase = 0u;
                if (callback_victim >= 0) {
                  const size_t v_idx = msl_idx_player(bi, callback_victim);
                  terminal_callback_phase =
                      (batch->state.hitlag[v_idx] > 0u && batch->state.hitlag[v_idx] <= 2u)
                          ? 1u
                          : ((batch->state.hitlag[v_idx] == 0u &&
                              batch->state.last_attack_landed[v_idx] == (uint8_t)lp->shot_itkind &&
                              batch->state.action_frame[v_idx] >= 11)
                                 ? 1u
                                 : 0u);
                }
                if (callback_victim >= 0 && terminal_callback_phase) {
                  // ThrowB terminal-pulse callback consume:
                  // - The final ThrowB projectile command is still a one-shot throw_flags_b0 pulse
                  //   owned by ftAction/ftFx_Throw_Anim.
                  // - In terminal ongoing-hitstun rows, item BODY callback/source bookkeeping is
                  //   represented without a live article at t+1. Suppress replaying the terminal
                  //   article, but still let the hidden state1 BODY producer top off the live
                  //   Fighter_ProcessHit percent/hitlag window when source victim identity matches.
                  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
                  // refs/melee/src/melee/ft/chara/ftFox/ftFx_Throw_Anim
                  // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
                  // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
                  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
                  const MslItemHitResult hidden_res = throw_laser_apply_hidden_state1_body_topoff(
                      batch, bi, p, callback_victim, lp);
                  if (hidden_res == MSL_ITEM_HIT_NONE) {
                    throw_laser_advance_combo_bookkeeping(batch, bi, p, callback_victim);
                  }
                  continue;
                }
              }
            }
            // Seed-bridge stale-latch suppressors for throw projectile pulses:
            // - Throw script pulses are one-shot `throw_flags_b0` events consumed in ftFx_Throw_Anim.
            // - With one-step reseed, command-timer/cursor ownership is not seeded; reconstructing by
            //   raw frame crossing can re-emit specific startup/mid pulse windows that were already
            //   consumed in the source frame.
            // - Keep suppression scoped to the observed pulse windows and action-frame phases that are
            //   decomp-owned by ftAction command timing.
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // data/moves/{fox,falco}.json set_throw_spawn_projectile pulse frames
            const int throwb_startup_carry_victim =
                (action_id == (uint16_t)MSL_ACT_THROW_B && cid == (uint8_t)MSL_CHAR_ID_FALCO &&
                 has_first_pulse && crossed_pulse_af == first_pulse_af)
                    ? throw_laser_unique_same_source_victim(batch, bi, p)
                    : -1;
            const uint8_t throwb_startup_carry =
                (throwb_startup_carry_victim >= 0 &&
                 batch->state.last_attack_landed[msl_idx_player(bi, throwb_startup_carry_victim)] !=
                     0u &&
                 batch->state.action_frame[msl_idx_player(bi, throwb_startup_carry_victim)] > 4)
                    ? 1u
                    : 0u;
            if (action_id == (uint16_t)MSL_ACT_THROW_HI &&
                crossed_pulse_af == (int16_t)MSL_THROWHI_PULSE_MID_AF &&
                batch->state.throw_pulse_crossed_prev_frame[idx] ==
                    (uint8_t)MSL_THROWHI_PREV_PHASE_AF &&
                (batch->state.throw_command_deferred_pulse_frame[idx] ==
                     (uint8_t)MSL_THROWHI_PULSE_MID_AF ||
                 item_throwhi_deferred_mid_pulse_rate_source_step(
                     batch->state.frame_speed_mul_fp_q16_16[idx])) &&
                throw_seed_shot_count[p] <= 1u) {
              uint8_t falco_prev18_cursor_carry = 0u;
              for (int vp = 0; vp < num_players; vp++) {
                if (vp == p) {
                  continue;
                }
                const size_t v_idx = msl_idx_player(bi, vp);
                if (batch->state.hitstun[v_idx] > 0u &&
                    msl_damage_source_victim_matches_attacker(batch, v_idx, idx, p)) {
                  falco_prev18_cursor_carry = 1u;
                  break;
                }
              }
              if (falco_prev18_cursor_carry != 0u) {
                // ThrowHi frame-20 cursor carry:
                // - The first frame-18 command has already advanced source/item bookkeeping: either
                //   one live state1 article remains, or the first article was consumed immediately
                //   and only same-source victim provenance / combo state remains.
                // - The seed-owned deferred cursor lane is derived from extracted ThrowHi pulse
                //   order, source frame crossing, one live state1 article, and same-source victim
                //   provenance. Runtime consumes that explicit source-step owner instead of
                //   guessing from a frame-speed threshold.
                // - Record the crossed command as current provenance without serializing the
                //   article in this callback; the frame scheduler promotes it to crossed-prev so the next
                //   runtime frame takes the same source-owned path as teacher-forced `pending=20`
                //   rows.
                // - Keep the owner on the same-source victim provenance used by the one-step
                //   crossed-prev frame-18 path; 1.25x rows and rows without prior source-proven
                //   first-pulse ownership continue to serialize through the ordinary pulse path.
                // refs/melee/src/melee/ft/ftaction.c::ftAction_80073354
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0
                // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"].events
                batch->state.throw_pulse_crossed_curr_frame[idx] = (uint8_t)crossed_pulse_af;
                batch->state.throw_command_deferred_pulse_frame[idx] = (uint8_t)crossed_pulse_af;
                continue;
              }
            }
            if (batch->state.throw_command_pending_seed_valid[idx] != 0u &&
                throw_blaster_pulse_is_seed_stale_latch(action_id, lp->shot_itkind,
                                                        crossed_pulse_af, prev_frame_i) &&
                !throwb_startup_carry) {
              if (action_id == (uint16_t)MSL_ACT_THROW_B) {
                // ThrowB startup stale-latch combo bookkeeping bridge:
                // - The first throw-side blaster pulse (frame 15) can be suppressed by the
                //   seed-stale-latch guard when one-step reseed lacks throw_flags_b0 cursor state.
                // - In Fox startup rows, that suppressed pulse still advances attacker-side combo
                //   bookkeeping when the victim remains uniquely in same-owner hitstun.
                // - Keep the bridge on this exact stale-latch suppressor path only; later ThrowB
                //   pulses remain owned by the existing hitstun-window suppressors above.
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
                int stale_victim_p = -1;
                for (int vp = 0; vp < num_players; vp++) {
                  if (vp == p) {
                    continue;
                  }
                  const size_t v_idx = msl_idx_player(bi, vp);
                  if (batch->state.hitstun[v_idx] == 0u ||
                      !msl_damage_source_victim_port_matches_attacker(batch, v_idx, idx, p)) {
                    continue;
                  }
                  if (stale_victim_p >= 0) {
                    stale_victim_p = -1;
                    break;
                  }
                  stale_victim_p = vp;
                }
                if (stale_victim_p >= 0) {
                  const size_t v_idx = msl_idx_player(bi, stale_victim_p);
                  const uint8_t attack_id_u8 = (uint8_t)batch->state.attack_id[idx];
                  const uint8_t cur_victim = batch->state.combo_victim_port[idx];
                  if ((cur_victim == 0xFFu || cur_victim == (uint8_t)stale_victim_p) &&
                      batch->state.attack_id[idx] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
                      batch->state.last_attack_landed[idx] == attack_id_u8 &&
                      batch->state.combo_count[idx] != 0u) {
                    batch->state.combo_count[idx] = (uint8_t)(batch->state.combo_count[idx] + 1u);
                    if (cur_victim == 0xFFu) {
                      batch->state.combo_victim_port[idx] = (uint8_t)stale_victim_p;
                    }
                    batch->state.combo_victim_instance_id[idx] = batch->state.instance_id[v_idx];
                  }
                }
              }
              continue;
            }
            if (crossed_pulse_af >= 0 && crossed_pulse_af <= 0xFF) {
              batch->state.throw_pulse_crossed_curr_frame[idx] = (uint8_t)crossed_pulse_af;
            }
            if (action_id == (uint16_t)MSL_ACT_THROW_HI &&
                crossed_pulse_af == (int16_t)MSL_THROWHI_PULSE_MID_AF &&
                batch->state.throw_command_pending_pulse_frame[idx] == 0u &&
                batch->state.throw_command_pending_seed_valid[idx] != 0u &&
                throw_seed_shot_count[p] == 1u && item_type_is_fox_laser(lp->shot_itkind)) {
              uint8_t same_character_first_pulse_carry = 0u;
              for (int vp = 0; vp < num_players; vp++) {
                if (vp == p) {
                  continue;
                }
                const size_t v_idx = msl_idx_player(bi, vp);
                if (batch->state.char_id[v_idx] == batch->state.char_id[idx] &&
                    batch->state.hitstun[v_idx] > 0u &&
                    msl_damage_source_victim_matches_attacker(batch, v_idx, idx, p)) {
                  same_character_first_pulse_carry = 1u;
                  break;
                }
              }
              if (same_character_first_pulse_carry) {
                // ThrowHi same-character first-pulse callback carry:
                // - In same-character mirror rows, the first state1 laser can already represent the
                //   callback/body phase when a teacher-forced seed explicitly says no frame-20
                //   command is pending. That seed-owned command-cursor lane is authoritative for
                //   this one frame; after it clears, rollout must fall back to live frame crossing
                //   so later source-owned ThrowHi pulses still serialize.
                // - Cross-character primary controls remain eligible for the frame-20 command path;
                //   those rows have different item hitcapsule/hurtcapsule callback phase evidence
                //   and are protected by replay-real locks.
                // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
                // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
                continue;
              }
            }
            should_shoot = 1u;
            // Throw-side spawn path in ftFx_Throw_Anim uses it_8029C6CC (msid=1).
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            shoot_spawn_state = 1u;
            if (action_id == (uint16_t)MSL_ACT_THROW_LW && has_first_pulse &&
                crossed_pulse_af >= first_pulse_af) {
              if (crossed_pulse_af == first_pulse_af) {
                // Common Throw-side intra-frame order:
                // - ftFx_Throw_Anim spawns the laser during the fighter Anim callback.
                // - the newly spawned shot can then consume its item motion callback later in the
                //   same frame before attached-victim collision is evaluated.
                // - Keep this on the first ThrowLw pulse only; later persistent pulses stay on the
                //   separate carried state1 owner below.
                // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
                // refs/melee/src/melee/it/items/itfoxlaser.c::itFoxlaser_UnkMotion1_Phys
                shoot_apply_motion_step = 1u;
              } else {
                shoot_throw_lw_late_pulse_transn_y = 1u;
              }
            }
            // ThrowHi pulse-crossing vector owner:
            // - ftFx_Throw_Anim recomputes FtGetHoldJoint / ItGetHoldJoint for every consumed
            //   throw_flags_b0 pulse. Do not reuse the latest live shot velocity for mid/late
            //   pulses; frame-20/24 ThrowHi rows rotate the gun between pulses and must sample the
            //   current float-pose hold-joint vector.
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
            // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
            // data/moves/{fox,falco}.json moves["ftCo_SM_ThrowHi"]["events"]
          }
        }
      }
      if (!should_shoot) {
        continue;
      }
      if (is_blaster_throw && action_id == (uint16_t)MSL_ACT_THROW_HI && shoot_spawn_state == 1u) {
        int16_t first_throwhi_pulse_af = -1;
        const uint8_t first_throwhi_spawn =
            (move_tables_throw_projectile_first_pulse_frame(cid, action_id,
                                                            &first_throwhi_pulse_af) &&
             batch->state.throw_pulse_crossed_curr_frame[idx] == (uint8_t)first_throwhi_pulse_af)
                ? 1u
                : 0u;
        int candidate = -1;
        if (first_throwhi_spawn) {
          for (int vp = 0; vp < num_players; vp++) {
            if (vp == p) {
              continue;
            }
            const size_t v_idx = msl_idx_player(bi, vp);
            if (batch->state.grab_owner_port[v_idx] == (uint8_t)p) {
              continue;
            }
            if (batch->state.hitstun[v_idx] == 0u ||
                !msl_damage_source_victim_matches_attacker(batch, v_idx, idx, p) ||
                batch->state.last_attack_landed[v_idx] == 0u) {
              continue;
            }
            if (candidate >= 0) {
              candidate = -1;
              break;
            }
            candidate = vp;
          }
        }
        if (candidate >= 0) {
          // Pending ThrowHi first-pulse hitlist carry:
          // - The command lane proves this Anim callback emitted the first throw_flags_b0 pulse.
          // - The unique non-attached victim is already in same-owner throw-laser hitstun with
          //   instance_hit_by equal to the owner's x2088/xDA8 seed. v10 dumps for this shape show the
          //   newly spawned item HitCapsule victims_1 entry on hitbox 2, so seed that hitbox instead
          //   of letting command timing alone own the consume/carry split.
          // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
          // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
          // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4}
          shoot_seed_hitlist_victim = candidate;
          shoot_seed_hitlist_victim_iid = batch->state.instance_id[msl_idx_player(bi, candidate)];
          shoot_seed_hitlist_mask = 0x04u;
        }
      }
      if (is_blaster_throw && action_id == (uint16_t)MSL_ACT_THROW_B &&
          item_type_is_falco_laser(lp->shot_itkind) && shoot_spawn_state == 1u) {
        int16_t first_throwb_pulse_af = -1;
        if (move_tables_throw_projectile_first_pulse_frame(cid, action_id,
                                                           &first_throwb_pulse_af) &&
            batch->state.throw_pulse_crossed_curr_frame[idx] == (uint8_t)first_throwb_pulse_af) {
          const int callback_victim = throw_laser_unique_same_source_victim(batch, bi, p);
          if (callback_victim >= 0) {
            const size_t v_idx = msl_idx_player(bi, callback_victim);
            // Falco ThrowB startup item HitCapsule carry:
            // - The first ThrowB pulse is command-owned by ftAction/ftFx_Throw_Anim, but the
            //   carry-vs-callback decision is per item HitCapsule just like the retained Falco
            //   ThrowLw hb2/3 split.
            // - Seed the Falco state1 hb2/3 victim lanes for startup carry rows so the article
            //   persists without replaying a false combo/source callback.
            // - If the first-pulse victim is already on the prior ThrowB frame-15 callback identity
            //   and the command has no pre-existing live state1 shot, seed all hitboxes: the source
            //   victims_1 ring has already admitted this fighter before the newly spawned article's
            //   first motion tick is serialized.
            // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
            // refs/melee/src/melee/ft/chara/ftFox/ftFx_Throw_Anim
            // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
            // refs/melee/src/melee/it/itcoll.c::{it_8026FA2C,it_8026FAC4,it_80272460}
            shoot_seed_hitlist_victim = callback_victim;
            shoot_seed_hitlist_victim_iid = batch->state.instance_id[v_idx];
            shoot_seed_hitlist_mask =
                (batch->state.throw_command_pending_seed_valid[idx] != 0u &&
                 throw_seed_shot_count[p] == 0u &&
                 batch->state.last_attack_landed[v_idx] == (uint8_t)first_throwb_pulse_af &&
                 batch->state.action_frame[v_idx] >= 5)
                    ? 0x0Fu
                    : 0x0Cu;
          }
        }
      }
      if (is_blaster_throw && shoot_spawn_state == 1u) {
        throw_laser_ensure_spawn_counter_for_pulse(
            batch, bi, p, lp->gun_itkind, action_id, cid,
            batch->state.throw_pulse_crossed_curr_frame[idx]);
      }
      const int spawned_slot = laser_spawn_from_fighter(
          batch, bi, p, lp, shoot_spawn_state, shoot_apply_motion_step,
          shoot_throw_lw_late_pulse_transn_y, shoot_seed_hitlist_victim,
          shoot_seed_hitlist_victim_iid, shoot_seed_hitlist_mask, is_blaster_throw);
      (void)spawned_slot;
      if (is_blaster_throw && action_id == (uint16_t)MSL_ACT_THROW_LW &&
          shoot_throw_lw_late_pulse_transn_y != 0u && item_type_is_fox_laser(lp->shot_itkind) &&
          throw_seed_shot_count[p] == 0u) {
        laser_spawn_apply_throwlw_attached_body_callback(batch, bi, p, spawned_slot, lp, 0u,
                                                         shoot_throw_lw_late_pulse_transn_y);
      }
      if (is_blaster_throw && action_id == (uint16_t)MSL_ACT_THROW_LW &&
          item_type_is_falco_laser(lp->shot_itkind) &&
          batch->state.throw_pulse_crossed_curr_frame[idx] == 28u &&
          batch->state.action_frame[idx] <= 29 && throw_seed_shot_count[p] == 0u) {
        // Falco ThrowLw frame-28 attached BODY callback:
        // - The source predicate is the current ftAction command pulse consumed by ftFx_Throw_Anim
        //   (`throw_flags_b0`), represented either by explicit pending-command seed ownership in
        //   one-step or by live anim-frame crossing in rollout.
        // - Do not require the explicit seed lane after `throw_pulse_crossed_curr_frame` proves the
        //   current frame-28 pulse; otherwise replay-seeded rollouts spawn the article but miss the
        //   same-frame attached BODY callback.
        // - Under post-hitlag rollout, the same crossing can expose post-frame action_frame 29
        //   after the frame-28 command is consumed. Keep ownership on the extracted pulse crossing,
        //   with the visible action frame only bounding this immediate post-command row.
        // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
        laser_spawn_apply_throwlw_attached_body_callback(batch, bi, p, spawned_slot, lp, 0u, 0u);
      }
      if (is_blaster_throw && action_id == (uint16_t)MSL_ACT_THROW_LW &&
          batch->state.throw_pulse_crossed_curr_frame[idx] ==
              (uint8_t)MSL_THROWLW_PULSE_ATTACH_AF &&
          throw_seed_shot_count[p] == 0u) {
        const int attached_victim = throwlw_attached_victim_for_owner(batch, bi, p);
        uint8_t source_throw_rate_allowed = 0u;
        if (attached_victim >= 0) {
          const size_t v_idx = msl_idx_player(bi, attached_victim);
          source_throw_rate_allowed = item_throwlw_frame25_post_hitlag_rate_allowed(
              batch->state.char_id[idx], batch->state.char_id[v_idx]);
        }
        const int16_t frame_start_action_frame = batch->state.prev_action_frame[idx];
        const uint8_t throwlw_frame25_post_hitlag_phase =
            (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_THROW_LW &&
             frame_start_action_frame >= 0 && attached_victim >= 0 &&
             frame_start_action_frame < ((int16_t)MSL_THROWLW_PULSE_ATTACH_AF - 1) &&
             source_throw_rate_allowed != 0u)
                ? 1u
                : 0u;
        // ThrowLw frame-25 post-hitlag callback:
        // - This is the same ftFx_Throw_Anim -> it_8029C6CC -> item BODY source path as the
        //   frame-28 callback above, but admitted only when the attached victim started the frame
        //   in hitlag and prio-0 timers have ended it before the resumed Anim callback.
        // - Keep the immediate callback to the source command phase that starts before frame 24,
        //   then crosses the frame-25 projectile command after the hitlag boundary, and is on the
        //   slower ftCo_800DD4B0 throw anim-speed path derived from victim weight/common x37C data.
        //   QGD controls are Fox-victim 1.333... source-rate rows that can serialize the article
        //   without same-frame BODY hitlag, so a broad "any current frame-25 pulse" gate is too wide.
        // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006A360}
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD4B0
        // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
        // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
        // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
        if (throwlw_frame25_post_hitlag_phase != 0u) {
          laser_spawn_apply_throwlw_attached_body_callback(batch, bi, p, spawned_slot, lp, 1u, 0u);
        }
      }
      if (is_blaster_throw && action_id == (uint16_t)MSL_ACT_THROW_B &&
          item_type_is_falco_laser(lp->shot_itkind)) {
        laser_spawn_apply_falco_throwb_final_prior_body_callback(batch, bi, p, spawned_slot, lp,
                                                                 throw_seed_shot_count[p]);
        laser_spawn_apply_falco_throwb_startup_body_callback(batch, bi, p, spawned_slot, lp);
      }
    }

    if (row_had_items != 0u || items_row_has_any(batch, bi) != 0u) {
      // Keep item ordering stable for fixed-slot comparisons.
      items_sort(batch, bi);
    }
  }
}
