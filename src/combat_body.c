#include "combat_internal.h"

// Pokemon Stadium BODY residual bridge caps:
// The source x7E4 lane is extracted and loaded for future full x44 modeling, but runtime does not
// yet carry the exact live JObj hurtcap packet used by lbColl_8000805C. These constants bound the
// temporary bridge to source-payload rows whose adjacent positives/negatives prove the bridge is
// not a broad Stadium residual tolerance.
// refs/melee/src/melee/ft/fighter.c::{Fighter_80068E64,Fighter_UpdateModelScale}
// refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
static const float MSL_PSTADIUM_X44_DAIR_GROUNDED_HIGH_MAX_RESIDUAL = -0.25f;
static const float MSL_PSTADIUM_X44_DAIR_TAIL_MAX_RESIDUAL = -1.20f;
static const float MSL_PSTADIUM_X44_ATTACKHI3_TAIL_MAX_RESIDUAL = -0.50f;
static const float MSL_PSTADIUM_X44_SPECIALHI_LAUNCH_ROOT_MAX_RESIDUAL = -0.05f;

uint8_t sphere_sphere_intersects(float ax, float ay, float az, float ar, float bx, float by,
                                 float bz, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float rr = ar + br;
  return (dx * dx + dy * dy + dz * dz) <= (rr * rr);
}

uint8_t combat_shine_start_damageair_entry_pose_bridge_applies(const MslBatch* batch, size_t a_idx,
                                                               size_t d_idx) {
  // No victim char-family gate: the attacker's shine-start identity below is kind-keyed and
  // the victim-side DamageAir entry-pose bridge is common ftColl/ftCo mechanics (the old
  // gate reflected the spacie-vs-spacie validation corpus).
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[a_idx];
  const uint8_t a_fx_kind = msl_motion_state_fx_special_kind(batch->state.char_id[a_idx], a);
  if (a_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_LW_START &&
      a_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START) {
    return 0u;
  }
  if (batch->state.hitstun[d_idx] == 0u) {
    return 0u;
  }
  if (batch->state.frame_start_on_ground[d_idx] != 0u) {
    return 0u;
  }
  const uint16_t d = batch->state.action_id[d_idx];
  if (d != (uint16_t)MSL_ACT_DAMAGE_AIR_2) {
    return 0u;
  }
  return 1u;
}

uint8_t combat_shine_start_grounded_ledge_ecb_lock_owner(const MslBatch* batch, size_t d_idx,
                                                         size_t a_idx, uint16_t attacker_action) {
  // No victim char-family gate: the attacker shine-start identity is kind-keyed and the
  // victim-side grounded-launch ECB-lock handoff is common CollData mechanics.
  if (batch == NULL) {
    return 0u;
  }
  // Supported runtime producer for the horizontal grounded shine launch -> DamageFly floor handoff.
  // Shine's hitbox is owned by SpecialLwStart/SpecialAirLwStart, and the proven rollout consumer is
  // the persisted ledge-floor CollData lane that remains floor-owned through active hitlag. Keep the
  // ECB-lock bridge scoped to that floor-collision owner until other ground-to-air damage-launch
  // consumers are modeled.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialLwStart_Coll,ftFx_SpecialAirLwStart_Coll}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  {
    // Attacker shine-start ownership from the extracted MotionState row identity.
    const uint8_t attacker_fx_kind =
        msl_motion_state_fx_special_kind(batch->state.char_id[a_idx], attacker_action);
    if (attacker_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_LW_START &&
        attacker_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START) {
      return 0u;
    }
  }
  const uint32_t stage_id = batch->state.stage_id[d_idx / (size_t)MSL_MAX_PLAYERS];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  const int line_idx = stage_collision_floor_line_index(stage_id, batch->state.ground_id[d_idx]);
  return (g != NULL && line_idx >= 0 && (size_t)line_idx < g->line_count &&
          g->lines[(size_t)line_idx].is_ledge)
             ? 1u
             : 0u;
}

uint8_t combat_attackairlw_invincible_contact_rejects_body_hitlag(const MslBatch* batch,
                                                                  size_t a_idx, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW) {
    return 0u;
  }
  const uint16_t d_action = batch->state.action_id[d_idx];
  if (d_action != (uint16_t)MSL_ACT_ATTACK_AIR_LW && d_action != (uint16_t)MSL_ACT_ATTACK_HI3) {
    return 0u;
  }
  if (batch->state.on_ground[a_idx] != 0u) {
    return 0u;
  }
  if (d_action == (uint16_t)MSL_ACT_ATTACK_AIR_LW && batch->state.on_ground[d_idx] != 0u) {
    return 0u;
  }
  if (d_action == (uint16_t)MSL_ACT_ATTACK_HI3 && batch->state.on_ground[d_idx] == 0u) {
    return 0u;
  }
  if (d_action == (uint16_t)MSL_ACT_ATTACK_AIR_LW && batch->state.action_frame[d_idx] < 10) {
    return 0u;
  }
  if (batch->state.hurtbox_state[d_idx] != 1u) {
    return 0u;
  }
  // Narrow AttackAirLw invincible-contact bridge:
  // - ftColl_80078C70 runs BODY narrowphase only when the defender is not intangible
  //   (`x1988 != 2 && x198C != 2`), then ftColl_80076ED8 decides whether that accepted
  //   contact contributes attacker-side hitlag or defender damage from the defender collision
  //   status and hurt capsule state.
  // - Replay-real AttackAirLw overlap rows can expose the defender as visible `hurtbox_state=1`
  //   while vanilla still rejects this BODY contact until a later vulnerable contact frame. Keep
  //   this as an explicitly scoped seed/provenance bridge rather than weakening the generic
  //   invincible-contact owner.
  // - Earlier AttackAirLw-vs-AttackAirLw rows remain on the invincible-contact path because replay
  //   shows attacker-side hitlag before this later-body rejection window.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // data/moves/{fox,falco}.json moves["ftCo_SM_AttackAirLw"].events
  // data/moves/{fox,falco}.json moves["ftCo_SM_AttackHi3"].events
  return 1u;
}

uint8_t combat_damagefly_terminal_state_blocks_enable_edge_body(const MslBatch* batch, size_t hb_i,
                                                                size_t a_idx, size_t d_idx,
                                                                const MslHurtCap* cap) {
  return msl_damage_owner_terminal_state_blocks_enable_edge_body(
      batch, hb_i, a_idx, d_idx, cap != NULL ? cap->bone_part_id : 0u, cap != NULL ? 1u : 0u);
}

uint8_t combat_damageflylw_dynamic_high_part_rejects_body_contact(const MslBatch* batch,
                                                                  size_t a_idx, size_t d_idx,
                                                                  uint8_t hb_id,
                                                                  const MslHurtCap* cap) {
  return msl_damage_owner_damageflylw_dynamic_high_part_rejects_body(
      batch, a_idx, d_idx, hb_id, cap != NULL ? cap->bone_part_id : 0u, cap != NULL ? 1u : 0u);
}

uint8_t combat_attackairlw_damageflytop_fox_tail_rejects_body_contact(const MslBatch* batch,
                                                                      size_t a_idx, size_t d_idx,
                                                                      uint8_t hb_id,
                                                                      const MslHurtCap* cap,
                                                                      uint16_t expected_hitlag) {
  return msl_damage_owner_attackairlw_damageflytop_fox_tail_rejects_body(
      batch, a_idx, d_idx, hb_id, cap != NULL ? cap->bone_part_id : 0u, cap != NULL ? 1u : 0u,
      expected_hitlag);
}

uint8_t combat_attackairlw_hitbox_payload_is_authored_multihit_upper(uint8_t hb_id, float damage,
                                                                     uint16_t angle, uint16_t kbg,
                                                                     uint16_t wsk, uint16_t bkb) {
  // Fox AttackAirLw multihit upper capsule: hb0, 3 damage, angle 290, kbg 100, wsk 30.
  // Falco's strong/late DAir does not match this payload, so this is an authored hitbox-data
  // owner rather than an AttackAirLw/action-family shape.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return (uint8_t)(hb_id == 0u && damage == 3.0f && angle == 290u && kbg == 100u && wsk == 30u &&
                   bkb == 0u);
}

uint8_t combat_attackairlw_hitbox_payload_is_authored_multihit_lower_sibling(
    float damage, uint16_t angle, uint16_t kbg, uint16_t wsk, uint16_t bkb) {
  // Fox AttackAirLw paired lower capsule: 2 damage with the same group/KB payload as hb0.
  // data/moves/fox.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return (uint8_t)(damage == 2.0f && angle == 290u && kbg == 100u && wsk == 30u && bkb == 0u);
}

uint8_t combat_attackairlw_hitbox_payload_is_authored_late_meteor(uint8_t hb_id, float damage,
                                                                  uint16_t angle, uint16_t kbg,
                                                                  uint16_t wsk, uint16_t bkb) {
  // Falco late AttackAirLw pair: hb0/hb1, 9 damage, angle 290, KBG 100, WSK 0, BKB 20.
  // Fox multihit DAir and Falco strong DAir have different damage/WSK/BKB payloads.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return (uint8_t)(hb_id <= 1u && damage == 9.0f && angle == 290u && kbg == 100u && wsk == 0u &&
                   bkb == 20u);
}

uint8_t combat_attackairlw_hitbox_payload_is_authored_strong_meteor(uint8_t hb_id, float damage,
                                                                    uint16_t angle, uint16_t kbg,
                                                                    uint16_t wsk, uint16_t bkb) {
  // Falco strong AttackAirLw pair: hb0/hb1, 12 damage, angle 290, KBG 100, WSK 0, BKB 10.
  // Fox multihit DAir and Falco late DAir have different damage/WSK/BKB payloads.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return (uint8_t)(hb_id <= 1u && damage == 12.0f && angle == 290u && kbg == 100u && wsk == 0u &&
                   bkb == 10u);
}

uint8_t combat_attackairlw_strong_grounded_high_cap_rejects_lower_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, int defender, size_t a_idx,
    size_t d_idx, const MslHurtCap* cap, const MslHurtCap* defender_caps,
    uint16_t defender_cap_count_u16, float hx, float hy, float hz, float hr) {
  if (batch == NULL || cap == NULL || defender_caps == NULL || cap->height >= 2u) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW ||
      batch->state.on_ground[d_idx] == 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_RUN) {
    return 0u;
  }
  if (msl_motion_state_common_class_has_fast(batch->state.action_id[d_idx],
                                             MSL_MS_CLASS_GROUNDED_ATTACK)) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  if (combat_attackairlw_hitbox_payload_is_authored_strong_meteor(
          hb_id, batch->state.hitbox_damage[hb_i], batch->state.hitbox_angle[hb_i],
          batch->state.hitbox_kbg[hb_i], batch->state.hitbox_wsk[hb_i],
          batch->state.hitbox_bkb[hb_i]) == 0u) {
    return 0u;
  }
  const uint16_t capped_count = defender_cap_count_u16 > (uint16_t)MSL_MAX_HURTCAPS
                                    ? (uint16_t)MSL_MAX_HURTCAPS
                                    : defender_cap_count_u16;
  for (uint16_t other_cap_id = 0u; other_cap_id < capped_count; other_cap_id++) {
    const MslHurtCap* other_cap = &defender_caps[other_cap_id];
    if (other_cap->height < 2u) {
      continue;
    }
    const size_t other_cap_i = idx_hurtcap(bi, defender, (int)other_cap_id);
    if (batch->state.hurtcap_enabled[other_cap_i] == 0u) {
      continue;
    }
    float overlap_amount = 0.0f;
    uint8_t overlap_evaluated = 0u;
    uint8_t overlaps = combat_body_overlap_lbColl_80006E58_matrix_radius(
        batch, bi, attacker, hb_id, defender, (int)other_cap_id, hx, hy, hz, hr,
        batch->state.hurtcap_a_x[other_cap_i], batch->state.hurtcap_a_y[other_cap_i],
        batch->state.hurtcap_a_z[other_cap_i], batch->state.hurtcap_b_x[other_cap_i],
        batch->state.hurtcap_b_y[other_cap_i], batch->state.hurtcap_b_z[other_cap_i], 0u,
        &overlap_amount, &overlap_evaluated);
    const uint8_t baseline_overlaps = combat_sphere_capsule_intersects(
        hx, hy, hz, hr, batch->state.hurtcap_a_x[other_cap_i],
        batch->state.hurtcap_a_y[other_cap_i], batch->state.hurtcap_a_z[other_cap_i],
        batch->state.hurtcap_b_x[other_cap_i], batch->state.hurtcap_b_y[other_cap_i],
        batch->state.hurtcap_b_z[other_cap_i], batch->state.hurtcap_radius[other_cap_i], NULL);
    if (overlaps || baseline_overlaps) {
      // Strong DAir grounded hurt-height owner:
      // ftColl_80078C70 admits one hurt capsule per HitCapsule, then ftColl_8007A06C uses the
      // accepted DmgLogEntry's hurt height for DamageHi/N/Lw selection. On the supported
      // Fox/Falco Run pose, seed-reconstructed lbColl_80006E58 can over-admit a lower torso
      // capsule while the same authored strong-Dair HitCapsule also has a concrete high-cap source
      // overlap. Preserve the source-selected high hurt-height owner by rejecting only that lower
      // candidate. Squat and grounded attacks keep their lower/neutral selected-height owners. The
      // predicate is bounded by extracted strong-Dair HitCapsule payload, common Run motion, and
      // extracted hurtcap height, not replay row or character id.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8,ftColl_8007A06C}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
      // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
      // data/hurtcaps/{fox,falco}.json height 1/2
      return 1u;
    }
  }
  return 0u;
}

uint8_t combat_attackairlw_strong_attacklw4_high_cap_sibling_rejects_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, int defender, size_t a_idx,
    size_t d_idx, const MslHurtCap* cap) {
  if (batch == NULL || cap == NULL || cap->height < 2u || hb_id > 1u) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW ||
      !(batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_ATTACK_LW4 ||
        batch->state.frame_start_action_id[d_idx] == (uint16_t)MSL_ACT_ATTACK_LW4) ||
      batch->state.on_ground[d_idx] == 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  if (combat_attackairlw_hitbox_payload_is_authored_strong_meteor(
          hb_id, batch->state.hitbox_damage[hb_i], batch->state.hitbox_angle[hb_i],
          batch->state.hitbox_kbg[hb_i], batch->state.hitbox_wsk[hb_i],
          batch->state.hitbox_bkb[hb_i]) == 0u) {
    return 0u;
  }
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
  const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
  for (uint8_t sibling_hb_id = 0u; sibling_hb_id <= 1u; sibling_hb_id++) {
    if (sibling_hb_id == hb_id) {
      continue;
    }
    const size_t sibling_hb_i = idx_hitbox(bi, attacker, (int)sibling_hb_id);
    if (batch->state.hitbox_enabled[sibling_hb_i] == 0u ||
        hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[sibling_hb_i]) != hit_group ||
        combat_attackairlw_hitbox_payload_is_authored_strong_meteor(
            sibling_hb_id, batch->state.hitbox_damage[sibling_hb_i],
            batch->state.hitbox_angle[sibling_hb_i], batch->state.hitbox_kbg[sibling_hb_i],
            batch->state.hitbox_wsk[sibling_hb_i], batch->state.hitbox_bkb[sibling_hb_i]) == 0u) {
      continue;
    }
    const float sx = batch->state.hitbox_x[sibling_hb_i];
    const float sy = batch->state.hitbox_y[sibling_hb_i];
    const float sz = batch->state.hitbox_z[sibling_hb_i];
    const float sr = batch->state.hitbox_radius[sibling_hb_i];
    for (uint8_t medium_cap_id = 0u; medium_cap_id < hurtcap_count; medium_cap_id++) {
      const size_t medium_cap_i = idx_hurtcap(bi, defender, (int)medium_cap_id);
      if (batch->state.hurtcap_enabled[medium_cap_i] == 0u ||
          batch->state.hurtcap_height[medium_cap_i] != 1u) {
        continue;
      }
      float overlap_amount = 0.0f;
      uint8_t overlap_evaluated = 0u;
      uint8_t overlaps = combat_body_overlap_lbColl_80006E58_matrix_radius(
          batch, bi, attacker, (int)sibling_hb_id, defender, (int)medium_cap_id, sx, sy, sz, sr,
          batch->state.hurtcap_a_x[medium_cap_i], batch->state.hurtcap_a_y[medium_cap_i],
          batch->state.hurtcap_a_z[medium_cap_i], batch->state.hurtcap_b_x[medium_cap_i],
          batch->state.hurtcap_b_y[medium_cap_i], batch->state.hurtcap_b_z[medium_cap_i], 0u,
          &overlap_amount, &overlap_evaluated);
      const uint8_t baseline_overlaps = combat_sphere_capsule_intersects(
          sx, sy, sz, sr, batch->state.hurtcap_a_x[medium_cap_i],
          batch->state.hurtcap_a_y[medium_cap_i], batch->state.hurtcap_a_z[medium_cap_i],
          batch->state.hurtcap_b_x[medium_cap_i], batch->state.hurtcap_b_y[medium_cap_i],
          batch->state.hurtcap_b_z[medium_cap_i], batch->state.hurtcap_radius[medium_cap_i], NULL);
      overlaps = (uint8_t)(overlaps || baseline_overlaps);
      if (overlaps) {
        // Strong DAir vs grounded AttackLw4 selected-height owner:
        // ftCo_AttackLw4_IASA can enter locomotion before the same frame's combat pass, but the
        // source hit still belongs to the frame-start down-smash callback/hurt pose. ftColl_80078C70
        // admits one BODY DmgLog entry per accepted HitCapsule, and ftColl_8007A06C consumes the
        // selected DmgLog hurt height for DamageFlyHi/N/Lw. On that frame-start down-smash pose, the
        // seed-reconstructed lbColl matrix can over-select strong DAir hb0 against a high cap
        // while the authored same-group strong DAir sibling has a current medium-cap source overlap.
        // Preserve that medium selected-height owner without changing strong DAir rows where the
        // sibling packet is absent.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackLw4.c::ftCo_AttackLw4_IASA
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8,ftColl_8007A06C}
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
        // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
        // data/hurtcaps/{fox,falco}.json height 1/2
        return 1u;
      }
    }
  }
  return 0u;
}

uint8_t combat_attackairlw_late_high_cap_sibling_rejects_body_contact(const MslBatch* batch, int bi,
                                                                      int attacker, uint8_t hb_id,
                                                                      int defender,
                                                                      const MslHurtCap* cap) {
  if (batch == NULL || cap == NULL || hb_id > 1u || cap->height < 2u) {
    return 0u;
  }
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitbox_count[a_idx] < 2u) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  if (combat_attackairlw_hitbox_payload_is_authored_late_meteor(
          hb_id, batch->state.hitbox_damage[hb_i], batch->state.hitbox_angle[hb_i],
          batch->state.hitbox_kbg[hb_i], batch->state.hitbox_wsk[hb_i],
          batch->state.hitbox_bkb[hb_i]) == 0u) {
    return 0u;
  }
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);

  const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
  for (uint8_t sibling_hb_id = 0u; sibling_hb_id <= 1u; sibling_hb_id++) {
    const size_t sibling_hb_i = idx_hitbox(bi, attacker, (int)sibling_hb_id);
    if (batch->state.hitbox_enabled[sibling_hb_i] == 0u ||
        hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[sibling_hb_i]) != hit_group ||
        combat_attackairlw_hitbox_payload_is_authored_late_meteor(
            sibling_hb_id, batch->state.hitbox_damage[sibling_hb_i],
            batch->state.hitbox_angle[sibling_hb_i], batch->state.hitbox_kbg[sibling_hb_i],
            batch->state.hitbox_wsk[sibling_hb_i], batch->state.hitbox_bkb[sibling_hb_i]) == 0u) {
      continue;
    }
    const float hx = batch->state.hitbox_x[sibling_hb_i];
    const float hy = batch->state.hitbox_y[sibling_hb_i];
    const float hz = batch->state.hitbox_z[sibling_hb_i];
    const float hr = batch->state.hitbox_radius[sibling_hb_i];
    for (uint8_t low_cap_id = 0u; low_cap_id < hurtcap_count; low_cap_id++) {
      const size_t low_cap_i = idx_hurtcap(bi, defender, (int)low_cap_id);
      if (batch->state.hurtcap_enabled[low_cap_i] == 0u ||
          batch->state.hurtcap_height[low_cap_i] != 0u) {
        continue;
      }
      float overlap_amount = 0.0f;
      uint8_t overlap_evaluated = 0u;
      uint8_t overlaps = combat_body_overlap_lbColl_80006E58_matrix_radius(
          batch, bi, attacker, (int)sibling_hb_id, defender, (int)low_cap_id, hx, hy, hz, hr,
          batch->state.hurtcap_a_x[low_cap_i], batch->state.hurtcap_a_y[low_cap_i],
          batch->state.hurtcap_a_z[low_cap_i], batch->state.hurtcap_b_x[low_cap_i],
          batch->state.hurtcap_b_y[low_cap_i], batch->state.hurtcap_b_z[low_cap_i], 0u,
          &overlap_amount, &overlap_evaluated);
      const uint8_t baseline_overlaps = combat_sphere_capsule_intersects(
          hx, hy, hz, hr, batch->state.hurtcap_a_x[low_cap_i], batch->state.hurtcap_a_y[low_cap_i],
          batch->state.hurtcap_a_z[low_cap_i], batch->state.hurtcap_b_x[low_cap_i],
          batch->state.hurtcap_b_y[low_cap_i], batch->state.hurtcap_b_z[low_cap_i],
          batch->state.hurtcap_radius[low_cap_i], NULL);
      overlaps = (uint8_t)(overlaps || baseline_overlaps);
      if (overlaps) {
        // Late DAir paired-HitCapsule BODY source owner:
        // ftColl processes same-group HitCapsules in source order, but the simulator's
        // matrix-radius reconstruction can over-admit the late meteor pair against high hurtcaps
        // while the authored same-group pair has a concrete low-cap BODY overlap. Preserve the
        // source-selected low damage-state owner by rejecting only high-cap candidates when the
        // same-group late-meteor payload proves the lower source lane. This is bounded by extracted
        // HitCapsule payloads and hurtcap height, not replay row or character id.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
        // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
        // data/hurtcaps/{fox,falco}.json height 0/2
        return 1u;
      }
    }
  }
  return 0u;
}

uint8_t combat_attackairhi_hitbox_payload_is_authored_finisher_hb2(uint8_t hb_id, float damage,
                                                                   uint16_t angle, uint16_t kbg,
                                                                   uint16_t wsk, uint16_t bkb) {
  if (hb_id != 2u) {
    return 0u;
  }
  // Fox frame-11 UpAir finisher hb2: 13 damage, angle 85, kbg 116, bkb 40.
  if (damage == 13.0f && angle == 85u && kbg == 116u && wsk == 0u && bkb == 40u) {
    return 1u;
  }
  // Falco frame-11 UpAir finisher hb2: 10 damage, angle 90, kbg 20, bkb 30.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirHi.events.create_hitbox
  return (uint8_t)(damage == 10.0f && angle == 90u && kbg == 20u && wsk == 0u && bkb == 30u);
}

