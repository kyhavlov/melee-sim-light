/* Native validation derivation for damage and guard hitlag history lanes. */

#include "msl_validation_damage_history.h"
#include "msl_validation_history_common.h"
#include "../src/msl_math.h"

PyObject* msl_derive_damage_time_since_hit_x18ac_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* flags_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOO", &action_obj, &hitlag_obj, &hitstun_obj, &flags_obj)) {
    return NULL;
  }
  PyArrayObject* action =
      require_contiguous_array_readonly(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* hitlag =
      require_contiguous_array_readonly(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun =
      require_contiguous_array_readonly(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* flags =
      require_contiguous_array_readonly(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  if (action == NULL || hitlag == NULL || hitstun == NULL || flags == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(hitlag, 0);
  if (PyArray_DIM(action, 0) != n || PyArray_DIM(hitstun, 0) != n || PyArray_DIM(flags, 0) != n ||
      PyArray_DIM(flags, 1) < 4) {
    PyErr_SetString(
        PyExc_ValueError,
        "damage-history input arrays must have matching frame counts and flags [n,>=4]");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out_arr = (PyArrayObject*)PyArray_SimpleNew(1, dims, NPY_INT16);
  if (out_arr == NULL) {
    return NULL;
  }

  const uint16_t* hitlag_p = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hitstun_p = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* flags_p = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp flags_s0 = PyArray_STRIDE(flags, 0);
  const npy_intp flags_s1 = PyArray_STRIDE(flags, 1);
  int16_t* out = (int16_t*)PyArray_DATA(out_arr);

  int timer = -1;
  uint16_t prev_hitlag = 0u;
  uint16_t prev_hitstun = 0u;
  for (npy_intp fi = 0; fi < n; fi++) {
    const uint16_t hl = hitlag_p[fi];
    const uint16_t hs = hitstun_p[fi];
    const uint8_t flag_221c =
        *(const uint8_t*)(const void*)(flags_p + fi * flags_s0 + 3 * flags_s1);
    const uint8_t in_hitstun = (uint8_t)((flag_221c & 0x02u) != 0u);
    const uint8_t fresh_damage =
        (uint8_t)(((prev_hitlag == 0u && hl > 0u && (hs > 0u || in_hitstun != 0u)) ||
                   ((uint32_t)hs > (uint32_t)prev_hitstun + 1u))
                      ? 1u
                      : 0u);

    if (fresh_damage != 0u) {
      timer = 0;
    } else if (timer >= 0 && hl == 0u && timer < INT16_MAX) {
      timer++;
    }

    out[fi] = (int16_t)timer;
    prev_hitlag = hl;
    prev_hitstun = hs;
  }

  return (PyObject*)out_arr;
}

PyObject* msl_derive_damage_hitlag_sdi_reset_post_mask_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* stick_x_obj = NULL;
  PyObject* stick_y_obj = NULL;
  PyObject* damage_actions_obj = NULL;
  double sdi_step_mul = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOd", &action_obj, &hitlag_obj, &flags_obj, &pos_x_obj,
                        &pos_y_obj, &stick_x_obj, &stick_y_obj, &damage_actions_obj,
                        &sdi_step_mul)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 1, "pos_x");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 1, "pos_y");
  PyArrayObject* stick_x = require_contiguous_array(stick_x_obj, NPY_FLOAT32, 1, "stick_x_unit");
  PyArrayObject* stick_y = require_contiguous_array(stick_y_obj, NPY_FLOAT32, 1, "stick_y_unit");
  if (action == NULL || hitlag == NULL || flags == NULL || pos_x == NULL || pos_y == NULL ||
      stick_x == NULL || stick_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitlag) != n || PyArray_SIZE(pos_x) != n || PyArray_SIZE(pos_y) != n ||
      PyArray_SIZE(stick_x) != n || PyArray_SIZE(stick_y) != n || PyArray_NDIM(flags) != 2 ||
      PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) != 5) {
    PyErr_SetString(PyExc_ValueError,
                    "damage SDI reset inputs must match shape [n] and flags [n,5]");
    return NULL;
  }
  uint16_t damage_actions[256];
  Py_ssize_t damage_count = 0;
  if (parse_u16_sequence_fixed(damage_actions_obj, damage_actions, 256, &damage_count,
                               "damage_actions must be a sequence") < 0) {
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_BOOL, 0);
  if (out == NULL) {
    return NULL;
  }
  if (n < 2) {
    return (PyObject*)out;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint8_t* sf = (const uint8_t*)PyArray_DATA(flags);
  const float* x = (const float*)PyArray_DATA(pos_x);
  const float* y = (const float*)PyArray_DATA(pos_y);
  const float* sx = (const float*)PyArray_DATA(stick_x);
  const float* sy = (const float*)PyArray_DATA(stick_y);
  npy_bool* out_p = (npy_bool*)PyArray_DATA(out);
  const float step = (float)sdi_step_mul;
  const float min_component = 0.25f;
  const float loose_abs_tol = 0.20f;
  for (npy_intp i = 1; i < n; i++) {
    const npy_intp prev = i - 1;
    if (!u16_in_fixed_set(a[prev], damage_actions, damage_count)) continue;
    if (hl[prev] <= 1u) continue;
    if ((sf[(prev * 5) + 1] & 0x20u) == 0u) continue;
    const float dx = x[i] - x[prev];
    const float dy = y[i] - y[prev];
    if (fabsf(dx) < min_component && fabsf(dy) < min_component) continue;
    const float expected_x = sx[i] * step;
    const float expected_y = sy[i] * step;
    const bool x_matches = fabsf(expected_x) >= min_component &&
                           (fabsf(dx - expected_x) <= loose_abs_tol || dx * expected_x > 0.0f);
    const bool y_matches = fabsf(expected_y) >= min_component &&
                           (fabsf(dy - expected_y) <= loose_abs_tol || dy * expected_y > 0.0f);
    if (x_matches || y_matches) {
      out_p[i] = 1;
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_damage_entry_tilt_timer_reset_post_mask_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* percent_obj = NULL;
  PyObject* iid_hit_obj = NULL;
  PyObject* damage_actions_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOO", &action_obj, &frame_obj, &hitlag_obj, &percent_obj,
                        &iid_hit_obj, &damage_actions_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* percent = require_contiguous_array(percent_obj, NPY_FLOAT32, 1, "percent");
  PyArrayObject* iid_hit = require_contiguous_array(iid_hit_obj, NPY_UINT32, 1, "instance_hit_by");
  if (action == NULL || frame == NULL || hitlag == NULL || percent == NULL || iid_hit == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(hitlag) != n || PyArray_SIZE(percent) != n ||
      PyArray_SIZE(iid_hit) != n) {
    PyErr_SetString(PyExc_ValueError, "damage entry reset inputs must have the same length");
    return NULL;
  }
  uint16_t damage_actions[256];
  Py_ssize_t damage_count = 0;
  if (parse_u16_sequence_fixed(damage_actions_obj, damage_actions, 256, &damage_count,
                               "damage_actions must be a sequence") < 0) {
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_BOOL, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const float* pct = (const float*)PyArray_DATA(percent);
  const uint32_t* iid = (const uint32_t*)PyArray_DATA(iid_hit);
  npy_bool* out_p = (npy_bool*)PyArray_DATA(out);
  if (n == 0) return (PyObject*)out;
  uint16_t prev_a = a[0];
  int16_t prev_af = af[0];
  float prev_pct = pct[0];
  uint32_t prev_iid = iid[0];
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur_a = a[i];
    const bool in_damage = u16_in_fixed_set(cur_a, damage_actions, damage_count);
    if (in_damage) {
      const bool prev_in_damage = i > 0 && u16_in_fixed_set(prev_a, damage_actions, damage_count);
      const bool fresh_action = i == 0 || !prev_in_damage || cur_a != prev_a;
      const bool same_action_reentry = i > 0 && cur_a == prev_a && af[i] <= 1 && prev_af > af[i];
      const bool fresh_damage_prov =
          hl[i] > 0u || (i > 0 && pct[i] != prev_pct) || (i > 0 && iid[i] != prev_iid);
      if ((fresh_action || same_action_reentry) && fresh_damage_prov) {
        out_p[i] = 1;
      }
    }
    prev_a = cur_a;
    prev_af = af[i];
    prev_pct = pct[i];
    prev_iid = iid[i];
  }
  return (PyObject*)out;
}

PyObject* msl_derive_guard_reflect_timer_plus1_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  int act_guard_reflect = 0;
  int init_frames = 0;
  if (!PyArg_ParseTuple(args, "OOii", &action_obj, &hitlag_obj, &act_guard_reflect, &init_frames)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  if (action == NULL || hitlag == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitlag) != n) {
    PyErr_SetString(PyExc_ValueError, "action_id_u16 and hitlag_u16 must have the same shape");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  int init = init_frames + 1;
  if (init < 0) init = 0;
  if (init > 255) init = 255;
  int t = 0;
  bool prev_in = false;
  const uint16_t act = (uint16_t)((uint32_t)act_guard_reflect & 0xFFFFu);
  for (npy_intp i = 0; i < n; i++) {
    const bool in_gr = a[i] == act;
    if (!in_gr) {
      t = 0;
    } else if (!prev_in) {
      t = init;
    } else {
      const int hl_prev = i > 0 ? (int)hl[i - 1] : 0;
      const int hl_after_prio0 = hl_prev > 0 ? hl_prev - 1 : 0;
      if (t > 0 && hl_after_prio0 == 0) t -= 1;
    }
    out_p[i] = (uint8_t)(t & 0xFF);
    prev_in = in_gr;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_guard_reflect_origin_guardon_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  int act_reflect = 0;
  int act_guard_on = 0;
  int act_guard = 0;
  if (!PyArg_ParseTuple(args, "Oiii", &action_obj, &act_reflect, &act_guard_on, &act_guard)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  if (action == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  uint8_t carry = 0u;
  for (npy_intp i = 0; i < n; i++) {
    if ((int)a[i] != act_reflect) {
      carry = 0u;
      continue;
    }
    const int prev = i > 0 ? (int)a[i - 1] : -1;
    if (i == 0 || prev != act_reflect) {
      carry = (uint8_t)((prev == act_guard_on || prev == act_guard) ? 1u : 0u);
    }
    out_p[i] = carry;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_guard_special_enable_timer_x1c_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* flags_obj = NULL;
  int init_frames = 0;
  int act_guard_on = 0;
  int act_guard = 0;
  int act_guard_off = 0;
  int act_guard_reflect = 0;
  int act_guard_set_off = 0;
  if (!PyArg_ParseTuple(args, "OOOiiiiii", &action_obj, &hitlag_obj, &flags_obj, &init_frames,
                        &act_guard_on, &act_guard, &act_guard_off, &act_guard_reflect,
                        &act_guard_set_off)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  if (action == NULL || hitlag == NULL || flags == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitlag) != n || PyArray_NDIM(flags) != 2 || PyArray_DIM(flags, 0) != n ||
      PyArray_DIM(flags, 1) < 4) {
    PyErr_SetString(PyExc_ValueError, "state_flags_u8 must have shape [n, >=4]");
    return NULL;
  }
  int init = init_frames;
  if (init < 0) init = 0;
  if (init > 255) init = 255;
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint8_t* sf = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp flags_cols = PyArray_DIM(flags, 1);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  int x1c = 0;
  for (npy_intp i = 0; i < n; i++) {
    const int cur_a = (int)a[i];
    const int prev_a = i > 0 ? (int)a[i - 1] : cur_a;
    if ((cur_a == act_guard_on && prev_a != act_guard_on) ||
        (cur_a == act_guard_reflect && prev_a != act_guard_reflect)) {
      x1c = 0;
    }
    if (cur_a == act_guard_set_off && (sf[(i * flags_cols) + 3] & 0x20u) != 0u) {
      x1c = init;
    } else if (cur_a == act_guard_on || cur_a == act_guard || cur_a == act_guard_reflect) {
      const int hl_prev = i > 0 ? (int)hl[i - 1] : 0;
      const int hl_after_prio0 = hl_prev > 0 ? hl_prev - 1 : 0;
      if (hl_after_prio0 == 0 && x1c > 0) x1c -= 1;
    } else if (cur_a == act_guard_off) {
    } else if (cur_a != act_guard_set_off) {
      x1c = 0;
    }
    out_p[i] = (uint8_t)(x1c & 0xFF);
  }
  return (PyObject*)out;
}

PyObject* msl_derive_guard_setoff_hitlag_damage_min_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* hitlag_obj = NULL;
  double hitlag_dmg_mul = 0.0;
  double hitlag_base = 0.0;
  int act_guard_set_off = 0;
  if (!PyArg_ParseTuple(args, "OOOddi", &action_obj, &frame_obj, &hitlag_obj, &hitlag_dmg_mul,
                        &hitlag_base, &act_guard_set_off)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag");
  if (action == NULL || frame == NULL || hitlag == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(hitlag) != n) {
    PyErr_SetString(PyExc_ValueError,
                    "action_id/action_frame_i16/hitlag must have the same length");
    return NULL;
  }
  const float slope = (float)hitlag_dmg_mul;
  const float base = (float)hitlag_base;
  if (!(slope > 0.0f)) {
    PyErr_SetString(PyExc_ValueError, "hitlag_dmg_mul must be > 0");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  int carried = 0;
  for (npy_intp i = 0; i < n; i++) {
    if ((int)a[i] != act_guard_set_off) {
      carried = 0;
      out_p[i] = 0u;
      continue;
    }
    const int prev_a = i > 0 ? (int)a[i - 1] : -1;
    const int prev_af = i > 0 ? (int)af[i - 1] : 0;
    const int prev_hl = i > 0 ? (int)hl[i - 1] : 0;
    const int cur_af = (int)af[i];
    const int cur_hl = (int)hl[i];
    const bool segment_entry =
        i == 0 || prev_a != act_guard_set_off || cur_af < prev_af || cur_hl > prev_hl;
    if (segment_entry && cur_hl > 0) {
      carried = 0xFF;
      for (int dmg = 1; dmg < 0xFF; dmg++) {
        const int result = (int)(((float)dmg * slope) + base);
        if (result >= cur_hl) {
          carried = dmg;
          break;
        }
      }
    }
    out_p[i] = (uint8_t)(carried & 0xFF);
  }
  return (PyObject*)out;
}

PyObject* msl_derive_guard_setoff_hitlag_exit_phase_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  int act_guard_set_off = 0;
  if (!PyArg_ParseTuple(args, "OOi", &action_obj, &hitlag_obj, &act_guard_set_off)) return NULL;
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag");
  if (action == NULL || hitlag == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitlag) != n) {
    PyErr_SetString(PyExc_ValueError, "action_id/hitlag must have the same length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n; i++) {
    if ((int)a[i] != act_guard_set_off) continue;
    const int cur_hl = (int)hl[i];
    const int prev_a = i > 0 ? (int)a[i - 1] : -1;
    const int prev_hl = i > 0 ? (int)hl[i - 1] : 0;
    if (cur_hl > 1) {
      out_p[i] = 1u;
    } else if (cur_hl == 1) {
      out_p[i] = 2u;
    } else if (prev_a == act_guard_set_off && prev_hl > 0) {
      out_p[i] = 3u;
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_damage_jump_buffer_x14_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* buttons_obj = NULL;
  PyObject* stick_y_obj = NULL;
  PyObject* tilt_y_obj = NULL;
  PyObject* hitlag_obj = Py_None;
  double tap_jump_threshold = 0.0;
  int tap_jump_tilt_max = 0;
  int button_mask_xy = 0;
  PyObject* damage_actions_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOdiiO", &action_obj, &hitstun_obj, &buttons_obj, &stick_y_obj,
                        &tilt_y_obj, &hitlag_obj, &tap_jump_threshold, &tap_jump_tilt_max,
                        &button_mask_xy, &damage_actions_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* buttons = require_contiguous_array(buttons_obj, NPY_UINT16, 1, "buttons_pressed");
  PyArrayObject* stick_y = require_contiguous_array(stick_y_obj, NPY_FLOAT32, 1, "stick_y_unit");
  PyArrayObject* tilt_y = require_contiguous_array(tilt_y_obj, NPY_UINT8, 1, "tilt_timer_y");
  PyArrayObject* hitlag = NULL;
  if (hitlag_obj != Py_None) {
    hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  }
  if (action == NULL || hitstun == NULL || buttons == NULL || stick_y == NULL || tilt_y == NULL ||
      (hitlag_obj != Py_None && hitlag == NULL)) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitstun) != n || PyArray_SIZE(buttons) != n || PyArray_SIZE(stick_y) != n ||
      PyArray_SIZE(tilt_y) != n || (hitlag != NULL && PyArray_SIZE(hitlag) != n)) {
    PyErr_SetString(PyExc_ValueError, "damage jump buffer inputs must have the same length");
    return NULL;
  }
  uint16_t damage_actions[256];
  Py_ssize_t damage_count = 0;
  if (parse_u16_sequence_fixed(damage_actions_obj, damage_actions, 256, &damage_count,
                               "damage_actions must be a sequence") < 0) {
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT16, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint16_t* hl = hitlag != NULL ? (const uint16_t*)PyArray_DATA(hitlag) : NULL;
  const uint16_t* bp = (const uint16_t*)PyArray_DATA(buttons);
  const float* sy = (const float*)PyArray_DATA(stick_y);
  const uint8_t* tty = (const uint8_t*)PyArray_DATA(tilt_y);
  uint16_t* out_p = (uint16_t*)PyArray_DATA(out);
  int x14 = 0;
  const uint16_t xy = (uint16_t)((uint32_t)button_mask_xy & 0xFFFFu);
  const float tap_thr = (float)tap_jump_threshold;
  for (npy_intp i = 0; i < n; i++) {
    const bool in_damage = u16_in_fixed_set(a[i], damage_actions, damage_count);
    if (!in_damage) {
      x14 = 0;
      continue;
    }
    const bool prev_in_damage = i > 0 && u16_in_fixed_set(a[i - 1], damage_actions, damage_count);
    if (i == 0 || !prev_in_damage) {
      x14 = 0;
    } else if (hs[i] > hs[i - 1]) {
      x14 = 0;
    }
    bool jump_input = (bp[i] & xy) != 0u;
    if (sy[i] >= tap_thr && (int)tty[i] < tap_jump_tilt_max) {
      jump_input = true;
    }
    if ((hl == NULL || hl[i] == 0u) && hs[i] > 0u && jump_input) {
      x14 = (int)hs[i];
    }
    if (x14 < 0) x14 = 0;
    if (x14 > 0xFFFF) x14 = 0xFFFF;
    out_p[i] = (uint16_t)x14;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_damage_meteor_cancel_x1a_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* source_angle_obj = NULL;
  int angle_min = 0;
  int angle_max = 0;
  PyObject* damage_actions_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOiiO", &action_obj, &hitstun_obj, &source_angle_obj, &angle_min,
                        &angle_max, &damage_actions_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* source_angle =
      require_contiguous_array(source_angle_obj, NPY_UINT16, 1, "source_angle_u16");
  if (action == NULL || hitstun == NULL || source_angle == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitstun) != n || PyArray_SIZE(source_angle) != n) {
    PyErr_SetString(PyExc_ValueError, "damage meteor x1A inputs must have the same length");
    return NULL;
  }
  uint16_t damage_actions[256];
  Py_ssize_t damage_count = 0;
  if (parse_u16_sequence_fixed(damage_actions_obj, damage_actions, 256, &damage_count,
                               "damage_actions must be a sequence") < 0) {
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint16_t* src_angle = (const uint16_t*)PyArray_DATA(source_angle);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  uint8_t x1a = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const bool in_damage = u16_in_fixed_set(a[i], damage_actions, damage_count) && hs[i] > 0u;
    if (!in_damage) {
      x1a = 0u;
      continue;
    }
    const bool prev_in_damage =
        i > 0 && u16_in_fixed_set(a[i - 1], damage_actions, damage_count) && hs[i - 1] > 0u;
    if (i == 0 || !prev_in_damage || hs[i] > hs[i - 1]) {
      const uint16_t angle = src_angle[i];
      x1a = (angle != 0xFFFFu && angle != 361u && angle >= (uint16_t)angle_min &&
             angle <= (uint16_t)angle_max)
                ? 1u
                : 0u;
    }
    out_p[i] = x1a;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_damage_post_hitlag_cb_kind_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* damage_actions_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &action_obj, &hitstun_obj, &damage_actions_obj)) return NULL;
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  if (action == NULL || hitstun == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitstun) != n) {
    PyErr_SetString(PyExc_ValueError, "action_id and hitstun_u16 must have same length");
    return NULL;
  }
  uint16_t damage_actions[256];
  Py_ssize_t damage_count = 0;
  if (parse_u16_sequence_fixed(damage_actions_obj, damage_actions, 256, &damage_count,
                               "damage_actions must be a sequence") < 0) {
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  uint8_t kind = 0u;
  bool prev_in_damage = false;
  uint16_t prev_hs = n > 0 ? hs[0] : 0u;
  for (npy_intp i = 0; i < n; i++) {
    const bool in_damage = u16_in_fixed_set(a[i], damage_actions, damage_count);
    if (!in_damage) {
      kind = 0u;
    } else {
      const bool entered_damage = i == 0 || !prev_in_damage;
      const bool fresh_hit_reentry = i > 0 && hs[i] > prev_hs;
      if (entered_damage || fresh_hit_reentry) {
        kind = 1u;
      } else if (kind == 0u) {
        kind = 1u;
      }
    }
    out_p[i] = kind;
    prev_in_damage = in_damage;
    prev_hs = hs[i];
  }
  return (PyObject*)out;
}

PyObject* msl_derive_guard_tilt_state_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* sx_obj = NULL;
  PyObject* sy_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* neutral_obj = NULL;
  PyObject* frame_max_obj = NULL;
  double lerp_d = 0.0;
  int act_guard_on = 0;
  int act_guard = 0;
  int act_guard_reflect = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOdiii", &sx_obj, &sy_obj, &facing_obj, &action_obj, &frame_obj,
                        &neutral_obj, &frame_max_obj, &lerp_d, &act_guard_on, &act_guard,
                        &act_guard_reflect)) {
    return NULL;
  }
  PyArrayObject* sx_arr = require_contiguous_array(sx_obj, NPY_FLOAT32, 1, "stick_x_unit");
  PyArrayObject* sy_arr = require_contiguous_array(sy_obj, NPY_FLOAT32, 1, "stick_y_unit");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame");
  PyArrayObject* neutral = require_contiguous_array(neutral_obj, NPY_UINT16, 1, "neutral_frame");
  PyArrayObject* frame_max = require_contiguous_array(frame_max_obj, NPY_UINT16, 1, "frame_max");
  if (sx_arr == NULL || sy_arr == NULL || facing == NULL || action == NULL || frame == NULL ||
      neutral == NULL || frame_max == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(sx_arr);
  if (PyArray_SIZE(sy_arr) != n || PyArray_SIZE(facing) != n || PyArray_SIZE(action) != n ||
      PyArray_SIZE(frame) != n || PyArray_SIZE(neutral) != n || PyArray_SIZE(frame_max) != n) {
    PyErr_SetString(PyExc_ValueError, "guard tilt inputs must have the same length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_x8 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT16, 0);
  PyArrayObject* out_x4 = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_FLOAT32, 0);
  if (out_x8 == NULL || out_x4 == NULL) {
    Py_XDECREF(out_x8);
    Py_XDECREF(out_x4);
    return NULL;
  }
  const float* sx = (const float*)PyArray_DATA(sx_arr);
  const float* sy = (const float*)PyArray_DATA(sy_arr);
  const uint8_t* fac = (const uint8_t*)PyArray_DATA(facing);
  const uint16_t* aid = (const uint16_t*)PyArray_DATA(action);
  const int16_t* afr = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* neu = (const uint16_t*)PyArray_DATA(neutral);
  const uint16_t* fmax = (const uint16_t*)PyArray_DATA(frame_max);
  uint16_t* out_x8_p = (uint16_t*)PyArray_DATA(out_x8);
  float* out_x4_p = (float*)PyArray_DATA(out_x4);
  uint16_t x8 = 0u;
  float x8_f = 0.0f;
  float x4 = 0.0f;
  const float lerp = (float)lerp_d;
  const float rad_to_deg = MSL_RAD_TO_DEG_F;
  for (npy_intp i = 0; i < n; i++) {
    const int a = (int)aid[i];
    const uint16_t neutral_i = neu[i];
    const uint16_t frame_max_i = fmax[i];
    if (a == act_guard_on && afr[i] == 0) {
      x8 = neutral_i;
      x8_f = (float)neutral_i;
      x4 = 0.0f;
    }
    if (a == act_guard_on || a == act_guard || a == act_guard_reflect) {
      const float facing_dir = fac[i] != 0u ? 1.0f : -1.0f;
      const float x = sx[i] * facing_dir;
      const float y = sy[i];
      float rad = msl_melee_lb_angle(y, x);
      if (rad < 0.0f) rad += 2.0f * 3.14159265358979323846f;
      float deg = rad * rad_to_deg;
      if (deg < 0.0f) deg = 0.0f;
      if (deg > 359.0f) deg = 359.0f;
      const float offset = x8_f - (float)neutral_i;
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
      const float next_x8_f = (float)neutral_i + next_offset;
      x8_f = next_x8_f;
      int next_x8 = (int)next_x8_f;
      if (next_x8 < 0) next_x8 = 0;
      if (next_x8 > (int)frame_max_i) next_x8 = (int)frame_max_i;
      x8 = (uint16_t)next_x8;
      float mag = msl_melee_sqrtf((sx[i] * sx[i]) + (sy[i] * sy[i]));
      if (mag > 1.0f) mag = 1.0f;
      if (mag < 0.0f) mag = 0.0f;
      x4 = fmaf(lerp, mag - x4, x4);
    }
    out_x8_p[i] = x8;
    out_x4_p[i] = x4;
  }
  return Py_BuildValue("NN", out_x8, out_x4);
}

PyObject* msl_derive_magnify_damage_counter_x1910_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* inside_obj = NULL;
  PyObject* percent_obj = NULL;
  PyObject* hitlag_obj = Py_None;
  PyObject* hitstun_obj = Py_None;
  PyObject* hit_by_obj = Py_None;
  PyObject* last_hit_obj = Py_None;
  int interval_frames = 0;
  int percent_limit = 0;
  int damage_amount = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOiii", &action_obj, &flags_obj, &inside_obj, &percent_obj,
                        &hitlag_obj, &hitstun_obj, &hit_by_obj, &last_hit_obj, &interval_frames,
                        &percent_limit, &damage_amount)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  PyArrayObject* inside =
      require_contiguous_array(inside_obj, NPY_UINT8, 1, "camera_target_inside");
  PyArrayObject* percent = require_contiguous_array(percent_obj, NPY_FLOAT32, 1, "percent_f32");
  PyArrayObject* hitlag = NULL;
  PyArrayObject* hitstun = NULL;
  PyArrayObject* hit_by = NULL;
  PyArrayObject* last_hit = NULL;
  if (hitlag_obj != Py_None) hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag");
  if (hitstun_obj != Py_None) {
    hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun");
  }
  if (hit_by_obj != Py_None) {
    hit_by = require_contiguous_array(hit_by_obj, NPY_UINT16, 1, "instance_hit_by");
  }
  if (last_hit_obj != Py_None) {
    last_hit = require_contiguous_array(last_hit_obj, NPY_UINT8, 1, "last_hit_by");
  }
  if (action == NULL || flags == NULL || inside == NULL || percent == NULL ||
      (hitlag_obj != Py_None && hitlag == NULL) || (hitstun_obj != Py_None && hitstun == NULL) ||
      (hit_by_obj != Py_None && hit_by == NULL) || (last_hit_obj != Py_None && last_hit == NULL)) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(percent);
  if (PyArray_SIZE(action) != n || PyArray_SIZE(inside) != n || PyArray_NDIM(flags) != 2 ||
      PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 5 ||
      (hitlag != NULL && PyArray_SIZE(hitlag) != n) ||
      (hitstun != NULL && PyArray_SIZE(hitstun) != n) ||
      (hit_by != NULL && PyArray_SIZE(hit_by) != n) ||
      (last_hit != NULL && PyArray_SIZE(last_hit) != n)) {
    PyErr_SetString(PyExc_ValueError, "magnify inputs must share length and flags shape [n,>=5]");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT16, 0);
  if (out == NULL) return NULL;
  if (interval_frames <= 0 || damage_amount <= 0) {
    return (PyObject*)out;
  }
  PyArrayObject* eligible_arr = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_BOOL, 0);
  PyArrayObject* tick_arr = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_BOOL, 0);
  if (eligible_arr == NULL || tick_arr == NULL) {
    Py_XDECREF(eligible_arr);
    Py_XDECREF(tick_arr);
    Py_DECREF(out);
    return NULL;
  }
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* flags_p = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp flags_cols = PyArray_DIM(flags, 1);
  const uint8_t* inside_p = (const uint8_t*)PyArray_DATA(inside);
  const float* pct = (const float*)PyArray_DATA(percent);
  const uint16_t* hl = hitlag != NULL ? (const uint16_t*)PyArray_DATA(hitlag) : NULL;
  const uint16_t* hs = hitstun != NULL ? (const uint16_t*)PyArray_DATA(hitstun) : NULL;
  const uint16_t* by = hit_by != NULL ? (const uint16_t*)PyArray_DATA(hit_by) : NULL;
  const uint8_t* last = last_hit != NULL ? (const uint8_t*)PyArray_DATA(last_hit) : NULL;
  npy_bool* eligible = (npy_bool*)PyArray_DATA(eligible_arr);
  npy_bool* tick = (npy_bool*)PyArray_DATA(tick_arr);
  uint16_t* out_p = (uint16_t*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n; i++) {
    const uint8_t f = flags_p[(i * flags_cols) + 4];
    const bool visible = (f & 0x80u) != 0u;
    const bool disabled = (f & 0x08u) != 0u;
    const uint16_t a = action_p[i];
    const bool live_fighter =
        !(a == 0x0000u || a == 0x0001u || a == 0x0002u || a == 0x0004u || a == 0x000Cu ||
          a == 0x000Du || a == 0x0142u || a == 0x0143u || a == 0x0144u);
    const bool elig = live_fighter && visible && !disabled && inside_p[i] == 0u &&
                      isfinite(pct[i]) && pct[i] < (float)percent_limit;
    eligible[i] = elig ? 1 : 0;
    if (elig && i + 1 < n) {
      bool no_contact = true;
      if (hl != NULL) no_contact = no_contact && hl[i] == 0u && hl[i + 1] == 0u;
      if (hs != NULL) no_contact = no_contact && hs[i] == 0u && hs[i + 1] == 0u;
      if (by != NULL) no_contact = no_contact && by[i] == by[i + 1];
      if (last != NULL) no_contact = no_contact && last[i] == last[i + 1];
      tick[i] = (npy_bool)(isfinite(pct[i + 1]) &&
                           fabsf((pct[i + 1] - pct[i]) - (float)damage_amount) <= 1e-4f &&
                           action_p[i + 1] == action_p[i] && no_contact);
    }
  }
  const int max_counter = interval_frames - 1 < 0xFFFF ? interval_frames - 1 : 0xFFFF;
  for (npy_intp tick_i = 0; tick_i < n; tick_i++) {
    if (!tick[tick_i]) continue;
    for (int delta = 0; delta < interval_frames; delta++) {
      const npy_intp j = tick_i - delta;
      if (j < 0 || !eligible[j]) break;
      const int value = max_counter - delta;
      out_p[j] = (uint16_t)(value > 0 ? value : 0);
    }
  }
  Py_DECREF(eligible_arr);
  Py_DECREF(tick_arr);
  return (PyObject*)out;
}

static int colanim_timer_remaining_from_action_frame_c(int init_frames, int action_frame) {
  if (init_frames <= 0) return 0;
  if (action_frame <= 0) return init_frames;
  int rem = init_frames + 1 - action_frame;
  if (rem < 0) rem = 0;
  if (rem > 0xFFFF) rem = 0xFFFF;
  return rem;
}

PyObject* msl_derive_colanim_internals_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* hurt_obj = NULL;
  int throw_frames = 0;
  int cliff_frames = 0;
  int damage_frames = 0;
  int passive_frames = 0;
  int rebirth_fall_frames = 0;
  PyObject* throw_obj = NULL;
  PyObject* cliff_obj = NULL;
  PyObject* passive_obj = NULL;
  PyObject* damage_obj = NULL;
  PyObject* fall_obj = NULL;
  PyObject* rebirth_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOiiiiiOOOOOO", &action_obj, &frame_obj, &hitlag_obj,
                        &hitstun_obj, &hurt_obj, &throw_frames, &cliff_frames, &damage_frames,
                        &passive_frames, &rebirth_fall_frames, &throw_obj, &cliff_obj, &passive_obj,
                        &damage_obj, &fall_obj, &rebirth_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* hurt = require_contiguous_array(hurt_obj, NPY_UINT8, 1, "hurtbox_state_u8");
  if (action == NULL || frame == NULL || hitlag == NULL || hitstun == NULL || hurt == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(hitlag) != n || PyArray_SIZE(hitstun) != n ||
      PyArray_SIZE(hurt) != n) {
    PyErr_SetString(PyExc_ValueError, "colanim input arrays must match length");
    return NULL;
  }
  uint16_t throw_set[128], cliff_set[128], passive_set[128], damage_set[256], fall_set[128],
      rebirth_set[128];
  Py_ssize_t throw_n = 0, cliff_n = 0, passive_n = 0, damage_n = 0, fall_n = 0, rebirth_n = 0;
  if (parse_u16_sequence_fixed(throw_obj, throw_set, 128, &throw_n, "throw_actions") < 0 ||
      parse_u16_sequence_fixed(cliff_obj, cliff_set, 128, &cliff_n, "cliff_actions") < 0 ||
      parse_u16_sequence_fixed(passive_obj, passive_set, 128, &passive_n, "passivewall_actions") <
          0 ||
      parse_u16_sequence_fixed(damage_obj, damage_set, 256, &damage_n, "damage_actions") < 0 ||
      parse_u16_sequence_fixed(fall_obj, fall_set, 128, &fall_n, "fall_actions") < 0 ||
      parse_u16_sequence_fixed(rebirth_obj, rebirth_set, 128, &rebirth_n, "rebirth_actions") < 0) {
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_x198c = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_x1990 = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT16, 0);
  PyArrayObject* out_x1994 = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT16, 0);
  PyArrayObject* out_x2221 = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_rebirth = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out_x198c == NULL || out_x1990 == NULL || out_x1994 == NULL || out_x2221 == NULL ||
      out_rebirth == NULL) {
    Py_XDECREF(out_x198c);
    Py_XDECREF(out_x1990);
    Py_XDECREF(out_x1994);
    Py_XDECREF(out_x2221);
    Py_XDECREF(out_rebirth);
    return NULL;
  }
  const uint16_t* aid = (const uint16_t*)PyArray_DATA(action);
  const int16_t* afr = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* hurt_p = (const uint8_t*)PyArray_DATA(hurt);
  uint8_t* x198c_p = (uint8_t*)PyArray_DATA(out_x198c);
  uint16_t* x1990_p = (uint16_t*)PyArray_DATA(out_x1990);
  uint16_t* x1994_p = (uint16_t*)PyArray_DATA(out_x1994);
  uint8_t* x2221_p = (uint8_t*)PyArray_DATA(out_x2221);
  uint8_t* rebirth_p = (uint8_t*)PyArray_DATA(out_rebirth);
  int x1990 = 0;
  int x1994 = 0;
  bool x1994_rebirth = false;
  uint8_t x2221 = 0u;
  uint8_t shine_mask = 0u;
  uint16_t prev_a = n > 0 ? aid[0] : 0u;
  int prev_afr = n > 0 ? (int)afr[0] : 0;
  uint16_t prev_hl = n > 0 ? hl[0] : 0u;
  uint16_t prev_hs = n > 0 ? hs[0] : 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur_a = aid[i];
    const int cur_afr = (int)afr[i];
    const uint16_t cur_hl = hl[i];
    const uint16_t cur_hs = hs[i];
    if (i > 0) {
      if (x1990 > 0) x1990 -= 1;
      if (x1994 > 0) {
        x1994 -= 1;
        if (x1994 == 0) x1994_rebirth = false;
      }
    }
    const bool entered = i == 0 || cur_a != prev_a || cur_afr < prev_afr;
    if (entered && u16_in_fixed_set(cur_a, throw_set, throw_n)) {
      const int rem = colanim_timer_remaining_from_action_frame_c(throw_frames, cur_afr);
      if (rem > x1994) {
        x1994 = rem;
        x1994_rebirth = false;
      }
    }
    if (entered && u16_in_fixed_set(cur_a, cliff_set, cliff_n)) {
      const int rem = colanim_timer_remaining_from_action_frame_c(cliff_frames, cur_afr);
      if (rem > x1990) x1990 = rem;
    }
    if (entered && u16_in_fixed_set(cur_a, passive_set, passive_n)) {
      int rem = passive_frames;
      if (rem > 0xFFFF) rem = 0xFFFF;
      if (rem > x1990) x1990 = rem;
    }
    if (entered && u16_in_fixed_set(cur_a, fall_set, fall_n) &&
        u16_in_fixed_set(prev_a, rebirth_set, rebirth_n)) {
      const int rem = colanim_timer_remaining_from_action_frame_c(rebirth_fall_frames, cur_afr);
      if (rem > x1994) {
        x1994 = rem;
        x1994_rebirth = true;
      }
    }
    if (i > 0 && prev_hl > 0u && cur_hl == 0u) {
      if (u16_in_fixed_set(cur_a, damage_set, damage_n) ||
          u16_in_fixed_set(prev_a, damage_set, damage_n) || cur_hs > 0u || prev_hs > 0u) {
        int rem = damage_frames;
        if (rem > 0xFFFF) rem = 0xFFFF;
        if (rem > x1994) {
          x1994 = rem;
          x1994_rebirth = false;
        }
      }
    }
    if (i == 0 && x1990 == 0 && x1994 == 0) {
      if (hurt_p[0] == 2u) {
        x1990 = 1;
      } else if (hurt_p[0] == 1u) {
        x1994 = 1;
      }
    }
    uint8_t x198c = 0u;
    if (x1990 > 0 || x2221) {
      x198c = 2u;
    } else if (x1994 > 0) {
      x198c = 1u;
    }
    const bool shine_entry_masks = entered && i > 0 && (cur_a == 0x0168u || cur_a == 0x016Du) &&
                                   cur_afr == 1 && hurt_p[i] == 2u && hurt_p[i - 1] == 1u &&
                                   x1990 == 0 && x1994 == 0 && !x2221;
    if (shine_entry_masks) {
      shine_mask = 1u;
    } else if (!((cur_a == 0x0168u || cur_a == 0x016Du) && cur_afr == 1 && hurt_p[i] == 2u)) {
      shine_mask = 0u;
    }
    if (shine_mask) x198c = 1u;
    const bool downbound = cur_a == 0x00BEu || cur_a == 0x00BFu;
    if (hurt_p[i] == 0u && x1990 > 0 && !downbound) {
      x1990 = 0;
      x2221 = 0u;
      x198c = x1994 > 0 ? 1u : 0u;
    }
    if (hurt_p[i] == 0u && x1994 > 0 && !u16_in_fixed_set(cur_a, damage_set, damage_n) &&
        !downbound) {
      x1994 = 0;
      x1994_rebirth = false;
      if (x1990 == 0 && !x2221) x198c = 0u;
    }
    x198c_p[i] = x198c;
    x1990_p[i] = (uint16_t)x1990;
    x1994_p[i] = (uint16_t)x1994;
    x2221_p[i] = x2221 ? 1u : 0u;
    rebirth_p[i] = (uint8_t)(x1994_rebirth && x1994 > 0 ? 1u : 0u);
    prev_a = cur_a;
    prev_afr = cur_afr;
    prev_hl = cur_hl;
    prev_hs = cur_hs;
  }
  return Py_BuildValue("NNNNN", out_x198c, out_x1990, out_x1994, out_x2221, out_rebirth);
}

