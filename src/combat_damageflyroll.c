#include "combat_internal.h"

uint8_t combat_damageflyroll_rng_subset_allows_pre_action(const MslBatch* batch, size_t d_idx,
                                                          uint16_t action_id) {
  return msl_damage_owner_damageflyroll_pre_action_allows_gate(batch, d_idx, action_id);
}

float combat_damage_ground_angle_to_floor_radians(float nx, float ny, float vx, float vy) {
  const float vmag_sq = vx * vx + vy * vy;
  if (!(vmag_sq > 0.0f)) {
    return 0.0f;
  }
  const float inv_vmag = 1.0f / sqrtf(vmag_sq);
  float cos_theta = (nx * vx + ny * vy) * inv_vmag;
  if (cos_theta > 1.0f) {
    cos_theta = 1.0f;
  } else if (cos_theta < -1.0f) {
    cos_theta = -1.0f;
  }
  return acosf(cos_theta);
}

void combat_damage_install_grounded_kb(const MslCommonParams* c, MslBatch* batch, size_t d_idx,
                                       float kb_applied, float kb_x, float kb_y,
                                       uint16_t hitlag_frames, uint8_t force_tumble_severity,
                                       uint8_t allow_damagefly_hitlag_ecb_lock) {
  const float nx = batch->state.ground_normal_x[d_idx];
  const float ny = batch->state.ground_normal_y[d_idx];
  const float angle_to_floor = combat_damage_ground_angle_to_floor_radians(nx, ny, kb_x, kb_y);
  const uint8_t sev = force_tumble_severity ? 3u : combat_damage_severity_u8_from_kb(c, kb_applied);

  // Grounded KB install owner:
  // - ftCo_8008DCE0 compares the floor normal to the raw KB vector with lbVector_Angle.
  // - angle < PI/2 launches immediately with full KB and clears grounded state.
  // - angle >= PI/2 projects along the floor for low/med severity.
  // - tumble severity (sev==3) still clears grounded state, and steep downward meteors
  //   (`angle > PI/2 + x1E8`) reflect their Y component upward with multiplier x1EC.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  if (angle_to_floor < combat_pi_over_two_f32()) {
    combat_apply_ftCommon_8007D5D4_ground_to_air(batch, d_idx);
    if (allow_damagefly_hitlag_ecb_lock && hitlag_frames != 0u) {
      // Supported runtime slice of the ftCommon_8007D5D4 ECB-lock producer:
      // - ftCommon_8007D5D4 sets fp->ecb_lock=10 on ground->air damage launch.
      // - This simulator only consumes that runtime-produced lock for active-hitlag DamageFly
      //   floor-callback handoffs that remain on a persisted ledge-floor line until hitlag exit.
      // - Other helper users remain on their existing seed/runtime owners until their callback
      //   consumers are modeled; replay seeds still carry explicit ecb_lock_timer when needed.
      // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
      batch->state.ecb_lock_timer[d_idx] = MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR;
    }
    combat_damage_calc_vel(batch, d_idx, kb_x, kb_y);
    return;
  }
  if (sev != 3u) {
    combat_damage_calc_vel(batch, d_idx, ny * kb_x, -nx * kb_x);
    return;
  }

  combat_apply_ftCommon_8007D5D4_ground_to_air(batch, d_idx);
  if (allow_damagefly_hitlag_ecb_lock && hitlag_frames != 0u) {
    batch->state.ecb_lock_timer[d_idx] = MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR;
  }
  if (angle_to_floor > (combat_pi_over_two_f32() + c->grounded_tumble_bounce_angle_extra_radians)) {
    combat_damage_calc_vel(batch, d_idx, kb_x, -kb_y * c->grounded_tumble_bounce_y_mul);
    return;
  }
  combat_damage_calc_vel(batch, d_idx, kb_x, kb_y);
}

void combat_damageflyroll_consume_fighter_8006cda4_pre_gate_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg) {
  if (batch == NULL) {
    return;
  }
  const uint8_t consume_count = batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx];
  if (consume_count == 0u) {
    return;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  const uint8_t replay_frame_rng_applied =
      (batch->replay_frame_rng_applied != NULL && batch->replay_frame_rng_applied[bi] != 0u) ? 1u
                                                                                             : 0u;
  if (replay_frame_rng_applied != 0u && consume_count >= 2u && source_hb_valid != 0u &&
      source_cap_valid != 0u && source_hb_i >= hb_base &&
      source_hb_i < hb_base + (size_t)MSL_MAX_HITBOXES && source_cap_i >= cap_base &&
      source_cap_i < cap_base + (size_t)MSL_MAX_HURTCAPS) {
    const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
    const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
    if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP && hb_id == 2u &&
        cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT &&
        source_hitcapsule_int_dmg == 12) {
      // Replay-frame RNG makes this delayed DamageFlyTop hb2/root BODY source seed-owned at the
      // gate. The current seed count can carry a neighboring pre-gate phase, but this selected
      // source reaches ftCo_8008DCE0 without Fighter_8006CDA4 stream advances.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
      batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] = 0u;
      return;
    }
    if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
        (source_motion_id == (uint16_t)MSL_ACT_ATTACK_AIR_B ||
         source_motion_id == (uint16_t)MSL_SM_ATTACK_AIR_B) &&
        hb_id == (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX &&
        cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT &&
        source_hitcapsule_int_dmg == 15) {
      // The current replay seed lane can carry a two-consume Fighter_8006CDA4 phase from a
      // neighboring BODY source. ftColl's final selected tail BAir/head-high HitCapsule does not
      // own that hidden held-item/x197C pre-gate stream phase, so keep this DamageFlyRoll gate on
      // the replay frame-start seed unless another selected-source owner consumes explicitly.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
      // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
      batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] = 0u;
      return;
    }
  }
  // Explicit pre-gate RNG stream-phase owner:
  // - Fighter_8006CDA4 runs before the ftCo_8008DCE0 block_33 HSD_Randf gate and can advance the
  //   same global RNG stream via HSD_Randi calls.
  // - Slippi does not expose the hidden held-item/x197C owner inputs directly, so the seed surface
  //   stores the total pre-gate stream phase explicitly.
  // - Value 4 is a source-proven zero-consume gate marker: admit the early AttackAirB
  //   DamageFlyTop gate but do not advance the RNG stream before the gate draw.
  // - The HSD_Randi return value is not otherwise used in this lite sim, so `max_val=1` is enough
  //   to model the stream advance without introducing extra gameplay constants.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/types.h
  // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  if (consume_count == 5u) {
    for (uint8_t i = 0u; i < 5u; i++) {
      (void)combat_rng_consume_randi_site(
          batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
    }
    batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] = 0u;
    return;
  }
  if (consume_count <= 3u) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  }
  if (consume_count >= 2u && consume_count <= 3u) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_SECONDARY, 1);
  }
  if (consume_count == 3u) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_TERTIARY, 1);
  }
  batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] = 0u;
}

uint8_t combat_attackairb_hitbox_is_authored_strong(uint8_t hb_id, float hitbox_damage) {
  // Fox/Falco BAir source data has exactly two 15-damage strong HitCapsules on the frame-4
  // create edge: hb0 at root and hb1 at tail. The later frame-8 refresh and hb2 are 9-damage weak
  // HitCapsules. This helper names that extracted split instead of using a row-shaped threshold.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return (uint8_t)((hb_id == (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_ROOT_HITBOX ||
                    hb_id == (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX) &&
                   hitbox_damage == 15.0f);
}

uint8_t combat_attackairn_hitbox_payload_is_authored_late(int hitcapsule_int_dmg,
                                                          uint16_t hitbox_angle,
                                                          uint16_t hitbox_kbg,
                                                          uint16_t hitbox_bkb) {
  // Fox/Falco late NAir refreshes all active HitCapsules to the authored 9-damage, Sakurai-angle
  // payload on frame 8. Use the extracted payload rather than visible action family alone.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  return (uint8_t)(hitcapsule_int_dmg == 9 && hitbox_angle == 361u && hitbox_kbg == 100u &&
                   hitbox_bkb == 0u);
}

uint8_t combat_attackairn_hitbox_payload_is_authored_strong(uint8_t hb_id, int hitcapsule_int_dmg,
                                                            uint16_t hitbox_angle,
                                                            uint16_t hitbox_kbg,
                                                            uint16_t hitbox_bkb) {
  // Fox/Falco strong NAir opening payload: hb0/hb1/hb2, 12 damage, Sakurai angle, bkb 10.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  return (uint8_t)(hb_id < (uint8_t)MSL_MAX_HITBOXES && hitcapsule_int_dmg == 12 &&
                   hitbox_angle == 361u && hitbox_kbg == 100u && hitbox_bkb == 10u);
}

uint8_t combat_attack11_hitbox_payload_is_authored_jab(uint8_t hb_id, int hitcapsule_int_dmg,
                                                       uint16_t hitbox_angle, uint16_t hitbox_kbg,
                                                       uint16_t hitbox_bkb) {
  // Fox/Falco Attack11 creates two 4-damage jab HitCapsules on frame 2. Use the extracted source
  // payload and selected hitbox id so Catch interruption does not become a visible-action bridge.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_Attack11.events.create_hitbox
  return (uint8_t)(hb_id <= 1u && hitcapsule_int_dmg == 4 && hitbox_angle == 70u &&
                   hitbox_kbg == 100u && hitbox_bkb == 0u);
}

uint8_t combat_attackairb_hitbox_payload_is_authored_strong(uint8_t hb_id, int hitcapsule_int_dmg,
                                                            uint16_t hitbox_angle,
                                                            uint16_t hitbox_kbg,
                                                            uint16_t hitbox_bkb) {
  // Fox/Falco strong BAir source payload: hb0/hb1, 15 damage, Sakurai angle, no base KB.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return (uint8_t)((hb_id == (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_ROOT_HITBOX ||
                    hb_id == (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX) &&
                   hitcapsule_int_dmg == 15 && hitbox_angle == 361u && hitbox_kbg == 100u &&
                   hitbox_bkb == 0u);
}

uint8_t combat_attackairb_hitbox_payload_is_authored_weak(uint8_t hb_id, int hitcapsule_int_dmg,
                                                          uint16_t hitbox_angle,
                                                          uint16_t hitbox_kbg,
                                                          uint16_t hitbox_bkb) {
  // Fox/Falco weak BAir payload: hb2 on the first create edge, then hb0/hb1/hb2 refresh to the
  // same 9-damage Sakurai-angle payload on frame 8.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return (uint8_t)(hb_id < (uint8_t)MSL_MAX_HITBOXES && hitcapsule_int_dmg == 9 &&
                   hitbox_angle == 361u && hitbox_kbg == 100u && hitbox_bkb == 0u);
}

