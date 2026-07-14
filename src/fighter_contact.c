#include "fighter_contact.h"

#include <errno.h>
#include <math.h>
#include <string.h>

#include "action_ids.h"
#include "anim_timebase.h"
#include "combat_internal.h"
#include "common_params.h"
#include "char_params.h"
#include "falcon_specials.h"
#include "grab_flow.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "marth_specials.h"
#include "motion_state_runtime.h"
#include "msl_math.h"
#include "state_flags.h"

typedef struct MslContactVec3 {
  float x;
  float y;
  float z;
} MslContactVec3;

static inline size_t contact_hitbox_index(int bi, int p, int hb) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES + (size_t)hb;
}

static inline size_t contact_hurtcap_index(int bi, int p, int cap) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap;
}

static inline MslContactVec3 contact_vec_sub(MslContactVec3 a, MslContactVec3 b) {
  return (MslContactVec3){a.x - b.x, a.y - b.y, a.z - b.z};
}

static inline float contact_vec_dot(MslContactVec3 a, MslContactVec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline float contact_clamp01(float value) {
  if (value < 0.0f) {
    return 0.0f;
  }
  if (value > 1.0f) {
    return 1.0f;
  }
  return value;
}

// Closest distance between two finite 3D segments. This is the geometry core consumed by
// lbColl_80006E58 before its local-radius comparison.
// refs/melee/src/melee/lb/lbcollision.c::{lbColl_80006E58,lbColl_8000805C,lbColl_80007AFC}
static float contact_segment_distance_sq(MslContactVec3 p0, MslContactVec3 p1, MslContactVec3 q0,
                                         MslContactVec3 q1) {
  const MslContactVec3 d0 = contact_vec_sub(p1, p0);
  const MslContactVec3 d1 = contact_vec_sub(q1, q0);
  const MslContactVec3 r = contact_vec_sub(p0, q0);
  const float a = contact_vec_dot(d0, d0);
  const float e = contact_vec_dot(d1, d1);
  const float f = contact_vec_dot(d1, r);
  float s = 0.0f;
  float t = 0.0f;

  if (a <= 1.0e-12f && e <= 1.0e-12f) {
    return contact_vec_dot(r, r);
  }
  if (a <= 1.0e-12f) {
    t = contact_clamp01(f / e);
  } else {
    const float c = contact_vec_dot(d0, r);
    if (e <= 1.0e-12f) {
      s = contact_clamp01(-c / a);
    } else {
      const float b = contact_vec_dot(d0, d1);
      const float denom = a * e - b * b;
      if (denom != 0.0f) {
        s = contact_clamp01((b * f - c * e) / denom);
      }
      t = (b * s + f) / e;
      if (t < 0.0f) {
        t = 0.0f;
        s = contact_clamp01(-c / a);
      } else if (t > 1.0f) {
        t = 1.0f;
        s = contact_clamp01((b - c) / a);
      }
    }
  }

  const MslContactVec3 delta = {
      r.x + d0.x * s - d1.x * t,
      r.y + d0.y * s - d1.y * t,
      r.z + d0.z * s - d1.z * t,
  };
  return contact_vec_dot(delta, delta);
}

static float contact_hitbox_hurtcap_overlap(const MslBatch* batch, int bi, int attacker, int hb,
                                            int defender, int cap) {
  const size_t hb_i = contact_hitbox_index(bi, attacker, hb);
  const size_t cap_i = contact_hurtcap_index(bi, defender, cap);
  float overlap = 0.0f;
  (void)combat_body_overlap_lbColl_80006E58_matrix_radius(
      batch, bi, attacker, hb, defender, cap, batch->state.hitbox_x[hb_i],
      batch->state.hitbox_y[hb_i], batch->state.hitbox_z[hb_i], batch->state.hitbox_radius[hb_i],
      batch->state.hurtcap_a_x[cap_i], batch->state.hurtcap_a_y[cap_i],
      batch->state.hurtcap_a_z[cap_i], batch->state.hurtcap_b_x[cap_i],
      batch->state.hurtcap_b_y[cap_i], batch->state.hurtcap_b_z[cap_i], 0u, &overlap, NULL);
  return overlap;
}

static uint8_t contact_hitboxes_overlap(const MslBatch* batch, size_t a_i, size_t b_i) {
  const MslContactVec3 a0 = {batch->state.hitbox_prev_x[a_i], batch->state.hitbox_prev_y[a_i],
                             batch->state.hitbox_prev_z[a_i]};
  const MslContactVec3 a1 = {batch->state.hitbox_x[a_i], batch->state.hitbox_y[a_i],
                             batch->state.hitbox_z[a_i]};
  const MslContactVec3 b0 = {batch->state.hitbox_prev_x[b_i], batch->state.hitbox_prev_y[b_i],
                             batch->state.hitbox_prev_z[b_i]};
  const MslContactVec3 b1 = {batch->state.hitbox_x[b_i], batch->state.hitbox_y[b_i],
                             batch->state.hitbox_z[b_i]};
  const float radius = batch->state.hitbox_radius[a_i] + batch->state.hitbox_radius[b_i];
  return contact_segment_distance_sq(a0, a1, b0, b1) <= radius * radius ? 1u : 0u;
}

static uint8_t contact_hitbox_shield_overlap(const MslBatch* batch, int bi, int attacker, int hb,
                                             int defender) {
  const size_t d_idx = msl_idx_player(bi, defender);
  if (msl_guard_reflect_has_reflectdesc_only(batch, d_idx) != 0u) {
    return 0u;
  }
  return combat_shield_overlap_ftcoll_80007bcc(batch, bi, attacker, defender, hb, NULL);
}

static uint8_t contact_players_can_interact(const MslBatch* batch, int bi, int a, int d) {
  if (a == d) {
    return 0u;
  }
  const size_t a_idx = msl_idx_player(bi, a);
  const size_t d_idx = msl_idx_player(bi, d);
  if (batch->state.stocks[a_idx] == 0u || batch->state.stocks[d_idx] == 0u ||
      msl_action_owns_x2219_collision_skip(batch->state.action_id[d_idx])) {
    return 0u;
  }
  const uint8_t thrown_owner = batch->state.grab_owner_port[a_idx];
  if (thrown_owner == (uint8_t)d) {
    return 0u;
  }
  // Supported VS/team simulations run with team attack enabled. Team identity does not suppress
  // fighter contact; thrown HitCapsules retain their source attacker for ownership only.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_800765E0
  return 1u;
}

static uint8_t contact_hitbox_targets(const MslBatch* batch, size_t hb_i, size_t d_idx) {
  const uint16_t flags = batch->state.hitbox_flags[hb_i];
  if (batch->state.on_ground[d_idx] != 0u) {
    return (flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) != 0u ? 1u : 0u;
  }
  return (flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) != 0u ? 1u : 0u;
}

static uint8_t contact_defender_status(const MslBatch* batch, size_t idx) {
  uint8_t status = batch->state.script_hit_status_x1988[idx] != 0u
                       ? batch->state.script_hit_status_x1988[idx]
                       : batch->state.colanim_hit_status_x198c[idx];
  if (batch->debug_hit_status_override != NULL && batch->debug_hit_status_override[idx] != 0xFFu) {
    status = batch->debug_hit_status_override[idx];
  }
  return status;
}

static uint8_t contact_attached_throw_pulse(const MslBatch* batch, size_t a_idx, size_t d_idx,
                                            int attacker, size_t hb_i) {
  const uint16_t action = batch->state.action_id[a_idx];
  if (!msl_action_is_throw_owner(action) ||
      batch->state.grab_owner_port[d_idx] != (uint8_t)attacker ||
      !msl_action_is_grabbed_victim(batch->state.action_id[d_idx]) ||
      batch->state.hitbox_only_hit_grabbed[hb_i] == 0u || batch->state.hitbox_kbg[hb_i] != 0u ||
      batch->state.hitbox_bkb[hb_i] != 0u) {
    return 0u;
  }
  // The live authored HitCapsule and attachment are the source state. Release consumes the
  // script flag and detaches the victim in the same Anim callback before fighter contact runs, so
  // no separate release-frame/payload snapshot is needed here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  return 1u;
}

static void contact_register_clank_group(MslBatch* batch, int bi, int attacker, int defender,
                                         int hb_id, uint16_t defender_iid) {
  const size_t hi = contact_hitbox_index(bi, attacker, hb_id);
  const uint8_t group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hi]);
  const uint8_t rehit = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hi]);
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007699C,inlineA0,inlineA1}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  hitlist_register_fighter_group(batch, bi, attacker, group, defender, defender_iid,
                                 (int)MSL_LBCOLL_INSERT_FT_HITBOX_CONTACT, rehit);
}

