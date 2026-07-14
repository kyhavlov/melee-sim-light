/* Native validation derivation for controller/input and guard prefix lanes. */

#include "msl_validation_input.h"
#include "msl_validation_history_common.h"

#include "../src/attack_id_tables.h"
#include "../src/common_params.h"
#include "../src/input_axis.h"
#include "../src/ucf.h"
#include "../src/msl_math.h"

static inline uint8_t vh_sat_inc_fe(uint8_t v) { return v < 0xFEu ? (uint8_t)(v + 1u) : 0xFEu; }

static inline uint8_t vh_sat_inc_ff(uint8_t v) { return v < 0xFFu ? (uint8_t)(v + 1u) : 0xFFu; }

static inline float vh_apply_deadzone(float v, float dz) {
  if (v > -dz && v < dz) return 0.0f;
  return v;
}

static inline bool vh_lb_8000d148(float prev_x, float prev_y, float cur_x, float cur_y,
                                  float center_x, float center_y, float threshold) {
  const float diff_01_y = prev_y - cur_y;
  const float diff_01_x = cur_x - prev_x;
  const float dist_squared_01 = diff_01_x * diff_01_x + diff_01_y * diff_01_y;
  if (dist_squared_01 < 0.00001f) return false;
  const float dist_01 = sqrtf(dist_squared_01);
  float var_f0 =
      ((prev_x * cur_y) - (prev_y * cur_x)) + ((diff_01_x * center_x) + (diff_01_y * center_y));
  if (var_f0 < 0.0f) var_f0 = -var_f0;
  if ((var_f0 / dist_01) > threshold) return false;
  const float diff_02_x = prev_x - center_x;
  const float diff_02_y = prev_y - center_y;
  const float diff_12_x = cur_x - center_x;
  const float diff_12_y = cur_y - center_y;
  const float threshold_squared = threshold * threshold;
  const float dist_squared_02 = diff_02_x * diff_02_x + diff_02_y * diff_02_y;
  const float dist_squared_12 = diff_12_x * diff_12_x + diff_12_y * diff_12_y;
  if (dist_squared_02 < threshold_squared) {
    if (dist_squared_12 > threshold_squared) return true;
    if (dist_squared_12 < threshold_squared) return false;
    return true;
  }
  if (dist_squared_02 > threshold_squared) {
    if (dist_squared_12 > threshold_squared) {
      if (((prev_x > center_x) && (cur_x < center_x)) ||
          ((prev_x < center_x) && (cur_x > center_x)) ||
          ((prev_y > center_y) && (cur_y < center_y)) ||
          ((prev_y < center_y) && (cur_y > center_y))) {
        return true;
      }
      return false;
    }
    if (dist_squared_12 < threshold_squared) return true;
    return true;
  }
  return true;
}

static inline float vh_ucf_popo_to_nana(float x) {
  if (x >= 0.0f) {
    return (float)((int8_t)(x * 127.0f)) / 127.0f;
  }
  return (float)((int8_t)(x * 128.0f)) / 128.0f;
}

static int vh_make_1d(npy_intp n, int typenum, PyArrayObject** out) {
  npy_intp dims[1] = {n};
  *out = (PyArrayObject*)PyArray_EMPTY(1, dims, typenum, 0);
  return *out == NULL ? -1 : 0;
}

PyObject* msl_validation_derive_guard_input_prefix_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject *seed_obj = NULL, *buttons_obj = NULL, *main_x_obj = NULL, *main_y_obj = NULL;
  PyObject *c_x_obj = NULL, *c_y_obj = NULL, *l_obj = NULL, *r_obj = NULL;
  PyObject *char_obj = NULL, *action_obj = NULL, *frame_obj = NULL, *facing_obj = NULL;
  PyObject *shield_obj = NULL, *hitlag_obj = NULL, *flags_obj = NULL;
  PyObject *neutral_obj = NULL, *frame_max_obj = NULL;
  int slot = 0;
  int ucf_enabled = 0;
  int cardinals = 0;
  double deadzone_x = 0.0, deadzone_y = 0.0, guard_lerp = 0.0;
  int button_mask_lr = 0, button_mask_z = 0;
  double trigger_deadzone = 0.0;
  int guard_x10_init = 0;
  int act_guard_on = 0, act_guard = 0, act_guard_off = 0, act_guard_reflect = 0;
  int act_guard_set_off = 0;
  int guard_special_enable_frames = 0;
  double hitlag_dmg_mul = 0.0, hitlag_base = 0.0;
  int powershield_reflect_frames = 0, powershield_reflect_total_frames = 0;
  if (!PyArg_ParseTuple(args, "OiOOOOOOOOOOOOOOOOiidddiidiiiiiiiddii", &seed_obj, &slot,
                        &buttons_obj, &main_x_obj, &main_y_obj, &c_x_obj, &c_y_obj, &l_obj, &r_obj,
                        &char_obj, &action_obj, &frame_obj, &facing_obj, &shield_obj, &hitlag_obj,
                        &flags_obj, &neutral_obj, &frame_max_obj, &ucf_enabled, &cardinals,
                        &deadzone_x, &deadzone_y, &guard_lerp, &button_mask_lr, &button_mask_z,
                        &trigger_deadzone, &guard_x10_init, &act_guard_on, &act_guard,
                        &act_guard_off, &act_guard_reflect, &act_guard_set_off,
                        &guard_special_enable_frames, &hitlag_dmg_mul, &hitlag_base,
                        &powershield_reflect_frames, &powershield_reflect_total_frames)) {
    return NULL;
  }

  PyArrayObject* seed_arr = vh_require(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* buttons_arr = vh_require(buttons_obj, NPY_UINT16, 1, "buttons");
  PyArrayObject* main_x_arr = vh_require(main_x_obj, NPY_INT8, 1, "main_x");
  PyArrayObject* main_y_arr = vh_require(main_y_obj, NPY_INT8, 1, "main_y");
  PyArrayObject* c_x_arr = vh_require(c_x_obj, NPY_INT8, 1, "c_x");
  PyArrayObject* c_y_arr = vh_require(c_y_obj, NPY_INT8, 1, "c_y");
  PyArrayObject* l_arr = vh_require(l_obj, NPY_UINT8, 1, "l");
  PyArrayObject* r_arr = vh_require(r_obj, NPY_UINT8, 1, "r");
  PyArrayObject* char_arr = vh_require(char_obj, NPY_UINT8, 1, "char_id");
  PyArrayObject* action_arr = vh_require(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* frame_arr = vh_require(frame_obj, NPY_INT16, 1, "action_frame");
  PyArrayObject* facing_arr = vh_require(facing_obj, NPY_UINT8, 1, "facing");
  PyArrayObject* shield_arr = vh_require(shield_obj, NPY_FLOAT32, 1, "shield_hp");
  PyArrayObject* hitlag_arr = vh_require(hitlag_obj, NPY_UINT16, 1, "hitlag");
  PyArrayObject* flags_arr = vh_require(flags_obj, NPY_UINT8, 2, "state_flags");
  PyArrayObject* neutral_arr = vh_require(neutral_obj, NPY_UINT16, 1, "neutral_frame");
  PyArrayObject* frame_max_arr = vh_require(frame_max_obj, NPY_UINT16, 1, "frame_max");
  if (seed_arr == NULL || buttons_arr == NULL || main_x_arr == NULL || main_y_arr == NULL ||
      c_x_arr == NULL || c_y_arr == NULL || l_arr == NULL || r_arr == NULL || char_arr == NULL ||
      action_arr == NULL || frame_arr == NULL || facing_arr == NULL || shield_arr == NULL ||
      hitlag_arr == NULL || flags_arr == NULL || neutral_arr == NULL || frame_max_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action_arr);
  const npy_intp n_samples = n - 1;
  if (n < 1) {
    PyErr_SetString(PyExc_ValueError, "action_id must contain at least one frame");
    return NULL;
  }
  if (slot < 0 || slot >= MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "slot out of range");
    return NULL;
  }
  if (vh_seed_rows(seed_arr, n_samples) != 0 || vh_check_len(buttons_arr, n, "buttons") != 0 ||
      vh_check_len(main_x_arr, n, "main_x") != 0 || vh_check_len(main_y_arr, n, "main_y") != 0 ||
      vh_check_len(c_x_arr, n, "c_x") != 0 || vh_check_len(c_y_arr, n, "c_y") != 0 ||
      vh_check_len(l_arr, n, "l") != 0 || vh_check_len(r_arr, n, "r") != 0 ||
      vh_check_len(char_arr, n, "char_id") != 0 ||
      vh_check_len(frame_arr, n, "action_frame") != 0 ||
      vh_check_len(facing_arr, n, "facing") != 0 || vh_check_len(shield_arr, n, "shield_hp") != 0 ||
      vh_check_len(hitlag_arr, n, "hitlag") != 0 ||
      vh_check_len(neutral_arr, n, "neutral_frame") != 0 ||
      vh_check_len(frame_max_arr, n, "frame_max") != 0) {
    return NULL;
  }
  if (PyArray_NDIM(flags_arr) != 2 || PyArray_DIM(flags_arr, 0) != n ||
      PyArray_DIM(flags_arr, 1) < 4) {
    PyErr_SetString(PyExc_ValueError, "state_flags must be uint8[n, >=4]");
    return NULL;
  }
  const float trigger_dz = (float)trigger_deadzone;
  const float trigger_denom = 1.0f - trigger_dz;
  if (!(trigger_denom > 0.0f)) {
    PyErr_SetString(PyExc_ValueError, "invalid trigger_deadzone");
    return NULL;
  }

  PyArrayObject *main_x_proc_arr = NULL, *main_y_proc_arr = NULL, *c_x_proc_arr = NULL;
  PyArrayObject *c_y_proc_arr = NULL, *stick_x_arr = NULL, *stick_y_arr = NULL,
                *cstick_y_arr = NULL;
  PyArrayObject *trigger_arr = NULL, *pressed_arr = NULL, *lr_timer_arr = NULL;
  PyArrayObject *light_arr = NULL, *guard_phase_arr = NULL;
  if (vh_make_1d(n, NPY_INT8, &main_x_proc_arr) != 0 ||
      vh_make_1d(n, NPY_INT8, &main_y_proc_arr) != 0 ||
      vh_make_1d(n, NPY_INT8, &c_x_proc_arr) != 0 || vh_make_1d(n, NPY_INT8, &c_y_proc_arr) != 0 ||
      vh_make_1d(n, NPY_FLOAT32, &stick_x_arr) != 0 ||
      vh_make_1d(n, NPY_FLOAT32, &stick_y_arr) != 0 ||
      vh_make_1d(n, NPY_FLOAT32, &cstick_y_arr) != 0 ||
      vh_make_1d(n, NPY_FLOAT32, &trigger_arr) != 0 ||
      vh_make_1d(n, NPY_UINT16, &pressed_arr) != 0 ||
      vh_make_1d(n, NPY_UINT8, &lr_timer_arr) != 0 || vh_make_1d(n, NPY_FLOAT32, &light_arr) != 0 ||
      vh_make_1d(n, NPY_UINT8, &guard_phase_arr) != 0) {
    Py_XDECREF(main_x_proc_arr);
    Py_XDECREF(main_y_proc_arr);
    Py_XDECREF(c_x_proc_arr);
    Py_XDECREF(c_y_proc_arr);
    Py_XDECREF(stick_x_arr);
    Py_XDECREF(stick_y_arr);
    Py_XDECREF(cstick_y_arr);
    Py_XDECREF(trigger_arr);
    Py_XDECREF(pressed_arr);
    Py_XDECREF(lr_timer_arr);
    Py_XDECREF(light_arr);
    Py_XDECREF(guard_phase_arr);
    return NULL;
  }

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const uint16_t* buttons = (const uint16_t*)PyArray_DATA(buttons_arr);
  const int8_t* main_x = (const int8_t*)PyArray_DATA(main_x_arr);
  const int8_t* main_y = (const int8_t*)PyArray_DATA(main_y_arr);
  const int8_t* c_x = (const int8_t*)PyArray_DATA(c_x_arr);
  const int8_t* c_y = (const int8_t*)PyArray_DATA(c_y_arr);
  const uint8_t* l = (const uint8_t*)PyArray_DATA(l_arr);
  const uint8_t* r = (const uint8_t*)PyArray_DATA(r_arr);
  const uint16_t* action = (const uint16_t*)PyArray_DATA(action_arr);
  const int16_t* frame = (const int16_t*)PyArray_DATA(frame_arr);
  const uint8_t* facing = (const uint8_t*)PyArray_DATA(facing_arr);
  const float* shield = (const float*)PyArray_DATA(shield_arr);
  const uint16_t* hitlag = (const uint16_t*)PyArray_DATA(hitlag_arr);
  const uint8_t* flags = (const uint8_t*)PyArray_DATA(flags_arr);
  const npy_intp flags_cols = PyArray_DIM(flags_arr, 1);
  const uint16_t* neutral = (const uint16_t*)PyArray_DATA(neutral_arr);
  const uint16_t* frame_max = (const uint16_t*)PyArray_DATA(frame_max_arr);
  int8_t* main_x_proc = (int8_t*)PyArray_DATA(main_x_proc_arr);
  int8_t* main_y_proc = (int8_t*)PyArray_DATA(main_y_proc_arr);
  int8_t* c_x_proc = (int8_t*)PyArray_DATA(c_x_proc_arr);
  int8_t* c_y_proc = (int8_t*)PyArray_DATA(c_y_proc_arr);
  float* stick_x = (float*)PyArray_DATA(stick_x_arr);
  float* stick_y = (float*)PyArray_DATA(stick_y_arr);
  float* cstick_y = (float*)PyArray_DATA(cstick_y_arr);
  float* trigger = (float*)PyArray_DATA(trigger_arr);
  uint16_t* pressed = (uint16_t*)PyArray_DATA(pressed_arr);
  uint8_t* lr_timer_out = (uint8_t*)PyArray_DATA(lr_timer_arr);
  float* light_out = (float*)PyArray_DATA(light_arr);
  uint8_t* guard_phase_out = (uint8_t*)PyArray_DATA(guard_phase_arr);

  const float fdz_x = (float)deadzone_x;
  const float fdz_y = (float)deadzone_y;
  const float lerp = (float)guard_lerp;
  const float rad_to_deg = MSL_RAD_TO_DEG_F;
  const uint16_t mask_lr = (uint16_t)((button_mask_lr | button_mask_z) & 0xFFFFu);
  int guard_init = guard_x10_init;
  if (guard_init < 0) guard_init = 0;
  if (guard_init > 255) guard_init = 255;
  const float hitlag_slope = (float)hitlag_dmg_mul;
  const float hitlag_base_f = (float)hitlag_base;
  int reflect_x14_init = powershield_reflect_frames + 1;
  int reflect_x18_init = powershield_reflect_total_frames + 1;
  if (reflect_x14_init < 0) reflect_x14_init = 0;
  if (reflect_x14_init > 255) reflect_x14_init = 255;
  if (reflect_x18_init < 0) reflect_x18_init = 0;
  if (reflect_x18_init > 255) reflect_x18_init = 255;

  uint16_t prev_buttons = 0u;
  uint16_t guard_x8 = 0u;
  float guard_x8_f = 0.0f;
  float guard_x4 = 0.0f;
  uint8_t guard_xc = 0u;
  int guard_x10 = 0;
  float light = 0.0f;
  uint8_t lr_timer = 0xFFu;
  bool prev_lr_held = false;
  bool lr_latched = false;
  int guard_x1c = 0;
  int guard_setoff_damage_min = 0;
  int reflect_x14 = 0;
  int reflect_x18 = 0;
  uint8_t reflect_origin = 0u;
  bool prev_in_reflect = false;

  for (npy_intp i = 0; i < n; i++) {
    const MslStickI8 mv = ucf_process_stick_i8(main_x[i], main_y[i], (uint8_t)(ucf_enabled != 0),
                                               (uint8_t)(cardinals != 0));
    const MslStickI8 cv = ucf_process_stick_i8(c_x[i], c_y[i], (uint8_t)(ucf_enabled != 0),
                                               (uint8_t)(cardinals != 0));
    main_x_proc[i] = mv.x;
    main_y_proc[i] = mv.y;
    c_x_proc[i] = cv.x;
    c_y_proc[i] = cv.y;
    stick_x[i] = vh_apply_deadzone(stick_i8_to_unit(mv.x), fdz_x);
    stick_y[i] = vh_apply_deadzone(stick_i8_to_unit(mv.y), fdz_y);
    cstick_y[i] = vh_apply_deadzone(stick_i8_to_unit(cv.y), fdz_y);
    pressed[i] = (uint16_t)(buttons[i] & (uint16_t)~prev_buttons);
    float trig = (float)(l[i] > r[i] ? l[i] : r[i]) / 255.0f;
    if ((buttons[i] & (uint16_t)button_mask_lr) != 0u) trig = 1.0f;
    trigger[i] = trig;

    const int a = (int)action[i];
    const int prev_a = i > 0 ? (int)action[i - 1] : a;
    const bool in_guard = a == act_guard_on || a == act_guard || a == act_guard_reflect;
    const bool prev_in_guard =
        prev_a == act_guard_on || prev_a == act_guard || prev_a == act_guard_reflect;
    const bool in_guard_setoff = a == act_guard_set_off;

    if (a == act_guard_on && frame[i] == 0) {
      guard_x8 = neutral[i];
      guard_x8_f = (float)neutral[i];
      guard_x4 = 0.0f;
    }
    if (in_guard) {
      const float facing_dir = facing[i] != 0u ? 1.0f : -1.0f;
      const float x = stick_x[i] * facing_dir;
      const float y = stick_y[i];
      float rad = msl_melee_lb_angle(y, x);
      if (rad < 0.0f) rad += 2.0f * 3.14159265358979323846f;
      float deg = rad * rad_to_deg;
      if (deg < 0.0f) deg = 0.0f;
      if (deg > 359.0f) deg = 359.0f;
      const float offset = guard_x8_f - (float)neutral[i];
      float delta = deg - offset;
      if (delta > 180.0f) {
        delta -= 360.0f;
      } else if (delta < -180.0f) {
        delta += 360.0f;
      }
      float next_offset = fmaf(lerp, delta, offset);
      if (next_offset > 360.0f) {
        next_offset -= 360.0f;
      } else if (next_offset < 0.0f) {
        next_offset += 360.0f;
      }
      const float next_x8_f = (float)neutral[i] + next_offset;
      guard_x8_f = next_x8_f;
      int next_x8 = (int)next_x8_f;
      if (next_x8 < 0) next_x8 = 0;
      if (next_x8 > (int)frame_max[i]) next_x8 = (int)frame_max[i];
      guard_x8 = (uint16_t)next_x8;
      float mag = msl_melee_sqrtf(stick_x[i] * stick_x[i] + stick_y[i] * stick_y[i]);
      if (mag > 1.0f) mag = 1.0f;
      if (mag < 0.0f) mag = 0.0f;
      guard_x4 = fmaf(lerp, mag - guard_x4, guard_x4);
    }

    if ((a == act_guard_on && prev_a != act_guard_on) ||
        (a == act_guard_reflect && prev_a != act_guard_reflect)) {
      guard_xc = 0u;
      guard_x10 = guard_init;
      light = 0.0f;
    }
    if (!in_guard && !in_guard_setoff) {
      guard_xc = 0u;
      guard_x10 = 0;
      light = 0.0f;
    } else if (in_guard && !prev_in_guard && a == act_guard && prev_a != act_guard_set_off) {
      guard_xc = 0u;
      guard_x10 = guard_init;
      light = 0.0f;
    } else if (in_guard_setoff) {
      if (prev_a != act_guard_set_off && !prev_in_guard && guard_x10 == 0) {
        guard_xc = 0u;
        guard_x10 = guard_init;
        const float t = (trig - trigger_dz) / trigger_denom;
        if (t >= 0.0f) light = t > 1.0f ? 1.0f : t;
      }
    } else {
      const int hl_prev = i > 0 ? (int)hitlag[i - 1] : 0;
      const int hl_after_prio0 = hl_prev > 0 ? hl_prev - 1 : 0;
      const bool held = ((buttons[i] & mask_lr) != 0u) || trig >= trigger_dz;
      if (hl_after_prio0 == 0 && shield[i] > 0.0f) {
        const float t = (trig - trigger_dz) / trigger_denom;
        if (t >= 0.0f) light = t > 1.0f ? 1.0f : t;
        if (guard_x10 > 0) guard_x10 -= 1;
        if (!held) guard_xc = 1u;
      }
    }

    const bool lr_held = ((buttons[i] & mask_lr) != 0u) || trig > trigger_dz;
    const bool lr_edge = lr_held && !prev_lr_held;
    if (hitlag[i] > 0u) {
      lr_latched = lr_latched || lr_edge;
    } else {
      lr_latched = lr_edge;
    }
    if (lr_latched) {
      lr_timer = 0u;
    } else if (lr_timer < 0xFFu) {
      lr_timer += 1u;
    }

    if ((a == act_guard_on && prev_a != act_guard_on) ||
        (a == act_guard_reflect && prev_a != act_guard_reflect)) {
      guard_x1c = 0;
    }
    if (a == act_guard_set_off && (flags[(i * flags_cols) + 3] & 0x20u) != 0u) {
      guard_x1c = guard_special_enable_frames;
      if (guard_x1c < 0) guard_x1c = 0;
      if (guard_x1c > 255) guard_x1c = 255;
    } else if (in_guard) {
      const int hl_prev = i > 0 ? (int)hitlag[i - 1] : 0;
      const int hl_after_prio0 = hl_prev > 0 ? hl_prev - 1 : 0;
      if (hl_after_prio0 == 0 && guard_x1c > 0) guard_x1c -= 1;
    } else if (a == act_guard_off) {
    } else if (a != act_guard_set_off) {
      guard_x1c = 0;
    }

    if (a != act_guard_set_off) {
      guard_setoff_damage_min = 0;
    } else {
      const int prev_af = i > 0 ? (int)frame[i - 1] : 0;
      const int prev_hl = i > 0 ? (int)hitlag[i - 1] : 0;
      const bool segment_entry = i == 0 || prev_a != act_guard_set_off || (int)frame[i] < prev_af ||
                                 (int)hitlag[i] > prev_hl;
      if (segment_entry && hitlag[i] > 0u) {
        guard_setoff_damage_min = 0xFF;
        for (int dmg = 1; dmg < 0xFF; dmg++) {
          const int result = (int)(((float)dmg * hitlag_slope) + hitlag_base_f);
          if (result >= (int)hitlag[i]) {
            guard_setoff_damage_min = dmg;
            break;
          }
        }
      }
    }
    uint8_t phase = 0u;
    if (a == act_guard_set_off) {
      const int cur_hl = (int)hitlag[i];
      const int prev_hl = i > 0 ? (int)hitlag[i - 1] : 0;
      if (cur_hl > 1) {
        phase = 1u;
      } else if (cur_hl == 1) {
        phase = 2u;
      } else if (prev_a == act_guard_set_off && prev_hl > 0) {
        phase = 3u;
      }
    }
    guard_phase_out[i] = phase;
    uint8_t post_hitlag_owner = 0u;
    if (a == act_guard_set_off) {
      if (phase == 2u || phase == 3u) {
        post_hitlag_owner = (flags[(i * flags_cols) + 3] & 0x20u) != 0u ? 2u : 1u;
      }
    }

    const bool in_reflect = a == act_guard_reflect;
    if (!in_reflect) {
      reflect_x14 = 0;
      reflect_x18 = 0;
      reflect_origin = 0u;
    } else if (!prev_in_reflect) {
      reflect_x14 = reflect_x14_init;
      reflect_x18 = reflect_x18_init;
      reflect_origin = (uint8_t)((prev_a == act_guard_on || prev_a == act_guard) ? 1u : 0u);
    } else {
      const int hl_prev = i > 0 ? (int)hitlag[i - 1] : 0;
      const int hl_after_prio0 = hl_prev > 0 ? hl_prev - 1 : 0;
      if (hl_after_prio0 == 0) {
        if (reflect_x14 > 0) reflect_x14 -= 1;
        if (reflect_x18 > 0) reflect_x18 -= 1;
      }
    }
    prev_in_reflect = in_reflect;

    if (i < n_samples) {
      MslSeed* seed = vh_seed_at(seed_u8, seed_stride, i);
      seed->guard_tilt_x8[slot] = guard_x8;
      seed->guard_tilt_x4[slot] = guard_x4;
      seed->guard_release_latched_xc[slot] = (uint8_t)(guard_xc ? 1u : 0u);
      seed->guard_x10[slot] = (uint8_t)(guard_x10 & 0xFF);
      seed->lightshield_amount[slot] = light;
      seed->guard_special_enable_timer_x1c[slot] = (uint8_t)(guard_x1c & 0xFF);
      seed->guard_setoff_hitlag_damage_min[slot] = (uint8_t)(guard_setoff_damage_min & 0xFF);
      seed->guard_setoff_hitlag_exit_phase_u8[slot] = phase;
      seed->guard_setoff_post_hitlag_owner_u8[slot] = post_hitlag_owner;
      seed->lr_press_timer[slot] = lr_timer;
      seed->guard_reflect_timer_x14[slot] = (uint8_t)(reflect_x14 & 0xFF);
      seed->guard_reflect_timer_x18[slot] = (uint8_t)(reflect_x18 & 0xFF);
      seed->guard_reflect_origin_guardon_u8[slot] = reflect_origin;
    }
    light_out[i] = light;
    lr_timer_out[i] = lr_timer;
    prev_buttons = buttons[i];
    prev_lr_held = lr_held;
  }

  return Py_BuildValue("NNNNNNNNNNNN", main_x_proc_arr, main_y_proc_arr, c_x_proc_arr, c_y_proc_arr,
                       stick_x_arr, stick_y_arr, cstick_y_arr, trigger_arr, pressed_arr,
                       lr_timer_arr, light_arr, guard_phase_arr);
}