uint8_t combat_attackairb_jump_tail_rejects_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, int defender, size_t a_idx,
    size_t d_idx, uint8_t cap_id, const MslHurtCap* cap, const MslHurtCap* defender_caps,
    uint16_t defender_cap_count_u16, float hx, float hy, float hz, float hr) {
  if (batch == NULL || cap == NULL || defender_caps == NULL) {
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
  if (cap_id != 12u || cap->height != 1u) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  if (combat_attackairb_hitbox_payload_is_authored_strong(
          hb_id, (int)batch->state.hitbox_damage[hb_i], batch->state.hitbox_angle[hb_i],
          batch->state.hitbox_kbg[hb_i], batch->state.hitbox_bkb[hb_i]) == 0u) {
    return 0u;
  }

  const uint16_t capped_count = defender_cap_count_u16 > (uint16_t)MSL_MAX_HURTCAPS
                                    ? (uint16_t)MSL_MAX_HURTCAPS
                                    : defender_cap_count_u16;
  for (uint16_t other_cap_id = 0u; other_cap_id < capped_count; other_cap_id++) {
    const MslHurtCap* other_cap = &defender_caps[other_cap_id];
    if (other_cap_id == 12u || other_cap->height != 0u) {
      continue;
    }
    const size_t other_cap_i = idx_hurtcap(bi, defender, (int)other_cap_id);
    if (batch->state.hurtcap_enabled[other_cap_i] == 0u) {
      continue;
    }
    float overlap_amount = 0.0f;
    uint8_t overlap_evaluated = 0u;
    uint8_t overlaps = combat_body_overlap_lbColl_80006E58_matrix_radius(
        batch, bi, attacker, (int)hb_id, defender, (int)other_cap_id, hx, hy, hz, hr,
        batch->state.hurtcap_a_x[other_cap_i], batch->state.hurtcap_a_y[other_cap_i],
        batch->state.hurtcap_a_z[other_cap_i], batch->state.hurtcap_b_x[other_cap_i],
        batch->state.hurtcap_b_y[other_cap_i], batch->state.hurtcap_b_z[other_cap_i], 0u,
        &overlap_amount, &overlap_evaluated);
    const uint8_t baseline_overlaps = combat_sphere_capsule_intersects(
        hx, hy, hz, hr, batch->state.hurtcap_a_x[other_cap_i],
        batch->state.hurtcap_a_y[other_cap_i], batch->state.hurtcap_a_z[other_cap_i],
        batch->state.hurtcap_b_x[other_cap_i], batch->state.hurtcap_b_y[other_cap_i],
        batch->state.hurtcap_b_z[other_cap_i], batch->state.hurtcap_radius[other_cap_i], NULL);
    overlaps = (uint8_t)(overlaps || baseline_overlaps);
    if (overlaps) {
      // Jump/JumpAerial -> strong BAir dynamic-tail-slot/body selected-height owner:
      // ftColl's BODY DmgLog source follows the concrete low hurtcap when strong AttackAirB hb0
      // overlaps both the generated Jump/JumpAerial pose's cap12 dynamic-tail slot and a low leg
      // capsule. The current generated substrate exposes hurtcap slot, bone owner, and height but
      // not a semantic body-region label, so reject only that extracted cap12/height-1 candidate and
      // only when a same-hitbox low BODY source is present. This is bounded by the authored strong
      // BAir payload, common jump/aerial-jump motion state, and `data/hurtcaps/{fox,falco}.json`
      // hurtcap metadata; it does not promote JumpAerialF/B into the global dynamic-pose index.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
      // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
      // data/hurtcaps/{fox,falco}.json cap12 height 1 dynamic tail slot, cap10/11 height 0
      return 1u;
    }
  }
  if (batch->state.stage_id[bi] == (uint32_t)MSL_STAGE_ID_POKEMON_STADIUM &&
      (defender_action == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
       defender_action == (uint16_t)MSL_ACT_JUMP_AERIAL_B)) {
    // Pokemon Stadium JumpAerial tail-only strong BAir boundary:
    // `ftCo_JumpAerial` updates the dynamic tail JObj chain before ftColl BODY selection. A
    // generated cap12-only overlap on Stadium, whose root x44 matrix also carries the stage
    // `x34_scale.z` lane, is not sufficient source proof for full BODY damage; the adjacent deeper
    // row that reaches low/body hurtcaps still hits before cap12 is considered. Non-Stadium
    // JumpAerial tail-only BAir rows remain ordinary BODY contacts, matching QGD/DCC controls.
    // Keep this local to the selected cap12 FtPart-18 slot and authored strong BAir payload instead
    // of adding JumpAerialF/B to the global dynamic-pose extraction index.
    // refs/melee/src/melee/ft/fighter.c::{Fighter_80068E64,Fighter_UpdateModelScale}
    // refs/melee/src/melee/ft/fighter.c::Fighter_procUpdate
    // refs/melee/src/melee/ft/chara/ftFox/ftFox_AttackAir.c::{ftCo_8009DD94,ftCo_8009E318}
    // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    return 1u;
  }
  return 0u;
}

uint8_t combat_attackairf_hitbox_payload_is_authored_mid(uint8_t hb_id, int hitcapsule_int_dmg,
                                                         uint16_t hitbox_angle, uint16_t hitbox_kbg,
                                                         uint16_t hitbox_bkb) {
  // Falco AttackAirF's first two flurry payloads are extracted 9- and 8-damage Sakurai-angle
  // capsules on hb0/hb1 with bkb 10. Use payload + selected HitCapsule id, not character id.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirF.events.create_hitbox
  return (uint8_t)(hb_id <= 1u && hitcapsule_int_dmg >= 8 && hitcapsule_int_dmg <= 9 &&
                   hitbox_angle == 361u && hitbox_kbg == 100u && hitbox_bkb == 10u);
}

uint8_t combat_attackairlw_hitbox_payload_is_authored_meteor(uint8_t hb_id, int hitcapsule_int_dmg,
                                                             uint16_t hitbox_angle,
                                                             uint16_t hitbox_kbg,
                                                             uint16_t hitbox_bkb) {
  // Falco AttackAirLw's source-owned meteor capsules are the extracted strong 12-damage pair and
  // late 9-damage pair. Fox multihit DAir has different damage/bkb/WSK payloads and stays out.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return (uint8_t)(hb_id <= 1u && hitbox_angle == 290u && hitbox_kbg == 100u &&
                   ((hitcapsule_int_dmg == 12 && hitbox_bkb == 10u) ||
                    (hitcapsule_int_dmg == 9 && hitbox_bkb == 20u)));
}

uint8_t combat_attacklw4_hitbox_payload_is_authored_strong(uint8_t hb_id, int hitcapsule_int_dmg,
                                                           uint16_t hitbox_angle,
                                                           uint16_t hitbox_kbg,
                                                           uint16_t hitbox_bkb) {
  // Fox/Falco down-smash strong outside capsules are extracted hb0/hb1 at the create edge. Fox and
  // Falco differ by damage/KBG, so use the authored payload range rather than character id.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackLw4.events.create_hitbox
  return (uint8_t)((hb_id == 0u || hb_id == 1u) && hitcapsule_int_dmg >= 15 &&
                   hitcapsule_int_dmg <= 16 && hitbox_angle == 25u && hitbox_kbg >= 65u &&
                   hitbox_kbg <= 70u && hitbox_bkb == 20u);
}

uint8_t combat_source_motion_is_attacks3(uint16_t source_motion_id) {
  if (msl_motion_state_common_class_has_fast(source_motion_id, MSL_MS_CLASS_ATTACK_S3) != 0u) {
    return 1u;
  }
  // The generated MSLMSO01 class is keyed by MotionState action id. Some DmgLog/source-motion
  // snapshots carry the animation/submotion id instead; MSLFTSC1 currently canonicalizes AttackS3
  // event lookup internally but does not expose a script-family predicate for those submotion ids.
  // Keep this fallback bounded to the extracted AttackS3 submotion family until that helper exists.
  // refs/melee/src/melee/ft/ftmotionstates.c (AttackS3* entries share ftCo_AttackS3 callbacks)
  // src/hitboxes_tables.c::canonical_hitbox_events_msid
  return (uint8_t)(source_motion_id == (uint16_t)MSL_SM_ATTACK_S3_HI ||
                   source_motion_id == (uint16_t)MSL_SM_ATTACK_S3_HI_S ||
                   source_motion_id == (uint16_t)MSL_SM_ATTACK_S3 ||
                   source_motion_id == (uint16_t)MSL_SM_ATTACK_S3_LW_S ||
                   source_motion_id == (uint16_t)MSL_SM_ATTACK_S3_LW);
}

uint8_t combat_attacks3_hitbox_payload_is_authored(uint8_t hb_id, int hitcapsule_int_dmg,
                                                   uint16_t hitbox_angle, uint16_t hitbox_kbg,
                                                   uint16_t hitbox_bkb) {
  // Fox/Falco side-tilt variants share the extracted ftCo_SM_AttackS3 script: hb0/hb1/hb2,
  // 9 damage, Sakurai angle, no base KB.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackS3.events.create_hitbox
  return (uint8_t)(hb_id < (uint8_t)MSL_MAX_HITBOXES && hitcapsule_int_dmg == 9 &&
                   hitbox_angle == 361u && hitbox_kbg == 100u && hitbox_bkb == 0u);
}

uint8_t combat_attackhi4_hitbox_payload_is_authored_late(uint8_t hb_id, int hitcapsule_int_dmg,
                                                         uint16_t hitbox_angle, uint16_t hitbox_kbg,
                                                         uint16_t hitbox_bkb) {
  // Fox/Falco late up-smash refreshes hb0/hb1 to Sakurai-angle, bkb-10 sourspots. Fox's payload is
  // 13 damage; Falco's is 12. Keep the family data-backed by hitbox id/angle/kb, not character id.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackHi4.events.create_hitbox
  return (uint8_t)((hb_id == 0u || hb_id == 1u) && hitbox_angle == 361u && hitbox_kbg == 100u &&
                   hitbox_bkb == 10u && hitcapsule_int_dmg >= 12 && hitcapsule_int_dmg <= 13);
}

uint8_t combat_hurt_height_damageflytop_weak_attackairb_head_high_uses_medium(
    const MslBatch* batch, size_t d_idx, size_t a_idx, size_t source_hb_i, size_t source_cap_i,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] > 1u ||
      !(source_motion_id == (uint16_t)MSL_ACT_ATTACK_AIR_B ||
        source_motion_id == (uint16_t)MSL_SM_ATTACK_AIR_B)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT ||
      !combat_attackairb_hitbox_payload_is_authored_weak(hb_id, source_hitcapsule_int_dmg,
                                                         source_hitbox_angle, source_hitbox_kbg,
                                                         source_hitbox_bkb)) {
    return 0u;
  }
  // Terminal DamageFlyTop weak BAir selected-height owner:
  // ftColl selected the authored weak BackAir BODY HitCapsule against cap2/head-high while the
  // victim's DamageFlyTop hitstun was expiring. The replay-reconstructed cap2 height is high, but
  // the source damage-state result follows the selected weak BAir terminal body lane and enters
  // DamageFlyN. Keep this bounded to the concrete weak BAir payload plus selected cap2 source;
  // strong BAir cap2 rows and non-terminal DamageFlyTop keep the generated hurtcap height.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  return 1u;
}

uint8_t combat_damageflyroll_kneebend_attacks3_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS) ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_KNEE_BEND ||
      batch->state.hitlag[d_idx] != 0u || source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attacks3(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (!combat_attacks3_hitbox_payload_is_authored(hb_id, source_hitcapsule_int_dmg,
                                                  source_hitbox_angle, source_hitbox_kbg,
                                                  source_hitbox_bkb)) {
    return 0u;
  }
  // Grounded KneeBend -> authored AttackS3 severe-airborne DamageFlyRoll owner:
  // ftCo_KneeBend can be entered by the victim's same-frame IASA/callback path before ftColl's
  // BODY log is resolved. When the selected BODY source is the extracted side-tilt payload,
  // ftCo_8008DCE0 still reaches the airborne severe-damage RNG gate after KB clears
  // ground_or_air. The owner is the selected HitCapsule payload and KneeBend source state, not the
  // replay row or attacker/victim character pair.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackS3.events.create_hitbox
  return 1u;
}

uint8_t combat_damageflyroll_attacklw3_late_attackhi4_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_ATTACK_LW3 ||
      defender_on_ground_before == 0u || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !(source_motion_id == (uint16_t)MSL_ACT_ATTACK_HI4 ||
        source_motion_id == (uint16_t)MSL_SM_ATTACK_HI4)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT ||
      !combat_attackhi4_hitbox_payload_is_authored_late(hb_id, source_hitcapsule_int_dmg,
                                                        source_hitbox_angle, source_hitbox_kbg,
                                                        source_hitbox_bkb)) {
    return 0u;
  }
  // AttackLw3 -> late AttackHi4 DamageFlyRoll stream owner:
  // ftColl selected an authored late up-smash HitCapsule against cap2/head-high while the victim
  // was still in grounded down-tilt. That concrete DmgLog source reaches Fighter_8006CDA4 before
  // ftCo_8008DCE0's DamageFlyRoll gate and owns one primary HSD_Randi prefix. Visible AttackLw3
  // state alone is not enough; the source proof is the selected late AttackHi4 payload and hurtcap.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackHi4.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  return 1u;
}

uint8_t combat_damageflyroll_wait_attacklw3_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before, uint16_t pre_action) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS) ||
      batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      pre_action != (uint16_t)MSL_ACT_WAIT || defender_on_ground_before == 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !(source_motion_id == (uint16_t)MSL_ACT_ATTACK_LW3 ||
        source_motion_id == (uint16_t)MSL_SM_ATTACK_LW3)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t attacker_char = batch->state.char_id[a_idx];
  if (!move_tables_grounded_attack_create_hitbox_payload_matches(
          attacker_char, (uint16_t)MSL_ACT_ATTACK_LW3, hb_id, source_hitcapsule_int_dmg,
          source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb)) {
    return 0u;
  }
  // Grounded Wait -> Marth AttackLw3 DamageFlyRoll owner:
  // ftCo_8008DCE0's severe grounded branch calls ftCommon_8007D5D4 and then evaluates the
  // airborne DamageFlyRoll gate after installing knockback. The selected DmgLog source must match
  // a concrete MSLFTSC1 create_hitbox row for the attacker's character/action; future characters
  // do not inherit this owner merely by sharing a common action id or local payload shape.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C,ftColl_80078538}
  // data/scripts/<char>.bin (MSLFTSC1)::ftCo_SM_AttackLw3 create_hitbox
  return 1u;
}

uint8_t combat_damageflyroll_jump_late_attackhi4_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  const uint16_t victim_action = batch->state.action_id[d_idx];
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      victim_action != (uint16_t)MSL_ACT_JUMP_F || batch->state.on_ground[d_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_HI4 || source_hb_valid == 0u ||
      source_cap_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != 12u || !combat_attackhi4_hitbox_payload_is_authored_late(
                           hb_id, source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                           source_hitbox_bkb)) {
    return 0u;
  }
  // JumpF -> late AttackHi4 DamageFlyRoll owner:
  // ftColl selected the live authored late up-smash HitCapsule against the victim's cap12 tail
  // hurtcap while the victim was airborne in JumpF. The current HitCapsule payload owns the normal
  // ftColl damage-effect RNG prefix before the severe-airborne `ftCo_8008DCE0` DamageFlyRoll gate;
  // stale residual motion labels from the previous attacker action are not sufficient proof.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackHi4.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap12
  return 1u;
}

uint8_t combat_ground_to_air_ecb_lock_late_attackhi4_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before,
    uint16_t pre_damage_action) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS) ||
      defender_on_ground_before == 0u ||
      combat_is_downed_damage_contact_action(pre_damage_action) || source_hb_valid == 0u ||
      source_cap_valid == 0u ||
      !(source_motion_id == (uint16_t)MSL_ACT_ATTACK_HI4 ||
        source_motion_id == (uint16_t)MSL_SM_ATTACK_HI4)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT ||
      !combat_attackhi4_hitbox_payload_is_authored_late(hb_id, source_hitcapsule_int_dmg,
                                                        source_hitbox_angle, source_hitbox_kbg,
                                                        source_hitbox_bkb)) {
    return 0u;
  }
  // Grounded late AttackHi4 -> DamageFly hitlag-exit ECB-lock owner:
  // Fighter_ProcessHit routes the selected BODY DmgLog into ftCo_8008DCE0, whose ground-vs-air KB
  // path calls ftCommon_8007D5D4 and sets fp->ecb_lock=10 / CollData_X130_Locked before hitlag.
  // This runtime consumer is bounded to the selected late UpSmash HitCapsule against cap2/head-high;
  // replay-visible action shape alone is not enough to reconstruct the hidden locked-bottom lane.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_8008DCE0,ftCo_DamageFly_Coll}
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackHi4.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  return 1u;
}

