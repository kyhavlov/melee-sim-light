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
#include "motion_state_runtime.h"
#include "msl_math.h"
#include "mtx34.h"
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
  MSL_ATTACKAIRB_STRONG_BODY_ROOT_HITBOX = 0u,
  MSL_ATTACKAIRB_STRONG_BODY_TAIL_HITBOX = 1u,
  MSL_HURTCAP_DAMAGEFLYTOP_UPPER_BODY_SLOT = 1u,
  MSL_HURTCAP_DAMAGEFLYTOP_HEAD_HIGH_SLOT = 2u,
  MSL_HURTCAP_DAMAGEFLYTOP_ROOT_BODY_SLOT = 0u,
  MSL_HURTCAP_DAMAGEFLYTOP_LEG_LOW_SLOT = 10u,
  MSL_HURTCAP_DAMAGEFLYTOP_LEG_HIGH_SLOT = 11u,
  MSL_HURTCAP_DAMAGEFLYTOP_XROTN_SLOT = 12u,
};

static inline uint8_t combat_action_is_catch_family(uint16_t action_id) {
  return (action_id >= (uint16_t)MSL_ACT_CATCH && action_id <= (uint16_t)MSL_ACT_CATCH_CUT) ? 1u
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
  float effect_damage;
} MslCombatBodyDamageLogEntry;

typedef struct MslCombatBodyDamageScratch {
  uint8_t count;
  int max_env_dmg;
  MslCombatBodyDamageLogEntry entries[MSL_COMBAT_BODY_DAMAGE_LOG_CAP];
} MslCombatBodyDamageScratch;

uint8_t combat_source_body_log_record(MslBatch* batch, MslCombatBodyDamageScratch* scratch, int bi,
                                      int attacker, int defender, int hb_id, int cap_id);
void combat_source_body_log_apply(MslBatch* batch, int bi, MslCombatBodyDamageScratch* scratch);
void combat_source_body_phantom(MslBatch* batch, int bi, int attacker, int defender, int hb_id,
                                uint8_t hit_group);
void combat_source_body_invincible(MslBatch* batch, int bi, int attacker, int defender, int hb_id,
                                   uint8_t hit_group, uint8_t rehit_frames);
void combat_source_shield_apply(MslBatch* batch, int bi, int attacker, int defender,
                                int max_int_damage, int shield_damage_taken, uint8_t element);
uint8_t combat_source_catch_wall_obstructed(const MslBatch* batch, int bi, size_t attacker_idx,
                                            size_t defender_idx);

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

uint8_t combat_shine_start_grounded_ledge_ecb_lock_owner(const MslBatch* batch, size_t d_idx,
                                                         size_t a_idx, uint16_t attacker_action);
uint8_t combat_attackairlw_hitbox_payload_is_authored_strong_meteor(uint8_t hb_id, float damage,
                                                                    uint16_t angle, uint16_t kbg,
                                                                    uint16_t wsk, uint16_t bkb);
uint8_t combat_is_damage_or_firefox_launch_victim_action(uint8_t char_id, uint16_t action_id);
uint8_t combat_is_damage_air_action(uint16_t action_id);
uint8_t combat_is_downed_damage_contact_action(uint16_t action_id);
uint16_t combat_down_damage_action_from_source(uint16_t action_id);
uint32_t combat_down_damage_submotion_from_action(uint16_t action_id);
uint8_t combat_body_overlap_lbColl_80006E58_matrix_radius(
    const MslBatch* batch, int bi, int attacker, int hb_id, int defender, int cap_id, float hx,
    float hy, float hz, float hr, float ax, float ay, float az, float bx, float by, float bz,
    uint8_t use_catch_grabbable_pose, float* out_overlap_amount, uint8_t* out_evaluated);
int combat_debug_body_matrix_overlap(const MslBatch* batch, int batch_index, int attacker,
                                     int hb_id, int defender, int cap_id, float* out_overlap);
uint8_t combat_shield_overlap_ftcoll_80007bcc(const MslBatch* batch, int bi, int attacker,
                                              int defender, int hb_id, float* out_overlap_margin);
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
uint8_t combat_shield_damage_powershield_suppressed_idx(const MslBatch* batch, size_t idx);
uint8_t combat_is_powershield_active_idx(const MslBatch* batch, size_t idx);
uint8_t combat_guard_setoff_recoil_x221c_b2_idx(const MslBatch* batch, size_t idx);
float combat_damage_ground_angle_to_floor_radians(float nx, float ny, float vx, float vy);
void combat_damage_install_grounded_kb(const MslCommonParams* c, MslBatch* batch, size_t d_idx,
                                       float kb_applied, float kb_x, float kb_y,
                                       uint16_t hitlag_frames, uint8_t force_tumble_severity,
                                       uint8_t allow_damagefly_hitlag_ecb_lock);
uint8_t combat_sheik_chain_start_terminal_owns_low_hurt_height(const MslBatch* batch,
                                                               size_t source_a_idx,
                                                               size_t source_hb_i,
                                                               uint8_t defender_on_ground_before,
                                                               uint8_t source_hb_valid);

int combat_get_env_dmg(float dmg);
float combat_pi_over_two_f32(void);
void combat_damage_calc_vel(MslBatch* batch, size_t d_idx, float x, float y);
uint8_t combat_damage_severity_u8_from_kb(const MslCommonParams* c, float kb_applied);
