#pragma once

#include "combat.h"
#include "char_registry.h"
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
#include "falcon_specials.h"
#include "ftcommon_ecb.h"
#include "guard_lifecycle.h"
#include "grab_attachment.h"
#include "common_params.h"
#include "damage_terminal_owner.h"
#include "damage_source.h"
#include "ecb_tables.h"
#include "escapeair_collision_owner.h"
#include "grab_flow.h"
#include "hit_elements.h"
#include "hitboxes.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "hurtcaps_tables.h"
#include "input_axis.h"
#include "items.h"
#include "item_common_params.h"
#include "item_article_params.h"
#include "laser_params.h"
#include "lbcollision_constants.h"
#include "motion_state_owners.h"
#include "msl_math.h"
#include "mtx34.h"
#include "move_tables.h"
#include "mpcoll_ecb_points.h"
#include "shielddesc_geometry.h"
#include "shield_tilt_table.h"
#include "sheik_specials.h"
#include "special_msids.h"
#include "stage_collision.h"
#include "staling.h"
#include "state_flags.h"

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

uint16_t combat_calc_hitlag_frames(const MslCommonParams* c, int dmg, uint16_t motion_id,
                                   float element_mul);
float combat_hitlag_mul_from_element(const MslCommonParams* c, uint8_t element);

enum {
  // The generated hurtcap substrate currently exposes the extracted Fighter_Part id, height,
  // grabbability, and capsule offsets, but not semantic body-region labels. Fox/Falco cap12's
  // source record is anchored to FtPart 18, the dynamic tail chain consumed by lb_8000B1CC.
  // data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18.
  MSL_HURTCAP_FOX_FALCO_TAIL_PART_ID = 18,
  // data/scripts/{fox,falco}.bin (MSLFTSC1 ftCo_SM_AttackLw3 first create_hitbox frame).
  MSL_ATTACK_LW3_FIRST_CREATE_FRAME = 7,
};

enum {
  MSL_ATTACKAIRB_STRONG_BODY_ROOT_HITBOX = 0u,
  MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX = 1u,
  MSL_HURTCAP_DAMAGEFLYTOP_UPPER_BODY_SLOT = 1u,
  MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT = 2u,
  MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT = 0u,
  MSL_HURTCAP_DAMAGEFLYTOP_LEG_LOW_SLOT = 10u,
  MSL_HURTCAP_DAMAGEFLYTOP_LEG_HIGH_SLOT = 11u,
  MSL_HURTCAP_DAMAGEFLYTOP_XROTN_SLOT = 12u,
};

static inline uint8_t combat_hurtcap_is_extracted_fox_falco_tail_part(const MslHurtCap* cap) {
  return (uint8_t)(cap != NULL &&
                   cap->bone_part_id == (uint16_t)MSL_HURTCAP_FOX_FALCO_TAIL_PART_ID);
}

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

enum {
  MSL_FTCOLL_NORMAL_DAMAGE_EFFECT_RANDI_CONSUMES = 4,
  MSL_DAMAGEFLYROLL_KNEEBEND_ATTACKS3_FIGHTER_8006CDA4_PRIMARY_CONSUMES = 5,
  MSL_DAMAGEFLYROLL_JUMP_ATTACKAIRN_FIGHTER_8006CDA4_CONSUMES = 7,
  MSL_DAMAGEFLYROLL_SUSTAINED_JUMP_LATE_ATTACKAIRN_FTCOLL_EXTRA_NORMAL_ENTRIES = 2,
  MSL_DAMAGEFLYROLL_SUSTAINED_JUMP_LATE_ATTACKAIRN_FIGHTER_8006CDA4_PRIMARY_CONSUMES = 4,
  MSL_DAMAGEFLYROLL_SPECAIRHI_ATTACKLW4_FIGHTER_8006CDA4_CONSUMES = 2,
  MSL_DAMAGEFLYROLL_CATCH_ATTACKAIRF_FIGHTER_8006CDA4_CONSUMES = 3,
  MSL_DAMAGEFLYROLL_KNEEBEND_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_CONSUMES = 3,
  MSL_DAMAGEFLYROLL_ATTACKHI4_WEAK_ATTACKAIRB_FTCOLL_DAMAGE_EFFECT_CONSUMES =
      MSL_FTCOLL_NORMAL_DAMAGE_EFFECT_RANDI_CONSUMES,
  MSL_DAMAGEFLYROLL_ATTACKHI4_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_PRIMARY_CONSUMES = 5,
  MSL_DAMAGEFLYROLL_SPECIALLW_END_WEAK_ATTACKAIRB_FTCOLL_DAMAGE_EFFECT_CONSUMES =
      MSL_FTCOLL_NORMAL_DAMAGE_EFFECT_RANDI_CONSUMES,
  MSL_DAMAGEFLYROLL_SPECIALLW_END_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_PRIMARY_CONSUMES = 1,
  MSL_DAMAGEFLYROLL_SPECIALLW_END_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_SECONDARY_CONSUMES = 1,
  MSL_DAMAGEFLYROLL_SPECIALLW_END_WEAK_ATTACKAIRB_FIGHTER_8006CDA4_TERTIARY_CONSUMES = 1,
  MSL_DAMAGEFLYROLL_SPECIALHIFALL_STRONG_ATTACKAIRB_CREATE_FIGHTER_8006CDA4_CONSUMES = 1,
  MSL_DAMAGEFLYROLL_SPECIALHIFALL_STRONG_ATTACKAIRB_CONTINUING_REFLECT_FIGHTER_8006CDA4_CONSUMES =
      2,
};

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
  uint16_t source_motion_id;
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
  size_t source_hb_i;
  size_t source_cap_i;
  uint16_t d_motion_id;
  uint16_t source_motion_id;
  uint16_t source_item_type;
  uint8_t source_item_state;
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
  uint8_t source_hb_valid;
  uint8_t source_cap_valid;
  uint8_t source_is_item_hit;
  uint8_t source_item_owns_motion_clear;
  int source_hitcapsule_int_dmg;
  uint16_t source_hitbox_angle;
  uint16_t source_hitbox_kbg;
  uint16_t source_hitbox_bkb;
  uint16_t stale_move_id;
  uint16_t stale_attack_instance;
  uint16_t combo_attack_id;
} MslCombatProcessHitResolved;

