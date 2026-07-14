#include "items_internal.h"

void sheik_chain_hidden_clear_slot(MslBatch* batch, size_t ii) {
  if (batch == NULL) {
    return;
  }
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
  batch->state.item_sheik_chain_hit_grace[ii] = 0u;
  batch->state.item_sheik_chain_hit_reset_prev[ii] = 0u;
  batch->state.item_sheik_chain_hit_prev_valid[ii] = 0u;
  batch->state.item_sheik_chain_stale_damage_valid[ii] = 0u;
  batch->state.item_sheik_chain_stale_damage_mul[ii] = 1.0f;
  batch->state.item_sheik_chain_env_flags[ii] = 0u;
  batch->state.item_sheik_chain_target_valid[ii] = 0u;
  batch->state.item_sheik_chain_target_x[ii] = 0.0f;
  batch->state.item_sheik_chain_target_y[ii] = 0.0f;
  batch->state.item_sheik_chain_target_z[ii] = 0.0f;
}

void items_reseed_clear_sheik_chain_hidden_slot(MslBatch* batch, size_t item_idx) {
  sheik_chain_hidden_clear_slot(batch, item_idx);
}

void items_reseed_clear_sheik_needle_hidden_slot(MslBatch* batch, size_t item_idx) {
  if (batch == NULL) {
    return;
  }
  // Dropped/bounced Needle xDD8/xDDC/xDE0 are live article-union state. Slippi does not expose
  // them, and replay-seeded state 1/4 deliberately uses the visible-velocity/table fallback in
  // sheik_needle_motion_step. Never let an unrelated prior item occupying this batch lane turn
  // that fallback into a stale valid hidden sample.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
  //   itSeakNeedleThrown_SetupDrop,itSeakNeedleThrown_SetupBounce,
  //   itSeakneedlethrown_UnkMotion1_Phys,itSeakneedlethrown_UnkMotion4_Phys}
  batch->state.item_sheik_needle_hidden_drop_valid[item_idx] = 0u;
  batch->state.item_sheik_needle_hidden_drop_min_vel_y[item_idx] = 0.0f;
  batch->state.item_sheik_needle_hidden_drop_gravity[item_idx] = 0.0f;
  batch->state.item_sheik_needle_hidden_drop_vel_x[item_idx] = 0.0f;
}

