#include "items_internal.h"

#include <float.h>
#include <math.h>
#include <stdint.h>

#include "action_ids.h"
#include "batch_internal.h"
#include "combat.h"
#include "fighter_script.h"
#include "hit_elements.h"
#include "hitlist.h"
#include "ids.h"
#include "input_axis.h"
#include "item_article_params.h"
#include "msl_math.h"
#include "motion_state_owners.h"
#include "stage_collision.h"

static inline uint8_t zd_din_fighter_is_owner_din_loop(const MslBatch* batch, size_t idx) {
  const uint16_t a = batch->state.action_id[idx];
  return (uint8_t)(a == (uint16_t)MSL_ACT_ZD_SPECIAL_S_LOOP ||
                   a == (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_S_LOOP);
}

static inline uint8_t zd_din_fighter_end_script_releases_article(const MslBatch* batch,
                                                                 size_t idx) {
  if (batch == NULL || batch->state.char_id[idx] != (uint8_t)MSL_CHAR_ID_ZELDA) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[idx];
  if (a != (uint16_t)MSL_ACT_ZD_SPECIAL_S_END && a != (uint16_t)MSL_ACT_ZD_SPECIAL_AIR_S_END) {
    return 0u;
  }
  if (batch->state.frame_start_action_id[idx] != a) {
    return 0u;
  }
  // Din fire state 0's Anim callback polls ftZd_SpecialLw_8013B574, which returns true only on
  // Zelda's Din End actions when script cmd_vars[1] pulses. This keeps the article's release state
  // owned by the item callback instead of the fighter Loop IASA transition.
  // refs/melee/src/melee/it/items/itzeldadinfire.c::itZeldadinfire_UnkMotion0_Anim
  // refs/melee/src/melee/ft/chara/ftZelda/ftZd_SpecialLw.c::ftZd_SpecialLw_8013B574
  return fighter_script_cmd_var(batch, idx, 1u) == 1u ? 1u : 0u;
}

static inline float zd_din_explode_visual_scale(const MslItemArticleParams* ap, float charge) {
  if (ap == NULL || !(ap->zelda_din_explode_charge_max_frames > 0.0f)) {
    return 1.0f;
  }
  return charge * ((ap->zelda_din_explode_scale_max - ap->zelda_din_explode_scale_min) /
                   ap->zelda_din_explode_charge_max_frames) +
         ap->zelda_din_explode_scale_min;
}

static inline uint8_t zd_din_find_owned_fire(const MslBatch* batch, int bi, int owner,
                                             int* out_it) {
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_ZELDA);
  if (batch == NULL || ap == NULL || out_it == NULL) {
    return 0u;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] != 0u &&
        batch->state.item_type[ii] == ap->zelda_din_fire_itkind &&
        batch->state.item_owner[ii] == (int8_t)owner) {
      *out_it = it;
      return 1u;
    }
  }
  return 0u;
}

uint8_t items_zelda_din_fire_article_live(const MslBatch* batch, size_t owner_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  int slot = -1;
  return zd_din_find_owned_fire(batch, bi, owner, &slot);
}