uint8_t sphere_sphere_intersects(float ax, float ay, float az, float ar, float bx, float by,
                                 float bz, float br);
uint8_t combat_shine_start_damageair_entry_pose_bridge_applies(const MslBatch* batch, size_t a_idx,
                                                               size_t d_idx);
uint8_t combat_shine_start_grounded_ledge_ecb_lock_owner(const MslBatch* batch, size_t d_idx,
                                                         size_t a_idx, uint16_t attacker_action);
uint8_t combat_attackairlw_invincible_contact_rejects_body_hitlag(const MslBatch* batch,
                                                                  size_t a_idx, size_t d_idx);
uint8_t combat_damagefly_terminal_state_blocks_enable_edge_body(const MslBatch* batch, size_t hb_i,
                                                                size_t a_idx, size_t d_idx,
                                                                const MslHurtCap* cap);
uint8_t combat_damageflylw_dynamic_high_part_rejects_body_contact(const MslBatch* batch,
                                                                  size_t a_idx, size_t d_idx,
                                                                  uint8_t hb_id,
                                                                  const MslHurtCap* cap);
uint8_t combat_attackairlw_damageflytop_fox_tail_rejects_body_contact(const MslBatch* batch,
                                                                      size_t a_idx, size_t d_idx,
                                                                      uint8_t hb_id,
                                                                      const MslHurtCap* cap,
                                                                      uint16_t expected_hitlag);
uint8_t combat_attackairlw_hitbox_payload_is_authored_multihit_upper(uint8_t hb_id, float damage,
                                                                     uint16_t angle, uint16_t kbg,
                                                                     uint16_t wsk, uint16_t bkb);
uint8_t combat_attackairlw_hitbox_payload_is_authored_multihit_lower_sibling(
    float damage, uint16_t angle, uint16_t kbg, uint16_t wsk, uint16_t bkb);
uint8_t combat_attackairlw_hitbox_payload_is_authored_late_meteor(uint8_t hb_id, float damage,
                                                                  uint16_t angle, uint16_t kbg,
                                                                  uint16_t wsk, uint16_t bkb);
uint8_t combat_attackairlw_hitbox_payload_is_authored_strong_meteor(uint8_t hb_id, float damage,
                                                                    uint16_t angle, uint16_t kbg,
                                                                    uint16_t wsk, uint16_t bkb);
uint8_t combat_attackairlw_strong_grounded_high_cap_rejects_lower_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, int defender, size_t a_idx,
    size_t d_idx, const MslHurtCap* cap, const MslHurtCap* defender_caps,
    uint16_t defender_cap_count_u16, float hx, float hy, float hz, float hr);
uint8_t combat_attackairlw_strong_attacklw4_high_cap_sibling_rejects_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, int defender, size_t a_idx,
    size_t d_idx, const MslHurtCap* cap);
uint8_t combat_attackairlw_late_high_cap_sibling_rejects_body_contact(const MslBatch* batch, int bi,
                                                                      int attacker, uint8_t hb_id,
                                                                      int defender,
                                                                      const MslHurtCap* cap);
uint8_t combat_attackairhi_hitbox_payload_is_authored_finisher_hb2(uint8_t hb_id, float damage,
                                                                   uint16_t angle, uint16_t kbg,
                                                                   uint16_t wsk, uint16_t bkb);
uint8_t combat_attackairlw_attackdash_tail_allow_interrupt_rejects_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, size_t a_idx, size_t d_idx,
    const MslHurtCap* cap);
uint8_t combat_attackairlw_strong_dair_group_has_non_tail_body_overlap(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, int defender,
    const MslHurtCap* defender_caps, uint16_t defender_cap_count_u16);
uint8_t combat_attackairlw_down_forward_tail_rejects_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, int defender, size_t a_idx,
    size_t d_idx, const MslHurtCap* cap, const MslHurtCap* defender_caps,
    uint16_t defender_cap_count_u16, float hx, float hy, float hz, float hr);
uint8_t combat_specialhi_launch_hitbox_payload_is_authored(const MslBatch* batch, size_t hb_i,
                                                           uint8_t hb_id);