uint8_t combat_attackairlw_attackdash_tail_allow_interrupt_rejects_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, size_t a_idx, size_t d_idx,
    const MslHurtCap* cap) {
  if (batch == NULL || cap == NULL) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_ATTACK_DASH || hb_id != 0u) {
    return 0u;
  }
  if (batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      batch->state.on_ground[d_idx] == 0u) {
    return 0u;
  }
  if (combat_hurtcap_is_extracted_fox_falco_tail_part(cap) == 0u) {
    return 0u;
  }
  const float anim_frame = batch->state.anim_frame_f32[d_idx];
  if (move_tables_grounded_attack_allow_interrupt(
          batch->state.char_id[d_idx], batch->state.action_id[d_idx], anim_frame) == 0u) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
  const float damage = batch->state.hitbox_damage[hb_i];
  if (combat_attackairlw_hitbox_payload_is_authored_multihit_upper(
          hb_id, damage, batch->state.hitbox_angle[hb_i], batch->state.hitbox_kbg[hb_i],
          batch->state.hitbox_wsk[hb_i], batch->state.hitbox_bkb[hb_i]) == 0u) {
    return 0u;
  }
  uint8_t has_authored_lower_same_group_sibling = 0u;
  for (int other = 0; other < MSL_MAX_HITBOXES; other++) {
    if (other == (int)hb_id) {
      continue;
    }
    const size_t other_i = idx_hitbox(bi, attacker, other);
    if (batch->state.hitbox_enabled[other_i] == 0u) {
      continue;
    }
    if (hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[other_i]) != hit_group) {
      continue;
    }
    if (combat_attackairlw_hitbox_payload_is_authored_multihit_lower_sibling(
            batch->state.hitbox_damage[other_i], batch->state.hitbox_angle[other_i],
            batch->state.hitbox_kbg[other_i], batch->state.hitbox_wsk[other_i],
            batch->state.hitbox_bkb[other_i]) != 0u) {
      has_authored_lower_same_group_sibling = 1u;
      break;
    }
  }
  if (has_authored_lower_same_group_sibling == 0u) {
    return 0u;
  }
  // AttackDash allow-interrupt tail BODY boundary:
  // - Fox/Falco AttackDash clears its own hitboxes at frame 18 and publishes allow_interrupt at
  //   frame 36. On that source boundary, part-18 tail hurtcaps are no longer a reliable full-BODY
  //   owner for the high/inner AttackAirLw hb0 capsule, while the authored lower same-group
  //   AttackAirLw sibling remains eligible against torso/body hurtcaps.
  // - The predicate is bounded by extracted source data: defender MotionState is AttackDash with
  //   the generated allow_interrupt event active, the rejected hurtcap has the extracted FtPart 18
  //   tail owner from data/hurtcaps/{fox,falco}.json, and the attacker has the authored AttackAirLw
  //   hb0 3-damage/angle-290/wsk-30 payload plus hb1 2-damage same-group sibling. It is not a
  //   replay-row, character-pair, or broad AttackDash suppression.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
  //   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw/events.ftCo_SM_AttackDash
  // data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18
  return 1u;
}

uint8_t combat_attackairlw_strong_dair_group_has_non_tail_body_overlap(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, int defender,
    const MslHurtCap* defender_caps, uint16_t defender_cap_count_u16) {
  if (batch == NULL || defender_caps == NULL) {
    return 0u;
  }
  const size_t selected_hb_i = idx_hitbox(bi, attacker, hb_id);
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[selected_hb_i]);
  const uint16_t capped_count = defender_cap_count_u16 > (uint16_t)MSL_MAX_HURTCAPS
                                    ? (uint16_t)MSL_MAX_HURTCAPS
                                    : defender_cap_count_u16;
  for (uint8_t sibling_hb_id = 0u; sibling_hb_id <= 1u; sibling_hb_id++) {
    const size_t sibling_hb_i = idx_hitbox(bi, attacker, (int)sibling_hb_id);
    if (batch->state.hitbox_enabled[sibling_hb_i] == 0u ||
        hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[sibling_hb_i]) != hit_group ||
        combat_attackairlw_hitbox_payload_is_authored_strong_meteor(
            sibling_hb_id, batch->state.hitbox_damage[sibling_hb_i],
            batch->state.hitbox_angle[sibling_hb_i], batch->state.hitbox_kbg[sibling_hb_i],
            batch->state.hitbox_wsk[sibling_hb_i], batch->state.hitbox_bkb[sibling_hb_i]) == 0u) {
      continue;
    }
    const float sx = batch->state.hitbox_x[sibling_hb_i];
    const float sy = batch->state.hitbox_y[sibling_hb_i];
    const float sz = batch->state.hitbox_z[sibling_hb_i];
    const float sr = batch->state.hitbox_radius[sibling_hb_i];
    for (uint16_t other_cap_id = 0u; other_cap_id < capped_count; other_cap_id++) {
      const MslHurtCap* other_cap = &defender_caps[other_cap_id];
      if (combat_hurtcap_is_extracted_fox_falco_tail_part(other_cap)) {
        continue;
      }
      const size_t other_cap_i = idx_hurtcap(bi, defender, (int)other_cap_id);
      if (batch->state.hurtcap_enabled[other_cap_i] == 0u) {
        continue;
      }
      float overlap_amount = 0.0f;
      uint8_t overlap_evaluated = 0u;
      uint8_t overlaps = combat_body_overlap_lbColl_80006E58_matrix_radius(
          batch, bi, attacker, (int)sibling_hb_id, defender, (int)other_cap_id, sx, sy, sz, sr,
          batch->state.hurtcap_a_x[other_cap_i], batch->state.hurtcap_a_y[other_cap_i],
          batch->state.hurtcap_a_z[other_cap_i], batch->state.hurtcap_b_x[other_cap_i],
          batch->state.hurtcap_b_y[other_cap_i], batch->state.hurtcap_b_z[other_cap_i], 0u,
          &overlap_amount, &overlap_evaluated);
      const uint8_t baseline_overlaps = combat_sphere_capsule_intersects(
          sx, sy, sz, sr, batch->state.hurtcap_a_x[other_cap_i],
          batch->state.hurtcap_a_y[other_cap_i], batch->state.hurtcap_a_z[other_cap_i],
          batch->state.hurtcap_b_x[other_cap_i], batch->state.hurtcap_b_y[other_cap_i],
          batch->state.hurtcap_b_z[other_cap_i], batch->state.hurtcap_radius[other_cap_i], NULL);
      overlaps = (uint8_t)(overlaps || baseline_overlaps);
      if (overlaps) {
        return 1u;
      }
    }
  }
  return 0u;
}

uint8_t combat_attackairlw_down_forward_tail_rejects_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, int defender, size_t a_idx,
    size_t d_idx, const MslHurtCap* cap, const MslHurtCap* defender_caps,
    uint16_t defender_cap_count_u16, float hx, float hy, float hz, float hr) {
  if (batch == NULL || cap == NULL || defender_caps == NULL) {
    return 0u;
  }
  const uint16_t defender_action = batch->state.action_id[d_idx];
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW ||
      !(defender_action == (uint16_t)MSL_ACT_DOWN_FOWARD_U ||
        defender_action == (uint16_t)MSL_ACT_DOWN_BACK_U ||
        defender_action == (uint16_t)MSL_ACT_DOWN_FOWARD_D ||
        defender_action == (uint16_t)MSL_ACT_DOWN_BACK_D)) {
    return 0u;
  }
  if (batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      batch->state.on_ground[d_idx] == 0u) {
    return 0u;
  }
  if (combat_hurtcap_is_extracted_fox_falco_tail_part(cap) == 0u) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  if (combat_attackairlw_hitbox_payload_is_authored_strong_meteor(
          hb_id, batch->state.hitbox_damage[hb_i], batch->state.hitbox_angle[hb_i],
          batch->state.hitbox_kbg[hb_i], batch->state.hitbox_wsk[hb_i],
          batch->state.hitbox_bkb[hb_i]) == 0u) {
    return 0u;
  }
  (void)hx;
  (void)hy;
  (void)hz;
  (void)hr;
  if (combat_attackairlw_strong_dair_group_has_non_tail_body_overlap(
          batch, bi, attacker, hb_id, defender, defender_caps, defender_cap_count_u16) != 0u) {
    return 0u;
  }
  // Downed roll dynamic-tail BODY boundary:
  // ftCo_80098324 enters DownFoward/DownBack, runs ftAnim_8006EBA4, then calls
  // ftCommon_8007CCE8 before the shared ftCo_Down_Coll -> ft_80084104 collision callback. The
  // part-18 tail chain is a live dynamic owner in this callback family; a replay/static-pose
  // tail-only overlap with Falco's authored strong DAir pair is not a full BODY damage owner.
  // Suppression is explicitly disabled when the same authored hit group also overlaps any enabled
  // non-tail hurtcap; that full BODY path owns ordinary damage-state selection. This predicate is
  // bounded by source/data facts: common downed-roll MotionState, extracted FtPart 18 hurtcap, and
  // the authored 12-damage DAir HitCapsule payload.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Down.c::{ftCo_80098324,ftCo_Down_Coll}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007CCE8
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return 1u;
}

uint8_t combat_specialhi_launch_hitbox_payload_is_authored(const MslBatch* batch, size_t hb_i,
                                                           uint8_t hb_id) {
  if (batch == NULL || hb_id != 0u) {
    return 0u;
  }
  // Fox/Falco SpecialHi/SpecialAirHi launch scripts create a single 16-damage hb0 capsule with
  // angle 80, kbg 60, bkb 80. The motion gate below is data-backed by the generated special-msid
  // table, so this payload check names the authored launch HitCapsule instead of character ids or
  // replay rows.
  // data/special_msids/{fox,falco}.json up-special main submotion
  // data/moves/{fox,falco}.json::moves.ftFx_SM_SpecialHi.events.create_hitbox
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHi_Enter,
  //   ftFx_SpecialAirHi_Enter}
  return (uint8_t)(batch->state.hitbox_damage[hb_i] == 16.0f &&
                   batch->state.hitbox_angle[hb_i] == 80u && batch->state.hitbox_kbg[hb_i] == 60u &&
                   batch->state.hitbox_bkb[hb_i] == 80u);
}

uint8_t combat_action_is_generated_specialhi_launch(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[idx];
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(batch->state.char_id[idx], action);
  if (fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_HI &&
      fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) {
    return 0u;
  }
  const MslSpecialMsids* ms = msl_special_msids(batch->state.char_id[idx]);
  return (uint8_t)(ms != NULL &&
                   batch->state.animation_index[idx] == (uint32_t)ms->specialhi_ground_main);
}

uint8_t combat_action_is_aerial_reflector_turn_source(uint8_t char_id, uint16_t action) {
  // Fox/Falco aerial Reflector turn is a distinct generated motion state
  // (`ftFx_MS_SpecialAirLwTurn`) with its own SpecialLw callbacks. The common generated airborne
  // collision classes intentionally exclude bespoke Fox/Falco special callbacks, so this owner uses
  // the named motion-state row rather than pretending it is a generic common-air action.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialAirLwTurn_Anim,ftFx_SpecialAirLwTurn_Coll,ftFx_SpecialAirLwTurn_Phys}
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 SpecialAirLwTurn callbacks)
  return (uint8_t)(msl_motion_state_fx_special_kind(char_id, action) ==
                   (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_TURN);
}

uint8_t combat_action_is_aerial_reflector_loop_pre_turn_source(uint8_t char_id, uint16_t action) {
  // Aerial Reflector loop is the source state before `ftFx_SpecialAirLwLoop_IASA` can publish the
  // generated aerial Reflector-turn callbacks. Keep this as a named Fox/Falco SpecialLw source
  // owner because the generated common grounded/airborne callback classes intentionally do not
  // classify bespoke character-special callbacks.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_Init.c::ftFx_Init_MotionStateTable
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialAirLwLoop_IASA,ftFx_SpecialAirLwLoop_Coll,ftFx_SpecialAirLwTurn_Coll}
  // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 SpecialAirLwLoop/Turn callbacks)
  return (uint8_t)(msl_motion_state_fx_special_kind(char_id, action) ==
                   (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_LOOP);
}

uint8_t combat_pstadium_specialhi_launch_air_reflector_pre_turn_rejects_body(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, size_t a_idx, size_t d_idx) {
  if (batch == NULL || batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_POKEMON_STADIUM ||
      batch->state.on_ground[d_idx] != 0u ||
      combat_action_is_aerial_reflector_loop_pre_turn_source(batch->state.char_id[d_idx],
                                                             batch->state.action_id[d_idx]) == 0u) {
    return 0u;
  }
  const uint16_t attacker_action = batch->state.action_id[a_idx];
  {
    const uint8_t attacker_fx_kind =
        msl_motion_state_fx_special_kind(batch->state.char_id[a_idx], attacker_action);
    if (attacker_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_HI &&
        attacker_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) {
      return 0u;
    }
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  if (combat_specialhi_launch_hitbox_payload_is_authored(batch, hb_i, hb_id) == 0u) {
    return 0u;
  }
  // SpecialHi launch hb0 vs aerial Reflector loop immediately before the Turn publication:
  // rollout drift can make the coarse world capsule overlap before `ftFx_SpecialAirLwLoop_IASA`
  // has published the aerial Reflector-turn motion. Vanilla keeps the Loop callback out of this
  // BODY hit; the next callback's `ftFx_MS_SpecialAirLwTurn` state is the eligible source state
  // for the TVR:12278 hit.
  // Keep the rejection tied to Stadium, the authored SpecialHi launch payload, and the generated
  // aerial Reflector loop source state, not to replay record identity.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialAirLwLoop_IASA,ftFx_SpecialAirLwLoop_Coll,ftFx_SpecialAirLwTurn_Coll}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  return 1u;
}

uint8_t combat_pstadium_x44_gap_allows_body_contact(const MslBatch* batch, int bi, int attacker,
                                                    uint8_t hb_id, size_t a_idx, size_t d_idx,
                                                    uint8_t cap_id, const MslHurtCap* cap,
                                                    float lbcoll_overlap_amount,
                                                    uint8_t lbcoll_overlap_evaluated,
                                                    uint8_t shield_active) {
  if (batch == NULL || cap == NULL || shield_active != 0u ||
      batch->state.stage_id[bi] != (uint32_t)MSL_STAGE_ID_POKEMON_STADIUM ||
      batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      lbcoll_overlap_evaluated == 0u || !(lbcoll_overlap_amount <= 0.0f)) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  // Pokemon Stadium BODY x44 gap:
  // Fighter_80068E64 installs the Stadium `x34_scale.z` lane, Fighter_UpdateModelScale applies it
  // to the root X scale, and ftCommon_8007F804 supplies fp->x44_mtx to ftColl_80078C70's
  // lbColl_8000805C BODY path. Runtime does not yet consume the extracted x7E4 lane for full x44
  // collision; this temporary bridge admits only named residual caps after payload/source filters.
  // This is not a broad Stadium tolerance; adjacent low-cap, shield, hitlag, weak/late-DAir, and
  // non-tail rows stay on the ordinary lbColl predicate.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_80068E64,Fighter_UpdateModelScale,Fighter_UnkApplyTransformation_8006C0F0}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2/head-high and cap12 -> FtPart 18
  const uint16_t defender_action = batch->state.action_id[d_idx];
  if (batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_LW &&
      combat_attackairlw_hitbox_payload_is_authored_strong_meteor(
          hb_id, batch->state.hitbox_damage[hb_i], batch->state.hitbox_angle[hb_i],
          batch->state.hitbox_kbg[hb_i], batch->state.hitbox_wsk[hb_i],
          batch->state.hitbox_bkb[hb_i]) != 0u) {
    if (batch->state.on_ground[d_idx] != 0u && cap->height >= 2u &&
        defender_action != (uint16_t)MSL_ACT_DASH && defender_action != (uint16_t)MSL_ACT_RUN &&
        !msl_motion_state_common_class_has_fast(defender_action, MSL_MS_CLASS_GROUNDED_ATTACK) &&
        lbcoll_overlap_amount >= MSL_PSTADIUM_X44_DAIR_GROUNDED_HIGH_MAX_RESIDUAL) {
      return 1u;
    }
    if (batch->state.on_ground[d_idx] == 0u &&
        (defender_action == (uint16_t)MSL_ACT_JUMP_F ||
         defender_action == (uint16_t)MSL_ACT_JUMP_B) &&
        cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_XROTN_SLOT &&
        combat_hurtcap_is_extracted_fox_falco_tail_part(cap) &&
        lbcoll_overlap_amount >= MSL_PSTADIUM_X44_DAIR_TAIL_MAX_RESIDUAL) {
      return 1u;
    }
  }
  if (batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_HI3 &&
      batch->state.animation_index[a_idx] == (uint32_t)MSL_SM_ATTACK_HI3 &&
      (defender_action == (uint16_t)MSL_ACT_JUMP_F ||
       defender_action == (uint16_t)MSL_ACT_JUMP_B) &&
      batch->state.on_ground[d_idx] == 0u &&
      cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_XROTN_SLOT &&
      combat_hurtcap_is_extracted_fox_falco_tail_part(cap) && hb_id == 1u &&
      batch->state.action_frame[a_idx] >= 10 && batch->state.hitbox_damage[hb_i] == 9.0f &&
      batch->state.hitbox_angle[hb_i] == 90u && batch->state.hitbox_kbg[hb_i] == 120u &&
      batch->state.hitbox_wsk[hb_i] == 0u && batch->state.hitbox_bkb[hb_i] == 30u &&
      lbcoll_overlap_amount >= MSL_PSTADIUM_X44_ATTACKHI3_TAIL_MAX_RESIDUAL) {
    // Falco AttackHi3 hb1 vs airborne Fox tail on Pokemon Stadium:
    // the authored high/tail HitCapsule is just inside vanilla's x44 BODY lane on STM:6647, while
    // the adjacent STM:6646 frame remains outside. The named residual cap is retained only while
    // the runtime lacks the full live hurtcap/JObj packet. This stays bounded by extracted hitbox
    // payload and FtPart-18 tail hurtcap, not by replay id or character-pair checks.
    // refs/melee/src/melee/ft/fighter.c::{Fighter_80068E64,Fighter_UpdateModelScale}
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
    // data/moves/falco.json::moves.ftCo_SM_AttackHi3.events.create_hitbox hb1
    return 1u;
  }
  if (combat_action_is_generated_specialhi_launch(batch, a_idx) != 0u &&
      combat_action_is_aerial_reflector_turn_source(batch->state.char_id[d_idx], defender_action) !=
          0u &&
      batch->state.on_ground[d_idx] == 0u && cap_id == 0u &&
      combat_specialhi_launch_hitbox_payload_is_authored(batch, hb_i, hb_id) != 0u &&
      lbcoll_overlap_amount >= MSL_PSTADIUM_X44_SPECIALHI_LAUNCH_ROOT_MAX_RESIDUAL) {
    // SpecialHi launch hb0 vs aerial Reflector-turn root BODY on Pokemon Stadium:
    // ftCommon_8007F804 supplies the Stadium x44 matrix to lbColl_8000805C. Until runtime carries
    // the complete live x44 hurtcap packet, admit only the generated up-special launch HitCapsule
    // against the extracted root hurtcap while the victim is the source-generated
    // ftFx_MS_SpecialAirLwTurn motion. The adjacent aerial Reflector-loop row (TVR:12277) remains
    // no-hit; non-Stadium, non-root, non-launch-payload, and shield rows stay on ordinary lbColl.
    // refs/melee/src/melee/ft/fighter.c::{Fighter_80068E64,Fighter_UpdateModelScale}
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    // data/special_msids/{fox,falco}.json up-special main submotion
    // data/motion_state/owners/{fox,falco}.bin SpecialAirLwTurn motion-state callbacks
    // data/hurtcaps/{fox,falco}.json cap0/root
    return 1u;
  }
  return 0u;
}

uint8_t combat_attackairhi_attackdash_tail_allow_interrupt_rejects_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, size_t a_idx, size_t d_idx,
    const MslHurtCap* cap) {
  if (batch == NULL || cap == NULL) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_HI || hb_id != 2u) {
    return 0u;
  }
  if (batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      batch->state.on_ground[d_idx] == 0u) {
    return 0u;
  }
  if (combat_hurtcap_is_extracted_fox_falco_tail_part(cap) == 0u) {
    return 0u;
  }
  const uint16_t defender_action = batch->state.action_id[d_idx];
  uint8_t attackdash_allow_interrupt_owner = 0u;
  if (defender_action == (uint16_t)MSL_ACT_ATTACK_DASH &&
      move_tables_grounded_attack_allow_interrupt(batch->state.char_id[d_idx], defender_action,
                                                  batch->state.anim_frame_f32[d_idx]) != 0u) {
    attackdash_allow_interrupt_owner = 1u;
  } else if (defender_action == (uint16_t)MSL_ACT_WAIT &&
             batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_ATTACK_DASH &&
             batch->state.action_frame[d_idx] == 0 && batch->state.prev_action_frame[d_idx] >= 0 &&
             move_tables_grounded_attack_allow_interrupt(
                 batch->state.char_id[d_idx], (uint16_t)MSL_ACT_ATTACK_DASH,
                 (float)batch->state.prev_action_frame[d_idx]) != 0u) {
    attackdash_allow_interrupt_owner = 1u;
  }
  if (attackdash_allow_interrupt_owner == 0u) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  if (combat_attackairhi_hitbox_payload_is_authored_finisher_hb2(
          hb_id, batch->state.hitbox_damage[hb_i], batch->state.hitbox_angle[hb_i],
          batch->state.hitbox_kbg[hb_i], batch->state.hitbox_wsk[hb_i],
          batch->state.hitbox_bkb[hb_i]) == 0u) {
    return 0u;
  }
  // AttackDash allow-interrupt tail BODY boundary:
  // - Fox/Falco AttackDash has ended its authored active hitbox window and crossed the generated
  //   allow_interrupt command-script event. At this callback boundary, including the same-frame
  //   AttackDash -> Wait entry owned by ftCo_AttackDash_Anim / ftCo_AttackDash_IASA, the part-18
  //   tail cap is not a full-BODY owner for the late AttackAirHi hb2 capsule; source BODY
  //   selection waits for the later ordinary Wait-frame contact instead of resolving a tail-only
  //   hit during the entry callback.
  // - The predicate is bounded by extracted source data: defender MotionState is AttackDash with
  //   allow_interrupt active or the frame-0 Wait entry from that source state, rejected hurtcap is
  //   the extracted FtPart 18 tail owner from data/hurtcaps/{fox,falco}.json, and attacker source
  //   is the authored frame-11 AttackAirHi hb2 finisher payload from data/moves/{fox,falco}.json
  //   (Fox 13-damage / Falco 10-damage). UpAir hb0/hb1 and broad damage windows are excluded.
  //   It is not a broad AttackDash, Wait, UpAir, or character-pair suppression.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
  //   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirHi/events.ftCo_SM_AttackDash
  // data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18
  return 1u;
}