uint8_t items_spawn_zelda_din_fire_article(MslBatch* batch, size_t owner_idx, float spawn_offset_x,
                                           float spawn_offset_y) {
  if (batch == NULL) {
    return 0u;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  int existing = -1;
  if (zd_din_find_owned_fire(batch, bi, owner, &existing)) {
    return 1u;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_ZELDA);
  if (ap == NULL || ap->zelda_din_fire_itkind == 0u) {
    return 0u;
  }
  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, slot);
  item_slot_clear(batch, ii);
  const float facing = batch->state.facing[owner_idx] ? 1.0f : -1.0f;
  batch->state.item_exists[ii] = 1u;
  batch->state.item_type[ii] = ap->zelda_din_fire_itkind;
  batch->state.item_owner[ii] = (int8_t)owner;
  batch->state.item_instance_id[ii] = batch->state.instance_id[owner_idx];
  batch->state.item_attack_id[ii] = batch->state.attack_id[owner_idx];
  batch->state.item_attack_instance[ii] = batch->state.attack_instance[owner_idx];
  batch->state.item_direction[ii] = facing;
  batch->state.item_pos_x[ii] = batch->state.pos_x[owner_idx] + spawn_offset_x * facing;
  batch->state.item_pos_y[ii] = batch->state.pos_y[owner_idx] + spawn_offset_y;
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  batch->state.item_timer[ii] = (float)ap->zelda_din_fire_lifetime_frames;
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_state[ii] = 0u;
  batch->state.item_zelda_din_charge[ii] = 0.0f;
  batch->state.item_zelda_din_angle_offset[ii] = ap->zelda_din_fire_initial_angle_offset * facing;
  batch->state.item_zelda_din_base_angle[ii] = facing > 0.0f ? 0.0f : MSL_PI_F;
  batch->state.item_zelda_din_speed[ii] = ap->zelda_din_fire_initial_speed;
  batch->state.item_vel_x[ii] =
      ap->zelda_din_fire_initial_speed * cosf(batch->state.item_zelda_din_base_angle[ii] +
                                              batch->state.item_zelda_din_angle_offset[ii]);
  batch->state.item_vel_y[ii] =
      ap->zelda_din_fire_initial_speed * sinf(batch->state.item_zelda_din_base_angle[ii] +
                                              batch->state.item_zelda_din_angle_offset[ii]);
  return 1u;
}

static void zd_din_enter_release_state(MslBatch* batch, size_t ii, const MslItemArticleParams* ap) {
  if (batch == NULL || ap == NULL || batch->state.item_state[ii] == 1u) {
    return;
  }
  batch->state.item_state[ii] = 1u;
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  // itZeldadinfire_UnkMotion0_Anim_inline installs state 1 with attrs.x2C, and the item animation
  // lifetime helper consumes that first tick before Slippi's public post-frame item row.
  // refs/melee/src/melee/it/items/itzeldadinfire.c::itZeldadinfire_UnkMotion0_Anim_inline
  batch->state.item_timer[ii] = (ap->zelda_din_fire_release_lifetime_frames > 0u)
                                    ? (float)(ap->zelda_din_fire_release_lifetime_frames - 1u)
                                    : 0.0f;
}

uint8_t items_release_zelda_din_fire_article(MslBatch* batch, size_t owner_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const int bi = (int)(owner_idx / (size_t)MSL_MAX_PLAYERS);
  const int owner = (int)(owner_idx % (size_t)MSL_MAX_PLAYERS);
  int slot = -1;
  if (!zd_din_find_owned_fire(batch, bi, owner, &slot)) {
    return 0u;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_ZELDA);
  zd_din_enter_release_state(batch, msl_idx_item(bi, slot), ap);
  return 1u;
}

