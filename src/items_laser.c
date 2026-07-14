#include "items_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "action_ids.h"
#include "anim_timebase.h"
#include "char_params.h"
#include "combat.h"
#include "combat_geom.h"
#include "common_params.h"
#include "guard_lifecycle.h"
#include "hitlist.h"
#include "item_reflect.h"
#include "laser_params.h"
#include "motion_state_owners.h"
#include "special_msids.h"
#include "stage_collision.h"

// Source-shaped Fox/Falco laser article owner.
//
// Article callbacks:
//   refs/melee/src/melee/it/items/itfoxlaser.c
// Common fighter/item traversal and callbacks:
//   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
//   refs/melee/src/melee/it/item.c::{Item_80269DC8,Item_80269F14,Item_8026A294}
//   refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}

enum {
  // `it_802790C0` copies these bits from the article create-HitCapsule command.
  MSL_ITEM_HIT_X41_B7_REFLECTABLE = UINT32_C(1) << 20,
  MSL_ITEM_HIT_X42_B0_ABSORBABLE = UINT32_C(1) << 19,
  MSL_ITEM_HIT_X42_B1_SHIELDABLE = UINT32_C(1) << 18,
  MSL_ITEM_HIT_X42_B2_FACING_FILTER = UINT32_C(1) << 17,
  MSL_ITEM_HIT_X42_B3_SHIELD_BOUNCE = UINT32_C(1) << 16,
  MSL_ITEM_HIT_X42_B4_SHIELD_FILTER = UINT32_C(1) << 15,
  MSL_ITEM_HIT_X42_B5_BODY_ENABLED = UINT32_C(1) << 14,
};

typedef struct MslLaserFrame {
  const MslLaserParams* params;
  float prev_x;
  float prev_y;
  float x;
  float y;
  float prev_scale;
  float scale;
  float ux;
  float uy;
  float bounce_vx;
  float bounce_vy;
  uint8_t state;
  uint8_t active;
  uint8_t stage_hit;
  uint8_t destroy_after_contact;
  uint8_t shield_bounce;
  uint8_t reflected;
} MslLaserFrame;

static inline uint8_t laser_hitbox_count(const MslLaserParams* p, uint8_t state) {
  const uint8_t n = state == 0u ? p->hitbox_offsets_x_count : p->state1_hitbox_offsets_x_count;
  return n > (uint8_t)MSL_MAX_HITBOXES ? (uint8_t)MSL_MAX_HITBOXES : n;
}

static inline float laser_hitbox_offset(const MslLaserParams* p, uint8_t state, int hitbox) {
  return state == 0u ? p->hitbox_offsets_x[hitbox] : p->state1_hitbox_offsets_x[hitbox];
}

static inline float laser_hitbox_radius(const MslLaserParams* p, uint8_t state, int hitbox) {
  const float authored = state == 0u ? p->hitbox_sizes[hitbox] : p->state1_hitbox_sizes[hitbox];
  return authored * p->item_scale;
}

static inline uint32_t laser_hitbox_word4(const MslLaserParams* p, uint8_t state, int hitbox) {
  return state == 0u ? p->hitbox_word4_raw[hitbox] : p->state1_hitbox_word4_raw[hitbox];
}

static inline uint32_t laser_hitbox_flags(const MslLaserParams* p, uint8_t state, int hitbox) {
  return state == 0u ? p->hitbox_flags_raw[hitbox] : p->state1_hitbox_flags_raw[hitbox];
}

static inline uint8_t laser_hitbox_group(const MslLaserParams* p, uint8_t state, int hitbox) {
  return state == 0u ? p->hitbox_groups[hitbox] : p->state1_hitbox_groups[hitbox];
}

static inline uint8_t laser_hitbox_id(const MslLaserParams* p, uint8_t state, int hitbox) {
  return state == 0u ? p->hitbox_ids[hitbox] : p->state1_hitbox_ids[hitbox];
}