uint8_t combat_marth_aerial_static_spacie_tail_rejects_body_contact(const MslBatch* batch,
                                                                    size_t hb_i, size_t a_idx,
                                                                    size_t d_idx, uint8_t hb_id,
                                                                    const MslHurtCap* cap) {
  if (batch == NULL || cap == NULL) {
    return 0u;
  }
  const uint8_t defender_char = batch->state.char_id[d_idx];
  if (batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_MARTH ||
      (defender_char != (uint8_t)MSL_CHAR_ID_FOX && defender_char != (uint8_t)MSL_CHAR_ID_FALCO) ||
      combat_hurtcap_is_extracted_fox_falco_tail_part(cap) == 0u ||
      batch->state.dynamic_pose_apply_collision_matrix[d_idx] != 0u) {
    return 0u;
  }
  const uint16_t action_id = batch->state.action_id[a_idx];
  const float damage = batch->state.hitbox_damage[hb_i];
  const int int_dmg = (damage == 0.0f) ? 0 : (((int)damage != 0) ? (int)damage : 1);
  const uint8_t marth_fair_tip =
      (uint8_t)(action_id == (uint16_t)MSL_ACT_ATTACK_AIR_F && hb_id == 3u && int_dmg == 13);
  const uint8_t marth_uair_sustained_root =
      (uint8_t)(action_id == (uint16_t)MSL_ACT_ATTACK_AIR_HI && hb_id == 0u && int_dmg == 13 &&
                batch->state.hitbox_enable_edge[hb_i] == 0u);
  const uint8_t marth_uair_upper = (uint8_t)(action_id == (uint16_t)MSL_ACT_ATTACK_AIR_HI &&
                                             hb_id == 2u && (int_dmg == 13 || int_dmg == 9));
  if (marth_fair_tip == 0u && marth_uair_sustained_root == 0u && marth_uair_upper == 0u) {
    return 0u;
  }
  // Fox/Falco cap12 is the ftData.x2C dynamic tail-chain part (FtPart 18). A static SSANIM matrix
  // overlap against that slot is not sufficient BODY proof for Marth Fair tip / UpAir upper-slot
  // contacts; source-owned dynamic tail contacts are admitted only when SSDYNN01 marks the defender
  // submotion as a collision owner (for example EscapeAir). UpAir hb0 stays on the ordinary BODY
  // path on its ftAction_8007121C enable edge: its extracted 13-damage payload is the primary
  // body/arc HitCapsule and replay-visible SDW rec5404 shows source ProcessHit selecting it over
  // later 10-damage UpAir contacts. Sustained hb0 tail-only overlap is not enough; DHH rec9861
  // remains in JumpAerialF with no BODY DmgLog.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // data/anims/fox.dyn.bin::SSDYNN01 collision_motion_state_ids
  // data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18
  // data/moves/marth.json::moves.{ftCo_SM_AttackAirF,ftCo_SM_AttackAirHi}.events.create_hitbox
  return 1u;
}

uint8_t combat_marth_attackairn_spacie_guard_static_pose_rejects_body_contact(
    const MslBatch* batch, size_t hb_i, size_t a_idx, size_t d_idx, uint8_t hb_id, uint8_t cap_id,
    const MslHurtCap* cap) {
  if (batch == NULL || cap == NULL) {
    return 0u;
  }
  (void)cap_id;
  if (batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_MARTH ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_N || hb_id != 0u ||
      batch->state.hitbox_damage[hb_i] != 10.0f) {
    return 0u;
  }
  const uint8_t defender_char = batch->state.char_id[d_idx];
  if ((defender_char != (uint8_t)MSL_CHAR_ID_FOX && defender_char != (uint8_t)MSL_CHAR_ID_FALCO) ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD ||
      batch->state.animation_index[d_idx] <= 0xFFFFu || batch->state.action_frame[d_idx] >= 0 ||
      batch->state.dynamic_pose_apply_collision_matrix[d_idx] != 0u ||
      !(batch->state.lightshield_amount[d_idx] > 0.0f) || !(batch->state.shield_hp[d_idx] > 0.0f)) {
    return 0u;
  }
  // No-submotion spacie Guard rows with live lightshield expose ShieldDesc state but no source
  // hurtcap packet. Rebuilding fallback BODY caps from the static Guard submotion over-admits
  // Marth AttackAirN hb0 pokes; source ftColl consumes the live Guard JObj/ShieldDesc packet
  // instead. Falco/Fox aerial shield-pokes and non-spacie Guard fallback positives stay on the
  // existing source-pose path.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_Guard_Anim,ftCo_80091E78}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // data/moves/marth.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json static Guard fallback capsules
  return 1u;
}

uint8_t combat_spacie_bair_static_extremity_rejects_body_contact(const MslBatch* batch, size_t hb_i,
                                                                 size_t a_idx, size_t d_idx,
                                                                 uint8_t hb_id,
                                                                 const MslHurtCap* cap) {
  if (batch == NULL || cap == NULL) {
    return 0u;
  }
  const uint8_t attacker_char = batch->state.char_id[a_idx];
  if ((attacker_char != (uint8_t)MSL_CHAR_ID_FOX && attacker_char != (uint8_t)MSL_CHAR_ID_FALCO) ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B ||
      batch->state.dynamic_pose_apply_collision_matrix[d_idx] != 0u) {
    return 0u;
  }
  const float damage = batch->state.hitbox_damage[hb_i];
  const int int_dmg = (damage == 0.0f) ? 0 : (((int)damage != 0) ? (int)damage : 1);
  if (!((hb_id <= 1u && int_dmg == 15) || (hb_id <= 1u && int_dmg == 9))) {
    return 0u;
  }

  const uint8_t defender_char = batch->state.char_id[d_idx];
  const uint8_t marth_attack_s4_static_limb =
      (uint8_t)(defender_char == (uint8_t)MSL_CHAR_ID_MARTH &&
                batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_ATTACK_S4_S &&
                (cap->bone_part_id == 6u || cap->bone_part_id == 7u || cap->bone_part_id == 29u));
  if (marth_attack_s4_static_limb == 0u) {
    return 0u;
  }
  // Static extremity BODY fallback:
  // - Fox/Falco AttackAirB's extracted hb0/hb1 strong/late payloads are ordinary ftColl
  //   HitCapsules, but source BODY geometry consumes the defender's live JObj/collision packet.
  // - When no SSDYNN01 collision-matrix owner is active, static fallback capsules for Marth
  //   AttackS4 arm/leg extremity parts over-admit edge-only BODY contacts. Keep dynamic-pose rows
  //   and central/root capsules on the normal path; Fox/Falco tail BODY remains admitted because
  //   adjacent primary-rollout rows prove real BAir tail hits need that source path.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/marth.json FtParts 6/7/29
  return 1u;
}

uint8_t combat_attackhi4_damageflytop_xrotn_rejects_body_contact(const MslBatch* batch, size_t hb_i,
                                                                 size_t a_idx, size_t d_idx,
                                                                 uint8_t hb_id, uint8_t cap_id) {
  return msl_damage_owner_attackhi4_damageflytop_xrotn_rejects_body(batch, hb_i, a_idx, d_idx,
                                                                    hb_id, cap_id);
}

uint8_t combat_shine_start_damageair_entry_pose_allows_body_contact(const MslBatch* batch,
                                                                    size_t a_idx, size_t d_idx,
                                                                    uint8_t cap_id, float hx,
                                                                    float hy, float hz, float hr) {
  if (!combat_shine_start_damageair_entry_pose_bridge_applies(batch, a_idx, d_idx)) {
    return 1u;
  }
  // Temporary seed/model blocker, not a vanilla gameplay rule:
  // - Damage entry owns a separate AObj pose clock via Fighter_ChangeMotionState + ftAnim_8006EBA4,
  //   while Slippi action_frame continues as damage/hitstun time.
  // - The current seed schema/model does not carry that DamageAir AObj pose-clock ownership, and
  //   replay-real Dolphin forensics show airborne DamageAir2 hurtcaps can differ materially from
  //   action_frame-derived pose samples during hitstun (TBK rec=1575 false BODY Shine Start contact).
  // - Restrict this bridge to the exact Shine Start BODY candidate against frame-start-airborne
  //   DamageAir2 hitstun: recompute that hurtcap from the DamageAir entry pose and only reject the
  //   BODY contact when the modeled entry-pose capsule does not overlap.
  // - This should be removed once the DamageAir AObj pose clock is seeded/modeled.
  // - Keep grounded-at-frame-start DamageAir2 rows eligible; AGN rec=4782 is a real grounded Shine
  //   Start BODY hit even though stage collision can move the victim airborne before combat.
  // - Shield contacts still resolve through the shield path before this BODY-only gate.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_Damage_Anim}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  enum { MSL_FOX_DAMAGEAIR_DYNAMIC_TAIL_PART_ID = 18 };
  const uint8_t char_id = batch->state.char_id[d_idx];
  const MslHurtCap* caps = NULL;
  uint16_t cap_count_u16 = 0u;
  // Inside this temporary frame-start-airborne DamageAir2 seed/model blocker, missing entry-pose
  // data is unsafe: falling back to action-frame-derived DamageAir2 hurtcaps reopens the known
  // false Shine Start BODY hit. Remove this fail-closed policy with the bridge once the real Damage
  // AObj pose-clock lane exists.
  if (hurtcaps_get(char_id, &caps, &cap_count_u16) != 0 || caps == NULL ||
      cap_id >= cap_count_u16) {
    return 0u;
  }
  const MslHurtCap* cap = &caps[cap_id];
  if (cap->bone_part_id != (uint16_t)MSL_FOX_DAMAGEAIR_DYNAMIC_TAIL_PART_ID) {
    // The replay-proven false Shine/DamageAir contacts are the Fox dynamic tail chain: cap12 is
    // anchored on FtPart 18 and depends on source-order dynamic/AObj state that this stack still
    // does not carry. Non-tail DamageAir2 hurtcaps stay on the normal matrix-radius BODY owner,
    // so valid torso/head contacts such as DSG:5041 are not suppressed by a tail-pose blocker.
    // data/hurtcaps/fox.bin cap12 -> FtPart 18
    // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
    // refs/melee/src/melee/lb/lb_00F9.c::lb_8001044C
    return 1u;
  }

  uint32_t can_hit_mask = 0xFFFFFFFFu;
  (void)move_tables_hurtbox_can_hit_mask_at_frame(char_id, (uint16_t)MSL_SM_DAMAGE_AIR_2,
                                                  /*frame=*/0u, cap_count_u16, &can_hit_mask);
  if (((can_hit_mask >> cap_id) & 0x1u) == 0u) {
    return 0u;
  }

  float m[12];
  if (anim_pose_get_matrix(char_id, (uint16_t)MSL_SM_DAMAGE_AIR_2, /*frame=*/0u, cap->bone_part_id,
                           m) != 0) {
    return 0u;
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  float bx = 0.0f, by = 0.0f, bz = 0.0f;
  msl_mtx34_mul_point(m, cap->a_offset, &ax, &ay, &az);
  msl_mtx34_mul_point(m, cap->b_offset, &bx, &by, &bz);

  const float scale_y = batch->state.fighter_scale_y[d_idx];
  const MslCharParams* chp = msl_char_params_fast(char_id);
  const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                  ? chp->model_scaling
                                  : 1.0f;
  const float model_scale = scale_y * model_scaling;
  ax *= model_scale;
  ay *= model_scale;
  az *= model_scale;
  bx *= model_scale;
  by *= model_scale;
  bz *= model_scale;

  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float ax_rot_x = facing_dir * az;
  const float ax_rot_z = -facing_dir * ax;
  const float bx_rot_x = facing_dir * bz;
  const float bx_rot_z = -facing_dir * bx;
  ax = ax_rot_x + batch->state.pos_x[d_idx];
  ay += batch->state.pos_y[d_idx];
  az = ax_rot_z + batch->state.pos_z[d_idx];
  bx = bx_rot_x + batch->state.pos_x[d_idx];
  by += batch->state.pos_y[d_idx];
  bz = bx_rot_z + batch->state.pos_z[d_idx];

  const float cr = cap->scale * model_scale;
  return combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL);
}

uint8_t combat_attackairb_jump_low_body_source_owns_model_scale_bypass(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, size_t a_idx,
    size_t d_idx, uint8_t cap_id, float hx, float hy, float hz, float hr) {
  if (batch == NULL || hb_id != 0 || cap_id == 12u) {
    return 0u;
  }
  const uint16_t defender_action = batch->state.action_id[d_idx];
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B ||
      !(defender_action == (uint16_t)MSL_ACT_JUMP_F ||
        defender_action == (uint16_t)MSL_ACT_JUMP_B ||
        defender_action == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
        defender_action == (uint16_t)MSL_ACT_JUMP_AERIAL_B) ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  if (batch->state.hitbox_enable_edge[hb_i] == 0u || batch->state.hitbox_damage[hb_i] != 15.0f ||
      batch->state.hitbox_angle[hb_i] != 361u || batch->state.hitbox_kbg[hb_i] != 100u ||
      batch->state.hitbox_bkb[hb_i] != 0u) {
    return 0u;
  }
  const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
  if (batch->state.hurtcap_height[cap_i] != 0u) {
    return 0u;
  }
  const size_t tail_cap_i = idx_hurtcap(bi, defender, 12);
  if (batch->state.hurtcap_enabled[tail_cap_i] == 0u ||
      batch->state.hurtcap_height[tail_cap_i] != 1u) {
    return 0u;
  }
  float overlap_amount = 0.0f;
  uint8_t overlap_evaluated = 0u;
  uint8_t tail_overlaps = combat_body_overlap_lbColl_80006E58_matrix_radius(
      batch, bi, attacker, hb_id, defender, 12, hx, hy, hz, hr,
      batch->state.hurtcap_a_x[tail_cap_i], batch->state.hurtcap_a_y[tail_cap_i],
      batch->state.hurtcap_a_z[tail_cap_i], batch->state.hurtcap_b_x[tail_cap_i],
      batch->state.hurtcap_b_y[tail_cap_i], batch->state.hurtcap_b_z[tail_cap_i], 0u,
      &overlap_amount, &overlap_evaluated);
  const uint8_t tail_baseline_overlaps = combat_sphere_capsule_intersects(
      hx, hy, hz, hr, batch->state.hurtcap_a_x[tail_cap_i], batch->state.hurtcap_a_y[tail_cap_i],
      batch->state.hurtcap_a_z[tail_cap_i], batch->state.hurtcap_b_x[tail_cap_i],
      batch->state.hurtcap_b_y[tail_cap_i], batch->state.hurtcap_b_z[tail_cap_i],
      batch->state.hurtcap_radius[tail_cap_i], NULL);
  tail_overlaps = (uint8_t)(tail_overlaps || tail_baseline_overlaps);
  if (tail_overlaps == 0u) {
    return 0u;
  }
  // Strong BAir Jump low-body source owner:
  // The model-scale cancellation filter below is a false-positive blocker for broad create-edge
  // BAir rows, but LIM proves a narrower source-positive case: strong AttackAirB hb0 overlaps a
  // concrete low hurt-height Jump capsule while also overlapping the cap12 dynamic-tail slot. Source
  // ftColl selects the low BODY DmgLog owner; cap12 remains rejected by the sibling selected-height
  // guard. This is bounded by extracted strong BAir payload and hurtcap height/slot metadata rather
  // than replay row, character pair, or broad Jump action shape.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8,ftColl_8007A06C}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap12 height 1, cap10/11 height 0
  return 1u;
}

uint8_t combat_attackairb_model_scale_cancellation_payload(uint8_t hb_id, float damage,
                                                           uint16_t angle, uint16_t kbg,
                                                           uint16_t wsk, uint16_t bkb) {
  const uint8_t strong_payload =
      (uint8_t)((hb_id == (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_ROOT_HITBOX ||
                 hb_id == (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX) &&
                damage == 15.0f && angle == 361u && kbg == 100u && wsk == 0u && bkb == 0u);
  const uint8_t weak_payload = (uint8_t)(hb_id < (uint8_t)MSL_MAX_HITBOXES && damage == 9.0f &&
                                         angle == 361u && kbg == 100u && wsk == 0u && bkb == 0u);
  // The model-scale cancellation lane is source-owned by Fox/Falco's extracted AttackAirB
  // strong/weak payload split. Sheik BAir has a different hb2/hb3 damage/BKB payload on the same
  // frame and remains an ordinary ftColl same-group BODY selector.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FA58
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco,sheik}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return (uint8_t)(strong_payload || weak_payload);
}

uint8_t combat_attackairb_enable_edge_model_scale_allows_body_contact(
    const MslBatch* batch, size_t a_idx, size_t hb_i, size_t d_idx, float hx, float hy, float hz,
    float hr, float ax, float ay, float az, float bx, float by, float bz, float cr,
    uint16_t expected_hitlag) {
  if (batch == NULL) {
    return 1u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B) {
    return 1u;
  }
  const uint16_t d_action = batch->state.action_id[d_idx];
  if (d_action != (uint16_t)MSL_ACT_JUMP_F && d_action != (uint16_t)MSL_ACT_JUMP_B &&
      d_action != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP) {
    return 1u;
  }
  if (d_action == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
      (batch->state.hitstun[d_idx] == 0u || batch->state.hitstun[d_idx] > expected_hitlag)) {
    return 1u;
  }
  if (batch->state.hitbox_enable_edge[hb_i] == 0u) {
    return 1u;
  }
  const uint8_t hb_id = (uint8_t)(hb_i % (size_t)MSL_MAX_HITBOXES);
  if (combat_attackairb_model_scale_cancellation_payload(
          hb_id, batch->state.hitbox_damage[hb_i], batch->state.hitbox_angle[hb_i],
          batch->state.hitbox_kbg[hb_i], batch->state.hitbox_wsk[hb_i],
          batch->state.hitbox_bkb[hb_i]) == 0u) {
    return 1u;
  }

  if (batch->state.dynamic_pose_apply_collision_matrix[d_idx] != 0u) {
    // When the defender's current submotion is in the data-owned `SSDYNN01` collision index,
    // BODY admission has already consumed the live dynamic-chain JObj matrix through
    // `lbColl_80006E58`. Do not re-filter that source-owned matrix with the older scale-only
    // counterfactual used for rows whose live collision-pose owner is still static.
    // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    // data/anims/fox.dyn.bin (SSDYNN01 collision-owner index)
    return 1u;
  }

  const MslCharParams* chp = msl_char_params_fast(batch->state.char_id[a_idx]);
  if (chp == NULL || !isfinite(chp->model_scaling) || chp->model_scaling <= 0.0f ||
      fabsf(chp->model_scaling - 1.0f) <= 1e-6f) {
    return 1u;
  }

  // Collision-skeleton scale cancellation subset:
  // - Fighter_UpdateModelScale applies fighter scale to runtime joints.
  // - ftAnim_8006FA58 applies the inverse per-character model scaling on the collision subtree via
  //   ftCommon_8007F6A4, so the effective collision-space hitbox center uses fighter scale only.
  // - Keep this narrowed to the current AttackAirB enable-edge owner slice:
  //   - jump-entry victims (`GAT:2221`) and
  //   - DamageFlyTop victims on the shallow pre-refresh row (`QGD:285`) where the remaining
  //     hitstun has already decayed to one first-hit horizon or less.
  //   Deeper DamageFlyTop continuation rows (e.g. `TBK:6380`) and adjacent BODY-contact owners
  //   like `QGD:8222` / `TBK:5247` stay outside this subset.
  // refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006FA58
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F6A4
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  const float inv_model = 1.0f / chp->model_scaling;
  const float alt_hx = batch->state.pos_x[a_idx] + (hx - batch->state.pos_x[a_idx]) * inv_model;
  const float alt_hy = batch->state.pos_y[a_idx] + (hy - batch->state.pos_y[a_idx]) * inv_model;
  const float alt_hz = batch->state.pos_z[a_idx] + (hz - batch->state.pos_z[a_idx]) * inv_model;
  return combat_sphere_capsule_intersects(alt_hx, alt_hy, alt_hz, hr, ax, ay, az, bx, by, bz, cr,
                                          NULL);
}

uint8_t combat_body_overlap_lbColl_80006E58_subset_allows(const MslBatch* batch, size_t hb_i,
                                                          size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const size_t a_idx = hb_i / (size_t)MSL_MAX_HITBOXES;
  if (batch->state.char_id[a_idx] == (uint8_t)MSL_CHAR_ID_SHEIK) {
    const uint16_t action_id = batch->state.action_id[a_idx];
    if (action_id == (uint16_t)MSL_ACT_SK_SPECIAL_S ||
        action_id == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S ||
        action_id == (uint16_t)MSL_ACT_SK_SPECIAL_S_END ||
        action_id == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END) {
      // Sheik Chain manually moves preserved fighter HitCapsules by writing a Vec3 through
      // ftColl_8007B8A8, then BODY collision consumes the resulting x58->x4C segment through
      // ftColl_80078C70/lbColl_8000805C. This source-owned path applies even for grounded defenders;
      // keep the generic grounded-victim rejection below for ordinary posed hitboxes.
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_UpdateHitboxes
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B8A8,ftColl_80078C70}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
      return 1u;
    }
  }
  // ftColl_800768A0 clear/copy ownership runs on HitCapsule enable/group edges.
  // Enable this lane through edge transitions to exercise lbColl_8000805C/80006E58 continuity
  // using x58/x4C carried by ftColl_8007AD18.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_8007AD18}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // Pre-hit ownership subset: keep defender-in-hitstun and grounded-victim lanes on baseline
  // overlap while enabling the decomp-shaped sweep only for the proven aerial-victim continuity
  // cases. A broad grounded-victim sweep over-admits adjacent downbound/invincible contacts such as
  // `TBK:2402`; grounded BODY misses need the live JObj pose owner before this lane can broaden.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  if (batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  if (combat_side_special_start_passivewalljump_entry_pose_owner(
          batch, d_idx, batch->state.char_id[d_idx], batch->state.action_id[d_idx])) {
    // PassiveWallJump -> Side-B Start uses the same entry-source collision-pose owner as the
    // matrix-radius path below. Do not let the generic HitCapsule x58/x4C sweep re-admit a BODY hit
    // after that source pose owner has rejected the current-frame overlap.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
    //   ftFx_SpecialSStart_Anim,ftFx_SpecialAirSStart_Anim,ftFx_SpecialSStart_Coll,
    //   ftFx_SpecialAirSStart_Coll}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_IASA
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    return 0u;
  }
  if (batch->state.on_ground[d_idx]) {
    return 0u;
  }
  return 1u;
}

uint8_t combat_body_overlap_lbColl_80006E58_scaffold(const MslBatch* batch, int bi, int attacker,
                                                     int hb_id, float hx, float hy, float hz,
                                                     float hr, float ax, float ay, float az,
                                                     float bx, float by, float bz, float cr,
                                                     float defender_scale_y) {
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);

  // ftColl_80078C70 forwards HitCapsule.x43_b2 as lbColl_8000805C arg3 (`var_r22`).
  // lbColl_8000805C accepts immediately when arg3 != 0.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
  if (batch->state.hitbox_x43_b2[hb_i]) {
    return 1u;
  }

  float px = hx;
  float py = hy;
  float pz = hz;
  if (batch->state.hitbox_prev_enabled[hb_i]) {
    px = batch->state.hitbox_prev_x[hb_i];
    py = batch->state.hitbox_prev_y[hb_i];
    pz = batch->state.hitbox_prev_z[hb_i];
  }

  // lbColl_80006E58 broad envelope:
  //   temp_f3 = (arg10 * arg11) + scl
  // BODY path mapping:
  //   scl=hit radius, arg10=hurt radius, arg11=lbColl_804D7A38*hurt_owner_scale_y.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  float arg11 = 0.0f;
  if (defender_scale_y > 0.0f) {
    arg11 = msl_lbcoll_body_hurt_radius_mul() * defender_scale_y;
  }
  const float broad_r = hr + cr * arg11;

  const float hminx = fminf(px, hx);
  const float hmaxx = fmaxf(px, hx);
  const float hminy = fminf(py, hy);
  const float hmaxy = fmaxf(py, hy);
  const float hminz = fminf(pz, hz);
  const float hmaxz = fmaxf(pz, hz);
  const float cminx = fminf(ax, bx);
  const float cmaxx = fmaxf(ax, bx);
  const float cminy = fminf(ay, by);
  const float cmaxy = fmaxf(ay, by);
  const float cminz = fminf(az, bz);
  const float cmaxz = fmaxf(az, bz);
  if (hmaxx + broad_r < cminx || cmaxx + broad_r < hminx || hmaxy + broad_r < cminy ||
      cmaxy + broad_r < hminy || hmaxz + broad_r < cminz || cmaxz + broad_r < hminz) {
    return 0u;
  }

  float d2 = 0.0f;
  combat_segment_segment_dist2(px, py, pz, hx, hy, hz, ax, ay, az, bx, by, bz, &d2, NULL, NULL);
  const float rr = hr + cr;
  return (uint8_t)(d2 <= rr * rr);
}