static inline uint8_t action_is_sheik_needle_cancel(uint8_t char_id, uint16_t action_id_u16) {
  if (char_id != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  return (uint8_t)(action_id_u16 == (uint16_t)MSL_ACT_SK_SPECIAL_N_CANCEL ||
                   action_id_u16 == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_N_CANCEL);
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

uint16_t item_article_sheik_needle_held_kind(uint8_t char_id) {
  const MslItemArticleParams* ap = item_article_params_get(char_id);
  return ap != NULL ? ap->needle_held_itkind : 0u;
}

uint16_t item_article_sheik_needle_throw_kind(uint8_t char_id) {
  const MslItemArticleParams* ap = item_article_params_get(char_id);
  return ap != NULL ? ap->needle_throw_itkind : 0u;
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

void sheik_needle_spawn_thrown_article_from_fighter(MslBatch* batch, int bi, int owner) {
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
    const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
    const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
    const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
    if (ap != NULL && batch->state.sheik_chain_pose_mag[owner_idx] <= 0.0001f) {
      const int slot = items_find_owned_item_slot(batch, bi, owner, ap->sheik_chain_itkind);
      if (slot >= 0) {
        const size_t ii = msl_idx_item(bi, slot);
        if (batch->state.item_sheik_chain_target_valid[ii] != 0u) {
          // Neutral active Chain keeps the static Chain item[2] overlay target. That pose is not
          // separately extracted; source probes show the target remains the previous
          // `fn_802BB44C`/transition sample while x14 is zero, rather than following the ordinary
          // active-action L3rdNa animation.
          // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_80110610
          // refs/melee/src/melee/it/items/itseakchain.c::{fn_802BB44C,fn_802BB694}
          *out_x = batch->state.item_sheik_chain_target_x[ii];
          *out_y = batch->state.item_sheik_chain_target_y[ii];
          if (out_z != NULL) {
            *out_z = batch->state.item_sheik_chain_target_z[ii];
          }
          return 1u;
        }
      }
    }
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
    // lb_8000B1CC. The exact static item[2] overlay is handled by the zero-magnitude target carry
    // above; for nonzero directed Chain frames, keep the existing extracted-pose blend so the target
    // approaches the source-directed hand sample.
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

static uint8_t sheik_chain_hitcap_cooldown_reload(const MslCharParams* chp);

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
  // - End_Anim calls it_802BB20C at x28, destroying the article; there is no x28-1 fighter-owned
  //   state-0 publication.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
  //   ftSk_SpecialS_CheckInitChain,ftSk_SpecialSEnd_Anim,ftSk_SpecialAirSEnd_Anim}
  // refs/melee/src/melee/it/items/itseakchain.c::{
  //   it_802BCFC4,it_802BCED4,it_802BCF84,it_802BB20C}
  batch->state.item_state[msl_idx_item(bi, slot)] = state;
  return 1u;
}

uint8_t items_activate_sheik_chain_hitcaps_on_entry(MslBatch* batch, size_t owner_idx) {
  if (batch == NULL || batch->state.char_id[owner_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  const MslCharParams* chp = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || chp == NULL || ap->sheik_chain_itkind == 0u) {
    return 0u;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  const int slot = items_find_owned_item_slot(batch, bi, owner, ap->sheik_chain_itkind);
  if (slot < 0) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, slot);
  // Held Chain entry calls ftSk_SpecialS_80110AEC immediately after entering SpecialS/SpecialAirS:
  // it enables the four x914 HitCapsules, zeroes x914[].x4C/x58, and marks the fighter hitbox
  // state dirty before the next Chain article solve can publish it_802BCB88 positions. Model that
  // one source activation explicitly; later movement after ftSk_SpecialS_80110BCC disables this
  // window is handled by the normal x1C/cmd0 gate in sheik_chain_update_hitcap_gate.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
  //   ftSk_SpecialS_80111830,ftSk_SpecialS_80111988,ftSk_SpecialS_80110AEC,
  //   ftSk_SpecialS_80110BCC}
  batch->state.item_sheik_chain_hitcaps_active[ii] = 1u;
  batch->state.item_sheik_chain_hit_reset_prev[ii] = 1u;
  batch->state.item_sheik_chain_hit_grace[ii] = 0u;
  batch->state.item_sheik_chain_hit_cooldown[ii] = sheik_chain_hitcap_cooldown_reload(chp);
  if (batch->state.item_sheik_chain_stale_damage_valid[ii] == 0u) {
    // The Chain article persists while ftSk_SpecialS_80110BCC disables/reactivates the fighter
    // x914 capsules. Preserve the source ftColl_8007ABD0 stale scalar with the article so later
    // reactivation does not recompute HitCapsule.damage from a stale table that now includes this
    // held Chain's prior hits.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0
    // refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
    const uint16_t attack_id = batch->state.attack_id[owner_idx];
    const uint16_t attack_instance = batch->state.attack_instance[owner_idx];
    batch->state.item_sheik_chain_stale_damage_mul[ii] =
        staling_multiplier_for_move_excluding_instance(batch, owner_idx, attack_id,
                                                       attack_instance);
    batch->state.item_sheik_chain_stale_damage_valid[ii] = 1u;
  }
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
    // active-frontier geometry: horizontal floors admit downward crossings from the active
    // hand-side neighbor, while sloped floors use the general segment intersection path. The
    // response below mirrors it_802BC080's env_flags&0x18000 branch.
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
    best_x = cur_x;
    if (fabsf(sx) > 0.0001f) {
      best_y = line.y0 + ((cur_x - line.x0) * (sy / sx)) + 0.0001f;
    } else {
      best_y = prev_y + ry * t + 0.0001f;
    }
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

static uint8_t sheik_chain_hitcap_cooldown_reload(const MslCharParams* chp) {
  int cd = (chp != NULL) ? (int)chp->sheik_chain_extension_frames : 0;
  // ftSk_SpecialS_80110BCC assigns mv.sk.specials.x1C = specialAttrs->x18 when the Chain movement
  // threshold reactivates the four fighter HitCapsules.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_80110BCC
  if (cd < 1) {
    cd = 1;
  }
  if (cd > 255) {
    cd = 255;
  }
  return (uint8_t)cd;
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
  // The x20 grace path is set by post-hitlag ftSk_SpecialS_ChainSomething. While it is nonzero,
  // the no-movement branch decrements x20 instead of clearing/disabling Chain HitCapsules.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
  //   ftSk_SpecialS_80110BCC,ftSeakSpecialS_LoopChainHitCollisions,
  //   ftSeakSpecialS_LoopChainHitActivate,ftSk_SpecialS_ChainSomething}
  if (batch->state.item_sheik_chain_hit_cooldown[ii] > 0u) {
    batch->state.item_sheik_chain_hit_cooldown[ii]--;
    if (batch->state.item_sheik_chain_hit_cooldown[ii] == 0u) {
      sheik_chain_disable_hitcaps(batch, ii, owner_idx);
    }
  }
  if (moved != 0u && batch->state.item_sheik_chain_hit_cooldown[ii] == 0u) {
    batch->state.item_sheik_chain_hit_cooldown[ii] = sheik_chain_hitcap_cooldown_reload(chp);
    // ftSeakSpecialS_LoopChainHitActivate enables the four Chain HitCapsules and immediately calls
    // ftSk_SpecialS_ZeroHitboxPositions, clearing both x914[].x4C and x914[].x58. The same-frame
    // article callback may republish nonzero it_802BCB88 positions through
    // ftSk_SpecialS_UpdateHitboxes. Movement over the article threshold owns the
    // ftSeakSpecialS_LoopChainHitActivate edge even though held active Chain's ordinary script
    // cmd_vars[0] was cleared on entry; the HitCapsule's own enabled/x1C window remains the runtime
    // collision-visible gate.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    //   ftSk_SpecialS_80110F70,ftSeakSpecialS_LoopChainHitActivate,
    //   ftSk_SpecialS_ZeroHitboxPositions,ftSk_SpecialS_UpdateHitboxes}
    // refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
    batch->state.item_sheik_chain_hitcaps_active[ii] = 1u;
    batch->state.item_sheik_chain_hit_reset_prev[ii] = 1u;
  } else if (moved == 0u && batch->state.item_sheik_chain_hit_cooldown[ii] == 0u) {
    if (batch->state.item_sheik_chain_hit_grace[ii] > 0u) {
      batch->state.item_sheik_chain_hit_grace[ii]--;
    } else {
      sheik_chain_disable_hitcaps(batch, ii, owner_idx);
    }
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

  if (item_state == 0u) {
    // The picked-up/spawn state uses the state-0 item callbacks and has not yet run
    // `it_802BCFC4` to activate source x0. Do not run the active-frontier/held link solver in this
    // state: `fn_802BB44C` starts the link walk only after the fighter callback enters state 1, and
    // source probes show no pre-extension gravity/integration on the free-end link.
    // refs/melee/src/melee/it/items/itseakchain.c::{
    //   itSeakChain_Spawn,it_2725_Logic54_PickedUp,it_802BCFC4,fn_802BB44C}
    batch->state.item_sheik_chain_target_valid[ii] = 0u;
    batch->state.item_sheik_chain_hitcaps_active[ii] = 0u;
    sheik_chain_publish_hitbox_map(batch, ii, ap, active, n);
    return;
  }

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
      batch->state.item_sheik_chain_hit_cooldown[ii] = sheik_chain_hitcap_cooldown_reload(chp);
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
    px[i] += vx[i];
    py[i] += vy[i];
    pz[i] += vz[i];
    uint8_t env_hit = 0u;
    if (counter > mode) {
      // `it_802BB938` seeds CollData.last_pos from the active hand-side neighbor (`link->next`) when
      // available, not from the link's own previous-frame position. Held Chain floor/platform hits
      // are therefore active-frontier segment sweeps. On floor flags, source preserves link->pos.x
      // and accepts mpColl's floor-adjusted y.
      // refs/melee/src/melee/it/items/itseakchain.c::{it_802BB938,it_802BC080}
      env_hit = sheik_chain_sweep_floor_link(batch, (int)(owner_idx / (size_t)MSL_MAX_PLAYERS),
                                             px[i - 1], py[i - 1], &px[i], &py[i]);
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
    // (`ftSk_SpecialS_80110BCC`) is only called by held/end Chain Anim callbacks. Before that gate
    // enables the held hitcap window, source publication is the terminal `it_802BCB88` frontier
    // payload; do not let lower approximate links become BODY owners one frame early.
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
    //   ftSk_SpecialSStart_Anim,ftSk_SpecialAirSStart_Anim,ftSk_SpecialS_UpdateHitboxes,
    //   ftSk_SpecialS_80110BCC}
    // refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
    // data/scripts/sheik.bin::MSLFTSC1 SpecialSStart/SpecialAirSStart frame-22 create_hitbox
    const uint8_t start_script_hitcaps =
        (batch->state.special_cmd0[fighter_idx] != 0u &&
         (owner_action == (uint16_t)MSL_ACT_SK_SPECIAL_S_START ||
          owner_action == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_START))
            ? 1u
            : 0u;
    const MslCharParams* chp = msl_char_params_fast((uint8_t)MSL_CHAR_ID_SHEIK);
    const uint8_t start_terminal_frontier_ready =
        (chp != NULL && batch->state.sheik_special_timer_frame_start[fighter_idx] >
                            (uint8_t)(chp->sheik_chain_spawn_frame + 4.0f))
            ? 1u
            : 0u;
    if (start_script_hitcaps == 0u || start_terminal_frontier_ready == 0u || hitbox_id != 3u) {
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

uint8_t sheik_chain_hitbox_stale_damage_mul(const MslBatch* batch, size_t fighter_idx,
                                            float* out_mul) {
  if (batch == NULL || out_mul == NULL ||
      batch->state.char_id[fighter_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const int bi = (int)(fighter_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(fighter_idx % (size_t)MSL_MAX_PLAYERS);
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_chain_itkind == 0u) {
    return 0u;
  }
  const int slot = items_find_owned_item_slot(batch, bi, owner, ap->sheik_chain_itkind);
  if (slot < 0) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, slot);
  if (batch->state.item_sheik_chain_stale_damage_valid[ii] == 0u ||
      !(batch->state.item_sheik_chain_stale_damage_mul[ii] > 0.0f)) {
    return 0u;
  }
  *out_mul = batch->state.item_sheik_chain_stale_damage_mul[ii];
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

void sheik_chain_items_update_anim_phase(MslBatch* batch, int bi) {
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
    if (batch->state.hitlag_pre_timer[owner_idx] != 0u && batch->state.hitlag[owner_idx] == 0u) {
      // Sheik Chain post-hitlag callback:
      // ftSk_SpecialS_ChainSomething runs when owner hitlag exits, calls it_802BAF0C, and sets
      // mv.sk.specials.x20 = 2. That callback is a post-hitlag handoff; do not also run/consume
      // ftSk_SpecialS_80110BCC's x20 branch in the same simulator frame. The next Chain Anim
      // callback spends the first no-movement grace tick.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
      //   ftSk_SpecialS_ChainSomething,ftSk_SpecialS_80110BCC}
      batch->state.item_sheik_chain_hit_grace[ii] = 2u;
      continue;
    }
    if (batch->state.hitlag[owner_idx] != 0u ||
        batch->state.hitlag_started_frame[owner_idx] != 0u) {
      // The owning fighter's hitlag sets fp->x2219_b5. Fighter_8006A360 gates MotionState Anim on
      // !x2219_b5, so ftSk_SpecialS_80110BCC (Chain hitcap cooldown/clear/reactivate) does not run
      // while the owner is frozen. Preserve both the cooldown counter and the x914 HitCapsule
      // victim rings; partially aged seed-reconstructed windows are still source-owned by that
      // same skipped callback, not by article/link accessory motion.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
      //   ftSk_SpecialS_80110BCC,ftSeakSpecialS_LoopChainHitCollisions}
      // refs/melee/src/melee/it/items/itseakchain.c::{fn_802BB694,it_802BC080,it_802BCB88}
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

void sheik_vanish_smoke_items_update(MslBatch* batch, int bi) {
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
  // The accessory callback samples HipN with lb_8000B1CC, spawns a fresh smoke article with
  // it_802B1C60, and initializes It_Kind_Seak_Vanish state 0. Source does not enforce one
  // smoke-per-owner: a second Vanish transition can publish a new smoke while the prior smoke's
  // ItemCommonData::xF8 lifetime is still active.
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

static inline uint8_t sheik_needle_item_ground_or_air(uint8_t state) {
  (void)state;
  // `it_802762BC` marks the article GA_Air on stage-stick and bounce setup, including stuck state
  // 2. Fighter HitCapsule -> item hurtbox admission in it_802703E8 uses that live ground_or_air
  // flag rather than the visual fact that state 2 is stuck to a stage line.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
  //   itSeakneedlethrown_UnkMotion0_Coll,itSeakneedlethrown_UnkMotion2_Coll}
  // refs/melee/src/melee/it/itmaplib.c::it_802762BC
  return 0u;
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
  // Held Needle item Anim destroys the article once fp->fv.sk.x4 is NULL; otherwise it persists as
  // a fighter-part-attached item and only updates model visibility/scale. Replay seeds do not expose
  // fp->fv.sk.x4 directly, but a visible Sheik-owned held article is evidence that the item-local
  // owner pointer is still live. Source item Anim runs before fighter Anim: SpecialNEnd firing
  // subframes clear the held slot later through shootNeedles' item/accessory phase, not here. Cancel
  // explicitly clears fp->fv.sk.x4 in its Anim callback, so a replay-seeded Cancel row owns the
  // pre-fighter item self-destruction. Do not infer fp->fv.sk.x4=NULL from common action ids after
  // air/ground transition; the item carries its internal seakneedleheld.owner across those states
  // until an explicit clear/drop owner runs.
  // refs/melee/src/melee/it/items/itseakneedleheld.c::itSeakneedleheld_UnkMotion0_Anim
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::{
  //   ftSk_SpecialNCancel_Anim,ftSk_SpecialAirNCancel_Anim,ftSk_SpecialNEnd_Anim,
  //   ftSk_SpecialAirNEnd_Anim,shootNeedles}
  if (action_is_sheik_needle_cancel((uint8_t)MSL_CHAR_ID_SHEIK, action)) {
    return 0u;
  }
  return 1u;
}

void sheik_held_needles_update_anim_phase(MslBatch* batch, int bi) {
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

static inline void sheik_needle_hurtbox_jobj_world_xy(const MslItemArticleParams* params,
                                                      float item_facing_dir, float* out_x,
                                                      float* out_y) {
  if (out_x == NULL || out_y == NULL) {
    return;
  }
  *out_x = 0.0f;
  *out_y = 0.0f;
  if (params == NULL || params->needle_hurtbox_bone_id == 0u) {
    return;
  }
  for (uint8_t hb = 0;
       hb < params->needle_hitbox_count && hb < (uint8_t)MSL_ITEM_ARTICLE_MAX_HITBOXES; hb++) {
    if (params->needle_hitbox_bone_id[hb] != params->needle_hurtbox_bone_id) {
      continue;
    }
    // The item hurtbox descriptor is bound to the same child JObj as the Needle command-11
    // HitCapsules. MSLITAR1 carries that child-JObj translation for each extracted hitbox bone; use
    // the matching bone's translation before applying the hurtbox local offsets. Source
    // lbColl_8000805C samples HurtCapsule.a/b through HSD_JObjGetMtxPtr(arg1->bone), not the item
    // root matrix.
    // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
    // refs/melee/src/melee/it/itcoll.c::it_802703E8
    // data/items/articles/*.bin (MSLITAR1 needle_hurtbox_bone_id,
    //   needle_hitbox_jobj_{y,z}_offset)
    *out_x = item_facing_dir * params->needle_hitbox_jobj_z_offset[hb];
    *out_y = params->needle_hitbox_jobj_y_offset[hb];
    return;
  }
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

static inline uint8_t sheik_needle_generic_blast_clear_after_motion(
    const MslStageBounds* blast_bounds, float x, float y) {
  if (blast_bounds == NULL) {
    return 0u;
  }
  // Thrown Needles use the generic item post-Phys destroy gate installed by Item_80268B18
  // (xDCC_flag.b3=1, b4567=15). Item_802697D4 integrates x40_vel+nudge, then Item_802696CC clears
  // on side/bottom blast bounds; the enabled top bit is the source's 10000.0f sentinel, not the
  // stage top blast bound.
  // refs/melee/src/melee/it/item.c::{Item_80268B18,Item_802697D4,Item_802696CC}
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_802AFD8C,it_802AFF08,it_802B00F4}
  return (uint8_t)(x > blast_bounds->right || x < blast_bounds->left || y < blast_bounds->bottom ||
                   y > 10000.0f);
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
  int xvel_sign = combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_BOUNCE_XVEL_SIGN2, 2);
  int xvel_idx = combat_rng_consume_randi_site(
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
  const int keep =
      combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_SHEIK_NEEDLE_DAMAGE_CALLBACK_KEEP3, 3);
  if (keep != 0) {
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
  // Capture the SetupBounce horizontal-drift/terminal/gravity lanes from the same live HSD stream.
  // SetupBounce writes xDD8 but not public x40_vel.x; state-4 Phys publishes xDD8 next frame.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
  //   it_2725_Logic109_DmgDealt,itSeakNeedleThrown_SetupBounce}
  sheik_needle_setup_bounce_hidden_lanes(batch, bi, ii, params);
  sheik_needle_apply_item_hitlag(batch, ii, hitlag_damage_i);
}

static inline uint8_t sheik_needle_try_shield_bounced_callback(
    MslBatch* batch, size_t ii, const MslItemArticleParams* params, float vx, float vy,
    float shield_x, float shield_y, float shield_z, float shield_radius, float prev_x, float prev_y,
    float cur_x, float cur_y, float hit_radius) {
  // Item_80269DC8 dispatches ShieldBounced before HitShield when ftColl_80077688 set the item
  // shield-bounce flag (xDCE.b5), the Needle is airborne, and the contact normal is within
  // ItemCommonData::unk_degrees of the shield plane. ShieldBounced mirrors x40_vel across hidden
  // xC58 and leaves the state-0 Needle alive; HitShield is the separate RNG destroy/state-4 callback.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
  // refs/melee/src/melee/it/item.c::Item_80269DC8
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{
  //   it_2725_Logic109_ShieldBounced,it_2725_Logic109_HitShield}
  (void)params;
  float bounce_vx = 0.0f;
  float bounce_vy = 0.0f;
  if (!laser_try_shield_bounce_velocity_from_segment(
          vx, vy, shield_x, shield_y, shield_z, shield_radius, prev_x, prev_y, 0.0f, cur_x, cur_y,
          0.0f, hit_radius, &bounce_vx, &bounce_vy)) {
    return 0u;
  }
  batch->state.item_vel_x[ii] = bounce_vx;
  batch->state.item_vel_y[ii] = bounce_vy;
  batch->state.item_direction[ii] = (bounce_vx >= 0.0f) ? 1.0f : -1.0f;
  return 1u;
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
  if (owner < 0 || owner >= (int)batch->config.num_players) {
    return 0u;
  }
  const uint8_t item_ga = sheik_needle_item_ground_or_air(batch->state.item_state[ii]);
  const float item_facing_dir = (batch->state.item_direction[ii] < 0.0f) ? -1.0f : 1.0f;
  float hurt_jobj_x = 0.0f;
  float hurt_jobj_y = 0.0f;
  sheik_needle_hurtbox_jobj_world_xy(params, item_facing_dir, &hurt_jobj_x, &hurt_jobj_y);
  const float ax = batch->state.item_pos_x[ii] + hurt_jobj_x + params->needle_hurtbox_a_offset[0];
  const float ay = batch->state.item_pos_y[ii] + hurt_jobj_y + params->needle_hurtbox_a_offset[1];
  const float az = params->needle_hurtbox_a_offset[2];
  const float bx = batch->state.item_pos_x[ii] + hurt_jobj_x + params->needle_hurtbox_b_offset[0];
  const float by = batch->state.item_pos_y[ii] + hurt_jobj_y + params->needle_hurtbox_b_offset[1];
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
      // refs/melee/src/melee/it/itcoll.c::it_8026D564 (fighter HitCapsule x42_b7 item gate)
      if (!msl_hitbox_x42_b7_enabled(flags) ||
          (flags & (uint16_t)MSL_HITBOX_FLAG_ITEM_HIT_INTERACTION) == 0u ||
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
      const float rr = batch->state.hitbox_radius[hb_i] + hurt_r;
      // Fighter HitCapsule -> item HurtCapsule uses the current HitCapsule packet through
      // lbColl_8000805C; unlike fast item BODY motion, this source path does not sweep the fighter
      // HitCapsule's previous publication. Keep the lite sim's item hurtbox test current-only so a
      // rollout's carried hitbox_prev lane cannot create a replay-invisible item damage callback.
      // refs/melee/src/melee/it/itcoll.c::it_802706D0
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
      const float d2 =
          item_segment_segment_dist2(hx1, hy1, hz1, hx1, hy1, hz1, ax, ay, az, bx, by, bz);
      if (d2 > rr * rr) {
        continue;
      }

      const int damage_i = combat_hitbox_collision_env_damage(batch, p_idx, hb_i);
      if (damage_i <= 0) {
        return 0u;
      }
      // Fighter HitCapsule -> item hurtbox path:
      // - `it_802703E8` gates x42_b7 item interaction, item ground/air flags, and victims_1.
      // - The accepted hit copies raw HitCapsule.damage into fighter->dmg.x1914, item->xCA0/xCA4,
      //   and the item hitlist. The accepted collision packet owns the applied damage; no future
      //   item outcome substitutes a different payload at a replay boundary.
      // - `Item_8026A294` then runs Needle DmgReceived and item hitlag from xCA8.
      // refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_8026F9AC_outline}
      // refs/melee/src/melee/it/item.c::{OnTakeDamageThink,Item_8026A294}
      // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgReceived
      combat_apply_deal_hitlag_raw_damage(batch, p_idx, damage_i);
      const int needle_damage_i = (int)ceilf(params->needle_hitbox_damage);
      if (batch->state.item_state[ii] == 4u && damage_i > needle_damage_i) {
        // High-damage fighter HitCapsules can also own the bounced-Needle clank side of the source
        // item pass. Use the same collision-time getEnvDmg payload that it_802703E8/Item_8026A294
        // consume for DmgReceived; raw unstaled HitCapsule.damage can be higher than the accepted
        // collision packet and must not widen the clank hitlag owner.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077970
        // refs/melee/src/melee/it/item.c::{OnClankThink,checkHitLag}
        const uint8_t item_hl = sheik_needle_item_common_hitlag_frames(needle_damage_i);
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

static inline void sheik_needle_hitbox_segment_xy(const MslItemArticleParams* params, uint8_t hb,
                                                  float contact_x0, float contact_y0,
                                                  float contact_x1, float contact_y1, float dirx,
                                                  float diry, float item_facing_dir, float* sx0,
                                                  float* sy0, float* sx1, float* sy1) {
  const float script_x = params->needle_hitbox_x_offset[hb];
  const float script_y = params->needle_hitbox_y_offset[hb];
  // it_802790C0 writes item command-11 offsets as HitCapsule.b_offset = {z, y, x}, then
  // it_8027137C samples that point through the bound JObj. Bone-0 hitboxes bind to the item root,
  // whose state-0 transform is just the M_PI_2 * facing_dir root rotation; child-bone hitboxes bind
  // to the rotated Needle child JObj and therefore follow the live travel angle.
  // refs/melee/src/melee/it/itanimlist.c::it_802790C0
  // refs/melee/src/melee/it/itcoll.c::it_8027137C
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::{it_802AFF08,itSeakneedlethrown_UnkMotion0_Coll}
  const float jobj_x = item_facing_dir * params->needle_hitbox_jobj_z_offset[hb];
  const float jobj_y = params->needle_hitbox_jobj_y_offset[hb];
  const uint16_t bone_id = params->needle_hitbox_bone_id[hb];
  const float script_world_x = (bone_id == 0u) ? (item_facing_dir * script_x) : (script_x * dirx);
  const float script_world_y = (bone_id == 0u) ? script_y : (script_y + script_x * diry);
  *sx0 = contact_x0 + jobj_x + script_world_x;
  *sy0 = contact_y0 + jobj_y + script_world_y;
  *sx1 = contact_x1 + jobj_x + script_world_x;
  *sy1 = contact_y1 + jobj_y + script_world_y;
}

static uint8_t sheik_needle_resolve_fighter_contacts(MslBatch* batch, int bi, int item_slot,
                                                     const MslItemArticleParams* params,
                                                     uint8_t spawned_this_frame) {
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
  const size_t owner_idx = msl_idx_player(bi, owner);
  const float cur_x = batch->state.item_pos_x[ii];
  const float cur_y = batch->state.item_pos_y[ii];
  const float vx = batch->state.item_vel_x[ii];
  const float vy = batch->state.item_vel_y[ii];
  const float facing = batch->state.item_direction[ii] < 0.0f ? -1.0f : 1.0f;
  const float x0 = spawned_this_frame != 0u ? cur_x : cur_x - vx;
  const float y0 = spawned_this_frame != 0u ? cur_y : cur_y - vy;
  float dir_x = 1.0f;
  float dir_y = 0.0f;
  const float speed_sq = vx * vx + vy * vy;
  if (speed_sq > 1e-12f) {
    const float inv_speed = 1.0f / sqrtf(speed_sq);
    dir_x = vx * inv_speed;
    dir_y = vy * inv_speed;
  }

  enum {
    NEEDLE_CALLBACK_NONE,
    NEEDLE_CALLBACK_BODY,
    NEEDLE_CALLBACK_CLANK,
    NEEDLE_CALLBACK_SHIELD
  };
  uint8_t callback = NEEDLE_CALLBACK_NONE;
  int callback_damage = 0;
  int pending_reflector = -1;
  float pending_reflect_damage_mul = 1.0f;
  float shield_x = 0.0f;
  float shield_y = 0.0f;
  float shield_z = 0.0f;
  float shield_radius = 0.0f;
  float shield_sx0 = 0.0f;
  float shield_sy0 = 0.0f;
  float shield_sx1 = 0.0f;
  float shield_sy1 = 0.0f;
  float shield_hit_radius = 0.0f;

  // ftColl_8007925C walks each fighter's item HitCapsules in authored order. The common selector
  // owns the complete per-capsule chain: ReflectDesc -> AbsorbDesc -> fighter HitCapsule clank ->
  // ShieldDesc -> BODY. Item_8026A294 runs only after the traversal and dispatches the highest
  // pending article callback class once.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/it/item.c::Item_8026A294
  for (int def = 0; def < (int)batch->config.num_players; def++) {
    if (def == owner) {
      continue;
    }
    const size_t d_idx = msl_idx_player(bi, def);
    const uint16_t def_iid = batch->state.instance_id[d_idx];
    for (uint8_t hb = 0u;
         hb < params->needle_hitbox_count && hb < (uint8_t)MSL_ITEM_ARTICLE_MAX_HITBOXES; hb++) {
      const float radius = params->needle_hitbox_size[hb];
      const float base_damage = params->needle_hitbox_damage_by_id[hb];
      if (!(radius > 0.0f) || !(base_damage > 0.0f)) {
        continue;
      }
      float sx0 = 0.0f;
      float sy0 = 0.0f;
      float sx1 = 0.0f;
      float sy1 = 0.0f;
      sheik_needle_hitbox_segment_xy(params, hb, x0, y0, cur_x, cur_y, dir_x, dir_y, facing, &sx0,
                                     &sy0, &sx1, &sy1);
      const MslItemHitCapsulePacket hit = {
          .x0 = sx0,
          .y0 = sy0,
          .z0 = 0.0f,
          .x1 = sx1,
          .y1 = sy1,
          .z1 = 0.0f,
          .radius = radius,
          .damage = msl_item_reflect_damage_lane(batch, ii, base_damage),
          .flags = params->needle_hitbox_flags[hb],
          .hitbox_id = hb,
          .element = params->needle_hitbox_element[hb],
          .item_grounded = 0u,
      };
      MslItemFighterContact contact;
      const MslItemFighterContactKind kind =
          item_hitcapsule_select_fighter_contact(batch, bi, item_slot, def, &hit, &contact);
      if (kind == MSL_ITEM_FIGHTER_CONTACT_NONE) {
        continue;
      }
      const int damage_i = (int)ceilf(hit.damage);
      if (kind == MSL_ITEM_FIGHTER_CONTACT_REFLECT) {
        hitlist_register_item_hitbox_fighter(batch, bi, item_slot, hb, def, def_iid,
                                             (int)MSL_LBCOLL_INSERT_TODO_7, 0);
        if (contact.reflect_max_damage >= 0 &&
            combat_get_env_dmg(hit.damage) > contact.reflect_max_damage) {
          if (callback < NEEDLE_CALLBACK_BODY) {
            callback = NEEDLE_CALLBACK_BODY;
          }
          if (damage_i > callback_damage) {
            callback_damage = damage_i;
          }
        } else {
          pending_reflector = def;
          pending_reflect_damage_mul = contact.reflect_damage_mul;
        }
        break;
      }
      if (kind == MSL_ITEM_FIGHTER_CONTACT_CLANK) {
        const uint8_t outcomes =
            item_hitcapsule_apply_fighter_hitbox_contact(batch, bi, item_slot, def, &hit, &contact);
        if ((outcomes & (uint8_t)MSL_ITEM_HITBOX_CONTACT_ITEM_RECEIVED) != 0u) {
          if (callback < NEEDLE_CALLBACK_CLANK) {
            callback = NEEDLE_CALLBACK_CLANK;
          }
          const size_t fighter_hi = idx_hitbox(bi, def, contact.fighter_hitbox);
          const int received_damage = combat_hitbox_collision_env_damage(batch, d_idx, fighter_hi);
          if (received_damage > callback_damage) {
            callback_damage = received_damage;
          }
        }
        break;
      }
      if (kind == MSL_ITEM_FIGHTER_CONTACT_SHIELD || kind == MSL_ITEM_FIGHTER_CONTACT_COUNTER) {
        combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                     batch->state.item_attack_instance[ii], hit.damage,
                                     params->needle_hitbox_shield_damage[hb], hit.element,
                                     batch->state.item_pos_x[ii]);
        hitlist_register_item_hitbox_fighter(batch, bi, item_slot, hb, def,
                                             batch->state.instance_id[d_idx],
                                             (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
        if (callback < NEEDLE_CALLBACK_SHIELD) {
          callback = NEEDLE_CALLBACK_SHIELD;
          shield_x = contact.descriptor_x;
          shield_y = contact.descriptor_y;
          shield_z = contact.descriptor_z;
          shield_radius = contact.descriptor_radius;
          shield_sx0 = sx0;
          shield_sy0 = sy0;
          shield_sx1 = sx1;
          shield_sy1 = sy1;
          shield_hit_radius = radius;
        }
        if (damage_i > callback_damage) {
          callback_damage = damage_i;
        }
        break;
      }

      hitlist_register_item_hitbox_fighter(batch, bi, item_slot, hb, def, def_iid,
                                           (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
      if (batch->state.hurtbox_state[d_idx] == (uint8_t)MSL_HURTCAPS_DISABLED ||
          contact.hurt_status == (uint8_t)MSL_HURTCAPS_DISABLED) {
        if (callback < NEEDLE_CALLBACK_BODY) {
          callback = NEEDLE_CALLBACK_BODY;
        }
        if (damage_i > callback_damage) {
          callback_damage = damage_i;
        }
        break;
      }
      const MslCommonParams* common = msl_common_params();
      if (common != NULL && contact.body_exact_evaluated != 0u &&
          batch->state.hitstun[d_idx] == 0u && contact.body_overlap > 0.0f &&
          contact.body_overlap <= common->phantom_overlap_max_x7a8) {
        combat_apply_item_phantom_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                      batch->state.item_instance_id[ii], hit.damage, hit.element);
        break;
      }
      const float stale_mul =
          item_hitcapsule_stale_damage_mul(batch, ii, owner_idx, batch->state.item_attack_id[ii]);
      const MslItemHitResult result = combat_apply_item_hit(
          batch, bi, owner, def, batch->state.item_attack_id[ii],
          batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
          batch->state.item_type[ii], 0u, hit.damage, params->needle_hitbox_angle[hb],
          params->needle_hitbox_kbg[hb], params->needle_hitbox_wsk[hb],
          params->needle_hitbox_bkb[hb], contact.hurt_height, hit.element, stale_mul, cur_x, vx,
          0u);
      if (result != MSL_ITEM_HIT_NONE) {
        if (callback < NEEDLE_CALLBACK_BODY) {
          callback = NEEDLE_CALLBACK_BODY;
        }
        if (damage_i > callback_damage) {
          callback_damage = damage_i;
        }
      }
      break;
    }
  }

  if (callback == NEEDLE_CALLBACK_SHIELD &&
      sheik_needle_try_shield_bounced_callback(batch, ii, params, vx, vy, shield_x, shield_y,
                                               shield_z, shield_radius, shield_sx0, shield_sy0,
                                               shield_sx1, shield_sy1, shield_hit_radius) != 0u) {
    return 1u;
  }
  if (callback != NEEDLE_CALLBACK_NONE) {
    sheik_needle_bounce_or_destroy_callback(batch, bi, ii, params, callback_damage);
    return 1u;
  }
  if (pending_reflector >= 0) {
    msl_item_reflect_apply_immediate_transfer(batch, ii, msl_idx_player(bi, pending_reflector),
                                              pending_reflector, pending_reflect_damage_mul, 1.0f);
    const MslItemCommonParams* common = msl_item_common_params();
    if (common != NULL && common->reflect_half_life_fraction > 0.0f) {
      batch->state.item_timer[ii] =
          (float)params->needle_lifetime_frames * common->reflect_half_life_fraction;
    }
    return 1u;
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
                                            const MslItemArticleParams* params) {
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
  float hit_x = cur_x;
  float hit_y = cur_y;
  float surface_vel_x = 0.0f;
  float surface_vel_y = 0.0f;
  if (!stage_collision_item_line_hit_runtime(batch, bi, old_x, old_y, cur_x, cur_y, &hit_x, &hit_y,
                                             &surface_vel_x, &surface_vel_y)) {
    return 0u;
  }
  // itSeakNeedleThrown_CheckGroundHit sweeps xDE4 -> current pos through it_8026EA20 and publishes
  // the stage-line contact before the state response. Leaving the article at the post-motion
  // endpoint creates large one-frame discontinuities when a fast Needle crosses hard ground.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakNeedleThrown_CheckGroundHit
  batch->state.item_pos_x[ii] = hit_x;
  batch->state.item_pos_y[ii] = hit_y;
  // CheckGroundHit publishes mpGetSpeed(line, &pos, &x40_vel) before every state-specific response.
  // Static lines therefore zero x40_vel; moving lines publish their current stage-object delta.
  // refs/melee/src/melee/it/items/itseakneedlethrown.c::itSeakNeedleThrown_CheckGroundHit
  // refs/melee/src/melee/mp/mplib.c::mpGetSpeed
  batch->state.item_vel_x[ii] = surface_vel_x;
  batch->state.item_vel_y[ii] = surface_vel_y;
  if (state == 4u) {
    sheik_needle_enter_stuck_state(batch, ii, params);
    return 1u;
  }
  if (state == 1u) {
    // UnkMotion1_Coll: vel.y = ABS(vel.y), then SetupBounce installs the state-4 drift/terminal/gravity.
    batch->state.item_state[ii] = 4u;
    batch->state.item_timer[ii] = (float)params->needle_bounce_lifetime_frames;
    batch->state.item_vel_y[ii] = fabsf(surface_vel_y);
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

void sheik_needles_update_and_collide(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  MslStageBounds blast_bounds = {0};
  const uint8_t has_blast_bounds =
      stage_collision_get_blast_bounds_world(batch->state.stage_id[bi], &blast_bounds);
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
      // it_802AFD8C-created Needles serialize at their spawn position/lifetime on quiet creation
      // rows, but the command-11 HitCapsules are already live before ftColl_8007925C's item BODY
      // pass. Run only the stationary BODY contact owner here; a miss still publishes the spawn
      // position/timer without same-frame motion.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::shootNeedles
      // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_802AFD8C
      // refs/melee/src/melee/it/itanimlist.c::it_802790C0
      // refs/melee/src/melee/it/itcoll.c::it_8027137C
      batch->state.item_hidden_callback_flags[ii] &=
          (uint8_t)~MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME;
      if (sheik_needle_resolve_fighter_contacts(batch, bi, it, params, 1u) != 0u) {
        needs_sort = 1u;
      }
      continue;
    }
    if (batch->state.item_hitlag[ii] > 0u) {
      batch->state.item_hitlag[ii]--;
      continue;
    }
    if (batch->state.item_state[ii] == 4u && batch->state.item_damage[ii] != 0u) {
      uint8_t fighter_hitlag_active = 0u;
      for (int p = 0; p < (int)batch->config.num_players; p++) {
        const size_t pidx = msl_idx_player(bi, p);
        if (batch->state.hitlag[pidx] != 0u || batch->state.hitlag_pre_timer[pidx] != 0u) {
          fighter_hitlag_active = 1u;
          break;
        }
      }
      if (fighter_hitlag_active != 0u) {
        // Slippi does not expose Item.xCBC_hitlagFrames. A one-step or rollout reseed of a Needle
        // that just bounced from DmgReceived can carry state4/damage/timer while the victim
        // deal-hitlag is still active; source Item_802697D4 freezes item anim/phys/lifetime until
        // item hitlag drains. Free-run callbacks seed item_hitlag directly, but teacher-forced rows
        // need this source-visible state4+damage+fighter-hitlag reconstruction.
        // refs/melee/src/melee/it/item.c::{checkHitLag,Item_802697D4}
        // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgReceived
        continue;
      }
    }
    if (sheik_needle_anim_lifetime_step(batch, ii) != 0u) {
      needs_sort = 1u;
      continue;
    }
    sheik_needle_motion_step(batch, ii);
    if (has_blast_bounds != 0u &&
        sheik_needle_generic_blast_clear_after_motion(&blast_bounds, batch->state.item_pos_x[ii],
                                                      batch->state.item_pos_y[ii]) != 0u) {
      item_slot_clear(batch, ii);
      needs_sort = 1u;
      continue;
    }
    if (sheik_needle_resolve_fighter_contacts(batch, bi, it, params, 0u) != 0u) {
      needs_sort = 1u;
      continue;
    }
    // Source-ordered after the item-vs-fighter Coll: itSeakneedlethrown_UnkMotion{0,1,4}_Coll then
    // test the stage line and stick/bounce the article so thrown Needles stop at the ground.
    if (sheik_needle_ground_hit_step(batch, bi, ii, params) != 0u) {
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
// removed at vanish_hitbox_remove_frame (anim-frame relative). The extracted command-11 packet is
// reflectable, absorbable, shieldable, and BODY-enabled. Vanish supplies only DmgDealt (false):
// Reflected and HitShield callbacks are NULL, so common item processing still transfers ownership
// and applies shield/BODY packets while the smoke itself persists for its full lifeTimer.
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
  const MslItemHitCapsulePacket hit = {
      .x0 = hx,
      .y0 = hy,
      .z0 = hz,
      .x1 = hx,
      .y1 = hy,
      .z1 = hz,
      .radius = hbr,
      .damage = msl_item_reflect_damage_lane(batch, ii, ap->vanish_hitbox_damage),
      .flags = ap->vanish_hitbox_flags,
      .hitbox_id = 0u,
      .element = ap->vanish_hitbox_element,
      .item_grounded = 0u,
  };
  uint8_t any = 0u;
  uint8_t higher_callback_pending = 0u;
  int pending_reflector = -1;
  float pending_reflect_damage_mul = 1.0f;
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
    const uint16_t def_iid = batch->state.instance_id[d_idx];
    MslItemFighterContact contact;
    const MslItemFighterContactKind kind =
        item_hitcapsule_select_fighter_contact(batch, bi, item_slot, def, &hit, &contact);
    if (kind == MSL_ITEM_FIGHTER_CONTACT_NONE) {
      continue;
    }
    any = 1u;
    if (kind == MSL_ITEM_FIGHTER_CONTACT_REFLECT) {
      hitlist_register_item_hitbox_fighter(batch, bi, item_slot, 0, def, def_iid,
                                           (int)MSL_LBCOLL_INSERT_TODO_7, 0);
      if (contact.reflect_max_damage >= 0 &&
          combat_get_env_dmg(hit.damage) > contact.reflect_max_damage) {
        higher_callback_pending = 1u;
      } else {
        pending_reflector = def;
        pending_reflect_damage_mul = contact.reflect_damage_mul;
      }
      continue;
    }
    if (kind == MSL_ITEM_FIGHTER_CONTACT_CLANK) {
      const uint8_t outcomes =
          item_hitcapsule_apply_fighter_hitbox_contact(batch, bi, item_slot, def, &hit, &contact);
      if ((outcomes & (uint8_t)MSL_ITEM_HITBOX_CONTACT_ITEM_RECEIVED) != 0u) {
        higher_callback_pending = 1u;
      }
      continue;
    }
    if (kind == MSL_ITEM_FIGHTER_CONTACT_SHIELD || kind == MSL_ITEM_FIGHTER_CONTACT_COUNTER) {
      combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                   batch->state.item_attack_instance[ii], hit.damage,
                                   ap->vanish_hitbox_shield_damage, hit.element,
                                   batch->state.item_pos_x[ii]);
      hitlist_register_item_hitbox_fighter(batch, bi, item_slot, 0, def,
                                           batch->state.instance_id[d_idx],
                                           (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
      higher_callback_pending = 1u;
      continue;
    }

    hitlist_register_item_hitbox_fighter(batch, bi, item_slot, 0, def, def_iid,
                                         (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
    higher_callback_pending = 1u;
    if (batch->state.hurtbox_state[d_idx] == (uint8_t)MSL_HURTCAPS_DISABLED ||
        contact.hurt_status == (uint8_t)MSL_HURTCAPS_DISABLED) {
      continue;
    }
    const MslCommonParams* common = msl_common_params();
    if (common != NULL && contact.body_overlap > 0.0f &&
        contact.body_overlap < common->phantom_overlap_max_x7a8) {
      combat_apply_item_phantom_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                    batch->state.item_instance_id[ii], hit.damage, hit.element);
      continue;
    }
    const MslItemHitResult res = combat_apply_item_hit(
        batch, bi, owner, def, batch->state.item_attack_id[ii],
        batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
        batch->state.item_type[ii], 0u, hit.damage, ap->vanish_hitbox_angle, ap->vanish_hitbox_kbg,
        ap->vanish_hitbox_wsk, ap->vanish_hitbox_bkb, contact.hurt_height,
        ap->vanish_hitbox_element, -1.0f, hx, 0.0f, 0u);
    (void)res;
  }
  // OnGiveDamage/HitShield outrank Reflect in Item_8026A294. Only a reflect-only traversal reaches
  // Item_80269F14; Vanish's NULL reflected callback preserves motion while the common owner/damage
  // snapshot is committed.
  // refs/melee/src/melee/it/item.c::{Item_80269F14,Item_8026A294}
  if (higher_callback_pending == 0u && pending_reflector >= 0) {
    msl_item_reflect_apply_transfer_state(batch, ii, msl_idx_player(bi, pending_reflector),
                                          pending_reflector, pending_reflect_damage_mul);
  }
  return any;  // smoke persists regardless of a hit (Logic42_DmgDealt returns false)
}

void sheik_vanish_smoke_collide(MslBatch* batch, int bi) {
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
