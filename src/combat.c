#include "combat.h"

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "action_ids.h"
#include "action.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_table.h"
#include "anim_timebase.h"
#include "buttons.h"
#include "char_params.h"
#include "combat_geom.h"
#include "common_params.h"
#include "grab_flow.h"
#include "hit_elements.h"
#include "hitboxes_tables.h"
#include "hit_status_tables.h"
#include "hitlist.h"
#include "hurtbox_modes_tables.h"
#include "hurtcaps_tables.h"
#include "input_axis.h"
#include "laser_params.h"
#include "msl_math.h"
#include "mtx34.h"
#include "move_tables.h"
#include "shield_tilt_table.h"
#include "stage_collision.h"
#include "staling.h"

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
}

static inline size_t idx_hitbox_victim(int bi, int attacker, int hb_i, int victim) {
  return (((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
          (size_t)hb_i) *
             (size_t)MSL_MAX_PLAYERS +
         (size_t)victim;
}

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline uint8_t combat_apply_throw_hit_core(MslBatch* batch, int batch_index, int attacker,
                                                  int defender, const MslThrowHitboxParams* p,
                                                  uint8_t update_bookkeeping);

static inline void combat_throw_release_integrate_position_now(MslBatch* batch, size_t owner_idx,
                                                               size_t victim_idx);
static inline void combat_throw_release_apply_immediate_di(MslBatch* batch, size_t victim_idx,
                                                           const MslCommonParams* c);

static inline uint32_t combat_hsd_rand_step(uint32_t seed) {
  // HSD global RNG LCG step:
  // refs/melee/src/sysdolphin/baselib/random.c::{HSD_Rand,HSD_Randf}
  return seed * 214013u + 2531011u;
}

static inline uint16_t combat_hsd_rand_u16_consume_site(MslBatch* batch, int bi, uint16_t site_id) {
  if (batch == NULL || bi < 0 || bi >= batch->batch_size) {
    return 0u;
  }
  if (batch->debug_rng_shadow_seed == NULL || batch->debug_rng_site_counts == NULL) {
    return 0u;
  }
  if (site_id >= (uint16_t)MSL_RNG_SITE_COUNT) {
    return 0u;
  }
  const size_t bi_u = (size_t)bi;
  const size_t site_i = bi_u * (size_t)MSL_RNG_SITE_COUNT + (size_t)site_id;
  if (batch->debug_rng_site_counts[site_i] != 0xFFFFu) {
    batch->debug_rng_site_counts[site_i]++;
  }
  uint32_t seed = batch->debug_rng_shadow_seed[bi_u];
  seed = combat_hsd_rand_step(seed);
  batch->debug_rng_shadow_seed[bi_u] = seed;
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
  if (batch == NULL || batch->debug_rng_shadow_seed == NULL || batch->debug_rng_seed_in == NULL ||
      batch->debug_rng_seed_out == NULL || batch->debug_rng_site_counts == NULL) {
    return;
  }
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const size_t bi_u = (size_t)bi;
    const uint32_t seed_in = batch->state.frame_pre_random_seed[bi_u];
    batch->debug_rng_shadow_seed[bi_u] = seed_in;
    batch->debug_rng_seed_in[bi_u] = seed_in;
    batch->debug_rng_seed_out[bi_u] = seed_in;
    memset(batch->debug_rng_site_counts + bi_u * (size_t)MSL_RNG_SITE_COUNT, 0,
           (size_t)MSL_RNG_SITE_COUNT * sizeof(uint16_t));
  }
}

void combat_rng_trace_end_frame(MslBatch* batch) {
  if (batch == NULL || batch->debug_rng_shadow_seed == NULL || batch->debug_rng_seed_out == NULL) {
    return;
  }
  FILE* trace_file = NULL;
  if (batch->debug_rng_trace_enabled && batch->debug_rng_trace_file != NULL) {
    trace_file = (FILE*)batch->debug_rng_trace_file;
  }
  const uint64_t step_id = batch->debug_rng_trace_step_counter++;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    const size_t bi_u = (size_t)bi;
    batch->debug_rng_seed_out[bi_u] = batch->debug_rng_shadow_seed[bi_u];
    if (trace_file == NULL || batch->debug_rng_seed_in == NULL ||
        batch->debug_rng_site_counts == NULL) {
      continue;
    }
    const int32_t frame_id = batch->state.frame_id[bi_u];
    const uint32_t seed_in = batch->debug_rng_seed_in[bi_u];
    const uint32_t seed_out = batch->debug_rng_seed_out[bi_u];
    for (uint16_t site_id = 1; site_id < (uint16_t)MSL_RNG_SITE_COUNT; site_id++) {
      const size_t site_i = bi_u * (size_t)MSL_RNG_SITE_COUNT + (size_t)site_id;
      const uint16_t count = batch->debug_rng_site_counts[site_i];
      (void)fprintf(trace_file, "%llu\t%d\t%d\t%u\t%u\t%u\t%u\n", (unsigned long long)step_id, bi,
                    (int)frame_id, seed_in, seed_out, (unsigned int)site_id, (unsigned int)count);
    }
  }
}

static inline uint8_t sphere_sphere_intersects(float ax, float ay, float az, float ar, float bx,
                                               float by, float bz, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float rr = ar + br;
  return (dx * dx + dy * dy + dz * dz) <= (rr * rr);
}

static inline float combat_lbColl_804D7A38(void) {
  // lbColl_8000805C BODY path forwards arg11 = lbColl_804D7A38 * hurt_owner_scale_y.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
  // refs/melee/src/melee/lb/lbcollision.c (lbColl_804D7A38 = 3)
  return 3.0f;
}