uint8_t combat_action_is_generated_specialhi_launch(const MslBatch* batch, size_t idx);
uint8_t combat_action_is_aerial_reflector_turn_source(uint8_t char_id, uint16_t action);
uint8_t combat_action_is_aerial_reflector_loop_pre_turn_source(uint8_t char_id, uint16_t action);
uint8_t combat_pstadium_specialhi_launch_air_reflector_pre_turn_rejects_body(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, size_t a_idx, size_t d_idx);
uint8_t combat_pstadium_x44_gap_allows_body_contact(const MslBatch* batch, int bi, int attacker,
                                                    uint8_t hb_id, size_t a_idx, size_t d_idx,
                                                    uint8_t cap_id, const MslHurtCap* cap,
                                                    float lbcoll_overlap_amount,
                                                    uint8_t lbcoll_overlap_evaluated,
                                                    uint8_t shield_active);
uint8_t combat_attackairhi_attackdash_tail_allow_interrupt_rejects_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, size_t a_idx, size_t d_idx,
    const MslHurtCap* cap);
uint8_t combat_marth_aerial_static_spacie_tail_rejects_body_contact(const MslBatch* batch,
                                                                    size_t hb_i, size_t a_idx,
                                                                    size_t d_idx, uint8_t hb_id,
                                                                    const MslHurtCap* cap);
uint8_t combat_marth_attackairn_spacie_guard_static_pose_rejects_body_contact(
    const MslBatch* batch, size_t hb_i, size_t a_idx, size_t d_idx, uint8_t hb_id, uint8_t cap_id,
    const MslHurtCap* cap);
uint8_t combat_spacie_bair_static_extremity_rejects_body_contact(const MslBatch* batch, size_t hb_i,
                                                                 size_t a_idx, size_t d_idx,
                                                                 uint8_t hb_id,
                                                                 const MslHurtCap* cap);
uint8_t combat_attackhi4_damageflytop_xrotn_rejects_body_contact(const MslBatch* batch, size_t hb_i,
                                                                 size_t a_idx, size_t d_idx,
                                                                 uint8_t hb_id, uint8_t cap_id);
uint8_t combat_shine_start_damageair_entry_pose_allows_body_contact(const MslBatch* batch,
                                                                    size_t a_idx, size_t d_idx,
                                                                    uint8_t cap_id, float hx,
                                                                    float hy, float hz, float hr);
uint8_t combat_attackairb_jump_low_body_source_owns_model_scale_bypass(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, size_t a_idx,
    size_t d_idx, uint8_t cap_id, float hx, float hy, float hz, float hr);
uint8_t combat_attackairb_model_scale_cancellation_payload(uint8_t hb_id, float damage,
                                                           uint16_t angle, uint16_t kbg,
                                                           uint16_t wsk, uint16_t bkb);
uint8_t combat_attackairb_enable_edge_model_scale_allows_body_contact(
    const MslBatch* batch, size_t a_idx, size_t hb_i, size_t d_idx, float hx, float hy, float hz,
    float hr, float ax, float ay, float az, float bx, float by, float bz, float cr,
    uint16_t expected_hitlag);
uint8_t combat_body_overlap_lbColl_80006E58_subset_allows(const MslBatch* batch, size_t hb_i,
                                                          size_t d_idx);
uint8_t combat_body_overlap_lbColl_80006E58_scaffold(const MslBatch* batch, int bi, int attacker,
                                                     int hb_id, float hx, float hy, float hz,
                                                     float hr, float ax, float ay, float az,
                                                     float bx, float by, float bz, float cr,
                                                     float defender_scale_y);
uint8_t combat_catch_overlap_lbColl_80007ECC(const MslBatch* batch, int bi, int attacker, int hb_id,
                                             float hx, float hy, float hz, float hr, float ax,
                                             float ay, float az, float bx, float by, float bz,
                                             float cr);
void combat_catch_hitbox_model_scale_compensated(const MslBatch* batch, int bi, int attacker,
                                                 int hb_id, float* io_hx, float* io_hy,
                                                 float* io_hz, float* io_hr);
uint8_t combat_guard_family_no_submotion_catch_source_msid(const MslBatch* batch, size_t d_idx,
                                                           uint16_t* out_msid);
uint8_t combat_guard_family_no_submotion_catch_source_applies(const MslBatch* batch, size_t d_idx);
uint8_t combat_guardreflect_expired_x14_body_fallback_applies(const MslBatch* batch, size_t d_idx);
uint8_t combat_guard_family_no_submotion_body_source_msid(const MslBatch* batch, size_t d_idx,
                                                          uint16_t* out_msid);
uint8_t combat_guardon_no_submotion_body_overrides_existing_hurtcaps(const MslBatch* batch,
                                                                     size_t d_idx);
uint8_t combat_common_entry_carry_action_to_msid(uint16_t action_id, uint16_t* out_msid);
uint8_t combat_guardon_live_pose_source(const MslBatch* batch, size_t d_idx, uint16_t* out_action,
                                        uint32_t* out_anim);
uint8_t combat_guard_family_no_submotion_body_source_pose(const MslBatch* batch, size_t d_idx,
                                                          uint16_t* out_msid, float* out_frame);
uint8_t combat_guard_family_catch_hurtcap_world(const MslBatch* batch, size_t d_idx,
                                                const MslHurtCap* cap, float* out_ax, float* out_ay,
                                                float* out_az, float* out_bx, float* out_by,
                                                float* out_bz, float* out_r);
uint8_t combat_catch_grabbable_dynamic_hurtcap_world(const MslBatch* batch, size_t d_idx,
                                                     uint8_t cap_id, float* out_ax, float* out_ay,
                                                     float* out_az, float* out_bx, float* out_by,
                                                     float* out_bz, float* out_r);
