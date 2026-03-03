#include "hitboxes.h"

#include <math.h>
#include <stdint.h>

#include "anim_frame.h"
#include "anim_pose.h"
#include "action_ids.h"
#include "char_params.h"
#include "common_params.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "mtx34.h"

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
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

static void hitboxes_seed_bridge_trim_impossible_indefinite(MslBatch* batch, int bi, int attacker,
                                                            int hb_id, const MslHitboxEvent* def,
                                                            uint16_t first_create_frame,
                                                            uint16_t second_create_frame,
                                                            float first_create_damage,
                                                            uint16_t pose_frame,
                                                            uint8_t seed_materialized_now,
                                                            uint8_t from_prev_active_snapshot) {
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
    if (e->cd != 0u) {
      continue;
    }
    const uint8_t victim_port = msl_hitlist_victim_slot(e->kind_slot);
    if (victim_port >= (uint8_t)batch->config.num_players || victim_port == (uint8_t)attacker) {
      continue;
    }
    const size_t v_idx = msl_idx_player(bi, (int)victim_port);

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
        const uint16_t first_hitlag = hitboxes_seed_bridge_shield_hitlag_frames(c, first_create_damage);
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
    // Shield-ownership guard for this trim:
    // - ftColl_80078C70 gates shield collision by live ShieldDesc ownership (`fp->x221B_b0`), then
    //   calls lbColl_80007BCC with the shield descriptor.
    // - Our runtime shield lane treats nonzero `shield_radius` as active ShieldDesc ownership in the
    //   same collision windows.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
    const uint8_t shield_desc_active = (batch->state.shield_radius[v_idx] > 0.0f) ? 1u : 0u;
    const uint16_t v_action = batch->state.action_id[v_idx];
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

    // Narrow stale-owner extension for active windows with repeated suite misses:
    // - AttackAirLw scripts (Fox/Falco) can re-enable same-group hitboxes in-window while dense seed
    //   hitlists carry stale indefinite victim presence across reseed boundaries.
    // - Keep this lane scoped to AttackAirLw where extracted multi-create scripts are present.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008A5C}
    // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
    const uint8_t stale_owner_action_lane =
        (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_LW) ? 1u : 0u;
    if (stale_owner_action_lane && !shield_desc_active &&
        !hitboxes_seed_bridge_is_guard_transition_owner(v_action) &&
        batch->state.hitlag[v_idx] == 0u && batch->state.hitstun[v_idx] == 0u &&
        batch->state.instance_hit_by[v_idx] != attacker_iid &&
        batch->state.last_hit_by[v_idx] != (uint8_t)attacker) {
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
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);
      batch->state.hitbox_count[idx] = 0;
      uint8_t x43_b2_prev[MSL_MAX_HITBOXES] = {0};

      // Clear fixed slots for stable debug readback.
      for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
        const size_t oi = idx_hitbox(bi, p, hi);
        x43_b2_prev[hi] = batch->state.hitbox_x43_b2[oi];
        // Decomp shape: ftColl_8007AD18 stores previous/current capsule centers in x58/x4C.
        // Preserve the previous frame's world center before refreshing this frame's pose sample.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
        batch->state.hitbox_prev_enabled[oi] = batch->state.hitbox_enabled[oi];
        batch->state.hitbox_prev_x[oi] = batch->state.hitbox_x[oi];
        batch->state.hitbox_prev_y[oi] = batch->state.hitbox_y[oi];
        batch->state.hitbox_prev_z[oi] = batch->state.hitbox_z[oi];
        batch->state.hitbox_pose_create[oi] = 0u;
        batch->state.hitbox_enable_edge[oi] = 0u;
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

      if (p >= num_players) {
        continue;
      }

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
              if (batch->state.fighter_hitlist_init_gen[hl_i] != hitlist_gen) {
                const uint8_t seed_materialized_now = 1u;
                const uint8_t g = hitlist_hit_group_from_u16_7(def[hi].u16_7);
                hitlist_seed_init_fighter_hitbox_from_group(batch, bi, p, hi, g);
                hitboxes_seed_bridge_trim_impossible_indefinite(batch, bi, p, hi, &def[hi],
                                                                first_create_frame[hi],
                                                                second_create_frame[hi],
                                                                first_create_damage[hi], pose_frame,
                                                                seed_materialized_now,
                                                                1u);
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
            if (batch->state.fighter_hitlist_init_gen[hl_i] != hitlist_gen) {
              const uint8_t seed_materialized_now = 1u;
              const uint8_t g = hitlist_hit_group_from_u16_7(def[hi].u16_7);
              hitlist_seed_init_fighter_hitbox_from_group(batch, bi, p, hi, g);
              hitboxes_seed_bridge_trim_impossible_indefinite(batch, bi, p, hi, &def[hi],
                                                              first_create_frame[hi],
                                                              second_create_frame[hi],
                                                              first_create_damage[hi], pose_frame,
                                                              seed_materialized_now,
                                                              1u);
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
        // Our SSANIM01 v3 pose matrices are extracted in a single canonical orientation and do
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
        if (have_prev[hi]) {
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

      if (batch->state.hitbox_prev_bootstrap[idx]) {
        // Teacher-forced reseed bootstrap (first frame only):
        // Decomp keeps previous/current hitcapsule centers (x58/x4C) across frames.
        // On reseed, we lack persisted x58; bootstrap it from in-frame translation so shield
        // overlap tests (lbColl_80007BCC) can still use a swept segment on the first stepped frame.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
        // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
        const float dx = batch->state.pos_x[idx] - batch->state.prev_pos_x[idx];
        const float dy = batch->state.pos_y[idx] - batch->state.prev_pos_y[idx];
        for (int hi = 0; hi < MSL_MAX_HITBOXES; hi++) {
          const size_t oi = idx_hitbox(bi, p, hi);
          if (batch->state.hitbox_enabled[oi]) {
            batch->state.hitbox_prev_enabled[oi] = 1u;
            batch->state.hitbox_prev_x[oi] = batch->state.hitbox_x[oi] - dx;
            batch->state.hitbox_prev_y[oi] = batch->state.hitbox_y[oi] - dy;
            batch->state.hitbox_prev_z[oi] = batch->state.hitbox_z[oi];
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