static void zd_din_spawn_explosion(MslBatch* batch, int bi, size_t src_ii,
                                   const MslItemArticleParams* ap) {
  if (batch == NULL || ap == NULL) {
    return;
  }
  const int slot = items_alloc_slot(batch, bi);
  if (slot < 0) {
    return;
  }
  const size_t ii = msl_idx_item(bi, slot);
  const float charge = batch->state.item_zelda_din_charge[src_ii];
  const int8_t owner = batch->state.item_owner[src_ii];
  item_slot_clear(batch, ii);
  batch->state.item_exists[ii] = 1u;
  batch->state.item_type[ii] = ap->zelda_din_fire_explode_itkind;
  batch->state.item_owner[ii] = owner;
  batch->state.item_instance_id[ii] = batch->state.item_instance_id[src_ii];
  batch->state.item_attack_id[ii] = batch->state.item_attack_id[src_ii];
  batch->state.item_attack_instance[ii] = batch->state.item_attack_instance[src_ii];
  batch->state.item_direction[ii] = batch->state.item_direction[src_ii];
  batch->state.item_pos_x[ii] = batch->state.item_pos_x[src_ii];
  batch->state.item_pos_y[ii] = batch->state.item_pos_y[src_ii];
  // it_802C46C4 initializes lifeTimer to 60 and immediately runs the explosion Anim callback;
  // the public same-frame item row has already consumed that first timer tick.
  // refs/melee/src/melee/it/items/itzeldadinfireexplode.c::{it_802C46C4,
  //   itZeldadinfireexplode_UnkMotion0_Anim}
  batch->state.item_timer[ii] = 59.0f;
  batch->state.item_spawn_id[ii] = items_next_spawn_id(batch, bi);
  batch->state.item_state[ii] = 0u;
  batch->state.item_zelda_din_charge[ii] = charge;
  batch->state.item_zelda_din_explode_base_size[ii] = ap->zelda_din_explode_hitbox_size;
}