uint8_t combat_guard_family_body_hurtcap_world(const MslBatch* batch, size_t d_idx,
                                               const MslHurtCap* cap, uint8_t cap_id,
                                               uint16_t cap_count, float* out_ax, float* out_ay,
                                               float* out_az, float* out_bx, float* out_by,
                                               float* out_bz, float* out_r);
uint8_t combat_hitbox_hitbox_overlap_lbColl_80007AFC(const MslBatch* batch, size_t hb0_i,
                                                     size_t hb1_i);
uint8_t combat_hitbox_targets_fighter_ground_state(uint16_t hitbox_flags,
                                                   uint8_t defender_on_ground);
void combat_clank_skip_same_hit_group(
    const MslBatch* batch, int bi, int attacker, int defender, int hb_id,
    uint8_t clank_skip_hb[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_MAX_HITBOXES]);
void combat_clank_candidate_skip_same_hit_group_all(
    const MslBatch* batch, int bi, int attacker, int defender, int hb_id,
    uint8_t clank_candidate_skip_hb[MSL_MAX_PLAYERS][MSL_MAX_PLAYERS][MSL_MAX_HITBOXES]);
void combat_clank_register_same_hit_group(MslBatch* batch, int bi, int attacker, int defender,
                                          int hb_id, uint16_t defender_iid);
uint8_t combat_mtx34_inverse_point(const float m[12], float x, float y, float z, float* out_x,
                                   float* out_y, float* out_z);
uint8_t combat_is_damage_or_firefox_launch_victim_action(uint8_t char_id, uint16_t action_id);
uint8_t combat_is_damage_air_action(uint16_t action_id);
uint8_t combat_residual_frame_start_hitcapsule_owner(const MslBatch* batch, size_t idx);
uint8_t combat_hitlist_victim_pointer_may_change(uint8_t stocks, uint16_t action_id);
uint8_t combat_is_downed_damage_contact_action(uint16_t action_id);
uint16_t combat_down_damage_action_from_source(uint16_t action_id);
uint32_t combat_down_damage_submotion_from_action(uint16_t action_id);
uint8_t combat_float_aobj_hurtcap_pose_owner(uint16_t action_id);
uint8_t combat_run_entry_collision_pose_sample_frame(const MslBatch* batch, size_t idx,
                                                     const MslCharParams* ch, uint16_t action_id,
                                                     float anim_frame_f32, float* out_frame);
uint8_t combat_side_special_start_passivewalljump_entry_pose_owner(const MslBatch* batch,
                                                                   size_t idx, uint8_t char_id,
                                                                   uint16_t action_id);
float combat_root_facing_dir_for_body_hurtcap(const MslBatch* batch, size_t idx);
float combat_hurtcap_pose_sample_frame(const MslBatch* batch, size_t idx, uint8_t char_id,
                                       uint16_t msid, uint16_t action_id, float anim_frame_f32,
                                       uint16_t pose_frame);
uint8_t combat_body_matrix_positive_pose_reliable(const MslBatch* batch, size_t d_idx);
uint8_t combat_attackdash_post_hitbox_collision_pose_owner(const MslBatch* batch, int bi,
                                                           size_t idx, uint16_t action_id,
                                                           uint8_t char_id, float anim_frame_f32,
                                                           uint16_t attacker_action_id,
                                                           uint8_t attacker_hitbox_active);
uint8_t combat_guard_no_tilt_current_pose_gap(const MslBatch* batch, size_t d_idx);
uint8_t combat_guard_tilt_live_body_pose_owner(const MslBatch* batch, size_t d_idx);
uint8_t combat_apply_guard_tilt_live_body_matrix(const MslBatch* batch, size_t d_idx,
                                                 uint8_t char_id, uint16_t part_id, float io_m[12]);
uint8_t combat_guard_tilt_live_body_z_owner_applies(const MslBatch* batch, size_t d_idx,
                                                    uint8_t shield_active);
uint8_t combat_shield_active_action(uint16_t action_id);
uint8_t combat_single_create_grounded_guardreflect_enable_edge_allows_shield(const MslBatch* batch,
                                                                             size_t a_idx,
                                                                             size_t d_idx,
                                                                             size_t hb_i);
void combat_preserve_guard_x10_for_immediate_setoff(MslBatch* batch, size_t d_idx,
                                                    uint16_t d_motion_id_pre,
                                                    const MslCommonParams* c,
                                                    uint8_t fighter_powershield_active);
uint8_t combat_body_overlap_lbColl_80006E58_matrix_radius_impl(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, int cap_id, float hx,
    float hy, float hz, float hr, float ax, float ay, float az, float bx, float by, float bz,
    uint8_t use_catch_grabbable_pose, float* out_overlap_amount, uint8_t* out_evaluated);
uint8_t combat_body_overlap_lbColl_80006E58_matrix_radius(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, int cap_id, float hx,
    float hy, float hz, float hr, float ax, float ay, float az, float bx, float by, float bz,
    uint8_t use_catch_grabbable_pose, float* out_overlap_amount, uint8_t* out_evaluated);
uint8_t combat_attackairb_continuation_body_overlap_exact(const MslBatch* batch, int bi,
                                                          int attacker, int hb_id, int defender,
                                                          int cap_id, float hx, float hy, float hz,
                                                          float hr, float ax, float ay, float az,
                                                          float bx, float by, float bz,
                                                          float* out_overlap_amount);