static inline uint8_t laser_hitbox_rehit(const MslLaserParams* p, uint8_t state, int hitbox) {
  return (uint8_t)(laser_hitbox_flags(p, state, hitbox) >> 24);
}

static inline uint32_t laser_hitbox_contact_flags(const MslLaserParams* p, uint8_t state,
                                                  int hitbox) {
  const uint32_t raw = laser_hitbox_flags(p, state, hitbox);
  const uint32_t word4 = laser_hitbox_word4(p, state, hitbox);
  const uint16_t clank_mask = state == 0u ? p->hitbox_clank_mask : p->state1_hitbox_clank_mask;
  uint32_t out = 0u;
  if ((word4 & 2u) != 0u) {
    out |= (uint32_t)MSL_ITEM_CONTACT_TARGET_GROUNDED;
  }
  if ((word4 & 1u) != 0u) {
    out |= (uint32_t)MSL_ITEM_CONTACT_TARGET_AERIAL;
  }
  if ((raw & (uint32_t)MSL_ITEM_HIT_X42_B5_BODY_ENABLED) != 0u) {
    out |= (uint32_t)MSL_ITEM_CONTACT_BODY_ENABLED;
  }
  if ((clank_mask & (uint16_t)(1u << hitbox)) != 0u) {
    out |= (uint32_t)MSL_ITEM_CONTACT_CLANK;
  }
  if ((raw & (uint32_t)MSL_ITEM_HIT_X41_B7_REFLECTABLE) != 0u) {
    out |= (uint32_t)MSL_ITEM_CONTACT_REFLECTABLE;
  }
  if ((raw & (uint32_t)MSL_ITEM_HIT_X42_B0_ABSORBABLE) != 0u) {
    out |= (uint32_t)MSL_ITEM_CONTACT_ABSORBABLE;
  }
  if ((raw & (uint32_t)MSL_ITEM_HIT_X42_B1_SHIELDABLE) != 0u) {
    out |= (uint32_t)MSL_ITEM_CONTACT_SHIELDABLE;
  }
  if ((raw & (uint32_t)MSL_ITEM_HIT_X42_B2_FACING_FILTER) != 0u) {
    out |= (uint32_t)MSL_ITEM_CONTACT_FACING_FILTER;
  }
  if ((raw & (uint32_t)MSL_ITEM_HIT_X42_B3_SHIELD_BOUNCE) != 0u) {
    out |= (uint32_t)MSL_ITEM_CONTACT_SHIELD_BOUNCE;
  }
  if ((raw & (uint32_t)MSL_ITEM_HIT_X42_B4_SHIELD_FILTER) != 0u) {
    out |= (uint32_t)MSL_ITEM_CONTACT_SHIELD_X42_B4;
  }
  return out;
}

static inline float laser_hitbox_damage(const MslLaserFrame* frame, int hitbox, float age) {
  const MslLaserParams* p = frame->params;
  if (frame->state != 0u) {
    return p->state1_damage;
  }
  const uint8_t id = laser_hitbox_id(p, frame->state, hitbox);
  if (p->damage_update_frame != 0u && p->damage_update_damage > 0.0f &&
      id < (uint8_t)MSL_LASER_MAX_HITBOX_OFFS_X &&
      (p->damage_update_hitbox_mask & (uint16_t)(1u << id)) != 0u &&
      age >= (float)p->damage_update_frame) {
    // it_80279544 mutates the already-created capsule selected by script id.
    // refs/melee/src/melee/it/it_2725.c::{it_802790C0,it_80279544}
    return p->damage_update_damage;
  }
  return p->damage;
}

