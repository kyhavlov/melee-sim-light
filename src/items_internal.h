#include "items.h"
#include "char_registry.h"
#include "ids.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "action_ids.h"
#include "anim_frame.h"
#include "anim_pose.h"
#include "anim_timebase.h"
#include "batch_internal.h"
#include "char_params.h"
#include "combat.h"
#include "combat_geom.h"
#include "common_params.h"
#include "damage_terminal_owner.h"
#include "damage_source.h"
#include "falcon_specials.h"
#include "hit_elements.h"
#include "hitboxes_tables.h"
#include "hitlist.h"
#include "input_axis.h"
#include "item_article_params.h"
#include "item_common_params.h"
#include "item_reflect.h"
#include "trigger_input.h"
#include "laser_params.h"
#include "lbcollision_constants.h"
#include "hurtcaps_tables.h"
#include "motion_state_owners.h"
#include "move_tables.h"
#include "msl_math.h"
#include "mtx34.h"
#include "shield_tilt_table.h"
#include "sheik_specials.h"
#include "special_msids.h"
#include "stage_collision.h"
#include "stage_item_params.h"
#include "staling.h"

enum {
  MSL_ITEM_HIDDEN_CALLBACK_CLEAR = 1u << 0u,
  MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME = 1u << 1u,
};

enum {
  // The generated hurtcap substrate exposes Fighter_Part ids but not semantic body-region labels.
  // Fox/Falco cap12 is anchored to FtPart 18, the dynamic tail chain used by lbColl body tests.
  // data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18.
  MSL_ITEM_HURTCAP_FOX_FALCO_TAIL_PART_ID = 18,
};

// Laser collision offset scale lanes.
typedef enum MslLaserCollisionSpaceLane {
  MSL_LASER_COLLISION_SPACE_SHIELD = 0,
  MSL_LASER_COLLISION_SPACE_REFLECT = 1,
  MSL_LASER_COLLISION_SPACE_BODY = 2,
} MslLaserCollisionSpaceLane;

typedef struct MslIllusionItemHitParams {
  float radius;
  float damage;
  int8_t shield_damage;
  uint16_t angle;
  uint16_t kbg;
  uint16_t wsk;
  uint16_t bkb;
  uint8_t element;
  float hitbox_y_offset;
} MslIllusionItemHitParams;

enum {
  // Item script extraction uses -128 as the "no shield-damage delta" sentinel for some lanes.
  // Source: data/characters/{fox,falco}.json illusion_item_state{0,1}_shield_damage.
  MSL_ILLUSION_SHIELD_DAMAGE_UNSET = INT8_C(-128),
};

uint8_t slippi_metadata_low_byte_from_f32(float v);
uint8_t item_state_flags_2218_is_reflect_behavior_only(uint8_t flags_2218);
uint8_t item_any_hitbox_allows_fighter(MslBatch* batch, int bi, int item_slot, int victim,
                                       uint16_t victim_iid);
float item_hitcapsule_stale_damage_mul(const MslBatch* batch, size_t item_idx, size_t owner_idx,
                                       uint16_t attack_id);
uint8_t item_stale_queue_contains_instance(const MslBatch* batch, size_t owner_idx,
                                           uint16_t attack_id, uint16_t attack_instance);
void sheik_chain_hidden_clear_slot(MslBatch* batch, size_t ii);
void item_slot_clear(MslBatch* batch, size_t ii);
uint8_t items_row_has_any(const MslBatch* batch, int bi);
void items_sort(MslBatch* batch, int bi);
int items_alloc_slot(MslBatch* batch, int bi);
uint32_t items_next_spawn_id(MslBatch* batch, int bi);
int items_find_gun_slot(const MslBatch* batch, int bi, int owner, uint16_t gun_itkind);
uint8_t blaster_gun_state_from_action_id(uint16_t action_id_u16);
uint8_t items_action_is_damage_family(uint16_t action_id_u16);
uint8_t item_type_is_fox_laser(uint16_t item_type);
uint8_t item_type_is_falco_laser(uint16_t item_type);
int items_find_owned_item_slot(const MslBatch* batch, int bi, int owner, uint16_t item_kind);
float items_cur_anim_frame_f32(const MslBatch* batch, size_t idx);
float item_segment_segment_dist2(float p0x, float p0y, float p0z, float p1x, float p1y, float p1z,
                                 float q0x, float q0y, float q0z, float q1x, float q1y, float q1z);
uint8_t item_swept_sphere_capsule_intersects(const MslBatch* batch, int bi, int p, float x0,
                                             float y0, float x1, float y1, float r, int cap_i,
                                             uint8_t* out_hurt_height);
