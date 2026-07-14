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
#include "fighter_pose.h"
#include "fighter_script.h"
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
#include "marth_specials.h"
#include "motion_state_owners.h"
#include "guard_lifecycle.h"
#include "msl_math.h"
#include "mtx34.h"
#include "shield_tilt_table.h"
#include "sheik_specials.h"
#include "special_msids.h"
#include "stage_collision.h"
#include "stage_item_params.h"
#include "staling.h"

enum {
  MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_THIS_FRAME = 1u << 1u,
  MSL_ITEM_HIDDEN_CALLBACK_SPAWNED_MOTION_APPLIED = 1u << 3u,
};

// Compact, table-backed projection of the item HitCapsule gates consumed by
// ftColl_8007925C. The extractor decodes these from command-11 words; article
// owners must not infer them from item kind or fighter action.
enum {
  MSL_ITEM_CONTACT_TARGET_GROUNDED = UINT32_C(1) << 0,
  MSL_ITEM_CONTACT_TARGET_AERIAL = UINT32_C(1) << 1,
  MSL_ITEM_CONTACT_BODY_ENABLED = UINT32_C(1) << 2,
  MSL_ITEM_CONTACT_GRABBABLE_ONLY = UINT32_C(1) << 3,
  MSL_ITEM_CONTACT_CLANK = UINT32_C(1) << 4,
  MSL_ITEM_CONTACT_REFLECTABLE = UINT32_C(1) << 5,
  MSL_ITEM_CONTACT_ABSORBABLE = UINT32_C(1) << 6,
  MSL_ITEM_CONTACT_SHIELDABLE = UINT32_C(1) << 7,
  MSL_ITEM_CONTACT_FACING_FILTER = UINT32_C(1) << 8,
  MSL_ITEM_CONTACT_SHIELD_BOUNCE = UINT32_C(1) << 9,
  MSL_ITEM_CONTACT_SHIELD_X42_B4 = UINT32_C(1) << 10,
};

typedef enum MslItemFighterContactKind {
  MSL_ITEM_FIGHTER_CONTACT_NONE = 0,
  MSL_ITEM_FIGHTER_CONTACT_REFLECT = 1,
  MSL_ITEM_FIGHTER_CONTACT_CLANK = 2,
  MSL_ITEM_FIGHTER_CONTACT_SHIELD = 3,
  MSL_ITEM_FIGHTER_CONTACT_COUNTER = 4,
  MSL_ITEM_FIGHTER_CONTACT_BODY = 5,
} MslItemFighterContactKind;

typedef struct MslItemHitCapsulePacket {
  float x0;
  float y0;
  float z0;
  float x1;
  float y1;
  float z1;
  float radius;
  float damage;
  uint32_t flags;
  uint8_t hitbox_id;
  uint8_t element;
  uint8_t item_grounded;
} MslItemHitCapsulePacket;

typedef struct MslItemFighterContact {
  MslItemFighterContactKind kind;
  int8_t fighter_hitbox;
  int8_t hurtcap;
  uint8_t hurt_height;
  uint8_t hurt_status;
  uint8_t body_exact_evaluated;
  float body_overlap;
  float descriptor_x;
  float descriptor_y;
  float descriptor_z;
  float descriptor_radius;
  float reflect_damage_mul;
  float reflect_speed_mul;
  int32_t reflect_max_damage;
} MslItemFighterContact;

enum {
  MSL_ITEM_HITBOX_CONTACT_FIGHTER_RECEIVED = 1u << 0,
  MSL_ITEM_HITBOX_CONTACT_ITEM_RECEIVED = 1u << 1,
};

enum {
  // The generated hurtcap substrate exposes Fighter_Part ids but not semantic body-region labels.
  // Fox/Falco cap12 is anchored to FtPart 18, the dynamic tail chain used by lbColl body tests.
  // data/hurtcaps/{fox,falco}.json cap12 -> FtPart 18.
  MSL_ITEM_HURTCAP_FOX_FALCO_TAIL_PART_ID = 18,
};

// Laser collision offset scale lanes.
typedef struct MslIllusionItemHitParams {
  float radius;
  float damage;
  uint32_t flags;
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
uint8_t item_reflector_descriptor(const MslBatch* batch, size_t idx, float* out_x, float* out_y,
                                  float* out_z, float* out_radius, float* out_damage_mul,
                                  float* out_speed_mul, int32_t* out_max_damage);
MslItemFighterContactKind item_hitcapsule_select_fighter_contact(MslBatch* batch, int bi,
                                                                 int item_slot, int defender,
                                                                 const MslItemHitCapsulePacket* hit,
                                                                 MslItemFighterContact* out);
uint8_t item_hitcapsule_apply_fighter_hitbox_contact(MslBatch* batch, int bi, int item_slot,
                                                     int defender,
                                                     const MslItemHitCapsulePacket* hit,
                                                     const MslItemFighterContact* contact);
uint8_t laser_try_shield_bounce_velocity_from_segment(float vx, float vy, float shield_x,
                                                      float shield_y, float shield_z,
                                                      float shield_radius, float prev_x,
                                                      float prev_y, float prev_z, float cur_x,
                                                      float cur_y, float cur_z, float hit_radius,
                                                      float* out_vx, float* out_vy);
uint8_t yoshi_shyguy_try_laser_item_hit(MslBatch* batch, int bi, int laser_slot,
                                        const MslLaserParams* lp, uint8_t laser_state, float x0,
                                        float y0, float x, float y, float ux, float uy,
                                        float laser_prev_scale_z, float laser_scale_z);
void yoshi_shyguy_stage_update(MslBatch* batch, int bi);
void yoshi_shyguy_items_update(MslBatch* batch, int bi);
uint8_t action_is_illusion_setphys(uint8_t char_id, uint16_t action_id_u16);
uint8_t items_row_has_illusion_setphys_source(const MslBatch* batch, int bi, int num_players);
void illusion_items_update_and_collide(MslBatch* batch, int bi);
void lasers_source_update_and_collide(MslBatch* batch, int bi);
void zelda_din_fire_update_and_collide(MslBatch* batch, int bi);
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