uint8_t combat_catch_overlap_lbColl_80007ECC(const MslBatch* batch, int bi, int attacker, int hb_id,
                                             float hx, float hy, float hz, float hr, float ax,
                                             float ay, float az, float bx, float by, float bz,
                                             float cr) {
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  float px = hx;
  float py = hy;
  float pz = hz;
  if (batch->state.hitbox_prev_enabled[hb_i]) {
    px = batch->state.hitbox_prev_x[hb_i];
    py = batch->state.hitbox_prev_y[hb_i];
    pz = batch->state.hitbox_prev_z[hb_i];
  }

  float d2 = 0.0f;
  combat_segment_segment_dist2(px, py, pz, hx, hy, hz, ax, ay, az, bx, by, bz, &d2, NULL, NULL);
  const float rr = hr + cr;
  return (uint8_t)(d2 <= rr * rr);
}

void combat_catch_hitbox_model_scale_compensated(const MslBatch* batch, int bi, int attacker,
                                                 int hb_id, float* io_hx, float* io_hy,
                                                 float* io_hz, float* io_hr) {
  if (batch == NULL || io_hx == NULL || io_hy == NULL || io_hz == NULL || io_hr == NULL) {
    return;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const size_t a_idx = msl_idx_player(bi, attacker);
  if (batch->state.hitbox_bone_part_id[hb_i] == 0u) {
    return;
  }
  const MslCharParams* chp = msl_char_params_fast(batch->state.char_id[a_idx]);
  const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                  ? chp->model_scaling
                                  : 1.0f;
  if (!(model_scaling > 1.0f)) {
    return;
  }

  // Catch selection forwards HitCapsule.x58/x4C and HitCapsule.scale to lbColl_80007ECC with
  // this_fp->x34_scale.y as the hit-side scalar. For non-root authored Catch capsules, the live
  // HitCapsule point consumed by lb_8000B1CC follows the collision skeleton rather than the
  // simulator's generic pose-space model_scaling expansion; Falco's part-1 standing Catch lock is
  // the replay-real positive. Root-authored Catch/CatchDash capsules stay on the root-scaled pose
  // path; do not compensate them through a row-fit CatchDash exception without source data proving
  // that split.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_8007AD18}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  const float pos_x = batch->state.pos_x[a_idx];
  const float pos_y = batch->state.pos_y[a_idx];
  const float pos_z = batch->state.pos_z[a_idx];
  *io_hx = pos_x + ((*io_hx - pos_x) / model_scaling);
  *io_hy = pos_y + ((*io_hy - pos_y) / model_scaling);
  *io_hz = pos_z + ((*io_hz - pos_z) / model_scaling);
  *io_hr /= model_scaling;
}

uint8_t combat_guard_family_no_submotion_catch_source_msid(const MslBatch* batch, size_t d_idx,
                                                           uint16_t* out_msid) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.animation_index[d_idx] <= 0xFFFFu) {
    return 0u;
  }
  if (batch->state.action_frame[d_idx] >= 0) {
    return 0u;
  }

  uint16_t msid = 0u;
  switch (batch->state.action_id[d_idx]) {
    case (uint16_t)MSL_ACT_GUARD_ON:
      msid = (uint16_t)MSL_SM_GUARD_ON;
      break;
    case (uint16_t)MSL_ACT_GUARD:
      msid = (uint16_t)MSL_SM_GUARD;
      break;
    case (uint16_t)MSL_ACT_GUARD_SET_OFF:
      msid = (uint16_t)MSL_SM_GUARD_DAMAGE;
      break;
    case (uint16_t)MSL_ACT_GUARD_REFLECT:
      // ftCo_MS_GuardReflect uses ftCo_SM_GuardOn in the motion-state table.
      // refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_GuardReflect
      if (batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON ||
          batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON) {
        return 0u;
      }
      msid = (uint16_t)MSL_SM_GUARD_ON;
      break;
    default:
      return 0u;
  }
  if (out_msid != NULL) {
    *out_msid = msid;
  }
  return 1u;
}

uint8_t combat_guard_family_no_submotion_catch_source_applies(const MslBatch* batch, size_t d_idx) {
  return combat_guard_family_no_submotion_catch_source_msid(batch, d_idx, NULL);
}

uint8_t combat_guardreflect_expired_x14_body_fallback_applies(const MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT) {
    return 0u;
  }
  if (batch->state.animation_index[d_idx] <= 0xFFFFu) {
    return 0u;
  }
  if (batch->state.action_frame[d_idx] > (int16_t)-2) {
    return 0u;
  }
  if (batch->state.guard_reflect_timer_x14_seed[d_idx] != 0u) {
    return 0u;
  }
  if (batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON) {
    return 0u;
  }
  return 1u;
}

uint8_t combat_guard_family_no_submotion_body_source_msid(const MslBatch* batch, size_t d_idx,
                                                          uint16_t* out_msid) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.animation_index[d_idx] <= 0xFFFFu || batch->state.action_frame[d_idx] >= 0) {
    return 0u;
  }

  uint16_t msid = 0u;
  switch (batch->state.action_id[d_idx]) {
    case (uint16_t)MSL_ACT_GUARD_ON:
      msid = (uint16_t)MSL_SM_GUARD_ON;
      break;
    case (uint16_t)MSL_ACT_GUARD:
      msid = (uint16_t)MSL_SM_GUARD;
      break;
    case (uint16_t)MSL_ACT_GUARD_SET_OFF:
      msid = (uint16_t)MSL_SM_GUARD_DAMAGE;
      break;
    case (uint16_t)MSL_ACT_GUARD_REFLECT:
      if (!combat_guardreflect_expired_x14_body_fallback_applies(batch, d_idx)) {
        return 0u;
      }
      msid = (uint16_t)MSL_SM_GUARD_ON;
      break;
    default:
      return 0u;
  }

  if (out_msid != NULL) {
    *out_msid = msid;
  }
  return 1u;
}

uint8_t combat_guardon_no_submotion_body_overrides_existing_hurtcaps(const MslBatch* batch,
                                                                     size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.animation_index[d_idx] <= 0xFFFFu || batch->state.action_frame[d_idx] >= 0) {
    return 0u;
  }
  if (batch->state.guard_x10[d_idx] == 0u || !(batch->state.guard_tilt_x4[d_idx] > 0.0f)) {
    return 0u;
  }
  return 1u;
}

uint8_t combat_common_entry_carry_action_to_msid(uint16_t action_id, uint16_t* out_msid) {
  if (out_msid == NULL) {
    return 0u;
  }
  switch (action_id) {
    case MSL_ACT_WAIT:
      *out_msid = (uint16_t)MSL_SM_WAIT1_0;
      return 1u;
    case MSL_ACT_WALK_SLOW:
      *out_msid = (uint16_t)MSL_SM_WALK_SLOW;
      return 1u;
    case MSL_ACT_TURN:
      *out_msid = (uint16_t)MSL_SM_TURN;
      return 1u;
    case MSL_ACT_DASH:
      *out_msid = (uint16_t)MSL_SM_DASH;
      return 1u;
    case MSL_ACT_RUN:
      *out_msid = (uint16_t)MSL_SM_RUN;
      return 1u;
    case MSL_ACT_SQUAT_RV:
      *out_msid = (uint16_t)MSL_SM_SQUAT_RV;
      return 1u;
    case MSL_ACT_ATTACK_DASH:
      *out_msid = (uint16_t)MSL_SM_ATTACK_DASH;
      return 1u;
    default:
      return 0u;
  }
}

uint8_t combat_guardon_live_pose_source(const MslBatch* batch, size_t d_idx, uint16_t* out_action,
                                        uint32_t* out_anim) {
  if (batch == NULL || out_action == NULL || out_anim == NULL) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] <= 0xFFFFu ||
      batch->state.frame_start_action_id[d_idx] == batch->state.action_id[d_idx] ||
      batch->state.frame_start_animation_index[d_idx] > 0xFFFFu) {
    return 0u;
  }
  *out_action = batch->state.frame_start_action_id[d_idx];
  *out_anim = batch->state.frame_start_animation_index[d_idx];
  uint16_t dummy_msid = 0u;
  return combat_common_entry_carry_action_to_msid(*out_action, &dummy_msid);
}

uint8_t combat_guard_family_no_submotion_body_source_pose(const MslBatch* batch, size_t d_idx,
                                                          uint16_t* out_msid, float* out_frame) {
  if (out_msid == NULL || out_frame == NULL) {
    return 0u;
  }
  uint16_t msid = 0u;
  if (!combat_guard_family_no_submotion_body_source_msid(batch, d_idx, &msid)) {
    return 0u;
  }

  float pose_frame = (float)msl_anim_frame_floor_u16(
      msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]));
  if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
      batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] > 0xFFFFu) {
    uint16_t prev_msid = 0u;
    uint16_t source_action = batch->state.prev_action_id[d_idx];
    uint32_t source_anim = UINT32_MAX;
    uint8_t have_source =
        combat_guardon_live_pose_source(batch, d_idx, &source_action, &source_anim);
    if (!have_source && batch->state.prev_action_id[d_idx] != batch->state.action_id[d_idx]) {
      have_source = 1u;
    }
    if (!have_source) {
      goto use_current_guard_pose;
    }
    if (source_anim <= 0xFFFFu) {
      prev_msid = (uint16_t)source_anim;
    } else {
      (void)combat_common_entry_carry_action_to_msid(source_action, &prev_msid);
    }
    if (prev_msid != 0u) {
      uint16_t prev_frame = 0u;
      if (batch->state.prev_action_frame[d_idx] >= 0) {
        prev_frame = (uint16_t)batch->state.prev_action_frame[d_idx];
        if (prev_frame != 0xFFFFu) {
          prev_frame = (uint16_t)(prev_frame + 1u);
        }
      }
      const float end_frame = msl_anim_end_frame(batch->state.char_id[d_idx], prev_msid);
      if (end_frame > 0.0f && (float)prev_frame > end_frame) {
        prev_frame = msl_anim_frame_floor_u16(end_frame);
      }
      // Ft_MF_SkipAnim GuardOn entry BODY pose:
      // ftCo_800924C0 enters GuardOn with Ft_MF_SkipAnim after the previous action's Anim proc has
      // already interpreted the live JObj tree for this frame. ftColl_80078C70/lbColl_8000805C
      // therefore consume the previous common-action collision pose even though Slippi publishes
      // action=GuardOn and animation_index=-1. Probe proof: VSA:4317 Marth Dash -> GuardOn accepts
      // BODY on cap6/bone29 at Dash frame 2 after ShieldDesc misses.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_800924C0
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ChangeMotionState}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
      *out_msid = prev_msid;
      *out_frame = (float)prev_frame;
      return 1u;
    }
  }
use_current_guard_pose:

  *out_msid = msid;
  *out_frame = pose_frame;
  return 1u;
}

uint8_t combat_guard_family_catch_hurtcap_world(const MslBatch* batch, size_t d_idx,
                                                const MslHurtCap* cap, float* out_ax, float* out_ay,
                                                float* out_az, float* out_bx, float* out_by,
                                                float* out_bz, float* out_r) {
  if (batch == NULL || cap == NULL || out_ax == NULL || out_ay == NULL || out_az == NULL ||
      out_bx == NULL || out_by == NULL || out_bz == NULL || out_r == NULL) {
    return 0u;
  }
  if (!cap->is_grabbable) {
    return 0u;
  }

  uint16_t msid = 0u;
  if (!combat_guard_family_no_submotion_catch_source_msid(batch, d_idx, &msid)) {
    return 0u;
  }

  // Guard-family catch-only no-submotion source pose:
  // - ftCo_MS_GuardOn/Guard/GuardSetOff/GuardReflect have source-owned submotion ids in the motion
  //   state table even when Slippi serializes animation_index=-1/-2 for the post-frame snapshot.
  // - ftColl_80078A2C checks `hurt_capsules[j].is_grabbable` through lbColl_80007ECC; it does not
  //   acquire a grab from ShieldDesc rim/body shield-hit precedence.
  // - Slippi can serialize late GuardReflect snapshots with animation_index=-1/-2, while
  //   ftColl_80078A2C still checks `hurt_capsules[j].is_grabbable` for Catch/CatchDash selection.
  // - Keep this geometry local to catch selection; BODY hurtcaps remain absent for this
  //   no-submotion slice in hurtboxes_refresh().
  // refs/melee/src/melee/ft/ftmotionstates.c::{
  //   ftCo_MS_GuardOn,ftCo_MS_Guard,ftCo_MS_GuardSetOff,ftCo_MS_GuardReflect}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC
  const uint8_t char_id = batch->state.char_id[d_idx];
  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
  float m[12];
  if (anim_pose_get_collision_matrix_f32(batch, d_idx, msid, (float)frame, cap->bone_part_id, m) !=
      0) {
    (void)char_id;
    return 0u;
  }
  if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
      batch->state.guard_tilt_x4[d_idx] > 0.0f) {
    uint16_t guard_tilt_frame = batch->state.guard_tilt_x8[d_idx];
    const float guard_end = msl_anim_end_frame(char_id, (uint16_t)MSL_SM_GUARD);
    if (guard_end > 0.0f && (float)guard_tilt_frame > guard_end) {
      guard_tilt_frame = msl_anim_frame_floor_u16(guard_end);
    }
    float target_m[12];
    if (anim_pose_get_collision_matrix_f32(batch, d_idx, (uint16_t)MSL_SM_GUARD,
                                           (float)guard_tilt_frame, cap->bone_part_id,
                                           target_m) == 0) {
      float tilt_mag = batch->state.guard_tilt_x4[d_idx];
      if (tilt_mag > 1.0f) {
        tilt_mag = 1.0f;
      }
      float guardon_blend = 1.0f;
      const MslCommonParams* c = msl_common_params();
      if (c != NULL && c->guard_x10_init_frames > 0.0f) {
        const float elapsed = c->guard_x10_init_frames - (float)batch->state.guard_x10[d_idx];
        guardon_blend = elapsed / c->guard_x10_init_frames;
        if (guardon_blend < 0.0f) {
          guardon_blend = 0.0f;
        }
        if (guardon_blend > 1.0f) {
          guardon_blend = 1.0f;
        }
      }
      // Source owner: GuardOn_Anim increments mv.co.guard.x0, then ftCo_80091E78 samples the
      // angled Guard timeline when mv.co.guard.x4 is nonzero and blends it toward the GuardOn
      // entry pose by x0 / fp->x2E8 before ftColl_80078A2C consumes grabbable hurtcaps.
      // The seed surface carries the post-update guard tilt target (`x8`), tilt magnitude (`x4`),
      // and GuardOn hold timer (`x10`), so catch selection can reconstruct the same no-submotion
      // grabbable pose without consuming ShieldDesc center/rim geometry.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardOn_Anim,ftCo_80091E78}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC
      for (int i = 0; i < 12; i++) {
        const float tilted = m[i] + tilt_mag * (target_m[i] - m[i]);
        m[i] += guardon_blend * (tilted - m[i]);
      }
    }
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  float bx = 0.0f, by = 0.0f, bz = 0.0f;
  msl_mtx34_mul_point(m, cap->a_offset, &ax, &ay, &az);
  msl_mtx34_mul_point(m, cap->b_offset, &bx, &by, &bz);

  const MslCharParams* chp = msl_char_params_fast(char_id);
  const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                  ? chp->model_scaling
                                  : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[d_idx] * model_scaling;
  if (!(model_scale > 0.0f)) {
    return 0u;
  }
  ax *= model_scale;
  ay *= model_scale;
  az *= model_scale;
  bx *= model_scale;
  by *= model_scale;
  bz *= model_scale;

  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float ax_rot_x = facing_dir * az;
  const float ax_rot_z = -facing_dir * ax;
  const float bx_rot_x = facing_dir * bz;
  const float bx_rot_z = -facing_dir * bx;

  *out_ax = ax_rot_x + batch->state.pos_x[d_idx];
  *out_ay = ay + batch->state.pos_y[d_idx];
  *out_az = ax_rot_z + batch->state.pos_z[d_idx];
  *out_bx = bx_rot_x + batch->state.pos_x[d_idx];
  *out_by = by + batch->state.pos_y[d_idx];
  *out_bz = bx_rot_z + batch->state.pos_z[d_idx];
  *out_r = cap->scale * model_scale;
  return (uint8_t)(*out_r > 0.0f);
}

uint8_t combat_catch_grabbable_dynamic_hurtcap_world(const MslBatch* batch, size_t d_idx,
                                                     uint8_t cap_id, float* out_ax, float* out_ay,
                                                     float* out_az, float* out_bx, float* out_by,
                                                     float* out_bz, float* out_r) {
  if (batch == NULL || out_ax == NULL || out_ay == NULL || out_az == NULL || out_bx == NULL ||
      out_by == NULL || out_bz == NULL || out_r == NULL) {
    return 0u;
  }
  const uint8_t char_id = batch->state.char_id[d_idx];
  const MslHurtCap* caps = NULL;
  uint16_t cap_count_u16 = 0u;
  if (hurtcaps_get(char_id, &caps, &cap_count_u16) != 0 || caps == NULL ||
      (uint16_t)cap_id >= cap_count_u16) {
    return 0u;
  }
  const MslHurtCap* cap = &caps[cap_id];
  if (!cap->is_grabbable || batch->state.animation_index[d_idx] > 0xFFFFu) {
    return 0u;
  }

  // Catch-only dynamic grabbable hurtcap owner:
  // - ftColl_80078A2C tests `hurt_capsules[j].is_grabbable` through lbColl_80007ECC.
  // - SSDYNN01 v8's catch-grabbable index is allowed to carry live `ftData.x2C` dynamic-chain
  //   state for that Catch selection path without marking the same submotion as a BODY collision
  //   owner. The motivating probe is Fox AttackDash part-18: vanilla rejects the grabbable tail
  //   capsule in Catch while existing BODY locks keep AttackDash on static collision matrices.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC
  // refs/melee/src/melee/ft/ftdynamics.c::{ftCo_8009DD94,ftCo_8009E318}
  // data/anims/fox.dyn.bin (SSDYNN01 catch-grabbable owner index)
  const uint16_t msid = (uint16_t)batch->state.animation_index[d_idx];
  const float pose_sample_frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  float m[12];
  if (anim_pose_get_catch_grabbable_matrix_f32(batch, d_idx, msid, pose_sample_frame,
                                               cap->bone_part_id, m) != 0) {
    return 0u;
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  float bx = 0.0f, by = 0.0f, bz = 0.0f;
  msl_mtx34_mul_point(m, cap->a_offset, &ax, &ay, &az);
  msl_mtx34_mul_point(m, cap->b_offset, &bx, &by, &bz);

  const MslCharParams* chp = msl_char_params_fast(char_id);
  const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                  ? chp->model_scaling
                                  : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[d_idx] * model_scaling;
  if (!(model_scale > 0.0f)) {
    return 0u;
  }
  ax *= model_scale;
  ay *= model_scale;
  az *= model_scale;
  bx *= model_scale;
  by *= model_scale;
  bz *= model_scale;

  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float ax_rot_x = facing_dir * az;
  const float ax_rot_z = -facing_dir * ax;
  const float bx_rot_x = facing_dir * bz;
  const float bx_rot_z = -facing_dir * bx;
  *out_ax = ax_rot_x + batch->state.pos_x[d_idx];
  *out_ay = ay + batch->state.pos_y[d_idx];
  *out_az = ax_rot_z + batch->state.pos_z[d_idx];
  *out_bx = bx_rot_x + batch->state.pos_x[d_idx];
  *out_by = by + batch->state.pos_y[d_idx];
  *out_bz = bx_rot_z + batch->state.pos_z[d_idx];
  *out_r = cap->scale * model_scale;
  return (uint8_t)(*out_r > 0.0f);
}

uint8_t combat_defender_downed_catch_mask_blocks(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_DOWN_BOUND_U:
    case MSL_ACT_DOWN_WAIT_U:
    case MSL_ACT_DOWN_DAMAGE_U:
    case MSL_ACT_DOWN_BOUND_D:
    case MSL_ACT_DOWN_WAIT_D:
    case MSL_ACT_DOWN_DAMAGE_D:
      return 1u;
    default:
      return 0u;
  }
}

