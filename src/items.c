#include "items_internal.h"

uint8_t slippi_metadata_low_byte_from_f32(float v) {
  uint32_t bits = 0;
  memcpy(&bits, &v, sizeof(bits));
  return (uint8_t)(bits & 0xFFu);
}

uint8_t item_state_flags_2218_is_reflect_behavior_only(uint8_t flags_2218) {
  const uint8_t mask = (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                                 MSL_STATE_FLAG_2218_B2 | MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR);
  return ((flags_2218 & mask) == (uint8_t)MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR) ? 1u : 0u;
}

uint8_t item_any_hitbox_allows_fighter(MslBatch* batch, int bi, int item_slot, int victim,
                                       uint16_t victim_iid) {
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

uint8_t item_all_hitboxes_allow_fighter(MslBatch* batch, int bi, int item_slot, int victim,
                                        uint16_t victim_iid) {
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    if (!hitlist_allows_item_hitbox_fighter(batch, bi, item_slot, hb, victim, victim_iid)) {
      return 0u;
    }
  }
  return 1u;
}

float item_hitcapsule_stale_damage_mul(const MslBatch* batch, size_t item_idx, size_t owner_idx,
                                       uint16_t attack_id) {
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

uint8_t item_stale_queue_contains_instance(const MslBatch* batch, size_t owner_idx,
                                           uint16_t attack_id, uint16_t attack_instance) {
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

void item_slot_clear(MslBatch* batch, size_t ii) {
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
  batch->state.item_sheik_needle_callback_bounce_vel_y_index[ii] = 0xFFu;
  batch->state.item_sheik_needle_callback_bounce_vel_x_index_sign[ii] = 0xFFu;
  batch->state.item_sheik_needle_callback_bounce_motion_valid[ii] = 0u;
  batch->state.item_sheik_needle_callback_bounce_gravity_index[ii] = 0u;
  batch->state.item_sheik_needle_callback_bounce_min_vel_y_index[ii] = 0u;
  batch->state.item_sheik_needle_stage_hit_seed_kind[ii] = 0u;
  batch->state.item_sheik_needle_stage_hit_vel_y_index[ii] = 0u;
  batch->state.item_sheik_needle_stage_hit_vel_x_index_sign[ii] = 0u;
  batch->state.item_sheik_needle_hidden_drop_valid[ii] = 0u;
  batch->state.item_sheik_needle_hidden_drop_min_vel_y[ii] = 0.0f;
  batch->state.item_sheik_needle_hidden_drop_gravity[ii] = 0.0f;
  batch->state.item_sheik_needle_hidden_drop_vel_x[ii] = 0.0f;
  sheik_chain_hidden_clear_slot(batch, ii);
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

uint8_t items_row_has_any(const MslBatch* batch, int bi) {
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
  SWAP(uint8_t, batch->state.item_sheik_needle_callback_bounce_vel_y_index);
  SWAP(uint8_t, batch->state.item_sheik_needle_callback_bounce_vel_x_index_sign);
  SWAP(uint8_t, batch->state.item_sheik_needle_callback_bounce_motion_valid);
  SWAP(uint8_t, batch->state.item_sheik_needle_callback_bounce_gravity_index);
  SWAP(uint8_t, batch->state.item_sheik_needle_callback_bounce_min_vel_y_index);
  SWAP(uint8_t, batch->state.item_sheik_needle_stage_hit_seed_kind);
  SWAP(uint8_t, batch->state.item_sheik_needle_stage_hit_vel_y_index);
  SWAP(uint8_t, batch->state.item_sheik_needle_stage_hit_vel_x_index_sign);
  SWAP(uint8_t, batch->state.item_sheik_needle_hidden_drop_valid);
  SWAP(float, batch->state.item_sheik_needle_hidden_drop_min_vel_y);
  SWAP(float, batch->state.item_sheik_needle_hidden_drop_gravity);
  SWAP(float, batch->state.item_sheik_needle_hidden_drop_vel_x);
  SWAP(uint8_t, batch->state.item_sheik_chain_links_valid);
  SWAP(float, batch->state.item_sheik_chain_prev_stick_x);
  SWAP(float, batch->state.item_sheik_chain_prev_stick_y);
  SWAP(uint8_t, batch->state.item_sheik_chain_hitcaps_active);
  SWAP(uint8_t, batch->state.item_sheik_chain_hit_cooldown);
  SWAP(uint8_t, batch->state.item_sheik_chain_hit_grace);
  SWAP(uint8_t, batch->state.item_sheik_chain_hit_reset_prev);
  SWAP(uint8_t, batch->state.item_sheik_chain_hit_prev_valid);
  SWAP(uint8_t, batch->state.item_sheik_chain_stale_damage_valid);
  SWAP(float, batch->state.item_sheik_chain_stale_damage_mul);
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
  // Sort key matches validation fixed-item ordering: (instance_id, spawn_id, type).
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

void items_sort(MslBatch* batch, int bi) {
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
  // Normalize empty-slot owner to -1 for deterministic validation parity.
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      batch->state.item_owner[ii] = -1;
    }
  }
}

int items_alloc_slot(MslBatch* batch, int bi) {
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (!batch->state.item_exists[ii]) {
      return it;
    }
  }
  return -1;
}

uint32_t items_next_spawn_id(MslBatch* batch, int bi) {
  // Item spawn id is the global item->x1C counter, not a function of currently-live items.
  // This matters for rollouts seeded after itemless gaps: future item fixed-slot ordering still
  // depends on historical spawns through x1C.
  // refs/melee/src/melee/it/item.c::Item_80267AA8
  const uint32_t out = batch->state.item_spawn_id_counter[bi];
  batch->state.item_spawn_id_counter[bi] = out + 1u;
  return out;
}

uint8_t items_action_is_damage_family(uint16_t action_id_u16) {
  // Generated MSLMSO01 Damage/DamageFly/DamageFall collision classes cover the same damage exit
  // callback family this item stale-owner clear follows.
  // data/motion_state/owners/{fox,falco}.bin (DAMAGE_*_COLL classes)
  return msl_damage_owner_is_damage_collision_landing_action(action_id_u16);
}

uint8_t items_action_is_dead_family(uint16_t action_id_u16) {
  // Common Dead* MotionState ids are contiguous at ftCo_MS_DeadDown..DeadUpFallHitCameraIce.
  // refs/melee/src/melee/ft/ftmotionstates.c
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCommon_MotionState
  return (action_id_u16 <= (uint16_t)10u) ? 1u : 0u;
}

int items_find_owned_item_slot(const MslBatch* batch, int bi, int owner, uint16_t item_kind) {
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

float items_cur_anim_frame_f32(const MslBatch* batch, size_t idx) {
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
    if (laser_params_for_item_type(type) != NULL ||
        item_article_params_is_illusion_item_type(type) != 0u ||
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

void items_update_sheik_needle_accessory_phase(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (batch->state.hitlag_started_frame[idx] != 0u) {
        continue;
      }
      // `shootNeedles` is Sheik's accessory4 callback armed by ftSk_Special{Air}NEnd_Anim.
      // Consume its latch after fighter Phys/Coll so the spawned article uses the same post-callback
      // root position as source. This stays out of the fighter Anim spawn phase used by blaster and
      // side-special script pulses.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
      //   ftSk_SpecialNEnd_Anim,ftSk_SpecialAirNEnd_Anim,shootNeedles}
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006C80C
      sheik_needle_spawn_thrown_article_from_fighter(batch, bi, p);
    }
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
      if (batch->state.char_id[idx] == (uint8_t)MSL_CHAR_ID_SHEIK &&
          batch->state.action_id[idx] >= (uint16_t)MSL_ACT_SK_SPECIAL_S_START &&
          batch->state.action_id[idx] <= (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END) {
        // Chain activation-edge flag lifetime:
        // hitboxes_refresh consumes item_sheik_chain_hit_reset_prev to seed x914[].x58 = x4C after
        // ftSeakSpecialS_LoopChainHitActivate zeroes both vectors. Keep the flag live through the
        // same combat pass so BODY selection can preserve the source victims_1 suppression on the
        // activation edge, then clear it after combat.
        // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
        //   ftSk_SpecialS_80110BCC,ftSeakSpecialS_LoopChainHitActivate,ftSk_SpecialS_ZeroHitboxPositions}
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008434,lbColl_8000ACFC}
        sheik_chain_clear_hitbox_reset_prev(batch, idx);
      }
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
