#include "combat.h"
#include "ids.h"

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
#include "guard_lifecycle.h"
#include "common_params.h"
#include "damage_terminal_owner.h"
#include "damage_source.h"
#include "ecb_tables.h"
#include "grab_flow.h"
#include "hit_elements.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "hurtcaps_tables.h"
#include "input_axis.h"
#include "item_common_params.h"
#include "item_article_params.h"
#include "laser_params.h"
#include "motion_state_owners.h"
#include "msl_math.h"
#include "mtx34.h"
#include "move_tables.h"
#include "shielddesc_geometry.h"
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
static inline uint8_t combat_defender_hit_status_u8(const MslBatch* batch, size_t d_idx);
static inline uint8_t combat_body_overlap_lbColl_80006E58_matrix_radius(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, int cap_id, float hx,
    float hy, float hz, float hr, float ax, float ay, float az, float bx, float by, float bz,
    uint8_t use_catch_grabbable_pose, float* out_overlap_amount, uint8_t* out_evaluated);
static inline uint8_t combat_side_special_start_passivewalljump_entry_pose_owner(
    const MslBatch* batch, size_t idx, uint8_t char_id, uint16_t action_id);

static inline uint8_t combat_action_is_catch_family(uint16_t action_id) {
  return (action_id >= (uint16_t)MSL_ACT_CATCH && action_id <= (uint16_t)MSL_ACT_CATCH_CUT) ? 1u
                                                                                            : 0u;
}

static inline uint8_t combat_catch_primary_enable_edge_rejects_down_forward(
    const MslBatch* batch, size_t a_idx, size_t d_idx, size_t hb_i, int hb_id) {
  if (batch == NULL || hb_id != 0) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_CATCH ||
      batch->state.hitbox_element[hb_i] != (uint8_t)MSL_HIT_ELEMENT_CATCH ||
      batch->state.hitbox_enable_edge[hb_i] == 0u) {
    return 0u;
  }
  // Catch's first active frame is now expressed through active MSLHITB1-derived HitCapsule state:
  // hb0 is the authored primary/far catch capsule, `hitbox_enable_edge` marks the script
  // create-hitbox edge, and `element=CATCH` keeps this out of ordinary BODY/SHIELD hitboxes. The
  // DownForward victim branch mirrors ftColl_80078A2C's grab eligibility check on the victim's
  // current action, without baking the generated frame number (Fox/Falco Catch create at frame 6)
  // into this gameplay predicate.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078A2C
  // data/scripts/{fox,falco}.bin (MSLFTSC1 Catch create_hitbox hb0)
  // data/hitboxes/{fox,falco}.bin (MSLHITB1 element=CATCH, hitbox order)
  return (batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_DOWN_FOWARD_U ||
          batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_DOWN_FOWARD_D)
             ? 1u
             : 0u;
}

static inline float combat_cross2(float ax, float ay, float bx, float by) {
  return ax * by - ay * bx;
}

static inline uint8_t combat_segment_intersects_2d(float ax0, float ay0, float ax1, float ay1,
                                                   float bx0, float by0, float bx1, float by1) {
  const float rx = ax1 - ax0;
  const float ry = ay1 - ay0;
  const float sx = bx1 - bx0;
  const float sy = by1 - by0;
  const float denom = combat_cross2(rx, ry, sx, sy);
  if (denom == 0.0f) {
    return 0u;
  }

  const float qpx = bx0 - ax0;
  const float qpy = by0 - ay0;
  const float t = combat_cross2(qpx, qpy, sx, sy) / denom;
  const float u = combat_cross2(qpx, qpy, rx, ry) / denom;
  return (uint8_t)(t >= 0.0f && t <= 1.0f && u >= 0.0f && u <= 1.0f);
}

enum { MSL_COMBAT_BODY_DAMAGE_LOG_CAP = MSL_MAX_PLAYERS * MSL_MAX_HITBOXES };

typedef struct MslCombatBodyDamageLogEntry {
  size_t a_idx;
  size_t d_idx;
  size_t hb_i;
  size_t cap_i;
  int attacker;
  int defender;
  uint8_t hit_group;
  uint8_t rehit_frames;
  uint8_t element;
  uint8_t hurt_height;
  uint8_t defender_on_ground;
  uint8_t attached_grabbed_victim;
  uint16_t attacker_motion_id;
  uint16_t defender_motion_id;
  uint16_t attacker_attack_id;
  uint16_t attacker_instance_id;
  uint16_t hitbox_angle;
  uint16_t hitbox_kbg;
  uint16_t hitbox_wsk;
  uint16_t hitbox_bkb;
  int hitcapsule_int_dmg;
  int env_dmg;
} MslCombatBodyDamageLogEntry;

typedef struct MslCombatBodyDamageScratch {
  uint8_t count;
  int max_env_dmg;
  MslCombatBodyDamageLogEntry entries[MSL_COMBAT_BODY_DAMAGE_LOG_CAP];
} MslCombatBodyDamageScratch;

typedef enum MslCombatDamageApplyClass {
  MSL_COMBAT_DAMAGE_REJECTED = 0,
  MSL_COMBAT_DAMAGE_FULL = 1,
  MSL_COMBAT_DAMAGE_PERCENT_ONLY_NO_ENTRY = 2,
  MSL_COMBAT_DAMAGE_ATTACHED_SUPPRESSED = 3,
  MSL_COMBAT_DAMAGE_TOP_OFF_MERGE = 4,
} MslCombatDamageApplyClass;

typedef struct MslCombatDamageProduct {
  uint16_t move_id;
  uint16_t attack_instance;
  float applied_damage;
  int hitcapsule_int_dmg;
  int kb_damage_i;
  int env_dmg;
} MslCombatDamageProduct;

typedef enum MslCombatProcessHitSourceWrite {
  MSL_PROCESS_HIT_SOURCE_WRITE_DIRECT = 0,
  MSL_PROCESS_HIT_SOURCE_WRITE_COMMIT_OWNER = 1,
} MslCombatProcessHitSourceWrite;

typedef enum MslCombatProcessHitlagMode {
  MSL_PROCESS_HITLAG_NONE = 0,
  MSL_PROCESS_HITLAG_ASSIGN_IF_POSITIVE = 1,
  MSL_PROCESS_HITLAG_FLAGS_IF_INCREASED = 2,
} MslCombatProcessHitlagMode;