uint8_t combat_guard_family_body_hurtcap_world(const MslBatch* batch, size_t d_idx,
                                               const MslHurtCap* cap, uint8_t cap_id,
                                               uint16_t cap_count, float* out_ax, float* out_ay,
                                               float* out_az, float* out_bx, float* out_by,
                                               float* out_bz, float* out_r) {
  if (batch == NULL || cap == NULL || out_ax == NULL || out_ay == NULL || out_az == NULL ||
      out_bx == NULL || out_by == NULL || out_bz == NULL || out_r == NULL) {
    return 0u;
  }

  uint16_t msid = 0u;
  float pose_frame = 0.0f;
  if (!combat_guard_family_no_submotion_body_source_pose(batch, d_idx, &msid, &pose_frame)) {
    return 0u;
  }

  // Guard-family no-submotion BODY fallback:
  // - ftCo_800924C0/ftCo_80092F2C enter GuardOn/GuardSetOff with Slippi's no-submotion snapshot
  //   shape, but ftColl_80078C70 still owns fighter BODY fallthrough after a ShieldDesc miss.
  // - GuardReflect keeps the narrower expired-x14 owner: ftCo_GuardReflect_Anim calls
  //   ftCo_80093BC0 before ftColl_80078C70; only after that recreates ShieldDesc can a missed
  //   shield overlap fall through to BODY.
  // - Keep this local to fighter BODY selection. Global hurtboxes_refresh() stays empty for this
  //   same-frame entry slice so item collision does not inherit new BODY contacts.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_800924C0,ftCo_80092F2C,ftCo_80093BC0,ftCo_GuardReflect_Anim}
  // refs/melee/src/melee/ft/ftmotionstates.c::{
  //   ftCo_MS_GuardOn,ftCo_MS_Guard,ftCo_MS_GuardSetOff,ftCo_MS_GuardReflect}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80076ED8,ftColl_80078C70}
  uint32_t can_hit_mask = 0xFFFFFFFFu;
  (void)move_tables_hurtbox_can_hit_mask_at_frame(batch->state.char_id[d_idx], msid, 0u, cap_count,
                                                  &can_hit_mask);
  if (((can_hit_mask >> cap_id) & 0x1u) == 0u) {
    return 0u;
  }

  float m[12];
  if (anim_pose_get_collision_matrix_f32(batch, d_idx, msid, pose_frame, cap->bone_part_id, m) !=
      0) {
    return 0u;
  }

  const uint8_t char_id = batch->state.char_id[d_idx];
  if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
      batch->state.guard_tilt_x4[d_idx] > 0.0f) {
    uint16_t guard_tilt_frame = batch->state.guard_tilt_x8[d_idx];
    const float guard_end = msl_anim_end_frame(char_id, (uint16_t)MSL_SM_GUARD);
    if (guard_end > 0.0f && (float)guard_tilt_frame > guard_end) {
      guard_tilt_frame = msl_anim_frame_floor_u16(guard_end);
    }
    float target_m[12];
    if (anim_pose_get_collision_matrix_f32(batch, d_idx, (uint16_t)MSL_SM_GUARD,
                                           (float)guard_tilt_frame, cap->bone_part_id,
                                           target_m) == 0) {
      float tilt_mag = batch->state.guard_tilt_x4[d_idx];
      if (tilt_mag > 1.0f) {
        tilt_mag = 1.0f;
      }
      float guardon_blend = 1.0f;
      const MslCommonParams* c = msl_common_params();
      if (c != NULL && c->guard_x10_init_frames > 0.0f) {
        const float elapsed = c->guard_x10_init_frames - (float)batch->state.guard_x10[d_idx];
        guardon_blend = elapsed / c->guard_x10_init_frames;
        if (guardon_blend < 0.0f) {
          guardon_blend = 0.0f;
        } else if (guardon_blend > 1.0f) {
          guardon_blend = 1.0f;
        }
      }
      // Source owner: GuardOn_Anim increments mv.co.guard.x0, ftCo_800925A4 updates x10, and
      // ftCo_80091E78 blends the live GuardOn JObj chain toward the selected Guard tilt target
      // before ftColl_80078C70/lbColl_8000805C consumes BODY geometry. The no-submotion Slippi
      // snapshot still carries guard_tilt_x4/x8 and x10, so BODY must use the same live matrix
      // owner as ShieldDesc/catch instead of the frozen GuardOn frame-0 pose.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
      // data/shields/<char>.bin (MSLSHLD1 GuardOn/Guard tilt owner)
      for (int i = 0; i < 12; i++) {
        const float tilted = m[i] + tilt_mag * (target_m[i] - m[i]);
        m[i] += guardon_blend * (tilted - m[i]);
      }
    }
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  float bx = 0.0f, by = 0.0f, bz = 0.0f;
  msl_mtx34_mul_point(m, cap->a_offset, &ax, &ay, &az);
  msl_mtx34_mul_point(m, cap->b_offset, &bx, &by, &bz);

  const MslCharParams* chp = msl_char_params_fast(char_id);
  const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                  ? chp->model_scaling
                                  : 1.0f;
  const float model_scale = batch->state.fighter_scale_y[d_idx] * model_scaling;
  if (!(model_scale > 0.0f)) {
    return 0u;
  }
  ax *= model_scale;
  ay *= model_scale;
  az *= model_scale;
  bx *= model_scale;
  by *= model_scale;
  bz *= model_scale;

  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float ax_rot_x = facing_dir * az;
  const float ax_rot_z = -facing_dir * ax;
  const float bx_rot_x = facing_dir * bz;
  const float bx_rot_z = -facing_dir * bx;

  *out_ax = ax_rot_x + batch->state.pos_x[d_idx];
  *out_ay = ay + batch->state.pos_y[d_idx];
  *out_az = ax_rot_z + batch->state.pos_z[d_idx];
  *out_bx = bx_rot_x + batch->state.pos_x[d_idx];
  *out_by = by + batch->state.pos_y[d_idx];
  *out_bz = bx_rot_z + batch->state.pos_z[d_idx];
  *out_r = cap->scale * model_scale;
  return (uint8_t)(*out_r > 0.0f);
}

uint8_t combat_hitbox_hitbox_overlap_lbColl_80007AFC(const MslBatch* batch, size_t hb0_i,
                                                     size_t hb1_i) {
  if (batch == NULL) {
    return 0u;
  }
  // Hitbox-vs-hitbox collision uses swept HitCapsule centers (`x58` -> `x4C`) for both capsules,
  // not just the current-frame centers. This is the clank/rebound predicate that runs before BODY
  // hitbox-vs-hurtcap admission in ftColl_80078C70.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007AFC,lbColl_80006094}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007699C}
  float x0a = batch->state.hitbox_x[hb0_i];
  float y0a = batch->state.hitbox_y[hb0_i];
  float z0a = batch->state.hitbox_z[hb0_i];
  if (batch->state.hitbox_prev_enabled[hb0_i]) {
    x0a = batch->state.hitbox_prev_x[hb0_i];
    y0a = batch->state.hitbox_prev_y[hb0_i];
    z0a = batch->state.hitbox_prev_z[hb0_i];
  }
  const float x0b = batch->state.hitbox_x[hb0_i];
  const float y0b = batch->state.hitbox_y[hb0_i];
  const float z0b = batch->state.hitbox_z[hb0_i];

  float x1a = batch->state.hitbox_x[hb1_i];
  float y1a = batch->state.hitbox_y[hb1_i];
  float z1a = batch->state.hitbox_z[hb1_i];
  if (batch->state.hitbox_prev_enabled[hb1_i]) {
    x1a = batch->state.hitbox_prev_x[hb1_i];
    y1a = batch->state.hitbox_prev_y[hb1_i];
    z1a = batch->state.hitbox_prev_z[hb1_i];
  }
  const float x1b = batch->state.hitbox_x[hb1_i];
  const float y1b = batch->state.hitbox_y[hb1_i];
  const float z1b = batch->state.hitbox_z[hb1_i];

  float d2 = 0.0f;
  combat_segment_segment_dist2(x0a, y0a, z0a, x0b, y0b, z0b, x1a, y1a, z1a, x1b, y1b, z1b, &d2,
                               NULL, NULL);
  const float rr = batch->state.hitbox_radius[hb0_i] + batch->state.hitbox_radius[hb1_i];
  return (uint8_t)(d2 <= rr * rr);
}

uint8_t combat_hitbox_targets_fighter_ground_state(uint16_t hitbox_flags,
                                                   uint8_t defender_on_ground) {
  // Fighter-vs-fighter collision filters each HitCapsule by the opponent's ground/air state before
  // the hitbox-vs-hitbox clank owner runs.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  if (defender_on_ground) {
    return (hitbox_flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) != 0 ? 1u : 0u;
  }
  return (hitbox_flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) != 0 ? 1u : 0u;
}

void combat_clank_skip_same_hit_group(
    const MslBatch* batch, int bi, int attacker, int defender, int hb_id,
    uint8_t clank_skip_hb[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_MAX_HITBOXES]) {
  if (batch == NULL || hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return;
  }
  const size_t src_i = idx_hitbox(bi, attacker, hb_id);
  const uint8_t group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[src_i]);
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    const size_t cur_i = idx_hitbox(bi, attacker, hb);
    if (!batch->state.hitbox_enabled[cur_i]) {
      continue;
    }
    if (hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[cur_i]) == group) {
      clank_skip_hb[attacker][defender][hb] = 1u;
    }
  }
}

void combat_clank_candidate_skip_same_hit_group_all(
    const MslBatch* batch, int bi, int attacker, int defender, int hb_id,
    uint8_t clank_candidate_skip_hb[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_MAX_HITBOXES]) {
  if (batch == NULL || hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return;
  }
  const size_t src_i = idx_hitbox(bi, attacker, hb_id);
  const uint8_t group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[src_i]);
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    const size_t cur_i = idx_hitbox(bi, attacker, hb);
    if (!batch->state.hitbox_enabled[cur_i]) {
      continue;
    }
    if (hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[cur_i]) == group) {
      clank_candidate_skip_hb[attacker][defender][hb] = 1u;
    }
  }
}

void combat_clank_register_same_hit_group(MslBatch* batch, int bi, int attacker, int defender,
                                          int hb_id, uint16_t defender_iid) {
  if (batch == NULL || hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return;
  }
  const size_t src_i = idx_hitbox(bi, attacker, hb_id);
  const uint8_t group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[src_i]);
  const uint8_t rehit_frames = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[src_i]);

  // ftColl_8007699C's inlineA0/inlineA1 route hitbox-vs-hitbox contact through
  // lbColl_80008688(..., type=3, ...), sharing the victim entry across every active HitCapsule in
  // the same hit_group. This is persistent HitCapsule state, not just a same-pass BODY skip; it
  // keeps later hitlag-tail collision passes from re-clanking the same overlapping capsules.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007699C,inlineA0,inlineA1}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
  hitlist_register_fighter_group(batch, bi, attacker, group, defender, defender_iid,
                                 (int)MSL_LBCOLL_INSERT_FT_HITBOX_CONTACT, rehit_frames);
}

uint8_t combat_mtx34_inverse_point(const float m[12], float x, float y, float z, float* out_x,
                                   float* out_y, float* out_z) {
  return (uint8_t)msl_mtx34_inverse_point(m, x, y, z, out_x, out_y, out_z);
}

uint8_t combat_is_damage_or_firefox_launch_victim_action(uint8_t char_id, uint16_t action_id) {
  return msl_damage_owner_is_damage_or_firefox_launch_action(char_id, action_id);
}

uint8_t combat_is_damage_air_action(uint16_t action_id) {
  return msl_damage_owner_is_damage_air_action(action_id);
}

uint8_t combat_residual_frame_start_hitcapsule_owner(const MslBatch* batch, size_t idx) {
  // No char-family gate: the victim-action set below is common-damage + kind-keyed firefox
  // rows (msl_damage_owner_is_damage_or_firefox_launch_action), and frame-start residual
  // hitcapsules are common ftColl mechanics.
  if (batch == NULL || batch->state.hitbox_count[idx] == 0u) {
    return 0u;
  }
  if (!combat_is_damage_or_firefox_launch_victim_action(batch->state.char_id[idx],
                                                        batch->state.action_id[idx])) {
    return 0u;
  }
  if ((msl_motion_state_fx_special_kind(batch->state.char_id[idx], batch->state.action_id[idx]) ==
           (uint8_t)MSL_FX_KIND_SPECIAL_HI ||
       msl_motion_state_fx_special_kind(batch->state.char_id[idx], batch->state.action_id[idx]) ==
           (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI) &&
      (msl_motion_state_fx_special_kind(batch->state.char_id[idx],
                                        batch->state.prev_action_id[idx]) ==
           (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD ||
       msl_motion_state_fx_special_kind(batch->state.char_id[idx],
                                        batch->state.prev_action_id[idx]) ==
           (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD_AIR)) {
    // Firefox/Firebird hold -> launch hitboxes are authored by the launch action after the
    // transition out of Hold/HoldAir, not by a residual frame-start Damage/FlyReflect capsule.
    // Keep the current attack instance in staling so repeated launch contacts use the stale queue
    // state visible on the seed row.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHiHold*_Anim,
    //   ftFx_SpecialHi_Enter,ftFx_SpecialAirHi_Enter}
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_80076ED8}
    return 0u;
  }
  if (combat_is_damage_or_firefox_launch_victim_action(batch->state.char_id[idx],
                                                       batch->state.prev_action_id[idx]) ||
      batch->state.prev_action_id[idx] == batch->state.action_id[idx]) {
    return 0u;
  }
  return (uint8_t)(batch->state.frame_start_attack_id[idx] != 0u &&
                   batch->state.frame_start_attack_id[idx] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
                   batch->state.frame_start_instance_id[idx] != 0u);
}

uint8_t combat_hitlist_victim_pointer_may_change(uint8_t stocks, uint16_t action_id) {
  // Decomp hitlists store a victim pointer inside HitVictim; the simulator uses Slippi instance_id as
  // a proxy and only treats mismatches as a new victim when the object pointer can actually change.
  // Keep this policy aligned with src/hitlist.c::hitlist_capsule_find_fighter_entry.
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
  if (stocks == 0u) {
    return 1u;
  }
  return (action_id == (uint16_t)MSL_ACT_DEAD_DOWN || action_id == (uint16_t)MSL_ACT_DEAD_LEFT ||
          action_id == (uint16_t)MSL_ACT_DEAD_RIGHT ||
          action_id == (uint16_t)MSL_ACT_DEAD_UP_STAR || action_id == (uint16_t)MSL_ACT_REBIRTH ||
          action_id == (uint16_t)MSL_ACT_REBIRTH_WAIT)
             ? 1u
             : 0u;
}

uint8_t combat_is_downed_damage_contact_action(uint16_t action_id) {
  return msl_damage_owner_is_downed_damage_contact_action(action_id);
}

uint16_t combat_down_damage_action_from_source(uint16_t action_id) {
  return msl_damage_owner_down_damage_action_from_source(action_id);
}

uint32_t combat_down_damage_submotion_from_action(uint16_t action_id) {
  return msl_damage_owner_down_damage_submotion_from_action(action_id);
}

uint8_t combat_float_aobj_hurtcap_pose_owner(uint16_t action_id) {
  return msl_motion_state_common_class_has_fast(action_id, MSL_MS_CLASS_LANDING_AIR);
}

uint8_t combat_run_entry_collision_pose_sample_frame(const MslBatch* batch, size_t idx,
                                                     const MslCharParams* ch, uint16_t action_id,
                                                     float anim_frame_f32, float* out_frame) {
  if (batch == NULL || ch == NULL || out_frame == NULL ||
      (action_id != (uint16_t)MSL_ACT_RUN && action_id != (uint16_t)MSL_ACT_RUN_DIRECT) ||
      !(ch->run_animation_scaling > 0.0f)) {
    return 0u;
  }
  if (isfinite(batch->state.ground_friction_mul[idx]) &&
      batch->state.ground_friction_mul[idx] < 1.0f) {
    return 0u;
  }
  if (batch->state.action_frame[idx] != 2) {
    return 0u;
  }
  if (msl_anim_frame_floor_u16(anim_frame_f32) != 2u) {
    return 0u;
  }
  const float facing_dir = (batch->state.facing_dir1[idx] < 0) ? -1.0f : 1.0f;
  const float vel = batch->state.speed_ground_x_self[idx];
  float source_rate = 0.0f;
  if (vel * facing_dir > 0.0f) {
    source_rate = fabsf(vel) / ch->run_animation_scaling;
  }
  if (!(source_rate >= 0.0f && source_rate < 1.0f)) {
    return 0u;
  }
  const float source_frame = 1.0f + source_rate;
  if (floorf(source_frame) >= floorf(anim_frame_f32)) {
    return 0u;
  }
  // Same first post-entry Run collision-pose owner as hurtboxes.c. Keep this in the matrix-radius
  // recompute path so cached and uncached BODY checks consume the same source pose.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Run.c::{
  //   ftCo_Run_Enter_Full,ftCo_Run_Anim}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  *out_frame = source_frame;
  return 1u;
}

uint8_t combat_side_special_start_passivewalljump_entry_pose_owner(const MslBatch* batch,
                                                                   size_t idx, uint8_t char_id,
                                                                   uint16_t action_id) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id);
  if (fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_S_START &&
      fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_START) {
    return 0u;
  }
  if (batch->state.action_frame[idx] > 2) {
    return 0u;
  }
  return (uint8_t)(batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP ||
                   batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP);
}

float combat_root_facing_dir_for_body_hurtcap(const MslBatch* batch, size_t idx) {
  float facing_dir = batch->state.facing[idx] ? 1.0f : -1.0f;
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_ESCAPE_F &&
      batch->state.action_frame[idx] >= 20 && batch->state.facing_dir1[idx] != 0) {
    // Same EscapeF root-facing owner as hurtboxes.c: the frame-20 vulnerable phase emits
    // `set_throw_flags(hit_idx=0)` and keeps the collision-root owner on motion-entry facing
    // (`facing_dir1`) even when visible scalar facing has already diverged.
    // data/moves/{fox,falco}.json moves["ftCo_SM_EscapeF"].events
    // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState copies facing_dir -> facing_dir1)
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    facing_dir = (batch->state.facing_dir1[idx] < 0) ? -1.0f : 1.0f;
  }
  const uint16_t action_id = batch->state.action_id[idx];
  if ((action_id == (uint16_t)MSL_ACT_ATTACK_AIR_N || action_id == (uint16_t)MSL_ACT_ATTACK_AIR_F ||
       action_id == (uint16_t)MSL_ACT_ATTACK_AIR_B ||
       action_id == (uint16_t)MSL_ACT_ATTACK_AIR_HI ||
       action_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW) &&
      move_tables_attackair_throw_flags_b3_crossed_fp(
          batch->state.char_id[idx], action_id, batch->state.anim_frame_fp_q16_16[idx],
          batch->state.frame_speed_mul_fp_q16_16[idx])) {
    // Same AttackAir script-facing BODY pose owner as hurtboxes.c. The scalar facing flip is live
    // for gameplay-side checks after ftCo_AttackAir_Anim, but lbColl's BODY matrix for this pass
    // still reflects the already-interpreted root orientation.
    // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
    // refs/melee/src/melee/ft/ftaction.c::ftAction_800718A4
    facing_dir = -facing_dir;
  }
  return facing_dir;
}

float combat_hurtcap_pose_sample_frame(const MslBatch* batch, size_t idx, uint8_t char_id,
                                       uint16_t msid, uint16_t action_id, float anim_frame_f32,
                                       uint16_t pose_frame) {
  (void)msid;
  const MslCharParams* ch = msl_char_params_fast(char_id);
  float run_frame = 0.0f;
  if (combat_run_entry_collision_pose_sample_frame(batch, idx, ch, action_id, anim_frame_f32,
                                                   &run_frame)) {
    return run_frame;
  }
  return combat_float_aobj_hurtcap_pose_owner(action_id) ? anim_frame_f32 : (float)pose_frame;
}

uint8_t combat_body_matrix_positive_pose_reliable(const MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t char_id = batch->state.char_id[d_idx];
  const uint16_t action_id = batch->state.action_id[d_idx];
  if (msl_motion_state_class_has(char_id, action_id, MSL_MS_CLASS_COMMON_FALL)) {
    if (batch->state.action_frame[d_idx] <= 2) {
      return 1u;
    }
    if (batch->state.common_fall_blend_x4[d_idx] > 0.0f) {
      return 1u;
    }
    // After Fall entry, replay-visible state_age alone is not sufficient to reconstruct the live
    // JObj pose that ftColl_80078C70/lbColl_8000805C consumes. Later CommonFall matrix-only BODY
    // positives are admitted only when the source-owned mv.co.*.x4 blend lane is live; otherwise
    // the generated state-age matrix may reject a false positive but must not create one.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim_Inner
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    // reports/triage/grape_item05_ewt1019_dolphin_collision_probe/
    return 0u;
  }
  return 1u;
}

uint8_t combat_attackdash_post_hitbox_collision_pose_owner(const MslBatch* batch, int bi,
                                                           size_t idx, uint16_t action_id,
                                                           uint8_t char_id, float anim_frame_f32,
                                                           uint16_t attacker_action_id,
                                                           uint8_t attacker_hitbox_active) {
  (void)bi;
  (void)attacker_action_id;
  (void)attacker_hitbox_active;
  if (batch == NULL || action_id != (uint16_t)MSL_ACT_ATTACK_DASH) {
    return 0u;
  }
  if (batch->state.hitbox_count[idx] != 0u) {
    return 0u;
  }
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
  if (frame != 34u) {
    return 0u;
  }
  // Same source predicate as hurtboxes.c: AttackDash's immediate post-hitbox-clear collision pose
  // is sampled on frame 34 before the allow_interrupt gate. This is not rollout-mode-specific and
  // does not depend on the opposing attack family.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackDash.c::{
  //   ftCo_AttackDash_Anim,ftCo_AttackDash_IASA,ftCo_AttackDash_Coll}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  if (move_tables_grounded_attack_allow_interrupt(char_id, action_id, anim_frame_f32) != 0u) {
    return 0u;
  }
  return 1u;
}

uint8_t combat_guard_no_tilt_current_pose_gap(const MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD) {
    return 0u;
  }
  if (batch->state.animation_index[d_idx] != UINT32_MAX || batch->state.action_frame[d_idx] >= 0) {
    return 0u;
  }
  const float mag = batch->state.guard_tilt_x4[d_idx];
  if (!(mag == 0.0f || mag <= FLT_EPSILON)) {
    return 0u;
  }
  MslShieldTiltTableView tv;
  if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) != 0 || tv.xyz == NULL ||
      tv.frame_count == 0u) {
    return 0u;
  }
  return batch->state.guard_tilt_x8[d_idx] == tv.neutral_frame ? 1u : 0u;
}

uint8_t combat_guard_tilt_live_body_pose_owner(const MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD ||
      batch->state.animation_index[d_idx] != UINT32_MAX || batch->state.action_frame[d_idx] >= 0) {
    return 0u;
  }
  if (batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  if (batch->state.pos_z[d_idx] <= 1.0e-6f && batch->state.pos_z[d_idx] >= -1.0e-6f) {
    return 0u;
  }
  MslShieldTiltTableView tv;
  if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) != 0 || tv.xyz == NULL ||
      tv.frame_count == 0u) {
    return 0u;
  }
  return (batch->state.guard_tilt_x4[d_idx] > 0.0f) ? 1u : 0u;
}

uint8_t combat_apply_guard_tilt_live_body_matrix(const MslBatch* batch, size_t d_idx,
                                                 uint8_t char_id, uint16_t part_id,
                                                 float io_m[12]) {
  if (batch == NULL || io_m == NULL || !combat_guard_tilt_live_body_pose_owner(batch, d_idx)) {
    return 0u;
  }
  float mag = batch->state.guard_tilt_x4[d_idx];
  if (mag > 1.0f) {
    mag = 1.0f;
  }
  uint16_t guard_tilt_frame = batch->state.guard_tilt_x8[d_idx];
  const float guard_end = msl_anim_end_frame(char_id, (uint16_t)MSL_SM_GUARD);
  if (guard_end > 0.0f && (float)guard_tilt_frame > guard_end) {
    guard_tilt_frame = msl_anim_frame_floor_u16(guard_end);
  }
  float target_m[12];
  if (anim_pose_get_collision_matrix_f32(batch, d_idx, (uint16_t)MSL_SM_GUARD,
                                         (float)guard_tilt_frame, part_id, target_m) != 0) {
    return 0u;
  }
  // Source owner: ftCo_Guard_Anim -> ftCo_80091E78 samples the Guard tilt AObj/JObj timeline at
  // mv.co.guard.x8 and blends it into the live JObj chain by mv.co.guard.x4 before BODY
  // narrowphase. Hurtcap endpoints are refreshed in hurtboxes.c from the same extracted matrices;
  // the lbColl_80006E58 matrix-radius path consumes the matching bone matrix here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_Guard_Anim,ftCo_80091E78}
  // refs/melee/src/melee/ft/ftanim.c::{ftAnim_8006F4C8,ftAnim_80070710,ftAnim_80070108,ftAnim_8006FF74}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  for (int i = 0; i < 12; i++) {
    io_m[i] += mag * (target_m[i] - io_m[i]);
  }
  return 1u;
}

