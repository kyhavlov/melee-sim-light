#include "hitboxes.h"

#include <math.h>
#include <stdint.h>

#include "anim_frame.h"
#include "anim_pose.h"
#include "action_ids.h"
#include "anim_table.h"
#include "char_params.h"
#include "common_params.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "msl_math.h"
#include "mtx34.h"
#include "trigger_input.h"

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
}

static inline uint8_t hitboxes_has_authoritative_hitlist_seed(const MslBatch* batch, int bi,
                                                              int attacker, int hb) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb < 0 ||
      hb >= (int)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const size_t hb_valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb;
  return batch->state.combat_hitlist_hb_valid[hb_valid_i] ? 1u : 0u;
}

static inline int hitboxes_seed_bridge_get_env_dmg(float dmg) {
  // Decomp (GALE01): "getEnvDmg" pattern used by collision when converting float hitbox damage to
  // the integer damage lane used by shield interactions / hitlag input.
  // refs/melee/src/melee/ft/ftcoll.c (inlineA0/inlineA1 and ftColl_80076CBC).
  if (dmg == 0.0f) {
    return 0;
  }
  const int i = (int)dmg;
  return (i != 0) ? i : 1;
}

static inline uint16_t hitboxes_seed_bridge_shield_hitlag_frames(const MslCommonParams* c,
                                                                 float dmg) {
  if (c == NULL) {
    return 0;
  }
  const int dmg_i = hitboxes_seed_bridge_get_env_dmg(dmg);
  if (dmg_i <= 0) {
    return 0;
  }
  // Decomp (GALE01): ftCommon_CalcHitlag truncation shape for shield-hit lanes.
  // Shield-hit entry (ftColl_80076CBC/Fighter_ProcessHit_8006D1EC) uses non-squat defenders and
  // hitlag mul 1.0 for this bucket, so we only need the base truncation lane here.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  const float tmp_f = (float)dmg_i * c->hitlag_dmg_mul + c->hitlag_base;
  int hl_i = (int)tmp_f;
  if (hl_i < 0) {
    hl_i = 0;
  }
  if (hl_i > 0xFFFF) {
    hl_i = 0xFFFF;
  }
  return (uint16_t)hl_i;
}

static inline void hitboxes_seed_bridge_entry_clear(MslHitlistVictimEntry* e) {
  if (e == NULL) {
    return;
  }
  e->id32 = 0;
  e->id16 = 0;
  e->kind_slot = 0xFFu;
  e->cd = 0;
}