static void contact_enter_rebound_stop(MslBatch* batch, const MslCommonParams* common, int bi,
                                       int player, int opponent, int int_damage) {
  const size_t idx = msl_idx_player(bi, player);
  const size_t other = msl_idx_player(bi, opponent);
  const MslCharParams* ch = msl_char_params_fast(batch->state.char_id[idx]);
  const float x191c =
      (float)int_damage * common->rebound_damage_x191c_mul + common->rebound_damage_x191c_base;
  if (!(x191c > 0.0f) || ch == NULL) {
    return;
  }
  const float damage_facing = batch->state.pos_x[idx] > batch->state.pos_x[other] ? -1.0f : 1.0f;
  float ground_x0 =
      -damage_facing * (x191c * common->rebound_ground_x0_mul + common->rebound_ground_x0_base);
  if (batch->state.ground_friction_mul[idx] < 1.0f) {
    ground_x0 *= batch->state.ground_friction_mul[idx];
  }
  const float rate = (ch->rebound_anim_numerator_frames + 0.1f) / x191c;
  motion_state_change(batch, bi, player, (uint16_t)MSL_ACT_REBOUND_STOP, UINT32_MAX, 0u, 0.0f, 1.0f,
                      MSL_ANIM_ENTER_TICK_NONE);
  batch->state.rebound_ground_accel_2[idx] = ground_x0;
  batch->state.rebound_anim_rate_fp_q16_16[idx] = msl_q16_16_from_f32(rate);
}