static inline void laser_hitbox_segment(const MslLaserFrame* frame, int hitbox, float* ax,
                                        float* ay, float* bx, float* by) {
  const float offset = laser_hitbox_offset(frame->params, frame->state, hitbox);
  const float prev_offset = offset * frame->prev_scale;
  const float cur_offset = offset * frame->scale;
  *ax = frame->prev_x + frame->ux * prev_offset;
  *ay = frame->prev_y + frame->uy * prev_offset;
  *bx = frame->x + frame->ux * cur_offset;
  *by = frame->y + frame->uy * cur_offset;
}

static void laser_register_group(MslBatch* batch, int bi, int item_slot, const MslLaserFrame* frame,
                                 int selected_hitbox, int defender, uint16_t defender_iid,
                                 int insert_kind) {
  const uint8_t group = laser_hitbox_group(frame->params, frame->state, selected_hitbox);
  const uint8_t rehit = laser_hitbox_rehit(frame->params, frame->state, selected_hitbox);
  const int n = (int)laser_hitbox_count(frame->params, frame->state);
  for (int hb = 0; hb < n; hb++) {
    if (laser_hitbox_group(frame->params, frame->state, hb) != group) {
      continue;
    }
    hitlist_register_item_hitbox_fighter(batch, bi, item_slot,
                                         (int)laser_hitbox_id(frame->params, frame->state, hb),
                                         defender, defender_iid, insert_kind, rehit);
  }
}

static void laser_enter_reflector_hit(MslBatch* batch, int bi, int item_slot, int reflector) {
  const size_t ii = msl_idx_item(bi, item_slot);
  const size_t idx = msl_idx_player(bi, reflector);
  const uint8_t kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[idx], batch->state.action_id[idx]);
  if (kind != (uint8_t)MSL_FX_KIND_SPECIAL_LW_LOOP && kind != (uint8_t)MSL_FX_KIND_SPECIAL_LW_HIT &&
      kind != (uint8_t)MSL_FX_KIND_SPECIAL_LW_TURN &&
      kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_LOOP &&
      kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_HIT &&
      kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_TURN) {
    return;
  }
  const MslSpecialMsids* msids = msl_special_msids(batch->state.char_id[idx]);
  if (msids == NULL) {
    return;
  }
  const float vx = batch->state.item_vel_x[ii];
  const float dir = vx > 0.0f ? -1.0f
                    : vx < 0.0f
                        ? 1.0f
                        : (batch->state.item_pos_x[ii] > batch->state.pos_x[idx] ? -1.0f : 1.0f);
  batch->state.facing[idx] = (uint8_t)(dir > 0.0f);
  if (batch->state.on_ground[idx] != 0u) {
    batch->state.action_id[idx] = msl_motion_state_action_for_fx_kind(
        batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_LW_HIT);
    batch->state.animation_index[idx] = (uint32_t)msids->speciallw_ground_hit;
  } else {
    batch->state.action_id[idx] = msl_motion_state_action_for_fx_kind(
        batch->state.char_id[idx], (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_HIT);
    batch->state.animation_index[idx] = (uint32_t)msids->speciallw_air_hit;
  }
  msl_anim_timebase_enter(batch, idx, 0.0f, 1.0f);
}