static inline uint8_t combat_shine_start_damageair_entry_pose_bridge_applies(const MslBatch* batch,
                                                                             size_t a_idx,
                                                                             size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t a = batch->state.action_id[a_idx];
  if (a != (uint16_t)MSL_ACT_FX_SPECIAL_LW_START &&
      a != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START) {
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

static inline uint8_t combat_shine_start_grounded_ledge_ecb_lock_owner(const MslBatch* batch,
                                                                       size_t d_idx,
                                                                       uint16_t attacker_action) {
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
  if (attacker_action != (uint16_t)MSL_ACT_FX_SPECIAL_LW_START &&
      attacker_action != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START) {
    return 0u;
  }
  const uint32_t stage_id = batch->state.stage_id[d_idx / (size_t)MSL_MAX_PLAYERS];
  const MslStageFloorGraph* g = stage_collision_get_floor_graph(stage_id);
  const int line_idx = stage_collision_floor_line_index(stage_id, batch->state.ground_id[d_idx]);
  return (g != NULL && line_idx >= 0 && (size_t)line_idx < g->line_count &&
          g->lines[(size_t)line_idx].is_ledge)
             ? 1u
             : 0u;
}

static inline uint8_t combat_attackairlw_invincible_contact_rejects_body_hitlag(
    const MslBatch* batch, size_t a_idx, size_t d_idx) {
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

static inline uint8_t combat_shine_start_damageair_entry_pose_allows_body_contact(
    const MslBatch* batch, size_t a_idx, size_t d_idx, uint8_t cap_id, float hx, float hy, float hz,
    float hr) {
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

  uint32_t can_hit_mask = 0xFFFFFFFFu;
  (void)hurtbox_modes_can_hit_mask(char_id, (uint16_t)MSL_SM_DAMAGE_AIR_2, /*frame=*/0u,
                                   cap_count_u16, &can_hit_mask);
  if (((can_hit_mask >> cap_id) & 0x1u) == 0u) {
    return 0u;
  }

  const MslHurtCap* cap = &caps[cap_id];
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
  const MslCharParams* chp = msl_char_params(char_id);
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

static inline uint8_t combat_attackairb_enable_edge_model_scale_allows_body_contact(
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

  const MslCharParams* chp = msl_char_params(batch->state.char_id[a_idx]);
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

static inline uint8_t combat_body_overlap_lbColl_80006E58_subset_allows(const MslBatch* batch,
                                                                        size_t hb_i, size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  (void)hb_i;
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
  if (batch->state.on_ground[d_idx]) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t combat_body_overlap_lbColl_80006E58_scaffold(
    const MslBatch* batch, int bi, int attacker, int hb_id, float hx, float hy, float hz, float hr,
    float ax, float ay, float az, float bx, float by, float bz, float cr, float defender_scale_y) {
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
    arg11 = combat_lbColl_804D7A38() * defender_scale_y;
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

static inline uint8_t combat_catch_overlap_lbColl_80007ECC(const MslBatch* batch, int bi,
                                                           int attacker, int hb_id, float hx,
                                                           float hy, float hz, float hr, float ax,
                                                           float ay, float az, float bx, float by,
                                                           float bz, float cr) {
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

static inline uint8_t combat_guardreflect_no_submotion_catch_fallback_applies(const MslBatch* batch,
                                                                              size_t d_idx) {
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
  if (batch->state.prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON ||
      batch->state.seed_prev_action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t combat_guardreflect_expired_x14_body_fallback_applies(const MslBatch* batch,
                                                                            size_t d_idx) {
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

static inline uint8_t combat_guardreflect_catch_hurtcap_world(const MslBatch* batch, size_t d_idx,
                                                              const MslHurtCap* cap, float* out_ax,
                                                              float* out_ay, float* out_az,
                                                              float* out_bx, float* out_by,
                                                              float* out_bz, float* out_r) {
  if (batch == NULL || cap == NULL || out_ax == NULL || out_ay == NULL || out_az == NULL ||
      out_bx == NULL || out_by == NULL || out_bz == NULL || out_r == NULL) {
    return 0u;
  }
  if (!cap->is_grabbable) {
    return 0u;
  }

  // GuardReflect catch-only no-submotion fallback:
  // - ftCo_MS_GuardReflect uses the GuardOn submotion table.
  // - Slippi can serialize late GuardReflect snapshots with animation_index=-1/-2, while
  //   ftColl_80078A2C still checks `hurt_capsules[j].is_grabbable` for Catch/CatchDash selection.
  // - Keep this geometry local to catch selection; BODY hurtcaps remain absent for this
  //   no-submotion slice in hurtboxes_refresh().
  // refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_GuardReflect
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC
  const uint8_t char_id = batch->state.char_id[d_idx];
  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
  float m[12];
  if (anim_pose_get_collision_matrix_f32(batch, d_idx, (uint16_t)MSL_SM_GUARD_ON, (float)frame,
                                         cap->bone_part_id, m) != 0) {
    (void)char_id;
    return 0u;
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  float bx = 0.0f, by = 0.0f, bz = 0.0f;
  msl_mtx34_mul_point(m, cap->a_offset, &ax, &ay, &az);
  msl_mtx34_mul_point(m, cap->b_offset, &bx, &by, &bz);

  const MslCharParams* chp = msl_char_params(char_id);
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

static inline uint8_t combat_defender_downed_catch_mask_blocks(uint16_t action_id) {
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

static inline uint8_t combat_guardreflect_body_hurtcap_world(const MslBatch* batch, size_t d_idx,
                                                             const MslHurtCap* cap, uint8_t cap_id,
                                                             uint16_t cap_count, float* out_ax,
                                                             float* out_ay, float* out_az,
                                                             float* out_bx, float* out_by,
                                                             float* out_bz, float* out_r) {
  if (batch == NULL || cap == NULL || out_ax == NULL || out_ay == NULL || out_az == NULL ||
      out_bx == NULL || out_by == NULL || out_bz == NULL || out_r == NULL) {
    return 0u;
  }

  // Expired-x14 GuardReflect no-submotion BODY fallback:
  // - ftCo_GuardReflect_Anim calls ftCo_80093BC0 before ftColl_80078C70.
  // - When x14 expires, ftCo_80093BC0 recreates ShieldDesc through ftCo_80092450, but if that
  //   shield overlap misses, the fighter-vs-fighter BODY path still consumes the GuardReflect
  //   motion-state's GuardOn submotion hurtcaps.
  // - Keep this local to fighter BODY selection. Global hurtboxes_refresh() stays empty for this
  //   no-submotion slice so item collision does not turn stale/frozen GuardReflect rows into item
  //   BODY contacts.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_GuardReflect_Anim}
  // refs/melee/src/melee/ft/ftmotionstates.c::ftCo_MS_GuardReflect
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007B1B8,ftColl_80076ED8,ftColl_80078C70}
  uint32_t can_hit_mask = 0xFFFFFFFFu;
  (void)hurtbox_modes_can_hit_mask(batch->state.char_id[d_idx], (uint16_t)MSL_SM_GUARD_ON, 0u,
                                   cap_count, &can_hit_mask);
  if (((can_hit_mask >> cap_id) & 0x1u) == 0u) {
    return 0u;
  }

  const uint8_t char_id = batch->state.char_id[d_idx];
  const float anim_frame_f32 = msl_anim_frame_sanitize_f32(batch->state.anim_frame_f32[d_idx]);
  const uint16_t frame = msl_anim_frame_floor_u16(anim_frame_f32);
  float m[12];
  if (anim_pose_get_collision_matrix_f32(batch, d_idx, (uint16_t)MSL_SM_GUARD_ON, (float)frame,
                                         cap->bone_part_id, m) != 0) {
    return 0u;
  }

  float ax = 0.0f, ay = 0.0f, az = 0.0f;
  float bx = 0.0f, by = 0.0f, bz = 0.0f;
  msl_mtx34_mul_point(m, cap->a_offset, &ax, &ay, &az);
  msl_mtx34_mul_point(m, cap->b_offset, &bx, &by, &bz);

  const MslCharParams* chp = msl_char_params(char_id);
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

static inline uint8_t combat_hitbox_hitbox_overlap_lbColl_80007AFC(const MslBatch* batch,
                                                                   size_t hb0_i, size_t hb1_i) {
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

static inline uint8_t combat_hitbox_targets_fighter_ground_state(uint16_t hitbox_flags,
                                                                 uint8_t defender_on_ground) {
  // Fighter-vs-fighter collision filters each HitCapsule by the opponent's ground/air state before
  // the hitbox-vs-hitbox clank owner runs.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  if (defender_on_ground) {
    return (hitbox_flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) != 0 ? 1u : 0u;
  }
  return (hitbox_flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) != 0 ? 1u : 0u;
}

static inline void combat_clank_skip_same_hit_group(
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

static inline void combat_clank_candidate_skip_same_hit_group_all(
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

static inline uint8_t combat_mtx34_inverse_point(const float m[12], float x, float y, float z,
                                                 float* out_x, float* out_y, float* out_z) {
  if (m == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    return 0u;
  }
  const float a00 = m[0], a01 = m[1], a02 = m[2];
  const float a10 = m[4], a11 = m[5], a12 = m[6];
  const float a20 = m[8], a21 = m[9], a22 = m[10];
  const float tx = m[3], ty = m[7], tz = m[11];

  const float c00 = a11 * a22 - a12 * a21;
  const float c01 = a02 * a21 - a01 * a22;
  const float c02 = a01 * a12 - a02 * a11;
  const float c10 = a12 * a20 - a10 * a22;
  const float c11 = a00 * a22 - a02 * a20;
  const float c12 = a02 * a10 - a00 * a12;
  const float c20 = a10 * a21 - a11 * a20;
  const float c21 = a01 * a20 - a00 * a21;
  const float c22 = a00 * a11 - a01 * a10;
  const float det = a00 * c00 + a01 * c10 + a02 * c20;
  if (!(fabsf(det) > 1.0e-8f)) {
    return 0u;
  }
  const float inv_det = 1.0f / det;
  const float rx = x - tx;
  const float ry = y - ty;
  const float rz = z - tz;
  *out_x = inv_det * (c00 * rx + c01 * ry + c02 * rz);
  *out_y = inv_det * (c10 * rx + c11 * ry + c12 * rz);
  *out_z = inv_det * (c20 * rx + c21 * ry + c22 * rz);
  return 1u;
}

static inline uint8_t combat_is_damage_or_firefox_launch_victim_action(uint16_t action_id) {
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
    case MSL_ACT_FLY_REFLECT_WALL:
    case MSL_ACT_FLY_REFLECT_CEIL:
    case MSL_ACT_FX_SPECIAL_HI:
    case MSL_ACT_FX_SPECIAL_AIR_HI:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t combat_hitlist_victim_pointer_may_change(uint8_t stocks, uint16_t action_id) {
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

static inline uint8_t combat_is_downed_damage_contact_action(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_DOWN_BOUND_U:
    case (uint16_t)MSL_ACT_DOWN_WAIT_U:
    case (uint16_t)MSL_ACT_DOWN_DAMAGE_U:
    case (uint16_t)MSL_ACT_DOWN_BOUND_D:
    case (uint16_t)MSL_ACT_DOWN_WAIT_D:
    case (uint16_t)MSL_ACT_DOWN_DAMAGE_D:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint16_t combat_down_damage_action_from_source(uint16_t action_id) {
  // Decomp: ftCo_8009F184 selects DownDamageU only from DownWaitU; other downed source motions
  // route to DownDamageD.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_8009F184
  return (action_id == (uint16_t)MSL_ACT_DOWN_WAIT_U) ? (uint16_t)MSL_ACT_DOWN_DAMAGE_U
                                                      : (uint16_t)MSL_ACT_DOWN_DAMAGE_D;
}

static inline uint32_t combat_down_damage_submotion_from_action(uint16_t action_id) {
  return (action_id == (uint16_t)MSL_ACT_DOWN_DAMAGE_U) ? (uint32_t)MSL_SM_DOWN_DAMAGE_U
                                                        : (uint32_t)MSL_SM_DOWN_DAMAGE_D;
}

static inline uint8_t combat_float_aobj_hurtcap_pose_owner(uint16_t action_id) {
  switch (action_id) {
    case MSL_ACT_LANDING_AIR_N:
    case MSL_ACT_LANDING_AIR_F:
    case MSL_ACT_LANDING_AIR_B:
    case MSL_ACT_LANDING_AIR_HI:
    case MSL_ACT_LANDING_AIR_LW:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t combat_guard_no_tilt_current_pose_gap(const MslBatch* batch, size_t d_idx) {
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

static inline uint8_t combat_body_overlap_lbColl_80006E58_matrix_radius(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, int cap_id, float hx,
    float hy, float hz, float hr, float ax, float ay, float az, float bx, float by, float bz,
    float* out_overlap_amount, uint8_t* out_evaluated) {
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
  const uint8_t char_id = batch->state.char_id[d_idx];
  const uint32_t anim_u32 = batch->state.animation_index[d_idx];
  const uint16_t action_id = batch->state.action_id[d_idx];
  uint16_t msid = 0u;
  if (anim_u32 > 0xFFFFu) {
    if (action_id != (uint16_t)MSL_ACT_GUARD) {
      return 0u;
    }
    msid = (uint16_t)MSL_SM_GUARD;
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

  float m[12];
  const float pose_sample_frame =
      combat_float_aobj_hurtcap_pose_owner(action_id) ? anim_frame_f32 : (float)frame;
  if (anim_pose_get_collision_matrix_f32(batch, d_idx, msid, pose_sample_frame, cap->bone_part_id,
                                         m) != 0) {
    return 0u;
  }
  if (out_evaluated) {
    *out_evaluated = 1u;
  }

  const MslCharParams* chp = msl_char_params(char_id);
  const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                  ? chp->model_scaling
                                  : 1.0f;
  const float scale_y = batch->state.fighter_scale_y[d_idx];
  const float model_scale = scale_y * model_scaling;
  if (!(model_scale > 0.0f)) {
    return 0u;
  }
  const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
  const float pos_x = batch->state.pos_x[d_idx];
  const float pos_y = batch->state.pos_y[d_idx];
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
  if (!combat_mtx34_inverse_point(m, hit_pose_x / model_scale, hit_pose_y / model_scale,
                                  hit_pose_z / model_scale, &hit_local_x, &hit_local_y,
                                  &hit_local_z) ||
      !combat_mtx34_inverse_point(m, hurt_pose_x / model_scale, hurt_pose_y / model_scale,
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

static inline uint8_t combat_attackairb_continuation_body_overlap_exact(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, int cap_id, float hx,
    float hy, float hz, float hr, float ax, float ay, float az, float bx, float by, float bz,
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
  if (!combat_is_damage_or_firefox_launch_victim_action(v_action)) {
    return 0u;
  }
  return combat_body_overlap_lbColl_80006E58_matrix_radius(batch, bi, attacker, hb_id, defender,
                                                           cap_id, hx, hy, hz, hr, ax, ay, az, bx,
                                                           by, bz, out_overlap_amount, NULL);
}

static inline uint8_t combat_attackairb_stale_owner_continuation_candidate(
    const MslBatch* batch, size_t a_idx, size_t d_idx, float hitbox_damage,
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

static inline uint8_t combat_enable_edge_dense_seed_suppresses_body(const MslBatch* batch, int bi,
                                                                    int attacker, int hb_id,
                                                                    int defender,
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
      (attacker_action == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START) ? 1u : 0u;
  if (!down_attack_dense_lane && !shine_start_dense_lane) {
    return 0u;
  }
  if (!batch->state.hitbox_enable_edge[hb_i] && !shine_start_dense_lane) {
    return 0u;
  }

  const size_t valid_i =
      ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
      (size_t)hb_id;
  if (batch->state.combat_hitlist_hb_valid[valid_i]) {
    return 0u;
  }

  const size_t d_idx = msl_idx_player(bi, defender);
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
        combat_is_damage_or_firefox_launch_victim_action(batch->state.action_id[d_idx])) {
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
    if (batch->state.colanim_hitstun_x198c1_seed[d_idx] == 0u) {
      return 0u;
    }
    if (batch->state.last_hit_by[d_idx] != batch->state.source_port0[a_idx]) {
      return 0u;
    }
    if (batch->state.instance_hit_by[d_idx] == batch->state.instance_id[a_idx]) {
      return 0u;
    }
    (void)expected_hitlag;
    if (batch->state.hitstun[d_idx] > 1u) {
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

static inline uint8_t combat_attackairb_dense_seed_suppresses_full_body(const MslBatch* batch,
                                                                        int bi, int attacker,
                                                                        int hb_id, int defender,
                                                                        uint16_t defender_iid) {
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
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_DAMAGE_FLY_TOP ||
      batch->state.hitlag[a_idx] != 0u || batch->state.hitstun[a_idx] != 0u ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] == 0u ||
      batch->state.last_hit_by[d_idx] != batch->state.source_port0[a_idx] ||
      batch->state.instance_hit_by[d_idx] == batch->state.instance_id[a_idx]) {
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
  if (batch->state.combat_hitlist_cd[cd_i] != 0xFFFFu) {
    return 0u;
  }
  const uint16_t seed_iid = batch->state.combat_hitlist_victim_iid[cd_i];
  if (seed_iid != 0u && seed_iid != defender_iid) {
    if (combat_hitlist_victim_pointer_may_change(batch->state.stocks[d_idx],
                                                 batch->state.action_id[d_idx])) {
      return 0u;
    }
  }
  // AttackAirB DamageFlyTop late-refresh dense fallback:
  // - The replay dense group lane cannot distinguish the same-group HitCapsules. The large outer
  //   late BAir capsule (hitbox 1 in extracted data/moves/{fox,falco}.json) carries the existing
  //   same-victim suppression in QGD-style continuation controls.
  // - Inner late capsules can be independently empty in replay-proven DCC continuation rows, so
  //   the dense group fallback must not suppress their matrix-first full BODY admission.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_800768A0}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  return (hb_id == 1) ? 1u : 0u;
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
                                                          az, bx, by, bz, out_overlap, NULL);
  return 0;
}

static inline uint8_t combat_shield_overlap_ftcoll_80007bcc(
    const MslBatch* batch, int bi, int attacker, int defender, int hb_id, float hx, float hy,
    float hz, float hr, float shx, float shy, float shz, float shr, float shield_desc_radius,
    float shield_owner_scale_y, uint8_t shield_desc_envelope_ready,
    uint8_t shield_extent_bridge_active, float* out_overlap_margin) {
  if (out_overlap_margin != NULL) {
    *out_overlap_margin = 0.0f;
  }
  if (batch == NULL) {
    return 0u;
  }
  const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
  const size_t d_idx = msl_idx_player(bi, defender);
  // Decomp geometry owner:
  // - ftCo_80091D58 scales the shield JObj from current shield health/lightshield state.
  // - lbColl_80007BCC passes ShieldDesc.size=1 plus the scaled shield JObj matrix into
  //   lbColl_80006E58. The simulator's `shield_radius` represents ftCo_80091D58's current
  //   shield-bone scale (shield HP/lightshield/fp->x34_scale.y); lbColl_80006E58's final local
  //   matrix test also carries the fighter model scale in the shield JObj matrix. Apply that
  //   data-backed model-scale term here instead of adding a fixed world-space ShieldDesc radius,
  //   or steady Guard rows over-admit shield before the BODY path can run.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091D58,ftCo_80092450}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B1B8
  const MslCharParams* defender_params = msl_char_params(batch->state.char_id[d_idx]);
  const float shield_owner_model_scale =
      (defender_params != NULL && isfinite(defender_params->model_scaling) &&
       defender_params->model_scaling > 0.0f)
          ? defender_params->model_scaling
          : 1.0f;
  const float shield_matrix_scale = shield_owner_scale_y * shield_owner_model_scale;
  float eff_shx = shx;
  float eff_shy = shy;
  float eff_shz = shz;
  const uint8_t guardreflect_x14_expired_this_callback =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       batch->state.animation_index[d_idx] == UINT32_MAX &&
       batch->state.guard_reflect_timer_x14_seed[d_idx] == 1u &&
       batch->state.guard_reflect_timer_x14[d_idx] == 0u &&
       batch->state.guard_reflect_origin_guardon[d_idx] != 0u)
          ? 1u
          : 0u;
  if (guardreflect_x14_expired_this_callback && !batch->state.hitbox_prev_enabled[hb_i]) {
    MslShieldTiltTableView tv;
    if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) == 0 &&
        tv.guard_on_xyz != NULL && tv.guard_on_frame_count > 0u) {
      // GuardReflect final active-x14 callback:
      // - `ftCo_GuardReflect_Anim -> ftCo_80093BC0` expires x14 and recreates ShieldDesc through
      //   `ftCo_80092450` before fighter collision. Keep this to GuardOn-origin episodes
      //   (`ftCo_8009388C`); direct locomotion powershields (`ftCo_80093A50`) stay on the
      //   ReflectDesc-only lane at the same visible timer boundary.
      // - For current-only/new HitCapsules, lbColl_80007BCC consumes that just-recreated
      //   ShieldDesc bone. Persistent HitCapsules keep the ordinary x58->x4C sweep owner below;
      //   broadening them to this recreated pose over-admits adjacent final-x14 shield misses.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0,ftCo_80092450}
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078C70}
      // data/shields/{fox,falco}.bin::guard_on_xyz[0]
      const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
      const float pose_scale = shield_owner_scale_y * shield_owner_model_scale;
      const float dx = tv.guard_on_xyz[0];
      const float dy = tv.guard_on_xyz[1];
      const float dz = tv.guard_on_xyz[2];
      eff_shx = batch->state.pos_x[d_idx] + facing_dir * dz * pose_scale;
      eff_shy = batch->state.pos_y[d_idx] + dy * pose_scale;
      eff_shz = batch->state.pos_z[d_idx] - facing_dir * dx * pose_scale;
    }
  }
  if (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD) {
    MslShieldTiltTableView tv;
    if (msl_shield_tilt_table_view(batch->state.char_id[d_idx], &tv) == 0 && tv.xyz != NULL &&
        tv.frame_count > 0u) {
      const uint16_t frame_max = (uint16_t)(tv.frame_count - 1u);
      const uint16_t f = (batch->state.guard_tilt_x8[d_idx] > frame_max)
                             ? frame_max
                             : batch->state.guard_tilt_x8[d_idx];
      float mag = batch->state.guard_tilt_x4[d_idx];
      if (mag < 0.0f) {
        mag = 0.0f;
      } else if (mag > 1.0f) {
        mag = 1.0f;
      }

      // `ftCo_80091E78` only samples the angled Guard timeline when x4 is nonzero, then blends
      // it against the current no-tilt pose through `ftAnim_80070108(..., 1 - x4, x4, x20)`.
      // For fighter-vs-fighter ShieldDesc collision (`ftColl_8007B1B8 -> lbColl_80007BCC`) use
      // that live frame-0 base locally. The shared shield center in shields_refresh() remains the
      // replay-visible/item-shield owner; broadening it regresses projectile shield accepts.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80091BC4,ftCo_80091E78}
      // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
      const size_t n_i = 0u;
      const size_t f_i = (size_t)f * 3u;
      const float nx = tv.xyz[n_i + 0];
      const float ny = tv.xyz[n_i + 1];
      const float nz = tv.xyz[n_i + 2];
      const float fx = tv.xyz[f_i + 0];
      const float fy = tv.xyz[f_i + 1];
      const float fz = tv.xyz[f_i + 2];
      const float dx = nx + mag * (fx - nx);
      const float dy = ny + mag * (fy - ny);
      const float dz = nz + mag * (fz - nz);
      const float facing_dir = batch->state.facing[d_idx] ? 1.0f : -1.0f;
      const float pose_scale = shield_owner_scale_y * shield_owner_model_scale;
      eff_shx = batch->state.pos_x[d_idx] + facing_dir * dz * pose_scale;
      eff_shy = batch->state.pos_y[d_idx] + dy * pose_scale;
      eff_shz = batch->state.pos_z[d_idx] - facing_dir * dx * pose_scale;
    }
  }

  float shield_desc_world_r = shield_desc_radius;
  if (shield_owner_scale_y > 0.0f) {
    shield_desc_world_r *= shield_matrix_scale;
  }
  // lbColl_80007BCC forwards an extra extent lane (`lbColl_804D7A34 * arg5`) into
  // lbColl_80006E58 (`arg11`) for shield overlap broadphase.
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  //
  // This simulator uses a reduced sphere/segment proxy; carry a small equivalent envelope from
  // that extent lane to avoid near-boundary false negatives in the guard shield path.
  // Apply ShieldDesc radius lane when the slot was not recreated at pose_frame, or when the slot
  // is on an enable-edge transition. This matches the ftColl_8007AD18 state ownership split:
  // - state 1/2 edge transitions are fed by ftAction create/copy/clear ownership (ftColl_800768A0),
  // - steady slots with no pose-frame create keep prior collision state.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800768A0,ftColl_8007AD18}
  const uint8_t shield_desc_lane_active =
      (shield_desc_envelope_ready &&
       (batch->state.hitbox_enable_edge[hb_i] || !batch->state.hitbox_pose_create[hb_i]))
          ? 1u
          : 0u;
  const uint8_t guardon_entry_no_submotion =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_ON &&
       batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX)
          ? 1u
          : 0u;
  const uint8_t guardreflect_final_x14_no_submotion =
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
       batch->state.action_frame[d_idx] < 0 && batch->state.animation_index[d_idx] == UINT32_MAX &&
       batch->state.guard_reflect_timer_x14_seed[d_idx] == 0u)
          ? 1u
          : 0u;
  // No-submotion GuardOn entry is the live `ftCo_800924C0 -> ftCo_800921DC ->
  // ftCo_80091E78(..., 0)` snapshot where the ShieldDesc was just recreated and Slippi does not
  // expose a settled Guard submotion frame. Keep the narrow ShieldDesc envelope there; steady Guard
  // rows consume the current shield-bone scale already represented by `shield_radius`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_800924C0,ftCo_800921DC,ftCo_80091E78}
  const float shield_desc_term =
      (shield_desc_lane_active && guardon_entry_no_submotion) ? shield_desc_world_r : 0.0f;
  const uint8_t shield_extent_lane_active =
      (shield_desc_envelope_ready && !guardreflect_final_x14_no_submotion &&
       (batch->state.hitbox_enable_edge[hb_i] || shield_extent_bridge_active))
          ? 1u
          : 0u;
  // Enable-edge capsules are exactly the create/copy/clear lane that lbColl_80007BCC receives
  // after ftAction_8007121C/ftColl_8007AD18 ownership. Use the full ShieldDesc extent term there;
  // the reduced 0.2 bridge remains only for steady capsules where this sim's shield proxy otherwise
  // over-accepts broadphase-only grazes.
  // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007AD18,ftColl_80078C70}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  const size_t a_idx = msl_idx_player(bi, attacker);
  const uint16_t a_action = batch->state.action_id[a_idx];
  const uint8_t shine_start_enable_edge = (batch->state.hitbox_enable_edge[hb_i] &&
                                           (a_action == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START ||
                                            a_action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START))
                                              ? 1u
                                              : 0u;
  const float shield_extent_scale =
      (shield_extent_bridge_active || shine_start_enable_edge) ? 1.0f : 0.2f;
  const float shield_extent_env_r =
      shield_extent_lane_active ? (shield_desc_world_r * shield_extent_scale) : 0.0f;
  // Final-x14 GuardReflect no-submotion is the `ftCo_80093BC0` handoff after ReflectDesc expiry.
  // The replay-visible shield radius already represents the live shield JObj scale for this frozen
  // transition; applying the generic model-scale term again over-admits near-rim BODY rows.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092450}
  const float shield_matrix_radius =
      shr * (guardreflect_final_x14_no_submotion ? 1.0f : shield_owner_model_scale);
  const float rr = hr + shield_matrix_radius + shield_desc_term + shield_extent_env_r;
  float d2 = 0.0f;

  // Decomp-owned geometry path:
  // - ftColl_8007AD18 carries previous/current hitcapsule centers in x58/x4C.
  // - lbColl_80007BCC consumes that x58->x4C sweep segment for shield overlap tests.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007AD18
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007BCC
  if (batch->state.hitbox_prev_enabled[hb_i]) {
    const float px = batch->state.hitbox_prev_x[hb_i];
    const float py = batch->state.hitbox_prev_y[hb_i];
    const float pz = batch->state.hitbox_prev_z[hb_i];
    combat_point_segment_dist2(eff_shx, eff_shy, eff_shz, px, py, pz, hx, hy, hz, &d2, NULL);
  } else {
    const float dx = hx - eff_shx;
    const float dy = hy - eff_shy;
    const float dz = hz - eff_shz;
    d2 = dx * dx + dy * dy + dz * dz;
  }

  if (out_overlap_margin != NULL) {
    *out_overlap_margin = rr - sqrtf(d2);
  }
  return (uint8_t)(d2 <= rr * rr);
}

static inline float combat_clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static inline float combat_trigger_u8_to_unit(uint8_t v) { return (float)v * (1.0f / 255.0f); }

static inline float combat_trigger_unit_from_input(uint16_t buttons, uint8_t l, uint8_t r) {
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:1868-1890 and :2019-2050.
  // - If digital L/R is held, Melee treats shield trigger as fully pressed (`x650 = 1.0f`).
  // - Otherwise use the analog max of L/R.
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((buttons & LR) != 0) {
    return 1.0f;
  }
  const uint8_t m = l > r ? l : r;
  return combat_trigger_u8_to_unit(m);
}

static inline float combat_lightshield_amount(const MslCommonParams* c, uint16_t buttons, uint8_t l,
                                              uint8_t r) {
  if (c == NULL) {
    return 0.0f;
  }

  // Decomp: fp->lightshield_amount = (x650 - x10)/(1-x10) (clamped) under trigger deadzone.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c:333-350.
  const float denom = 1.0f - c->trigger_deadzone;
  if (denom <= 0.0f) {
    return 0.0f;
  }
  const float trig = combat_trigger_unit_from_input(buttons, l, r);
  const float light = (trig - c->trigger_deadzone) / denom;
  return combat_clamp01(light);
}

static inline void combat_state_flags_set_is_hitlag(MslBatch* batch, size_t idx, uint16_t hitlag) {
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
  // Slippi post-frame: `lbz r3,0x221A(REG_PlayerData)  #0x20 = isHitlag`.
  //
  // Decomp-first references (GALE01):
  // - `fp->x221A_b2` is toggled with hitlag start/end:
  //   - set when hitlag is applied (Fighter_ProcessHit_8006D1EC),
  //   - cleared when hitlag reaches 0 (Fighter_8006A1BC).
  // refs/melee/src/melee/ft/fighter.c
  // - Bitfield layout at fp+0x221A is documented in refs/melee/src/melee/ft/types.h.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  enum { MSL_STATE_FLAG_221A_IS_HITLAG = 0x20 };

  const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221A_INDEX;
  uint8_t f = batch->state.state_flags[flags_i];
  if (hitlag > 0) {
    f |= (uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
  } else {
    f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
  }
  batch->state.state_flags[flags_i] = f;
}

static inline void combat_state_flags_set_x221a_b3(MslBatch* batch, size_t idx) {
  // Decomp: Fighter_ProcessHit sets fp->x221A_b3 = 1 alongside hitlag start under certain
  // knockback/damage paths (see `bool2`), and Fighter_8006A1BC clears it on hitlag end.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  //
  // In GALE01, `bool2` is set to 1 when the hit takes the "forceAppliedOnHit && !no_kb" path
  // (Fighter_ProcessHit_8006D1EC sets `bool2 = 1` shortly before the `if (bool2) fp->x221A_b3 = 1`
  // assignment). In this light sim we do not model the full `forceAppliedOnHit` / `no_kb` plumbing,
  // so callers gate this bit on `hitstun > 0` (derived from knockback), which is a safe proxy for
  // the current suite domain (Fox/Falco) and matches Slippi's observable "KB hit that causes hitstun"
  // cases where this bit is set.
  //
  // Slippi post-frame: this bit lives in the fp+0x221A byte (`state_flags[...,1]`). The isHitlag
  // bit is 0x20 (x221A_b2), so x221A_b3 is the adjacent 0x10 bit under the same packing.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221A_INDEX = 1 };
  enum { MSL_STATE_FLAG_221A_B3 = 0x10 };

  const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221A_INDEX;
  batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_221A_B3;
}

static inline void combat_state_flags_set_is_hitstun(MslBatch* batch, size_t idx,
                                                     uint16_t hitstun) {
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  // Slippi post-frame: `lbz r3,0x221C(REG_PlayerData)  #0x2 = isHitstun`.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //
  // Decomp: `fp->x221C_b6` is set on Damage state entry and cleared when hitstun ends.
  // - set: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (end of function)
  // - clear: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
  enum { MSL_STATE_FLAG_221C_IS_HITSTUN = 0x02 };

  const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  uint8_t f = batch->state.state_flags[flags_i];
  if (hitstun > 0) {
    f |= (uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
  } else {
    f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_IS_HITSTUN;
  }
  batch->state.state_flags[flags_i] = f;
}

static inline void combat_state_flags_set_x221c_b0(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_B0 = 0x80 };

  // fp+0x221C bit 0x80 ownership in attached hit windows:
  // - This bit is exposed by Slippi's post-frame byte capture at fp+0x221C.
  //   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // - Damage flow consumes fp->x221C_b0 as part of the no-reaction branch gate.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
  // - Generic motion-state change clears fp+0x221C lanes owned by transition reset paths.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_221C_B0;
}

static inline void combat_state_flags_clear_x221c_b0(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_B0 = 0x80 };

  // Motion-state reset ownership for fp+0x221C_b0:
  // - Fighter_ChangeMotionState clears fp->x221C_b0 on destination entry.
  // - Damage state entry uses Fighter_ChangeMotionState via ftCo_8008DCE0 / ftCo_8008EC90.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_8008EC90}
  const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B0;
}

static inline void combat_state_flags_clear_guard_reflecting(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_2218_INDEX = 0 };
  enum { MSL_STATE_FLAG_2218_REFLECTING = 0x10 };

  // GuardSetOff destination reset:
  // - shield-hit transition enters GuardSetOff through Fighter_ChangeMotionState in ftCo_80092F2C,
  // - that destination does not keep the live `fp->reflecting` owner from the GuardReflect
  //   descriptor, even when x221C_b1/x221C_b2 timer lanes remain visible on the same row.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
}

static inline void combat_state_flags_set_guard_reflect_timer_bits(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_GUARD_REFLECT_X14 = 0x40 };
  enum { MSL_STATE_FLAG_221C_GUARD_REFLECT_X18 = 0x20 };

  uint8_t bits = 0u;
  if (batch->state.guard_reflect_timer_x14[idx] != 0u) {
    bits |= (uint8_t)MSL_STATE_FLAG_221C_GUARD_REFLECT_X14;
  }
  if (batch->state.guard_reflect_timer_x18[idx] != 0u) {
    bits |= (uint8_t)MSL_STATE_FLAG_221C_GUARD_REFLECT_X18;
  }
  if (bits == 0u) {
    return;
  }

  // ProcessHit ordering after an item BODY hit from GuardReflect:
  // - GuardReflect entry/timer bits (x221C_b1/x221C_b2) remain visible on the post-frame even
  //   after the damage-state entry consumes the live reflect descriptor.
  // - The live reflecting bit itself is cleared separately through fp+0x2218.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] |= bits;
}

static inline void combat_apply_guard_reflect_body_hit_followup(const MslCommonParams* c,
                                                                MslBatch* batch, size_t idx,
                                                                uint16_t pre_motion_id) {
  if (c == NULL || batch == NULL || pre_motion_id != (uint16_t)MSL_ACT_GUARD_REFLECT) {
    return;
  }

  combat_state_flags_clear_guard_reflecting(batch, idx);
  combat_state_flags_set_guard_reflect_timer_bits(batch, idx);

  // Fighter_ProcessHit runs after the GuardReflect/GuardOn anim callback. If that callback drained
  // shield health and the BODY hit then leaves shield ownership, the standard !x221A_b7 recharge
  // gate can add one recharge tick on the same post-frame.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_GuardOn_Anim,ftCo_800925A4}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  if (batch->state.stocks[idx] == 0u) {
    return;
  }
  float hp = batch->state.shield_hp[idx];
  if (hp < c->start_shield_health) {
    hp += c->shield_recharge_per_frame;
    if (hp > c->start_shield_health) {
      hp = c->start_shield_health;
    }
    batch->state.shield_hp[idx] = hp;
  }
}

static inline void combat_apply_ftCommon_8007D5D4_ground_to_air(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Decomp common helper ownership:
  // - ftCommon_8007D5D4 sets ground_or_air=Air, gr_vel=0, jumpsUsed=1, ecb_lock=10.
  // - Damage entry / throw-release lanes call this helper when launching victim airborne.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  batch->state.on_ground[idx] = 0u;
  batch->state.ecb_lock_timer[idx] = MSL_ECB_LOCK_FRAMES_COMMON_GROUND_TO_AIR;
  // Narrow ownership parity for this lane: keep existing velocity ownership in its current
  // systems and source jumpsUsed parity here (jumps_left=max_jumps-1).
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_8007D5D4
  const MslCharParams* ch = msl_char_params(batch->state.char_id[idx]);
  if (ch != NULL) {
    batch->state.jumps_left[idx] = (ch->max_jumps > 0u) ? (uint8_t)(ch->max_jumps - 1u) : 0u;
  }
}

static inline uint8_t combat_is_guard_reflect_frozen_snapshot_idx(const MslBatch* batch,
                                                                  size_t idx) {
  if (batch == NULL) {
    return 0;
  }
  if (batch->state.action_id[idx] != (uint16_t)MSL_ACT_GUARD_REFLECT) {
    return 0;
  }
  // Decomp ordering context:
  // - Guard->GuardReflect entry path ftCo_8009388C preserves the current timebase state while
  //   GuardReflect_Anim/ftCo_80093BC0 owns the canonical callback tick.
  // - Under teacher-forced replay snapshots, this boundary appears as frozen no-submotion rows.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
  //
  // Keep frozen-snapshot detection behavior identical to the existing lane:
  // action_frame <= -2.
  enum { MSL_GUARD_REFLECT_FROZEN_ACTION_FRAME_MAX = -2 };
  return (batch->state.action_frame[idx] <= MSL_GUARD_REFLECT_FROZEN_ACTION_FRAME_MAX) ? 1u : 0u;
}

static inline uint8_t combat_defer_late_slot_same_frame_speciallw_entry_hit(
    const MslBatch* batch, size_t a_idx, size_t d_idx, int attacker, int defender) {
  if (batch == NULL || attacker <= defender) {
    return 0u;
  }
  const uint16_t action = batch->state.action_id[a_idx];
  if (action != (uint16_t)MSL_ACT_FX_SPECIAL_LW_START) {
    return 0u;
  }
  if (batch->state.prev_action_id[a_idx] == action) {
    return 0u;
  }
  if (batch->state.action_id[d_idx] != (uint16_t)MSL_ACT_TURN || !batch->state.on_ground[d_idx] ||
      batch->state.hitlag[d_idx] != 0u || batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  if (batch->state.turn_has_turned[d_idx] == 0u) {
    return 0u;
  }

  // Fighter BODY pair-order + Turn internal-facing microphase owner:
  // - ftColl_80078C70 walks the fighter entity list as an unordered pair pass. For a later entity
  //   attacking an earlier entity, its freshly-created same-frame HitCapsule can miss the earlier
  //   fighter's already-processed collision-pair phase.
  // - This boundary is only replay-proven for the Turn internal-facing microphase (`has_turned=1`):
  //   ftCo_Turn_Anim_Inner has flipped fp->facing_dir while the replay-visible facing byte can
  //   still be stale, and the simulator's SSANIM/x58 bootstrap can otherwise create an extra
  //   grounded Shine frame-0 BODY hit from the internal-facing hurtcap pose. Pre-turn Turn
  //   (`has_turned=0`) remains on the normal BODY path; HVG:5200/TCH:3376 prove those same later
  //   slot grounded Shine entries still hit.
  // - Aerial SpecialLwStart remains on the normal BODY path.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Turn.c::ftCo_Turn_Anim_Inner
  // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialLw_Enter
  // data/moves/{fox,falco}.json specials_by_msid["313"|"317"].events
  return 1u;
}

static inline uint8_t combat_prev_action_is_guard_reflect_locomotion_source(uint16_t action_id) {
  switch (action_id) {
    case (uint16_t)MSL_ACT_WAIT:
    case (uint16_t)MSL_ACT_WALK_SLOW:
    case (uint16_t)MSL_ACT_WALK_MIDDLE:
    case (uint16_t)MSL_ACT_WALK_FAST:
    case (uint16_t)MSL_ACT_TURN:
    case (uint16_t)MSL_ACT_DASH:
    case (uint16_t)MSL_ACT_RUN:
    case (uint16_t)MSL_ACT_RUN_DIRECT:
    case (uint16_t)MSL_ACT_SQUAT:
    case (uint16_t)MSL_ACT_SQUAT_WAIT:
    case (uint16_t)MSL_ACT_SQUAT_RV:
      return 1u;
    default:
      return 0u;
  }
}

static inline uint8_t combat_is_guard_reflect_fresh_locomotion_snapshot_idx(const MslBatch* batch,
                                                                            size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint16_t prev_action = batch->state.seed_prev_action_id[idx];
  return (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.action_frame[idx] < 0 && batch->state.animation_index[idx] == UINT32_MAX &&
          batch->state.guard_reflect_timer_x14_seed[idx] == 0u &&
          batch->state.guard_reflect_timer_x18_seed[idx] == 0u &&
          combat_prev_action_is_guard_reflect_locomotion_source(prev_action) &&
          prev_action != (uint16_t)MSL_ACT_GUARD_ON && prev_action != (uint16_t)MSL_ACT_GUARD &&
          prev_action != (uint16_t)MSL_ACT_GUARD_REFLECT &&
          prev_action != (uint16_t)MSL_ACT_GUARD_SET_OFF)
             ? 1u
             : 0u;
}

static inline uint8_t combat_guard_reflect_no_submotion_reflectdesc_only_lane(const MslBatch* batch,
                                                                              size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  // `ftCo_8009388C` enters GuardReflect from an already-shielding guard state by clearing
  // `x221B_b0` and creating only ReflectDesc; ShieldDesc is recreated later by
  // `ftCo_80093BC0` after x14 expires. The exact expiry callback boundary
  // (`x14_seed==1 -> x14==0`) has already run ftCo_80093BC0 by the collision pass, so it must
  // leave this ReflectDesc-only lane. The locomotion-entry path
  // `ftCo_80091A4C -> ftCo_800939B4 -> ftCo_80093A50` is different: it calls
  // `ftCo_80092450` before creating ReflectDesc, so active-x14 no-submotion rows from locomotion
  // still expose ShieldDesc to fighter-vs-fighter collision. Use the frame-start seed latch as the
  // phase predicate only for non-locomotion snapshots.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
  const uint8_t expired_this_callback = (batch->state.guard_reflect_timer_x14_seed[idx] == 1u &&
                                         batch->state.guard_reflect_timer_x14[idx] == 0u &&
                                         batch->state.guard_reflect_origin_guardon[idx] != 0u)
                                            ? 1u
                                            : 0u;
  return (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.action_frame[idx] < 0 && batch->state.animation_index[idx] == UINT32_MAX &&
          (batch->state.guard_reflect_timer_x14_seed[idx] != 0u ||
           batch->state.guard_reflect_timer_x14[idx] != 0u) &&
          !expired_this_callback &&
          !combat_is_guard_reflect_fresh_locomotion_snapshot_idx(batch, idx) &&
          batch->state.hitlag[idx] == 0u && batch->state.hitstun[idx] == 0u)
             ? 1u
             : 0u;
}

static inline uint8_t combat_shield_damage_powershield_suppressed_idx(const MslBatch* batch,
                                                                      size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  uint8_t powershield_active = combat_is_powershield_active_idx(batch, idx);
  if (!powershield_active) {
    return 0u;
  }
  // GuardReflect frozen snapshot ownership split:
  // - ftColl_80076CBC shield-damage accumulation reads fp->x221C_b2, but callback ownership for
  //   active reflect window expiration is in GuardReflect_Anim (ftCo_80093BC0, x14 lane).
  // - In no-submotion frozen snapshots, x18 can remain non-zero while x14 is already expired.
  // - For shield-damage accumulation only (GuardSetOff/hitlag ownership), suppress powershield
  //   gating in this narrow lane; item reflect ownership continues to use
  //   combat_is_powershield_active_idx().
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
      combat_is_guard_reflect_frozen_snapshot_idx(batch, idx) &&
      batch->state.animation_index[idx] == UINT32_MAX &&
      batch->state.guard_reflect_timer_x14[idx] == 0u &&
      batch->state.guard_reflect_timer_x18[idx] != 0u &&
      // When x14 expires during this callback (`seed==1 -> current==0`), ftCo_80093BC0 has
      // just recreated ShieldDesc before the collision pass while x18/x221C_b2 remains live.
      // That boundary should still suppress shieldDamageTaken; rows already seeded with x14
      // expired use this branch as the older frozen-snapshot shield-damage handoff.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_GuardReflect_Anim,ftCo_80093BC0}
      batch->state.guard_reflect_timer_x14_seed[idx] == 0u) {
    powershield_active = 0u;
  }
  return powershield_active;
}

uint8_t combat_is_powershield_active_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0;
  }
  // Decomp gate at collision-time is on fp->x221C_b2 directly.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE = 0x20 };
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT) {
    // GuardReflect lane split:
    // - frozen no-submotion snapshot lane keeps x18 (legacy powershield-active lane) so replay-real
    //   frozen rows do not collapse before the first canonical callback-owned transition;
    // - normal GuardReflect lane uses x14 (active reflect callback lane).
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_8009388C,ftCo_GuardReflect_Anim,ftCo_80093BC0}
    if (combat_is_guard_reflect_frozen_snapshot_idx(batch, idx)) {
      return (batch->state.guard_reflect_timer_x18[idx] != 0u) ? 1u : 0u;
    }
    // For non-frozen GuardReflect frames, gate suppression on the active reflect callback lane.
    return (batch->state.guard_reflect_timer_x14[idx] != 0u) ? 1u : 0u;
  }
  return (flags_221c & (uint8_t)MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE) ? 1u : 0u;
}

static inline uint8_t combat_guard_setoff_recoil_x221c_b2_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE = 0x20 };
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  if ((flags_221c & (uint8_t)MSL_STATE_FLAG_221C_POWERSHIELD_ACTIVE) != 0u) {
    return 1u;
  }
  if (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT) {
    // GuardSetOff recoil consumes fp->x221C_b2 directly inside ftCo_80092F2C. That lane is owned by
    // GuardReflect's x18 timer, not the shorter ReflectDesc/x14 ownership used by item reflect.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093BC0,ftCo_80092F2C}
    // refs/melee/build/GALE01/asm/melee/ft/chara/ftCommon/ftCo_Guard.s:0x80093080..0x800930A0
    return (batch->state.guard_reflect_timer_x18[idx] != 0u) ? 1u : 0u;
  }
  return 0u;
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

static inline float combat_hitlag_mul_from_element(const MslCommonParams* c, uint8_t element) {
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

static inline uint16_t combat_calc_hitlag_frames(const MslCommonParams* c, int dmg,
                                                 uint16_t motion_id, float hitlag_mul) {
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
  if (batch->state.hitlag[idx] == 0u || batch->state.hitstun[idx] == 0u) {
    return 0u;
  }
  if (!combat_is_damage_or_firefox_launch_victim_action(batch->state.action_id[idx])) {
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

static inline int combat_get_env_dmg(float dmg) {
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

static inline uint8_t combat_guardreflect_expired_x14_earlier_body_hitcapsule_precedes_shield(
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
  if (!combat_guardreflect_expired_x14_body_fallback_applies(batch, d_idx)) {
    return 0u;
  }

  const MslHurtCap* fallback_caps = NULL;
  uint16_t fallback_count_u16 = 0u;
  if (hurtcaps_get(batch->state.char_id[d_idx], &fallback_caps, &fallback_count_u16) != 0 ||
      fallback_caps == NULL || fallback_count_u16 == 0u) {
    return 0u;
  }
  const uint8_t fallback_count = fallback_count_u16 > (uint16_t)MSL_MAX_HURTCAPS
                                     ? (uint8_t)MSL_MAX_HURTCAPS
                                     : (uint8_t)fallback_count_u16;
  const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;
  const uint16_t a_motion_id = (batch->state.animation_index[a_idx] <= 0xFFFFu)
                                   ? (uint16_t)batch->state.animation_index[a_idx]
                                   : batch->state.action_id[a_idx];

  for (int prev_hb_id = 0; prev_hb_id < shield_hb_id; prev_hb_id++) {
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
    if (!combat_shine_start_damageair_entry_pose_allows_body_contact(batch, a_idx, d_idx, 0u, hx,
                                                                     hy, hz, hr)) {
      continue;
    }
    if (!combat_attackairb_enable_edge_model_scale_allows_body_contact(
            batch, a_idx, hb_i, d_idx, hx, hy, hz, hr, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
            combat_calc_hitlag_frames(c, int_dmg, a_motion_id, 1.0f))) {
      continue;
    }

    for (uint8_t cap_id = 0; cap_id < fallback_count; cap_id++) {
      float ax = 0.0f, ay = 0.0f, az = 0.0f;
      float bx = 0.0f, by = 0.0f, bz = 0.0f;
      float cr = 0.0f;
      if (!combat_guardreflect_body_hurtcap_world(batch, d_idx, &fallback_caps[cap_id], cap_id,
                                                  fallback_count_u16, &ax, &ay, &az, &bx, &by, &bz,
                                                  &cr)) {
        continue;
      }

      float lbcoll_overlap_amount = 0.0f;
      uint8_t lbcoll_overlap_evaluated = 0u;
      const uint8_t lbcoll_overlap_valid = combat_body_overlap_lbColl_80006E58_matrix_radius(
          batch, bi, attacker, prev_hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az, bx,
          by, bz, &lbcoll_overlap_amount, &lbcoll_overlap_evaluated);
      const uint8_t overlaps =
          lbcoll_overlap_evaluated
              ? lbcoll_overlap_valid
              : combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL);
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
  // - Replay seeds can already include a same-attack stale entry from an earlier contact in this
  //   attack instance. That entry must not retroactively stale the live HitCapsule; older instances
  //   of the same move still stale normally.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_8007699C,inlineA0,inlineA1}
  // refs/melee/src/melee/ft/ft_0DF0.c::ftCo_800DEEB8
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t move_id = staling_move_id_from_state(batch, a_idx);
  const uint16_t attack_instance = batch->state.attack_instance[a_idx];
  const float stale_mult =
      staling_multiplier_for_move_excluding_instance(batch, a_idx, move_id, attack_instance);
  float dmg = combat_apply_attacker_smash_release_damage_mul(batch, a_idx,
                                                             batch->state.hitbox_damage[hb_i]);
  if (stale_mult != 1.0f) {
    dmg *= stale_mult;
  }
  return dmg;
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
  const MslCharParams* ch = (batch != NULL) ? msl_char_params(batch->state.char_id[idx]) : NULL;
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
  // docs/DECOMP_PROC_ORDER.md (prio 1 vs prio 3)
  // refs/melee/src/melee/ft/ftanim.c::ftAnim_8006EBA4
  // refs/melee/src/melee/ft/ftaction.c::ftAction_80073240
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007B868 (eligibility aggregates x1988/x198C)
  uint8_t hit_status = 0;
  const uint16_t cur_action = batch->state.action_id[d_idx];
  const uint8_t is_shine_start_entry = (cur_action == (uint16_t)MSL_ACT_FX_SPECIAL_LW_START ||
                                        cur_action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START)
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
    (void)hit_status_get(d_char, d_msid, d_frame, &hit_status);
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

static inline float combat_pi_over_two_f32(void) { return 1.57079632679489661923f; }

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

static inline void combat_damage_calc_vel(MslBatch* batch, size_t d_idx, float x, float y) {
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
  if (batch == NULL) {
    return;
  }
  if (batch->state.on_ground[d_idx] == 0u) {
    return;
  }
  // Decomp: grounded `ftCommon_800804FC` clears source owner and disables the x18C8 countdown.
  // Fighter_ProcessHit calls this in the percent-only/no-KB path after Fighter_UnkTakeDamage.
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  enum { MSL_LAST_HIT_BY_SOURCE_NONE = 6 };
  batch->state.last_hit_by[d_idx] = (uint8_t)MSL_LAST_HIT_BY_SOURCE_NONE;
  batch->state.source_clear_timer_x18c8[d_idx] = 0u;
}

static inline uint8_t combat_source_port0_for_attacker(const MslBatch* batch, size_t a_idx,
                                                       int attacker) {
  // Slippi records dmg.x18C4_source_ply in raw controller-port domain; local attacker indices are
  // compact dataset slots.
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

static inline void combat_processhit_commit_source_owner(MslBatch* batch, size_t d_idx,
                                                         uint8_t source_port) {
  if (batch == NULL) {
    return;
  }
  // Decomp owner shape:
  // - collision writes source owner before Fighter_ProcessHit,
  // - grounded percent-only/no-KB paths then clear it via ftCommon_800804FC,
  // - airborne rows keep the source owner live.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/ftcommon.c::ftCommon_800804FC
  if (batch->state.on_ground[d_idx] != 0u) {
    combat_source_owner_clear_ftCommon_800804FC(batch, d_idx);
    return;
  }
  batch->state.last_hit_by[d_idx] = source_port;
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

static inline uint8_t combat_damage_severity_u8_from_kb(const MslCommonParams* c,
                                                        float kb_applied) {
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

static int combat_local_slot_from_source_port0(const MslBatch* batch, int bi, int num_players,
                                               uint8_t source_port0) {
  if (batch == NULL) {
    return -1;
  }
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(bi, p);
    if (batch->state.source_port0[idx] == source_port0) {
      return p;
    }
  }
  return -1;
}

static inline uint8_t combat_damageflyroll_rng_subset_allows_pre_action(const MslBatch* batch,
                                                                        size_t d_idx,
                                                                        uint16_t action_id) {
  if (batch == NULL) {
    return 0u;
  }
  // Narrowed pre-gate ownership bridge for ftCo_8008DCE0 block_33:
  // - DamageFlyRoll RNG gate is evaluated while entering damage from Fighter_ProcessHit.
  // - Keep the gate scoped to decomp-confirmed carry contexts until additional pre-gate
  //   random-consumer ownership lanes are modeled.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{
  //   ftCo_Damage_Anim,ftCo_DamageFall_IASA,ftCo_8008DCE0
  // }
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //
  // narrowed_temporary:
  // - Includes Fall/Run/AttackAirLw carry windows with replay-exact RNG pulse parity in the
  //   suite's severe-airborne damage transition families.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Fall.c::ftCo_Fall_Anim
  // - SpecialHiFall is admitted only when the current AttackAirB HitCapsule is on its
  //   ftAction_8007121C enable edge; steady active contacts remain excluded.
  // - Keep the remaining SpecialHi/Landing pre-actions excluded until their upstream RNG
  //   consumers are represented in this runtime.
  // refs/melee/src/melee/ft/chara/ftCommon/forward.h::ftCommon_MotionState
  switch (action_id) {
    case (uint16_t)MSL_ACT_DAMAGE_FALL:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_N:
    case (uint16_t)MSL_ACT_DAMAGE_FLY_LW:
    case (uint16_t)MSL_ACT_FALL:
    case (uint16_t)MSL_ACT_RUN:
    case (uint16_t)MSL_ACT_JUMP_AERIAL_F:
    case (uint16_t)MSL_ACT_JUMP_AERIAL_B:
    case (uint16_t)MSL_ACT_LANDING_AIR_LW:
    case (uint16_t)MSL_ACT_ATTACK_HI4:
    case (uint16_t)MSL_ACT_ATTACK_LW3:
    case (uint16_t)MSL_ACT_FX_SPECIAL_LW_END:
    case (uint16_t)MSL_ACT_ATTACK_AIR_LW:
      return 1u;
    case (uint16_t)MSL_ACT_FX_SPECIAL_HI_FALL: {
      // SpecialHiFall carry rows are admitted only on a current AttackAirB hitbox enable-edge.
      // Steady already-active AttackAirB contacts can still apply damage, but do not show this
      // DamageFlyRoll gate ownership in replay-real controls.
      // refs/melee/src/melee/ft/ftaction.c::ftAction_8007121C
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076808,ftColl_800768A0}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
      const int num_players = (int)batch->config.num_players;
      const size_t bi = d_idx / (size_t)MSL_MAX_PLAYERS;
      const int attacker = combat_local_slot_from_source_port0(batch, (int)bi, num_players,
                                                               batch->state.last_hit_by[d_idx]);
      if (attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
        return 0u;
      }
      const size_t bi_base = bi * (size_t)MSL_MAX_PLAYERS;
      const size_t a_idx = bi_base + (size_t)attacker;
      if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B) {
        return 0u;
      }
      const size_t hb_base = a_idx * (size_t)MSL_MAX_HITBOXES;
      for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
        if (batch->state.hitbox_enable_edge[hb_base + (size_t)hb] != 0u) {
          return 1u;
        }
      }
      return 0u;
    }
    case (uint16_t)MSL_ACT_DAMAGE_FLY_TOP: {
      // narrowed_temporary:
      // - ftCo_8008DCE0 evaluates the DamageFlyRoll RNG gate before entering a new damage
      //   motion state, so `action_id` here is the defender pre-action from the prior frame.
      // - In the currently modeled stream, AttackAirB create-window carry rows use an explicit
      //   Fighter_8006CDA4 pre-gate stream-phase seed lane before this gate is evaluated.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
      // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
      // data/moves/{fox,falco}.json moves["ftCo_SM_AttackAirB"].events create_hitbox frame=4
      //
      // TODO(narrowed_temporary): Expand beyond AttackAirB once upstream pre-gate RNG consumers
      // are represented for the regressing AttackAirF/ThrowHi windows.
      const int num_players = (int)batch->config.num_players;
      const size_t bi = d_idx / (size_t)MSL_MAX_PLAYERS;
      const int attacker = combat_local_slot_from_source_port0(batch, (int)bi, num_players,
                                                               batch->state.last_hit_by[d_idx]);
      if (attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
        return 0u;
      }
      const size_t a_idx = bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker;
      const uint16_t a_action = batch->state.action_id[a_idx];
      const int16_t a_af = batch->state.action_frame[a_idx];
      // Create-window threshold:
      // - AttackAirB's first create event is script frame 4. This gate is checked after the
      //   same-frame fighter tick.
      // - Early create-edge rows are admitted only when the explicit Fighter_8006CDA4 pre-gate
      //   stream-phase lane proves hidden RNG ownership; steady rows keep the existing
      //   post-create gate. The explicit zero-consume marker is required because lane value 0
      //   means ordinary rollout/no seed-lane, not "source-proven gate with zero pre-consumes".
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_AttackAir.c::ftCo_AttackAir_Anim
      // data/moves/{fox,falco}.json moves["ftCo_SM_AttackAirB"].events create_hitbox frame=4
      if (a_action == (uint16_t)MSL_ACT_ATTACK_AIR_B &&
          (a_af >= 6 || batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u)) {
        return 1u;
      }
      return 0u;
    }
    case (uint16_t)MSL_ACT_ATTACK_AIR_B: {
      // narrowed_temporary:
      // - AttackAirB pre-action is enabled from the extracted create-window onward.
      // - Early pre-create frames require the explicit Fighter_8006CDA4 stream-phase seed; visible
      //   action shape alone is not enough to admit the gate.
      // data/moves/{fox,falco}.json moves["ftCo_SM_AttackAirB"].events create_hitbox frame=4
      const int16_t pre_af = batch->state.action_frame[d_idx];
      return (pre_af >= 5 || batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u)
                 ? 1u
                 : 0u;
    }
    case (uint16_t)MSL_ACT_ATTACK_AIR_N:
      // AttackAirN pre-action can reach the same ftCo_8008DCE0 DamageFlyRoll gate, but only when
      // the replay seed proves the hidden Fighter_8006CDA4 stream phase. Visible action shape alone
      // is not enough because Slippi does not expose item_gobj/x197C branch inputs.
      // refs/melee/src/melee/ft/fighter.c::Fighter_8006CDA4
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
      return (batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx] != 0u) ? 1u : 0u;
    default:
      return 0u;
  }
}

static inline float combat_damage_ground_angle_to_floor_radians(float nx, float ny, float vx,
                                                                float vy) {
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

static inline void combat_damage_install_grounded_kb(const MslCommonParams* c, MslBatch* batch,
                                                     size_t d_idx, float kb_applied, float kb_x,
                                                     float kb_y, uint16_t hitlag_frames,
                                                     uint8_t allow_damagefly_hitlag_ecb_lock) {
  const float nx = batch->state.ground_normal_x[d_idx];
  const float ny = batch->state.ground_normal_y[d_idx];
  const float angle_to_floor = combat_damage_ground_angle_to_floor_radians(nx, ny, kb_x, kb_y);
  const uint8_t sev = combat_damage_severity_u8_from_kb(c, kb_applied);

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

static inline void combat_damageflyroll_consume_fighter_8006cda4_pre_gate_count(MslBatch* batch,
                                                                                int bi,
                                                                                size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  const uint8_t consume_count = batch->state.fighter_8006cda4_pre_gate_consume_count[d_idx];
  if (consume_count == 0u) {
    return;
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

static inline void combat_damageflyroll_consume_jumpaerial_attackairb_carry(MslBatch* batch, int bi,
                                                                            size_t d_idx) {
  if (batch == NULL) {
    return;
  }
  const uint16_t pre_action = batch->state.action_id[d_idx];
  if (pre_action != (uint16_t)MSL_ACT_JUMP_AERIAL_F &&
      pre_action != (uint16_t)MSL_ACT_JUMP_AERIAL_B) {
    return;
  }
  // Narrow carry bridge for the remaining severe-airborne AttackAirB -> JumpAerialF/B admission
  // family:
  // - ftCo_8008DCE0 block_33 evaluates the DamageFlyRoll gate on the defender pre-action while the
  //   attacker is still in AttackAirB.
  // - Keep the extra pre-gate advance scoped to the actual damage-entry row and current attacker
  //   action, avoiding any replay-keyed behavior or broad motion-polish changes.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  // refs/melee/src/sysdolphin/baselib/random.c::{HSD_Randi,HSD_Randf}
  const int num_players = (int)batch->config.num_players;
  const size_t bi_slot = d_idx / (size_t)MSL_MAX_PLAYERS;
  const int attacker = combat_local_slot_from_source_port0(batch, (int)bi_slot, num_players,
                                                           batch->state.last_hit_by[d_idx]);
  if (attacker < 0 || (size_t)attacker == (d_idx % (size_t)MSL_MAX_PLAYERS)) {
    return;
  }
  const size_t bi_base = bi_slot * (size_t)MSL_MAX_PLAYERS;
  const size_t a_idx = bi_base + (size_t)attacker;
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B) {
    return;
  }
  combat_rng_consume_step_site(batch, bi,
                               MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_JUMPAERIAL_ATTACKAIRB_CARRY);
}

static inline void combat_damage_enter_state(const MslCommonParams* c, MslBatch* batch, int bi,
                                             size_t d_idx, uint8_t defender_on_ground_before,
                                             uint8_t defender_on_ground_after, uint8_t hurt_height,
                                             float kb_applied, float kb_angle_rad) {
  if (batch == NULL) {
    return;
  }

  if (hurt_height > 2u) {
    hurt_height = 2u;
  }

  const uint8_t sev = combat_damage_severity_u8_from_kb(c, kb_applied);

  uint16_t act = (uint16_t)MSL_ACT_WAIT;
  uint32_t sm = (uint32_t)MSL_SM_WAIT1_0;

  const uint16_t pre_damage_action = batch->state.action_id[d_idx];
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
        const uint8_t damagefly_roll_rng_subset_ok =
            combat_damageflyroll_rng_subset_allows_pre_action(batch, d_idx, pre_action);
        const float percent_cur = batch->state.percent[d_idx] + batch->state.percent_temp[d_idx];
        if (damagefly_roll_rng_subset_ok &&
            percent_cur >= (float)c->damagefly_roll_percent_threshold) {
          combat_damageflyroll_consume_jumpaerial_attackairb_carry(batch, bi, d_idx);
          combat_damageflyroll_consume_fighter_8006cda4_pre_gate_count(batch, bi, d_idx);
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
  // Fighter_ChangeMotionState reset clears fp->x221B_b0 (shield descriptor active) on damage
  // entry, so do not carry seeded Guard no-submotion shield-active bits into Damage* states.
  // refs/melee/src/melee/ft/fighter.c (Fighter_ChangeMotionState reset block)
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221B_INDEX = 2 };
  enum { MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE = 0x80 };
  {
    const size_t flags_i = d_idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221B_INDEX;
    batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221B_IS_SHIELD_ACTIVE;
  }
  // Decomp: ftCo_8008DCE0 clears mv.co.damage.x14 on damage entry.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  batch->state.damage_jump_buffer_x14[d_idx] = 0;
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
}

// Combat Mutations Pass 1 (BODY-only).
//
// This is the minimal "writeback" set needed for one-step eval:
// - hitlag via decomp ftCommon_CalcHitlag
// - attribution fields compared in-suite (instance_hit_by, last_hit_by)
static inline void combat_mutations_pass1_future_apply_body_hit(
    MslBatch* batch, size_t a_idx, size_t d_idx, int attacker, int defender, size_t hb_i,
    size_t cap_i, int int_dmg, uint16_t attacker_motion_id, uint16_t attacker_attack_id) {
  if (batch == NULL) {
    return;
  }
  batch->state.phantom_damage_pending_x1898[d_idx] = 0.0f;
  batch->state.phantom_damage_timer_x189c[d_idx] = 0u;
  batch->state.phantom_damage_source_port[d_idx] = 0xFFu;

  // === GALE01 Fighter_ProcessHit/TakDamage ordering (write-site checklist) ===
  //
  // Decomp sources:
  // - refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // - refs/melee/src/melee/ft/fighter.c::Fighter_TakeDamage_8006CC7C
  //
  // Inputs (collision-produced, per-frame accumulators):
  // 1) float damage accumulator: fp->dmg.x1838_percentTemp (sum of applied float damage this frame)
  // 2) int damage (hitlag input): fp->dmg.x183C_applied (max of getEnvDmg(applied_float_damage))
  // 3) knockback magnitude: fp->dmg.kb_applied (float; 0.0 means "no KB path")
  // 4) element/flags: fp->dmg.x1860_element and other collision-written fields (angle/facing/etc.)
  //
  // Consume / apply ordering (victim side):
  // 5) If fp->dmg.kb_applied != 0.0:
  //    a) Fighter_UnkTakeDamage(fp, fp->dmg.x1838_percentTemp)  // percent add (gated inside TakeDamage)
  //    b) ftCo_Damage_CalcKnockback(fp)                         // scales/clamps kb_applied, subtracts armor
  //    c) enter Damage state / write KB velocity / hitstun
  // 6) Else if fp->dmg.kb_applied == 0.0 and fp->dmg.x1838_percentTemp != 0.0:
  //    a) Fighter_UnkTakeDamage(fp, fp->dmg.x1838_percentTemp)  // percent add only (no KB path)
  //
  // Damage gate inside Fighter_TakeDamage_8006CC7C:
  // 7) If (!fp->x2226_b4 || fp->x2226_b3): percent += damage_amount; clamp to 999.
  //
  // Hitlag (both sides, driven by per-side "max int dmg this frame"):
  // 8) If the resolved `bool1` is nonzero, set fp->dmg.x195c_hitlag_frames = ftCommon_CalcHitlag(...)
  //    and start hitlag (x221A_b2).
  //
  // End-of-frame cleanup:
  // 9) Reset fp->dmg.x1838_percentTemp to 0 and clear per-hit accumulators/flags.

  // - Set hitlag for both attacker and defender using decomp ftCommon_CalcHitlag.
  // - Update seeded/compared attribution fields that are decomp-backed:
  //   - instance_hit_by: fighter.dmg.x18ec_instancehitby (Slippi post offset 0x51)
  //     refs/melee/src/melee/ft/types.h
  //     refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //     refs/slippi-wiki/SPEC.md ("Instance Hit By")
  //   - last_hit_by: fighter.dmg.x18c4_source_ply (Slippi post offset 0x20)
  //     refs/melee/src/melee/ft/types.h
  //     refs/slippi-wiki/SPEC.md ("Last Hit By")
  //
  // NOTE (writeback reshaping pass):
  // This BODY path aims to be GALE01-shaped for the "damage/KB writeback" fields that the one-step
  // suite compares (percent, hitlag, hitstun, KB vel, damage-state entry). It is not yet a full
  // collision+damage pipeline replica (e.g. armor/reflect/absorb), but it does model the collision
  // multiplier chain that feeds ftColl_80079AB0 (attack/defense ratios and gm_8016B248()).
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079AB0
  // refs/melee/src/melee/gm/gm_16AE.c::gm_8016B248
  //
  // Intentionally NOT set in this pass:
  // - last_attack_landed (Slippi "Last Hitting Attack ID") requires attack-id extraction.
  // - combo_count requires combo tracking logic beyond strict one-step mutation.

  const MslCommonParams* c = msl_common_params();

  // BODY damage (staling):
  //
  // Decomp (GALE01): collision applies stale-move multiplier to hitbox->damage (float) before
  // converting to int via getEnvDmg and before Fighter_ProcessHit consumes the values for
  // percent/hitlag/knockback.
  // - Stale multiplier: refs/melee/src/melee/ft/ft_0881.c::ft_80089118
  // - getEnvDmg pattern: refs/melee/src/melee/ft/ftcoll.c
  // - Hitlag/percent consumption: refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //
  // Simulator policy: apply staling to the float damage that drives percent/KB, and compute the
  // int damage input for hitlag from the staled float (decomp-shaped).
  //
  // Use the collision-time HitCapsule attack id, not the attacker's live motion-state field after
  // another same-frame hit may have already mutated that fighter into Damage*. The caller snapshots
  // `pre_combat_attack_id` before pass-1 mutations.
  // Decomp trail:
  // - ftColl_8007ABD0 builds HitCapsule.damage from fp->x2068 via ft_80089228 before collision,
  // - Fighter_ProcessHit later consumes the already-staled HitCapsule damage.
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
  // refs/melee/src/melee/ft/ft_0881.c::{ft_80089118,ft_80089228}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t move_id = attacker_attack_id;
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, move_id);

  float hb_dmg = combat_apply_attacker_smash_release_damage_mul(batch, a_idx,
                                                                batch->state.hitbox_damage[hb_i]);
  const int hitcapsule_int_dmg =
      (batch->state.smash_charge_state[a_idx] == 3u) ? (int)hb_dmg : int_dmg;
  if (stale_mult != 1.0f) {
    hb_dmg *= stale_mult;
  }

  // Applied damage (float) drives percent and the KB magnitude computation inputs.
  //
  // Decomp trail (GALE01):
  // - Collision accumulates per-frame float damage into fp->dmg.x1838_percentTemp via ftColl_80076640.
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076640
  // - Fighter_ProcessHit consumes fp->dmg.x1838_percentTemp for percent add.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //
  // In this BODY-only path, `hb_dmg` is already stale-adjusted; additional modifiers/gates (armor,
  // reflect/absorb, etc.) are handled elsewhere or require seed fields we do not yet represent.
  const float dmg_f = hb_dmg;

  // Decomp: collision converts float damage -> env int via getEnvDmg, and Fighter_ProcessHit uses
  // a nonzero fp->dmg.x183C_applied (`bool1`) as ftCommon_CalcHitlag's `dmg` input.
  // refs/melee/src/melee/ft/ftcoll.c::getEnvDmg + ftColl_80076640 (x183C_applied update)
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (hitlag calc under `if (bool1)`)
  //
  // Keep two integer damage lanes:
  // - `dmg_i`: env damage from applied float (drives hitlag path via x183C_applied)
  // - `int_dmg`: collision HitCapsule integer lane (drives ftColl_80079AB0 KB formula)
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_80079EA8
  const int dmg_i = combat_get_env_dmg(dmg_f);
  if (dmg_i <= 0) {
    return;
  }

  // Percent-temp accumulation (BODY): fp->dmg.x1838_percentTemp.
  //
  // Decomp trail (GALE01):
  // - Collision accumulates per-frame float damage into fp->dmg.x1838_percentTemp via ftColl_80076640.
  //   refs/melee/src/melee/ft/ftcoll.c::ftColl_80076640
  // - Fighter_ProcessHit consumes it for percent add, then resets it to 0 at end of the frame.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const float percent_pre = batch->state.percent[d_idx];
  batch->state.percent_temp[d_idx] += dmg_f;
  const float dmg_temp = batch->state.percent_temp[d_idx];

  uint16_t d_motion_id = batch->state.action_id[d_idx];
  if (d_motion_id == (uint16_t)MSL_ACT_FALL &&
      msl_action_is_thrown_victim(batch->state.prev_action_id[d_idx])) {
    // Deferred throw-release bridge ownership:
    // - This sim may transiently place the victim in FALL before deferred throw-hit consume.
    // - In decomp, set_throw_flags consume + throw-hit apply run while victim is still in Thrown*;
    //   there is no intermediate FALL state feeding Damage calc inputs.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
    d_motion_id = batch->state.prev_action_id[d_idx];
  }

  const uint8_t element = batch->state.hitbox_element[hb_i];
  // Decomp/ASM: fp->x1960_vibrateMult electric override is victim-owned (ftColl_8007A06C writes
  // 0x1960 on the fighter being processed as the collision victim), so attacker-side hitlag should
  // use mul=1.0 while defender-side uses the element-derived multiplier.
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  const float d_hitlag_mul = combat_hitlag_mul_from_element(c, element);
  const uint16_t a_hl = combat_calc_hitlag_frames(c, dmg_i, attacker_motion_id, 1.0f);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, dmg_i, d_motion_id, d_hitlag_mul);
  if (!combat_received_kb_hitlag_owns_over_deal_hitlag(batch, a_idx) &&
      a_hl > batch->state.hitlag[a_idx]) {
    batch->state.hitlag[a_idx] = a_hl;
    combat_state_flags_set_is_hitlag(batch, a_idx, a_hl);
  }
  const uint16_t d_hl_prev = batch->state.hitlag[d_idx];
  const uint8_t d_hl_increased = (d_hl > d_hl_prev) ? 1u : 0u;

  // Grabbed/thrown victims are driven by an attachment joint and have empty Phys/Coll callbacks in
  // decomp; do not force a Damage* transition from a collision-confirmed hit while the victim is
  // still attached to the thrower/grab-owner.
  //
  // Decomp anchors (GALE01):
  // - Thrown victim pos driver: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
  // - Thrown* Phys/Coll are empty (attachment-driven loop):
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::{ftCo_ThrownF_Phys,ftCo_ThrownF_Coll}
  // - Throw scripts can apply damage/hitlag via set_throw_hitbox while the victim is still Thrown*:
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  const uint8_t d_grab_owner = batch->state.grab_owner_port[d_idx];
  if (d_grab_owner != 0xFFu && d_grab_owner == (uint8_t)attacker &&
      msl_action_is_grabbed_victim(d_motion_id)) {
    if (d_hl_increased) {
      batch->state.hitlag[d_idx] = d_hl;
      combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
    }
    // Attached grabbed/thrown victim lane ownership:
    // - Keep instance attribution (`x18EC`) on the confirming contact, but do not overwrite
    //   source player (`x18C4`) here.
    // - In decomp, source-player reset ownership is timer-driven in Fighter_8006A360 via
    //   `dmg.x18C8` expiry, not a direct write in this attached-collision branch.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Thrown.c::ftCo_800DE508
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];

    // Decomp: Fighter_ProcessHit can set fp->x221A_b3 alongside hitlag start under KB/damage paths.
    // For ThrowF/ThrownF style attached hits, Slippi observes x221A_b3 set even though the victim
    // does not enter Damage* (hitstun/misc-as remains non-damage).
    //
    // Evidence (suite dataset):
    // - datasets/.../AttachedGoodNaturedGuanaco.msl record=215 p0 ref_t1 has:
    //   hitstun=0, hitlag=4, state_flags[1]=0x30 (0x20 isHitlag | 0x10 x221A_b3).
    //
    // Decomp anchor:
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    if (d_hl_increased) {
      combat_state_flags_set_x221a_b3(batch, d_idx);
      // Keep x221C_b0 ownership on the attached fighter-vs-fighter hitlag lane; Damage no-reaction
      // gate consumes this bit in inlineB1 while transition clears are owned by motion-state change.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
      // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
      combat_state_flags_set_x221c_b0(batch, d_idx);
    }

    // Attacker-side staling/combo tracking still updates on the confirmed hit.
    const uint16_t attack_instance = batch->state.attack_instance[a_idx];
    staling_queue_update(batch, a_idx, move_id, attack_instance);
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, attacker_attack_id);
    return;
  }

  // Knockback velocity + hitstun + damage-state entry (BODY).
  //
  // Decomp entry:
  // - Fighter_ProcessHit consumes `fp->dmg.kb_applied` (computed by collision) and then:
  //   - ftCo_Damage_CalcKnockback (scales/clamps kb_applied),
  //   - ftCo_8008EC90 / ftCo_8008E908 -> ftCo_8008DCE0 (damage state entry, kb vel, hitstun).
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_CalcKnockback
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008E908
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  const uint16_t hb_angle = batch->state.hitbox_angle[hb_i];
  const uint16_t hb_kbg = batch->state.hitbox_kbg[hb_i];
  const uint16_t hb_wsk = batch->state.hitbox_wsk[hb_i];
  const uint16_t hb_bkb = batch->state.hitbox_bkb[hb_i];

  const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;
  const uint16_t pre_damage_action = batch->state.action_id[d_idx];
  const uint8_t hurt_height = batch->state.hurtcap_height[cap_i];

  const MslCharParams* d_ch = msl_char_params(batch->state.char_id[d_idx]);
  const size_t bi = a_idx / (size_t)MSL_MAX_PLAYERS;
  float coll_kb_mul = batch->state.match_damage_ratio[bi];
  coll_kb_mul *= batch->state.attack_ratio[a_idx];
  coll_kb_mul *= batch->state.defense_ratio[d_idx];
  // NOTE: This multiplier chain is applied only to kb_applied (ftColl_80079AB0 output), not to
  // percent add. Keep any percent/damage scaling tasks separate and decomp-backed.
  if (!(coll_kb_mul > 0.0f)) {
    coll_kb_mul = 1.0f;
  }
  const float kb_applied = combat_damage_calc_kb_applied(
      c, d_ch, d_motion_id, percent_pre, dmg_temp, hitcapsule_int_dmg, hb_kbg, hb_wsk, hb_bkb,
      coll_kb_mul, batch->state.dmg_x2225_b7[d_idx], batch->state.dmg_x2224_b2[d_idx],
      batch->state.kb_smashcharge_active[d_idx]);
  const float kb_angle_rad =
      combat_damage_calc_angle_radians(c, hb_angle, defender_on_ground, kb_applied);

  // Decomp (GALE01): Fighter_ProcessHit_8006D1EC only enters the damage/KB path when
  // `fp->dmg.kb_applied` is nonzero via the exact conditional:
  // - `forceAppliedOnHit = fp->dmg.kb_applied;`  (fighter.c:2839)
  // - `if (forceAppliedOnHit) {`                 (fighter.c:2840)
  // which then does `Fighter_UnkTakeDamage_8006CC30` + `ftCo_Damage_CalcKnockback` + damage state entry.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  if (kb_applied == 0.0f) {
    if (d_hl_increased) {
      batch->state.hitlag[d_idx] = d_hl;
      combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
    }
    batch->state.speed_x_attack[d_idx] = 0.0f;
    batch->state.speed_y_attack[d_idx] = 0.0f;
    batch->state.hitstun[d_idx] = 0;
    combat_state_flags_set_is_hitstun(batch, d_idx, 0);
    batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
    combat_processhit_commit_source_owner(batch, d_idx, (uint8_t)attacker);

    // Stale-move queue update on successful damaging BODY hit (attacker-side).
    // Decomp: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromFighter
    const uint16_t attack_instance = batch->state.attack_instance[a_idx];
    staling_queue_update(batch, a_idx, move_id, attack_instance);

    // Combo count + last-attack tracking still happens on the collision-confirmed hit, even if the
    // later damage-state path is skipped due to `fp->dmg.kb_applied == 0`.
    // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_80076444 -> ftColl_800763C0(fp->x2068_attackID).
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, attacker_attack_id);
    return;
  }

  // KB velocity + grounded/airborne interaction.
  //
  // Decomp entry: ftCo_8008DCE0 computes:
  // - var_f31 = kb_applied * p_ftCommonData->x100 (kb_vel_mul),
  // - x = var_f31 * cos(kb_angle), y = var_f31 * sin(kb_angle),
  // - then applies sign via `-x * fp->facing_dir` after setting `fp->facing_dir = fp->dmg.facing_dir_1`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  float kb_vel_mag = kb_applied * c->kb_vel_mul;
  if (!defender_on_ground && combat_damage_check_air_motion_kb_mul(c, batch, d_idx)) {
    kb_vel_mag *= c->air_motion_kb_mul;
  }

  const float x = kb_vel_mag * cosf(kb_angle_rad);
  const float y = kb_vel_mag * sinf(kb_angle_rad);

  // Horizontal sign for knockback:
  //
  // Decomp:
  // - ftCo_8008DCE0 uses `fp->dmg.facing_dir_1` (set by collision) to set `fp->facing_dir`, then flips
  //   the KB x component via `-x * fp->facing_dir`.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  //
  // Decomp (GALE01): collision writes fp->dmg.facing_dir_1 as a +/-1 sign based on relative X
  // position between the victim and the source (fighter/item), e.g.:
  // - If victim_pos.x > src_pos.x => facing_dir_1 = -1
  // - Else                        => facing_dir_1 = +1
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C (0x8007A74C..0x8007A77C sets f24)
  const uint8_t downed_damage_contact_facing_owner =
      (uint8_t)(combat_is_downed_damage_contact_action(pre_damage_action) &&
                (batch->state.dmg_x2224_b2[d_idx] ||
                 batch->state.percent_temp[d_idx] < (float)c->down_damage_percent_threshold));
  const float one = combat_damage_ftColl_804D82EC_one();
  const float collision_facing_dir_1 =
      (batch->state.pos_x[d_idx] > batch->state.pos_x[a_idx]) ? -one : one;
  // DownDamage contact entry has two separate facing owners:
  // - collision still owns `dmg.facing_dir_1`, which ftCo_8008DCE0 uses for the KB x sign before
  //   block_44;
  // - ftCo_8009F184 passes the fighter's current `fp->facing_dir` as arg2 so block_44 restores the
  //   visible facing after velocity calculation.
  //
  // Keep those lanes separate so downed reverse-shine / jab-reset-style contacts can launch by
  // source position without turning the downed victim around.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_DownDamage.c::ftCo_8009F184
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  const float defender_facing_dir_1 = collision_facing_dir_1;
  const float post_damage_facing_dir = downed_damage_contact_facing_owner
                                           ? (batch->state.facing[d_idx] ? one : -one)
                                           : collision_facing_dir_1;
  batch->state.facing[d_idx] = (uint8_t)(post_damage_facing_dir > 0.0f);

  const float kb_x = -x * defender_facing_dir_1;
  const float kb_y = y;

  // Ground-vs-air handling for KB velocity:
  // - When grounded, ftCo_8008DCE0 compares the floor normal to the KB vector (lbVector_Angle) and:
  //   - if angle < 90°, launches with full (kb_x, kb_y) and clears grounded state (ftCommon_8007D5D4),
  //   - else (angle >= 90°) and low/med damage: keeps grounded and projects along the floor normal,
  //   - else tumble (sev==3): launches regardless.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (blocks 21-28)
  if (!defender_on_ground) {
    combat_damage_calc_vel(batch, d_idx, kb_x, kb_y);
  } else {
    combat_damage_install_grounded_kb(c, batch, d_idx, kb_applied, kb_x, kb_y, d_hl,
                                      (!combat_is_downed_damage_contact_action(pre_damage_action) &&
                                       combat_shine_start_grounded_ledge_ecb_lock_owner(
                                           batch, d_idx, batch->state.action_id[a_idx]))
                                          ? 1u
                                          : 0u);
  }

  // Decomp: after setting KB velocity, ftCo_8008DCE0 clears self velocity (self_vel and gr_vel).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (block_28)
  batch->state.speed_air_x_self[d_idx] = 0.0f;
  batch->state.speed_ground_x_self[d_idx] = 0.0f;
  batch->state.speed_y_self[d_idx] = 0.0f;

  const uint16_t hs = combat_damage_hitstun_from_kb(c, kb_applied);
  batch->state.hitstun[d_idx] = hs;
  combat_state_flags_set_is_hitstun(batch, d_idx, hs);
  combat_state_flags_clear_x221c_b0(batch, d_idx);
  combat_damage_mark_entry_time_since_hit(batch, d_idx);
  // Decomp: Fighter_ProcessHit can set fp->x221A_b3 alongside hitlag start under KB/damage paths
  // (see `bool2`). The stable latch point we model here is the received-hit bool1 path, because
  // x221A_b3 is:
  // - set at hitlag start by Fighter_ProcessHit_8006D1EC, and
  // - cleared when hitlag ends by Fighter_8006A1BC.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  //
  // This intentionally excludes throw-release damage entry (ftCo_800DDDE4) where Slippi observes
  // hitstun without hitlag and x221A_b3 unset.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  const uint8_t defender_on_ground_after = batch->state.on_ground[d_idx] ? 1u : 0u;
  combat_damage_enter_state(c, batch, (int)bi, d_idx, defender_on_ground, defender_on_ground_after,
                            hurt_height, kb_applied, kb_angle_rad);
  // Decomp ordering: Damage state entry runs before hitlag assignment in Fighter_ProcessHit.
  // - forceAppliedOnHit path enters ftCo_8008DCE0 (ChangeMotionState + immediate ftAnim_8006EBA4),
  // - then bool1 drives fp->dmg.x195c_hitlag_frames assignment.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  if (d_hl > 0u) {
    // Fighter_ProcessHit priority for reciprocal BODY hits:
    // - If this fighter received KB (`forceAppliedOnHit`), bool1 is `dmg.x183C_applied`.
    // - Deal-hitlag lanes (`dmg.x1914`/`x1924`) are only consulted when no received KB is pending.
    // Therefore a same-frame outgoing hitlag value already written by this simplified sequential
    // pass must not keep a larger value than the received-hit `d_hl`.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    batch->state.hitlag[d_idx] = d_hl;
    combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
    combat_state_flags_set_x221a_b3(batch, d_idx);
  }

  batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
  batch->state.last_hit_by[d_idx] = combat_source_port0_for_attacker(batch, a_idx, attacker);

  // Stale-move queue update on successful damaging BODY hit (attacker-side).
  // Decomp: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromFighter
  const uint16_t attack_instance = batch->state.attack_instance[a_idx];
  staling_queue_update(batch, a_idx, move_id, attack_instance);

  // Combo count + last-attack tracking (attacker-side).
  // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_80076444 -> ftColl_800763C0(fp->x2068_attackID).
  combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, attacker_attack_id);
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
  // - ftColl_80076ED8 halves the float damage and stores it into fp->dmg.x1840.
  // - Fighter_ProcessHit consumes x1840 through the x18a0 branch, starting victim hitlag without
  //   percent/KB/damage-state entry and without attacker hitlag.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  float phantom_dmg = 0.5f * dmg_f;
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
  }

  batch->state.phantom_damage_pending_x1898[d_idx] = phantom_dmg;
  batch->state.phantom_damage_timer_x189c[d_idx] = d_hl;
  batch->state.phantom_damage_source_port[d_idx] =
      (attacker >= 0 && attacker < (int)batch->config.num_players) ? (uint8_t)attacker : 0xFFu;
  batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
  batch->state.last_hit_by[d_idx] = combat_source_port0_for_attacker(batch, a_idx, attacker);
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
  }
  batch->state.instance_hit_by[d_idx] = item_instance_id;
  batch->state.last_hit_by[d_idx] = combat_source_port0_for_attacker(batch, a_idx, attacker);
}

MslItemHitResult combat_apply_item_hit(MslBatch* batch, int batch_index, int attacker, int defender,
                                       uint16_t item_attack_id, uint16_t item_attack_instance,
                                       uint16_t item_instance_id, uint16_t item_type,
                                       uint8_t item_state, float damage, uint16_t angle,
                                       uint16_t kbg, uint16_t wsk, uint16_t bkb,
                                       uint8_t defender_hurt_height, uint8_t element) {
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

  enum {
    MSL_COMBAT_IT_KIND_FOX_ILLUSION = 56,
    MSL_COMBAT_IT_KIND_FALCO_PHANTASM = 57,
  };
  const uint8_t item_is_illusion = (item_type == (uint16_t)MSL_COMBAT_IT_KIND_FOX_ILLUSION ||
                                    item_type == (uint16_t)MSL_COMBAT_IT_KIND_FALCO_PHANTASM)
                                       ? 1u
                                       : 0u;

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
  // The item apply path receives the base hitbox damage (integer-valued for Fox/Falco blasters),
  // but hitlag/KB lanes below follow decomp ownership via getEnvDmg(staled_damage).
  const int dmg_raw_i = (int)damage;
  if (dmg_raw_i <= 0) {
    return MSL_ITEM_HIT_NONE;
  }

  // Decomp (GALE01): item collision applies staling to the item's hitbox damage before the
  // float->int getEnvDmg conversion and before Fighter_ProcessHit consumes the values.
  // refs/melee/src/melee/it/itcoll.c::it_80272460 (calls ft_80089228)
  // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
  float dmg_f_base = damage;
  const float stale_mult = staling_multiplier_for_move(batch, a_idx, item_attack_id);
  if (stale_mult != 1.0f) {
    dmg_f_base *= stale_mult;
  }

  const uint16_t d_motion_id = batch->state.action_id[d_idx];
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

  float dmg_f = dmg_f_base;
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
    dmg_f *= c->ftcoll_damage_mul_x128;
  }

  const int dmg_env_i = combat_get_env_dmg(dmg_f);
  if (dmg_env_i <= 0) {
    return MSL_ITEM_HIT_NONE;
  }

  // Percent-temp accumulation (BODY): fp->dmg.x1838_percentTemp.
  const float percent_pre = batch->state.percent[d_idx];
  batch->state.percent_temp[d_idx] += dmg_f;
  const float dmg_temp = batch->state.percent_temp[d_idx];

  // Special-case: non-flinch laser hits.
  //
  // Scope gate: only apply this rule to item kinds that are known (via ISO-extracted MSLLASR1
  // lasers.bin) to be Fox/Falco blaster shots.
  const MslLaserParams* lp = laser_params_for_item_type(item_type);
  const uint8_t non_flinch_hit =
      (lp != NULL && ((item_state == 0u) ? lp->non_flinch : lp->state1_non_flinch) != 0u) ? 1u : 0u;
  if (non_flinch_hit) {
    // Extracted proxy lane ownership:
    // - non_flinch is extracted into MSLLASR1 from article hitbox kbg/wsk/bkb terms
    //   (tools/extraction/extract_lasers.py), then consumed directly here.
    // - This narrows runtime proxy logic to data ownership instead of recomputing the triplet test
    //   in combat, but the source signal is still derived from KB terms.
    // TODO(decomp/non-flinch-authoritative-signal): replace this KB-triplet-derived lane with a
    // truly authoritative decomp/data-owned no-flinch signal once identified.
    batch->state.instance_hit_by[d_idx] = item_instance_id;
    combat_processhit_commit_source_owner(batch, d_idx, (uint8_t)attacker);
    // Non-flinch damage still routes through Fighter_ProcessHit's percent-temp consume without a
    // fresh Damage* entry. Keep fp->x221C_b0 aligned to the same hidden-damage ownership so the
    // post-frame no-reaction lane does not stale-carry after the item hit is accepted.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
    combat_state_flags_clear_x221c_b0(batch, d_idx);

    // Stale-move queue update on successful damaging BODY hit (attacker-side).
    // Decomp: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    staling_queue_update(batch, a_idx, item_attack_id, item_attack_instance);

    // Combo count + last-attack tracking (attacker-side).
    // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, item_attack_id);
    return MSL_ITEM_HIT_APPLIED_CONSUME_ITEM;
  }

  const float hitlag_mul = combat_hitlag_mul_from_element(c, element);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, dmg_env_i, d_motion_id, hitlag_mul);
  const uint16_t d_hl_prev = batch->state.hitlag[d_idx];
  const uint8_t same_frame_throwhi_item_damage_topoff =
      (lp != NULL && item_state == (uint8_t)1u &&
       batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_HI &&
       batch->state.hitlag_pre_timer[d_idx] == 0u && d_hl_prev > 0u &&
       batch->state.hitstun[d_idx] > 0u &&
       batch->state.instance_hit_by[d_idx] == item_instance_id &&
       batch->state.last_hit_by[d_idx] == combat_source_port0_for_attacker(batch, a_idx, attacker))
          ? 1u
          : 0u;
  if (d_hl > d_hl_prev) {
    batch->state.hitlag[d_idx] = d_hl;
    combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
  }

  if (item_state == (uint8_t)1u && batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_LW &&
      batch->state.throw_pending_victim_port[a_idx] == (uint8_t)defender &&
      batch->state.throw_pending_hit_idx[a_idx] != 0xFFu &&
      (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_FALL ||
       (batch->state.grab_owner_port[d_idx] == (uint8_t)attacker &&
        msl_action_is_grabbed_victim(batch->state.action_id[d_idx])))) {
    // ThrowLw release + same-frame blaster ordering bridge:
    // - ftCo_800DD724 consumes set_throw_flags(0) in ThrowLw Anim and applies the throw release hit
    //   via ftCo_800DE2A8/ftCo_800DDDE4 before later frame contacts.
    // - On replay-real one-step rows the pending release victim can still be visible as attached
    //   `Thrown*` at the item-collision snapshot even though the common release event already
    //   belongs to this frame.
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
    // staled float damage (see combat_mutations_pass1_future_apply_body_hit for the fighter-vs-fighter
    // equivalent). For attached projectile hits, suite refs observe grab-owner hitlag=3 when
    // victim hitlag=4 for Falco lasers (damage=3 => d_hl=4; getEnvDmg(staled_damage)=1 => a_hl=3).
    //
    // Victim-side hitlag_mul is driven by fp->x1960_vibrateMult (electric hits), but attacker-side
    // hitlag in this case is observed to *not* apply the electric multiplier.
    // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s (fp->x1960_vibrateMult set site)
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC (passes vibrateMult into CalcHitlag)
    const uint16_t a_hl = combat_calc_hitlag_frames(c, dmg_env_i, a_motion_id, 1.0f);
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
    batch->state.last_hit_by[d_idx] = prev_last_hit_by;

    // Attached item-hit bookkeeping:
    // - Throw-side item hits on an attached victim still feed the attacker-side item-domain stale
    //   move queue and combo lanes even when victim state entry stays in Thrown*/Capture*.
    // - Keep the non-Damage* victim suppression above, but preserve ftColl_8007646C ownership for
    //   last_attack_landed/combo_count in the item attack-id domain.
    // refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007646C,ftColl_800763C0}
    staling_queue_update(batch, a_idx, item_attack_id, item_attack_instance);
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, item_attack_id);

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
  const MslCharParams* d_ch = msl_char_params(batch->state.char_id[d_idx]);
  if (d_ch == NULL) {
    return MSL_ITEM_HIT_NONE;
  }

  const float kb_applied = combat_damage_calc_kb_applied(
      c, d_ch, d_motion_id, percent_pre, dmg_temp, dmg_env_i, kbg, wsk, bkb, 1.0f,
      batch->state.dmg_x2225_b7[d_idx], batch->state.dmg_x2224_b2[d_idx],
      batch->state.kb_smashcharge_active[d_idx]);
  const uint16_t hs = combat_damage_hitstun_from_kb(c, kb_applied);
  const float kb_angle_rad =
      combat_damage_calc_angle_radians(c, angle, defender_on_ground, kb_applied);

  // Decomp: Fighter_ProcessHit only enters common Damage* state entry when `fp->dmg.kb_applied`
  // is nonzero. Otherwise the percent-only path consumes Fighter_UnkTakeDamage and grounded
  // ftCommon_800804FC source-clear ownership without hitstun or Damage* entry.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  if (kb_applied == 0.0f) {
    batch->state.speed_x_attack[d_idx] = 0.0f;
    batch->state.speed_y_attack[d_idx] = 0.0f;
    batch->state.hitstun[d_idx] = 0u;
    combat_state_flags_set_is_hitstun(batch, d_idx, 0u);
    batch->state.instance_hit_by[d_idx] = item_instance_id;
    combat_processhit_commit_source_owner(batch, d_idx, (uint8_t)attacker);

    staling_queue_update(batch, a_idx, item_attack_id, item_attack_instance);
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, item_attack_id);
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
  // - Generic item BODY hits follow the same facing_dir_1 ownership as BODY hits:
  //   collision stores fp->dmg.facing_dir_1 from the relative X ordering between victim and
  //   source, then ftCo_8008DCE0 sets facing from that sign before applying `-x * facing_dir_1`.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  //   refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007A06C
  // - ThrowHi throw-side blaster shots are spawned by ftFx_Throw_Anim via it_8029C6CC in the
  //   state1 projectile lane, and their active BODY overlap is driven forward along the scripted
  //   shot velocity rather than the thrower root transform.
  //   refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialN.c::ftFx_Throw_Anim
  //   refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C6CC
  const float one = combat_damage_ftColl_804D82EC_one();
  float defender_facing_dir_1 =
      (batch->state.pos_x[d_idx] > batch->state.pos_x[a_idx]) ? -one : one;
  if (lp != NULL && item_state == (uint8_t)1u &&
      batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_LW &&
      batch->state.throw_pending_victim_port[a_idx] == (uint8_t)defender &&
      batch->state.throw_pending_hit_idx[a_idx] != 0xFFu) {
    // ThrowLw release + same-frame blaster ordering:
    // - ftCo_800DD724 / ftCo_800DDDE4 have already installed the released victim's facing lane
    //   before the later throw-side laser overlap is processed.
    // - Keep the late pulse on that already-owned left/right sign instead of recomputing from the
    //   fighter-vs-fighter X ordering used by ordinary item BODY hits.
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::{ftCo_800DD724,ftCo_800DDDE4}
    defender_facing_dir_1 = batch->state.facing[d_idx] ? one : -one;
  }
  // Keep ThrowHi's thrower-facing ownership scoped to the initial full state1 refresh. Later
  // top-off overlaps (+1 hitlag frame) stay on the generic BODY facing lane.
  if (lp != NULL && item_state == (uint8_t)1u &&
      batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_HI &&
      d_hl > (uint16_t)(d_hl_prev + 1u)) {
    const float thrower_facing_dir = batch->state.facing[a_idx] ? one : -one;
    defender_facing_dir_1 = -thrower_facing_dir;
  }
  batch->state.facing[d_idx] = (uint8_t)(defender_facing_dir_1 > 0.0f);

  const float kb_x = -defender_facing_dir_1 * (kb_vel_mag * cosf(kb_angle_rad));
  const float kb_y = kb_vel_mag * sinf(kb_angle_rad);

  if (same_frame_throwhi_item_damage_topoff && d_hl <= d_hl_prev &&
      hs <= batch->state.hitstun[d_idx]) {
    // Same-frame ThrowHi state1 laser top-off:
    // - Multiple throw-side state1 articles can overlap the same already-damaged victim in one
    //   item pass. In vanilla, their HitCapsule damage contributes to the same
    //   Fighter_ProcessHit percent-temp frame, but the first accepted hit owns the Damage entry and
    //   x2088 motion-state instance.
    // - The later article can still contribute to the ftCo_Damage_CalcVel merge. The simulator
    //   processes items serially, so without this boundary it sees x18AC reset by the first entry
    //   and incorrectly treats the later top-off as a fresh replace + Damage entry.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_CalcVel,ftCo_8008DCE0}
    // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    combat_damage_merge_vel_after_window(batch, d_idx, kb_x, kb_y);
    staling_queue_update(batch, a_idx, item_attack_id, item_attack_instance);
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, item_attack_id);
    if (item_is_illusion) {
      return MSL_ITEM_HIT_APPLIED_DONT_CONSUME;
    }
    return MSL_ITEM_HIT_APPLIED_CONSUME_ITEM;
  }

  // Item BODY hits route through Fighter_ProcessHit the same way as fighter BODY hits, so grounded
  // victims use the same ftCo_8008DCE0 ground-vs-air KB install owner:
  // - launch with full (kb_x, kb_y) and clear grounded state via ftCommon_8007D5D4 when the floor
  //   normal dot KB vector is positive or the damage severity is tumble,
  // - otherwise keep grounded and project along the floor.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  if (!defender_on_ground) {
    combat_damage_calc_vel(batch, d_idx, kb_x, kb_y);
  } else {
    combat_damage_install_grounded_kb(c, batch, d_idx, kb_applied, kb_x, kb_y, d_hl,
                                      (!combat_is_downed_damage_contact_action(d_motion_id) &&
                                       combat_shine_start_grounded_ledge_ecb_lock_owner(
                                           batch, d_idx, batch->state.action_id[a_idx]))
                                          ? 1u
                                          : 0u);
  }

  batch->state.hitstun[d_idx] = hs;
  combat_state_flags_set_is_hitstun(batch, d_idx, hs);
  combat_damage_mark_entry_time_since_hit(batch, d_idx);
  // Mirror Fighter_ProcessHit's x221A_b3 update shape (gate on hitlag start).
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  if (d_hl > d_hl_prev) {
    combat_state_flags_set_x221a_b3(batch, d_idx);
  }

  // Decomp: item/fighter BODY hits still route through Fighter_ProcessHit -> ftCo_8008DCE0 for
  // damage-state entry, and ftCo_8008DCE0 clears self_vel/gr_vel at block_28 before selecting the
  // Damage* motion state.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  batch->state.speed_air_x_self[d_idx] = 0.0f;
  batch->state.speed_ground_x_self[d_idx] = 0.0f;
  batch->state.speed_y_self[d_idx] = 0.0f;

  // Decomp: ftCo_8008DCE0 can clear grounded state (ftCommon_8007D5D4) before selecting the
  // damage motion state. Use the post-KB on_ground value for state entry.
  const uint8_t defender_on_ground_after = batch->state.on_ground[d_idx] ? 1u : 0u;
  combat_damage_enter_state(c, batch, batch_index, d_idx, defender_on_ground,
                            defender_on_ground_after, defender_hurt_height, kb_applied,
                            kb_angle_rad);
  combat_apply_guard_reflect_body_hit_followup(c, batch, d_idx, d_motion_id);

  batch->state.instance_hit_by[d_idx] = item_instance_id;
  batch->state.last_hit_by[d_idx] = combat_source_port0_for_attacker(batch, a_idx, attacker);

  // Stale-move queue update on successful damaging BODY hit (attacker-side).
  // Decomp: refs/melee/src/melee/pl/plstale.c::plStale_UpdateStaleMovesFromItem
  staling_queue_update(batch, a_idx, item_attack_id, item_attack_instance);

  // ThrowLw release-frame combo-victim continuation:
  // - Throw Anim detaches the victim immediately and defers the throw hit to post-items
  //   (`throw_pending_victim_port` in this sim), so a same-frame blaster hit can land after
  //   release while the attacker is still in the throw-laser attack-id domain.
  // - When fp->x2094 was not seed-visible but the pending released victim matches this item hit,
  //   preserve ftColl_800763C0 continuation ownership instead of restarting combo_count at 1.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DD724
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_800763C0,ftColl_8007646C}
  if (batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_LW &&
      batch->state.throw_pending_victim_port[a_idx] == (uint8_t)defender &&
      batch->state.combo_victim_port[a_idx] == 0xFFu &&
      item_attack_id != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
      batch->state.attack_id[a_idx] == item_attack_id && batch->state.combo_count[a_idx] != 0u &&
      batch->state.last_attack_landed[a_idx] == (uint8_t)item_attack_id) {
    batch->state.combo_victim_port[a_idx] = (uint8_t)defender;
    batch->state.combo_victim_instance_id[a_idx] = batch->state.instance_id[d_idx];
  }

  // Combo count + last-attack tracking (attacker-side).
  // Decomp: refs/melee/src/melee/ft/ftcoll.c::ftColl_8007646C -> ftColl_800763C0(item attack id domain).
  combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, item_attack_id);
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

  // Decomp (GALE01):
  // - set_throw_hitbox (ftAction_80071E04) writes the raw integer damage into HitCapsule.unk_count
  //   and the staled float into HitCapsule.damage via ftColl_8007ABD0 -> ft_80089228(fp->x2068, fp->x206c).
  //   refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071E04
  //   refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
  //
  // In this lite sim, the throw hitbox params come from extracted move script data (base integer
  // damage). Apply staling to the float percent write (HitCapsule.damage analog), but keep the
  // raw integer damage (HitCapsule.unk_count analog) for hitlag/KB inputs.
  const int dmg_raw_i = (int)p->damage;
  if (dmg_raw_i <= 0) {
    return 0;
  }

  // Throw-hit damage ownership:
  // - set_throw_hitbox writes HitCapsule.damage through ft_80089228(fp->x2068, fp->x206c, raw_damage).
  // - On ThrowLw release rows in this family, the live attack-id lane is already the throw-laser
  //   item-domain id (`56`), so use that seeded/runtime identity when present instead of forcing a
  //   state-based ThrowLw move id.
  // refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071E04
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
  uint16_t move_id = batch->state.attack_id[a_idx];
  if (move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT) {
    move_id = staling_move_id_from_state(batch, a_idx);
  }
  // Throw hit capsules store their staled float damage when the set_throw_hitbox movescript event
  // creates the capsule. Same-instance low-throw laser contacts can update the stale queue before
  // this simulator's deferred release hit applies, but those later queue entries must not
  // retroactively stale the already-created capsule damage.
  // refs/melee/build/GALE01/asm/melee/ft/ftaction.s::ftAction_80071E04
  // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
  const uint16_t throw_attack_instance = batch->state.attack_instance[a_idx];
  const float stale_mult =
      staling_multiplier_for_move_excluding_instance(batch, a_idx, move_id, throw_attack_instance);

  float dmg_f = p->damage;
  if (stale_mult != 1.0f) {
    dmg_f *= stale_mult;
  }
  const int dmg_env_i = combat_get_env_dmg(dmg_f);
  if (dmg_env_i <= 0) {
    return 0;
  }

  if (defender_no_damage) {
    return 0;
  }

  // Percent-temp accumulation (BODY): fp->dmg.x1838_percentTemp.
  const float percent_pre = batch->state.percent[d_idx];
  batch->state.percent_temp[d_idx] += dmg_f;
  const float dmg_temp = batch->state.percent_temp[d_idx];

  const uint16_t d_motion_id = batch->state.action_id[d_idx];

  const MslCharParams* d_ch = msl_char_params(batch->state.char_id[d_idx]);
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
      c, &d_ch_throw, d_motion_id, percent_pre, dmg_temp, dmg_raw_i, p->kbg, p->wsk, p->bkb,
      coll_kb_mul, batch->state.dmg_x2225_b7[d_idx], batch->state.dmg_x2224_b2[d_idx],
      batch->state.kb_smashcharge_active[d_idx]);
  const float kb_angle_rad =
      combat_damage_calc_angle_radians(c, p->angle, defender_on_ground, kb_applied);
  float damage_state_angle_rad = kb_angle_rad;
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
    }
  }

  if (kb_applied == 0.0f) {
    batch->state.speed_x_attack[d_idx] = 0.0f;
    batch->state.speed_y_attack[d_idx] = 0.0f;
    batch->state.hitstun[d_idx] = 0;
    combat_state_flags_set_is_hitstun(batch, d_idx, 0);
    batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
    batch->state.last_hit_by[d_idx] = combat_source_port0_for_attacker(batch, a_idx, attacker);

    if (update_bookkeeping) {
      const uint16_t attack_instance = batch->state.attack_instance[a_idx];
      staling_queue_update(batch, a_idx, move_id, attack_instance);
      combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, batch->state.attack_id[a_idx]);
    }
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

  combat_damage_calc_vel(batch, d_idx, kb_x, kb_y);
  combat_throw_release_apply_immediate_di(batch, d_idx, c);

  // Decomp: after setting KB velocity, ftCo_8008DCE0 clears self velocity (self_vel and gr_vel).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (block_28)
  batch->state.speed_air_x_self[d_idx] = 0.0f;
  batch->state.speed_ground_x_self[d_idx] = 0.0f;
  batch->state.speed_y_self[d_idx] = 0.0f;

  const uint16_t hs = combat_damage_hitstun_from_kb(c, kb_applied);
  batch->state.hitstun[d_idx] = hs;
  combat_state_flags_set_is_hitstun(batch, d_idx, hs);
  combat_damage_mark_entry_time_since_hit(batch, d_idx);
  // Throw-release hits do not apply hitlag in the suite (Slippi hitlag stays 0), and `x221A_b3`
  // is observed unset. Do not set it here.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4

  // Throw hits mark the damaged hurtbox as "mid" in decomp (x184c_damaged_hurtbox = 1).
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  const uint8_t hurt_height = 1u;
  combat_damage_enter_state(c, batch, batch_index, d_idx, defender_on_ground, defender_on_ground,
                            hurt_height, kb_applied, damage_state_angle_rad);
  // Throw-release ordering:
  // - ftCo_800DDDE4 routes into Fighter_ProcessHit damage entry, and ftCo_8008DCE0 already performs
  //   an immediate ftAnim_8006EBA4 on state change.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Throw.c::ftCo_800DDDE4
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0
  //
  // Deferred throw-hit apply in this simulator happens post-items; keep only the damage-entry
  // immediate tick here (no extra deferred tick) so release rows do not over-advance action_frame.

  batch->state.instance_hit_by[d_idx] = batch->state.instance_id[a_idx];
  batch->state.last_hit_by[d_idx] = combat_source_port0_for_attacker(batch, a_idx, attacker);

  if (update_bookkeeping) {
    const uint16_t attack_instance = batch->state.attack_instance[a_idx];
    staling_queue_update(batch, a_idx, move_id, attack_instance);
    combat_combo_ftColl_800763C0(batch, a_idx, defender, d_idx, batch->state.attack_id[a_idx]);
  }

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
  // current-input install. This sim defers the throw hit until post-items, so read the pre-input
  // stick lane preserved in prev_input_main_* rather than the already-applied current input. The
  // L/R x1AC multiplier belongs to ftCo_Damage_OnExitHitlag and is intentionally not applied here.
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

  // Guard-owned lightshield transform:
  // - shield depletion and GuardSetOff stun both consume the current guard lightshield lane,
  // - ftCo_800921DC / ftCo_800925A4 derive that lane from trigger input (`x650`) via the same
  //   deadzone/clamp transform mirrored by combat_lightshield_amount().
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800921DC,ftCo_800925A4,ftCo_80092F2C}
  const float light =
      combat_lightshield_amount(c, batch->state.input_buttons[d_idx], batch->state.input_l[d_idx],
                                batch->state.input_r[d_idx]);
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
  combat_state_flags_clear_guard_reflecting(batch, d_idx);

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