uint8_t combat_attackairb_stale_owner_continuation_candidate(const MslBatch* batch, size_t a_idx,
                                                             size_t d_idx, float hitbox_damage,
                                                             uint16_t expected_hitlag);
uint8_t combat_hitcapsule_is_authored_same_group_primary(const MslBatch* batch, int bi,
                                                         int attacker, int hb_id);
uint8_t combat_primary_phantom_tiplog_allows_later_same_group_body(const MslBatch* batch,
                                                                   const MslCommonParams* c, int bi,
                                                                   int attacker, int hb_id,
                                                                   int defender, uint8_t hit_group);
uint8_t combat_sheik_chain_same_frontier_later_hitbox_owns_body(
    const MslBatch* batch, size_t a_idx, int bi, int attacker, int hb_id, uint8_t hit_group,
    int defender, int cap_id, uint8_t defender_on_ground, float ax, float ay, float az, float bx,
    float by, float bz, float cr);
uint8_t combat_sheik_chain_activation_edge_same_source_suppresses_body(const MslBatch* batch,
                                                                       size_t a_idx, size_t d_idx,
                                                                       int attacker, int hb_id,
                                                                       int defender);
uint16_t combat_sheik_chain_same_source_terminal_hitstun_horizon(uint16_t hitlag);
uint8_t combat_sheik_chain_terminal_same_source_episode_suppresses_body(const MslBatch* batch,
                                                                        size_t a_idx, size_t d_idx);
uint8_t combat_sheik_chain_same_source_damage_followup_admits_body(
    const MslBatch* batch, const MslCommonParams* c, size_t a_idx, size_t d_idx, int attacker,
    int hb_id, int int_dmg, uint16_t defender_motion_id, uint8_t hitbox_element);
uint8_t combat_sheik_chain_damageflytop_high_horizon_suppresses_body(
    const MslBatch* batch, const MslCommonParams* c, size_t a_idx, size_t d_idx, int attacker,
    int hb_id, int int_dmg, uint16_t defender_motion_id, uint8_t hitbox_element);
uint8_t combat_replay_rollout_advanced_past_reseed(const MslBatch* batch, int bi);
uint8_t combat_enable_edge_dense_seed_suppresses_body(const MslBatch* batch, int bi, int attacker,
                                                      int hb_id, int defender,
                                                      uint16_t defender_iid,
                                                      uint16_t expected_hitlag);
uint8_t combat_attackairb_dense_seed_suppresses_full_body(const MslBatch* batch, int bi,
                                                          int attacker, int hb_id, int defender,
                                                          uint16_t defender_iid,
                                                          uint16_t expected_hitlag);
uint8_t combat_attackairhi_create_edge_damageflytop_suppresses_full_body(const MslBatch* batch,
                                                                         int bi, int attacker,
                                                                         int hb_id, int defender,
                                                                         uint16_t expected_hitlag);
uint8_t combat_attackairn_wait_dense_seed_suppresses_full_body(const MslBatch* batch, int bi,
                                                               int attacker, int hb_id,
                                                               int defender, uint16_t defender_iid);
uint8_t combat_attackairn_post_contact_dense_seed_suppresses_full_body(const MslBatch* batch,
                                                                       int bi, int attacker,
                                                                       int hb_id, int defender);
uint8_t combat_attackairn_guard_dense_seed_suppresses_full_body(const MslBatch* batch, int bi,
                                                                int attacker, int hb_id,
                                                                int defender,
                                                                uint16_t defender_iid);
uint8_t combat_attackairn_hb0_damageflytop_hitcapsule_owner(const MslBatch* batch, int bi,
                                                            int attacker, int hb_id, int defender);
uint8_t combat_attackhi3_landing_source_clear_blocks_enable_edge_body(const MslBatch* batch, int bi,
                                                                      size_t hb_i, size_t a_idx,
                                                                      size_t d_idx, int attacker,
                                                                      int defender);
uint8_t combat_seed_hitlist_suppresses_clank_candidate(const MslBatch* batch, int bi, int attacker,
                                                       int hb_id, int defender);
int combat_debug_attackairb_continuation_overlap(const MslBatch* batch, int batch_index,
                                                 int attacker, int hb_id, int defender, int cap_id,
                                                 float* out_overlap);
int combat_debug_body_matrix_overlap(const MslBatch* batch, int batch_index, int attacker,
                                     int hb_id, int defender, int cap_id, float* out_overlap);
uint8_t combat_shield_overlap_ftcoll_80007bcc(const MslBatch* batch, int bi, int attacker,
                                              int defender, int hb_id, float hx, float hy, float hz,
                                              float hr, float shx, float shy, float shz, float shr,
                                              float shield_desc_radius, float shield_owner_scale_y,
                                              uint8_t shield_desc_envelope_ready,
                                              uint8_t shield_extent_bridge_active,
                                              float* out_overlap_margin);
