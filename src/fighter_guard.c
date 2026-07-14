#include "fighter_guard.h"

#include "action_ids.h"
#include "anim_table.h"
#include "combat_internal.h"
#include "common_params.h"
#include "guard_lifecycle.h"
#include "motion_state_runtime.h"

uint8_t fighter_guard_apply_shield_contact(MslBatch* batch, int batch_index, int defender,
                                           const MslGuardShieldContact* contact,
                                           MslGuardShieldContactResult* result) {
  if (batch == NULL || contact == NULL || batch_index < 0 || batch_index >= batch->batch_size ||
      defender < 0 || defender >= (int)batch->config.num_players) {
    return 0u;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return 0u;
  }

  const size_t idx = msl_idx_player(batch_index, defender);
  int max_int_damage = contact->max_int_damage;
  int shield_damage_taken = contact->shield_damage_taken;
  if (max_int_damage < 0) {
    max_int_damage = 0;
  }
  if (shield_damage_taken < 0) {
    shield_damage_taken = 0;
  }

  // Fighter shield collision suppresses x19A0 and arms the GuardOff attack/special timer during a
  // powershield. The item collision owner writes x19A0 unconditionally.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80077688}
  uint8_t powershield_active = 0u;
  if (contact->source == MSL_GUARD_CONTACT_FIGHTER) {
    powershield_active = combat_shield_damage_powershield_suppressed_idx(batch, idx);
    if (powershield_active != 0u) {
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80094138
      batch->state.guard_special_enable_timer_x1c[idx] = c->guard_special_enable_frames;
      batch->state.guard_x10[idx] = 0u;
      shield_damage_taken = 0;
    }
  }

  const uint8_t recoil_powershield_active = combat_guard_setoff_recoil_x221c_b2_idx(batch, idx);
  const float light = combat_latched_lightshield_amount_idx(batch, idx);
  const float depletion_light =
      light * (c->shield_hit_lightshield_max - c->shield_hit_lightshield_min) +
      c->shield_hit_lightshield_min;
  const float depletion =
      c->shield_hit_damage_mul * ((float)shield_damage_taken * (1.0f - depletion_light)) +
      c->shield_hit_damage_base;
  float hp = batch->state.shield_hp[idx] - depletion;
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[idx] = hp;

  // x19A4 is the max integer damage packet consumed by GuardSetOff's animation-rate, recoil, and
  // in-hitlag input callback owners. Store it before changing motion, as ftColl does before
  // Fighter_ProcessHit dispatches ftCo_80092F2C.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80077688}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093240}
  batch->state.guard_setoff_hitlag_damage_min[idx] =
      max_int_damage > 255 ? 255u : (uint8_t)max_int_damage;

  const float stun_light =
      light * (c->shield_stun_lightshield_max - c->shield_stun_lightshield_min) +
      c->shield_stun_lightshield_min;
  float stun_frames =
      c->shield_stun_mul * ((float)max_int_damage * (1.0f - stun_light)) + c->shield_stun_base;
  if (!(stun_frames > 0.0f)) {
    stun_frames = 1.0f;
  }
  const float end_frame =
      msl_anim_end_frame(batch->state.char_id[idx], (uint16_t)MSL_SM_GUARD_DAMAGE);
  const float anim_rate = end_frame > 0.0f ? (end_frame + 0.1f) / stun_frames : 1.0f;
  const uint16_t prior_motion = batch->state.action_id[idx];

  // ftCo_80092F2C owns one ordinary MotionState entry. In particular, it does not restore the
  // frame-start value of guard.x10 after GuardOn/Guard's Anim callback has decremented it.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  motion_state_change(batch, batch_index, defender, (uint16_t)MSL_ACT_GUARD_SET_OFF,
                      (uint32_t)MSL_SM_GUARD_DAMAGE, 0u, 0.0f, anim_rate, MSL_ANIM_ENTER_TICK_NONE);
  batch->state.tilt_timer_x[idx] = 0xFEu;
  combat_state_flags_clear_guard_reflecting(batch, idx);
  combat_state_flags_clear_stale_guard_timer_bits_on_setoff_entry(batch, idx);
  msl_guard_set_shield_desc_active(batch, idx, 1u);

  // The collision helper stores a source-relative sign and x19B0 element; ftCo_80092F2C consumes
  // them only for grounded, non-ground-element recoil.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80077688}
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Guard.s:1656-1679
  if (batch->state.on_ground[idx] && contact->hit_element != (uint8_t)MSL_HIT_ELEMENT_GROUND) {
    float push = stun_frames * c->shield_setoff_push_mul;
    if (recoil_powershield_active == 0u) {
      push *= c->shield_setoff_push_mul_non_yoshi;
    }
    if (push > c->shield_setoff_push_max) {
      push = c->shield_setoff_push_max;
    }
    batch->state.speed_ground_x_self[idx] =
        batch->state.pos_x[idx] > contact->source_pos_x ? push : -push;
  }

  const uint16_t defender_hitlag = combat_calc_hitlag_frames(c, max_int_damage, prior_motion, 1.0f);
  batch->state.hitlag[idx] = defender_hitlag;
  combat_state_flags_set_is_hitlag(batch, idx, defender_hitlag);

  if (result != NULL) {
    result->lightshield_amount = light;
    result->defender_motion_id = prior_motion;
    result->defender_hitlag = defender_hitlag;
  }
  return 1u;
}