uint8_t item_laser_hitcapsule_overlaps_fighter_hitcapsule(const MslBatch* batch, size_t hb_i,
                                                          float x0, float y0, float x1, float y1,
                                                          float r);
uint8_t item_active_hitbox_overlaps_fighter_hurtcaps(MslBatch* batch, int bi, int defender,
                                                     int hitbox_port, int owner);
uint8_t item_swept_sphere_capsule_overlap_amount(const MslBatch* batch, int bi, int p, float x0,
                                                 float y0, float x1, float y1, float r, int cap_i,
                                                 uint8_t* out_hurt_height,
                                                 float* out_overlap_amount, uint8_t flatten_hurt_z,
                                                 float hurt_radius_mul);
uint8_t item_body_lbcoll_matrix_radius_overlap(const MslBatch* batch, int bi, int p, float x0,
                                               float y0, float x1, float y1, float r, int cap_i,
                                               uint8_t* out_hurt_height, float* out_overlap_amount,
                                               uint8_t* out_evaluated, uint8_t flatten_hurt_z);
uint8_t item_swept_sphere_sphere_intersects_3d(float ax0, float ay0, float az0, float ax1,
                                               float ay1, float az1, float ar, float bx, float by,
                                               float bz, float br);
uint8_t item_sphere_sphere_intersects_2d(float ax, float ay, float ar, float bx, float by,
                                         float br);
uint8_t item_sphere_sphere_intersects_3d(float ax, float ay, float az, float ar, float bx, float by,
                                         float bz, float br);
float item_guard_shield_radius_from_state(const MslBatch* batch, const MslCommonParams* common,
                                          size_t idx);
uint8_t item_guardsetoff_current_shielddesc_allows_item_contact(const MslBatch* batch, size_t idx,
                                                                size_t item_idx);
float item_guard_reflect_entry_pose_radius(const MslBatch* batch, const MslCommonParams* common,
                                           size_t idx);
uint8_t item_guard_reflect_center_xyz(const MslBatch* batch, size_t idx, float* out_x, float* out_y,
                                      float* out_z);
uint8_t laser_try_shield_bounce_velocity_from_segment(float vx, float vy, float shield_x,
                                                      float shield_y, float shield_z,
                                                      float shield_radius, float prev_x,
                                                      float prev_y, float prev_z, float cur_x,
                                                      float cur_y, float cur_z, float hit_radius,
                                                      float* out_vx, float* out_vy);
float laser_collision_offset_scale(const MslLaserParams* lp, uint8_t laser_state,
                                   float laser_scale_z, MslLaserCollisionSpaceLane lane,
                                   uint8_t body_shield_adjacent, uint8_t current_endpoint);
uint8_t item_laser_fighter_hitcapsule_contact_mask_precedes_shield_body(
    const MslBatch* batch, int bi, int fighter, const MslLaserParams* lp, uint8_t laser_state,
    float x0, float y0, float x, float y, float ux, float uy, float sr, float laser_prev_scale_z,
    float laser_scale_z, float item_damage);
uint8_t item_prev_action_is_guard_reflect_locomotion_pose_source(uint16_t action_id);
uint8_t item_prev_action_is_guardon_spawn_frame_reflect_source(uint16_t action_id);
uint8_t item_prev_action_is_guardon_spawn_frame_non_reflect_source(uint16_t action_id);
uint8_t item_is_fresh_guardreflect_shield_center_source(const MslBatch* batch, size_t d_idx,
                                                        uint16_t frame_start_prev_action_id);
uint8_t item_fresh_guardon_locomotion_shielddesc_owner(const MslBatch* batch, size_t d_idx);
uint8_t item_fresh_guardon_landing_shielddesc_owner(const MslBatch* batch, size_t d_idx);
uint8_t laser_body_uses_exact_lbcoll_hitcapsule_sweep(const MslBatch* batch, size_t d_idx,
                                                      uint8_t laser_state, float laser_age_frames,
                                                      uint16_t item_type,
                                                      uint8_t body_shield_adjacent,
                                                      uint8_t flatten_body_hurt_z);
uint8_t laser_body_exact_lbcoll_flattens_hurt_z(const MslBatch* batch, size_t d_idx,
                                                uint8_t body_shield_adjacent,
                                                uint8_t flatten_body_hurt_z);
uint8_t laser_grounded_body_landing_fall_special_exact_z_owner(const MslBatch* batch, size_t d_idx,
                                                               float laser_age_frames);