typedef struct MslCombatProcessHitResolved {
  int bi;
  int attacker;
  int defender;
  size_t a_idx;
  size_t d_idx;
  uint16_t d_motion_id;
  uint16_t d_hl;
  uint16_t d_hl_prev;
  float kb_applied;
  float kb_angle_rad;
  float kb_x;
  float kb_y;
  uint8_t defender_on_ground;
  uint8_t use_grounded_kb;
  uint8_t force_tumble_severity;
  uint8_t grounded_ecb_lock_owner;
  uint8_t clear_x221c_on_damage_entry;
  uint8_t apply_throw_release_di;
  uint8_t apply_guard_reflect_followup;
  uint8_t hurt_height;
  uint16_t damage_state_raw_angle;
  uint16_t instance_hit_by;
  uint8_t last_hit_by;
  MslCombatProcessHitSourceWrite source_write;
  MslCombatProcessHitlagMode hitlag_mode;
  uint8_t hitlag_sets_x221a;
  uint8_t hitlag_allows_sdi;
  uint8_t update_bookkeeping;
  uint16_t stale_move_id;
  uint16_t stale_attack_instance;
  uint16_t combo_attack_id;
} MslCombatProcessHitResolved;

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
    uint32_t seed_in = batch->state.frame_pre_random_seed[bi_u];
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

static inline uint8_t combat_damagefly_terminal_state_blocks_enable_edge_body(
    const MslBatch* batch, size_t hb_i, size_t a_idx, size_t d_idx, const MslHurtCap* cap) {
  return msl_damage_owner_terminal_state_blocks_enable_edge_body(
      batch, hb_i, a_idx, d_idx, cap != NULL ? cap->bone_part_id : 0u, cap != NULL ? 1u : 0u);
}

static inline uint8_t combat_damageflylw_dynamic_high_part_rejects_body_contact(
    const MslBatch* batch, size_t a_idx, size_t d_idx, uint8_t hb_id, const MslHurtCap* cap) {
  return msl_damage_owner_damageflylw_dynamic_high_part_rejects_body(
      batch, a_idx, d_idx, hb_id, cap != NULL ? cap->bone_part_id : 0u, cap != NULL ? 1u : 0u);
}