PyObject* msl_derive_capture_mash_buttons_pressed_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* buttons_obj = NULL;
  PyObject* l_obj = NULL;
  PyObject* r_obj = NULL;
  double trigger_deadzone = 0.0;
  int button_mask_a = 0;
  int button_mask_z = 0;
  int button_mask_lr = 0;
  if (!PyArg_ParseTuple(args, "OOOdiii", &buttons_obj, &l_obj, &r_obj, &trigger_deadzone,
                        &button_mask_a, &button_mask_z, &button_mask_lr)) {
    return NULL;
  }
  PyArrayObject* buttons = require_contiguous_array(buttons_obj, NPY_UINT16, 2, "buttons_u16_2d");
  PyArrayObject* l_trigger = require_contiguous_array(l_obj, NPY_UINT8, 2, "l_trigger_u8_2d");
  PyArrayObject* r_trigger = require_contiguous_array(r_obj, NPY_UINT8, 2, "r_trigger_u8_2d");
  if (buttons == NULL || l_trigger == NULL || r_trigger == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(buttons, 0);
  const npy_intp width = PyArray_DIM(buttons, 1);
  if (require_exact_2d_shape(l_trigger, n, width, "l_trigger_u8_2d") < 0 ||
      require_exact_2d_shape(r_trigger, n, width, "r_trigger_u8_2d") < 0) {
    return NULL;
  }

  npy_intp dims[2] = {n, width};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_UINT16, 0);
  if (out == NULL) {
    return NULL;
  }

  const uint16_t* b = (const uint16_t*)PyArray_DATA(buttons);
  const uint8_t* l = (const uint8_t*)PyArray_DATA(l_trigger);
  const uint8_t* r = (const uint8_t*)PyArray_DATA(r_trigger);
  uint16_t* out_p = (uint16_t*)PyArray_DATA(out);
  const uint16_t m_a = (uint16_t)button_mask_a;
  const uint16_t m_z = (uint16_t)button_mask_z;
  const uint16_t m_lr = (uint16_t)button_mask_lr;
  const uint16_t m_lr_z = (uint16_t)(m_lr | m_z);
  const float dz = (float)trigger_deadzone;

  for (npy_intp i = 0; i < n; i++) {
    for (npy_intp p = 0; p < width; p++) {
      const npy_intp idx = i * width + p;
      const uint16_t cur = b[idx];
      const uint16_t prev = (i > 0) ? b[((i - 1) * width) + p] : 0u;
      uint16_t pressed = (uint16_t)(cur & (uint16_t)~prev);
      if ((pressed & m_z) != 0u) {
        pressed = (uint16_t)(pressed | m_a);
      }

      float cur_trigger = (float)(l[idx] > r[idx] ? l[idx] : r[idx]) / 255.0f;
      if ((cur & m_lr) != 0u) {
        cur_trigger = 1.0f;
      }
      bool cur_lr_held = ((cur & m_lr_z) != 0u) || cur_trigger > dz;
      bool prev_lr_held = false;
      if (i > 0) {
        const npy_intp prev_idx = ((i - 1) * width) + p;
        const uint16_t prev_buttons = b[prev_idx];
        float prev_trigger =
            (float)(l[prev_idx] > r[prev_idx] ? l[prev_idx] : r[prev_idx]) / 255.0f;
        if ((prev_buttons & m_lr) != 0u) {
          prev_trigger = 1.0f;
        }
        prev_lr_held = ((prev_buttons & m_lr_z) != 0u) || prev_trigger > dz;
      }
      if (cur_lr_held && !prev_lr_held) {
        pressed = (uint16_t)(pressed | m_lr);
      }
      out_p[idx] = pressed;
    }
  }
  return (PyObject*)out;
}