PyObject* msl_validation_derive_input_history_suffix_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject *seed_obj = NULL, *buttons_obj = NULL, *raw_main_x_obj = NULL, *raw_main_y_obj = NULL;
  PyObject *stick_x_obj = NULL, *stick_y_obj = NULL, *cstick_y_obj = NULL, *trigger_obj = NULL;
  PyObject *pressed_obj = NULL, *action_obj = NULL;
  PyObject *frame_obj = NULL, *facing_obj = NULL, *hitlag_obj = NULL, *speed_y_obj = NULL;
  PyObject *ground_obj = NULL, *reset_obj = NULL, *turn_frames_obj = NULL;
  int slot = 0;
  double tilt_x = 0.0, tilt_y = 0.0, fastfall_stick = 0.0, tap_jump = 0.0;
  int fastfall_tilt_max = 0, tap_jump_tilt_max = 0;
  double dash_run_jump_y = 0.0, tap_release = 0.0, dash_flick_abs = 0.0;
  int dash_flick_tilt_max = 0;
  double trigger_min = 0.0;
  int button_mask_a = 0, button_mask_b = 0, button_mask_xy = 0, button_mask_du = 0;
  int button_mask_dd = 0, button_mask_lr = 0, button_mask_z = 0;
  int act_guard_reflect = 0, act_kneebend = 0, act_dash = 0, act_run = 0, act_run_direct = 0;
  int act_run_brake = 0, act_turn_run = 0, act_turn = 0;
  int act_jump_f = 0, act_jump_b = 0, act_jump_air_f = 0, act_jump_air_b = 0;
  int act_fall = 0, act_fall_f = 0, act_fall_b = 0, act_fall_aerial = 0, act_fall_aerial_f = 0;
  int act_fall_aerial_b = 0, act_fall_special = 0, act_fall_special_f = 0, act_fall_special_b = 0;
  int act_damage_fall = 0, act_attack_air_n = 0, act_attack_air_f = 0, act_attack_air_b = 0;
  int act_attack_air_hi = 0, act_attack_air_lw = 0, act_escape_air = 0;
  if (!PyArg_ParseTuple(
          args, "OiOOOOOOOOOOOOOOOdddididddiOdiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiiii", &seed_obj,
          &slot, &buttons_obj, &raw_main_x_obj, &raw_main_y_obj, &stick_x_obj, &stick_y_obj,
          &cstick_y_obj, &trigger_obj, &pressed_obj, &action_obj, &frame_obj, &facing_obj,
          &hitlag_obj, &speed_y_obj, &ground_obj, &reset_obj, &tilt_x, &tilt_y, &fastfall_stick,
          &fastfall_tilt_max, &tap_jump, &tap_jump_tilt_max, &dash_run_jump_y, &tap_release,
          &dash_flick_abs, &dash_flick_tilt_max, &turn_frames_obj, &trigger_min, &button_mask_a,
          &button_mask_b, &button_mask_xy, &button_mask_du, &button_mask_dd, &button_mask_lr,
          &button_mask_z, &act_guard_reflect, &act_kneebend, &act_dash, &act_run, &act_run_direct,
          &act_run_brake, &act_turn_run, &act_turn, &act_jump_f, &act_jump_b, &act_jump_air_f,
          &act_jump_air_b, &act_fall, &act_fall_f, &act_fall_b, &act_fall_aerial,
          &act_fall_aerial_f, &act_fall_aerial_b, &act_fall_special, &act_fall_special_f,
          &act_fall_special_b, &act_damage_fall, &act_attack_air_n, &act_attack_air_f,
          &act_attack_air_b, &act_attack_air_hi, &act_attack_air_lw, &act_escape_air)) {
    return NULL;
  }
  PyArrayObject* seed_arr = vh_require(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* buttons_arr = vh_require(buttons_obj, NPY_UINT16, 1, "buttons");
  PyArrayObject* raw_main_x_arr = vh_require(raw_main_x_obj, NPY_INT8, 1, "raw_main_x");
  PyArrayObject* raw_main_y_arr = vh_require(raw_main_y_obj, NPY_INT8, 1, "raw_main_y");
  PyArrayObject* sx_arr = vh_require(stick_x_obj, NPY_FLOAT32, 1, "stick_x");
  PyArrayObject* sy_arr = vh_require(stick_y_obj, NPY_FLOAT32, 1, "stick_y");
  PyArrayObject* cy_arr = vh_require(cstick_y_obj, NPY_FLOAT32, 1, "cstick_y");
  PyArrayObject* trig_arr = vh_require(trigger_obj, NPY_FLOAT32, 1, "trigger");
  PyArrayObject* pressed_arr = vh_require(pressed_obj, NPY_UINT16, 1, "buttons_pressed");
  PyArrayObject* action_arr = vh_require(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* frame_arr = vh_require(frame_obj, NPY_INT16, 1, "action_frame");
  PyArrayObject* facing_arr = vh_require(facing_obj, NPY_UINT8, 1, "facing");
  PyArrayObject* hitlag_arr = vh_require(hitlag_obj, NPY_UINT16, 1, "hitlag");
  PyArrayObject* speed_y_arr = vh_require(speed_y_obj, NPY_FLOAT32, 1, "speed_y");
  PyArrayObject* ground_arr = vh_require(ground_obj, NPY_UINT8, 1, "on_ground");
  PyArrayObject* reset_arr = vh_require(reset_obj, NPY_BOOL, 1, "reset_mask");
  PyArrayObject* turn_frames_arr = vh_require(turn_frames_obj, NPY_UINT8, 1, "turn_frames");
  if (seed_arr == NULL || buttons_arr == NULL || raw_main_x_arr == NULL || raw_main_y_arr == NULL ||
      sx_arr == NULL || sy_arr == NULL || cy_arr == NULL || trig_arr == NULL ||
      pressed_arr == NULL || action_arr == NULL || frame_arr == NULL || facing_arr == NULL ||
      hitlag_arr == NULL || speed_y_arr == NULL || ground_arr == NULL || reset_arr == NULL ||
      turn_frames_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action_arr);
  const npy_intp n_samples = n - 1;
  if (n < 1) {
    PyErr_SetString(PyExc_ValueError, "action_id must contain at least one frame");
    return NULL;
  }
  if (slot < 0 || slot >= MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "slot out of range");
    return NULL;
  }
  if (vh_seed_rows(seed_arr, n_samples) != 0 || vh_check_len(buttons_arr, n, "buttons") != 0 ||
      vh_check_len(raw_main_x_arr, n, "raw_main_x") != 0 ||
      vh_check_len(raw_main_y_arr, n, "raw_main_y") != 0 ||
      vh_check_len(sx_arr, n, "stick_x") != 0 || vh_check_len(sy_arr, n, "stick_y") != 0 ||
      vh_check_len(cy_arr, n, "cstick_y") != 0 || vh_check_len(trig_arr, n, "trigger") != 0 ||
      vh_check_len(pressed_arr, n, "buttons_pressed") != 0 ||
      vh_check_len(frame_arr, n, "action_frame") != 0 ||
      vh_check_len(facing_arr, n, "facing") != 0 || vh_check_len(hitlag_arr, n, "hitlag") != 0 ||
      vh_check_len(speed_y_arr, n, "speed_y") != 0 ||
      vh_check_len(ground_arr, n, "on_ground") != 0 ||
      vh_check_len(reset_arr, n, "reset_mask") != 0 ||
      vh_check_len(turn_frames_arr, n, "turn_frames") != 0) {
    return NULL;
  }
  PyArrayObject *tilt_y_pre_arr = NULL, *tilt_y_post_arr = NULL, *fall_fast_arr = NULL;
  PyArrayObject* turn_has_arr = NULL;
  if (vh_make_1d(n, NPY_UINT8, &tilt_y_pre_arr) != 0 ||
      vh_make_1d(n, NPY_UINT8, &tilt_y_post_arr) != 0 ||
      vh_make_1d(n, NPY_UINT8, &fall_fast_arr) != 0 ||
      vh_make_1d(n, NPY_UINT8, &turn_has_arr) != 0) {
    Py_XDECREF(tilt_y_pre_arr);
    Py_XDECREF(tilt_y_post_arr);
    Py_XDECREF(fall_fast_arr);
    Py_XDECREF(turn_has_arr);
    return NULL;
  }
  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const uint16_t* buttons = (const uint16_t*)PyArray_DATA(buttons_arr);
  const int8_t* raw_main_x = (const int8_t*)PyArray_DATA(raw_main_x_arr);
  const int8_t* raw_main_y = (const int8_t*)PyArray_DATA(raw_main_y_arr);
  const float* sx = (const float*)PyArray_DATA(sx_arr);
  const float* sy = (const float*)PyArray_DATA(sy_arr);
  const float* cy = (const float*)PyArray_DATA(cy_arr);
  const float* trig = (const float*)PyArray_DATA(trig_arr);
  const uint16_t* pressed = (const uint16_t*)PyArray_DATA(pressed_arr);
  const uint16_t* action = (const uint16_t*)PyArray_DATA(action_arr);
  const int16_t* frame = (const int16_t*)PyArray_DATA(frame_arr);
  const uint8_t* facing = (const uint8_t*)PyArray_DATA(facing_arr);
  const uint16_t* hitlag = (const uint16_t*)PyArray_DATA(hitlag_arr);
  const float* speed_y = (const float*)PyArray_DATA(speed_y_arr);
  const uint8_t* ground = (const uint8_t*)PyArray_DATA(ground_arr);
  const npy_bool* reset = (const npy_bool*)PyArray_DATA(reset_arr);
  const uint8_t* turn_frames = (const uint8_t*)PyArray_DATA(turn_frames_arr);
  uint8_t* tilt_y_pre_out = (uint8_t*)PyArray_DATA(tilt_y_pre_arr);
  uint8_t* tilt_y_post_out = (uint8_t*)PyArray_DATA(tilt_y_post_arr);
  uint8_t* fall_fast_out = (uint8_t*)PyArray_DATA(fall_fast_arr);
  uint8_t* turn_has_out = (uint8_t*)PyArray_DATA(turn_has_arr);

  float prev_x = 0.0f, prev_y = 0.0f, prev_trig = 0.0f;
  uint8_t tilt_x_post = 0xFEu, tilt_y_post = 0xFEu, fall_fast_prev = 0u;
  float vy_prev = 0.0f;
  uint8_t ground_prev = n > 0 ? ground[0] : 0u;
  uint8_t x673 = 0xFEu, x676 = 0xFEu, x2228 = 0u, x679 = 0xFEu;
  uint8_t x674 = 0xFEu, x677 = 0xFEu, x67A = 0xFEu;
  uint8_t x675 = 0xFEu, x67B = 0xFEu, x678 = 0xFEu;
  uint8_t x67C = 0xFFu, x67D = 0xFFu, x67E = 0xFFu, x680 = 0xFFu;
  uint8_t x681 = 0xFFu, x682 = 0xFFu, x683 = 0xFFu, x684 = 0xFFu;
  uint16_t x668_latched = 0u;
  uint8_t x672 = 0xFEu;
  uint8_t kb_jump = 0u, kb_short = 0u;
  bool prev_in_kb = false;
  int turn_to = 0, turn_x8 = 0;
  uint8_t turn_has = 0u;
  bool prev_in_turn = false;
  int8_t pad_x[4] = {0, 0, 0, 0};
  int8_t pad_y[4] = {0, 0, 0, 0};
  uint8_t pad_index = 0u, pad_sdrop = 0u;
  const float sdrop_y_thresh = vh_ucf_popo_to_nana(-0.6125f);
  const int sdrop_delta_sq_thresh = 44 * 44;

  for (npy_intp i = 0; i < n; i++) {
    const uint16_t a = action[i];
    const bool is_dash = (int)a == act_dash;
    const bool is_jump_ground = (int)a == act_jump_f || (int)a == act_jump_b;
    const bool is_jump_air = (int)a == act_jump_air_f || (int)a == act_jump_air_b;
    const bool is_jump = is_jump_ground || is_jump_air;
    const uint16_t prev_a = i > 0 ? action[i - 1] : action[i];
    const bool pre_input_jump_entry = is_jump_ground && a != prev_a;
    const bool jump_entry = is_jump_air && a != prev_a;
    const bool fastfall_ok =
        is_jump || (int)a == act_fall || (int)a == act_fall_f || (int)a == act_fall_b ||
        (int)a == act_fall_aerial || (int)a == act_fall_aerial_f || (int)a == act_fall_aerial_b ||
        (int)a == act_fall_special || (int)a == act_fall_special_f ||
        (int)a == act_fall_special_b || (int)a == act_damage_fall || (int)a == act_attack_air_n ||
        (int)a == act_attack_air_f || (int)a == act_attack_air_b || (int)a == act_attack_air_hi ||
        (int)a == act_attack_air_lw || (int)a == act_escape_air;

    uint8_t tx_pre = tilt_x_post;
    if (sx[i] >= (float)tilt_x) {
      tx_pre = prev_x >= (float)tilt_x ? vh_sat_inc_fe(tx_pre) : 0u;
    } else if (sx[i] <= -(float)tilt_x) {
      tx_pre = prev_x <= -(float)tilt_x ? vh_sat_inc_fe(tx_pre) : 0u;
    } else {
      tx_pre = 0xFEu;
    }
    tilt_x_post = tx_pre;
    if (is_dash && (i == 0 || action[i - 1] != action[i])) tilt_x_post = 0xFEu;
    if (reset[i]) tilt_x_post = 0xFEu;

    uint8_t ty_pre = tilt_y_post;
    if (sy[i] >= (float)tilt_y) {
      ty_pre = prev_y >= (float)tilt_y ? vh_sat_inc_fe(ty_pre) : 0u;
    } else if (sy[i] <= -(float)tilt_y) {
      ty_pre = prev_y <= -(float)tilt_y ? vh_sat_inc_fe(ty_pre) : 0u;
    } else {
      ty_pre = 0xFEu;
    }
    uint8_t ff_start = fall_fast_prev;
    uint8_t ty_post = ty_pre;
    if (pre_input_jump_entry) {
      ff_start = 0u;
      if (sy[i] >= (float)tilt_y) {
        ty_post = prev_y >= (float)tilt_y ? 0xFEu : 0u;
      } else if (sy[i] <= -(float)tilt_y) {
        ty_post = prev_y <= -(float)tilt_y ? 0xFEu : 0u;
      } else {
        ty_post = 0xFEu;
      }
    }
    if (jump_entry) {
      ty_post = 0xFEu;
      ff_start = 0u;
    }
    uint8_t ff_after = ff_start;
    if (!ground_prev && fastfall_ok && hitlag[i] <= 1u) {
      if (!ff_start && vy_prev < 0.0f && sy[i] <= -(float)fastfall_stick &&
          (int)ty_post < fastfall_tilt_max) {
        ff_after = 1u;
        ty_post = 0xFEu;
      }
    }
    if (reset[i]) ty_post = 0xFEu;
    const uint8_t ff_post = ground[i] ? 0u : ff_after;
    tilt_y_pre_out[i] = ty_pre;
    tilt_y_post_out[i] = ty_post;
    fall_fast_out[i] = ff_post;

    pad_index = (uint8_t)((pad_index + 1u) & 3u);
    pad_x[pad_index] = raw_main_x[i];
    pad_y[pad_index] = raw_main_y[i];
    const int ix = (int)truncf(fabsf(sx[i]) * 80.0f - 0.0001f) + 2;
    const int iy = (int)truncf(fabsf(sy[i]) * 80.0f - 0.0001f) + 2;
    const bool is_rim = (ix * ix + iy * iy) > 80 * 80;
    const int16_t prev2_y = (i >= 2) ? (int16_t)pad_y[(pad_index + 2u) & 3u] : 0;
    const int dy = (int)((int16_t)pad_y[pad_index] - prev2_y);
    if (sy[i] > sdrop_y_thresh || !is_rim) {
      pad_sdrop = 0u;
    } else if (pad_sdrop != 0u) {
      pad_sdrop = (uint8_t)(pad_sdrop + 1u);
    } else if (ty_pre < 2u && dy * dy > sdrop_delta_sq_thresh) {
      pad_sdrop = 1u;
    } else {
      pad_sdrop = 0u;
    }

    x676 = vh_sat_inc_fe(x676);
    if (sx[i] >= (float)tilt_x) {
      if (prev_x >= (float)tilt_x) {
        x673 = vh_sat_inc_fe(x673);
        x679 = vh_sat_inc_fe(x679);
      } else {
        x676 = x673 = 0u;
        x2228 = 1u;
      }
    } else if (sx[i] <= -(float)tilt_x) {
      if (prev_x <= -(float)tilt_x) {
        x673 = vh_sat_inc_fe(x673);
        x679 = vh_sat_inc_fe(x679);
      } else {
        x676 = x673 = 0u;
        x2228 = 0u;
      }
    } else {
      x679 = x673 = 0xFEu;
    }
    x677 = vh_sat_inc_fe(x677);
    if (sy[i] >= (float)tilt_y) {
      if (prev_y >= (float)tilt_y) {
        x674 = vh_sat_inc_fe(x674);
        x67A = vh_sat_inc_fe(x67A);
      } else {
        x677 = x674 = 0u;
      }
    } else if (sy[i] <= -(float)tilt_y) {
      if (prev_y <= -(float)tilt_y) {
        x674 = vh_sat_inc_fe(x674);
        x67A = vh_sat_inc_fe(x67A);
      } else {
        x677 = x674 = 0u;
      }
    } else {
      x67A = x674 = 0xFEu;
    }
    if (vh_lb_8000d148(prev_x, prev_y, sx[i], sy[i], 0.0f, 0.0f, (float)tilt_x)) {
      x67A = 0u;
      x679 = 0u;
    }
    x678 = vh_sat_inc_fe(x678);
    if (trig[i] >= (float)trigger_min) {
      if (prev_trig >= (float)trigger_min) {
        x675 = vh_sat_inc_fe(x675);
        x67B = vh_sat_inc_fe(x67B);
      } else {
        x67B = x678 = x675 = 0u;
      }
    } else {
      x67B = x675 = 0xFEu;
    }
    uint16_t raw = pressed[i];
    if ((raw & (uint16_t)button_mask_z) != 0u) raw = (uint16_t)(raw | (uint16_t)button_mask_a);
    uint16_t bpi = raw;
    if (hitlag[i] > 0u) {
      x668_latched = (uint16_t)(x668_latched | raw);
      bpi = x668_latched;
    } else {
      x668_latched = 0u;
    }
    if ((bpi & (uint16_t)button_mask_a) != 0u) {
      x683 = x67C;
      x67C = 0u;
    } else {
      x67C = vh_sat_inc_ff(x67C);
    }
    if ((bpi & (uint16_t)button_mask_b) != 0u)
      x67D = 0u;
    else
      x67D = vh_sat_inc_ff(x67D);
    if ((bpi & (uint16_t)button_mask_xy) != 0u)
      x67E = 0u;
    else
      x67E = vh_sat_inc_ff(x67E);
    if ((bpi & (uint16_t)button_mask_du) != 0u)
      x681 = 0u;
    else
      x681 = vh_sat_inc_ff(x681);
    if ((bpi & (uint16_t)button_mask_dd) != 0u)
      x682 = 0u;
    else
      x682 = vh_sat_inc_ff(x682);
    if ((bpi & (uint16_t)button_mask_lr) != 0u) {
      x684 = x680;
      x680 = 0u;
    } else {
      x680 = vh_sat_inc_ff(x680);
    }
    uint8_t x672_pre = x672;
    if (trig[i] >= (float)trigger_min) {
      x672_pre = prev_trig >= (float)trigger_min ? vh_sat_inc_fe(x672_pre) : 0u;
    } else {
      x672_pre = 0xFEu;
    }
    x672 = ((int)a == act_guard_reflect && (i == 0 || (int)prev_a != act_guard_reflect)) ? 0xFEu
                                                                                         : x672_pre;

    const bool cur_kb = (int)a == act_kneebend;
    if (!cur_kb) {
      kb_jump = 0u;
      kb_short = 0u;
      prev_in_kb = false;
    } else {
      if (!prev_in_kb) {
        const bool dash_src = (int)prev_a == act_dash || (int)prev_a == act_run ||
                              (int)prev_a == act_run_direct || (int)prev_a == act_run_brake ||
                              (int)prev_a == act_turn_run;
        kb_jump = 0u;
        if (dash_src) {
          if ((pressed[i] & (uint16_t)button_mask_xy) != 0u)
            kb_jump = 3u;
          else if (sy[i] >= (float)dash_run_jump_y && (int)ty_pre < tap_jump_tilt_max)
            kb_jump = 1u;
          else if (cy[i] >= (float)tap_jump)
            kb_jump = 2u;
        } else {
          if (sy[i] >= (float)tap_jump && (int)ty_pre < tap_jump_tilt_max)
            kb_jump = 1u;
          else if ((pressed[i] & (uint16_t)button_mask_xy) != 0u)
            kb_jump = 3u;
          else if (cy[i] >= (float)tap_jump)
            kb_jump = 2u;
        }
        kb_short = 0u;
      }
      if (!kb_short) {
        if (kb_jump == 3u) {
          if ((buttons[i] & (uint16_t)button_mask_xy) == 0u) kb_short = 1u;
        } else if (kb_jump == 1u) {
          if (sy[i] < (float)tap_release) kb_short = 1u;
        } else if (kb_jump == 2u) {
          if (cy[i] < (float)tap_release) kb_short = 1u;
        }
      }
      prev_in_kb = true;
    }

    const bool cur_turn = (int)a == act_turn;
    if (!cur_turn) {
      turn_to = 0;
      turn_has = 0u;
      turn_x8 = 0;
      prev_in_turn = false;
    } else {
      const bool restart = prev_in_turn && i > 0 && frame[i] < frame[i - 1];
      if (!prev_in_turn || restart) {
        bool is_smash = false;
        float facing_dir = 1.0f;
        if (i > 0) {
          facing_dir = facing[i - 1] != 0u ? 1.0f : -1.0f;
          if (fabsf(sx[i]) >= (float)dash_flick_abs && (int)tilt_x_post < dash_flick_tilt_max &&
              sx[i] * facing_dir < 0.0f) {
            is_smash = true;
          }
        }
        turn_to = is_smash ? 0 : (int)turn_frames[i];
        if (turn_to < 0) turn_to = 0;
        if (turn_to > 0xFE) turn_to = 0xFE;
        turn_has = 0u;
        turn_x8 = is_smash ? (facing_dir > 0.0f ? 1 : -1) : 0;
      } else {
        if (turn_to > 0) {
          turn_to -= 1;
        } else if (!turn_has) {
          turn_has = 1u;
        }
        const float facing_dir_i = facing[i] != 0u ? 1.0f : -1.0f;
        const float facing_after = turn_has ? facing_dir_i : -facing_dir_i;
        if (sx[i] * facing_after >= (float)dash_flick_abs &&
            (int)tilt_x_post < dash_flick_tilt_max) {
          turn_x8 = facing_after > 0.0f ? 1 : -1;
        }
      }
      prev_in_turn = true;
    }
    turn_has_out[i] = turn_has;

    if (i < n_samples) {
      MslSeed* seed = vh_seed_at(seed_u8, seed_stride, i);
      seed->tilt_timer_x[slot] = tilt_x_post;
      seed->tilt_timer_y[slot] = ty_post;
      seed->fall_fast[slot] = ff_post;
      seed->fall_fast_hitlag_exit_owner[slot] =
          (uint8_t)((hitlag[i] == 1u && fastfall_ok && ff_post != 0u) ? 1u : 0u);
      seed->ucf_padbuf_index[slot] = pad_index;
      seed->ucf_padbuf_sdrop_up_frames[slot] = pad_sdrop;
      for (int k = 0; k < 4; k++) {
        seed->ucf_padbuf_stick_x[slot][k] = pad_x[k];
        seed->ucf_padbuf_stick_y[slot][k] = pad_y[k];
      }
      seed->x672_input_timer[slot] = x672;
      seed->x673[slot] = x673;
      seed->x674[slot] = x674;
      seed->x675[slot] = x675;
      seed->x676_x[slot] = x676;
      seed->x2228_b7[slot] = x2228;
      seed->x677_y[slot] = x677;
      seed->x678[slot] = x678;
      seed->x679_x[slot] = x679;
      seed->x67A_y[slot] = x67A;
      seed->x67B[slot] = x67B;
      seed->x67C[slot] = x67C;
      seed->x67D[slot] = x67D;
      seed->x67E[slot] = x67E;
      seed->x680[slot] = x680;
      seed->x681[slot] = x681;
      seed->x682[slot] = x682;
      seed->x683[slot] = x683;
      seed->x684[slot] = x684;
      seed->kneebend_jump_input[slot] = kb_jump;
      seed->kneebend_is_short_hop[slot] = kb_short;
      seed->turn_frames_to_turn[slot] = (uint8_t)(turn_to & 0xFF);
      seed->turn_has_turned[slot] = turn_has;
      seed->turn_x8[slot] = (int8_t)turn_x8;
    }
    prev_x = sx[i];
    prev_y = sy[i];
    prev_trig = trig[i];
    tilt_y_post = ty_post;
    fall_fast_prev = ff_post;
    vy_prev = speed_y[i];
    ground_prev = ground[i];
  }
  return Py_BuildValue("NNNN", tilt_y_pre_arr, tilt_y_post_arr, fall_fast_arr, turn_has_arr);
}