static inline uint8_t combat_attackairlw_damageflytop_fox_tail_rejects_body_contact(
    const MslBatch* batch, size_t a_idx, size_t d_idx, uint8_t hb_id, const MslHurtCap* cap,
    uint16_t expected_hitlag) {
  return msl_damage_owner_attackairlw_damageflytop_fox_tail_rejects_body(
      batch, a_idx, d_idx, hb_id, cap != NULL ? cap->bone_part_id : 0u, cap != NULL ? 1u : 0u,
      expected_hitlag);
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

static inline void combat_catch_hitbox_model_scale_compensated(const MslBatch* batch, int bi,
                                                               int attacker, float* io_hx,
                                                               float* io_hy, float* io_hz,
                                                               float* io_hr) {
  if (batch == NULL || io_hx == NULL || io_hy == NULL || io_hz == NULL || io_hr == NULL) {
    return;
  }
  const size_t a_idx = msl_idx_player(bi, attacker);
  const MslCharParams* chp = msl_char_params(batch->state.char_id[a_idx]);
  const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                  ? chp->model_scaling
                                  : 1.0f;
  if (!(model_scaling > 1.0f)) {
    return;
  }

  // Catch selection forwards HitCapsule.x58/x4C and HitCapsule.scale to lbColl_80007ECC with
  // this_fp->x34_scale.y as the hit-side scalar. HitCapsule points and radius are produced from the
  // collision skeleton before the catch narrowphase; remove the simulator's generic pose-space
  // model_scaling expansion for enlarged models so the catch bubble uses that source skeleton. Do
  // not use this helper to extend reach for model_scaling < 1; the reviewed generic hitbox path
  // remains the source for those rows until the broader hitbox geometry owner is replaced.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078A2C,ftColl_8007AD18}
  // refs/melee/src/melee/lb/lbcollision.c::lbColl_80007ECC
  // refs/melee/src/melee/lb/lb_00B0.c::lb_8000B1CC
  const float pos_x = batch->state.pos_x[a_idx];
  const float pos_y = batch->state.pos_y[a_idx];
  const float pos_z = batch->state.pos_z[a_idx];
  *io_hx = pos_x + ((*io_hx - pos_x) / model_scaling);
  *io_hy = pos_y + ((*io_hy - pos_y) / model_scaling);
  *io_hz = pos_z + ((*io_hz - pos_z) / model_scaling);
  *io_hr /= model_scaling;
}

static inline uint8_t combat_guard_family_no_submotion_catch_source_msid(const MslBatch* batch,
                                                                         size_t d_idx,
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

static inline uint8_t combat_guard_family_no_submotion_catch_source_applies(const MslBatch* batch,
                                                                            size_t d_idx) {
  return combat_guard_family_no_submotion_catch_source_msid(batch, d_idx, NULL);
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

static inline uint8_t combat_guard_family_catch_hurtcap_world(const MslBatch* batch, size_t d_idx,
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

static inline uint8_t combat_catch_grabbable_dynamic_hurtcap_world(
    const MslBatch* batch, size_t d_idx, uint8_t cap_id, float* out_ax, float* out_ay,
    float* out_az, float* out_bx, float* out_by, float* out_bz, float* out_r) {
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
  (void)move_tables_hurtbox_can_hit_mask_at_frame(
      batch->state.char_id[d_idx], (uint16_t)MSL_SM_GUARD_ON, 0u, cap_count, &can_hit_mask);
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

static inline void combat_clank_register_same_hit_group(MslBatch* batch, int bi, int attacker,
                                                        int defender, int hb_id,
                                                        uint16_t defender_iid) {
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

static inline uint8_t combat_mtx34_inverse_point(const float m[12], float x, float y, float z,
                                                 float* out_x, float* out_y, float* out_z) {
  return (uint8_t)msl_mtx34_inverse_point(m, x, y, z, out_x, out_y, out_z);
}

static inline uint8_t combat_is_damage_or_firefox_launch_victim_action(uint16_t action_id) {
  return msl_damage_owner_is_damage_or_firefox_launch_action(action_id);
}

static inline uint8_t combat_is_damage_air_action(uint16_t action_id) {
  return msl_damage_owner_is_damage_air_action(action_id);
}

static inline uint8_t combat_residual_frame_start_hitcapsule_owner(const MslBatch* batch,
                                                                   size_t idx) {
  if (batch == NULL || batch->state.hitbox_count[idx] == 0u) {
    return 0u;
  }
  if (!combat_is_damage_or_firefox_launch_victim_action(batch->state.action_id[idx])) {
    return 0u;
  }
  if ((batch->state.action_id[idx] == (uint16_t)MSL_ACT_FX_SPECIAL_HI ||
       batch->state.action_id[idx] == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_HI) &&
      (batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD ||
       batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_FX_SPECIAL_HI_HOLD_AIR)) {
    // Firefox/Firebird hold -> launch hitboxes are authored by the launch action after the
    // transition out of Hold/HoldAir, not by a residual frame-start Damage/FlyReflect capsule.
    // Keep the current attack instance in staling so repeated launch contacts use the stale queue
    // state visible on the seed row.
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialHi.c::{ftFx_SpecialHiHold*_Anim,
    //   ftFx_SpecialHi_Enter,ftFx_SpecialAirHi_Enter}
    // refs/melee/src/melee/ft/ftcoll.c::{ftColl_8007ABD0,ftColl_80076ED8}
    return 0u;
  }
  if (combat_is_damage_or_firefox_launch_victim_action(batch->state.prev_action_id[idx]) ||
      batch->state.prev_action_id[idx] == batch->state.action_id[idx]) {
    return 0u;
  }
  return (uint8_t)(batch->state.frame_start_attack_id[idx] != 0u &&
                   batch->state.frame_start_attack_id[idx] != (uint16_t)MSL_FT_MOVE_ID_DEFAULT &&
                   batch->state.frame_start_instance_id[idx] != 0u);
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
  return msl_damage_owner_is_downed_damage_contact_action(action_id);
}

static inline uint16_t combat_down_damage_action_from_source(uint16_t action_id) {
  return msl_damage_owner_down_damage_action_from_source(action_id);
}

static inline uint32_t combat_down_damage_submotion_from_action(uint16_t action_id) {
  return msl_damage_owner_down_damage_submotion_from_action(action_id);
}

static inline uint8_t combat_float_aobj_hurtcap_pose_owner(uint16_t action_id) {
  return msl_motion_state_common_class_has(action_id, MSL_MS_CLASS_LANDING_AIR);
}

static inline uint8_t combat_side_special_start_passivewalljump_entry_pose_owner(
    const MslBatch* batch, size_t idx, uint8_t char_id, uint16_t action_id) {
  if (batch == NULL ||
      (char_id != (uint8_t)MSL_CHAR_ID_FOX && char_id != (uint8_t)MSL_CHAR_ID_FALCO)) {
    return 0u;
  }
  if (action_id != (uint16_t)MSL_ACT_FX_SPECIAL_S_START &&
      action_id != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_START) {
    return 0u;
  }
  if (batch->state.action_frame[idx] > 2) {
    return 0u;
  }
  return (uint8_t)(batch->state.prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP ||
                   batch->state.seed_prev_action_id[idx] == (uint16_t)MSL_ACT_PASSIVE_WALL_JUMP);
}

static inline float combat_root_facing_dir_for_body_hurtcap(const MslBatch* batch, size_t idx) {
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
  return facing_dir;
}

static inline float combat_hurtcap_pose_sample_frame(const MslBatch* batch, size_t idx,
                                                     uint8_t char_id, uint16_t msid,
                                                     uint16_t action_id, float anim_frame_f32,
                                                     uint16_t pose_frame) {
  (void)batch;
  (void)idx;
  (void)char_id;
  (void)msid;
  return combat_float_aobj_hurtcap_pose_owner(action_id) ? anim_frame_f32 : (float)pose_frame;
}

static inline uint8_t combat_body_matrix_positive_pose_reliable(const MslBatch* batch,
                                                                size_t d_idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t char_id = batch->state.char_id[d_idx];
  const uint16_t action_id = batch->state.action_id[d_idx];
  if (msl_motion_state_class_has(char_id, action_id, MSL_MS_CLASS_COMMON_FALL)) {
    if (batch->state.action_frame[d_idx] <= 2) {
      return 1u;
    }
    // After Fall entry, replay-visible state_age is not sufficient to reconstruct the live JObj
    // pose that ftColl_80078C70/lbColl_8000805C consumes. Dolphin collision probes on later Fall
    // frames show `x894_currentAnimFrame` staying at 0 while the live leg JObj matrix changes.
    // Later CommonFall BODY positives are therefore only source-backed when the ordinary
    // world/sphere geometry path already overlaps; the generated state-age matrix may reject a
    // false positive but must not create a matrix-only positive over a world-geometry miss without
    // an explicit live JObj pose lane. The entry step stays eligible because
    // Fighter_ChangeMotionState has just initialized the source JObj chain; after the normal
    // per-frame tick this appears as action_frame 2 in the combat pass, matching DCC's frame-1
    // seeded Fall BODY admission.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    // refs/melee/src/melee/lb/lbcollision.c::{lbColl_8000805C,lbColl_80006E58}
    // reports/triage/grape_item05_ewt1019_dolphin_collision_probe/
    return 0u;
  }
  return 1u;
}

static inline uint8_t combat_attackdash_post_hitbox_collision_pose_owner(
    const MslBatch* batch, int bi, size_t idx, uint16_t action_id, uint8_t char_id,
    float anim_frame_f32, uint16_t attacker_action_id, uint8_t attacker_hitbox_active) {
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

static inline uint8_t combat_guard_tilt_live_body_pose_owner(const MslBatch* batch, size_t d_idx) {
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

static inline uint8_t combat_apply_guard_tilt_live_body_matrix(const MslBatch* batch, size_t d_idx,
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

static inline uint8_t combat_guard_tilt_live_body_z_owner_applies(const MslBatch* batch,
                                                                  size_t d_idx,
                                                                  uint8_t shield_active) {
  if (batch == NULL || !shield_active || !combat_guard_tilt_live_body_pose_owner(batch, d_idx)) {
    return 0u;
  }
  return 1u;
}

static inline uint8_t combat_shield_active_action(uint16_t action_id) {
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

static inline void combat_preserve_guard_x10_for_immediate_setoff(
    MslBatch* batch, size_t d_idx, uint16_t d_motion_id_pre, const MslCommonParams* c,
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

static inline uint8_t combat_body_overlap_lbColl_80006E58_matrix_radius(
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
  const MslCharParams* chp = msl_char_params(char_id);
  const float model_scaling = (chp && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
                                  ? chp->model_scaling
                                  : 1.0f;
  const float scale_y = batch->state.fighter_scale_y[d_idx];
  const float model_scale = scale_y * model_scaling;
  if (!(model_scale > 0.0f)) {
    return 0u;
  }
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
      (!use_catch_grabbable_pose && !attackdash_post_hitbox_pose &&
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
    const float pose_sample_frame = combat_hurtcap_pose_sample_frame(
        batch, d_idx, char_id, msid, action_id, anim_frame_f32, combat_pose_frame);
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
  const float pos_z = batch->state.pos_z[d_idx];

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
                                                           by, bz, 0u, out_overlap_amount, NULL);
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

static inline uint8_t combat_hitcapsule_is_authored_same_group_primary(const MslBatch* batch,
                                                                       int bi, int attacker,
                                                                       int hb_id) {
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
  //   active maximum-damage HitCapsule in that group. Keep that capsule on the full BODY path;
  //   later/equal siblings and lower-damage limb capsules may take the `coll_distance < x7A8`
  //   phantom/tip-log branch.
  // - This predicate is derived from active MSLHITB1 hitbox table fields (`damage`, `hit_group`,
  //   source HitCapsule id/order), rather than an action id / row slice.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076ED8}
  // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
  // data/hitboxes/{fox,falco}.bin (MSLHITB1 damage + hit_group + hitbox id/order)
  return saw_same_group_sibling;
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
    if (!msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker)) {
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

static inline uint8_t combat_attackairn_wait_dense_seed_suppresses_full_body(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, uint16_t defender_iid) {
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

static inline uint8_t combat_seed_hitlist_suppresses_clank_candidate(const MslBatch* batch, int bi,
                                                                     int attacker, int hb_id,
                                                                     int defender) {
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

static inline uint8_t combat_guard_reflect_active_x14_no_guardon_blocks_body(const MslBatch* batch,
                                                                             size_t idx);

static inline uint8_t combat_shield_overlap_ftcoll_80007bcc(
    const MslBatch* batch, int bi, int attacker, int defender, int hb_id, float hx, float hy,
    float hz, float hr, float shx, float shy, float shz, float shr, float shield_desc_radius,
    float shield_owner_scale_y, uint8_t shield_desc_envelope_ready,
    uint8_t shield_extent_bridge_active, float* out_overlap_margin) {
  return msl_shielddesc_fighter_overlap_ftcoll_80007bcc(
      batch, bi, attacker, defender, hb_id, hx, hy, hz, hr, shx, shy, shz, shr, shield_desc_radius,
      shield_owner_scale_y, shield_desc_envelope_ready, shield_extent_bridge_active,
      out_overlap_margin);
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

static inline float combat_latched_lightshield_amount_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0.0f;
  }

  // Decomp: shield-hit HP depletion, GuardSetOff shieldstun/recoil, and attacker shield push read
  // `fp->lightshield_amount`, a latched Guard callback lane. Do not recompute it from current
  // trigger input here: ftCo_800925A4 preserves the previous non-negative squeeze when trigger
  // input is below deadzone, and ftCo_80092F2C consumes that stored value on same-frame shield hits.
  // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_800925A4,ftCo_80092F2C}
  const float light = batch->state.lightshield_amount[idx];
  if (!isfinite(light)) {
    return 0.0f;
  }
  return combat_clamp01(light);
}

static inline void combat_state_flags_set_is_hitlag(MslBatch* batch, size_t idx, uint16_t hitlag) {
  if (batch == NULL) {
    return;
  }
  // Slippi post-frame: `lbz r3,0x221A(REG_PlayerData)  #0x20 = isHitlag`.
  //
  // Decomp-first references (GALE01):
  // - `fp->x221A_b2` is toggled with hitlag start/end:
  //   - set when hitlag is applied (Fighter_ProcessHit_8006D1EC),
  //   - cleared when hitlag reaches 0 (Fighter_8006A1BC).
  // refs/melee/src/melee/ft/fighter.c
  // - Bitfield layout at fp+0x221A is documented in refs/melee/src/melee/ft/types.h.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm

  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221A_INDEX;
  uint8_t f = batch->state.state_flags[flags_i];
  if (hitlag > 0) {
    f |= (uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
  } else {
    f &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221A_IS_HITLAG;
  }
  batch->state.state_flags[flags_i] = f;
}

static inline uint8_t combat_damage_allow_sdi_owner_action(uint16_t action) {
  return msl_damage_owner_allows_sdi_action(action);
}

static inline void combat_damage_allow_sdi_set(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }
  // Source owner: Fighter_ProcessHit starts damage hitlag and owns `allow_sdi`
  // (fp+0x221A:2) for ftCo_Damage_OnEveryHitlag. Keep this separate from the generic
  // hitlag-active bit so shield, clank, deal-hitlag, and item-shield helpers do not gain SDI.
  // refs/melee/src/melee/ft/fighter.c::{Fighter_ProcessHit_8006D1EC,Fighter_8006A1BC}
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_Damage_OnEveryHitlag
  // refs/melee/src/melee/ft/types.h
  batch->state.damage_allow_sdi[idx] = 1u;
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

  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221A_INDEX;
  batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_221A_B3;
}

static inline void combat_state_flags_set_is_hitstun(MslBatch* batch, size_t idx,
                                                     uint16_t hitstun) {
  if (batch == NULL) {
    return;
  }
  // Slippi post-frame: `lbz r3,0x221C(REG_PlayerData)  #0x2 = isHitstun`.
  // refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  //
  // Decomp: `fp->x221C_b6` is set on Damage state entry and cleared when hitstun ends.
  // - set: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008DCE0 (end of function)
  // - clear: refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744

  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
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

  // fp+0x221C bit 0x80 ownership in attached hit windows:
  // - This bit is exposed by Slippi's post-frame byte capture at fp+0x221C.
  //   refs/slippi-ssbm-asm/Recording/SendGamePostFrame.asm
  // - Damage flow consumes fp->x221C_b0 as part of the no-reaction branch gate.
  //   refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::inlineB1
  // - Generic motion-state change clears fp+0x221C lanes owned by transition reset paths.
  //   refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] |= (uint8_t)MSL_STATE_FLAG_221C_B0;
}

static inline void combat_state_flags_clear_x221c_b0(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  // Motion-state reset ownership for fp+0x221C_b0:
  // - Fighter_ChangeMotionState clears fp->x221C_b0 on destination entry.
  // - Damage state entry uses Fighter_ChangeMotionState via ftCo_8008DCE0 / ftCo_8008EC90.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_8008DCE0,ftCo_8008EC90}
  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_B0;
}

static inline void combat_state_flags_clear_guard_reflecting(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  // GuardSetOff destination reset:
  // - shield-hit transition enters GuardSetOff through Fighter_ChangeMotionState in ftCo_80092F2C,
  // - that destination does not keep the live `fp->reflecting` owner from the GuardReflect
  //   descriptor, even when x221C_b1/x221C_b2 timer lanes remain visible on the same row.
  // refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_2218_INDEX;
  batch->state.state_flags[flags_i] &= (uint8_t) ~(uint8_t)MSL_STATE_FLAG_2218_REFLECTING;
}

static inline void combat_state_flags_clear_stale_guard_timer_bits_on_setoff_entry(MslBatch* batch,
                                                                                   size_t idx) {
  if (batch == NULL) {
    return;
  }

  if (batch->state.guard_reflect_timer_x14[idx] != 0u ||
      batch->state.guard_reflect_timer_x18[idx] != 0u) {
    return;
  }

  // GuardSetOff entry through shield contact does not create GuardReflect timer bits. Those bits
  // are written by GuardReflect / powershield entry (`ftCo_8009388C` / `ftCo_80093A50`) and ticked
  // by `ftCo_80093BC0`; when both timers are already expired, a repeated GuardSetOff shield-hit
  // entry must not carry a stale seeded x221C_b1/b2 post-frame lane.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_80092F2C,ftCo_8009388C,ftCo_80093A50,ftCo_80093BC0}
  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
  batch->state.state_flags[flags_i] &=
      (uint8_t) ~(uint8_t)(MSL_STATE_FLAG_221C_B1 | MSL_STATE_FLAG_221C_B2);
}

static inline void combat_state_flags_set_guard_reflect_timer_bits(MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return;
  }

  uint8_t bits = 0u;
  if (batch->state.guard_reflect_timer_x14[idx] != 0u) {
    bits |= (uint8_t)MSL_STATE_FLAG_221C_B1;
  }
  if (batch->state.guard_reflect_timer_x18[idx] != 0u) {
    bits |= (uint8_t)MSL_STATE_FLAG_221C_B2;
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
  const size_t flags_i = idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX;
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
  return msl_guard_reflect_is_frozen_snapshot(batch, idx);
}

static inline uint8_t combat_defer_late_slot_same_frame_speciallw_entry_hit(
    const MslBatch* batch, int bi, size_t a_idx, size_t d_idx, int attacker, int defender,
    int hb_id) {
  if (batch == NULL || attacker <= defender) {
    return 0u;
  }
  (void)bi;
  (void)hb_id;
  const uint16_t action = batch->state.action_id[a_idx];
  if (action != (uint16_t)MSL_ACT_FX_SPECIAL_LW_START &&
      action != (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START) {
    return 0u;
  }
  if (batch->state.prev_action_id[a_idx] == action) {
    return 0u;
  }
  const uint16_t defender_action = batch->state.action_id[d_idx];
  const uint16_t attacker_prev_action = batch->state.prev_action_id[a_idx];
  const uint8_t attacker_squat_family_platform_pass_source =
      (batch->state.frame_start_on_ground[a_idx] != 0u &&
       (attacker_prev_action == (uint16_t)MSL_ACT_SQUAT ||
        attacker_prev_action == (uint16_t)MSL_ACT_SQUAT_WAIT ||
        attacker_prev_action == (uint16_t)MSL_ACT_SQUAT_RV))
          ? 1u
          : 0u;
  if (action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START &&
      attacker_squat_family_platform_pass_source && batch->state.on_ground[d_idx] == 0u &&
      batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] != 0u &&
      (msl_motion_state_common_class_has(defender_action, MSL_MS_CLASS_DAMAGE_FLY_COLL) ||
       msl_motion_state_common_class_has(defender_action, MSL_MS_CLASS_DAMAGE_COMMON_COLL))) {
    // Fighter BODY pair-order + active airborne damage collision owner:
    // - A frame-start grounded Squat-family state can enter grounded Reflector and immediately
    //   platform-pass into SpecialAirLwStart, preserving the ground-start submotion/hitbox while
    //   `ftFx_SpecialLwStart_Pass` explicitly creates the reflect hit.
    // - If that platform-pass creator is a later entity, an earlier active Damage/DamageFly fighter
    //   has already run its collision callback for this frame, so the fresh HitCapsule cannot damage
    //   it until a later pair phase. Ordinary aerial Shine entries stay on the normal BODY path; QGD
    //   and aggregate controls show those hits are real.
    // - Use MSLMSO01 collision-owner classes for the victim family instead of an action-id list.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::{
    //   ftFx_SpecialLwStart_Pass,ftFx_SpecialAirLw_Enter}
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Squat.c::ftCo_Squat_IASA
    // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 DAMAGE_*_COLL classes)
    return 1u;
  }
  if (!batch->state.on_ground[d_idx] || batch->state.hitlag[d_idx] != 0u ||
      batch->state.hitstun[d_idx] != 0u) {
    return 0u;
  }
  if (action == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_LW_START &&
      batch->state.prev_action_id[d_idx] != defender_action &&
      batch->state.action_frame[d_idx] <= 1 &&
      msl_motion_state_common_class_has(defender_action, MSL_MS_CLASS_GROUNDED_ATTACK)) {
    // Fighter BODY pair-order + same-frame grounded attack entry owner:
    // - ftColl_80078C70 walks fighter entity pairs after action/IASA entry. For a later entity
    //   entering aerial SpecialLwStart on the same frame an earlier grounded attack state is entered,
    //   the fresh Shine HitCapsule can miss that earlier fighter's already-processed pair phase.
    // - The defender boundary is table-backed by MSLMSO01's GROUNDED_ATTACK class and the local
    //   entry snapshot (`prev_action_id != action`, action_frame <= 1), not an AttackHi3 row list.
    // - Grounded SpecialLwStart stays on the narrower Turn microphase below; HVG:5200/TCH:3376
    //   prove fresh grounded Shine can still hit pre-turn earlier-slot defenders.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80078C70
    // refs/melee/src/melee/ft/chara/ftFox/ftFx_SpecialLw.c::ftFx_SpecialAirLw_Enter
    // data/motion_state/owners/{fox,falco}.bin (MSLMSO01 class GROUNDED_ATTACK)
    return 1u;
  }
  if (action != (uint16_t)MSL_ACT_FX_SPECIAL_LW_START) {
    return 0u;
  }
  if (defender_action != (uint16_t)MSL_ACT_TURN || batch->state.turn_has_turned[d_idx] == 0u) {
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

static inline uint8_t combat_guard_reflect_active_x14_reflectdesc_blocks_hitshield(
    const MslBatch* batch, size_t idx, float overlap_margin) {
  if (batch == NULL) {
    return 0u;
  }
  // Carried GuardReflect rows with an active x14 reflect timer are still in the ReflectDesc-owned
  // window when fighter-vs-fighter collision runs. Do not let the simulator's reconstructed
  // ShieldDesc overlap hand off to GuardSetOff until ftCo_80093BC0 has consumed x14. Same-frame
  // locomotion / landing powershield entries have x14_seed==0 and are not this carried phase:
  // ftCo_80093A50 creates ShieldDesc before collision in that entry frame. While x14 is still
  // active, only accept reconstructed ShieldDesc hits whose sphere/segment overlap penetrates by
  // at least the source ShieldDesc size lane (`lbColl_80007BCC` arg4=1 scaled by the shield JObj);
  // shallower contacts are the reduced-proxy grazes that the live ReflectDesc owner would keep out
  // of GuardSetOff.
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{
  //   ftCo_80093A50,ftCo_80093BC0,ftCo_GuardReflect_Anim}
  // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80007BCC,lbColl_80006E58}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  const MslCharParams* chp = msl_char_params(batch->state.char_id[idx]);
  const float model_scale =
      (chp != NULL && isfinite(chp->model_scaling) && chp->model_scaling > 0.0f)
          ? chp->model_scaling
          : 1.0f;
  if (model_scale > 1.0f) {
    // Enlarged collision models carry the ShieldDesc JObj scale in the lbColl_80007BCC matrix
    // path; the current sphere proxy already admits the retained Falco landing/Wait front-door
    // contacts there. The grazing false positives guarded below are the non-expanded-model carried
    // ReflectDesc rows.
    // refs/melee/src/melee/ft/fighter.c::Fighter_UpdateModelScale
    return 0u;
  }
  const float shield_desc_size_world = batch->state.fighter_scale_y[idx] * model_scale;
  return (batch->state.action_id[idx] == (uint16_t)MSL_ACT_GUARD_REFLECT &&
          batch->state.guard_reflect_timer_x14_seed[idx] != 0u &&
          batch->state.guard_reflect_timer_x14[idx] != 0u &&
          overlap_margin < shield_desc_size_world && batch->state.hitlag[idx] == 0u &&
          batch->state.hitstun[idx] == 0u)
             ? 1u
             : 0u;
}

static inline uint8_t combat_guard_reflect_final_x14_live_x18_blocks_body(const MslBatch* batch,
                                                                          size_t idx) {
  return msl_guard_reflect_final_x14_live_x18_blocks_body(batch, idx);
}

static inline uint8_t combat_guard_reflect_active_x14_no_guardon_blocks_body(const MslBatch* batch,
                                                                             size_t idx) {
  return msl_guard_reflect_active_x14_no_guardon_blocks_body(batch, idx);
}

static inline uint8_t combat_guard_reflect_active_x14_rejects_strong_attackairb_shield(
    const MslBatch* batch, size_t a_idx, size_t d_idx, size_t hb_i) {
  if (batch == NULL) {
    return 0u;
  }
  if (!combat_guard_reflect_active_x14_no_guardon_blocks_body(batch, d_idx)) {
    return 0u;
  }
  if (batch->state.action_id[a_idx] != (uint16_t)MSL_ACT_ATTACK_AIR_B) {
    return 0u;
  }
  // Direct active-x14 powershield rows keep the ReflectDesc owner for the strong early Back-Air
  // torso/leg capsules; the lower-damage tail capsule can still reach the live ShieldDesc handoff.
  // Express the boundary through the extracted AttackAirB damage split (15-damage strong capsules
  // vs 9-damage tail/late capsules) rather than an EWT row or hitbox slot id.
  // data/moves/{fox,falco}.json::moves.ftCo_SM_AttackAirB.events.create_hitbox
  // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::{ftCo_80093A50,ftCo_80093BC0}
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80078C70,ftColl_80076CBC}
  return (batch->state.hitbox_damage[hb_i] > 9.5f) ? 1u : 0u;
}

static inline uint8_t combat_shield_damage_powershield_suppressed_idx(const MslBatch* batch,
                                                                      size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  if ((flags_221c & (uint8_t)MSL_STATE_FLAG_221C_B2) != 0u) {
    // Shield-hit damage/recoil consumes the live powershield flag directly. This is intentionally
    // broader than item reflect ownership: x14 owns ReflectDesc/item reflection, while
    // ftColl_80076CBC and ftCo_80092F2C gate shieldDamageTaken and GuardSetOff pushback on
    // fp->x221C_b2.
    // refs/melee/src/melee/ft/ftcoll.c::ftColl_80076CBC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
    return 1u;
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
    if ((flags_221c & (uint8_t)MSL_STATE_FLAG_221C_B2) != 0u) {
      return powershield_active;
    }
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
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX];
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
  return (flags_221c & (uint8_t)MSL_STATE_FLAG_221C_B2) ? 1u : 0u;
}

static inline uint8_t combat_guard_setoff_recoil_x221c_b2_idx(const MslBatch* batch, size_t idx) {
  if (batch == NULL) {
    return 0u;
  }
  const uint8_t flags_221c =
      batch->state.state_flags[idx * MSL_STATE_FLAGS_BYTES + (size_t)MSL_STATE_FLAGS_221C_INDEX];
  if ((flags_221c & (uint8_t)MSL_STATE_FLAG_221C_B2) != 0u) {
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
  if (hurtcap_count == 0u && combat_guardreflect_expired_x14_body_fallback_applies(batch, d_idx)) {
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

    for (uint8_t cap_id = 0; cap_id < hurtcap_count; cap_id++) {
      const size_t cap_i = idx_hurtcap(bi, defender, (int)cap_id);
      float ax = 0.0f, ay = 0.0f, az = 0.0f;
      float bx = 0.0f, by = 0.0f, bz = 0.0f;
      float cr = 0.0f;
      if (use_fallback_caps) {
        if (!combat_guardreflect_body_hurtcap_world(batch, d_idx, &fallback_caps[cap_id], cap_id,
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
      if (overlaps && !combat_attackairb_enable_edge_model_scale_allows_body_contact(
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
  // Decomp proc ordering: prio 1 Anim runs before prio 3 input callbacks.
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

static inline uint8_t combat_illusion_owner_is_end_phase(uint16_t action_id_u16) {
  return (action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_S_END ||
          action_id_u16 == (uint16_t)MSL_ACT_FX_SPECIAL_AIR_S_END)
             ? 1u
             : 0u;
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
  return combat_illusion_owner_is_end_phase(batch->state.action_id[attacker_idx]) ? item_angle
                                                                                  : UINT16_MAX;
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

static inline uint8_t combat_damageflyroll_rng_subset_allows_pre_action(const MslBatch* batch,
                                                                        size_t d_idx,
                                                                        uint16_t action_id) {
  return msl_damage_owner_damageflyroll_pre_action_allows_gate(batch, d_idx, action_id);
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
                                                     uint8_t force_tumble_severity,
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
  if (msl_damage_owner_damageflyroll_jumpaerial_attackairb_carry(batch, d_idx)) {
    combat_rng_consume_step_site(batch, bi,
                                 MSL_RNG_SITE_DAMAGE_FLY_ROLL_PRE_GATE_JUMPAERIAL_ATTACKAIRB_CARRY);
  }
}

static inline void combat_damage_enter_state(const MslCommonParams* c, MslBatch* batch, int bi,
                                             size_t d_idx, uint8_t defender_on_ground_before,
                                             uint8_t defender_on_ground_after, uint8_t hurt_height,
                                             float kb_applied, float kb_angle_rad,
                                             uint16_t raw_kb_angle) {
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

  const uint16_t hs = combat_damage_hitstun_from_kb(c, ev->kb_applied);
  batch->state.hitstun[ev->d_idx] = hs;
  combat_state_flags_set_is_hitstun(batch, ev->d_idx, hs);
  if (ev->clear_x221c_on_damage_entry != 0u) {
    combat_state_flags_clear_x221c_b0(batch, ev->d_idx);
  }
  combat_damage_mark_entry_time_since_hit(batch, ev->d_idx);

  const uint8_t defender_on_ground_after = batch->state.on_ground[ev->d_idx] ? 1u : 0u;
  combat_damage_enter_state(c, batch, ev->bi, ev->d_idx, ev->defender_on_ground,
                            defender_on_ground_after, ev->hurt_height, ev->kb_applied,
                            ev->kb_angle_rad, ev->damage_state_raw_angle);
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
}

static inline void combat_processhit_clear_phantom_damage(MslBatch* batch, size_t idx) {
  batch->state.phantom_damage_pending_x1898[idx] = 0.0f;
  batch->state.phantom_damage_timer_x189c[idx] = 0u;
  batch->state.phantom_damage_source_port[idx] = 0xFFu;
}

static inline uint8_t combat_body_damage_producer_build(const MslBatch* batch, size_t a_idx,
                                                        size_t hb_i, int int_dmg,
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

  const float stale_mult = (exclude_attacker_attack_instance != 0u)
                               ? staling_multiplier_for_move_excluding_instance(
                                     batch, a_idx, out->move_id, attacker_attack_instance)
                               : staling_multiplier_for_move(batch, a_idx, out->move_id);
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
  // - Same-instance low-throw laser contacts can later stale-queue the same attack instance; they
  //   must not retroactively stale the already-created throw capsule.
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
  combat_processhit_clear_phantom_damage(batch, d_idx);

  // ftColl_80076ED8 writeback shape:
  // - compute already-staled HitCapsule damage and getEnvDmg,
  // - immediately accumulate victim x1838_percentTemp / x183C_applied,
  // - immediately register stale/combo side effects through ftColl_8007891C,
  // - append one DmgLogEntry for later ftColl_8007A06C best-KB selection.
  // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,inlineB2,ftColl_8007891C,ftColl_8007A06C}
  MslCombatDamageProduct prod;
  if (!combat_body_damage_producer_build(batch, a_idx, hb_i, int_dmg, attacker_attack_id,
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
    const MslCharParams* d_ch = msl_char_params(batch->state.char_id[e->d_idx]);
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
    if (d_hl_increased) {
      batch->state.hitlag[d_idx] = d_hl;
      combat_state_flags_set_is_hitlag(batch, d_idx, d_hl);
      combat_state_flags_set_x221a_b3(batch, d_idx);
      combat_state_flags_set_x221c_b0(batch, d_idx);
    }
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
    ev.d_motion_id = e->defender_motion_id;
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
  ev.d_motion_id = e->defender_motion_id;
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
  ev.grounded_ecb_lock_owner = (!combat_is_downed_damage_contact_action(pre_damage_action) &&
                                combat_shine_start_grounded_ledge_ecb_lock_owner(
                                    batch, d_idx, batch->state.action_id[a_idx]))
                                   ? 1u
                                   : 0u;
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
  if (d_hl != d_hl_prev) {
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
  const MslCharParams* d_ch = msl_char_params(batch->state.char_id[d_idx]);
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
    ev.d_hl = d_hl;
    ev.d_hl_prev = d_hl_prev;
    ev.kb_applied = 0.0f;
    ev.instance_hit_by = item_instance_id;
    ev.last_hit_by = combat_source_port0_for_attacker(batch, a_idx, attacker);
    ev.source_write = MSL_PROCESS_HIT_SOURCE_WRITE_COMMIT_OWNER;
    ev.update_bookkeeping = 1u;
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
      !(lp != NULL && item_state == (uint8_t)1u &&
        batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_THROW_HI)) {
    // Retained item-position facing owner scope:
    // ftColl_8007A06C's item branch uses item position for stationary/slow item damage and item
    // velocity once the item reaches ItemCommonData->x78. Callers only set this flag for live item
    // BODY collision records whose item pos/velocity are the collision owner's values; same-frame
    // synthetic spawn bridges and ThrowHi's explicit thrower-facing state1 owner keep their
    // narrower source-owner lanes.
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

  if (same_frame_throw_laser_item_damage_topoff && d_hl <= d_hl_prev &&
      hs <= batch->state.hitstun[d_idx]) {
    item_damage_class = MSL_COMBAT_DAMAGE_TOP_OFF_MERGE;
  }
  if (item_damage_class == MSL_COMBAT_DAMAGE_TOP_OFF_MERGE) {
    // Same-frame throw-side state1 laser top-off:
    // - Multiple throw-side state1 articles can overlap the same already-damaged victim in one
    //   item pass. In vanilla, their HitCapsule damage contributes to the same
    //   Fighter_ProcessHit percent-temp frame, but the first accepted hit owns the Damage entry and
    //   x2088 motion-state instance.
    // - The later article can still contribute to the ftCo_Damage_CalcVel merge. The simulator
    //   processes items serially, so without this boundary it sees x18AC reset by the first entry
    //   and incorrectly treats the later top-off as a fresh replace + Damage entry.
    // - ThrowB and ThrowHi share the same ftFx_Throw_Anim throw_flags_b0 item producer and state1
    //   BODY callback; ThrowLw attached-victim pulses use the separate grabbed-victim owner above.
    // refs/melee/src/melee/ft/fighter.c::Fighter_ProcessHit_8006D1EC
    // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::{ftCo_Damage_CalcVel,ftCo_8008DCE0}
    // refs/melee/src/melee/it/items/itfoxlaser.c::{it_8029C6CC,it_8029C4D4}
    combat_damage_merge_vel_after_window(batch, d_idx, kb_x, kb_y);
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
  ev.d_hl = d_hl;
  ev.d_hl_prev = d_hl_prev;
  ev.kb_applied = kb_applied;
  ev.kb_angle_rad = kb_angle_rad;
  ev.kb_x = kb_x;
  ev.kb_y = kb_y;
  ev.defender_on_ground = defender_on_ground;
  ev.use_grounded_kb = 1u;
  ev.grounded_ecb_lock_owner = (!combat_is_downed_damage_contact_action(d_motion_id) &&
                                combat_shine_start_grounded_ledge_ecb_lock_owner(
                                    batch, d_idx, batch->state.action_id[a_idx]))
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
      c, &d_ch_throw, d_motion_id, percent_pre, dmg_temp, damage_product.kb_damage_i, p->kbg,
      p->wsk, p->bkb, coll_kb_mul, batch->state.dmg_x2225_b7[d_idx],
      batch->state.dmg_x2224_b2[d_idx], batch->state.kb_smashcharge_active[d_idx]);
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
  // The caller passes the source-shaped per-frame accumulator for accepted shield contacts in this
  // (attacker, defender) pair. GuardSetOff entry remains one transition, matching the later
  // Fighter_ProcessHit consume point for the accumulated x19A0/x19A4 lanes.
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
      (max_int_dmg > 255) ? 255u : (uint8_t)max_int_dmg;
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
        combat_catch_hitbox_model_scale_compensated(batch, bi, attacker, &hx, &hy, &hz, &hr);

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
        uint8_t shield_contact_groups[MSL_MAX_HITBOXES] = {0};
        uint8_t shield_contact_rehit_frames[MSL_MAX_HITBOXES] = {0};
        uint8_t shield_contact_group_seen[8] = {0};

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
          if (!overlaps_shield) {
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
          int contact_shield_damage = int_dmg + (int)batch->state.hitbox_shield_damage[hb_i];
          if (contact_shield_damage < 0) {
            contact_shield_damage = 0;
          }
          shield_damage_taken_sum += contact_shield_damage;
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
          int tmp_dmg = shield_damage_taken_sum;
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

      uint8_t hurtcap_count = batch->state.hurtcap_count[d_idx];
      const MslHurtCap* defender_caps = NULL;
      uint16_t defender_cap_count_u16 = 0u;
      (void)hurtcaps_get(batch->state.char_id[d_idx], &defender_caps, &defender_cap_count_u16);
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
        const uint8_t attackairb_stale_owner_candidate =
            combat_attackairb_stale_owner_continuation_candidate(batch, a_idx, d_idx, hdmg,
                                                                 expected_body_hitlag) &&
                    batch->state.instance_hit_by[d_idx] != attacker_iid
                ? 1u
                : 0u;
        const uint8_t allows_v1 =
            hitlist_allows_fighter(batch, bi, attacker, hb_id, defender, defender_iid);
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
            (!allows_v1 && batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_N &&
             batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
             batch->state.hitlag[d_idx] == 0u && batch->state.hitstun[d_idx] != 0u &&
             msl_damage_source_victim_port_matches_attacker(batch, d_idx, a_idx, attacker) &&
             !move_tables_attackair_second_create_hitbox_phase(batch->state.char_id[a_idx],
                                                               batch->state.action_id[a_idx],
                                                               batch->state.anim_frame_f32[a_idx]))
                ? 1u
                : 0u;
        const uint8_t dense_seed_suppresses_body = combat_enable_edge_dense_seed_suppresses_body(
            batch, bi, attacker, hb_id, defender, defender_iid, expected_body_hitlag);
        const uint8_t attackairb_dense_seed_suppresses_full_body =
            combat_attackairb_dense_seed_suppresses_full_body(batch, bi, attacker, hb_id, defender,
                                                              defender_iid, expected_body_hitlag);
        const uint8_t attackairn_wait_dense_seed_suppresses_full_body =
            combat_attackairn_wait_dense_seed_suppresses_full_body(batch, bi, attacker, hb_id,
                                                                   defender, defender_iid);
        const uint8_t rehit_frames =
            hitlist_rehit_frames_from_u16_7(batch->state.hitbox_u16_7[hb_i]);
        const uint8_t v1_group_seen_this_pass =
            v1_group_registered_this_pass[attacker][defender][hit_group];
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
          if (lbcoll_overlap_valid && !baseline_overlaps &&
              !combat_body_matrix_positive_pose_reliable(batch, d_idx)) {
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
          if (!overlaps && !use_guardreflect_body_fallback_caps &&
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
                              use_guardreflect_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id])) {
            overlaps = 0u;
          }
          if (overlaps && combat_attackairlw_damageflytop_fox_tail_rejects_body_contact(
                              batch, a_idx, d_idx, hb_id,
                              use_guardreflect_body_fallback_caps || defender_caps == NULL ||
                                      cap_id >= defender_cap_count_u16
                                  ? NULL
                                  : &defender_caps[cap_id],
                              expected_body_hitlag)) {
            overlaps = 0u;
          }
          if (overlaps && combat_damagefly_terminal_state_blocks_enable_edge_body(
                              batch, hb_i, a_idx, d_idx,
                              use_guardreflect_body_fallback_caps || defender_caps == NULL ||
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
          if (v1_group_seen_this_pass) {
            // ftColl_80076ED8 / ftColl_80076CBC immediately register victims_1 across all active
            // HitCapsules with the same hit_group through inlineB0 / ftColl_80076808. The retained
            // AttackAirB stale-owner bridge may bypass a stale seeded `lbColl_8000ACFC` miss, but it
            // must not bypass a same-pass source registration from an earlier HitCapsule.
            // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076ED8,ftColl_80076CBC}
            // refs/melee/src/melee/lb/lbcollision.c::{lbColl_80008688,lbColl_8000ACFC}
            continue;
          }
          const int16_t attackair_second_create_frame =
              move_tables_attackair_second_create_hitbox_frame(batch->state.char_id[a_idx],
                                                               batch->state.action_id[a_idx]);
          const size_t hb_seed_valid_i =
              ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)attacker) * (size_t)MSL_MAX_HITBOXES +
              (size_t)hb_id;
          if (batch->state.action_id[a_idx] == (uint16_t)MSL_ACT_ATTACK_AIR_N && hb_id > 0 &&
              batch->state.combat_hitlist_hb_valid[hb_seed_valid_i] == 0u &&
              attackair_second_create_frame >= 0 &&
              batch->state.anim_frame_f32[a_idx] >= (float)attackair_second_create_frame &&
              batch->state.anim_frame_f32[a_idx] < (float)(attackair_second_create_frame + 3) &&
              batch->state.action_id[d_idx] == (uint16_t)MSL_ACT_DAMAGE_FLY_TOP &&
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
          if (!allows_v1 && !attackairb_stale_owner_candidate &&
              !attackairn_first_window_ignores_dense_damageflytop_latch) {
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
          const uint8_t same_group_primary_full_body =
              (batch->state.hitbox_enable_edge[hb_i] == 0u &&
               combat_hitcapsule_is_authored_same_group_primary(batch, bi, attacker, hb_id))
                  ? 1u
                  : 0u;
          const uint8_t fighter_phantom_tiplog_range =
              (!attackairb_stale_owner_candidate && lbcoll_overlap_valid &&
               lbcoll_overlap_amount > 0.0f &&
               ((!shield_active && !same_group_primary_full_body &&
                 lbcoll_overlap_amount <= c->phantom_overlap_max_x7a8) ||
                guard_shield_poke_phantom_boundary))
                  ? 1u
                  : 0u;
          const uint8_t attackairb_phantom_tiplog_range =
              (attackairb_stale_owner_candidate && attackairb_overlap_amount > 0.0f &&
               attackairb_overlap_amount <= c->phantom_overlap_max_x7a8)
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
              batch->state.hitstun[d_idx] == 0u && fighter_phantom_tiplog_range) {
            // General fighter BODY phantom-hit lane:
            // - lbColl_8000805C writes HitCapsule.coll_distance from lbColl_80006E58.
            // - ftColl_80076ED8 routes 0 < coll_distance < p_ftCommonData->x7A8 through
            //   checkTipLog/inlineB1 instead of the percent/KB damage-state path.
            // - Apply this for source-evaluated BODY matrix overlaps below x7A8 when the defender is
            //   not shield-active. Active same-group strict damage leaders remain on the full BODY
            //   source path; lower-damage limb capsules and equal-damage groups keep the retained
            //   tip-log reconstruction. Shield rows have a separate Guard/ShieldDesc owner and only
            //   enter this lane through the guarded shield-poke predicate above.
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
              attackairn_wait_dense_seed_suppresses_full_body) {
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
    combat_processhit_clear_phantom_damage(batch, idx);
    return;
  }
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
          (uint8_t) ~(uint8_t)MSL_STATE_FLAG_221C_DETECT_HITBOX_TOUCHING_SHIELD;
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
