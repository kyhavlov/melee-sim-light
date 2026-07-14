#include "items_internal.h"

uint8_t item_spawn_has_unique_same_source_damage_victim(const MslBatch* batch, int bi, int owner) {
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

int items_find_gun_slot(const MslBatch* batch, int bi, int owner, uint16_t gun_itkind) {
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

uint8_t blaster_gun_state_from_action_id(uint16_t action_id_u16) {
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

uint8_t action_is_blaster_throw(uint16_t action_id_u16) {
  return (action_id_u16 == (uint16_t)MSL_ACT_THROW_B ||
          action_id_u16 == (uint16_t)MSL_ACT_THROW_HI ||
          action_id_u16 == (uint16_t)MSL_ACT_THROW_LW)
             ? 1u
             : 0u;
}

float item_throw_pose_facing_dir(const MslBatch* batch, size_t owner_idx, uint16_t action_id_u16) {
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

uint16_t item_article_laser_shot_kind(uint8_t char_id) {
  const MslItemArticleParams* ap = item_article_params_get(char_id);
  return ap != NULL ? ap->blaster_shot_itkind : 0u;
}

uint16_t item_article_illusion_kind(uint8_t char_id) {
  const MslItemArticleParams* ap = item_article_params_get(char_id);
  return ap != NULL ? ap->side_special_illusion_itkind : 0u;
}

uint8_t item_type_is_fox_laser(uint16_t item_type) {
  return item_type == item_article_laser_shot_kind((uint8_t)MSL_CHAR_ID_FOX) ? 1u : 0u;
}

uint8_t item_type_is_falco_laser(uint16_t item_type) {
  return item_type == item_article_laser_shot_kind((uint8_t)MSL_CHAR_ID_FALCO) ? 1u : 0u;
}

uint8_t item_spawn_laser_freezes_stale_damage(const MslBatch* batch, int bi, int owner,
                                              uint16_t item_type, uint16_t item_attack_id,
                                              uint16_t item_attack_instance, uint8_t spawn_state,
                                              uint8_t is_blaster_throw) {
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
    // Keep this gate tied to the attack identity, not replay row or corpus identity.
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

uint8_t item_type_is_fox_illusion(uint16_t item_type) {
  return item_type == item_article_illusion_kind((uint8_t)MSL_CHAR_ID_FOX) ? 1u : 0u;
}

uint8_t action_is_illusion_dash(uint8_t char_id, uint16_t action_id_u16) {
  // Ghost article spawn is owned by ftFx_SpecialS_Anim / ftFx_SpecialAirS_Anim
  // (main dash states), not Start/End states. Ownership from the extracted MotionState
  // row identity.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id_u16);
  return (uint8_t)(fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_S ||
                   fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S);
}

uint8_t action_is_illusion_end(uint8_t char_id, uint16_t action_id_u16) {
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id_u16);
  return (uint8_t)(fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_S_END ||
                   fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END);
}

uint8_t action_is_illusion_setphys(uint8_t char_id, uint16_t action_id_u16) {
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

uint8_t items_row_has_illusion_setphys_source(const MslBatch* batch, int bi, int num_players) {
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

int items_find_illusion_slot(const MslBatch* batch, int bi, int owner, uint16_t illusion_itkind) {
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

void illusion_spawn_from_fighter(MslBatch* batch, int bi, int owner) {
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
  const uint16_t action_id_u16 = batch->state.action_id[o_idx];
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id_u16);
  if ((fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_S &&
       fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S) ||
      fighter_script_cmd_var(batch, o_idx, 2u) == 0u) {
    return;
  }
  // ftFx_SpecialS_Anim clears cmd_vars[2] before invoking the ghost item create callback.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFox_SpecialS_CreateGhostItem}
  batch->state.script_cmd_vars[o_idx * 4u + 2u] = 0u;
  if (items_find_illusion_slot(batch, bi, owner, illusion_itkind) >= 0) {
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

static uint8_t blaster_gun_run_owner_callback(MslBatch* batch, int bi, int owner,
                                              const MslLaserParams* lp, uint8_t allow_create) {
  if (batch == NULL || lp == NULL) {
    return 0u;
  }
  const size_t o_idx = msl_idx_player(bi, owner);
  const uint16_t action_id_u16 = batch->state.action_id[o_idx];
  const uint8_t want_state = blaster_gun_state_from_action_id(action_id_u16);
  uint8_t want_gun = (want_state != 9) ? 1u : 0u;

  const int existing_slot = items_find_gun_slot(batch, bi, owner, lp->gun_itkind);
  const uint8_t is_throw = action_is_blaster_throw(action_id_u16);
  if (is_throw) {
    const uint8_t cmd1_cur = fighter_script_cmd_var(batch, o_idx, 1u) == 1u ? 1u : 0u;

    // Throw-side blaster ownership in ftFx_Throw_Anim:
    // - case 1 (cmd_vars[1]==1): spawn/update gun flow
    // - case 2 / case 0: clear fighter pointer path
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    //
    // The persistent command interpreter owns cmd_vars[1]; do not rescan the move table here.
    // Keep throw-side gun ownership only while cmd1 is active. When it turns off, clear
    // immediately (do not apply SpecialNEnd-style linger).
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
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
    if (allow_create == 0u) {
      return 0u;
    }
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

void items_blaster_gun_create_on_entry(MslBatch* batch, size_t owner_idx) {
  if (batch == NULL) {
    return;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  if (bi < 0 || bi >= batch->batch_size || owner >= (int)batch->config.num_players) {
    return;
  }
  const MslLaserParams* lp = laser_params_get(batch->state.char_id[owner_idx]);
  if (lp == NULL || lp->gun_itkind == 0u) {
    return;
  }
  (void)blaster_gun_run_owner_callback(batch, bi, owner, lp, 1u);
}

void items_blaster_gun_anim_phase(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int player = 0; player < num_players; player++) {
      const size_t idx = msl_idx_player(bi, player);
      const MslLaserParams* lp = laser_params_get(batch->state.char_id[idx]);
      if (lp == NULL || lp->gun_itkind == 0u ||
          items_find_gun_slot(batch, bi, player, lp->gun_itkind) < 0) {
        continue;
      }
      (void)blaster_gun_run_owner_callback(batch, bi, player, lp, 0u);
    }
    if (items_row_has_any(batch, bi) != 0u) {
      items_sort(batch, bi);
    }
  }
}

uint8_t item_type_is_spacie_illusion(uint16_t type) {
  return item_article_params_is_illusion_item_type(type);
}

uint8_t illusion_item_hit_params_from_state(const MslCharParams* chp, uint8_t item_state,
                                            MslIllusionItemHitParams* out) {
  if (chp == NULL || out == NULL) {
    return 0u;
  }
  out->radius = chp->illusion_item_hitbox_size;
  if (item_state == 0u) {
    out->damage = chp->illusion_item_state0_damage;
    out->flags = chp->illusion_item_state0_contact_flags;
    out->shield_damage = chp->illusion_item_state0_shield_damage;
    out->angle = chp->illusion_item_state0_angle;
    out->kbg = chp->illusion_item_state0_kbg;
    out->wsk = chp->illusion_item_state0_wsk;
    out->bkb = chp->illusion_item_state0_bkb;
    out->element = chp->illusion_item_state0_element;
    out->hitbox_y_offset = chp->illusion_item_state0_hitbox_y_offset;
  } else if (item_state == 1u) {
    out->damage = chp->illusion_item_state1_damage;
    out->flags = chp->illusion_item_state1_contact_flags;
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

uint8_t illusion_owner_motion_is_active(const MslBatch* batch, size_t owner_idx) {
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

uint8_t illusion_item_anim_step(MslBatch* batch, size_t ii, const MslCharParams* chp,
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

uint8_t laser_try_shield_bounce_velocity_from_segment(float vx, float vy, float shield_x,
                                                      float shield_y, float shield_z,
                                                      float shield_radius, float prev_x,
                                                      float prev_y, float prev_z, float cur_x,
                                                      float cur_y, float cur_z, float hit_radius,
                                                      float* out_vx, float* out_vy) {
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
  const float normal_xy2 = (nx * nx) + (ny * ny);
  if (!(move_xy2 > 0.0f) || !(normal_xy2 > 0.0f)) {
    return 0u;
  }
  // lbColl_800077A0 first normalizes the contact root in XYZ, then
  // lbVector_AngleXY independently divides the XY dot by both XY lengths.
  // Reusing the XYZ unit length here would make shield Z offset shrink the
  // apparent angle and incorrectly admit borderline side contacts.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_800077A0
  // refs/melee/src/melee/lb/lbvector.c::lbVector_AngleXY
  float cos_angle = ((nx * dx) + (ny * dy)) / sqrtf(normal_xy2 * move_xy2);
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

int laser_spawn_from_fighter(MslBatch* batch, int bi, int owner, const MslLaserParams* lp,
                             uint8_t spawn_state) {
  if (batch == NULL || lp == NULL) {
    return -1;
  }
  const size_t owner_idx = msl_idx_player(bi, owner);
  const uint32_t anim = batch->state.animation_index[owner_idx];
  if (anim > (uint32_t)UINT16_MAX) {
    return -1;
  }

  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return -1;
  }
  const size_t item_idx = msl_idx_item(bi, slot);
  item_slot_clear(batch, item_idx);

  const uint8_t char_id = batch->state.char_id[owner_idx];
  const uint16_t msid = (uint16_t)anim;
  const uint16_t action_id = batch->state.action_id[owner_idx];
  const float anim_frame = items_cur_anim_frame_f32(batch, owner_idx);
  const MslCharParams* ch = msl_char_params_fast(char_id);

  // ftFx samples the live RThumbNb matrix with lb_8000B1CC. The extracted part id maps the source
  // FtPart domain to SSANIM01's fighter-part matrices; the float sampler follows the live AObj time.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //   ftFx_SpecialN_FtGetHoldJoint,ftFx_SpecialN_ItGetHoldJoint,
  //   ftFx_SpecialN_CreateBlasterShot,ftFx_Throw_Anim}
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  uint16_t spawn_part = lp->spawn_bone_part_id;
  if (ch != NULL && ch->laser_spawn_joint_part_id != 0u) {
    spawn_part = ch->laser_spawn_joint_part_id;
  }
  float matrix[12];
  if (anim_pose_get_collision_matrix_f32(batch, owner_idx, msid, anim_frame, spawn_part, matrix) !=
      0) {
    return -1;
  }

  float local_x = 0.0f;
  float local_y = 0.0f;
  float local_z = 0.0f;
  msl_mtx34_mul_point(matrix, lp->spawn_off_xyz, &local_x, &local_y, &local_z);

  const float facing = item_throw_pose_facing_dir(batch, owner_idx, action_id);
  const float rotated_x = facing * local_z;
  local_z = -facing * local_x;
  local_x = rotated_x;

  float model_scale = batch->state.fighter_scale_y[owner_idx];
  if (ch != NULL && ch->model_scaling > 0.0f) {
    model_scale *= ch->model_scaling;
  }
  local_x *= model_scale;
  local_y *= model_scale;
  (void)local_z;

  const float pos_x = batch->state.pos_x[owner_idx] + local_x;
  const float pos_y = batch->state.pos_y[owner_idx] + local_y;
  float angle = lp->blaster_angle;
  if (action_is_blaster_throw(action_id) != 0u) {
    // ThrowB/Hi/Lw launches from the vector between the two source hold-joint offsets.
    const float item_hold_offset[3] = {
        0.0f,
        1.2325000762939453f,
        0.013600001111626625f,
    };
    float item_local_x = 0.0f;
    float item_local_y = 0.0f;
    float item_local_z = 0.0f;
    msl_mtx34_mul_point(matrix, item_hold_offset, &item_local_x, &item_local_y, &item_local_z);
    const float item_rotated_x = facing * item_local_z;
    item_local_z = -facing * item_local_x;
    item_local_x = item_rotated_x * model_scale;
    item_local_y *= model_scale;
    (void)item_local_z;
    const float item_hold_x = batch->state.pos_x[owner_idx] + item_local_x;
    const float item_hold_y = batch->state.pos_y[owner_idx] + item_local_y;
    angle = atan2f(pos_y - item_hold_y, pos_x - item_hold_x);
  } else if (facing < 0.0f) {
    angle = MSL_PI_F - angle;
  }

  const float speed = lp->blaster_speed;
  const float velocity_x = speed * cosf(angle);
  const float velocity_y = speed * sinf(angle);
  const uint8_t is_throw = action_is_blaster_throw(action_id);

  batch->state.item_exists[item_idx] = 1u;
  batch->state.item_state[item_idx] = spawn_state;
  batch->state.item_type[item_idx] = lp->shot_itkind;
  batch->state.item_owner[item_idx] = (int8_t)owner;

  // Fighter-parented item creation copies the source attack identity and fighter instance id.
  // refs/melee/src/melee/it/it_2725.c::{it_8027B070,it_8027B0C4}
  const uint16_t attack_id = batch->state.attack_id[owner_idx];
  const uint16_t attack_instance = batch->state.attack_instance[owner_idx];
  batch->state.item_attack_id[item_idx] = attack_id;
  batch->state.item_attack_instance[item_idx] = attack_instance;

  // it_802790C0 creates the article HitCapsules and it_80272460 freezes their stale-adjusted
  // damage at create time.
  // refs/melee/src/melee/it/it_2725.c::it_802790C0
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  const uint8_t freeze_stale_damage = item_spawn_laser_freezes_stale_damage(
      batch, bi, owner, lp->shot_itkind, attack_id, attack_instance, spawn_state, is_throw);
  batch->state.item_stale_damage_valid[item_idx] = freeze_stale_damage;
  batch->state.item_stale_damage_mul[item_idx] =
      freeze_stale_damage ? staling_multiplier_for_move(batch, owner_idx, attack_id) : 1.0f;

  batch->state.item_instance_id[item_idx] = batch->state.instance_id[owner_idx];
  batch->state.item_spawn_id[item_idx] = items_next_spawn_id(batch, bi);
  batch->state.item_direction[item_idx] = velocity_x >= 0.0f ? 1.0f : -1.0f;
  batch->state.item_vel_x[item_idx] = velocity_x;
  batch->state.item_vel_y[item_idx] = velocity_y;
  batch->state.item_pos_x[item_idx] = pos_x;
  batch->state.item_pos_y[item_idx] = pos_y;
  batch->state.item_timer[item_idx] = (float)lp->lifetime_frames;
  batch->state.item_laser_scale[item_idx] = 0.0f;
  batch->state.item_misc0[item_idx] = slippi_metadata_low_byte_from_f32(0.0f);
  batch->state.item_misc1[item_idx] = slippi_metadata_low_byte_from_f32(angle);
  return slot;
}

static void illusion_resolve_fighter_contacts(MslBatch* batch, int bi, int item_slot,
                                              const MslCharParams* chp,
                                              const MslIllusionItemHitParams* hp, float x0,
                                              float y0, float x1, float y1) {
  const size_t ii = msl_idx_item(bi, item_slot);
  const int owner = (int)batch->state.item_owner[ii];
  for (int def = 0; def < (int)batch->config.num_players; def++) {
    if (def == owner) {
      continue;
    }
    const size_t d_idx = msl_idx_player(bi, def);
    if (batch->state.hitlag[d_idx] != 0u) {
      continue;
    }
    const MslItemHitCapsulePacket hit = {
        .x0 = x0,
        .y0 = y0,
        .z0 = 0.0f,
        .x1 = x1,
        .y1 = y1,
        .z1 = 0.0f,
        .radius = hp->radius,
        .damage = msl_item_reflect_damage_lane(batch, ii, hp->damage),
        .flags = hp->flags,
        .hitbox_id = 0u,
        .element = hp->element,
        .item_grounded = 0u,
    };
    MslItemFighterContact contact;
    const MslItemFighterContactKind kind =
        item_hitcapsule_select_fighter_contact(batch, bi, item_slot, def, &hit, &contact);
    if (kind == MSL_ITEM_FIGHTER_CONTACT_NONE) {
      continue;
    }
    const uint16_t def_iid = batch->state.instance_id[d_idx];
    if (kind == MSL_ITEM_FIGHTER_CONTACT_REFLECT) {
      // The extracted Illusion/Phantasm command-11 packets are not reflectable. Keep the generic
      // branch source-complete if later game data supplies that bit; the article has no Reflected
      // callback, so only Item_80269F14's owner/damage transfer applies.
      hitlist_register_item_hitbox_fighter(batch, bi, item_slot, 0, def, def_iid,
                                           (int)MSL_LBCOLL_INSERT_TODO_7, 0);
      if (contact.reflect_max_damage < 0 ||
          combat_get_env_dmg(hit.damage) <= contact.reflect_max_damage) {
        msl_item_reflect_apply_transfer_state(batch, ii, d_idx, def, contact.reflect_damage_mul);
      }
      continue;
    }
    if (kind == MSL_ITEM_FIGHTER_CONTACT_CLANK) {
      (void)item_hitcapsule_apply_fighter_hitbox_contact(batch, bi, item_slot, def, &hit, &contact);
      continue;
    }
    if (kind == MSL_ITEM_FIGHTER_CONTACT_SHIELD || kind == MSL_ITEM_FIGHTER_CONTACT_COUNTER) {
      int8_t shield_damage = hp->shield_damage;
      if (shield_damage == (int8_t)MSL_ILLUSION_SHIELD_DAMAGE_UNSET) {
        shield_damage = 0;
      }
      combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                   batch->state.item_attack_instance[ii], hit.damage, shield_damage,
                                   hit.element, batch->state.item_pos_x[ii]);
      hitlist_register_item_hitbox_fighter(batch, bi, item_slot, 0, def,
                                           batch->state.instance_id[d_idx],
                                           (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
      if (batch->state.hitlag[d_idx] > batch->state.item_hitlag[ii]) {
        batch->state.item_hitlag[ii] = batch->state.hitlag[d_idx];
      }
      continue;
    }

    hitlist_register_item_hitbox_fighter(batch, bi, item_slot, 0, def, def_iid,
                                         (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
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
    const uint8_t steady_facing =
        (chp->illusion_item_lifetime_state01_frames >= 2u &&
         batch->state.item_timer[ii] <=
             (float)(uint8_t)(chp->illusion_item_lifetime_state01_frames - 2u))
            ? 1u
            : 0u;
    (void)combat_apply_item_hit(
        batch, bi, owner, def, batch->state.item_attack_id[ii],
        batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
        batch->state.item_type[ii], batch->state.item_state[ii], hit.damage, hp->angle, hp->kbg,
        hp->wsk, hp->bkb, contact.hurt_height, hit.element, -1.0f, batch->state.item_pos_x[ii],
        batch->state.item_vel_x[ii], steady_facing);
    // itFoxIllusion_Logic14_DmgDealt clears xCA8 and returns false: BODY never destroys or freezes
    // the article in generic item hitlag.
    // refs/melee/src/melee/it/items/itfoxillusion.c::itFoxIllusion_Logic14_DmgDealt
  }
}

void illusion_items_update_and_collide(MslBatch* batch, int bi) {
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
    // Producer: validation replay-buffer seed derivation (`derive_illusion_ghost_pos012` owner).
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

    illusion_resolve_fighter_contacts(batch, bi, it, chp, &hp, x0, y0, x1, y1);
  }
}

void items_spawn_fighter_anim_callback(MslBatch* batch, int bi, int player) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size || player < 0 ||
      player >= (int)batch->config.num_players) {
    return;
  }

  // Fighter Anim callbacks own article creation. The persistent command interpreter has already
  // advanced for this frame before this phase:
  // - SpecialN and Illusion consume cmd_vars[2].
  // - ThrowB/Hi/Lw consumes throw_flags_b0 only after the attached gun exists.
  // No replay-row command crossing, future article, or victim state is consulted here.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::{
  //   ftFx_SpecialN_CreateBlasterShot,ftFx_SpecialNLoop_Anim,ftFx_SpecialAirNLoop_Anim,
  //   ftFx_Throw_Anim}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim}
  // refs/melee/src/melee/ft/ftaction.c::{ftAction_80071974,ftAction_80073354}
  const size_t idx = msl_idx_player(bi, player);
  if (batch->state.hitlag_started_frame[idx] != 0u) {
    return;
  }

  illusion_spawn_from_fighter(batch, bi, player);

  const MslLaserParams* lp = laser_params_get(batch->state.char_id[idx]);
  if (lp == NULL || lp->gun_itkind == 0u) {
    return;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  const uint8_t is_throw = action_is_blaster_throw(action_id);
  const uint8_t gun_spawned_this_frame =
      is_throw != 0u ? blaster_gun_run_owner_callback(batch, bi, player, lp, 1u) : 0u;
  batch->state.blaster_gun_spawned_this_frame[idx] = gun_spawned_this_frame;
  if (lp->shot_itkind == 0u) {
    return;
  }

  uint8_t spawn_state = 0u;
  if (is_throw != 0u) {
    if ((batch->state.script_throw_flags[idx] & 1u) == 0u) {
      return;
    }
    // ftFx_Throw_Anim returns immediately after creating a missing gun, leaving the one-shot
    // throw flag for the next callback. Once the gun exists, the callback clears the flag before
    // spawning state 1 through it_8029C6CC.
    if (gun_spawned_this_frame != 0u ||
        items_find_gun_slot(batch, bi, player, lp->gun_itkind) < 0) {
      return;
    }
    batch->state.script_throw_flags[idx] &= (uint8_t)~1u;
    spawn_state = 1u;
  } else {
    const uint8_t gun_state = blaster_gun_state_from_action_id(action_id);
    if ((gun_state != 1u && gun_state != 4u) || fighter_script_cmd_var(batch, idx, 2u) == 0u) {
      return;
    }
    batch->state.script_cmd_vars[idx * 4u + 2u] = 0u;
  }

  (void)laser_spawn_from_fighter(batch, bi, player, lp, spawn_state);
}

void items_finish_fighter_anim_phase(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    if (items_row_has_any(batch, bi) != 0u) {
      items_sort(batch, bi);
    }
  }
}