void combat_damageflyroll_consume_attacklw3_late_attackhi4_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  if (!combat_damageflyroll_attacklw3_late_attackhi4_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
}

uint8_t combat_source_motion_is_attackairb(uint16_t source_motion_id) {
  return (uint8_t)(source_motion_id == (uint16_t)MSL_ACT_ATTACK_AIR_B ||
                   source_motion_id == (uint16_t)MSL_SM_ATTACK_AIR_B);
}

uint8_t combat_source_motion_is_attackairf(uint16_t source_motion_id) {
  return (uint8_t)(source_motion_id == (uint16_t)MSL_ACT_ATTACK_AIR_F ||
                   source_motion_id == (uint16_t)MSL_SM_ATTACK_AIR_F);
}

uint8_t combat_source_motion_is_attackairn(uint16_t source_motion_id) {
  return (uint8_t)(source_motion_id == (uint16_t)MSL_ACT_ATTACK_AIR_N ||
                   source_motion_id == (uint16_t)MSL_SM_ATTACK_AIR_N);
}

uint8_t combat_source_motion_is_attackairlw(uint16_t source_motion_id) {
  return (uint8_t)(source_motion_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW ||
                   source_motion_id == (uint16_t)MSL_SM_ATTACK_AIR_LW);
}

uint8_t combat_source_motion_is_attacklw4(uint16_t source_motion_id) {
  return (uint8_t)(source_motion_id == (uint16_t)MSL_ACT_ATTACK_LW4 ||
                   source_motion_id == (uint16_t)MSL_SM_ATTACK_LW4);
}

uint8_t combat_action_is_specialairn_family(uint8_t char_id, uint16_t action_id) {
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id);
  return (uint8_t)(fx_kind >= (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_START &&
                   fx_kind <= (uint8_t)MSL_FX_KIND_SPECIAL_AIR_N_END);
}

uint8_t combat_damageflyroll_fallspecial_attackairf_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_FALL_SPECIAL ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairf(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT ||
      !combat_attackairf_hitbox_payload_is_authored_mid(hb_id, source_hitcapsule_int_dmg,
                                                        source_hitbox_angle, source_hitbox_kbg,
                                                        source_hitbox_bkb)) {
    return 0u;
  }
  // FallSpecial -> AttackAirF DamageFlyRoll owner:
  // ftColl selected the authored mid-flurry Forward-Air BODY HitCapsule against cap2/head-high
  // while the victim was still in FallSpecial. The normal BODY path owns ftColl_80078538's visual
  // effect prefix, then this selected source reaches Fighter_8006CDA4 once before
  // ftCo_8008DCE0's DamageFlyRoll HSD_Randf gate. Visible FallSpecial or AttackAirF alone is not
  // enough; the source proof is the selected HitCapsule payload plus hurtcap provenance.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirF.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  return 1u;
}

void combat_damageflyroll_consume_fallspecial_attackairf_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_fallspecial_attackairf_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
}

uint8_t combat_damageflyroll_jump_strong_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  const uint16_t pre_action = batch->state.action_id[d_idx];
  const uint8_t jump_to_attackairf_entry =
      (uint8_t)(pre_action == (uint16_t)MSL_ACT_ATTACK_AIR_F &&
                batch->state.action_frame[d_idx] <= 1 &&
                (batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_JUMP_F ||
                 batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_JUMP_B));
  const uint8_t early_jump_source = (uint8_t)((pre_action == (uint16_t)MSL_ACT_JUMP_F ||
                                               pre_action == (uint16_t)MSL_ACT_JUMP_B) &&
                                              batch->state.action_frame[d_idx] <= 2);
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      (early_jump_source == 0u && jump_to_attackairf_entry == 0u) ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairn(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT ||
      !combat_attackairn_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                           source_hitbox_angle, source_hitbox_kbg,
                                                           source_hitbox_bkb)) {
    return 0u;
  }
  // Early JumpF/JumpB or same-callback Jump -> AttackAirF entry -> strong AttackAirN
  // DamageFlyRoll owner: the victim's jump callback can leave source state as early JumpF/B, or
  // enter the first frame of AttackAirF before combat, when a current strong NAir BODY HitCapsule
  // is selected against the root/body hurtcap. Later sustained Jump frames keep the ordinary
  // seed-owned path. ftColl_80078538 consumes the normal effect prefix for each eligible BODY
  // contact, then this selected source reaches the Fighter_8006CDA4 pre-gate callsites before
  // ftCo_8008DCE0 samples DamageFlyRoll. The selected hb/cap/payload tuple and the callback-local
  // Jump provenance are required; visible Jump/AttackAirF shape alone remains seed-owned.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap0
  return 1u;
}

void combat_damageflyroll_consume_jump_strong_attackairn_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_jump_strong_attackairn_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb)) {
    return;
  }
  for (uint8_t i = 0u; i < (uint8_t)MSL_DAMAGEFLYROLL_JUMP_ATTACKAIRN_FIGHTER_8006CDA4_CONSUMES;
       i++) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  }
}

uint8_t combat_damageflyroll_sustained_jump_late_attackairn_leg_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_JUMP_F &&
       batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_JUMP_B) ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairn(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (hb_id != 0u || cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_LEG_HIGH_SLOT ||
      !combat_attackairn_hitbox_payload_is_authored_late(
          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb)) {
    return 0u;
  }
  // Sustained JumpF/JumpB -> late AttackAirN leg BODY DamageFlyRoll owner:
  // ftColl selected the extracted late NAir hb0 source against cap11, the non-grabbable leg
  // hurtcap (`data/hurtcaps/{fox,falco}.json` bone 7). The selected DmgLog packet owns the
  // normal-element ftColl_80078538 effect entries before Fighter_ProcessHit; the same late-NAir
  // source episode then owns four Fighter_8006CDA4 primary advances before ftCo_8008DCE0 samples
  // DamageFlyRoll. JumpF/JumpB visible state alone, strong NAir payloads, other hitboxes, and
  // root/head hurtcaps stay on their existing owners.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap11
  return 1u;
}

void combat_damageflyroll_consume_sustained_jump_late_attackairn_leg_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_sustained_jump_late_attackairn_leg_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb)) {
    return;
  }
  for (uint8_t i = 0u;
       i < (uint8_t)(MSL_DAMAGEFLYROLL_SUSTAINED_JUMP_LATE_ATTACKAIRN_FTCOLL_EXTRA_NORMAL_ENTRIES *
                     MSL_FTCOLL_NORMAL_DAMAGE_EFFECT_RANDI_CONSUMES);
       i++) {
    (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_FTCOLL_DAMAGE_EFFECT, 1);
  }
  for (uint8_t i = 0u;
       i <
       (uint8_t)MSL_DAMAGEFLYROLL_SUSTAINED_JUMP_LATE_ATTACKAIRN_FIGHTER_8006CDA4_PRIMARY_CONSUMES;
       i++) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  }
}

uint8_t combat_damageflyroll_specialairhi_attacklw4_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  // No attacker char-family gate: the victim-side spacie special states below are keyed on
  // the extracted MotionState kind, the attacker's move payload is verified against the
  // attacker's own extracted move data, and source ftCo_8008DCE0's DamageFlyRoll gate is
  // victim-side char-agnostic. The old gate reflected the spacie-vs-spacie validation
  // corpus, not the mechanism (de-spacie pass).
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      msl_motion_state_fx_special_kind(batch->state.char_id[d_idx],
                                       batch->state.action_id[d_idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attacklw4(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT ||
      !combat_attacklw4_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                          source_hitbox_angle, source_hitbox_kbg,
                                                          source_hitbox_bkb)) {
    return 0u;
  }
  // SpecialAirHi -> strong AttackLw4 DamageFlyRoll owner:
  // up-special travel is not excluded by ftCo_8008DCE0. Runtime admits the DamageFlyRoll gate only
  // when the selected DmgLog source is an authored strong down-smash HitCapsule against the
  // root/body hurtcap. This source family owns two Fighter_8006CDA4 pre-gate advances; broad
  // SpecialAirHi or AttackLw4 action shape remains rejected.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialAirHi_Phys
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackLw4.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap0
  return 1u;
}

void combat_damageflyroll_consume_specialairhi_attacklw4_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_specialairhi_attacklw4_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb)) {
    return;
  }
  for (uint8_t i = 0u; i < (uint8_t)MSL_DAMAGEFLYROLL_SPECAIRHI_ATTACKLW4_FIGHTER_8006CDA4_CONSUMES;
       i++) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  }
}

uint8_t combat_damageflyroll_specialairn_attackairlw_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      !combat_action_is_specialairn_family(batch->state.char_id[d_idx],
                                           batch->state.action_id[d_idx]) ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairlw(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (!combat_attackairlw_hitbox_payload_is_authored_meteor(hb_id, source_hitcapsule_int_dmg,
                                                            source_hitbox_angle, source_hitbox_kbg,
                                                            source_hitbox_bkb)) {
    return 0u;
  }
  // SpecialAirN -> AttackAirLw meteor DamageFlyRoll owner:
  // the selected source is an authored DAir meteor payload during blaster startup/loop. The source
  // proof is selected HitCapsule payload plus BODY hurtcap provenance; this admits the gate and
  // lets ftColl_80078538's normal-effect prefix supply the RNG phase without adding a
  // Fighter_8006CDA4 consume. Visible SpecialAirN or DAir shape alone is not enough.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_SpecialAirNLoop_Phys
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json BODY slots
  (void)source_cap_i;
  return 1u;
}

uint8_t combat_damageflyroll_recovery_action_attackairlw_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  const uint16_t defender_action = batch->state.action_id[d_idx];
  const uint8_t downstand_source = (uint8_t)((defender_action == (uint16_t)MSL_ACT_DOWN_STAND_U ||
                                              defender_action == (uint16_t)MSL_ACT_DOWN_STAND_D) &&
                                             defender_on_ground_before != 0u);
  const uint8_t attackairb_source = (uint8_t)(defender_action == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
                                              batch->state.on_ground[d_idx] == 0u);
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      (downstand_source == 0u && attackairb_source == 0u) || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairlw(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  const uint8_t cap_owner =
      downstand_source != 0u
          ? (uint8_t)(cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_UPPER_BODY_SLOT)
          : (uint8_t)(cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT ||
                      cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_LEG_LOW_SLOT ||
                      cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_XROTN_SLOT);
  if (cap_owner == 0u || !combat_attackairlw_hitbox_payload_is_authored_meteor(
                             hb_id, source_hitcapsule_int_dmg, source_hitbox_angle,
                             source_hitbox_kbg, source_hitbox_bkb)) {
    return 0u;
  }
  // DownStand*/AttackAirB -> strong/late AttackAirLw DamageFlyRoll owner:
  // ftColl selected the authored Falco DAir meteor payload against a concrete BODY hurtcap while
  // the victim was still in a recovery-source state. Fighter_ProcessHit then reaches
  // ftCo_8008DCE0's severe-airborne DamageFlyRoll gate. This is bounded by selected
  // HitCapsule/hurtcap DmgLog provenance, not visible action shape alone.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap1/cap10/cap12
  return 1u;
}

void combat_damageflyroll_consume_recovery_action_attackairlw_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  if (!combat_damageflyroll_recovery_action_attackairlw_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before)) {
    return;
  }
  (void)batch;
  (void)bi;
}

uint8_t combat_damageflyroll_catch_attackairf_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint8_t defender_on_ground_before, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      !combat_action_is_catch_family(batch->state.action_id[d_idx]) ||
      defender_on_ground_before == 0u || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairf(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT ||
      !combat_attackairf_hitbox_payload_is_authored_mid(hb_id, source_hitcapsule_int_dmg,
                                                        source_hitbox_angle, source_hitbox_kbg,
                                                        source_hitbox_bkb)) {
    return 0u;
  }
  // Grounded Catch-family -> AttackAirF DamageFlyRoll owner:
  // current ProcessHit can interrupt Catch before ftCo_8008DCE0's severe airborne damage-state
  // selection. The selected source is an authored mid-flurry Forward-Air HitCapsule against
  // cap0/root-body; that source path owns the normal BODY effect prefix plus three
  // Fighter_8006CDA4 pre-gate advances. Catch state alone remains rejected.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Catch_Anim,ftCo_800D8C54}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirF.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap0
  return 1u;
}

void combat_damageflyroll_consume_catch_attackairf_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint8_t defender_on_ground_before, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_catch_attackairf_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, defender_on_ground_before, source_motion_id, source_hitcapsule_int_dmg,
          source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb)) {
    return;
  }
  for (uint8_t i = 0u; i < (uint8_t)MSL_DAMAGEFLYROLL_CATCH_ATTACKAIRF_FIGHTER_8006CDA4_CONSUMES;
       i++) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  }
}

uint8_t combat_damageflyroll_kneebend_weak_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  const uint16_t pre_action = batch->state.action_id[d_idx];
  const uint8_t kneebend_to_jumpf_entry =
      (uint8_t)(pre_action == (uint16_t)MSL_ACT_JUMP_F && batch->state.action_frame[d_idx] <= 0 &&
                batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_KNEE_BEND);
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      (pre_action != (uint16_t)MSL_ACT_KNEE_BEND && kneebend_to_jumpf_entry == 0u) ||
      batch->state.hitlag[d_idx] != 0u || source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairb(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != 1u || !combat_attackairb_hitbox_payload_is_authored_weak(
                          hb_id, source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb)) {
    return 0u;
  }
  // KneeBend or same-callback KneeBend -> JumpF entry -> weak AttackAirB DamageFlyRoll owner:
  // ftColl selected the authored weak BAir BODY HitCapsule against upper-body cap1 while the
  // victim's jump-squat callback source state was still KneeBend, or had just entered JumpF from
  // that source state. The two eligible BODY contacts consume ftColl_80078538's normal-effect
  // prefix; this source family does not add a Fighter_8006CDA4 consume. Keep this on selected
  // hb/cap/payload provenance, not KneeBend/JumpF or BAir action shape alone.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_KneeBend.c::ftCo_KneeBend_Anim
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap1
  return 1u;
}