static inline void combat_mutations_pass1_future_apply_shield_hit(MslBatch* batch, size_t a_idx,
                                                                  size_t d_idx, int max_int_dmg,
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

  // Capture defender motion id before we transition into GuardSetOff.
  const uint16_t d_motion_id_pre = batch->state.action_id[d_idx];

  // Shield HP depletion:
  //
  // Decomp collision accumulates `shieldDamageTaken` as Σ max(0, int_dmg + hitbox_shield_damage):
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  //
  // This pass is intentionally simplified: we resolve at most one SHIELD contact per
  // (attacker, defender) per frame (deterministic hitbox_id order), and pass that contact's
  // max(0, int_dmg + hitbox_shield_damage) as `shield_damage_taken`.
  //
  // Fighter_ProcessHit applies the per-frame shield health reduction:
  // shield_health -= x284 * (shieldDamageTaken*(1 - (lightshield_amount*(x2E0-x2DC)+x2DC))) + x288
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  if (max_int_dmg < 0) {
    max_int_dmg = 0;
  }
  if (shield_damage_taken < 0) {
    shield_damage_taken = 0;
  }

  // Powershield gating: collision does not accumulate shieldDamageTaken when the "powershield
  // active" flag is set (x221C_b2).
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC (`if (!fp1->x221C_b2) { ...shieldDamageTaken... }`)
  uint8_t powershield_active = combat_shield_damage_powershield_suppressed_idx(batch, d_idx);
  if (powershield_active) {
    shield_damage_taken = 0;
  }

  const float light =
      combat_lightshield_amount(c, batch->state.input_buttons[d_idx], batch->state.input_l[d_idx],
                                batch->state.input_r[d_idx]);
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
  batch->state.action_id[d_idx] = (uint16_t)MSL_ACT_GUARD_SET_OFF;
  // Decomp: GuardSetOff uses ftCo_SM_GuardDamage as its submotion (msid=40).
  // refs/melee/src/melee/ft/ftmotionstates.c (GuardSetOff motion-state entry uses ftCo_SM_GuardDamage)
  batch->state.animation_index[d_idx] = (uint32_t)MSL_SM_GUARD_DAMAGE;
  combat_state_flags_clear_guard_reflecting(batch, d_idx);

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
      c->shield_stun_mul * ((float)max_int_dmg * (1.0f - ls_stun)) + c->shield_stun_base;
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
    if (!powershield_active) {
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
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC and fighter.c::Fighter_ProcessHit_8006D1EC
  const uint16_t a_hl = combat_calc_hitlag_frames(c, max_int_dmg, attacker_motion_id, 1.0f);
  const uint16_t d_hl = combat_calc_hitlag_frames(c, max_int_dmg, d_motion_id_pre, 1.0f);
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
    const float eval = light * (float)max_int_dmg * c->shield_attacker_ground_kb_mul +
                       c->shield_attacker_ground_kb_base;
    batch->state.attacker_shield_ground_kb_vel[a_idx] =
        (batch->state.pos_x[d_idx] > batch->state.pos_x[a_idx]) ? -eval : eval;
  } else {
    batch->state.attacker_shield_ground_kb_vel[a_idx] = 0.0f;
  }
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
    if (a_motion_id != (uint16_t)MSL_ACT_CATCH && a_motion_id != (uint16_t)MSL_ACT_CATCH_DASH) {
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
      if (batch->state.is_teams[bi] && batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
        continue;
      }
      if (msl_action_owns_respawn_collision_skip(batch->state.action_id[d_idx])) {
        // Rebirth/RebirthWait set fp->x2219_b1. In vanilla, Fighter_8006CB94 does not call the
        // common collision pass for that fighter while the bit is set, and catch selection also
        // rejects x2219_b1 victims. Keep this separate from visible Slippi hurtbox_state: the
        // platform row can still report Wait1/vulnerable hit status while being collision-skipped.
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
      if (combat_defender_downed_catch_mask_blocks(batch->state.action_id[d_idx])) {
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

      uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      const MslHurtCap* catch_fallback_caps = NULL;
      uint16_t catch_fallback_count_u16 = 0u;
      uint8_t use_catch_fallback_caps = 0u;
      if (hurtcap_count == 0 &&
          combat_guardreflect_no_submotion_catch_fallback_applies(batch, d_idx)) {
        if (hurtcaps_get(batch->state.char_id[d_idx], &catch_fallback_caps,
                         &catch_fallback_count_u16) == 0 &&
            catch_fallback_caps != NULL && catch_fallback_count_u16 != 0u) {
          use_catch_fallback_caps = 1u;
          hurtcap_count = catch_fallback_count_u16 > (uint16_t)MSL_MAX_HURTCAPS
                              ? (uint8_t)MSL_MAX_HURTCAPS
                              : (uint8_t)catch_fallback_count_u16;
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

        const float hx = batch->state.hitbox_x[hb_i];
        const float hy = batch->state.hitbox_y[hb_i];
        const float hz = batch->state.hitbox_z[hb_i];
        const float hr = batch->state.hitbox_radius[hb_i];

        uint8_t found_grab_contact = 0u;
        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          float ax = 0.0f, ay = 0.0f, az = 0.0f;
          float bx = 0.0f, by = 0.0f, bz = 0.0f;
          float cr = 0.0f;
          uint8_t overlaps = 0u;
          if (use_catch_fallback_caps) {
            if (!combat_guardreflect_catch_hurtcap_world(batch, d_idx, &catch_fallback_caps[cap_id],
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
            overlaps =
                combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr, NULL);
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
        if (!found_grab_contact) {
          const float shr = batch->state.shield_radius[d_idx];
          // Guard/shield catch fallback:
          // - Vanilla grabs shielded fighters; decomp catch selection ultimately records a fighter
          //   victim (`ftGrabDist` / `victim_gobj`), not a shield-hit event.
          // - Slippi guard-family rows can expose `animation_index=-1` sentinel pose snapshots while
          //   the live shield descriptor is still exact. When pose-derived hurtcaps miss, use the
          //   ShieldDesc world center as a seed-bridge proxy for the shielded fighter's grabbable
          //   center. Do not use shield-edge overlap here: ftColl_80078A2C grabs the fighter, not
          //   the shield rim.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
          // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
          const float shx = batch->state.shield_x[d_idx];
          const float shy = batch->state.shield_y[d_idx];
          const float shz = batch->state.shield_z[d_idx];
          const float dx = shx - hx;
          const float dy = shy - hy;
          const float dz = shz - hz;
          if (shr > 0.0f && (dx * dx + dy * dy + dz * dz) <= (hr * hr)) {
            const float abs_dx = fabsf(batch->state.pos_x[d_idx] - batch->state.pos_x[a_idx]);
            if (best_victim < 0 || abs_dx < best_abs_dx ||
                (abs_dx == best_abs_dx && defender < best_victim)) {
              best_victim = defender;
              best_abs_dx = abs_dx;
              best_hit_group = hit_group;
              best_rehit_frames = rehit_frames;
            }
            found_grab_contact = 1u;
          }
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
  const MslCommonParams* c = msl_common_params();
  if (c == NULL) {
    return;
  }
  const int num_players = (int)batch->config.num_players;

  // Slippi post-frame `state_flags` includes fp+0x221C bits at byte index 3.
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD = 0x04 };

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
  uint16_t pre_combat_attack_id[MSL_MAX_PLAYERS] = {0};

  // Collision attack-id snapshot:
  // - ftColl_80076444 / ftColl_800763C0 consume the attack id attached to the current collision
  //   pass, before later same-frame ProcessHit/ChangeMotionState effects can rewrite fp->x2068.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076444,ftColl_800763C0}
  // refs/melee/src/melee/ft/ft_0881.c::ft_800890D0
  for (int p = 0; p < num_players; p++) {
    const size_t idx = msl_idx_player(bi, p);
    pre_combat_attack_id[p] = batch->state.attack_id[idx];
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
      if (msl_action_owns_respawn_collision_skip(batch->state.action_id[d_idx])) {
        // See combat_select_catch_hits_one_mutating(): Rebirth/RebirthWait own x2219_b1, so the
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
              // See the p0-side note above: hitbox-vs-hitbox clank uses live HitCapsule geometry,
              // then writes victim rings; replay-reconstructed BODY rings are not a safe prefilter.
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

              if (!combat_hitbox_hitbox_overlap_lbColl_80007AFC(batch, hb0_i, hb1_i)) {
                continue;
              }
              // Decomp clank confirmation requires reciprocal x3CC comparisons.
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_8007699C
              const int raw0 = (int)d0;
              const int raw1 = (int)d1;
              if (!(((raw0 - clank_damage_diff_threshold) < raw1) &&
                    ((raw1 - clank_damage_diff_threshold) < raw0))) {
                continue;
              }

              did_clank = 1u;
              // ftColl_8007699C registers the clank victim into every active HitCapsule sharing
              // the clanking hit_group (`x4`), via inlineA0/inlineA1 and lbColl_80008688. BODY
              // admission later consults lbColl_8000ACFC, so the whole same-group cluster is
              // suppressed for the opponent on this collision pass, not only the exact pair that
              // overlapped in lbColl_80007AFC.
              // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007699C,inlineA0,inlineA1}
              // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
              combat_clank_candidate_skip_same_hit_group_all(batch, bi, p0, p1, hb0,
                                                             clank_candidate_skip_hb);
              combat_clank_candidate_skip_same_hit_group_all(batch, bi, p1, p0, hb1,
                                                             clank_candidate_skip_hb);
              combat_clank_skip_same_hit_group(batch, bi, p0, p1, hb0, clank_skip_hb);
              combat_clank_skip_same_hit_group(batch, bi, p1, p0, hb1, clank_skip_hb);
              // Electric-vs-electric clank SFX lane consumes HSD_Randi(3) to pick one of three
              // entries in ftColl_803C0C4C.
              // refs/melee/src/melee/ft/ftcoll.c::ftColl_800784B4
              // refs/melee/src/sysdolphin/baselib/random.c::HSD_Randi
              if (e0 == (uint8_t)MSL_HIT_ELEMENT_ELECTRIC &&
                  e1 == (uint8_t)MSL_HIT_ELEMENT_ELECTRIC) {
                (void)combat_rng_consume_randi_site(batch, bi,
                                                    MSL_RNG_SITE_FTCOLL_ELECTRIC_CLANK_SFX, 3);
              }

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
              break;
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

      if (batch->state.is_teams[bi]) {
        if (batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
          continue;
        }
      }

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
      // suppress ShieldDesc envelope expansion lanes on that entry snapshot.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_80093694,ftCo_8009388C,ftCo_80093A50,ftCo_80092450}
      const uint8_t guard_reflect_entry_no_submotion =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           batch->state.guard_reflect_timer_x14[d_idx] != 0u)
              ? 1u
              : 0u;
      const uint8_t shield_active = (shr > 0.0f) ? 1u : 0u;
      const uint8_t shield_desc_envelope_ready = !guard_reflect_entry_no_submotion;
      // ShieldDesc sweep extent is owned by the live HitCapsule x58->x4C segment. Do not widen
      // final-x14/no-submotion GuardReflect fighter-vs-fighter shield admission without an
      // enable-edge capsule or explicit teacher-forced accepted shield-contact provenance: the
      // broad bridge over-admits near-rim shine BODY rows as GuardSetOff.
      // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80093BC0
      const uint8_t shield_extent_bridge_active = 0u;
      const uint8_t guard_reflect_reflectdesc_only =
          combat_guard_reflect_no_submotion_reflectdesc_only_lane(batch, d_idx);

      // Combat collision consumes world-space hitbox/hurtcap primitives derived from:
      // - pose matrices driven by fp->cur_anim_frame (prio 1, ftAnim_8006EBA4), and
      // - post-Phys fighter translation (prio 4), applied to the model at prio 6/9 before
      //   the prio 13 fighter-vs-fighter collision pass.
      //
      // Decomp-backed ordering summary: docs/DECOMP_PROC_ORDER.md ("Implications for sim step order").
      // In particular, collision uses post-integration translation; do not shift primitives by
      // (prev_pos - pos) here.

      // Shield precedence (non-inert): if a hitbox intersects the defender shield bubble and
      // `element != HitElement_Inert`, resolve the shield hit (HP depletion, GuardSetOff, hitlag)
      // and do not apply BODY selection for this attacker→defender pair this frame.
      //
      // Decomp pointer (GALE01): refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70 uses
      // `lbColl_80007BCC(..., &this_fp->shield_hit, ...)` as the overlap test and splits on
      // `hit->element`:
      // - non-inert calls `ftColl_80076CBC(...)` (shield hit handling),
      // - inert sets `victim_fp->x221C_b5 = true` (detection hitbox touching shield bubble) and
      //   does NOT enter the normal shield-hit effects path.
      uint8_t did_hit = 0;
      if (shield_active) {
        // Decomp (GALE01): shield collision consumes the staled HitCapsule.damage lane.
        // - Collision writes staled float damage via ft_80089228 when building HitCapsule.
        // - ftColl_80076CBC then derives max int damage with getEnvDmg(hit0->damage).
        // refs/melee/src/melee/ft/ft_0881.c::ft_80089228
        // refs/melee/build/GALE01/asm/melee/ft/ftcoll.s::ftColl_8007ABD0
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
        const uint16_t shield_move_id = staling_move_id_from_state(batch, a_idx);
        const float shield_stale_mult = staling_multiplier_for_move(batch, a_idx, shield_move_id);

        // Decomp (GALE01): shield collision accumulates max int damage for hitlag as:
        // - attacker: `fp0->dmg.x1924 = max(fp0->dmg.x1924, getEnvDmg(hit0->damage))`
        // - defender: `fp1->x19A4 = max(fp1->x19A4, getEnvDmg(hit0->damage))`
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
        //
        // Fighter_ProcessHit then computes hitlag from these max int damage values.
        // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
        //
        // Our shield pass is simplified (one GuardSetOff entry per pair per frame), but we still
        // match the decomp "hitlag uses max int damage over shield overlaps" rule by:
        // - selecting the first eligible shield overlap for shield HP / GuardSetOff, and
        // - computing hitlag using the max int damage over all eligible shield overlaps.
        int max_int_dmg = 0;
        int sel_int_dmg = 0;
        int8_t sel_shield_dmg_s8 = 0;
        uint8_t sel_element = 0u;
        uint8_t sel_hit_group = 0;
        uint8_t sel_rehit_frames = 0;

        for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
          const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
          if (!batch->state.hitbox_enabled[hb_i]) {
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
          // Teacher-forced accepted ShieldDesc lane. A value of 2 is derived only when the replay
          // proves the full shield-hit admission result at t+1 (GuardSetOff plus hitlag), not just
          // the geometric bubble overlap. That proof includes the hidden lbColl_8000ACFC
          // victims_1 decision which is otherwise approximated by the dense group hitlist seed, so
          // it may override stale group suppression for this reseeded frame.
          //
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
          // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000ACFC,lbColl_80008688}
          const uint8_t shield_seed_kind =
              batch->state
                  .combat_shield_contact_hb_kind[idx_hitbox_victim(bi, attacker, hb_id, defender)];
          uint8_t allows =
              hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid);
          if (!allows && shield_seed_kind == 2u) {
            allows = 1u;
          }
          if (!allows) {
            continue;
          }

          // ftColl_80078C70 processes shield and BODY for each HitCapsule before advancing to the
          // next HitCapsule. In the expired-x14 GuardReflect no-submotion handoff, a lower-index
          // BODY hit can therefore commit before a later shield-overlap candidate. Keep this
          // scoped to the local GuardOn-hurtcap fallback owner; the broader shield pass remains
          // pair-wide until the full HitCapsule ordering model is closed.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
          // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
          //   ftCo_GuardReflect_Anim,ftCo_80093BC0}
          if (shield_seed_kind == 0u &&
              combat_guardreflect_expired_x14_earlier_body_hitcapsule_precedes_shield(
                  batch, c, bi, attacker, defender, hb_id, defender_iid, shx, shy, shz, shr,
                  shield_desc_envelope_ready, shield_extent_bridge_active,
                  guard_reflect_reflectdesc_only, clank_skip_hb)) {
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
          uint8_t overlaps_shield = 0u;
          if (shield_seed_kind == 2u) {
            overlaps_shield = 1u;
          } else if (!guard_reflect_reflectdesc_only) {
            overlaps_shield = combat_shield_overlap_ftcoll_80007bcc(
                batch, bi, attacker, defender, hb_id, hx, hy, hz, hr, shx, shy, shz, shr,
                /*shield_desc_radius=*/1.0f, batch->state.fighter_scale_y[d_idx],
                shield_desc_envelope_ready, shield_extent_bridge_active, NULL);
          }
          if (!overlaps_shield) {
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
                d_idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
            batch->state.state_flags[d_flags_i] |=
                (uint8_t)MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD;

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

          float hdmg = batch->state.hitbox_damage[hb_i];
          if (shield_stale_mult != 1.0f) {
            hdmg *= shield_stale_mult;
          }
          if (!(hdmg > 0.0f)) {
            continue;
          }

          // Decomp (GALE01) uses getEnvDmg(hit0->damage) to compute the int damage used for shield
          // interactions and hitlag inputs.
          // refs/melee/src/melee/ft/ftcoll.c::getEnvDmg and ftColl_80076CBC
          const int int_dmg = combat_get_env_dmg(hdmg);
          if (int_dmg > max_int_dmg) {
            max_int_dmg = int_dmg;
          }

          if (sel_int_dmg == 0) {
            sel_int_dmg = int_dmg;
            sel_shield_dmg_s8 = batch->state.hitbox_shield_damage[hb_i];
            sel_element = element;
            sel_hit_group = hit_group;
            sel_rehit_frames = hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
          }
        }

        if (sel_int_dmg > 0) {
          const uint8_t seeded_x19a4 = batch->state.combat_shield_hit_int_damage[d_idx];
          if (seeded_x19a4 != 0u) {
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
          }
          int tmp_dmg = sel_int_dmg + (int)sel_shield_dmg_s8;
          const uint8_t seeded_x19a0 = batch->state.combat_shield_damage_taken[d_idx];
          if (seeded_x19a0 != 0u) {
            // Teacher-forced shield HP accumulator:
            // - ftColl_80076CBC accumulates `fp->x19A0_shieldDamageTaken` separately from x19A4.
            // - Fighter_ProcessHit later consumes x19A0 for shield HP depletion.
            // Use the explicit hidden x19A0 lane only for replay-proven accepted shield contacts;
            // normal rollouts leave it zero and consume the selected runtime contact.
            // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
            // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
            tmp_dmg = (int)seeded_x19a0;
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
          combat_mutations_pass1_future_apply_shield_hit(batch, a_idx, d_idx, max_int_dmg, tmp_dmg,
                                                         a_motion_id, sel_element);

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
          hitlist_register_fighter_group(batch, bi, attacker, sel_hit_group, defender,
                                         defender_iid_post, (int)MSL_LBCOLL_INSERT_FT_SHIELD,
                                         sel_rehit_frames);

          did_hit = 1;
        }
      }

      if (did_hit) {
        continue;
      }

      uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      const MslHurtCap* body_fallback_caps = NULL;
      uint16_t body_fallback_count_u16 = 0u;
      uint8_t use_guardreflect_body_fallback_caps = 0u;
      if (hurtcap_count == 0 &&
          combat_guardreflect_expired_x14_body_fallback_applies(batch, d_idx)) {
        if (hurtcaps_get(batch->state.char_id[d_idx], &body_fallback_caps,
                         &body_fallback_count_u16) == 0 &&
            body_fallback_caps != NULL && body_fallback_count_u16 != 0u) {
          use_guardreflect_body_fallback_caps = 1u;
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

      // BODY contacts (pass 1): deterministic "first overlap wins" selection in (hitbox_id,
      // hurtcap_id) order.
      //
      // Note (approximation): GALE01 maintains additional per-hitbox bookkeeping and also tracks
      // `fp->dmg.int_value` as a max of getEnvDmg(damage) across eligible contacts during the
      // collision pass. We intentionally do NOT attempt to mirror the max-accumulation behavior
      // until we model more of collision ordering/priority/clank semantics, because max-accumulation
      // in this simplified pass can bias one-step timing when multiple hitboxes overlap.
      // refs/melee/src/melee/ft/ftcoll.c::inlineA0/inlineA1
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES && !did_hit; hb_id++) {
        const size_t hb_i = idx_hitbox(bi, attacker, hb_id);
        if (!batch->state.hitbox_enabled[hb_i]) {
          continue;
        }
        if (combat_defer_late_slot_same_frame_speciallw_entry_hit(batch, a_idx, d_idx, attacker,
                                                                  defender)) {
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
        const uint8_t attackairb_stale_owner_candidate =
            combat_attackairb_stale_owner_continuation_candidate(
                batch, a_idx, d_idx, hdmg,
                combat_calc_hitlag_frames(c, int_dmg, a_motion_id, 1.0f)) &&
                    batch->state.instance_hit_by[d_idx] != attacker_iid
                ? 1u
                : 0u;
        const uint8_t allows_v1 =
            hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid);
        const uint8_t dense_seed_suppresses_body = combat_enable_edge_dense_seed_suppresses_body(
            batch, bi, attacker, hb_id, defender, defender_iid,
            combat_calc_hitlag_frames(c, int_dmg, a_motion_id, 1.0f));
        const uint8_t attackairb_dense_seed_suppresses_full_body =
            combat_attackairb_dense_seed_suppresses_full_body(batch, bi, attacker, hb_id, defender,
                                                              defender_iid);
        const uint8_t rehit_frames =
            hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);

        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
          float ax = 0.0f, ay = 0.0f, az = 0.0f;
          float bx = 0.0f, by = 0.0f, bz = 0.0f;
          float cr = 0.0f;
          if (use_guardreflect_body_fallback_caps) {
            if (!combat_guardreflect_body_hurtcap_world(batch, d_idx, &body_fallback_caps[cap_id],
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
              bz, &lbcoll_overlap_amount, &lbcoll_overlap_evaluated);
          // Decomp BODY narrowphase owner:
          // - ftColl_80078C70 calls lbColl_8000805C for fighter BODY admission.
          // - lbColl_8000805C forwards to lbColl_80006E58, whose matrix-derived scalar is the
          //   accept/reject predicate and writes HitCapsule.coll_distance.
          // - When extracted pose data provides the hurt bone matrix, run that owner directly; the
          //   simple world sphere/capsule test is only a missing-data fallback, not a prefilter.
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
          // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
          uint8_t overlaps = lbcoll_overlap_evaluated
                                 ? lbcoll_overlap_valid
                                 : combat_sphere_capsule_intersects(hx, hy, hz, hr, ax, ay, az, bx,
                                                                    by, bz, cr, NULL);
          if (!overlaps && !lbcoll_overlap_evaluated) {
            if (combat_body_overlap_lbColl_80006E58_subset_allows(batch, hb_i, d_idx)) {
              overlaps = combat_body_overlap_lbColl_80006E58_scaffold(
                  batch, bi, attacker, hb_id, hx, hy, hz, hr, ax, ay, az, bx, by, bz, cr,
                  batch->state.fighter_scale_y[d_idx]);
              if (overlaps) {
                lbcoll_overlap_valid = combat_body_overlap_lbColl_80006E58_matrix_radius(
                    batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az,
                    bx, by, bz, &lbcoll_overlap_amount, &lbcoll_overlap_evaluated);
              }
            }
          }
          if (attackairb_stale_owner_candidate) {
            overlaps = combat_attackairb_continuation_body_overlap_exact(
                batch, bi, attacker, hb_id, defender, (int)cap_id, hx, hy, hz, hr, ax, ay, az, bx,
                by, bz, &attackairb_overlap_amount);
          }
          if (!overlaps) {
            continue;
          }
          if (combat_attackairlw_invincible_contact_rejects_body_hitlag(batch, a_idx, d_idx)) {
            continue;
          }
          if (!allows_v1 && !attackairb_stale_owner_candidate) {
            continue;
          }
          if (!combat_shine_start_damageair_entry_pose_allows_body_contact(
                  batch, a_idx, d_idx, cap_id, hx, hy, hz, hr)) {
            continue;
          }
          if (!combat_attackairb_enable_edge_model_scale_allows_body_contact(
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
          if (!attackairb_stale_owner_candidate && !defender_no_damage &&
              batch->state.hitstun[d_idx] == 0u &&
              ((!batch->state.on_ground[d_idx] && batch->state.hitbox_enable_edge[hb_i]) ||
               guard_shield_poke_phantom_boundary) &&
              lbcoll_overlap_valid && lbcoll_overlap_amount > 0.0f &&
              (lbcoll_overlap_amount <= c->phantom_overlap_max_x7a8 ||
               guard_shield_poke_phantom_boundary)) {
            // General fighter BODY phantom-hit lane:
            // - lbColl_8000805C writes HitCapsule.coll_distance from lbColl_80006E58.
            // - ftColl_80076ED8 routes 0 < coll_distance < p_ftCommonData->x7A8 through
            //   checkTipLog/inlineB1 instead of the percent/KB damage-state path.
            // - Apply the matrix-radius helper on enable-edge capsules, where ftColl_8007AD18 has
            //   just initialized x58=x4C for the live HitCapsule. Sustained capsules remain on the
            //   damage path until the exact lbColl scalar is ported for all edge/non-edge cases.
            // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
            // refs/melee/src/melee/ft/ftcoll.c::{checkTipLog,inlineB1,ftColl_80076ED8,ftColl_8007AD18}
            if (!hitlist_allows_fighter_v2(batch, bi, attacker, hb_id, defender, defender_iid)) {
              continue;
            }
            combat_mutations_pass1_future_apply_body_phantom_hit(
                batch, a_idx, d_idx, attacker, hdmg, batch->state.hitbox_element[hb_i]);
            hitlist_register_fighter_group_v2(batch, bi, attacker, hit_group, defender,
                                              defender_iid, (int)MSL_LBCOLL_INSERT_FT_BODY, 0u);
            did_hit = 1;
            break;
          }
          if (attackairb_stale_owner_candidate && !defender_no_damage &&
              attackairb_overlap_amount > 0.0f &&
              attackairb_overlap_amount <= c->phantom_overlap_max_x7a8) {
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
            did_hit = 1;
            break;
          }
          if (dense_seed_suppresses_body || attackairb_dense_seed_suppresses_full_body) {
            continue;
          }
          // Combat Mutations Pass 1 (BODY-only).
          if (defender_no_damage) {
            combat_mutations_pass1_future_apply_body_hit_invincible(batch, a_idx, hb_i,
                                                                    a_motion_id);
          } else {
            if (use_guardreflect_body_fallback_caps) {
              batch->state.hurtcap_height[cap_i] = body_fallback_caps[cap_id].height;
            }
            combat_mutations_pass1_future_apply_body_hit(batch, a_idx, d_idx, attacker, defender,
                                                         hb_i, cap_i, int_dmg, a_motion_id,
                                                         pre_combat_attack_id[attacker]);
          }
          // Hitlist register: decomp hitlists store a victim pointer inside HitCapsule
          // (HitVictim.victim), so the victim identity is stable across the defender's damage-state
          // entry and other motion-state changes.
          // refs/melee/src/melee/lb/lbcollision.c::lbColl_80008688
          //
          // Our hitlist uses `instance_id` as a proxy identity key. BODY hits can enter a damage
          // motion state within this step, which can bump `instance_id` via ft_800895E0 on the
          // decomp-shaped ChangeMotionState path (msl_anim_timebase_enter()).
          // refs/melee/build/GALE01/asm/melee/ft/ft_0892.s::ft_800895E0
          //
          // Register using the post-mutation instance_id so that the seeded identity key at t+1
          // matches teacher-forced reseed (ref post-frame) and we don't spuriously treat the same
          // victim as "new" on the next step.
          const uint16_t defender_iid_post = batch->state.instance_id[d_idx];
          // Decomp insertion type on BODY hit path: ftColl_80076ED8 calls inlineB0(..., type=0, cb=lbColl_80008688).
          // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076ED8
          hitlist_register_fighter_group(batch, bi, attacker, hit_group, defender,
                                         defender_iid_post, (int)MSL_LBCOLL_INSERT_FT_BODY,
                                         rehit_frames);

          did_hit = 1;

          break;
        }
      }
    }
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
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
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
      if (msl_action_owns_respawn_collision_skip(batch->state.action_id[d_idx])) {
        // Debug BODY selection mirrors the runtime x2219_b1 collision skip for Rebirth/RebirthWait.
        continue;
      }
      const uint16_t defender_iid = batch->state.instance_id[d_idx];

      if (batch->state.is_teams[bi]) {
        if (batch->state.team_id[a_idx] == batch->state.team_id[d_idx]) {
          continue;
        }
      }

      uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      const MslHurtCap* body_fallback_caps = NULL;
      uint16_t body_fallback_count_u16 = 0u;
      uint8_t use_guardreflect_body_fallback_caps = 0u;
      if (hurtcap_count == 0 &&
          combat_guardreflect_expired_x14_body_fallback_applies(batch, d_idx)) {
        if (hurtcaps_get(batch->state.char_id[d_idx], &body_fallback_caps,
                         &body_fallback_count_u16) == 0 &&
            body_fallback_caps != NULL && body_fallback_count_u16 != 0u) {
          use_guardreflect_body_fallback_caps = 1u;
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
        if (combat_defer_late_slot_same_frame_speciallw_entry_hit(batch, a_idx, d_idx, attacker,
                                                                  defender)) {
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

        for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
          const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
          float ax = 0.0f, ay = 0.0f, az = 0.0f;
          float bx = 0.0f, by = 0.0f, bz = 0.0f;
          float cr = 0.0f;
          if (use_guardreflect_body_fallback_caps) {
            if (!combat_guardreflect_body_hurtcap_world(batch, d_idx, &body_fallback_caps[cap_id],
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
              bz, &lbcoll_overlap_amount, &lbcoll_overlap_evaluated);
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
                  by, bz, &lbcoll_overlap_amount, &lbcoll_overlap_evaluated);
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
          if (!overlaps) {
            continue;
          }
          if (!combat_shine_start_damageair_entry_pose_allows_body_contact(
                  batch, a_idx, d_idx, cap_id, hx, hy, hz, hr)) {
            continue;
          }
          if (!combat_attackairb_enable_edge_model_scale_allows_body_contact(
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
    batch->state.phantom_damage_pending_x1898[idx] = 0.0f;
    batch->state.phantom_damage_timer_x189c[idx] = 0u;
    batch->state.phantom_damage_source_port[idx] = 0xFFu;
    return;
  }
  uint16_t timer = batch->state.phantom_damage_timer_x189c[idx];
  if (timer == 0u) {
    batch->state.phantom_damage_pending_x1898[idx] = 0.0f;
    batch->state.phantom_damage_source_port[idx] = 0xFFu;
    return;
  }
  timer--;
  batch->state.phantom_damage_timer_x189c[idx] = timer;
  if (timer != 0u) {
    return;
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

  batch->state.phantom_damage_pending_x1898[idx] = 0.0f;
  batch->state.phantom_damage_timer_x189c[idx] = 0u;
  batch->state.phantom_damage_source_port[idx] = 0xFFu;
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
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };
  enum { MSL_STATE_FLAGS_221C_INDEX = 3 };
  enum { MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD = 0x04 };

  const int num_players = (int)batch->config.num_players;
  for (int bi = 0; bi < batch->batch_size; bi++) {
    for (int p = 0; p < num_players; p++) {
      const size_t idx = msl_idx_player(bi, p);
      combat_processhit_apply_expired_phantom_damage(batch, bi, p, idx);
      const size_t flags_i = idx * MSL_STATE_FLAGS_STRIDE + (size_t)MSL_STATE_FLAGS_221C_INDEX;
      batch->state.state_flags[flags_i] &=
          (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD;
      // fp->dmg.x1838_percentTemp is a per-frame accumulator consumed/reset by Fighter_ProcessHit.
      // We don't simulate the full Fighter_ProcessHit pipeline; clear it at the start of each frame
      // to ensure deterministic intra-frame accumulation during items_update/combat_resolve.
      batch->state.percent_temp[idx] = 0.0f;
    }
  }
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
    }
  }
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
  enum { MSL_STATE_FLAGS_STRIDE = MSL_STATE_FLAGS_BYTES };

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
      const uint8_t teams_friendly =
          (batch->state.is_teams[bi] && batch->state.team_id[a_idx] == batch->state.team_id[d_idx])
              ? 1u
              : 0u;
      const uint8_t attacker_hitlag_started = batch->state.hitlag_started_frame[a_idx] ? 1u : 0u;
      const uint8_t defender_hitlag_started = batch->state.hitlag_started_frame[d_idx] ? 1u : 0u;
      const uint8_t hitlag_gate = attacker_hitlag_started ? 1u : 0u;
      const float shx = batch->state.shield_x[d_idx];
      const float shy = batch->state.shield_y[d_idx];
      const float shz = batch->state.shield_z[d_idx];
      const float shr = batch->state.shield_radius[d_idx];
      const uint8_t guard_reflect_entry_no_submotion =
          (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
           batch->state.action_frame[d_idx] < 0 &&
           batch->state.animation_index[d_idx] == UINT32_MAX &&
           batch->state.guard_reflect_timer_x14[d_idx] != 0u)
              ? 1u
              : 0u;
      const uint8_t shield_active = (shr > 0.0f) ? 1u : 0u;
      // GuardReflect no-submotion entry snapshots (action_frame<0, msid sentinel) carry
      // ambiguous ordering between ftCo_8009388C clear and ftCo_80092450 recreate.
      // Keep shield-active ownership from x221B_b0, but disable ShieldDesc envelope expansion
      // lanes only for that entry frame.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
      //   ftCo_80093694,ftCo_8009388C,ftCo_80093A50,ftCo_80092450}
      const uint8_t shield_desc_envelope_ready = !guard_reflect_entry_no_submotion;
      const uint8_t shield_extent_bridge_active = 0u;
      const uint8_t guard_reflect_reflectdesc_only =
          combat_guard_reflect_no_submotion_reflectdesc_only_lane(batch, d_idx);
      const uint8_t defender_on_ground = batch->state.on_ground[d_idx] ? 1u : 0u;

      uint8_t pair_reason = (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD;
      if (attacker_stock_zero) {
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_ATTACKER_STOCKS_ZERO;
      } else if (defender_stock_zero) {
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_DEFENDER_STOCKS_ZERO;
      } else if (teams_friendly) {
        pair_reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_TEAMS_FRIENDLY;
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
              batch->state
                  .combat_shield_contact_hb_kind[idx_hitbox_victim(bi, attacker, hb_id, defender)];
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
          }
          out->hitlist_allows = allows ? 1u : 0u;
          if (!allows) {
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_HITLIST_CONTAINS;
          }
        }

        if (reason == (uint8_t)MSL_DEBUG_SHIELD_DECISION_ACCEPT_SHIELD) {
          float overlap_margin = 0.0f;
          const uint8_t shield_seed_kind =
              batch->state
                  .combat_shield_contact_hb_kind[idx_hitbox_victim(bi, attacker, hb_id, defender)];
          uint8_t overlaps = 0u;
          if (shield_seed_kind == 1u) {
            reason = (uint8_t)MSL_DEBUG_SHIELD_REJECT_SHIELD_GEOM_NO_OVERLAP;
          } else if (shield_seed_kind == 0u && c != NULL &&
                     combat_guardreflect_expired_x14_earlier_body_hitcapsule_precedes_shield(
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