/* Preprocess entrypoints owned by this validation family. */

static inline uint8_t msl_py_u8_sat_inc_fe(uint8_t v) {
  return v < 0xFEu ? (uint8_t)(v + 1u) : 0xFEu;
}
static inline uint8_t msl_py_u8_sat_inc_ff(uint8_t v) {
  return v < 0xFFu ? (uint8_t)(v + 1u) : 0xFFu;
}

static inline uint8_t msl_py_lb_8000D148(float point0_x, float point0_y, float point1_x,
                                         float point1_y, float point2_x, float point2_y,
                                         float threshold) {
  const float diff_01_y = point0_y - point1_y;
  const float diff_01_x = point1_x - point0_x;
  const float dist_squared_01 = diff_01_x * diff_01_x + diff_01_y * diff_01_y;
  if (dist_squared_01 < 0.00001f) {
    return 0u;
  }
  const float dist_01 = sqrtf(dist_squared_01);
  float var_f0 = ((point0_x * point1_y) - (point0_y * point1_x)) +
                 ((diff_01_x * point2_x) + (diff_01_y * point2_y));
  if (var_f0 < 0.0f) {
    var_f0 = -var_f0;
  }
  const float thr = threshold;
  if ((var_f0 / dist_01) <= thr) {
    const float diff_02_x = point0_x - point2_x;
    const float diff_02_y = point0_y - point2_y;
    const float diff_12_x = point1_x - point2_x;
    const float diff_12_y = point1_y - point2_y;
    const float threshold_squared = thr * thr;
    const float dist_squared_02 = diff_02_x * diff_02_x + diff_02_y * diff_02_y;
    const float dist_squared_12 = diff_12_x * diff_12_x + diff_12_y * diff_12_y;
    if (dist_squared_02 < threshold_squared) {
      if (dist_squared_12 > threshold_squared) {
        return 1u;
      }
      if (dist_squared_12 < threshold_squared) {
        return 0u;
      }
      return 1u;
    }
    if (dist_squared_02 > threshold_squared) {
      if (dist_squared_12 > threshold_squared) {
        if (((point0_x > point2_x) && (point1_x < point2_x)) ||
            ((point0_x < point2_x) && (point1_x > point2_x)) ||
            ((point0_y > point2_y) && (point1_y < point2_y)) ||
            ((point0_y < point2_y) && (point1_y > point2_y))) {
          return 1u;
        }
        return 0u;
      }
      if (dist_squared_12 < threshold_squared) {
        return 1u;
      }
      return 1u;
    }
    return 1u;
  }
  return 0u;
}