uint8_t combat_damageflyroll_landingairn_weak_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_LANDING_AIR_N ||
      defender_on_ground_before == 0u || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairb(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT ||
      !combat_attackairb_hitbox_payload_is_authored_weak(hb_id, source_hitcapsule_int_dmg,
                                                         source_hitbox_angle, source_hitbox_kbg,
                                                         source_hitbox_bkb)) {
    return 0u;
  }
  // LandingAirN -> weak AttackAirB DamageFlyRoll owner:
  // ftColl selected the extracted weak BAir hb2/cap2 BODY source while the victim was still in the
  // grounded LandingAirN callback. Fighter_ProcessHit clears ground state before ftCo_8008DCE0's
  // severe-airborne terminal selection, so the selected DmgLog source owns one Fighter_8006CDA4
  // primary pre-gate consume. LandingAirN or BAir visible action alone remains insufficient.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  return 1u;
}

void combat_damageflyroll_consume_landingairn_weak_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  if (!combat_damageflyroll_landingairn_weak_attackairb_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
}

void combat_damageflyroll_consume_kneebend_weak_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_kneebend_weak_attackairb_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb)) {
    return;
  }
  for (uint8_t i = 0u;
       i < (uint8_t)MSL_DAMAGEFLYROLL_KNEEBEND_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_CONSUMES; i++) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  }
}

enum {
  MSL_SM_FX_SPECIAL_LW_START_SOURCE = 313u,
  MSL_SM_FX_SPECIAL_AIR_LW_START_SOURCE = 317u,
};

uint8_t combat_source_motion_is_speciallw_start(uint8_t char_id, uint16_t source_motion_id) {
  // DmgLog stores the selected HitCapsule's source submotion id for Reflector startup, not always
  // the high-level action id. MSLFTSC1 extracts Fox/Falco SpecialLw Start create-hitbox payloads
  // under specials_by_msid 313/317.
  // data/moves/{fox,falco}.json::specials_by_msid["313"|"317"].events.create_hitbox
  // source_motion_id mixes action ids and submotion source ids in one lane: the action-id
  // values resolve through the extracted MotionState identity (kind 0 for sub-341 commons
  // and other chars' specials); the SM_* submotion sources stay raw vocabulary.
  return (uint8_t)(msl_motion_state_fx_special_kind(char_id, source_motion_id) ==
                       (uint8_t)MSL_FX_KIND_SPECIAL_LW_START ||
                   msl_motion_state_fx_special_kind(char_id, source_motion_id) ==
                       (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START ||
                   source_motion_id == (uint16_t)MSL_SM_FX_SPECIAL_LW_START_SOURCE ||
                   source_motion_id == (uint16_t)MSL_SM_FX_SPECIAL_AIR_LW_START_SOURCE);
}

uint8_t combat_speciallw_start_hitbox_is_authored_reflector_start(const MslBatch* batch,
                                                                  size_t hb_i) {
  if (batch == NULL) {
    return 0u;
  }
  // Fox/Falco Reflector startup payloads are extracted in MSLFTSC1 specials_by_msid 313/317:
  // - Fox: 5 damage, angle 0, KBG 100, WSK 80, BKB 0.
  // - Falco: 8 damage, angle 84, KBG 50, WSK 0, BKB 110.
  // Use the live HitCapsule payload rather than the resolved ProcessHit damage product; the latter
  // can already include stale-damage ownership, while this predicate names the authored source.
  // data/moves/{fox,falco}.json::specials_by_msid["313"|"317"].events.create_hitbox
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
  return (
      uint8_t)((batch->state.hitbox_damage[hb_i] == 5.0f && batch->state.hitbox_angle[hb_i] == 0u &&
                batch->state.hitbox_kbg[hb_i] == 100u && batch->state.hitbox_wsk[hb_i] == 80u &&
                batch->state.hitbox_bkb[hb_i] == 0u) ||
               (batch->state.hitbox_damage[hb_i] == 8.0f &&
                batch->state.hitbox_angle[hb_i] == 84u && batch->state.hitbox_kbg[hb_i] == 50u &&
                batch->state.hitbox_wsk[hb_i] == 0u && batch->state.hitbox_bkb[hb_i] == 110u));
}

uint8_t combat_speciallw_start_source_payload_is_authored_reflector_start(int hitcapsule_int_dmg,
                                                                          uint16_t hitbox_angle,
                                                                          uint16_t hitbox_kbg,
                                                                          uint16_t hitbox_bkb) {
  // Same extracted reflector startup payload as the live HitCapsule helper above, but for
  // DmgLog-selected source records after the live HitCapsule lane may already be cleared. The
  // selected ProcessHit payload does not carry WSK, so use the remaining authored tuple.
  // data/moves/{fox,falco}.json::specials_by_msid["313"|"317"].events.create_hitbox
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  return (uint8_t)((hitcapsule_int_dmg == 5 && hitbox_angle == 0u && hitbox_kbg == 100u &&
                    hitbox_bkb == 0u) ||
                   (hitcapsule_int_dmg == 8 && hitbox_angle == 84u && hitbox_kbg == 50u &&
                    hitbox_bkb == 110u));
}

uint8_t combat_damage_hitstun_strong_attackairlw_terminal_damageflytop_subtracts(
    const MslBatch* batch, const MslCombatProcessHitResolved* ev) {
  if (batch == NULL || ev == NULL) {
    return 0u;
  }
  if (!combat_source_motion_is_attackairlw(ev->source_motion_id) ||
      ev->d_motion_id != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.on_ground[ev->d_idx] != 0u || batch->state.hitstun[ev->d_idx] != 0u ||
      ev->source_hb_valid == 0u) {
    return 0u;
  }
  const size_t total_hitboxes =
      (size_t)batch->batch_size * (size_t)MSL_MAX_PLAYERS * (size_t)MSL_MAX_HITBOXES;
  if (ev->source_hb_i >= total_hitboxes) {
    return 0u;
  }
  if (batch->state.hitbox_prev_enabled[ev->source_hb_i] == 0u) {
    return 0u;
  }
  const int16_t first_create_frame = move_tables_attackair_first_create_hitbox_frame(
      batch->state.char_id[ev->a_idx], batch->state.action_id[ev->a_idx]);
  if (first_create_frame < 0 || batch->state.action_frame[ev->a_idx] <= first_create_frame) {
    return 0u;
  }
  // Carried strong DAir meteor payload into a terminal DamageFlyTop victim:
  // ftColl_80076ED8 selected a live HitCapsule whose previous position is still valid, so this is
  // a continuation hit after the extracted create-hitbox frame, not a first-create DAir capsule.
  // In this carried path ftCo_8008DCE0 installs the meteor DamageFly state after the victim's prior
  // DamageFlyTop callback has consumed the terminal hitstun tick. The replay-visible timer is
  // therefore one below the raw ftCo_ScaleBy154 result for this carried meteor source, while
  // hitlag, percent, action, instance, and KB velocity remain owned by the selected HitCapsule
  // payload. Use the DmgLog-selected source payload plus the live HitCapsule previous-position
  // lane and MSLFTSC1 create frame, not character-pair, visible action alone, or row identity.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return (uint8_t)(ev->source_hitcapsule_int_dmg == 12 && ev->source_hitbox_angle == 290u &&
                   ev->source_hitbox_kbg == 100u && ev->source_hitbox_bkb == 10u);
}

uint8_t combat_attackairb_damageflytop_selected_body_source_owns_pre_gate(uint8_t hb_id,
                                                                          uint8_t cap_id) {
  // Selected BODY source slots for the narrowed DamageFlyTop pre-gate owner:
  // - cap12 is the live XRotN/upper-body hurtcap in data/hurtcaps/{fox,falco}.json and is selected
  //   by ftColl_80076ED8 once DamageFlyRoll/DamageFlyTop rotates the damage pose high enough.
  // - hb1/cap2 is the authored strong tail HitCapsule against the high/head hurtcap slot.
  // - hb1/cap0 is the same authored strong tail HitCapsule against the root/body slot while a
  //   carried DamageFlyTop x14 source lane is live; root/body create-edge rows without that lane
  //   stay seed-owned or ordinary DamageFlyN/Hi.
  // - hb1/cap10 is the low leg hurtcap (height 0, not grabbable in data/hurtcaps) selected by the
  //   same create-edge BAir DmgLog path. It admits the DamageFlyRoll gate but does not imply a
  //   Fighter_8006CDA4 pre-gate consume by itself.
  // Raw cap ids are kept here only because the generated hurtcap substrate currently exposes ids
  // and metadata but not semantic names. Tests lock cap1, hb0/cap2, unrelated low/root, and aggregate rows
  // onto their original path.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/hurtcaps/{fox,falco}.json cap0/cap2/cap12
  return (uint8_t)(cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_XROTN_SLOT ||
                   (hb_id == (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX &&
                    (cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT ||
                     cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT ||
                     cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_LEG_LOW_SLOT)));
}

uint8_t combat_damageflyroll_damageflytop_attackairb_create_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B ||
      batch->state.action_frame[a_idx] < 3 || source_hb_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (!combat_attackairb_hitbox_is_authored_strong(hb_id,
                                                   batch->state.hitbox_damage[source_hb_i])) {
    return 0u;
  }
  if (source_cap_valid == 0u) {
    return 0u;
  }
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  const uint8_t root_body_x14_strong_root_source =
      (uint8_t)(hb_id == (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_ROOT_HITBOX &&
                cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT &&
                batch->state.damage_jump_buffer_x14[d_idx] != 0);
  if (!combat_attackairb_damageflytop_selected_body_source_owns_pre_gate(hb_id, cap_id) &&
      root_body_x14_strong_root_source == 0u) {
    return 0u;
  }
  const size_t flags_i = a_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  const uint8_t attacker_2218 = batch->state.state_flags[flags_i];
  const uint8_t root_body_create_edge_hidden_item_lane =
      (cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT &&
       batch->state.damage_jump_buffer_x14[d_idx] == 0 &&
       batch->state.hitbox_enable_edge[source_hb_i] != 0u &&
       (attacker_2218 & (uint8_t)MSL_STATE_FLAG_2218_B1) == 0u)
          ? 1u
          : 0u;
  if (cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT) {
    if (batch->state.damage_jump_buffer_x14[d_idx] == 0 &&
        root_body_create_edge_hidden_item_lane == 0u) {
      return 0u;
    }
  }
  // Live DamageFlyTop -> strong BAir create-HitCapsule pre-gate owner:
  // `Fighter_8006CDA4` runs before the `ftCo_8008DCE0` DamageFlyRoll gate. Replay seed generation
  // marks AttackAirB hits against active DamageFlyTop as exactly two pre-gate HSD_Randi advances.
  // The retained runtime owner requires the resolved BODY DmgLog source to be a concrete authored
  // strong AttackAirB HitCapsule created through ftAction_8007121C, plus the selected
  // HitCapsule/hurtcap source pair from ftColl_80076ED8. The first-create hb1/cap0 root/body
  // variant usually requires a live mv.co.damage.x14 jump-buffer carry from the current
  // DamageFlyTop episode (`ftCo_Damage_IASA` / `inlineC0`). The bounded first-create exception is
  // the source create-edge hb1/cap0 row where raw fp+0x2218_b1 is clear: that visible source byte
  // keeps the hidden Fighter_8006CDA4 item/bunny-hood branch lane separate from the x2218_b1 command
  // rows already locked as ordinary or seed-owned aggregate controls. Cap-1 contacts that need an
  // unmodeled stream step, hb0/cap2, and non-selected root/low contacts remain outside this hidden
  // Fighter_8006CDA4 stream-phase fallback.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_Damage_IASA}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json (source hurtcap slot/height)
  return 1u;
}

uint8_t combat_damageflyroll_damageflytop_attackairb_root_x14_primary_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS) ||
      batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] == 0u || source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairb(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  const size_t victim_flags_i =
      d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  if (hb_id != (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_ROOT_HITBOX ||
      cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT ||
      batch->state.damage_jump_buffer_x14[d_idx] == 0u ||
      (uint16_t)(batch->state.damage_jump_buffer_x14[d_idx] + 1u) !=
          batch->state.frame_start_hitstun[d_idx] ||
      (batch->state.state_flags[victim_flags_i] & (uint8_t)MSL_STATE_FLAG_2218_B1) == 0u ||
      !combat_attackairb_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                           source_hitbox_angle, source_hitbox_kbg,
                                                           source_hitbox_bkb)) {
    return 0u;
  }
  // Active DamageFlyTop -> strong BAir root/body primary branch:
  // the selected DmgLog source is the authored strong BAir root HitCapsule against cap0/root-body,
  // and the victim still carries the live mv.co.damage.x14 lane from this DamageFlyTop episode.
  // DamageFlyTop's callback decrements x14 before ProcessHit; when x14 is exactly one callback tick
  // below the frame-start hitstun countdown and raw fp+0x2218_b1 is still published, the
  // source-owned seed ledger marks only Fighter_8006CDA4's primary held-item/x418 branch before
  // ftCo_8008DCE0's DamageFlyRoll gate. Adjacent x14 root/body rows without this callback-phase
  // proof keep the existing two-consume or seed-owned paths.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_Damage_IASA}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return 1u;
}