static inline uint8_t hitboxes_seed_bridge_is_guard_transition_owner(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_GUARD_ON:
    case MSL_ACT_GUARD:
    case MSL_ACT_GUARD_SET_OFF:
    case MSL_ACT_GUARD_REFLECT:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t hitboxes_seed_bridge_is_attackair_owner(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_ATTACK_AIR_N:
    case MSL_ACT_ATTACK_AIR_F:
    case MSL_ACT_ATTACK_AIR_B:
    case MSL_ACT_ATTACK_AIR_HI:
    // Decomp owner map for aerial AttackAir windows in stale hitlist trim bridge.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008A5C,lbColl_8000ACFC}
    case MSL_ACT_ATTACK_AIR_LW:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t hitboxes_seed_bridge_is_attackair_guard_shield_reentry_owner(
    uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_ATTACK_AIR_N:
    case MSL_ACT_ATTACK_AIR_B:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t hitboxes_seed_bridge_is_guard_admission_source(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_WAIT:
    case MSL_ACT_WALK_SLOW:
    case MSL_ACT_WALK_MIDDLE:
    case MSL_ACT_WALK_FAST:
    case MSL_ACT_TURN:
    case MSL_ACT_TURN_RUN:
    case MSL_ACT_DASH:
    case MSL_ACT_RUN:
    case MSL_ACT_RUN_DIRECT:
    case MSL_ACT_RUN_BRAKE:
    case MSL_ACT_SQUAT:
    case MSL_ACT_SQUAT_WAIT:
    case MSL_ACT_SQUAT_RV:
    case MSL_ACT_LANDING:
    case MSL_ACT_LANDING_FALL_SPECIAL:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t hitboxes_source_port0_for_attacker(const MslBatch* batch, size_t a_idx,
                                                         int attacker) {
  // Slippi records dmg.x18C4_source_ply in raw controller-port domain; hitbox cleanup compares the
  // replay-facing last_hit_by lane against that raw owner.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (last_hit_by lane)
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
  if (batch == NULL || attacker < 0 || attacker >= MSL_MAX_PLAYERS) {
    return 6u;
  }
  const uint8_t source_port0 = batch->state.source_port0[a_idx];
  if (source_port0 < (uint8_t)MSL_MAX_PLAYERS) {
    return source_port0;
  }
  return (uint8_t)attacker;
}

static inline uint8_t hitboxes_seed_bridge_post_contact_hitlag_hitlist_applies(
    const MslBatch* batch, int bi, int attacker, int hb_id, uint8_t hit_group) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES) {
    return 0u;
  }
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return 0u;
  }

  const size_t valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  if (batch->state.combat_hitlist_hb_valid[valid_i]) {
    return 0u;
  }

  const size_t a_idx = msl_idx_player(bi, attacker);
  if (batch->state.hitlag[a_idx] == 0u) {
    return 0u;
  }

  const uint16_t attacker_iid = batch->state.instance_id[a_idx];
  const size_t group_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
  for (int victim = 0; victim < (int)batch->config.num_players; victim++) {
    if (victim == attacker) {
      continue;
    }
    const size_t cd_i =
        group_base + (((size_t)attacker * (size_t)MSL_HITLIST_GROUPS + (size_t)hit_group) *
                          (size_t)MSL_MAX_PLAYERS +
                      (size_t)victim);
    if (batch->state.combat_hitlist_cd[cd_i] == 0u) {
      continue;
    }
    const size_t v_idx = msl_idx_player(bi, victim);
    if (batch->state.hitlag[v_idx] == 0u || batch->state.hitstun[v_idx] == 0u) {
      continue;
    }
    if (batch->state.instance_hit_by[v_idx] != attacker_iid) {
      continue;
    }
    // Teacher-forced post-contact seed lane:
    // - ftAction_8007121C -> ftColl_800768A0 clears/copies HitCapsule victims on enable edges.
    // - ftColl_80076ED8 / Fighter_ProcessHit then inserts the accepted BODY victim through
    //   lbColl_80008688 and starts attacker/defender hitlag.
    // - Slippi rows inside that same hitlag window can still expose the create-frame pose, so
    //   replay reseed must materialize the post-contact victims_1 state after the enable-edge
    //   clear/copy when hitlag/hitstun/source ownership proves that accepted BODY hit.
    // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    return 1u;
  }
  return 0u;
}

static inline uint8_t hitboxes_runtime_specialhi_pose_owner(uint8_t char_id, uint16_t action_id) {
  if (!(char_id == 1u || char_id == 22u)) {
    return 0u;
  }
  switch (action_id) {
    case MSL_ACT_FX_SPECIAL_HI:
    case MSL_ACT_FX_SPECIAL_AIR_HI:
    case MSL_ACT_FX_SPECIAL_HI_LANDING:
    case MSL_ACT_FX_SPECIAL_HI_FALL:
    case MSL_ACT_FX_SPECIAL_HI_BOUND:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t hitboxes_apply_specialhi_local_xrotn(const MslBatch* batch, size_t idx,
                                                           uint8_t char_id, uint16_t msid,
                                                           uint16_t pose_frame, uint16_t part_id,
                                                           float facing_dir, float model_scale,
                                                           float* io_x, float* io_y, float* io_z) {
  if (batch == NULL || io_x == NULL || io_y == NULL || io_z == NULL) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if (!hitboxes_runtime_specialhi_pose_owner(char_id, action_id) ||
      !msl_anim_part_under_xrotn(char_id, part_id)) {
    return 0u;
  }

  float m[12];
  if (anim_pose_get_matrix(char_id, msid, pose_frame, 2u, m) != 0) {  // FtPart_XRotN
    return 0u;
  }

  const float vel_x = batch->state.speed_air_x_self[idx];
  const float vel_y = batch->state.speed_y_self[idx];
  if (!(fabsf(vel_x) > 0.0f || fabsf(vel_y) > 0.0f)) {
    return 0u;
  }

  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  const float origin[3] = {0.0f, 0.0f, 0.0f};
  const float local_x[3] = {1.0f, 0.0f, 0.0f};
  msl_mtx34_mul_point(m, origin, &ax0, &ay0, &az0);
  msl_mtx34_mul_point(m, local_x, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  // Decomp: Firefox/Firebird launch writes `rotateModel = atan2f(self_vel.y, self_vel.x * facing_dir)`
  // and applies it with `ftPartSetRotX(..., 2*pi - rotateModel)`.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFox_SpecialHi_RotateModel,ftFx_SpecialAirHi_Enter,ftFx_SpecialAirHi_Coll}
  const float angle = (2.0f * MSL_PI_F) - atan2f(vel_y, vel_x * facing_dir);

  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

static inline uint8_t hitboxes_seed_bridge_is_damage_or_firefox_launch_victim_action(
    uint16_t action_id) {
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
    case MSL_ACT_FX_SPECIAL_HI:
    case MSL_ACT_FX_SPECIAL_AIR_HI:
      return 1u;
    default:
      return 0u;
  }
}

static void hitboxes_seed_bridge_trim_impossible_indefinite(
    MslBatch* batch, int bi, int attacker, int hb_id, const MslHitboxEvent* def,
    uint16_t first_create_frame, uint16_t second_create_frame, float first_create_damage,
    uint16_t pose_frame, uint8_t seed_materialized_now, uint8_t from_prev_active_snapshot) {
  if (batch == NULL || def == NULL) {
    return;
  }
  // Teacher-forced reseed snapshot bridge only; not GALE01 runtime behavior.
  // This trim is valid only when the hitcapsule was just materialized from seeded dense hitlist
  // lanes and the slot came from the pose_frame-1 active snapshot lane (not a pose-frame
  // create/enable-edge path).
  //
  // Defensive guardrail: keep this path impossible to trigger unless seed materialization happened
  // this frame, even if a future refactor broadens callsites.
  if (!seed_materialized_now) {
    return;
  }
  if (!from_prev_active_snapshot) {
    return;
  }
  if (attacker < 0 || attacker >= (int)batch->config.num_players) {
    return;
  }
  if (hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return;
  }
  if (!(def->damage > 0.0f)) {
    return;
  }
  if (pose_frame < def->frame) {
    return;
  }

  const MslCommonParams* c = msl_common_params();
  const uint16_t expected_hitlag = hitboxes_seed_bridge_shield_hitlag_frames(c, def->damage);
  if (expected_hitlag == 0u) {
    return;
  }
  const uint16_t window_age = (uint16_t)(pose_frame - def->frame);
  const uint16_t window_age_1based = (uint16_t)(window_age + 1u);
  const uint8_t early_window = (window_age_1based <= expected_hitlag) ? 1u : 0u;
  // Only trim at the tail of the decomp hitlag horizon (age >= hitlag-1).
  //
  // Safety proof (decomp-shaped):
  // - A real shield hit in this active window applies defender hitlag in ftColl_80076CBC.
  // - Hitlag duration follows ftCommon_CalcHitlag truncation shape.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  //
  // Therefore, when window_age+1 has reached expected_hitlag and defender is still neutral
  // (hitlag==0 && hitstun==0), an indefinite seeded victim entry cannot represent a real prior
  // hit from this same active window.
  //
  // Decomp shape:
  // - ftColl_800768A0 clear/copy ownership is tied to hitbox enable-edge / hit_group transitions.
  // - ftColl_80076CBC applies nonzero defender hitlag on real shield contact.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC}
  const size_t a_idx = msl_idx_player(bi, attacker);
  const uint16_t attacker_iid = batch->state.instance_id[a_idx];
  const size_t hb_valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  const uint8_t authoritative_hitbox_seed =
      batch->state.combat_hitlist_hb_valid[hb_valid_i] ? 1u : 0u;

  // Reseed bridge: dense per-group hitlist snapshots can over-latch indefinite (x4==0) entries
  // onto active capsules before the first real hit in a newly active window, because the seed
  // schema lacks per-HitCapsule victim lists and per-victim insertion frame provenance.
  //
  // Decomp anchors:
  // - lbColl_8000ACFC gates by victim presence in victims_1 (x4 is ignored for acceptance).
  // - lbColl_80008A5C only decrements nonzero x4; x4==0 entries persist until clear/copy.
  // - ftColl_80076CBC shield hits set nonzero defender hitlag on contact.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  //
  // At this tail lane, if defender is neutral (hitlag==0 && hitstun==0), any seeded indefinite
  // entry is stale for the current overlap window and must be cleared so ftColl_800768A0 ownership
  // can proceed from real runtime contacts.
  const size_t hl_i = idx_hitbox(bi, attacker, hb_id);
  MslHitlistCapsule* hit = &batch->state.fighter_hitlist[hl_i];
  for (size_t i = 0; i < (size_t)MSL_HITLIST_VICTIM_CAP; i++) {
    MslHitlistVictimEntry* e = &hit->victims_1[i];
    if (msl_hitlist_victim_is_empty(e->kind_slot)) {
      continue;
    }
    if (msl_hitlist_victim_kind(e->kind_slot) != (uint8_t)MSL_HITLIST_VICTIM_KIND_FIGHTER) {
      continue;
    }
    const uint8_t victim_port = msl_hitlist_victim_slot(e->kind_slot);
    if (victim_port >= (uint8_t)batch->config.num_players || victim_port == (uint8_t)attacker) {
      continue;
    }
    const size_t v_idx = msl_idx_player(bi, (int)victim_port);

    // Authoritative per-HitCapsule seed lanes already represent the decomp-owned victims_1 list
    // for this exact hitbox slot. The stale suppression trim below is only for legacy dense
    // group fallback materialization, whose provenance is too coarse to distinguish copied/cleared
    // HitCapsule lists.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC,ftColl_80076ED8}
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    if (authoritative_hitbox_seed) {
      continue;
    }

    // Seed-bridge stale suppression trim (narrow early-window BODY lane only):
    // - BODY attribution (`instance_hit_by`) is written on ftColl_80076ED8/Fighter_ProcessHit paths.
    // - Shield-only path (`ftColl_80076CBC`) does not own this BODY attribution lane.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80076CBC}
    // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (0x18EC export)
    //
    // In the first hitlag horizon of a newly materialized active window, an indefinite seeded
    // suppression entry with mismatched BODY source owner is stale dense-map carry (group-level
    // seed lacks per-HitCapsule insertion provenance). BODY source ownership is exported via
    // instance_hit_by on ftColl_80076ED8/Fighter_ProcessHit paths; a mismatched source cannot own
    // the current attacker's live hitcapsule suppression lane.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // Trim only this early-window, non-shield subset before lbColl_8000ACFC-style victim-presence
    // suppression.
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
    //
    // Decomp/data-backed AttackAir continuation extension:
    // - AttackAir scripts can produce multiple Body-collision windows while the victim is already
    //   in hitstun, and each confirmed contact rewrites attribution through ftColl_80076ED8.
    // - Scope this stale-owner trim to the hitbox-local create window (`early_window`) derived from
    //   extracted create_hitbox timing (`def->frame`) + ftCommon_CalcHitlag horizon
    //   (`expected_hitlag`), not a fixed action-frame constant.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    // data/moves/{fox,falco}.json: moves.{ftCo_SM_AttackAirN,ftCo_SM_AttackAirF,ftCo_SM_AttackAirB,
    //   ftCo_SM_AttackAirHi,ftCo_SM_AttackAirLw}.events.create_hitbox
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    uint8_t attacker_is_attackair_window = 0u;
    if (hitboxes_seed_bridge_is_attackair_owner(batch->state.action_id[a_idx])) {
      // AttackAir stale-trim window anchor:
      // - Keep the trim scoped to the first create_hitbox window of this action (not later refresh
      //   create events like AttackAirB frame 8), because decomp hitlist suppression for rehit=0
      //   moves is expected to persist after the first-hit horizon.
      // - Window length uses the same ftCommon_CalcHitlag truncation lane as the non-attackair trim.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
      uint16_t anchor_frame = def->frame;
      uint16_t anchor_hitlag = expected_hitlag;
      if (first_create_frame != 0xFFFFu) {
        const uint16_t first_hitlag =
            hitboxes_seed_bridge_shield_hitlag_frames(c, first_create_damage);
        if (first_hitlag > 0u) {
          anchor_frame = first_create_frame;
          anchor_hitlag = first_hitlag;
        }
      }
      // Multi-create AttackAir scripts (e.g. N/B early->late refresh) can legitimately carry a
      // same-move rehit=0 suppression latch beyond the second create edge. Limit this reseed-only
      // stale-trim window to the first create segment.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
      // data/moves/{fox,falco}.json: moves.ftCo_SM_AttackAir*.events.create_hitbox
      if (second_create_frame != 0xFFFFu && pose_frame >= second_create_frame) {
        anchor_hitlag = 0u;
      }
      if (pose_frame >= anchor_frame) {
        const uint16_t age_1based = (uint16_t)((pose_frame - anchor_frame) + 1u);
        // Use a strict-before-tail window for AttackAir stale trims: once the first-hit horizon
        // reaches its terminal frame, keep rehit=0 suppression latched unless a decomp copy/clear
        // edge rewires ownership.
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
        attacker_is_attackair_window = (age_1based < anchor_hitlag) ? 1u : 0u;
      }
    }
    const uint8_t stale_clear_window = attacker_is_attackair_window ? 1u : early_window;
    const uint16_t attacker_action = batch->state.action_id[a_idx];
    const uint8_t attacker_source_port0 =
        hitboxes_source_port0_for_attacker(batch, a_idx, attacker);
    // Shield-ownership guard for this trim:
    // - ftColl_80078C70 gates shield collision by live ShieldDesc ownership (`fp->x221B_b0`), then
    //   calls lbColl_80007BCC with the shield descriptor.
    // - Our runtime shield lane treats nonzero `shield_radius` as active ShieldDesc ownership in the
    //   same collision windows.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    const uint8_t shield_desc_active = (batch->state.shield_radius[v_idx] > 0.0f) ? 1u : 0u;
    const uint16_t v_action = batch->state.action_id[v_idx];
    const float v_trigger_unit =
        msl_trigger_unit_from_input(batch->state.input_buttons[v_idx], batch->state.input_l[v_idx],
                                    batch->state.input_r[v_idx]);
    const uint8_t shield_input_held = (v_trigger_unit > c->trigger_deadzone) ? 1u : 0u;
    const uint8_t guard_admission_pending =
        (!shield_desc_active && batch->state.on_ground[v_idx] && shield_input_held &&
         hitboxes_seed_bridge_is_guard_admission_source(v_action))
            ? 1u
            : 0u;
    const uint8_t guard_entry_desc_pending =
        (!shield_desc_active && v_action == (uint16_t)MSL_ACT_GUARD_ON && shield_input_held) ? 1u
                                                                                             : 0u;
    if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_N && v_action == (uint16_t)MSL_ACT_WAIT &&
        batch->state.action_frame[v_idx] <= 1 && batch->state.hitlag[v_idx] == 0u &&
        batch->state.hitstun[v_idx] == 0u && e->id16 == batch->state.instance_id[v_idx]) {
      // AttackAirN neutral victim latch:
      // - lbColl_8000ACFC tests HitCapsule.victims_1 by victim object identity; it does not use
      //   fp->dmg.x18ec_instancehitby as part of the suppression key.
      // - `instance_hit_by` is BODY damage attribution exported by Slippi from
      //   Fighter_ProcessHit/ftColl_80076ED8, and can still name an older source while a live
      //   neutral defender remains present in the HitCapsule victim list.
      // - Dense seed fallback has no per-HitCapsule owner, but it does carry the victim instance id.
      //   In the proven Wait entry lane (the action frame has just advanced for pre-combat), keep
      //   the latch instead of clearing it solely because BODY attribution points elsewhere; later
      //   Wait frames and JumpF remain eligible for the create-window stale-clear path.
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
      // refs/melee/src/melee/lb/types.h::HitCapsule
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
      // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      continue;
    }
    if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_HI && second_create_frame != 0xFFFFu &&
        pose_frame >= second_create_frame && batch->state.hitlag[v_idx] == 0u &&
        batch->state.hitstun[v_idx] == 0u) {
      // AttackAirHi late-window victim latch:
      // - Fox/Falco UpAir clears the early hitboxes and recreates the same-group late hitboxes
      //   (data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirHi frame 10 clear, frame 11
      //   create). After that recreate edge, lbColl_80008688 owns repeat suppression by the
      //   HitCapsule victim pointer, not by BODY attribution.
      // - Slippi's `instance_hit_by` can still name an older source, and the victim's replay
      //   `instance_id` can advance on a same-frame motion-state entry even though the decomp
      //   HitVictim pointer is stable. Preserve the current-port latch instead of clearing it as
      //   stale dense fallback; hitlist.c owns the instance-id proxy rebind boundary.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
      continue;
    }
    // Guard-family transitions own shield/GuardSetOff timing lanes in decomp even when snapshot
    // artifacts temporarily expose no-submotion/no-desc windows; do not clear reseed suppression
    // there from BODY attribution mismatch alone.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
    if (stale_clear_window && !shield_desc_active &&
        !hitboxes_seed_bridge_is_guard_transition_owner(v_action) &&
        batch->state.hitlag[v_idx] == 0u &&
        (attacker_is_attackair_window || batch->state.hitstun[v_idx] == 0u) &&
        batch->state.instance_hit_by[v_idx] != attacker_iid) {
      hitboxes_seed_bridge_entry_clear(e);
      continue;
    }

    if (((shield_desc_active && hitboxes_seed_bridge_is_guard_transition_owner(v_action)) ||
         guard_admission_pending || guard_entry_desc_pending) &&
        hitboxes_seed_bridge_is_attackair_guard_shield_reentry_owner(attacker_action) &&
        batch->state.hitlag[a_idx] == 0u && batch->state.hitlag[v_idx] == 0u &&
        batch->state.hitstun[a_idx] == 0u && batch->state.hitstun[v_idx] == 0u && e->cd == 0u) {
      // Reseed-only dense shield re-entry trim:
      // - ftColl_80078C70 checks lbColl_8000ACFC(victim_fp, hitcapsule) before the shield
      //   overlap branch and then calls ftColl_80076CBC for the accepted shield hit.
      // - ftColl_80076CBC inserts the shield owner into the exact HitCapsule victims_1 list
      //   through ftColl_80076808(..., type=1, ...).
      // - Legacy dense hitlist seed lanes are keyed by hit_group/victim and cannot encode the
      //   per-HitCapsule clear/copy provenance from ftColl_800768A0, so an indefinite dense entry
      //   can suppress a newly admitted shield hit on the same pre-combat GuardOn/GuardSetOff
      //   re-entry frame.
      //
      // Keep this bridge out of authoritative per-HitCapsule seeds (guarded above) and out of
      // active hitlag/hitstun rows; real prior shield contact would still carry shield-hit hitlag
      // through Fighter_ProcessHit.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC,ftColl_80076808}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091A4C
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092450
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
      hitboxes_seed_bridge_entry_clear(e);
      continue;
    }

    // Narrow stale-owner extensions for active windows with repeated suite misses:
    // - AttackAirB/Lw scripts (Fox/Falco) carry multiple same-group create_hitbox refreshes, and
    //   ftAction_8007121C/ftColl_800768A0 rewires suppression ownership on those refresh edges.
    // - AttackLw3 keeps same-group hitcapsules active across damage followup windows where decomp
    //   still rewrites BODY attribution through ftColl_80076ED8.
    // - Dense seed hitlists carry only victim presence + BODY attribution (`instance_hit_by`), so a
    //   same-port stale victim from an older attacker instance can survive reseed and suppress the
    //   live capsule even when the current attack should connect.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw3.c::ftCo_AttackLw3_Anim
    // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
    // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAir{B,Lw}.events.create_hitbox
    // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackLw3.events.create_hitbox
    const uint8_t stale_owner_attackairlw_lane =
        (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_LW) ? 1u : 0u;
    if (stale_owner_attackairlw_lane && !shield_desc_active &&
        !hitboxes_seed_bridge_is_guard_transition_owner(v_action) &&
        batch->state.hitlag[v_idx] == 0u && batch->state.instance_hit_by[v_idx] != attacker_iid) {
      if (batch->state.hitstun[v_idx] != 0u &&
          !(batch->state.last_hit_by[v_idx] == attacker_source_port0 &&
            hitboxes_seed_bridge_is_damage_or_firefox_launch_victim_action(v_action) &&
            batch->state.hitstun[v_idx] <= expected_hitlag)) {
        continue;
      }
      hitboxes_seed_bridge_entry_clear(e);
      continue;
    }

    const uint8_t stale_owner_attackairn_refresh_lane =
        (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_N && second_create_frame != 0xFFFFu &&
         def->frame == second_create_frame && early_window &&
         pose_frame > (uint16_t)(second_create_frame + 2u))
            ? 1u
            : 0u;
    if (stale_owner_attackairn_refresh_lane && !shield_desc_active &&
        !hitboxes_seed_bridge_is_guard_transition_owner(v_action) &&
        batch->state.hitlag[v_idx] == 0u && batch->state.hitstun[v_idx] != 0u &&
        batch->state.last_hit_by[v_idx] == attacker_source_port0 &&
        hitboxes_seed_bridge_is_damage_or_firefox_launch_victim_action(v_action) &&
        batch->state.instance_hit_by[v_idx] != attacker_iid) {
      // AttackAirN continuation refresh bridge:
      // - Neutral aerial scripts refresh hitcapsules on a later create_hitbox edge (frame 8 in the
      //   extracted Fox/Falco data) while the victim can still be in DamageFlyTop hitstun from an
      //   older same-port attacker instance.
      // - Keep the proven subset on the later refresh-continuity lane only; the shallower
      //   pre-contact rows in the same refresh segment remain on the baseline owner until that
      //   adjacent geometry/timing slice is modeled separately.
      // - ftColl_80076ED8 still owns the fresh BODY contact on that later create window and
      //   rewrites BODY attribution through Fighter_ProcessHit_8006D1EC.
      // - Dense reseed hitlists carry only per-hitbox victim presence/cooldown, so the older
      //   same-port `victims_1` entry can survive here with either an indefinite or finite
      //   cooldown even though the live continuation hit should re-own the lane on this refresh.
      // - Clear that stale entry before the generic `cd != 0` gate so lbColl_8000ACFC-style
      //   suppression can admit the live continuation hit.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
      // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
      hitboxes_seed_bridge_entry_clear(e);
      continue;
    }

    if (e->cd != 0u) {
      continue;
    }

    const uint8_t stale_owner_attacklw3_lane =
        (attacker_action == (uint16_t)MSL_ACT_ATTACK_LW3) ? 1u : 0u;
    if (stale_owner_attacklw3_lane && !shield_desc_active &&
        !hitboxes_seed_bridge_is_guard_transition_owner(v_action) &&
        batch->state.hitlag[v_idx] == 0u && batch->state.hitstun[v_idx] != 0u &&
        batch->state.last_hit_by[v_idx] == attacker_source_port0 &&
        hitboxes_seed_bridge_is_damage_or_firefox_launch_victim_action(v_action) &&
        batch->state.instance_hit_by[v_idx] != attacker_iid) {
      hitboxes_seed_bridge_entry_clear(e);
      continue;
    }

    if ((uint16_t)(window_age + 1u) < expected_hitlag) {
      continue;
    }

    if (batch->state.hitlag[v_idx] != 0u || batch->state.hitstun[v_idx] != 0u) {
      continue;
    }
    const uint8_t guard_no_submotion_snapshot =
        (v_action == (uint16_t)MSL_ACT_GUARD && batch->state.action_frame[v_idx] < 0 &&
         batch->state.animation_index[v_idx] == 0xFFFFFFFFu &&
         batch->state.anim_frame_f32[v_idx] < 0.0f)
            ? 1u
            : 0u;
    if (!guard_no_submotion_snapshot) {
      continue;
    }
    if (batch->state.prev_action_id[v_idx] != (uint16_t)MSL_ACT_GUARD) {
      continue;
    }
    // Authoritative per-HitCapsule seed for the frozen-Guard shield-provenance family must survive
    // this reseed-only dense stale trim. Keep the legacy dense-fallback trim unchanged.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC}
    // refs/melee/src/melee/lb/types.h::HitCapsule
    if (authoritative_hitbox_seed) {
      continue;
    }
    hitboxes_seed_bridge_entry_clear(e);
  }
}

void hitboxes_refresh(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // World-space hitbox centers are pose-driven:
  // - We interpret hitbox attachment records against the fighter's current submotion id
  //   (Slippi post-frame `animation_index`) and decomp-shaped anim/script time:
  //   fp->cur_anim_frame (Slippi post-frame `state_age`, float).
  //   refs/melee/src/melee/ft/ftaction.c::ftAction_80073240 (movescript timers use fp->cur_anim_frame)
  // - For each active hitbox, we sample the 3x4 bone matrix via anim_pose_get_matrix(...) and apply
  //   it to the bone-local offset (x,y,z), then translate by fighter (pos_x,pos_y,pos_z) to get world
  //   space.
  //
  // Combat note:
  // - combat_resolve() consumes these pose-driven world-space hitbox centers for hitbox-vs-hurtcap
  //   intersection (Pass 1).

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      uint8_t x43_b2_prev[MSL_MAX_HITBOXES] = {0};
      uint8_t seeded_prev_enabled[MSL_MAX_HITBOXES] = {0};
      float seeded_prev_x[MSL_MAX_HITBOXES] = {0.0f};
      float seeded_prev_y[MSL_MAX_HITBOXES] = {0.0f};
      float seeded_prev_z[MSL_MAX_HITBOXES] = {0.0f};
      uint8_t preserve_frozen_hitlag_hitboxes = 0u;
      uint8_t preserved_hitbox_count = 0u;

      // Hitlag freeze ownership:
      // - Fighter_8006A360 skips ftAnim_8006EBA4 / anim_cb / ftColl_800764DC while x2219_b5
      //   ("is in hitlag after decrement") is set, so ftAction_8007121C create/clear callbacks do
      //   not re-run on frozen frames.
      // - HitCapsule state for an already-active create frame must therefore persist through
      //   continuing hitlag; replaying pose_frame events here would spuriously clear/copy the
      //   victim rings and allow illegal same-window re-hits on rehit=0 moves.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      if (batch->state.hitlag_started_frame[idx] != 0u && batch->state.hitbox_count[idx] != 0u &&
          batch->state.hitbox_prev_bootstrap[idx] == 0u &&
          batch->state.action_id[idx] == batch->state.prev_action_id[idx]) {
        preserve_frozen_hitlag_hitboxes = 1u;
      }

      // Clear fixed slots for stable debug readback.
      for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
        const size_t oi = idx_hitbox(bi, p, hi);
        x43_b2_prev[hi] = batch->state.hitbox_x43_b2[oi];
        if (batch->state.hitbox_prev_bootstrap[idx] == 2u && batch->state.hitbox_prev_enabled[oi]) {
          seeded_prev_enabled[hi] = 1u;
          seeded_prev_x[hi] = batch->state.hitbox_prev_x[oi];
          seeded_prev_y[hi] = batch->state.hitbox_prev_y[oi];
          seeded_prev_z[hi] = batch->state.hitbox_prev_z[oi];
        }
        // Decomp shape: ftColl_8007AD18 stores previous/current capsule centers in x58/x4C.
        // Preserve the previous frame's world center before refreshing this frame's pose sample.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
        batch->state.hitbox_prev_enabled[oi] = batch->state.hitbox_enabled[oi];
        batch->state.hitbox_prev_x[oi] = batch->state.hitbox_x[oi];
        batch->state.hitbox_prev_y[oi] = batch->state.hitbox_y[oi];
        batch->state.hitbox_prev_z[oi] = batch->state.hitbox_z[oi];
        batch->state.hitbox_pose_create[oi] = 0u;
        batch->state.hitbox_enable_edge[oi] = 0u;
        if (preserve_frozen_hitlag_hitboxes) {
          batch->state.hitbox_x43_b2[oi] = x43_b2_prev[hi];
          if (batch->state.hitbox_enabled[oi]) {
            preserved_hitbox_count = (uint8_t)(preserved_hitbox_count + 1u);
          }
          continue;
        }
        batch->state.hitbox_x43_b2[oi] = 0u;

        batch->state.hitbox_enabled[oi] = 0;
        batch->state.hitbox_x[oi] = 0.0f;
        batch->state.hitbox_y[oi] = 0.0f;
        batch->state.hitbox_z[oi] = 0.0f;
        batch->state.hitbox_radius[oi] = 0.0f;
        batch->state.hitbox_damage[oi] = 0.0f;
        batch->state.hitbox_bone_part_id[oi] = 0;
        batch->state.hitbox_u16_0[oi] = 0;
        batch->state.hitbox_u16_1[oi] = 0;
        batch->state.hitbox_u16_2[oi] = 0;
        batch->state.hitbox_u16_3[oi] = 0;
        batch->state.hitbox_u16_4[oi] = 0;
        batch->state.hitbox_u16_5[oi] = 0;
        batch->state.hitbox_u16_6[oi] = 0;
        batch->state.hitbox_u16_7[oi] = 0;
        batch->state.hitbox_angle[oi] = 0;
        batch->state.hitbox_kbg[oi] = 0;
        batch->state.hitbox_wsk[oi] = 0;
        batch->state.hitbox_bkb[oi] = 0;
        batch->state.hitbox_element[oi] = 0;
        batch->state.hitbox_shield_damage[oi] = 0;
        batch->state.hitbox_sfx_severity[oi] = 0;
        batch->state.hitbox_sfx_kind[oi] = 0;
        batch->state.hitbox_flags[oi] = 0;
      }

      if (preserve_frozen_hitlag_hitboxes) {
        batch->state.hitbox_count[idx] = preserved_hitbox_count;
        continue;
      }

      batch->state.hitbox_count[idx] = 0;

      // Same-frame motion-state entry ownership:
      // - Entry paths such as ftFx_SpecialLw_Enter call ftAnim_8006EBA4 after
      //   Fighter_ChangeMotionState, so frame-0 create_hitbox commands can have fired even when
      //   the current sampled pose_frame is already 1.
      // - Those capsules are newly created in this frame; their HitCapsule victim lists are owned
      //   by ftAction_8007121C -> ftColl_8007AD18/ftColl_800768A0 clear/copy semantics, not by the
      //   previous replay snapshot's dense hitlist bridge.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_800768A0}
      const uint8_t motion_entered_this_frame =
          (batch->state.action_id[idx] != batch->state.prev_action_id[idx]) ? 1u : 0u;

      const uint32_t anim_u32 = batch->state.animation_index[idx];
      if (anim_u32 > 0xFFFFu) {
        continue;
      }
      const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);

      const uint8_t char_id = batch->state.char_id[idx];
      const uint16_t msid = (uint16_t)anim_u32;
      const uint16_t pose_frame = msl_anim_frame_floor_u16(anim_frame_f32);

      const MslHitboxEvent* events = NULL;
      uint16_t event_count = 0;
      if (hitboxes_get_events(char_id, msid, &events, &event_count) != 0 || events == NULL ||
          event_count == 0) {
        continue;
      }

      // Apply events up to this frame to derive the current active hitbox definitions by id.
      uint8_t have_def[MSL_MAX_HITBOXES] = {0};
      MslHitboxEvent def[MSL_MAX_HITBOXES] = {0};
      // Also derive the active hitbox definitions at the end of the previous integer frame
      // (pose_frame - 1), so we can reproduce Melee's hitlist clear-on-enable edge without relying
      // on sim-owned "previous frame hitbox enabled" state (important for teacher-forced reseed).
      //
      // Decomp: hitlists are cleared/copied only when the hitbox slot becomes enabled from Disabled
      // (or its hit_group changes).
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      uint8_t have_prev[MSL_MAX_HITBOXES] = {0};
      MslHitboxEvent def_prev[MSL_MAX_HITBOXES] = {0};
      uint8_t pose_create_count[MSL_MAX_HITBOXES] = {0};
      uint8_t x43_b2_cur[MSL_MAX_HITBOXES] = {0};
      uint16_t first_create_frame[MSL_MAX_HITBOXES];
      uint16_t second_create_frame[MSL_MAX_HITBOXES];
      float first_create_damage[MSL_MAX_HITBOXES] = {0.0f};
      for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
        first_create_frame[hi] = 0xFFFFu;
        second_create_frame[hi] = 0xFFFFu;
      }

      // First create_hitbox anchors per slot for this action/frame snapshot.
      // These anchors are used by reseed hitlist stale-trim windows for AttackAir ownership.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
      // data/moves/{fox,falco}.json: moves.*.events.create_hitbox
      for (uint16_t ei = 0; ei < event_count; ei++) {
        const MslHitboxEvent* ev = &events[ei];
        if ((float)ev->frame > anim_frame_f32) {
          continue;
        }
        if (ev->frame > pose_frame) {
          break;
        }
        if (ev->kind == 1 || ev->hitbox_id >= (uint8_t)MSL_MAX_HITBOXES) {
          continue;
        }
        const uint8_t hb = ev->hitbox_id;
        if (first_create_frame[hb] == 0xFFFFu) {
          first_create_frame[hb] = ev->frame;
          first_create_damage[hb] = ev->damage;
        } else if (second_create_frame[hb] == 0xFFFFu && ev->frame != first_create_frame[hb]) {
          second_create_frame[hb] = ev->frame;
        }
      }

      uint8_t has_no_damage_contact_victim = 0u;
      for (int victim = 0; victim < num_players; victim++) {
        if (victim == p) {
          continue;
        }
        const size_t v_idx = msl_idx_player(bi, victim);
        if (batch->state.hurtbox_state[v_idx] == 1u && batch->state.hitlag[v_idx] == 0u &&
            batch->state.hitstun[v_idx] == 0u) {
          has_no_damage_contact_victim = 1u;
          break;
        }
      }
      uint8_t frozen_hitlag_seeded_prev_capsules = 0u;
      if (batch->state.hitlag_started_frame[idx] != 0u &&
          batch->state.hitbox_prev_bootstrap[idx] == 2u &&
          batch->state.action_id[idx] == batch->state.prev_action_id[idx]) {
        for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
          if (hitboxes_has_authoritative_hitlist_seed(batch, bi, p, hb)) {
            frozen_hitlag_seeded_prev_capsules = 1u;
            break;
          }
        }
      }
      const uint8_t frozen_reseed_prev_capsules =
          ((has_no_damage_contact_victim && batch->state.hitlag[idx] != 0u &&
            batch->state.hitbox_prev_bootstrap[idx] == 2u &&
            batch->state.action_id[idx] == batch->state.prev_action_id[idx]) ||
           frozen_hitlag_seeded_prev_capsules)
              ? 1u
              : 0u;
      if (frozen_reseed_prev_capsules) {
        // Hitlag-frozen reseed on a create frame:
        // - Fighter_8006A360 skips ftAnim_8006EBA4 / anim_cb while hitlag is active, so
        //   ftAction_8007121C does not re-run the create command on frozen rows.
        // - Slippi can still expose the create-frame action/pose time for every frozen row; the
        //   explicit x58 previous-capsule seed (`combat_hitbox_prev_valid`) plus authoritative
        //   per-HitCapsule victims_1 seed proves the HitCapsule already exists and must be treated
        //   as the pose_frame-1 active snapshot.
        // - Without this, teacher-forced reseed replays the enable-edge clear, dropping
        //   HitCapsule.victims_1 and allowing illegal post-hitlag re-hits.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_800768A0}
        for (uint16_t ei = 0; ei < event_count; ei++) {
          const MslHitboxEvent* ev = &events[ei];
          if (ev->frame != pose_frame) {
            continue;
          }
          if (ev->kind == 1 || ev->hitbox_id >= (uint8_t)MSL_MAX_HITBOXES) {
            continue;
          }
          const uint8_t hb = ev->hitbox_id;
          if (!seeded_prev_enabled[hb] ||
              !hitboxes_has_authoritative_hitlist_seed(batch, bi, p, (int)hb)) {
            continue;
          }
          def_prev[hb] = *ev;
          have_prev[hb] = 1u;
        }
      }

      // Build hitbox definitions for:
      // - pose_frame - 1 (previous integer frame): for detecting enable edges, and
      // - pose_frame (current integer frame): used to emit the active hitboxes this step.
      //
      // We intentionally avoid relying on sim-owned "last frame hitboxes" state so that a
      // teacher-forced reseed can still reproduce the correct enable edge just from move events.
      //
      // Decomp: hitlists are cleared/copied only when a hitbox slot becomes enabled from Disabled
      // (or its hit_group changes).
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      uint8_t cur_inited = 0;

      // Hitlist materialization policy (teacher-forced reseed bridge):
      // - The seed schema carries a dense per-(attacker, hit_group, victim_port) hitlist snapshot.
      // - Runtime uses decomp-shaped HitCapsule victim rings per hitbox slot.
      // - For hitboxes that are already active at (pose_frame - 1), materialize their victim rings
      //   from the seeded snapshot once per reseed generation, before applying pose_frame events.
      //
      // This allows ftColl_800768A0 copy/clear semantics at pose_frame to see a decomp-shaped list
      // state even under teacher-forced reseed.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008440
      const uint32_t hitlist_gen = batch->state.hitlist_reseed_gen[bi];

      for (uint16_t ei = 0; ei < event_count; ei++) {
        const MslHitboxEvent* ev = &events[ei];
        // Decomp shape: movescript event timers are float-driven (fp->cur_anim_frame and
        // fp->frame_speed_mul) rather than an integer action_frame counter.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
        if ((float)ev->frame > anim_frame_f32) {
          continue;
        }
        if (ev->frame > pose_frame) {
          // Events are extracted in chronological order; nothing after pose_frame can fire this step.
          break;
        }

        // Apply all events strictly before pose_frame to build the state at pose_frame-1.
        if (ev->frame < pose_frame) {
          if (ev->kind == 1) {
            if (ev->hitbox_id == 0xFFu) {
              for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
                have_prev[hi] = 0;
              }
            } else if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
              have_prev[ev->hitbox_id] = 0;
            }
          } else if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
            def_prev[ev->hitbox_id] = *ev;
            have_prev[ev->hitbox_id] = 1;
          }
          continue;
        }

        // We are at pose_frame: initialize the current state from the pose_frame-1 snapshot once.
        if (!cur_inited) {
          for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
            if (have_prev[hi]) {
              def[hi] = def_prev[hi];
              have_def[hi] = 1;
              x43_b2_cur[hi] = x43_b2_prev[hi];

              // Seed materialize the victim list for hitboxes already active at pose_frame-1.
              const size_t hl_i = idx_hitbox(bi, p, hi);
              if (motion_entered_this_frame) {
                hitlist_capsule_clear(&batch->state.fighter_hitlist[hl_i]);
                batch->state.fighter_hitlist_init_gen[hl_i] = hitlist_gen;
                x43_b2_cur[hi] = 0u;
              } else if (batch->state.fighter_hitlist_init_gen[hl_i] != hitlist_gen) {
                const uint8_t seed_materialized_now = 1u;
                const uint8_t g = hitlist_hit_group_from_u16_7(def[hi].u16_7);
                hitlist_seed_init_fighter_hitbox_from_group(batch, bi, p, hi, g);
                hitboxes_seed_bridge_trim_impossible_indefinite(
                    batch, bi, p, hi, &def[hi], first_create_frame[hi], second_create_frame[hi],
                    first_create_damage[hi], pose_frame, seed_materialized_now, 1u);
              }
            }
          }
          cur_inited = 1;
        }

        // Apply pose_frame events to produce the current active definition set, and apply
        // ftColl_800768A0 copy/clear semantics on enable edges (including clear->create sequences
        // within the frame).
        //
        // Decomp:
        // - When a hitbox becomes enabled (or its hit_group changes), Melee copies the victim list
        //   from an existing active hitbox with the same hit_group, else clears it.
        // - This is mediated by ftColl_800768A0 calling lbColl_CopyHitCapsule (copy) or
        //   lbColl_80008440 (clear).
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_CopyHitCapsule,lbColl_80008440}
        if (ev->kind == 1) {
          if (ev->hitbox_id == 0xFFu) {
            for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
              have_def[hi] = 0;
              x43_b2_cur[hi] = 0u;
            }
          } else if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
            have_def[ev->hitbox_id] = 0;
            x43_b2_cur[ev->hitbox_id] = 0u;
          }
        } else if (ev->hitbox_id < (uint8_t)MSL_MAX_HITBOXES) {
          const uint8_t hb = ev->hitbox_id;
          pose_create_count[hb] = (uint8_t)(pose_create_count[hb] + 1u);
          const uint8_t new_g = hitlist_hit_group_from_u16_7(ev->u16_7);
          const uint8_t had_old = have_def[hb] ? 1u : 0u;
          const uint8_t old_g = had_old ? hitlist_hit_group_from_u16_7(def[hb].u16_7) : 0u;
          uint8_t x43_b2_next = had_old ? x43_b2_cur[hb] : 0u;

          def[hb] = *ev;
          have_def[hb] = 1;

          const uint8_t enable_edge = (!had_old || old_g != new_g) ? 1u : 0u;
          if (enable_edge) {
            // ftColl_800768A0: copy from an existing active hitbox with same hit_group, else clear.
            uint8_t copied = 0;
            for (int src = 0; src < MSL_MAX_HITBOXES; src++) {
              if (src == (int)hb) {
                continue;
              }
              if (!have_def[src]) {
                continue;
              }
              const uint8_t src_g = hitlist_hit_group_from_u16_7(def[src].u16_7);
              if (src_g != new_g) {
                continue;
              }
              const size_t src_i = idx_hitbox(bi, p, src);
              if (batch->state.fighter_hitlist_init_gen[src_i] != hitlist_gen) {
                hitlist_seed_init_fighter_hitbox_from_group(batch, bi, p, src, src_g);
              }
              const size_t dst_i = idx_hitbox(bi, p, hb);
              hitlist_capsule_copy(&batch->state.fighter_hitlist[src_i],
                                   &batch->state.fighter_hitlist[dst_i]);
              batch->state.fighter_hitlist_init_gen[dst_i] = hitlist_gen;
              // Decomp ownership map:
              // - ftAction_8007121C enable-edge path calls ftColl_800768A0 (copy/clear by hit_group).
              // - lbColl_CopyHitCapsule copies full HitCapsule fields, including x43_b2.
              // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
              // refs/melee/src/melee/lb/lbcollision.c::lbColl_CopyHitCapsule
              x43_b2_next = x43_b2_cur[src];
              copied = 1;
              break;
            }
            if (!copied) {
              const size_t dst_i = idx_hitbox(bi, p, hb);
              hitlist_capsule_clear(&batch->state.fighter_hitlist[dst_i]);
              batch->state.fighter_hitlist_init_gen[dst_i] = hitlist_gen;
              // ftColl_800768A0 clear lane delegates to lbColl_80008440, which leaves x43_b2 unchanged
              // inside the struct, but ftAction_8007121C immediately rewrites x43_b2=0 on create.
              // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
              // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008440
              x43_b2_next = 0u;
            }
            if (hitboxes_seed_bridge_post_contact_hitlag_hitlist_applies(batch, bi, p, (int)hb,
                                                                         new_g)) {
              hitlist_seed_init_fighter_hitbox_from_group(batch, bi, p, (int)hb, new_g);
            }
          }
          // ftAction_8007121C always initializes x43_b2=0 after processing create payload.
          // Keep this reset even if ftColl_800768A0 copied a prior capsule first.
          // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
          (void)x43_b2_next;
          x43_b2_cur[hb] = 0u;
        }
      }

      if (!cur_inited) {
        // No pose_frame events fired: the active set is the pose_frame-1 snapshot.
        for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
          if (have_prev[hi]) {
            def[hi] = def_prev[hi];
            have_def[hi] = 1;
            x43_b2_cur[hi] = x43_b2_prev[hi];

            // Seed materialize for hitboxes active at pose_frame-1 even when no pose_frame events fire.
            const size_t hl_i = idx_hitbox(bi, p, hi);
            if (motion_entered_this_frame) {
              hitlist_capsule_clear(&batch->state.fighter_hitlist[hl_i]);
              batch->state.fighter_hitlist_init_gen[hl_i] = hitlist_gen;
              x43_b2_cur[hi] = 0u;
            } else if (batch->state.fighter_hitlist_init_gen[hl_i] != hitlist_gen) {
              const uint8_t seed_materialized_now = 1u;
              const uint8_t g = hitlist_hit_group_from_u16_7(def[hi].u16_7);
              hitlist_seed_init_fighter_hitbox_from_group(batch, bi, p, hi, g);
              hitboxes_seed_bridge_trim_impossible_indefinite(
                  batch, bi, p, hi, &def[hi], first_create_frame[hi], second_create_frame[hi],
                  first_create_damage[hi], pose_frame, seed_materialized_now, 1u);
            }
          }
        }
      }

      // Hitlist clear-on-enable (per hit_group).
      //
      // Decomp:
      // - When a hitbox becomes enabled (or its hit_group changes), Melee clears its victim list
      //   unless it can copy an existing active hitbox with the same `hit_group`.
      // - This is mediated by ftColl_800768A0 calling lbColl_80008440 (clear) or lbColl_CopyHitCapsule (copy).
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008440
      //
      // Simulator policy: implemented above during pose_frame event application.

      const float pos_x = batch->state.pos_x[idx];
      const float pos_y = batch->state.pos_y[idx];
      const float pos_z = batch->state.pos_z[idx];
      const float scale_y = batch->state.fighter_scale_y[idx];
      // Decomp: runtime joint matrices include both per-fighter model scale (`fp->x34_scale.y`)
      // and the per-character "model scaling" attribute (`ftCo_DatAttrs::model_scaling`) via
      // ftCommon_GetModelScale(fp). However, Melee also applies an inverse per-character model
      // scaling at part index `fp->ft_data->x8->x10` (ftAnim_8006FA58 calls ftCommon_8007F6A4),
      // canceling out `co_attrs.model_scaling` for the collision skeleton subtree. Net effect in
      // that subtree is typically just `fp->x34_scale.y`.
      //
      // Our SSANIM01 pose matrices are extracted without those runtime scalars, so apply them here
      // before the root facing rotation and world translation.
      //
      // Decomp refs:
      // - refs/melee/src/melee/ft/fighter.c (Fighter_UpdateModelScale -> HSD_JObjSetScale)
      // - refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FA58 (inv-scale part `fp->ft_data->x8->x10`)
      // - refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F6A4 (applies 1/model_scaling at that part)
      // - refs/melee/src/melee/ft/ftparts.c::ftParts_80074B8C (ftCommon_GetModelScale usage)
      const MslCharParams* chp = msl_char_params(char_id);
      const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                      ? chp->model_scaling
                                      : 1.0f;
      const float model_scale = scale_y * model_scaling;
      const float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;

      uint8_t out_count = 0;
      for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
        if (!have_def[hi]) {
          continue;
        }

        float m[12];
        if (anim_pose_get_matrix(char_id, msid, pose_frame, def[hi].bone_part_id, m) != 0) {
          // Fallback policy: drop only this hitbox if its pose lookup fails.
          continue;
        }

        // NOTE (scaling + facing): In-engine attachment points come from `lb_8000B1CC` against the
        // bound joint's runtime HSD_JObj matrix (refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC).
        //
        // That runtime joint matrix already includes:
        // - per-fighter model scale (`fp->x34_scale.y`) via Fighter_UpdateModelScale ->
        //   HSD_JObjSetScale (refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale),
        // - per-fighter facing via a root-part Y rotation set from `fp->facing_dir`
        //   (ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir)),
        //    refs/melee/src/melee/ft/fighter.c:1180-1182).
        //
        // Our SSANIM01 v4 pose matrices are extracted in a single canonical orientation and do
        // not include the runtime facing rotation or fp->x34_scale. We apply scale in pose space
        // and apply the same decomp-shaped root facing rotation used elsewhere in the sim
        // (mixing X/Z).
        //
        // Decomp: the root part is rotated about Y by +/-90° based on `fp->facing_dir`:
        // `ftPartSetRotY(fp, 0, (M_PI_2 * fp->facing_dir))`.
        // refs/melee/src/melee/ft/fighter.c
        //
        // IMPORTANT: This must match hurtboxes_refresh() (hurtcaps) and other pose-derived geometry
        // (e.g. blaster spawn offsets). Inconsistent facing transforms can create suite-visible
        // false-positive BODY overlaps (hitlag/hitstun applied when ref has none).
        //
        // Policy:
        //   local = (pose_mtx * offset) * scale_y;
        //   local = rotY90(local, facing_dir);
        //   world = pos + local.
        //
        // Offset basis note (MSLHITB1):
        // `data/hitboxes/<char>.bin` stores hitbox center offsets in HitCapsule.b_offset component
        // order (b_offset.x/b_offset.y/b_offset.z), not the raw script field names
        // (x_offset/y_offset/z_offset). The extractor already performs the decomp-shaped mapping:
        //   b_offset.x := z_offset; b_offset.y := y_offset; b_offset.z := x_offset.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
        // Runtime policy: use extracted (x,y,z) directly as the bone-local offset passed through
        // anim_pose_get_matrix(...), i.e. do not re-apply the mapping here.
        const float off[3] = {def[hi].x, def[hi].y, def[hi].z};
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        msl_mtx34_mul_point(m, off, &cx, &cy, &cz);
        cx *= model_scale;
        cy *= model_scale;
        cz *= model_scale;
        (void)hitboxes_apply_specialhi_local_xrotn(batch, idx, char_id, msid, pose_frame,
                                                   def[hi].bone_part_id, facing_dir, model_scale,
                                                   &cx, &cy, &cz);

        // Decomp: apply root facing rotation (rotY = M_PI_2 * facing_dir), mixing X/Z.
        const float cx_rot_x = facing_dir * cz;
        const float cx_rot_z = -facing_dir * cx;
        cx = cx_rot_x;
        cz = cx_rot_z;
        cx += pos_x;
        cy += pos_y;
        cz += pos_z;

        float radius = def[hi].radius;
        // Decomp (radius scaling): Hitbox size does not get `co_attrs.model_scaling` applied at
        // creation time (ftAction_8007121C assigns hitbox->scale directly from the movescript).
        // Collision radius math uses only `fp->x34_scale.y` unless ignore_fighter_scale is set.
        //
        // Decomp refs:
        // - refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C (hitbox->scale = size/256)
        // - refs/melee/src/melee/lb/lbcollision.c::lbColl_80007AFC (radius *= fp->x34_scale.y)
        if (!msl_hitbox_ignore_fighter_scale(def[hi].u16_6)) {
          radius *= scale_y;
        }

        const size_t oi = idx_hitbox(bi, p, hi);
        uint8_t enable_edge = 1u;
        if (have_prev[hi] && !motion_entered_this_frame) {
          const uint8_t old_g = hitlist_hit_group_from_u16_7(def_prev[hi].u16_7);
          const uint8_t new_g = hitlist_hit_group_from_u16_7(def[hi].u16_7);
          enable_edge = (old_g != new_g) ? 1u : 0u;
        }
        batch->state.hitbox_enabled[oi] = 1;
        batch->state.hitbox_pose_create[oi] = (pose_create_count[hi] != 0u) ? 1u : 0u;
        batch->state.hitbox_enable_edge[oi] = enable_edge;
        batch->state.hitbox_x[oi] = cx;
        batch->state.hitbox_y[oi] = cy;
        batch->state.hitbox_z[oi] = cz;
        batch->state.hitbox_radius[oi] = radius;
        batch->state.hitbox_damage[oi] = def[hi].damage;
        batch->state.hitbox_bone_part_id[oi] = def[hi].bone_part_id;
        batch->state.hitbox_u16_0[oi] = def[hi].u16_0;
        batch->state.hitbox_u16_1[oi] = def[hi].u16_1;
        batch->state.hitbox_u16_2[oi] = def[hi].u16_2;
        batch->state.hitbox_u16_3[oi] = def[hi].u16_3;
        batch->state.hitbox_u16_4[oi] = def[hi].u16_4;
        batch->state.hitbox_u16_5[oi] = def[hi].u16_5;
        batch->state.hitbox_u16_6[oi] = def[hi].u16_6;
        batch->state.hitbox_u16_7[oi] = def[hi].u16_7;
        batch->state.hitbox_angle[oi] = def[hi].u16_0;
        batch->state.hitbox_kbg[oi] = def[hi].u16_1;
        batch->state.hitbox_wsk[oi] = def[hi].u16_2;
        batch->state.hitbox_bkb[oi] = def[hi].u16_3;
        batch->state.hitbox_element[oi] = (uint8_t)(def[hi].u16_4 & 0xFFu);
        batch->state.hitbox_shield_damage[oi] = (int8_t)((def[hi].u16_4 >> 8) & 0xFFu);
        batch->state.hitbox_sfx_severity[oi] = (uint8_t)(def[hi].u16_5 & 0xFFu);
        batch->state.hitbox_sfx_kind[oi] = (uint8_t)((def[hi].u16_5 >> 8) & 0xFFu);
        batch->state.hitbox_flags[oi] = def[hi].u16_6;
        // Same-frame entry capsule continuity:
        // - ftAction_8007121C runs from the fighter anim script at proc priority 1.
        // - ftColl_8007AE80 refreshes hitcapsules later at priority 9.
        // - For a newly enabled capsule, ftColl_8007AD18 sets x4C from the current bone pose and
        //   immediately copies x58 = x4C before BODY/shield collision runs.
        // Do not synthesize a previous-action x58 for normal same-frame entries; that creates
        // broad swept BODY contacts that vanilla never tests.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_8007AE80}
        // x43_b2 ownership mapping:
        // - ftAction_8007121C create path initializes x43_b2=0.
        // - ftColl_800768A0 copy/clear transitions preserve per-slot runtime ownership otherwise.
        // - ftColl_80078C70 forwards x43_b2 as lbColl_8000805C arg3.
        // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}
        // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
        batch->state.hitbox_x43_b2[oi] = x43_b2_cur[hi];
        out_count++;
      }

      batch->state.hitbox_count[idx] = out_count;

      if (frozen_reseed_prev_capsules && out_count != 0u) {
        // Teacher-forced no-damage contact carry:
        // - ftColl_80076ED8 inserts the BODY victim into HitCapsule.victims_1 before checking
        //   vulnerable damage state. Invincible/no-damage contacts can therefore freeze only the
        //   attacker in hitlag while still latching the victim for rehit suppression.
        // - A reseed inside that hitlag segment has no live HitCapsule history unless the seed
        //   explicitly materializes it. The explicit x58 previous-capsule lane plus hitlag proves the
        //   active capsules already existed; invincible visible hurtbox state selects the no-damage
        //   victim boundary without keying on replay identity.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80008688}
        for (int victim = 0; victim < num_players; victim++) {
          if (victim == p) {
            continue;
          }
          const size_t v_idx = msl_idx_player(bi, victim);
          if (batch->state.hurtbox_state[v_idx] != 1u || batch->state.hitlag[v_idx] != 0u ||
              batch->state.hitstun[v_idx] != 0u) {
            continue;
          }
          uint8_t seed_already_carries_victim = 0u;
          const size_t group_base = (size_t)bi * (size_t)MSL_MAX_PLAYERS *
                                    (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
          for (int g = 0; g < MSL_HITLIST_GROUPS; g++) {
            const size_t cd_i = group_base + (((size_t)p * (size_t)MSL_HITLIST_GROUPS + (size_t)g) *
                                                  (size_t)MSL_MAX_PLAYERS +
                                              (size_t)victim);
            if (batch->state.combat_hitlist_cd[cd_i] != 0u) {
              seed_already_carries_victim = 1u;
              break;
            }
          }
          const size_t hb_valid_base =
              ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES;
          for (int hb = 0; hb < MSL_MAX_HITBOXES && !seed_already_carries_victim; hb++) {
            if (!batch->state.combat_hitlist_hb_valid[hb_valid_base + (size_t)hb]) {
              continue;
            }
            // `combat_hitlist_hb_valid=1, combat_hitlist_hb_cd=0` is an authoritative empty
            // HitCapsule victims_1 seed, not a missing seed. Respect it as blocking the coarse
            // dense fallback/materialization path.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_800768A0
            // refs/melee/src/melee/lb/types.h::HitCapsule
            seed_already_carries_victim = 1u;
            break;
          }
          if (seed_already_carries_victim) {
            continue;
          }
          for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
            if (!have_def[hi]) {
              continue;
            }
            const uint8_t hit_group = hitlist_hit_group_from_u16_7(def[hi].u16_7);
            const uint8_t rehit_frames = (uint8_t)(def[hi].u16_7 & 0xFFu);
            hitlist_register_fighter_group(batch, bi, p, hit_group, victim,
                                           batch->state.instance_id[v_idx],
                                           (int)MSL_LBCOLL_INSERT_FT_BODY, rehit_frames);
          }
        }
      }

      if (batch->state.hitbox_prev_bootstrap[idx]) {
        // Teacher-forced reseed bootstrap (first frame only):
        // Decomp keeps previous/current hitcapsule centers (x58/x4C) across frames.
        // On reseed, use the explicit seed x58 lane when available; otherwise bootstrap from
        // in-frame translation so older/synthetic seeds still have a deterministic swept segment.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_8000805C}
        const float dx = batch->state.pos_x[idx] - batch->state.prev_pos_x[idx];
        const float dy = batch->state.pos_y[idx] - batch->state.prev_pos_y[idx];
        const uint8_t use_seeded_x58 = (batch->state.hitbox_prev_bootstrap[idx] == 2u) ? 1u : 0u;
        for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
          const size_t oi = idx_hitbox(bi, p, hi);
          if (batch->state.hitbox_enabled[oi]) {
            if (use_seeded_x58 && seeded_prev_enabled[hi] && !motion_entered_this_frame) {
              batch->state.hitbox_prev_enabled[oi] = 1u;
              batch->state.hitbox_prev_x[oi] = seeded_prev_x[hi];
              batch->state.hitbox_prev_y[oi] = seeded_prev_y[hi];
              batch->state.hitbox_prev_z[oi] = seeded_prev_z[hi];
            } else if (motion_entered_this_frame || batch->state.hitbox_enable_edge[oi] ||
                       !batch->state.hitbox_prev_enabled[oi]) {
              // Newly enabled capsules enter ftColl_8007AD18 through the HitCapsule_Enabled case,
              // which sets x4C from the refreshed current pose and immediately copies x58 = x4C.
              // This applies to ordinary same-action create edges as well as motion-entry edges;
              // do not synthesize a translation sweep for a HitCapsule that did not have a live
              // previous x58 in vanilla.
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
              batch->state.hitbox_prev_enabled[oi] = 1u;
              batch->state.hitbox_prev_x[oi] = batch->state.hitbox_x[oi];
              batch->state.hitbox_prev_y[oi] = batch->state.hitbox_y[oi];
              batch->state.hitbox_prev_z[oi] = batch->state.hitbox_z[oi];
            } else {
              batch->state.hitbox_prev_enabled[oi] = 1u;
              batch->state.hitbox_prev_x[oi] = batch->state.hitbox_x[oi] - dx;
              batch->state.hitbox_prev_y[oi] = batch->state.hitbox_y[oi] - dy;
              batch->state.hitbox_prev_z[oi] = batch->state.hitbox_z[oi];
            }
          } else {
            batch->state.hitbox_prev_enabled[oi] = 0u;
            batch->state.hitbox_prev_x[oi] = 0.0f;
            batch->state.hitbox_prev_y[oi] = 0.0f;
            batch->state.hitbox_prev_z[oi] = 0.0f;
          }
        }
        batch->state.hitbox_prev_bootstrap[idx] = 0u;
      }
    }
  }
}