static inline bool msl_py_capture_wait_or_damage_action(uint16_t a) {
  return msl_py_capture_wait_action(a) || msl_py_capture_damage_action(a);
}

static inline float msl_py_capture_grab_timer_init(int slot_index, int handicap, float percent,
                                                   float base, float h_mul, float h_base,
                                                   float slot_mul, float slot_base, float pct_mul) {
  const float slot = (float)(slot_index + 1);
  const float hcap = (float)handicap;
  return base + h_mul * (h_base - hcap) + slot_mul * (slot_base - slot) + percent * pct_mul;
}

static void msl_py_capture_seed_mid_segment(uint16_t action, int16_t action_frame, float percent,
                                            float frame_speed, int slot_index, int handicap,
                                            float base, float h_mul, float h_base, float slot_mul,
                                            float slot_base, float pct_mul, float decrement,
                                            float hold_frames, float* timer, float* counter,
                                            float* anim_timer, uint8_t* jump_latch) {
  *timer = msl_py_capture_grab_timer_init(slot_index, handicap, percent, base, h_mul, h_base,
                                          slot_mul, slot_base, pct_mul);
  *counter = 0.0f;
  *anim_timer = 0.0f;
  *jump_latch = 0u;
  if (msl_py_capture_wait_or_damage_action(action)) {
    const int callbacks = action_frame > 0 ? (int)action_frame : 0;
    if (callbacks > 0) {
      *counter = (float)callbacks;
      *timer = fmaxf(0.0f, *timer - (float)callbacks * decrement);
    }
    if (frame_speed > 1.0f) {
      *anim_timer = hold_frames;
    }
  }
}