float combat_clamp01(float x);
float combat_latched_lightshield_amount_idx(const MslBatch* batch, size_t idx);
void combat_state_flags_set_is_hitlag(MslBatch* batch, size_t idx, uint16_t hitlag);
uint8_t combat_damage_allow_sdi_owner_action(uint16_t action);
void combat_damage_allow_sdi_set(MslBatch* batch, size_t idx);
void combat_state_flags_set_x221a_b3(MslBatch* batch, size_t idx);
void combat_state_flags_set_is_hitstun(MslBatch* batch, size_t idx, uint16_t hitstun);
void combat_state_flags_set_x221c_b0(MslBatch* batch, size_t idx);
void combat_state_flags_clear_x221c_b0(MslBatch* batch, size_t idx);
void combat_state_flags_clear_guard_reflecting(MslBatch* batch, size_t idx);
void combat_state_flags_clear_stale_guard_timer_bits_on_setoff_entry(MslBatch* batch, size_t idx);
void combat_state_flags_set_guard_reflect_timer_bits(MslBatch* batch, size_t idx);
void combat_apply_guard_reflect_body_hit_followup(const MslCommonParams* c, MslBatch* batch,
                                                  size_t idx, uint16_t pre_motion_id);
void combat_apply_ftCommon_8007D5D4_ground_to_air(MslBatch* batch, size_t idx);
void combat_publish_damage_entry_hitlag_ecb_current(MslBatch* batch, size_t idx);
uint8_t combat_is_guard_reflect_frozen_snapshot_idx(const MslBatch* batch, size_t idx);
uint8_t combat_defer_late_slot_same_frame_speciallw_entry_hit(const MslBatch* batch, int bi,
                                                              size_t a_idx, size_t d_idx,
                                                              int attacker, int defender,
                                                              int hb_id);
uint8_t combat_defer_late_slot_same_frame_speciallw_guardon_shield_hit(const MslBatch* batch,
                                                                       size_t a_idx, size_t d_idx,
                                                                       int attacker, int defender);
uint8_t combat_guard_reflect_active_x14_reflectdesc_blocks_hitshield(const MslBatch* batch,
                                                                     size_t idx,
                                                                     float overlap_margin);
uint8_t combat_pstadium_guardreflect_attackairlw_extent_accepts_shield(const MslBatch* batch,
                                                                       int bi, size_t a_idx,
                                                                       size_t d_idx, size_t hb_i,
                                                                       float overlap_margin);
uint8_t combat_pstadium_guardon_x44_reduced_proxy_rejects_shield(const MslBatch* batch, int bi,
                                                                 size_t a_idx, size_t d_idx,
                                                                 uint8_t hb_id, size_t hb_i,
                                                                 float overlap_margin);
uint8_t combat_guardon_raise_attacks4_age_rejects_shield(const MslBatch* batch,
                                                         const MslCommonParams* c, int bi,
                                                         size_t a_idx, size_t d_idx, size_t hb_i);
uint8_t combat_pstadium_guardon_attackairb_weak_x44_miss_suppresses_body(const MslBatch* batch,
                                                                         int bi, int attacker,
                                                                         int hb_id, size_t a_idx,
                                                                         size_t d_idx, size_t hb_i);
uint8_t combat_guard_reflect_final_x14_live_x18_blocks_body(const MslBatch* batch, size_t idx);
uint8_t combat_guard_reflect_active_x14_no_guardon_blocks_body(const MslBatch* batch, size_t idx);
uint8_t combat_guard_reflect_active_x14_rejects_strong_attackairb_shield(const MslBatch* batch,
                                                                         size_t a_idx, size_t d_idx,
                                                                         size_t hb_i);
uint8_t combat_guard_reflect_x18_expiry_rejects_strong_attackairn_hb1_shield(
    const MslBatch* batch, size_t a_idx, size_t d_idx, size_t hb_i, uint8_t hb_id);
uint8_t combat_guardsetoff_carried_shield_packet_owner(const MslBatch* batch, size_t idx);
uint8_t combat_shield_damage_powershield_suppressed_idx(const MslBatch* batch, size_t idx);
uint8_t combat_is_powershield_active_idx(const MslBatch* batch, size_t idx);
uint8_t combat_guard_setoff_recoil_x221c_b2_idx(const MslBatch* batch, size_t idx);
uint8_t combat_damageflyroll_rng_subset_allows_pre_action(const MslBatch* batch, size_t d_idx,
                                                          uint16_t action_id);
float combat_damage_ground_angle_to_floor_radians(float nx, float ny, float vx, float vy);
void combat_damage_install_grounded_kb(const MslCommonParams* c, MslBatch* batch, size_t d_idx,
                                       float kb_applied, float kb_x, float kb_y,
                                       uint16_t hitlag_frames, uint8_t force_tumble_severity,
                                       uint8_t allow_damagefly_hitlag_ecb_lock);
void combat_damageflyroll_consume_fighter_8006cda4_pre_gate_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg);
uint8_t combat_attackairb_hitbox_is_authored_strong(uint8_t hb_id, float hitbox_damage);
uint8_t combat_attackairn_hitbox_payload_is_authored_late(int hitcapsule_int_dmg,
                                                          uint16_t hitbox_angle,
                                                          uint16_t hitbox_kbg, uint16_t hitbox_bkb);
uint8_t combat_attackairn_hitbox_payload_is_authored_strong(uint8_t hb_id, int hitcapsule_int_dmg,
                                                            uint16_t hitbox_angle,
                                                            uint16_t hitbox_kbg,
                                                            uint16_t hitbox_bkb);
uint8_t combat_attack11_hitbox_payload_is_authored_jab(uint8_t hb_id, int hitcapsule_int_dmg,
                                                       uint16_t hitbox_angle, uint16_t hitbox_kbg,
                                                       uint16_t hitbox_bkb);