static uint8_t zd_din_explosion_collide_fighters(MslBatch* batch, int bi, int item_slot,
                                                 const MslItemArticleParams* ap) {
  if (batch == NULL || ap == NULL || ap->zelda_din_explode_hitbox_count == 0u) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, item_slot);
  const int owner = (int)batch->state.item_owner[ii];
  if (owner < 0 || owner >= (int)batch->config.num_players) {
    return 0u;
  }
  const float charge = batch->state.item_zelda_din_charge[ii];
  const float scale = zd_din_explode_visual_scale(ap, charge);
  // itZeldadinfireexplode_UnkMotion0_Anim passes the computed float through it_80272460's u32
  // damage argument before collision consumes the HitCapsule damage lane.
  // refs/melee/src/melee/it/items/itzeldadinfireexplode.c::itZeldadinfireexplode_UnkMotion0_Anim
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  const float damage_f =
      charge * ap->zelda_din_explode_damage_charge_mul + ap->zelda_din_explode_damage_base;
  const float damage = (float)(uint32_t)(damage_f > 0.0f ? damage_f : 0.0f);
  const float base_size = batch->state.item_zelda_din_explode_base_size[ii] > 0.0f
                              ? batch->state.item_zelda_din_explode_base_size[ii]
                              : ap->zelda_din_explode_hitbox_size;
  const float radius = base_size * scale;
  const float hx = batch->state.item_pos_x[ii] + ap->zelda_din_explode_hitbox_x_offset;
  const float hy = batch->state.item_pos_y[ii] + ap->zelda_din_explode_hitbox_y_offset;
  const float hz = ap->zelda_din_explode_hitbox_z_offset;
  const MslItemHitCapsulePacket hit = {
      .x0 = hx,
      .y0 = hy,
      .z0 = hz,
      .x1 = hx,
      .y1 = hy,
      .z1 = hz,
      .radius = radius,
      .damage = msl_item_reflect_damage_lane(batch, ii, damage),
      .flags = ap->zelda_din_explode_hitbox_flags,
      .hitbox_id = 0u,
      .element = ap->zelda_din_explode_hitbox_element,
      .item_grounded = 0u,
  };
  uint8_t destroy_after_callback = 0u;
  uint8_t body_callback_pending = 0u;
  int pending_reflector = -1;
  float pending_reflect_damage_mul = 1.0f;
  for (int def = 0; def < (int)batch->config.num_players; def++) {
    if (def == owner) {
      continue;
    }
    const size_t d_idx = msl_idx_player(bi, def);
    const uint16_t def_iid = batch->state.instance_id[d_idx];
    MslItemFighterContact contact;
    const MslItemFighterContactKind kind =
        item_hitcapsule_select_fighter_contact(batch, bi, item_slot, def, &hit, &contact);
    if (kind == MSL_ITEM_FIGHTER_CONTACT_NONE) {
      continue;
    }
    if (kind == MSL_ITEM_FIGHTER_CONTACT_REFLECT) {
      hitlist_register_item_hitbox_fighter(batch, bi, item_slot, 0, def, def_iid,
                                           (int)MSL_LBCOLL_INSERT_TODO_7, 0);
      if (contact.reflect_max_damage >= 0 &&
          combat_get_env_dmg(hit.damage) > contact.reflect_max_damage) {
        body_callback_pending = 1u;
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
        destroy_after_callback = 1u;
      }
      continue;
    }
    if (kind == MSL_ITEM_FIGHTER_CONTACT_SHIELD || kind == MSL_ITEM_FIGHTER_CONTACT_COUNTER) {
      combat_apply_item_shield_hit(batch, bi, owner, def, batch->state.item_attack_id[ii],
                                   batch->state.item_attack_instance[ii], hit.damage,
                                   ap->zelda_din_explode_hitbox_shield_damage, hit.element,
                                   batch->state.item_pos_x[ii]);
      hitlist_register_item_hitbox_fighter(batch, bi, item_slot, 0, def,
                                           batch->state.instance_id[d_idx],
                                           (int)MSL_LBCOLL_INSERT_FT_SHIELD, 0);
      destroy_after_callback = 1u;
      continue;
    }

    hitlist_register_item_hitbox_fighter(batch, bi, item_slot, 0, def, def_iid,
                                         (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
    body_callback_pending = 1u;
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
        batch->state.item_type[ii], batch->state.item_state[ii], hit.damage,
        ap->zelda_din_explode_hitbox_angle, ap->zelda_din_explode_hitbox_kbg,
        ap->zelda_din_explode_hitbox_wsk, ap->zelda_din_explode_hitbox_bkb, contact.hurt_height,
        ap->zelda_din_explode_hitbox_element, -1.0f, batch->state.item_pos_x[ii],
        batch->state.item_vel_x[ii], 1u);
    if (res == MSL_ITEM_HIT_APPLIED_CONSUME_ITEM) {
      destroy_after_callback = 1u;
    }
  }
  // Item_8026A294 collapses the completed fighter traversal to one callback class. Din's
  // shield/clank callbacks destroy, its DmgDealt callback is NULL, and its Reflected callback is
  // NULL even though Item_80269F14 still transfers ownership and rebuilds HitCapsule damage.
  // refs/melee/src/melee/it/item.c::{Item_80269DC8,Item_80269F14,Item_8026A294}
  // refs/melee/src/melee/it/it_279C.c::itZeldaDinFireExplode
  if (destroy_after_callback != 0u) {
    return 1u;
  }
  if (body_callback_pending == 0u && pending_reflector >= 0) {
    msl_item_reflect_apply_transfer_state(batch, ii, msl_idx_player(bi, pending_reflector),
                                          pending_reflector, pending_reflect_damage_mul);
  }
  return 0u;
}