uint8_t laser_tail_shallow_body_contact_rejected(const MslBatch* batch, size_t d_idx,
                                                 uint16_t item_type, uint8_t laser_state,
                                                 uint8_t hit_hb_id, float laser_radius, int cap_i,
                                                 float overlap_amount, uint16_t item_attack_id);
uint8_t laser_exact_lbcoll_body_contact_admits_candidate(
    const MslBatch* batch, size_t d_idx, const MslCommonParams* common, uint8_t hurt_height,
    float overlap_amount, float laser_prev_scale_z, float laser_scale_z, uint16_t item_type,
    uint8_t laser_state, uint8_t hit_hb_id, float laser_radius, int cap_i, uint16_t item_attack_id);
uint8_t laser_item_phantom_hitlag_suppressed_by_reflect_behavior_carry(
    const MslBatch* batch, size_t d_idx, uint8_t laser_state, float overlap_amount,
    const MslCommonParams* common);
uint8_t laser_airborne_body_uses_flattened_hurt_z(const MslBatch* batch, size_t d_idx,
                                                  uint8_t laser_state, uint16_t item_type,
                                                  uint16_t item_attack_id);
uint8_t laser_airborne_damagefall_uses_lbcoll_hurt_radius(const MslBatch* batch, size_t d_idx,
                                                          size_t o_idx, uint8_t laser_state,
                                                          float laser_age_frames,
                                                          uint16_t item_type,
                                                          uint16_t item_attack_id);
uint8_t laser_grounded_body_uses_lbcoll_hurt_radius(const MslBatch* batch, size_t d_idx,
                                                    uint8_t laser_state, float laser_age_frames,
                                                    uint16_t item_type);
uint8_t item_try_guard_fresh_shield_center(const MslBatch* batch, size_t d_idx,
                                           float laser_age_frames, float* out_x, float* out_y,
                                           float* out_z);
uint8_t item_try_guardon_carried_behavior_shield_center(const MslBatch* batch, size_t d_idx,
                                                        float* out_x, float* out_y, float* out_z);
void item_guard_reflect_apply_recharge(MslBatch* batch, size_t d_idx);
void item_guard_reflect_restore_anim_drain(MslBatch* batch, size_t d_idx);
uint8_t item_guard_reflect_body_hit_consumes_shield_state(const MslBatch* batch, size_t d_idx);
uint8_t item_guardon_reflect_body_hit_undoes_action_recharge(const MslBatch* batch, size_t d_idx);
void item_guardon_reflect_undo_action_recharge(MslBatch* batch, size_t d_idx);
uint8_t item_guardreflect_active_timer_shield_contact_needs_drain(const MslBatch* batch,
                                                                  size_t d_idx);
uint8_t item_guardreflect_origin_x14_expired_this_callback(const MslBatch* batch, size_t d_idx);
void item_guardreflect_apply_contact_drain(MslBatch* batch, size_t d_idx);
uint8_t yoshi_shyguy_try_laser_item_hit(MslBatch* batch, int bi, int laser_slot,
                                        const MslLaserParams* lp, uint8_t laser_state, float x0,
                                        float y0, float x, float y, float ux, float uy, float sr,
                                        float laser_prev_scale_z, float laser_scale_z);
void yoshi_shyguy_stage_update(MslBatch* batch, int bi);
void yoshi_shyguy_items_update(MslBatch* batch, int bi);
uint8_t action_is_illusion_setphys(uint8_t char_id, uint16_t action_id_u16);
uint8_t items_row_has_illusion_setphys_source(const MslBatch* batch, int bi, int num_players);
void illusion_items_update_and_collide(MslBatch* batch, int bi);
void lasers_update_and_collide(MslBatch* batch, int bi);
void sheik_held_needles_update_anim_phase(MslBatch* batch, int bi);
void sheik_chain_items_update_anim_phase(MslBatch* batch, int bi);
void sheik_needle_spawn_thrown_article_from_fighter(MslBatch* batch, int bi, int owner);
void sheik_needles_update_and_collide(MslBatch* batch, int bi);
void sheik_vanish_smoke_items_update(MslBatch* batch, int bi);
void sheik_vanish_smoke_collide(MslBatch* batch, int bi);

static inline size_t idx_hurtcap(int bi, int p, int cap_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HURTCAPS +
         (size_t)cap_i;
}

static inline size_t idx_hitbox(int bi, int p, int hb_i) {
  return ((size_t)bi * (size_t)MSL_MAX_PLAYERS + (size_t)p) * (size_t)MSL_MAX_HITBOXES +
         (size_t)hb_i;
}

// refs/melee/src/melee/lb/forward.h::HurtCapsuleState
enum { MSL_HURTCAPS_DISABLED = 1u };