uint8_t combat_attackairb_hitbox_payload_is_authored_strong(uint8_t hb_id, int hitcapsule_int_dmg,
                                                            uint16_t hitbox_angle,
                                                            uint16_t hitbox_kbg,
                                                            uint16_t hitbox_bkb);
uint8_t combat_attackairb_hitbox_payload_is_authored_weak(uint8_t hb_id, int hitcapsule_int_dmg,
                                                          uint16_t hitbox_angle,
                                                          uint16_t hitbox_kbg, uint16_t hitbox_bkb);
uint8_t combat_attackairb_jump_tail_rejects_body_contact(
    const MslBatch* batch, int bi, int attacker, uint8_t hb_id, int defender, size_t a_idx,
    size_t d_idx, uint8_t cap_id, const MslHurtCap* cap, const MslHurtCap* defender_caps,
    uint16_t defender_cap_count_u16, float hx, float hy, float hz, float hr);
uint8_t combat_attackairf_hitbox_payload_is_authored_mid(uint8_t hb_id, int hitcapsule_int_dmg,
                                                         uint16_t hitbox_angle, uint16_t hitbox_kbg,
                                                         uint16_t hitbox_bkb);
uint8_t combat_attackairlw_hitbox_payload_is_authored_meteor(uint8_t hb_id, int hitcapsule_int_dmg,
                                                             uint16_t hitbox_angle,
                                                             uint16_t hitbox_kbg,
                                                             uint16_t hitbox_bkb);
uint8_t combat_attacklw4_hitbox_payload_is_authored_strong(uint8_t hb_id, int hitcapsule_int_dmg,
                                                           uint16_t hitbox_angle,
                                                           uint16_t hitbox_kbg,
                                                           uint16_t hitbox_bkb);
uint8_t combat_source_motion_is_attacks3(uint16_t source_motion_id);
uint8_t combat_attacks3_hitbox_payload_is_authored(uint8_t hb_id, int hitcapsule_int_dmg,
                                                   uint16_t hitbox_angle, uint16_t hitbox_kbg,
                                                   uint16_t hitbox_bkb);
uint8_t combat_attackhi4_hitbox_payload_is_authored_late(uint8_t hb_id, int hitcapsule_int_dmg,
                                                         uint16_t hitbox_angle, uint16_t hitbox_kbg,
                                                         uint16_t hitbox_bkb);
uint8_t combat_hurt_height_damageflytop_weak_attackairb_head_high_uses_medium(
    const MslBatch* batch, size_t d_idx, size_t a_idx, size_t source_hb_i, size_t source_cap_i,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_kneebend_attacks3_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_attacklw3_late_attackhi4_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
uint8_t combat_damageflyroll_wait_attacklw3_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before, uint16_t pre_action);
uint8_t combat_damageflyroll_jump_late_attackhi4_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb);
uint8_t combat_ground_to_air_ecb_lock_late_attackhi4_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before,
    uint16_t pre_damage_action);
void combat_damageflyroll_consume_attacklw3_late_attackhi4_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
uint8_t combat_source_motion_is_attackairb(uint16_t source_motion_id);
uint8_t combat_source_motion_is_attackairf(uint16_t source_motion_id);
uint8_t combat_source_motion_is_attackairn(uint16_t source_motion_id);
uint8_t combat_source_motion_is_attackairlw(uint16_t source_motion_id);
uint8_t combat_source_motion_is_attacklw4(uint16_t source_motion_id);
uint8_t combat_action_is_specialairn_family(uint8_t char_id, uint16_t action_id);
uint8_t combat_damageflyroll_fallspecial_attackairf_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_fallspecial_attackairf_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_jump_strong_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_jump_strong_attackairn_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_sustained_jump_late_attackairn_leg_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_sustained_jump_late_attackairn_leg_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_specialairhi_attacklw4_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_specialairhi_attacklw4_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_specialairn_attackairlw_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_recovery_action_attackairlw_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
void combat_damageflyroll_consume_recovery_action_attackairlw_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
uint8_t combat_damageflyroll_catch_attackairf_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint8_t defender_on_ground_before, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_catch_attackairf_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint8_t defender_on_ground_before, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_kneebend_weak_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_landingairn_weak_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
void combat_damageflyroll_consume_landingairn_weak_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
void combat_damageflyroll_consume_kneebend_weak_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_source_motion_is_speciallw_start(uint8_t char_id, uint16_t source_motion_id);
uint8_t combat_speciallw_start_hitbox_is_authored_reflector_start(const MslBatch* batch,
                                                                  size_t hb_i);
uint8_t combat_speciallw_start_source_payload_is_authored_reflector_start(int hitcapsule_int_dmg,
                                                                          uint16_t hitbox_angle,
                                                                          uint16_t hitbox_kbg,
                                                                          uint16_t hitbox_bkb);
uint8_t combat_damage_hitstun_strong_attackairlw_terminal_damageflytop_subtracts(
    const MslBatch* batch, const MslCombatProcessHitResolved* ev);
uint8_t combat_attackairb_damageflytop_selected_body_source_owns_pre_gate(uint8_t hb_id,
                                                                          uint8_t cap_id);
