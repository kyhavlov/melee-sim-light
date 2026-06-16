#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  MSL_ITEM_ARTICLE_MAX_HITBOXES = 4,
  MSL_ITEM_ARTICLE_VANISH_SIZE_KEYFRAMES = 2,
  MSL_ITEM_ARTICLE_NEEDLE_DROP_TABLE_LEN = 8,
};

typedef struct MslItemArticleParams {
  uint8_t loaded;
  uint8_t needle_hurtbox_count;
  uint8_t needle_hitbox_count;
  uint16_t blaster_shot_itkind;
  uint16_t blaster_gun_itkind;
  uint16_t laser_spawn_joint_part_id;
  uint16_t laser_lifetime_frames;
  uint16_t side_special_illusion_itkind;
  uint16_t needle_throw_itkind;
  uint16_t needle_held_itkind;
  uint16_t needle_lifetime_frames;
  uint16_t needle_bounce_lifetime_frames;
  uint16_t needle_hurtbox_bone_id;
  uint16_t sheik_chain_itkind;
  uint16_t sheik_chain_spawn_part_id;
  uint16_t sheik_chain_lifetime_frames;
  uint16_t sheik_vanish_itkind;
  uint16_t sheik_vanish_spawn_part_id;
  uint16_t sheik_vanish_lifetime_frames;
  uint8_t vanish_hitbox_count;
  float laser_damage;
  float laser_size;
  float illusion_item_state0_damage;
  float illusion_item_state1_damage;
  float shield_bounce_extra_degrees;
  float needle_launch_speed;
  float needle_hurtbox_a_offset[3];
  float needle_hurtbox_b_offset[3];
  float needle_hurtbox_scale;
  float needle_hitbox_damage;
  float needle_hitbox_damage_by_id[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  float needle_hitbox_size[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  float needle_hitbox_x_offset[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  float needle_hitbox_y_offset[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  float needle_hitbox_z_offset[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  uint16_t needle_hitbox_angle[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  uint16_t needle_hitbox_kbg[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  uint16_t needle_hitbox_wsk[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  uint16_t needle_hitbox_bkb[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  uint8_t needle_hitbox_element[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  int8_t needle_hitbox_shield_damage[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  uint32_t needle_hitbox_flags[MSL_ITEM_ARTICLE_MAX_HITBOXES];
  float vanish_hitbox_damage;
  float vanish_hitbox_size;
  float vanish_hitbox_x_offset;
  float vanish_hitbox_y_offset;
  float vanish_hitbox_z_offset;
  uint16_t vanish_hitbox_angle;
  uint16_t vanish_hitbox_kbg;
  uint16_t vanish_hitbox_wsk;
  uint16_t vanish_hitbox_bkb;
  uint8_t vanish_hitbox_element;
  int8_t vanish_hitbox_shield_damage;
  uint32_t vanish_hitbox_flags;
  uint8_t vanish_hitbox_size_keyframe_count;
  uint16_t vanish_hitbox_size_keyframe_frame[MSL_ITEM_ARTICLE_VANISH_SIZE_KEYFRAMES];
  float vanish_hitbox_size_keyframe_value[MSL_ITEM_ARTICLE_VANISH_SIZE_KEYFRAMES];
  uint16_t vanish_hitbox_remove_frame;
  // Thrown-Needle dropped/bounced motion RNG tables from itseakneedlethrown.c. Indexed by HSD_Randi(8).
  // - needle_drop_min_vel_y    <- it_803F6FA0 (SetupDrop xDDC terminal velocity)
  // - needle_drop_gravity      <- it_803F6FC0 (SetupDrop xDE0 gravity)
  // - needle_bounce_min_vel_y  <- it_803F7020 (DmgReceived x40_vel.y = ABS(...))
  // - needle_bounce_gravity    <- it_803F7040 (SetupBounce xDE0 gravity)
  float needle_drop_min_vel_y[MSL_ITEM_ARTICLE_NEEDLE_DROP_TABLE_LEN];
  float needle_drop_gravity[MSL_ITEM_ARTICLE_NEEDLE_DROP_TABLE_LEN];
  float needle_bounce_min_vel_y[MSL_ITEM_ARTICLE_NEEDLE_DROP_TABLE_LEN];
  float needle_bounce_gravity[MSL_ITEM_ARTICLE_NEEDLE_DROP_TABLE_LEN];
} MslItemArticleParams;

// Init-time loader for MSLITAR1 known item/article constants. May perform IO/allocation; call only
// during batch init. Per-frame gameplay queries below read fixed tables only.
int item_article_params_init(void);

const MslItemArticleParams* item_article_params_get(uint8_t sim_char_id);
const MslItemArticleParams* item_article_params_for_laser_item_type(uint16_t type);
const MslItemArticleParams* item_article_params_for_illusion_item_type(uint16_t type);
const MslItemArticleParams* item_article_params_for_sheik_needle_throw_item_type(uint16_t type);
uint8_t item_article_params_is_illusion_item_type(uint16_t type);

#ifdef __cplusplus
}  // extern "C"
#endif