static void zd_din_fire_update_one(MslBatch* batch, int bi, int it,
                                   const MslItemArticleParams* ap) {
  const size_t ii = msl_idx_item(bi, it);
  if (batch->state.item_hitlag[ii] != 0u) {
    batch->state.item_hitlag[ii]--;
    return;
  }
  if (batch->state.item_state[ii] == 0u) {
    float charge = batch->state.item_zelda_din_charge[ii] + 1.0f;
    if (charge > ap->zelda_din_fire_charge_max_frames) {
      charge = ap->zelda_din_fire_charge_max_frames;
    }
    batch->state.item_zelda_din_charge[ii] = charge;
    const int owner = (int)batch->state.item_owner[ii];
    if (owner >= 0 && owner < (int)batch->config.num_players) {
      const size_t o_idx = msl_idx_player(bi, owner);
      if (zd_din_fighter_end_script_releases_article(batch, o_idx) != 0u) {
        zd_din_enter_release_state(batch, ii, ap);
        return;
      }
      if (zd_din_fighter_is_owner_din_loop(batch, o_idx) != 0u) {
        const float stick_x = stick_i8_to_unit(batch->state.input_main_x[o_idx]);
        if (fabsf(stick_x) > ap->zelda_din_fire_stick_threshold) {
          float angle =
              batch->state.item_zelda_din_angle_offset[ii] +
              batch->state.item_direction[ii] * ap->zelda_din_fire_stick_angle_mul * stick_x;
          if (fabsf(angle) > ap->zelda_din_fire_angle_max) {
            angle = angle > 0.0f ? ap->zelda_din_fire_angle_max : -ap->zelda_din_fire_angle_max;
          }
          batch->state.item_zelda_din_angle_offset[ii] = angle;
        }
        float speed = batch->state.item_zelda_din_speed[ii] + ap->zelda_din_fire_accel;
        if (speed > ap->zelda_din_fire_speed_max) {
          speed = ap->zelda_din_fire_speed_max;
        }
        batch->state.item_zelda_din_speed[ii] = speed;
        const float angle = batch->state.item_zelda_din_base_angle[ii] +
                            batch->state.item_zelda_din_angle_offset[ii];
        batch->state.item_vel_x[ii] = speed * cosf(angle);
        batch->state.item_vel_y[ii] = speed * sinf(angle);
      }
    }
    const float old_x = batch->state.item_pos_x[ii];
    const float old_y = batch->state.item_pos_y[ii];
    batch->state.item_pos_x[ii] += batch->state.item_vel_x[ii];
    batch->state.item_pos_y[ii] += batch->state.item_vel_y[ii];
    float hit_x = 0.0f;
    float hit_y = 0.0f;
    if (stage_collision_item_line_hit_runtime(batch, bi, old_x, old_y, batch->state.item_pos_x[ii],
                                              batch->state.item_pos_y[ii], &hit_x, &hit_y, NULL,
                                              NULL)) {
      batch->state.item_pos_x[ii] = hit_x;
      batch->state.item_pos_y[ii] = hit_y;
      zd_din_enter_release_state(batch, ii, ap);
      return;
    }
    if (batch->state.item_timer[ii] <= 1.0f) {
      zd_din_enter_release_state(batch, ii, ap);
      return;
    }
    batch->state.item_timer[ii] -= 1.0f;
    return;
  }
  if (batch->state.item_timer[ii] <= 1.0f) {
    zd_din_spawn_explosion(batch, bi, ii, ap);
    item_slot_clear(batch, ii);
  } else {
    batch->state.item_timer[ii] -= 1.0f;
  }
}

static void zd_din_explode_update_one(MslBatch* batch, int bi, int it,
                                      const MslItemArticleParams* ap) {
  const size_t ii = msl_idx_item(bi, it);
  if (batch->state.item_hitlag[ii] != 0u) {
    batch->state.item_hitlag[ii]--;
    return;
  }
  if (zd_din_explosion_collide_fighters(batch, bi, it, ap) != 0u) {
    item_slot_clear(batch, ii);
    return;
  }
  batch->state.item_vel_x[ii] = 0.0f;
  batch->state.item_vel_y[ii] = 0.0f;
  if (batch->state.item_timer[ii] <= 1.0f) {
    item_slot_clear(batch, ii);
  } else {
    batch->state.item_timer[ii] -= 1.0f;
  }
}

void zelda_din_fire_update_and_collide(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_ZELDA);
  if (ap == NULL) {
    return;
  }
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] == 0u) {
      continue;
    }
    if (batch->state.item_type[ii] == ap->zelda_din_fire_itkind) {
      zd_din_fire_update_one(batch, bi, it, ap);
    } else if (batch->state.item_type[ii] == ap->zelda_din_fire_explode_itkind) {
      zd_din_explode_update_one(batch, bi, it, ap);
    }
  }
}