uint8_t combat_damageflyroll_damageflytop_attackairb_hb0_cap1_effect_prefix_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS) ||
      batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] == 0u || batch->state.damage_jump_buffer_x14[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairb(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (hb_id != (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_ROOT_HITBOX ||
      cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_UPPER_BODY_SLOT ||
      !combat_attackairb_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                           source_hitbox_angle, source_hitbox_kbg,
                                                           source_hitbox_bkb)) {
    return 0u;
  }
  // Active DamageFlyTop -> strong BAir upper-body effect-prefix owner:
  // ftColl selected the authored strong BAir root HitCapsule (hb0, bone 4, 15/361/100/0) against
  // the victim's cap1 upper-body hurtcap while the current DamageFlyTop episode has already
  // drained mv.co.damage.x14. This source reaches ftCo_8008DCE0's DamageFlyRoll gate through the
  // current ProcessHit/DmgLog BODY path after ftColl_80078538's normal-hit visual-effect RNG
  // prefix, but does not own a hidden Fighter_8006CDA4 pre-gate advance. Keep this separate from
  // JumpAerial cap1 and the hb1/cap0 two-consume create-HitCapsule owner.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_Damage_IASA}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap1
  return 1u;
}

void combat_damageflyroll_consume_damageflytop_attackairb_live_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (combat_damageflyroll_damageflytop_attackairb_root_x14_primary_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb)) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
    return;
  }
  if (!combat_damageflyroll_damageflytop_attackairb_create_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid)) {
    return;
  }
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_cap_i >= cap_base && source_cap_i < cap_base + (size_t)MSL_MAX_HURTCAPS) {
    const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
    if (cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_LEG_LOW_SLOT) {
      return;
    }
    const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
    if (source_hb_i >= hb_base && source_hb_i < hb_base + (size_t)MSL_MAX_HITBOXES) {
      const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
      (void)hb_id;
    }
    if (cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT &&
        batch->state.damage_jump_buffer_x14[d_idx] == 0) {
      const size_t flags_i =
          a_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
      if (batch->state.hitbox_enable_edge[source_hb_i] != 0u &&
          (batch->state.state_flags[flags_i] & (uint8_t)MSL_STATE_FLAG_2218_B1) == 0u) {
        (void)combat_rng_consume_randi_site(
            batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
        (void)combat_rng_consume_randi_site(
            batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_SECONDARY, 1);
        (void)combat_rng_consume_randi_site(
            batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_TERTIARY, 1);
        return;
      }
    }
  }
  (void)a_idx;
  (void)source_hb_i;
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_SECONDARY, 1);
}

uint8_t combat_damageflyroll_landingairlw_strong_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_LANDING_AIR_LW ||
      batch->state.hitlag[d_idx] != 0u || !combat_source_motion_is_attackairn(source_motion_id) ||
      source_hb_valid == 0u || source_cap_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT) {
    return 0u;
  }
  if (!combat_attackairn_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                           source_hitbox_angle, source_hitbox_kbg,
                                                           source_hitbox_bkb)) {
    return 0u;
  }
  // LandingAirLw -> strong AttackAirN DamageFlyRoll owner:
  // ftColl selects the current strong NAir BODY HitCapsule against the head/high hurtcap while the
  // defender is still in LandingAirLw. The extracted 12-damage NAir payload plus selected cap2
  // DmgLog provenance owns the two Fighter_8006CDA4 pre-gate advances before ftCo_8008DCE0's
  // DamageFlyRoll HSD_Randf gate. LandingAirLw visible state alone remains rejected.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  return 1u;
}

void combat_damageflyroll_consume_landingairlw_strong_attackairn_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_landingairlw_strong_attackairn_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_SECONDARY, 1);
}

uint8_t combat_damageflyroll_catch_strong_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t defender_on_ground_before, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      !combat_action_is_catch_family(batch->state.action_id[d_idx]) ||
      defender_on_ground_before == 0u || batch->state.hitlag[d_idx] != 0u ||
      !combat_source_motion_is_attackairn(source_motion_id) || source_hb_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (!combat_attackairn_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                           source_hitbox_angle, source_hitbox_kbg,
                                                           source_hitbox_bkb)) {
    return 0u;
  }
  // Grounded Catch-family -> strong AttackAirN severe-airborne DamageFlyRoll owner:
  // the replay-visible Catch state is interrupted by a current ProcessHit BODY DmgLog entry before
  // ftCo_8008DCE0 runs. The selected source HitCapsule is the extracted strong NAir opening
  // payload, which carries one Fighter_8006CDA4 pre-gate HSD_Randi before the DamageFlyRoll gate in
  // the same source episode. `hitstun` has already been written by Fighter_ProcessHit when this
  // helper runs inside ftCo_8008DCE0, so the source proof is the captured Catch-family pre-action,
  // grounded pre-hit state, and current selected HitCapsule payload rather than a zero-hitstun
  // post-entry test. Visible Catch alone only admits the gate; this helper supplies the concrete
  // current-hit source for the hidden stream phase.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  return 1u;
}

uint8_t combat_damageflyroll_attacklw4_strong_attackairn_zero_marker_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_ATTACK_LW4 ||
      batch->state.on_ground[d_idx] == 0u || batch->state.hitlag[d_idx] != 0u ||
      !combat_source_motion_is_attackairn(source_motion_id) || source_hb_valid == 0u ||
      source_cap_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (hb_id != 1u || cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT ||
      !combat_attackairn_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                           source_hitbox_angle, source_hitbox_kbg,
                                                           source_hitbox_bkb)) {
    return 0u;
  }
  // AttackLw4 -> strong AttackAirN zero-consume DamageFlyRoll marker:
  // ftColl selected the current authored strong NAir hb1 HitCapsule against the victim's
  // cap2/head-high BODY hurtcap while the victim was still in grounded down-smash. Fighter_ProcessHit
  // launches the victim airborne before ftCo_8008DCE0 reaches the severe-airborne DamageFlyRoll
  // gate. The owner is the selected DmgLog HitCapsule/hurtcap payload, not visible AttackLw4 or
  // NAir shape alone.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  return 1u;
}

void combat_damageflyroll_consume_catch_strong_attackairn_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t defender_on_ground_before, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_catch_strong_attackairn_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, defender_on_ground_before,
          source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
          source_hitbox_bkb)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
}

uint8_t combat_damageflyroll_attackairn_strong_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint16_t defender_motion_id) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      defender_motion_id != (uint16_t)MSL_ACT_ATTACK_AIR_N || batch->state.on_ground[d_idx] != 0u ||
      !combat_source_motion_is_attackairb(source_motion_id) || source_hb_valid == 0u ||
      source_cap_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (!combat_attackairb_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                           source_hitbox_angle, source_hitbox_kbg,
                                                           source_hitbox_bkb)) {
    return 0u;
  }
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT) {
    const size_t bi = d_idx / (size_t)MSL_MAX_PLAYERS;
    const int defender = (int)(d_idx % (size_t)MSL_MAX_PLAYERS);
    const int num_players = (int)batch->config.num_players;
    const int prior_attacker = msl_damage_source_local_slot_from_port0(
        batch, (int)bi, num_players, batch->state.last_hit_by[a_idx]);
    if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL ||
        batch->state.hitlag[a_idx] == 0u || batch->state.hitstun[a_idx] == 0u ||
        prior_attacker != defender ||
        batch->state.instance_hit_by[a_idx] != batch->state.instance_id[d_idx]) {
      return 0u;
    }
  } else if (!msl_damage_owner_replay_rollout_advanced_under_rng_owner(batch, d_idx)) {
    return 0u;
  }
  // Current ProcessHit source owner for AttackAirN -> strong AttackAirB DamageFlyRoll:
  // exact one-step rows use the explicit Fighter_8006CDA4 seed lane, while free-running rollout
  // can prove the same source family from the DmgLog-selected BODY HitCapsule. Non-root BODY
  // hurtcaps still require an advanced replay-clock owner. The root/body cap0 reciprocal-trade path
  // is admitted only after the BAir attacker has already been damage-entered by this same victim in
  // the current ftColl pass; that live instance_hit_by proof replaces the replay seed lane without
  // turning cap0/root BAir into a generic stream-phase selector. This family carries one pre-gate
  // Fighter_8006CDA4 HSD_Randi before ftCo_8008DCE0's HSD_Randf gate.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return 1u;
}

void combat_damageflyroll_consume_attackairn_strong_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint16_t defender_motion_id) {
  if (!combat_damageflyroll_attackairn_strong_attackairb_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb, defender_motion_id)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
}

uint8_t combat_damageflyroll_attackairb_late_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb, uint16_t defender_motion_id) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      defender_motion_id != (uint16_t)MSL_ACT_ATTACK_AIR_B || batch->state.on_ground[d_idx] != 0u ||
      !combat_source_motion_is_attackairn(source_motion_id) || source_hb_valid == 0u ||
      source_cap_valid == 0u) {
    return 0u;
  }
  if (!msl_damage_owner_replay_rollout_advanced_under_rng_owner(batch, d_idx)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const size_t source_hb_id = source_hb_i - hb_base;
  if (source_hb_id != 0u) {
    return 0u;
  }
  if (!combat_attackairn_hitbox_payload_is_authored_late(
          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb)) {
    return 0u;
  }
  // Current ProcessHit source owner for AttackAirB -> late AttackAirN DamageFlyRoll:
  // this same-frame trade reaches ftCo_8008DCE0 with the authored late NAir root/body hb0
  // HitCapsule. Limb hb1/hb2 contacts use the same late payload but do not carry this gate owner.
  // The selected hb0 plus extracted payload proves the source owner; this family admits the
  // DamageFlyRoll gate but does not add a Fighter_8006CDA4 pre-gate consume (the explicit seed
  // marker for exact rows is `4`).
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  return 1u;
}

uint8_t combat_damageflyroll_specialhifall_late_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb) {
  // No attacker char-family gate: the victim-side spacie special states below are keyed on
  // the extracted MotionState kind, the attacker's move payload is verified against the
  // attacker's own extracted move data, and source ftCo_8008DCE0's DamageFlyRoll gate is
  // victim-side char-agnostic. The old gate reflected the spacie-vs-spacie validation
  // corpus, not the mechanism (de-spacie pass).
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      msl_motion_state_fx_special_kind(batch->state.char_id[d_idx],
                                       batch->state.action_id[d_idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      !combat_source_motion_is_attackairn(source_motion_id) || source_hb_valid == 0u ||
      source_cap_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  if (!combat_attackairn_hitbox_payload_is_authored_late(
          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb)) {
    return 0u;
  }
  // SpecialHiFall -> late AttackAirN DamageFlyRoll owner:
  // ftCo_8008DCE0 does not exclude up-special fall from the severe-airborne DamageFlyRoll
  // gate. Runtime admits the gate only when ftColl selected a concrete late NAir BODY HitCapsule
  // from the DmgLog path. The authored 9-damage Sakurai-angle payload plus selected BODY source
  // owns the normal-hit ftColl_80078538 effect prefix before the gate; visible SpecialHiFall state
  // alone remains rejected by msl_damage_owner_damageflyroll_pre_action_allows_gate.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::ftFx_SpecialHiFall_Phys
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  return 1u;
}

uint8_t combat_damageflyroll_specialhifall_attackairb_create_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id) {
  // No attacker char-family gate: the victim-side spacie special states below are keyed on
  // the extracted MotionState kind, the attacker's move payload is verified against the
  // attacker's own extracted move data, and source ftCo_8008DCE0's DamageFlyRoll gate is
  // victim-side char-agnostic. The old gate reflected the spacie-vs-spacie validation
  // corpus, not the mechanism (de-spacie pass).
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      msl_motion_state_fx_special_kind(batch->state.char_id[d_idx],
                                       batch->state.action_id[d_idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B &&
       !combat_source_motion_is_attackairb(source_motion_id)) ||
      source_hb_valid == 0u || source_cap_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  const int16_t first_create_frame = move_tables_attackair_first_create_hitbox_frame(
      batch->state.char_id[a_idx], (uint16_t)MSL_ACT_ATTACK_AIR_B);
  if (first_create_frame < 0 || batch->state.action_frame[a_idx] != first_create_frame ||
      hb_id != 0u || cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT ||
      !combat_attackairb_hitbox_is_authored_strong(hb_id,
                                                   batch->state.hitbox_damage[source_hb_i])) {
    return 0u;
  }
  // SpecialHiFall -> AttackAirB selected create-edge DamageFlyRoll owner:
  // the current DmgLog source must be the first-create BAir hb0/root-body HitCapsule selected from
  // the live attacker by ftColl_80076ED8/8007A06C. A different live BAir create edge is not enough;
  // steady selected BAir contacts and selected cap2/head-high contacts remain seed-owned and
  // follow the ordinary DamageFlyHi/N path.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076808,ftColl_800768A0,ftColl_80076ED8,ftColl_8007A06C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap0
  return 1u;
}

uint8_t combat_damageflyroll_catch_late_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_CATCH ||
      batch->state.animation_index[d_idx] != (uint32_t)MSL_SM_CATCH ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u ||
      !combat_source_motion_is_attackairn(source_motion_id) || source_hb_valid == 0u ||
      source_cap_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  if (!combat_attackairn_hitbox_payload_is_authored_late(
          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb)) {
    return 0u;
  }
  // Catch -> late AttackAirN DamageFlyRoll phase owner:
  // a current ftColl BODY DmgLog entry can interrupt Catch and route through ftCo_8008DCE0's
  // severe-airborne damage entry path. The source proof is the live Catch motion plus selected late
  // NAir HitCapsule payload and selected BODY hurtcap, not the numeric action id alone. This owns
  // the two Fighter_8006CDA4 pre-gate HSD_Randi advances observed in the replay seed lane for the
  // same source episode.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{ftCo_Catch_Anim,ftCo_800D8C54}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  return 1u;
}

void combat_damageflyroll_consume_catch_late_attackairn_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_catch_late_attackairn_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_valid,
          source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
          source_hitbox_bkb)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_SECONDARY, 1);
}

