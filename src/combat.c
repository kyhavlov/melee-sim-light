#include "combat_internal.h"

static inline uint8_t combat_apply_throw_hit_core(MslBatch* batch, int batch_index, int attacker,
                                                  int defender, const MslThrowHitboxParams* p,
                                                  uint8_t update_bookkeeping);
static inline void combat_throw_release_integrate_position_now(MslBatch* batch, size_t owner_idx,
                                                               size_t victim_idx);
static inline void combat_throw_release_apply_immediate_di(MslBatch* batch, size_t victim_idx,
                                                           const MslCommonParams* c);
static inline uint8_t combat_defender_hit_status_u8(const MslBatch* batch, size_t d_idx);
static inline uint8_t combat_specialhi_frozen_guard_dense_seed_allows_live_shield(
    MslBatch* batch, const MslCommonParams* c, int bi, int attacker, int defender, int hb_id,
    size_t a_idx, size_t d_idx, uint16_t defender_iid, uint8_t shield_seed_kind, float hx, float hy,
    float hz, float hr, float shx, float shy, float shz, float shr,
    uint8_t shield_desc_envelope_ready, uint8_t shield_extent_bridge_active,
    uint8_t guard_reflect_reflectdesc_only);

static inline uint32_t combat_hsd_rand_step(uint32_t seed) {
  // HSD global RNG LCG step:
  // refs/melee/src/sysdolphin/baselib/random.c::{HSD_Rand,HSD_Randf}
  return seed * 214013u + 2531011u;
}

static inline uint16_t combat_hsd_rand_u16_consume_site(MslBatch* batch, int bi, uint16_t site_id) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  if (batch->rng_shadow_seed == NULL || batch->rng_site_counts == NULL) {
    return 0u;
  }
  if (site_id >= (uint16_t)MSL_RNG_SITE_COUNT) {
    return 0u;
  }
  const size_t bi_u = (size_t)bi;
  const size_t site_i = bi_u * (size_t)MSL_RNG_SITE_COUNT + (size_t)site_id;
  if (batch->rng_site_counts[site_i] != 0xFFFFu) {
    batch->rng_site_counts[site_i]++;
  }
  uint32_t seed = batch->rng_shadow_seed[bi_u];
  seed = combat_hsd_rand_step(seed);
  batch->rng_shadow_seed[bi_u] = seed;
  return (uint16_t)(seed >> 16);
}

float combat_rng_consume_randf_site(MslBatch* batch, int bi, uint16_t site_id) {
  const uint16_t rnd = combat_hsd_rand_u16_consume_site(batch, bi, site_id);
  // HSD_Randf output mapping: upper 16 bits / 65536.0f.
  // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randf
  return (float)rnd * (1.0f / 65536.0f);
}

int32_t combat_rng_consume_randi_site(MslBatch* batch, int bi, uint16_t site_id, int32_t max_val) {
  if (max_val <= 0) {
    return 0;
  }
  const uint16_t rnd = combat_hsd_rand_u16_consume_site(batch, bi, site_id);
  // HSD_Randi(max) = max * HSD_Rand() / 65536.
  // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
  return (int32_t)(((int64_t)max_val * (int64_t)rnd) >> 16);
}

void combat_rng_consume_step_site(MslBatch* batch, int bi, uint16_t site_id) {
  // One-step RNG advance helper for sites where gameplay only depends on stream ordering, not on
  // the sampled value itself.
  // refs/melee/src/sysdolphin/baselib/random.c::{HSD_Randi,HSD_Randf}
  (void)combat_hsd_rand_u16_consume_site(batch, bi, site_id);
}

void combat_rng_trace_begin_frame(MslBatch* batch) {
  if (batch == NULL || batch->rng_shadow_seed == NULL || batch->debug_rng_seed_in == NULL ||
      batch->debug_rng_seed_out == NULL || batch->rng_site_counts == NULL) {
    return;
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const size_t bi_u = (size_t)bi;
    uint32_t seed_in = batch->state.frame_pre_random_seed[bi_u];
    batch->rng_shadow_seed[bi_u] = seed_in;
    batch->debug_rng_seed_in[bi_u] = seed_in;
    batch->debug_rng_seed_out[bi_u] = seed_in;
    memset(batch->rng_site_counts + bi_u * (size_t)MSL_RNG_SITE_COUNT, 0,
           (size_t)MSL_RNG_SITE_COUNT * sizeof(uint16_t));
  }
}

void combat_rng_trace_end_frame(MslBatch* batch) {
  if (batch == NULL || batch->rng_shadow_seed == NULL || batch->debug_rng_seed_out == NULL) {
    return;
  }
  FILE* trace_file = NULL;
  if (batch->debug_rng_trace_enabled && batch->debug_rng_trace_file != NULL) {
    trace_file = (FILE*)batch->debug_rng_trace_file;
  }
  const uint64_t step_id = batch->debug_rng_trace_step_counter++;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const size_t bi_u = (size_t)bi;
    batch->debug_rng_seed_out[bi_u] = batch->rng_shadow_seed[bi_u];
    if (trace_file == NULL || batch->debug_rng_seed_in == NULL || batch->rng_site_counts == NULL) {
      continue;
    }
    const int32_t frame_id = batch->state.frame_id[bi_u];
    const uint32_t seed_in = batch->debug_rng_seed_in[bi_u];
    const uint32_t seed_out = batch->debug_rng_seed_out[bi_u];
    for (uint16_t site_id = 1; site_id < (uint16_t)MSL_RNG_SITE_COUNT; site_id++) {
      const size_t site_i = bi_u * (size_t)MSL_RNG_SITE_COUNT + (size_t)site_id;
      const uint16_t count = batch->rng_site_counts[site_i];
      (void)fprintf(trace_file, "%llu\t%d\t%d\t%u\t%u\t%u\t%u\n", (unsigned long long)step_id, bi,
                    (int)frame_id, seed_in, seed_out, (unsigned int)site_id, (unsigned int)count);
    }
  }
}

void combat_rng_use_next_replay_frame_seed_if_unconsumed(MslBatch* batch, int bi) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return;
  }
  const uint32_t seed =
      (batch->replay_frame_rng_applied != NULL && batch->replay_frame_rng_applied[bi] != 0u)
          ? batch->state.frame_pre_random_seed[(size_t)bi]
          : batch->state.frame_pre_random_seed[(size_t)bi] + 0x10000u;
  combat_rng_set_replay_frame_seed_if_unconsumed(batch, bi, seed);
}

void combat_rng_set_replay_frame_seed_if_unconsumed(MslBatch* batch, int bi, uint32_t seed) {
  if (batch == NULL || batch->rng_shadow_seed == NULL || batch->rng_site_counts == NULL ||
      batch->rollout_clock_rng_owned == NULL || bi < 0 || bi >= batch->batch_size ||
      batch->rollout_clock_rng_owned[bi] != (uint8_t)MSL_ROLLOUT_CLOCK_REPLAY_FRAME_SEED) {
    return;
  }
  const size_t base = (size_t)bi * (size_t)MSL_RNG_SITE_COUNT;
  for (uint16_t site_id = 1; site_id < (uint16_t)MSL_RNG_SITE_COUNT; site_id++) {
    if (batch->rng_site_counts[base + (size_t)site_id] != 0u) {
      return;
    }
  }
  batch->rng_shadow_seed[(size_t)bi] = seed;
}

// Combo / last-attack tracking (decomp-first).
//
// Decomp trail (GALE01):
// - Combo update + last-attack id:
//   refs/melee/src/melee/ft/ftcoll.c::ftColl_800763C0 (writes fp->x208C, fp->x2090, fp->x2094)
//   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076444 (calls ftColl_800763C0(attacker, victim, fp->x2068_attackID))
//   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C (item->fighter variant; attack id in item domain)
// - Slippi post-frame fields:
//   - last_attack_landed: low byte of lwz 0x208C(fp)
//   - combo_count: low byte of lhz 0x2090(fp)
//   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
static inline void combat_combo_ftColl_800763C0(MslBatch* batch, size_t a_idx, int defender,
                                                size_t d_idx, uint16_t attack_id_u16) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const int num_players = (int)batch->config.num_players;
  if (defender < 0 || defender >= num_players) {
    return;
  }

  const uint8_t attack_id_u8 = (uint8_t)attack_id_u16;  // Slippi stores the low byte.
  const uint8_t cur_victim = batch->state.combo_victim_port[a_idx];
  if (cur_victim == 0xFFu) {
    batch->state.last_attack_landed[a_idx] = attack_id_u8;
    batch->state.combo_count[a_idx] = 1u;
    batch->state.combo_victim_port[a_idx] = (uint8_t)defender;
    batch->state.combo_victim_instance_id[a_idx] = batch->state.instance_id[d_idx];
    return;
  }
  // Decomp uses a raw victim GObj pointer for `fp->x2094` equality checks (not an integer ID).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_800763C0
  //
  // Slippi's `instance_id` (fp+0x2070 union-as-int) is not a stable "fighter object identity"
  // across frames, so do not include it in the x2094-equivalence check. We keep the instance_id
  // snapshot only as a seeded/debuggable overlay.
  // refs/melee/src/melee/ft/types.h (union Struct2070 at fp+0x2070, used as s32 x2070_int)
  if (cur_victim == (uint8_t)defender) {
    if (attack_id_u16 != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
        batch->state.last_attack_landed[a_idx] == attack_id_u8) {
      batch->state.combo_count[a_idx] = (uint8_t)(batch->state.combo_count[a_idx] + 1u);
      if (c != NULL && c->combo_push_count_threshold != 0u &&
          batch->state.combo_count[a_idx] >= (uint8_t)c->combo_push_count_threshold) {
        // Decomp: ftColl_800763C0 arms fp->x2092 = p_ftCommonData->x4D8 once repeated
        // same-attack combo count reaches x4C4; ftColl_80076528 consumes it later as a grounded
        // attacker push along the floor normal.
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_80076528}
        batch->state.combo_push_timer_x2092[a_idx] = c->combo_push_timer_frames;
      }
    } else {
      batch->state.combo_count[a_idx] = 0u;
      batch->state.last_attack_landed[a_idx] = attack_id_u8;
    }
    batch->state.combo_victim_instance_id[a_idx] = batch->state.instance_id[d_idx];
  }
}

float combat_hitlag_mul_from_element(const MslCommonParams* c, uint8_t element) {
  if (c == NULL) {
    return 1.0f;
  }
  // Decomp/ASM: ftColl_8007A06C sets `fp->x1960_vibrateMult = p_ftCommonData->x1A4` when hit
  // element is 2 (electric), and Fighter_ProcessHit passes `fp->x1960_vibrateMult` into
  // ftCommon_CalcHitlag as the `mul` argument.
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s (search for `stfs f0, 0x1960`)
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  if (element == (uint8_t)MSL_HIT_ELEMENT_ELECTRIC) {
    return c->hitlag_electric_mul;
  }
  return 1.0f;
}

uint16_t combat_calc_hitlag_frames(const MslCommonParams* c, int dmg, uint16_t motion_id,
                                   float hitlag_mul) {
  if (c == NULL) {
    return 0;
  }

  // Decomp (GALE01): ftCommon_CalcHitlag
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_CalcHitlag
  //
  // Notes for this pass:
  // - `hitlag_mul` corresponds to `fp->x1960_vibrateMult` in decomp (see combat_hitlag_mul_from_element).
  const float tmp_f = (float)dmg * c->hitlag_dmg_mul + c->hitlag_base;
  int tmp = (int)tmp_f;

  float mul = hitlag_mul;
  if (!(mul > 0.0f)) {
    mul = 1.0f;
  }

  // Decomp truncates before applying squat scaling:
  // `result = (int)(tmp * mul); if ((unsigned)msid - ftCo_MS_Squat <= 1) result = (int)(result * x1A0);`
  // refs/melee/src/melee/ft/ftcommon.c:653-655 (ftCommon_CalcHitlag)
  int result_i = (int)((float)tmp * mul);
  if (motion_id == (uint16_t)MSL_ACT_SQUAT || motion_id == (uint16_t)MSL_ACT_SQUAT_WAIT) {
    result_i = (int)((float)result_i * c->hitlag_squat_mul);
  }
  if (result_i < 0) {
    result_i = 0;
  }
  if (result_i > 0xFFFF) {
    result_i = 0xFFFF;
  }
  return (uint16_t)result_i;
}

static inline uint8_t combat_received_kb_hitlag_owns_over_deal_hitlag(const MslBatch* batch,
                                                                      size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  if (batch->state.hitlag[idx] != 0u && batch->state.phantom_damage_timer_x189c[idx] != 0u) {
    // Fighter_ProcessHit priority:
    // - a received phantom/tip-log contact stores `dmg.x18a0` / `dmg.x1840` and starts hitlag,
    // - an outgoing same-frame BODY contact stores deal-hitlag in `dmg.x1914`,
    // - source consumes x18a0 before x1914, so the later deal-hitlag lane must not overwrite the
    //   received phantom hitlag in this sequential simulator pass.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,checkTipLog,inlineB1}
    return 1u;
  }
  if (batch->state.hitlag[idx] == 0u || batch->state.hitstun[idx] == 0u) {
    return 0u;
  }
  if (!combat_is_damage_or_firefox_launch_victim_action(batch->state.char_id[idx],
                                                        batch->state.action_id[idx])) {
    return 0u;
  }
  // Reciprocal BODY hit priority:
  // - Fighter_ProcessHit consumes received-KB hitlag from `dmg.x183C_applied` before deal-hitlag
  //   lanes (`dmg.x1914` / `x1924`).
  // - This simplified pass applies BODY contacts sequentially, so a later outgoing hit from a
  //   fighter already mutated into Damage* must not overwrite the received-hitlag value.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
  return 1u;
}

void combat_apply_deal_hitlag_raw_damage(MslBatch* batch, size_t idx, int damage) {
  if (batch == NULL || damage <= 0) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Deal-hitlag owner:
  // - collision producers write `fp->dmg.x1914` with integer damage when an attacking HitCapsule
  //   contacts an invincible fighter or an item hurtbox.
  // - Fighter_ProcessHit then consumes x1914 through `ftCommon_CalcHitlag` using the attacker's
  //   current MotionState id and default hitlag multiplier.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80076808}
  // refs/melee/src/melee/it/itcoll.c::it_802703E8
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t motion_id = batch->state.action_id[idx];
  const uint16_t hl = combat_calc_hitlag_frames(c, damage, motion_id, 1.0f);
  if (!combat_received_kb_hitlag_owns_over_deal_hitlag(batch, idx) &&
      hl > batch->state.hitlag[idx]) {
    batch->state.hitlag[idx] = hl;
    combat_state_flags_set_is_hitlag(batch, idx, hl);
  }
}

void combat_apply_min_hitlag_frames(MslBatch* batch, size_t idx, uint16_t hitlag_frames) {
  if (batch == NULL || hitlag_frames == 0u) {
    return;
  }
  if (!combat_received_kb_hitlag_owns_over_deal_hitlag(batch, idx) &&
      hitlag_frames > batch->state.hitlag[idx]) {
    batch->state.hitlag[idx] = hitlag_frames;
    combat_state_flags_set_is_hitlag(batch, idx, hitlag_frames);
  }
}

int combat_get_env_dmg(float dmg) {
  // Decomp (GALE01): "getEnvDmg" pattern used by collision when turning a hitbox's float damage into
  // the integer damage used for shield interactions and hitlag inputs.
  // refs/melee/src/melee/ft/ftcoll.c (inlineA0/inlineA1 and ftColl_80076CBC).
  //
  // Behavior:
  // - dmg == 0 -> 0
  // - dmg != 0 and (int)dmg != 0 -> (int)dmg
  // - dmg != 0 and (int)dmg == 0 -> 1
  // Note: this intentionally matches `if (dmg)` rather than `if (dmg > 0)` (so negative nonzero and
  // NaN follow the decomp path).
  if (dmg == 0.0f) {
    return 0;
  }
  const int i = (int)dmg;
  return (i != 0) ? i : 1;
}

static inline void combat_damage_product_init(MslCombatDamageProduct* out, uint16_t move_id,
                                              uint16_t attack_instance, int hitcapsule_int_dmg,
                                              int kb_damage_i) {
  if (out == NULL) {
    return;
  }
  memset(out, 0, sizeof(*out));
  out->move_id = move_id;
  out->attack_instance = attack_instance;
  out->hitcapsule_int_dmg = hitcapsule_int_dmg;
  out->kb_damage_i = kb_damage_i;
}

static inline uint8_t combat_damage_product_set_applied(MslCombatDamageProduct* product,
                                                        float applied_damage) {
  if (product == NULL) {
    return 0u;
  }
  product->applied_damage = applied_damage;
  product->env_dmg = combat_get_env_dmg(applied_damage);
  return (product->env_dmg > 0) ? 1u : 0u;
}

// Source-order contact selector:
// ftColl_80078C70 evaluates shield then BODY for one attacking HitCapsule before advancing to the
// next HitCapsule. The simulator still applies shield mutations through a compact pair-level pass,
// so this helper preserves the source-owned ordering boundary: a lower-index HitCapsule that would
// select BODY under the same gates prevents a later HitCapsule from becoming the pair shield hit.
// refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8,ftColl_80076CBC}
// refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_8000805C,lbColl_8000ACFC}
static inline uint8_t combat_source_order_earlier_body_hitcapsule_precedes_shield(
    MslBatch* batch, const MslCommonParams* c, int bi, int attacker, int defender, int shield_hb_id,
    uint16_t defender_iid, float shx, float shy, float shz, float shr,
    uint8_t shield_desc_envelope_ready, uint8_t shield_extent_bridge_active,
    uint8_t guard_reflect_reflectdesc_only,
    const uint8_t clank_skip_hb[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_MAX_HITBOXES]) {
  if (batch == NULL || c == NULL || shield_hb_id <= 0) {
    return 0u;
  }
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);

  uint8_t hurt_state = batch->state.hurtbox_state[d_idx];
  const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
  if (hit_status > hurt_state) {
    hurt_state = hit_status;
  }
  if (hurt_state == 2u) {
    return 0u;
  }

  const MslHurtCap* fallback_caps = NULL;
  uint16_t fallback_count_u16 = 0u;
  uint8_t use_fallback_caps = 0u;
  uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
  if (hurtcap_count == 0u &&
      combat_guard_family_no_submotion_body_source_msid(batch, d_idx, NULL)) {
    if (hurtcaps_get(batch->state.char_id[d_idx], &fallback_caps, &fallback_count_u16) == 0 &&
        fallback_caps != NULL && fallback_count_u16 != 0u) {
      use_fallback_caps = 1u;
      hurtcap_count = fallback_count_u16 > (uint16_t)MSL_MAX_HURTCAPS ? (uint8_t)MSL_MAX_HURTCAPS
                                                                      : (uint8_t)fallback_count_u16;
    }
  }
  if (hurtcap_count == 0u) {
    return 0u;
  }
  const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;
  const uint16_t a_motion_id = (batch->state.animation_index[a_idx] <= 0xFFFFu)
                                   ? (uint16_t)batch->state.animation_index[a_idx]
                                   : batch->state.action_id[a_idx];

  for (int prev_hb_id = 0; prev_hb_id < shield_hb_id; prev_hb_id++) {
    if (combat_defer_late_slot_same_frame_speciallw_entry_hit(batch, bi, a_idx, d_idx, attacker,
                                                              defender, prev_hb_id)) {
      continue;
    }
    if (clank_skip_hb != NULL && clank_skip_hb[attacker][defender][prev_hb_id]) {
      continue;
    }
    const size_t hb_i = idx_hitbox(bi, attacker, prev_hb_id);
    if (!batch->state.hitbox_enabled[hb_i]) {
      continue;
    }
    const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
    if (defender_on_ground) {
      if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
        continue;
      }
    } else if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
      continue;
    }

    const float hx = batch->state.hitbox_x[hb_i];
    const float hy = batch->state.hitbox_y[hb_i];
    const float hz = batch->state.hitbox_z[hb_i];
    const float hr = batch->state.hitbox_radius[hb_i];
    const float hdmg = batch->state.hitbox_damage[hb_i];
    if (!(hdmg > 0.0f)) {
      continue;
    }
    const int int_dmg = combat_get_env_dmg(hdmg);
    if (int_dmg <= 0) {
      continue;
    }

    const uint8_t shield_seed_kind =
        batch->state
            .combat_shield_contact_hb_kind[idx_hitbox_victim(bi, attacker, prev_hb_id, defender)];
    uint8_t previous_hb_blocked_by_shield = 0u;
    if (shr > 0.0f && shield_seed_kind == 2u) {
      previous_hb_blocked_by_shield = 1u;
    } else if (shr > 0.0f && shield_seed_kind != 1u && !guard_reflect_reflectdesc_only) {
      previous_hb_blocked_by_shield = combat_shield_overlap_ftcoll_80007bcc(
          batch, bi, attacker, defender, prev_hb_id, hx, hy, hz, hr, shx, shy, shz, shr,
          /*shield_desc_radius=*/1.0f, batch->state.fighter_scale_y[d_idx],
          shield_desc_envelope_ready, shield_extent_bridge_active, NULL);
    }
    if (previous_hb_blocked_by_shield) {
      continue;
    }

    if (!hitlist_allows_fighter(batch, bi, attacker, prev_hb_id, defender, defender_iid)) {
      continue;
    }
    if (combat_attackairlw_invincible_contact_rejects_body_hitlag(batch, a_idx, d_idx)) {
      continue;
    }
    if (combat_guard_reflect_active_x14_no_guardon_blocks_body(batch, d_idx)) {
      continue;
    }
    if (combat_guard_reflect_final_x14_live_x18_blocks_body(batch, d_idx)) {
      continue;
    }
    if (combat_sheik_chain_terminal_same_source_episode_suppresses_body(batch, a_idx, d_idx)) {
      continue;
    }

    for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
      const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
      float ax = 0.0f, ay = 0.0f, az = 0.0f;
      float bx = 0.0f, by = 0.0f, bz = 0.0f;
      float cr = 0.0f;
      if (use_fallback_caps) {
        if (!combat_guard_family_body_hurtcap_world(batch, d_idx, &fallback_caps[cap_id], cap_id,
                                                    fallback_count_u16, &ax, &ay, &az, &bx, &by,
                                                    &bz, &cr)) {
          continue;
        }
      } else {
        if (!batch->state.hurtcap_enabled[cap_i]) {
          continue;
        }
        ax = batch->state.hurtcap_a_x[cap_i];
        ay = batch->state.hurtcap_a_y[cap_i];
        az = batch->state.hurtcap_a_z[cap_i];
        bx = batch->state.hurtcap_b_x[cap_i];
        by = batch->state.hurtcap_b_y[cap_i];
        bz = batch->state.hurtcap_b_z[cap_i];
        cr = batch->state.hurtcap_radius[cap_i];
      }

      float lbcoll_overlap_amount = 0.0f;
      uint8_t lbcoll_overlap_evaluated = 0u;
      const uint8_t lbcoll_overlap_valid = combat_body_overlap_lbColl_80006E58_matrix_radius(
          batch, bi, attacker, prev_hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az, bx,
          by, bz, 0u, &lbcoll_overlap_amount, &lbcoll_overlap_evaluated);
      const uint8_t overlaps =
          lbcoll_overlap_evaluated
              ? lbcoll_overlap_valid
              : combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL);
      if (overlaps && !combat_shine_start_damageair_entry_pose_allows_body_contact(
                          batch, a_idx, d_idx, cap_id, hx, hy, hz, hr)) {
        continue;
      }
      const uint8_t attackairb_jump_low_body_model_scale_source =
          combat_attackairb_jump_low_body_source_owns_model_scale_bypass(
              batch, bi, attacker, prev_hb_id, defender, a_idx, d_idx, cap_id, hx, hy, hz, hr);
      if (overlaps && attackairb_jump_low_body_model_scale_source == 0u &&
          !combat_attackairb_enable_edge_model_scale_allows_body_contact(
              batch, a_idx, hb_i, d_idx, hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr,
              combat_calc_hitlag_frames(c, int_dmg, a_motion_id, 1.0f))) {
        continue;
      }
      if (overlaps) {
        return 1u;
      }
    }
  }
  return 0u;
}

static inline float combat_apply_attacker_smash_release_damage_mul(const MslBatch* batch,
                                                                   size_t a_idx, float damage) {
  if (batch == NULL || batch->state.smash_charge_state[a_idx] != 3u) {
    return damage;
  }
  const uint8_t hold_frames = batch->state.smash_charge_hold_frames_max[a_idx];
  if (hold_frames == 0u) {
    return damage;
  }
  uint8_t charge_frames = batch->state.smash_charge_frames[a_idx];
  if (charge_frames > hold_frames) {
    charge_frames = hold_frames;
  }
  const float damage_mul = move_tables_grounded_smash_charge_damage_mul(
      batch->state.char_id[a_idx], batch->state.action_id[a_idx]);
  if (!(damage_mul > 0.0f)) {
    return damage;
  }
  // Released-smash hitcapsule damage owner:
  // - ftAction_80073008 stores command damage_mul in smash_attrs.x2120_damageMul.
  // - ftCo_800DEF38 / ftCo_800DF0D0 set SmashState_Release on max-charge or A release.
  // - ftColl_8007ABD0 calls ftCo_800DEEB8 before writing HitCapsule.{unk_count,damage}.
  // Formula: damage * (((damage_mul - 1) * (frames / hold_frames)) + 1).
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073008
  // refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEF38,ftCo_800DF0D0,ftCo_800DEEB8}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0
  const float t = (float)charge_frames / (float)hold_frames;
  const float mul = ((damage_mul - 1.0f) * t) + 1.0f;
  return damage * mul;
}

static inline float combat_hitcapsule_collision_damage(const MslBatch* batch, size_t a_idx,
                                                       size_t hb_i) {
  if (batch == NULL) {
    return 0.0f;
  }
  // HitCapsule.damage is collision-time damage, not the raw movescript value:
  // - ftColl_8007ABD0 calls ftCo_800DEEB8 for smash-release damage and ft_80089228 for stale
  //   damage before writing HitCapsule.damage.
  // - ftColl_8007699C then consumes HitCapsule.damage for reciprocal clank thresholds and the
  //   per-fighter int damage later used by Fighter_ProcessHit hitlag/rebound.
  // - ft_80089228 receives x206C_attack_instance, but ft_80089118 computes the scalar from matching
  //   move_id entries only. attack_instance owns stale-table duplicate suppression, not the damage
  //   multiplier.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_8007699C,inlineA0,inlineA1}
  // refs/melee/src/melee/ft/ft_0DF0.c::ftCo_800DEEB8
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  float stale_mult = 1.0f;
  if (batch->state.hitbox_stale_damage_valid[hb_i] != 0u &&
      batch->state.hitbox_stale_damage_mul[hb_i] > 0.0f) {
    stale_mult = batch->state.hitbox_stale_damage_mul[hb_i];
  } else {
    const uint16_t move_id = staling_move_id_from_state(batch, a_idx);
    stale_mult = staling_multiplier_for_move(batch, a_idx, move_id);
  }
  float dmg = combat_apply_attacker_smash_release_damage_mul(batch, a_idx,
                                                             batch->state.hitbox_damage[hb_i]);
  if (stale_mult != 1.0f) {
    dmg *= stale_mult;
  }
  return dmg;
}

void combat_apply_deal_hitlag_hitbox_damage(MslBatch* batch, size_t idx, size_t hb_i) {
  if (batch == NULL) {
    return;
  }
  const int dmg_i = combat_hitbox_collision_env_damage(batch, idx, hb_i);
  combat_apply_deal_hitlag_raw_damage(batch, idx, dmg_i);
}

int combat_hitbox_collision_env_damage(const MslBatch* batch, size_t idx, size_t hb_i) {
  const float dmg = combat_hitcapsule_collision_damage(batch, idx, hb_i);
  return combat_get_env_dmg(dmg);
}

static inline int combat_guard_setoff_x19a4_int_damage(const MslBatch* batch, size_t a_idx,
                                                       size_t d_idx, size_t hb_i, int int_dmg) {
  if (batch == NULL) {
    return int_dmg;
  }
  if (!combat_guard_setoff_recoil_x221c_b2_idx(batch, d_idx)) {
    return int_dmg;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_LW) {
    return int_dmg;
  }
  const float authored_damage = batch->state.hitbox_damage[hb_i];
  if (!(authored_damage > 0.0f && authored_damage <= 3.0f)) {
    return int_dmg;
  }
  const int authored_int_dmg = combat_get_env_dmg(authored_damage);
  if (authored_int_dmg <= int_dmg) {
    return int_dmg;
  }
  // Powershield-active low-damage DAir x19A4 owner:
  // ftColl_80076CBC writes the defender x19A4 max-damage / x19AC recoil lane before branching on
  // x221C_b2. For the extracted 3/2-damage AttackAirLw multihit create payload, this recoil scalar
  // is owned by the current HitCapsule payload that ftColl_8007ABD0 created, while x19A0 shield HP
  // remains on the stale/applied collision-damage path. Keep the split only on x221C_b2
  // GuardSetOff recoil so ordinary BODY and non-powershield shield contacts are unaffected.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_8007ABD0}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s:0x80076D1C..0x80076E64
  // data/moves/fox.json::moves.ftCo_SM_AttackAirLw.events.create_hitbox
  return authored_int_dmg;
}

static inline float combat_rebound_x191c_from_int_dmg(const MslCommonParams* c, int int_dmg) {
  if (c == NULL || int_dmg <= 0) {
    return 0.0f;
  }
  // Rebound clank setup:
  // - ftColl inlineA0/inlineA1 write `fp->dmg.x191C = int_dmg * x3D0 + x3D4` for grounded
  //   rebound-requesting clanks.
  // refs/melee/src/melee/ft/ftcoll.c::{inlineA0,inlineA1}
  return (float)int_dmg * c->rebound_damage_x191c_mul + c->rebound_damage_x191c_base;
}

static inline float combat_clank_damage_facing_dir(const MslBatch* batch, size_t self_idx,
                                                   size_t opponent_idx) {
  if (batch == NULL) {
    return 1.0f;
  }
  // Clank damage-facing owner:
  // - ftColl_8007699C inlineA0/inlineA1 write `fp->dmg.facing_dir` from the two fighter root X
  //   positions before ftCo_80099D9C consumes it.
  // - This is not necessarily the replay-visible scalar facing byte; cross-up/back-facing clanks
  //   still rebound away from the opponent's current root position.
  // Source shape matches BODY damage facing ownership used by ftCo_8008DCE0:
  //   self.x > opponent.x => -1, else +1.
  // refs/melee/src/melee/ft/ftcoll.c::{inlineA0,inlineA1}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_80099D9C
  return (batch->state.pos_x[self_idx] > batch->state.pos_x[opponent_idx]) ? -1.0f : 1.0f;
}

static inline float combat_rebound_ground_accel_2_from_int_dmg(const MslBatch* batch,
                                                               const MslCommonParams* c, size_t idx,
                                                               int int_dmg,
                                                               float damage_facing_dir) {
  const float rebound_x191c = combat_rebound_x191c_from_int_dmg(c, int_dmg);
  if (!(rebound_x191c > 0.0f)) {
    return 0.0f;
  }
  // Rebound xE8 ownership:
  // - ftCo_80099D9C derives `mv.co.rebound.x0 = -facing_dir * (x191C * x3D8 + x3DC)`.
  // - ftCommon_800804A0 writes that through xE8_ground_accel_2, scaled by
  //   ft_GetGroundFrictionMultiplier only when the multiplier is below 1.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_80099D9C
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804A0
  float x0 =
      -damage_facing_dir * (rebound_x191c * c->rebound_ground_x0_mul + c->rebound_ground_x0_base);
  const float friction_mul = batch->state.ground_friction_mul[idx];
  if (friction_mul < 1.0f) {
    x0 *= friction_mul;
  }
  return x0;
}

static inline int32_t combat_rebound_anim_rate_from_int_dmg(const MslBatch* batch,
                                                            const MslCommonParams* c, size_t idx,
                                                            int int_dmg) {
  const float rebound_x191c = combat_rebound_x191c_from_int_dmg(c, int_dmg);
  const MslCharParams* ch =
      (batch != NULL) ? msl_char_params_fast(batch->state.char_id[idx]) : NULL;
  if (!(rebound_x191c > 0.0f) || ch == NULL) {
    return 0;
  }
  // Rebound anim-rate ownership:
  // - ftCo_80099D9C stores `mv.co.rebound.anim_start = (fp->co_attrs.x9C + 0.1f) / fp->dmg.x191C`.
  // - ftCo_ReboundStop_Anim -> ftCo_80099E44 later enters Rebound with that stored rate.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
  //   ftCo_80099D9C,ftCo_ReboundStop_Anim,ftCo_80099E44}
  const float rate = (ch->rebound_anim_numerator_frames + 0.1f) / rebound_x191c;
  return (rate > 0.0f) ? msl_q16_16_from_f32(rate) : 0;
}

static inline uint8_t combat_defender_hit_status_u8(const MslBatch* batch, size_t d_idx) {
  // Debug override (test-only): 0xFF means "use tables".
  if (batch->debug_hit_status_override != NULL) {
    const uint8_t ov = batch->debug_hit_status_override[d_idx];
    if (ov != 0xFFu) {
      return ov;
    }
  }

  const uint8_t d_char = batch->state.char_id[d_idx];

  uint16_t d_msid = 0;
  const uint32_t d_msid_u32 = batch->state.animation_index[d_idx];
  if (d_msid_u32 <= 0xFFFFu) {
    d_msid = (uint16_t)d_msid_u32;
  }

  // Current policy (suite-neutral): negative/NaN anim_frame consults frame 0.
  // If we later want "negative anim_frame => don't consult tables", gate that here.
  const float d_anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  const uint16_t d_frame = msl_anim_frame_floor_u16(d_anim_frame_f32);

  // Decomp timing: move-induced hit status (fp->x1988) is set by movescript opcode 26 while
  // executing ftAction_80073240 inside the prio 1 Anim proc (ftAnim_8006EBA4). If a motion-state
  // transition happens after that Anim tick (e.g. due to input/IASA), the new state's cmd script
  // does not run until next frame, so x1988 should not be treated as active on the entry frame.
  // Decomp proc ordering: prio 1 Anim runs before prio 3 input callbacks.
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868 (eligibility aggregates x1988/x198C)
  uint8_t hit_status = 0;
  const uint16_t cur_action = batch->state.action_id[d_idx];
  const uint8_t cur_action_fx_kind =
      msl_motion_state_fx_special_kind(batch->state.char_id[d_idx], cur_action);
  const uint8_t is_shine_start_entry =
      (cur_action_fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_LW_START ||
       cur_action_fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_LW_START)
          ? 1u
          : 0u;
  // Entry-frame x1988 ownership:
  // - Generic post-Anim action transitions should not consume new-state script hit_status until the
  //   next frame's ftAnim_8006EBA4 tick.
  // - Shine Start is a decomp-anchored exception where enter helpers call ftAnim_8006EBA4
  //   immediately after state change, so opcode-26 hit_status is valid on entry.
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{ftFx_SpecialLw_Enter,ftFx_SpecialAirLw_Enter}
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  if (!(d_frame == 0u && batch->state.prev_action_id[d_idx] != cur_action &&
        !is_shine_start_entry)) {
    (void)move_tables_hit_status_at_frame(d_char, d_msid, d_frame, &hit_status);
  }

  // Decomp collision eligibility uses max(fp->x1988, fp->x198C):
  // - x1988: script/hurtcaps-derived hit status (vulnerable/invincible/intangible).
  // - x198C: color-animation hit status lane that can independently elevate collision immunity.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
  // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_8006A1BC}
  const uint8_t colanim_status = batch->state.colanim_hit_status_x198c[d_idx];
  if (colanim_status > hit_status) {
    hit_status = colanim_status;
  }
  return hit_status;
}

static inline float combat_deg_to_rad_f32(void) {
  // Decomp uses a global `deg_to_rad` float constant.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcAngle
  return 0.01745329251994329577f;
}

float combat_pi_over_two_f32(void) { return 1.57079632679489661923f; }

static inline float combat_damage_sakurai_angle_radians(const MslCommonParams* c,
                                                        uint8_t defender_on_ground,
                                                        float kb_applied) {
  if (c == NULL) {
    return 0.0f;
  }

  // Decomp (GALE01): ftCo_Damage_CalcAngle, hitbox angle=361 ("Sakurai angle" sentinel).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcAngle
  if (!defender_on_ground) {
    return c->sakurai_air_radians;
  }
  if (kb_applied < c->sakurai_kb_threshold) {
    return 0.0f;
  }

  const float denom = c->sakurai_kb_max - c->sakurai_kb_threshold;
  const float t = (denom > 0.0f) ? ((kb_applied - c->sakurai_kb_threshold) / denom) : 0.0f;
  const float deg = c->sakurai_ground_deg_max * t + 1.0f;
  float rad = combat_deg_to_rad_f32() * deg;

  const float max_rad = combat_deg_to_rad_f32() * c->sakurai_ground_deg_max;
  if (rad > max_rad) {
    rad = max_rad;
  }
  return rad;
}

static inline float combat_damage_calc_angle_radians(const MslCommonParams* c, uint16_t angle_deg,
                                                     uint8_t defender_on_ground, float kb_applied) {
  if (angle_deg != 361u) {
    return combat_deg_to_rad_f32() * (float)angle_deg;
  }
  return combat_damage_sakurai_angle_radians(c, defender_on_ground, kb_applied);
}

static inline uint8_t combat_damage_check_air_motion_kb_mul(const MslCommonParams* c,
                                                            const MslBatch* batch, size_t idx) {
  if (c == NULL || batch == NULL) {
    return 0;
  }

  // Decomp (GALE01): ftCo_Damage_CheckAirMotion.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CheckAirMotion
  const uint16_t a = batch->state.action_id[idx];
  switch (a) {
    case (uint16_t)MSL_ACT_JUMP_F:
    case (uint16_t)MSL_ACT_JUMP_B:
    case (uint16_t)MSL_ACT_JUMP_AERIAL_F:
    case (uint16_t)MSL_ACT_JUMP_AERIAL_B:
    case (uint16_t)MSL_ACT_FALL:
    case (uint16_t)MSL_ACT_FALL_F:
    case (uint16_t)MSL_ACT_FALL_B:
    case (uint16_t)MSL_ACT_FALL_AERIAL:
    case (uint16_t)MSL_ACT_FALL_AERIAL_F:
    case (uint16_t)MSL_ACT_FALL_AERIAL_B:
    case (uint16_t)MSL_ACT_FALL_SPECIAL:
    case (uint16_t)MSL_ACT_FALL_SPECIAL_F:
    case (uint16_t)MSL_ACT_FALL_SPECIAL_B:
    case (uint16_t)MSL_ACT_DAMAGE_FALL:
    case (uint16_t)MSL_ACT_ESCAPE_AIR:
      if (batch->state.x680[idx] <= c->air_motion_max_frames &&
          batch->state.x684[idx] >= c->tech_lr_debounce_frames) {
        return 1;
      }
      return 0;
    default:
      return 0;
  }
}

static inline float combat_damage_ftColl_804D82EC_one(void) {
  // Decomp declares this as an extern float constant.
  // refs/melee/src/melee/ft/ftcoll.c (extern float const ftColl_804D82EC)
  //
  // GALE01 definition (not ftCommonData; lives in `.sdata2`):
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::.obj ftColl_804D82EC
  // - `.float 1`
  //
  // Used in asm:
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0 and ::ftColl_80079EA8
  // - `lfs f8, ftColl_804D82EC@sda21(r0)` then `fadds f0, f8, f1`.
  return 1.0f;
}

static inline float combat_damage_ftColl_804D8314_kbg_mul(void) {
  // Decomp declares this as an extern float constant.
  // refs/melee/src/melee/ft/ftcoll.c (extern float const ftColl_804D8314)
  //
  // GALE01 definition (not ftCommonData; lives in `.sdata2`):
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::.obj ftColl_804D8314
  // - `.float 0.01`
  //
  // Used in asm:
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0 and ::ftColl_80079EA8
  // - `lfs f8, ftColl_804D8314@sda21(r0)` and `fmuls f6, f8, f6` (kbg / 100).
  return 0.01f;
}

static inline float combat_damage_calc_kb_applied(
    const MslCommonParams* c, const MslCharParams* d, uint16_t defender_action_id,
    float defender_percent_pre, float defender_percent_temp, int hitbox_damage_i,
    uint16_t hitbox_kbg, uint16_t hitbox_wsk, uint16_t hitbox_bkb, float collision_kb_mul,
    uint8_t defender_dmg_x2225_b7, uint8_t defender_dmg_x2224_b2,
    uint8_t defender_kb_smashcharge_active) {
  if (c == NULL) {
    return 0.0f;
  }

  // Knockback magnitude computation comes from collision (ftColl_80079AB0).
  //
  // Decomp entry point:
  // refs/melee/src/melee/ft/ftcoll.h::ftColl_80079AB0(Fighter*, HitCapsule*, int, float, float, float, float)
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80079AB0 (stub)
  //
  // Authoritative asm:
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
  //
  // Call chain for fighter-vs-fighter hits:
  // - ftColl_8007A06C computes kb_applied via ftColl_80079AB0 and writes it into fp->dmg.kb_applied.
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C
  //
  // Notes:
  // - The `int` arg corresponds to `HitCapsule.unk_count` (lb/types.h:+8). The hitbox pipeline
  //   stores that integer directly when building the HitCapsule:
  //   refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
  //   - `stw r0, 0x8(r30)` (unk_count)
  //   - `stfs f1, 0xc(r30)` (damage)
  // - The `float` damage term used in `s = percent_int + dmg_temp` is `fp->dmg.x1838_percentTemp`
  //   (fighter.dmg:+0x1838), which accumulates the float damage this frame:
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076640 (adds `*dmg` into x1838_percentTemp).
  //
  // Percent-term selection (non-WSK branch):
  //
  // The GALE01 asm has a flag-gated path that replaces the `percent_int = (int)fp->dmg.x1830_percent`
  // term with one of two p_ftCommonData ints (0x6D4/0x6D8):
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0 (0x80079B68..0x80079BA0)
  // - if (lbz fp+0x2225) & 0x01: choose base from p_ftCommonData, else use (int)fp->dmg.x1830_percent
  // - if (lbz fp+0x2224) & 0x20: choose +0x6D8, else +0x6D4
  //
  // Decomp names for these bits (confirmed by matching bitfield operations in ftCommon_GrabMash asm):
  // - fp->x2225_b7 (mask 0x01) and fp->x2224_b2 (mask 0x20)
  // refs/melee/src/melee/ft/types.h

  float weight = 100.0f;
  if (d != NULL && d->weight > 0.0f) {
    weight = d->weight;
  }

  // Shared prelude in asm (both WSK / non-WSK):
  // - f1 = fp->co_attrs.weight * p_ftCommonData->0xF4
  // - denom = 1.0 + f1
  // - tmp = (f1 * p_ftCommonData->0xF8) / denom
  // - weight_factor = p_ftCommonData->0xF8 - tmp
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
  const float one = combat_damage_ftColl_804D82EC_one();
  const float w = weight * c->kb_weight_mul;  // p_ftCommonData->0xF4
  const float denom = one + w;
  const float tmp =
      (denom != 0.0f) ? ((w * c->kb_weight_mul2) / denom) : 0.0f;  // p_ftCommonData->0xF8
  const float weight_factor = c->kb_weight_mul2 - tmp;             // p_ftCommonData->0xF8

  const float kbg_scale = combat_damage_ftColl_804D8314_kbg_mul() * (float)hitbox_kbg;
  const float bkb_f = (float)hitbox_bkb;

  float kb = 0.0f;
  if (hitbox_wsk != 0u) {
    // WSK branch (HitCapsule.x28 != 0):
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x80079EC8..0x80079F60)
    //
    // Interpreting HitCapsule fields from lb/types.h:
    // - x28 = WSK
    // - x24 = KBG
    // - x2C = BKB
    //
    // The branch replaces the `(s*dmg)/20 + s)/10` part with a WSK-derived term:
    // - wsk_term = p_ftCommonData->0x118 * WSK
    // - t = (p_ftCommonData->0x114 * wsk_term) + (p_ftCommonData->0x118 * p_ftCommonData->0x110)
    // - kb = (kbg/100) * (p_ftCommonData->0x11C * (weight_factor * t) + p_ftCommonData->0x120) + BKB
    const float wsk = (float)hitbox_wsk;
    const float wsk_term = c->kb_wsk_mul * wsk;  // p_ftCommonData->0x118
    const float t =
        c->kb_dmg_mul * wsk_term + (c->kb_wsk_mul * c->kb_base_term);  // 0x114, 0x118, 0x110
    const float inner = c->kb_growth_mul * (weight_factor * t) + c->kb_base_add;  // 0x11C, 0x120
    kb = bkb_f + kbg_scale * inner;

    // The asm multiplies by `ftColl_804D82EC` (1.0) three times before returning.
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x80079F54..0x80079F5C)
    kb = one * (one * (one * kb));
  } else {
    // Non-WSK branch:
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x80079F64..0x8007A04C)
    //
    // s = percent_int + dmg_temp, where dmg_temp is fp->dmg.x1838_percentTemp (float).
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x80079F8C..0x8007A00C)
    float percent_int = (float)(int)defender_percent_pre;  // fctiwz
    if (defender_dmg_x2225_b7) {
      // Use p_ftCommonData base ints instead of (int)percent_pre.
      const int32_t base =
          defender_dmg_x2224_b2 ? c->ftcoll_percent_base_x6d8 : c->ftcoll_percent_base_x6d4;
      percent_int = (float)base;
    }
    const float s = percent_int + defender_percent_temp;
    const float dmg = (float)hitbox_damage_i;  // HitCapsule.unk_count analogue

    // term = s * (p_ftCommonData->0x110 + p_ftCommonData->0x114 * dmg)
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x8007A008..0x8007A034)
    const float term = s * (c->kb_base_term + c->kb_dmg_mul * dmg);                  // 0x110, 0x114
    const float inner = c->kb_growth_mul * (weight_factor * term) + c->kb_base_add;  // 0x11C, 0x120
    kb = bkb_f + kbg_scale * inner;

    // The asm multiplies by `ftColl_804D82EC` (1.0) three times before returning.
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x8007A044..0x8007A04C)
    kb = one * (one * (one * kb));
  }

  // Collision multiplier chain (post-formula):
  //
  // ftColl_80079AB0 multiplies the computed KB by:
  // - gm_8016B248() (StartMeleeRules.x30),
  // - Player_GetAttackRatio(attacker_slot),
  // - Player_GetDefenseRatio(defender_slot),
  // before clamping to p_ftCommonData->0x108 (kb_applied_max).
  //
  // Call-site evidence (fighter-vs-fighter):
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C
  // - 0x8007A130..0x8007A148: Player_GetDefenseRatio / Player_GetAttackRatio / gm_8016B248
  // - 0x8007A160: call ftColl_80079AB0
  //
  // In-function evidence:
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
  // - 0x80079B58..0x80079B64 and 0x80079C48..0x80079C50: `fmuls` chain by (f1,f2,f3).
  if (collision_kb_mul != 1.0f) {
    kb *= collision_kb_mul;
  }

  // Collision clamps to p_ftCommonData->0x108 (max KB).
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8 (0x8007A050..0x8007A060)
  if (kb > c->kb_applied_max) {
    kb = c->kb_applied_max;
  }

  // Decomp: ftCo_Damage_CalcKnockback applies squat scaling for [Squat, SquatWait].
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
  if (defender_action_id == (uint16_t)MSL_ACT_SQUAT ||
      defender_action_id == (uint16_t)MSL_ACT_SQUAT_WAIT) {
    kb *= c->kb_squat_mul;
  }

  // Decomp: ftCo_Damage_CalcKnockback applies additional state-based KB multipliers:
  // - DamageIce: kb *= p_ftCommonData->kb_ice_mul
  // - Smash charge: kb *= p_ftCommonData->kb_smashcharge_mul (when fp->smash_attrs.state == Charging)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
  if (defender_action_id == (uint16_t)MSL_ACT_DAMAGE_ICE) {
    kb *= c->kb_ice_mul;
  }
  // Smash-charge state is owned by the grounded-smash command/input lifecycle:
  // - opcode 56 (`ftAction_80073008`) seeds SmashState_PreCharge through ftCo_800DEE84.
  // - `ftCo_800DF0D0` promotes held-A PreCharge to SmashState_Charging.
  // - this damage path consumes exactly that Charging state for the KB multiplier.
  // refs/melee/src/melee/ft/ft_0DF0.c::{ftCo_800DEE84,ftCo_800DF0D0}
  // Source: data/moves/{fox,falco}.json `start_smash_charge` events.
  if (defender_kb_smashcharge_active) {
    kb *= c->kb_smashcharge_mul;
  }

  // Decomp: ftCo_Damage_CalcKnockback subtracts armor and clamps to kb_min. We do not model armor yet;
  // keep the kb_min clamp.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
  if (kb < c->kb_min) {
    kb = c->kb_min;
  }
  return kb;
}

static inline void combat_damage_merge_vel_after_window(MslBatch* batch, size_t d_idx, float x,
                                                        float y) {
  if (batch == NULL) {
    return;
  }
  const float cur_x = batch->state.speed_x_attack[d_idx];
  const float cur_y = batch->state.speed_y_attack[d_idx];
  if (cur_x * x < 0.0f) {
    batch->state.speed_x_attack[d_idx] = cur_x + x;
  } else if (fabsf(x) > fabsf(cur_x)) {
    batch->state.speed_x_attack[d_idx] = x;
  }
  if (cur_y * y < 0.0f) {
    batch->state.speed_y_attack[d_idx] = cur_y + y;
  } else if (fabsf(y) > fabsf(cur_y)) {
    batch->state.speed_y_attack[d_idx] = y;
  }
}

void combat_damage_calc_vel(MslBatch* batch, size_t d_idx, float x, float y) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  const int16_t time_since_hit = batch->state.damage_time_since_hit_x18ac[d_idx];
  const int32_t merge_window = (c != NULL) ? c->kb_vel_merge_since_hit_frames : (int32_t)0;

  // Decomp: ftCo_Damage_CalcVel replaces fp->x8c_kb_vel while
  // `fp->dmg.x18AC_time_since_hit < p_ftCommonData->xFC`; after that window it merges same-axis
  // vectors by adding opposite signs and keeping the larger same-sign magnitude.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcVel
  // refs/melee/src/melee/ft/types.h (fp+0x18AC, fp+0x8C)
  // data/common/ft_common_data.json: kb_vel_merge_since_hit_frames
  if ((int32_t)time_since_hit < merge_window) {
    batch->state.speed_x_attack[d_idx] = x;
    batch->state.speed_y_attack[d_idx] = y;
    return;
  }

  combat_damage_merge_vel_after_window(batch, d_idx, x, y);
}

static inline void combat_damage_mark_entry_time_since_hit(MslBatch* batch, size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp: ftCo_8008DCE0 sets fp->dmg.x18AC_time_since_hit = 0 after installing the Damage
  // callbacks and x221C_b6 on fresh Damage entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  batch->state.damage_time_since_hit_x18ac[d_idx] = 0;
}

static inline void combat_source_owner_clear_ftCommon_800804FC(MslBatch* batch, size_t d_idx) {
  if (batch != NULL && batch->state.on_ground[d_idx] != 0u) {
    msl_damage_source_clear(batch, d_idx);
  }
}

static inline uint8_t combat_source_port0_for_attacker(const MslBatch* batch, size_t a_idx,
                                                       int attacker) {
  return msl_damage_source_port0_for_slot(batch, a_idx, attacker);
}

static inline void combat_processhit_commit_source_owner(MslBatch* batch, size_t d_idx,
                                                         uint8_t source_port) {
  if (batch == NULL) {
    return;
  }
  msl_damage_source_commit_processhit(batch, d_idx, source_port);
}

static inline uint16_t combat_damage_hitstun_from_kb(const MslCommonParams* c, float kb_applied) {
  // Decomp: hitstun frames left are `mv.co.damage.x0 = (int)(kb_applied * p_ftCommonData->x154)`,
  // with a minimum of 1.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_ScaleBy154
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  if (c == NULL) {
    return 1;
  }
  int hs = (int)(kb_applied * c->damage_hitstun_mul);  // p_ftCommonData->0x154
  if (hs <= 0) {
    hs = 1;
  }
  if (hs > 0xFFFF) {
    hs = 0xFFFF;
  }
  return (uint16_t)hs;
}

static inline uint8_t combat_damage_meteor_cancel_x1a_from_raw_angle(const MslCommonParams* c,
                                                                     uint16_t raw_angle) {
  if (c == NULL) {
    return 0u;
  }
  // Decomp: ftColl_8007AC68 writes mv.co.damage.x1A from the hit's source knockback angle before
  // doIasa later admits meteor-cancel escape dispatch. The retained runtime escape branch below is
  // JumpAerial-only; SpecialHi admission remains a separate source-owner boundary. Sakurai angle
  // 361 takes a separate ftCo_Damage_CalcAngle branch and never calls ftColl_8007AC68.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AC68
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_CalcAngle,doIasa}
  if (raw_angle == 361u) {
    return 0u;
  }
  return (raw_angle >= c->damage_meteor_cancel_angle_min_deg &&
          raw_angle <= c->damage_meteor_cancel_angle_max_deg)
             ? 1u
             : 0u;
}

static inline uint8_t combat_illusion_owner_is_end_phase(uint8_t char_id, uint16_t action_id_u16) {
  const uint8_t fx_kind = msl_motion_state_fx_special_kind(char_id, action_id_u16);
  return (uint8_t)(fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_S_END ||
                   fx_kind == (uint8_t)MSL_FX_KIND_SPECIAL_AIR_S_END);
}

static inline uint16_t combat_item_damage_meteor_cancel_raw_angle(const MslBatch* batch,
                                                                  size_t attacker_idx,
                                                                  uint8_t item_is_illusion,
                                                                  uint16_t item_angle) {
  if (batch == NULL || !item_is_illusion) {
    return item_angle;
  }

  // Illusion/Phantasm article damage angle and meteor-cancel x1A provenance are not identical
  // during the main dash phase: the live article can apply the downward state1 hitbox while
  // ftCo_Damage_CalcAngle's x1A source only appears on the End-phase owner. Keep the damage/KB
  // angle table-backed, but suppress x1A for SpecialS/SpecialAirS main-phase article hits.
  // Positive/negative replay locks: GAT doubles rec1146 (SpecialAirSEnd x1A) and HVG rec552
  // (SpecialAirS downward hit without x1A).
  // refs/melee/src/melee/it/items/itfoxillusion.c::{it_8029CFF0,itFoxillusion_UnkMotion1_Phys}
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialS.c::{
  //   ftFx_SpecialS_Anim,ftFx_SpecialAirS_Anim,ftFx_SpecialSEnd_Anim,ftFx_SpecialAirSEnd_Anim}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcAngle
  return combat_illusion_owner_is_end_phase(batch->state.char_id[attacker_idx],
                                            batch->state.action_id[attacker_idx])
             ? item_angle
             : UINT16_MAX;
}

uint8_t combat_damage_severity_u8_from_kb(const MslCommonParams* c, float kb_applied) {
  // Decomp: ftCo_8008DCE0 derives severity by comparing `kb_applied * x154` against thresholds:
  // - < x158 => 0
  // - < x15C => 1
  // - < x160 => 2
  // - else   => 3
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008D8E8
  if (c == NULL) {
    return 0;
  }
  const float f = kb_applied * c->damage_hitstun_mul;  // p_ftCommonData->0x154
  if (f < c->damage_severity_x158) {                   // p_ftCommonData->0x158
    return 0;
  }
  if (f < c->damage_severity_x15c) {  // p_ftCommonData->0x15C
    return 1;
  }
  if (f < c->damage_severity_x160) {  // p_ftCommonData->0x160
    return 2;
  }
  return 3;
}

static inline void combat_damage_enter_state(
    const MslCommonParams* c, MslBatch* batch, int bi, size_t d_idx,
    uint8_t defender_on_ground_before, uint8_t defender_on_ground_after, uint8_t hurt_height,
    float kb_applied, float kb_angle_rad, uint16_t raw_kb_angle, size_t source_a_idx,
    int source_attacker, size_t source_hb_i, uint8_t source_hb_valid, size_t source_cap_i,
    uint8_t source_cap_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb,
    uint16_t defender_motion_id, uint16_t source_item_type, uint8_t source_item_state,
    uint8_t force_tumble_severity) {
  if (batch == NULL) {
    return;
  }

  if (hurt_height > 2u) {
    hurt_height = 2u;
  }
  if (combat_sheik_chain_start_terminal_owns_low_hurt_height(
          batch, source_a_idx, source_hb_i, defender_on_ground_before, source_hb_valid)) {
    hurt_height = 0u;
  }

  const uint8_t sev = force_tumble_severity ? 3u : combat_damage_severity_u8_from_kb(c, kb_applied);
  uint16_t act = (uint16_t)MSL_ACT_WAIT;
  uint32_t sm = (uint32_t)MSL_SM_WAIT1_0;

  const uint16_t pre_damage_action = batch->state.action_id[d_idx];
  if (batch->state.char_id[d_idx] == (uint8_t)MSL_CHAR_ID_SHEIK) {
    // Source take-damage/death callback owner:
    // Fighter_ProcessHit reaches ftCommon_8007DB58 before ftCo_8008DCE0 changes motion state and
    // Fighter_ChangeMotionState clears callback pointers. ftSk_Init_80110198 (the Sheik
    // take-damage/death callback, installed by SpecialN and by SpecialS while a live Chain article
    // exists) drops the live held Needle stock or clears the stored count if the held pointer is
    // already null; the callee gates on the pre-damage action and Chain ownership accordingly.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007DB58
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_Init.c::ftSk_Init_80110198
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialN.c::ftSk_SpecialN_80111FBC
    items_sheik_needle_damage_callback(batch, bi, (int)(d_idx % (size_t)MSL_MAX_PLAYERS),
                                       pre_damage_action, defender_on_ground_before);
  }
  const uint8_t downed_damage_contact =
      (uint8_t)(combat_is_downed_damage_contact_action(pre_damage_action) &&
                (batch->state.dmg_x2224_b2[d_idx] ||
                 batch->state.percent_temp[d_idx] < (float)c->down_damage_percent_threshold));

  if (downed_damage_contact) {
    // Downed contact damage override:
    // - ftCo_8009F0F0 intercepts DownBound/DownWait/DownDamage before the generic damage-state
    //   result when `x2224_b2` is set or same-frame percentTemp is below p_ftCommonData->x428.
    // - ftCo_8009F184 re-enters ftCo_8008DCE0 with an explicit DownDamage motion id.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
    //   ftCo_8009F0F0,ftCo_8009F184}
    act = combat_down_damage_action_from_source(pre_damage_action);
    sm = combat_down_damage_submotion_from_action(act);
    if (defender_on_ground_after != 0u && batch->state.frame_start_on_ground[d_idx] == 0u) {
      // Downed-contact damage can be selected after this sim's map-collision pass has refreshed a
      // carried DownBound floor, but source ftCo_8009F0F0/ftCo_8009F184 runs from the current
      // damage/contact callback lifetime and does not turn a frame-start airborne DownBound into a
      // grounded DownDamage publication. Preserve that pre-map airborne ground_or_air; CollData
      // floor.index remains available for the following DownDamage_Coll callback.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
      //   ftCo_8009F0F0,ftCo_8009F184,ftCo_DownDamage_Coll}
      batch->state.on_ground[d_idx] = 0u;
    }
  } else if (sev == 3u) {
    // High-knockback / tumble-style damage states.
    //
    // Decomp: ftCo_8008DCE0 chooses DamageFly* for var_r28==3 and later conditionally overrides
    // to DamageFlyTop based on angle.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    //
    // Decomp: ftCo_8008DCE0 includes an additional RNG-gated DamageFlyRoll path when airborne,
    // not in the DamageFlyTop window, and percent >= a common-data threshold.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (block_33)
    //
    // This sim does not currently model the global RNG stream (HSD_Randf consumers). Keep the
    // DamageFlyRoll branch disabled until we have a decomp-backed RNG site/stream.
    // Decomp order in ftCo_8008DCE0:
    // - Ground-vs-air KB handling can call ftCommon_8007D5D4 (blocks 21-28), which flips
    //   `ground_or_air` to Air for launched grounded victims.
    // - DamageFlyTop window check runs later and gates on the *current* `ground_or_air` (block_36).
    //
    // Sim mapping for `defender_on_ground_after`:
    // - this is sampled immediately after this contact's KB application in combat pass 1
    //   (i.e. after we may clear `state.on_ground[d_idx]` on launch in the same damage apply),
    // - before later frame systems (stage collision/physics integration of resulting velocity).
    //
    // Intentional scope: only this tumble DamageFlyTop window uses post-KB grounded state. The
    // broader low/med airborne-vs-grounded damage state split below continues to use the pre-hit
    // grounded flag to avoid changing non-tumble behavior.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    if (!defender_on_ground_after && c != NULL) {
      // DamageFlyTop window (radians).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (block_33)
      // refs/melee/src/melee/ft/types.h (ftCommonData offsets 0x234/0x238)
      if (kb_angle_rad > c->damagefly_top_angle_min_radians &&
          kb_angle_rad < c->damagefly_top_angle_max_radians) {
        act = (uint16_t)MSL_ACT_DAMAGE_FLY_TOP;
        sm = (uint32_t)MSL_SM_DAMAGE_FLY_TOP;
      } else {
        // RNG-gated DamageFlyRoll lane (decomp block_33):
        // - sev==3 (var_r28)
        // - airborne after KB ownership (fp->ground_or_air == GA_Air)
        // - outside DamageFlyTop angle window
        // - percent >= p_ftCommonData->x23C
        // - HSD_Randf() < p_ftCommonData->x240
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
        // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randf
        //
        // Runtime mapping:
        // - percent lane uses replay-seeded percent + per-frame damage accumulator (x1838).
        // - RNG stream ownership is active by default; keep
        //   MSL_RNG_ENABLE_DAMAGE_FLY_ROLL_GATE=1 as a debug kill-switch for ablations.
        const uint16_t pre_action = batch->state.action_id[d_idx];
        const uint8_t speciallw_start_rng_owner =
            combat_damageflyroll_speciallw_start_hitcapsule_owner(
                batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                source_hitbox_bkb);
        const uint8_t jumpaerial_attackairb_carry_owner =
            combat_damageflyroll_jumpaerial_attackairb_carry_selected_owner(
                batch, d_idx, source_a_idx, source_attacker, source_cap_i, source_cap_valid);
        const uint8_t jumpaerial_illusion_article_owner =
            combat_damageflyroll_jumpaerial_illusion_article_owner(
                batch, d_idx, source_a_idx, source_attacker, source_hb_valid, source_item_type,
                source_item_state);
        // Side-special article BODY source:
        // itFoxIllusion/Phantasm articles apply item HitCapsules through Fighter_ProcessHit, so
        // there is no fighter source HitCapsule/hurtcap pair for the regular BODY damage log. The
        // generated MSLITAR1 side_special_illusion_itkind is the source proof. Grounded
        // Wait/Dash/etc. victims can be launched to air before ftCo_8008DCE0's terminal selection,
        // so this owner is not inferred from the victim's pre-action family. Unlike fighter BODY
        // ftColl_80078538 entries, this item article path reaches the common damage-state entry
        // without a modeled fighter BODY damage-effect RNG prefix.
        // refs/melee/src/melee/it/items/itfoxillusion.c::{itFoxIllusion_Logic14_DmgDealt,it_8029CFF0}
        // refs/melee/src/melee/it/itcoll.c::it_80272460
        // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
        // data/items/articles/fox_falco.bin (MSLITAR1 side_special_illusion_itkind)
        const uint8_t side_special_article_body_owner =
            (uint8_t)(source_hb_valid == 0u && source_item_state <= 1u &&
                      item_article_params_is_illusion_item_type(source_item_type) != 0u);
        const uint8_t specialhifall_attackairb_create_owner =
            combat_damageflyroll_specialhifall_attackairb_create_hitcapsule_owner(
                batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                source_cap_i, source_cap_valid, source_motion_id);
        uint8_t pre_action_gate_owner =
            combat_damageflyroll_rng_subset_allows_pre_action(batch, d_idx, pre_action);
        if ((pre_action == (uint16_t)MSL_ACT_JUMP_AERIAL_F ||
             pre_action == (uint16_t)MSL_ACT_JUMP_AERIAL_B) &&
            batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] == 0u) {
          pre_action_gate_owner =
              (uint8_t)(jumpaerial_attackairb_carry_owner || jumpaerial_illusion_article_owner);
        }
        enum { MSL_DAMAGEFLYROLL_SPECIALHIFALL_ENTRY_ENABLE_EDGE_FRAME_MAX = 3 };
        if (msl_motion_state_fx_special_kind(batch->state.char_id[d_idx], pre_action) ==
                (uint8_t)MSL_FX_KIND_SPECIAL_HI_FALL &&
            batch->state.action_frame[d_idx] >
                MSL_DAMAGEFLYROLL_SPECIALHIFALL_ENTRY_ENABLE_EDGE_FRAME_MAX &&
            specialhifall_attackairb_create_owner == 0u) {
          // Late SpecialHiFall can have an unrelated AttackAirB enable edge live in the attacker,
          // but the source gate is owned by the selected DmgLog HitCapsule, not visible
          // pre-action plus any BAir edge. The first SpecialHiFall callbacks keep the existing
          // terminal enable-edge owner; late rows without selected proof stay seed-owned.
          // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{
          //   ftFx_SpecialHiFall_Anim,ftFx_SpecialHiFall_Phys,ftFx_SpecialHiFall_Coll}
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
          pre_action_gate_owner = 0u;
        }
        uint8_t damagefly_roll_rng_subset_ok =
            (uint8_t)(pre_action_gate_owner || jumpaerial_attackairb_carry_owner ||
                      jumpaerial_illusion_article_owner || side_special_article_body_owner ||
                      combat_damageflyroll_damageflytop_attackairb_root_x14_primary_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb) ||
                      combat_damageflyroll_damageflytop_attackairb_hb0_cap1_effect_prefix_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb) ||
                      combat_damageflyroll_damageflytop_attackairb_create_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid) ||
                      combat_damageflyroll_catch_strong_attackairn_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          defender_on_ground_before, source_motion_id, source_hitcapsule_int_dmg,
                          source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb) ||
                      combat_damageflyroll_landingairlw_strong_attackairn_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb) ||
                      combat_damageflyroll_attackairn_strong_attackairb_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb, defender_motion_id) ||
                      combat_damageflyroll_attackairb_late_attackairn_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
                          source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb,
                          defender_motion_id) ||
                      combat_damageflyroll_specialhifall_late_attackairn_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
                          source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb) ||
                      specialhifall_attackairb_create_owner ||
                      combat_damageflyroll_catch_late_attackairn_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
                          source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb) ||
                      combat_damageflyroll_kneebend_attacks3_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
                          source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb) ||
                      combat_damageflyroll_wait_attacklw3_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
                          source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb,
                          defender_on_ground_before, pre_action) ||
                      combat_damageflyroll_jump_hitlag_strong_attackairn_tiplog_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
                          source_hitbox_kbg, source_hitbox_bkb) ||
                      combat_damageflyroll_attackairn_specialairhi_strong_attackairlw_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i,
                          source_hb_valid) ||
                      combat_damageflyroll_recovering_ground_downattacku_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i,
                          source_hb_valid) ||
                      combat_damageflyroll_thrownf_throwf_hitlag_owner(
                          batch, d_idx, source_a_idx, source_attacker, defender_on_ground_before) ||
                      combat_damageflyroll_attackairb_strong_attackairb_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i,
                          source_hb_valid) ||
                      combat_damageflyroll_dash_weak_attackairb_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
                          source_hitbox_kbg, source_hitbox_bkb) ||
                      combat_damageflyroll_attackhi4_weak_attackairb_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb, defender_on_ground_before) ||
                      combat_damageflyroll_specialairhi_attackairb_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid) ||
                      combat_damageflyroll_specialairs_attackairb_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid) ||
                      combat_damageflyroll_fallspecial_attackairf_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb) ||
                      combat_damageflyroll_jump_late_attackhi4_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_hitcapsule_int_dmg,
                          source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb) ||
                      combat_damageflyroll_jump_strong_attackairn_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb) ||
                      combat_damageflyroll_sustained_jump_late_attackairn_leg_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb) ||
                      combat_damageflyroll_specialairhi_attacklw4_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb) ||
                      combat_damageflyroll_specialairn_attackairlw_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb) ||
                      combat_damageflyroll_recovery_action_attackairlw_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb, defender_on_ground_before) ||
                      combat_damageflyroll_landingairn_weak_attackairb_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb, defender_on_ground_before) ||
                      combat_damageflyroll_catch_attackairf_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, defender_on_ground_before,
                          source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
                          source_hitbox_kbg, source_hitbox_bkb) ||
                      combat_damageflyroll_kneebend_weak_attackairb_hitcapsule_owner(
                          batch, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
                          source_cap_i, source_cap_valid, source_motion_id,
                          source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                          source_hitbox_bkb) ||
                      speciallw_start_rng_owner ||
                      combat_damageflyroll_selected_source_normal_effect_prefix_count(
                          batch, d_idx, source_hb_i, source_hb_valid, source_cap_valid,
                          source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
                          source_hitbox_kbg, source_hitbox_bkb, pre_action) != 0u);
        const float percent_cur = batch->state.percent[d_idx] + batch->state.percent_temp[d_idx];
        if (damagefly_roll_rng_subset_ok &&
            percent_cur >= (float)c->damagefly_roll_percent_threshold) {
          combat_damageflyroll_consume_jumpaerial_attackairb_carry(
              batch, bi, d_idx, source_a_idx, source_attacker, source_cap_i, source_cap_valid);
          combat_damageflyroll_consume_damageflytop_attackairb_live_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_catch_strong_attackairn_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              defender_on_ground_before, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_landingairlw_strong_attackairn_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_attackairn_strong_attackairb_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb, defender_motion_id);
          combat_damageflyroll_consume_attackairn_specialairhi_strong_attackairlw_hitcapsule_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid);
          combat_damageflyroll_consume_catch_late_attackairn_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
              source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_recovering_ground_downattacku_hitcapsule_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid);
          combat_damageflyroll_consume_thrownf_throwf_hitlag_count(
              batch, bi, d_idx, source_a_idx, source_attacker, defender_on_ground_before);
          combat_damageflyroll_consume_attackairb_strong_attackairb_hitcapsule_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid);
          combat_damageflyroll_consume_jump_hitlag_strong_attackairn_tiplog_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
              source_hitbox_bkb);
          combat_damageflyroll_consume_dash_weak_attackairb_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
              source_hitbox_bkb);
          combat_damageflyroll_consume_attackhi4_weak_attackairb_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before);
          combat_damageflyroll_consume_kneebend_attacks3_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_valid, source_motion_id, source_hitcapsule_int_dmg, source_hitbox_angle,
              source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_attacklw3_late_attackhi4_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before);
          combat_damageflyroll_consume_fallspecial_attackairf_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_jump_strong_attackairn_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_sustained_jump_late_attackairn_leg_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_specialairhi_attacklw4_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_recovery_action_attackairlw_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before);
          combat_damageflyroll_consume_landingairn_weak_attackairb_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before);
          combat_damageflyroll_consume_kneebend_weak_attackairb_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_catch_attackairf_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, defender_on_ground_before, source_motion_id,
              source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb);
          combat_damageflyroll_consume_speciallw_end_strong_attackairb_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before);
          combat_damageflyroll_consume_speciallw_end_continuing_weak_attackairb_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb, defender_on_ground_before);
          combat_damageflyroll_consume_specialhifall_strong_attackairb_count(
              batch, bi, d_idx, source_a_idx, source_attacker, source_hb_i, source_hb_valid,
              source_cap_i, source_cap_valid, source_motion_id, source_hitcapsule_int_dmg,
              source_hitbox_angle, source_hitbox_kbg, source_hitbox_bkb);
          {
            // Selected-source ftColl_80078538 normal-hit effect prefix draws before the gate;
            // count is named per owner inside the helper (1 create-edge entry or 2 sustained
            // dual-HitCapsule entries). Runs before the Fighter_8006CDA4 consume below to match
            // the source DmgLog -> ProcessHit order.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078538
            // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006CDA4}
            const uint8_t normal_effect_prefix_count =
                combat_damageflyroll_selected_source_normal_effect_prefix_count(
                    batch, d_idx, source_hb_i, source_hb_valid, source_cap_valid, source_motion_id,
                    source_hitcapsule_int_dmg, source_hitbox_angle, source_hitbox_kbg,
                    source_hitbox_bkb, pre_action);
            for (uint8_t i = 0u; i < normal_effect_prefix_count; i++) {
              (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_FTCOLL_DAMAGE_EFFECT, 1);
            }
          }
          combat_damageflyroll_consume_fighter_8006cda4_pre_gate_count(
              batch, bi, d_idx, source_a_idx, source_hb_i, source_hb_valid, source_cap_i,
              source_cap_valid, source_motion_id, source_hitcapsule_int_dmg);
          const float roll =
              combat_rng_consume_randf_site(batch, bi, MSL_RNG_SITE_DAMAGE_FLY_ROLL_GATE);

          if (!batch->debug_rng_enable_damage_fly_roll_gate && roll < c->damagefly_roll_prob) {
            act = (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL;
            sm = (uint32_t)MSL_SM_DAMAGE_FLY_ROLL;
          }
        }
      }
    }

    if (act == (uint16_t)MSL_ACT_WAIT) {
      if (hurt_height == 2u) {
        act = (uint16_t)MSL_ACT_DAMAGE_FLY_HI;
        sm = (uint32_t)MSL_SM_DAMAGE_FLY_HI;
      } else if (hurt_height == 1u) {
        act = (uint16_t)MSL_ACT_DAMAGE_FLY_N;
        sm = (uint32_t)MSL_SM_DAMAGE_FLY_N;
      } else {
        act = (uint16_t)MSL_ACT_DAMAGE_FLY_LW;
        sm = (uint32_t)MSL_SM_DAMAGE_FLY_LW;
      }
    }
  } else if (!defender_on_ground_before) {
    // Airborne low/med damage states (DamageAir1/2/3).
    if (sev == 0u) {
      act = (uint16_t)MSL_ACT_DAMAGE_AIR_1;
      sm = (uint32_t)MSL_SM_DAMAGE_AIR_1;
    } else if (sev == 1u) {
      act = (uint16_t)MSL_ACT_DAMAGE_AIR_2;
      sm = (uint32_t)MSL_SM_DAMAGE_AIR_2;
    } else {
      act = (uint16_t)MSL_ACT_DAMAGE_AIR_3;
      sm = (uint32_t)MSL_SM_DAMAGE_AIR_3;
    }
  } else {
    // Grounded low/med damage states select Hi/N/Lw group by the hit hurt height.
    // refs/melee/src/melee/ft/chara/ftCommon/forward.h (DamageHi*/DamageN*/DamageLw* ids)
    if (hurt_height == 2u) {
      act = (uint16_t)((sev == 0u)   ? MSL_ACT_DAMAGE_HI_1
                       : (sev == 1u) ? MSL_ACT_DAMAGE_HI_2
                                     : MSL_ACT_DAMAGE_HI_3);
      sm = (uint32_t)((sev == 0u)   ? MSL_SM_DAMAGE_HI_1
                      : (sev == 1u) ? MSL_SM_DAMAGE_HI_2
                                    : MSL_SM_DAMAGE_HI_3);
    } else if (hurt_height == 1u) {
      act = (uint16_t)((sev == 0u)   ? MSL_ACT_DAMAGE_N_1
                       : (sev == 1u) ? MSL_ACT_DAMAGE_N_2
                                     : MSL_ACT_DAMAGE_N_3);
      sm = (uint32_t)((sev == 0u)   ? MSL_SM_DAMAGE_N_1
                      : (sev == 1u) ? MSL_SM_DAMAGE_N_2
                                    : MSL_SM_DAMAGE_N_3);
    } else {
      act = (uint16_t)((sev == 0u)   ? MSL_ACT_DAMAGE_LW_1
                       : (sev == 1u) ? MSL_ACT_DAMAGE_LW_2
                                     : MSL_ACT_DAMAGE_LW_3);
      sm = (uint32_t)((sev == 0u)   ? MSL_SM_DAMAGE_LW_1
                      : (sev == 1u) ? MSL_SM_DAMAGE_LW_2
                                    : MSL_SM_DAMAGE_LW_3);
    }
  }

  batch->state.action_id[d_idx] = act;
  batch->state.animation_index[d_idx] = sm;
  if (c != NULL && msl_action_is_cliff_any(pre_damage_action)) {
    // Cliff-owned Damage entry ledge cooldown:
    // ftCo_8008E908 tests the old `fp->x221D_b7` cliff-ownership bit and writes
    // `fp->x2064_ledgeCooldown` before Fighter_ChangeMotionState clears the cliff state. This is
    // the runtime counterpart to the replay seed reconstruction in
    // msl_derive_ledge_cooldown_py; without it, free-running MissFoot can immediately regrab a
    // ledge after a cliff option is interrupted into Damage*.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E908
    const uint16_t cooldown = c->ledge_cooldown_frames;
    batch->state.ledge_cooldown[d_idx] = (cooldown > 0xFFu) ? 0xFFu : (uint8_t)cooldown;
  }
  // Fighter_ChangeMotionState reset clears fp->x221B_b0 (shield descriptor active) on damage
  // entry, so do not carry seeded Guard no-submotion shield-active bits into Damage* states.
  // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState reset block)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  {
    const size_t flags_i = d_idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221B_INDEX;
    batch->state.state_flags[flags_i] &= (uint8_t)~(uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
  }
  // Decomp: ftCo_8008DCE0 clears mv.co.damage.x14 on damage entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  batch->state.damage_jump_buffer_x14[d_idx] = 0;
  batch->state.damage_meteor_cancel_eligible_x1a[d_idx] =
      combat_damage_meteor_cancel_x1a_from_raw_angle(c, raw_kb_angle);
  // Decomp: ftCo_8008DCE0 clears the x670/x671 tilt timer window on damage entry. This matters
  // before the per-hitlag `ftCo_Damage_OnEveryHitlag` callback: fresh damage hitlag should not
  // inherit stale pre-hit flick timers into timer-window SDI.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  batch->state.tilt_timer_x[d_idx] = 0xFEu;
  batch->state.tilt_timer_y[d_idx] = 0xFEu;
  // Decomp: ftCo_8008DCE0 performs Fighter_ChangeMotionState then immediate ftAnim_8006EBA4.
  // This call path is inside Fighter_ProcessHit (prio 14), not Fighter_8006A360's `!hitlag`
  // callback gate, so the entry tick is consumed even when hitlag is currently active.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
  msl_anim_timebase_enter(batch, d_idx, 0.0f, 1.0f);
  batch->state.anim_frame_fp_q16_16[d_idx] += batch->state.frame_speed_mul_fp_q16_16[d_idx];
  msl_anim_timebase_recompute_derived(batch, d_idx);
}

static inline void combat_processhit_apply_bookkeeping(MslBatch* batch,
                                                       const MslCombatProcessHitResolved* ev) {
  if (batch == NULL || ev == NULL || ev->update_bookkeeping == 0u) {
    return;
  }
  staling_queue_update(batch, ev->a_idx, ev->stale_move_id, ev->stale_attack_instance);
  combat_combo_ftColl_800763C0(batch, ev->a_idx, ev->defender, ev->d_idx, ev->combo_attack_id);
}

static inline void combat_processhit_write_source(MslBatch* batch,
                                                  const MslCombatProcessHitResolved* ev) {
  if (batch == NULL || ev == NULL) {
    return;
  }
  batch->state.instance_hit_by[ev->d_idx] = ev->instance_hit_by;
  if (ev->source_write == MSL_PROCESS_HIT_SOURCE_WRITE_COMMIT_OWNER) {
    combat_processhit_commit_source_owner(batch, ev->d_idx, ev->last_hit_by);
  } else {
    msl_damage_source_write_direct(batch, ev->d_idx, ev->last_hit_by);
  }
}

static inline void combat_processhit_apply_hitlag_after_entry(
    MslBatch* batch, const MslCombatProcessHitResolved* ev) {
  if (batch == NULL || ev == NULL) {
    return;
  }
  uint8_t apply_flags = 0u;
  switch (ev->hitlag_mode) {
    case MSL_PROCESS_HITLAG_ASSIGN_IF_POSITIVE:
      if (ev->d_hl > 0u) {
        batch->state.hitlag[ev->d_idx] = ev->d_hl;
        combat_state_flags_set_is_hitlag(batch, ev->d_idx, ev->d_hl);
        apply_flags = 1u;
      }
      break;
    case MSL_PROCESS_HITLAG_FLAGS_IF_INCREASED:
      apply_flags = (ev->d_hl > ev->d_hl_prev) ? 1u : 0u;
      break;
    case MSL_PROCESS_HITLAG_NONE:
    default:
      break;
  }
  if (apply_flags != 0u) {
    if (batch->state.action_id[ev->d_idx] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL &&
        !combat_damageflyroll_weak_attackairb_source_skips_x1994(ev)) {
      // Runtime DamageFlyRoll hitlag-exit x1994 provenance:
      // - ftCo_8008DCE0 installs ftCo_Damage_OnExitHitlag for this live ProcessHit damage entry.
      // - Damage_OnExitHitlag later calls ftColl_8007B7A4(..., p_ftCommonData->x130), which arms
      //   x1994. Keep this as internal source state rather than inferring from arbitrary
      //   replay-seeded DamageFlyRoll action/hitlag rows.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
      //   ftCo_8008DCE0,ftCo_Damage_OnExitHitlag}
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B7A4
      batch->state.damageflyroll_runtime_x1994_on_exit[ev->d_idx] = 1u;
    }
    if (ev->hitlag_allows_sdi != 0u) {
      combat_damage_allow_sdi_set(batch, ev->d_idx);
    }
    if (ev->hitlag_sets_x221a != 0u) {
      combat_state_flags_set_x221a_b3(batch, ev->d_idx);
    }
  }
}

static inline void combat_processhit_apply_resolved_damage(const MslCommonParams* c,
                                                           MslBatch* batch,
                                                           const MslCombatProcessHitResolved* ev) {
  if (c == NULL || batch == NULL || ev == NULL) {
    return;
  }

  // Decomp-shaped Fighter_ProcessHit consumer:
  // - collision/item/throw producers fill the source-specific lanes in `ev`;
  // - this helper owns the shared percent aftermath: no-KB cleanup, KB velocity/state entry,
  //   hitstun flags, hitlag post-entry flags, source lanes, stale queue, and combo bookkeeping.
  //
  // Decomp/source trail:
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C}
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  if (ev->kb_applied == 0.0f) {
    if (ev->d_hl > ev->d_hl_prev) {
      batch->state.hitlag[ev->d_idx] = ev->d_hl;
      combat_state_flags_set_is_hitlag(batch, ev->d_idx, ev->d_hl);
    }
    batch->state.speed_x_attack[ev->d_idx] = 0.0f;
    batch->state.speed_y_attack[ev->d_idx] = 0.0f;
    batch->state.hitstun[ev->d_idx] = 0;
    batch->state.damage_meteor_cancel_eligible_x1a[ev->d_idx] = 0u;
    combat_state_flags_set_is_hitstun(batch, ev->d_idx, 0);
    combat_processhit_write_source(batch, ev);
    combat_processhit_apply_bookkeeping(batch, ev);
    return;
  }

  if (ev->defender_on_ground != 0u && ev->use_grounded_kb != 0u) {
    combat_damage_install_grounded_kb(c, batch, ev->d_idx, ev->kb_applied, ev->kb_x, ev->kb_y,
                                      ev->d_hl, ev->force_tumble_severity,
                                      ev->grounded_ecb_lock_owner);
  } else {
    combat_damage_calc_vel(batch, ev->d_idx, ev->kb_x, ev->kb_y);
  }
  if (ev->apply_throw_release_di != 0u) {
    combat_throw_release_apply_immediate_di(batch, ev->d_idx, c);
  }

  // Decomp: ftCo_8008DCE0 clears self velocity after installing damage KB.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  batch->state.speed_air_x_self[ev->d_idx] = 0.0f;
  batch->state.speed_ground_x_self[ev->d_idx] = 0.0f;
  batch->state.speed_y_self[ev->d_idx] = 0.0f;

  uint16_t hs = combat_damage_hitstun_from_kb(c, ev->kb_applied);
  if (hs > 1u &&
      combat_damage_hitstun_strong_attackairlw_terminal_damageflytop_subtracts(batch, ev)) {
    hs = (uint16_t)(hs - 1u);
  }
  batch->state.hitstun[ev->d_idx] = hs;
  combat_state_flags_set_is_hitstun(batch, ev->d_idx, hs);
  if (ev->clear_x221c_on_damage_entry != 0u) {
    combat_state_flags_clear_x221c_b0(batch, ev->d_idx);
  }
  combat_damage_mark_entry_time_since_hit(batch, ev->d_idx);

  MslEcbWorldPoints active_hitlag_attackair_ecb = {0};
  const uint8_t active_hitlag_attackair_ecb_valid =
      (ev->defender_on_ground == 0u && ev->d_hl != 0u && ev->source_is_item_hit != 0u &&
       ev->source_item_owns_motion_clear != 0u)
          ? combat_sample_active_hitlag_attackair_ecb(batch, ev->d_idx,
                                                      &active_hitlag_attackair_ecb)
          : 0u;
  const uint8_t defender_on_ground_after = batch->state.on_ground[ev->d_idx] ? 1u : 0u;
  const uint16_t pre_entry_action = batch->state.action_id[ev->d_idx];
  combat_damage_enter_state(
      c, batch, ev->bi, ev->d_idx, ev->defender_on_ground, defender_on_ground_after,
      ev->hurt_height, ev->kb_applied, ev->kb_angle_rad, ev->damage_state_raw_angle, ev->a_idx,
      ev->attacker, ev->source_hb_i, ev->source_hb_valid, ev->source_cap_i, ev->source_cap_valid,
      ev->source_motion_id, ev->source_hitcapsule_int_dmg, ev->source_hitbox_angle,
      ev->source_hitbox_kbg, ev->source_hitbox_bkb, ev->d_motion_id, ev->source_item_type,
      ev->source_item_state, ev->force_tumble_severity);
  const uint16_t post_entry_action = batch->state.action_id[ev->d_idx];
  if (post_entry_action != pre_entry_action && ev->source_is_item_hit != 0u &&
      ev->source_item_owns_motion_clear != 0u &&
      !msl_motion_state_has_motion_flag(batch->state.char_id[ev->d_idx], post_entry_action,
                                        MSL_MOTION_FLAG_SKIP_HIT)) {
    // Fighter_ChangeMotionState clears x914 HitCapsules through ftColl_8007AFF8 unless the
    // destination MotionState carries Ft_MF_SkipHit. ProcessHit damage entry can run while hitlag
    // is active, before the next hitboxes_refresh pass, so clear the live outgoing capsules here
    // instead of letting frozen-hitlag preservation carry the old attack into later Needle
    // item/contact callbacks. Keep this on the retained thrown-Needle owner: broader independent
    // projectile sources can resolve after fighter collision in source order, so clearing their
    // victim's outgoing HitCapsules immediately would erase same-frame reciprocal fighter hits.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AFF8,ftColl_8007ABD0,ftColl_80076ED8}
    // refs/melee/src/melee/it/itcoll.c::it_80272460
    hitboxes_clear_player_active(batch, ev->bi, ev->defender);
  }
  if (ev->grounded_ecb_lock_owner != 0u && ev->d_hl != 0u) {
    combat_publish_damage_entry_hitlag_ecb_current(batch, ev->d_idx);
  }
  if (active_hitlag_attackair_ecb_valid != 0u) {
    combat_publish_damage_hitlag_ecb_points(batch, ev->d_idx, &active_hitlag_attackair_ecb);
    if (ev->source_item_owns_motion_clear != 0u) {
      // Thrown-Needle item BODY owns the frozen active-hitlag Damage ECB even if the article's
      // DmgDealt callback destroys or bounces it later in the same item pass. Persist the source
      // kind on the victim's CollData state instead of rediscovering it from live item slots.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_ProcessHit_8006D1EC}
      // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgDealt
      batch->state.coll_damage_hitlag_ecb_source_kind[ev->d_idx] =
          MSL_DAMAGE_HITLAG_ECB_SOURCE_THROWN_NEEDLE;
    }
  }
  combat_processhit_apply_hitlag_after_entry(batch, ev);
  if (ev->apply_guard_reflect_followup != 0u) {
    combat_apply_guard_reflect_body_hit_followup(c, batch, ev->d_idx, ev->d_motion_id);
  }
  combat_processhit_write_source(batch, ev);
  combat_processhit_apply_bookkeeping(batch, ev);
}

static inline void combat_mutations_pass1_future_apply_body_hit_invincible(
    MslBatch* batch, size_t a_idx, size_t hb_i, uint16_t attacker_motion_id) {
  if (batch == NULL) {
    return;
  }

  // "Invincible BODY contact" (no damage / no KB / no hitstun), but attacker still experiences hitlag.
  //
  // Decomp-first evidence (GALE01):
  // - Collision performs hurtcapsule checks if `x1988 != 2 && x198C != 2` ("not intangible"):
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868
  // - Even when the defender is invincible (x1988/x198C != 0), the hit handler still computes
  //   attacker-side max int damage (`fp0->dmg.x1914 = max(..., getEnvDmg(dmg))`) before returning
  //   without applying percentTemp/KB to the defender:
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
  // - Fighter_ProcessHit consumes `fp->dmg.x1914` (deal-dmg path) to drive hitlag via ftCommon_CalcHitlag:
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //
  // Selection-side rehit suppression is handled at the call-site (hitlists).

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Match the BODY stale-move ordering: apply staling to float damage before getEnvDmg, then use the
  // resulting int as hitlag input.
  const uint16_t move_id = staling_move_id_from_state(batch, a_idx);
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, move_id);

  float dmg_f = combat_apply_attacker_smash_release_damage_mul(batch, a_idx,
                                                               batch->state.hitbox_damage[hb_i]);
  if (stale_mult != 1.0f) {
    dmg_f *= stale_mult;
  }

  const int dmg_i = combat_get_env_dmg(dmg_f);
  if (dmg_i <= 0) {
    return;
  }

  // Decomp/ASM: electric hitlag multiplier is written to the *victim* fighter's fp+0x1960 in
  // ftColl_8007A06C when element==HitElement_Electric; attacker-side CalcHitlag uses default 1.0.
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C (stfs ... 0x1960(r25))
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (ftCommon_CalcHitlag(..., x1960))
  const uint16_t a_hl = combat_calc_hitlag_frames(c, dmg_i, attacker_motion_id, 1.0f);
  if (!combat_received_kb_hitlag_owns_over_deal_hitlag(batch, a_idx) &&
      a_hl > batch->state.hitlag[a_idx]) {
    batch->state.hitlag[a_idx] = a_hl;
    combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
  }
  // Invincible BODY contact still sets fp->dmg.x1914 (ftColl_80076ED8 writes without applying
  // percent/KB), so deal_dmg_cb owners fire here too.
  falcon_speciallw_on_deal_dmg_x1914(batch, a_idx);
  puff_rollout_on_deal_dmg(batch, a_idx);
}

// Marth Counter intercept (ftMs_SpecialLw): while the script-armed window is live
// (speciallw_counter_window == 2), a fighter BODY contact is consumed by the counter
// descriptor instead of damage intake: both sides take normal CalcHitlag hitlag, the
// defender stores incoming_int_damage * x5C (consumed by the Roy/Emblem LwHit override;
// Marth's LwHit keeps script damage), flips to face the attacker, and enters
// SpecialLwHit (ground 370 / air 372) at frame 0.
// refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{ftMs_SpecialLw_Anim,
//   ftMs_SpecialLw_80139140,ftMs_SpecialLwHit_Anim}
// refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8 (descriptor install + hit callback)
// Counter intercept descriptor sphere: the AbsorbDesc (bone, local offset, radius) installed by
// ftColl_8007B1B8 while the script window is live, posed at the defender's current animation.
// World composition mirrors the engine's bone-anchored sampler (api.c debug_sample_hitbox_center
// proxy): local offset through the part matrix, model scale on offsets, facing on the x/z swap,
// fighter scale_y on the radius (shield/hitbox radius policy).
// refs/melee/src/melee/ft/chara/ftMars/types.h::MarsAttributes::x64 (AbsorbDesc)
// refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_8007AEE0}
static inline uint8_t marth_counter_desc_world_sphere(const MslBatch* batch, size_t d_idx,
                                                      float* out_x, float* out_y, float* out_z,
                                                      float* out_r) {
  const uint8_t cid = batch->state.char_id[d_idx];
  const MslCharParams* ms_ch = msl_char_params_fast(cid);
  if (ms_ch == NULL || !(ms_ch->speciallw_counter_desc_size > 0.0f)) {
    // FAIL CLOSED: the intercept is descriptor-backed; without extracted AbsorbDesc data there
    // is no counter (no silent fall-back to body-contact admission).
    return 0u;
  }
  const uint16_t msid = (uint16_t)(batch->state.animation_index[d_idx] & 0xFFFFu);
  const uint16_t frame =
      msl_anim_frame_floor_u16(msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]));
  float m[12];
  if (anim_pose_get_matrix(cid, msid, frame, (uint16_t)ms_ch->speciallw_counter_desc_bone, m) !=
      0) {
    // FAIL CLOSED: an unposeable descriptor (e.g. Slippi no-submotion sentinel anim on a
    // teacher-forced seed row) cannot intercept. Live rollout entries always carry the real
    // SpecialLw submotion (323/325), so this path is reachable only from degenerate seeds.
    return 0u;
  }
  const float scale_y = batch->state.fighter_scale_y[d_idx];
  const float model_scaling =
      (isfinite(ms_ch->model_scaling) && ms_ch->model_scaling > 0.0f) ? ms_ch->model_scaling : 1.0f;
  const float model_scale = scale_y * model_scaling;
  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float off[3] = {ms_ch->speciallw_counter_desc_offset_x,
                        ms_ch->speciallw_counter_desc_offset_y,
                        ms_ch->speciallw_counter_desc_offset_z};
  float cx = 0.0f, cy = 0.0f, cz = 0.0f;
  msl_mtx34_mul_point(m, off, &cx, &cy, &cz);
  cx *= model_scale;
  cy *= model_scale;
  cz *= model_scale;
  *out_x = facing_dir * cz + batch->state.pos_x[d_idx];
  *out_y = cy + batch->state.pos_y[d_idx];
  *out_z = -facing_dir * cx + batch->state.pos_z[d_idx];
  *out_r = ms_ch->speciallw_counter_desc_size * scale_y;
  return 1u;
}

static inline uint8_t marth_counter_desc_overlaps_hitbox(const MslBatch* batch, size_t d_idx,
                                                         size_t hb_i) {
  float wx, wy, wz, r;
  if (!marth_counter_desc_world_sphere(batch, d_idx, &wx, &wy, &wz, &r)) {
    return 0u;
  }
  const float dx = batch->state.hitbox_x[hb_i] - wx;
  const float dy = batch->state.hitbox_y[hb_i] - wy;
  const float dz = batch->state.hitbox_z[hb_i] - wz;
  const float rr = r + batch->state.hitbox_radius[hb_i];
  return (dx * dx + dy * dy + dz * dz <= rr * rr) ? 1u : 0u;
}

// Item/projectile variant: items travel in the x/y plane; test the descriptor disc against the
// item position with the item's contact radius.
static inline uint8_t marth_counter_desc_overlaps_point(const MslBatch* batch, size_t d_idx,
                                                        float px, float py, float extra_r) {
  float wx, wy, wz, r;
  if (!marth_counter_desc_world_sphere(batch, d_idx, &wx, &wy, &wz, &r)) {
    return 0u;
  }
  (void)wz;
  const float dx = px - wx;
  const float dy = py - wy;
  const float rr = r + extra_r;
  return (dx * dx + dy * dy <= rr * rr) ? 1u : 0u;
}

static inline uint8_t marth_counter_intercepts_contact(const MslBatch* batch, size_t d_idx) {
  if (batch->state.char_id[d_idx] != (uint8_t)MSL_CHAR_ID_MARTH) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[d_idx];
  if (a == (uint16_t)MSL_ACT_MS_SPECIAL_LW_HIT || a == (uint16_t)MSL_ACT_MS_SPECIAL_AIR_LW_HIT) {
    // Same-frame sibling capsules of the intercepted swing: the trigger already flipped this
    // defender into LwHit during this combat pass; consume the remaining contacts too (the
    // descriptor swallowed the whole swing, ftColl_8007B1B8 callback fires once).
    return (batch->state.action_frame[d_idx] <= 0) ? 1u : 0u;
  }
  if (a != (uint16_t)MSL_ACT_MS_SPECIAL_LW && a != (uint16_t)MSL_ACT_MS_SPECIAL_AIR_LW) {
    return 0u;
  }
  return (batch->state.speciallw_counter_window[d_idx] == 2u) ? 1u : 0u;
}

static inline void marth_counter_trigger(MslBatch* batch, size_t a_idx, size_t d_idx, size_t hb_i,
                                         uint16_t attacker_motion_id) {
  const MslCommonParams* c = msl_common_params();
  const MslCharParams* ms_ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  if (c == NULL || ms_ch == NULL) {
    return;
  }
  const uint16_t d_act_pre = batch->state.action_id[d_idx];
  if (d_act_pre == (uint16_t)MSL_ACT_MS_SPECIAL_LW_HIT ||
      d_act_pre == (uint16_t)MSL_ACT_MS_SPECIAL_AIR_LW_HIT) {
    return;  // sibling capsule of the already-intercepted swing
  }
  const uint16_t move_id = staling_move_id_from_state(batch, a_idx);
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, move_id);
  float dmg_f = combat_apply_attacker_smash_release_damage_mul(batch, a_idx,
                                                               batch->state.hitbox_damage[hb_i]);
  if (stale_mult != 1.0f) {
    dmg_f *= stale_mult;
  }
  const int dmg_i = combat_get_env_dmg(dmg_f);
  if (dmg_i <= 0) {
    return;
  }

  // Both sides take standard CalcHitlag from the intercepted HitCapsule, with the MarsAttributes
  // x60 floor only when the current descriptor still owns shield_unk0/1 from the Anim creation path.
  // The ground/air swap helpers recreate the descriptor without restoring shield_unk0/1, so swapped
  // rows such as WWS:860 publish ordinary 6f hitlag while uninterrupted aerial rows such as LDG:3964
  // keep the x60=11 floor.
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{ftMs_SpecialLw_Anim,
  //   ftMs_SpecialAirLw_Anim,ftMs_SpecialLw_80138D38,ftMs_SpecialLw_80138DD0}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_8007B1B8}
  const uint16_t hl_floor = (batch->state.speciallw_counter_hitlag_floor_active[d_idx] != 0u &&
                             ms_ch->speciallw_counter_shield_strength > 0.0f)
                                ? (uint16_t)ms_ch->speciallw_counter_shield_strength
                                : 0u;
  uint16_t a_hl = combat_calc_hitlag_frames(c, dmg_i, attacker_motion_id, 1.0f);
  if (a_hl < hl_floor) {
    a_hl = hl_floor;
  }
  if (a_hl > batch->state.hitlag[a_idx]) {
    batch->state.hitlag[a_idx] = a_hl;
    combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
  }
  uint16_t d_hl = combat_calc_hitlag_frames(c, dmg_i, batch->state.action_id[d_idx], 1.0f);
  if (d_hl < hl_floor) {
    d_hl = hl_floor;
  }
  if (d_hl > batch->state.hitlag[d_idx]) {
    batch->state.hitlag[d_idx] = d_hl;
    combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
  }

  // ftMs_SpecialLw_80139140: stash x19A4 * x5C, face the stored direction, enter LwHit.
  float stash = (float)dmg_i * ms_ch->speciallw_counter_damage_mul;
  if (stash < 0.0f) {
    stash = 0.0f;
  }
  if (stash > 65535.0f) {
    stash = 65535.0f;
  }
  batch->state.speciallw_countered_damage[d_idx] = (uint16_t)stash;
  batch->state.speciallw_counter_window[d_idx] = 0u;
  batch->state.speciallw_counter_hitlag_floor_active[d_idx] = 0u;
  // ftColl_80076CBC writes specialn_facing_dir from the descriptor contact side; the
  // CounterHit callback then copies that stored lane into facing_dir. This matters for cross-up
  // contacts because the hit callback does not recompute facing from the attacker position.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::ftMs_SpecialLw_80139140
  batch->state.specialn_facing_dir1[d_idx] =
      (batch->state.pos_x[d_idx] > batch->state.pos_x[a_idx]) ? (int8_t)-1 : (int8_t)1;
  batch->state.facing_dir1[d_idx] = batch->state.specialn_facing_dir1[d_idx];
  batch->state.facing[d_idx] = (uint8_t)(batch->state.facing_dir1[d_idx] > 0);
  const uint8_t grounded = batch->state.on_ground[d_idx] ? 1u : 0u;
  batch->state.action_id[d_idx] =
      grounded ? (uint16_t)MSL_ACT_MS_SPECIAL_LW_HIT : (uint16_t)MSL_ACT_MS_SPECIAL_AIR_LW_HIT;
  batch->state.animation_index[d_idx] =
      (uint32_t)(grounded ? 324u : 326u);  // marth_special_submotion(370/372)
  msl_anim_timebase_enter(batch, d_idx, 0.0f, 1.0f);
}

static inline void combat_processhit_clear_phantom_damage(MslBatch* batch, size_t idx) {
  batch->state.phantom_damage_pending_x1898[idx] = 0.0f;
  batch->state.phantom_damage_timer_x189c[idx] = 0u;
  batch->state.phantom_damage_source_port[idx] = 0xFFu;
}

static inline uint8_t combat_damageflytop_terminal_phantom_expiry_seed_gap(
    const MslBatch* batch, int bi, int p, size_t idx, float* out_damage, uint8_t* out_source_slot) {
  if (batch == NULL || out_damage == NULL || out_source_slot == NULL || bi < 0 || p < 0) {
    return 0u;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.hitlag[idx] != 0u || batch->state.hitstun[idx] != 3u ||
      batch->state.damage_time_since_hit_x18ac[idx] != 84) {
    return 0u;
  }
  if (!combat_replay_rollout_advanced_past_reseed(batch, bi)) {
    return 0u;
  }
  const MslDamageSourceEpisode ep = msl_damage_source_episode_from_victim(batch, bi, p, idx);
  if (ep.has_source == 0u || ep.source_slot < 0 || ep.source_slot == p) {
    return 0u;
  }
  // Terminal DamageFlyTop phantom-expiry source gap:
  // replay rollout can expose the last ftCo_Damage_OnExitHitlag / ftCo_DamageFly_Coll countdown
  // row with the hidden x1898/x189C phantom damage lane already active in source. DamageFlyTop
  // entry owns `post_hitlag_cb = ftCo_Damage_OnExitHitlag`; the serialized
  // damage_post_hitlag_cb_kind lane is a teacher-forced one-step seed hint and is intentionally
  // not reconstructed on the seed frame. The source evidence that remains visible after rollout is
  // the terminal x18AC/hitstun phase and a live damage source port. Apply the minimum
  // ftColl_8007BE3C phantom damage and stale/combo bookkeeping on that terminal callback only.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007BE3C
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_OnExitHitlag,ftCo_DamageFly_Coll}
  *out_damage = 1.0f;
  *out_source_slot = (uint8_t)ep.source_slot;
  return 1u;
}

static inline uint8_t combat_body_damage_producer_build(const MslBatch* batch, size_t a_idx,
                                                        size_t d_idx, size_t hb_i, int int_dmg,
                                                        uint16_t attacker_attack_id,
                                                        uint16_t attacker_attack_instance,
                                                        uint8_t exclude_attacker_attack_instance,
                                                        MslCombatDamageProduct* out) {
  if (batch == NULL || out == NULL) {
    return 0u;
  }

  // ftColl_80076ED8 producer shape:
  // - HitCapsule.damage already owns the staled float damage lane created through
  //   ftColl_8007ABD0 -> ft_80089228(fp->x2068, fp->x206c).
  // - Fighter_ProcessHit consumes the resulting x1838_percentTemp/x183C_applied lanes later.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007ABD0}
  // refs/melee/src/melee/ft/ft_0881.c::{ft_80089118,ft_80089228}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  float hb_dmg = combat_apply_attacker_smash_release_damage_mul(batch, a_idx,
                                                                batch->state.hitbox_damage[hb_i]);
  const int hitcapsule_int_dmg =
      (batch->state.smash_charge_state[a_idx] == 3u) ? (int)hb_dmg : int_dmg;
  combat_damage_product_init(out, attacker_attack_id, attacker_attack_instance, hitcapsule_int_dmg,
                             hitcapsule_int_dmg);

  float stale_mult = 1.0f;
  float chain_stale_mul = 1.0f;
  const uint16_t attacker_action = batch->state.action_id[a_idx];
  if (batch->state.char_id[a_idx] == (uint8_t)MSL_CHAR_ID_SHEIK &&
      (attacker_action == (uint16_t)MSL_ACT_SK_SPECIAL_S ||
       attacker_action == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S ||
       attacker_action == (uint16_t)MSL_ACT_SK_SPECIAL_S_START ||
       attacker_action == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_START ||
       attacker_action == (uint16_t)MSL_ACT_SK_SPECIAL_S_END ||
       attacker_action == (uint16_t)MSL_ACT_SK_SPECIAL_AIR_S_END) &&
      batch->state.hitstun[d_idx] == 0u && batch->state.hitlag[d_idx] == 0u &&
      sheik_chain_hitbox_stale_damage_mul(batch, a_idx, &chain_stale_mul) != 0u) {
    // Sheik Chain HitCapsule.damage is source-frozen when the Start payload/it_802BCB88
    // publication becomes live and persists across ftSk_SpecialS_80110BCC disable/reactivate
    // windows. Consume the Chain-private article scalar here without publishing a generic
    // stale-valid fighter hitbox lane or generic item stale lane. Same-source Chain re-hits during
    // Damage hitstun stay on the existing contact/hitlist horizon path; they are already inside
    // the prior x914 victims_1 episode and source does not reclassify that admission from this
    // article scalar.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007ABD0
    // refs/melee/src/melee/it/items/itseakchain.c::it_802BCB88
    // refs/melee/src/melee/ft/chara/ftSeak/ftSk_SpecialS.c::ftSk_SpecialS_UpdateHitboxes
    stale_mult = chain_stale_mul;
  } else if (batch->state.hitbox_stale_damage_valid[hb_i] != 0u &&
             batch->state.hitbox_stale_damage_mul[hb_i] > 0.0f) {
    stale_mult = batch->state.hitbox_stale_damage_mul[hb_i];
  } else {
    // Source fallback for runtime-published fighter HitCapsules without a latched stale scalar:
    // ftColl_8007ABD0 passes fp->x206C_attack_instance into ft_80089228, but ft_80089118 only
    // compares move_id while applying stale weights. attack_instance remains the duplicate
    // suppression key for plStale updates, not a damage-scalar exclusion.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007ABD0}
    // refs/melee/src/melee/ft/ft_0881.c::{ft_80089118,ft_80089228}
    (void)attacker_attack_instance;
    (void)exclude_attacker_attack_instance;
    stale_mult = staling_multiplier_for_move(batch, a_idx, out->move_id);
  }
  if (stale_mult != 1.0f) {
    hb_dmg *= stale_mult;
  }
  return combat_damage_product_set_applied(out, hb_dmg);
}

static inline uint8_t combat_item_damage_product_build(const MslBatch* batch, size_t a_idx,
                                                       uint16_t item_attack_id,
                                                       uint16_t item_attack_instance, float damage,
                                                       float stale_mult_override,
                                                       MslCombatDamageProduct* out) {
  if (batch == NULL || out == NULL) {
    return 0u;
  }

  // Item BODY producer shape:
  // - it_80272460 stores raw/base integer damage in HitCapsule.unk_count and staled float damage in
  //   HitCapsule.damage through ft_80089228(owner, item->xD88, item->xD8C, ...).
  // - The reflected-damage multiplier is item-local (`item->xC6C`) and is applied before this
  //   product is built by the item caller when present as `stale_mult_override`.
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  const int dmg_raw_i = (int)damage;
  if (dmg_raw_i <= 0) {
    return 0u;
  }

  combat_damage_product_init(out, item_attack_id, item_attack_instance, dmg_raw_i, dmg_raw_i);
  float dmg_f = damage;
  const float stale_mult = (stale_mult_override >= 0.0f)
                               ? stale_mult_override
                               : staling_multiplier_for_move(batch, a_idx, item_attack_id);
  if (stale_mult != 1.0f) {
    dmg_f *= stale_mult;
  }
  if (!combat_damage_product_set_applied(out, dmg_f)) {
    return 0u;
  }
  out->kb_damage_i = out->env_dmg;
  return 1u;
}

static inline uint8_t combat_throw_damage_product_build(const MslBatch* batch, size_t a_idx,
                                                        const MslThrowHitboxParams* p,
                                                        uint16_t move_id,
                                                        MslCombatDamageProduct* out) {
  if (batch == NULL || p == NULL || out == NULL) {
    return 0u;
  }

  // Throw-release producer shape:
  // - set_throw_hitbox writes HitCapsule.unk_count from the raw script damage and HitCapsule.damage
  //   through ft_80089228 at capsule creation time.
  // - Throw capsules are created before later same-instance throw/item contacts can stale-queue the
  //   attack instance; preserve that creation-time scalar instead of recomputing from the
  //   post-contact seed table.
  // refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071E04
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
  const int dmg_raw_i = (int)p->damage;
  if (dmg_raw_i <= 0) {
    return 0u;
  }

  const uint16_t attack_instance = batch->state.attack_instance[a_idx];
  combat_damage_product_init(out, move_id, attack_instance, dmg_raw_i, dmg_raw_i);
  float dmg_f = p->damage;
  const float stale_mult =
      staling_multiplier_for_move_excluding_instance(batch, a_idx, move_id, attack_instance);
  if (stale_mult != 1.0f) {
    dmg_f *= stale_mult;
  }
  return combat_damage_product_set_applied(out, dmg_f);
}

static inline void combat_body_damage_producer_apply_attacker_side(
    MslBatch* batch, size_t a_idx, const MslCombatDamageProduct* prod,
    uint16_t attacker_motion_id) {
  if (batch == NULL || prod == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Attacker-side hitlag uses the producer's x183C_applied-style env damage, with no victim
  // electric multiplier. Stale/combo bookkeeping is registered immediately by ftColl_8007891C.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007891C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t a_hl = combat_calc_hitlag_frames(c, prod->env_dmg, attacker_motion_id, 1.0f);
  if (!combat_received_kb_hitlag_owns_over_deal_hitlag(batch, a_idx) &&
      a_hl > batch->state.hitlag[a_idx]) {
    batch->state.hitlag[a_idx] = a_hl;
    combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
  }
  // deal_dmg_cb owners fire from the x1914 dealt-damage path once per frame (Fighter_ProcessHit).
  falcon_speciallw_on_deal_dmg_x1914(batch, a_idx);
  puff_rollout_on_deal_dmg(batch, a_idx);
}

static inline void combat_body_damage_log_entry_init(
    MslBatch* batch, MslCombatBodyDamageLogEntry* e, size_t a_idx, size_t d_idx, int attacker,
    int defender, size_t hb_i, size_t cap_i, uint16_t attacker_motion_id,
    uint16_t attacker_attack_id, uint16_t attacker_instance_id, uint8_t hit_group,
    uint8_t rehit_frames, const MslCombatDamageProduct* prod) {
  if (batch == NULL || e == NULL || prod == NULL) {
    return;
  }
  memset(e, 0, sizeof(*e));
  e->a_idx = a_idx;
  e->d_idx = d_idx;
  e->hb_i = hb_i;
  e->cap_i = cap_i;
  e->attacker = attacker;
  e->defender = defender;
  e->hit_group = hit_group;
  e->rehit_frames = rehit_frames;
  e->element = batch->state.hitbox_element[hb_i];
  e->hurt_height = batch->state.hurtcap_height[cap_i];
  e->defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;
  e->attacker_motion_id = attacker_motion_id;
  e->source_motion_id = attacker_motion_id;
  e->defender_motion_id = batch->state.action_id[d_idx];
  if (e->defender_motion_id == (uint16_t)MSL_ACT_FALL &&
      msl_action_is_thrown_victim(batch->state.prev_action_id[d_idx])) {
    e->defender_motion_id = batch->state.prev_action_id[d_idx];
  }
  const uint8_t d_grab_owner = batch->state.grab_owner_port[d_idx];
  e->attached_grabbed_victim =
      (uint8_t)(d_grab_owner != 0xFFu && d_grab_owner == (uint8_t)attacker &&
                msl_action_is_grabbed_victim(e->defender_motion_id));
  e->attacker_attack_id = attacker_attack_id;
  e->attacker_instance_id = attacker_instance_id;
  e->hitbox_angle = batch->state.hitbox_angle[hb_i];
  e->hitbox_kbg = batch->state.hitbox_kbg[hb_i];
  e->hitbox_wsk = batch->state.hitbox_wsk[hb_i];
  e->hitbox_bkb = batch->state.hitbox_bkb[hb_i];
  e->hitcapsule_int_dmg = prod->hitcapsule_int_dmg;
  e->env_dmg = prod->env_dmg;
}

static inline uint8_t combat_body_damage_log_record(
    MslBatch* batch, MslCombatBodyDamageScratch* scratch, size_t a_idx, size_t d_idx, int attacker,
    int defender, size_t hb_i, size_t cap_i, int int_dmg, uint16_t attacker_motion_id,
    uint16_t attacker_attack_id, uint16_t attacker_attack_instance, uint16_t attacker_instance_id,
    uint8_t exclude_attacker_attack_instance, uint8_t hit_group, uint8_t rehit_frames) {
  if (batch == NULL || scratch == NULL ||
      scratch->count >= (uint8_t)MSL_COMBAT_BODY_DAMAGE_LOG_CAP) {
    return 0u;
  }
  if (combat_sheik_chain_terminal_same_source_episode_suppresses_body(batch, a_idx, d_idx)) {
    return 0u;
  }
  const uint8_t chain_high_horizon_suppresses_body =
      combat_sheik_chain_damageflytop_high_horizon_suppresses_body(
          batch, msl_common_params(), a_idx, d_idx, attacker,
          (int)(hb_i % (size_t)MSL_MAX_HITBOXES), int_dmg, batch->state.action_id[d_idx],
          batch->state.hitbox_element[hb_i]);
  if (chain_high_horizon_suppresses_body) {
    return 0u;
  }
  combat_processhit_clear_phantom_damage(batch, d_idx);

  // ftColl_80076ED8 writeback shape:
  // - compute already-staled HitCapsule damage and getEnvDmg,
  // - immediately accumulate victim x1838_percentTemp / x183C_applied,
  // - immediately register stale/combo side effects through ftColl_8007891C,
  // - append one DmgLogEntry for later ftColl_8007A06C best-KB selection.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,inlineB2,ftColl_8007891C,ftColl_8007A06C}
  MslCombatDamageProduct prod;
  if (!combat_body_damage_producer_build(batch, a_idx, d_idx, hb_i, int_dmg, attacker_attack_id,
                                         attacker_attack_instance, exclude_attacker_attack_instance,
                                         &prod)) {
    return 0u;
  }

  batch->state.percent_temp[d_idx] += prod.applied_damage;
  if (prod.env_dmg > scratch->max_env_dmg) {
    scratch->max_env_dmg = prod.env_dmg;
  }

  combat_body_damage_producer_apply_attacker_side(batch, a_idx, &prod, attacker_motion_id);
  staling_queue_update(batch, a_idx, prod.move_id, prod.attack_instance);
  combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, attacker_attack_id);

  MslCombatBodyDamageLogEntry* e = &scratch->entries[scratch->count++];
  combat_body_damage_log_entry_init(batch, e, a_idx, d_idx, attacker, defender, hb_i, cap_i,
                                    attacker_motion_id, attacker_attack_id, attacker_instance_id,
                                    hit_group, rehit_frames, &prod);
  return 1u;
}

static inline void combat_body_damage_log_register_accepted_hitlists(
    MslBatch* batch, int bi, const MslCombatBodyDamageScratch* scratch,
    uint16_t defender_iid_post) {
  if (batch == NULL || scratch == NULL) {
    return;
  }
  for (uint8_t i = 0u; i < scratch->count; i++) {
    const MslCombatBodyDamageLogEntry* e = &scratch->entries[i];
    hitlist_register_fighter_group(batch, bi, e->attacker, e->hit_group, e->defender,
                                   defender_iid_post, (int)MSL_LBCOLL_INSERT_FT_BODY,
                                   e->rehit_frames);
  }
}

static inline void combat_body_damage_log_select_best_kb_entry(
    const MslCommonParams* c, const MslBatch* batch, int bi,
    const MslCombatBodyDamageScratch* scratch, float* out_best_kb, uint8_t* out_best_i) {
  if (out_best_kb != NULL) {
    *out_best_kb = 0.0f;
  }
  if (out_best_i != NULL) {
    *out_best_i = 0u;
  }
  if (c == NULL || batch == NULL || scratch == NULL || out_best_kb == NULL || out_best_i == NULL) {
    return;
  }

  // ftColl_8007A06C selects the highest knockback DmgLogEntry after all ftColl_80076ED8 calls have
  // accumulated x1838_percentTemp. Ties keep the earliest log entry because source updates only on
  // `kb > best_kb`.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
  for (uint8_t i = 0u; i < scratch->count; i++) {
    const MslCombatBodyDamageLogEntry* e = &scratch->entries[i];
    const MslCharParams* d_ch = msl_char_params_fast(batch->state.char_id[e->d_idx]);
    float coll_kb_mul = batch->state.match_damage_ratio[(size_t)bi];
    coll_kb_mul *= batch->state.attack_ratio[e->a_idx];
    coll_kb_mul *= batch->state.defense_ratio[e->d_idx];
    if (!(coll_kb_mul > 0.0f)) {
      coll_kb_mul = 1.0f;
    }
    const float kb = combat_damage_calc_kb_applied(
        c, d_ch, e->defender_motion_id, batch->state.percent[e->d_idx],
        batch->state.percent_temp[e->d_idx], e->hitcapsule_int_dmg, e->hitbox_kbg, e->hitbox_wsk,
        e->hitbox_bkb, coll_kb_mul, batch->state.dmg_x2225_b7[e->d_idx],
        batch->state.dmg_x2224_b2[e->d_idx], batch->state.kb_smashcharge_active[e->d_idx]);
    if (kb > *out_best_kb) {
      *out_best_kb = kb;
      *out_best_i = i;
    }
  }
}

static inline uint8_t combat_body_damage_log_entry_owns_ftcoll_damage_effect_rng(
    const MslCombatBodyDamageLogEntry* e) {
  if (e == NULL) {
    return 0u;
  }
  // ftColl_8007A06C routes normal fighter BODY DmgLog entries through ftColl_80078538 before
  // selecting the final damage result. That helper consumes HSD_Randi for nonzero integer damage
  // when the defender's co_attrs.xA0 damage-effect lane is active; Fox/Falco supported attrs use
  // that source lane, and the spawned effect is visual-only for this sim.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007A06C,ftColl_80078538}
  // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
  return (uint8_t)(e->element == (uint8_t)MSL_HIT_ELEMENT_NORMAL && e->hitcapsule_int_dmg > 0);
}

static inline uint8_t combat_body_damage_log_entry_skips_ftcoll_damage_effect_rng(
    const MslBatch* batch, const MslCombatBodyDamageLogEntry* e) {
  if (batch == NULL || e == NULL) {
    return 0u;
  }
  if (msl_motion_state_fx_special_kind(batch->state.char_id[e->d_idx],
                                       batch->state.action_id[e->d_idx]) !=
          (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI ||
      !combat_source_motion_is_attackairb(e->source_motion_id)) {
    return 0u;
  }
  const size_t hb_base = e->a_idx * (size_t)MSL_MAX_HITBOXES;
  const size_t cap_base = e->d_idx * (size_t)MSL_MAX_HURTCAPS;
  if (e->hb_i < hb_base || e->hb_i >= hb_base + (size_t)MSL_MAX_HITBOXES || e->cap_i < cap_base ||
      e->cap_i >= cap_base + (size_t)MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(e->hb_i - hb_base);
  const uint8_t cap_id = (uint8_t)(e->cap_i - cap_base);
  if (cap_id != (uint8_t)MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT) {
    return 0u;
  }
  if (hb_id != 2u ||
      !combat_attackairb_hitbox_payload_is_authored_weak(
          hb_id, e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 0u;
  }
  // SpecialAirHi / BackAir effect-prefix skip owner:
  // selected current weak BAir hb2 HitCapsules against cap2/head-high can reach ftCo_8008DCE0's
  // DamageFlyRoll gate without the ftColl_80078538 normal-hit visual-effect RNG prefix. Adjacent
  // strong SpecialAirHi cap2 and cap12/XRotN owners keep their ordinary effect prefix.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007A06C,ftColl_80078538}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // data/hurtcaps/{fox,falco}.json cap2
  return 1u;
}

static inline void combat_rng_consume_ftcoll_normal_damage_effect_site(MslBatch* batch, int bi) {
  // Source normal-hit effect phase for ftColl_80078538:
  // - efSync_Spawn(0x3E8) dispatches through efAsync_Dispatch(0x3E8), whose normal-hit variant
  //   consumes HSD_Randi(8) to choose effect id 9 vs 10.
  // - ftColl_80078538 then consumes the damage-effect co_attrs.xA0 lane via HSD_Randi.
  // - The attached effect object's source initialization lives in the same effect path and advances
  //   the shared HSD stream before ftCo_8008DCE0's DamageFlyRoll HSD_Randf gate; its visual result
  //   is outside this sim, but the stream phase is not.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078538
  // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch case 0x3E8
  // refs/melee/src/melee/ef/eflib.c::efLib_Create_Attach_Pos
  // refs/melee/src/sysdolphin/baselib/random.c::{HSD_Randi,HSD_Randf}
  for (uint8_t i = 0u; i < (uint8_t)MSL_FTCOLL_NORMAL_DAMAGE_EFFECT_RANDI_CONSUMES; i++) {
    (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_FTCOLL_DAMAGE_EFFECT, 1);
  }
}

static inline uint8_t combat_body_damage_log_damageflyroll_gate_candidate(
    const MslCommonParams* c, const MslBatch* batch, const MslCombatBodyDamageLogEntry* e,
    float best_kb, float kb_angle_rad) {
  if (c == NULL || batch == NULL || e == NULL) {
    return 0u;
  }
  if (combat_damage_severity_u8_from_kb(c, best_kb) != 3u) {
    return 0u;
  }
  if (kb_angle_rad > c->damagefly_top_angle_min_radians &&
      kb_angle_rad < c->damagefly_top_angle_max_radians) {
    return 0u;
  }
  const float percent_cur = batch->state.percent[e->d_idx] + batch->state.percent_temp[e->d_idx];
  if (percent_cur < (float)c->damagefly_roll_percent_threshold) {
    return 0u;
  }
  if (combat_damageflyroll_dash_weak_attackairb_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_recovering_ground_downattacku_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u)) {
    return 1u;
  }
  if (combat_damageflyroll_specialairhi_attackairb_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u)) {
    return 1u;
  }
  if (combat_damageflyroll_jump_hitlag_strong_attackairn_tiplog_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_landingairlw_strong_attackairn_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_attackhi4_weak_attackairb_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb,
          e->defender_on_ground)) {
    return 1u;
  }
  if (combat_damageflyroll_specialhifall_late_attackairn_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_catch_late_attackairn_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_kneebend_attacks3_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_wait_attacklw3_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb,
          e->defender_on_ground, batch->state.action_id[e->d_idx])) {
    return 1u;
  }
  if (combat_damageflyroll_fallspecial_attackairf_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_jump_late_attackhi4_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->hitcapsule_int_dmg,
          e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_jump_strong_attackairn_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_sustained_jump_late_attackairn_leg_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_specialairn_attackairlw_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_recovery_action_attackairlw_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb,
          e->defender_on_ground)) {
    return 1u;
  }
  if (combat_damageflyroll_catch_attackairf_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->defender_on_ground,
          e->source_motion_id, e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg,
          e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_kneebend_weak_attackairb_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (combat_damageflyroll_damageflytop_attackairb_hb0_cap1_effect_prefix_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->source_motion_id,
          e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)) {
    return 1u;
  }
  if (batch->state.action_id[e->d_idx] == (uint16_t)MSL_ACT_RUN) {
    const size_t hb_base = e->a_idx * (size_t)MSL_MAX_HITBOXES;
    if (e->hb_i >= hb_base && e->hb_i < hb_base + (size_t)MSL_MAX_HITBOXES) {
      const uint8_t hb_id = (uint8_t)(e->hb_i - hb_base);
      const size_t bi = e->d_idx / (size_t)MSL_MAX_PLAYERS;
      if (batch->replay_frame_rng_applied != NULL && batch->replay_frame_rng_applied[bi] != 0u &&
          combat_source_motion_is_attackairb(e->source_motion_id) &&
          hb_id <= (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX && e->hitcapsule_int_dmg == 15) {
        return 1u;
      }
    }
  }
  if (e->defender_on_ground != 0u) {
    return 0u;
  }
  // The normal-hit effect stream phase is currently source-proven only for the SpecialAirHi
  // strong-DAir owner. AttackAirN delayed segments already carry their explicit Fighter_8006CDA4
  // stream phase; broadening this visual-effect consume into AttackAirN shifts TBK's full-rollout
  // RNG phase before its otherwise aligned DamageFlyRoll gate.
  if (msl_motion_state_fx_special_kind(batch->state.char_id[e->d_idx],
                                       batch->state.action_id[e->d_idx]) ==
          (uint8_t)MSL_FX_KIND_SPECIAL_AIR_HI &&
      combat_damageflyroll_attackairn_specialairhi_strong_attackairlw_hitcapsule_owner(
          batch, e->d_idx, e->a_idx, e->attacker, e->hb_i, 1u)) {
    return 1u;
  }
  const uint16_t defender_action = batch->state.action_id[e->d_idx];
  uint8_t aerial_effect_source =
      (uint8_t)(combat_source_motion_is_attackairlw(e->source_motion_id) &&
                defender_action != (uint16_t)MSL_ACT_DAMAGE_FLY_N);
  const size_t hb_base = e->a_idx * (size_t)MSL_MAX_HITBOXES;
  if (e->hb_i >= hb_base && e->hb_i < hb_base + (size_t)MSL_MAX_HITBOXES) {
    const uint8_t hb_id = (uint8_t)(e->hb_i - hb_base);
    const uint8_t bair_effect_callback_phase =
        (uint8_t)(defender_action == (uint16_t)MSL_ACT_RUN ||
                  defender_action == (uint16_t)MSL_ACT_LANDING_AIR_LW);
    if (bair_effect_callback_phase != 0u &&
        combat_source_motion_is_attackairb(e->source_motion_id) &&
        ((hb_id <= (uint8_t)MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX &&
          e->hitcapsule_int_dmg == 15) ||
         combat_attackairb_hitbox_payload_is_authored_weak(
             hb_id, e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb))) {
      aerial_effect_source = 1u;
    }
  }
  if (aerial_effect_source != 0u) {
    const size_t bi = e->d_idx / (size_t)MSL_MAX_PLAYERS;
    if (batch->state.fighter_8006cda4_pre_gate_consume_count[e->d_idx] != 0u) {
      return 0u;
    }
    if (batch->replay_frame_rng_applied != NULL && batch->replay_frame_rng_applied[bi] != 0u &&
        (combat_damageflyroll_rng_subset_allows_pre_action(batch, e->d_idx, defender_action) ||
         defender_action == (uint16_t)MSL_ACT_RUN)) {
      return 1u;
    }
  }
  return 0u;
}

static inline void combat_body_damage_log_consume_ftcoll_effect_rng_before_damageflyroll(
    const MslCommonParams* c, MslBatch* batch, int bi, const MslCombatBodyDamageScratch* scratch,
    const MslCombatBodyDamageLogEntry* best_e, float best_kb, float kb_angle_rad) {
  if (batch == NULL || scratch == NULL ||
      !combat_body_damage_log_damageflyroll_gate_candidate(c, batch, best_e, best_kb,
                                                           kb_angle_rad)) {
    return;
  }
  for (uint8_t i = 0u; i < scratch->count; i++) {
    if (combat_body_damage_log_entry_owns_ftcoll_damage_effect_rng(&scratch->entries[i]) &&
        !combat_body_damage_log_entry_skips_ftcoll_damage_effect_rng(batch, &scratch->entries[i])) {
      combat_rng_consume_ftcoll_normal_damage_effect_site(batch, bi);
    }
  }
  if (best_e != NULL && combat_damageflyroll_dash_weak_attackairb_hitcapsule_owner(
                            batch, best_e->d_idx, best_e->a_idx, best_e->attacker, best_e->hb_i, 1u,
                            best_e->source_motion_id, best_e->hitcapsule_int_dmg,
                            best_e->hitbox_angle, best_e->hitbox_kbg, best_e->hitbox_bkb)) {
    batch->state.fighter_8006cda4_pre_gate_consume_count[best_e->d_idx] = 1u;
  }
}

static inline void combat_body_damage_log_materialize_damageflyroll_zero_consume_marker(
    MslBatch* batch, const MslCombatBodyDamageLogEntry* best_e) {
  if (batch == NULL || best_e == NULL) {
    return;
  }
  if (combat_damageflyroll_attacklw4_strong_attackairn_zero_marker_owner(
          batch, best_e->d_idx, best_e->a_idx, best_e->attacker, best_e->hb_i, 1u, best_e->cap_i,
          1u, best_e->source_motion_id, best_e->hitcapsule_int_dmg, best_e->hitbox_angle,
          best_e->hitbox_kbg, best_e->hitbox_bkb)) {
    batch->state.fighter_8006cda4_pre_gate_consume_count[best_e->d_idx] = 4u;
  }
  if (combat_damageflyroll_attackhi4_weak_attackairb_hitcapsule_owner(
          batch, best_e->d_idx, best_e->a_idx, best_e->attacker, best_e->hb_i, 1u, best_e->cap_i,
          1u, best_e->source_motion_id, best_e->hitcapsule_int_dmg, best_e->hitbox_angle,
          best_e->hitbox_kbg, best_e->hitbox_bkb, best_e->defender_on_ground)) {
    batch->state.fighter_8006cda4_pre_gate_consume_count[best_e->d_idx] =
        (uint8_t)MSL_DAMAGEFLYROLL_ATTACKHI4_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_PRIMARY_CONSUMES;
  }
}

static inline uint8_t combat_attached_throw_body_hit_suppresses_victim_hitlag(
    const MslBatch* batch, const MslCombatBodyDamageLogEntry* e) {
  if (batch == NULL || e == NULL || e->attached_grabbed_victim == 0u ||
      !msl_action_is_throw_owner(e->attacker_motion_id)) {
    return 0u;
  }
  if (e->hitbox_kbg != 0u || e->hitbox_bkb != 0u) {
    return 0u;
  }
  float release_af = 0.0f;
  if (move_tables_throw_release_frame(batch->state.char_id[e->a_idx], e->attacker_motion_id,
                                      &release_af) == 0u) {
    return 0u;
  }
  const float attacker_anim_frame =
      msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[e->a_idx]);
  return attacker_anim_frame < release_af ? 1u : 0u;
}

static inline uint8_t combat_attached_falcon_dive_catch_hit_suppresses_victim_hitlag(
    const MslBatch* batch, const MslCombatBodyDamageLogEntry* e) {
  // Falcon Dive hold: the SpecialHiCatch scripted BODY pulse damages the held CaptureCaptain
  // victim without starting the victim's own x195C hitlag (Slippi victim hitlag stays 0 across
  // the connect window). The victim's freeze instead rides the OWNER's hitlag through the
  // recursive x2219_b5 propagation over the fp->x1A5C held-victim link (modeled by the
  // CaptureCaptain owner-slaved gate in anim_timebase.c).
  // refs/melee/src/melee/ft/fighter.c::{Fighter_UnkRecursiveFunc_8006D044,Fighter_8006D10C}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCaptain.c::ftCo_8009CA0C
  if (batch == NULL || e == NULL || e->attached_grabbed_victim == 0u) {
    return 0u;
  }
  return (batch->state.char_id[e->a_idx] == (uint8_t)MSL_CHAR_ID_FALCON &&
          e->attacker_motion_id == (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH &&
          batch->state.action_id[e->d_idx] == (uint16_t)MSL_ACT_CAPTURE_CAPTAIN)
             ? 1u
             : 0u;
}

static inline uint8_t combat_attached_throw_body_pose_gap_admits_pre_release_contact(
    const MslBatch* batch, size_t a_idx, size_t d_idx, int attacker, size_t hb_i) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t attacker_motion_id = batch->state.action_id[a_idx];
  if (!msl_action_is_throw_owner(attacker_motion_id)) {
    return 0u;
  }
  const uint16_t defender_motion_id =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_FALL &&
       msl_action_is_thrown_victim(batch->state.prev_action_id[d_idx]))
          ? batch->state.prev_action_id[d_idx]
          : batch->state.action_id[d_idx];
  if (batch->state.grab_owner_port[d_idx] != (uint8_t)attacker ||
      !msl_action_is_grabbed_victim(defender_motion_id)) {
    return 0u;
  }
  if (batch->state.hitbox_kbg[hb_i] != 0u || batch->state.hitbox_bkb[hb_i] != 0u) {
    return 0u;
  }
  float release_af = 0.0f;
  if (move_tables_throw_release_frame(batch->state.char_id[a_idx], attacker_motion_id,
                                      &release_af) == 0u) {
    return 0u;
  }
  const float attacker_anim_frame = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[a_idx]);
  if (!(attacker_anim_frame < release_af)) {
    return 0u;
  }
  const uint8_t hb_id = (uint8_t)(hb_i % (size_t)MSL_MAX_HITBOXES);
  const int int_dmg = combat_get_env_dmg(batch->state.hitbox_damage[hb_i]);
  if (!move_tables_throw_pre_release_create_hitbox_payload_matches(
          batch->state.char_id[a_idx], attacker_motion_id, hb_id, int_dmg,
          batch->state.hitbox_angle[hb_i], batch->state.hitbox_kbg[hb_i],
          batch->state.hitbox_bkb[hb_i], attacker_anim_frame)) {
    return 0u;
  }
  // Source throw BODY pulses are ordinary HitCapsule contacts before ftCo_800DD724 consumes the
  // later set_throw_flags release. Attached Thrown* victims are attachment-driven
  // (ftCo_800DE508); if replay-derived hurt capsules expose the held pose one frame away from the
  // source collision JObj chain, still admit the authored zero-direct-KB pre-release throw BODY
  // pulse so the existing attached-victim damage class owns percent/bookkeeping without releasing
  // the victim. CatchAttack has an authored only_hit_grabbed bit; throw BODY hitboxes do not, so
  // this path binds to the concrete pre-release throw create_hitbox payload instead.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // data/scripts/<char>.bin (MSLFTSC1 create_hitbox before set_throw_flags for Throw*)
  return 1u;
}

static inline void combat_body_damage_log_apply(MslBatch* batch, int bi,
                                                MslCombatBodyDamageScratch* scratch) {
  if (batch == NULL || scratch == NULL || scratch->count == 0u) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  float best_kb = 0.0f;
  uint8_t best_i = 0u;
  combat_body_damage_log_select_best_kb_entry(c, batch, bi, scratch, &best_kb, &best_i);

  const MslCombatBodyDamageLogEntry* e = &scratch->entries[best_i];
  const size_t d_idx = e->d_idx;
  const size_t a_idx = e->a_idx;
  const float d_hitlag_mul = combat_hitlag_mul_from_element(c, e->element);
  const uint16_t d_hl =
      combat_calc_hitlag_frames(c, scratch->max_env_dmg, e->defender_motion_id, d_hitlag_mul);
  const uint16_t d_hl_prev = batch->state.hitlag[d_idx];
  const uint8_t d_hl_increased = (d_hl > d_hl_prev) ? 1u : 0u;

  MslCombatDamageApplyClass body_damage_class =
      e->attached_grabbed_victim ? MSL_COMBAT_DAMAGE_ATTACHED_SUPPRESSED : MSL_COMBAT_DAMAGE_FULL;
  if (body_damage_class == MSL_COMBAT_DAMAGE_ATTACHED_SUPPRESSED) {
    const uint8_t pre_release_throw_body_hit =
        (uint8_t)(combat_attached_throw_body_hit_suppresses_victim_hitlag(batch, e) ||
                  combat_attached_falcon_dive_catch_hit_suppresses_victim_hitlag(batch, e));
    if (d_hl_increased && pre_release_throw_body_hit == 0u) {
      batch->state.hitlag[d_idx] = d_hl;
      combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
      combat_state_flags_set_x221a_b3(batch, d_idx);
      combat_state_flags_set_x221c_b0(batch, d_idx);
    } else {
      // Pre-release attached throw BODY damage can take the no-reaction damage path without
      // starting victim hitlag. Source still exposes fp->x221C_b0 for that accepted percent pulse;
      // only the hitlag/x221A side effects above are gated on victim hitlag actually increasing.
      //
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007891C}
      combat_state_flags_set_x221c_b0(batch, d_idx);
    }
    // Throw-state BODY hitboxes can strike the still-attached Thrown* victim before
    // set_throw_flags(0) releases them. For zero-direct-kb pre-release throw BODY pulses, source
    // keeps the victim attachment state and publishes damage/bookkeeping without starting victim
    // hitlag; direct-kb pre-release throw hitboxes keep the normal attached victim hitlag path.
    // The thrower-side hitlag/bookkeeping was already registered by ftColl_8007891C when the damage
    // log was recorded above.
    // data/scripts/<char>.bin (MSLFTSC1 set_throw_flags/create_hitbox for Throw*)
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE508,ftCo_ThrownLw_Phys,ftCo_ThrownLw_Coll}
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007891C}
    batch->state.instance_hit_by[d_idx] = e->attacker_instance_id;
    const uint16_t defender_iid_post = batch->state.instance_id[d_idx];
    combat_body_damage_log_register_accepted_hitlists(batch, bi, scratch, defender_iid_post);
    return;
  }

  if (best_kb == 0.0f) {
    body_damage_class = MSL_COMBAT_DAMAGE_PERCENT_ONLY_NO_ENTRY;
  }
  if (body_damage_class == MSL_COMBAT_DAMAGE_PERCENT_ONLY_NO_ENTRY) {
    MslCombatProcessHitResolved ev = {0};
    ev.bi = bi;
    ev.attacker = e->attacker;
    ev.defender = e->defender;
    ev.a_idx = a_idx;
    ev.d_idx = d_idx;
    ev.source_hb_i = e->hb_i;
    ev.source_hb_valid = 1u;
    ev.source_cap_i = e->cap_i;
    ev.source_cap_valid = 1u;
    ev.source_hitcapsule_int_dmg = e->hitcapsule_int_dmg;
    ev.source_hitbox_angle = e->hitbox_angle;
    ev.source_hitbox_kbg = e->hitbox_kbg;
    ev.source_hitbox_bkb = e->hitbox_bkb;
    ev.d_motion_id = e->defender_motion_id;
    ev.source_motion_id = e->source_motion_id;
    ev.d_hl = d_hl;
    ev.d_hl_prev = d_hl_prev;
    ev.kb_applied = 0.0f;
    ev.instance_hit_by = e->attacker_instance_id;
    ev.last_hit_by = combat_source_port0_for_attacker(batch, a_idx, e->attacker);
    ev.source_write = MSL_PROCESS_HIT_SOURCE_WRITE_COMMIT_OWNER;
    combat_processhit_apply_resolved_damage(c, batch, &ev);
    const uint16_t defender_iid_post = batch->state.instance_id[d_idx];
    combat_body_damage_log_register_accepted_hitlists(batch, bi, scratch, defender_iid_post);
    return;
  }

  const uint16_t pre_damage_action = batch->state.action_id[d_idx];
  const uint8_t downed_damage_contact_facing_owner =
      (uint8_t)(combat_is_downed_damage_contact_action(pre_damage_action) &&
                (batch->state.dmg_x2224_b2[d_idx] ||
                 batch->state.percent_temp[d_idx] < (float)c->down_damage_percent_threshold));
  const float kb_angle_rad =
      combat_damage_calc_angle_radians(c, e->hitbox_angle, e->defender_on_ground, best_kb);
  combat_body_damage_log_consume_ftcoll_effect_rng_before_damageflyroll(c, batch, bi, scratch, e,
                                                                        best_kb, kb_angle_rad);
  combat_body_damage_log_materialize_damageflyroll_zero_consume_marker(batch, e);
  float kb_vel_mag = best_kb * c->kb_vel_mul;
  if (!e->defender_on_ground && combat_damage_check_air_motion_kb_mul(c, batch, d_idx)) {
    kb_vel_mag *= c->air_motion_kb_mul;
  }
  const float x = kb_vel_mag * cosf(kb_angle_rad);
  const float y = kb_vel_mag * sinf(kb_angle_rad);
  const float one = combat_damage_ftColl_804D82EC_one();
  const float collision_facing_dir_1 =
      (batch->state.pos_x[d_idx] > batch->state.pos_x[a_idx]) ? -one : one;
  const float defender_facing_dir_1 = collision_facing_dir_1;
  const float post_damage_facing_dir = downed_damage_contact_facing_owner
                                           ? (batch->state.facing[d_idx] ? one : -one)
                                           : collision_facing_dir_1;
  batch->state.facing[d_idx] = (uint8_t)(post_damage_facing_dir > 0.0f);

  const float kb_x = -x * defender_facing_dir_1;
  const float kb_y = y;
  MslCombatProcessHitResolved ev = {0};
  ev.bi = bi;
  ev.attacker = e->attacker;
  ev.defender = e->defender;
  ev.a_idx = a_idx;
  ev.d_idx = d_idx;
  ev.source_hb_i = e->hb_i;
  ev.source_hb_valid = 1u;
  ev.source_cap_i = e->cap_i;
  ev.source_cap_valid = 1u;
  ev.source_hitcapsule_int_dmg = e->hitcapsule_int_dmg;
  ev.source_hitbox_angle = e->hitbox_angle;
  ev.source_hitbox_kbg = e->hitbox_kbg;
  ev.source_hitbox_bkb = e->hitbox_bkb;
  ev.d_motion_id = e->defender_motion_id;
  ev.source_motion_id = e->source_motion_id;
  ev.d_hl = d_hl;
  ev.d_hl_prev = d_hl_prev;
  ev.kb_applied = best_kb;
  ev.kb_angle_rad = kb_angle_rad;
  ev.kb_x = kb_x;
  ev.kb_y = kb_y;
  // Downed-contact damage can be logged after this sim's map pass refreshed `on_ground`, but
  // source `ftCo_8009F0F0 -> ftCo_8009F184 -> ftCo_8008DCE0` calls ftCo_8008DCE0 with an explicit
  // motion-state id, which forces the internal damage severity selector (`var_r28`) to tumble even
  // when the hitstun scalar is low. Preserve the collision target's grounded angle/floor-normal
  // path, then let that forced-tumble branch launch/clear ground_or_air if the KB points into the
  // floor.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::{
  //   ftCo_8009F0F0,ftCo_8009F184,ftCo_DownDamage_Coll}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  ev.defender_on_ground = e->defender_on_ground;
  ev.use_grounded_kb = 1u;
  ev.force_tumble_severity = downed_damage_contact_facing_owner;
  ev.grounded_ecb_lock_owner =
      (!combat_is_downed_damage_contact_action(pre_damage_action) &&
       combat_shine_start_grounded_ledge_ecb_lock_owner(batch, d_idx, a_idx,
                                                        batch->state.action_id[a_idx]))
          ? 1u
      : combat_ground_to_air_ecb_lock_late_attackhi4_hitcapsule_owner(
            batch, d_idx, a_idx, e->attacker, e->hb_i, 1u, e->cap_i, 1u, e->source_motion_id,
            e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb,
            e->defender_on_ground, pre_damage_action)
          ? 1u
          : 0u;
  ev.clear_x221c_on_damage_entry = 1u;
  ev.hurt_height = combat_hurt_height_damageflytop_weak_attackairb_head_high_uses_medium(
                       batch, d_idx, a_idx, e->hb_i, e->cap_i, e->source_motion_id,
                       e->hitcapsule_int_dmg, e->hitbox_angle, e->hitbox_kbg, e->hitbox_bkb)
                       ? 1u
                       : e->hurt_height;
  ev.damage_state_raw_angle = e->hitbox_angle;
  ev.instance_hit_by = e->attacker_instance_id;
  ev.last_hit_by = combat_source_port0_for_attacker(batch, a_idx, e->attacker);
  ev.source_write = MSL_PROCESS_HIT_SOURCE_WRITE_DIRECT;
  ev.hitlag_mode = MSL_PROCESS_HITLAG_ASSIGN_IF_POSITIVE;
  ev.hitlag_sets_x221a = 1u;
  ev.hitlag_allows_sdi = 1u;
  combat_processhit_apply_resolved_damage(c, batch, &ev);

  const uint16_t defender_iid_post = batch->state.instance_id[d_idx];
  combat_body_damage_log_register_accepted_hitlists(batch, bi, scratch, defender_iid_post);
}

static inline void combat_mutations_pass1_future_apply_body_phantom_hit(MslBatch* batch,
                                                                        size_t a_idx, size_t d_idx,
                                                                        int attacker, float dmg_f,
                                                                        uint8_t element) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Fighter phantom-hit lane:
  // - ft_80089228 stores stale-scaled HitCapsule.damage before collision.
  // - ftColl_80076ED8 halves that float damage and stores it into fp->dmg.x1840.
  // - Fighter_ProcessHit consumes x1840 through the x18a0 branch, starting victim hitlag without
  //   percent/KB/damage-state entry and without attacker hitlag.
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t move_id = staling_move_id_from_state(batch, a_idx);
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, move_id);
  float phantom_dmg = 0.5f * dmg_f * stale_mult;
  if (!((int)phantom_dmg) && dmg_f > 0.0f) {
    phantom_dmg = 1.0f;
  }
  const int phantom_dmg_i = combat_get_env_dmg(phantom_dmg);
  if (phantom_dmg_i <= 0) {
    return;
  }

  const uint16_t d_motion_id = batch->state.action_id[d_idx];
  const float d_hitlag_mul = combat_hitlag_mul_from_element(c, element);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, phantom_dmg_i, d_motion_id, d_hitlag_mul);
  if (d_hl > batch->state.hitlag[d_idx]) {
    batch->state.hitlag[d_idx] = d_hl;
    combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
    if (combat_damage_allow_sdi_owner_action(batch->state.action_id[d_idx])) {
      combat_damage_allow_sdi_set(batch, d_idx);
    }
  }

  batch->state.phantom_damage_pending_x1898[d_idx] = phantom_dmg;
  batch->state.phantom_damage_timer_x189c[d_idx] = d_hl;
  batch->state.phantom_damage_source_port[d_idx] =
      (attacker >= 0 && attacker < (int)batch->config.num_players) ? (uint8_t)attacker : 0xFFu;
  batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
  msl_damage_source_write_direct(batch, d_idx,
                                 combat_source_port0_for_attacker(batch, a_idx, attacker));
}

void combat_apply_item_phantom_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                   uint16_t item_attack_id, uint16_t item_instance_id, float damage,
                                   uint8_t element) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (batch_index < 0 || batch_index >= batch->batch_size || attacker < 0 ||
      attacker >= num_players || defender < 0 || defender >= num_players || attacker == defender) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const size_t a_idx = msl_idx_player(batch_index, attacker);
  const size_t d_idx = msl_idx_player(batch_index, defender);

  // Item phantom-hit lane:
  // - Fighter BODY phantom hits route through `checkTipLog` when HitCapsule.coll_distance is below
  //   p_ftCommonData->x7A8; Fighter_ProcessHit then consumes x1840 as victim hitlag without
  //   percent/KB/damage-state entry.
  // - Item hitboxes are still normalized by it_80272460 before the same hitlag calculation.
  // refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80076ED8}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  float dmg_f = damage;
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, item_attack_id);
  if (stale_mult != 1.0f) {
    dmg_f *= stale_mult;
  }
  float phantom_dmg = 0.5f * dmg_f;
  if (!((int)phantom_dmg) && dmg_f > 0.0f) {
    phantom_dmg = 1.0f;
  }
  const int phantom_dmg_i = combat_get_env_dmg(phantom_dmg);
  if (phantom_dmg_i <= 0) {
    return;
  }
  const float d_hitlag_mul = combat_hitlag_mul_from_element(c, element);
  const uint16_t d_hl =
      combat_calc_hitlag_frames(c, phantom_dmg_i, batch->state.action_id[d_idx], d_hitlag_mul);
  if (d_hl > batch->state.hitlag[d_idx]) {
    batch->state.hitlag[d_idx] = d_hl;
    combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
    if (combat_damage_allow_sdi_owner_action(batch->state.action_id[d_idx])) {
      combat_damage_allow_sdi_set(batch, d_idx);
    }
  }
  batch->state.instance_hit_by[d_idx] = item_instance_id;
  msl_damage_source_write_direct(batch, d_idx,
                                 combat_source_port0_for_attacker(batch, a_idx, attacker));
}

void combat_apply_item_phantom_attribution(MslBatch* batch, int batch_index, int attacker,
                                           int defender, uint16_t item_instance_id) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (batch_index < 0 || batch_index >= batch->batch_size || attacker < 0 ||
      attacker >= num_players || defender < 0 || defender >= num_players || attacker == defender) {
    return;
  }
  const size_t a_idx = msl_idx_player(batch_index, attacker);
  const size_t d_idx = msl_idx_player(batch_index, defender);
  batch->state.instance_hit_by[d_idx] = item_instance_id;
  msl_damage_source_write_direct(batch, d_idx,
                                 combat_source_port0_for_attacker(batch, a_idx, attacker));
}

MslItemHitResult combat_apply_item_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                       uint16_t item_attack_id, uint16_t item_attack_instance,
                                       uint16_t item_instance_id, uint16_t item_type,
                                       uint8_t item_state, float damage, uint16_t angle,
                                       uint16_t kbg, uint16_t wsk, uint16_t bkb,
                                       uint8_t defender_hurt_height, uint8_t element,
                                       float stale_mult_override, float item_pos_x,
                                       float item_pos_y, float item_hit_radius, float item_vel_x,
                                       uint8_t item_damage_facing_owner_valid) {
  if (batch == NULL) {
    return MSL_ITEM_HIT_NONE;
  }
  const int num_players = (int)batch->config.num_players;
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return MSL_ITEM_HIT_NONE;
  }
  if (attacker < 0 || attacker >= num_players || defender < 0 || defender >= num_players ||
      attacker == defender) {
    return MSL_ITEM_HIT_NONE;
  }

  const size_t a_idx = msl_idx_player(batch_index, attacker);
  const size_t d_idx = msl_idx_player(batch_index, defender);
  const uint8_t prev_last_hit_by = batch->state.last_hit_by[d_idx];

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return MSL_ITEM_HIT_NONE;
  }

  const uint8_t item_is_illusion = item_article_params_is_illusion_item_type(item_type);

  // Decomp (GALE01): item-vs-fighter BODY apply stores both:
  // - `HitCapsule.unk_count` (raw/base integer lane from it_80272460),
  // - `HitCapsule.damage` (staled/adjusted float lane).
  //
  // For ProcessHit ownership, `fp->dmg.x183C_applied` (hitlag/KB input lane) is sourced from
  // getEnvDmg(HitCapsule.damage), i.e. the staled float converted to int.
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,inlineB2}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //
  // The item apply path receives the base hitbox damage (integer-valued for Fox/Falco blasters);
  // `damage_product` keeps that raw HitCapsule lane beside the staled float ProcessHit lane.
  MslCombatDamageProduct damage_product;
  if (!combat_item_damage_product_build(batch, a_idx, item_attack_id, item_attack_instance, damage,
                                        stale_mult_override, &damage_product)) {
    return MSL_ITEM_HIT_NONE;
  }

  const uint16_t d_motion_id = batch->state.action_id[d_idx];
  const MslItemArticleParams* sheik_ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (sheik_ap != NULL && item_type == sheik_ap->sheik_vanish_itkind &&
      batch->state.char_id[a_idx] == (uint8_t)MSL_CHAR_ID_SHEIK &&
      item_instance_id == batch->state.instance_id[a_idx] &&
      batch->state.hitlag_pre_timer[d_idx] != 0u && batch->state.hitstun[d_idx] != 0u &&
      combat_is_damage_or_firefox_launch_victim_action(batch->state.char_id[d_idx], d_motion_id) &&
      msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker) &&
      batch->state.instance_hit_by[d_idx] == item_instance_id) {
    // Sheik Vanish smoke hitlag-tail victims_1 carry:
    // itSeakvanish publishes one item HitCapsule through it_802B1D40 -> it_8027518C. After that
    // HitCapsule has already damaged a fighter, lbColl_80008688 records the victim in the item
    // capsule's victims_1 list; Fighter_8006A360 then freezes the victim while hitlag is active.
    // Replay rows do not serialize the smoke item's hidden victims_1 ring, but the last-hitlag-tick
    // Damage victim still exposes the accepted source through x18C4/x18EC (`last_hit_by` and
    // `instance_hit_by`). Reject only that same-source Vanish smoke BODY repeat before percent-temp
    // accumulation; fresh Vanish smoke contacts and non-Vanish projectiles stay on the normal item
    // BODY path.
    // refs/melee/src/melee/it/items/itseakvanish.c::it_802B1D40
    // refs/melee/src/melee/it/itcoll.c::it_80272460
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    return MSL_ITEM_HIT_NONE;
  }

  // Marth Counter intercepts item/projectile contacts through the same descriptor used for
  // fighter BODY contacts: the AbsorbDesc is a ShieldDesc-family intercept, and item collision
  // consults the defender's shield_hit descriptor exactly like fighter collision does. The
  // projectile is consumed (vanilla: countering destroys the incoming article) and the
  // counterattack damage stays script-authored.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80077688}
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::ftMs_SpecialLw_80139140
  // Descriptor geometry requires the caller's real projectile position/radius; callers that
  // cannot provide them pass a negative radius and Counter FAILS CLOSED for that contact.
  if (item_hit_radius >= 0.0f && isfinite(item_pos_y) &&
      marth_counter_intercepts_contact(batch, d_idx) &&
      marth_counter_desc_overlaps_point(batch, d_idx, item_pos_x, item_pos_y, item_hit_radius)) {
    const int dmg_i = combat_get_env_dmg(damage_product.applied_damage);
    if (dmg_i > 0) {
      const MslCharParams* ms_ch = msl_char_params_fast(batch->state.char_id[d_idx]);
      uint16_t d_hl = combat_calc_hitlag_frames(c, dmg_i, d_motion_id, 1.0f);
      if (ms_ch != NULL && batch->state.speciallw_counter_hitlag_floor_active[d_idx] != 0u &&
          ms_ch->speciallw_counter_shield_strength > 0.0f &&
          d_hl < (uint16_t)ms_ch->speciallw_counter_shield_strength) {
        // shield_unk0 hitlag floor (see the fighter-contact intercept above).
        d_hl = (uint16_t)ms_ch->speciallw_counter_shield_strength;
      }
      if (d_hl > batch->state.hitlag[d_idx]) {
        batch->state.hitlag[d_idx] = d_hl;
        combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
      }
      float stash = (ms_ch != NULL) ? (float)dmg_i * ms_ch->speciallw_counter_damage_mul : 0.0f;
      if (stash < 0.0f) {
        stash = 0.0f;
      }
      if (stash > 65535.0f) {
        stash = 65535.0f;
      }
      batch->state.speciallw_countered_damage[d_idx] = (uint16_t)stash;
      batch->state.speciallw_counter_window[d_idx] = 0u;
      batch->state.speciallw_counter_hitlag_floor_active[d_idx] = 0u;
      // Item/projectile Counter follows the same source lane: ftColl_80077688 writes
      // specialn_facing_dir from item position, and ftMs_SpecialLw_80139140 copies it.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
      // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::ftMs_SpecialLw_80139140
      batch->state.specialn_facing_dir1[d_idx] =
          (batch->state.pos_x[d_idx] > item_pos_x) ? (int8_t)-1 : (int8_t)1;
      batch->state.facing_dir1[d_idx] = batch->state.specialn_facing_dir1[d_idx];
      batch->state.facing[d_idx] = (uint8_t)(batch->state.facing_dir1[d_idx] > 0);
      const uint8_t grounded = batch->state.on_ground[d_idx] ? 1u : 0u;
      batch->state.action_id[d_idx] =
          grounded ? (uint16_t)MSL_ACT_MS_SPECIAL_LW_HIT : (uint16_t)MSL_ACT_MS_SPECIAL_AIR_LW_HIT;
      batch->state.animation_index[d_idx] = (uint32_t)(grounded ? 324u : 326u);
      msl_anim_timebase_enter(batch, d_idx, 0.0f, 1.0f);
      return MSL_ITEM_HIT_APPLIED_CONSUME_ITEM;
    }
  }

  // Compatibility/seed pending-release lane:
  // normal runtime ThrowLw release damage runs in throw_flow_update_anim_callback_pre_input(), but
  // one-step/reseed rows can still expose a pending detached release victim when an item BODY hit is
  // applied from the seed snapshot.
  const uint8_t throw_release_pending =
      (batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_LW &&
       batch->state.throw_pending_victim_port[a_idx] == (uint8_t)defender &&
       batch->state.throw_pending_hit_idx[a_idx] != 0xFFu)
          ? 1u
          : 0u;

  const uint8_t d_grab_owner = batch->state.grab_owner_port[d_idx];
  const uint8_t d_is_attached_grabbed_victim =
      (!throw_release_pending && d_grab_owner != 0xFFu && d_grab_owner == (uint8_t)attacker &&
       msl_action_is_grabbed_victim(d_motion_id))
          ? 1u
          : 0u;

  if (d_is_attached_grabbed_victim) {
    // Damage scalar (ftCommonData.x128) applied by ftColl under certain victim/attacker gobj
    // relationships. In the grabbed-victim + item-hit edge case, the defender is attached to a
    // fighter gobj while the collision attacker is the item gobj, so this multiplier applies.
    // refs/melee/src/melee/ft/ftcoll.c::inlineB3
    // refs/melee/src/melee/ft/types.h (ftCommonData +0x128)
    //
    // Data contract: `data/common/ft_common_data.json` -> `ftcoll_damage_mul_x128`.
    // Suite anchor: required for bitwise-f32 percent parity on the attached-victim laser records
    // (e.g. QuerulousGrandDinosaur.msl records 448/8116) where ref shows a 0.91 damage increment.
    if (!combat_damage_product_set_applied(
            &damage_product, damage_product.applied_damage * c->ftcoll_damage_mul_x128)) {
      return MSL_ITEM_HIT_NONE;
    }
  }

  // Percent-temp accumulation (BODY): fp->dmg.x1838_percentTemp.
  const float percent_pre = batch->state.percent[d_idx];
  batch->state.percent_temp[d_idx] += damage_product.applied_damage;
  const float dmg_temp = batch->state.percent_temp[d_idx];

  // Special-case: zero-KB laser hits.
  //
  // Scope gate: only apply this rule to item kinds that are known (via ISO-extracted MSLLASR1
  // lasers.bin) to be Fox/Falco blaster shots.
  const MslLaserParams* lp = laser_params_for_item_type(item_type);
  const uint8_t zero_kb_damage_class_hit =
      (lp != NULL &&
       ((item_state == 0u) ? lp->zero_kb_damage_class : lp->state1_zero_kb_damage_class) != 0u)
          ? 1u
          : 0u;
  const uint8_t item_damage_owns_motion_clear =
      (item_article_params_for_sheik_needle_throw_item_type(item_type) != NULL) ? 1u : 0u;
  MslCombatDamageApplyClass item_damage_class =
      zero_kb_damage_class_hit ? MSL_COMBAT_DAMAGE_PERCENT_ONLY_NO_ENTRY : MSL_COMBAT_DAMAGE_FULL;
  if (item_damage_class == MSL_COMBAT_DAMAGE_PERCENT_ONLY_NO_ENTRY) {
    // Source owner:
    // - item hitbox scripts write kbg/wsk/bkb into HitCapsule.x24/x28/x2C (it_2725.c);
    // - ftColl_80077C60 writes item BODY percent-temp/applied damage;
    // - Fighter_ProcessHit_8006D1EC enters Damage* only when applied KB is nonzero.
    // MSLLASR1 zero_kb_damage_class marks the supported Fox/Falco laser states whose source
    // HitCapsule KB tuple is all zero, so these contacts consume percent without hitlag/hitstun
    // or a fresh Damage* entry. Falco laser states carry nonzero KB terms and use normal BODY.
    batch->state.instance_hit_by[d_idx] = item_instance_id;
    combat_processhit_commit_source_owner(batch, d_idx,
                                          combat_source_port0_for_attacker(batch, a_idx, attacker));
    // Zero-KB damage still routes through Fighter_ProcessHit's percent-temp consume without a
    // fresh Damage* entry. Keep fp->x221C_b0 aligned to the same hidden-damage ownership so the
    // post-frame no-reaction lane does not stale-carry after the item hit is accepted.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
    combat_state_flags_clear_x221c_b0(batch, d_idx);

    // Stale-move queue update on successful damaging BODY hit (attacker-side).
    // Decomp: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    staling_queue_update(batch, a_idx, damage_product.move_id, damage_product.attack_instance);

    // Combo count + last-attack tracking (attacker-side).
    // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, damage_product.move_id);
    return MSL_ITEM_HIT_APPLIED_CONSUME_ITEM;
  }

  const float hitlag_mul = combat_hitlag_mul_from_element(c, element);
  const uint16_t d_hl =
      combat_calc_hitlag_frames(c, damage_product.env_dmg, d_motion_id, hitlag_mul);
  const uint16_t d_hl_prev = batch->state.hitlag[d_idx];
  const MslItemArticleParams* sheik_needle_article =
      item_article_params_for_sheik_needle_throw_item_type(item_type);
  const uint8_t same_frame_throw_laser_item_damage_topoff =
      (lp != NULL && item_state == (uint8_t)1u &&
       (batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_B ||
        batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_HI) &&
       batch->state.hitlag_pre_timer[d_idx] == 0u && d_hl_prev > 0u &&
       batch->state.hitstun[d_idx] > 0u &&
       batch->state.instance_hit_by[d_idx] == item_instance_id &&
       batch->state.last_hit_by[d_idx] == combat_source_port0_for_attacker(batch, a_idx, attacker))
          ? 1u
          : 0u;
  const uint8_t sheik_needle_damage_hitlag_topoff =
      (sheik_needle_article != NULL && item_type == sheik_needle_article->needle_throw_itkind &&
       item_state == 0u && d_hl_prev > 1u && batch->state.hitstun[d_idx] > 0u &&
       d_motion_id == (uint16_t)MSL_ACT_DAMAGE_N_2)
          ? 1u
          : 0u;
  if (sheik_needle_damage_hitlag_topoff != 0u) {
    if (d_hl > batch->state.hitlag[d_idx]) {
      batch->state.hitlag[d_idx] = d_hl;
      combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
    }
  } else if (d_hl != d_hl_prev) {
    batch->state.hitlag[d_idx] = d_hl;
    combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
  }

  if (item_state == (uint8_t)1u && batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_LW &&
      batch->state.throw_pending_victim_port[a_idx] == (uint8_t)defender &&
      batch->state.throw_pending_hit_idx[a_idx] != 0xFFu &&
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_FALL ||
       (batch->state.grab_owner_port[d_idx] == (uint8_t)attacker &&
        msl_action_is_grabbed_victim(batch->state.action_id[d_idx])))) {
    // ThrowLw compatibility pending release + same-frame blaster ordering bridge:
    // - ftCo_800DD724 consumes set_throw_flags(0) in ThrowLw Anim and applies the throw release hit
    //   via ftCo_800DE2A8/ftCo_800DDDE4 before later frame contacts.
    // - On replay-real one-step/reseed rows the pending release victim can still be visible as
    //   attached `Thrown*` at the item-collision snapshot even though the common release event
    //   already belongs to this frame.
    // - Apply the pending release owner before the generic attached-victim suppression path so the
    //   later throw-side laser stacks onto the released victim instead of suppressing the release.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_ThrowLw_Anim,ftCo_800DD724,ftCo_800DE2A8,ftCo_800DDDE4}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    MslThrowHitboxParams throw_p = {0};
    if (move_tables_throw_hitbox_params(batch->state.char_id[a_idx], (uint16_t)MSL_ACT_THROW_LW,
                                        batch->state.throw_pending_hit_idx[a_idx], &throw_p) &&
        combat_apply_throw_hit_core(batch, batch_index, attacker, defender, &throw_p, 0u)) {
      combat_throw_release_integrate_position_now(batch, a_idx, d_idx);
    }
  }

  // Grabbed/thrown victims are driven by an attachment joint and have empty Phys/Coll callbacks in
  // decomp; when a projectile owned by the grabber/thrower hits the attached victim, Slippi
  // commonly shows percent+hitlag but no forced Damage* entry on the victim (hitstun remains 0 and
  // action_id stays in Thrown*/Capture* while the attachment is active).
  //
  // Decomp anchors (GALE01):
  // - Thrown victim pos driver: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // - Thrown* Phys/Coll are empty (attachment-driven loop):
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_ThrownF_Phys,ftCo_ThrownF_Coll}
  // - Grab-owner identity is tracked via fp->x1064_thrownHitbox.owner and is set/cleared by:
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B8CC and ::ftColl_8007B8E8
  // - Thrower release sequencing (context for when the attachment is removed):
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  //
  // Simulator policy (seed-minimal, replay-parity):
  // - If the defender is a grabbed/thrown victim attached to the attacker (grab_owner_port),
  //   apply percent/hitlag attribution but do not enter Damage* (no hitstun/KB state change).
  if (d_is_attached_grabbed_victim) {
    item_damage_class = MSL_COMBAT_DAMAGE_ATTACHED_SUPPRESSED;
  }
  if (item_damage_class == MSL_COMBAT_DAMAGE_ATTACHED_SUPPRESSED) {
    // Attached item-hit pulses apply percent/hitlag while the victim stays in Thrown*/Capture*.
    // Because this path suppresses Damage* entry, stale Damage no-reaction state (`x221C_b0`)
    // must not carry into the frozen attached window. Low-throw laser pulse rows with the bit
    // seeded set (PRH) and seeded clear (QGD) both observe the post-hitlag row clear. Clear this
    // for the accepted attached item-hit path as a whole: hitlag may already have been established
    // by an earlier same-frame pulse before this branch sees the final max hitlag value.
    //
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
    // refs/melee/src/melee/it/itcoll.c::{it_8026FAC4,it_80272460}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
    combat_state_flags_clear_x221c_b0(batch, d_idx);

    // Victim-side x221A_b3 can be set alongside hitlag start even without Damage* entry.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    if (d_hl > d_hl_prev) {
      combat_state_flags_set_x221a_b3(batch, d_idx);
    }

    // Apply hitlag to the grab owner as well (victim is attached to the grabber during the
    // grabbed/thrown window, and the suite observes grabber hitlag on these attached hits).
    //
    // NOTE: for non-attached projectile hits, Melee applies hitlag to the item object, not the
    // owning fighter. This path is intentionally scoped to (attacker == grab_owner_port).
    const uint16_t a_motion_id = batch->state.action_id[a_idx];
    // Decomp ordering for hitlag `dmg` input uses the getEnvDmg(int) derived from the *applied*
    // staled float damage (see the ftColl damage-log BODY producer for the fighter-vs-fighter
    // equivalent). For attached projectile hits, suite refs observe grab-owner hitlag=3 when
    // victim hitlag=4 for Falco lasers (damage=3 => d_hl=4; getEnvDmg(staled_damage)=1 => a_hl=3).
    //
    // Victim-side hitlag_mul is driven by fp->x1960_vibrateMult (electric hits), but attacker-side
    // hitlag in this case is observed to *not* apply the electric multiplier.
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s (fp->x1960_vibrateMult set site)
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (passes vibrateMult into CalcHitlag)
    const uint16_t a_hl = combat_calc_hitlag_frames(c, damage_product.env_dmg, a_motion_id, 1.0f);
    if (a_hl > batch->state.hitlag[a_idx]) {
      batch->state.hitlag[a_idx] = a_hl;
      combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
    }

    // Attached item-hit attribution:
    // - Even when the victim stays in Thrown*/Capture* (no Damage* entry), the accepted item hit
    //   still owns the victim-side item instance lane in post-frame data.
    // - The source-owner lane (`last_hit_by`) does not take the fresh attacker port on these
    //   suppressed attached rows; replay keeps the prior owner/sentinel while only
    //   `instance_hit_by` advances to the live item instance.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
    batch->state.instance_hit_by[d_idx] = item_instance_id;
    msl_damage_source_write_direct(batch, d_idx, prev_last_hit_by);

    // Attached item-hit bookkeeping:
    // - Throw-side item hits on an attached victim still feed the attacker-side item-domain stale
    //   move queue and combo lanes even when victim state entry stays in Thrown*/Capture*.
    // - Keep the non-Damage* victim suppression above, but preserve ftColl_8007646C ownership for
    //   last_attack_landed/combo_count in the item attack-id domain.
    // refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
    staling_queue_update(batch, a_idx, damage_product.move_id, damage_product.attack_instance);
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, damage_product.move_id);

    return MSL_ITEM_HIT_SUPPRESSED_DONT_CONSUME;
  }

  // Knockback velocity + hitstun + damage-state entry (BODY), following the same helper chain as
  // fighter-vs-fighter hits.
  //
  // Decomp chain:
  // - Fighter_ProcessHit_8006D1EC consumes kb_applied computed by collision and enters damage
  //   states via ftCo_8008DCE0.
  // refs/melee/src/melee/ft/fighter.c and refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c
  const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;
  const MslCharParams* d_ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  if (d_ch == NULL) {
    return MSL_ITEM_HIT_NONE;
  }

  const float kb_applied = combat_damage_calc_kb_applied(
      c, d_ch, d_motion_id, percent_pre, dmg_temp, damage_product.kb_damage_i, kbg, wsk, bkb, 1.0f,
      batch->state.dmg_x2225_b7[d_idx], batch->state.dmg_x2224_b2[d_idx],
      batch->state.kb_smashcharge_active[d_idx]);
  const uint16_t hs = combat_damage_hitstun_from_kb(c, kb_applied);
  const float kb_angle_rad =
      combat_damage_calc_angle_radians(c, angle, defender_on_ground, kb_applied);
  // Item-article grounded launch severity owner:
  // it_80272460 feeds Fox/Falco laser and side-special article HitCapsules into
  // Fighter_ProcessHit. When grounded KB ownership launches the victim before terminal selection,
  // ftCo_8008DCE0 uses the high-hitstun tumble branch for the same item source; otherwise the sim
  // can match hitstun/velocity but fall back to DamageFlyN before the DamageFlyRoll RNG gate. Keep
  // this on extracted item kinds and the decomp severity threshold, not on replay row or fighter
  // action.
  // refs/melee/src/melee/it/itcoll.c::it_80272460
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // data/items/lasers.bin (MSLLASR1)
  // data/items/articles/fox_falco.bin (MSLITAR1)
  const uint8_t item_article_high_hitstun_tumble_owner =
      (uint8_t)((lp != NULL || item_is_illusion != 0u) && item_state <= 1u &&
                defender_on_ground != 0u && hs >= (uint16_t)c->damage_severity_x160);

  // Decomp: Fighter_ProcessHit only enters common Damage* state entry when `fp->dmg.kb_applied`
  // is nonzero. Otherwise the percent-only path consumes Fighter_UnkTakeDamage and grounded
  // ftCommon_800804FC source-clear ownership without hitstun or Damage* entry.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  if (kb_applied == 0.0f) {
    MslCombatProcessHitResolved ev = {0};
    ev.bi = batch_index;
    ev.attacker = attacker;
    ev.defender = defender;
    ev.a_idx = a_idx;
    ev.d_idx = d_idx;
    ev.d_motion_id = d_motion_id;
    ev.source_item_type = item_type;
    ev.source_item_state = item_state;
    ev.source_is_item_hit = 1u;
    ev.d_hl = d_hl;
    ev.d_hl_prev = d_hl_prev;
    ev.kb_applied = 0.0f;
    ev.instance_hit_by = item_instance_id;
    ev.last_hit_by = combat_source_port0_for_attacker(batch, a_idx, attacker);
    ev.source_write = MSL_PROCESS_HIT_SOURCE_WRITE_COMMIT_OWNER;
    ev.update_bookkeeping = 1u;
    ev.source_item_owns_motion_clear = item_damage_owns_motion_clear;
    ev.stale_move_id = damage_product.move_id;
    ev.stale_attack_instance = damage_product.attack_instance;
    ev.combo_attack_id = damage_product.move_id;
    combat_processhit_apply_resolved_damage(c, batch, &ev);
    if (item_is_illusion) {
      return MSL_ITEM_HIT_APPLIED_DONT_CONSUME;
    }
    return MSL_ITEM_HIT_APPLIED_CONSUME_ITEM;
  }

  float kb_vel_mag = kb_applied * c->kb_vel_mul;
  if (!defender_on_ground && combat_damage_check_air_motion_kb_mul(c, batch, d_idx)) {
    kb_vel_mag *= c->air_motion_kb_mul;
  }

  // Horizontal sign for item-hit knockback:
  // - ftColl_8007A06C item damage uses item position for slow/stationary items and item velocity
  //   sign once abs(x40_vel.x) reaches ItemCommonData->x78. ftCo_8008DCE0 then sets facing from
  //   that sign before applying `-x * facing_dir_1`.
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
  //   refs/melee/src/melee/it/types.h::ItemCommonData::x78_float
  //   data/items/item_common.json::item_damage_facing_velocity_threshold
  // - ThrowHi throw-side blaster shots are spawned by ftFx_Throw_Anim via it_8029C6CC in the
  //   state1 projectile lane, and their active BODY overlap is driven forward along the scripted
  //   shot velocity rather than the thrower root transform.
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  //   refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
  const float one = combat_damage_ftColl_804D82EC_one();
  float defender_facing_dir_1 =
      (batch->state.pos_x[d_idx] > batch->state.pos_x[a_idx]) ? -one : one;
  if (item_damage_facing_owner_valid != 0u &&
      !(same_frame_throw_laser_item_damage_topoff != 0u &&
        batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_HI)) {
    // Retained item-position facing owner scope:
    // ftColl_8007A06C's item branch uses item position for stationary/slow item damage and item
    // velocity once the item reaches ItemCommonData->x78. Callers only set this flag for live item
    // BODY collision records whose item pos/velocity are the collision owner's values. ThrowHi
    // state1 blaster articles still enter through this live item BODY owner once the shot exists;
    // same-frame top-off rows that merge a second laser into the current Damage entry keep the
    // narrower thrower-facing owner below.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
    // refs/melee/src/melee/it/itcoll.c (case 2 item damage direction)
    // refs/melee/src/melee/it/types.h::ItemCommonData::x78_float
    const MslItemCommonParams* item_common = msl_item_common_params();
    const float item_facing_vel_threshold =
        (item_common != NULL) ? item_common->item_damage_facing_velocity_threshold : 0.0f;
    float item_vel_abs_x = item_vel_x;
    if (item_vel_abs_x < 0.0f) {
      item_vel_abs_x = -item_vel_abs_x;
    }
    if (item_vel_abs_x < item_facing_vel_threshold) {
      defender_facing_dir_1 = (batch->state.pos_x[d_idx] > item_pos_x) ? -one : one;
    } else {
      defender_facing_dir_1 = (item_vel_x < 0.0f) ? one : -one;
    }
  }
  if (lp != NULL && item_state == (uint8_t)1u &&
      batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_LW &&
      batch->state.throw_pending_victim_port[a_idx] == (uint8_t)defender &&
      batch->state.throw_pending_hit_idx[a_idx] != 0xFFu) {
    // ThrowLw compatibility pending release + same-frame blaster ordering:
    // - ftCo_800DD724 / ftCo_800DDDE4 have already installed the released victim's facing lane
    //   before the later throw-side laser overlap is processed.
    // - Keep the late pulse on that already-owned left/right sign instead of recomputing from the
    //   fighter-vs-fighter X ordering used by ordinary item BODY hits.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
    defender_facing_dir_1 = batch->state.facing[d_idx] ? one : -one;
  }
  // Keep ThrowHi's synthetic thrower-facing bridge scoped to same-frame top-off rows whose second
  // state1 laser merges into the current Damage entry instead of owning a fresh item-damage
  // direction. Full live item BODY rows continue through the item pos/velocity owner above.
  if (lp != NULL && item_state == (uint8_t)1u &&
      batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_HI &&
      same_frame_throw_laser_item_damage_topoff != 0u) {
    const float thrower_facing_dir = batch->state.facing[a_idx] ? one : -one;
    defender_facing_dir_1 = -thrower_facing_dir;
  }
  batch->state.facing[d_idx] = (uint8_t)(defender_facing_dir_1 > 0.0f);

  const float kb_x = -defender_facing_dir_1 * (kb_vel_mag * cosf(kb_angle_rad));
  const float kb_y = kb_vel_mag * sinf(kb_angle_rad);

  if (same_frame_throw_laser_item_damage_topoff && d_hl <= d_hl_prev &&
      hs <= batch->state.hitstun[d_idx]) {
    item_damage_class = MSL_COMBAT_DAMAGE_TOP_OFF_MERGE;
  }
  if (sheik_needle_damage_hitlag_topoff != 0u) {
    item_damage_class = MSL_COMBAT_DAMAGE_TOP_OFF_MERGE;
  }
  if (item_damage_class == MSL_COMBAT_DAMAGE_TOP_OFF_MERGE) {
    // Same-damage-window item top-off:
    // - Multiple throw-side state1 articles can overlap the same already-damaged victim in one
    //   item pass. In vanilla, their HitCapsule damage contributes to the same
    //   Fighter_ProcessHit percent-temp frame, but the first accepted hit owns the Damage entry and
    //   x2088 motion-state instance.
    // - Flying Sheik Needles can likewise BODY-hit a defender whose current Damage* hitlag packet
    //   is still live in the same middle Damage reaction. Fighter_8006A360 freezes the victim
    //   animation/JObj packet during hitlag, and Fighter_8006CB94 still evaluates item BODY
    //   contact; while that middle Damage packet still has multiple hitlag ticks remaining, the
    //   later Needle contributes its already-frozen HitCapsule.damage and article callback without
    //   replacing the existing Damage* action owner. Hi/Lw reactions and one-tick edge packets are
    //   not suppressed here: source can still replace them with the new hit's Damage owner.
    // - The later article can still contribute to ftCo_Damage_CalcVel. The simulator processes
    //   items serially, so without this boundary it sees x18AC reset by the first entry and
    //   incorrectly treats the later top-off as a fresh replace + Damage entry.
    // - ThrowB and ThrowHi share the same ftFx_Throw_Anim throw_flags_b0 item producer and state1
    //   BODY callback; ThrowLw attached-victim pulses use the separate grabbed-victim owner above.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_8006CB94}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_CalcVel,ftCo_8008DCE0}
    // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgDealt
    if (sheik_needle_damage_hitlag_topoff != 0u) {
      // Hitlag-window Needles still run ftCo_Damage_CalcVel on the existing Damage packet. When
      // fp->dmg.x18AC is inside p_ftCommonData->xFC, source replaces KB velocity even if the later
      // same-sign vector is smaller; only the action/hitlag owner is suppressed here.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
      //   ftCo_Damage_CalcVel,ftCo_8008DCE0}
      combat_damage_calc_vel(batch, d_idx, kb_x, kb_y);
    } else {
      combat_damage_merge_vel_after_window(batch, d_idx, kb_x, kb_y);
    }
    staling_queue_update(batch, a_idx, damage_product.move_id, damage_product.attack_instance);
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, damage_product.move_id);
    if (item_is_illusion) {
      return MSL_ITEM_HIT_APPLIED_DONT_CONSUME;
    }
    return MSL_ITEM_HIT_APPLIED_CONSUME_ITEM;
  }

  // Item BODY hits route through Fighter_ProcessHit the same way as fighter BODY hits, so grounded
  // victims use the same ftCo_8008DCE0 ground-vs-air KB install owner.
  const uint16_t meteor_cancel_raw_angle =
      combat_item_damage_meteor_cancel_raw_angle(batch, a_idx, item_is_illusion, angle);
  MslCombatProcessHitResolved ev = {0};
  ev.bi = batch_index;
  ev.attacker = attacker;
  ev.defender = defender;
  ev.a_idx = a_idx;
  ev.d_idx = d_idx;
  ev.d_motion_id = d_motion_id;
  ev.source_item_type = item_type;
  ev.source_item_state = item_state;
  ev.source_is_item_hit = 1u;
  ev.source_item_owns_motion_clear = item_damage_owns_motion_clear;
  ev.d_hl = d_hl;
  ev.d_hl_prev = d_hl_prev;
  ev.kb_applied = kb_applied;
  ev.kb_angle_rad = kb_angle_rad;
  ev.kb_x = kb_x;
  ev.kb_y = kb_y;
  ev.defender_on_ground = defender_on_ground;
  ev.use_grounded_kb = 1u;
  ev.force_tumble_severity = item_article_high_hitstun_tumble_owner;
  ev.grounded_ecb_lock_owner = (!combat_is_downed_damage_contact_action(d_motion_id) &&
                                combat_shine_start_grounded_ledge_ecb_lock_owner(
                                    batch, d_idx, a_idx, batch->state.action_id[a_idx]))
                                   ? 1u
                                   : 0u;
  ev.hurt_height = defender_hurt_height;
  ev.damage_state_raw_angle = meteor_cancel_raw_angle;
  ev.instance_hit_by = item_instance_id;
  ev.last_hit_by = combat_source_port0_for_attacker(batch, a_idx, attacker);
  ev.source_write = MSL_PROCESS_HIT_SOURCE_WRITE_DIRECT;
  ev.hitlag_mode = MSL_PROCESS_HITLAG_FLAGS_IF_INCREASED;
  ev.hitlag_sets_x221a = 1u;
  ev.hitlag_allows_sdi = 1u;
  ev.apply_guard_reflect_followup = 1u;
  combat_processhit_apply_resolved_damage(c, batch, &ev);
  if (lp != NULL && item_state == (uint8_t)1u &&
      batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_LW &&
      msl_action_is_thrown_victim(batch->state.seed_prev_action_id[d_idx]) &&
      defender_on_ground == 0u && d_hl > d_hl_prev && batch->state.ecb_lock_timer[d_idx] != 0u) {
    // Same-frame ThrowLw laser re-entry owner:
    // ftCo_800DDDE4 may call ftCommon_8007D5D4 during release, but the later item BODY hit routes
    // through Fighter_ProcessHit -> ftCo_8008DCE0 from an already-airborne Damage/Fall state. That
    // airborne damage branch does not install a fresh CollData_X130 lock for the item hit; clear the
    // stale release lock so the following DamageAir_Coll map callback consumes the live damage ECB.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    // refs/melee/src/melee/mp/mpcoll.c::{mpColl_LoadECB_inline,mpCollInterpolateECB}
    batch->state.ecb_lock_timer[d_idx] = 0u;
    batch->state.coll_desired_ecb_bottom_locked_owner[d_idx] = 0u;
  }

  // Stale-move queue update on successful damaging BODY hit (attacker-side).
  // Decomp: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
  staling_queue_update(batch, a_idx, damage_product.move_id, damage_product.attack_instance);

  // ThrowLw compatibility release-frame combo-victim continuation:
  // - Live Throw Anim release detaches/damages the victim before later item BODY contacts.
  // - Seed/reseed pending latches can expose that same released-victim relationship through
  //   throw_pending_victim_port; when fp->x2094 was not seed-visible but the pending released
  //   victim matches this item hit, preserve ftColl_800763C0 continuation ownership instead of
  //   restarting combo_count at 1.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_8007646C}
  if (batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_LW &&
      batch->state.throw_pending_victim_port[a_idx] == (uint8_t)defender &&
      batch->state.combo_victim_port[a_idx] == 0xFFu &&
      damage_product.move_id != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
      batch->state.attack_id[a_idx] == damage_product.move_id &&
      batch->state.combo_count[a_idx] != 0u &&
      batch->state.last_attack_landed[a_idx] == (uint8_t)damage_product.move_id) {
    batch->state.combo_victim_port[a_idx] = (uint8_t)defender;
    batch->state.combo_victim_instance_id[a_idx] = batch->state.instance_id[d_idx];
  }

  // Combo count + last-attack tracking (attacker-side).
  // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C -> ftColl_800763C0(item attack id domain).
  combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, damage_product.move_id);

  // Illusion/Phantasm body hits do not destroy the article on hit; itFoxIllusion_Logic14_DmgDealt
  // clears an item var and returns false so the article persists through the hitlag window.
  // refs/melee/src/melee/it/items/itfoxillusion.c::itFoxIllusion_Logic14_DmgDealt
  if (item_is_illusion) {
    return MSL_ITEM_HIT_APPLIED_DONT_CONSUME;
  }
  return MSL_ITEM_HIT_APPLIED_CONSUME_ITEM;
}

static inline uint8_t combat_apply_throw_hit_core(MslBatch* batch, int batch_index, int attacker,
                                                  int defender, const MslThrowHitboxParams* p,
                                                  uint8_t update_bookkeeping) {
  if (batch == NULL || p == NULL) {
    return 0;
  }
  const int num_players = (int)batch->config.num_players;
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return 0;
  }
  if (attacker < 0 || attacker >= num_players || defender < 0 || defender >= num_players ||
      attacker == defender) {
    return 0;
  }

  const size_t a_idx = msl_idx_player(batch_index, attacker);
  const size_t d_idx = msl_idx_player(batch_index, defender);

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return 0;
  }

  // Hit status / hurtbox-state eligibility gate (movescript-derived; opcode 26 + Slippi passthrough).
  //
  // Decomp pointers (GALE01):
  // - set_throw_flags triggers throw hit application (ftCo_800DD724), which then gates the throw
  //   damage float on ftColl_8007B868(victim):
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // - Intangible blocks hurtcapsule collision checks entirely:
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868 (guards the hurtcapsule loop on `x1988 != 2 && x198C != 2`)
  // - Invincible still allows a "contact" that can contribute to attacker hitlag, but suppresses
  //   defender damage/KB/state entry:
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
  const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
  uint8_t hurt_state = batch->state.hurtbox_state[d_idx];
  if (hit_status > hurt_state) {
    hurt_state = hit_status;
  }
  if (hurt_state == 2u) {
    return 0;
  }
  const uint8_t defender_no_damage = (hurt_state != 0u) ? 1u : 0u;

  // Throw-hit damage ownership:
  // - set_throw_hitbox writes HitCapsule.damage through ft_80089228(fp->x2068, fp->x206c, raw_damage).
  // - On ThrowLw release rows in this family, the live attack-id lane can already be the
  //   side-special article item-domain id, so use that seeded/runtime identity when present instead
  //   of forcing a state-based ThrowLw move id.
  // refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071E04
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
  uint16_t move_id = batch->state.attack_id[a_idx];
  if (move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    move_id = staling_move_id_from_state(batch, a_idx);
  }

  MslCombatDamageProduct damage_product;
  if (!combat_throw_damage_product_build(batch, a_idx, p, move_id, &damage_product)) {
    return 0;
  }

  const MslCombatDamageApplyClass throw_damage_class =
      defender_no_damage ? MSL_COMBAT_DAMAGE_REJECTED : MSL_COMBAT_DAMAGE_FULL;
  if (throw_damage_class == MSL_COMBAT_DAMAGE_REJECTED) {
    return 0;
  }

  // Percent-temp accumulation (BODY): fp->dmg.x1838_percentTemp.
  const float percent_pre = batch->state.percent[d_idx];
  batch->state.percent_temp[d_idx] += damage_product.applied_damage;
  const float dmg_temp = batch->state.percent_temp[d_idx];

  const uint16_t d_motion_id = batch->state.action_id[d_idx];

  const MslCharParams* d_ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  if (d_ch == NULL) {
    return 0;
  }
  MslCharParams d_ch_throw = *d_ch;
  // Decomp throw-release KB path uses p_ftCommonData->x10C as the ftColl_80079AB0 "weight"
  // argument (instead of victim co_attrs.weight), then routes into ftCo_Damage_CalcKnockback.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
  if (c->throw_kb_weight_x10c > 0.0f) {
    d_ch_throw.weight = c->throw_kb_weight_x10c;
  }

  // Throw hits are treated as airborne damage entry (victim is detached from the throw joint).
  // Decomp: throw release clears grounded state via ftCommon_8007D5D4 on the thrown fighter.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  combat_apply_ftCommon_8007D5D4_ground_to_air(batch, d_idx);
  const uint8_t defender_on_ground = 0u;

  // Knockback magnitude (ftColl_80079AB0) + damage angle (ftCo_Damage_CalcAngle).
  const size_t bi = a_idx / (size_t)MSL_MAX_PLAYERS;
  float coll_kb_mul = batch->state.match_damage_ratio[bi];
  coll_kb_mul *= batch->state.attack_ratio[a_idx];
  coll_kb_mul *= batch->state.defense_ratio[d_idx];
  if (!(coll_kb_mul > 0.0f)) {
    coll_kb_mul = 1.0f;
  }

  const float kb_applied = combat_damage_calc_kb_applied(
      c, &d_ch_throw, d_motion_id, percent_pre, dmg_temp, damage_product.kb_damage_i, p->kbg,
      p->wsk, p->bkb, coll_kb_mul, batch->state.dmg_x2225_b7[d_idx],
      batch->state.dmg_x2224_b2[d_idx], batch->state.kb_smashcharge_active[d_idx]);
  const float kb_angle_rad =
      combat_damage_calc_angle_radians(c, p->angle, defender_on_ground, kb_applied);
  float damage_state_angle_rad = kb_angle_rad;
  uint8_t low_throw_damage_state_arg_owner = 0u;
  {
    uint16_t throw_action = batch->state.action_id[a_idx];
    if (throw_action != (uint16_t)MSL_ACT_THROW_F && throw_action != (uint16_t)MSL_ACT_THROW_B &&
        throw_action != (uint16_t)MSL_ACT_THROW_HI && throw_action != (uint16_t)MSL_ACT_THROW_LW) {
      throw_action = batch->state.prev_action_id[a_idx];
    }
    if (throw_action == (uint16_t)MSL_ACT_THROW_LW) {
      // Thrown damage-state entry owner for low throw:
      // - ftCo_800DDDE4 computes damage / knockback from the throw hitbox, then ftCo_800DE7C0
      //   enters damage with `calcKnockbackAngle(true)`, i.e. 90 degrees, for ThrowLw.
      // - Keep velocity/KB magnitude from the hitbox-owned throw data, but use the decomp-backed
      //   low-throw damage-entry angle for action-state selection.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4,ftCo_800DE7C0}
      damage_state_angle_rad = 0.5f * MSL_PI_F;
      low_throw_damage_state_arg_owner = 1u;
    }
  }

  if (kb_applied == 0.0f) {
    MslCombatProcessHitResolved ev = {0};
    ev.bi = batch_index;
    ev.attacker = attacker;
    ev.defender = defender;
    ev.a_idx = a_idx;
    ev.d_idx = d_idx;
    ev.d_motion_id = d_motion_id;
    ev.kb_applied = 0.0f;
    ev.instance_hit_by = batch->state.instance_id[a_idx];
    ev.last_hit_by = combat_source_port0_for_attacker(batch, a_idx, attacker);
    ev.source_write = MSL_PROCESS_HIT_SOURCE_WRITE_DIRECT;
    ev.update_bookkeeping = update_bookkeeping;
    ev.stale_move_id = damage_product.move_id;
    ev.stale_attack_instance = damage_product.attack_instance;
    ev.combo_attack_id = batch->state.attack_id[a_idx];
    combat_processhit_apply_resolved_damage(c, batch, &ev);
    return 1;
  }

  float kb_vel_mag = kb_applied * c->kb_vel_mul;
  if (!defender_on_ground && combat_damage_check_air_motion_kb_mul(c, batch, d_idx)) {
    kb_vel_mag *= c->air_motion_kb_mul;
  }

  const float x = kb_vel_mag * cosf(kb_angle_rad);
  const float y = kb_vel_mag * sinf(kb_angle_rad);

  // Horizontal sign for throw KB uses the thrower's facing, not relative X position.
  //
  // Decomp:
  // - ftCo_800DDDE4 sets fp2->dmg.facing_dir_1 = -(fp->facing_dir) for the thrown fighter.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // - ftCo_8008DCE0 applies `kb_x = -x * fp->facing_dir` after setting facing_dir from
  //   fp->dmg.facing_dir_1.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  const float one = combat_damage_ftColl_804D82EC_one();
  const float thrower_facing_dir = batch->state.facing[a_idx] ? one : -one;
  const float defender_facing_dir_1 = -thrower_facing_dir;
  batch->state.facing[d_idx] = (uint8_t)(defender_facing_dir_1 > 0.0f);
  const float kb_x = -x * defender_facing_dir_1;
  const float kb_y = y;
  if (p->angle > 90u && p->angle < 270u) {
    // Throw-release final facing override:
    // - ftCo_800DDDE4 writes `victim->dmg.facing_dir_1 = -thrower->facing_dir`; ftCo_8008DCE0
    //   uses that sign for KB velocity (`kb_x = -x * facing_dir_1`).
    // - ftCo_800DE7C0 then passes `facing_dir = -dmg.facing_dir_1` to ftCo_8008DCE0 for raw
    //   throw-hit angles strictly between 90 and 270 degrees, and ftCo_8008DCE0 overwrites
    //   `fp->facing_dir` with that argument after velocity selection but before motion entry.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Thrown.s::ftCo_800DE7C0
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    const float final_facing_dir = -defender_facing_dir_1;
    batch->state.facing[d_idx] = (uint8_t)(final_facing_dir > 0.0f);
  }

  // Throw-release hits do not apply hitlag in the suite (Slippi hitlag stays 0), and `x221A_b3`
  // is observed unset. Do not set it here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4

  // Throw hits mark the damaged hurtbox as "mid" in decomp (x184c_damaged_hurtbox = 1).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  const uint8_t hurt_height = 1u;
  MslCombatProcessHitResolved ev = {0};
  ev.bi = batch_index;
  ev.attacker = attacker;
  ev.defender = defender;
  ev.a_idx = a_idx;
  ev.d_idx = d_idx;
  ev.d_motion_id = d_motion_id;
  ev.kb_applied = kb_applied;
  ev.kb_angle_rad = damage_state_angle_rad;
  ev.kb_x = kb_x;
  ev.kb_y = kb_y;
  ev.defender_on_ground = defender_on_ground;
  ev.apply_throw_release_di = 1u;
  ev.hurt_height = hurt_height;
  ev.damage_state_raw_angle = p->angle;
  if (low_throw_damage_state_arg_owner != 0u) {
    // Low-throw release damage-state argument:
    // `ftCo_800DE7C0(victim, thrower, fp->motion_id == ThrowLw)` passes arg1=90 for ThrowLw.
    // `ftCo_8008DCE0` first promotes any non--1 arg1 to var_r28=3 (tumble/fly severity), then
    // forces the final motion id to arg1. Keeping only the 90-degree angle leaves low-percent
    // ThrownLw release rows in DamageAir3 instead of source-owned DamageFlyTop.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_800DE7C0,calcKnockbackAngle}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
    ev.force_tumble_severity = 1u;
  }
  ev.instance_hit_by = batch->state.instance_id[a_idx];
  ev.last_hit_by = combat_source_port0_for_attacker(batch, a_idx, attacker);
  ev.source_write = MSL_PROCESS_HIT_SOURCE_WRITE_DIRECT;
  ev.update_bookkeeping = update_bookkeeping;
  ev.stale_move_id = damage_product.move_id;
  ev.stale_attack_instance = damage_product.attack_instance;
  ev.combo_attack_id = batch->state.attack_id[a_idx];
  combat_processhit_apply_resolved_damage(c, batch, &ev);
  // Throw-release ordering:
  // - ftCo_800DDDE4 routes into Fighter_ProcessHit damage entry, and ftCo_8008DCE0 already performs
  //   an immediate ftAnim_8006EBA4 on state change.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  //
  // Keep only the damage-entry immediate tick here; any compatibility pending-release caller that
  // runs later must not add a second entry tick and over-advance action_frame.

  return 1;
}

uint8_t combat_apply_throw_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                               const MslThrowHitboxParams* p) {
  return combat_apply_throw_hit_core(batch, batch_index, attacker, defender, p, 1u);
}

static inline void combat_throw_release_integrate_position_now(MslBatch* batch, size_t owner_idx,
                                                               size_t victim_idx) {
  if (batch == NULL) {
    return;
  }
  const uint8_t on_ground = batch->state.on_ground[victim_idx] ? 1u : 0u;
  const float vx_self = on_ground ? batch->state.speed_ground_x_self[victim_idx]
                                  : batch->state.speed_air_x_self[victim_idx];
  if (on_ground) {
    batch->state.speed_air_x_self[victim_idx] = vx_self;
  }
  const float vy_self = batch->state.speed_y_self[victim_idx];
  const float vx = vx_self + batch->state.speed_x_attack[victim_idx];
  const float vy = vy_self + batch->state.speed_y_attack[victim_idx];
  float owner_dx = 0.0f;
  if (owner_idx != victim_idx) {
    owner_dx = batch->state.on_ground[owner_idx] ? batch->state.speed_ground_x_self[owner_idx]
                                                 : batch->state.speed_air_x_self[owner_idx];
  }
  // ftCo_800DDDE4 resolves throw-release world placement before later same-frame contacts.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  batch->state.pos_x[victim_idx] += vx + owner_dx;
  batch->state.pos_y[victim_idx] += vy;
}

static inline void combat_throw_release_apply_immediate_di(MslBatch* batch, size_t victim_idx,
                                                           const MslCommonParams* c) {
  if (batch == NULL || c == NULL) {
    return;
  }
  if (batch->state.on_ground[victim_idx]) {
    return;
  }

  // Throw-release damage entry has no hitlag in the replay-visible Fox/Falco slice, but decomp still
  // runs the same DI velocity mutator immediately after ftCo_8008DCE0 installs throw KB:
  //   ftCo_800DD724 -> ftCo_800DDDE4 -> ftCo_800DE7C0 -> ftCo_8008E5A4
  // ftCo_800DD724 is the Throw Anim callback path, which runs before Fighter_procUpdate's
  // current-input install. Read the pre-input stick lane preserved in prev_input_main_* rather than
  // the already-applied current input; the L/R x1AC multiplier belongs to
  // ftCo_Damage_OnExitHitlag and is intentionally not applied here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E5A4
  // refs/melee/src/melee/ft/fighter.c::{Fighter_8006A360,Fighter_procUpdate}
  float kb_x = batch->state.speed_x_attack[victim_idx];
  float kb_y = batch->state.speed_y_attack[victim_idx];
  const float kb_mag_sq = kb_x * kb_x + kb_y * kb_y;
  if (kb_mag_sq < 0.00001f) {
    return;
  }

  const float lstick_x = stick_i8_to_unit(batch->state.prev_input_main_x[victim_idx]);
  const float lstick_y = stick_i8_to_unit(batch->state.prev_input_main_y[victim_idx]);
  const float lstick_full_x = apply_deadzone(lstick_x, c->lstick_deadzone_x);
  const float lstick_full_y = apply_deadzone(lstick_y, c->lstick_deadzone_y);
  const float kb_neg_x = -kb_x;
  const float dot = kb_y * lstick_full_x + kb_neg_x * lstick_full_y;
  float dir = (dot * dot) / kb_mag_sq;
  const float cross_z = kb_x * lstick_full_y - kb_y * lstick_full_x;
  if (cross_z < 0.0f) {
    dir = -dir;
  }

  const float kb_angle = atan2f(kb_y, kb_x) + (c->di_max_deg * (MSL_PI_F / 180.0f)) * dir;
  const float kb_mag = sqrtf(kb_mag_sq);
  kb_x = kb_mag * cosf(kb_angle);
  kb_y = kb_mag * sinf(kb_angle);

  batch->state.speed_x_attack[victim_idx] = kb_x;
  batch->state.speed_y_attack[victim_idx] = kb_y;
}

void combat_apply_item_shield_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                  uint16_t item_attack_id, uint16_t item_attack_instance,
                                  float damage, int8_t hitbox_shield_damage, uint8_t hit_element,
                                  float item_pos_x) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return;
  }
  if (attacker < 0 || attacker >= num_players || defender < 0 || defender >= num_players ||
      attacker == defender) {
    return;
  }

  const size_t d_idx = msl_idx_player(batch_index, defender);
  const size_t a_idx = msl_idx_player(batch_index, attacker);

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Decomp (GALE01): item stale multiplier is applied to item hitbox damage before collision
  // consumes it for shieldstun/hitlag as well.
  // refs/melee/src/melee/it/itcoll.c::it_80272460 (calls ft_80089228)
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  (void)item_attack_instance;
  float dmg_f = damage;
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, item_attack_id);
  if (stale_mult != 1.0f) {
    dmg_f *= stale_mult;
  }

  const int int_dmg = combat_get_env_dmg(dmg_f);
  if (int_dmg <= 0) {
    return;
  }

  // Decomp item->shield path does not gate shieldDamageTaken on powershield-active
  // (`fp->x221C_b2`); ftColl_80077688 accumulates fp->x19A0 unconditionally, while the separate
  // item callback state still uses shield/reflect flags for bounce/reflect ownership.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const uint8_t powershield_active_recoil = combat_guard_setoff_recoil_x221c_b2_idx(batch, d_idx);
  int shield_damage_taken =
      (int_dmg + (int)hitbox_shield_damage > 0) ? (int_dmg + (int)hitbox_shield_damage) : 0;

  const float light = combat_latched_lightshield_amount_idx(batch, d_idx);
  const float ls = (light * (c->shield_hit_lightshield_max - c->shield_hit_lightshield_min)) +
                   c->shield_hit_lightshield_min;
  const float depletion = c->shield_hit_damage_mul * ((float)shield_damage_taken * (1.0f - ls)) +
                          c->shield_hit_damage_base;

  float hp = batch->state.shield_hp[d_idx];
  hp -= depletion;
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[d_idx] = hp;

  // Capture defender motion id before we transition into GuardSetOff.
  const uint16_t d_motion_id_pre = batch->state.action_id[d_idx];

  // Shieldstun (GuardSetOff) entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  batch->state.action_id[d_idx] = (uint16_t)MSL_ACT_GUARD_SET_OFF;
  batch->state.animation_index[d_idx] = (uint32_t)MSL_SM_GUARD_DAMAGE;
  batch->state.tilt_timer_x[d_idx] = 0xFEu;
  // Source x19A4 owner for GuardSetOff callbacks. Item shield contact writes the integer damage
  // owner before ftCo_80092F2C installs the active-hitlag `ftCo_80093240` callback; that callback
  // can consume the same source lane on later hitlag ticks after input timers advance.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80077688
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093240}
  batch->state.guard_setoff_hitlag_damage_min[d_idx] = (int_dmg > 255) ? 255u : (uint8_t)int_dmg;
  combat_state_flags_clear_guard_reflecting(batch, d_idx);
  combat_state_flags_clear_stale_guard_timer_bits_on_setoff_entry(batch, d_idx);
  combat_preserve_guard_x10_for_immediate_setoff(batch, d_idx, d_motion_id_pre, c, 0u);

  const float ls_stun =
      (light * (c->shield_stun_lightshield_max - c->shield_stun_lightshield_min)) +
      c->shield_stun_lightshield_min;
  float stun_frames =
      c->shield_stun_mul * ((float)int_dmg * (1.0f - ls_stun)) + c->shield_stun_base;
  if (!(stun_frames > 0.0f)) {
    stun_frames = 1.0f;
  }
  const float end_frame =
      msl_anim_end_frame(batch->state.char_id[d_idx], (uint16_t)MSL_SM_GUARD_DAMAGE);
  float anim_rate = 1.0f;
  if (end_frame > 0.0f) {
    anim_rate = (end_frame + 0.1f) / stun_frames;
  }
  msl_anim_timebase_enter(batch, d_idx, 0.0f, anim_rate);

  // Item->shield GuardSetOff grounded recoil ownership:
  // - ftColl_80077688 is the item-specific shield-contact helper that feeds the same GuardSetOff
  //   x19A4/x19AC/x19B0 lanes later consumed by ftCo_80092F2C.
  // - When the new max int damage wins, it writes:
  //     if (fp->cur_pos.x > item->pos.x) specialn_facing_dir = -1.0f; else +1.0f
  //     x19B0 = hit->element
  // - ftCo_80092F2C then uses that sign to write GuardSetOff recoil `gr_vel`, while the frozen
  //   entry row still preserves grounded `self_vel.x` separately.
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80077688 (0x800778F4..0x80077918)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Guard.s:1656-1679
  if (batch->state.on_ground[d_idx] && hit_element != (uint8_t)MSL_HIT_ELEMENT_GROUND) {
    float push = stun_frames * c->shield_setoff_push_mul;
    if (!powershield_active_recoil) {
      push *= c->shield_setoff_push_mul_non_yoshi;
    }
    if (push > c->shield_setoff_push_max) {
      push = c->shield_setoff_push_max;
    }
    // Item->shield recoil sign consumption:
    // - ftColl_80077688 writes `specialn_facing_dir = -1` when defender.x > item.x, else `+1`,
    // - ftCo_80092F2C then writes `gr_vel = +push` when specialn_facing_dir < 0, else `-push`.
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80077688 (0x800778F4..0x80077918)
    // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Guard.s:1668-1679
    const float recoil_sign = (batch->state.pos_x[d_idx] > item_pos_x) ? 1.0f : -1.0f;
    batch->state.speed_ground_x_self[d_idx] = recoil_sign * push;
  }

  // Hitlag (defender only): the "attacker" for projectiles is the item, not the owning fighter.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC and fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t d_hl = combat_calc_hitlag_frames(c, int_dmg, d_motion_id_pre, 1.0f);
  batch->state.hitlag[d_idx] = d_hl;
  combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);

  // Track the owner as the source for shield state (Slippi instance_hit_by/last_hit_by are BODY-only).
  (void)a_idx;
}

static inline void combat_mutations_pass1_future_apply_shield_hit(
    MslBatch* batch, size_t a_idx, size_t d_idx, int a_max_int_dmg, int d_max_int_dmg,
    int shield_damage_taken, uint16_t attacker_motion_id, uint8_t hit_element) {
  if (batch == NULL) {
    return;
  }

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Capture defender motion id before we transition into GuardSetOff.
  const uint16_t d_motion_id_pre = batch->state.action_id[d_idx];

  // Shield HP depletion:
  //
  // Decomp collision accumulates `shieldDamageTaken` as Σ max(0, int_dmg + hitbox_shield_damage):
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  //
  // The caller passes the source-shaped per-frame accumulator for accepted shield contacts in this
  // (attacker, defender) pair. GuardSetOff entry remains one transition, matching the later
  // Fighter_ProcessHit consume point for the accumulated x19A0/x19A4 lanes.
  //
  // Fighter_ProcessHit applies the per-frame shield health reduction:
  // shield_health -= x284 * (shieldDamageTaken*(1 - (lightshield_amount*(x2E0-x2DC)+x2DC))) + x288
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  if (a_max_int_dmg < 0) {
    a_max_int_dmg = 0;
  }
  if (d_max_int_dmg < 0) {
    d_max_int_dmg = 0;
  }
  if (shield_damage_taken < 0) {
    shield_damage_taken = 0;
  }

  // Powershield gating: collision does not accumulate shieldDamageTaken when the "powershield
  // active" flag is set (x221C_b2).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC (`if (!fp1->x221C_b2) { ...shieldDamageTaken... }`)
  uint8_t powershield_active = combat_shield_damage_powershield_suppressed_idx(batch, d_idx);
  const uint8_t guardsetoff_carried_x19a0_owner =
      (combat_guardsetoff_carried_shield_packet_owner(batch, d_idx) != 0u) ? 1u : 0u;
  if (powershield_active != 0u && guardsetoff_carried_x19a0_owner != 0u) {
    // Active GuardSetOff carried shield-hit packet:
    // Slippi can still expose fp+0x221C_b2 on an already-entered GuardSetOff snapshot, while the
    // hidden x19A4/x19A0 lanes prove ftColl_80076CBC accumulated a fresh shield-hit packet for
    // this callback. The x221C bit is stale replay-visible state here; source collision already
    // wrote x19A0, so Fighter_ProcessHit consumes that damage instead of powershield-suppressing it.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093240}
    powershield_active = 0u;
  }
  const uint8_t recoil_powershield_active = combat_guard_setoff_recoil_x221c_b2_idx(batch, d_idx);
  if (powershield_active) {
    // Powershield-active fighter shield contact:
    // ftColl_80076CBC skips x19A0 shieldDamageTaken, then calls ftCo_80094138, which arms
    // mv.co.guard.x1C from p_ftCommonData->x2B8 and clears mv.co.guard.x10. GuardOff_IASA later
    // uses x1C as the only gate for its special/attack chain.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80094138,ftCo_GuardOff_IASA}
    batch->state.guard_special_enable_timer_x1c[d_idx] = c->guard_special_enable_frames;
    batch->state.guard_x10[d_idx] = 0u;
    shield_damage_taken = 0;
  }

  const float light = combat_latched_lightshield_amount_idx(batch, d_idx);
  const float ls = (light * (c->shield_hit_lightshield_max - c->shield_hit_lightshield_min)) +
                   c->shield_hit_lightshield_min;
  const float depletion = c->shield_hit_damage_mul * ((float)shield_damage_taken * (1.0f - ls)) +
                          c->shield_hit_damage_base;

  float hp = batch->state.shield_hp[d_idx];
  hp -= depletion;
  if (hp < 0.0f) {
    hp = 0.0f;
  }
  batch->state.shield_hp[d_idx] = hp;

  // Shieldstun (GuardSetOff) entry.
  //
  // Decomp entry: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  // - Changes motion state to ftCo_MS_GuardSetOff.
  // - Sets x670_timer_lstick_tilt_x = -2.
  // - Computes shieldstun duration f (float) and sets anim rate to (0.1 + end_frame) / f.
  // Its input x19A4 is written by ftColl_80076CBC before Fighter_ProcessHit consumes the shield
  // contact; retain the same source lane for the active-hitlag ftCo_80093240 callback window.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093240}
  batch->state.action_id[d_idx] = (uint16_t)MSL_ACT_GUARD_SET_OFF;
  batch->state.guard_setoff_hitlag_damage_min[d_idx] =
      (d_max_int_dmg > 255) ? 255u : (uint8_t)d_max_int_dmg;
  // Decomp: GuardSetOff uses ftCo_SM_GuardDamage as its submotion (msid=40).
  // refs/melee/src/melee/ft/ftmotionstates.c (GuardSetOff motion-state entry uses ftCo_SM_GuardDamage)
  batch->state.animation_index[d_idx] = (uint32_t)MSL_SM_GUARD_DAMAGE;
  combat_state_flags_clear_guard_reflecting(batch, d_idx);
  combat_state_flags_clear_stale_guard_timer_bits_on_setoff_entry(batch, d_idx);
  combat_preserve_guard_x10_for_immediate_setoff(batch, d_idx, d_motion_id_pre, c,
                                                 powershield_active);

  // Decomp: fp->x670_timer_lstick_tilt_x = -2.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  batch->state.tilt_timer_x[d_idx] = 0xFEu;

  // Shieldstun duration f (float) and anim rate.
  //
  // Decomp:
  // f = x28C*(x19A4*(1 - (lightshield_amount*(x2E8-x2E4)+x2E4))) + x290
  // anim_rate = (0.1 + lbGetJObjEndFrame(GET_JOBJ(gobj))) / f
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  const float ls_stun =
      (light * (c->shield_stun_lightshield_max - c->shield_stun_lightshield_min)) +
      c->shield_stun_lightshield_min;
  float stun_frames =
      c->shield_stun_mul * ((float)d_max_int_dmg * (1.0f - ls_stun)) + c->shield_stun_base;
  if (!(stun_frames > 0.0f)) {
    stun_frames = 1.0f;
  }
  // GuardSetOff uses ftCo_SM_GuardDamage as the underlying animation timeline (submotion id 40).
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCo_Submotion)
  const float end_frame =
      msl_anim_end_frame(batch->state.char_id[d_idx], (uint16_t)MSL_SM_GUARD_DAMAGE);
  float anim_rate = 1.0f;
  if (end_frame > 0.0f) {
    anim_rate = (end_frame + 0.1f) / stun_frames;
  }
  msl_anim_timebase_enter(batch, d_idx, 0.0f, anim_rate);

  // GuardSetOff grounded pushback ownership:
  // - ftColl_80076CBC stores the shield owner's x19A4 (max int dmg over shield overlaps this frame),
  //   specialn_facing_dir sign, and x19B0 element before ftCo_80092F2C runs.
  // - ftCo_80092F2C computes `f = x28C*(x19A4*(1-(light*(x2E8-x2E4)+x2E4))) + x290`,
  //   then when x19B0 != 10 writes:
  //     push = clamp(f * x294 * (x221C_b2 ? 1.0f : x2BC), x298)
  //     gr_vel = (specialn_facing_dir < 0) ? +push : -push
  // - ASM confirms the final write is `stfs +/-f2, fp->gr_vel` (not the broken decomp line).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Guard.s:1656-1679
  if (batch->state.on_ground[d_idx] && hit_element != (uint8_t)MSL_HIT_ELEMENT_GROUND) {
    float push = stun_frames * c->shield_setoff_push_mul;
    if (!recoil_powershield_active) {
      push *= c->shield_setoff_push_mul_non_yoshi;
    }
    if (push > c->shield_setoff_push_max) {
      push = c->shield_setoff_push_max;
    }
    const float shield_sign =
        (batch->state.pos_x[d_idx] > batch->state.pos_x[a_idx]) ? 1.0f : -1.0f;
    batch->state.speed_ground_x_self[d_idx] = shield_sign * push;
  }

  // Hitlag on shield contact uses the same decomp ftCommon_CalcHitlag path as BODY, but with
  // shield-collision inputs:
  // - attacker uses fp->dmg.x1924 (max int_dmg over shield contacts this frame),
  // - defender uses fp->x19A4 (max int_dmg over shield contacts this frame),
  // both computed from hit0->damage via getEnvDmg.
  //
  // Replay seed lanes can recover the defender-only x19A4 scalar without recovering the exact
  // attacker dmg.x1924 packet. Keep the two consumers split so a teacher-forced x19A4 lower-bound
  // does not incorrectly rewrite attacker hitlag.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC and fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t a_hl = combat_calc_hitlag_frames(c, a_max_int_dmg, attacker_motion_id, 1.0f);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, d_max_int_dmg, d_motion_id_pre, 1.0f);
  batch->state.hitlag[a_idx] = a_hl;
  batch->state.hitlag[d_idx] = d_hl;
  combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
  combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);

  if (batch->state.on_ground[a_idx]) {
    // Grounded attacker shield-pushback onset ownership:
    // - ftColl_80076CBC stores `attacker.dmg.x1928 = defender.lightshield_amount * int_dmg` and a
    //   sign in `attacker.dmg.x192C` from relative X positions.
    // - Fighter_ProcessHit_8006D1EC then writes:
    //     eval = x1928 * x3E0 + x3E4
    //     xF4_ground_attacker_shield_kb_vel = +/-eval
    //   and projects it to `x98_atk_shield_kb` through ftCommon_8007E2A4.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E2A4
    const float eval = light * (float)a_max_int_dmg * c->shield_attacker_ground_kb_mul +
                       c->shield_attacker_ground_kb_base;
    batch->state.attacker_shield_ground_kb_vel[a_idx] =
        (batch->state.pos_x[d_idx] > batch->state.pos_x[a_idx]) ? -eval : eval;
  } else {
    batch->state.attacker_shield_ground_kb_vel[a_idx] = 0.0f;
  }
}

static inline float combat_ecb_midpoint_world_y(const MslBatch* batch, size_t idx) {
  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[idx]);
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
  const MslEcbExtentsRel ecb =
      msl_ecb_extents_rel(batch->state.char_id[idx], batch->state.animation_index[idx], (int)frame);
  float bottom_rel_y = ecb.min_y;
  if (batch->state.coll_ecb_bottom_valid[idx] &&
      isfinite(batch->state.coll_ecb_bottom_rel_y[idx])) {
    bottom_rel_y = batch->state.coll_ecb_bottom_rel_y[idx];
  }
  return batch->state.pos_y[idx] + 0.5f * (ecb.max_y + bottom_rel_y);
}

static inline uint8_t combat_catch_wall_obstructed_ft_80084CE4(const MslBatch* batch, int bi,
                                                               size_t a_idx, size_t d_idx) {
  const float ax = batch->state.pos_x[a_idx];
  const float ay = combat_ecb_midpoint_world_y(batch, a_idx);
  const float dx = batch->state.pos_x[d_idx];
  const float dy = combat_ecb_midpoint_world_y(batch, d_idx);
  if (!isfinite(ax) || !isfinite(ay) || !isfinite(dx) || !isfinite(dy) || ax == dx) {
    return 0u;
  }

  const uint32_t stage_id = batch->state.stage_id[bi];
  const MslStageWallGraph* g = (ax > dx) ? stage_collision_get_right_wall_graph(stage_id)
                                         : stage_collision_get_left_wall_graph(stage_id);
  if (g == NULL || g->lines == NULL || g->line_count == 0u) {
    return 0u;
  }
  const float min_x = ax < dx ? ax : dx;
  const float max_x = ax > dx ? ax : dx;
  const float min_y = ay < dy ? ay : dy;
  const float max_y = ay > dy ? ay : dy;
  if (max_x < g->min_x || min_x > g->max_x || max_y < g->min_y || min_y > g->max_y) {
    return 0u;
  }

  for (size_t i = 0; i < g->line_count; i++) {
    const MslStageWallLine* line = &g->lines[i];
    if (!line->fighter_solid) {
      continue;
    }
    const float line_min_x = line->x0 < line->x1 ? line->x0 : line->x1;
    const float line_max_x = line->x0 > line->x1 ? line->x0 : line->x1;
    const float line_min_y = line->y0 < line->y1 ? line->y0 : line->y1;
    const float line_max_y = line->y0 > line->y1 ? line->y0 : line->y1;
    if (max_x < line_min_x || min_x > line_max_x || max_y < line_min_y || min_y > line_max_y) {
      continue;
    }
    if (combat_segment_intersects_2d(ax, ay, dx, dy, line->x0, line->y0, line->x1, line->y1)) {
      return 1u;
    }
  }
  return 0u;
}

static void combat_select_catch_hits_one_mutating(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    if (batch->state.stocks[a_idx] == 0) {
      continue;
    }
    if (batch->state.hitlag_started_frame[a_idx] != 0) {
      continue;
    }
    if (batch->state.hitbox_count[a_idx] == 0) {
      continue;
    }

    const uint16_t a_motion_id = batch->state.action_id[a_idx];
    // Catch mask kind (fp->x1A68): ordinary Catch/CatchDash arm kind 1 (ftCo_800D8C54 via
    // ftCommon_8007E2D0); Falcon Dive arms kind 2 at Special(Air)Hi entry. The kind selects
    // which downed victims the x1A6A mask rejects below.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8C54
    // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{ftCa_SpecialHi_Enter,
    //   ftCa_SpecialAirHi_Enter}
    // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007E2D0
    uint8_t catch_kind_x1a68 = 0u;
    if (a_motion_id == (uint16_t)MSL_ACT_CATCH || a_motion_id == (uint16_t)MSL_ACT_CATCH_DASH) {
      catch_kind_x1a68 = 1u;
    } else if (batch->state.char_id[a_idx] == (uint8_t)MSL_CHAR_ID_FALCON &&
               (a_motion_id == (uint16_t)MSL_ACT_CA_SPECIAL_HI ||
                a_motion_id == (uint16_t)MSL_ACT_CA_SPECIAL_AIR_HI)) {
      catch_kind_x1a68 = 2u;
    } else {
      continue;
    }

    // Decomp shape: ftColl_80078A2C keeps nearest victim by X distance (ftGrabDist), then runs the
    // catch connect transition once for that selected victim.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
    int best_victim = -1;
    float best_abs_dx = 0.0f;
    uint8_t best_hit_group = 0u;
    uint8_t best_rehit_frames = 0u;

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }

      const size_t d_idx = msl_idx_player(bi, defender);
      if (batch->state.stocks[d_idx] == 0) {
        continue;
      }
      if (batch->state.hitlag_started_frame[d_idx] != 0) {
        continue;
      }
      if (batch->state.grab_owner_port[d_idx] != 0xFFu) {
        continue;
      }
      if (msl_action_owns_x2219_collision_skip(batch->state.action_id[d_idx])) {
        // Dead*/Rebirth source states set fp->x2219_b1. In vanilla, Fighter_8006CB94 does not call the
        // common collision pass for that fighter while the bit is set, and catch selection also
        // rejects x2219_b1 victims. Keep this separate from visible Slippi hurtbox_state: the
        // platform row can still report Wait1/vulnerable hit status while being collision-skipped.
        // refs/melee/src/melee/ft/ft_0D31.c::{ftCo_800D3680,ftCo_800D3950,ftCo_800D3BC8,ftCo_800D3E40,ftCo_800D4580,ftCo_800D481C}
        // refs/melee/src/melee/ft/ft_0D4D.c::{ftCo_800D4FF4,ftCo_800D5600}
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
        continue;
      }
      // Decomp catch target mask:
      // - Catch entry calls ftCommon_8007E2D0(fp, 1, ...), installing attacker fp->x1A68 = 1.
      // - DownBound entry calls ftCommon_8007E2F4(fp, 0x1FF); DownBound/DownDamage -> DownWait
      //   handoffs call ftCommon_8007E2F4(fp, 1).
      // - ftColl_80078A2C rejects victims when `(victim_fp->x1A6A & this_fp->x1A68) != 0`,
      //   before grabbable capsule overlap. This is why knocked-down victims are not catchable
      //   even when their ordinary hurt capsules overlap the grab bubble.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::ftCo_800D8C54
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_8009794C,ftCo_80097E8C,ftCo_80097F38}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_8009F184
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
      // Kind-2 (Falcon Dive) rejection set: only DownBound rows still carry x1A6A=0x1FF
      // (0x1FF & 2 != 0); the DownBound/DownDamage -> DownWait handoffs and DownDamage entry
      // reset x1A6A to 1, which kind 2 does not mask (1 & 2 == 0) — the Dive can grab downed
      // victims ordinary catches cannot.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownBound.c::{ftCo_8009794C,ftCo_80097E8C,
      //   ftCo_80097F38}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_8009F184
      if (catch_kind_x1a68 == 2u
              ? combat_defender_downed_catch_mask_kind2_blocks(batch->state.action_id[d_idx])
              : combat_defender_downed_catch_mask_blocks(batch->state.action_id[d_idx])) {
        continue;
      }

      // Catch eligibility mirrors decomp vulnerable gate (x1988==0 && x198C==0): unlike BODY hits,
      // invincible victims are not catch-selectable.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
      const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
      uint8_t hurt_state = batch->state.hurtbox_state[d_idx];
      if (hit_status > hurt_state) {
        hurt_state = hit_status;
      }
      if (hurt_state != 0u) {
        continue;
      }

      // Catch wall occlusion:
      // ftColl_80078A2C rejects a candidate after grabbable capsule contact when ft_80084CE4
      // reports a left/right wall between the fighters' ECB midpoints. The predicate is independent
      // of replay rows and consumes the ISO-derived MSLSTG01 wall graph.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
      // refs/melee/src/melee/ft/ft_081B.c::ft_80084CE4
      // refs/melee/src/melee/mp/mplib.c::{mpCheckLeftWall,mpCheckRightWall}
      // data/stages/bin/*.bin::MSLSTG01
      if (combat_catch_wall_obstructed_ft_80084CE4(batch, bi, a_idx, d_idx)) {
        continue;
      }

      uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      const MslHurtCap* catch_source_caps = NULL;
      uint16_t catch_source_count_u16 = 0u;
      uint8_t use_catch_source_caps = 0u;
      if (combat_guard_family_no_submotion_catch_source_applies(batch, d_idx)) {
        if (hurtcaps_get(batch->state.char_id[d_idx], &catch_source_caps,
                         &catch_source_count_u16) == 0 &&
            catch_source_caps != NULL && catch_source_count_u16 != 0u) {
          use_catch_source_caps = 1u;
          hurtcap_count = catch_source_count_u16 > (uint16_t)MSL_MAX_HURTCAPS
                              ? (uint8_t)MSL_MAX_HURTCAPS
                              : (uint8_t)catch_source_count_u16;
        }
      }
      if (hurtcap_count == 0) {
        continue;
      }
      const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;

      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
        if (!batch->state.hitbox_enabled[hb_i]) {
          continue;
        }
        if (combat_catch_primary_enable_edge_rejects_down_forward(batch, a_idx, d_idx, hb_i,
                                                                  hb_id)) {
          continue;
        }
        if (batch->state.hitbox_element[hb_i] != (uint8_t)MSL_HIT_ELEMENT_CATCH) {
          continue;
        }

        const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
        if (defender_on_ground) {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
            continue;
          }
        } else {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
            continue;
          }
        }

        const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        // Catch-select path is not identical to our BODY-hit suppression pipeline.
        //
        // Decomp:
        // - ftColl_80078A2C does run lbColl_8000ACFC(this_hit, victim) plus the victim mask gate
        //   `(victim_fp->x1A6A & this_fp->x1A68)` before overlap tests.
        //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
        //   refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
        //
        // Seed-bridge note:
        // - Our reseed bridge reconstructs HitVictim rings from the dense seed cooldown map
        //   (`combat_hitlist_cd`/`combat_hitlist_victim_iid`). On catch frames this can over-latch
        //   stale victims relative to decomp runtime pointers/masks and block replay-real connect.
        // - Intentional v1 approximation here: keep decomp-shaped catch eligibility gates above and
        //   defer HitVictim insertion to the selected catch connect below.
        //
        // This is scoped to catch selection only; BODY hits still use hitlist_allows_fighter.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
        const uint8_t rehit_frames =
            hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);

        float hx = batch->state.hitbox_x[hb_i];
        float hy = batch->state.hitbox_y[hb_i];
        float hz = batch->state.hitbox_z[hb_i];
        const float raw_hr = batch->state.hitbox_radius[hb_i];
        float hr = raw_hr;
        combat_catch_hitbox_model_scale_compensated(batch, bi, attacker, hb_id, &hx, &hy, &hz, &hr);

        uint8_t found_grab_contact = 0u;
        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          float ax = 0.0f, ay = 0.0f, az = 0.0f;
          float bx = 0.0f, by = 0.0f, bz = 0.0f;
          float cr = 0.0f;
          uint8_t overlaps = 0u;
          if (use_catch_source_caps) {
            if (!combat_guard_family_catch_hurtcap_world(batch, d_idx, &catch_source_caps[cap_id],
                                                         &ax, &ay, &az, &bx, &by, &bz, &cr)) {
              continue;
            }
            overlaps = combat_catch_overlap_lbColl_80007ECC(batch, bi, attacker, hb_id, hx, hy, hz,
                                                            hr, ax, ay, az, bx, by, bz, cr);
          } else {
            const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
            // Catch uses grabbability, not the BODY-hit enabled bit:
            // ftColl_80078A2C checks victim `hurt_capsules[j].is_grabbable` after fighter-wide
            // x1988/x198C/victim-mask gates. The per-capsule body-hit mask can be disabled on
            // shield / guard snapshots while grabs are still legal.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
            if (!batch->state.hurtcap_is_grabbable[cap_i] ||
                !(batch->state.hurtcap_radius[cap_i] > 0.0f)) {
              continue;
            }
            ax = batch->state.hurtcap_a_x[cap_i];
            ay = batch->state.hurtcap_a_y[cap_i];
            az = batch->state.hurtcap_a_z[cap_i];
            bx = batch->state.hurtcap_b_x[cap_i];
            by = batch->state.hurtcap_b_y[cap_i];
            bz = batch->state.hurtcap_b_z[cap_i];
            cr = batch->state.hurtcap_radius[cap_i];
            (void)combat_catch_grabbable_dynamic_hurtcap_world(batch, d_idx, cap_id, &ax, &ay, &az,
                                                               &bx, &by, &bz, &cr);
            overlaps = combat_catch_overlap_lbColl_80007ECC(batch, bi, attacker, hb_id, hx, hy, hz,
                                                            hr, ax, ay, az, bx, by, bz, cr);
            if (overlaps) {
              uint8_t catch_lbcoll_evaluated = 0u;
              uint8_t matrix_overlaps = combat_body_overlap_lbColl_80006E58_matrix_radius(
                  batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az, bx,
                  by, bz, 1u, NULL, &catch_lbcoll_evaluated);
              if (!catch_lbcoll_evaluated) {
                matrix_overlaps = combat_body_overlap_lbColl_80006E58_matrix_radius(
                    batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az,
                    bx, by, bz, 0u, NULL, &catch_lbcoll_evaluated);
              }
              if (catch_lbcoll_evaluated && !matrix_overlaps) {
                // Catch narrowphase source refinement:
                // - ftColl_80078A2C routes grabbable hurtcaps through lbColl_80007ECC, which in
                //   turn consumes lbColl_80006E58's matrix-derived radius.
                // - SSDYNN01's catch-grabbable owner index can expose live dynamic-chain matrices
                //   for Catch selection without widening ordinary BODY collision owners; Fox
                //   AttackDash is the motivating split where Dolphin probes show live part-18
                //   grabbable pose in Catch but existing BODY locks keep AttackDash static.
                // - Ordinary grabbable hurtcaps still use the same source-pose matrix veto when no
                //   dynamic catch-grabbable owner exists. Fox KneeBend near-miss probes show
                //   lbColl_80007ECC rejecting the simple replay-state capsule overlap after
                //   lbColl_80006E58's source matrix/radius refinement.
                // - The local matrix reconstruction is exact enough to reject evaluated misses, but
                //   not yet exact enough to add new catch admissions over the simpler world capsule
                //   path, so keep it fail-closed for source-owned veto only.
                // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
                // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007ECC,lbColl_80006E58}
                overlaps = 0u;
              }
            }
          }

          if (!overlaps) {
            continue;
          }

          const float abs_dx = fabsf(batch->state.pos_x[d_idx] - batch->state.pos_x[a_idx]);
          if (best_victim < 0 || abs_dx < best_abs_dx ||
              (abs_dx == best_abs_dx && defender < best_victim)) {
            best_victim = defender;
            best_abs_dx = abs_dx;
            best_hit_group = hit_group;
            best_rehit_frames = rehit_frames;
          }
          found_grab_contact = 1u;
          break;
        }
        if (found_grab_contact) {
          // Decomp shape: after finding a valid grabbable overlap for this defender, advance to the
          // next defender candidate (ftColl_80078A2C uses a goto next_gobj path).
          break;
        }
      }
    }

    if (best_victim < 0) {
      continue;
    }

    grab_flow_on_catch_connect(batch, bi, attacker, best_victim);
    const size_t d_idx = msl_idx_player(bi, best_victim);
    const uint16_t defender_iid_post = batch->state.instance_id[d_idx];
    // Decomp catch path insert type is 0 via ftColl_80076808(..., type=0, ...).
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
    hitlist_register_fighter_group(batch, bi, attacker, best_hit_group, best_victim,
                                   defender_iid_post, (int)MSL_LBCOLL_INSERT_FT_CATCH,
                                   best_rehit_frames);
  }
}

static void combat_select_body_hits_one_mutating(MslBatch* batch, int bi) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  uint8_t any_active_hitboxes = 0u;
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(bi, p);
    if (batch->state.stocks[idx] != 0u && batch->state.hitbox_count[idx] != 0u) {
      any_active_hitboxes = 1u;
      break;
    }
  }
  if (!any_active_hitboxes) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  // Slippi post-frame `state_flags` includes fp+0x221C bits at byte index 3.

  // Clank bookkeeping:
  // - resolve clank hitlag/rebound once per unordered pair,
  // - suppress only the clanked hitboxes (not the entire fighter pair).
  //
  // Decomp ownership:
  // - ftColl_80078C70 evaluates clank per victim hitbox branch, and only that branch skips
  //   shield/body follow-up when ftColl_8007699C confirms a clank.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007699C}
  uint8_t clank_pair_done[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS] = {{0}};
  uint8_t clank_skip_hb[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{{0}}};
  uint8_t clank_candidate_skip_hb[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{{0}}};
  uint8_t v1_group_registered_this_pass[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS] = {
      {{0}}};
  uint16_t pre_combat_attack_id[MSL_MAX_PLAYERS] = {0};
  uint16_t pre_combat_attack_instance[MSL_MAX_PLAYERS] = {0};
  uint16_t pre_combat_instance_id[MSL_MAX_PLAYERS] = {0};
  uint8_t pre_combat_residual_hitcapsule_owner[MSL_MAX_PLAYERS] = {0};
  MslCombatBodyDamageScratch body_damage_logs[MSL_MAX_PLAYERS];
  uint8_t body_damage_apply_order[MSL_MAX_PLAYERS] = {0};
  uint8_t body_damage_apply_count = 0u;
  memset(body_damage_logs, 0, sizeof(body_damage_logs));
  // Collision attack/source snapshot:
  // - ftColl_80076444 / ftColl_800763C0 consume the attack id attached to the current collision
  //   pass, before later same-frame ProcessHit/ChangeMotionState effects can rewrite fp->x2068.
  // - ftColl_80076ED8 writes the attacker GObj identity into the victim's damage source before
  //   Fighter_ProcessHit mutates either fighter, so reciprocal BODY hits keep the pre-pass attacker
  //   instance rather than a post-damage motion-state instance.
  // - HitCapsule.damage is authored before this pass by ftColl_8007ABD0 / ft_80089228; use the
  //   pre-pass attack instance when excluding same-instance stale queue entries.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076444,ftColl_800763C0}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007ABD0}
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(bi, p);
    if (combat_residual_frame_start_hitcapsule_owner(batch, idx)) {
      pre_combat_residual_hitcapsule_owner[p] = 1u;
      pre_combat_attack_id[p] = batch->state.frame_start_attack_id[idx];
      pre_combat_attack_instance[p] = batch->state.frame_start_attack_instance[idx];
      pre_combat_instance_id[p] = batch->state.frame_start_instance_id[idx];
    } else {
      pre_combat_attack_id[p] = batch->state.attack_id[idx];
      pre_combat_attack_instance[p] = batch->state.attack_instance[idx];
      pre_combat_instance_id[p] = batch->state.instance_id[idx];
    }
  }

  // Process HitElement_Catch fighter-vs-fighter contacts before shield/body damage selection.
  // Decomp shape: refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
  combat_select_catch_hits_one_mutating(batch, bi);

  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    if (batch->state.stocks[a_idx] == 0) {
      continue;
    }
    if (batch->state.hitbox_count[a_idx] == 0) {
      continue;
    }

    const uint16_t a_motion_id = batch->state.action_id[a_idx];

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, defender);
      if (batch->state.stocks[d_idx] == 0) {
        continue;
      }
      if (msl_action_owns_x2219_collision_skip(batch->state.action_id[d_idx])) {
        // See combat_select_catch_hits_one_mutating(): Dead*/Rebirth states own x2219_b1, so the
        // defender's common collision pass is skipped even when visible hurtbox_state is vulnerable.
        continue;
      }

      // Clank / rebound (hitbox-vs-hitbox).
      //
      // Decomp: clanks are resolved as part of the fighter-vs-fighter collision pass and can
      // trigger ReboundStop/Rebound transitions depending on the hitbox flags (rebound/clank).
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_80099D9C (enter ReboundStop)
      // refs/melee/src/melee/ft/chara/ftCommon/forward.h (ftCo_MS_ReboundStop=237, Rebound=238)
      //
      // Bounded v1 policy (decomp-shaped ordering):
      // - Resolve clank hitlag/rebound once per unordered fighter pair.
      // - Iterate lower-slot/current-owner hitboxes against higher-slot/victim hitboxes in the same
      //   nested order as the two-player fighter list. Once ftColl_8007699C accepts a clank, it
      //   registers the victim across same-group HitCapsules immediately; later same-group clank
      //   candidates cannot raise max hitlag damage.
      // - Suppress only the clanked attacker hitboxes on each directional BODY pass
      //   (attacker->defender).
      // - Apply per-fighter hitlag using decomp ftCommon_CalcHitlag inputs derived from each
      //   fighter's max int damage among the clanking hitboxes.
      // - If a fighter has any clanking hitbox with the `rebound` flag set, enter ReboundStop for
      //   that fighter (animation_index is -1 in-suite for ReboundStop).
      const int p0 = attacker < defender ? attacker : defender;
      const int p1 = attacker < defender ? defender : attacker;
      if (!clank_pair_done[p0][p1]) {
        clank_pair_done[p0][p1] = 1u;
        clank_pair_done[p1][p0] = 1u;
        const size_t p0_idx = msl_idx_player(bi, p0);
        const size_t p1_idx = msl_idx_player(bi, p1);
        // Hitlag gating for the once-per-pair clank approximation:
        // - Decomp runs fighter-vs-fighter collision through the owner proc at priority 13
        //   (`Fighter_8006CB94` -> `ftColl_80078C70`), and frozen fighters do not keep re-owning
        //   that collision work every frame while hitlag is active.
        // - Our BODY/SHIELD lanes already model the owner-side freeze with `hitlag_started_frame`;
        //   mirror that here for the unordered clank pair approximation by skipping the pair only
        //   when both fighters are already frozen at frame start. This preserves the attacker-owned
        //   "non-hitlag owner vs frozen victim" lane while preventing same-pair hitlag refresh
        //   loops from overlapping active hitboxes (reported modelplay shine-start deadlock).
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006CB94
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_8007699C}
        if (batch->state.hitlag_started_frame[p0_idx] != 0u &&
            batch->state.hitlag_started_frame[p1_idx] != 0u) {
          continue;
        }
        // Decomp: hitbox-vs-hitbox clank check in ftColl_80079AB0 is gated to both fighters being
        // grounded (`this_fp->ground_or_air == GA_Ground && victim_fp->ground_or_air == GA_Ground`).
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80079AB0
        const uint8_t p0_grounded = batch->state.on_ground[p0_idx] != 0 ? 1u : 0u;
        const uint8_t p1_grounded = batch->state.on_ground[p1_idx] != 0 ? 1u : 0u;
        if (p0_grounded && p1_grounded && batch->state.hitbox_count[p0_idx] != 0 &&
            batch->state.hitbox_count[p1_idx] != 0) {
          // Clank damage-delta threshold (ftCommonData.x3CC) consumed by ftColl_8007699C.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007699C
          // refs/melee/src/melee/ft/types.h (ftCommonData +0x3CC)
          const int clank_damage_diff_threshold = c->clank_damage_diff_threshold;
          int max_int_dmg[2] = {0, 0};
          int max_rebound_int_dmg[2] = {0, 0};
          float rebound_damage_facing_dir[2] = {0.0f, 0.0f};
          uint8_t want_rebound_stop[2] = {0, 0};
          uint8_t did_clank = 0;
          for (int hb1 = 0; hb1 < MSL_MAX_HITBOXES; hb1++) {
            const size_t hb1_i = idx_hitbox(bi, p1, hb1);
            if (!batch->state.hitbox_enabled[hb1_i]) {
              continue;
            }
            if (clank_candidate_skip_hb[p1][p0][hb1]) {
              continue;
            }
            // Clank is a hitbox-vs-hitbox collision owner. Do not let replay-reconstructed BODY
            // victim rings suppress the clank predicate before ftColl_8007699C can refresh the
            // same-group victims for this collision pass.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007699C,inlineA0,inlineA1}
            const uint16_t f1 = batch->state.hitbox_flags[hb1_i];
            if ((f1 & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0) {
              continue;
            }
            if (!combat_hitbox_targets_fighter_ground_state(f1, p0_grounded)) {
              continue;
            }
            const uint8_t e1 = batch->state.hitbox_element[hb1_i];
            if (e1 == (uint8_t)MSL_HIT_ELEMENT_INERT) {
              continue;
            }
            const float d1 = combat_hitcapsule_collision_damage(batch, p1_idx, hb1_i);
            if (!(d1 > 0.0f)) {
              continue;
            }

            for (int hb0 = 0; hb0 < MSL_MAX_HITBOXES; hb0++) {
              const size_t hb0_i = idx_hitbox(bi, p0, hb0);
              if (!batch->state.hitbox_enabled[hb0_i]) {
                continue;
              }
              if (clank_candidate_skip_hb[p0][p1][hb0]) {
                continue;
              }
              const uint16_t f0 = batch->state.hitbox_flags[hb0_i];
              if ((f0 & (uint16_t)MSL_HITBOX_FLAG_CLANK) == 0) {
                continue;
              }
              if (!combat_hitbox_targets_fighter_ground_state(f0, p1_grounded)) {
                continue;
              }
              const uint8_t e0 = batch->state.hitbox_element[hb0_i];
              if (e0 == (uint8_t)MSL_HIT_ELEMENT_INERT) {
                continue;
              }
              const float d0 = combat_hitcapsule_collision_damage(batch, p0_idx, hb0_i);
              if (!(d0 > 0.0f)) {
                continue;
              }

              // Decomp candidate gates:
              // - Current-fighter `ftColl_804D6560` is populated only for HitCapsules whose
              //   victims_1 list does not already contain the earlier fighter.
              // - The earlier fighter branch applies the reciprocal `lbColl_8000ACFC` gate before
              //   hitbox-vs-hitbox overlap.
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
              // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
              if (!hitlist_allows_fighter_live_collision(batch, bi, p1, hb1, p0,
                                                         batch->state.instance_id[p0_idx])) {
                continue;
              }
              if (!hitlist_allows_fighter_live_collision(batch, bi, p0, hb0, p1,
                                                         batch->state.instance_id[p1_idx])) {
                continue;
              }
              if (combat_seed_hitlist_suppresses_clank_candidate(batch, bi, p1, hb1, p0)) {
                continue;
              }
              if (combat_seed_hitlist_suppresses_clank_candidate(batch, bi, p0, hb0, p1)) {
                continue;
              }

              const uint8_t clank_overlaps =
                  combat_hitbox_hitbox_overlap_lbColl_80007AFC(batch, hb0_i, hb1_i);
              if (!clank_overlaps) {
                continue;
              }
              // Decomp clank confirmation is asymmetric:
              // - ftColl_8007699C first lets `hit1` contribute clank damage to fp1 when
              //   `(int)hit1.damage - x3CC < (int)hit0.damage`,
              // - then returns true when `hit0` contributes clank damage to fp0 under the mirror
              //   comparison.
              // The return value is what skips shield/BODY follow-up for this victim HitCapsule.
              // A high-damage owner hitbox can therefore make the lower-damage victim hitbox
              // confirm the clank while the owner side receives no clank hitlag/rebound.
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007699C
              const int raw0 = (int)d0;
              const int raw1 = (int)d1;
              const uint8_t p0_side_clank_damage =
                  ((raw0 - clank_damage_diff_threshold) < raw1) ? 1u : 0u;
              const uint8_t p1_side_confirms_clank =
                  ((raw1 - clank_damage_diff_threshold) < raw0) ? 1u : 0u;
              if (!p0_side_clank_damage && !p1_side_confirms_clank) {
                continue;
              }

              // ftColl_8007699C registers the clank victim into every active HitCapsule sharing
              // the clanking hit_group (`x4`), via inlineA0/inlineA1 and lbColl_80008688. BODY
              // admission later consults lbColl_8000ACFC, so the whole same-group cluster is
              // suppressed for the opponent on this collision pass when that side's threshold
              // branch runs, not only the exact pair that overlapped in lbColl_80007AFC.
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007699C,inlineA0,inlineA1}
              // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
              if (p0_side_clank_damage) {
                did_clank = 1u;
                combat_clank_candidate_skip_same_hit_group_all(batch, bi, p0, p1, hb0,
                                                               clank_candidate_skip_hb);
                combat_clank_skip_same_hit_group(batch, bi, p0, p1, hb0, clank_skip_hb);
                combat_clank_register_same_hit_group(batch, bi, p0, p1, hb0,
                                                     batch->state.instance_id[p1_idx]);
              }
              if (p1_side_confirms_clank) {
                did_clank = 1u;
                combat_clank_candidate_skip_same_hit_group_all(batch, bi, p1, p0, hb1,
                                                               clank_candidate_skip_hb);
                combat_clank_skip_same_hit_group(batch, bi, p1, p0, hb1, clank_skip_hb);
                combat_clank_register_same_hit_group(batch, bi, p1, p0, hb1,
                                                     batch->state.instance_id[p0_idx]);
              }
              // Electric-vs-electric clank SFX lane consumes HSD_Randi(3) to pick one of three
              // entries in ftColl_803C0C4C.
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_800784B4
              // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
              if (p1_side_confirms_clank && e0 == (uint8_t)MSL_HIT_ELEMENT_ELECTRIC &&
                  e1 == (uint8_t)MSL_HIT_ELEMENT_ELECTRIC) {
                (void)combat_rng_consume_randi_site(batch, bi,
                                                    MSL_RNG_SITE_FTCOLL_ELECTRIC_CLANK_SFX, 3);
              }

              if (p0_side_clank_damage) {
                const int int0 = combat_get_env_dmg(d0);
                if (int0 > max_int_dmg[0]) {
                  max_int_dmg[0] = int0;
                }
                if ((f0 & (uint16_t)MSL_HITBOX_FLAG_REBOUND) != 0) {
                  want_rebound_stop[0] = 1u;
                  if (int0 > max_rebound_int_dmg[0]) {
                    max_rebound_int_dmg[0] = int0;
                    rebound_damage_facing_dir[0] =
                        combat_clank_damage_facing_dir(batch, p0_idx, p1_idx);
                  }
                }
              }

              if (p1_side_confirms_clank) {
                const int int1 = combat_get_env_dmg(d1);
                if (int1 > max_int_dmg[1]) {
                  max_int_dmg[1] = int1;
                }
                if ((f1 & (uint16_t)MSL_HITBOX_FLAG_REBOUND) != 0) {
                  want_rebound_stop[1] = 1u;
                  if (int1 > max_rebound_int_dmg[1]) {
                    max_rebound_int_dmg[1] = int1;
                    rebound_damage_facing_dir[1] =
                        combat_clank_damage_facing_dir(batch, p1_idx, p0_idx);
                  }
                }
              }
              if (p1_side_confirms_clank) {
                break;
              }
            }
          }

          if (did_clank) {
            // Apply hitlag per fighter using each side's max int damage among clanking hitboxes.
            // Decomp: ftCommon_CalcHitlag, used by Fighter_ProcessHit_8006D1EC.
            // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
            const uint16_t m0 = batch->state.action_id[p0_idx];
            const uint16_t m1 = batch->state.action_id[p1_idx];
            if (max_int_dmg[0] > 0) {
              // Clank/ReboundStop uses the collision-produced `dmg.int_value` from
              // ftColl_8007699C. The electric element has a separate clank-SFX RNG path
              // (ftColl_800784B4); it does not install the BODY hitlag vibrate multiplier
              // (`x1960`) that Fighter_ProcessHit applies for damaging BODY contacts.
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007699C,ftColl_800784B4}
              // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
              const uint16_t hl0 = combat_calc_hitlag_frames(c, max_int_dmg[0], m0, 1.0f);
              if (hl0 > batch->state.hitlag[p0_idx]) {
                batch->state.hitlag[p0_idx] = hl0;
                combat_state_flags_set_is_hitlag(batch, p0_idx, hl0);
              }
            }
            if (max_int_dmg[1] > 0) {
              const uint16_t hl1 = combat_calc_hitlag_frames(c, max_int_dmg[1], m1, 1.0f);
              if (hl1 > batch->state.hitlag[p1_idx]) {
                batch->state.hitlag[p1_idx] = hl1;
                combat_state_flags_set_is_hitlag(batch, p1_idx, hl1);
              }
            }

            // ReboundStop transitions for hitboxes that request rebound on clank.
            if (want_rebound_stop[0]) {
              if (max_rebound_int_dmg[0] > 0) {
                batch->state.rebound_ground_accel_2[p0_idx] =
                    combat_rebound_ground_accel_2_from_int_dmg(
                        batch, c, p0_idx, max_rebound_int_dmg[0], rebound_damage_facing_dir[0]);
                batch->state.rebound_anim_rate_fp_q16_16[p0_idx] =
                    combat_rebound_anim_rate_from_int_dmg(batch, c, p0_idx, max_rebound_int_dmg[0]);
              }
              // ReboundStop entry is post-physics in this simulator's collision pass. Decomp
              // ftCo_80099D9C writes `mv.co.rebound.x0` through ftCommon_800804A0, i.e. the
              // transient xE8_ground_accel_2 lane consumed by the next Fighter_procUpdate, not the
              // already-reported current-frame ground velocity.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::ftCo_80099D9C
              // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804A0
              batch->state.action_id[p0_idx] = (uint16_t)MSL_ACT_REBOUND_STOP;
              batch->state.animation_index[p0_idx] = 0xFFFFFFFFu;
              msl_anim_timebase_enter(batch, p0_idx, 0.0f, 1.0f);
              // ReboundStop is suite-observed with no submotion (animation_index=-1, action_frame=-1).
              // Collision ownership enters ReboundStop before the shared anim pass that would
              // otherwise advance the newly-entered timebase, so preserve the destination snapshot
              // shape until ReboundStop_Anim consumes into Rebound on the first !hitlag callback.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
              //   ftCo_80099D9C,ftCo_ReboundStop_Anim,ftCo_80099E44}
              msl_anim_timebase_seed(batch, p0_idx, -1.0f, 1.0f);
            }
            if (want_rebound_stop[1]) {
              if (max_rebound_int_dmg[1] > 0) {
                batch->state.rebound_ground_accel_2[p1_idx] =
                    combat_rebound_ground_accel_2_from_int_dmg(
                        batch, c, p1_idx, max_rebound_int_dmg[1], rebound_damage_facing_dir[1]);
                batch->state.rebound_anim_rate_fp_q16_16[p1_idx] =
                    combat_rebound_anim_rate_from_int_dmg(batch, c, p1_idx, max_rebound_int_dmg[1]);
              }
              batch->state.action_id[p1_idx] = (uint16_t)MSL_ACT_REBOUND_STOP;
              batch->state.animation_index[p1_idx] = 0xFFFFFFFFu;
              msl_anim_timebase_enter(batch, p1_idx, 0.0f, 1.0f);
              // ReboundStop is suite-observed with no submotion (animation_index=-1, action_frame=-1).
              // Collision ownership enters ReboundStop before the shared anim pass that would
              // otherwise advance the newly-entered timebase, so preserve the destination snapshot
              // shape until ReboundStop_Anim consumes into Rebound on the first !hitlag callback.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Rebound.c::{
              //   ftCo_80099D9C,ftCo_ReboundStop_Anim,ftCo_80099E44}
              msl_anim_timebase_seed(batch, p1_idx, -1.0f, 1.0f);
            }
          }
        }
      }
      const uint16_t defender_iid = batch->state.instance_id[d_idx];

      // Hitlag gating (attacker-owned):
      // - Decomp collision pass ftColl_80078C70 does not gate BODY/SHIELD candidate evaluation on
      //   victim hitlag state; each fighter is processed independently as collision owner.
      // - Keep only the attacker-side gate in this simulator lane.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
      if (batch->state.hitlag_started_frame[a_idx]) {
        continue;
      }

      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      // GuardReflect no-submotion entry (`action_frame<0`, sentinel anim index) is the ambiguous
      // ordering frame between ftCo_8009388C clear and ftCo_80092450 recreate.
      // Keep shield-active ownership from the live ShieldDesc radius (x221B_b0 lane) and only
      // suppress ShieldDesc envelope expansion lanes on guard-origin entry snapshots. Direct
      // ftCo_80091A4C powershield entry (including Landing_IASA) calls ftCo_80092450 before
      // creating ReflectDesc, so its same-frame fighter-vs-fighter shield path still has
      // ShieldDesc.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_80093694,ftCo_8009388C,ftCo_80093A50,ftCo_80092450}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
      const MslGuardReflectOwner guard_reflect_owner =
          msl_guard_reflect_owner_resolve(batch, d_idx);
      const uint8_t guard_reflect_entry_no_submotion =
          guard_reflect_owner.shield_entry_no_submotion;
      const uint8_t shield_active = (shr > 0.0f) ? 1u : 0u;
      const uint8_t shield_desc_envelope_ready = !guard_reflect_entry_no_submotion;
      // ShieldDesc sweep extent is owned by the live HitCapsule x58->x4C segment. Do not widen
      // final-x14/no-submotion GuardReflect fighter-vs-fighter shield admission without an
      // enable-edge capsule or explicit teacher-forced accepted shield-contact provenance: the
      // broad bridge over-admits near-rim shine BODY rows as GuardSetOff.
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
      const uint8_t shield_extent_bridge_active = 0u;
      const uint8_t guard_reflect_reflectdesc_only = guard_reflect_owner.reflectdesc_only;

      // Combat collision consumes world-space hitbox/hurtcap primitives derived from:
      // - pose matrices driven by fp->cur_anim_frame (prio 1, ftAnim_8006EBA4), and
      // - post-Phys fighter translation (prio 4), applied to the model at prio 6/9 before
      //   the prio 13 fighter-vs-fighter collision pass.
      //
      // In decomp proc order, collision uses post-integration translation; do not shift
      // primitives by (prev_pos - pos) here.

      // Shield precedence (non-inert): if a HitCapsule intersects the defender shield bubble and
      // `element != HitElement_Inert`, resolve the shield hit (HP depletion, GuardSetOff, hitlag)
      // and do not apply BODY selection for that HitCapsule. Source then continues to later
      // HitCapsules rather than suppressing the whole attacker->defender pair.
      //
      // Decomp pointer (GALE01): refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70 uses
      // `lbColl_80007BCC(..., &this_fp->shield_hit, ...)` as the overlap test and splits on
      // `hit->element`:
      // - non-inert calls `ftColl_80076CBC(...)` (shield hit handling),
      // - inert sets `victim_fp->x221C_b5 = true` (detection hitbox touching shield bubble) and
      //   does NOT enter the normal shield-hit effects path.
      if (shield_active) {
        // Decomp (GALE01): shield collision accumulates max int damage for hitlag as:
        // - attacker: `fp0->dmg.x1924 = max(fp0->dmg.x1924, getEnvDmg(hit0->damage))`
        // - defender: `fp1->x19A4 = max(fp1->x19A4, getEnvDmg(hit0->damage))`
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
        //
        // Fighter_ProcessHit then computes hitlag from these max int damage values.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        //
        // The sim still applies one GuardSetOff entry per pair per frame, but the collision
        // scratch/result state below mirrors ftColl_80076CBC's source-owned accumulators:
        // - x19A4 / hitlag scalar uses the max int damage over accepted shield contacts;
        // - x19A0 / shieldDamageTaken accumulates every accepted hitbox's
        //   max(0, int_dmg + hitbox_shield_damage);
        // - accepted hit groups are registered before BODY selection so later same-group BODY
        //   candidates are suppressed by lbColl_8000ACFC.
        int max_int_dmg = 0;
        int sel_int_dmg = 0;
        uint8_t sel_element = 0u;
        uint8_t sel_hit_group = 0;
        uint8_t sel_rehit_frames = 0;
        int shield_damage_taken_sum = 0;
        uint8_t shield_contact_count = 0u;
        uint8_t shield_seed_accept_count = 0u;
        uint8_t zero_shield_damage_contact_count = 0u;
        uint8_t zero_shield_damage_x19a0_match_count = 0u;
        uint8_t shield_contact_groups[MSL_MAX_HITBOXES] = {0};
        uint8_t shield_contact_rehit_frames[MSL_MAX_HITBOXES] = {0};
        uint8_t shield_contact_group_seen[8] = {0};
        for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
          const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
          if (!batch->state.hitbox_enabled[hb_i]) {
            continue;
          }
          // Shield candidates exclude Catch element capsules (Falcon Dive's persistent grab
          // bubbles must not poke shields when the catch mask rejects the victim). Unlike the
          // BODY predicate, Inert stays admitted here: the Raptor Boost inert-detect flag is set
          // from the shield-overlap path.
          // refs/melee/src/melee/ft/ftcoll.c (shield candidate: element != Catch only)
          if (batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_CATCH) {
            continue;
          }
          if (clank_skip_hb[attacker][defender][hb_id]) {
            continue;
          }

          const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
          const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1 : 0;
          if (defender_on_ground) {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
              continue;
            }
          } else {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
              continue;
            }
          }

          const float hx = batch->state.hitbox_x[hb_i];
          const float hy = batch->state.hitbox_y[hb_i];
          const float hz = batch->state.hitbox_z[hb_i];
          const float hr = batch->state.hitbox_radius[hb_i];

          // Rehit suppression (hitlists): decomp splits "shield overlap geometry" from "hit
          // acceptance gating".
          //
          // - Geometry only (no hitlist logic inside): lbColl_80007BCC(...)
          //   refs/melee/src/melee/ft/ftcoll.c (shield path around lbColl_80007BCC)
          //   refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
          // - Rehit/hitlist gate outside geometry: lbColl_8000ACFC(victim_fp, hitcapsule)
          //   refs/melee/src/melee/ft/ftcoll.c (eligible hitcapsule predicate includes lbColl_8000ACFC(...)==0)
          //   refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
          //
          // Mirror that ordering here: gate before the shield sphere overlap test.
          const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
          if (shield_contact_group_seen[hit_group]) {
            // ftColl_80076CBC calls ftColl_80076808 immediately for an accepted shield contact,
            // registering the victim across every active HitCapsule with the same hit_group.
            // Later same-group shield/body candidates are therefore rejected by lbColl_8000ACFC
            // during this same ftColl_80078C70 owner pass.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80076808}
            // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
            continue;
          }
          // Teacher-forced accepted ShieldDesc lane. A value of 2 is derived only when the replay
          // proves the full shield-hit admission result at t+1 (GuardSetOff plus hitlag), not just
          // the geometric bubble overlap. That proof includes the hidden lbColl_8000ACFC
          // victims_1 decision which is otherwise approximated by the dense group hitlist seed.
          //
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
          // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
          const uint8_t shield_seed_kind =
              (msl_shielddesc_attackairb_guard_lower_bound_seed_owner(batch, bi, attacker,
                                                                      defender) != 0u)
                  ? 0u
                  : batch->state.combat_shield_contact_hb_kind[idx_hitbox_victim(bi, attacker,
                                                                                 hb_id, defender)];
          uint8_t allows =
              hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid);
          if (!allows && shield_seed_kind == 2u) {
            allows = 1u;
          } else if (!allows &&
                     combat_single_create_grounded_guardreflect_enable_edge_allows_shield(
                         batch, a_idx, d_idx, hb_i)) {
            allows = 1u;
          } else if (!allows && combat_specialhi_frozen_guard_dense_seed_allows_live_shield(
                                    batch, c, bi, attacker, defender, hb_id, a_idx, d_idx,
                                    defender_iid, shield_seed_kind, hx, hy, hz, hr, shx, shy, shz,
                                    shr, shield_desc_envelope_ready, shield_extent_bridge_active,
                                    guard_reflect_reflectdesc_only)) {
            allows = 1u;
          }
          if (!allows) {
            continue;
          }

          // ftColl_80078C70 processes shield and BODY for each HitCapsule before advancing to the
          // next HitCapsule. A lower-index BODY hit can therefore commit before a later
          // shield-overlap candidate. This source-order helper keeps the runtime shield pass from
          // using the older pair-wide shield-before-BODY shortcut for that case.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
          if (shield_seed_kind == 0u &&
              combat_source_order_earlier_body_hitcapsule_precedes_shield(
                  batch, c, bi, attacker, defender, hb_id, defender_iid, shx, shy, shz, shr,
                  shield_desc_envelope_ready, shield_extent_bridge_active,
                  guard_reflect_reflectdesc_only, clank_skip_hb)) {
            continue;
          }
          if (shield_seed_kind == 0u &&
              combat_defer_late_slot_same_frame_speciallw_guardon_shield_hit(batch, a_idx, d_idx,
                                                                             attacker, defender)) {
            continue;
          }

          // Teacher-forced ShieldDesc/narrowphase seed lane:
          // - ftColl_80078C70 consumes the accepted lbColl_80007BCC shield-contact result before
          //   deciding between shield and BODY paths.
          // - One-step reseed can know the accepted/missed contact from replay-visible
          //   GuardSetOff/hitlag or stable shield rows even when the hidden ShieldDesc sweep state
          //   and victims_1 carry are not reconstructible from visible pose alone.
          // - Normal rollouts keep this zero and use live geometry.
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
          // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_8000ACFC}
          if (shield_seed_kind == 1u) {
            continue;
          }
          const int16_t attackair_second_create_frame =
              move_tables_attackair_second_create_hitbox_frame(batch->state.char_id[a_idx],
                                                               batch->state.action_id[a_idx]);
          const int16_t attackair_late_limb_x18_callback_frame =
              (attackair_second_create_frame >= 0) ? (int16_t)(attackair_second_create_frame + 4)
                                                   : -1;
          if (shield_seed_kind == 0u && hb_id == 1 && attackair_second_create_frame >= 0 &&
              batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_N &&
              msl_motion_state_has_motion_flag(batch->state.char_id[a_idx],
                                               batch->state.action_id[a_idx],
                                               MSL_MOTION_FLAG_SKIP_HIT) &&
              batch->state.action_frame[a_idx] == attackair_late_limb_x18_callback_frame &&
              combat_guard_reflect_final_x14_live_x18_blocks_body(batch, d_idx)) {
            // AttackAirN SkipHit second-create limb / final-x18 GuardReflect handoff:
            // - `ftCo_GuardReflect_Anim -> ftCo_80093BC0` has already consumed the shorter x14
            //   reflect lane, but x18/x221C_b2 is still the powershield-active owner until the
            //   next callback. BODY already honors this owner; sustained AttackAirN additionally
            //   carries the source SkipHit/HitCapsule victim phase across the first collision
            //   callback where the late limb slot reaches the final-x18 ShieldDesc boundary. Fox/Falco
            //   AttackAirN's generated MSLFTSC1 script rewrites the opening frame-4 capsules at frame
            //   8; the retained boundary is the matching frame-12 late-limb callback, not the full
            //   late-hit lifetime. Keep this per-HitCapsule: other AttackAirN slots with live
            //   ShieldDesc overlap must still reach GuardSetOff in the same ftColl_80078C70 pass.
            // - When x18 reaches the final seed tick, the next callback clears the owner before
            //   collision and the ordinary ShieldDesc handoff remains eligible.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
            //   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092F2C}
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
            // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_AttackAirN
            // data/scripts/{fox,falco}.bin (MSLFTSC1 AttackAirN second create_hitbox phase)
            continue;
          }
          if (shield_seed_kind == 0u && guard_reflect_entry_no_submotion) {
            // Guard-origin GuardReflect no-submotion rows with x14 still active expose ReflectDesc,
            // not a normal ShieldDesc HitShield accept. Keep BODY/clank candidates live, but do not
            // let the simulator's proxy shield sphere enter GuardSetOff before ftCo_80093BC0 has
            // expired the x14 reflect window. Locomotion/Landing powershield entries are excluded
            // by guard_reflect_entry_no_submotion and retain their source-backed same-frame
            // ShieldDesc boundary.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
            //   ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
            continue;
          }
          uint8_t overlaps_shield = 0u;
          float shield_overlap_margin = 0.0f;
          if (shield_seed_kind == 2u) {
            overlaps_shield = 1u;
          } else if (!guard_reflect_reflectdesc_only) {
            overlaps_shield = combat_shield_overlap_ftcoll_80007bcc(
                batch, bi, attacker, defender, hb_id, hx, hy, hz, hr, shx, shy, shz, shr,
                /*shield_desc_radius=*/1.0f, batch->state.fighter_scale_y[d_idx],
                shield_desc_envelope_ready, shield_extent_bridge_active, &shield_overlap_margin);
          }
          if (!overlaps_shield && combat_pstadium_guardreflect_attackairlw_extent_accepts_shield(
                                      batch, bi, a_idx, d_idx, hb_i, shield_overlap_margin)) {
            overlaps_shield = 1u;
          }
          if (!overlaps_shield) {
            continue;
          }
          if (shield_seed_kind == 0u && hb_id == 0 &&
              msl_shielddesc_attackairb_guard_lower_bound_seed_owner(batch, bi, attacker,
                                                                     defender) != 0u &&
              fabsf(hy - shy) > shr) {
            const size_t hb2_i = idx_hitbox(bi, attacker, 2);
            uint8_t weak_hb2_overlaps = 0u;
            if (batch->state.hitbox_enabled[hb2_i] != 0u &&
                batch->state.hitbox_damage[hb2_i] == 9.0f) {
              float hb2_overlap_margin = 0.0f;
              weak_hb2_overlaps = combat_shield_overlap_ftcoll_80007bcc(
                  batch, bi, attacker, defender, 2, batch->state.hitbox_x[hb2_i],
                  batch->state.hitbox_y[hb2_i], batch->state.hitbox_z[hb2_i],
                  batch->state.hitbox_radius[hb2_i], shx, shy, shz, shr,
                  /*shield_desc_radius=*/1.0f, batch->state.fighter_scale_y[d_idx],
                  shield_desc_envelope_ready, shield_extent_bridge_active, &hb2_overlap_margin);
              (void)hb2_overlap_margin;
            }
            if (weak_hb2_overlaps != 0u) {
              // Strong BAir hb0 / weak hb2 ShieldDesc packet ordering:
              // Existing seed lanes can mark the whole BAir packet as shield-accepted. Source
              // lbColl_80007BCC still checks the current HitCapsule packet. Runtime does not yet
              // carry the full source ShieldDesc matrix packet here, so this uses the live packet's
              // axis-aligned vertical radius as the bounded lower-dimensional guard: only rows
              // where strong-root hb0 is outside that radius and weak hb2 independently overlaps
              // can hand shield ownership to hb2. If hb0 is inside the live radius, rows such as
              // DCC keep hb0 as the source owner even with the same all-slot lower-bound seed.
              // TODO: replace this boundary with the exact ftColl/lbColl ShieldDesc matrix packet
              // once runtime carries that packet generically.
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
              // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
              // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
              continue;
            }
          }
          if (shield_seed_kind == 0u &&
              combat_guardon_raise_attacks4_age_rejects_shield(batch, c, bi, a_idx, d_idx, hb_i)) {
            continue;
          }
          if (shield_seed_kind == 0u &&
              combat_pstadium_guardon_x44_reduced_proxy_rejects_shield(
                  batch, bi, a_idx, d_idx, (uint8_t)hb_id, hb_i, shield_overlap_margin)) {
            continue;
          }
          if (shield_seed_kind == 0u &&
              combat_guard_reflect_active_x14_reflectdesc_blocks_hitshield(batch, d_idx,
                                                                           shield_overlap_margin)) {
            continue;
          }
          if (shield_seed_kind == 0u &&
              combat_guard_reflect_active_x14_rejects_strong_attackairb_shield(batch, a_idx, d_idx,
                                                                               hb_i)) {
            continue;
          }
          if (shield_seed_kind == 0u &&
              batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
              combat_guard_reflect_final_x14_live_x18_blocks_body(batch, d_idx) &&
              batch->state.facing[a_idx] == batch->state.facing[d_idx]) {
            // GuardReflect final-x18 same-facing ShieldDesc side owner:
            // - At the x14-expired/x18-live callback, direct no-submotion GuardReflect still owns
            //   the powershield-active side lane. Same-facing AttackAirB rows on this boundary can
            //   expose a broad shield sphere overlap while the source ShieldDesc side test remains
            //   a miss until the next callback clears x18.
            // - Opposite-facing final-x18 AttackAirB controls with accepted ShieldDesc provenance
            //   remain on the normal GuardSetOff path; this is a side/pose discriminator, not a
            //   character-pair or replay-row branch.
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
            //   ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092F2C}
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
            continue;
          }
          if (shield_seed_kind == 0u &&
              combat_guard_reflect_x18_expiry_rejects_strong_attackairn_hb1_shield(
                  batch, a_idx, d_idx, hb_i, (uint8_t)hb_id)) {
            continue;
          }
          const uint8_t element = batch->state.hitbox_element[hb_i];
          if (element == (uint8_t)MSL_HIT_ELEMENT_INERT) {
            // Slippi post-frame bit 0x221C:0x04 (GALE01): detection hitbox touching shield bubble.
            // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
            //
            // Decomp (GALE01) sets this on inert (HitElement_Inert) shield overlaps only:
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
            //   `victim_fp->x221C_b5 = true;`
            //
            // Set on the victim/defender (the fighter whose shield bubble was overlapped).
            const size_t d_flags_i =
                d_idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
            batch->state.state_flags[d_flags_i] |=
                (uint8_t)MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD;
            // The same source branch writes the ATTACKER's fp->unk_gobj, so detect-driven
            // specials (Raptor Boost) connect on shielding opponents too.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70 (HitElement_Inert shield branch)
            falcon_specials_on_inert_shield_contact(batch, a_idx);

            // Decomp does not take the normal shield-hit path for inert hitboxes:
            // `if (hit->element != HitElement_Inert) ftColl_80076CBC(...); else victim_fp->x221C_b5=true`.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
            //
            // So this overlap should NOT apply the normal "damaging block" mutations:
            // - no shield HP depletion (Fighter_ProcessHit_8006D1EC),
            // - no GuardSetOff entry,
            // - no hitlag application.
            continue;
          }

          // Source consumes the HitCapsule.damage value built at create/set-damage time. Do not
          // recompute stale damage from the live stale queue here: same-attack shield/body contacts
          // can mutate the queue after this HitCapsule's damage was frozen.
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_80076CBC}
          // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
          float hdmg = combat_hitcapsule_collision_damage(batch, a_idx, hb_i);
          if (!(hdmg > 0.0f)) {
            continue;
          }

          // Decomp (GALE01) uses getEnvDmg(hit0->damage) to compute the int damage used for shield
          // interactions and hitlag inputs.
          // refs/melee/src/melee/ft/ftcoll.c::getEnvDmg and ftColl_80076CBC
          const int int_dmg = combat_get_env_dmg(hdmg);
          const int x19a4_int_dmg =
              combat_guard_setoff_x19a4_int_damage(batch, a_idx, d_idx, hb_i, int_dmg);
          if (x19a4_int_dmg > max_int_dmg) {
            max_int_dmg = x19a4_int_dmg;
          }
          if (shield_seed_kind == 2u && shield_seed_accept_count < (uint8_t)MSL_MAX_HITBOXES) {
            shield_seed_accept_count++;
          }
          int contact_shield_damage = int_dmg + (int)batch->state.hitbox_shield_damage[hb_i];
          if (contact_shield_damage < 0) {
            contact_shield_damage = 0;
          }
          shield_damage_taken_sum += contact_shield_damage;
          if (batch->state.hitbox_shield_damage[hb_i] == 0 &&
              zero_shield_damage_contact_count < (uint8_t)MSL_MAX_HITBOXES) {
            zero_shield_damage_contact_count++;
            if (contact_shield_damage == (int)batch->state.combat_shield_damage_taken[d_idx]) {
              zero_shield_damage_x19a0_match_count++;
            }
          }
          shield_contact_group_seen[hit_group] = 1u;

          if (sel_int_dmg == 0) {
            sel_int_dmg = int_dmg;
            sel_element = element;
            sel_hit_group = hit_group;
            sel_rehit_frames = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
          }
          if (shield_contact_count < (uint8_t)MSL_MAX_HITBOXES) {
            shield_contact_groups[shield_contact_count] = hit_group;
            shield_contact_rehit_frames[shield_contact_count] =
                hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
            shield_contact_count++;
          }
          v1_group_registered_this_pass[attacker][defender][hit_group] = 1u;
        }

        if (sel_int_dmg > 0) {
          int attacker_max_int_dmg = max_int_dmg;
          const uint8_t lower_bound_attackairb_guard_seed =
              msl_shielddesc_attackairb_guard_lower_bound_seed_owner(batch, bi, attacker, defender);
          const uint8_t lower_bound_attackairb_guardreflect_x19a4_seed =
              msl_shielddesc_attackairb_guardreflect_x19a4_lower_bound_seed_owner(
                  batch, bi, attacker, defender);
          const uint8_t no_submotion_zero_shield_damage_x19a0_seed =
              msl_shielddesc_no_submotion_zero_shield_damage_x19a0_seed_shape(batch, bi, attacker,
                                                                              defender);
          const uint8_t no_submotion_single_contact_x19a0_owner =
              (uint8_t)(no_submotion_zero_shield_damage_x19a0_seed != 0u &&
                        shield_contact_count == 1u && zero_shield_damage_contact_count == 1u &&
                        zero_shield_damage_x19a0_match_count == 1u &&
                        shield_damage_taken_sum ==
                            (int)batch->state.combat_shield_damage_taken[d_idx]);
          const uint8_t guard_no_command_x19a4_attacker_seed =
              msl_shielddesc_guard_no_command_x19a4_attacker_seed_owner(batch, bi, attacker,
                                                                        defender);
          const uint8_t guard_zero_x19a0_x19a4_attacker_seed =
              msl_shielddesc_guard_zero_x19a0_x19a4_attacker_seed_owner(batch, bi, attacker,
                                                                        defender);
          const uint16_t defender_action = batch->state.action_id[d_idx];
          const uint8_t no_submotion_guardon_reflect_x19a4_seed_shape =
              (uint8_t)((defender_action == (uint16_t)MSL_ACT_GUARD_ON ||
                         defender_action == (uint16_t)MSL_ACT_GUARD_REFLECT) &&
                        batch->state.action_frame[d_idx] < 0 &&
                        batch->state.animation_index[d_idx] == UINT32_MAX &&
                        batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] == 0u);
          const uint8_t frame_start_flags_2218 = batch->state.state_flags_2218_frame_start[d_idx];
          const uint8_t settled_guard_b2_current_x19a4_seed_shape =
              (uint8_t)(defender_action == (uint16_t)MSL_ACT_GUARD &&
                        batch->state.action_frame[d_idx] < 0 &&
                        batch->state.animation_index[d_idx] == UINT32_MAX &&
                        batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] == 0u &&
                        frame_start_flags_2218 == (uint8_t)MSL_STATE_FLAG_2218_B2);
          const uint8_t seeded_x19a4 = (lower_bound_attackairb_guard_seed != 0u ||
                                        lower_bound_attackairb_guardreflect_x19a4_seed != 0u)
                                           ? 0u
                                           : batch->state.combat_shield_hit_int_damage[d_idx];
          const uint8_t exact_seeded_current_x19a4_packet =
              (seeded_x19a4 != 0u && shield_seed_accept_count != 0u &&
               shield_seed_accept_count == shield_contact_count &&
               (no_submotion_guardon_reflect_x19a4_seed_shape != 0u ||
                settled_guard_b2_current_x19a4_seed_shape != 0u) &&
               lower_bound_attackairb_guard_seed == 0u &&
               lower_bound_attackairb_guardreflect_x19a4_seed == 0u &&
               no_submotion_single_contact_x19a0_owner == 0u)
                  ? 1u
                  : 0u;
          const uint8_t guardon_x19a4_from_x19a0 =
              (no_submotion_single_contact_x19a0_owner != 0u)
                  ? batch->state.combat_shield_damage_taken[d_idx]
                  : 0u;
          if (guardon_x19a4_from_x19a0 != 0u) {
            // No-submotion GuardOn lower-bound reconstruction:
            // this source shape has replay-proven ShieldDesc admission and an x19A4 seed that can
            // be only a hitlag-derived lower bound. x19A0 is an accumulator, so consume it as the
            // x19A4 source-damage lane only when this callback accepts exactly one
            // zero-shield-damage contact whose collision-time damage contribution equals x19A0.
            // Multi-contact accumulator rows stay on the normal max-int-damage owner.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_8007ABD0}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
            max_int_dmg = (int)guardon_x19a4_from_x19a0;
            if (no_submotion_single_contact_x19a0_owner != 0u) {
              attacker_max_int_dmg = (int)guardon_x19a4_from_x19a0;
            }
          } else if (seeded_x19a4 != 0u) {
            // Teacher-forced shield-hit max-damage lane:
            // - ftColl_80076CBC writes defender fp->x19A4 as the max getEnvDmg(hit0->damage)
            //   across accepted shield contacts before ftCo_80092F2C consumes it for GuardSetOff
            //   hitlag and shieldstun anim rate.
            // - When reseed supplies a hidden ShieldDesc contact result but not exact capsule
            //   ordering, runtime geometry may over-include active slots. Keep the accepted
            //   shield-hit entry, but consume the explicit current-frame x19A4 damage scalar if
            //   preprocessing recovered it. shieldDamageTaken keeps the selected hit's damage;
            //   this hidden lane is only the GuardSetOff hitlag/shieldstun scalar.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
            max_int_dmg = (int)seeded_x19a4;
            if (exact_seeded_current_x19a4_packet != 0u) {
              // Current-frame ShieldDesc seed packet:
              // the seed bridge proves every accepted live ShieldDesc contact for this
              // attacker/defender pair. In that source shape x19A4 and attacker dmg.x1924 share
              // the same max getEnvDmg packet, while x19A0 remains a separate accumulator consumed
              // below for shield HP. Do not require x19A0==0 here; multi-contact x19A0 rows can
              // still source attacker hitlag from x19A4 without treating x19A0 as selected damage.
              // Settled Guard rows enter this packet owner only on the raw fp+0x2218_b2 command
              // lane with no other fp+0x2218 owners; the no-command helper above keeps broader
              // settled Guard seed packets off this command-owned path.
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,ftColl_80078C70}
              // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
              // bindings/msl_preprocess_native.c::msl_derive_shield_contact_seed_bridge_py
              attacker_max_int_dmg = (int)seeded_x19a4;
            } else if (guard_no_command_x19a4_attacker_seed != 0u ||
                       guard_zero_x19a0_x19a4_attacker_seed != 0u) {
              // Settled no-command Guard shield-hit packet:
              // with raw fp+0x2218_b2 clear, the replay-proven ShieldDesc packet owns attacker
              // hitlag as well as defender x19A4. One-step live geometry can otherwise choose a
              // sibling HitCapsule or miss the seed-only ShieldDesc packet consumed by
              // Fighter_ProcessHit. The zero-x19A0 variant still consumes only x19A4 here; x19A0
              // remains an accumulator and is not used as selected damage.
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
              // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
              attacker_max_int_dmg = (int)seeded_x19a4;
            } else if (combat_guardsetoff_carried_shield_packet_owner(batch, d_idx) != 0u &&
                       shield_seed_accept_count != 0u) {
              // Active GuardSetOff shield-hit carry:
              // ftCo_80093240/Fighter_ProcessHit consumes the carried x19A4/x19A0 shield-hit
              // packet from an already-active GuardSetOff callback. A one-step reseed can expose
              // current live HitCapsule proxies that are stronger or wider than that carried
              // packet; source does not replay a fresh attacker dmg.x1924 max from those proxies.
              // Keep attacker hitlag on the same carried x19A4 scalar as the source packet.
              // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80092F2C,ftCo_80093240}
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
              attacker_max_int_dmg = (int)seeded_x19a4;
            }
          }
          int tmp_dmg = shield_damage_taken_sum;
          const uint8_t seeded_x19a0 = (lower_bound_attackairb_guard_seed != 0u)
                                           ? 0u
                                           : batch->state.combat_shield_damage_taken[d_idx];
          if (seeded_x19a0 != 0u) {
            // Teacher-forced shield HP accumulator:
            // - ftColl_80076CBC accumulates `fp->x19A0_shieldDamageTaken` separately from x19A4.
            // - Fighter_ProcessHit later consumes x19A0 for shield HP depletion.
            // Use the explicit hidden x19A0 lane only for replay-proven accepted shield contacts;
            // normal rollouts leave it zero and consume the selected runtime contact.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
            // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
            tmp_dmg = (int)seeded_x19a0;
          } else if (exact_seeded_current_x19a4_packet != 0u &&
                     settled_guard_b2_current_x19a4_seed_shape != 0u &&
                     shield_damage_taken_sum > (int)seeded_x19a4) {
            // Settled Guard command-lane current packet:
            // raw fp+0x2218_b2 with no other fp+0x2218 owners can carry the source command-owned
            // current ShieldDesc packet even when x19A0 is not replay-recovered. In this narrow
            // lane x19A4 is the only bounded source damage scalar for both hitlag and shield HP;
            // broader no-command/current ShieldDesc packets keep live shieldDamageTaken because
            // x19A0 is the real accumulator and x19A4 is only the max-damage lane.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
            // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm (fp+0x2218 byte)
            tmp_dmg = (int)seeded_x19a4;
          }
          if (tmp_dmg < 0) {
            tmp_dmg = 0;
          }

          // Combat Mutations Pass 1 (SHIELD-only).
          //
          // Decomp (GALE01): shield hitlag + shieldstun duration use the max int damage over shield
          // overlaps for this frame (fp->dmg.x1924 / fp->x19A4), while shieldDamageTaken is accumulated
          // separately by collision.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
          // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
          combat_mutations_pass1_future_apply_shield_hit(batch, a_idx, d_idx, attacker_max_int_dmg,
                                                         max_int_dmg, tmp_dmg, a_motion_id,
                                                         sel_element);

          // Hitlist register: decomp hitlists store a `victim` pointer inside the HitCapsule, so
          // the victim identity is stable across motion-state changes (e.g. GuardSetOff entry).
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688 (HitVictim.victim)
          //
          // Our hitlist uses `instance_id` as a proxy for victim identity. A shield hit applies
          // GuardSetOff by calling the decomp-shaped Fighter_ChangeMotionState bundle
          // (msl_anim_timebase_enter()), which can bump `instance_id` on motion-state entry
          // (src/instance_id.c::instance_id_on_motion_state_change_ft_800895E0).
          //
          // Register using the post-mutation instance_id so that, in teacher-forced one-step
          // reseed, the hitlist identity key matches the victim identity that the reference
          // post-frame uses at t+1 (after the GuardSetOff transition), preventing spurious shield
          // re-hits / GuardSetOff re-entry on the next step.
          const uint16_t defender_iid_post = batch->state.instance_id[d_idx];
          // Decomp insertion type on shield hit path: ftColl_80076CBC calls ftColl_80076808(..., type=1, ...).
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
          if (shield_contact_count == 0u) {
            hitlist_register_fighter_group(batch, bi, attacker, sel_hit_group, defender,
                                           defender_iid_post, (int)MSL_LBCOLL_INSERT_FT_SHIELD,
                                           sel_rehit_frames);
          } else {
            for (uint8_t contact_i = 0u; contact_i < shield_contact_count; contact_i++) {
              hitlist_register_fighter_group(batch, bi, attacker, shield_contact_groups[contact_i],
                                             defender, defender_iid_post,
                                             (int)MSL_LBCOLL_INSERT_FT_SHIELD,
                                             shield_contact_rehit_frames[contact_i]);
            }
          }

          // Keep BODY candidates live for later HitCapsules. ftColl_80078C70 processes shield/BODY
          // per HitCapsule; ftColl_80076CBC does not terminate the attacker->defender pair loop.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
        }
      }

      uint8_t counter_desc_intercepted = 0u;
      if (marth_counter_intercepts_contact(batch, d_idx)) {
        for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
          const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
          if (!batch->state.hitbox_enabled[hb_i]) {
            continue;
          }
          if (combat_defer_late_slot_same_frame_speciallw_entry_hit(batch, bi, a_idx, d_idx,
                                                                    attacker, defender, hb_id)) {
            continue;
          }
          if (clank_skip_hb[attacker][defender][hb_id]) {
            continue;
          }

          const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
          const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1 : 0;
          if (defender_on_ground) {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
              continue;
            }
          } else {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
              continue;
            }
          }

          const float hdmg = batch->state.hitbox_damage[hb_i];
          if (!(hdmg > 0.0f) || combat_get_env_dmg(hdmg) <= 0) {
            continue;
          }

          const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
          const uint8_t rehit_frames =
              hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
          if (!hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid)) {
            continue;
          }
          if (!marth_counter_desc_overlaps_hitbox(batch, d_idx, hb_i)) {
            continue;
          }

          // Marth Counter is an AbsorbDesc/ShieldDesc owner installed by ftColl_8007B1B8, not
          // a BODY hurtcapsule owner. A valid descriptor-hitbox contact triggers even when no
          // BODY capsule overlaps the attack. Keep the same HitCapsule eligibility and hitlist
          // gates as ftColl_80078C70, but do not require the later BODY loop to select a hurtcap.
          // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::{
          //   ftMs_SpecialLw_Anim,ftMs_SpecialAirLw_Anim,ftMs_SpecialLw_80139140}
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80078C70}
          marth_counter_trigger(batch, a_idx, d_idx, hb_i, a_motion_id);
          if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_MS_SPECIAL_LW_HIT ||
              batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_MS_SPECIAL_AIR_LW_HIT) {
            const uint16_t post_counter_iid = batch->state.instance_id[d_idx];
            hitlist_register_fighter_group_v2(batch, bi, attacker, hit_group, defender,
                                              post_counter_iid, (int)MSL_LBCOLL_INSERT_FT_BODY,
                                              rehit_frames);
            counter_desc_intercepted = 1u;
            break;
          }
        }
      }
      if (counter_desc_intercepted) {
        continue;
      }

      uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      const MslHurtCap* defender_caps = NULL;
      uint16_t defender_cap_count_u16 = 0u;
      (void)hurtcaps_get(batch->state.char_id[d_idx], &defender_caps, &defender_cap_count_u16);
      const MslHurtCap* body_fallback_caps = NULL;
      uint16_t body_fallback_count_u16 = 0u;
      uint8_t use_guard_family_body_fallback_caps = 0u;
      const uint8_t guard_family_body_source =
          combat_guard_family_no_submotion_body_source_msid(batch, d_idx, NULL);
      if (guard_family_body_source &&
          (hurtcap_count == 0u ||
           combat_guardon_no_submotion_body_overrides_existing_hurtcaps(batch, d_idx))) {
        if (hurtcaps_get(batch->state.char_id[d_idx], &body_fallback_caps,
                         &body_fallback_count_u16) == 0 &&
            body_fallback_caps != NULL && body_fallback_count_u16 != 0u) {
          // No-submotion Guard-family BODY ownership is source-pose authoritative when Slippi
          // publishes no hurtcaps. Existing hurtcaps remain live for Guard/GuardDamage/explicit
          // debug geometry, except for active continuing GuardOn tilt/x10 rows where source
          // ftCo_800924C0/GuardOn_Anim has already advanced the GuardOn JObj chain and stale
          // previous-action seed hurtcaps must not win.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_800924C0,ftCo_GuardOn_Anim,ftCo_800925A4,ftCo_80091E78}
          // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
          use_guard_family_body_fallback_caps = 1u;
          hurtcap_count = body_fallback_count_u16 > (uint16_t)MSL_MAX_HURTCAPS
                              ? (uint8_t)MSL_MAX_HURTCAPS
                              : (uint8_t)body_fallback_count_u16;
        }
      }
      if (hurtcap_count == 0) {
        continue;
      }

      // Hit status / hurtbox-state eligibility gate (movescript-derived; opcode 26 + Slippi passthrough).
      //
      // Decomp pointers (GALE01):
      // - Hit status is driven by movescript opcode 26; decomp entry:
      //   refs/melee/src/melee/ft/ftaction.c::ftAction_80071A14
      // - Intangible blocks hurtcapsule collision checks entirely:
      //   refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868 (guards the hurtcapsule loop on `x1988 != 2 && x198C != 2`)
      // - Invincible (x1988/x198C != 0) still allows a "contact" that contributes to attacker-side max int damage
      //   (hitlag driver), but the defender percentTemp write is gated on vulnerability:
      //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8 (`if (fp1->x1988 == 0 && fp1->x198C == 0 ...) { inlineB2(...) }`)
      //
      // Policy (bounded v1, Slippi-seedable):
      // - state==2 ("intangible"): no BODY contacts selected.
      // - state==1 ("invincible"): allow contact selection but suppress defender percent/KB/hitstun writes (attacker hitlag only).
      const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
      uint8_t hurt_state = batch->state.hurtbox_state[d_idx];
      if (hit_status > hurt_state) {
        hurt_state = hit_status;
      }
      if (hurt_state == 2u) {
        continue;
      }
      const uint8_t defender_no_damage = (hurt_state != 0u) ? 1u : 0u;

      // BODY contacts (pass 1): source order is HitCapsule id, then first overlapping hurt capsule.
      // A BODY contact does not terminate the attacker->defender pair; ftColl_80078C70 breaks the
      // hurt-capsule loop for that HitCapsule, registers the victim ring through ftColl_80076ED8,
      // and then advances to the next HitCapsule. Distinct hit_groups can therefore accumulate
      // percent/max-hitlag in the same Fighter_ProcessHit frame, while same-group followups are
      // rejected by lbColl_8000ACFC.
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8,inlineB0,inlineB2}
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
        if (!batch->state.hitbox_enabled[hb_i]) {
          continue;
        }
        // ftColl's fighter BODY-hit candidate predicate excludes Catch/Inert element capsules —
        // those participate only in the catch-selection / inert-detect passes. Falcon Dive's
        // persistent grab bubbles carry damage 1 and otherwise fall through here when the catch
        // mask rejects the victim (witnessed: ledge-hanging CliffWait victim taking 1% instead
        // of nothing).
        // refs/melee/src/melee/ft/ftcoll.c (BODY candidate: element != Catch && != Inert)
        if (batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_CATCH ||
            batch->state.hitbox_element[hb_i] == (uint8_t)MSL_HIT_ELEMENT_INERT) {
          continue;
        }
        if (combat_defer_late_slot_same_frame_speciallw_entry_hit(batch, bi, a_idx, d_idx, attacker,
                                                                  defender, hb_id)) {
          continue;
        }
        if (clank_skip_hb[attacker][defender][hb_id]) {
          continue;
        }

        const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
        const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1 : 0;
        if (defender_on_ground) {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
            continue;
          }
        } else {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
            continue;
          }
        }

        const float hx = batch->state.hitbox_x[hb_i];
        const float hy = batch->state.hitbox_y[hb_i];
        const float hz = batch->state.hitbox_z[hb_i];
        const float hr = batch->state.hitbox_radius[hb_i];
        const float hdmg = batch->state.hitbox_damage[hb_i];

        // Hitlag mutations are only applied when the resolved damage is nonzero.
        //
        // Decomp pointer (GALE01):
        // - Fighter_ProcessHit_8006D1EC sets `fp->dmg.x195c_hitlag_frames` only under `if (bool1)`
        //   (where `bool1` is the resolved nonzero damage int for the collision path):
        //   refs/melee/src/melee/ft/fighter.c:2952-2978.
        //
        // In our BODY-only pass (no percent/KB yet), treat hitboxes with nonpositive extracted
        // damage as non-damaging contacts and do not select them for hitlag/attribution writes.
        if (!(hdmg > 0.0f)) {
          continue;
        }

        const int int_dmg = combat_get_env_dmg(hdmg);
        if (int_dmg <= 0) {
          continue;
        }
        // Shield precedence (BODY path): if the hitbox intersects the defender shield bubble, do
        // not apply BODY selection for this hitbox. The shield-hit selection above handles
        // (hitbox_id)-order shield resolution; this check is a conservative fallback.
        const uint8_t shield_seed_kind =
            batch->state
                .combat_shield_contact_hb_kind[idx_hitbox_victim(bi, attacker, hb_id, defender)];
        uint8_t body_blocked_by_shield = 0u;
        if (shield_active && shield_seed_kind == 2u) {
          body_blocked_by_shield = 1u;
        } else if (shield_active && shield_seed_kind != 1u && !guard_reflect_reflectdesc_only) {
          body_blocked_by_shield = combat_shield_overlap_ftcoll_80007bcc(
              batch, bi, attacker, defender, hb_id, hx, hy, hz, hr, shx, shy, shz, shr,
              /*shield_desc_radius=*/1.0f, batch->state.fighter_scale_y[d_idx],
              shield_desc_envelope_ready, shield_extent_bridge_active, NULL);
        }
        if (body_blocked_by_shield) {
          continue;
        }

        // Rehit suppression (hitlists): suppress repeats while the victim is present in the
        // hitbox's victims_1 list (HitCapsule victim rings shared across same hit_group).
        const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        const uint16_t attacker_iid = batch->state.instance_id[a_idx];
        const uint16_t expected_body_hitlag =
            combat_calc_hitlag_frames(c, int_dmg, a_motion_id, 1.0f);
        const uint16_t attacker_action = batch->state.action_id[a_idx];
        const uint16_t defender_action = batch->state.action_id[d_idx];
        const uint8_t attackairb_stale_owner_candidate =
            combat_attackairb_stale_owner_continuation_candidate(batch, a_idx, d_idx, hdmg,
                                                                 expected_body_hitlag) &&
                    batch->state.instance_hit_by[d_idx] != attacker_iid
                ? 1u
                : 0u;
        const uint8_t rehit_frames =
            hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        const uint8_t allows_v1 =
            hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid);
        if (allows_v1 && combat_attackairn_hb0_damageflytop_hitcapsule_owner(batch, bi, attacker,
                                                                             hb_id, defender)) {
          hitlist_register_fighter_hitbox(batch, bi, attacker, hb_id, defender, defender_iid,
                                          (int)MSL_LBCOLL_INSERT_FT_BODY, rehit_frames);
        }
        const uint8_t allows_v1_after_source_materialize =
            hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid);
        int16_t attackairb_late_carry_callback_frame = -1;
        if (!allows_v1_after_source_materialize && attackairb_stale_owner_candidate) {
          const int16_t attackairb_second_create_frame =
              move_tables_attackair_second_create_hitbox_frame(batch->state.char_id[a_idx],
                                                               attacker_action);
          attackairb_late_carry_callback_frame = (attackairb_second_create_frame >= 0)
                                                     ? (int16_t)(attackairb_second_create_frame + 4)
                                                     : -1;
        }
        // AttackAirB live HitCapsule carry:
        // - The stale-owner continuation path below must still allow the tip-log/phantom branch to
        //   run for replay-clock rows where the hidden victims_1 list is reconstructed from the
        //   SkipHit source owner.
        // - Full BODY suppression begins only after the generated late-create band has survived to
        //   the matching callback-age boundary. The earlier late callback remains damage-eligible
        //   when source geometry reaches a full BODY overlap.
        // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 x4_flags Ft_MF_SkipHit)
        // data/scripts/{fox,falco}.bin (MSLFTSC1 AttackAirB second create_hitbox phase)
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80006E58,lbColl_8000ACFC}
        const uint8_t attackairb_live_hitlist_suppresses_full_body =
            (!allows_v1_after_source_materialize && attackairb_stale_owner_candidate &&
             attackairb_late_carry_callback_frame >= 0 &&
             batch->state.action_frame[a_idx] >= attackairb_late_carry_callback_frame)
                ? 1u
                : 0u;
        // AttackAirN first-window contact boundary:
        // - Dense seed materialization can carry a same-source DamageFlyTop victim pointer from an
        //   older attacker action instance when per-HitCapsule seed data is unavailable.
        // - The first create_hitbox band must still admit its ordinary first BODY contact when
        //   geometry overlaps; after the generated second-create phase, the carried victims_1
        //   latch owns repeat suppression until ftColl_800768A0 copy/clear changes it.
        // data/scripts/{fox,falco}.bin (MSLFTSC1 AttackAirN create_hitbox phases)
        // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
        // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
        const uint8_t attackairn_first_window_ignores_dense_damageflytop_latch =
            (!allows_v1_after_source_materialize &&
             attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_N &&
             defender_action == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
             batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] != 0u &&
             msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker) &&
             !move_tables_attackair_second_create_hitbox_phase(
                 batch->state.char_id[a_idx], attacker_action, batch->state.anim_frame_f32[a_idx]))
                ? 1u
                : 0u;
        uint8_t dense_seed_suppresses_body = 0u;
        if (!allows_v1_after_source_materialize || batch->state.hitbox_enable_edge[hb_i] != 0u ||
            (msl_motion_state_fx_special_kind(batch->state.char_id[a_idx], attacker_action) ==
             (uint8_t)MSL_FX_KIND_SPECIAL_LW_START)) {
          dense_seed_suppresses_body = combat_enable_edge_dense_seed_suppresses_body(
              batch, bi, attacker, hb_id, defender, defender_iid, expected_body_hitlag);
        }
        uint8_t attackairb_dense_seed_suppresses_full_body = 0u;
        if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_B) {
          attackairb_dense_seed_suppresses_full_body =
              combat_attackairb_dense_seed_suppresses_full_body(
                  batch, bi, attacker, hb_id, defender, defender_iid, expected_body_hitlag);
        }
        const uint8_t pstadium_guardon_attackairb_x44_suppresses_body =
            combat_pstadium_guardon_attackairb_weak_x44_miss_suppresses_body(
                batch, bi, attacker, hb_id, a_idx, d_idx, hb_i);
        uint8_t attackairhi_create_edge_suppresses_full_body = 0u;
        if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_HI &&
            defender_action == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP) {
          attackairhi_create_edge_suppresses_full_body =
              combat_attackairhi_create_edge_damageflytop_suppresses_full_body(
                  batch, bi, attacker, hb_id, defender, expected_body_hitlag);
        }
        uint8_t attackairn_wait_dense_seed_suppresses_full_body = 0u;
        uint8_t attackairn_post_contact_dense_seed_suppresses_full_body = 0u;
        uint8_t attackairn_guard_dense_seed_suppresses_full_body = 0u;
        if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_N) {
          attackairn_wait_dense_seed_suppresses_full_body =
              combat_attackairn_wait_dense_seed_suppresses_full_body(batch, bi, attacker, hb_id,
                                                                     defender, defender_iid);
          attackairn_post_contact_dense_seed_suppresses_full_body =
              combat_attackairn_post_contact_dense_seed_suppresses_full_body(batch, bi, attacker,
                                                                             hb_id, defender);
          attackairn_guard_dense_seed_suppresses_full_body =
              combat_attackairn_guard_dense_seed_suppresses_full_body(batch, bi, attacker, hb_id,
                                                                      defender, defender_iid);
        }
        if (combat_pstadium_specialhi_launch_air_reflector_pre_turn_rejects_body(
                batch, bi, attacker, (uint8_t)hb_id, a_idx, d_idx)) {
          continue;
        }
        const uint8_t v1_group_seen_this_pass =
            v1_group_registered_this_pass[attacker][defender][hit_group];
        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
          float ax = 0.0f, ay = 0.0f, az = 0.0f;
          float bx = 0.0f, by = 0.0f, bz = 0.0f;
          float cr = 0.0f;
          if (use_guard_family_body_fallback_caps) {
            if (!combat_guard_family_body_hurtcap_world(batch, d_idx, &body_fallback_caps[cap_id],
                                                        cap_id, body_fallback_count_u16, &ax, &ay,
                                                        &az, &bx, &by, &bz, &cr)) {
              continue;
            }
          } else {
            if (!batch->state.hurtcap_enabled[cap_i]) {
              continue;
            }
            ax = batch->state.hurtcap_a_x[cap_i];
            ay = batch->state.hurtcap_a_y[cap_i];
            az = batch->state.hurtcap_a_z[cap_i];
            bx = batch->state.hurtcap_b_x[cap_i];
            by = batch->state.hurtcap_b_y[cap_i];
            bz = batch->state.hurtcap_b_z[cap_i];
            cr = batch->state.hurtcap_radius[cap_i];
          }
          float lbcoll_overlap_amount = 0.0f;
          uint8_t lbcoll_overlap_valid = 0u;
          uint8_t lbcoll_overlap_evaluated = 0u;
          float attackairb_overlap_amount = 0.0f;
          lbcoll_overlap_valid = combat_body_overlap_lbColl_80006E58_matrix_radius(
              batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az, bx, by,
              bz, 0u, &lbcoll_overlap_amount, &lbcoll_overlap_evaluated);
          // Decomp BODY narrowphase owner:
          // - ftColl_80078C70 calls lbColl_8000805C for fighter BODY admission.
          // - lbColl_8000805C forwards to lbColl_80006E58, whose matrix-derived scalar is the
          //   accept/reject predicate and writes HitCapsule.coll_distance.
          // - When extracted pose data provides the hurt bone matrix, run that owner directly; the
          //   simple world sphere/capsule test is only a missing-data fallback, not a prefilter.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
          // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
          const uint8_t baseline_overlaps =
              combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL);
          const uint8_t damageflytop_attackairhi_hb0_matrix_only_unreliable =
              (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
               batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_HI && hb_id == 0 &&
               batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] <= 3u)
                  ? 1u
                  : 0u;
          if (lbcoll_overlap_valid && !baseline_overlaps &&
              (!combat_body_matrix_positive_pose_reliable(batch, d_idx) ||
               damageflytop_attackairhi_hb0_matrix_only_unreliable)) {
            // Terminal DamageFlyTop / UpAir hb0 BODY candidate owner:
            // TBK:5248 shows source contact on UpAir hb1 while hb0 is a matrix-only false
            // positive from replay-reconstructed terminal DamageFlyTop JObj pose. Keep the
            // decomp-shaped matrix path for other DamageFlyTop contacts; only reject this
            // matrix-only lower UpAir capsule over a world-space miss.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
            // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_DamageFly_Coll
            lbcoll_overlap_valid = 0u;
            lbcoll_overlap_amount = 0.0f;
          }
          // The Guard-tilt matrix owner is currently reconstructed from extracted world matrices,
          // while HSD blends local JObj SRT state before matrix setup. Use that reconstructed matrix
          // as a positive owner/supplement for no-submotion tilted Guard, but do not let it reject
          // an already-valid BODY capsule admission.
          const uint8_t guard_tilt_live_body_pose =
              combat_guard_tilt_live_body_pose_owner(batch, d_idx);
          uint8_t overlaps = (lbcoll_overlap_evaluated && guard_tilt_live_body_pose == 0u)
                                 ? lbcoll_overlap_valid
                                 : (uint8_t)(baseline_overlaps || lbcoll_overlap_valid);
          if (!overlaps && !use_guard_family_body_fallback_caps &&
              combat_guard_tilt_live_body_z_owner_applies(batch, d_idx, shield_active)) {
            // Source owner: lbColl_8000805C recomputes hurtcap endpoints through lb_8000B1CC from
            // the same live angled-Guard JObj/AObj chain used by ShieldDesc. The simulator lacks a
            // serialized `x34_scale.z` lane for ftCommon_8007F804's optional transform path, so
            // no-transform no-submotion Guard rows consume the live guard collision depth already
            // produced by the pre-combat ShieldDesc/JObj owner. This is not keyed by replay
            // ShieldDesc miss provenance.
            // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000805C
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
            // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007F804
            const float guard_live_az = shz;
            const float guard_live_bz = shz;
            uint8_t guard_live_evaluated = 0u;
            float guard_live_overlap_amount = 0.0f;
            const uint8_t guard_live_overlap_valid =
                combat_body_overlap_lbColl_80006E58_matrix_radius(
                    batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay,
                    guard_live_az, bx, by, guard_live_bz, 0u, &guard_live_overlap_amount,
                    &guard_live_evaluated);
            const uint8_t guard_live_baseline_overlaps = combat_sphere_capsule_intersects(
                hx, hy, hz, hr, ax, ay, guard_live_az, bx, by, guard_live_bz, cr, NULL);
            overlaps =
                guard_live_evaluated ? guard_live_overlap_valid : guard_live_baseline_overlaps;
            if (overlaps) {
              az = guard_live_az;
              bz = guard_live_bz;
              lbcoll_overlap_valid = guard_live_evaluated ? guard_live_overlap_valid : overlaps;
              lbcoll_overlap_evaluated = guard_live_evaluated;
              lbcoll_overlap_amount = guard_live_overlap_amount;
            }
          }
          if (overlaps && combat_damageflylw_dynamic_high_part_rejects_body_contact(
                              batch, a_idx, d_idx, hb_id,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (overlaps && combat_attackairlw_damageflytop_fox_tail_rejects_body_contact(
                              batch, a_idx, d_idx, hb_id,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id],
                              expected_body_hitlag)) {
            overlaps = 0u;
          }
          if (overlaps && combat_attackairlw_strong_grounded_high_cap_rejects_lower_body_contact(
                              batch, bi, attacker, hb_id, defender, a_idx, d_idx,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id],
                              use_guard_family_body_fallback_caps ? NULL : defender_caps,
                              use_guard_family_body_fallback_caps ? 0u : defender_cap_count_u16, hx,
                              hy, hz, hr)) {
            overlaps = 0u;
          }
          if (overlaps && combat_attackairlw_strong_attacklw4_high_cap_sibling_rejects_body_contact(
                              batch, bi, attacker, hb_id, defender, a_idx, d_idx,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (overlaps && combat_attackairlw_attackdash_tail_allow_interrupt_rejects_body_contact(
                              batch, bi, attacker, hb_id, a_idx, d_idx,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (overlaps && combat_attackairlw_down_forward_tail_rejects_body_contact(
                              batch, bi, attacker, hb_id, defender, a_idx, d_idx,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id],
                              use_guard_family_body_fallback_caps ? NULL : defender_caps,
                              use_guard_family_body_fallback_caps ? 0u : defender_cap_count_u16, hx,
                              hy, hz, hr)) {
            overlaps = 0u;
          }
          if (overlaps && combat_attackairlw_late_high_cap_sibling_rejects_body_contact(
                              batch, bi, attacker, hb_id, defender,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (overlaps && combat_attackairhi_attackdash_tail_allow_interrupt_rejects_body_contact(
                              batch, bi, attacker, hb_id, a_idx, d_idx,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (overlaps && combat_damagefly_terminal_state_blocks_enable_edge_body(
                              batch, hb_i, a_idx, d_idx,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (overlaps && combat_attackhi3_landing_source_clear_blocks_enable_edge_body(
                              batch, bi, hb_i, a_idx, d_idx, attacker, defender)) {
            overlaps = 0u;
          }
          if (overlaps && combat_marth_aerial_static_spacie_tail_rejects_body_contact(
                              batch, hb_i, a_idx, d_idx, (uint8_t)hb_id,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (overlaps && combat_marth_attackairn_spacie_guard_static_pose_rejects_body_contact(
                              batch, hb_i, a_idx, d_idx, (uint8_t)hb_id, (uint8_t)cap_id,
                              use_guard_family_body_fallback_caps
                                  ? &body_fallback_caps[cap_id]
                                  : (defender_caps == NULL || cap_id >= defender_cap_count_u16
                                         ? NULL
                                         : &defender_caps[cap_id]))) {
            overlaps = 0u;
          }
          if (overlaps && combat_spacie_bair_static_extremity_rejects_body_contact(
                              batch, hb_i, a_idx, d_idx, (uint8_t)hb_id,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (!overlaps && !lbcoll_overlap_evaluated) {
            if (combat_body_overlap_lbColl_80006E58_subset_allows(batch, hb_i, d_idx)) {
              overlaps = combat_body_overlap_lbColl_80006E58_scaffold(
                  batch, bi, attacker, hb_id, hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr,
                  batch->state.fighter_scale_y[d_idx]);
              if (overlaps) {
                lbcoll_overlap_valid = combat_body_overlap_lbColl_80006E58_matrix_radius(
                    batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az,
                    bx, by, bz, 0u, &lbcoll_overlap_amount, &lbcoll_overlap_evaluated);
              }
            }
          }
          if (attackairb_stale_owner_candidate) {
            overlaps = combat_attackairb_continuation_body_overlap_exact(
                batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az, bx,
                by, bz, &attackairb_overlap_amount);
          }
          if (overlaps && combat_attackairb_jump_tail_rejects_body_contact(
                              batch, bi, attacker, hb_id, defender, a_idx, d_idx, (uint8_t)cap_id,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id],
                              use_guard_family_body_fallback_caps ? NULL : defender_caps,
                              use_guard_family_body_fallback_caps ? 0u : defender_cap_count_u16, hx,
                              hy, hz, hr)) {
            overlaps = 0u;
          }
          if (overlaps && combat_attackhi4_damageflytop_xrotn_rejects_body_contact(
                              batch, hb_i, a_idx, d_idx, hb_id, cap_id)) {
            overlaps = 0u;
          }
          if (!overlaps && !use_guard_family_body_fallback_caps) {
            const MslHurtCap* source_cap =
                (defender_caps == NULL || cap_id >= defender_cap_count_u16)
                    ? NULL
                    : &defender_caps[cap_id];
            overlaps = combat_pstadium_x44_gap_allows_body_contact(
                batch, bi, attacker, (uint8_t)hb_id, a_idx, d_idx, (uint8_t)cap_id, source_cap,
                lbcoll_overlap_amount, lbcoll_overlap_evaluated, shield_active);
          }
          if (overlaps && combat_marth_aerial_static_spacie_tail_rejects_body_contact(
                              batch, hb_i, a_idx, d_idx, (uint8_t)hb_id,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (overlaps && combat_marth_attackairn_spacie_guard_static_pose_rejects_body_contact(
                              batch, hb_i, a_idx, d_idx, (uint8_t)hb_id, (uint8_t)cap_id,
                              use_guard_family_body_fallback_caps
                                  ? &body_fallback_caps[cap_id]
                                  : (defender_caps == NULL || cap_id >= defender_cap_count_u16
                                         ? NULL
                                         : &defender_caps[cap_id]))) {
            overlaps = 0u;
          }
          if (overlaps && combat_spacie_bair_static_extremity_rejects_body_contact(
                              batch, hb_i, a_idx, d_idx, (uint8_t)hb_id,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (!overlaps) {
            overlaps = combat_attached_throw_body_pose_gap_admits_pre_release_contact(
                batch, a_idx, d_idx, attacker, hb_i);
          }
          if (!overlaps) {
            continue;
          }
          if (combat_attackairlw_invincible_contact_rejects_body_hitlag(batch, a_idx, d_idx)) {
            continue;
          }
          if (combat_guard_reflect_active_x14_no_guardon_blocks_body(batch, d_idx)) {
            continue;
          }
          if (combat_guard_reflect_final_x14_live_x18_blocks_body(batch, d_idx)) {
            continue;
          }
          if (combat_sheik_chain_terminal_same_source_episode_suppresses_body(batch, a_idx,
                                                                              d_idx)) {
            continue;
          }
          if (combat_sheik_chain_activation_edge_same_source_suppresses_body(
                  batch, a_idx, d_idx, attacker, hb_id, defender)) {
            continue;
          }
          if (combat_sheik_chain_damageflytop_high_horizon_suppresses_body(
                  batch, c, a_idx, d_idx, attacker, hb_id, int_dmg, defender_action,
                  batch->state.hitbox_element[hb_i])) {
            continue;
          }
          if (v1_group_seen_this_pass) {
            // ftColl_80076ED8 / ftColl_80076CBC immediately register victims_1 across all active
            // HitCapsules with the same hit_group through inlineB0 / ftColl_80076808. The retained
            // AttackAirB stale-owner bridge may bypass a stale seeded `lbColl_8000ACFC` miss, but it
            // must not bypass a same-pass source registration from an earlier HitCapsule.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80076CBC}
            // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
            continue;
          }
          if (combat_sheik_chain_same_frontier_later_hitbox_owns_body(
                  batch, a_idx, bi, attacker, hb_id, hit_group, defender, (int)cap_id,
                  defender_on_ground, ax, ay, az, bx, by, bz, cr)) {
            continue;
          }
          int16_t attackair_second_create_frame = -1;
          const size_t hb_seed_valid_i =
              ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
              (size_t)hb_id;
          if (attacker_action == (uint16_t)MSL_ACT_ATTACK_AIR_N && hb_id > 0 &&
              batch->state.combat_hitlist_hb_valid[hb_seed_valid_i] == 0u &&
              (attackair_second_create_frame = move_tables_attackair_second_create_hitbox_frame(
                   batch->state.char_id[a_idx], attacker_action)) >= 0 &&
              batch->state.anim_frame_f32[a_idx] >= (float)attackair_second_create_frame &&
              batch->state.anim_frame_f32[a_idx] < (float)(attackair_second_create_frame + 3) &&
              defender_action == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
              batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] != 0u &&
              msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker)) {
            // AttackAirN limb HitCapsule carry:
            // - Ft_MF_SkipHit keeps prior HitCapsule state on AttackAirN entry, and
            //   ftAction_8007121C / ftColl_800768A0 preserve same-group victims_1 through the
            //   second create band. Vanilla can therefore suppress a same-source DamageFlyTop
            //   victim on the lateral limb slots before the per-slot carry release is visible.
            // - The boundary is sourced from MSLFTSC1's second create_hitbox frame instead of a
            //   local action-id/frame slice. Slot 0 is intentionally not handled here; its longer
            //   same-source DamageFlyTop body carry is materialized by the hitlist owner.
            // data/scripts/{fox,falco}.bin (MSLFTSC1 AttackAirN create_hitbox events)
            // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCo_MF_AttackAirN
            // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
            // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_80076ED8}
            continue;
          }
          const uint8_t sheik_chain_same_source_damage_followup_admits_body =
              combat_sheik_chain_same_source_damage_followup_admits_body(
                  batch, c, a_idx, d_idx, attacker, hb_id, int_dmg, defender_action,
                  batch->state.hitbox_element[hb_i]);
          if (!allows_v1_after_source_materialize && !attackairb_stale_owner_candidate &&
              !attackairn_first_window_ignores_dense_damageflytop_latch &&
              !sheik_chain_same_source_damage_followup_admits_body) {
            continue;
          }
          if (!combat_shine_start_damageair_entry_pose_allows_body_contact(
                  batch, a_idx, d_idx, cap_id, hx, hy, hz, hr)) {
            continue;
          }
          const uint8_t attackairb_jump_low_body_model_scale_source =
              combat_attackairb_jump_low_body_source_owns_model_scale_bypass(
                  batch, bi, attacker, hb_id, defender, a_idx, d_idx, cap_id, hx, hy, hz, hr);
          if (attackairb_jump_low_body_model_scale_source == 0u &&
              !combat_attackairb_enable_edge_model_scale_allows_body_contact(
                  batch, a_idx, hb_i, d_idx, hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr,
                  combat_calc_hitlag_frames(c, int_dmg, a_motion_id, 1.0f))) {
            continue;
          }
          // Guard shield-poke phantom path keeps the decomp predicate:
          // `0 < HitCapsule.coll_distance < p_ftCommonData->x7A8`. The one retained extra
          // admission is restricted to the steady no-tilt Guard live-pose gap: source
          // `ftCo_80091E78(..., 1)` applies the current/no-tilt Guard pose directly when x4 is
          // zero, while the generic extracted Guard matrix path is still a coarse stand-in for
          // that live JObj collision matrix. Do not use this as a broad Guard/no-hitstun tolerance
          // bridge; nonzero-tilt, visible-submotion, enable-edge, and non-neutral Guard rows stay
          // on the exact x7A8 scalar.
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80091E78
          // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
          // refs/melee/src/melee/ft/ftcoll.c::{inlineB1,ftColl_80076ED8}
          const uint8_t guard_no_tilt_live_pose_gap =
              combat_guard_no_tilt_current_pose_gap(batch, d_idx);
          const uint8_t guard_shield_poke_phantom_boundary =
              (shield_active && batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD &&
               batch->state.hitstun[d_idx] == 0u && batch->state.hitbox_enable_edge[hb_i] == 0u &&
               lbcoll_overlap_valid && lbcoll_overlap_amount > 0.0f &&
               (lbcoll_overlap_amount <= c->phantom_overlap_max_x7a8 ||
                (guard_no_tilt_live_pose_gap &&
                 lbcoll_overlap_amount <=
                     (c->phantom_overlap_max_x7a8 + c->phantom_overlap_max_x7a8))))
                  ? 1u
                  : 0u;
          const uint8_t same_group_primary_full_body =
              (batch->state.hitbox_enable_edge[hb_i] == 0u &&
               combat_hitcapsule_is_authored_same_group_primary(batch, bi, attacker, hb_id))
                  ? 1u
                  : 0u;
          const uint8_t primary_phantom_has_later_same_group_body =
              same_group_primary_full_body && !shield_active && lbcoll_overlap_valid &&
                      lbcoll_overlap_amount > 0.0f &&
                      lbcoll_overlap_amount <= c->phantom_overlap_max_x7a8
                  ? combat_primary_phantom_tiplog_allows_later_same_group_body(
                        batch, c, bi, attacker, hb_id, defender, hit_group)
                  : 0u;
          const uint8_t ordinary_fighter_phantom_tiplog_range =
              (!shield_active &&
               (!same_group_primary_full_body || primary_phantom_has_later_same_group_body) &&
               lbcoll_overlap_amount <= c->phantom_overlap_max_x7a8)
                  ? 1u
                  : 0u;
          const uint8_t fighter_phantom_tiplog_range =
              (!attackairb_stale_owner_candidate && lbcoll_overlap_valid &&
               lbcoll_overlap_amount > 0.0f &&
               (ordinary_fighter_phantom_tiplog_range || guard_shield_poke_phantom_boundary))
                  ? 1u
                  : 0u;
          const uint8_t attackairb_phantom_tiplog_range =
              (attackairb_stale_owner_candidate && attackairb_overlap_amount > 0.0f &&
               attackairb_overlap_amount <= c->phantom_overlap_max_x7a8)
                  ? 1u
                  : 0u;
          const uint8_t terminal_damageflytop_phantom_tiplog_range =
              (defender_action == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
               batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] != 0u &&
               expected_body_hitlag != 0u && batch->state.hitstun[d_idx] <= expected_body_hitlag &&
               fighter_phantom_tiplog_range)
                  ? 1u
                  : 0u;
          if (fighter_phantom_tiplog_range &&
              (body_damage_logs[defender].count != 0u ||
               batch->state.phantom_damage_timer_x189c[d_idx] != 0u)) {
            // ftColl_80076ED8's phantom/tip-log branch is not an alternate full-BODY path:
            // once `dmg_log0_idx` has a BODY damage log, or x189C is already armed, an
            // inlineB1-range contact returns false instead of falling through to percent/KB.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,inlineB1,checkTipLog}
            continue;
          }
          if (!attackairb_stale_owner_candidate && !defender_no_damage &&
              (batch->state.hitstun[d_idx] == 0u || terminal_damageflytop_phantom_tiplog_range) &&
              fighter_phantom_tiplog_range) {
            // General fighter BODY phantom-hit lane:
            // - lbColl_8000805C writes HitCapsule.coll_distance from lbColl_80006E58.
            // - ftColl_80076ED8 routes 0 < coll_distance < p_ftCommonData->x7A8 through
            //   checkTipLog/inlineB1 instead of the percent/KB damage-state path.
            // - Apply this for source-evaluated BODY matrix overlaps below x7A8 when the defender is
            //   not shield-active. Active same-group strict damage leaders remain on the full BODY
            //   source path; lower-damage limb capsules and equal-damage groups keep the retained
            //   tip-log reconstruction. Shield rows have a separate Guard/ShieldDesc owner and only
            //   enter this lane through the guarded shield-poke predicate above.
            // - Terminal DamageFlyTop keeps the same source branch when remaining hitstun is inside
            //   the current hit's hitlag horizon: the tiny x7A8 contact starts victim-only hitlag
            //   without percent/KB, while the next source BODY overlap can still own the full hit.
            // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
            // refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80076ED8}
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
            // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_8009F0F0
            if (!hitlist_allows_fighter_v2(batch, bi, attacker, hb_id, defender, defender_iid)) {
              continue;
            }
            combat_mutations_pass1_future_apply_body_phantom_hit(
                batch, a_idx, d_idx, attacker, hdmg, batch->state.hitbox_element[hb_i]);
            hitlist_register_fighter_group_v2(batch, bi, attacker, hit_group, defender,
                                              defender_iid, (int)MSL_LBCOLL_INSERT_FT_BODY, 0u);
            break;
          }
          if (attackairb_stale_owner_candidate && !defender_no_damage &&
              attackairb_phantom_tiplog_range) {
            // Phantom/tip-log branch for fighter BODY hits:
            // - ftColl_80076ED8 takes the phantom lane when 0 < coll_distance < x7A8 and the
            //   victim is not already present in HitCapsule.victims_2.
            // - That branch starts victim hitlag through Fighter_ProcessHit's x18a0 path without
            //   percent/KB/damage-state entry.
            // refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80076ED8}
            // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
            if (!hitlist_allows_fighter_v2(batch, bi, attacker, hb_id, defender, defender_iid)) {
              continue;
            }
            combat_mutations_pass1_future_apply_body_phantom_hit(
                batch, a_idx, d_idx, attacker, hdmg, batch->state.hitbox_element[hb_i]);
            hitlist_register_fighter_group_v2(batch, bi, attacker, hit_group, defender,
                                              defender_iid, (int)MSL_LBCOLL_INSERT_FT_BODY, 0u);
            break;
          }
          if (dense_seed_suppresses_body || attackairb_dense_seed_suppresses_full_body ||
              pstadium_guardon_attackairb_x44_suppresses_body ||
              attackairb_live_hitlist_suppresses_full_body ||
              attackairhi_create_edge_suppresses_full_body ||
              attackairn_wait_dense_seed_suppresses_full_body ||
              attackairn_post_contact_dense_seed_suppresses_full_body ||
              attackairn_guard_dense_seed_suppresses_full_body) {
            continue;
          }
          // Marth Counter intercept consumes the BODY contact before damage intake.
          if (marth_counter_intercepts_contact(batch, d_idx) &&
              marth_counter_desc_overlaps_hitbox(batch, d_idx, hb_i)) {
            marth_counter_trigger(batch, a_idx, d_idx, hb_i, a_motion_id);
            const uint16_t post_counter_iid = batch->state.instance_id[d_idx];
            hitlist_register_fighter_group_v2(batch, bi, attacker, hit_group, defender,
                                              post_counter_iid, (int)MSL_LBCOLL_INSERT_FT_BODY,
                                              rehit_frames);
            continue;
          }
          // Combat Mutations Pass 1 (BODY-only).
          if (defender_no_damage) {
            combat_mutations_pass1_future_apply_body_hit_invincible(batch, a_idx, hb_i,
                                                                    a_motion_id);
          } else {
            if (use_guard_family_body_fallback_caps) {
              batch->state.hurtcap_height[cap_i] = body_fallback_caps[cap_id].height;
            }
            const uint8_t first_defender_log = (body_damage_logs[defender].count == 0u) ? 1u : 0u;
            const uint8_t recorded = combat_body_damage_log_record(
                batch, &body_damage_logs[defender], a_idx, d_idx, attacker, defender, hb_i, cap_i,
                int_dmg, a_motion_id, pre_combat_attack_id[attacker],
                pre_combat_attack_instance[attacker], pre_combat_instance_id[attacker],
                pre_combat_residual_hitcapsule_owner[attacker], hit_group, rehit_frames);
            if (recorded && first_defender_log &&
                body_damage_apply_count < (uint8_t)MSL_MAX_PLAYERS) {
              body_damage_apply_order[body_damage_apply_count++] = (uint8_t)defender;
            }
          }
          // Hitlist register: decomp hitlists store a victim pointer inside HitCapsule
          // (HitVictim.victim), so the victim identity is stable across the defender's damage-state
          // entry and other motion-state changes.
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
          //
          // The full BODY damage path now delays victim ProcessHit writeback until after the
          // collision pass so ftColl_8007A06C can select the best damage log. Register the current
          // identity immediately for same-pass lbColl_8000ACFC suppression; the delayed writer
          // re-registers the accepted groups against the post-ProcessHit instance id for reseed
          // continuity.
          const uint16_t defender_iid_post = batch->state.instance_id[d_idx];
          // Decomp insertion type on BODY hit path: ftColl_80076ED8 calls inlineB0(..., type=0, cb=lbColl_80008688).
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
          hitlist_register_fighter_group(batch, bi, attacker, hit_group, defender,
                                         defender_iid_post, (int)MSL_LBCOLL_INSERT_FT_BODY,
                                         rehit_frames);
          v1_group_registered_this_pass[attacker][defender][hit_group] = 1u;

          break;
        }
      }
    }
  }

  for (uint8_t order_i = 0u; order_i < body_damage_apply_count; order_i++) {
    const int defender = (int)body_damage_apply_order[order_i];
    combat_body_damage_log_apply(batch, bi, &body_damage_logs[defender]);
  }
}

static void combat_select_body_hits_one_debug(MslBatch* batch, int bi,
                                              MslDebugCombatContact* out_contacts,
                                              uint16_t max_contacts, uint16_t* inout_written) {
  if (batch == NULL || inout_written == NULL) {
    return;
  }
  if (out_contacts == NULL || max_contacts == 0) {
    return;
  }

  const int num_players = (int)batch->config.num_players;
  uint16_t written = *inout_written;
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }

  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    if (batch->state.stocks[a_idx] == 0) {
      continue;
    }
    if (batch->state.hitbox_count[a_idx] == 0) {
      continue;
    }

    const uint32_t msid_u32 = batch->state.animation_index[a_idx];
    const uint16_t msid = (msid_u32 <= 0xFFFFu) ? (uint16_t)msid_u32 : 0u;
    const int16_t action_frame = batch->state.action_frame[a_idx];
    const uint16_t a_motion_id = batch->state.action_id[a_idx];

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, defender);
      if (batch->state.stocks[d_idx] == 0) {
        continue;
      }
      if (msl_action_owns_x2219_collision_skip(batch->state.action_id[d_idx])) {
        // Debug BODY selection mirrors the runtime x2219_b1 collision skip for Dead*/Rebirth.
        continue;
      }
      const uint16_t defender_iid = batch->state.instance_id[d_idx];

      uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      const MslHurtCap* defender_caps = NULL;
      uint16_t defender_cap_count_u16 = 0u;
      (void)hurtcaps_get(batch->state.char_id[d_idx], &defender_caps, &defender_cap_count_u16);
      const MslHurtCap* body_fallback_caps = NULL;
      uint16_t body_fallback_count_u16 = 0u;
      uint8_t use_guard_family_body_fallback_caps = 0u;
      const uint8_t guard_family_body_source =
          combat_guard_family_no_submotion_body_source_msid(batch, d_idx, NULL);
      if (guard_family_body_source &&
          (hurtcap_count == 0u ||
           combat_guardon_no_submotion_body_overrides_existing_hurtcaps(batch, d_idx))) {
        if (hurtcaps_get(batch->state.char_id[d_idx], &body_fallback_caps,
                         &body_fallback_count_u16) == 0 &&
            body_fallback_caps != NULL && body_fallback_count_u16 != 0u) {
          use_guard_family_body_fallback_caps = 1u;
          hurtcap_count = body_fallback_count_u16 > (uint16_t)MSL_MAX_HURTCAPS
                              ? (uint8_t)MSL_MAX_HURTCAPS
                              : (uint8_t)body_fallback_count_u16;
        }
      }
      if (hurtcap_count == 0) {
        continue;
      }

      // Hit status / hurtbox-state eligibility gate (matches combat_select_body_hits_one policy).
      const uint8_t hit_status = combat_defender_hit_status_u8(batch, d_idx);
      uint8_t hurt_state = batch->state.hurtbox_state[d_idx];
      if (hit_status > hurt_state) {
        hurt_state = hit_status;
      }
      if (hurt_state == 2u) {
        continue;
      }
      // Hitlag gating (attacker-owned): mirror combat_select_body_hits_one.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
      if (batch->state.hitlag_started_frame[a_idx]) {
        continue;
      }

      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      const uint8_t shield_active = (shr > 0.0f) ? 1u : 0u;

      // Deterministic selection: pick the first BODY overlap in (hitbox_id, hurtcap_id) order.
      uint8_t did_hit = 0;
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES && !did_hit; hb_id++) {
        const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
        if (!batch->state.hitbox_enabled[hb_i]) {
          continue;
        }
        if (combat_defer_late_slot_same_frame_speciallw_entry_hit(batch, bi, a_idx, d_idx, attacker,
                                                                  defender, hb_id)) {
          continue;
        }

        const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
        const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1 : 0;
        if (defender_on_ground) {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
            continue;
          }
        } else {
          if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
            continue;
          }
        }

        const float hx = batch->state.hitbox_x[hb_i];
        const float hy = batch->state.hitbox_y[hb_i];
        const float hz = batch->state.hitbox_z[hb_i];
        const float hr = batch->state.hitbox_radius[hb_i];
        const float hdmg = batch->state.hitbox_damage[hb_i];

        if (!(hdmg > 0.0f)) {
          continue;
        }
        // SHIELD precedence: if the hitbox intersects the defender shield bubble, treat as shielded
        // and do not apply BODY selection for this hitbox.
        if (shield_active && sphere_sphere_intersects(hx, hy, hz, hr, shx, shy, shz, shr)) {
          continue;
        }

        // Rehit suppression (debug view): suppress repeats while the victim is present in the
        // hitbox's victims_1 list.
        const uint8_t hit_group = hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        (void)hit_group;
        if (!hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid)) {
          continue;
        }
        if (combat_guard_reflect_active_x14_no_guardon_blocks_body(batch, d_idx)) {
          continue;
        }

        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
          float ax = 0.0f, ay = 0.0f, az = 0.0f;
          float bx = 0.0f, by = 0.0f, bz = 0.0f;
          float cr = 0.0f;
          if (use_guard_family_body_fallback_caps) {
            if (!combat_guard_family_body_hurtcap_world(batch, d_idx, &body_fallback_caps[cap_id],
                                                        cap_id, body_fallback_count_u16, &ax, &ay,
                                                        &az, &bx, &by, &bz, &cr)) {
              continue;
            }
          } else {
            if (!batch->state.hurtcap_enabled[cap_i]) {
              continue;
            }
            ax = batch->state.hurtcap_a_x[cap_i];
            ay = batch->state.hurtcap_a_y[cap_i];
            az = batch->state.hurtcap_a_z[cap_i];
            bx = batch->state.hurtcap_b_x[cap_i];
            by = batch->state.hurtcap_b_y[cap_i];
            bz = batch->state.hurtcap_b_z[cap_i];
            cr = batch->state.hurtcap_radius[cap_i];
          }

          float lbcoll_overlap_amount = 0.0f;
          uint8_t lbcoll_overlap_valid = 0u;
          uint8_t lbcoll_overlap_evaluated = 0u;
          lbcoll_overlap_valid = combat_body_overlap_lbColl_80006E58_matrix_radius(
              batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az, bx, by,
              bz, 0u, &lbcoll_overlap_amount, &lbcoll_overlap_evaluated);
          uint8_t overlaps = lbcoll_overlap_evaluated
                                 ? lbcoll_overlap_valid
                                 : combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx,
                                                                    by, bz, cr, NULL);
          if (!overlaps && !lbcoll_overlap_evaluated &&
              combat_body_overlap_lbColl_80006E58_subset_allows(batch, hb_i, d_idx)) {
            overlaps = combat_body_overlap_lbColl_80006E58_scaffold(
                batch, bi, attacker, hb_id, hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr,
                batch->state.fighter_scale_y[d_idx]);
            if (overlaps) {
              lbcoll_overlap_valid = combat_body_overlap_lbColl_80006E58_matrix_radius(
                  batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az, bx,
                  by, bz, 0u, &lbcoll_overlap_amount, &lbcoll_overlap_evaluated);
              (void)lbcoll_overlap_valid;
            }
          }
          if (!overlaps) {
            const uint8_t attackairb_stale_owner_candidate =
                combat_attackairb_stale_owner_continuation_candidate(
                    batch, a_idx, d_idx, hdmg,
                    combat_calc_hitlag_frames(c, combat_get_env_dmg(hdmg), a_motion_id, 1.0f)) &&
                        batch->state.instance_hit_by[d_idx] != batch->state.instance_id[a_idx]
                    ? 1u
                    : 0u;
            if (attackairb_stale_owner_candidate) {
              overlaps = combat_attackairb_continuation_body_overlap_exact(
                  batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az, bx,
                  by, bz, NULL);
            }
          }
          if (overlaps && combat_marth_aerial_static_spacie_tail_rejects_body_contact(
                              batch, hb_i, a_idx, d_idx, (uint8_t)hb_id,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (overlaps && combat_marth_attackairn_spacie_guard_static_pose_rejects_body_contact(
                              batch, hb_i, a_idx, d_idx, (uint8_t)hb_id, (uint8_t)cap_id,
                              use_guard_family_body_fallback_caps
                                  ? &body_fallback_caps[cap_id]
                                  : (defender_caps == NULL || cap_id >= defender_cap_count_u16
                                         ? NULL
                                         : &defender_caps[cap_id]))) {
            overlaps = 0u;
          }
          if (overlaps && combat_spacie_bair_static_extremity_rejects_body_contact(
                              batch, hb_i, a_idx, d_idx, (uint8_t)hb_id,
                              use_guard_family_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (!overlaps) {
            overlaps = combat_attached_throw_body_pose_gap_admits_pre_release_contact(
                batch, a_idx, d_idx, attacker, hb_i);
          }
          if (!overlaps) {
            continue;
          }
          if (!combat_shine_start_damageair_entry_pose_allows_body_contact(
                  batch, a_idx, d_idx, cap_id, hx, hy, hz, hr)) {
            continue;
          }
          const uint8_t attackairb_jump_low_body_model_scale_source =
              combat_attackairb_jump_low_body_source_owns_model_scale_bypass(
                  batch, bi, attacker, hb_id, defender, a_idx, d_idx, cap_id, hx, hy, hz, hr);
          if (attackairb_jump_low_body_model_scale_source == 0u &&
              !combat_attackairb_enable_edge_model_scale_allows_body_contact(
                  batch, a_idx, hb_i, d_idx, hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr,
                  combat_calc_hitlag_frames(c, combat_get_env_dmg(hdmg), a_motion_id, 1.0f))) {
            continue;
          }

          if (written < max_contacts) {
            MslDebugCombatContact* out = &out_contacts[written];
            memset(out, 0, sizeof(*out));
            out->attacker = (uint8_t)attacker;
            out->defender = (uint8_t)defender;
            out->hitbox_id = (uint8_t)hb_id;
            out->hurtcap_id = cap_id;
            out->attacker_msid = msid;
            out->attacker_action_frame = action_frame;
            out->hitbox_x = hx;
            out->hitbox_y = hy;
            out->hitbox_z = hz;
            out->hitbox_radius = hr;
            out->hitbox_damage = hdmg;
            out->hurtcap_ax = ax;
            out->hurtcap_ay = ay;
            out->hurtcap_az = az;
            out->hurtcap_bx = bx;
            out->hurtcap_by = by;
            out->hurtcap_bz = bz;
            out->hurtcap_radius = cr;
            written++;
          }

          did_hit = 1;
          break;
        }
      }

      if (written >= max_contacts) {
        *inout_written = written;
        return;
      }
    }
  }

  *inout_written = written;
}

static inline void combat_processhit_apply_expired_phantom_damage(MslBatch* batch, int bi, int p,
                                                                  size_t idx) {
  float dmg = batch->state.phantom_damage_pending_x1898[idx];
  if (!(dmg > 0.0f) || !isfinite(dmg)) {
    uint8_t fallback_source_slot = 0xFFu;
    if (!combat_damageflytop_terminal_phantom_expiry_seed_gap(batch, bi, p, idx, &dmg,
                                                              &fallback_source_slot)) {
      combat_processhit_clear_phantom_damage(batch, idx);
      return;
    }
    batch->state.phantom_damage_source_port[idx] = fallback_source_slot;
  } else {
    uint16_t timer = batch->state.phantom_damage_timer_x189c[idx];
    if (timer == 0u) {
      combat_processhit_clear_phantom_damage(batch, idx);
      return;
    }
    timer--;
    batch->state.phantom_damage_timer_x189c[idx] = timer;
    if (timer != 0u) {
      return;
    }
  }

  // Delayed fighter phantom/tip-log damage:
  // - Fighter_ProcessHit decrements `dmg.x189C_unk_num_frames` during the ProcessHit pass.
  // - If no knockback damage path has superseded it, ftColl_8007BE3C applies `dmg.x1898` to
  //   percent and runs stale/combo bookkeeping against the stored source gobj.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007BE3C
  float percent = batch->state.percent[idx] + dmg;
  if (percent > 999.0f) {
    percent = 999.0f;
  }
  batch->state.percent[idx] = percent;

  // Legacy field name: this hidden phantom/tip-log source is stored as a local simulator slot.
  const uint8_t src_slot = batch->state.phantom_damage_source_port[idx];
  if (src_slot < (uint8_t)batch->config.num_players && src_slot != (uint8_t)p) {
    const size_t a_idx = msl_idx_player(bi, (int)src_slot);
    const uint16_t move_id = batch->state.attack_id[a_idx];
    const uint16_t attack_instance = batch->state.attack_instance[a_idx];
    staling_queue_update(batch, a_idx, move_id, attack_instance);
    combat_combo_ftColl_800763C0(batch, a_idx, p, idx, move_id);
  }

  combat_processhit_clear_phantom_damage(batch, idx);
}

void combat_processhit_consume(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  // Clear the Slippi `state_flags` bit for "detection hitbox touching shield bubble" once per
  // fighter at a decomp-shaped "ProcessHit" consume point.
  //
  // Decomp-first references (GALE01):
  // - Set site (inert shield-overlap branch): refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  //     `victim_fp->x221C_b5 = true;` under `temp_r23->element == HitElement_Inert` and
  //     `lbColl_80007BCC(..., &this_fp->shield_hit, ...) != false`.
  // - Clear site: refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //     clears `fp->x221C_b5 = 0`.
  // - Bitfield layout at fp+0x221C is documented in refs/melee/src/melee/ft/types.h.
  //
  // In this simulator, collision detection (combat_resolve) can set this bit on inert shield
  // overlaps; this consume stage clears it on the next frame, matching the intent that the bit
  // represents overlaps observed in the most recent collision pass.

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      combat_processhit_apply_expired_phantom_damage(batch, bi, p, idx);
      const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
      batch->state.state_flags[flags_i] &=
          (uint8_t)~(uint8_t)MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD;
      // fp->dmg.x1838_percentTemp is a per-frame accumulator consumed/reset by Fighter_ProcessHit.
      // We don't simulate the full Fighter_ProcessHit pipeline; clear it at the start of each frame
      // to ensure deterministic intra-frame accumulation during items_update/combat_resolve.
      batch->state.percent_temp[idx] = 0.0f;
    }
  }
}

static inline void combat_preserve_fresh_air_damage_entry_root_y(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const uint16_t action_id = batch->state.action_id[idx];
      if (!combat_is_damage_air_action(action_id)) {
        continue;
      }
      const uint16_t prev_action = batch->state.prev_action_id[idx];
      if (combat_is_damage_air_action(prev_action)) {
        continue;
      }
      if (batch->state.on_ground[idx] != 0u || batch->state.hitlag[idx] == 0u ||
          batch->state.hitlag_pre_timer[idx] != 0u ||
          !isfinite(batch->state.coll_stage_prev_pos_y[idx])) {
        continue;
      }

      // Fresh airborne DamageAir entry root ownership:
      // - The current frame's motion-state Coll/map callback has already run under the pre-hit
      //   action before Fighter_ProcessHit enters ftCo_8008DCE0.
      // - Continued active-hitlag Damage rows still use the Damage/OnEveryHitlag collision owner
      //   through mpcoll_ground.c, but a same-frame low/med airborne damage entry must publish the
      //   pre-ProcessHit root instead of inheriting a persisted floor id snap.
      // refs/melee/src/melee/ft/fighter.c::{Fighter_procUpdate,Fighter_ProcessHit_8006D1EC}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
      batch->state.pos_y[idx] = batch->state.coll_stage_prev_pos_y[idx];
    }
  }
}

static inline void combat_clear_replay_shield_contact_seed_lanes(MslBatch* batch) {
  if (batch == NULL || batch->state.combat_shield_contact_hb_kind == NULL) {
    return;
  }
  const size_t total = (size_t)batch->batch_size * (size_t)MSL_MAX_PLAYERS *
                       (size_t)MSL_MAX_HITBOXES * (size_t)MSL_MAX_PLAYERS;
  for (size_t i = 0; i < total; i++) {
    batch->state.combat_shield_contact_hb_kind[i] = 0u;
  }
}

static inline uint8_t combat_specialhi_frozen_guard_dense_seed_allows_live_shield(
    MslBatch* batch, const MslCommonParams* c, int bi, int attacker, int defender, int hb_id,
    size_t a_idx, size_t d_idx, uint16_t defender_iid, uint8_t shield_seed_kind, float hx, float hy,
    float hz, float hr, float shx, float shy, float shz, float shr,
    uint8_t shield_desc_envelope_ready, uint8_t shield_extent_bridge_active,
    uint8_t guard_reflect_reflectdesc_only) {
  (void)c;
  if (batch == NULL || shield_seed_kind != 0u || guard_reflect_reflectdesc_only != 0u) {
    return 0u;
  }
  if (batch->replay_reseed_frame_active == NULL || batch->replay_reseed_frame_active[bi] == 0u ||
      (batch->replay_rollout_reseeded != NULL && batch->replay_rollout_reseeded[bi] != 0u)) {
    return 0u;
  }
  if (!msl_motion_state_class_has(batch->state.char_id[a_idx], batch->state.action_id[a_idx],
                                  MSL_MS_CLASS_SPECIALHI)) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_GUARD ||
      batch->state.prev_action_id[d_idx] != (uint16_t)MSL_ACT_GUARD ||
      batch->state.action_frame[d_idx] >= 0 || batch->state.animation_index[d_idx] != 0xFFFFFFFFu ||
      batch->state.anim_frame_f32[d_idx] >= 0.0f) {
    return 0u;
  }
  if (batch->state.hitlag[a_idx] != 0u || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[a_idx] != 0u || batch->state.hitstun[d_idx] != 0u || !(shr > 0.0f)) {
    return 0u;
  }
  if (!hitlist_allows_fighter_live_collision(batch, bi, attacker, hb_id, defender, defender_iid)) {
    return 0u;
  }
  if (!combat_shield_overlap_ftcoll_80007bcc(
          batch, bi, attacker, defender, hb_id, hx, hy, hz, hr, shx, shy, shz, shr,
          /*shield_desc_radius=*/1.0f, batch->state.fighter_scale_y[d_idx],
          shield_desc_envelope_ready, shield_extent_bridge_active, NULL)) {
    return 0u;
  }

  // Exact one-step reseed dense HitCapsule stale trim for SpecialHi -> frozen Guard shield hits:
  // - ftColl_80078C70 gates each HitCapsule through lbColl_8000ACFC, then performs live
  //   ShieldDesc narrowphase through lbColl_80007BCC before ftColl_80076CBC.
  // - The legacy dense group seed can only say "some hit_group victim existed"; it cannot prove
  //   this exact SpecialHi HitCapsule's victims_1 list still owns suppression after a neutral
  //   frozen-Guard no-submotion snapshot with no hitlag/hitstun.
  // - Replay-seeded rollouts are excluded: once the rollout owns live prior-frame HitCapsule
  //   history, the dense entry is no longer a standalone one-step seed-bridge proof.
  // - Exact/live/per-HitCapsule victims_1 entries remain authoritative because
  //   hitlist_allows_fighter_live_collision ignores only MSL_HITLIST_FIGHTER_ID32_SEED_DENSE and
  //   still rejects concrete source-owned entries.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC,ftColl_800768A0}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80007BCC}
  // data/motion_state/owners/*.bin (MSLMSO01 MSL_MS_CLASS_SPECIALHI)
  return 1u;
}

void combat_resolve(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }

  for (int bi = 0; bi < batch->batch_size; bi++) {
    combat_select_body_hits_one_mutating(batch, bi);
  }

  // Consume fp->dmg.x1838_percentTemp into percent and reset it, matching the end-of-frame cleanup
  // in Fighter_ProcessHit_8006D1EC.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (x1838_percentTemp reset)
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const float temp = batch->state.percent_temp[idx];
      if (temp != 0.0f) {
        float percent = batch->state.percent[idx] + temp;
        if (percent > 999.0f) {
          percent = 999.0f;
        }
        batch->state.percent[idx] = percent;
      }
      batch->state.percent_temp[idx] = 0.0f;
      if (combat_action_is_catch_family(batch->state.action_id[idx])) {
        // Catch-family Fighter_8006CDA4 pre-gate counts are replay seed reconstruction for a
        // same-frame severe-airborne DamageFlyRoll gate. If no gate consumed the marker this frame,
        // it must not persist into later Catch/throw gameplay; delayed carry owners such as
        // DamageFlyTop/AttackAir remain outside this Catch-family clear.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
        batch->state.fighter_8006cda4_pre_gate_consume_count[idx] = 0u;
      }
    }
  }

  combat_preserve_fresh_air_damage_entry_root_y(batch);
  // Raptor Boost OnDetect runs after the damage passes: Fighter_ProcessHit's branch ladder
  // reaches fp->unk_gobj only when the fighter neither dealt nor took damage this frame.
  // refs/melee/src/melee/ft/fighter.c (hurtbox_detect_cb under the unk_gobj else-branch)
  falcon_specials_inert_detect_pass(batch);
  // `combat_shield_contact_hb_kind` is one-step replay provenance for lbColl_80007BCC /
  // ftColl_80076CBC admission. It is authoritative for the frame seeded by replay validation, but
  // it is not live HitCapsule/ShieldDesc state and must not persist into the next rollout step.
  // Runtime free-running rows keep this lane zero after init.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  combat_clear_replay_shield_contact_seed_lanes(batch);
}

int combat_debug_select_body_hits(MslBatch* batch, int batch_index,
                                  MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                  uint16_t* out_count) {
  if (batch == NULL || out_count == NULL) {
    return EINVAL;
  }
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (max_contacts == 0) {
    *out_count = 0;
    return 0;
  }
  if (out_contacts == NULL) {
    return EINVAL;
  }
  uint16_t written = 0;
  combat_select_body_hits_one_debug(batch, batch_index, out_contacts, max_contacts, &written);
  *out_count = written;
  return 0;
}

int combat_debug_shield_candidate_decisions(MslBatch* batch, int batch_index,
                                            MslDebugShieldCandidateDecision* out_rows,
                                            uint16_t max_rows, uint16_t* out_count) {
  if (batch == NULL || out_count == NULL) {
    return EINVAL;
  }
  *out_count = 0;
  if (batch_index < 0 || batch_index >= batch->batch_size) {
    return EINVAL;
  }
  if (max_rows == 0) {
    return 0;
  }
  if (out_rows == NULL) {
    return EINVAL;
  }

  const int num_players = (int)batch->config.num_players;
  const MslCommonParams* c = msl_common_params();
  uint16_t written = 0;
  const int bi = batch_index;

  for (int attacker = 0; attacker < num_players; attacker++) {
    const size_t a_idx = msl_idx_player(bi, attacker);
    const uint32_t msid_u32 = batch->state.animation_index[a_idx];
    const uint16_t msid = (msid_u32 <= 0xFFFFu) ? (uint16_t)msid_u32 : 0u;
    const int16_t action_frame = batch->state.action_frame[a_idx];
    const uint8_t attacker_stock_zero = (batch->state.stocks[a_idx] == 0u) ? 1u : 0u;

    for (int defender = 0; defender < num_players; defender++) {
      if (defender == attacker) {
        continue;
      }
      const size_t d_idx = msl_idx_player(bi, defender);
      const uint8_t defender_stock_zero = (batch->state.stocks[d_idx] == 0u) ? 1u : 0u;
      const uint8_t attacker_hitlag_started = batch->state.hitlag_started_frame[a_idx] ? 1u : 0u;
      const uint8_t defender_hitlag_started = batch->state.hitlag_started_frame[d_idx] ? 1u : 0u;
      const uint8_t hitlag_gate = attacker_hitlag_started ? 1u : 0u;
      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      const MslGuardReflectOwner guard_reflect_owner =
          msl_guard_reflect_owner_resolve(batch, d_idx);
      const uint8_t guard_reflect_entry_no_submotion =
          guard_reflect_owner.shield_entry_no_submotion;
      const uint8_t shield_active = (shr > 0.0f) ? 1u : 0u;
      // GuardReflect no-submotion entry snapshots (action_frame<0, msid sentinel) carry
      // ambiguous ordering between ftCo_8009388C clear and ftCo_80092450 recreate.
      // Keep shield-active ownership from x221B_b0, but disable ShieldDesc envelope expansion
      // lanes only for guard-origin entry frames. Direct ftCo_80091A4C powershield entry
      // (including Landing_IASA) runs ftCo_80092450 before ReflectDesc creation and keeps
      // ShieldDesc for fighter-vs-fighter shield collision.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_80093694,ftCo_8009388C,ftCo_80093A50,ftCo_80092450}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c::ftCo_Landing_IASA
      const uint8_t shield_desc_envelope_ready = !guard_reflect_entry_no_submotion;
      const uint8_t shield_extent_bridge_active = 0u;
      const uint8_t guard_reflect_reflectdesc_only = guard_reflect_owner.reflectdesc_only;
      const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;

      uint8_t pair_reason = (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD;
      if (attacker_stock_zero) {
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_ATTACKER_STOCKS_ZERO;
      } else if (defender_stock_zero) {
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_DEFENDER_STOCKS_ZERO;
      } else if (hitlag_gate) {
        // Decomp gate shape: ftColl_80078C70 collision ownership is attacker-centric; keep this
        // debug pair gate aligned to attacker-side hitlag only.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_HITLAG_GATE;
      } else if (!shield_active) {
        // Decomp shield overlap path is only reached when the defender shield descriptor is active.
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_SHIELD_INACTIVE;
      }

      {
        MslDebugShieldCandidateDecision* out = &out_rows[written];
        memset(out, 0, sizeof(*out));
        out->source_kind = (uint8_t)MSL_DEBUG_SHIELD_SOURCE_PAIR_GATE;
        out->attacker = (uint8_t)attacker;
        out->defender = (uint8_t)defender;
        out->hitbox_id = 0xFFu;
        out->reject_reason = pair_reason;
        out->attacker_hitlag_started_frame = attacker_hitlag_started;
        out->defender_hitlag_started_frame = defender_hitlag_started;
        out->shield_active = shield_active;
        out->defender_on_ground = defender_on_ground;
        out->attacker_msid = msid;
        out->attacker_action_frame = action_frame;
        out->shield_x = shx;
        out->shield_y = shy;
        out->shield_z = shz;
        out->shield_radius = shr;
        written++;
      }

      if (written >= max_rows) {
        *out_count = written;
        return 0;
      }

      if (pair_reason != (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD) {
        continue;
      }

      const uint16_t defender_iid = batch->state.instance_id[d_idx];
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
        MslDebugShieldCandidateDecision* out = &out_rows[written];
        memset(out, 0, sizeof(*out));
        out->source_kind = (uint8_t)MSL_DEBUG_SHIELD_SOURCE_FIGHTER_HITBOX;
        out->attacker = (uint8_t)attacker;
        out->defender = (uint8_t)defender;
        out->hitbox_id = (uint8_t)hb_id;
        out->attacker_hitlag_started_frame = attacker_hitlag_started;
        out->defender_hitlag_started_frame = defender_hitlag_started;
        out->shield_active = shield_active;
        out->defender_on_ground = defender_on_ground;
        out->attacker_msid = msid;
        out->attacker_action_frame = action_frame;
        out->shield_x = shx;
        out->shield_y = shy;
        out->shield_z = shz;
        out->shield_radius = shr;

        const uint8_t enabled = batch->state.hitbox_enabled[hb_i] ? 1u : 0u;
        out->hitbox_enabled = enabled;
        out->hb_flags = batch->state.hitbox_flags[hb_i];
        out->element = batch->state.hitbox_element[hb_i];
        out->hitbox_damage = batch->state.hitbox_damage[hb_i];
        out->hitbox_x = batch->state.hitbox_x[hb_i];
        out->hitbox_y = batch->state.hitbox_y[hb_i];
        out->hitbox_z = batch->state.hitbox_z[hb_i];
        out->hitbox_radius = batch->state.hitbox_radius[hb_i];

        uint8_t reason = (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD;

        if (!enabled) {
          reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_HITBOX_DISABLED;
        } else {
          // Ground/air eligibility gate (decomp hitcapsule x40_b2/x40_b3).
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
          const uint16_t hb_flags = batch->state.hitbox_flags[hb_i];
          if (defender_on_ground) {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_GROUNDED) == 0) {
              reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_GROUND_AIR_FLAGS;
            }
          } else {
            if ((hb_flags & MSL_HITBOX_FLAG_HIT_AERIAL) == 0) {
              reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_GROUND_AIR_FLAGS;
            }
          }
        }

        if (reason == (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD) {
          // Decomp ownership: rehit suppression gate (lbColl_8000ACFC) is evaluated outside
          // shield geometry helper lbColl_80007BCC.
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_8000ACFC
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
          const uint8_t shield_seed_kind =
              (msl_shielddesc_attackairb_guard_lower_bound_seed_owner(batch, bi, attacker,
                                                                      defender) != 0u)
                  ? 0u
                  : batch->state.combat_shield_contact_hb_kind[idx_hitbox_victim(bi, attacker,
                                                                                 hb_id, defender)];
          uint8_t allows =
              hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid);
          // GuardReflect no-submotion x14-expired bridge:
          // - Decomp suppression uses HitVictim.victim pointer identity; this simulator uses seeded
          //   replay-visible proxy identity under one-step reseed.
          // - In frozen GuardReflect rows after callback-owned x14 expiry, stale suppression can
          //   over-block the immediate shield-contact transition lane.
          // - Keep this bypass restricted to the expired-x14 no-submotion window.
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
          if (!allows && shield_seed_kind == 2u) {
            allows = 1u;
          } else if (!allows &&
                     combat_specialhi_frozen_guard_dense_seed_allows_live_shield(
                         batch, c, bi, attacker, defender, hb_id, a_idx, d_idx, defender_iid,
                         shield_seed_kind, out->hitbox_x, out->hitbox_y, out->hitbox_z,
                         out->hitbox_radius, shx, shy, shz, shr, shield_desc_envelope_ready,
                         shield_extent_bridge_active, guard_reflect_reflectdesc_only)) {
            allows = 1u;
          }
          out->hitlist_allows = allows ? 1u : 0u;
          if (!allows) {
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_HITLIST_CONTAINS;
          }
        }

        if (reason == (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD) {
          float overlap_margin = 0.0f;
          const uint8_t shield_seed_kind =
              (msl_shielddesc_attackairb_guard_lower_bound_seed_owner(batch, bi, attacker,
                                                                      defender) != 0u)
                  ? 0u
                  : batch->state.combat_shield_contact_hb_kind[idx_hitbox_victim(bi, attacker,
                                                                                 hb_id, defender)];
          uint8_t overlaps = 0u;
          if (shield_seed_kind == 1u) {
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_SHIELD_GEOM_NO_OVERLAP;
          } else if (shield_seed_kind == 0u && c != NULL &&
                     combat_source_order_earlier_body_hitcapsule_precedes_shield(
                         batch, c, bi, attacker, defender, hb_id, defender_iid, shx, shy, shz, shr,
                         shield_desc_envelope_ready, shield_extent_bridge_active,
                         guard_reflect_reflectdesc_only, NULL)) {
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_EARLIER_BODY_HITCAPSULE;
          } else if (shield_seed_kind == 2u) {
            overlaps = 1u;
          } else if (!guard_reflect_reflectdesc_only) {
            overlaps = combat_shield_overlap_ftcoll_80007bcc(
                batch, bi, attacker, defender, hb_id, out->hitbox_x, out->hitbox_y, out->hitbox_z,
                out->hitbox_radius, shx, shy, shz, shr, /*shield_desc_radius=*/1.0f,
                batch->state.fighter_scale_y[d_idx], shield_desc_envelope_ready,
                shield_extent_bridge_active, &overlap_margin);
          }
          out->overlap_shield = overlaps ? 1u : 0u;
          out->shield_overlap_margin = overlap_margin;
          if (reason == (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD && overlaps &&
              shield_seed_kind == 0u && hb_id == 0 &&
              msl_shielddesc_attackairb_guard_lower_bound_seed_owner(batch, bi, attacker,
                                                                     defender) != 0u &&
              fabsf(out->hitbox_y - shy) > shr) {
            const size_t hb2_i = idx_hitbox(bi, attacker, 2);
            uint8_t weak_hb2_overlaps = 0u;
            if (batch->state.hitbox_enabled[hb2_i] != 0u &&
                batch->state.hitbox_damage[hb2_i] == 9.0f) {
              weak_hb2_overlaps = combat_shield_overlap_ftcoll_80007bcc(
                  batch, bi, attacker, defender, 2, batch->state.hitbox_x[hb2_i],
                  batch->state.hitbox_y[hb2_i], batch->state.hitbox_z[hb2_i],
                  batch->state.hitbox_radius[hb2_i], shx, shy, shz, shr,
                  /*shield_desc_radius=*/1.0f, batch->state.fighter_scale_y[d_idx],
                  shield_desc_envelope_ready, shield_extent_bridge_active, NULL);
            }
            if (weak_hb2_overlaps != 0u) {
              overlaps = 0u;
              out->overlap_shield = 0u;
            }
          }
          if (reason == (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD && !overlaps) {
            // Decomp shield geometry test helper:
            // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_SHIELD_GEOM_NO_OVERLAP;
          }
        }

        if (reason == (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD) {
          if (out->element == (uint8_t)MSL_HIT_ELEMENT_INERT) {
            // Decomp split: inert overlaps set x221C_b5 and do not enter ftColl_80076CBC.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_INERT_ELEMENT;
          } else if (!(out->hitbox_damage > 0.0f)) {
            // Decomp shield-hit effects consume damaging hitcapsules (ftColl_80076CBC path).
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_NONPOS_DAMAGE;
          }
        }

        out->reject_reason = reason;
        written++;
        if (written >= max_rows) {
          *out_count = written;
          return 0;
        }
      }
    }
  }

  *out_count = written;
  return 0;
}