PyObject* msl_derive_capture_grab_hidden_post_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* owner_obj = NULL;
  PyObject* percent_obj = NULL;
  PyObject* buttons_pressed_obj = NULL;
  PyObject* stick_x_obj = NULL;
  PyObject* stick_y_obj = NULL;
  PyObject* frame_speed_obj = NULL;
  PyObject* mash_x_obj = NULL;
  PyObject* mash_y_obj = NULL;
  int slot_index = 0;
  int handicap = 0;
  double base = 0.0, h_mul = 0.0, h_base = 0.0, slot_mul = 0.0, slot_base = 0.0, pct_mul = 0.0;
  double decrement = 0.0, mash_damage = 0.0, hold_frames = 0.0, jump_window = 0.0,
         stick_threshold = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOiiddddddddddd", &action_obj, &frame_obj, &owner_obj,
                        &percent_obj, &buttons_pressed_obj, &stick_x_obj, &stick_y_obj,
                        &frame_speed_obj, &mash_x_obj, &mash_y_obj, &slot_index, &handicap, &base,
                        &h_mul, &h_base, &slot_mul, &slot_base, &pct_mul, &decrement, &mash_damage,
                        &hold_frames, &jump_window, &stick_threshold)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* owner = require_contiguous_array(owner_obj, NPY_UINT8, 1, "grab_owner_port_u8");
  PyArrayObject* percent = require_contiguous_array(percent_obj, NPY_FLOAT32, 1, "percent_f32");
  PyArrayObject* buttons =
      require_contiguous_array(buttons_pressed_obj, NPY_UINT16, 1, "buttons_pressed_u16");
  PyArrayObject* stick_x = require_contiguous_array(stick_x_obj, NPY_FLOAT32, 1, "stick_x_unit");
  PyArrayObject* stick_y = require_contiguous_array(stick_y_obj, NPY_FLOAT32, 1, "stick_y_unit");
  PyArrayObject* frame_speed =
      require_contiguous_array(frame_speed_obj, NPY_FLOAT32, 1, "frame_speed_mul_f32");
  PyArrayObject* mash_x =
      require_contiguous_array(mash_x_obj, NPY_INT8, 1, "grab_mash_stick_x_sign_post");
  PyArrayObject* mash_y =
      require_contiguous_array(mash_y_obj, NPY_INT8, 1, "grab_mash_stick_y_sign_post");
  if (action == NULL || frame == NULL || owner == NULL || percent == NULL || buttons == NULL ||
      stick_x == NULL || stick_y == NULL || frame_speed == NULL || mash_x == NULL ||
      mash_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(owner) != n || PyArray_SIZE(percent) != n ||
      PyArray_SIZE(buttons) != n || PyArray_SIZE(stick_x) != n || PyArray_SIZE(stick_y) != n ||
      PyArray_SIZE(frame_speed) != n || PyArray_SIZE(mash_x) != n || PyArray_SIZE(mash_y) != n) {
    PyErr_SetString(PyExc_ValueError, "capture/grab hidden derivation inputs must all match");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_timer = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_counter = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_anim = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_jump = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_break = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out_timer == NULL || out_counter == NULL || out_anim == NULL || out_jump == NULL ||
      out_break == NULL) {
    Py_XDECREF(out_timer);
    Py_XDECREF(out_counter);
    Py_XDECREF(out_anim);
    Py_XDECREF(out_jump);
    Py_XDECREF(out_break);
    return NULL;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint8_t* own = (const uint8_t*)PyArray_DATA(owner);
  const float* pct = (const float*)PyArray_DATA(percent);
  const uint16_t* b = (const uint16_t*)PyArray_DATA(buttons);
  const float* fs = (const float*)PyArray_DATA(frame_speed);
  const int8_t* mx = (const int8_t*)PyArray_DATA(mash_x);
  const int8_t* my = (const int8_t*)PyArray_DATA(mash_y);
  float* timer_p = (float*)PyArray_DATA(out_timer);
  float* counter_p = (float*)PyArray_DATA(out_counter);
  float* anim_p = (float*)PyArray_DATA(out_anim);
  uint8_t* jump_p = (uint8_t*)PyArray_DATA(out_jump);
  uint8_t* break_p = (uint8_t*)PyArray_DATA(out_break);
  if (n > 0 && msl_py_capture_attach_action(a[0]) && own[0] != 0xFFu) {
    msl_py_capture_seed_mid_segment(a[0], af[0], pct[0], fs[0], slot_index, handicap, (float)base,
                                    (float)h_mul, (float)h_base, (float)slot_mul, (float)slot_base,
                                    (float)pct_mul, (float)decrement, (float)hold_frames,
                                    &timer_p[0], &counter_p[0], &anim_p[0], &jump_p[0]);
  }
  for (npy_intp i = 0; i + 1 < n; i++) {
    float timer = timer_p[i];
    float counter = counter_p[i];
    float anim_timer = anim_p[i];
    uint8_t jump_latch = jump_p[i];
    float next_timer = 0.0f;
    float next_counter = 0.0f;
    float next_anim = 0.0f;
    uint8_t next_jump = 0u;
    if (msl_py_capture_attach_action(a[i]) && own[i] != 0xFFu) {
      if (msl_py_capture_wait_or_damage_action(a[i])) {
        counter += 1.0f;
        timer -= (float)decrement;
        // ftCommon_GrabMash for frame i+1 (the step i -> i+1) sees fp-visible inputs that lag
        // the serialized pre-frame rows by one: button edges come from pressed[i], and the
        // stick latch event is visible as the GrabMash-scheduled mx/my lanes moving between
        // rows i and i+1 (see msl_derive_grab_mash_stick_sign_post_py; v12 Dolphin probe
        // windows GAT 2482/5677, AGNG 3208, QGD 8257, CDO 12724, PRH 7602).
        bool mash_active =
            (b[i] & (0x0100u | 0x0200u | 0x0400u | 0x0800u | 0x0040u | 0x0020u)) != 0u;
        if (i + 1 < n && (mx[i + 1] != mx[i] || my[i + 1] != my[i])) mash_active = true;
        if (mash_active) timer -= (float)mash_damage;
        // The x2344 anim-rate window is a CaptureWait_Anim-only mechanism: fn_800DB8A4
        // (CaptureDamage) writes the x8 mash latch and grab timer but never arms/decrements
        // x2344. With the lagged inputs above, any frame that STARTS in CaptureWait runs the
        // wait callback (Damage->Wait and Pulled->Wait first full frames both qualify); the
        // entry-frame callback case (early Pulled->Wait entry, af lands at 1) is handled at
        // the transition write below.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Attack100.c::{
        //   fn_800DB8A4,ftCo_CaptureWaitHi_Anim}
        if (timer > 0.0f && msl_py_capture_wait_action(a[i])) {
          if (anim_timer != 0.0f) {
            anim_timer -= 1.0f;
            if (anim_timer <= 0.0f && !mash_active) anim_timer = 0.0f;
          }
          if (anim_timer <= 0.0f && mash_active) anim_timer = (float)hold_frames;
        }
      }
      if (msl_py_capture_wait_action(a[i])) {
        if (counter < (float)jump_window && (b[i + 1] & 0x0C00u) != 0u) {
          jump_latch = 1u;
        }
      }
      if (msl_py_capture_wait_action(a[i]) && (a[i + 1] == 0x00E5u || a[i + 1] == 0x00E6u)) {
        break_p[i] = 1u;
      }
      if (msl_py_capture_attach_action(a[i + 1]) && own[i + 1] == own[i]) {
        next_timer = fmaxf(0.0f, timer);
        next_counter = fmaxf(0.0f, counter);
        next_anim = fmaxf(0.0f, anim_timer);
        next_jump = jump_latch ? 1u : 0u;
        if (msl_py_capture_wait_action(a[i]) && msl_py_capture_damage_action(a[i + 1])) {
          next_anim = 0.0f;
        }
        const bool family_matched_entry =
            (a[i] == 0x00DFu && a[i + 1] == 0x00E0u) || (a[i] == 0x00E2u && a[i + 1] == 0x00E3u);
        if (family_matched_entry && af[i + 1] == 1 && next_anim <= 0.0f) {
          // Early Pulled->Wait entry: the wait state was installed before the victim's anim
          // phase of the entry frame, so its callback (and GrabMash) ran the same frame --
          // observable as the entry row landing at af == 1. Arm the window when that frame's
          // lagged inputs carry a mash (v12 Dolphin probe, PRH rec 7602: X edge at the entry
          // frame, x2344 = 10.0 and af = 1 at the entry row).
          // Cross-family entries (CapturePulledHi -> CaptureWaitLw) arm vanilla's x2344 but
          // the AObj rate stays 1.0 (TBK rec 5911 window: x2344 counts 10..5 with af stepping
          // +1); the lane intentionally stays 0 there so the teacher-forced rate
          // reconstruction does not boost those rows.
          bool entry_mash =
              (b[i] & (0x0100u | 0x0200u | 0x0400u | 0x0800u | 0x0040u | 0x0020u)) != 0u;
          if (mx[i + 1] != mx[i] || my[i + 1] != my[i]) entry_mash = true;
          if (entry_mash) {
            next_anim = (float)hold_frames;
          }
        }
      }
    }
    if (next_timer == 0.0f && next_counter == 0.0f && msl_py_capture_attach_action(a[i + 1]) &&
        own[i + 1] != 0xFFu) {
      msl_py_capture_seed_mid_segment(
          a[i + 1], af[i + 1], pct[i + 1], fs[i + 1], slot_index, handicap, (float)base,
          (float)h_mul, (float)h_base, (float)slot_mul, (float)slot_base, (float)pct_mul,
          (float)decrement, (float)hold_frames, &next_timer, &next_counter, &next_anim, &next_jump);
    }
    timer_p[i + 1] = next_timer;
    counter_p[i + 1] = next_counter;
    anim_p[i + 1] = next_anim;
    jump_p[i + 1] = next_jump;
  }
  return Py_BuildValue("NNNNN", out_timer, out_counter, out_anim, out_jump, out_break);
}