PyObject* msl_derive_combo_seed_fields_py(PyObject* self, PyObject* args) {
  (void)self;
  int num_players = 0;
  PyObject* src_ports_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* state_flags_obj = NULL;
  PyObject* instance_id_obj = NULL;
  PyObject* last_hit_by_obj = NULL;
  PyObject* instance_hit_by_obj = Py_None;
  if (!PyArg_ParseTuple(args, "iOOOOO|O", &num_players, &src_ports_obj, &hitlag_obj,
                        &state_flags_obj, &instance_id_obj, &last_hit_by_obj,
                        &instance_hit_by_obj)) {
    return NULL;
  }
  if (num_players != 2 && num_players != 4) {
    PyErr_SetString(PyExc_ValueError, "num_players must be 2 or 4");
    return NULL;
  }
  if (common_params_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "common_params_init failed");
    return NULL;
  }
  const MslCommonParams* common = msl_common_params();
  if (common == NULL) {
    PyErr_SetString(PyExc_RuntimeError, "common params unavailable");
    return NULL;
  }

  PyObject* src_seq = PySequence_Fast(src_ports_obj, "src_ports must be a sequence");
  if (src_seq == NULL) {
    return NULL;
  }
  if (PySequence_Fast_GET_SIZE(src_seq) != num_players) {
    PyErr_Format(PyExc_ValueError, "src_ports length must equal num_players (%d)", num_players);
    Py_DECREF(src_seq);
    return NULL;
  }
  int slot_by_port0[4] = {-1, -1, -1, -1};
  for (int i = 0; i < num_players; i++) {
    PyObject* item = PySequence_Fast_GET_ITEM(src_seq, i);
    const long port1 = PyLong_AsLong(item);
    if (PyErr_Occurred()) {
      Py_DECREF(src_seq);
      return NULL;
    }
    if (port1 < 1 || port1 > 4) {
      PyErr_SetString(PyExc_ValueError, "src_ports must be in 1..4");
      Py_DECREF(src_seq);
      return NULL;
    }
    slot_by_port0[(int)port1 - 1] = i;
  }
  Py_DECREF(src_seq);

  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag");
  PyArrayObject* state_flags =
      require_contiguous_array(state_flags_obj, NPY_UINT8, 3, "state_flags");
  PyArrayObject* instance_id =
      require_contiguous_array(instance_id_obj, NPY_UINT16, 2, "instance_id");
  PyArrayObject* last_hit_by =
      require_contiguous_array(last_hit_by_obj, NPY_UINT8, 2, "last_hit_by");
  PyArrayObject* instance_hit_by = NULL;
  if (instance_hit_by_obj != Py_None) {
    instance_hit_by =
        require_contiguous_array(instance_hit_by_obj, NPY_UINT16, 2, "instance_hit_by");
  }
  if (hitlag == NULL || state_flags == NULL || instance_id == NULL || last_hit_by == NULL ||
      (instance_hit_by_obj != Py_None && instance_hit_by == NULL)) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(hitlag, 0);
  const npy_intp width = PyArray_DIM(hitlag, 1);
  if (width < num_players || width > (npy_intp)MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError,
                    "hitlag width must cover num_players and fit MSL_MAX_PLAYERS");
    return NULL;
  }
  if (require_exact_2d_shape(instance_id, n, width, "instance_id") != 0 ||
      require_exact_2d_shape(last_hit_by, n, width, "last_hit_by") != 0 ||
      (instance_hit_by != NULL &&
       require_exact_2d_shape(instance_hit_by, n, width, "instance_hit_by") != 0)) {
    return NULL;
  }
  if (PyArray_NDIM(state_flags) != 3 || PyArray_DIM(state_flags, 0) != n ||
      PyArray_DIM(state_flags, 1) != width || PyArray_DIM(state_flags, 2) < 4) {
    PyErr_Format(
        PyExc_ValueError,
        "state_flags must have shape [frames, players, >=4] matching hitlag; got "
        "[%zd,%zd,%zd], expected [%zd,%zd,>=4]",
        PyArray_NDIM(state_flags) >= 1 ? (Py_ssize_t)PyArray_DIM(state_flags, 0) : (Py_ssize_t)-1,
        PyArray_NDIM(state_flags) >= 2 ? (Py_ssize_t)PyArray_DIM(state_flags, 1) : (Py_ssize_t)-1,
        PyArray_NDIM(state_flags) >= 3 ? (Py_ssize_t)PyArray_DIM(state_flags, 2) : (Py_ssize_t)-1,
        (Py_ssize_t)n, (Py_ssize_t)width);
    return NULL;
  }

  npy_intp dims2[2] = {n, (npy_intp)MSL_MAX_PLAYERS};
  PyArrayObject* out_victim_port = (PyArrayObject*)PyArray_EMPTY(2, dims2, NPY_UINT8, 0);
  PyArrayObject* out_victim_iid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT16, 0);
  PyArrayObject* out_timer = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT16, 0);
  if (out_victim_port == NULL || out_victim_iid == NULL || out_timer == NULL) {
    Py_XDECREF(out_victim_port);
    Py_XDECREF(out_victim_iid);
    Py_XDECREF(out_timer);
    return NULL;
  }
  memset(PyArray_DATA(out_victim_port), 0xFF, (size_t)(n * (npy_intp)MSL_MAX_PLAYERS));

  const uint16_t* hitlag_p = (const uint16_t*)PyArray_DATA(hitlag);
  const uint8_t* flags_p = (const uint8_t*)PyArray_DATA(state_flags);
  const uint16_t* iid_p = (const uint16_t*)PyArray_DATA(instance_id);
  const uint8_t* last_hit_p = (const uint8_t*)PyArray_DATA(last_hit_by);
  const uint16_t* hit_iid_p =
      instance_hit_by != NULL ? (const uint16_t*)PyArray_DATA(instance_hit_by) : NULL;
  uint8_t* out_port_p = (uint8_t*)PyArray_DATA(out_victim_port);
  uint16_t* out_iid_p = (uint16_t*)PyArray_DATA(out_victim_iid);
  uint16_t* out_timer_p = (uint16_t*)PyArray_DATA(out_timer);

  uint8_t combo_victim_port[MSL_MAX_PLAYERS];
  uint16_t combo_victim_iid[MSL_MAX_PLAYERS] = {0};
  uint16_t combo_timer[MSL_MAX_PLAYERS] = {0};
  uint16_t prev_hitlag[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_in_hitstun[MSL_MAX_PLAYERS] = {0};
  for (int i = 0; i < MSL_MAX_PLAYERS; i++) {
    combo_victim_port[i] = 0xFFu;
  }

  const uint16_t reset_frames = common->combo_timer_post_hitstun_frames;
  const uint8_t hitstun_mask_221c = 0x02u;
  for (npy_intp fi = 0; fi < n; fi++) {
    for (int p = 0; p < num_players; p++) {
      const npy_intp pi = fi * width + p;
      if (hitlag_p[pi] == 0u) {
        if (combo_timer[p] != 0u) {
          combo_timer[p] = (uint16_t)(combo_timer[p] - 1u);
        }
        const uint8_t v = combo_victim_port[p];
        if (v == 0xFFu) {
          continue;
        }
        if (v >= (uint8_t)num_players) {
          combo_victim_port[p] = 0xFFu;
          combo_victim_iid[p] = 0u;
          continue;
        }
        const uint8_t v_in_hitstun =
            (flags_p[(fi * width + (npy_intp)v) * PyArray_DIM(state_flags, 2) + 3] &
             hitstun_mask_221c) != 0u;
        // Source order bridge for hidden fp->x2094/x2098 derivation:
        // Fighter_8006A360 calls ftColl_800764DC before the Damage callback clears x221C_b6 and
        // writes `victim->x2098 = p_ftCommonData->x4CC`. A post-frame row where hitstun just ended
        // therefore still owns the attacker x2094 victim pointer even though the replay-visible
        // hitstun bit is now clear.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        // refs/melee/src/melee/ft/ftcoll.c::ftColl_800764DC
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Damage.c::ftCo_8008F744
        const uint8_t v_ended_hitstun_this_row = (prev_in_hitstun[v] && !v_in_hitstun) ? 1u : 0u;
        if (!v_in_hitstun && combo_timer[v] == 0u && !v_ended_hitstun_this_row) {
          combo_victim_port[p] = 0xFFu;
          combo_victim_iid[p] = 0u;
        }
      }
    }
    for (int p = 0; p < num_players; p++) {
      const uint8_t in_hitstun =
          (flags_p[(fi * width + p) * PyArray_DIM(state_flags, 2) + 3] & hitstun_mask_221c) != 0u;
      if (prev_in_hitstun[p] && !in_hitstun) {
        combo_timer[p] = reset_frames;
      }
      prev_in_hitstun[p] = in_hitstun;
    }

    for (int v = 0; v < num_players; v++) {
      const npy_intp vi = fi * width + v;
      const uint16_t hl_prev = prev_hitlag[v];
      const uint16_t hl_cur = hitlag_p[vi];
      if (hl_prev == 0u && hl_cur > 0u) {
        int attacker = -1;
        const uint8_t port0 = last_hit_p[vi];
        if (port0 < 4u) {
          attacker = slot_by_port0[port0];
        }
        if (attacker < 0 && hit_iid_p != NULL) {
          const uint16_t hit_iid = hit_iid_p[vi];
          if (hit_iid != 0u) {
            int match = -1;
            int match_count = 0;
            for (int p = 0; p < num_players; p++) {
              if (iid_p[fi * width + p] == hit_iid) {
                match = p;
                match_count++;
              }
            }
            if (match_count == 1) {
              attacker = match;
            }
          }
        }
        if (attacker >= 0 && attacker != v && combo_victim_port[attacker] == 0xFFu) {
          combo_victim_port[attacker] = (uint8_t)v;
          combo_victim_iid[attacker] = iid_p[vi];
        }
      }
    }

    for (int p = 0; p < num_players; p++) {
      prev_hitlag[p] = hitlag_p[fi * width + p];
    }

    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      const npy_intp oi = fi * (npy_intp)MSL_MAX_PLAYERS + p;
      out_port_p[oi] = combo_victim_port[p];
      out_iid_p[oi] = combo_victim_iid[p];
      out_timer_p[oi] = combo_timer[p];
    }
  }

  return Py_BuildValue("NNN", out_victim_port, out_victim_iid, out_timer);
}