static uint8_t laser_try_fighter_contact(MslBatch* batch, int bi, int item_slot, int defender,
                                         MslLaserFrame* frame, float age) {
  const size_t ii = msl_idx_item(bi, item_slot);
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint16_t defender_iid = batch->state.instance_id[d_idx];
  const int n = (int)laser_hitbox_count(frame->params, frame->state);
  for (int hb = 0; hb < n; hb++) {
    const float radius = laser_hitbox_radius(frame->params, frame->state, hb);
    const float base_damage = laser_hitbox_damage(frame, hb, age);
    if (!(radius > 0.0f) || !(base_damage > 0.0f)) {
      continue;
    }
    float ax = 0.0f, ay = 0.0f, bx = 0.0f, by = 0.0f;
    laser_hitbox_segment(frame, hb, &ax, &ay, &bx, &by);
    const uint8_t element =
        frame->state == 0u ? frame->params->element : frame->params->state1_element;
    const MslItemHitCapsulePacket hit = {
        .x0 = ax,
        .y0 = ay,
        .z0 = 0.0f,
        .x1 = bx,
        .y1 = by,
        .z1 = 0.0f,
        .radius = radius,
        .damage = msl_item_reflect_damage_lane(batch, ii, base_damage),
        .flags = laser_hitbox_contact_flags(frame->params, frame->state, hb),
        .hitbox_id = laser_hitbox_id(frame->params, frame->state, hb),
        .element = element,
        .item_grounded = 0u,
    };
    MslItemFighterContact contact;
    const MslItemFighterContactKind kind =
        item_hitcapsule_select_fighter_contact(batch, bi, item_slot, defender, &hit, &contact);
    if (kind == MSL_ITEM_FIGHTER_CONTACT_NONE) {
      continue;
    }
    if (kind == MSL_ITEM_FIGHTER_CONTACT_REFLECT) {
      if (contact.reflect_max_damage >= 0 &&
          combat_get_env_dmg(hit.damage) > contact.reflect_max_damage) {
        frame->destroy_after_contact = 1u;
      } else {
        msl_item_reflect_stage_snapshot_defer_velocity(batch, ii, defender,
                                                       contact.reflect_damage_mul);
        batch->state.item_reflect_body_owner_port[ii] = (uint8_t)defender;
        batch->state.item_reflect_body_attack_id[ii] = batch->state.attack_id[d_idx];
        batch->state.item_reflect_body_attack_instance[ii] = batch->state.attack_instance[d_idx];
        batch->state.item_reflect_body_damage_valid[ii] = 1u;
        laser_enter_reflector_hit(batch, bi, item_slot, defender);
        frame->reflected = 1u;
      }
      laser_register_group(batch, bi, item_slot, frame, hb, defender, defender_iid,
                           (int)MSL_LBCOLL_INSERT_TODO_7);
      return 1u;
    }
    if (kind == MSL_ITEM_FIGHTER_CONTACT_CLANK) {
      const uint8_t outcomes = item_hitcapsule_apply_fighter_hitbox_contact(
          batch, bi, item_slot, defender, &hit, &contact);
      if ((outcomes & (uint8_t)MSL_ITEM_HITBOX_CONTACT_ITEM_RECEIVED) != 0u) {
        frame->destroy_after_contact = 1u;
      }
      return 1u;
    }
    if (kind == MSL_ITEM_FIGHTER_CONTACT_SHIELD || kind == MSL_ITEM_FIGHTER_CONTACT_COUNTER) {
      float bounce_vx = 0.0f;
      float bounce_vy = 0.0f;
      const uint8_t can_bounce =
          ((hit.flags & (uint32_t)MSL_ITEM_CONTACT_SHIELD_BOUNCE) != 0u &&
           laser_try_shield_bounce_velocity_from_segment(
               batch->state.item_vel_x[ii], batch->state.item_vel_y[ii], contact.descriptor_x,
               contact.descriptor_y, contact.descriptor_z, contact.descriptor_radius, ax, ay, 0.0f,
               bx, by, 0.0f, radius, &bounce_vx, &bounce_vy) != 0u)
              ? 1u
              : 0u;
      const int8_t shield_damage =
          frame->state == 0u ? frame->params->shield_damage : frame->params->state1_shield_damage;
      combat_apply_item_shield_hit(
          batch, bi, (int)batch->state.item_owner[ii], defender, batch->state.item_attack_id[ii],
          batch->state.item_attack_instance[ii], hit.damage, shield_damage, element, frame->x);
      laser_register_group(batch, bi, item_slot, frame, hb, defender,
                           batch->state.instance_id[d_idx], (int)MSL_LBCOLL_INSERT_FT_SHIELD);
      if (can_bounce != 0u && frame->shield_bounce == 0u) {
        frame->shield_bounce = 1u;
        frame->bounce_vx = bounce_vx;
        frame->bounce_vy = bounce_vy;
      } else if (can_bounce == 0u) {
        frame->destroy_after_contact = 1u;
      }
      return 1u;
    }

    laser_register_group(batch, bi, item_slot, frame, hb, defender, batch->state.instance_id[d_idx],
                         (int)MSL_LBCOLL_INSERT_FT_BODY);
    if (batch->state.hurtbox_state[d_idx] == (uint8_t)MSL_HURTCAPS_DISABLED ||
        contact.hurt_status == (uint8_t)MSL_HURTCAPS_DISABLED) {
      frame->destroy_after_contact = 1u;
      return 1u;
    }
    const MslCommonParams* common = msl_common_params();
    if (common != NULL && contact.body_overlap > 0.0f &&
        contact.body_overlap < common->phantom_overlap_max_x7a8) {
      combat_apply_item_phantom_hit(batch, bi, (int)batch->state.item_owner[ii], defender,
                                    batch->state.item_attack_id[ii],
                                    batch->state.item_instance_id[ii], hit.damage, element);
      return 1u;
    }

    uint16_t attack_id = batch->state.item_attack_id[ii];
    uint16_t attack_instance = batch->state.item_attack_instance[ii];
    const uint8_t reflected_owner = batch->state.item_reflect_body_owner_port[ii];
    if (reflected_owner < (uint8_t)batch->config.num_players) {
      attack_id = batch->state.item_reflect_body_attack_id[ii];
      attack_instance = batch->state.item_reflect_body_attack_instance[ii];
    }
    const size_t stale_owner_idx = msl_idx_player(bi, (int)batch->state.item_owner[ii]);
    const float stale_mul = item_hitcapsule_stale_damage_mul(batch, ii, stale_owner_idx,
                                                             batch->state.item_attack_id[ii]);
    const uint16_t angle = frame->state == 0u ? frame->params->angle : frame->params->state1_angle;
    const uint16_t kbg = frame->state == 0u ? frame->params->kbg : frame->params->state1_kbg;
    const uint16_t wsk = frame->state == 0u ? frame->params->wsk : frame->params->state1_wsk;
    const uint16_t bkb = frame->state == 0u ? frame->params->bkb : frame->params->state1_bkb;
    const MslItemHitResult result = combat_apply_item_hit(
        batch, bi, (int)batch->state.item_owner[ii], defender, attack_id, attack_instance,
        batch->state.item_instance_id[ii], batch->state.item_type[ii], frame->state, hit.damage,
        angle, kbg, wsk, bkb, contact.hurt_height, element, stale_mul, frame->x,
        batch->state.item_vel_x[ii], 1u);
    if (result == MSL_ITEM_HIT_APPLIED_CONSUME_ITEM) {
      frame->destroy_after_contact = 1u;
    }
    return result != MSL_ITEM_HIT_NONE ? 1u : 0u;
  }
  return 0u;
}