static void contact_resolve_catch_row(MslBatch* batch, int bi) {
  const int players = (int)batch->config.num_players;
  for (int attacker = 0; attacker < players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    if (batch->state.catch_kind_x1a68[a_idx] == 0u || batch->state.hitlag[a_idx] != 0u) {
      continue;
    }
    int best = -1;
    float best_dx = 0.0f;
    uint8_t best_group = 0u;
    uint8_t best_rehit = 0u;
    for (int defender = 0; defender < players; defender++) {
      if (!contact_players_can_interact(batch, bi, attacker, defender)) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, defender);
      if (batch->state.grab_owner_port[d_idx] != 0xFFu || batch->state.dmg_x2224_b2[d_idx] != 0u ||
          batch->state.hurtbox_state[d_idx] != 0u ||
          (batch->state.catch_target_mask_x1a6a[d_idx] & batch->state.catch_kind_x1a68[a_idx]) !=
              0u ||
          combat_source_catch_wall_obstructed(batch, bi, a_idx, d_idx)) {
        continue;
      }
      uint8_t found = 0u;
      for (int hb = 0; hb < MSL_MAX_HITBOXES && found == 0u; hb++) {
        const size_t hb_i = contact_hitbox_index(bi, attacker, hb);
        if (batch->state.hitbox_enabled[hb_i] == 0u ||
            batch->state.hitbox_element[hb_i] != (uint8_t)MSL_HIT_ELEMENT_CATCH ||
            !contact_hitbox_targets(batch, hb_i, d_idx) ||
            !hitlist_allows_fighter_live_collision(batch, bi, attacker, hb, defender,
                                                   batch->state.instance_id[d_idx])) {
          continue;
        }
        const uint8_t caps = batch->state.hurtcap_count[d_idx];
        for (uint8_t cap = 0u; cap < caps; cap++) {
          const size_t cap_i = contact_hurtcap_index(bi, defender, cap);
          if (batch->state.hurtcap_is_grabbable[cap_i] == 0u ||
              batch->state.script_hurtcap_state[cap_i] != 0u ||
              !(batch->state.hurtcap_radius[cap_i] > 0.0f) ||
              contact_hitbox_hurtcap_overlap(batch, bi, attacker, hb, defender, cap) <= 0.0f) {
            continue;
          }
          const float dx = fabsf(batch->state.pos_x[d_idx] - batch->state.pos_x[a_idx]);
          if (best < 0 || dx < best_dx || (dx == best_dx && defender < best)) {
            best = defender;
            best_dx = dx;
            best_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
            best_rehit = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
          }
          found = 1u;
          break;
        }
      }
    }
    if (best >= 0) {
      grab_flow_on_catch_connect(batch, bi, attacker, best);
      const size_t d_idx = msl_idx_player(bi, best);
      hitlist_register_fighter_group(batch, bi, attacker, best_group, best,
                                     batch->state.instance_id[d_idx],
                                     (int)MSL_LBCOLL_INSERT_FT_CATCH, best_rehit);
    }
  }
}