uint8_t combat_damageflyroll_jump_hitlag_strong_attackairn_tiplog_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  const uint16_t pre_action = batch->state.action_id[d_idx];
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      (pre_action != (uint16_t)MSL_ACT_JUMP_F && pre_action != (uint16_t)MSL_ACT_JUMP_B) ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] == 0u ||
      !combat_source_motion_is_attackairn(source_motion_id) || source_hb_valid == 0u) {
    return 0u;
  }
  if (batch->state.instance_hit_by[d_idx] != batch->state.instance_id[a_idx]) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (batch->state.hitbox_prev_enabled[source_hb_i] == 0u ||
      !combat_attackairn_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                           source_hitbox_angle, source_hitbox_kbg,
                                                           source_hitbox_bkb)) {
    return 0u;
  }
  // Strong NAir tip-log followup DamageFlyRoll owner:
  // the previous ftColl_80076ED8 pass can write defender hitlag/instance_hit_by through the
  // phantom/tip-log victims_2 path without full BODY damage. On the next collision pass the same
  // live strong NAir HitCapsule can produce ordinary severe airborne damage and reach
  // ftCo_8008DCE0's zero-prefix DamageFlyRoll HSD_Randf gate. The proof is the current selected
  // DmgLog HitCapsule payload plus hitlag-only JumpF/JumpB victim state from the same attacker
  // instance, not visible Jump action or replay row identity.
  // refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,ftColl_80076ED8,ftColl_8007A06C}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirN.events.create_hitbox
  return 1u;
}

void combat_damageflyroll_consume_jump_hitlag_strong_attackairn_tiplog_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_jump_hitlag_strong_attackairn_tiplog_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_motion_id,
          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_SECONDARY, 1);
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_TERTIARY, 1);
}

uint8_t combat_attackairlw_hitbox_is_authored_strong(uint8_t hb_id, float damage) {
  // Falco AttackAirLw's opening source HitCapsules are the authored 12-damage strong DAir pair.
  // Fox's multihit AttackAirLw and Falco's late DAir HitCapsules remain below this source-data
  // threshold and do not own Fighter_8006CDA4 pre-gate stream phase.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return (uint8_t)(hb_id <= 1u && damage >= 11.5f);
}

uint8_t combat_damageflyroll_attackairn_specialairhi_strong_attackairlw_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_N &&
       !(msl_motion_state_fx_special_kind(batch->state.char_id[d_idx],
                                          batch->state.action_id[d_idx]) ==
         (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI)) ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW || source_hb_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  if (batch->state.hitbox_enabled[source_hb_i] == 0u &&
      batch->state.hitbox_prev_enabled[source_hb_i] == 0u) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (!combat_attackairlw_hitbox_is_authored_strong(hb_id,
                                                    batch->state.hitbox_damage[source_hb_i])) {
    return 0u;
  }
  // Resolved BODY DmgLog source owner for strong AttackAirLw -> AttackAirN/SpecialAirHi
  // DamageFlyRoll:
  // Fighter_8006CDA4's held-item/x197C branch can advance the RNG stream before ftCo_8008DCE0's
  // DamageFlyRoll gate. In rollout, Slippi's hidden stream lane is not reseeded every frame, so the
  // runtime owner is the concrete ProcessHit source HitCapsule selected by ftColl_8007A06C:
  // active AttackAirLw, source_hb_i from the DmgLog entry, and authored strong dair damage.
  // SpecialAirHi is not excluded by ftCo_8008DCE0; it still requires this concrete source owner
  // before using the live/pre-gate RNG phase.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return 1u;
}

void combat_damageflyroll_consume_attackairn_specialairhi_strong_attackairlw_hitcapsule_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid) {
  if (!combat_damageflyroll_attackairn_specialairhi_strong_attackairlw_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_SECONDARY, 1);
}

uint8_t combat_downattacku_hitbox_is_authored_ground_sweep(const MslBatch* batch, size_t hb_i) {
  if (batch == NULL) {
    return 0u;
  }
  // Fox/Falco DownAttackU creates only 6-damage Sakurai-angle sweep HitCapsules in the supported
  // data. This names the extracted source owner without depending on character pair or replay row.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_DownAttackU.events.create_hitbox
  return (uint8_t)(batch->state.hitbox_damage[hb_i] == 6.0f &&
                   batch->state.hitbox_angle[hb_i] == 361u &&
                   batch->state.hitbox_kbg[hb_i] == 50u && batch->state.hitbox_bkb[hb_i] == 80u);
}

uint8_t combat_damageflyroll_recovering_ground_downattacku_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  const uint16_t defender_action = batch->state.action_id[d_idx];
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      !((defender_action == (uint16_t)MSL_ACT_LANDING_AIR_LW &&
         batch->state.action_frame[d_idx] > 0 && batch->state.action_frame[d_idx] < 16) ||
        (defender_action == (uint16_t)MSL_ACT_LANDING_FALL_SPECIAL &&
         batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_KNEE_BEND &&
         batch->state.action_frame[d_idx] >= 0 && batch->state.action_frame[d_idx] <= 3)) ||
      batch->state.hitlag[d_idx] != 0u ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_DOWN_ATTACK_U || source_hb_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  if (!combat_downattacku_hitbox_is_authored_ground_sweep(batch, source_hb_i)) {
    return 0u;
  }
  // LandingAirLw/LandingFallSpecial hidden Fighter_8006CDA4 phase:
  // source seed derivation proves LandingAirLw frame 1..15 maps to one pre-gate HSD_Randi before
  // ftCo_8008DCE0's DamageFlyRoll gate. KneeBend -> LandingFallSpecial's first callback has no
  // seed-lane marker, but the selected normal BODY damage source also owns ftColl_80078538's
  // damage-effect prefix before this one Fighter_8006CDA4 advance. Runtime admits that stream
  // phase only from the selected current DmgLog source; defender action shape or DownAttackU
  // visibility alone still does not advance RNG.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // bindings/msl_preprocess_native.c::msl_derive_fighter_8006cda4_pre_gate_count_py
  // data/moves/{fox,falco}.json::moves.ftCo_SM_DownAttackU.events.create_hitbox
  return 1u;
}

void combat_damageflyroll_consume_recovering_ground_downattacku_hitcapsule_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid) {
  if (!combat_damageflyroll_recovering_ground_downattacku_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
}

uint8_t combat_damageflyroll_thrownf_throwf_hitlag_owner(const MslBatch* batch, size_t d_idx,
                                                         size_t a_idx, int attacker,
                                                         uint8_t defender_on_ground_before) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  const uint8_t live_thrownf_hitlag =
      (uint8_t)(batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_THROWN_F &&
                defender_on_ground_before != 0u);
  const uint8_t released_thrownf_hitlag =
      (uint8_t)(batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_FALL &&
                batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_THROWN_F);
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_THROW_F ||
      (live_thrownf_hitlag == 0u && released_thrownf_hitlag == 0u) ||
      batch->state.hitlag_pre_timer[d_idx] == 0u) {
    return 0u;
  }
  // Grounded ThrowF -> ThrownF hitlag-exit DamageFlyRoll source owner:
  // the throw damage entry runs through the same ftCo_8008DCE0 path as fighter BODY damage after
  // Fighter_8006A1BC has just decremented hitlag to zero. Slippi exposes the active-hitlag episode
  // as ThrownF + ThrowF with x221A_b3/x221A_b2 on the preceding post-frame. Runtime can reach the
  // damage entry after throw_flow has detached the victim through Fall; `prev_action_id==ThrownF`
  // preserves that source transition without keying on a replay row. The source Fighter_8006CDA4
  // phase for this family is two pre-gate HSD_Randi calls, matching the explicit replay seed lane
  // used for one-step rows.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowF_Anim,ftCo_800DD724}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A1BC,Fighter_8006CDA4}
  return 1u;
}

void combat_damageflyroll_consume_thrownf_throwf_hitlag_count(MslBatch* batch, int bi, size_t d_idx,
                                                              size_t a_idx, int attacker,
                                                              uint8_t defender_on_ground_before) {
  if (!combat_damageflyroll_thrownf_throwf_hitlag_owner(batch, d_idx, a_idx, attacker,
                                                        defender_on_ground_before)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_SECONDARY, 1);
}

uint8_t combat_damageflyroll_attackairb_strong_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B || source_hb_valid == 0u) {
    return 0u;
  }
  if (!msl_damage_owner_replay_rollout_advanced_under_rng_owner(batch, d_idx)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (!combat_attackairb_hitbox_is_authored_strong(hb_id,
                                                   batch->state.hitbox_damage[source_hb_i])) {
    return 0u;
  }
  // Replay-rollout current ProcessHit owner for airborne AttackAirB -> strong AttackAirB
  // DamageFlyRoll entries:
  // exact one-step reseeds keep the HSD_Randf/Randi DamageFlyRoll phase seed-owned, while
  // free-running replay rollout can use the source-owned current ProcessHit RNG clock once
  // deterministic pre-gate state and the concrete authored strong Back-Air HitCapsule are aligned.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return 1u;
}

void combat_damageflyroll_consume_attackairb_strong_attackairb_hitcapsule_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid) {
  if (!combat_damageflyroll_attackairb_strong_attackairb_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
}

uint8_t combat_damageflyroll_dash_weak_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DASH || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] != 0u || !combat_source_motion_is_attackairb(source_motion_id) ||
      source_hb_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (!combat_attackairb_hitbox_payload_is_authored_weak(hb_id, source_hitcapsule_int_dmg,
                                                         source_hitbox_angle, source_hitbox_kbg,
                                                         source_hitbox_bkb)) {
    return 0u;
  }
  // Grounded Dash -> weak AttackAirB severe-airborne DamageFlyRoll owner:
  // ftColl_8007A06C records the selected BODY DmgLog entry, runs ftColl_80078538's normal-hit
  // visual RNG prefix, then Fighter_ProcessHit reaches ftCo_8008DCE0. The damage entry calls
  // Fighter_8006CDA4 before the DamageFlyRoll HSD_Randf gate. This source slice requires the
  // concrete authored weak BAir HitCapsule selected by the DmgLog path; visible Dash alone is not
  // enough to infer the hidden stream phase.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007A06C,ftColl_80078538}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return 1u;
}

void combat_damageflyroll_consume_dash_weak_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_dash_weak_attackairb_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_motion_id,
          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
}

uint8_t combat_damageflyroll_attackhi4_weak_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  const uint8_t attackairb_source =
      (uint8_t)(combat_source_motion_is_attackairb(source_motion_id) ||
                batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_B);
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_ATTACK_HI4 ||
      defender_on_ground_before == 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] != 0u || attackairb_source == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  uint8_t weak_bair_source = 0u;
  if (source_hb_valid != 0u && source_hb_i >= hb_base &&
      source_hb_i < hb_base + (size_t)MSL_MAX_HITBOXES) {
    const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
    weak_bair_source = combat_attackairb_hitbox_payload_is_authored_weak(
        hb_id, source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
        source_hitbox_bkb);
  } else {
    const size_t live_hb_i = hb_base + (size_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX;
    weak_bair_source =
        (uint8_t)(batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
                  batch->state.hitbox_enabled[live_hb_i] != 0u &&
                  combat_attackairb_hitbox_payload_is_authored_weak(
                      (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX,
                      (int)batch->state.hitbox_damage[live_hb_i],
                      batch->state.hitbox_angle[live_hb_i], batch->state.hitbox_kbg[live_hb_i],
                      batch->state.hitbox_bkb[live_hb_i]) != 0u);
  }
  if (weak_bair_source == 0u) {
    return 0u;
  }
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_cap_valid == 0u || source_cap_i < cap_base ||
      source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != 3u) {
    return 0u;
  }
  // Grounded AttackHi4 -> weak AttackAirB severe-airborne DamageFlyRoll owner:
  // ftColl selected the authored weak BAir hb1 HitCapsule against extracted hurtcap slot 3 while
  // the victim was still in grounded up-smash. The selected DmgLog payload reaches
  // Fighter_8006CDA4 before ftCo_8008DCE0's DamageFlyRoll gate and owns one primary consume.
  // Visible AttackHi4 state alone is not sufficient; runtime requires either selected hb/payload
  // provenance from ftColl_80076ED8/ftColl_8007A06C plus selected cap3 provenance. The live-hb
  // payload fallback only covers missing source_hb_valid; it still requires the selected cap lane.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80078538,ftColl_8007A06C}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap3
  return 1u;
}

void combat_damageflyroll_consume_attackhi4_weak_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  if (!combat_damageflyroll_attackhi4_weak_attackairb_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before)) {
    return;
  }
  for (uint8_t i = 0u;
       i < (uint8_t)MSL_DAMAGEFLYROLL_ATTACKHI4_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_PRIMARY_CONSUMES;
       i++) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  }
}

void combat_damageflyroll_consume_kneebend_attacks3_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_kneebend_attacks3_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_valid,
          source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
          source_hitbox_bkb)) {
    return;
  }
  // This is the source-owned Fighter_8006CDA4 primary-callsite prefix for the same selected
  // AttackS3/KneeBend damage episode. Fighter_8006CDA4 has explicit HSD_Randi callsites at
  // x418/x41C/x418; the BHH source trace reaches the x418 primary site five times before
  // ftCo_8008DCE0's DamageFlyRoll gate. Keep the count named and ledgered instead of treating it as
  // a row-local phase tweak.
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/build/GALE01/asm/melee/ft/fighter.s::{8006CE94,8006CEC8,8006CF10}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  for (uint8_t i = 0u;
       i < (uint8_t)MSL_DAMAGEFLYROLL_KNEEBEND_ATTACKS3_FIGHTER_8006CDA4_PRIMARY_CONSUMES; i++) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  }
}

