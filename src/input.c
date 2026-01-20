#include "input.h"

#include <errno.h>

#include "api.h"
#include "buttons.h"
#include "common_params.h"
#include "ucf.h"

// Input axes in MslStateSoA are Melee-legalized via ucf_clamp_stick_i8:
// ucf.h: clamp_stickMax = 80 (HSD_PadClampCheck3).
enum { MSL_STICK_MAX_I8 = 80 };

static inline float msl_absf(float x) { return x < 0.0f ? -x : x; }

static inline float stick_i8_to_unit(int8_t v) { return (float)v / (float)MSL_STICK_MAX_I8; }

static inline float apply_deadzone(float v, float dz) {
  if (msl_absf(v) < dz) {
    return 0.0f;
  }
  return v;
}

static inline uint8_t tilt_timer_update(uint8_t prev_timer, float axis, float prev_axis,
                                        float tilt_thresh) {
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:1908-2008.
  uint16_t t = (uint16_t)prev_timer;
  if (axis >= tilt_thresh) {
    if (prev_axis >= tilt_thresh) {
      t++;
      if (t > 0xFEu) {
        t = 0xFEu;
      }
    } else {
      t = 0;
    }
  } else if (axis <= -tilt_thresh) {
    if (prev_axis <= -tilt_thresh) {
      t++;
      if (t > 0xFEu) {
        t = 0xFEu;
      }
    } else {
      t = 0;
    }
  } else {
    t = 0xFEu;
  }
  return (uint8_t)t;
}

static inline uint8_t press_timer_u8_update(uint8_t prev_timer, uint16_t buttons_pressed,
                                            uint16_t press_mask) {
  // Decomp example (x67F): refs/melee/src/melee/ft/fighter.c:2078-2086.
  uint16_t t = (uint16_t)prev_timer;
  if ((buttons_pressed & press_mask) != 0) {
    t = 0;
  } else if (t < 0xFFu) {
    t++;
  }
  return (uint8_t)t;
}

static inline float trigger_u8_to_unit(uint8_t v) { return (float)v * (1.0f / 255.0f); }

static inline float trigger_unit_from_input(uint16_t buttons, uint8_t l, uint8_t r) {
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:1868-1890 and :2019-2050.
  // - If digital L/R is held, Melee treats shield trigger as fully pressed (`x650 = 1.0f`).
  // - Otherwise use the analog max of L/R.
  enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
  if ((buttons & LR) != 0) {
    return 1.0f;
  }
  const uint8_t m = l > r ? l : r;
  return trigger_u8_to_unit(m);
}

static inline uint8_t x672_trigger_timer_update(uint8_t prev_timer, float trig, float prev_trig,
                                                float trigger_min) {
  // fp->x672_input_timer_counter (saturating at 0xFE).
  // Decomp reference: refs/melee/src/melee/ft/fighter.c:2020-2050.
  uint16_t t = (uint16_t)prev_timer;
  if (trig >= trigger_min) {
    if (prev_trig >= trigger_min) {
      t++;
      if (t > 0xFEu) {
        t = 0xFEu;
      }
    } else {
      t = 0;
    }
  } else {
    t = 0xFEu;
  }
  return (uint8_t)t;
}

int input_apply(MslBatch* batch, const uint8_t* prev_input_bytes, size_t prev_input_stride_bytes,
                const uint8_t* input_bytes, size_t input_stride_bytes) {
  if (batch == NULL) {
    return EINVAL;
  }
  const MslCommonParams* com = msl_common_params();
  if (com == NULL) {
    return EINVAL;
  }
  if (prev_input_bytes == NULL || input_bytes == NULL) {
    return EINVAL;
  }
  if (prev_input_stride_bytes < sizeof(MslInput) || input_stride_bytes < sizeof(MslInput)) {
    return EINVAL;
  }

  const uint8_t ucf_enabled = batch->config.ucf_enabled ? 1 : 0;
  const uint8_t cardinals = batch->config.ucf_cardinals_1_0_enabled ? 1 : 0;

  for (int bi = 0; bi < batch->batch_size; bi++) {
    const uint8_t* prev_ptr = prev_input_bytes + (size_t)bi * prev_input_stride_bytes;
    const uint8_t* cur_ptr = input_bytes + (size_t)bi * input_stride_bytes;
    const MslInput* prev = (const MslInput*)prev_ptr;
    const MslInput* cur = (const MslInput*)cur_ptr;

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const size_t idx = msl_idx_player(bi, p);

      const uint16_t prev_buttons = prev->p[p].buttons;
      const uint16_t cur_buttons = cur->p[p].buttons;
      batch->state.prev_input_buttons[idx] = prev_buttons;
      batch->state.input_buttons[idx] = cur_buttons;
      batch->state.input_buttons_pressed[idx] = (uint16_t)(cur_buttons & (uint16_t)~prev_buttons);
      batch->state.input_buttons_released[idx] = (uint16_t)(prev_buttons & (uint16_t)~cur_buttons);

      const MslStickI8 main =
          ucf_process_stick_i8(cur->p[p].main_x, cur->p[p].main_y, ucf_enabled, cardinals);
      const MslStickI8 cstick =
          ucf_process_stick_i8(cur->p[p].c_x, cur->p[p].c_y, ucf_enabled, cardinals);
      const MslStickI8 prev_main =
          ucf_process_stick_i8(prev->p[p].main_x, prev->p[p].main_y, ucf_enabled, cardinals);
      batch->state.input_main_x[idx] = main.x;
      batch->state.input_main_y[idx] = main.y;
      batch->state.prev_input_main_x[idx] = prev_main.x;
      batch->state.prev_input_main_y[idx] = prev_main.y;
      batch->state.input_c_x[idx] = cstick.x;
      batch->state.input_c_y[idx] = cstick.y;

      // Update input-history tilt timers (x670/x671) using legalized axes.
      const float stick_x = apply_deadzone(stick_i8_to_unit(main.x), com->lstick_deadzone_x);
      const float stick_y = apply_deadzone(stick_i8_to_unit(main.y), com->lstick_deadzone_y);
      const float prev_stick_x =
          apply_deadzone(stick_i8_to_unit(prev_main.x), com->lstick_deadzone_x);
      const float prev_stick_y =
          apply_deadzone(stick_i8_to_unit(prev_main.y), com->lstick_deadzone_y);
      batch->state.tilt_timer_x[idx] =
          tilt_timer_update(batch->state.tilt_timer_x[idx], stick_x, prev_stick_x,
                            com->lstick_tilt_x_thresh);
      batch->state.tilt_timer_y[idx] =
          tilt_timer_update(batch->state.tilt_timer_y[idx], stick_y, prev_stick_y,
                            com->lstick_tilt_y_thresh);

      batch->state.input_l[idx] = cur->p[p].l;
      batch->state.input_r[idx] = cur->p[p].r;

      // Update additional input-history timers (single-writer invariant: input.c).
      enum { LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R };
      batch->state.lr_press_timer[idx] =
          press_timer_u8_update(batch->state.lr_press_timer[idx], batch->state.input_buttons_pressed[idx], LR);

      const float trig = trigger_unit_from_input(cur_buttons, cur->p[p].l, cur->p[p].r);
      const float prev_trig = trigger_unit_from_input(prev_buttons, prev->p[p].l, prev->p[p].r);
      batch->state.x672_input_timer[idx] = x672_trigger_timer_update(
          batch->state.x672_input_timer[idx], trig, prev_trig, com->powershield_reflect_trigger_min);
    }
  }

  return 0;
}
