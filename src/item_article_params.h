#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MslItemArticleParams {
  uint8_t loaded;
  uint8_t needle_hurtbox_count;
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
  uint16_t sheik_chain_lifetime_frames;
  uint16_t sheik_vanish_itkind;
  uint16_t sheik_vanish_lifetime_frames;
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