uint8_t combat_damageflyroll_weak_attackairb_source_skips_x1994(
    const MslCombatProcessHitResolved* ev) {
  if (ev == NULL || ev->source_hb_valid == 0u ||
      !combat_source_motion_is_attackairb(ev->source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = ev->a_idx * (size_t)MSL_MAX_HITBOXES;
  if (ev->source_hb_i < hb_base || ev->source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(ev->source_hb_i - hb_base);
  // Weak AttackAirB DamageFlyRoll owner:
  // PJO's grounded Dash -> weak BAir selected HitCapsule enters DamageFlyRoll, but the later
  // landing/FallSpecial row is damage-eligible in replay, so this source episode does not carry
  // the stale x1994 hit-status protection that stronger DamageFlyRoll rows do. Keep the split on
  // the authored weak BAir payload selected by ftColl_80076ED8 rather than on replay row shape.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_8008DCE0,ftCo_Damage_OnExitHitlag}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return combat_attackairb_hitbox_payload_is_authored_weak(
      hb_id, ev->source_hitcapsule_int_dmg, ev->source_hitbox_angle, ev->source_hitbox_kbg,
      ev->source_hitbox_bkb);
}

uint8_t combat_damageflyroll_specialairhi_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid) {
  // No attacker char-family gate: the victim-side spacie special states below are keyed on
  // the extracted MotionState kind, the attacker's move payload is verified against the
  // attacker's own extracted move data, and source ftCo_8008DCE0's DamageFlyRoll gate is
  // victim-side char-agnostic. The old gate reflected the spacie-vs-spacie validation
  // corpus, not the mechanism (de-spacie pass).
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  enum { MSL_DAMAGEFLYROLL_SPECIALHIFALL_ENTRY_ENABLE_EDGE_FRAME_MAX = 3 };
  const uint16_t victim_action = batch->state.action_id[d_idx];
  const uint8_t victim_fx_kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[d_idx], victim_action);
  const uint8_t specialhifall_entry =
      (uint8_t)(victim_fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL &&
                batch->state.action_frame[d_idx] <=
                    MSL_DAMAGEFLYROLL_SPECIALHIFALL_ENTRY_ENABLE_EDGE_FRAME_MAX);
  if ((victim_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI && specialhifall_entry == 0u) ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B || source_hb_valid == 0u) {
    return 0u;
  }
  if (!msl_damage_owner_replay_rollout_advanced_under_rng_owner(batch, d_idx)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (hb_id >= (uint8_t)MSL_MAX_HITBOXES || batch->state.hitbox_enabled[source_hb_i] == 0u) {
    return 0u;
  }
  if (source_cap_valid == 0u) {
    return 0u;
  }
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_XROTN_SLOT) {
    return 0u;
  }
  if (cap_id == (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT) {
    const uint8_t strong_bair =
        combat_attackairb_hitbox_is_authored_strong(hb_id, batch->state.hitbox_damage[source_hb_i]);
    const uint8_t early_weak_hb2_bair =
        (uint8_t)(hb_id == 2u &&
                  combat_attackairb_hitbox_payload_is_authored_weak(
                      hb_id, combat_get_env_dmg(batch->state.hitbox_damage[source_hb_i]),
                      batch->state.hitbox_angle[source_hb_i], batch->state.hitbox_kbg[source_hb_i],
                      batch->state.hitbox_bkb[source_hb_i]));
    if (!strong_bair && !early_weak_hb2_bair) {
      return 0u;
    }
  }
  // Source-specific SpecialAirHi / entry-window SpecialHiFall DamageFlyRoll admission:
  // `ftCo_8008DCE0` does not exclude up-special damage states, but replay-exact rollout still needs
  // a concrete current ProcessHit owner before using the live RNG clock. The positive source slice
  // is a live AttackAirB HitCapsule plus selected BODY hurtcap from ftColl_80076ED8/ftColl_8007A06C.
  // SpecialHiFall is bounded to the first callbacks, before the existing late-row selected-source
  // rejection takes over. cap2/head-high is admitted only when the selected current BAir payload is
  // the authored strong BackAir source or the early weak hb2 source. Late weak hb0/hb1 cap2 rows
  // remain seed-owned; cap12/XRotN remains the high-pose DamageFlyTop provenance path used by the
  // narrowed ordinary DamageFlyN/Hi owner. Do not infer a live gate from visible action shape alone.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2/cap12
  return 1u;
}

uint8_t combat_damageflyroll_specialhifall_strong_attackairb_cap2_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  // No attacker char-family gate: the victim-side spacie special states below are keyed on
  // the extracted MotionState kind, the attacker's move payload is verified against the
  // attacker's own extracted move data, and source ftCo_8008DCE0's DamageFlyRoll gate is
  // victim-side char-agnostic. The old gate reflected the spacie-vs-spacie validation
  // corpus, not the mechanism (de-spacie pass).
  if (!combat_damageflyroll_specialairhi_attackairb_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid)) {
    return 0u;
  }
  if (batch == NULL ||
      msl_motion_state_fx_special_kind(batch->state.char_id[d_idx],
                                       batch->state.action_id[d_idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL ||
      batch->state.action_frame[d_idx] > 3 ||
      !combat_source_motion_is_attackairb(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (hb_id != (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX ||
      cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT ||
      !combat_attackairb_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                           source_hitbox_angle, source_hitbox_kbg,
                                                           source_hitbox_bkb)) {
    return 0u;
  }
  return 1u;
}

void combat_damageflyroll_consume_specialhifall_strong_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  if (!combat_damageflyroll_specialhifall_strong_attackairb_cap2_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb)) {
    return;
  }
  uint8_t consume_count =
      MSL_DAMAGEFLYROLL_SPECIALHIFALL_STRONG_ATTACKAIRB_CREATE_FIGHTER_8006CDA4_CONSUMES;
  if (batch->state.hitbox_enable_edge[source_hb_i] == 0u) {
    const size_t flags_i =
        d_idx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
    const uint8_t flags_2218 = batch->state.state_flags[flags_i];
    const uint8_t reflect_entry_mask =
        (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                  MSL_STATE_FLAG_2218_B2 | MSL_STATE_FLAG_2218_REFLECTING |
                  MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR);
    const uint8_t reflect_entry_value =
        (uint8_t)(MSL_STATE_FLAG_2218_ALLOW_INTERRUPT | MSL_STATE_FLAG_2218_B1 |
                  MSL_STATE_FLAG_2218_REFLECT_BEHAVIOR);
    if (batch->state.action_frame[d_idx] > 1 ||
        (flags_2218 & reflect_entry_mask) != reflect_entry_value) {
      return;
    }
    consume_count =
        MSL_DAMAGEFLYROLL_SPECIALHIFALL_STRONG_ATTACKAIRB_CONTINUING_REFLECT_FIGHTER_8006CDA4_CONSUMES;
  }
  // SpecialHiFall entry -> strong AttackAirB cap2/head-high DamageFlyRoll phase owner:
  // ftColl selected the authored strong BackAir tail HitCapsule against cap2 while Fox's up-special
  // fall entry is still in the same source episode as ftFx_SpecialAirHi_Coll. Source traces split
  // the create edge and the first continuing reflect-behavior callback: create-edge owns one
  // Fighter_8006CDA4 primary consume, while the continuing reflected-entry source state owns two
  // primary consumes before ftCo_8008DCE0's DamageFlyRoll gate. This is selected hb/cap/payload and
  // x2218 source-state provenance, not a broad SpecialHiFall or Back-Air action predicate.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
  //   ftFx_SpecialAirHi_Coll,ftFx_SpecialHiFall_Anim}
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  for (uint8_t i = 0u; i < consume_count; i++) {
    (void)combat_rng_consume_randi_site(
        batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
  }
}

uint8_t combat_damageflyroll_specialairs_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid) {
  // No attacker char-family gate: the victim-side spacie special states below are keyed on
  // the extracted MotionState kind, the attacker's move payload is verified against the
  // attacker's own extracted move data, and source ftCo_8008DCE0's DamageFlyRoll gate is
  // victim-side char-agnostic. The old gate reflected the spacie-vs-spacie validation
  // corpus, not the mechanism (de-spacie pass).
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (msl_motion_state_fx_special_kind(batch->state.char_id[d_idx],
                                       batch->state.action_id[d_idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S ||
      batch->state.on_ground[d_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B || source_hb_valid == 0u ||
      source_cap_valid == 0u) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (hb_id != (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX ||
      cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT ||
      batch->state.hitbox_enable_edge[source_hb_i] == 0u ||
      !combat_attackairb_hitbox_is_authored_strong(hb_id,
                                                   batch->state.hitbox_damage[source_hb_i])) {
    return 0u;
  }
  // SpecialAirS startup -> strong AttackAirB DamageFlyRoll admission:
  // ftCo_8008DCE0 does not exclude side-special startup from the severe-airborne DamageFlyRoll
  // gate. Runtime admits the gate only when ftColl selected the create-edge strong BAir tail
  // HitCapsule against the root/body hurtcap; this source proof does not add Fighter_8006CDA4
  // pre-gate advances and is not inferred from visible SpecialAirS state alone.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap0
  return 1u;
}

uint8_t combat_damageflyroll_speciallw_start_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb) {
  (void)attacker;
  if (batch == NULL || a_idx == d_idx) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u || batch->state.on_ground[a_idx] == 0u ||
      source_hb_valid == 0u ||
      !combat_source_motion_is_speciallw_start(batch->state.char_id[a_idx], source_motion_id)) {
    return 0u;
  }
  const uint16_t victim_action = batch->state.action_id[d_idx];
  if (victim_action != (uint16_t)MSL_ACT_KNEE_BEND && victim_action != (uint16_t)MSL_ACT_JUMP_F &&
      victim_action != (uint16_t)MSL_ACT_JUMP_B && victim_action != (uint16_t)MSL_ACT_DAMAGE_LW_1 &&
      victim_action != (uint16_t)MSL_ACT_DAMAGE_LW_2 &&
      victim_action != (uint16_t)MSL_ACT_DAMAGE_LW_3 &&
      victim_action != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  if (hb_id != 0u) {
    return 0u;
  }
  (void)source_hitcapsule_int_dmg;
  (void)source_hitbox_angle;
  (void)source_hitbox_kbg;
  (void)source_hitbox_bkb;
  if (!combat_speciallw_start_source_payload_is_authored_reflector_start(
          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb)) {
    return 0u;
  }
  // Current ProcessHit owner for grounded SpecialLwStart DamageFlyRoll:
  // replay seeds can expose this family as a grounded KneeBend zero-consume marker. In a
  // free-running rollout, input/action callbacks can advance either side before the combat pass,
  // but the selected BODY DmgLog source is still the authored grounded Reflector startup
  // HitCapsule. The same ProcessHit source can strike a DamageLw* or terminal DamageFlyTop victim:
  // ftCo_8008DCE0 then reaches its DamageFlyRoll HSD_Randf gate without any Fighter_8006CDA4
  // pre-gate stream advance. This same-frame source owner may use the replay frame-start seed
  // directly because the selected reflector HitCapsule is the bounded RNG-site proof; visible
  // jump/KneeBend/DamageFlyTop shape alone is not enough. `hitstun` is intentionally not a
  // zero-state proof here because
  // Fighter_ProcessHit writes the new hitstun before `ftCo_8008DCE0` reaches the DamageFlyRoll
  // gate.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
  //   ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
  // data/moves/{fox,falco}.json::specials_by_msid["313"|"317"].events.create_hitbox
  return 1u;
}

uint8_t combat_damageflyroll_speciallw_end_strong_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  // No attacker char-family gate: the victim-side spacie special states below are keyed on
  // the extracted MotionState kind, the attacker's move payload is verified against the
  // attacker's own extracted move data, and source ftCo_8008DCE0's DamageFlyRoll gate is
  // victim-side char-agnostic. The old gate reflected the spacie-vs-spacie validation
  // corpus, not the mechanism (de-spacie pass).
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      msl_motion_state_fx_special_kind(batch->state.char_id[d_idx],
                                       batch->state.action_id[d_idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_LW_END ||
      defender_on_ground_before == 0u || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairb(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT ||
      !combat_attackairb_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                           source_hitbox_angle, source_hitbox_kbg,
                                                           source_hitbox_bkb)) {
    return 0u;
  }
  // Grounded Reflector end -> strong BackAir DamageFlyRoll stream owner:
  // ftColl selected the current authored strong AttackAirB HitCapsule against the victim's
  // cap2/head-high BODY hurtcap while ftFx_SpecialLwEnd is still in its grounded end callback.
  // Source then reaches Fighter_ProcessHit -> Fighter_8006CDA4 before ftCo_8008DCE0's
  // DamageFlyRoll HSD_Randf gate. This reconstructs the one primary pre-gate HSD_Randi only from
  // the selected DmgLog HitCapsule/hurtcap provenance; weak/late BAir hb2 rows and unselected
  // Reflector-end action shape remain seed/live-clock owned.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLwEnd_Anim
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  return 1u;
}

void combat_damageflyroll_consume_speciallw_end_strong_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  if (!combat_damageflyroll_speciallw_end_strong_attackairb_hitcapsule_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before)) {
    return;
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY, 1);
}

uint8_t combat_damageflyroll_speciallw_end_continuing_weak_attackairb_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  // No attacker char-family gate: the victim-side spacie special states below are keyed on
  // the extracted MotionState kind, the attacker's move payload is verified against the
  // attacker's own extracted move data, and source ftCo_8008DCE0's DamageFlyRoll gate is
  // victim-side char-agnostic. The old gate reflected the spacie-vs-spacie validation
  // corpus, not the mechanism (de-spacie pass).
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u ||
      msl_motion_state_fx_special_kind(batch->state.char_id[d_idx],
                                       batch->state.action_id[d_idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_LW_END ||
      defender_on_ground_before == 0u || batch->state.hitlag[d_idx] != 0u ||
      source_hb_valid == 0u || source_cap_valid == 0u ||
      !combat_source_motion_is_attackairb(source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_hb_i < hb_base || source_hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES ||
      source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT ||
      batch->state.hitbox_enable_edge[source_hb_i] != 0u ||
      !combat_attackairb_hitbox_payload_is_authored_weak(hb_id, source_hitcapsule_int_dmg,
                                                         source_hitbox_angle, source_hitbox_kbg,
                                                         source_hitbox_bkb)) {
    return 0u;
  }
  // Grounded Reflector end -> continuing weak BackAir DamageFlyRoll stream owner:
  // the selected DmgLog source is the already-live weak BAir HitCapsule against cap2/head-high,
  // not the frame-4 create-edge hb2 source. Source order runs ftColl_80078538's normal-hit visual
  // effect prefix before Fighter_ProcessHit, then Fighter_8006CDA4's three HSD_Randi callsites
  // before ftCo_8008DCE0 samples DamageFlyRoll. Keep this separated from create-edge weak BAir
  // rows by the live HitCapsule enable-edge bit and from strong BAir by the authored payload.
  // The consume counts below are named from this source path: four normal-hit visual effect
  // HSD_Randi draws in ftColl_80078538, then one primary/secondary/tertiary Fighter_8006CDA4 draw
  // before the DamageFlyRoll HSD_Randf gate.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007A06C,ftColl_80078538}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80078538
  // refs/melee/build/GALE01/asm/melee/ft/fighter.s::Fighter_8006CDA4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  return 1u;
}

void combat_damageflyroll_consume_speciallw_end_continuing_weak_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before) {
  if (!combat_damageflyroll_speciallw_end_continuing_weak_attackairb_owner(
          batch, d_idx, a_idx, attacker, source_hb_i, source_hb_valid, source_cap_i,
          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
          source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before)) {
    return;
  }
  for (uint8_t i = 0u;
       i < (uint8_t)MSL_DAMAGEFLYROLL_SPECIALLW_END_WEAK_ATTACKAIRB_FTCOLL_DAMAGE_EFFECT_CONSUMES;
       i++) {
    (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_FTCOLL_DAMAGE_EFFECT, 1);
  }
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_PRIMARY,
      MSL_DAMAGEFLYROLL_SPECIALLW_END_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_PRIMARY_CONSUMES);
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_SECONDARY,
      MSL_DAMAGEFLYROLL_SPECIALLW_END_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_SECONDARY_CONSUMES);
  (void)combat_rng_consume_randi_site(
      batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_FIGHTER_8006CDA4_TERTIARY,
      MSL_DAMAGEFLYROLL_SPECIALLW_END_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_TERTIARY_CONSUMES);
}