PyObject* msl_derive_instance_id_x2073_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* action_frame_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &char_obj, &action_obj, &action_frame_obj)) {
    return NULL;
  }
  PyArrayObject* char_id = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action_id = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* action_frame =
      require_contiguous_array(action_frame_obj, NPY_INT16, 1, "action_frame_i16");
  if (char_id == NULL || action_id == NULL || action_frame == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action_id);
  if (PyArray_SIZE(char_id) != n || PyArray_SIZE(action_frame) != n) {
    PyErr_SetString(PyExc_ValueError,
                    "char_id_u8/action_id_u16/action_frame_i16 must have the same length");
    return NULL;
  }
  if (attack_id_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "attack_id_tables_init failed");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out == NULL) {
    return NULL;
  }
  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_id);
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action_id);
  const int16_t* frame_p = (const int16_t*)PyArray_DATA(action_frame);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  uint8_t x2073 = 0u;
  for (npy_intp i = 0; i < n; i++) {
    uint8_t entry = (i == 0) ? 1u : 0u;
    if (i > 0) {
      if (action_p[i] != action_p[i - 1] || frame_p[i] < frame_p[i - 1]) {
        entry = 1u;
      }
    }
    if (entry) {
      x2073 = (uint8_t)(attack_id_x4_flags_from_action(char_p[i], action_p[i]) & 0xFFu);
    }
    out_p[i] = x2073;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_instance_id_counter_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* fighter_obj = NULL;
  PyObject* item_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &fighter_obj, &item_obj)) {
    return NULL;
  }
  PyArrayObject* fighters =
      require_contiguous_array(fighter_obj, NPY_UINT16, 2, "fighter_instance_id_u16_2d");
  PyArrayObject* items =
      require_contiguous_array(item_obj, NPY_UINT16, 2, "item_instance_id_u16_2d");
  if (fighters == NULL || items == NULL) {
    return NULL;
  }
  if (PyArray_NDIM(fighters) != 2 || PyArray_NDIM(items) != 2) {
    PyErr_SetString(PyExc_ValueError,
                    "fighter_instance_id_u16_2d and item_instance_id_u16_2d must be exactly 2D");
    return NULL;
  }
  const npy_intp n = PyArray_DIM(fighters, 0);
  const npy_intp fw = PyArray_DIM(fighters, 1);
  if (PyArray_DIM(items, 0) != n) {
    PyErr_SetString(PyExc_ValueError,
                    "fighter/item instance_id arrays must have the same frame length");
    return NULL;
  }
  const npy_intp iw = PyArray_DIM(items, 1);
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT16, 0);
  if (out == NULL) {
    return NULL;
  }
  const uint16_t* f = (const uint16_t*)PyArray_DATA(fighters);
  const uint16_t* it = (const uint16_t*)PyArray_DATA(items);
  uint16_t* out_p = (uint16_t*)PyArray_DATA(out);
  uint16_t max_seen = 0u;
  for (npy_intp i = 0; i < n; i++) {
    uint16_t row_max = 0u;
    for (npy_intp p = 0; p < fw; p++) {
      const uint16_t v = f[i * fw + p];
      if (v > row_max) {
        row_max = v;
      }
    }
    for (npy_intp k = 0; k < iw; k++) {
      const uint16_t v = it[i * iw + k];
      if (v > row_max) {
        row_max = v;
      }
    }
    if (row_max > max_seen) {
      max_seen = row_max;
    }
    uint16_t next_id = (uint16_t)(max_seen + 1u);
    if (next_id == 0u) {
      next_id = 1u;
    }
    out_p[i] = next_id;
  }
  return (PyObject*)out;
}

