#include "combat_internal.h"
#include "alloc.h"
#include "fighter_contact.h"
#include "fighter_guard.h"
#include "grab_flow.h"
#include "marth_specials.h"

int combat_processhit_pending_init(MslBatch* batch) {
  if (batch == NULL || batch->batch_size <= 0) {
    return -1;
  }
  const size_t count = (size_t)batch->batch_size * (size_t)MSL_MAX_PLAYERS;
  batch->processhit_pending =
      (MslCombatProcessHitResolved*)alloc_calloc(count, sizeof(MslCombatProcessHitResolved));
  batch->processhit_pending_valid = (uint8_t*)alloc_calloc(count, sizeof(uint8_t));
  batch->processhit_shield_pending =
      (MslCombatShieldPending*)alloc_calloc(count, sizeof(MslCombatShieldPending));
  batch->processhit_dealt_damage_pending = (uint16_t*)alloc_calloc(count, sizeof(uint16_t));
  if (batch->processhit_pending == NULL || batch->processhit_pending_valid == NULL ||
      batch->processhit_shield_pending == NULL || batch->processhit_dealt_damage_pending == NULL) {
    combat_processhit_pending_free(batch);
    return -1;
  }
  return 0;
}

void combat_processhit_pending_free(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  alloc_free(batch->processhit_pending_valid);
  alloc_free(batch->processhit_pending);
  alloc_free(batch->processhit_shield_pending);
  alloc_free(batch->processhit_dealt_damage_pending);
  batch->processhit_pending_valid = NULL;
  batch->processhit_pending = NULL;
  batch->processhit_shield_pending = NULL;
  batch->processhit_dealt_damage_pending = NULL;
  batch->processhit_collecting = 0u;
}

void combat_processhit_pending_begin(MslBatch* batch) {
  if (batch == NULL || batch->processhit_pending == NULL ||
      batch->processhit_pending_valid == NULL || batch->processhit_shield_pending == NULL ||
      batch->processhit_dealt_damage_pending == NULL) {
    return;
  }
  const size_t count = (size_t)batch->batch_size * (size_t)MSL_MAX_PLAYERS;
  memset(batch->processhit_pending_valid, 0, count * sizeof(uint8_t));
  memset(batch->processhit_dealt_damage_pending, 0, count * sizeof(uint16_t));
  for (size_t i = 0u; i < count; i++) {
    batch->processhit_shield_pending[i].valid = 0u;
  }
  // x221C_b5 publishes the most recent inert HitCapsule/ShieldDesc traversal. Clear the previous
  // publication before priority-13 produces this pass, so a new contact remains observable until
  // the next collision pass.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
      batch->state.state_flags[flags_i] &=
          (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD;
    }
  }
  batch->processhit_collecting = 1u;
}

static inline uint8_t combat_apply_throw_hit_core(MslBatch* batch, int batch_index, int attacker,
                                                  int defender, const MslThrowHitboxParams* p,
                                                  uint8_t update_bookkeeping,
                                                  uint8_t stale_excludes_current_instance,
                                                  uint8_t use_throw_weight,
                                                  uint8_t apply_throw_release_di);
static inline void combat_throw_release_apply_immediate_di(MslBatch* batch, size_t victim_idx,
                                                           const MslCommonParams* c);
static inline void combat_processhit_apply_shield(MslBatch* batch, size_t a_idx, size_t d_idx,
                                                  int a_max_int_dmg, int d_max_int_dmg,
                                                  int shield_damage_taken,
                                                  uint16_t attacker_motion_id, uint8_t hit_element);