uint8_t combat_damageflyroll_selected_source_normal_effect_prefix_count(
    const MslBatch* batch, size_t d_idx, size_t source_hb_i, uint8_t source_hb_valid,
    uint8_t source_cap_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb,
    uint16_t pre_action) {
  // ftColl_80078538 normal-hit visual-effect RNG prefix:
  // the BODY DmgLog loop calls ftColl_80078538 once per accepted normal-element
  // (`ftColl_803C0CAC[element] == 0x3E8`) DmgLogEntry before Fighter_ProcessHit reaches
  // ftCo_8008DCE0's DamageFlyRoll HSD_Randf gate. This helper returns the number of site-24
  // HSD_Randi calls still missing from the runtime trace, not a raw DmgLog entry count: most
  // selected-source owners are one call per accepted entry, while the AttackAirB/strong-DAir root
  // owner below is explicitly a post-selected-entry eight-call prefix.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077AD8,ftColl_80078538}
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
  if (batch == NULL || source_hb_valid == 0u || source_cap_valid == 0u) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(source_hb_i % (size_t)MSL_MAX_HITBOXES);
  const float wsk_dmg = (float)source_hitcapsule_int_dmg;
  const uint16_t wsk = batch->state.hitbox_wsk[source_hb_i];
  const uint8_t enable_edge = batch->state.hitbox_enable_edge[source_hb_i];
  const uint8_t source_is_strong_dair =
      (uint8_t)((source_motion_id == (uint16_t)MSL_ACT_ATTACK_AIR_LW ||
                 source_motion_id == (uint16_t)MSL_SM_ATTACK_AIR_LW) &&
                combat_attackairlw_hitbox_payload_is_authored_strong_meteor(
                    hb_id, wsk_dmg, source_hitbox_angle, source_hitbox_kbg, wsk,
                    source_hitbox_bkb));
  if (source_is_strong_dair && enable_edge != 0u && pre_action == (uint16_t)MSL_ACT_ATTACK_AIR_F) {
    // AttackAirF victim on the strong DAir create edge: one accepted DmgLogEntry, one
    // normal-hit effect draw before the gate (MAJ rec3948 randf stream: draw #2 = 0.064 < 0.3).
    // Broader aerial-attack-class victims are NOT equivalent owners: the prefix count is the
    // victim's full per-frame DmgLog entry set, and an adjacent AttackAirB victim row (PRH
    // rec2113, vanilla no-roll only at draw #4) proves a different hidden entry count under the
    // same selected source. Keep this owner on the witnessed single-entry shape.
    return 1u;
  }
  if (source_is_strong_dair && enable_edge != 0u && pre_action == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
      hb_id == 0u) {
    // AttackAirB victim on the strong DAir hb0 create edge: the BODY sweep accepts the root
    // HitCapsule source and the same source DAir packet contributes the remaining normal-hit
    // visual-effect entries before Fighter_ProcessHit reaches ftCo_8008DCE0. The generic DmgLog
    // effect path has already consumed the selected entry; the PTE rec9389 trace lock proves this
    // owner contributes exactly eight additional site-24 HSD_Randi calls before the gate. This is
    // bounded by extracted strong DAir hb0 payload plus selected AttackAirB recovery action, not by
    // replay row or RNG outcome.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C,ftColl_80078538}
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
    return 8u;
  }
  if (source_is_strong_dair && enable_edge == 0u &&
      (msl_motion_state_fx_special_kind(batch->state.char_id[d_idx], pre_action) ==
           (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD ||
       msl_motion_state_fx_special_kind(batch->state.char_id[d_idx], pre_action) ==
           (uint8_t)MSL_FX_KIND_SPECIAL_HI_HOLD_AIR)) {
    // Sustained strong DAir against a FireFox/FireBird charge victim: both live hb0/hb1
    // HitCapsules log entries against the stationary charge hurt envelope, two effect draws
    // before the gate (MAJ rec8959 randf stream: draw #3 = 0.059).
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHiHold_Anim}
    return 2u;
  }
  if ((source_motion_id == (uint16_t)MSL_ACT_ATTACK_AIR_B ||
       source_motion_id == (uint16_t)MSL_SM_ATTACK_AIR_B) &&
      enable_edge == 0u && pre_action == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
      batch->state.hitstun[d_idx] != 0u &&
      hb_id == (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX &&
      combat_attackairb_hitbox_is_authored_strong(hb_id, wsk_dmg)) {
    // Sustained strong BAir against an active DamageFlyTop victim: both live strong HitCapsules
    // (hb0 root + hb1 tail) log normal-element entries against the tumbling victim, two effect
    // draws before the gate (MAJ rec4166 randf stream: draw #3 = 0.237 < 0.3; the selected
    // source is the live tail hb1, enable_edge==0, outside the create-edge and root/x14
    // DamageFlyTop owners above).
    // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
    return 2u;
  }
  if ((source_motion_id == (uint16_t)MSL_ACT_ATTACK_LW4 ||
       source_motion_id == (uint16_t)MSL_SM_ATTACK_LW4) &&
      enable_edge != 0u &&
      combat_attacklw4_hitbox_payload_is_authored_strong(hb_id, source_hitcapsule_int_dmg,
                                                         source_hitbox_angle, source_hitbox_kbg,
                                                         source_hitbox_bkb) &&
      pre_action == (uint16_t)MSL_ACT_DOWN_BACK_D) {
    // Down-smash create edge against a downed roll victim: one accepted DmgLogEntry, one
    // effect draw before the gate (MAJ rec8737 randf stream: draw #2 = 0.253).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBack.c
    return 1u;
  }
  if ((source_motion_id == (uint16_t)MSL_ACT_ATTACK_11 ||
       source_motion_id == (uint16_t)MSL_SM_ATTACK_11) &&
      pre_action == (uint16_t)MSL_ACT_CATCH &&
      combat_attack11_hitbox_payload_is_authored_jab(hb_id, source_hitcapsule_int_dmg,
                                                     source_hitbox_angle, source_hitbox_kbg,
                                                     source_hitbox_bkb)) {
    // Catch interrupted by selected Attack11 jab BODY source: ftColl logs the accepted normal
    // HitCapsule entry and calls ftColl_80078538 once before Fighter_ProcessHit routes the victim
    // through ftCo_8008DCE0. This owns one visual-effect RNG prefix but no Fighter_8006CDA4
    // held-item/x197C pre-gate consume.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077AD8,ftColl_80078538,ftColl_8007A06C}
    // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    // data/moves/{fox,falco}.json::moves.ftCo_SM_Attack11.events.create_hitbox
    return 1u;
  }
  return 0u;
}

uint8_t combat_damageflyroll_jumpaerial_attackairb_carry_selected_owner(const MslBatch* batch,
                                                                        size_t d_idx, size_t a_idx,
                                                                        int attacker,
                                                                        size_t source_cap_i,
                                                                        uint8_t source_cap_valid) {
  if (batch == NULL || source_cap_valid == 0u) {
    return 0u;
  }
  const size_t cap_base = d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (source_cap_i < cap_base || source_cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t cap_id = (uint8_t)(source_cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT) {
    // JumpAerial -> AttackAirB carry is the selected cap2/head-high BODY lane preserved by
    // ftColl's DmgLog source, not visible JumpAerial + BAir shape alone. Cap1 and unrelated
    // hurtcap selections stay seed-owned at the DamageFlyRoll gate.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
    // data/hurtcaps/{fox,falco}.json cap2
    return 0u;
  }
  return msl_damage_owner_damageflyroll_jumpaerial_attackairb_carry(batch, d_idx, a_idx, attacker);
}

uint8_t combat_damageflyroll_jumpaerial_illusion_article_owner(const MslBatch* batch, size_t d_idx,
                                                               size_t a_idx, int attacker,
                                                               uint8_t source_hb_valid,
                                                               uint16_t source_item_type,
                                                               uint8_t source_item_state) {
  // No attacker char-family gate: the victim-side spacie special states below are keyed on
  // the extracted MotionState kind, the attacker's move payload is verified against the
  // attacker's own extracted move data, and source ftCo_8008DCE0's DamageFlyRoll gate is
  // victim-side char-agnostic. The old gate reflected the spacie-vs-spacie validation
  // corpus, not the mechanism (de-spacie pass).
  if (batch == NULL || attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return 0u;
  }
  const uint16_t pre_action = batch->state.action_id[d_idx];
  if (pre_action != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
      pre_action != (uint16_t)MSL_ACT_JUMP_AERIAL_B) {
    return 0u;
  }
  if (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u || source_hb_valid != 0u ||
      source_item_state >= 2u || !item_article_params_is_illusion_item_type(source_item_type)) {
    return 0u;
  }
  {
    const uint8_t a_fx_kind = msl_motion_state_fx_special_kind(batch->state.char_id[a_idx],
                                                               batch->state.action_id[a_idx]);
    if (a_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_S &&
        a_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S &&
        a_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_S_END &&
        a_fx_kind != (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END) {
      return 0u;
    }
  }
  // JumpAerial -> Illusion/Phantasm article BODY owner:
  // side-special articles are generated-data-backed item kinds (MSLITAR1). They enter
  // `combat_apply_item_hit`, so the shared damage entry has no fighter HitCapsule/hurtcap source.
  // Keep the JumpAerial AttackAirB cap2 carry narrowed to fighter BODY hits, but admit the
  // DamageFlyRoll gate for the concrete item article source. State 0/1 are the only damaging
  // Illusion/Phantasm article states; state2 is lifetime-only and never reaches this item BODY
  // path.
  // refs/melee/src/melee/it/items/itfoxillusion.c::{
  //   itFoxillusion_UnkMotion0_Phys,itFoxillusion_UnkMotion1_Phys,itFoxIllusion_Logic14_DmgDealt}
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/items/articles/fox_falco.bin::MSLITAR1 side_special_illusion_itkind
  return 1u;
}

void combat_damageflyroll_consume_jumpaerial_attackairb_carry(MslBatch* batch, int bi, size_t d_idx,
                                                              size_t a_idx, int attacker,
                                                              size_t source_cap_i,
                                                              uint8_t source_cap_valid) {
  if (combat_damageflyroll_jumpaerial_attackairb_carry_selected_owner(
          batch, d_idx, a_idx, attacker, source_cap_i, source_cap_valid)) {
    combat_rng_consume_step_site(batch, bi,
                                 MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_JUMPAERIAL_ATTACKAIRB_CARRY);
  }
}

uint8_t combat_sheik_chain_start_terminal_owns_low_hurt_height(const MslBatch* batch,
                                                               size_t source_a_idx,
                                                               size_t source_hb_i,
                                                               uint8_t defender_on_ground_before,
                                                               uint8_t source_hb_valid) {
  if (batch == NULL || source_hb_valid == 0u || defender_on_ground_before == 0u ||
      batch->state.char_id[source_a_idx] != (uint8_t)MSL_CHAR_ID_SHEIK) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[source_a_idx];
  if (action != (uint16_t)MSL_ACT_SK_SPECIAL_S_START &&
      action != (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_START) {
    return 0u;
  }
  if ((source_hb_i % (size_t)MSL_MAX_HITBOXES) != 3u) {
    return 0u;
  }
  // Chain Start terminal frontier DmgLog height:
  // source `it_802BCB88` publishes hb3 as the terminal Chain payload while
  // `ftColl_8007A06C` selects the grounded DamageHi/N/Lw group from the accepted DmgLog hurt
  // height. The terminal Start payload's replay-visible KB/damage pair selects DamageLw2 even
  // when the lite matrix scaffold also overlaps a high torso capsule, so keep the source hb3 DmgLog
  // height low instead of letting the scaffold's higher sibling reclassify the same hit.
  // refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007A06C}
  return 1u;
}
