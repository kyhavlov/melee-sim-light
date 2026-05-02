#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MSL_YOSHI_SHYGUY_VPOS_COUNT 6
#define MSL_YOSHI_SHYGUY_SPEED_COUNT 3
#define MSL_YOSHI_SHYGUY_DYN_Y_COUNT 128

typedef struct MslYoshiShyguyParams {
  uint8_t loaded;
  uint8_t _pad0;
  uint16_t stage_id;
  uint16_t item_kind;
  uint16_t timer_min;
  uint16_t timer_rand;
  uint16_t timer_reset;
  uint16_t spawnmany_rarity;
  uint16_t spawn_delay_step;
  float fall_accel;
  float spawn_left_x;
  float spawn_right_x;
  float state4_speed_mul;
  float jitter_y_amp;
  float vpos[MSL_YOSHI_SHYGUY_VPOS_COUNT];
  float speed[MSL_YOSHI_SHYGUY_SPEED_COUNT];
  float dyn_y_vel[MSL_YOSHI_SHYGUY_DYN_Y_COUNT];
} MslYoshiShyguyParams;

// Init-time loader for generated stage-owned item data (MSLSTIO1). May perform IO/allocation;
// per-frame gameplay reads the fixed table below only.
int stage_item_params_init(void);

const MslYoshiShyguyParams* stage_item_params_yoshi_shyguy(void);

#ifdef __cplusplus
}  // extern "C"
#endif
