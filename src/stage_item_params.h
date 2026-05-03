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
  float fall_speed_max;
  float spawn_left_x;
  float spawn_right_x;
  float state4_speed_mul;
  float jitter_y_amp;
  float vpos[MSL_YOSHI_SHYGUY_VPOS_COUNT];
  float speed[MSL_YOSHI_SHYGUY_SPEED_COUNT];
  float dyn_y_vel[MSL_YOSHI_SHYGUY_DYN_Y_COUNT];
} MslYoshiShyguyParams;

typedef struct MslDreamWhispyParams {
  uint8_t loaded;
  uint8_t _pad0[3];
  uint16_t stage_id;
  uint16_t _pad1;
  float wind_speed;
  float right_rect_left;
  float right_rect_right;
  float left_rect_left;
  float left_rect_right;
  float rect_bottom;
  float rect_top;
} MslDreamWhispyParams;

// Init-time loader for generated stage-owned item/object data (MSLSTIO1/MSLWHSP1).
// May perform IO/allocation; per-frame gameplay reads the fixed tables below only.
int stage_item_params_init(void);

const MslYoshiShyguyParams* stage_item_params_yoshi_shyguy(void);
const MslDreamWhispyParams* stage_item_params_dream_whispy(void);

#ifdef __cplusplus
}  // extern "C"
#endif