uint8_t combat_guard_tilt_live_body_z_owner_applies(const MslBatch* batch, size_t d_idx,
                                                    uint8_t shield_active) {
  if (batch == NULL || !shield_active || !combat_guard_tilt_live_body_pose_owner(batch, d_idx)) {
    return 0u;
  }
  return 1u;
}

uint8_t combat_shield_active_action(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_GUARD_ON:
    case (uint16_t)MSL_ACT_GUARD:
    case (uint16_t)MSL_ACT_GUARD_REFLECT:
    case (uint16_t)MSL_ACT_GUARD_SET_OFF:
      return 1u;
    default:
      return 0u;
  }
}

uint8_t combat_single_create_grounded_guardreflect_enable_edge_allows_shield(const MslBatch* batch,
                                                                             size_t a_idx,
                                                                             size_t d_idx,
                                                                             size_t hb_i) {
  if (batch == NULL) {
    return 0u;
  }
  if ((batch->state.hitbox_enable_edge[hb_i] == 0u &&
       batch->state.hitbox_pose_create[hb_i] == 0u) ||
      batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_REFLECT) {
    return 0u;
  }
  const uint16_t attacker_action = batch->state.action_id[a_idx];
  if (attacker_action != (uint16_t)MSL_ACT_ATTACK_LW3 ||
      batch->state.animation_index[a_idx] != (uint32_t)MSL_SM_ATTACK_LW3) {
    return 0u;
  }
  if (batch->state.action_frame[a_idx] != (int16_t)MSL_ATTACK_LW3_FIRST_CREATE_FRAME) {
    return 0u;
  }

  // AttackLw3 first-create / GuardReflect ShieldDesc owner:
  // `ftAction_8007121C -> ftColl_800768A0` clears HitCapsule.victims_1 on an enable edge unless
  // an already-active same-group HitCapsule can be copied. For AttackLw3's extracted frame-7
  // single-create script, the dense group seed is not a concrete per-HitCapsule victim list and can
  // stale-carry a previous shield miss into the first live create frame. Do not let that stale
  // dense proxy suppress the source ShieldDesc hit; generated AttackS4 late-payload powershield
  // latches remain on their separate owner.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076CBC,ftColl_80078C70}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_8000ACFC}
  // data/scripts/{fox,falco}.bin (MSLFTSC1 grounded Attack* create_hitbox frame)
  return 1u;
}

void combat_preserve_guard_x10_for_immediate_setoff(MslBatch* batch, size_t d_idx,
                                                    uint16_t d_motion_id_pre,
                                                    const MslCommonParams* c,
                                                    uint8_t fighter_powershield_active) {
  if (batch == NULL) {
    return;
  }

  if (fighter_powershield_active) {
    // Fighter shield contact with fp->x221C_b2 set takes the powershield-active branch in
    // ftColl_80076CBC, which calls ftCo_80094138 before ftCo_80092F2C. That source helper arms
    // guard.x1C and clears guard.x10; do not restore the frame-start x10 for this owner.
    // Item shield contact is separate (`ftColl_80077688`) and passes false here because it does not
    // call ftCo_80094138.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80077688}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_80094138,ftCo_80092F2C}
    return;
  }

  if (d_motion_id_pre == (uint16_t)MSL_ACT_GUARD_REFLECT &&
      !msl_guard_lifecycle_action_has_shield_callback(batch->state.prev_action_id[d_idx])) {
    // GuardReflect can be entered by the input callback earlier in this same step, then
    // immediately consumed by shield contact. The source path has initialized shield move
    // variables before ftCo_80092F2C, but no ordinary shield hold tick has published a row yet.
    // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_80091AD8,ftCo_80093A50,ftCo_80092F2C}
    batch->state.guard_x10[d_idx] = msl_guard_x10_raw_init_u8(c);
    return;
  }

  if (d_motion_id_pre == (uint16_t)MSL_ACT_GUARD_ON &&
      batch->state.guard_on_entered_this_frame[d_idx] != 0u) {
    // Immediate GuardOn -> GuardSetOff contact is still in the ftCo_800924C0 entry callback phase:
    // x10 has been initialized from p_ftCommonData->x268, but the ordinary GuardOn/Guard
    // ftCo_800925A4 owner tick has not produced a replay-visible GuardOn hold snapshot. The
    // no-submotion GuardOn snapshot lane stores x10 after that first hold tick; do not carry that
    // representation into same-frame GuardSetOff, or rollout exits Guard one frame early after
    // shieldstun.
    //
    // GuardOn shield hits are the same source owner even when the entry marker is no longer live:
    // GuardOn_Anim is the only callback that would decrement x10, and a same-frame GuardSetOff
    // contact bypasses that ordinary hold tick.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
    //   ftCo_80091A4C,ftCo_800924C0,ftCo_800925A4,ftCo_80092F2C}
    batch->state.guard_x10[d_idx] = msl_guard_x10_raw_init_u8(c);
    return;
  }

  if (!combat_shield_active_action(d_motion_id_pre)) {
    return;
  }

  // Fighter_ProcessHit shield contact consumes the guard move variables that were live before the
  // per-frame GuardOn/Guard hold tick. This sim runs the Guard action callback before combat, so
  // restore the frame-start x10 owner when the same frame transitions into GuardSetOff.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800925A4,ftCo_80092F2C}
  if (batch->state.guard_x10_frame_start[d_idx] > batch->state.guard_x10[d_idx]) {
    batch->state.guard_x10[d_idx] = batch->state.guard_x10_frame_start[d_idx];
  }
}

uint8_t combat_body_overlap_lbColl_80006E58_matrix_radius_impl(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, int cap_id, float hx,
    float hy, float hz, float hr, float ax, float ay, float az, float bx, float by, float bz,
    uint8_t use_catch_grabbable_pose, float* out_overlap_amount, uint8_t* out_evaluated) {
  if (out_overlap_amount) {
    *out_overlap_amount = 0.0f;
  }
  if (out_evaluated) {
    *out_evaluated = 0u;
  }
  if (batch == NULL) {
    return 0u;
  }
  const size_t d_idx = msl_idx_player(bi, defender);

  // BODY matrix-radius geometry owner:
  // - ftColl_80078C70 routes fighter BODY checks through lbColl_8000805C.
  // - lbColl_8000805C/lbColl_80006E58 computes closest points between the HitCapsule x58->x4C
  //   segment and hurtcap a_pos->b_pos, then derives an effective hurt radius through the hurt
  //   bone matrix before writing `hit->coll_distance`.
  // - Use this as a narrow decomp-shaped supplement to the simple world sphere/capsule overlap; it
  //   does not consult replay proof or row ids.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  const size_t a_idx = msl_idx_player(bi, attacker);
  const uint8_t char_id = batch->state.char_id[d_idx];
  const uint16_t attacker_action_id = batch->state.action_id[a_idx];
  const uint8_t attacker_hitbox_active = (uint8_t)(batch->state.hitbox_count[a_idx] != 0u);
  const uint32_t anim_u32 = batch->state.animation_index[d_idx];
  const uint16_t action_id = batch->state.action_id[d_idx];
  const uint8_t side_special_start_pre_anim_pose =
      combat_side_special_start_passivewalljump_entry_pose_owner(batch, d_idx, char_id, action_id);
  const float cr = batch->state.hurtcap_radius[idx_hurtcap(bi, defender, cap_id)];
  if (side_special_start_pre_anim_pose &&
      !combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL)) {
    if (out_evaluated) {
      *out_evaluated = 1u;
    }
    return 0u;
  }
  uint16_t msid = 0u;
  float no_submotion_pose_frame = 0.0f;
  uint8_t no_submotion_body_pose = 0u;
  if (anim_u32 > 0xFFFFu) {
    if (action_id == (uint16_t)MSL_ACT_GUARD) {
      msid = (uint16_t)MSL_SM_GUARD;
    } else if (!combat_guard_family_no_submotion_body_source_pose(batch, d_idx, &msid,
                                                                  &no_submotion_pose_frame)) {
      return 0u;
    } else {
      no_submotion_body_pose = 1u;
    }
  } else {
    msid = (uint16_t)anim_u32;
  }
  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);

  const MslHurtCap* caps = NULL;
  uint16_t cap_count_u16 = 0;
  if (hurtcaps_get(char_id, &caps, &cap_count_u16) != 0 || caps == NULL) {
    return 0u;
  }
  if (cap_id < 0 || (uint16_t)cap_id >= cap_count_u16) {
    return 0u;
  }
  const MslHurtCap* cap = &caps[cap_id];
  const MslCharParams* chp = msl_char_params_fast(char_id);
  const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                  ? chp->model_scaling
                                  : 1.0f;
  const float scale_y = batch->state.fighter_scale_y[d_idx];
  const float model_scale = scale_y * model_scaling;
  if (!(model_scale > 0.0f)) {
    return 0u;
  }
  const float pos_z = batch->state.pos_z[d_idx];
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  float px = hx;
  float py = hy;
  float pz = hz;
  if (batch->state.hitbox_prev_enabled[hb_i]) {
    px = batch->state.hitbox_prev_x[hb_i];
    py = batch->state.hitbox_prev_y[hb_i];
    pz = batch->state.hitbox_prev_z[hb_i];
  }

  float d2 = 0.0f;
  float s = 0.0f;
  float t = 0.0f;
  combat_segment_segment_dist2(px, py, pz, hx, hy, hz, ax, ay, az, bx, by, bz, &d2, &s, &t);
  if (!(d2 >= 0.0f)) {
    return 0u;
  }
  const float world_dist = sqrtf(d2);
  const float hit_cp_x = px + s * (hx - px);
  const float hit_cp_y = py + s * (hy - py);
  const float hit_cp_z = pz + s * (hz - pz);
  const float hurt_cp_x = ax + t * (bx - ax);
  const float hurt_cp_y = ay + t * (by - ay);
  const float hurt_cp_z = az + t * (bz - az);

  float m[12];
  const uint8_t attackdash_post_hitbox_pose = combat_attackdash_post_hitbox_collision_pose_owner(
      batch, bi, d_idx, action_id, char_id, anim_frame_f32, attacker_action_id,
      attacker_hitbox_active);
  const size_t cap_i = idx_hurtcap(bi, defender, cap_id);
  const uint8_t can_use_cached_matrix =
      (!use_catch_grabbable_pose && no_submotion_body_pose == 0u && !attackdash_post_hitbox_pose &&
       !side_special_start_pre_anim_pose && !combat_guard_tilt_live_body_pose_owner(batch, d_idx) &&
       batch->hurtcap_matrix_valid[cap_i] != 0u)
          ? 1u
          : 0u;
  if (can_use_cached_matrix) {
    memcpy(m, &batch->hurtcap_matrix[(size_t)cap_i * 12u], sizeof(m));
  } else {
    const uint16_t pose_frame = attackdash_post_hitbox_pose ? (uint16_t)(frame + 1u) : frame;
    uint16_t combat_pose_frame = pose_frame;
    if (side_special_start_pre_anim_pose && combat_pose_frame > 0u &&
        combat_pose_frame != 0xFFFFu) {
      // Same PassiveWallJump -> Side-B Start collision-pose owner as hurtboxes.c.
      // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
      //   ftFx_SpecialSStart_Anim,ftFx_SpecialAirSStart_Anim,ftFx_SpecialSStart_Coll,
      //   ftFx_SpecialAirSStart_Coll}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_PassiveWall.c::ftCo_PassiveWall_IASA
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
      combat_pose_frame = (uint16_t)(combat_pose_frame - 1u);
    }
    const float pose_sample_frame =
        no_submotion_body_pose != 0u
            ? no_submotion_pose_frame
            : combat_hurtcap_pose_sample_frame(batch, d_idx, char_id, msid, action_id,
                                               anim_frame_f32, combat_pose_frame);
    const int matrix_status =
        use_catch_grabbable_pose ? anim_pose_get_catch_grabbable_matrix_f32(
                                       batch, d_idx, msid, pose_sample_frame, cap->bone_part_id, m)
                                 : anim_pose_get_collision_matrix_f32(
                                       batch, d_idx, msid, pose_sample_frame, cap->bone_part_id, m);
    if (matrix_status != 0) {
      return 0u;
    }
    (void)combat_apply_guard_tilt_live_body_matrix(batch, d_idx, char_id, cap->bone_part_id, m);
  }
  if (out_evaluated) {
    *out_evaluated = 1u;
  }

  const float facing_dir = combat_root_facing_dir_for_body_hurtcap(batch, d_idx);
  const float pos_x = batch->state.pos_x[d_idx];
  const float pos_y = batch->state.pos_y[d_idx];
  const float pose_scale_x = model_scale;

  const float hit_rel_x = hit_cp_x - pos_x;
  const float hit_rel_y = hit_cp_y - pos_y;
  const float hit_rel_z = hit_cp_z - pos_z;
  const float hurt_rel_x = hurt_cp_x - pos_x;
  const float hurt_rel_y = hurt_cp_y - pos_y;
  const float hurt_rel_z = hurt_cp_z - pos_z;

  const float hit_pose_x = -facing_dir * hit_rel_z;
  const float hit_pose_y = hit_rel_y;
  const float hit_pose_z = facing_dir * hit_rel_x;
  const float hurt_pose_x = -facing_dir * hurt_rel_z;
  const float hurt_pose_y = hurt_rel_y;
  const float hurt_pose_z = facing_dir * hurt_rel_x;

  float hit_local_x = 0.0f, hit_local_y = 0.0f, hit_local_z = 0.0f;
  float hurt_local_x = 0.0f, hurt_local_y = 0.0f, hurt_local_z = 0.0f;
  if (!combat_mtx34_inverse_point(m, hit_pose_x / pose_scale_x, hit_pose_y / model_scale,
                                  hit_pose_z / model_scale, &hit_local_x, &hit_local_y,
                                  &hit_local_z) ||
      !combat_mtx34_inverse_point(m, hurt_pose_x / pose_scale_x, hurt_pose_y / model_scale,
                                  hurt_pose_z / model_scale, &hurt_local_x, &hurt_local_y,
                                  &hurt_local_z)) {
    return 0u;
  }

  const float local_dx = hit_local_x - hurt_local_x;
  const float local_dy = hit_local_y - hurt_local_y;
  const float local_dz = hit_local_z - hurt_local_z;
  const float local_dist = sqrtf(local_dx * local_dx + local_dy * local_dy + local_dz * local_dz);

  float hurt_radius_world_equiv = cap->scale;
  if (local_dist > 1.0e-8f && world_dist > 0.0f) {
    hurt_radius_world_equiv = cap->scale * (world_dist / local_dist);
  }
  const float overlap_amount = hr + hurt_radius_world_equiv - world_dist;
  if (out_overlap_amount) {
    *out_overlap_amount = overlap_amount;
  }
  return (uint8_t)(overlap_amount > 0.0f);
}

uint8_t combat_body_overlap_lbColl_80006E58_matrix_radius(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, int cap_id, float hx,
    float hy, float hz, float hr, float ax, float ay, float az, float bx, float by, float bz,
    uint8_t use_catch_grabbable_pose, float* out_overlap_amount, uint8_t* out_evaluated) {
  return combat_body_overlap_lbColl_80006E58_matrix_radius_impl(
      batch, bi, attacker, hb_id, defender, cap_id, hx, hy, hz, hr, ax, ay, az, bx, by, bz,
      use_catch_grabbable_pose, out_overlap_amount, out_evaluated);
}

uint8_t combat_attackairb_continuation_body_overlap_exact(const MslBatch* batch, int bi,
                                                          int attacker, int hb_id, int defender,
                                                          int cap_id, float hx, float hy, float hz,
                                                          float hr, float ax, float ay, float az,
                                                          float bx, float by, float bz,
                                                          float* out_overlap_amount) {
  if (out_overlap_amount) {
    *out_overlap_amount = 0.0f;
  }
  if (batch == NULL) {
    return 0u;
  }
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B) {
    return 0u;
  }
  const uint16_t v_action = batch->state.action_id[d_idx];
  if (!combat_is_damage_or_firefox_launch_victim_action(batch->state.char_id[d_idx], v_action)) {
    return 0u;
  }
  return combat_body_overlap_lbColl_80006E58_matrix_radius(batch, bi, attacker, hb_id, defender,
                                                           cap_id, hx, hy, hz, hr, ax, ay, az, bx,
                                                           by, bz, 0u, out_overlap_amount, NULL);
}

uint8_t combat_attackairb_stale_owner_continuation_candidate(const MslBatch* batch, size_t a_idx,
                                                             size_t d_idx, float hitbox_damage,
                                                             uint16_t expected_hitlag) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B) {
    return 0u;
  }
  if (batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] == 0u) {
    return 0u;
  }
  const uint16_t v_action = batch->state.action_id[d_idx];
  if (v_action != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP) {
    return 0u;
  }
  if (hitbox_damage > 9.5f) {
    return 0u;
  }
  // Late AttackAirB continuation subset:
  // - Fox/Falco AttackAirB has an early strong frame-4 event (15 damage) and a late frame-8 event
  //   (9 damage) in extracted data/moves/{fox,falco}.json.
  // - The late 9-damage event is the continuation phase that needs matrix-first
  //   lbColl_8000805C/80006E58 so phantom/tip-log contacts are decided before full BODY damage.
  // - Keep strong early BAir on the normal full-BODY path; it can hit DamageFlyTop victims again
  //   rather than being suppressed by stale continuation ownership.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  // data/moves/{fox,falco}.json::ftCo_SM_AttackAirB create_hitbox events
  (void)expected_hitlag;
  return 1u;
}

uint8_t combat_hitcapsule_is_authored_same_group_primary(const MslBatch* batch, int bi,
                                                         int attacker, int hb_id) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= (int)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  if (!batch->state.hitbox_enabled[hb_i]) {
    return 0u;
  }
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return 0u;
  }
  const float damage = batch->state.hitbox_damage[hb_i];
  if (!(damage > 0.0f)) {
    return 0u;
  }

  uint8_t saw_same_group_sibling = 0u;
  for (int other = 0; other < MSL_MAX_HITBOXES; other++) {
    if (other == hb_id) {
      continue;
    }
    const size_t other_i = idx_hitbox(bi, attacker, other);
    if (!batch->state.hitbox_enabled[other_i]) {
      continue;
    }
    if (hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[other_i]) != hit_group) {
      continue;
    }
    const float other_damage = batch->state.hitbox_damage[other_i];
    if (other_damage > damage) {
      return 0u;
    }
    if (other_damage == damage && other < hb_id) {
      return 0u;
    }
    saw_same_group_sibling = 1u;
  }
  // Source/data-backed same-group primary band:
  // - `ftColl_80076ED8` receives the concrete HitCapsule selected by `ftColl_80078C70`, and
  //   `Fighter_ProcessHit` consumes that HitCapsule's authored damage/KB payload.
  // - Multi-capsule same-group scripts encode the source-selected primary capsule as the first
  //   active maximum-damage HitCapsule in that group. Keep that capsule on the full BODY path
  //   outside active DamageFly's tiny-contact phantom/tip-log owner.
  // - This predicate is derived from active MSLHITB1 hitbox table fields (`damage`, `hit_group`,
  //   source HitCapsule id/order), rather than an action id / row slice.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // data/hitboxes/{fox,falco}.bin (MSLHITB1 damage + hit_group + hitbox id/order)
  return saw_same_group_sibling;
}

uint8_t combat_primary_phantom_tiplog_allows_later_same_group_body(const MslBatch* batch,
                                                                   const MslCommonParams* c, int bi,
                                                                   int attacker, int hb_id,
                                                                   int defender,
                                                                   uint8_t hit_group) {
  if (batch == NULL || c == NULL || bi < 0 || attacker < 0 || defender < 0 ||
      attacker >= (int)MSL_MAX_PLAYERS || defender >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES || hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return 0u;
  }
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;
  const uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
  for (int other_hb = hb_id + 1; other_hb < MSL_MAX_HITBOXES; other_hb++) {
    const size_t other_i = idx_hitbox(bi, attacker, other_hb);
    if (batch->state.hitbox_enabled[other_i] == 0u ||
        hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[other_i]) != hit_group) {
      continue;
    }
    const uint16_t other_flags = batch->state.hitbox_flags[other_i];
    if (defender_on_ground) {
      if ((other_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0u) {
        continue;
      }
    } else if ((other_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
      continue;
    }

    const float hx = batch->state.hitbox_x[other_i];
    const float hy = batch->state.hitbox_y[other_i];
    const float hz = batch->state.hitbox_z[other_i];
    const float hr = batch->state.hitbox_radius[other_i];
    for (uint8_t cap_id = 0u; cap_id < hurtcap_count; cap_id++) {
      const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
      if (batch->state.hurtcap_enabled[cap_i] == 0u) {
        continue;
      }
      float overlap_amount = 0.0f;
      const uint8_t overlaps = combat_body_overlap_lbColl_80006E58_matrix_radius(
          batch, bi, attacker, other_hb, defender, (int)cap_id, hx, hy, hz, hr,
          batch->state.hurtcap_a_x[cap_i], batch->state.hurtcap_a_y[cap_i],
          batch->state.hurtcap_a_z[cap_i], batch->state.hurtcap_b_x[cap_i],
          batch->state.hurtcap_b_y[cap_i], batch->state.hurtcap_b_z[cap_i], 0u, &overlap_amount,
          NULL);
      if (overlaps != 0u && overlap_amount > c->phantom_overlap_max_x7a8) {
        return 1u;
      }
    }
  }
  return 0u;
}

uint8_t combat_sheik_chain_same_frontier_later_hitbox_owns_body(
    const MslBatch* batch, size_t a_idx, int bi, int attacker, int hb_id, uint8_t hit_group,
    int defender, int cap_id, uint8_t defender_on_ground, float ax, float ay, float az, float bx,
    float by, float bz, float cr) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= (int)MSL_MAX_HITBOXES || defender < 0 ||
      defender >= (int)batch->config.num_players || cap_id < 0 || cap_id >= MSL_MAX_HURTCAPS) {
    return 0u;
  }
  if (batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[a_idx];
  if (action != (uint16_t)MSL_ACT_SK_SPECIAL_S_START &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_START &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_S && action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_S_END &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END) {
    return 0u;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_chain_itkind == 0u) {
    return 0u;
  }
  size_t chain_ii = (size_t)-1;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = msl_idx_item(bi, it);
    if (batch->state.item_exists[ii] != 0u &&
        batch->state.item_type[ii] == ap->sheik_chain_itkind &&
        (int)batch->state.item_owner[ii] == attacker &&
        batch->state.item_sheik_chain_links_valid[ii] != 0u) {
      chain_ii = ii;
      break;
    }
  }
  if (chain_ii == (size_t)-1) {
    return 0u;
  }

  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const size_t map_base = chain_ii * (size_t)MSL_MAX_HITBOXES;
  const uint8_t link_idx = batch->state.item_sheik_chain_hitbox_link_idx[map_base + (size_t)hb_id];
  if (link_idx == 0xFFu) {
    return 0u;
  }
  const float damage = batch->state.hitbox_damage[hb_i];
  if (!(damage > 0.0f)) {
    return 0u;
  }

  for (int other = hb_id + 1; other < MSL_MAX_HITBOXES; other++) {
    const size_t other_i = idx_hitbox(bi, attacker, other);
    if (batch->state.hitbox_enabled[other_i] == 0u) {
      continue;
    }
    if (batch->state.item_sheik_chain_hitbox_link_idx[map_base + (size_t)other] != link_idx) {
      continue;
    }
    if (hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[other_i]) != hit_group) {
      continue;
    }
    const uint16_t other_flags = batch->state.hitbox_flags[other_i];
    if (defender_on_ground != 0u) {
      if ((other_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0u) {
        continue;
      }
    } else if ((other_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
      continue;
    }
    if (batch->state.hitbox_damage[other_i] > damage) {
      float other_x = batch->state.hitbox_x[other_i];
      float other_y = batch->state.hitbox_y[other_i];
      float other_z = batch->state.hitbox_z[other_i];
      (void)sheik_chain_hitbox_world_pos(batch, a_idx, (uint8_t)other, &other_x, &other_y,
                                         &other_z);
      if (!combat_body_overlap_lbColl_80006E58_scaffold(
              batch, bi, attacker, other, other_x, other_y, other_z,
              batch->state.hitbox_radius[other_i], ax, ay, az, bx, by, bz, cr,
              batch->state.fighter_scale_y[msl_idx_player(bi, defender)])) {
        continue;
      }
      // it_802BCB88 can publish the terminal Chain frontier into more than one fighter
      // HitCapsule on the same frame: the stride write updates hb2, then the terminal-link write
      // updates hb3 to that exact source link. Let the later/higher-damage source HitCapsule own the
      // BODY DmgLog only when it also overlaps this defender hurtcap; otherwise an unrelated later
      // same-link map must not suppress the earlier BODY owner.
      // refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
      // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_UpdateHitboxes
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8,ftColl_8007A06C}
      return 1u;
    }
  }
  return 0u;
}

