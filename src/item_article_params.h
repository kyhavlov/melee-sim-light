#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MslItemArticleParams {
  uint8_t loaded;
  uint8_t _pad0[1];
  uint16_t blaster_shot_itkind;
  uint16_t blaster_gun_itkind;
  uint16_t laser_spawn_joint_part_id;
  uint16_t laser_lifetime_frames;
  uint16_t side_special_illusion_itkind;
  uint16_t _pad1;
  float laser_damage;
  float laser_size;
  float illusion_item_state0_damage;
  float illusion_item_state1_damage;
  float shield_bounce_extra_degrees;
} MslItemArticleParams;

// Init-time loader for MSLITAR1 known item/article constants. May perform IO/allocation; call only
// during batch init. Per-frame gameplay queries below read fixed tables only.
int item_article_params_init(void);

const MslItemArticleParams* item_article_params_get(uint8_t sim_char_id);
const MslItemArticleParams* item_article_params_for_laser_item_type(uint16_t type);
const MslItemArticleParams* item_article_params_for_illusion_item_type(uint16_t type);
uint8_t item_article_params_is_illusion_item_type(uint16_t type);

#ifdef __cplusplus
}  // extern "C"
#endif