PyObject* msl_process_stick_i8_units_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* raw_x_obj = NULL;
  PyObject* raw_y_obj = NULL;
  int ucf_enabled = 0;
  int cardinals = 0;
  double dz_x = 0.0;
  double dz_y = 0.0;
  if (!PyArg_ParseTuple(args, "OOiidd", &raw_x_obj, &raw_y_obj, &ucf_enabled, &cardinals, &dz_x,
                        &dz_y)) {
    return NULL;
  }
  PyArrayObject* raw_x = require_contiguous_array(raw_x_obj, NPY_INT8, 1, "raw_x");
  PyArrayObject* raw_y = require_contiguous_array(raw_y_obj, NPY_INT8, 1, "raw_y");
  if (raw_x == NULL || raw_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(raw_x);
  if (PyArray_SIZE(raw_y) != n) {
    PyErr_SetString(PyExc_ValueError, "raw_x/raw_y must have the same length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_x = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_INT8, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_INT8, 0);
  PyArrayObject* out_unit_x = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_unit_y = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_FLOAT32, 0);
  if (out_x == NULL || out_y == NULL || out_unit_x == NULL || out_unit_y == NULL) {
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    Py_XDECREF(out_unit_x);
    Py_XDECREF(out_unit_y);
    return NULL;
  }
  const int8_t* rx = (const int8_t*)PyArray_DATA(raw_x);
  const int8_t* ry = (const int8_t*)PyArray_DATA(raw_y);
  int8_t* ox = (int8_t*)PyArray_DATA(out_x);
  int8_t* oy = (int8_t*)PyArray_DATA(out_y);
  float* ux = (float*)PyArray_DATA(out_unit_x);
  float* uy = (float*)PyArray_DATA(out_unit_y);
  const float fdz_x = (float)dz_x;
  const float fdz_y = (float)dz_y;
  for (npy_intp i = 0; i < n; i++) {
    const MslStickI8 v =
        ucf_process_stick_i8(rx[i], ry[i], (uint8_t)(ucf_enabled != 0), (uint8_t)(cardinals != 0));
    ox[i] = v.x;
    oy[i] = v.y;
    ux[i] = apply_deadzone(stick_i8_to_unit(v.x), fdz_x);
    uy[i] = apply_deadzone(stick_i8_to_unit(v.y), fdz_y);
  }
  return Py_BuildValue("NNNN", out_x, out_y, out_unit_x, out_unit_y);
}

static float msl_py_ucf_popo_to_nana(float x) {
  if (x >= 0.0f) {
    return (float)((int8_t)(x * 127.0f)) / 127.0f;
  }
  return (float)((int8_t)(x * 128.0f)) / 128.0f;
}

PyObject* msl_derive_ucf_pad_buffer_state_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* raw_x_obj = NULL;
  PyObject* raw_y_obj = NULL;
  PyObject* hold_y_obj = NULL;
  int ucf_enabled = 0;
  int cardinals = 0;
  double dz_x = 0.0;
  double dz_y = 0.0;
  if (!PyArg_ParseTuple(args, "OOOiidd", &raw_x_obj, &raw_y_obj, &hold_y_obj, &ucf_enabled,
                        &cardinals, &dz_x, &dz_y)) {
    return NULL;
  }
  PyArrayObject* raw_x = require_contiguous_array(raw_x_obj, NPY_INT8, 1, "raw_x");
  PyArrayObject* raw_y = require_contiguous_array(raw_y_obj, NPY_INT8, 1, "raw_y");
  PyArrayObject* hold_y = require_contiguous_array(hold_y_obj, NPY_UINT8, 1, "stick_y_hold_time");
  if (raw_x == NULL || raw_y == NULL || hold_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(raw_x);
  if (PyArray_SIZE(raw_y) != n || PyArray_SIZE(hold_y) != n) {
    PyErr_SetString(PyExc_ValueError, "raw_x/raw_y/stick_y_hold_time must have the same length");
    return NULL;
  }

  npy_intp dims1[1] = {n};
  npy_intp dims2[2] = {n, 4};
  PyArrayObject* out_index = (PyArrayObject*)PyArray_EMPTY(1, dims1, NPY_UINT8, 0);
  PyArrayObject* out_sdrop = (PyArrayObject*)PyArray_EMPTY(1, dims1, NPY_UINT8, 0);
  PyArrayObject* out_x = (PyArrayObject*)PyArray_EMPTY(2, dims2, NPY_INT8, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_EMPTY(2, dims2, NPY_INT8, 0);
  if (out_index == NULL || out_sdrop == NULL || out_x == NULL || out_y == NULL) {
    Py_XDECREF(out_index);
    Py_XDECREF(out_sdrop);
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    return NULL;
  }

  const int8_t* rx = (const int8_t*)PyArray_DATA(raw_x);
  const int8_t* ry = (const int8_t*)PyArray_DATA(raw_y);
  const uint8_t* hold = (const uint8_t*)PyArray_DATA(hold_y);
  uint8_t* index_p = (uint8_t*)PyArray_DATA(out_index);
  uint8_t* sdrop_p = (uint8_t*)PyArray_DATA(out_sdrop);
  int8_t* ox = (int8_t*)PyArray_DATA(out_x);
  int8_t* oy = (int8_t*)PyArray_DATA(out_y);

  const float fdz_x = (float)dz_x;
  const float fdz_y = (float)dz_y;
  const float sdrop_y_thresh = msl_py_ucf_popo_to_nana(-0.6125f);
  const int sdrop_delta_sq_thresh = 44 * 44;

  int8_t entries_x[4] = {0, 0, 0, 0};
  int8_t entries_y[4] = {0, 0, 0, 0};
  uint8_t index = 0u;
  uint8_t sdrop = 0u;

  for (npy_intp i = 0; i < n; i++) {
    index = (uint8_t)((index + 1u) & 3u);
    entries_x[index] = rx[i];
    entries_y[index] = ry[i];

    const MslStickI8 v =
        ucf_process_stick_i8(rx[i], ry[i], (uint8_t)(ucf_enabled != 0), (uint8_t)(cardinals != 0));
    const float stick_x_unit = apply_deadzone(stick_i8_to_unit(v.x), fdz_x);
    const float stick_y_unit = apply_deadzone(stick_i8_to_unit(v.y), fdz_y);

    const int ix = (int)truncf(fabsf(stick_x_unit) * 80.0f - 0.0001f) + 2;
    const int iy = (int)truncf(fabsf(stick_y_unit) * 80.0f - 0.0001f) + 2;
    const uint8_t is_rim = (uint8_t)((ix * ix + iy * iy) > (80 * 80));

    const int16_t prev2_y = (i >= 2) ? (int16_t)ry[i - 2] : 0;
    const int dy = (int)((int16_t)ry[i] - prev2_y);
    const int dy_sq = dy * dy;

    if (stick_y_unit > sdrop_y_thresh) {
      sdrop = 0u;
    } else if (!is_rim) {
      sdrop = 0u;
    } else if (sdrop != 0u) {
      sdrop = (uint8_t)(sdrop + 1u);
    } else if (hold[i] < 2u && dy_sq > sdrop_delta_sq_thresh) {
      sdrop = 1u;
    } else {
      sdrop = 0u;
    }

    index_p[i] = index;
    sdrop_p[i] = sdrop;
    for (int k = 0; k < 4; k++) {
      ox[i * 4 + k] = entries_x[k];
      oy[i * 4 + k] = entries_y[k];
    }
  }

  return Py_BuildValue("NNNN", out_index, out_sdrop, out_x, out_y);
}

PyObject* msl_compute_tilt_timer_axis_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* axis_obj = NULL;
  double tilt_thresh = 0.0;
  int start_timer = 0xFE;
  if (!PyArg_ParseTuple(args, "Odi", &axis_obj, &tilt_thresh, &start_timer)) {
    return NULL;
  }
  PyArrayObject* axis = require_contiguous_array(axis_obj, NPY_FLOAT32, 1, "axis_unit");
  if (axis == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(axis);
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out == NULL) {
    return NULL;
  }
  const float* a = (const float*)PyArray_DATA(axis);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  const float thresh = (float)tilt_thresh;
  float prev_axis = 0.0f;
  int timer = start_timer & 0xFF;
  for (npy_intp i = 0; i < n; i++) {
    const float cur = a[i];
    if (cur >= thresh) {
      if (prev_axis >= thresh) {
        timer += 1;
        if (timer > 0xFE) {
          timer = 0xFE;
        }
      } else {
        timer = 0;
      }
    } else if (cur <= -thresh) {
      if (prev_axis <= -thresh) {
        timer += 1;
        if (timer > 0xFE) {
          timer = 0xFE;
        }
      } else {
        timer = 0;
      }
    } else {
      timer = 0xFE;
    }
    out_p[i] = (uint8_t)timer;
    prev_axis = cur;
  }
  return (PyObject*)out;
}

PyObject* msl_compute_tilt_timer_axis_pre_post_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* axis_obj = NULL;
  double tilt_thresh = 0.0;
  PyObject* override_obj = Py_None;
  int override_post_value = 0xFE;
  PyObject* reset_obj = Py_None;
  int start_timer_post = 0xFE;
  if (!PyArg_ParseTuple(args, "OdOiOi", &axis_obj, &tilt_thresh, &override_obj,
                        &override_post_value, &reset_obj, &start_timer_post)) {
    return NULL;
  }
  PyArrayObject* axis = require_contiguous_array(axis_obj, NPY_FLOAT32, 1, "axis_unit");
  if (axis == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(axis);
  PyArrayObject* override = NULL;
  PyArrayObject* reset = NULL;
  if (override_obj != Py_None) {
    override = require_contiguous_array(override_obj, NPY_BOOL, 1, "override_post_mask");
    if (override == NULL) {
      return NULL;
    }
    if (PyArray_SIZE(override) != n) {
      PyErr_SetString(PyExc_ValueError, "override_post_mask must match axis_unit length");
      return NULL;
    }
  }
  if (reset_obj != Py_None) {
    reset = require_contiguous_array(reset_obj, NPY_BOOL, 1, "reset_post_mask");
    if (reset == NULL) {
      return NULL;
    }
    if (PyArray_SIZE(reset) != n) {
      PyErr_SetString(PyExc_ValueError, "reset_post_mask must match axis_unit length");
      return NULL;
    }
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out_pre = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_post = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out_pre == NULL || out_post == NULL) {
    Py_XDECREF(out_pre);
    Py_XDECREF(out_post);
    return NULL;
  }

  const float* axis_p = (const float*)PyArray_DATA(axis);
  const npy_bool* override_p = override != NULL ? (const npy_bool*)PyArray_DATA(override) : NULL;
  const npy_bool* reset_p = reset != NULL ? (const npy_bool*)PyArray_DATA(reset) : NULL;
  uint8_t* pre_p = (uint8_t*)PyArray_DATA(out_pre);
  uint8_t* post_p = (uint8_t*)PyArray_DATA(out_post);
  const float thresh = (float)tilt_thresh;
  const int override_value = override_post_value & 0xFF;
  float prev_axis = 0.0f;
  int timer_post = start_timer_post & 0xFF;
  for (npy_intp i = 0; i < n; i++) {
    const float cur = axis_p[i];
    int timer_pre = timer_post;
    if (cur >= thresh) {
      if (prev_axis >= thresh) {
        timer_pre += 1;
        if (timer_pre > 0xFE) timer_pre = 0xFE;
      } else {
        timer_pre = 0;
      }
    } else if (cur <= -thresh) {
      if (prev_axis <= -thresh) {
        timer_pre += 1;
        if (timer_pre > 0xFE) timer_pre = 0xFE;
      } else {
        timer_pre = 0;
      }
    } else {
      timer_pre = 0xFE;
    }
    timer_post = timer_pre;
    if (override_p != NULL && override_p[i]) {
      timer_post = override_value;
    }
    if (reset_p != NULL && reset_p[i]) {
      timer_post = 0xFE;
    }
    pre_p[i] = (uint8_t)timer_pre;
    post_p[i] = (uint8_t)timer_post;
    prev_axis = cur;
  }
  return Py_BuildValue("NN", out_pre, out_post);
}

PyObject* msl_compute_tilt_timer_y_pre_post_with_fall_fast_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* axis_obj = NULL;
  double tilt_thresh = 0.0;
  PyObject* jump_obj = NULL;
  PyObject* pre_jump_obj = Py_None;
  PyObject* fastfall_ok_obj = NULL;
  PyObject* vy_obj = NULL;
  PyObject* ground_obj = NULL;
  double fastfall_stick_threshold = 0.0;
  int fastfall_tilt_max_frames = 0;
  PyObject* reset_obj = Py_None;
  int start_timer_post = 0xFE;
  if (!PyArg_ParseTuple(args, "OdOOOOOdiOi", &axis_obj, &tilt_thresh, &jump_obj, &pre_jump_obj,
                        &fastfall_ok_obj, &vy_obj, &ground_obj, &fastfall_stick_threshold,
                        &fastfall_tilt_max_frames, &reset_obj, &start_timer_post)) {
    return NULL;
  }
  PyArrayObject* axis = require_contiguous_array(axis_obj, NPY_FLOAT32, 1, "stick_y_unit");
  PyArrayObject* jump = require_contiguous_array(jump_obj, NPY_BOOL, 1, "jump_entry");
  PyArrayObject* fastfall_ok =
      require_contiguous_array(fastfall_ok_obj, NPY_BOOL, 1, "fastfall_ok");
  PyArrayObject* vy = require_contiguous_array(vy_obj, NPY_FLOAT32, 1, "speed_y_self_post");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_BOOL, 1, "on_ground_post");
  if (axis == NULL || jump == NULL || fastfall_ok == NULL || vy == NULL || ground == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(axis);
  if (PyArray_SIZE(jump) != n || PyArray_SIZE(fastfall_ok) != n || PyArray_SIZE(vy) != n ||
      PyArray_SIZE(ground) != n) {
    PyErr_SetString(PyExc_ValueError, "fall-fast tilt inputs must have the same length");
    return NULL;
  }
  PyArrayObject* pre_jump = NULL;
  PyArrayObject* reset = NULL;
  if (pre_jump_obj != Py_None) {
    pre_jump = require_contiguous_array(pre_jump_obj, NPY_BOOL, 1, "pre_input_jump_entry");
    if (pre_jump == NULL) {
      return NULL;
    }
    if (PyArray_SIZE(pre_jump) != n) {
      PyErr_SetString(PyExc_ValueError, "pre_input_jump_entry must match stick_y_unit length");
      return NULL;
    }
  }
  if (reset_obj != Py_None) {
    reset = require_contiguous_array(reset_obj, NPY_BOOL, 1, "reset_post_mask");
    if (reset == NULL) {
      return NULL;
    }
    if (PyArray_SIZE(reset) != n) {
      PyErr_SetString(PyExc_ValueError, "reset_post_mask must match stick_y_unit length");
      return NULL;
    }
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out_pre = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_post = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_fall = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out_pre == NULL || out_post == NULL || out_fall == NULL) {
    Py_XDECREF(out_pre);
    Py_XDECREF(out_post);
    Py_XDECREF(out_fall);
    return NULL;
  }

  const float* axis_p = (const float*)PyArray_DATA(axis);
  const npy_bool* jump_p = (const npy_bool*)PyArray_DATA(jump);
  const npy_bool* pre_jump_p = pre_jump != NULL ? (const npy_bool*)PyArray_DATA(pre_jump) : NULL;
  const npy_bool* fastfall_ok_p = (const npy_bool*)PyArray_DATA(fastfall_ok);
  const float* vy_p = (const float*)PyArray_DATA(vy);
  const npy_bool* ground_p = (const npy_bool*)PyArray_DATA(ground);
  const npy_bool* reset_p = reset != NULL ? (const npy_bool*)PyArray_DATA(reset) : NULL;
  uint8_t* pre_p = (uint8_t*)PyArray_DATA(out_pre);
  uint8_t* post_p = (uint8_t*)PyArray_DATA(out_post);
  uint8_t* fall_p = (uint8_t*)PyArray_DATA(out_fall);

  const float thresh = (float)tilt_thresh;
  const float stick_thresh = (float)fastfall_stick_threshold;
  const int tilt_max = fastfall_tilt_max_frames;
  float prev_axis = 0.0f;
  int timer_post = start_timer_post & 0xFF;
  uint8_t fall_fast_prev_post = 0u;
  float vy_prev_post = 0.0f;
  npy_bool on_ground_prev_post = n > 0 ? ground_p[0] : 0;

  for (npy_intp i = 0; i < n; i++) {
    const float cur = axis_p[i];
    const npy_bool on_ground_start = on_ground_prev_post;
    const float vy_start = vy_prev_post;
    uint8_t fall_fast_start = fall_fast_prev_post;
    int t_pre = timer_post & 0xFF;

    if (cur >= thresh) {
      if (prev_axis >= thresh) {
        t_pre += 1;
        if (t_pre > 0xFE) t_pre = 0xFE;
      } else {
        t_pre = 0;
      }
    } else if (cur <= -thresh) {
      if (prev_axis <= -thresh) {
        t_pre += 1;
        if (t_pre > 0xFE) t_pre = 0xFE;
      } else {
        t_pre = 0;
      }
    } else {
      t_pre = 0xFE;
    }

    int t_post = t_pre;
    if (pre_jump_p != NULL && pre_jump_p[i]) {
      fall_fast_start = 0u;
      if (cur >= thresh) {
        t_post = prev_axis >= thresh ? 0xFE : 0;
      } else if (cur <= -thresh) {
        t_post = prev_axis <= -thresh ? 0xFE : 0;
      } else {
        t_post = 0xFE;
      }
    }
    if (jump_p[i]) {
      t_post = 0xFE;
      fall_fast_start = 0u;
    }

    uint8_t ff_after = fall_fast_start;
    if (!on_ground_start && fastfall_ok_p[i]) {
      if (!fall_fast_start && vy_start < 0.0f && cur <= -stick_thresh && t_post < tilt_max) {
        ff_after = 1u;
        t_post = 0xFE;
      }
    }
    if (reset_p != NULL && reset_p[i]) {
      t_post = 0xFE;
    }
    const uint8_t ff_post = ground_p[i] ? 0u : ff_after;
    pre_p[i] = (uint8_t)t_pre;
    post_p[i] = (uint8_t)t_post;
    fall_p[i] = ff_post;
    prev_axis = cur;
    timer_post = t_post;
    fall_fast_prev_post = ff_post;
    vy_prev_post = vy_p[i];
    on_ground_prev_post = ground_p[i];
  }

  return Py_BuildValue("NNN", out_pre, out_post, out_fall);
}

// True when ftCommon_GrabMash runs for the victim during frame F (post-row index f):
// - the frame STARTS in CaptureWait or CaptureDamage (their Anim callbacks call GrabMash), or
// - the wait state was entered early enough in F for its own Anim callback to run the same
//   frame (observable as the entry row landing at action_frame == 1 instead of 0; v12 Dolphin
//   probe, PRH rec 7602 window).
static inline bool msl_py_grab_mash_runs_during(const uint16_t* a, const int16_t* af, npy_intp f) {
  if (f <= 0) {
    return false;
  }
  if (msl_py_capture_wait_action(a[f - 1]) || msl_py_capture_damage_action(a[f - 1])) {
    return true;
  }
  return msl_py_capture_wait_action(a[f]) && !msl_py_capture_wait_action(a[f - 1]) && af[f] == 1;
}

PyObject* msl_derive_grab_mash_stick_sign_post_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* sx_obj = NULL;
  PyObject* sy_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* owner_obj = NULL;
  double threshold = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOd", &sx_obj, &sy_obj, &action_obj, &frame_obj, &owner_obj,
                        &threshold)) {
    return NULL;
  }
  PyArrayObject* sx = require_contiguous_array(sx_obj, NPY_FLOAT32, 1, "stick_x_unit");
  PyArrayObject* sy = require_contiguous_array(sy_obj, NPY_FLOAT32, 1, "stick_y_unit");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* owner = require_contiguous_array(owner_obj, NPY_UINT8, 1, "grab_owner_port_u8");
  if (sx == NULL || sy == NULL || action == NULL || frame == NULL || owner == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(sx);
  if (PyArray_SIZE(sy) != n || PyArray_SIZE(action) != n || PyArray_SIZE(frame) != n ||
      PyArray_SIZE(owner) != n) {
    PyErr_SetString(PyExc_ValueError, "grab mash sign inputs must all match");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_x = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_INT8, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_INT8, 0);
  if (out_x == NULL || out_y == NULL) {
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    return NULL;
  }
  const float* sx_p = (const float*)PyArray_DATA(sx);
  const float* sy_p = (const float*)PyArray_DATA(sy);
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint8_t* own = (const uint8_t*)PyArray_DATA(owner);
  int8_t* out_x_p = (int8_t*)PyArray_DATA(out_x);
  int8_t* out_y_p = (int8_t*)PyArray_DATA(out_y);
  const float thresh = (float)threshold;
  // x1A50/x1A51 mirror the source exactly:
  // - cleared by ftCommon_InitGrab on a fresh capture attach,
  // - updated ONLY while ftCommon_GrabMash runs (CaptureWait/CaptureDamage callbacks; see
  //   msl_py_grab_mash_runs_during for the entry-frame case),
  // - frozen everywhere else,
  // - GrabMash reads fp->input.lstick, which lags the serialized pre-frame rows by one frame
  //   (v12 Dolphin probe, CDO rec 12724 / QGD rec 8257 / AGNG rec 3208 windows).
  // refs/melee/src/melee/ft/ftcommon.c::{ftCommon_InitGrab,ftCommon_GrabMash}
  int8_t latch_x = 0;
  int8_t latch_y = 0;
  for (npy_intp i = 0; i < n; i++) {
    const bool attached = msl_py_capture_attach_action(a[i]) && own[i] != 0xFFu;
    const bool was_attached =
        i > 0 && msl_py_capture_attach_action(a[i - 1]) && own[i - 1] != 0xFFu;
    if (attached && !was_attached) {
      latch_x = 0;
      latch_y = 0;
    }
    if (i > 0 && msl_py_grab_mash_runs_during(a, af, i)) {
      const float fx = sx_p[i - 1];
      const float fy = sy_p[i - 1];
      if (fx < -thresh) {
        latch_x = -1;
      } else if (fx > thresh) {
        latch_x = 1;
      }
      if (fy < -thresh) {
        latch_y = -1;
      } else if (fy > thresh) {
        latch_y = 1;
      }
    }
    out_x_p[i] = latch_x;
    out_y_p[i] = latch_y;
  }
  return Py_BuildValue("NN", out_x, out_y);
}