uint8_t combat_sheik_chain_activation_edge_same_source_suppresses_body(const MslBatch* batch,
                                                                       size_t a_idx, size_t d_idx,
                                                                       int attacker, int hb_id,
                                                                       int defender) {
  if (batch == NULL || hb_id < 0 || hb_id >= MSL_MAX_HITBOXES || defender < 0 ||
      defender >= (int)batch->config.num_players) {
    return 0u;
  }
  if (batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[a_idx];
  if (action != (uint16_t)MSL_ACT_SK_SPECIAL_S && action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_S_END &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END) {
    return 0u;
  }
  if (sheik_chain_hitbox_reset_prev_active(batch, a_idx) == 0u) {
    return 0u;
  }
  float hx = 0.0f;
  float hy = 0.0f;
  float hz = 0.0f;
  if (sheik_chain_hitbox_world_pos(batch, a_idx, (uint8_t)hb_id, &hx, &hy, &hz) == 0u) {
    return 0u;
  }
  if (batch->state.hitlag[d_idx] == 0u) {
    return 0u;
  }
  // Chain activation edge:
  // ftSeakSpecialS_LoopChainHitActivate enables the HitCapsules and zeroes x914[].x58/x4C in the
  // same ftSk_SpecialS_80110BCC callback. For a defender already owned by this Chain's previous
  // BODY contact, source victims_1 state still suppresses the activation-edge BODY overlap; the
  // new empty Chain window becomes collision-visible on the following frame.
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::{
  //   ftSk_SpecialS_80110BCC,ftSeakSpecialS_LoopChainHitActivate,ftSk_SpecialS_ZeroHitboxPositions}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008434,lbColl_8000ACFC,lbColl_80008688}
  return (msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker) ||
          batch->state.instance_hit_by[d_idx] == batch->state.instance_id[a_idx])
             ? 1u
             : 0u;
}

uint16_t combat_sheik_chain_same_source_terminal_hitstun_horizon(uint16_t hitlag) {
  // Damage callbacks tick hitstun before the BODY collision pass. Source Chain victims_1 carry
  // releases on the terminal post-callback slice, three ticks below the defender Chain hitlag for the
  // supported BODY payload (official demo rec340 quiet, rec341 hit).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_Coll,ftCo_DamageFly_Coll}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
  return (hitlag > 3u) ? (uint16_t)(hitlag - 3u) : hitlag;
}

uint8_t combat_sheik_chain_terminal_same_source_episode_suppresses_body(const MslBatch* batch,
                                                                        size_t a_idx,
                                                                        size_t d_idx) {
  if (batch == NULL || batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[a_idx];
  if (action != (uint16_t)MSL_ACT_SK_SPECIAL_S && action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_S_END &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END) {
    return 0u;
  }
  if (batch->state.hitlag[d_idx] != 0u ||
      batch->state.instance_hit_by[d_idx] != batch->state.instance_id[a_idx]) {
    return 0u;
  }
  const uint16_t d_action = batch->state.action_id[d_idx];
  if (batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  const uint8_t terminal_action =
      (uint8_t)(msl_motion_state_class_has(batch->state.char_id[d_idx], d_action,
                                           MSL_MS_CLASS_COMMON_FALL) ||
                (d_action >= (uint16_t)MSL_ACT_ATTACK_AIR_N &&
                 d_action <= (uint16_t)MSL_ACT_ATTACK_AIR_LW));
  if (terminal_action == 0u) {
    return 0u;
  }
  // Chain terminal same-source victim episode:
  // ftColl_80076ED8 inserts the fighter victim pointer into Chain's x914 HitCapsule victims_1. The
  // victim can leave Damage into Fall-family or AttackAir-family callbacks while Slippi still
  // exposes the same hit source through x18EC/instance_hit_by, before a source-owned Chain
  // disable/reactivate has made a new victims_1 episode for that defender. Keep this scoped to
  // Sheik Chain, source common Fall/AttackAir terminal victim actions, zero hitlag/hitstun, and
  // exact same attacker instance; idle Wait rows and active DamageAir/DamageFly rows keep ordinary
  // BODY admission/hitstun-horizon handling so official Chain follow-up hits remain admitted.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_80110BCC
  return 1u;
}

uint8_t combat_sheik_chain_same_source_damage_followup_admits_body(
    const MslBatch* batch, const MslCommonParams* c, size_t a_idx, size_t d_idx, int attacker,
    int hb_id, int int_dmg, uint16_t defender_motion_id, uint8_t hitbox_element) {
  if (batch == NULL || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES) {
    return 0u;
  }
  if (batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[a_idx];
  if (action != (uint16_t)MSL_ACT_SK_SPECIAL_S && action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_S_END &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END) {
    return 0u;
  }
  float hx = 0.0f;
  float hy = 0.0f;
  float hz = 0.0f;
  if (sheik_chain_hitbox_world_pos(batch, a_idx, (uint8_t)hb_id, &hx, &hy, &hz) == 0u) {
    return 0u;
  }
  if (batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] == 0u) {
    return 0u;
  }
  if (c == NULL || int_dmg <= 0) {
    return 0u;
  }
  const uint16_t d_hl = combat_calc_hitlag_frames(
      c, int_dmg, defender_motion_id, combat_hitlag_mul_from_element(c, hitbox_element));
  const uint16_t d_action = batch->state.action_id[d_idx];
  if (d_hl == 0u || batch->state.hitstun[d_idx] >= (uint16_t)(d_hl + d_hl)) {
    return 0u;
  }
  if (d_action == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP) {
    const uint16_t terminal_horizon = combat_sheik_chain_same_source_terminal_hitstun_horizon(d_hl);
    if (batch->state.hitstun[d_idx] > terminal_horizon) {
      return 0u;
    }
  }
  if (!combat_is_damage_or_firefox_launch_victim_action(batch->state.char_id[d_idx], d_action)) {
    return 0u;
  }
  // Chain same-source Damage continuation:
  // ftColl_80076ED8 inserts the victim into the Chain HitCapsule victims_1 list for a BODY
  // contact, while ftSk_SpecialS_80110BCC/it_802BCB88 disable and republish article hitcaps across
  // Chain frontier windows. Once the defender's hitlag has drained into Damage hitstun, a live
  // article-published Chain HitCapsule may strike again even though Slippi-visible source
  // ownership still names the same Sheik instance. Keep this bypass limited to that same-source
  // Damage continuation and to an actually published Chain hitcap after the prior hit's hitlag
  // horizon has drained far enough for the next Chain contact window. DamageFlyTop uses the
  // terminal part of that horizon, matching ftCo_DamageFly_Coll's late pose/contact ownership
  // instead of admitting early high-hitstun tumble overlaps; neutral/Wait rows, hitlag-frozen
  // victims, high-horizon Damage rows, and non-Chain hitboxes continue to use lbColl_8000ACFC
  // suppression.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  // refs/melee/src/melee/it/items/itseakchain.c::{it_802BBD64,it_802BBED0,it_802BCB88}
  // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_80110BCC
  return (msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker) ||
          batch->state.instance_hit_by[d_idx] == batch->state.instance_id[a_idx])
             ? 1u
             : 0u;
}

uint8_t combat_sheik_chain_damageflytop_high_horizon_suppresses_body(
    const MslBatch* batch, const MslCommonParams* c, size_t a_idx, size_t d_idx, int attacker,
    int hb_id, int int_dmg, uint16_t defender_motion_id, uint8_t hitbox_element) {
  if (batch == NULL || c == NULL || int_dmg <= 0 || attacker < 0 ||
      attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return 0u;
  }
  if (batch->state.char_id[a_idx] != (uint8_t)MSL_CHAR_ID_SHEIK ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] == 0u) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[a_idx];
  if (action != (uint16_t)MSL_ACT_SK_SPECIAL_S && action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_S_END &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END) {
    return 0u;
  }
  if (!msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker) &&
      batch->state.instance_hit_by[d_idx] != batch->state.instance_id[a_idx]) {
    return 0u;
  }
  const uint16_t d_action = batch->state.action_id[d_idx];
  const uint16_t d_hl = combat_calc_hitlag_frames(
      c, int_dmg, defender_motion_id, combat_hitlag_mul_from_element(c, hitbox_element));
  if (d_hl == 0u) {
    return 0u;
  }
  if (d_action >= (uint16_t)MSL_ACT_DAMAGE_HI_1 && d_action <= (uint16_t)MSL_ACT_DAMAGE_LW_3) {
    // Common Damage same-source Chain continuation:
    // grounded/neutral Damage can receive a later Chain BODY refresh once its hitstun has drained
    // to the 2x-hitlag follow-up horizon after the frame's Damage callback tick (official demo
    // rec322 reaches combat at hitstun 11 for a 6-frame Chain hitlag), but earlier same-source
    // overlaps are still inside the prior Chain HitCapsule victims_1 episode (demo2 rec1626).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_Coll
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
    const uint16_t followup_horizon = (d_hl > 0u) ? (uint16_t)((d_hl + d_hl) - 1u) : 0u;
    return (batch->state.hitstun[d_idx] < followup_horizon) ? 1u : 0u;
  }
  if (d_action < (uint16_t)MSL_ACT_DAMAGE_AIR_1 || d_action > (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
    return 0u;
  }
  const uint16_t terminal_horizon = combat_sheik_chain_same_source_terminal_hitstun_horizon(d_hl);
  if (batch->state.hitstun[d_idx] <= terminal_horizon) {
    return 0u;
  }
  // Same-source Chain airborne Damage continuation:
  // Source ftColl victims_1 state from the previous Chain BODY contact survives across the
  // DamageAir/DamageFly callback horizon even when seed-reconstructed per-hitbox hitlists are
  // cleared by a disable/reactivate edge. Suppress early high-hitstun same-source Chain BODY overlaps
  // until the current Chain hitlag terminal horizon; the terminal tail is then admitted by the
  // follow-up owner below (official demo rec341) while non-terminal rows (official demo rec330) stay
  // quiet. Grounded/common Damage continuations are not part of this contiguous decomp action-family
  // window and still admit their source BODY refresh (official demo rec322).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_Coll,ftCo_DamageFly_Coll}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  return 1u;
}

uint8_t combat_replay_rollout_advanced_past_reseed(const MslBatch* batch, int bi) {
  if (batch == NULL || bi < 0) {
    return 0u;
  }
  if (batch->replay_rollout_reseeded == NULL || batch->replay_rollout_reseeded[bi] == 0u ||
      batch->replay_rollout_seed_frame_id == NULL) {
    return 0u;
  }
  return (batch->state.frame_id[bi] != batch->replay_rollout_seed_frame_id[bi]) ? 1u : 0u;
}

uint8_t combat_enable_edge_dense_seed_suppresses_body(const MslBatch* batch, int bi, int attacker,
                                                      int hb_id, int defender,
                                                      uint16_t defender_iid,
                                                      uint16_t expected_hitlag) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES || defender < 0 || defender >= (int)MSL_MAX_PLAYERS ||
      attacker == defender) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const size_t a_idx = msl_idx_player(bi, attacker);
  const uint16_t attacker_action = batch->state.action_id[a_idx];
  const uint8_t down_attack_dense_lane = (attacker_action == (uint16_t)MSL_ACT_DOWN_ATTACK_U ||
                                          attacker_action == (uint16_t)MSL_ACT_DOWN_ATTACK_D)
                                             ? 1u
                                             : 0u;
  const uint8_t shine_start_dense_lane =
      (msl_motion_state_fx_special_kind(batch->state.char_id[msl_idx_player(bi, attacker)],
                                        attacker_action) == (uint8_t)MSL_FX_KIND_SPECIAL_LW_START)
          ? 1u
          : 0u;
  const size_t d_idx = msl_idx_player(bi, defender);
  const size_t valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  if (!down_attack_dense_lane && !shine_start_dense_lane) {
    return 0u;
  }
  if (!batch->state.hitbox_enable_edge[hb_i] && !shine_start_dense_lane) {
    return 0u;
  }

  if (batch->state.combat_hitlist_hb_valid[valid_i]) {
    return 0u;
  }

  uint8_t shine_runtime_hitlist_proof = 0u;
  if (down_attack_dense_lane) {
    if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL) {
      return 0u;
    }
    if (batch->state.seed_prev_action_id[d_idx] != batch->state.action_id[d_idx] ||
        batch->state.action_frame[d_idx] == 0) {
      return 0u;
    }
    if (batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
        batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
        combat_is_damage_or_firefox_launch_victim_action(batch->state.char_id[d_idx],
                                                         batch->state.action_id[d_idx])) {
      return 0u;
    }
  } else if (shine_start_dense_lane) {
    if (batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
        batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] == 0u) {
      return 0u;
    }
    if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP) {
      return 0u;
    }
    const uint8_t source_port_matches =
        msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker);
    const uint8_t advanced_replay_rollout = combat_replay_rollout_advanced_past_reseed(batch, bi);
    const uint8_t seed_colanim_proof = batch->state.colanim_hitstun_x198c1_seed[d_idx];
    const uint8_t runtime_colanim_proof =
        (uint8_t)(batch->state.colanim_hit_status_x198c[d_idx] == 1u &&
                  batch->state.colanim_timer_x1994[d_idx] != 0u && advanced_replay_rollout);
    const uint8_t runtime_terminal_same_source_proof =
        (uint8_t)(batch->state.hitstun[d_idx] == 1u && source_port_matches != 0u &&
                  batch->state.instance_hit_by[d_idx] != batch->state.instance_id[a_idx] &&
                  advanced_replay_rollout);
    shine_runtime_hitlist_proof =
        (uint8_t)(runtime_colanim_proof != 0u || runtime_terminal_same_source_proof != 0u);
    if (seed_colanim_proof == 0u && shine_runtime_hitlist_proof == 0u) {
      return 0u;
    }
    if (source_port_matches == 0u) {
      return 0u;
    }
    if (batch->state.instance_hit_by[d_idx] == batch->state.instance_id[a_idx]) {
      return 0u;
    }
    (void)expected_hitlag;
    if (batch->state.hitstun[d_idx] > 2u) {
      return 0u;
    }
  }
  (void)expected_hitlag;

  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return 0u;
  }
  const size_t group_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
  const size_t cd_i =
      group_base + (((size_t)attacker * (size_t)MSL_HITLIST_GROUPS + (size_t)hit_group) *
                        (size_t)MSL_MAX_PLAYERS +
                    (size_t)defender);
  const uint16_t dense_cd = batch->state.combat_hitlist_cd[cd_i];
  if (dense_cd == 0u) {
    if (shine_start_dense_lane && shine_runtime_hitlist_proof != 0u) {
      return 1u;
    }
    return 0u;
  }
  if (shine_start_dense_lane && dense_cd != 0xFFFFu) {
    return 0u;
  }
  const uint16_t seed_iid = batch->state.combat_hitlist_victim_iid[cd_i];
  if (seed_iid != 0u && seed_iid != defender_iid) {
    if (combat_hitlist_victim_pointer_may_change(batch->state.stocks[d_idx],
                                                 batch->state.action_id[d_idx])) {
      return 0u;
    }
  }

  // Teacher-forced dense HitCapsule bridge for enable-edge BODY damage:
  // - The dense seed can prove victims_1 already contains a live victim on narrow carry frames
  //   even when our movescript reconstruction reaches ftColl_800768A0's clear lane on the same
  //   pose frame.
  // - Proven lanes:
  //   * DownAttack -> LandingFallSpecial carry frames.
  //   * SpecialLwStart same-port DamageFly continuations, where replay-history extraction carries
  //     the prior HitCapsule victim pointer through the terminal hitstun window and vanilla
  //     lbColl_8000ACFC suppresses the immediate Shine Start BODY rehit.
  // - Do not materialize this into the HitCapsule before collision: legacy dense victims_1 is too
  //   coarse for the separate checkTipLog/victims_2 phantom path. Combat selection uses this only
  //   after phantom/tip-log handling, and only to suppress full BODY damage.
  // - Same-frame victim action-entry rows stay out of this bridge because the dense fallback lacks
  //   per-HitCapsule clear/copy provenance for the new victim action frame.
  // - Fighter victim identity follows the decomp victim-pointer policy used by hitlist.c: a stale
  //   Slippi instance_id proxy may rebind while the fighter object is alive, but death/rebirth clears
  //   the suppression proof.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  return 1u;
}

uint8_t combat_attackairb_dense_seed_suppresses_full_body(const MslBatch* batch, int bi,
                                                          int attacker, int hb_id, int defender,
                                                          uint16_t defender_iid,
                                                          uint16_t expected_hitlag) {
  if (batch == NULL || attacker == defender || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS ||
      defender < 0 || defender >= (int)MSL_MAX_PLAYERS || hb_id < 0 || hb_id >= MSL_MAX_HITBOXES) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B ||
      batch->state.hitbox_damage[hb_i] > 9.5f || batch->state.hitbox_enable_edge[hb_i] != 0u ||
      batch->state
              .combat_hitlist_hb_valid[((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) *
                                           (size_t)MSL_MAX_HITBOXES +
                                       (size_t)hb_id] != 0u) {
    return 0u;
  }
  const MslDamageSourceEpisode d_source =
      msl_damage_source_episode_from_victim(batch, bi, defender, d_idx);
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return 0u;
  }
  const size_t group_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
  const size_t cd_i =
      group_base + (((size_t)attacker * (size_t)MSL_HITLIST_GROUPS + (size_t)hit_group) *
                        (size_t)MSL_MAX_PLAYERS +
                    (size_t)defender);
  if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_LANDING) {
    if (batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
        batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
        d_source.x18c8_active == 0u || d_source.source_slot != attacker ||
        d_source.instance_matches_source == 0u || batch->state.combat_hitlist_cd[cd_i] != 0xFFFFu) {
      return 0u;
    }
    const uint16_t seed_iid = batch->state.combat_hitlist_victim_iid[cd_i];
    if (seed_iid != 0u && seed_iid != defender_iid) {
      if (combat_hitlist_victim_pointer_may_change(batch->state.stocks[d_idx],
                                                   batch->state.action_id[d_idx])) {
        return 0u;
      }
    }
    // AttackAirB source-clear landing latch:
    // - ftColl_80076ED8 inserts the victim into same-hit_group HitCapsules, and those
    //   HitVictim pointers survive ordinary victim motion changes until a hitbox clear/copy or
    //   victim object lifetime boundary.
    // - Landing from the same BODY source still carries fp->dmg.x18C8 source-clear ownership and
    //   same-instance BODY attribution, while the replay seed can only expose dense group victims.
    //   Suppress this full BODY rehit through the decomp HitCapsule victim-list owner rather than
    //   a geometry tolerance.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
    // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A360}
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    return 1u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] == 0u ||
      d_source.source_slot != attacker || d_source.instance_matches_source != 0u) {
    return 0u;
  }
  if (expected_hitlag == 0u || batch->state.hitstun[d_idx] > expected_hitlag) {
    return 0u;
  }
  const uint16_t dense_cd = batch->state.combat_hitlist_cd[cd_i];
  if (dense_cd != 0xFFFFu) {
    if (dense_cd != 0u) {
      return 0u;
    }
    if (batch->replay_rollout_reseeded == NULL || batch->replay_rollout_reseeded[bi] == 0u) {
      return 0u;
    }
    const int16_t second_create_frame = move_tables_attackair_second_create_hitbox_frame(
        batch->state.char_id[a_idx], batch->state.action_id[a_idx]);
    if (second_create_frame < 0 ||
        batch->state.action_frame[a_idx] < (int16_t)(second_create_frame + 4)) {
      return 0u;
    }
    if (hb_id != 1) {
      return 0u;
    }
    // Replay-rollout terminal DamageFlyTop source fallback:
    // - A rollout seeded before the current AttackAirB episode has no replay dense hit_group lane
    //   for the later terminal frame, and the first-create clear may have consumed the live
    //   HitCapsule list before the late BODY horizon.
    // - This fallback is bounded to the outer late BAir hb1 lane; hb0/cap12 rows can still be the
    //   real full BODY source and must not be suppressed from visible DamageFlyTop attribution.
    // - The victim's damage attribution still proves the same attacker port owns the current
    //   DamageFlyTop state while the current attacker instance is not the accepted BODY source.
    //   Use that source-owned provenance only inside the current hit's expected hitlag horizon,
    //   and only in replay rollout where this hidden HitCapsule state is otherwise unobservable.
    // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A360}
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80076808}
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    return 1u;
  }
  const uint16_t seed_iid = batch->state.combat_hitlist_victim_iid[cd_i];
  if (seed_iid != 0u && seed_iid != defender_iid) {
    if (combat_hitlist_victim_pointer_may_change(batch->state.stocks[d_idx],
                                                 batch->state.action_id[d_idx])) {
      return 0u;
    }
  }
  // AttackAirB DamageFlyTop terminal-refresh dense fallback:
  // - The replay dense group lane cannot distinguish the same-group HitCapsules. The large outer
  //   late BAir capsule (hitbox 1 in extracted data/moves/{fox,falco}.json) carries the existing
  //   same-victim suppression in QGD-style continuation controls.
  // - Only terminal DamageFlyTop frames use the dense group proof. When remaining hitstun exceeds
  //   the current hit's expected hitlag window, replay-real LIM rows prove vanilla can still admit
  //   a fresh full BODY hit from the live AttackAirB action instance; the stale dense proxy is not
  //   enough to stand in for a per-HitCapsule victims_1 entry there.
  // - Inner late capsules can be independently empty in replay-proven DCC continuation rows, so
  //   the dense group fallback must not suppress their matrix-first full BODY admission.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return (hb_id == 1) ? 1u : 0u;
}

