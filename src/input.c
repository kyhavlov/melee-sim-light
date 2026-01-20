#include "input.h"

#include <errno.h>

#include "api.h"
#include "buttons.h"
#include "common_params.h"
#include "decomp/lb/lb_00ce.h"
#include "ucf.h"

// Input axes in MslStateSoA are Melee-legalized via ucf_clamp_stick_i8:
// ucf.h: clamp_stickMax = 80 (HSD_PadClampCheck3).
enum { MSL_STICK_MAX_I8 = 80 };

// UCF pad buffer: refs/ucf/include/ucf/pad_buffer.h
enum { MSL_UCF_PADBUF_SIZE = 4, MSL_UCF_PADBUF_MASK = MSL_UCF_PADBUF_SIZE - 1 };

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

static inline float msl_ucf_popo_to_nana(float x) {
  // refs/ucf/include/util/melee/pad.h::popo_to_nana
  if (x >= 0.0f) {
    const int8_t q = (int8_t)(x * 127.0f);
    return (float)q * (1.0f / 127.0f);
  }
  const int8_t q = (int8_t)(x * 128.0f);
  return (float)q * (1.0f / 128.0f);
}

static inline uint8_t msl_ucf_is_rim_coord(float stick_x_unit, float stick_y_unit) {
  // refs/ucf/include/util/melee/pad.h::is_rim_coord
  // - abs_coord_to_int(x) = (int)(abs(x)*80 - 0.0001f) + 1
  // - is_rim_coord adds +1 again per axis and compares length^2 > 80^2
  const float bias = 0.0001f;

  const float ax = msl_absf(stick_x_unit) * 80.0f - bias;
  const float ay = msl_absf(stick_y_unit) * 80.0f - bias;

  const int ix = (int)ax + 2;
  const int iy = (int)ay + 2;
  const int lsq = ix * ix + iy * iy;
  return lsq > (80 * 80) ? 1 : 0;
}

static inline int8_t msl_ucf_padbuf_get_raw_x(const MslStateSoA* s, size_t idx, uint8_t offset) {
  const uint8_t base = s->ucf_padbuf_index[idx];
  const uint8_t slot = (uint8_t)((base + offset) & (uint8_t)MSL_UCF_PADBUF_MASK);
  return s->ucf_padbuf_stick_x[idx * MSL_UCF_PADBUF_SIZE + (size_t)slot];
}