static void contact_resolve_clanks(
    MslBatch* batch, int bi, uint8_t skip[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_MAX_HITBOXES]) {
  const MslCommonParams* common = msl_common_params();
  const int players = (int)batch->config.num_players;
  if (common == NULL) {
    return;
  }
  for (int left = 0; left < players; left++) {
    const size_t left_idx = msl_idx_player(bi, left);
    for (int right = left + 1; right < players; right++) {
      const size_t right_idx = msl_idx_player(bi, right);
      if (!contact_players_can_interact(batch, bi, left, right) ||
          batch->state.hitbox_count[left_idx] == 0u || batch->state.hitbox_count[right_idx] == 0u ||
          batch->state.on_ground[left_idx] == 0u || batch->state.on_ground[right_idx] == 0u) {
        continue;
      }
      int max_damage[2] = {0, 0};
      uint8_t rebound[2] = {0u, 0u};
      for (int rh = 0; rh < MSL_MAX_HITBOXES; rh++) {
        const size_t rh_i = contact_hitbox_index(bi, right, rh);
        const uint16_t rf = batch->state.hitbox_flags[rh_i];
        if (batch->state.hitbox_enabled[rh_i] == 0u ||
            (rf & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0u || !msl_hitbox_x42_b5_enabled(rf)) {
          continue;
        }
        if (!hitlist_allows_fighter_live_collision(batch, bi, right, rh, left,
                                                   batch->state.instance_id[left_idx])) {
          continue;
        }
        for (int lh = 0; lh < MSL_MAX_HITBOXES; lh++) {
          const size_t lh_i = contact_hitbox_index(bi, left, lh);
          const uint16_t lf = batch->state.hitbox_flags[lh_i];
          if (batch->state.hitbox_enabled[lh_i] == 0u ||
              (lf & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0u || !msl_hitbox_x42_b5_enabled(lf) ||
              !hitlist_allows_fighter_live_collision(batch, bi, left, lh, right,
                                                     batch->state.instance_id[right_idx]) ||
              !contact_hitboxes_overlap(batch, lh_i, rh_i)) {
            continue;
          }
          const int ld = combat_hitbox_collision_env_damage(batch, left_idx, lh_i);
          const int rd = combat_hitbox_collision_env_damage(batch, right_idx, rh_i);
          if (ld - common->clank_damage_diff_threshold < rd) {
            skip[left][right][lh] = 1u;
            max_damage[0] = ld > max_damage[0] ? ld : max_damage[0];
            rebound[0] |= (lf & (uint16_t)MSL_HITBOX_FLAG_REBOUND) != 0u ? 1u : 0u;
            contact_register_clank_group(batch, bi, left, right, lh,
                                         batch->state.instance_id[right_idx]);
          }
          if (rd - common->clank_damage_diff_threshold < ld) {
            skip[right][left][rh] = 1u;
            max_damage[1] = rd > max_damage[1] ? rd : max_damage[1];
            rebound[1] |= (rf & (uint16_t)MSL_HITBOX_FLAG_REBOUND) != 0u ? 1u : 0u;
            contact_register_clank_group(batch, bi, right, left, rh,
                                         batch->state.instance_id[left_idx]);
          }
        }
      }
      const size_t indices[2] = {left_idx, right_idx};
      const int players_by_side[2] = {left, right};
      const int opponents_by_side[2] = {right, left};
      for (uint8_t side = 0u; side < 2u; side++) {
        if (max_damage[side] > 0) {
          const uint16_t hitlag = combat_calc_hitlag_frames(
              common, max_damage[side], batch->state.action_id[indices[side]], 1.0f);
          if (hitlag > batch->state.hitlag[indices[side]]) {
            batch->state.hitlag[indices[side]] = hitlag;
          }
        }
        if (rebound[side] != 0u) {
          contact_enter_rebound_stop(batch, common, bi, players_by_side[side],
                                     opponents_by_side[side], max_damage[side]);
        }
      }
    }
  }
}

static void contact_resolve_damage_row(MslBatch* batch, int bi) {
  const MslCommonParams* common = msl_common_params();
  const int players = (int)batch->config.num_players;
  uint8_t clank_skip[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{{0}}};
  MslCombatBodyDamageScratch logs[MSL_MAX_PLAYERS];
  if (common == NULL) {
    return;
  }
  memset(logs, 0, sizeof(logs));
  uint8_t any_hitcapsule = 0u;
  for (int p = 0; p < players; p++) {
    if (batch->state.hitbox_count[msl_idx_player(bi, p)] != 0u) {
      any_hitcapsule = 1u;
      break;
    }
  }
  if (any_hitcapsule == 0u) {
    // ftColl_80078C70 has no candidate traversal when every fighter x914 capsule is disabled.
    // The priority-14 ProcessHit owner already clears the prior frame's inert-shield detection bit
    // before this phase, so no collision-side cleanup remains here.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    return;
  }
  contact_resolve_clanks(batch, bi, clank_skip);

  // Source owner order is current fighter (defender), fighter-list attacker, HitCapsule, then the
  // first overlapping HurtCapsule. Each current fighter gets fresh dmg_log0/1 scratch.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800765E0,ftColl_80078C70,ftColl_8007A06C}
  for (int defender = 0; defender < players; defender++) {
    const size_t d_idx = msl_idx_player(bi, defender);
    if (batch->state.stocks[d_idx] == 0u ||
        msl_action_owns_x2219_collision_skip(batch->state.action_id[d_idx])) {
      continue;
    }
    for (int attacker = 0; attacker < players; attacker++) {
      if (!contact_players_can_interact(batch, bi, attacker, defender)) {
        continue;
      }
      const size_t a_idx = msl_idx_player(bi, attacker);
      if (batch->state.hitbox_count[a_idx] == 0u) {
        continue;
      }
      int shield_max = 0;
      int shield_damage = 0;
      uint8_t shield_element = 0u;
      for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
        const size_t hb_i = contact_hitbox_index(bi, attacker, hb);
        const uint16_t flags = batch->state.hitbox_flags[hb_i];
        if (batch->state.hitbox_enabled[hb_i] == 0u || clank_skip[attacker][defender][hb] != 0u ||
            !msl_hitbox_x42_b5_enabled(flags) || !contact_hitbox_targets(batch, hb_i, d_idx) ||
            (((flags & (uint16_t)MSL_HITBOX_FLAG_IGNORE_THROWN_FIGHTERS) != 0u) &&
             batch->state.grab_owner_port[d_idx] < batch->config.num_players) ||
            (batch->state.hitbox_only_hit_grabbed[hb_i] != 0u &&
             batch->state.attached_victim_port[a_idx] < batch->config.num_players &&
             batch->state.attached_victim_port[a_idx] != (uint8_t)defender) ||
            !hitlist_allows_fighter_live_collision(batch, bi, attacker, hb, defender,
                                                   batch->state.instance_id[d_idx])) {
          continue;
        }
        const uint8_t element = batch->state.hitbox_element[hb_i];
        if (element == (uint8_t)MSL_HIT_ELEMENT_CATCH) {
          continue;
        }
        const uint8_t group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        const uint8_t rehit = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        // HitCapsule.damage is collision-time damage in Melee: ftColl_8007ABD0 applies the
        // latched stale scalar (and smash-release multiplier) before ftColl_80076CBC consumes the
        // integer value for shield damage and ProcessHit hitlag. The script owner deliberately
        // retains authored damage plus the scalar as separate fixed state, so collapse them at
        // this same source boundary rather than feeding raw script damage to shields.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_80076CBC}
        // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
        const int int_damage = combat_hitbox_collision_env_damage(batch, a_idx, hb_i);

        // ShieldDesc-family intercepts (Marth Counter in the supported domain) precede ordinary
        // shield and HurtCapsule tests and do not require BODY overlap.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80078C70}
        if (marth_counter_try_fighter_contact(batch, bi, attacker, defender, hb) != 0u) {
          // The descriptor callback changes the defender to CounterHit before priority-13 DmgLog
          // collapse. Any earlier BODY candidate from a sibling capsule belongs to the same
          // intercepted swing and cannot survive into ProcessHit.
          logs[defender].count = 0u;
          continue;
        }

        if (batch->state.shield_radius[d_idx] > 0.0f &&
            contact_hitbox_shield_overlap(batch, bi, attacker, hb, defender)) {
          if (element == (uint8_t)MSL_HIT_ELEMENT_INERT) {
            const size_t flags_i =
                d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
            batch->state.state_flags[flags_i] |=
                (uint8_t)MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD;
            falcon_specials_on_inert_shield_contact(batch, a_idx);
          } else {
            const int contribution = int_damage + (int)batch->state.hitbox_shield_damage[hb_i];
            shield_max = int_damage > shield_max ? int_damage : shield_max;
            shield_damage += contribution > 0 ? contribution : 0;
            shield_element = element;
            hitlist_register_fighter_group(batch, bi, attacker, group, defender,
                                           batch->state.instance_id[d_idx],
                                           (int)MSL_LBCOLL_INSERT_FT_SHIELD, rehit);
          }
          continue;
        }

        const uint8_t hurt_state = contact_defender_status(batch, d_idx);
        if (hurt_state == 2u) {
          continue;
        }
        const uint8_t caps = batch->state.hurtcap_count[d_idx];
        if (caps != 0u && contact_attached_throw_pulse(batch, a_idx, d_idx, attacker, hb_i)) {
          // Thrown* skeletons are parented into the thrower's live attachment chain. The authored
          // zero-direct-KB pre-release x914 pulse targets that attached fighter even though the
          // simulator exposes fighter roots independently instead of retaining the parent JObj.
          // This is the narrow attachment-owner contract, not an alternate collision heuristic.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
          (void)combat_source_body_log_record(batch, &logs[defender], bi, attacker, defender, hb,
                                              0);
          hitlist_register_fighter_group(batch, bi, attacker, group, defender,
                                         batch->state.instance_id[d_idx],
                                         (int)MSL_LBCOLL_INSERT_FT_BODY, rehit);
          continue;
        }
        for (uint8_t cap = 0u; cap < caps; cap++) {
          const size_t cap_i = contact_hurtcap_index(bi, defender, cap);
          if (batch->state.hurtcap_enabled[cap_i] == 0u) {
            continue;
          }
          const float overlap =
              contact_hitbox_hurtcap_overlap(batch, bi, attacker, hb, defender, cap);
          if (overlap <= 0.0f) {
            continue;
          }
          if (element == (uint8_t)MSL_HIT_ELEMENT_INERT) {
            falcon_specials_on_inert_body_contact(batch, a_idx);
          } else if (hurt_state != 0u || batch->state.script_hurtcap_state[cap_i] != 0u) {
            combat_source_body_invincible(batch, bi, attacker, defender, hb, group, rehit);
          } else {
            if (overlap > 0.0f && overlap < common->phantom_overlap_max_x7a8 &&
                logs[defender].count == 0u) {
              combat_source_body_phantom(batch, bi, attacker, defender, hb, group);
            } else {
              (void)combat_source_body_log_record(batch, &logs[defender], bi, attacker, defender,
                                                  hb, cap);
              hitlist_register_fighter_group(batch, bi, attacker, group, defender,
                                             batch->state.instance_id[d_idx],
                                             (int)MSL_LBCOLL_INSERT_FT_BODY, rehit);
            }
          }
          break;
        }
      }
      if (shield_max > 0) {
        combat_source_shield_apply(batch, bi, attacker, defender, shield_max, shield_damage,
                                   shield_element);
      }
    }
    combat_source_body_log_apply(batch, bi, &logs[defender]);
  }
}

void fighter_contact_resolve_catch(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    contact_resolve_catch_row(batch, bi);
  }
}

