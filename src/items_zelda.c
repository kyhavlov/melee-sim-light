#include "items_internal.h"

#include <float.h>
#include <math.h>
#include <stdint.h>

#include "action_ids.h"
#include "batch_internal.h"
#include "combat.h"
#include "hit_elements.h"
#include "hitlist.h"
#include "ids.h"
#include "input_axis.h"
#include "item_article_params.h"
#include "msl_math.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "stage_collision.h"

enum {
  MSL_ZELDA_DIN_HITBOX_FLAG_TARGET_GROUNDED = 1u << 0,
  MSL_ZELDA_DIN_HITBOX_FLAG_TARGET_AERIAL = 1u << 1,
  MSL_ZELDA_DIN_HITBOX_FLAG_BODY_ENABLED = 1u << 2,
};

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
  return move_tables_special_cmd_var_value_at_frame(
             batch->state.char_id[idx], msl_motion_state_submotion_id(batch->state.char_id[idx], a),
             1u, batch->state.anim_frame_f32[idx]) == 1u
             ? 1u
             : 0u;
}

static inline uint8_t zd_din_targets_ground_state(uint32_t flags, uint8_t defender_grounded) {
  return defender_grounded
             ? ((flags & (uint32_t)MSL_ZELDA_DIN_HITBOX_FLAG_TARGET_GROUNDED) != 0u ? 1u : 0u)
             : ((flags & (uint32_t)MSL_ZELDA_DIN_HITBOX_FLAG_TARGET_AERIAL) != 0u ? 1u : 0u);
}

static inline float zd_din_fire_visual_scale(const MslItemArticleParams* ap, float charge) {
  if (ap == NULL || !(ap->zelda_din_fire_charge_max_frames > 0.0f)) {
    return 1.0f;
  }
  return charge * ((ap->zelda_din_fire_scale_max - ap->zelda_din_fire_scale_min) /
                   ap->zelda_din_fire_charge_max_frames) +
         ap->zelda_din_fire_scale_min;
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

static uint8_t zd_din_explosion_try_hit_fighters(MslBatch* batch, int bi, int item_slot,
                                                 const MslItemArticleParams* ap) {
  if (batch == NULL || ap == NULL || ap->zelda_din_explode_hitbox_count == 0u) {
    return 0u;
  }
  const size_t ii = msl_idx_item(bi, item_slot);
  const int owner = (int)batch->state.item_owner[ii];
  if (owner < 0 || owner >= (int)batch->config.num_players) {
    return 0u;
  }
  const uint32_t flags = ap->zelda_din_explode_hitbox_flags;
  if ((flags & (uint32_t)MSL_ZELDA_DIN_HITBOX_FLAG_BODY_ENABLED) == 0u) {
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
  for (int def = 0; def < (int)batch->config.num_players; def++) {
    if (def == owner) {
      continue;
    }
    const size_t d_idx = msl_idx_player(bi, def);
    if (batch->state.hurtbox_state[d_idx] != 0u ||
        !zd_din_targets_ground_state(flags, (uint8_t)(batch->state.on_ground[d_idx] != 0u))) {
      continue;
    }
    const uint16_t def_iid = batch->state.instance_id[d_idx];
    if (!hitlist_allows_item_hitbox_fighter(batch, bi, item_slot, 0, def, def_iid)) {
      continue;
    }
    uint8_t hurt_height = 0u;
    uint8_t hit = 0u;
    for (uint8_t ci = 0; ci < batch->state.hurtcap_count[d_idx]; ci++) {
      if (item_swept_sphere_capsule_intersects(batch, bi, def, hx, hy, hx, hy, radius, (int)ci,
                                               &hurt_height)) {
        hit = 1u;
        break;
      }
    }
    if (hit == 0u) {
      continue;
    }
    const MslItemHitResult res = combat_apply_item_hit(
        batch, bi, owner, def, batch->state.item_attack_id[ii],
        batch->state.item_attack_instance[ii], batch->state.item_instance_id[ii],
        batch->state.item_type[ii], batch->state.item_state[ii], damage,
        ap->zelda_din_explode_hitbox_angle, ap->zelda_din_explode_hitbox_kbg,
        ap->zelda_din_explode_hitbox_wsk, ap->zelda_din_explode_hitbox_bkb, hurt_height,
        ap->zelda_din_explode_hitbox_element, -1.0f, batch->state.item_pos_x[ii],
        batch->state.item_pos_y[ii], radius, batch->state.item_vel_x[ii], 1u);
    if (res != MSL_ITEM_HIT_NONE) {
      const uint16_t def_iid_post = batch->state.instance_id[d_idx];
      hitlist_register_item_fighter(batch, bi, item_slot, def, def_iid_post,
                                    (int)MSL_LBCOLL_INSERT_FT_BODY, 0);
      return 1u;
    }
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
    if (stage_collision_item_line_hit_floor(batch->state.stage_id[bi], old_x, old_y,
                                            batch->state.item_pos_x[ii],
                                            batch->state.item_pos_y[ii], &hit_x, &hit_y)) {
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
  (void)zd_din_explosion_try_hit_fighters(batch, bi, it, ap);
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