PyObject* msl_derive_guard_release_lockout_and_lightshield_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hp_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* buttons_obj = NULL;
  PyObject* trig_obj = NULL;
  int button_mask_lr = 0;
  int button_mask_z = 0;
  double trigger_deadzone = 0.0;
  int guard_x10_init_frames = 0;
  int act_guard_on = 0;
  int act_guard = 0;
  int act_guard_reflect = 0;
  int act_guard_set_off = 0;
  if (!PyArg_ParseTuple(args, "OOOOOiidiiiii", &action_obj, &hp_obj, &hitlag_obj, &buttons_obj,
                        &trig_obj, &button_mask_lr, &button_mask_z, &trigger_deadzone,
                        &guard_x10_init_frames, &act_guard_on, &act_guard, &act_guard_reflect,
                        &act_guard_set_off)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* hp = require_contiguous_array(hp_obj, NPY_FLOAT32, 1, "shield_hp");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag");
  PyArrayObject* buttons = require_contiguous_array(buttons_obj, NPY_UINT16, 1, "buttons_held");
  PyArrayObject* trig = require_contiguous_array(trig_obj, NPY_FLOAT32, 1, "trigger_unit");
  if (action == NULL || hp == NULL || hitlag == NULL || buttons == NULL || trig == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hp) != n || PyArray_SIZE(hitlag) != n || PyArray_SIZE(buttons) != n ||
      PyArray_SIZE(trig) != n) {
    PyErr_SetString(PyExc_ValueError, "guard release inputs must have the same length");
    return NULL;
  }
  const int mask_lr = (button_mask_lr | button_mask_z) & 0xFFFF;
  if (mask_lr == 0) {
    PyErr_SetString(PyExc_ValueError, "button_mask_lr must be non-zero");
    return NULL;
  }
  const float dz = (float)trigger_deadzone;
  const float denom = 1.0f - dz;
  if (!(denom > 0.0f)) {
    PyErr_SetString(PyExc_ValueError, "invalid trigger_deadzone");
    return NULL;
  }
  int init = guard_x10_init_frames;
  if (init < 0) init = 0;
  if (init > 255) init = 255;

  npy_intp dims[1] = {n};
  PyArrayObject* out_xc = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x10 = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_light = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  if (out_xc == NULL || out_x10 == NULL || out_light == NULL) {
    Py_XDECREF(out_xc);
    Py_XDECREF(out_x10);
    Py_XDECREF(out_light);
    return NULL;
  }
  const uint16_t* aid = (const uint16_t*)PyArray_DATA(action);
  const float* shield_hp = (const float*)PyArray_DATA(hp);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* btn = (const uint16_t*)PyArray_DATA(buttons);
  const float* trigger = (const float*)PyArray_DATA(trig);
  uint8_t* out_xc_p = (uint8_t*)PyArray_DATA(out_xc);
  uint8_t* out_x10_p = (uint8_t*)PyArray_DATA(out_x10);
  float* out_light_p = (float*)PyArray_DATA(out_light);

  uint8_t xC = 0u;
  int x10 = 0;
  float light = 0.0f;
  for (npy_intp i = 0; i < n; i++) {
    const int a = (int)aid[i];
    const int prev_a = i > 0 ? (int)aid[i - 1] : a;
    const uint8_t in_guard =
        (uint8_t)(a == act_guard_on || a == act_guard || a == act_guard_reflect);
    const uint8_t prev_in_guard =
        (uint8_t)(prev_a == act_guard_on || prev_a == act_guard || prev_a == act_guard_reflect);
    const uint8_t in_guard_set_off = (uint8_t)(a == act_guard_set_off);

    if ((a == act_guard_on && prev_a != act_guard_on) ||
        (a == act_guard_reflect && prev_a != act_guard_reflect)) {
      xC = 0u;
      x10 = init;
      light = 0.0f;
    }

    if (!in_guard && !in_guard_set_off) {
      xC = 0u;
      x10 = 0;
      light = 0.0f;
      out_xc_p[i] = 0u;
      out_x10_p[i] = 0u;
      out_light_p[i] = 0.0f;
      continue;
    }

    if (in_guard && !prev_in_guard && a == act_guard && prev_a != act_guard_set_off) {
      xC = 0u;
      x10 = init;
      light = 0.0f;
    }

    if (in_guard_set_off) {
      if (prev_a != act_guard_set_off && !prev_in_guard && x10 == 0) {
        xC = 0u;
        x10 = init;
        float t = (trigger[i] - dz) / denom;
        if (t >= 0.0f) {
          if (t > 1.0f) t = 1.0f;
          light = t;
        }
      }
      out_xc_p[i] = (uint8_t)(xC ? 1u : 0u);
      out_x10_p[i] = (uint8_t)(x10 & 0xFF);
      out_light_p[i] = light;
      continue;
    }

    const int hl_prev = i > 0 ? (int)hl[i - 1] : 0;
    const int hl_after_prio0 = hl_prev > 0 ? hl_prev - 1 : 0;
    const uint8_t held = (uint8_t)(((btn[i] & mask_lr) != 0) || (trigger[i] >= dz));
    if (hl_after_prio0 == 0 && shield_hp[i] > 0.0f) {
      float t = (trigger[i] - dz) / denom;
      if (t >= 0.0f) {
        if (t > 1.0f) t = 1.0f;
        light = t;
      }
      if (x10 > 0) {
        x10 -= 1;
        if (x10 < 0) x10 = 0;
      }
      if (!held) {
        xC = 1u;
      }
    }
    out_xc_p[i] = (uint8_t)(xC ? 1u : 0u);
    out_x10_p[i] = (uint8_t)(x10 & 0xFF);
    out_light_p[i] = light;
  }
  return Py_BuildValue("NNN", out_xc, out_x10, out_light);
}

PyObject* msl_compute_press_timer_u8_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* pressed_obj = NULL;
  int press_mask = 0;
  int start_timer = 0xFF;
  if (!PyArg_ParseTuple(args, "Oii", &pressed_obj, &press_mask, &start_timer)) return NULL;
  PyArrayObject* pressed = require_contiguous_array(pressed_obj, NPY_UINT16, 1, "buttons_pressed");
  if (pressed == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(pressed);
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* bp = (const uint16_t*)PyArray_DATA(pressed);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  uint8_t timer = (uint8_t)(start_timer & 0xFF);
  const uint16_t mask = (uint16_t)((uint32_t)press_mask & 0xFFFFu);
  for (npy_intp i = 0; i < n; i++) {
    if ((bp[i] & mask) != 0u) {
      timer = 0u;
    } else if (timer < 0xFFu) {
      timer += 1u;
    }
    out_p[i] = timer;
  }
  return (PyObject*)out;
}