void fighter_contact_resolve_damage(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    contact_resolve_damage_row(batch, bi);
  }
}

int fighter_contact_debug_select_body(MslBatch* batch, int bi, MslDebugCombatContact* out_contacts,
                                      uint16_t max_contacts, uint16_t* out_count) {
  if (batch == NULL || out_count == NULL || bi < 0 || bi >= batch->batch_size ||
      (max_contacts != 0u && out_contacts == NULL)) {
    return EINVAL;
  }
  uint16_t written = 0u;
  const int players = (int)batch->config.num_players;
  for (int attacker = 0; attacker < players && written < max_contacts; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    for (int defender = 0; defender < players && written < max_contacts; defender++) {
      if (!contact_players_can_interact(batch, bi, attacker, defender)) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, defender);
      if (contact_defender_status(batch, d_idx) == 2u) {
        continue;
      }
      uint8_t selected = 0u;
      for (int hb = 0; hb < MSL_MAX_HITBOXES && selected == 0u; hb++) {
        const size_t hb_i = contact_hitbox_index(bi, attacker, hb);
        const uint8_t element = batch->state.hitbox_element[hb_i];
        if (batch->state.hitbox_enabled[hb_i] == 0u || element == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
            element == (uint8_t)MSL_HIT_ELEMENT_INERT ||
            !msl_hitbox_x42_b5_enabled(batch->state.hitbox_flags[hb_i]) ||
            !contact_hitbox_targets(batch, hb_i, d_idx) ||
            !hitlist_allows_fighter_live_collision(batch, bi, attacker, hb, defender,
                                                   batch->state.instance_id[d_idx]) ||
            (batch->state.shield_radius[d_idx] > 0.0f &&
             contact_hitbox_shield_overlap(batch, bi, attacker, hb, defender))) {
          continue;
        }
        for (uint8_t cap = 0u; cap < batch->state.hurtcap_count[d_idx]; cap++) {
          const size_t cap_i = contact_hurtcap_index(bi, defender, cap);
          if (batch->state.hurtcap_enabled[cap_i] == 0u ||
              contact_hitbox_hurtcap_overlap(batch, bi, attacker, hb, defender, cap) <= 0.0f) {
            continue;
          }
          MslDebugCombatContact* out = &out_contacts[written++];
          memset(out, 0, sizeof(*out));
          out->attacker = (uint8_t)attacker;
          out->defender = (uint8_t)defender;
          out->hitbox_id = (uint8_t)hb;
          out->hurtcap_id = cap;
          out->attacker_msid = batch->state.animation_index[a_idx] <= UINT16_MAX
                                   ? (uint16_t)batch->state.animation_index[a_idx]
                                   : 0u;
          out->attacker_action_frame = batch->state.action_frame[a_idx];
          out->hitbox_x = batch->state.hitbox_x[hb_i];
          out->hitbox_y = batch->state.hitbox_y[hb_i];
          out->hitbox_z = batch->state.hitbox_z[hb_i];
          out->hitbox_radius = batch->state.hitbox_radius[hb_i];
          out->hitbox_damage = batch->state.hitbox_damage[hb_i];
          out->hurtcap_ax = batch->state.hurtcap_a_x[cap_i];
          out->hurtcap_ay = batch->state.hurtcap_a_y[cap_i];
          out->hurtcap_az = batch->state.hurtcap_a_z[cap_i];
          out->hurtcap_bx = batch->state.hurtcap_b_x[cap_i];
          out->hurtcap_by = batch->state.hurtcap_b_y[cap_i];
          out->hurtcap_bz = batch->state.hurtcap_b_z[cap_i];
          out->hurtcap_radius = batch->state.hurtcap_radius[cap_i];
          selected = 1u;
          break;
        }
      }
    }
  }
  *out_count = written;
  return 0;
}