uint8_t combat_damageflyroll_damageflytop_attackairb_create_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid);
uint8_t combat_damageflyroll_damageflytop_attackairb_root_x14_primary_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_damageflytop_attackairb_hb0_cap1_effect_prefix_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_damageflytop_attackairb_live_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_landingairlw_strong_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_landingairlw_strong_attackairn_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_catch_strong_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t defender_on_ground_before, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_attacklw4_strong_attackairn_zero_marker_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_catch_strong_attackairn_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t defender_on_ground_before, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_attackairn_strong_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint16_t defender_motion_id);
void combat_damageflyroll_consume_attackairn_strong_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint16_t defender_motion_id);
uint8_t combat_damageflyroll_attackairb_late_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb, uint16_t defender_motion_id);
uint8_t combat_damageflyroll_specialhifall_late_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_specialhifall_attackairb_create_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id);
uint8_t combat_damageflyroll_catch_late_attackairn_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_catch_late_attackairn_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_jump_hitlag_strong_attackairn_tiplog_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_jump_hitlag_strong_attackairn_tiplog_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_attackairlw_hitbox_is_authored_strong(uint8_t hb_id, float damage);
uint8_t combat_downattacku_hitbox_is_authored_ground_sweep(const MslBatch* batch, size_t hb_i);
uint8_t combat_damageflyroll_recovering_ground_downattacku_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid);
void combat_damageflyroll_consume_recovering_ground_downattacku_hitcapsule_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid);
uint8_t combat_damageflyroll_thrownf_throwf_hitlag_owner(const MslBatch* batch, size_t d_idx,
                                                         size_t a_idx, int attacker,
                                                         uint8_t defender_on_ground_before);
void combat_damageflyroll_consume_thrownf_throwf_hitlag_count(MslBatch* batch, int bi, size_t d_idx,
                                                              size_t a_idx, int attacker,
                                                              uint8_t defender_on_ground_before);
uint8_t combat_damageflyroll_attackairb_strong_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid);
void combat_damageflyroll_consume_attackairb_strong_attackairb_hitcapsule_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid);
uint8_t combat_damageflyroll_dash_weak_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_dash_weak_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_attackhi4_weak_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
void combat_damageflyroll_consume_attackhi4_weak_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
void combat_damageflyroll_consume_kneebend_attacks3_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint8_t source_cap_valid, uint16_t source_motion_id,
    int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg,
    uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_weak_attackairb_source_skips_x1994(
    const MslCombatProcessHitResolved* ev);
uint8_t combat_damageflyroll_specialairhi_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid);
uint8_t combat_damageflyroll_specialhifall_strong_attackairb_cap2_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
void combat_damageflyroll_consume_specialhifall_strong_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_specialairs_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid);
uint8_t combat_damageflyroll_speciallw_start_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb);
uint8_t combat_damageflyroll_speciallw_end_strong_attackairb_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
void combat_damageflyroll_consume_speciallw_end_strong_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
uint8_t combat_damageflyroll_speciallw_end_continuing_weak_attackairb_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
void combat_damageflyroll_consume_speciallw_end_continuing_weak_attackairb_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid, size_t source_cap_i, uint8_t source_cap_valid,
    uint16_t source_motion_id, int source_hitcapsule_int_dmg, uint16_t source_hitbox_angle,
    uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb, uint8_t defender_on_ground_before);
uint8_t combat_damageflyroll_selected_source_normal_effect_prefix_count(
    const MslBatch* batch, size_t d_idx, size_t source_hb_i, uint8_t source_hb_valid,
    uint8_t source_cap_valid, uint16_t source_motion_id, int source_hitcapsule_int_dmg,
    uint16_t source_hitbox_angle, uint16_t source_hitbox_kbg, uint16_t source_hitbox_bkb,
    uint16_t pre_action);
uint8_t combat_damageflyroll_jumpaerial_attackairb_carry_selected_owner(const MslBatch* batch,
                                                                        size_t d_idx, size_t a_idx,
                                                                        int attacker,
                                                                        size_t source_cap_i,
                                                                        uint8_t source_cap_valid);
uint8_t combat_damageflyroll_jumpaerial_illusion_article_owner(const MslBatch* batch, size_t d_idx,
                                                               size_t a_idx, int attacker,
                                                               uint8_t source_hb_valid,
                                                               uint16_t source_item_type,
                                                               uint8_t source_item_state);
void combat_damageflyroll_consume_jumpaerial_attackairb_carry(MslBatch* batch, int bi, size_t d_idx,
                                                              size_t a_idx, int attacker,
                                                              size_t source_cap_i,
                                                              uint8_t source_cap_valid);
uint8_t combat_damageflyroll_attackairn_specialairhi_strong_attackairlw_hitcapsule_owner(
    const MslBatch* batch, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid);
void combat_damageflyroll_consume_attackairn_specialairhi_strong_attackairlw_hitcapsule_count(
    MslBatch* batch, int bi, size_t d_idx, size_t a_idx, int attacker, size_t source_hb_i,
    uint8_t source_hb_valid);
uint8_t combat_sheik_chain_start_terminal_owns_low_hurt_height(const MslBatch* batch,
                                                               size_t source_a_idx,
                                                               size_t source_hb_i,
                                                               uint8_t defender_on_ground_before,
                                                               uint8_t source_hb_valid);

int combat_get_env_dmg(float dmg);
float combat_pi_over_two_f32(void);
void combat_damage_calc_vel(MslBatch* batch, size_t d_idx, float x, float y);
uint8_t combat_damage_severity_u8_from_kb(const MslCommonParams* c, float kb_applied);