PyObject* msl_compute_lr_press_timer_x67f_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* buttons_obj = NULL;
  PyObject* trigger_obj = NULL;
  PyObject* hitlag_obj = Py_None;
  double trigger_deadzone = 0.0;
  int button_mask_lr = 0;
  int button_mask_z = 0;
  int start_timer = 0xFF;
  if (!PyArg_ParseTuple(args, "OOOdiii", &buttons_obj, &trigger_obj, &hitlag_obj, &trigger_deadzone,
                        &button_mask_lr, &button_mask_z, &start_timer)) {
    return NULL;
  }
  PyArrayObject* buttons = require_contiguous_array(buttons_obj, NPY_UINT16, 1, "buttons");
  PyArrayObject* trigger = require_contiguous_array(trigger_obj, NPY_FLOAT32, 1, "trigger_unit");
  PyArrayObject* hitlag = NULL;
  if (hitlag_obj != Py_None) {
    hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_frames");
  }
  if (buttons == NULL || trigger == NULL || (hitlag_obj != Py_None && hitlag == NULL)) return NULL;
  const npy_intp n = PyArray_SIZE(buttons);
  if (PyArray_SIZE(trigger) != n || (hitlag != NULL && PyArray_SIZE(hitlag) != n)) {
    PyErr_SetString(PyExc_ValueError, "buttons/trigger_unit/hitlag_frames must match length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* b = (const uint16_t*)PyArray_DATA(buttons);
  const float* trig = (const float*)PyArray_DATA(trigger);
  const uint16_t* hl = hitlag != NULL ? (const uint16_t*)PyArray_DATA(hitlag) : NULL;
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  uint8_t timer = (uint8_t)(start_timer & 0xFF);
  bool prev_held = false;
  bool x668_lr_latched = false;
  const uint16_t mask = (uint16_t)((button_mask_lr | button_mask_z) & 0xFFFF);
  const float dz = (float)trigger_deadzone;
  for (npy_intp i = 0; i < n; i++) {
    const bool held = ((b[i] & mask) != 0u) || trig[i] > dz;
    const bool pressed_edge = held && !prev_held;
    if (hl != NULL && hl[i] > 0u) {
      x668_lr_latched = x668_lr_latched || pressed_edge;
    } else {
      x668_lr_latched = pressed_edge;
    }
    if (x668_lr_latched) {
      timer = 0u;
    } else if (timer < 0xFFu) {
      timer += 1u;
    }
    out_p[i] = timer;
    prev_held = held;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_turn_internals_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* stick_x_obj = NULL;
  PyObject* tilt_x_obj = NULL;
  PyObject* turn_frames_obj = NULL;
  double dash_flick_abs = 0.0;
  int dash_flick_tilt_max = 0;
  int act_turn = 0;
  int act_turn_run = 0;
  if (!PyArg_ParseTuple(args, "OOOOOdiOii", &action_obj, &frame_obj, &facing_obj, &stick_x_obj,
                        &tilt_x_obj, &dash_flick_abs, &dash_flick_tilt_max, &turn_frames_obj,
                        &act_turn, &act_turn_run)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing");
  PyArrayObject* stick_x = require_contiguous_array(stick_x_obj, NPY_FLOAT32, 1, "stick_x_unit");
  PyArrayObject* tilt_x = require_contiguous_array(tilt_x_obj, NPY_UINT8, 1, "tilt_timer_x");
  PyArrayObject* turn_frames =
      require_contiguous_array(turn_frames_obj, NPY_UINT8, 1, "turn_frames");
  if (action == NULL || frame == NULL || facing == NULL || stick_x == NULL || tilt_x == NULL ||
      turn_frames == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(facing) != n || PyArray_SIZE(stick_x) != n ||
      PyArray_SIZE(tilt_x) != n || PyArray_SIZE(turn_frames) != n) {
    PyErr_SetString(PyExc_ValueError, "turn internal inputs must have the same length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_frames = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_has = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x8 = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_INT8, 0);
  if (out_frames == NULL || out_has == NULL || out_x8 == NULL) {
    Py_XDECREF(out_frames);
    Py_XDECREF(out_has);
    Py_XDECREF(out_x8);
    return NULL;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint8_t* fac = (const uint8_t*)PyArray_DATA(facing);
  const float* sx = (const float*)PyArray_DATA(stick_x);
  const uint8_t* ttx = (const uint8_t*)PyArray_DATA(tilt_x);
  const uint8_t* tf = (const uint8_t*)PyArray_DATA(turn_frames);
  uint8_t* frames_p = (uint8_t*)PyArray_DATA(out_frames);
  uint8_t* has_p = (uint8_t*)PyArray_DATA(out_has);
  int8_t* x8_p = (int8_t*)PyArray_DATA(out_x8);
  int frames_to_turn = 0;
  uint8_t has_turned = 0u;
  int x8 = 0;
  bool prev_in_turn = false;
  const float dash_abs = (float)dash_flick_abs;
  for (npy_intp i = 0; i < n; i++) {
    const bool cur_in_turn = (int)a[i] == act_turn;
    if (!cur_in_turn) {
      frames_to_turn = 0;
      has_turned = 0u;
      x8 = 0;
      prev_in_turn = false;
      continue;
    }
    const bool same_action_restart = prev_in_turn && i > 0 && af[i] < af[i - 1];
    if (!prev_in_turn || same_action_restart) {
      bool is_smash = false;
      float facing_dir = 1.0f;
      if ((int)a[i] == act_turn && i > 0) {
        facing_dir = fac[i - 1] != 0u ? 1.0f : -1.0f;
        if (fabsf(sx[i]) >= dash_abs && (int)ttx[i] < dash_flick_tilt_max &&
            (sx[i] * facing_dir) < 0.0f) {
          is_smash = true;
        }
      }
      frames_to_turn = is_smash ? 0 : (int)tf[i];
      if (frames_to_turn < 0) frames_to_turn = 0;
      if (frames_to_turn > 0xFE) frames_to_turn = 0xFE;
      has_turned = 0u;
      x8 = is_smash ? (facing_dir > 0.0f ? 1 : -1) : 0;
      frames_p[i] = (uint8_t)frames_to_turn;
      has_p[i] = has_turned;
      x8_p[i] = (int8_t)x8;
      prev_in_turn = true;
      continue;
    }
    if (frames_to_turn > 0) {
      frames_to_turn -= 1;
    } else if (!has_turned) {
      has_turned = 1u;
    }
    const float facing_dir_i = fac[i] != 0u ? 1.0f : -1.0f;
    const float facing_after = has_turned ? facing_dir_i : -facing_dir_i;
    if ((sx[i] * facing_after) >= dash_abs && (int)ttx[i] < dash_flick_tilt_max) {
      x8 = facing_after > 0.0f ? 1 : -1;
    }
    frames_p[i] = (uint8_t)frames_to_turn;
    has_p[i] = has_turned;
    x8_p[i] = (int8_t)x8;
    prev_in_turn = true;
  }
  return Py_BuildValue("NNN", out_frames, out_has, out_x8);
}

PyObject* msl_compute_x672_trigger_timer_pre_post_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* trigger_obj = NULL;
  PyObject* prev_obj = Py_None;
  double trigger_min = 0.0;
  PyObject* guard_obj = Py_None;
  int start_timer = 0xFE;
  if (!PyArg_ParseTuple(args, "OOdOi", &trigger_obj, &prev_obj, &trigger_min, &guard_obj,
                        &start_timer)) {
    return NULL;
  }
  PyArrayObject* trigger = require_contiguous_array(trigger_obj, NPY_FLOAT32, 1, "trigger_unit");
  PyArrayObject* prev = NULL;
  PyArrayObject* guard = NULL;
  if (prev_obj != Py_None) {
    prev = require_contiguous_array(prev_obj, NPY_FLOAT32, 1, "prev_trigger_unit");
  }
  if (guard_obj != Py_None) {
    guard = require_contiguous_array(guard_obj, NPY_BOOL, 1, "guard_reflect_entry");
  }
  if (trigger == NULL || (prev_obj != Py_None && prev == NULL) ||
      (guard_obj != Py_None && guard == NULL)) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(trigger);
  if ((prev != NULL && PyArray_SIZE(prev) != n) || (guard != NULL && PyArray_SIZE(guard) != n)) {
    PyErr_SetString(PyExc_ValueError, "x672 trigger inputs must have matching length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_pre = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_post = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out_pre == NULL || out_post == NULL) {
    Py_XDECREF(out_pre);
    Py_XDECREF(out_post);
    return NULL;
  }
  const float* trig = (const float*)PyArray_DATA(trigger);
  const float* prev_trig = prev != NULL ? (const float*)PyArray_DATA(prev) : NULL;
  const npy_bool* guard_p = guard != NULL ? (const npy_bool*)PyArray_DATA(guard) : NULL;
  uint8_t* pre_p = (uint8_t*)PyArray_DATA(out_pre);
  uint8_t* post_p = (uint8_t*)PyArray_DATA(out_post);
  const float thr = (float)trigger_min;
  int timer_post = start_timer & 0xFF;
  float prev_t = 0.0f;
  for (npy_intp i = 0; i < n; i++) {
    const float cur = trig[i];
    const float pv = prev_trig != NULL ? prev_trig[i] : prev_t;
    int t_pre = timer_post & 0xFF;
    if (cur >= thr) {
      if (pv >= thr) {
        t_pre += 1;
        if (t_pre > 0xFE) t_pre = 0xFE;
      } else {
        t_pre = 0;
      }
    } else {
      t_pre = 0xFE;
    }
    int t_post = t_pre;
    if (guard_p != NULL && guard_p[i]) {
      t_post = 0xFE;
    }
    pre_p[i] = (uint8_t)t_pre;
    post_p[i] = (uint8_t)t_post;
    timer_post = t_post;
    prev_t = cur;
  }
  return Py_BuildValue("NN", out_pre, out_post);
}

PyObject* msl_compute_fighter_stick_input_counters_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* sx_obj = NULL;
  PyObject* sy_obj = NULL;
  double tilt_thresh_x = 0.0;
  double tilt_thresh_y = 0.0;
  int start_timer = 0xFE;
  if (!PyArg_ParseTuple(args, "OOddi", &sx_obj, &sy_obj, &tilt_thresh_x, &tilt_thresh_y,
                        &start_timer)) {
    return NULL;
  }
  PyArrayObject* sx_arr = require_contiguous_array(sx_obj, NPY_FLOAT32, 1, "stick_x_unit");
  PyArrayObject* sy_arr = require_contiguous_array(sy_obj, NPY_FLOAT32, 1, "stick_y_unit");
  if (sx_arr == NULL || sy_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(sx_arr);
  if (PyArray_SIZE(sy_arr) != n) {
    PyErr_SetString(PyExc_ValueError, "stick_y_unit must match stick_x_unit length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_x673 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x674 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x676_x = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x2228_b7 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x677_y = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x679_x = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x67A_y = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out_x673 == NULL || out_x674 == NULL || out_x676_x == NULL || out_x2228_b7 == NULL ||
      out_x677_y == NULL || out_x679_x == NULL || out_x67A_y == NULL) {
    Py_XDECREF(out_x673);
    Py_XDECREF(out_x674);
    Py_XDECREF(out_x676_x);
    Py_XDECREF(out_x2228_b7);
    Py_XDECREF(out_x677_y);
    Py_XDECREF(out_x679_x);
    Py_XDECREF(out_x67A_y);
    return NULL;
  }
  const float* sx = (const float*)PyArray_DATA(sx_arr);
  const float* sy = (const float*)PyArray_DATA(sy_arr);
  uint8_t* ox673 = (uint8_t*)PyArray_DATA(out_x673);
  uint8_t* ox674 = (uint8_t*)PyArray_DATA(out_x674);
  uint8_t* ox676 = (uint8_t*)PyArray_DATA(out_x676_x);
  uint8_t* ox2228 = (uint8_t*)PyArray_DATA(out_x2228_b7);
  uint8_t* ox677 = (uint8_t*)PyArray_DATA(out_x677_y);
  uint8_t* ox679 = (uint8_t*)PyArray_DATA(out_x679_x);
  uint8_t* ox67A = (uint8_t*)PyArray_DATA(out_x67A_y);
  const float thr_x = (float)tilt_thresh_x;
  const float thr_y = (float)tilt_thresh_y;
  uint8_t x673 = (uint8_t)start_timer;
  uint8_t x676 = (uint8_t)start_timer;
  uint8_t x2228 = 0u;
  uint8_t x679 = (uint8_t)start_timer;
  uint8_t x674 = (uint8_t)start_timer;
  uint8_t x677 = (uint8_t)start_timer;
  uint8_t x67A = (uint8_t)start_timer;
  float prev_x = 0.0f;
  float prev_y = 0.0f;
  for (npy_intp i = 0; i < n; i++) {
    const float cur_x = sx[i];
    const float cur_y = sy[i];
    x676 = msl_py_u8_sat_inc_fe(x676);
    if (cur_x >= thr_x) {
      if (prev_x >= thr_x) {
        x673 = msl_py_u8_sat_inc_fe(x673);
        x679 = msl_py_u8_sat_inc_fe(x679);
      } else {
        x676 = 0u;
        x673 = 0u;
        x2228 = 1u;
      }
    } else if (cur_x <= -thr_x) {
      if (prev_x <= -thr_x) {
        x673 = msl_py_u8_sat_inc_fe(x673);
        x679 = msl_py_u8_sat_inc_fe(x679);
      } else {
        x676 = 0u;
        x673 = 0u;
        x2228 = 0u;
      }
    } else {
      x679 = 0xFEu;
      x673 = 0xFEu;
    }
    x677 = msl_py_u8_sat_inc_fe(x677);
    if (cur_y >= thr_y) {
      if (prev_y >= thr_y) {
        x674 = msl_py_u8_sat_inc_fe(x674);
        x67A = msl_py_u8_sat_inc_fe(x67A);
      } else {
        x677 = 0u;
        x674 = 0u;
      }
    } else if (cur_y <= -thr_y) {
      if (prev_y <= -thr_y) {
        x674 = msl_py_u8_sat_inc_fe(x674);
        x67A = msl_py_u8_sat_inc_fe(x67A);
      } else {
        x677 = 0u;
        x674 = 0u;
      }
    } else {
      x67A = 0xFEu;
      x674 = 0xFEu;
    }
    if (msl_py_lb_8000D148(prev_x, prev_y, cur_x, cur_y, 0.0f, 0.0f, thr_x)) {
      x67A = 0u;
      x679 = 0u;
    }
    ox673[i] = x673;
    ox674[i] = x674;
    ox676[i] = x676;
    ox2228[i] = x2228;
    ox677[i] = x677;
    ox679[i] = x679;
    ox67A[i] = x67A;
    prev_x = cur_x;
    prev_y = cur_y;
  }
  return Py_BuildValue("NNNNNNN", out_x673, out_x674, out_x676_x, out_x2228_b7, out_x677_y,
                       out_x679_x, out_x67A_y);
}

PyObject* msl_compute_fighter_trigger_input_counters_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* trig_obj = NULL;
  double trigger_min = 0.0;
  int start_timer = 0xFE;
  if (!PyArg_ParseTuple(args, "Odi", &trig_obj, &trigger_min, &start_timer)) {
    return NULL;
  }
  PyArrayObject* trig_arr = require_contiguous_array(trig_obj, NPY_FLOAT32, 1, "trigger_unit");
  if (trig_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(trig_arr);
  npy_intp dims[1] = {n};
  PyArrayObject* out_x675 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x67B = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x678 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out_x675 == NULL || out_x67B == NULL || out_x678 == NULL) {
    Py_XDECREF(out_x675);
    Py_XDECREF(out_x67B);
    Py_XDECREF(out_x678);
    return NULL;
  }
  const float* trig = (const float*)PyArray_DATA(trig_arr);
  uint8_t* ox675 = (uint8_t*)PyArray_DATA(out_x675);
  uint8_t* ox67B = (uint8_t*)PyArray_DATA(out_x67B);
  uint8_t* ox678 = (uint8_t*)PyArray_DATA(out_x678);
  const float thr = (float)trigger_min;
  uint8_t x675 = (uint8_t)start_timer;
  uint8_t x67B = (uint8_t)start_timer;
  uint8_t x678 = (uint8_t)start_timer;
  float prev = 0.0f;
  for (npy_intp i = 0; i < n; i++) {
    const float cur = trig[i];
    x678 = msl_py_u8_sat_inc_fe(x678);
    if (cur >= thr) {
      if (prev >= thr) {
        x675 = msl_py_u8_sat_inc_fe(x675);
        x67B = msl_py_u8_sat_inc_fe(x67B);
      } else {
        x67B = 0u;
        x678 = 0u;
        x675 = 0u;
      }
    } else {
      x67B = 0xFEu;
      x675 = 0xFEu;
    }
    ox675[i] = x675;
    ox67B[i] = x67B;
    ox678[i] = x678;
    prev = cur;
  }
  return Py_BuildValue("NNN", out_x675, out_x67B, out_x678);
}

PyObject* msl_compute_fighter_button_timers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* buttons_obj = NULL;
  PyObject* hitlag_obj = Py_None;
  int mask_a = 0;
  int mask_b = 0;
  int mask_xy = 0;
  int mask_dpad_up = 0;
  int mask_dpad_down = 0;
  int mask_lr = 0;
  int mask_z = 0;
  int start_timer = 0xFF;
  if (!PyArg_ParseTuple(args, "OOiiiiiiii", &buttons_obj, &hitlag_obj, &mask_a, &mask_b, &mask_xy,
                        &mask_dpad_up, &mask_dpad_down, &mask_lr, &mask_z, &start_timer)) {
    return NULL;
  }
  PyArrayObject* buttons_arr =
      require_contiguous_array(buttons_obj, NPY_UINT16, 1, "buttons_pressed");
  if (buttons_arr == NULL) {
    return NULL;
  }
  PyArrayObject* hitlag_arr = NULL;
  if (hitlag_obj != Py_None) {
    hitlag_arr = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_frames");
    if (hitlag_arr == NULL) {
      return NULL;
    }
  }
  const npy_intp n = PyArray_SIZE(buttons_arr);
  if (hitlag_arr != NULL && PyArray_SIZE(hitlag_arr) != n) {
    PyErr_SetString(PyExc_ValueError, "hitlag_frames must match buttons_pressed length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_x67C = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x67D = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x67E = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x680 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x681 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x682 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x683 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x684 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out_x67C == NULL || out_x67D == NULL || out_x67E == NULL || out_x680 == NULL ||
      out_x681 == NULL || out_x682 == NULL || out_x683 == NULL || out_x684 == NULL) {
    Py_XDECREF(out_x67C);
    Py_XDECREF(out_x67D);
    Py_XDECREF(out_x67E);
    Py_XDECREF(out_x680);
    Py_XDECREF(out_x681);
    Py_XDECREF(out_x682);
    Py_XDECREF(out_x683);
    Py_XDECREF(out_x684);
    return NULL;
  }
  const uint16_t* bp = (const uint16_t*)PyArray_DATA(buttons_arr);
  const uint16_t* hl = hitlag_arr != NULL ? (const uint16_t*)PyArray_DATA(hitlag_arr) : NULL;
  uint8_t* ox67C = (uint8_t*)PyArray_DATA(out_x67C);
  uint8_t* ox67D = (uint8_t*)PyArray_DATA(out_x67D);
  uint8_t* ox67E = (uint8_t*)PyArray_DATA(out_x67E);
  uint8_t* ox680 = (uint8_t*)PyArray_DATA(out_x680);
  uint8_t* ox681 = (uint8_t*)PyArray_DATA(out_x681);
  uint8_t* ox682 = (uint8_t*)PyArray_DATA(out_x682);
  uint8_t* ox683 = (uint8_t*)PyArray_DATA(out_x683);
  uint8_t* ox684 = (uint8_t*)PyArray_DATA(out_x684);
  uint8_t x67C = (uint8_t)start_timer;
  uint8_t x67D = (uint8_t)start_timer;
  uint8_t x67E = (uint8_t)start_timer;
  uint8_t x680 = (uint8_t)start_timer;
  uint8_t x681 = (uint8_t)start_timer;
  uint8_t x682 = (uint8_t)start_timer;
  uint8_t x683 = (uint8_t)start_timer;
  uint8_t x684 = (uint8_t)start_timer;
  uint16_t x668_latched = 0u;
  const uint16_t m_a = (uint16_t)mask_a;
  const uint16_t m_b = (uint16_t)mask_b;
  const uint16_t m_xy = (uint16_t)mask_xy;
  const uint16_t m_du = (uint16_t)mask_dpad_up;
  const uint16_t m_dd = (uint16_t)mask_dpad_down;
  const uint16_t m_lr = (uint16_t)mask_lr;
  const uint16_t m_z = (uint16_t)mask_z;
  for (npy_intp i = 0; i < n; i++) {
    uint16_t raw = bp[i];
    if ((raw & m_z) != 0u) {
      raw = (uint16_t)(raw | m_a);
    }
    uint16_t bpi = raw;
    if (hl != NULL && hl[i] > 0u) {
      x668_latched = (uint16_t)(x668_latched | raw);
      bpi = x668_latched;
    } else {
      x668_latched = 0u;
    }
    if ((bpi & m_a) != 0u) {
      x683 = x67C;
      x67C = 0u;
    } else {
      x67C = msl_py_u8_sat_inc_ff(x67C);
    }
    if ((bpi & m_b) != 0u) {
      x67D = 0u;
    } else {
      x67D = msl_py_u8_sat_inc_ff(x67D);
    }
    if ((bpi & m_xy) != 0u) {
      x67E = 0u;
    } else {
      x67E = msl_py_u8_sat_inc_ff(x67E);
    }
    if ((bpi & m_du) != 0u) {
      x681 = 0u;
    } else {
      x681 = msl_py_u8_sat_inc_ff(x681);
    }
    if ((bpi & m_dd) != 0u) {
      x682 = 0u;
    } else {
      x682 = msl_py_u8_sat_inc_ff(x682);
    }
    if ((bpi & m_lr) != 0u) {
      x684 = x680;
      x680 = 0u;
    } else {
      x680 = msl_py_u8_sat_inc_ff(x680);
    }
    ox67C[i] = x67C;
    ox67D[i] = x67D;
    ox67E[i] = x67E;
    ox680[i] = x680;
    ox681[i] = x681;
    ox682[i] = x682;
    ox683[i] = x683;
    ox684[i] = x684;
  }
  return Py_BuildValue("NNNNNNNN", out_x67C, out_x67D, out_x67E, out_x680, out_x681, out_x682,
                       out_x683, out_x684);
}