static inline void combat_processhit_apply_body_phantom(MslBatch* batch, size_t a_idx, size_t d_idx,
                                                        int attacker, float dmg_f, uint8_t element);

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
  if (batch->processhit_collecting != 0u && batch->processhit_dealt_damage_pending != NULL) {
    const uint16_t bounded = damage > (int)UINT16_MAX ? UINT16_MAX : (uint16_t)damage;
    if (bounded > batch->processhit_dealt_damage_pending[idx]) {
      batch->processhit_dealt_damage_pending[idx] = bounded;
    }
    // x1914 is also the sole gate for deal_dmg_cb. The callback's branch priority is resolved
    // after every collision producer has run by falcon_specials_processhit_consume().
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    falcon_specials_processhit_note_dealt_x1914(batch, idx);
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
  // x1914 is also the sole gate for deal_dmg_cb. Keep callback ownership on this shared producer
  // so fighter HitCapsule contacts against item hurtboxes and item HitCapsules cannot bypass
  // character callbacks such as grounded Falcon Kick's cumulative slowdown.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/it/itcoll.c::{it_802703E8,it_8026D564}
  falcon_specials_processhit_note_dealt_x1914(batch, idx);
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
  const float damage_mul = batch->state.smash_charge_damage_mul[a_idx];
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

static inline void combat_damage_enter_state(const MslCommonParams* c, MslBatch* batch, int bi,
                                             size_t d_idx, uint8_t defender_on_ground_before,
                                             uint8_t defender_on_ground_after, uint8_t hurt_height,
                                             float kb_applied, float kb_angle_rad,
                                             uint16_t raw_kb_angle, size_t source_a_idx,
                                             size_t source_hb_i, uint8_t source_hb_valid,
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
        // The source gate is independent of attacker action, HitCapsule identity, and replay row.
        // Fighter_8006CDA4 can advance the same RNG stream beforehand only from actual held-item
        // or x197C fighter state. Neither source state exists in the supported item-free runtime,
        // so there is no inferred pre-gate phase here.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
        // Fighter_ProcessHit commits x1838_percentTemp through Fighter_UnkTakeDamage_8006CC30
        // before ftCo_8008DCE0 reaches the roll gate. Runtime defers the shared percent write until
        // the end of ProcessHit, so read the equivalent committed value here.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (block_33)
        const float percent_at_gate =
            batch->state.percent[d_idx] + batch->state.percent_temp[d_idx];
        if (percent_at_gate >= (float)c->damagefly_roll_percent_threshold) {
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
    batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
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
  // This is the explicit ftAnim_8006EBA4 in ftCo_8008DCE0, not the ordinary priority-1 Anim
  // callback. It runs inside Fighter_ProcessHit even though the contact has already armed hitlag,
  // so interpret the new AObj/JObj and command script without the ordinary hitlag gate.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  msl_anim_timebase_tick_once_interpret(batch, d_idx);
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
    if (batch->state.action_id[ev->d_idx] == (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) {
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

  if (batch->processhit_collecting != 0u && batch->processhit_pending != NULL &&
      batch->processhit_pending_valid != NULL) {
    MslCombatProcessHitResolved* pending = &batch->processhit_pending[ev->d_idx];
    uint8_t* valid = &batch->processhit_pending_valid[ev->d_idx];
    if (*valid == 0u || ev->kb_applied > pending->kb_applied) {
      *pending = *ev;
    } else {
      // Priority-13 producers have already accumulated percentTemp. ProcessHit selects the largest
      // knockback packet, while retaining the largest victim hitlag contribution from lower-KB
      // contacts in the same frame.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007A06C
      // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      if (ev->d_hl > pending->d_hl) {
        pending->d_hl = ev->d_hl;
      }
    }
    *valid = 1u;
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

  const uint16_t hs = combat_damage_hitstun_from_kb(c, ev->kb_applied);
  batch->state.hitstun[ev->d_idx] = hs;
  combat_state_flags_set_is_hitstun(batch, ev->d_idx, hs);
  if (ev->clear_x221c_on_damage_entry != 0u) {
    combat_state_flags_clear_x221c_b0(batch, ev->d_idx);
  }
  combat_damage_mark_entry_time_since_hit(batch, ev->d_idx);

  const uint8_t defender_on_ground_after = batch->state.on_ground[ev->d_idx] ? 1u : 0u;
  const uint16_t pre_entry_action = batch->state.action_id[ev->d_idx];
  combat_damage_enter_state(c, batch, ev->bi, ev->d_idx, ev->defender_on_ground,
                            defender_on_ground_after, ev->hurt_height, ev->kb_applied,
                            ev->kb_angle_rad, ev->damage_state_raw_angle, ev->a_idx,
                            ev->source_hb_i, ev->source_hb_valid, ev->force_tumble_severity);
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
  if (ev->d_hl != 0u && batch->state.on_ground[ev->d_idx] == 0u &&
      msl_coll_handler_is_damage(msl_motion_state_coll_handler_kind(
          batch->state.char_id[ev->d_idx], batch->state.action_id[ev->d_idx]))) {
    combat_publish_damage_entry_hitlag_ecb_current(batch, ev->d_idx);
  }
  if (ev->source_item_owns_motion_clear != 0u &&
      batch->state.coll_damage_hitlag_ecb_valid[ev->d_idx] != 0u) {
    // Thrown-Needle destruction must not clear the victim's already-published CollData owner.
    // refs/melee/src/melee/it/items/itseakneedlethrown.c::it_2725_Logic109_DmgDealt
    batch->state.coll_damage_hitlag_ecb_source_kind[ev->d_idx] =
        MSL_DAMAGE_HITLAG_ECB_SOURCE_THROWN_NEEDLE;
  }
  combat_processhit_apply_hitlag_after_entry(batch, ev);
  if (ev->apply_guard_reflect_followup != 0u) {
    combat_apply_guard_reflect_body_hit_followup(c, batch, ev->d_idx, ev->d_motion_id);
  }
  combat_processhit_write_source(batch, ev);
  combat_processhit_apply_bookkeeping(batch, ev);
}

static inline void combat_processhit_clear_phantom_damage(MslBatch* batch, size_t idx) {
  batch->state.phantom_damage_pending_x1898[idx] = 0.0f;
  batch->state.phantom_damage_timer_x189c[idx] = 0u;
  batch->state.phantom_damage_source_port[idx] = 0xFFu;
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
                                                        uint8_t exclude_current_instance,
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
  // Ordinary Throw* capsules are created before same-instance release contacts can update the stale
  // queue, so they exclude their current instance. Falcon Dive release is different: the CATCH
  // contact has already stale-queued the same move/instance before doCatchAnim creates Throw0's
  // hit capsule, so that release damage must count the current queue entry.
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{ftCa_SpecialLw_800E5128,doCatchAnim}
  // refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071E04
  const float stale_mult = staling_multiplier_for_move_excluding_instance(
      batch, a_idx, move_id, exclude_current_instance ? attack_instance : 0u);
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
  // Priority-13 collision stores the maximum environment damage in attacker dmg.x1914.
  // Priority-14 Fighter_ProcessHit owns hitlag and branch precedence; applying it here would let
  // one accepted contact alter the remainder of the same collision traversal.
  // Stale/combo bookkeeping remains immediate through ftColl_8007891C below.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_8007891C}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  (void)attacker_motion_id;
  combat_apply_deal_hitlag_raw_damage(batch, a_idx, prod->env_dmg);
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
      (uint8_t)(d_grab_owner != 0xFFu && ((d_grab_owner == (uint8_t)attacker &&
                                           msl_action_is_grabbed_victim(e->defender_motion_id)) ||
                                          (d_grab_owner != (uint8_t)attacker &&
                                           msl_action_is_thrown_victim(e->defender_motion_id))));
  e->attacker_attack_id = attacker_attack_id;
  e->attacker_instance_id = attacker_instance_id;
  e->hitbox_angle = batch->state.hitbox_angle[hb_i];
  e->hitbox_kbg = batch->state.hitbox_kbg[hb_i];
  e->hitbox_wsk = batch->state.hitbox_wsk[hb_i];
  e->hitbox_bkb = batch->state.hitbox_bkb[hb_i];
  e->hitcapsule_int_dmg = prod->hitcapsule_int_dmg;
  e->env_dmg = prod->env_dmg;
  e->effect_damage = prod->applied_damage;
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
  falcon_specials_processhit_note_higher_priority(batch, d_idx);
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

static inline uint8_t combat_damage_effect_owns_async_normal(uint8_t element) {
  // ftColl_803C0CAC maps these three HitElements to effect 0x3E8. All other elements select a
  // deterministic effect (or no effect) and therefore do not enter efAsync's HSD_Randi(8) path.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_803C0CAC
  return (uint8_t)(element == (uint8_t)MSL_HIT_ELEMENT_NORMAL ||
                   element == (uint8_t)MSL_HIT_ELEMENT_GROUND ||
                   element == (uint8_t)MSL_HIT_ELEMENT_CAPE);
}

static inline void combat_damage_effect_rng_consume(const MslCommonParams* c, MslBatch* batch,
                                                    int bi, size_t d_idx, uint8_t element,
                                                    float damage, float kb) {
  if (c == NULL || batch == NULL) {
    return;
  }

  if (element == (uint8_t)MSL_HIT_ELEMENT_SLASH) {
    // ftColl_803C0CAC maps Slash to effect 0x3EC. Its async dispatcher applies a random Z
    // rotation after creating the attached effect, consuming one HSD_Randf before ProcessHit.
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_803C0CAC,ftColl_8007A06C}
    // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch case 0x3EC
    combat_rng_consume_step_site(batch, bi, MSL_RNG_SITE_FTCOLL_DAMAGE_EFFECT);
    return;
  }
  if (!combat_damage_effect_owns_async_normal(element)) {
    return;
  }

  // ftColl_8007A06C visits every accepted DmgLog entry in insertion order. Normal/Ground/Cape
  // entries call ftColl_80078538 before best-KB selection:
  // - low-KB effect 0x3E8 consumes HSD_Randi(8) in efAsync_Dispatch;
  // - integer damage >= 1 consumes one more draw selected by defender co_attrs.xA0.
  // Visual results are outside the simulator domain, but these draws share the gameplay RNG
  // stream with the later DamageFlyRoll gate and therefore remain causal gameplay state.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007A06C,ftColl_80078538}
  // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch
  if (kb < c->damage_effect_async_kb_threshold) {
    (void)combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_FTCOLL_DAMAGE_EFFECT, 8);
  }
  if ((uint32_t)damage < 1u) {
    return;
  }
  const MslCharParams* d_ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  if (d_ch == NULL) {
    return;
  }
  int32_t range = 0;
  uint8_t kind0 = 0u;
  if (d_ch->damage_effect_randi_kind == 0u) {
    range = c->damage_effect_randi_range_kind0;
    kind0 = 1u;
  } else if (d_ch->damage_effect_randi_kind == 1u) {
    range = c->damage_effect_randi_range_kind1;
  }
  if (range > 0) {
    const int32_t result =
        combat_rng_consume_randi_site(batch, bi, MSL_RNG_SITE_FTCOLL_DAMAGE_EFFECT, range);
    if (kind0 != 0u && result == 0) {
      // xA0 kind 0 conditionally spawns effect 0x3EF. Its generator 0x42 is initialized by
      // hsd_8039F05C and consumes the number of HSD_Randf steps extracted from EfCoData.dat.
      // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078538
      // refs/melee/src/melee/ef/efasync.c::efAsync_Dispatch case 0x3EF
      // refs/melee/src/sysdolphin/baselib/particle.c::hsd_8039F05C
      for (uint8_t i = 0u; i < c->damage_effect_kind0_spawn_rng_steps; i++) {
        combat_rng_consume_step_site(batch, bi, MSL_RNG_SITE_FTCOLL_DAMAGE_EFFECT);
      }
    }
  }
}

static inline void combat_body_damage_log_collapse(const MslCommonParams* c, MslBatch* batch,
                                                   int bi,
                                                   const MslCombatBodyDamageScratch* scratch,
                                                   float* out_best_kb, uint8_t* out_best_i) {
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
    combat_damage_effect_rng_consume(c, batch, bi, e->d_idx, e->element, e->effect_damage, kb);
    if (kb > *out_best_kb) {
      *out_best_kb = kb;
      *out_best_i = i;
    }
  }
}

static inline uint8_t combat_attached_throw_body_hit_suppresses_victim_hitlag(
    const MslBatch* batch, const MslCombatBodyDamageLogEntry* e) {
  if (batch == NULL || e == NULL || e->attached_grabbed_victim == 0u ||
      batch->state.grab_owner_port[e->d_idx] != (uint8_t)e->attacker ||
      !msl_action_is_throw_owner(e->attacker_motion_id)) {
    return 0u;
  }
  if (e->hitbox_kbg != 0u || e->hitbox_bkb != 0u) {
    return 0u;
  }
  // The attached-victim link is cleared by the release callback before contact resolution.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  return 1u;
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
  return (batch->state.grab_owner_port[e->d_idx] == (uint8_t)e->attacker &&
          batch->state.char_id[e->a_idx] == (uint8_t)MSL_CHAR_ID_FALCON &&
          e->attacker_motion_id == (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH &&
          batch->state.action_id[e->d_idx] == (uint16_t)MSL_ACT_CAPTURE_CAPTAIN)
             ? 1u
             : 0u;
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
  combat_body_damage_log_collapse(c, batch, bi, scratch, &best_kb, &best_i);

  const MslCombatBodyDamageLogEntry* e = &scratch->entries[best_i];
  const size_t d_idx = e->d_idx;
  const size_t a_idx = e->a_idx;
  const float d_hitlag_mul = combat_hitlag_mul_from_element(c, e->element);
  const uint16_t d_hl =
      combat_calc_hitlag_frames(c, scratch->max_env_dmg, e->defender_motion_id, d_hitlag_mul);
  const uint16_t d_hl_prev = batch->state.hitlag[d_idx];
  const uint8_t d_hl_increased = (d_hl > d_hl_prev) ? 1u : 0u;

  const uint8_t attachment_still_live =
      (e->attached_grabbed_victim && batch->state.grab_owner_port[d_idx] != 0xFFu) ? 1u : 0u;
  MslCombatDamageApplyClass body_damage_class =
      attachment_still_live ? MSL_COMBAT_DAMAGE_ATTACHED_SUPPRESSED : MSL_COMBAT_DAMAGE_FULL;
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
    if (batch->state.hitbox_only_hit_grabbed[e->hb_i] != 0u) {
      // CaptureDamage is a victim ProcessHit consequence (`ftCo_Damage.c::inlineF0`), not a
      // HitCapsule-create callback. Restart it only after this attached BODY log is accepted.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineF0
      (void)grab_flow_on_attached_body_damage(batch, d_idx);
    }
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
  // Every ftCommon_8007D5D4 ground-to-air transition installs the same ten-frame ECB lock.
  // It is not conditional on the attacking move or selected HitCapsule.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  ev.grounded_ecb_lock_owner = 1u;
  ev.clear_x221c_on_damage_entry = 1u;
  ev.hurt_height = e->hurt_height;
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

uint8_t combat_source_body_log_record(MslBatch* batch, MslCombatBodyDamageScratch* scratch, int bi,
                                      int attacker, int defender, int hb_id, int cap_id) {
  if (batch == NULL || scratch == NULL || attacker < 0 || defender < 0 ||
      attacker >= (int)batch->config.num_players || defender >= (int)batch->config.num_players ||
      hb_id < 0 || hb_id >= MSL_MAX_HITBOXES || cap_id < 0 || cap_id >= MSL_MAX_HURTCAPS) {
    return 0u;
  }
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const size_t cap_i = idx_hurtcap(bi, defender, cap_id);
  return combat_body_damage_log_record(
      batch, scratch, a_idx, d_idx, attacker, defender, hb_i, cap_i,
      combat_get_env_dmg(batch->state.hitbox_damage[hb_i]), batch->state.action_id[a_idx],
      batch->state.attack_id[a_idx], batch->state.attack_instance[a_idx],
      batch->state.instance_id[a_idx], 0u,
      hitlist_hit_group_from_u16_7(batch->state.hitbox_u16_7[hb_i]),
      hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]));
}

void combat_source_body_log_apply(MslBatch* batch, int bi, MslCombatBodyDamageScratch* scratch) {
  combat_body_damage_log_apply(batch, bi, scratch);
}

void combat_source_body_phantom(MslBatch* batch, int bi, int attacker, int defender, int hb_id,
                                uint8_t hit_group) {
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  combat_processhit_apply_body_phantom(batch, a_idx, d_idx, attacker,
                                       batch->state.hitbox_damage[hb_i],
                                       batch->state.hitbox_element[hb_i]);
  hitlist_register_fighter_group_v2(batch, bi, attacker, hit_group, defender,
                                    batch->state.instance_id[d_idx], (int)MSL_LBCOLL_INSERT_FT_BODY,
                                    0u);
}

void combat_source_body_invincible(MslBatch* batch, int bi, int attacker, int defender, int hb_id,
                                   uint8_t hit_group, uint8_t rehit_frames) {
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  MslCombatDamageProduct product;
  if (combat_body_damage_producer_build(
          batch, a_idx, d_idx, hb_i, combat_get_env_dmg(batch->state.hitbox_damage[hb_i]),
          batch->state.attack_id[a_idx], batch->state.attack_instance[a_idx], 0u, &product)) {
    combat_body_damage_producer_apply_attacker_side(batch, a_idx, &product,
                                                    batch->state.action_id[a_idx]);
  }
  hitlist_register_fighter_group(batch, bi, attacker, hit_group, defender,
                                 batch->state.instance_id[d_idx], (int)MSL_LBCOLL_INSERT_FT_BODY,
                                 rehit_frames);
}

void combat_source_shield_apply(MslBatch* batch, int bi, int attacker, int defender,
                                int max_int_damage, int shield_damage_taken, uint8_t element) {
  if (batch == NULL || attacker < 0 || defender < 0 || attacker >= (int)batch->config.num_players ||
      defender >= (int)batch->config.num_players) {
    return;
  }
  const size_t a_idx = msl_idx_player(bi, attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  if (batch->processhit_collecting != 0u && batch->processhit_shield_pending != NULL) {
    MslCombatShieldPending* pending = &batch->processhit_shield_pending[d_idx];
    uint32_t total = (uint32_t)(shield_damage_taken > 0 ? shield_damage_taken : 0);
    if (pending->valid != 0u) {
      total += (uint32_t)pending->damage_total;
    }
    pending->damage_total = total > UINT16_MAX ? UINT16_MAX : (uint16_t)total;
    if (pending->valid == 0u || max_int_damage > (int)pending->max_damage) {
      pending->max_damage =
          max_int_damage > (int)UINT16_MAX ? UINT16_MAX : (uint16_t)max_int_damage;
      pending->source_player = (uint8_t)attacker;
      pending->element = element;
    }
    pending->valid = 1u;
    return;
  }
  combat_processhit_apply_shield(batch, a_idx, d_idx, max_int_damage, max_int_damage,
                                 shield_damage_taken, batch->state.action_id[a_idx], element);
}

static inline void combat_processhit_apply_body_phantom(MslBatch* batch, size_t a_idx, size_t d_idx,
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
  falcon_specials_processhit_note_higher_priority(batch, d_idx);
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
  falcon_specials_processhit_note_higher_priority(batch, d_idx);
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
                                       float item_vel_x, uint8_t item_damage_facing_owner_valid) {
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
  const uint8_t item_body_hit_keeps_article = item_article_params_body_hit_keeps_article(item_type);

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

  const uint8_t d_grab_owner = batch->state.grab_owner_port[d_idx];
  const uint8_t d_is_attached_grabbed_victim =
      (d_grab_owner != 0xFFu && d_grab_owner == (uint8_t)attacker &&
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
  falcon_specials_processhit_note_higher_priority(batch, d_idx);
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
  const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;
  const MslCharParams* d_ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  if (d_ch == NULL) {
    return MSL_ITEM_HIT_NONE;
  }
  const float kb_applied = combat_damage_calc_kb_applied(
      c, d_ch, d_motion_id, percent_pre, dmg_temp, damage_product.kb_damage_i, kbg, wsk, bkb, 1.0f,
      batch->state.dmg_x2225_b7[d_idx], batch->state.dmg_x2224_b2[d_idx],
      batch->state.kb_smashcharge_active[d_idx]);
  combat_damage_effect_rng_consume(c, batch, batch_index, d_idx, element,
                                   damage_product.applied_damage, kb_applied);
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
    if (item_body_hit_keeps_article != 0u) {
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
    if (item_body_hit_keeps_article != 0u) {
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

  // Combo count + last-attack tracking (attacker-side).
  // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C -> ftColl_800763C0(item attack id domain).
  combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, damage_product.move_id);

  // Illusion/Phantasm body hits do not destroy the article on hit; itFoxIllusion_Logic14_DmgDealt
  // clears an item var and returns false so the article persists through the hitlag window.
  // refs/melee/src/melee/it/items/itfoxillusion.c::itFoxIllusion_Logic14_DmgDealt
  if (item_body_hit_keeps_article != 0u) {
    return MSL_ITEM_HIT_APPLIED_DONT_CONSUME;
  }
  return MSL_ITEM_HIT_APPLIED_CONSUME_ITEM;
}

static inline uint8_t combat_apply_throw_hit_core(MslBatch* batch, int batch_index, int attacker,
                                                  int defender, const MslThrowHitboxParams* p,
                                                  uint8_t update_bookkeeping,
                                                  uint8_t stale_excludes_current_instance,
                                                  uint8_t use_throw_weight,
                                                  uint8_t apply_throw_release_di) {
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

  // Throw release has its own hit-status ordering, distinct from ordinary BODY admission:
  // - ftCo_800DDDE4 first clears the victim's script-owned x1988 through ftColl_8007B62C(..., 0),
  // - then ftColl_8007B868 tests the remaining x198C lane only and selects either hit damage or 0,
  // - either way it continues through knockback, damage-state entry, detach, and D5D4 release.
  // `hurtbox_state` is the replay-visible merged x1988/x198C lane; publish the exposed x198C value
  // immediately so the cleared x1988 cannot leak into later same-frame owners.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B62C,ftColl_8007B868}
  const uint8_t remaining_x198c = batch->state.colanim_hit_status_x198c[d_idx];
  batch->state.hurtbox_state[d_idx] = remaining_x198c;
  const uint8_t defender_no_damage = (remaining_x198c != 0u) ? 1u : 0u;

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
  if (!combat_throw_damage_product_build(batch, a_idx, p, move_id, stale_excludes_current_instance,
                                         &damage_product)) {
    return 0;
  }

  // Percent-temp accumulation (BODY): fp->dmg.x1838_percentTemp.
  const float percent_pre = batch->state.percent[d_idx];
  if (defender_no_damage == 0u) {
    batch->state.percent_temp[d_idx] += damage_product.applied_damage;
    falcon_specials_processhit_note_higher_priority(batch, d_idx);
  }
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
  if (use_throw_weight != 0u && c->throw_kb_weight_x10c > 0.0f) {
    d_ch_throw.weight = c->throw_kb_weight_x10c;
  }

  // The release owner has already run ftCo_800DDDE4 before entering this damage owner. For an
  // ordinary throw, DDDE4 applies D5D4 to the victim and conditionally unlocks its constrained
  // ECB; Falcon Dive's x221B_b7 branch applies D5D4 to Falcon and leaves the victim's ground state
  // for ftCo_8008DCE0. Do not repeat D5D4 here: doing so recreated the ten-frame ECB lock after
  // DDDE4 had explicitly unlocked it and made low throws falsely collide with the floor.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE7C0
  // refs/melee/src/melee/ft/chara/ftCaptain/ftCa_SpecialHi.c::{ftCa_SpecialLw_800E5128,doCatchAnim}
  const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;

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
  ev.use_grounded_kb = 1u;
  ev.grounded_ecb_lock_owner = 1u;
  ev.apply_throw_release_di = apply_throw_release_di;
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
  // Keep only the source damage-entry immediate tick here; ProcessHit consumes accumulated percent
  // later in the frame without adding a second motion tick.

  return 1;
}

uint8_t combat_apply_throw_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                               const MslThrowHitboxParams* p) {
  return combat_apply_throw_hit_core(batch, batch_index, attacker, defender, p, 1u, 1u, 1u, 1u);
}

uint8_t combat_apply_throw_hit_falcon_dive_release(MslBatch* batch, int batch_index, int attacker,
                                                   int defender, const MslThrowHitboxParams* p) {
  return combat_apply_throw_hit_core(batch, batch_index, attacker, defender, p, 1u, 0u, 1u, 1u);
}

typedef struct MslCaptureHitSource {
  int attacker;
  float attack_ratio;
  float facing_dir_1;
  uint16_t instance_hit_by;
  uint16_t stale_move_id;
  uint16_t stale_attack_instance;
  uint16_t combo_attack_id;
  uint8_t last_hit_by;
  uint8_t update_bookkeeping;
} MslCaptureHitSource;

static uint8_t combat_capture_hit_event_build(MslBatch* batch, int bi, int defender,
                                              const MslThrowHitboxParams* p,
                                              const MslCaptureHitSource* source,
                                              MslCombatProcessHitResolved* out) {
  if (batch == NULL || p == NULL || source == NULL || out == NULL || bi < 0 ||
      bi >= batch->batch_size || defender < 0 || defender >= (int)batch->config.num_players ||
      source->attacker < 0 || source->attacker >= (int)batch->config.num_players) {
    return 0u;
  }
  const MslCommonParams* c = msl_common_params();
  const size_t a_idx = msl_idx_player(bi, source->attacker);
  const size_t d_idx = msl_idx_player(bi, defender);
  const MslCharParams* d_ch = msl_char_params_fast(batch->state.char_id[d_idx]);
  if (c == NULL || d_ch == NULL) {
    return 0u;
  }

  float coll_kb_mul = batch->state.match_damage_ratio[bi];
  coll_kb_mul *= source->attack_ratio;
  coll_kb_mul *= batch->state.defense_ratio[d_idx];
  if (!(coll_kb_mul > 0.0f)) {
    coll_kb_mul = 1.0f;
  }
  const float kb = combat_damage_calc_kb_applied(
      c, d_ch, batch->state.action_id[d_idx], batch->state.percent[d_idx],
      batch->state.percent_temp[d_idx], (int)p->damage, p->kbg, p->wsk, p->bkb, coll_kb_mul,
      batch->state.dmg_x2225_b7[d_idx], batch->state.dmg_x2224_b2[d_idx],
      batch->state.kb_smashcharge_active[d_idx]);
  const uint8_t on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;
  const float angle = combat_damage_calc_angle_radians(c, p->angle, on_ground, kb);
  float kb_vel = kb * c->kb_vel_mul;
  if (!on_ground && combat_damage_check_air_motion_kb_mul(c, batch, d_idx)) {
    kb_vel *= c->air_motion_kb_mul;
  }
  const float x = kb_vel * cosf(angle);
  const float y = kb_vel * sinf(angle);

  *out = (MslCombatProcessHitResolved){0};
  out->bi = bi;
  out->attacker = source->attacker;
  out->defender = defender;
  out->a_idx = a_idx;
  out->d_idx = d_idx;
  out->d_motion_id = batch->state.action_id[d_idx];
  out->kb_applied = kb;
  out->kb_angle_rad = angle;
  out->kb_x = -x * source->facing_dir_1;
  out->kb_y = y;
  out->defender_on_ground = on_ground;
  out->use_grounded_kb = 1u;
  out->grounded_ecb_lock_owner = 1u;
  out->hurt_height = 1u;
  out->damage_state_raw_angle = p->angle;
  out->instance_hit_by = source->instance_hit_by;
  out->last_hit_by = source->last_hit_by;
  out->source_write = MSL_PROCESS_HIT_SOURCE_WRITE_DIRECT;
  out->update_bookkeeping = source->update_bookkeeping;
  out->source_motion_id = batch->state.action_id[a_idx];
  out->source_hitcapsule_int_dmg = (int)p->damage;
  out->source_hitbox_angle = p->angle;
  out->source_hitbox_kbg = p->kbg;
  out->source_hitbox_bkb = p->bkb;
  out->stale_move_id = source->stale_move_id;
  out->stale_attack_instance = source->stale_attack_instance;
  out->combo_attack_id = source->combo_attack_id;
  return 1u;
}

uint8_t combat_apply_falcon_dive_capture_break_hit(MslBatch* batch, int bi, int falcon,
                                                   int victim) {
  MslThrowHitboxParams p = {0};
  if (batch == NULL) {
    return 0u;
  }
  const size_t fidx = msl_idx_player(bi, falcon);
  if (!fighter_script_throw_hitbox_params(batch, fidx, 1u, &p)) {
    return 0u;
  }
  const size_t vidx = msl_idx_player(bi, victim);

  // DCFD4 clears x1988, forces D5D4, computes ftColl_80079C70 from raw xDF4[1].unk_count before
  // adding its creation-time stale-scaled damage, then enters Damage with no hitlag. It does not
  // run DDDE4's remaining-x198C admission gate.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_CaptureCut.c::ftCo_800DCFD4
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_80079C70,ftColl_8007891C}
  batch->state.hurtbox_state[vidx] = batch->state.colanim_hit_status_x198c[vidx];
  combat_apply_ftCommon_8007D5D4_ground_to_air(batch, vidx);
  const float falcon_facing = batch->state.facing[fidx] ? 1.0f : -1.0f;
  const uint16_t move_id = batch->state.attack_id[fidx];
  const uint16_t attack_instance = batch->state.attack_instance[fidx];
  const MslCaptureHitSource source = {
      .attacker = falcon,
      .attack_ratio = batch->state.attack_ratio[fidx],
      .facing_dir_1 = -falcon_facing,
      .instance_hit_by = batch->state.instance_id[fidx],
      .stale_move_id = move_id,
      .stale_attack_instance = attack_instance,
      .combo_attack_id = move_id,
      .last_hit_by = combat_source_port0_for_attacker(batch, fidx, falcon),
      .update_bookkeeping = 1u,
  };
  MslCombatProcessHitResolved ev = {0};
  if (!combat_capture_hit_event_build(batch, bi, victim, &p, &source, &ev)) {
    return 0u;
  }
  const float stale_mult =
      staling_multiplier_for_move_excluding_instance(batch, fidx, move_id, attack_instance);
  batch->state.percent_temp[vidx] += p.damage * stale_mult;
  falcon_specials_processhit_note_higher_priority(batch, vidx);
  combat_processhit_apply_resolved_damage(msl_common_params(), batch, &ev);
  return 1u;
}

static uint8_t combat_falcon_dive_de854_stored_hit(MslBatch* batch, int bi, int falcon, int victim,
                                                   MslCombatProcessHitResolved* out) {
  MslThrowHitboxParams p = {0};
  if (batch == NULL || out == NULL) {
    return 0u;
  }
  const size_t fidx = msl_idx_player(bi, falcon);
  if (!fighter_script_throw_hitbox_params(batch, fidx, 1u, &p)) {
    return 0u;
  }
  const size_t vidx = msl_idx_player(bi, victim);
  const float falcon_facing = batch->state.facing[fidx] ? 1.0f : -1.0f;
  const uint16_t move_id = batch->state.attack_id[fidx];
  const uint16_t attack_instance = batch->state.attack_instance[fidx];
  const MslCaptureHitSource source = {
      .attacker = falcon,
      .attack_ratio = batch->state.attack_ratio[fidx],
      .facing_dir_1 = -falcon_facing,
      .instance_hit_by = batch->state.instance_id[fidx],
      .stale_move_id = move_id,
      .stale_attack_instance = attack_instance,
      .combo_attack_id = move_id,
      .last_hit_by = combat_source_port0_for_attacker(batch, fidx, falcon),
      .update_bookkeeping = 1u,
  };
  // DE854 computes xDF4[1] knockback from raw unk_count six against the direct incoming
  // percentTemp, then adds the capsule's creation-time stale-scaled damage. It does not enter
  // Damage or create hitlag; x1828 coordinates the later ProcessHit entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE854
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_80079C70,ftColl_8007891C}
  if (!combat_capture_hit_event_build(batch, bi, victim, &p, &source, out)) {
    return 0u;
  }
  const float stale_mult =
      staling_multiplier_for_move_excluding_instance(batch, fidx, move_id, attack_instance);
  batch->state.percent_temp[vidx] += p.damage * stale_mult;
  falcon_specials_processhit_note_higher_priority(batch, vidx);
  return 1u;
}

static uint8_t combat_falcon_dive_de2f0_release_hit(MslBatch* batch, int bi, int holder) {
  const MslCommonParams* c = msl_common_params();
  if (batch == NULL || c == NULL) {
    return 0u;
  }
  const MslThrowHitboxParams p = {
      .damage = (float)c->capture_release_hit_damage_x384,
      .angle = c->capture_release_hit_angle_x388,
      .kbg = c->capture_release_hit_kbg_x38c,
      .wsk = c->capture_release_hit_wsk_x390,
      .bkb = c->capture_release_hit_bkb_x394,
      .element = (uint8_t)c->capture_release_hit_element_x398,
      .sfx_kind = (uint8_t)c->capture_release_hit_sfx_kind_x3a0,
      .sfx_severity = (uint8_t)c->capture_release_hit_sfx_severity_x39c,
  };
  const size_t hidx = msl_idx_player(bi, holder);
  const float facing = batch->state.facing[hidx] ? 1.0f : -1.0f;
  const MslCaptureHitSource source = {
      .attacker = holder,
      .attack_ratio = 1.0f,
      .facing_dir_1 = facing,
      .instance_hit_by = 0u,
      .last_hit_by = 6u,
      .update_bookkeeping = 0u,
  };
  MslCombatProcessHitResolved ev = {0};
  if (!combat_capture_hit_event_build(batch, bi, holder, &p, &source, &ev)) {
    return 0u;
  }
  // DE2F0 clears attack attribution, applies the extracted zero-damage/BKB-20 descriptor, and
  // enters Damage without hitlag.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DE2F0
  batch->state.instance_hit_by[hidx] = 0u;
  msl_damage_source_write_direct(batch, hidx, 6u);
  combat_processhit_apply_resolved_damage(c, batch, &ev);
  return 1u;
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
  falcon_specials_processhit_note_higher_priority(batch, d_idx);

  // Counter is the other supported ShieldDesc callback owner. The common item selector has
  // already admitted this descriptor overlap; consume x19A4 through Marth's shield_hit_cb rather
  // than routing the packet into common GuardSetOff.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077688,ftColl_8007B1B8}
  // refs/melee/src/melee/ft/chara/ftMars/ftMs_SpecialLw.c::ftMs_SpecialLw_80139140
  if (marth_counter_apply_item_contact(batch, batch_index, attacker, defender, item_attack_id,
                                       item_attack_instance, damage, item_pos_x) != 0u) {
    return;
  }

  const int shield_damage_taken = int_dmg + (int)hitbox_shield_damage;
  const MslGuardShieldContact contact = {
      .max_int_damage = int_dmg,
      .shield_damage_taken = shield_damage_taken > 0 ? shield_damage_taken : 0,
      .source_pos_x = item_pos_x,
      .hit_element = hit_element,
      .source = MSL_GUARD_CONTACT_ITEM,
  };
  if (fighter_guard_apply_shield_contact(batch, batch_index, defender, &contact, NULL) == 0u) {
    return;
  }

  // Track the owner as the source for shield state (Slippi instance_hit_by/last_hit_by are BODY-only).
  (void)a_idx;
}

static inline void combat_processhit_apply_shield(MslBatch* batch, size_t a_idx, size_t d_idx,
                                                  int a_max_int_dmg, int d_max_int_dmg,
                                                  int shield_damage_taken,
                                                  uint16_t attacker_motion_id,
                                                  uint8_t hit_element) {
  if (batch == NULL) {
    return;
  }

  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  falcon_specials_processhit_note_higher_priority(batch, d_idx);
  if (a_max_int_dmg < 0) {
    a_max_int_dmg = 0;
  }
  const MslGuardShieldContact contact = {
      .max_int_damage = d_max_int_dmg,
      .shield_damage_taken = shield_damage_taken,
      .source_pos_x = batch->state.pos_x[a_idx],
      .hit_element = hit_element,
      .source = MSL_GUARD_CONTACT_FIGHTER,
  };
  MslGuardShieldContactResult result = {0};
  if (fighter_guard_apply_shield_contact(batch, (int)(d_idx / (size_t)MSL_MAX_PLAYERS),
                                         (int)(d_idx % (size_t)MSL_MAX_PLAYERS), &contact,
                                         &result) == 0u) {
    return;
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
  batch->state.hitlag[a_idx] = a_hl;
  combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);

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
    const float eval =
        result.lightshield_amount * (float)a_max_int_dmg * c->shield_attacker_ground_kb_mul +
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

uint8_t combat_source_catch_wall_obstructed(const MslBatch* batch, int bi, size_t a_idx,
                                            size_t d_idx) {
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

static inline void combat_processhit_apply_expired_phantom_damage(MslBatch* batch, int bi, int p,
                                                                  size_t idx) {
  float dmg = batch->state.phantom_damage_pending_x1898[idx];
  if (!(dmg > 0.0f) || !isfinite(dmg)) {
    combat_processhit_clear_phantom_damage(batch, idx);
    return;
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

static void combat_processhit_apply_capture_low_event(MslBatch* batch,
                                                      const MslCombatProcessHitResolved* ev) {
  if (batch == NULL || ev == NULL) {
    return;
  }
  if (ev->d_hl > ev->d_hl_prev) {
    batch->state.hitlag[ev->d_idx] = ev->d_hl;
    combat_state_flags_set_is_hitlag(batch, ev->d_idx, ev->d_hl);
    if (ev->hitlag_sets_x221a != 0u) {
      combat_state_flags_set_x221a_b3(batch, ev->d_idx);
    }
    if (ev->hitlag_allows_sdi != 0u) {
      combat_damage_allow_sdi_set(batch, ev->d_idx);
    }
  }
  combat_processhit_write_source(batch, ev);
  combat_processhit_apply_bookkeeping(batch, ev);
}

static void combat_processhit_resolve_falcon_dive_pairs(
    MslBatch* batch, int bi, MslCombatProcessHitResolved pending[MSL_MAX_PLAYERS],
    uint8_t valid[MSL_MAX_PLAYERS]) {
  if (batch == NULL) {
    return;
  }
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;
  for (int holder = 0; holder < num_players; holder++) {
    const size_t hidx = msl_idx_player(bi, holder);
    const uint8_t victim_u8 = batch->state.attached_victim_port[hidx];
    if (batch->state.char_id[hidx] != (uint8_t)MSL_CHAR_ID_FALCON ||
        batch->state.action_id[hidx] != (uint16_t)MSL_ACT_CA_SPECIAL_HI_CATCH ||
        victim_u8 == 0xFFu || victim_u8 >= (uint8_t)num_players || victim_u8 == (uint8_t)holder) {
      continue;
    }
    const int victim = (int)victim_u8;
    const size_t vidx = msl_idx_player(bi, victim);
    if (batch->state.action_id[vidx] != (uint16_t)MSL_ACT_CAPTURE_CAPTAIN ||
        batch->state.grab_owner_port[vidx] != (uint8_t)holder) {
      continue;
    }

    const uint8_t holder_hit = valid[holder] && pending[holder].kb_applied > 0.0f;
    const uint8_t victim_hit = valid[victim] && pending[victim].kb_applied > 0.0f;
    if (!holder_hit && !victim_hit) {
      continue;
    }
    if (holder_hit) {
      valid[holder] = 0u;
    }
    if (victim_hit) {
      valid[victim] = 0u;
    }
    const size_t vflags = vidx * (size_t)MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
    const uint8_t victim_low =
        victim_hit && ((batch->state.state_flags[vflags] & (uint8_t)MSL_STATE_FLAG_221C_B0) != 0u ||
                       batch->state.percent_temp[vidx] < (float)c->capture_damage_release_threshold)
            ? 1u
            : 0u;

    // ftCo_8008EC90 owns this as a linked pair while both x1A5C pointers and hold poses remain
    // live. Only the captured victim's inlineB1 predicate selects low/high behavior.
    // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Damage.s::ftCo_8008EC90
    if (holder_hit && !victim_hit) {
      (void)combat_apply_falcon_dive_capture_break_hit(batch, bi, holder, victim);
      grab_attachment_dc920_release_now(batch, bi, holder, victim,
                                        batch->state.falcon_specialhi_x221b_b7[hidx]);
      combat_processhit_apply_resolved_damage(c, batch, &pending[holder]);
      continue;
    }

    if (!holder_hit && victim_hit) {
      if (victim_low) {
        combat_processhit_apply_capture_low_event(batch, &pending[victim]);
        uint16_t pair_hitlag = batch->state.hitlag[vidx];
        if (batch->state.hitlag[hidx] > pair_hitlag) {
          pair_hitlag = batch->state.hitlag[hidx];
        }
        batch->state.hitlag[hidx] = pair_hitlag;
        batch->state.hitlag[vidx] = pair_hitlag;
      } else {
        grab_attachment_dc920_release_now(batch, bi, holder, victim,
                                          batch->state.falcon_specialhi_x221b_b7[hidx]);
        combat_processhit_apply_resolved_damage(c, batch, &pending[victim]);
        (void)combat_falcon_dive_de2f0_release_hit(batch, bi, holder);
      }
      continue;
    }

    if (victim_low) {
      MslCombatProcessHitResolved stored = {0};
      if (combat_falcon_dive_de854_stored_hit(batch, bi, holder, victim, &stored)) {
        stored.d_hl = pending[victim].d_hl;
        stored.d_hl_prev = pending[victim].d_hl_prev;
        stored.hitlag_mode = pending[victim].hitlag_mode;
        stored.hitlag_sets_x221a = pending[victim].hitlag_sets_x221a;
        stored.hitlag_allows_sdi = pending[victim].hitlag_allows_sdi;
        combat_processhit_apply_bookkeeping(batch, &pending[victim]);
        grab_attachment_dc920_release_now(batch, bi, holder, victim,
                                          batch->state.falcon_specialhi_x221b_b7[hidx]);
        combat_processhit_apply_resolved_damage(c, batch, &pending[holder]);
        combat_processhit_apply_resolved_damage(c, batch, &stored);
      }
    } else {
      grab_attachment_dc920_release_now(batch, bi, holder, victim,
                                        batch->state.falcon_specialhi_x221b_b7[hidx]);
      combat_processhit_apply_resolved_damage(c, batch, &pending[holder]);
      combat_processhit_apply_resolved_damage(c, batch, &pending[victim]);
    }
  }
}

void combat_processhit_resolve(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  batch->processhit_collecting = 0u;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    uint8_t dealt_hitlag_suppressed[MSL_MAX_PLAYERS] = {0u};
    MslCombatProcessHitResolved* pending =
        &batch->processhit_pending[(size_t)bi * (size_t)MSL_MAX_PLAYERS];
    uint8_t* valid = &batch->processhit_pending_valid[(size_t)bi * (size_t)MSL_MAX_PLAYERS];
    MslCombatShieldPending* shield =
        &batch->processhit_shield_pending[(size_t)bi * (size_t)MSL_MAX_PLAYERS];
    for (int defender = 0; defender < (int)batch->config.num_players; defender++) {
      if (shield[defender].valid == 0u ||
          shield[defender].source_player >= batch->config.num_players) {
        continue;
      }
      const int attacker = (int)shield[defender].source_player;
      dealt_hitlag_suppressed[defender] = 1u;
      combat_processhit_apply_shield(
          batch, msl_idx_player(bi, attacker), msl_idx_player(bi, defender),
          (int)shield[defender].max_damage, (int)shield[defender].max_damage,
          (int)shield[defender].damage_total, batch->state.action_id[msl_idx_player(bi, attacker)],
          shield[defender].element);
      shield[defender].valid = 0u;
    }
    // Phantom/tip-log countdown is part of this same ProcessHit pass. A live incoming-KB packet
    // supersedes the delayed damage exactly as source clears x189C before consuming kb_applied;
    // otherwise expiry applies before the lower-priority percent-temp branch.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    for (int p = 0; p < (int)batch->config.num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      if (valid[p] != 0u && pending[p].kb_applied != 0.0f) {
        dealt_hitlag_suppressed[p] = 1u;
        combat_processhit_clear_phantom_damage(batch, idx);
      } else {
        if (batch->state.phantom_damage_timer_x189c[idx] != 0u) {
          dealt_hitlag_suppressed[p] = 1u;
        }
        combat_processhit_apply_expired_phantom_damage(batch, bi, p, idx);
      }
    }
    combat_processhit_resolve_falcon_dive_pairs(batch, bi, pending, valid);
    const MslCommonParams* common = msl_common_params();
    if (common != NULL) {
      for (int p = 0; p < (int)batch->config.num_players; p++) {
        if (valid[p] != 0u) {
          valid[p] = 0u;
          combat_processhit_apply_resolved_damage(common, batch, &pending[p]);
        }
      }
      for (int p = 0; p < (int)batch->config.num_players; p++) {
        const size_t idx = msl_idx_player(bi, p);
        const int dealt_damage = (int)batch->processhit_dealt_damage_pending[idx];
        if (dealt_damage != 0 && dealt_hitlag_suppressed[p] == 0u) {
          const uint16_t hitlag =
              combat_calc_hitlag_frames(common, dealt_damage, batch->state.action_id[idx], 1.0f);
          batch->state.hitlag[idx] = hitlag;
          combat_state_flags_set_is_hitlag(batch, idx, hitlag);
        }
      }
    }
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
      if (batch->state.speciallw_counter_window[idx] == 1u &&
          (batch->state.action_id[idx] == (uint16_t)MSL_ACT_MS_SPECIAL_LW_HIT ||
           batch->state.action_id[idx] == (uint16_t)MSL_ACT_MS_SPECIAL_AIR_LW_HIT)) {
        batch->state.speciallw_counter_window[idx] = 0u;
      }
      if (combat_action_is_catch_family(batch->state.action_id[idx])) {
        // Catch-family Fighter_8006CDA4 pre-gate counts are replay seed reconstruction for a
        // same-frame severe-airborne DamageFlyRoll gate. If no gate consumed the marker this frame,
        // it must not persist into later Catch/throw gameplay; delayed carry owners such as
        // DamageFlyTop/AttackAir remain outside this Catch-family clear.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
      }
    }
  }

  combat_preserve_fresh_air_damage_entry_root_y(batch);
  // Resolve Falcon's accumulated ProcessHit packet after every item/fighter contact producer.
  // Fighter_ProcessHit's strict branch ladder suppresses dealt-damage and inert-detect callbacks
  // when a higher-priority shield/incoming-damage packet exists in the same frame.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  falcon_specials_processhit_consume(batch);
}

void combat_resolve(MslBatch* batch) {
  if (batch == NULL) {
    return;
  }
  fighter_contact_resolve_catch(batch);
  fighter_contact_resolve_damage(batch);
  combat_processhit_resolve(batch);
}

int combat_debug_select_body_hits(MslBatch* batch, int batch_index,
                                  MslDebugCombatContact* out_contacts, uint16_t max_contacts,
                                  uint16_t* out_count) {
  return fighter_contact_debug_select_body(batch, batch_index, out_contacts, max_contacts,
                                           out_count);
}