static inline int8_t msl_ucf_padbuf_get_raw_y(const MslStateSoA* s, size_t idx, uint8_t offset) {
  const uint8_t base = s->ucf_padbuf_index[idx];
  const uint8_t slot = (uint8_t)((base + offset) & (uint8_t)MSL_UCF_PADBUF_MASK);
  return s->ucf_padbuf_stick_y[idx * MSL_UCF_PADBUF_SIZE + (size_t)slot];
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

static inline uint8_t clamp_inc_u8_fe(uint8_t prev) {
  uint16_t t = (uint16_t)prev + 1u;
  if (t > 0xFEu) {
    t = 0xFEu;
  }
  return (uint8_t)t;
}

static inline uint8_t clamp_inc_u8_ff(uint8_t prev) {
  uint16_t t = (uint16_t)prev;
  if (t < 0xFFu) {
    t++;
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

      // -------------------------------
      // UCF pad buffer + "effective" stick
      // -------------------------------
      // Match UCF pad buffer write ordering:
      // refs/ucf/src/pad_buffer/pad_buffer.cpp
      // - buffer->index = (buffer->index + 1) & 3
      // - buffer->entries[buffer->index].stick = status.stick (raw PAD bytes)
      //
      // The simulator's gameplay logic should consume the resulting "effective stick",
      // i.e. Melee-legalized axes after any UCF preprocessing (cardinals snap + clamp).
      {
        uint8_t pb = batch->state.ucf_padbuf_index[idx];
        pb = (uint8_t)((pb + 1u) & (uint8_t)MSL_UCF_PADBUF_MASK);
        batch->state.ucf_padbuf_index[idx] = pb;
        batch->state.ucf_padbuf_stick_x[idx * MSL_UCF_PADBUF_SIZE + (size_t)pb] = cur->p[p].main_x;
        batch->state.ucf_padbuf_stick_y[idx * MSL_UCF_PADBUF_SIZE + (size_t)pb] = cur->p[p].main_y;
      }

      const int8_t raw_main_x = msl_ucf_padbuf_get_raw_x(&batch->state, idx, 0);
      const int8_t raw_main_y = msl_ucf_padbuf_get_raw_y(&batch->state, idx, 0);

      const MslStickI8 main = ucf_process_stick_i8(raw_main_x, raw_main_y, ucf_enabled, cardinals);
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

      const uint8_t tilt_timer_x_next =
          tilt_timer_update(batch->state.tilt_timer_x[idx], stick_x, prev_stick_x,
                            com->lstick_tilt_x_thresh);
      const uint8_t tilt_timer_y_next =
          tilt_timer_update(batch->state.tilt_timer_y[idx], stick_y, prev_stick_y,
                            com->lstick_tilt_y_thresh);

      batch->state.tilt_timer_x[idx] = tilt_timer_x_next;
      batch->state.tilt_timer_y[idx] = tilt_timer_y_next;

      // UCF sdrop-up helper counter (`sdrop_up_frames`) lives in the pad buffer shared state.
      // refs/ucf/src/pad_buffer/pad_buffer.cpp::{check_ucf_sdrop, check_sdrop_up}
      //
      // This is not used by the sim gameplay logic yet; we update it here to keep the
      // pad buffer state self-contained, seedable, and strictly causal.
      {
        // Must be -0.6125 or below along the rim, adjusted to prevent an ICs desync.
        // refs/ucf/src/pad_buffer/pad_buffer.cpp::check_sdrop_up
        const float sdrop_y_thresh = msl_ucf_popo_to_nana(-0.6125f);
        uint8_t sdrop = batch->state.ucf_padbuf_sdrop_up_frames[idx];

        if (stick_y > sdrop_y_thresh || !msl_ucf_is_rim_coord(stick_x, stick_y)) {
          sdrop = 0;
        } else if (sdrop != 0) {
          sdrop = (uint8_t)(sdrop + 1u);
        } else {
          // UCF gate: only check speed on first frame.
          //
          // Source tie-down:
          // - UCF reads `player->input.stick_y_hold_time` (u8) at offset 0x671:
          //   refs/ucf/include/melee/asm/player.h
          // - In decomp, the per-frame update for fp->x671_timer_lstick_tilt_y is:
          //   refs/melee/src/melee/ft/fighter.c:1963-2008
          //
          // Assumption: this sim's `tilt_timer_y` (x671-style timer) corresponds to UCF's
          // `stick_y_hold_time` closely enough for gating the sdrop-up helper.
          //
          // Ordering assumption to revisit if shielddrop behavior is off later:
          // UCF's pad-buffer injection applies 1.0 cardinals before check_sdrop_up
          // (refs/ucf/src/pad_buffer/pad_buffer.cpp), but we haven't proven whether Melee updates
          // `stick_y_hold_time` using pre- or post-injection stick values. We currently treat it
          // as a function of the post-UCF-processed stick (ucf_process_stick_i8 + deadzone),
          // consistent with the x671 input-history timer logic in fighter.c.
          //
          // This matches Melee's x671 input-history timer update (tilt_timer_update),
          // which counts consecutive frames beyond the tilt threshold.
          if (tilt_timer_y_next < 2) {
            const int8_t raw_prev2_y = msl_ucf_padbuf_get_raw_y(&batch->state, idx, (uint8_t)-2);
            const int dy = (int)raw_main_y - (int)raw_prev2_y;
            // refs/ucf/src/pad_buffer/pad_buffer.cpp::check_ucf_sdrop (44*44).
            const int dy_sq = dy * dy;
            if (dy_sq > (44 * 44)) {
              sdrop = 1;
            }
          }
        }

        batch->state.ucf_padbuf_sdrop_up_frames[idx] = sdrop;
      }

      batch->state.input_l[idx] = cur->p[p].l;
      batch->state.input_r[idx] = cur->p[p].r;

      // Update fighter input counters / input-history timers (single-writer invariant: input.c).
      // Decomp reference for the full block:
      // refs/melee/src/melee/ft/fighter.c:1897-2094 (and lb_8000D148 in refs/melee/src/melee/lb/lb_00CE.c:163-225).
      enum {
        A = (uint16_t)MSL_BUTTON_A,
        B = (uint16_t)MSL_BUTTON_B,
        XY = (uint16_t)MSL_BUTTON_XY,
        DPAD_UP = (uint16_t)MSL_BUTTON_D_UP,
        DPAD_DOWN = (uint16_t)MSL_BUTTON_D_DOWN,
        LR = (uint16_t)MSL_BUTTON_L | (uint16_t)MSL_BUTTON_R,
      };

      batch->state.lr_press_timer[idx] =
          press_timer_u8_update(batch->state.lr_press_timer[idx], batch->state.input_buttons_pressed[idx], LR);

      const float trig = trigger_unit_from_input(cur_buttons, cur->p[p].l, cur->p[p].r);
      const float prev_trig = trigger_unit_from_input(prev_buttons, prev->p[p].l, prev->p[p].r);
      batch->state.x672_input_timer[idx] = x672_trigger_timer_update(
          batch->state.x672_input_timer[idx], trig, prev_trig, com->powershield_reflect_trigger_min);

      // x676_x: increment (clamp to 0xFE) then reset on fresh directional entry.
      batch->state.x676_x[idx] = clamp_inc_u8_fe(batch->state.x676_x[idx]);
      if (stick_x >= com->lstick_tilt_x_thresh) {
        if (prev_stick_x >= com->lstick_tilt_x_thresh) {
          batch->state.x673[idx] = clamp_inc_u8_fe(batch->state.x673[idx]);
          batch->state.x679_x[idx] = clamp_inc_u8_fe(batch->state.x679_x[idx]);
        } else {
          batch->state.x676_x[idx] = 0;
          batch->state.x673[idx] = 0;
        }
      } else if (stick_x <= -com->lstick_tilt_x_thresh) {
        if (prev_stick_x <= -com->lstick_tilt_x_thresh) {
          batch->state.x673[idx] = clamp_inc_u8_fe(batch->state.x673[idx]);
          batch->state.x679_x[idx] = clamp_inc_u8_fe(batch->state.x679_x[idx]);
        } else {
          batch->state.x676_x[idx] = 0;
          batch->state.x673[idx] = 0;
        }
      } else {
        batch->state.x679_x[idx] = 0xFEu;
        batch->state.x673[idx] = 0xFEu;
      }

      // x677_y: increment (clamp to 0xFE) then reset on fresh directional entry.
      batch->state.x677_y[idx] = clamp_inc_u8_fe(batch->state.x677_y[idx]);
      if (stick_y >= com->lstick_tilt_y_thresh) {
        if (prev_stick_y >= com->lstick_tilt_y_thresh) {
          batch->state.x674[idx] = clamp_inc_u8_fe(batch->state.x674[idx]);
          batch->state.x67A_y[idx] = clamp_inc_u8_fe(batch->state.x67A_y[idx]);
        } else {
          batch->state.x677_y[idx] = 0;
          batch->state.x674[idx] = 0;
        }
      } else if (stick_y <= -com->lstick_tilt_y_thresh) {
        if (prev_stick_y <= -com->lstick_tilt_y_thresh) {
          batch->state.x674[idx] = clamp_inc_u8_fe(batch->state.x674[idx]);
          batch->state.x67A_y[idx] = clamp_inc_u8_fe(batch->state.x67A_y[idx]);
        } else {
          batch->state.x677_y[idx] = 0;
          batch->state.x674[idx] = 0;
        }
      } else {
        batch->state.x67A_y[idx] = 0xFEu;
        batch->state.x674[idx] = 0xFEu;
      }

      // lb_8000D148 zeroing for certain stick transitions.
      if (lb_8000D148(prev_stick_x, prev_stick_y, stick_x, stick_y, 0.0f, 0.0f,
                      com->lstick_tilt_x_thresh)) {
        batch->state.x67A_y[idx] = 0;
        batch->state.x679_x[idx] = 0;
      }

      // x678: increment (clamp to 0xFE) then reset on fresh trigger press.
      batch->state.x678[idx] = clamp_inc_u8_fe(batch->state.x678[idx]);
      if (trig >= com->powershield_reflect_trigger_min) {
        if (prev_trig >= com->powershield_reflect_trigger_min) {
          batch->state.x675[idx] = clamp_inc_u8_fe(batch->state.x675[idx]);
          batch->state.x67B[idx] = clamp_inc_u8_fe(batch->state.x67B[idx]);
        } else {
          batch->state.x67B[idx] = 0;
          batch->state.x678[idx] = 0;
          batch->state.x675[idx] = 0;
        }
      } else {
        batch->state.x67B[idx] = 0xFEu;
        batch->state.x675[idx] = 0xFEu;
      }

      // Button timers (saturating at 0xFF, reset to 0 on press).
      // Note: `input_buttons_pressed` is the per-frame rising-edge mask (decomp: fp->input.x668).
      const uint16_t pressed = batch->state.input_buttons_pressed[idx];
      if ((pressed & A) != 0) {
        batch->state.x683[idx] = batch->state.x67C[idx];
        batch->state.x67C[idx] = 0;
      } else {
        batch->state.x67C[idx] = clamp_inc_u8_ff(batch->state.x67C[idx]);
      }

      if ((pressed & B) != 0) {
        batch->state.x67D[idx] = 0;
      } else {
        batch->state.x67D[idx] = clamp_inc_u8_ff(batch->state.x67D[idx]);
      }

      if ((pressed & XY) != 0) {
        batch->state.x67E[idx] = 0;
      } else {
        batch->state.x67E[idx] = clamp_inc_u8_ff(batch->state.x67E[idx]);
      }

      if ((pressed & DPAD_UP) != 0) {
        batch->state.x681[idx] = 0;
      } else {
        batch->state.x681[idx] = clamp_inc_u8_ff(batch->state.x681[idx]);
      }

      if ((pressed & DPAD_DOWN) != 0) {
        batch->state.x682[idx] = 0;
      } else {
        batch->state.x682[idx] = clamp_inc_u8_ff(batch->state.x682[idx]);
      }

      if ((pressed & LR) != 0) {
        batch->state.x684[idx] = batch->state.x680[idx];
        batch->state.x680[idx] = 0;
      } else {
        batch->state.x680[idx] = clamp_inc_u8_ff(batch->state.x680[idx]);
      }
    }
  }

  return 0;
}