void lasers_source_update_and_collide(MslBatch* batch, int bi) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return;
  }
  MslLaserFrame frames[MSL_MAX_ITEMS];
  uint8_t laser_slots[MSL_MAX_ITEMS];
  uint8_t laser_count = 0u;
  const int players = (int)batch->config.num_players;
  const uint32_t stage_id = batch->state.stage_id[bi];
  MslStageBounds blast = {0};
  const uint8_t have_blast = stage_collision_get_blast_bounds_world(stage_id, &blast);

  // Item prio 0/1/4/5: pending callbacks, Anim, Phys snapshot, motion, and stage collision.
  for (int item_slot = 0; item_slot < MSL_MAX_ITEMS; item_slot++) {
    const size_t ii = msl_idx_item(bi, item_slot);
    const MslLaserParams* params = batch->state.item_exists[ii]
                                       ? laser_params_for_item_type(batch->state.item_type[ii])
                                       : NULL;
    if (params == NULL) {
      continue;
    }
    // The source walks the live item GObj list, not every empty fixed-capacity storage slot. Build
    // that deterministic ascending-slot view once, then carry the same per-item callback packet
    // through fighter traversal and ProcessHit collapse.
    // refs/melee/src/melee/it/item.c::{Item_802693E4,Item_80269528,Item_802697D4}
    laser_slots[laser_count] = (uint8_t)item_slot;
    MslLaserFrame* frame = &frames[laser_count++];
    *frame = (MslLaserFrame){
        .params = params,
        .state = batch->state.item_state[ii] == 0u ? 0u : 1u,
        .active = 1u,
    };

    const uint8_t hidden_flags = batch->state.item_hidden_callback_flags[ii];

    const uint8_t had_pending_reflect =
        batch->state.item_pending_reflect_owner_port[ii] < (uint8_t)players ? 1u : 0u;
    msl_item_reflect_apply_pending_laser_callback(batch, ii);
    if (had_pending_reflect != 0u) {
      batch->state.item_laser_scale[ii] = 1.0e-3f;
    }

    frame->prev_x = batch->state.item_pos_x[ii];
    frame->prev_y = batch->state.item_pos_y[ii];
    frame->prev_scale = batch->state.item_laser_scale[ii];
    const float vx = batch->state.item_vel_x[ii];
    const float vy = batch->state.item_vel_y[ii];
    const float speed = sqrtf(vx * vx + vy * vy);
    if (speed > 0.0f) {
      frame->ux = vx / speed;
      frame->uy = vy / speed;
    } else {
      frame->ux = batch->state.item_direction[ii] >= 0.0f ? 1.0f : -1.0f;
      frame->uy = 0.0f;
    }
    const MslCharParams* source = msl_char_params_fast(params->source_char_id);
    const float scale_cap =
        source != NULL && source->laser_scale_max > 0.0f ? source->laser_scale_max : 1.0f;
    float scale = frame->prev_scale + fabsf(speed) / 11.25f;
    if (scale > scale_cap) {
      scale = scale_cap;
    }
    if (scale < 1.0e-5f) {
      scale = 1.0e-3f;
    }
    frame->scale = scale;
    batch->state.item_laser_scale[ii] = scale;

    const uint8_t spawned = hidden_flags & (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME;
    const uint8_t motion_already_applied =
        hidden_flags & (uint8_t)MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_MOTION_APPLIED;
    frame->x = frame->prev_x + vx;
    frame->y = frame->prev_y + vy;
    if (spawned != 0u && motion_already_applied != 0u) {
      frame->x = frame->prev_x;
      frame->y = frame->prev_y;
      frame->prev_x -= vx;
      frame->prev_y -= vy;
    }
    batch->state.item_pos_x[ii] = frame->x;
    batch->state.item_pos_y[ii] = frame->y;
    batch->state.item_hidden_callback_flags[ii] =
        (uint8_t)(hidden_flags &
                  (uint8_t) ~(uint8_t)(MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME |
                                       MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_MOTION_APPLIED));

    if (have_blast != 0u &&
        (frame->x > blast.right || frame->x < blast.left || frame->y < blast.bottom)) {
      item_slot_clear(batch, ii);
      frame->active = 0u;
      continue;
    }
    batch->state.item_timer[ii] -= 1.0f;
    if (!(batch->state.item_timer[ii] > 0.0f)) {
      item_slot_clear(batch, ii);
      frame->active = 0u;
      continue;
    }
    frame->stage_hit = stage_collision_item_line_hits_runtime(batch, bi, frame->prev_x,
                                                              frame->prev_y, frame->x, frame->y);
    batch->state.item_misc0[ii] = slippi_metadata_low_byte_from_f32(frame->scale);
    batch->state.item_misc1[ii] = slippi_metadata_low_byte_from_f32(msl_melee_normalize_angle(
        msl_melee_atan2f(batch->state.item_vel_y[ii], batch->state.item_vel_x[ii])));
  }

  // Fighter priority-13 traversal: fighter list, then item list, then HitCapsule order.
  for (int defender = 0; defender < players; defender++) {
    const size_t d_idx = msl_idx_player(bi, defender);
    if (msl_action_owns_x2219_collision_skip(batch->state.action_id[d_idx]) != 0u) {
      continue;
    }
    for (uint8_t laser_i = 0u; laser_i < laser_count; laser_i++) {
      MslLaserFrame* frame = &frames[laser_i];
      const int item_slot = (int)laser_slots[laser_i];
      const size_t ii = msl_idx_item(bi, item_slot);
      if (frame->active == 0u || batch->state.item_exists[ii] == 0u ||
          batch->state.item_owner[ii] == defender) {
        continue;
      }
      const float age = (float)frame->params->lifetime_frames - batch->state.item_timer[ii];
      (void)laser_try_fighter_contact(batch, bi, item_slot, defender, frame, age);
    }
  }

  // Fighter collision runs before the later item/item callback. This ordering naturally lets an
  // earlier BODY callback consume a throw laser before it can contact a Yoshi stage article; no
  // victim/provenance deferral path is needed in the stage-item owner.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007925C
  // refs/melee/src/melee/it/items/itheiho.c::it_802D8EC8
  for (uint8_t laser_i = 0u; laser_i < laser_count; laser_i++) {
    MslLaserFrame* frame = &frames[laser_i];
    const int item_slot = (int)laser_slots[laser_i];
    const size_t ii = msl_idx_item(bi, item_slot);
    if (frame->active == 0u || batch->state.item_exists[ii] == 0u) {
      continue;
    }
    if (yoshi_shyguy_try_laser_item_hit(batch, bi, item_slot, frame->params, frame->state,
                                        frame->prev_x, frame->prev_y, frame->x, frame->y, frame->ux,
                                        frame->uy, frame->prev_scale, frame->scale) != 0u) {
      frame->destroy_after_contact = 1u;
    }
  }

  // Item priority-14 callback collapse and laser-specific stage/lifetime aftermath.
  for (uint8_t laser_i = 0u; laser_i < laser_count; laser_i++) {
    MslLaserFrame* frame = &frames[laser_i];
    const int item_slot = (int)laser_slots[laser_i];
    const size_t ii = msl_idx_item(bi, item_slot);
    if (frame->active == 0u || batch->state.item_exists[ii] == 0u) {
      continue;
    }
    if (frame->reflected != 0u) {
      msl_item_reflect_apply_pending_laser_callback(batch, ii);
      batch->state.item_laser_scale[ii] = 1.0e-3f;
      batch->state.item_misc0[ii] = slippi_metadata_low_byte_from_f32(1.0e-3f);
    } else if (frame->shield_bounce != 0u) {
      batch->state.item_vel_x[ii] = frame->bounce_vx;
      batch->state.item_vel_y[ii] = frame->bounce_vy;
      batch->state.item_direction[ii] = frame->bounce_vx >= 0.0f ? 1.0f : -1.0f;
      batch->state.item_laser_scale[ii] = 1.0e-3f;
      batch->state.item_misc0[ii] = slippi_metadata_low_byte_from_f32(1.0e-3f);
      batch->state.item_misc1[ii] = slippi_metadata_low_byte_from_f32(
          msl_melee_normalize_angle(msl_melee_atan2f(frame->bounce_vy, frame->bounce_vx)));
    } else if (frame->destroy_after_contact != 0u) {
      item_slot_clear(batch, ii);
      continue;
    }
    if (frame->stage_hit != 0u && batch->state.item_exists[ii] != 0u &&
        batch->state.item_timer[ii] > 1.0f) {
      batch->state.item_timer[ii] = 1.0f;
    }
    batch->state.item_hidden_callback_flags[ii] = 0u;
  }
}