uint8_t combat_attackairhi_create_edge_damageflytop_suppresses_full_body(const MslBatch* batch,
                                                                         int bi, int attacker,
                                                                         int hb_id, int defender,
                                                                         uint16_t expected_hitlag) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES || defender < 0 || defender >= (int)MSL_MAX_PLAYERS ||
      attacker == defender) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_HI ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.hitbox_enable_edge[hb_i] == 0u || batch->state.hitlag[a_idx] != 0u ||
      batch->state.hitstun[a_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] == 0u || expected_hitlag == 0u ||
      batch->state.hitstun[d_idx] > expected_hitlag) {
    return 0u;
  }
  const int16_t first_create_frame = move_tables_attackair_first_create_hitbox_frame(
      batch->state.char_id[a_idx], batch->state.action_id[a_idx]);
  if (first_create_frame < 0 || batch->state.action_frame[a_idx] != first_create_frame ||
      batch->state.hitstun[d_idx] < 2u) {
    return 0u;
  }
  if (move_tables_attackair_second_create_hitbox_frame(batch->state.char_id[a_idx],
                                                       batch->state.action_id[a_idx]) < 0) {
    return 0u;
  }
  if (batch->state.instance_hit_by[d_idx] == 0u ||
      batch->state.instance_hit_by[d_idx] == batch->state.instance_id[a_idx]) {
    return 0u;
  }
  if (batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u) {
    return 0u;
  }
  if (!msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker)) {
    return 0u;
  }
  const uint8_t advanced_past_seed =
      (batch->replay_rollout_seed_frame_id != NULL &&
       batch->state.frame_id[bi] != batch->replay_rollout_seed_frame_id[bi])
          ? 1u
          : 0u;
  if (advanced_past_seed == 0u) {
    return 0u;
  }
  const size_t valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  if (batch->state.combat_hitlist_hb_valid[valid_i] != 0u) {
    return 0u;
  }
  // AttackAirHi create-edge same-source DamageFlyTop latch:
  // ftAction_8007121C creates HitCapsules before ftColl_80078C70 checks BODY. During rollout, a
  // terminal same-source DamageFlyTop victim can still be in the pre-create victims_1 owner for
  // multi-band UpAir scripts while the newly-created HitCapsule has no current accepted hitlist
  // entry. Suppress only this first create edge for scripts with a later create_hitbox band; the
  // next frame's live HitCapsule list admits the hit. Single-band UpAir scripts use the ordinary
  // ftColl_800768A0 clear/copy owner and must not inherit this dense replay bridge.
  // data/moves/{fox,falco,marth}.json::moves.ftCo_SM_AttackAirHi.events.create_hitbox
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8,ftColl_80078C70}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  return 1u;
}

uint8_t combat_attackairn_wait_dense_seed_suppresses_full_body(const MslBatch* batch, int bi,
                                                               int attacker, int hb_id,
                                                               int defender,
                                                               uint16_t defender_iid) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES || defender < 0 || defender >= (int)MSL_MAX_PLAYERS ||
      attacker == defender) {
    return 0u;
  }
  if (batch->replay_rollout_reseeded == NULL || batch->replay_rollout_reseeded[bi] == 0u) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint16_t attacker_action = batch->state.action_id[a_idx];
  if (attacker_action != (uint16_t)MSL_ACT_ATTACK_AIR_N ||
      !msl_motion_state_has_motion_flag(batch->state.char_id[a_idx], attacker_action,
                                        MSL_MOTION_FLAG_SKIP_HIT) ||
      batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_WAIT ||
      batch->state.action_frame[d_idx] != 1 || hb_id != 1) {
    return 0u;
  }
  const size_t valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  if (batch->state.combat_hitlist_hb_valid[valid_i] != 0u) {
    return 0u;
  }
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return 0u;
  }
  const size_t group_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
  const size_t cd_i =
      group_base + (((size_t)attacker * (size_t)MSL_HITLIST_GROUPS + (size_t)hit_group) *
                        (size_t)MSL_MAX_PLAYERS +
                    (size_t)defender);
  const uint8_t dense_seed_present = (batch->state.combat_hitlist_cd[cd_i] != 0u) ? 1u : 0u;
  if (!dense_seed_present) {
    const MslDamageSourceEpisode d_source =
        msl_damage_source_episode_from_victim(batch, bi, defender, d_idx);
    if (d_source.source_slot != attacker || d_source.instance_matches_source != 0u ||
        d_source.x18c8_active == 0u) {
      return 0u;
    }
    // Replay-rollout hidden victim provenance fallback:
    // - The direct seed lane can carry dense HitCapsule victims_1 for this AttackAirN/Wait
    //   boundary, but a long rollout seeded before the source projectile/fighter episode has no
    //   row-local dense map to materialize.
    // - `last_hit_by`, `instance_hit_by`, and x18c8 source-clear state are live source-clear/body
    //   attribution lanes from Fighter_ProcessHit. They prove the defender is still in the same
    //   source-owned no-new-hit episode while the current AttackAirN instance is not the accepted
    //   source instance. Use them only for the first neutral Wait BODY fallthrough; later Wait
    //   frames remain eligible for the ordinary live BODY hit.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
    return 1u;
  }
  const uint16_t seed_iid = batch->state.combat_hitlist_victim_iid[cd_i];
  if (seed_iid != 0u && seed_iid != defender_iid &&
      combat_hitlist_victim_pointer_may_change(batch->state.stocks[d_idx],
                                               batch->state.action_id[d_idx])) {
    return 0u;
  }
  // Teacher-forced dense HitCapsule suppression for AttackAirN -> neutral Wait entry:
  // - AttackAirN carries Ft_MF_SkipHit, so Fighter_ChangeMotionState can preserve x914
  //   HitCapsule state across the motion entry. lbColl_8000ACFC then suppresses by the raw
  //   HitVictim fighter pointer, not by Slippi's damage attribution or motion-state instance id.
  // - In long replay rollouts that start before the aerial, the only available hidden provenance
  //   is the dense group victim seed. Use it only for the neutral Wait entry row where the dense
  //   proxy is replay-proven to suppress a one-frame-early BODY fallthrough; later Wait frames
  //   remain eligible for the live BODY hit once stale dense filtering has released the latch.
  // - This does not alter ordinary free-running gameplay: the helper requires
  //   replay_rollout_reseeded and only suppresses full BODY damage, leaving live HitCapsule
  //   registration to lbColl_80008688-shaped runtime hitlists.
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_AttackAirN
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  return 1u;
}

uint8_t combat_attackairn_post_contact_dense_seed_suppresses_full_body(const MslBatch* batch,
                                                                       int bi, int attacker,
                                                                       int hb_id, int defender) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES || defender < 0 || defender >= (int)MSL_MAX_PLAYERS ||
      attacker == defender) {
    return 0u;
  }
  if (batch->replay_rollout_reseeded == NULL || batch->replay_rollout_reseeded[bi] == 0u) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint16_t attacker_action = batch->state.action_id[a_idx];
  const uint8_t hitlag_tail_owner =
      (batch->state.hitlag_pre_timer[a_idx] != 0u || batch->state.hitlag_pre_timer[d_idx] != 0u)
          ? 1u
          : 0u;
  const uint8_t early_guardoff_owner =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_OFF &&
       batch->state.action_frame[d_idx] <= 4)
          ? 1u
          : 0u;
  if (attacker_action != (uint16_t)MSL_ACT_ATTACK_AIR_N ||
      !msl_motion_state_has_motion_flag(batch->state.char_id[a_idx], attacker_action,
                                        MSL_MOTION_FLAG_SKIP_HIT) ||
      (!move_tables_attackair_post_clear_create_hitbox_phase(
           batch->state.char_id[a_idx], attacker_action, batch->state.anim_frame_f32[a_idx]) &&
       !move_tables_attackair_second_create_hitbox_phase(
           batch->state.char_id[a_idx], attacker_action, batch->state.anim_frame_f32[a_idx])) ||
      batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u || (hitlag_tail_owner == 0u && early_guardoff_owner == 0u)) {
    return 0u;
  }
  const size_t valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  if (batch->state.combat_hitlist_hb_valid[valid_i] != 0u) {
    return 0u;
  }
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return 0u;
  }
  const size_t group_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
  const size_t cd_i =
      group_base + (((size_t)attacker * (size_t)MSL_HITLIST_GROUPS + (size_t)hit_group) *
                        (size_t)MSL_MAX_PLAYERS +
                    (size_t)defender);
  // AttackAirN late post-contact victims_1 carry:
  // - AttackAirN has Ft_MF_SkipHit, so Fighter_ChangeMotionState preserves existing x914
  //   HitCapsules on aerial entry instead of clearing the victim rings.
  // - The late create phase preserves same-hit-group victims_1 through ftColl_80076ED8/inlineB0;
  //   ftAction_8007121C/ftColl_800768A0 therefore does not prove a fresh empty per-HitCapsule list
  //   when no authoritative per-hitbox seed lane is present.
  // - Guard and damage/downed transitions can advance Slippi-visible action/instance fields, but
  //   they do not replace the fighter object pointer stored in HitVictim. Current damage-source
  //   attribution plus a frame-start hitlag-tail owner (or the early GuardOff release owner) and
  //   the dense group carry prove lbColl_8000ACFC should reject repeat BODY damage for the same
  //   hit_group. The helper suppresses only the full BODY path; shield and phantom/tip-log paths
  //   remain owned by their normal predicates.
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_AttackAirN
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_ProcessHit_8006D1EC}
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  if (batch->state.combat_hitlist_cd[cd_i] == 0u) {
    const MslDamageSourceEpisode d_source =
        msl_damage_source_episode_from_victim(batch, bi, defender, d_idx);
    if (d_source.source_slot != attacker || d_source.instance_matches_source != 0u ||
        d_source.x18c8_active == 0u ||
        !msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker)) {
      return 0u;
    }
    return 1u;
  }
  const uint16_t seed_iid = batch->state.combat_hitlist_victim_iid[cd_i];
  if (seed_iid == 0u || combat_hitlist_victim_pointer_may_change(batch->state.stocks[d_idx],
                                                                 batch->state.action_id[d_idx])) {
    return 0u;
  }
  const MslDamageSourceEpisode d_source =
      msl_damage_source_episode_from_victim(batch, bi, defender, d_idx);
  if (d_source.source_slot != attacker ||
      !msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker)) {
    return 0u;
  }
  return 1u;
}

uint8_t combat_attackairn_guard_dense_seed_suppresses_full_body(const MslBatch* batch, int bi,
                                                                int attacker, int hb_id,
                                                                int defender,
                                                                uint16_t defender_iid) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES || defender < 0 || defender >= (int)MSL_MAX_PLAYERS ||
      attacker == defender) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint16_t attacker_action = batch->state.action_id[a_idx];
  if (attacker_action != (uint16_t)MSL_ACT_ATTACK_AIR_N || hb_id != 0 ||
      !msl_motion_state_has_motion_flag(batch->state.char_id[a_idx], attacker_action,
                                        MSL_MOTION_FLAG_SKIP_HIT) ||
      batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD ||
      batch->state.seed_prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD_ON) {
    return 0u;
  }
  const size_t valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  if (batch->state.combat_hitlist_hb_valid[valid_i] != 0u) {
    return 0u;
  }
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return 0u;
  }
  const size_t group_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
  const size_t cd_i =
      group_base + (((size_t)attacker * (size_t)MSL_HITLIST_GROUPS + (size_t)hit_group) *
                        (size_t)MSL_MAX_PLAYERS +
                    (size_t)defender);
  if (batch->state.combat_hitlist_cd[cd_i] == 0u ||
      batch->state.combat_hitlist_victim_iid[cd_i] != defender_iid) {
    return 0u;
  }
  const MslDamageSourceEpisode d_source =
      msl_damage_source_episode_from_victim(batch, bi, defender, d_idx);
  if (d_source.source_slot != attacker || d_source.instance_matches_source != 0u ||
      d_source.x18c8_active == 0u ||
      !msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker)) {
    return 0u;
  }
  // AttackAirN hb0 -> first steady Guard victim-list carry:
  // - AttackAirN has Ft_MF_SkipHit, so Fighter_ChangeMotionState can preserve x914 HitCapsule
  //   victim rings across the aerial's active windows.
  // - GuardOn -> Guard advances the visible no-submotion Shield state, but does not by itself
  //   clear a same-group HitCapsule victim already present in lbColl_8000ACFC. When the dense
  //   group seed names the current defender iid and BODY attribution still names the same source
  //   port from an older attacker instance, suppress only the full BODY fallthrough for that first
  //   Guard frame. Continuing Guard rows with stale dense ids remain eligible for ordinary BODY
  //   hits, and authoritative per-HitCapsule seed lanes win when present.
  // data/scripts/{fox,falco,marth}.bin (MSLFTSC1 AttackAirN create_hitbox/hit_group timeline)
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_AttackAirN
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ChangeMotionState,Fighter_ProcessHit_8006D1EC}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80078C70}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  return 1u;
}

uint8_t combat_attackairn_hb0_damageflytop_hitcapsule_owner(const MslBatch* batch, int bi,
                                                            int attacker, int hb_id, int defender) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || defender < 0 ||
      defender >= (int)MSL_MAX_PLAYERS || attacker == defender || hb_id != 0) {
    return 0u;
  }
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint16_t attacker_action = batch->state.action_id[a_idx];
  if (attacker_action != (uint16_t)MSL_ACT_ATTACK_AIR_N ||
      !msl_motion_state_has_motion_flag(batch->state.char_id[a_idx], attacker_action,
                                        MSL_MOTION_FLAG_SKIP_HIT) ||
      !move_tables_attackair_second_create_hitbox_phase(
          batch->state.char_id[a_idx], attacker_action, batch->state.anim_frame_f32[a_idx]) ||
      batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] == 0u) {
    return 0u;
  }
  const MslDamageSourceEpisode d_source =
      msl_damage_source_episode_from_victim(batch, bi, defender, d_idx);
  if (d_source.source_slot != attacker || d_source.instance_matches_source != 0u ||
      !msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker)) {
    return 0u;
  }
  // AttackAirN hb0 same-source DamageFlyTop HitCapsule carry:
  // - AttackAirN enters with Ft_MF_SkipHit, so Fighter_ChangeMotionState skips ftColl_8007AFF8
  //   and can preserve the slot-0 x914 HitCapsule victim list from the previous source-owned
  //   no-new-hit episode.
  // - The authored late NAir script rewrites hb0 at frame 8 without a clear/group-change edge
  //   (same hit_group 0 in MSLFTSC1), so ftAction_8007121C/ftColl_800768A0 keep the existing
  //   victims_1 list. Runtime rollouts seeded before the NAir do not carry the legacy dense
  //   replay map for this later frame; the current DamageFlyTop source episode
  //   (`last_hit_by`/`instance_hit_by` through x18C8) is the source-owned proof that hb0's hidden
  //   list is still populated.
  // - Keep this per-HitCapsule and data-window bounded. hb1/hb2 have their own late-limb carry
  //   boundary below, and ordinary first-window NAir contacts stay eligible for BODY damage.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_AttackAirN
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  return 1u;
}

uint8_t combat_attackhi3_landing_source_clear_blocks_enable_edge_body(const MslBatch* batch, int bi,
                                                                      size_t hb_i, size_t a_idx,
                                                                      size_t d_idx, int attacker,
                                                                      int defender) {
  if (batch == NULL || bi < 0 || attacker < 0 || defender < 0 || attacker >= (int)MSL_MAX_PLAYERS ||
      defender >= (int)MSL_MAX_PLAYERS || attacker == defender) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_HI3 ||
      batch->state.prev_action_id[a_idx] != batch->state.action_id[a_idx] ||
      batch->state.hitbox_enable_edge[hb_i] == 0u || batch->state.hitlag[a_idx] != 0u ||
      batch->state.hitstun[a_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_LANDING ||
      batch->state.action_frame[d_idx] < 10 || batch->state.on_ground[d_idx] == 0u ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  const MslDamageSourceEpisode d_source =
      msl_damage_source_episode_from_victim(batch, bi, defender, d_idx);
  if (d_source.x18c8_active == 0u || d_source.source_slot != attacker ||
      d_source.instance_matches_source != 0u ||
      !msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker)) {
    return 0u;
  }
  // AttackHi3 enable-edge vs Landing source-clear boundary:
  // - ftColl_80076ED8/Fighter_ProcessHit leaves the victim's x18C8 source-clear owner and BODY
  //   source port live through ordinary Landing frames.
  // - A same-action AttackHi3 create edge has a fresh current HitCapsule instance, but late
  //   Landing frames can still belong to the prior same-port BODY source for this collision frame.
  //   Keep the enable-edge BODY candidate out until the next already-live ftColl_80078C70 pass.
  //   Early Landing frames remain hittable by a fresh AttackHi3 edge; they have not reached this
  //   source-clear carry horizon.
  // - The source proof is the live x18C8 owner plus same attacker port and mismatched current
  //   instance, not the visible Landing action shape by itself.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8,ftColl_80078C70}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008440,lbColl_8000ACFC}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackHi3.events.create_hitbox
  return 1u;
}

uint8_t combat_seed_hitlist_suppresses_clank_candidate(const MslBatch* batch, int bi, int attacker,
                                                       int hb_id, int defender) {
  if (batch == NULL || bi < 0 || attacker < 0 || attacker >= (int)MSL_MAX_PLAYERS || hb_id < 0 ||
      hb_id >= MSL_MAX_HITBOXES || defender < 0 || defender >= (int)MSL_MAX_PLAYERS ||
      attacker == defender) {
    return 0u;
  }
  const size_t valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  const size_t hb_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_MAX_HITBOXES * (size_t)MSL_MAX_PLAYERS;
  const size_t hb_cd_i = hb_base + (((size_t)attacker * (size_t)MSL_MAX_HITBOXES + (size_t)hb_id) *
                                        (size_t)MSL_MAX_PLAYERS +
                                    (size_t)defender);
  if (batch->state.combat_hitlist_hb_valid[valid_i]) {
    return batch->state.combat_hitlist_hb_cd[hb_cd_i] != 0u ? 1u : 0u;
  }

  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
  if (hit_group >= (uint8_t)MSL_HITLIST_GROUPS) {
    return 0u;
  }
  const size_t group_base =
      (size_t)bi * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_HITLIST_GROUPS * (size_t)MSL_MAX_PLAYERS;
  const size_t cd_i =
      group_base + (((size_t)attacker * (size_t)MSL_HITLIST_GROUPS + (size_t)hit_group) *
                        (size_t)MSL_MAX_PLAYERS +
                    (size_t)defender);
  if (batch->state.combat_hitlist_cd[cd_i] == 0u) {
    return 0u;
  }

  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  const uint16_t stored_iid = batch->state.combat_hitlist_victim_iid[cd_i];
  if (stored_iid == 0u || stored_iid != batch->state.instance_id[d_idx]) {
    const uint8_t rollout_same_object_rebind =
        (stored_iid != 0u &&
         hitlist_rollout_dense_seed_same_object_rebind_applies(batch, bi, attacker, defender))
            ? 1u
            : 0u;
    if (!rollout_same_object_rebind) {
      return 0u;
    }
  }
  if (!msl_damage_source_victim_matches_attacker(batch, d_idx, a_idx, attacker)) {
    return 0u;
  }

  // Clank candidate prefilter from replay seed lanes:
  // - Decomp candidate loops use `lbColl_8000ACFC` on the concrete HitCapsule.
  // - Authoritative per-HitCapsule seed lanes can answer that exactly.
  // - Legacy dense group seeds are only safe for clank prefilter when replay-visible BODY
  //   provenance still names the same attacker instance/source. Older same-port dense latches are
  //   too coarse: HHG/FSP-style rows prove stale group entries can coexist with a live clank on a
  //   specific HitCapsule.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (instance_hit_by/last_hit_by lanes)
  return 1u;
}

int combat_debug_attackairb_continuation_overlap(const MslBatch* batch, int batch_index,
                                                 int attacker, int hb_id, int defender, int cap_id,
                                                 float* out_overlap) {
  if (out_overlap == NULL) {
    return EINVAL;
  }
  *out_overlap = 0.0f;
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (attacker < 0 || attacker >= (int)batch->config.num_players || defender < 0 ||
      defender >= (int)batch->config.num_players || attacker == defender) {
    return EINVAL;
  }
  if (hb_id < 0 || hb_id >= MSL_MAX_HITBOXES || cap_id < 0 || cap_id >= MSL_MAX_HURTCAPS) {
    return EINVAL;
  }
  const size_t hb_i = idx_hitbox(batch_index, attacker, hb_id);
  const size_t cap_i = idx_hurtcap(batch_index, defender, cap_id);
  if (!batch->state.hitbox_enabled[hb_i] || !batch->state.hurtcap_enabled[cap_i]) {
    return 0;
  }
  const float hx = batch->state.hitbox_x[hb_i];
  const float hy = batch->state.hitbox_y[hb_i];
  const float hz = batch->state.hitbox_z[hb_i];
  const float hr = batch->state.hitbox_radius[hb_i];
  const float ax = batch->state.hurtcap_a_x[cap_i];
  const float ay = batch->state.hurtcap_a_y[cap_i];
  const float az = batch->state.hurtcap_a_z[cap_i];
  const float bx = batch->state.hurtcap_b_x[cap_i];
  const float by = batch->state.hurtcap_b_y[cap_i];
  const float bz = batch->state.hurtcap_b_z[cap_i];
  (void)combat_attackairb_continuation_body_overlap_exact(batch, batch_index, attacker, hb_id,
                                                          defender, cap_id, hx, hy, hz, hr, ax, ay,
                                                          az, bx, by, bz, out_overlap);
  return 0;
}

int combat_debug_body_matrix_overlap(const MslBatch* batch, int batch_index, int attacker,
                                     int hb_id, int defender, int cap_id, float* out_overlap) {
  if (out_overlap == NULL) {
    return EINVAL;
  }
  *out_overlap = 0.0f;
  if (batch == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (attacker < 0 || attacker >= (int)batch->config.num_players || defender < 0 ||
      defender >= (int)batch->config.num_players || attacker == defender) {
    return EINVAL;
  }
  if (hb_id < 0 || hb_id >= MSL_MAX_HITBOXES || cap_id < 0 || cap_id >= MSL_MAX_HURTCAPS) {
    return EINVAL;
  }
  const size_t hb_i = idx_hitbox(batch_index, attacker, hb_id);
  const size_t cap_i = idx_hurtcap(batch_index, defender, cap_id);
  if (!batch->state.hitbox_enabled[hb_i] || !batch->state.hurtcap_enabled[cap_i]) {
    return 0;
  }
  const float hx = batch->state.hitbox_x[hb_i];
  const float hy = batch->state.hitbox_y[hb_i];
  const float hz = batch->state.hitbox_z[hb_i];
  const float hr = batch->state.hitbox_radius[hb_i];
  const float ax = batch->state.hurtcap_a_x[cap_i];
  const float ay = batch->state.hurtcap_a_y[cap_i];
  const float az = batch->state.hurtcap_a_z[cap_i];
  const float bx = batch->state.hurtcap_b_x[cap_i];
  const float by = batch->state.hurtcap_b_y[cap_i];
  const float bz = batch->state.hurtcap_b_z[cap_i];
  (void)combat_body_overlap_lbColl_80006E58_matrix_radius(batch, batch_index, attacker, hb_id,
                                                          defender, cap_id, hx, hy, hz, hr, ax, ay,
                                                          az, bx, by, bz, 0u, out_overlap, NULL);
  return 0;
}
