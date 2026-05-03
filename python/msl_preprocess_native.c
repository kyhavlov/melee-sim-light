/* Native preprocessing derivation wrappers for the msl_binding extension. */

#define PY_SSIZE_T_CLEAN
#define PY_ARRAY_UNIQUE_SYMBOL MSL_BINDING_ARRAY_API
#define NO_IMPORT_ARRAY

#include "msl_preprocess_native.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../src/action_ids.h"
#include "../src/api.h"
#include "../src/anim_pose.h"
#include "../src/anim_table.h"
#include "../src/attack_id_tables.h"
#include "../src/char_params.h"
#include "../src/common_params.h"
#include "../src/hitboxes_tables.h"
#include "../src/hitlist.h"
#include "../src/hurtcaps_tables.h"
#include "../src/input_axis.h"
#include "../src/shield_tilt_table.h"
#include "../src/specialhi_pose.h"
#include "../src/staling.h"
#include "../src/ucf.h"

static int parse_u16_sequence_fixed(PyObject* obj, uint16_t* out, Py_ssize_t cap,
                                    Py_ssize_t* out_count, const char* name) {
  PyObject* seq = PySequence_Fast(obj, name);
  if (seq == NULL) {
    return -1;
  }
  const Py_ssize_t n = PySequence_Fast_GET_SIZE(seq);
  if (n > cap) {
    PyErr_Format(PyExc_ValueError, "%s has too many entries (got %zd, max %zd)", name, n, cap);
    Py_DECREF(seq);
    return -1;
  }
  for (Py_ssize_t i = 0; i < n; i++) {
    const long v = PyLong_AsLong(PySequence_Fast_GET_ITEM(seq, i));
    if (PyErr_Occurred()) {
      Py_DECREF(seq);
      return -1;
    }
    out[i] = (uint16_t)((uint32_t)v & 0xFFFFu);
  }
  Py_DECREF(seq);
  *out_count = n;
  return 0;
}
static inline bool u16_in_fixed_set(uint16_t v, const uint16_t* set, Py_ssize_t count) {
  for (Py_ssize_t i = 0; i < count; i++) {
    if (set[i] == v) {
      return true;
    }
  }
  return false;
}

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
        if (!v_in_hitstun && combo_timer[v] == 0u) {
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

PyObject* msl_derive_item_spawn_id_counter_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* exists_obj = NULL;
  PyObject* spawn_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &exists_obj, &spawn_obj)) {
    return NULL;
  }
  PyArrayObject* exists = require_contiguous_array(exists_obj, NPY_UINT8, 2, "item_exists_u8_2d");
  PyArrayObject* spawn = require_contiguous_array(spawn_obj, NPY_UINT32, 2, "item_spawn_id_u32_2d");
  if (exists == NULL || spawn == NULL) {
    return NULL;
  }
  if (PyArray_NDIM(exists) != 2 || PyArray_NDIM(spawn) != 2) {
    PyErr_SetString(PyExc_ValueError,
                    "item_exists_u8_2d and item_spawn_id_u32_2d must be exactly 2D");
    return NULL;
  }
  const npy_intp n = PyArray_DIM(spawn, 0);
  const npy_intp w = PyArray_DIM(spawn, 1);
  if (require_exact_2d_shape(exists, n, w, "item_exists_u8_2d") != 0) {
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT32, 0);
  if (out == NULL) {
    return NULL;
  }
  const uint8_t* exists_p = (const uint8_t*)PyArray_DATA(exists);
  const uint32_t* spawn_p = (const uint32_t*)PyArray_DATA(spawn);
  uint32_t* out_p = (uint32_t*)PyArray_DATA(out);
  uint32_t max_seen = 0u;
  uint8_t any_seen = 0u;
  for (npy_intp i = 0; i < n; i++) {
    uint32_t row_max = 0u;
    uint8_t row_any = 0u;
    for (npy_intp k = 0; k < w; k++) {
      const npy_intp ix = i * w + k;
      if (exists_p[ix] != 0u) {
        row_any = 1u;
        if (spawn_p[ix] > row_max) {
          row_max = spawn_p[ix];
        }
      }
    }
    if (row_any) {
      any_seen = 1u;
      if (row_max > max_seen) {
        max_seen = row_max;
      }
    }
    out_p[i] = any_seen ? (max_seen + 1u) : 0u;
  }
  return (PyObject*)out;
}

static inline uint16_t msl_py_inc_attack_instance_counter(uint16_t* counter) {
  const uint16_t before = *counter;
  *counter = (uint16_t)(*counter + 1u);
  if (*counter == 0u) {
    *counter = 1u;
  }
  return before;
}

PyObject* msl_derive_staling_history_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* src_ports_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* percent_obj = NULL;
  PyObject* stocks_obj = NULL;
  PyObject* state_iid_obj = NULL;
  PyObject* last_hit_by_obj = NULL;
  PyObject* last_hit_by_iid_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOO", &src_ports_obj, &char_obj, &action_obj,
                        &action_frame_obj, &anim_obj, &percent_obj, &stocks_obj, &state_iid_obj,
                        &last_hit_by_obj, &last_hit_by_iid_obj)) {
    return NULL;
  }
  PyObject* src_seq = PySequence_Fast(src_ports_obj, "src_ports must be a sequence");
  if (src_seq == NULL) {
    return NULL;
  }
  const Py_ssize_t num_players_ssize = PySequence_Fast_GET_SIZE(src_seq);
  if (num_players_ssize <= 0 || num_players_ssize > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "src_ports length must be in 1..4");
    Py_DECREF(src_seq);
    return NULL;
  }
  const int num_players = (int)num_players_ssize;
  int slot_by_port0[4] = {-1, -1, -1, -1};
  for (int i = 0; i < num_players; i++) {
    const long port1 = PyLong_AsLong(PySequence_Fast_GET_ITEM(src_seq, i));
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

  PyArrayObject* char_id = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id");
  PyArrayObject* action_id = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id");
  PyArrayObject* action_frame =
      require_contiguous_array(action_frame_obj, NPY_FLOAT32, 2, "action_frame");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_UINT32, 2, "animation_index");
  PyArrayObject* percent = require_contiguous_array(percent_obj, NPY_FLOAT32, 2, "percent");
  PyArrayObject* stocks = require_contiguous_array(stocks_obj, NPY_UINT8, 2, "stocks");
  PyArrayObject* state_iid = require_contiguous_array(state_iid_obj, NPY_UINT16, 2, "instance_id");
  PyArrayObject* last_hit_by =
      require_contiguous_array(last_hit_by_obj, NPY_UINT8, 2, "last_hit_by");
  PyArrayObject* last_hit_by_iid =
      require_contiguous_array(last_hit_by_iid_obj, NPY_UINT16, 2, "last_hit_by_instance");
  if (char_id == NULL || action_id == NULL || action_frame == NULL || anim == NULL ||
      percent == NULL || stocks == NULL || state_iid == NULL || last_hit_by == NULL ||
      last_hit_by_iid == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action_id, 0);
  const npy_intp w = PyArray_DIM(action_id, 1);
  if (w != num_players) {
    PyErr_SetString(PyExc_ValueError, "staling arrays must have width equal to len(src_ports)");
    return NULL;
  }
  if (require_exact_2d_shape(char_id, n, w, "char_id") != 0 ||
      require_exact_2d_shape(action_frame, n, w, "action_frame") != 0 ||
      require_exact_2d_shape(anim, n, w, "animation_index") != 0 ||
      require_exact_2d_shape(percent, n, w, "percent") != 0 ||
      require_exact_2d_shape(stocks, n, w, "stocks") != 0 ||
      require_exact_2d_shape(state_iid, n, w, "instance_id") != 0 ||
      require_exact_2d_shape(last_hit_by, n, w, "last_hit_by") != 0 ||
      require_exact_2d_shape(last_hit_by_iid, n, w, "last_hit_by_instance") != 0) {
    return NULL;
  }
  if (attack_id_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "attack_id_tables_init failed");
    return NULL;
  }

  npy_intp dims2[2] = {n, w};
  npy_intp dims3[3] = {n, w, 10};
  PyArrayObject* out_attack_id = (PyArrayObject*)PyArray_EMPTY(2, dims2, NPY_UINT16, 0);
  PyArrayObject* out_attack_inst = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT16, 0);
  PyArrayObject* out_qi = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* out_mid = (PyArrayObject*)PyArray_ZEROS(3, dims3, NPY_UINT16, 0);
  PyArrayObject* out_inst = (PyArrayObject*)PyArray_ZEROS(3, dims3, NPY_UINT16, 0);
  if (out_attack_id == NULL || out_attack_inst == NULL || out_qi == NULL || out_mid == NULL ||
      out_inst == NULL) {
    Py_XDECREF(out_attack_id);
    Py_XDECREF(out_attack_inst);
    Py_XDECREF(out_qi);
    Py_XDECREF(out_mid);
    Py_XDECREF(out_inst);
    return NULL;
  }

  uint16_t* map_mid = (uint16_t*)PyMem_Malloc((size_t)num_players * 65536u * sizeof(uint16_t));
  uint16_t* map_inst = (uint16_t*)PyMem_Malloc((size_t)num_players * 65536u * sizeof(uint16_t));
  if (map_mid == NULL || map_inst == NULL) {
    PyMem_Free(map_mid);
    PyMem_Free(map_inst);
    Py_DECREF(out_attack_id);
    Py_DECREF(out_attack_inst);
    Py_DECREF(out_qi);
    Py_DECREF(out_mid);
    Py_DECREF(out_inst);
    return PyErr_NoMemory();
  }
  for (size_t i = 0; i < (size_t)num_players * 65536u; i++) {
    map_mid[i] = 0xFFFFu;
    map_inst[i] = 0u;
  }

  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_id);
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action_id);
  const float* frame_p = (const float*)PyArray_DATA(action_frame);
  const uint32_t* anim_p = (const uint32_t*)PyArray_DATA(anim);
  const float* percent_p = (const float*)PyArray_DATA(percent);
  const uint8_t* stocks_p = (const uint8_t*)PyArray_DATA(stocks);
  const uint16_t* iid_p = (const uint16_t*)PyArray_DATA(state_iid);
  const uint8_t* last_hit_p = (const uint8_t*)PyArray_DATA(last_hit_by);
  const uint16_t* last_hit_iid_p = (const uint16_t*)PyArray_DATA(last_hit_by_iid);
  uint16_t* out_attack_id_p = (uint16_t*)PyArray_DATA(out_attack_id);
  uint16_t* out_attack_inst_p = (uint16_t*)PyArray_DATA(out_attack_inst);
  uint8_t* out_qi_p = (uint8_t*)PyArray_DATA(out_qi);
  uint16_t* out_mid_p = (uint16_t*)PyArray_DATA(out_mid);
  uint16_t* out_inst_p = (uint16_t*)PyArray_DATA(out_inst);

  uint8_t qi[MSL_MAX_PLAYERS] = {0};
  uint16_t table_mid[MSL_MAX_PLAYERS][10] = {{0}};
  uint16_t table_inst[MSL_MAX_PLAYERS][10] = {{0}};
  uint16_t cur_attack_id[MSL_MAX_PLAYERS];
  uint16_t cur_attack_inst[MSL_MAX_PLAYERS] = {0};
  uint16_t prev_action_id[MSL_MAX_PLAYERS];
  uint16_t prev_state_iid[MSL_MAX_PLAYERS] = {0};
  for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
    cur_attack_id[p] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
    prev_action_id[p] = 0xFFFFu;
  }
  uint16_t stale_attack_counter = 1u;
#define RESET_STALE_TABLE(P)         \
  do {                               \
    qi[(P)] = 0u;                    \
    memset(table_mid[(P)], 0, 20u);  \
    memset(table_inst[(P)], 0, 20u); \
  } while (0)
#define RESET_ATTACK_IDENTITY(P)                           \
  do {                                                     \
    cur_attack_id[(P)] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT; \
    cur_attack_inst[(P)] = 0u;                             \
    prev_action_id[(P)] = 0xFFFFu;                         \
    prev_state_iid[(P)] = 0u;                              \
  } while (0)

  const uint16_t ACT_GUARD_ON = 178u;
  const uint16_t ACT_GUARD = 179u;
  const uint16_t ACT_GUARD_OFF = 180u;
  const uint16_t ACT_FX_SPECIAL_N_LOOP = 0x0156u;
  const uint16_t ACT_FX_SPECIAL_AIR_N_LOOP = 0x0159u;
  const uint32_t NO_SUBMOTION_INDEX = 0xFFFFFFFFu;

  for (npy_intp t = 0; t < n; t++) {
    if (t > 0) {
      for (int p = 0; p < num_players; p++) {
        if (stocks_p[t * w + p] < stocks_p[(t - 1) * w + p]) {
          RESET_STALE_TABLE(p);
          RESET_ATTACK_IDENTITY(p);
        }
      }
    }

    for (int p = 0; p < num_players; p++) {
      const npy_intp pi = t * w + p;
      const uint16_t iid = iid_p[pi];
      if (iid == 0u) {
        RESET_ATTACK_IDENTITY(p);
        out_attack_id_p[pi] = (uint16_t)MSL_FT_MOVE_ID_DEFAULT;
        continue;
      }
      const uint16_t act = action_p[pi];
      if (act != prev_action_id[p]) {
        if (act == ACT_GUARD_OFF && prev_action_id[p] == ACT_GUARD_ON && t > 0 &&
            action_p[(t - 1) * w + p] == ACT_GUARD_ON &&
            anim_p[(t - 1) * w + p] == NO_SUBMOTION_INDEX && frame_p[(t - 1) * w + p] < 0.0f) {
          const uint16_t hidden_guard_inst =
              msl_py_inc_attack_instance_counter(&stale_attack_counter);
          cur_attack_id[p] = attack_id_move_id_from_action(char_p[pi], ACT_GUARD);
          cur_attack_inst[p] = hidden_guard_inst;
        }

        const uint16_t move_id = attack_id_move_id_from_action(char_p[pi], act);
        if (move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT || move_id != cur_attack_id[p]) {
          cur_attack_id[p] = move_id;
          cur_attack_inst[p] = msl_py_inc_attack_instance_counter(&stale_attack_counter);
        }
        prev_action_id[p] = act;
      } else if ((act == ACT_FX_SPECIAL_N_LOOP || act == ACT_FX_SPECIAL_AIR_N_LOOP) &&
                 iid != prev_state_iid[p] && frame_p[pi] == 0.0f) {
        cur_attack_inst[p] = msl_py_inc_attack_instance_counter(&stale_attack_counter);
      }

      if (iid != prev_state_iid[p]) {
        const size_t mi = (size_t)p * 65536u + (size_t)iid;
        if (map_mid[mi] == 0xFFFFu && map_inst[mi] == 0u) {
          map_mid[mi] = cur_attack_id[p];
          map_inst[mi] = cur_attack_inst[p];
        }
        prev_state_iid[p] = iid;
      }
      out_attack_inst_p[pi] = cur_attack_inst[p];
      out_attack_id_p[pi] = cur_attack_id[p];
    }

    if (t > 0) {
      for (int victim = 0; victim < num_players; victim++) {
        const npy_intp vi = t * w + victim;
        const float dp = percent_p[vi] - percent_p[(t - 1) * w + victim];
        if (!(dp > 0.0f)) {
          continue;
        }
        int attacker = -1;
        const uint8_t port0 = last_hit_p[vi];
        if (port0 < 4u) {
          attacker = slot_by_port0[port0];
        }
        if (attacker < 0) {
          const uint16_t hit_iid = last_hit_iid_p[vi];
          if (hit_iid != 0u) {
            int match = -1;
            int match_count = 0;
            for (int p = 0; p < num_players; p++) {
              if (iid_p[t * w + p] == hit_iid) {
                match = p;
                match_count++;
              }
            }
            if (match_count == 1) {
              attacker = match;
            }
          }
        }
        if (attacker < 0 || attacker == victim) {
          continue;
        }

        uint16_t att_move_id = 0xFFFFu;
        uint16_t att_attack_inst = 0u;
        const uint16_t hit_iid = last_hit_iid_p[vi];
        if (hit_iid != 0u) {
          const size_t mi = (size_t)attacker * 65536u + (size_t)hit_iid;
          if (map_mid[mi] != 0xFFFFu || map_inst[mi] != 0u) {
            att_move_id = map_mid[mi];
            att_attack_inst = map_inst[mi];
          }
        }
        if (att_move_id == 0xFFFFu || att_attack_inst == 0u) {
          att_move_id = cur_attack_id[attacker];
          att_attack_inst = cur_attack_inst[attacker];
        }
        if (att_move_id == 0xFFFFu || att_move_id == (uint16_t)MSL_FT_MOVE_ID_DEFAULT ||
            att_attack_inst == 0u) {
          continue;
        }
        uint8_t duplicate = 0u;
        for (int k = 0; k < 10; k++) {
          if (table_mid[attacker][k] == att_move_id && table_inst[attacker][k] == att_attack_inst) {
            duplicate = 1u;
            break;
          }
        }
        if (!duplicate) {
          int pos = qi[attacker];
          if (pos >= 10) {
            pos = 0;
          }
          table_mid[attacker][pos] = att_move_id;
          table_inst[attacker][pos] = att_attack_inst;
          qi[attacker] = (uint8_t)(pos == 9 ? 0 : pos + 1);
        }
      }
    }

    for (int p = 0; p < num_players; p++) {
      out_qi_p[t * w + p] = qi[p];
      for (int k = 0; k < 10; k++) {
        const npy_intp oi = (t * w + p) * 10 + k;
        out_mid_p[oi] = table_mid[p][k];
        out_inst_p[oi] = table_inst[p][k];
      }
    }
  }

#undef RESET_STALE_TABLE
#undef RESET_ATTACK_IDENTITY
  PyMem_Free(map_mid);
  PyMem_Free(map_inst);
  return Py_BuildValue("NNNNN", out_attack_id, out_attack_inst, out_qi, out_mid, out_inst);
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

PyObject* msl_derive_grab_mash_stick_sign_post_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* sx_obj = NULL;
  PyObject* sy_obj = NULL;
  double threshold = 0.0;
  if (!PyArg_ParseTuple(args, "OOd", &sx_obj, &sy_obj, &threshold)) {
    return NULL;
  }
  PyArrayObject* sx = require_contiguous_array(sx_obj, NPY_FLOAT32, 1, "stick_x_unit");
  PyArrayObject* sy = require_contiguous_array(sy_obj, NPY_FLOAT32, 1, "stick_y_unit");
  if (sx == NULL || sy == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(sx);
  if (PyArray_SIZE(sy) != n) {
    PyErr_SetString(PyExc_ValueError, "stick_x_unit and stick_y_unit must match");
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
  int8_t* out_x_p = (int8_t*)PyArray_DATA(out_x);
  int8_t* out_y_p = (int8_t*)PyArray_DATA(out_y);
  const float thresh = (float)threshold;
  int8_t latch_x = 0;
  int8_t latch_y = 0;
  for (npy_intp i = 0; i < n; i++) {
    if (sx_p[i] < -thresh) {
      latch_x = -1;
    } else if (sx_p[i] > thresh) {
      latch_x = 1;
    }
    if (sy_p[i] < -thresh) {
      latch_y = -1;
    } else if (sy_p[i] > thresh) {
      latch_y = 1;
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

PyObject* msl_derive_guard_setoff_post_hitlag_owner_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* phase_obj = NULL;
  PyObject* flags_obj = NULL;
  int act_guard_set_off = 0;
  if (!PyArg_ParseTuple(args, "OOOi", &action_obj, &phase_obj, &flags_obj, &act_guard_set_off)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* phase =
      require_contiguous_array(phase_obj, NPY_UINT8, 1, "guard_setoff_hitlag_exit_phase_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 1, "state_flags_221c_u8");
  if (action == NULL || phase == NULL || flags == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(phase) != n || PyArray_SIZE(flags) != n) {
    PyErr_SetString(PyExc_ValueError, "action_id/phase/state_flags_221c must have the same length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* ph = (const uint8_t*)PyArray_DATA(phase);
  const uint8_t* fl = (const uint8_t*)PyArray_DATA(flags);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n; i++) {
    if ((int)a[i] != act_guard_set_off) continue;
    if (ph[i] != 2u && ph[i] != 3u) continue;
    out_p[i] = (uint8_t)((fl[i] & 0x20u) != 0u ? 2u : 1u);
  }
  return (PyObject*)out;
}

PyObject* msl_derive_run_x0_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  double run_x0_init = 0.0;
  int act_run = 0;
  int act_run_direct = 0;
  int act_turn_run = 0;
  if (!PyArg_ParseTuple(args, "OOdiii", &action_obj, &hitlag_obj, &run_x0_init, &act_run,
                        &act_run_direct, &act_turn_run)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  if (action == NULL || hitlag == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitlag) != n) {
    PyErr_SetString(PyExc_ValueError, "action_id and hitlag_u16 must have the same length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  int init = (int)run_x0_init;
  if (init < 0) init = 0;
  if (init > 255) init = 255;
  for (npy_intp i = 1; i < n; i++) {
    const int prev_a = (int)a[i - 1];
    const int cur_a = (int)a[i];
    const int prev_x0 = (int)out_p[i - 1];
    int x0 = 0;
    if (cur_a == act_run || cur_a == act_run_direct) {
      if (prev_a == act_turn_run) {
        x0 = init;
      } else if (prev_a == cur_a) {
        x0 = prev_x0;
        if (hl[i - 1] == 0u && x0 > 0) x0 -= 1;
      }
    }
    out_p[i] = (uint8_t)x0;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_runbrake_cmd0_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* on_obj = NULL;
  PyObject* off_obj = NULL;
  int act_run_brake = 0;
  if (!PyArg_ParseTuple(args, "OOOOOi", &action_obj, &anim_obj, &char_obj, &on_obj, &off_obj,
                        &act_run_brake)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  if (action == NULL || anim == NULL || chr == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(anim) != n || PyArray_SIZE(chr) != n) {
    PyErr_SetString(PyExc_ValueError, "action_id_u16/anim_frame_f32/char_id_u8 must match length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const float* af = (const float*)PyArray_DATA(anim);
  const uint8_t* cid = (const uint8_t*)PyArray_DATA(chr);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n; i++) {
    if ((int)a[i] != act_run_brake) continue;
    PyObject* key = PyLong_FromLong((long)cid[i]);
    if (key == NULL) return NULL;
    PyObject* on_val = PyObject_GetItem(on_obj, key);
    PyObject* off_val = PyObject_GetItem(off_obj, key);
    Py_DECREF(key);
    if (on_val == NULL || off_val == NULL) {
      PyErr_Clear();
      Py_XDECREF(on_val);
      Py_XDECREF(off_val);
      continue;
    }
    const long start = PyLong_AsLong(on_val);
    const long end = PyLong_AsLong(off_val);
    Py_DECREF(on_val);
    Py_DECREF(off_val);
    if (PyErr_Occurred()) return NULL;
    if (start < 0 || end < 0 || end < start) continue;
    if (isfinite(af[i]) && af[i] >= (float)start && af[i] < (float)end) {
      out_p[i] = 1u;
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_dash_x4_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  int act_dash = 0;
  int act_turn = 0;
  if (!PyArg_ParseTuple(args, "OOii", &action_obj, &frame_obj, &act_dash, &act_turn)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  if (action == NULL || frame == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n) {
    PyErr_SetString(PyExc_ValueError, "action_frame_i16 must match action_id_u16 length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  const uint16_t dash = (uint16_t)((uint32_t)act_dash & 0xFFFFu);
  const uint16_t turn = (uint16_t)((uint32_t)act_turn & 0xFFFFu);
  uint8_t x4 = 0u;
  for (npy_intp i = 0; i < n; i++) {
    if (a[i] != dash) {
      x4 = 0u;
      continue;
    }
    const uint16_t prev_a = i > 0 ? a[i - 1] : a[i];
    const int16_t prev_af = i > 0 ? af[i - 1] : af[i];
    const bool dash_entry = i == 0 || a[i] != prev_a || af[i] < prev_af;
    if (dash_entry) {
      x4 = (uint8_t)(prev_a == turn ? 0u : 1u);
    }
    out_p[i] = x4;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_ecb_lock_timer_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* ground_obj = NULL;
  PyObject* action_obj = NULL;
  int lock_frames = 10;
  int act_jump_f = 0;
  int act_jump_b = 0;
  int act_jump_aerial_f = 0;
  int act_jump_aerial_b = 0;
  if (!PyArg_ParseTuple(args, "OOiiiii", &ground_obj, &action_obj, &lock_frames, &act_jump_f,
                        &act_jump_b, &act_jump_aerial_f, &act_jump_aerial_b)) {
    return NULL;
  }
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  if (ground == NULL || action == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(ground);
  if (PyArray_SIZE(action) != n) {
    PyErr_SetString(PyExc_ValueError, "action_id_u16 must match on_ground_u8 length");
    return NULL;
  }
  if (lock_frames < 0) lock_frames = 0;
  if (lock_frames > 255) lock_frames = 255;
  const int set_post = lock_frames > 0 ? lock_frames - 1 : 0;
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint8_t* g = (const uint8_t*)PyArray_DATA(ground);
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  uint8_t* out_p = (uint8_t*)PyArray_DATA(out);
  int timer = 0;
  bool prev_ground = n > 0 && g[0] != 0u;
  for (npy_intp i = 0; i < n; i++) {
    const bool cur_ground = g[i] != 0u;
    const int cur_action = (int)a[i];
    const int prev_action = i > 0 ? (int)a[i - 1] : cur_action;
    const bool is_jump = cur_action == act_jump_f || cur_action == act_jump_b ||
                         cur_action == act_jump_aerial_f || cur_action == act_jump_aerial_b;
    const bool jump_entry = i > 0 && is_jump && cur_action != prev_action;
    if (cur_ground) {
      timer = 0;
    } else if (jump_entry || (i > 0 && prev_ground)) {
      timer = set_post;
    } else if (timer > 0) {
      timer -= 1;
    }
    out_p[i] = (uint8_t)timer;
    prev_ground = cur_ground;
  }
  return (PyObject*)out;
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

PyObject* msl_derive_downwait_timer_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitstun_obj = Py_None;
  int down_wait_frames = 0;
  int act_down_damage_u = -1;
  int act_down_damage_d = -1;
  int act_down_wait_u = 0;
  int act_down_wait_d = 0;
  if (!PyArg_ParseTuple(args, "OOiiiii", &action_obj, &hitstun_obj, &down_wait_frames,
                        &act_down_damage_u, &act_down_damage_d, &act_down_wait_u,
                        &act_down_wait_d)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* hitstun = NULL;
  if (hitstun_obj != Py_None) {
    hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  }
  if (action == NULL || (hitstun_obj != Py_None && hitstun == NULL)) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (hitstun != NULL && PyArray_SIZE(hitstun) != n) {
    PyErr_SetString(PyExc_ValueError, "hitstun_u16 must match action_id_u16 length");
    return NULL;
  }
  if (down_wait_frames < 0) down_wait_frames = 0;
  if (down_wait_frames > 0x7FFF) down_wait_frames = 0x7FFF;
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_INT16, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hs = hitstun != NULL ? (const uint16_t*)PyArray_DATA(hitstun) : NULL;
  int16_t* out_p = (int16_t*)PyArray_DATA(out);
  int timer = 0;
  bool prev_is_dw = false;
  for (npy_intp i = 0; i < n; i++) {
    const bool is_dw = (int)a[i] == act_down_wait_u || (int)a[i] == act_down_wait_d;
    if (!is_dw) {
      timer = 0;
      prev_is_dw = false;
      continue;
    }
    if (!prev_is_dw) {
      const int prev_a = i > 0 ? (int)a[i - 1] : 0;
      const bool prev_was_down_damage =
          i > 0 && act_down_damage_u >= 0 && act_down_damage_d >= 0 &&
          (prev_a == act_down_damage_u || prev_a == act_down_damage_d);
      if (prev_was_down_damage && hs != NULL) {
        timer = (int)hs[i - 1] - 1;
        if (timer < 1) timer = 1;
        if (timer > down_wait_frames) timer = down_wait_frames;
      } else {
        timer = down_wait_frames;
      }
    } else if (timer > 0) {
      timer -= 1;
    }
    out_p[i] = (int16_t)timer;
    prev_is_dw = true;
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
  float x4 = 0.0f;
  const float lerp = (float)lerp_d;
  const float rad_to_deg = 180.0f / 3.14159265358979323846f;
  for (npy_intp i = 0; i < n; i++) {
    const int a = (int)aid[i];
    const uint16_t neutral_i = neu[i];
    const uint16_t frame_max_i = fmax[i];
    if (a == act_guard_on && afr[i] == 0) {
      x8 = neutral_i;
      x4 = 0.0f;
    }
    if (a == act_guard_on || a == act_guard || a == act_guard_reflect) {
      const float facing_dir = fac[i] != 0u ? 1.0f : -1.0f;
      const float x = sx[i] * facing_dir;
      const float y = sy[i];
      float rad = atan2f(y, x);
      if (rad < 0.0f) rad += 2.0f * 3.14159265358979323846f;
      float deg = rad * rad_to_deg;
      if (deg < 0.0f) deg = 0.0f;
      if (deg > 359.0f) deg = 359.0f;
      const float offset = (float)x8 - (float)neutral_i;
      float delta = deg - offset;
      if (delta > 180.0f) {
        delta -= 360.0f;
      } else if (delta < -180.0f) {
        delta += 360.0f;
      }
      float next_offset = delta * lerp + offset;
      if (next_offset > 360.0f) {
        next_offset -= 360.0f;
      } else if (next_offset < 0.0f) {
        next_offset += 360.0f;
      }
      const float next_x8_f = (float)neutral_i + next_offset;
      int next_x8 = (int)next_x8_f;
      if (next_x8 < 0) next_x8 = 0;
      if (next_x8 > (int)frame_max_i) next_x8 = (int)frame_max_i;
      x8 = (uint16_t)next_x8;
      float mag = sqrtf((sx[i] * sx[i]) + (sy[i] * sy[i]));
      if (mag > 1.0f) mag = 1.0f;
      if (mag < 0.0f) mag = 0.0f;
      x4 = lerp * (mag - x4) + x4;
    }
    out_x8_p[i] = x8;
    out_x4_p[i] = x4;
  }
  return Py_BuildValue("NN", out_x8, out_x4);
}

PyObject* msl_derive_shine_release_state_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* held_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* lag_init_obj = NULL;
  int button_mask_b = 0;
  int acts[10] = {0};
  if (!PyArg_ParseTuple(args, "OOOOOiiiiiiiiiii", &action_obj, &frame_obj, &held_obj, &hitlag_obj,
                        &lag_init_obj, &button_mask_b, &acts[0], &acts[1], &acts[2], &acts[3],
                        &acts[4], &acts[5], &acts[6], &acts[7], &acts[8], &acts[9])) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* held = require_contiguous_array(held_obj, NPY_UINT16, 1, "buttons_held_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* lag_init =
      require_contiguous_array(lag_init_obj, NPY_UINT8, 1, "release_lag_init_u8");
  if (action == NULL || frame == NULL || held == NULL || hitlag == NULL || lag_init == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(held) != n || PyArray_SIZE(hitlag) != n ||
      PyArray_SIZE(lag_init) != n) {
    PyErr_SetString(PyExc_ValueError, "shine release inputs must have matching length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_lag = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_rel = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out_lag == NULL || out_rel == NULL) {
    Py_XDECREF(out_lag);
    Py_XDECREF(out_rel);
    return NULL;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* buttons = (const uint16_t*)PyArray_DATA(held);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint8_t* init = (const uint8_t*)PyArray_DATA(lag_init);
  uint8_t* lag_p = (uint8_t*)PyArray_DATA(out_lag);
  uint8_t* rel_p = (uint8_t*)PyArray_DATA(out_rel);
  int lag = 0;
  uint8_t is_release = 0u;
  const uint16_t mask_b = (uint16_t)((uint32_t)button_mask_b & 0xFFFFu);
  for (npy_intp i = 0; i < n; i++) {
    const int cur_a = (int)a[i];
    const int cur_af = (int)af[i];
    const int prev_a = i > 0 ? (int)a[i - 1] : cur_a;
    const int prev_af = i > 0 ? (int)af[i - 1] : cur_af;
    const bool is_ground_start = cur_a == acts[0];
    const bool is_air_start = cur_a == acts[5];
    const bool in_start = is_ground_start || is_air_start;
    const bool in_latch = cur_a == acts[0] || cur_a == acts[5] || cur_a == acts[1] ||
                          cur_a == acts[2] || cur_a == acts[4] || cur_a == acts[6] ||
                          cur_a == acts[7] || cur_a == acts[9];
    const bool in_tick = cur_a == acts[1] || cur_a == acts[2] || cur_a == acts[4] ||
                         cur_a == acts[6] || cur_a == acts[7] || cur_a == acts[9];
    bool in_any = false;
    for (int k = 0; k < 10; k++) {
      if (cur_a == acts[k]) {
        in_any = true;
        break;
      }
    }
    if (!in_any) {
      lag = 0;
      is_release = 0u;
      continue;
    }
    bool shine_start_entry = false;
    bool skip_tick = false;
    if (in_start) {
      bool prev_in_any = false;
      for (int k = 0; k < 10; k++) {
        if (prev_a == acts[k]) {
          prev_in_any = true;
          break;
        }
      }
      if (i == 0 || !prev_in_any || (cur_a == prev_a && cur_af < prev_af)) {
        shine_start_entry = true;
      }
    }
    if (shine_start_entry) {
      lag = (int)init[i];
      is_release = 0u;
      skip_tick = true;
    } else if (i == 0) {
      lag = (int)init[i] - (cur_af + 1);
      if (lag < 0) lag = 0;
      is_release = 0u;
    }
    if (in_latch && !skip_tick && hl[i] == 0u) {
      if ((buttons[i] & mask_b) == 0u) is_release = 1u;
    }
    if (in_tick && !skip_tick && hl[i] == 0u && lag > 0) {
      lag -= 1;
    }
    lag_p[i] = (uint8_t)lag;
    rel_p[i] = is_release;
  }
  return Py_BuildValue("NN", out_lag, out_rel);
}

PyObject* msl_derive_kneebend_internals_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* buttons_obj = NULL;
  PyObject* pressed_obj = NULL;
  PyObject* stick_y_obj = NULL;
  PyObject* cstick_y_obj = NULL;
  PyObject* tilt_y_obj = NULL;
  double tap_thr_d = 0.0;
  double dash_run_thr_d = 0.0;
  int tilt_max = 0;
  double release_thr_d = 0.0;
  int act_kneebend = 0;
  int act_dash = 0;
  int act_run = 0;
  int act_run_direct = 0;
  int act_run_brake = 0;
  int act_turn_run = 0;
  int button_mask_xy = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOddidiiiiiii", &action_obj, &buttons_obj, &pressed_obj,
                        &stick_y_obj, &cstick_y_obj, &tilt_y_obj, &tap_thr_d, &dash_run_thr_d,
                        &tilt_max, &release_thr_d, &act_kneebend, &act_dash, &act_run,
                        &act_run_direct, &act_run_brake, &act_turn_run, &button_mask_xy)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* buttons = require_contiguous_array(buttons_obj, NPY_UINT16, 1, "buttons");
  PyArrayObject* pressed = require_contiguous_array(pressed_obj, NPY_UINT16, 1, "buttons_pressed");
  PyArrayObject* stick_y = require_contiguous_array(stick_y_obj, NPY_FLOAT32, 1, "stick_y_unit");
  PyArrayObject* cstick_y = require_contiguous_array(cstick_y_obj, NPY_FLOAT32, 1, "cstick_y_unit");
  PyArrayObject* tilt_y = require_contiguous_array(tilt_y_obj, NPY_UINT8, 1, "tilt_timer_y");
  if (action == NULL || buttons == NULL || pressed == NULL || stick_y == NULL || cstick_y == NULL ||
      tilt_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(buttons) != n || PyArray_SIZE(pressed) != n || PyArray_SIZE(stick_y) != n ||
      PyArray_SIZE(cstick_y) != n || PyArray_SIZE(tilt_y) != n) {
    PyErr_SetString(PyExc_ValueError, "kneebend inputs must have the same length");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_jump = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_short = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out_jump == NULL || out_short == NULL) {
    Py_XDECREF(out_jump);
    Py_XDECREF(out_short);
    return NULL;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* b = (const uint16_t*)PyArray_DATA(buttons);
  const uint16_t* bp = (const uint16_t*)PyArray_DATA(pressed);
  const float* sy = (const float*)PyArray_DATA(stick_y);
  const float* cy = (const float*)PyArray_DATA(cstick_y);
  const uint8_t* tty = (const uint8_t*)PyArray_DATA(tilt_y);
  uint8_t* jump_p = (uint8_t*)PyArray_DATA(out_jump);
  uint8_t* short_p = (uint8_t*)PyArray_DATA(out_short);
  uint8_t jump_input = 0u;
  uint8_t is_short_hop = 0u;
  bool prev_in = false;
  const float tap_thr = (float)tap_thr_d;
  const float dash_run_thr = (float)dash_run_thr_d;
  const float rel_thr = (float)release_thr_d;
  const uint16_t xy = (uint16_t)((uint32_t)button_mask_xy & 0xFFFFu);
  for (npy_intp i = 0; i < n; i++) {
    const bool cur_in = (int)a[i] == act_kneebend;
    if (!cur_in) {
      jump_input = 0u;
      is_short_hop = 0u;
      prev_in = false;
      continue;
    }
    if (!prev_in) {
      jump_input = 0u;
      const int prev_action = i > 0 ? (int)a[i - 1] : 0xFFFF;
      const bool dash_src = prev_action == act_dash || prev_action == act_run ||
                            prev_action == act_run_direct || prev_action == act_run_brake ||
                            prev_action == act_turn_run;
      if (dash_src) {
        if ((bp[i] & xy) != 0u) {
          jump_input = 3u;
        } else if (sy[i] >= dash_run_thr && (int)tty[i] < tilt_max) {
          jump_input = 1u;
        } else if (cy[i] >= tap_thr) {
          jump_input = 2u;
        }
      } else {
        if (sy[i] >= tap_thr && (int)tty[i] < tilt_max) {
          jump_input = 1u;
        } else if ((bp[i] & xy) != 0u) {
          jump_input = 3u;
        } else if (cy[i] >= tap_thr) {
          jump_input = 2u;
        }
      }
      is_short_hop = 0u;
    }
    if (!is_short_hop) {
      if (jump_input == 3u) {
        if ((b[i] & xy) == 0u) is_short_hop = 1u;
      } else if (jump_input == 1u) {
        if (sy[i] < rel_thr) is_short_hop = 1u;
      } else if (jump_input == 2u) {
        if (cy[i] < rel_thr) is_short_hop = 1u;
      }
    }
    jump_p[i] = jump_input;
    short_p[i] = is_short_hop;
    prev_in = true;
  }
  return Py_BuildValue("NN", out_jump, out_short);
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

static inline bool msl_py_capture_attach_action(uint16_t a) {
  return a == 0x00DFu || a == 0x00E0u || a == 0x00E1u || a == 0x00E2u || a == 0x00E3u ||
         a == 0x00E4u;
}

static inline bool msl_py_capture_wait_action(uint16_t a) { return a == 0x00E0u || a == 0x00E3u; }

static inline bool msl_py_capture_damage_action(uint16_t a) { return a == 0x00E1u || a == 0x00E4u; }

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
  PyObject* buttons_obj = NULL;
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
                        &percent_obj, &buttons_obj, &stick_x_obj, &stick_y_obj, &frame_speed_obj,
                        &mash_x_obj, &mash_y_obj, &slot_index, &handicap, &base, &h_mul, &h_base,
                        &slot_mul, &slot_base, &pct_mul, &decrement, &mash_damage, &hold_frames,
                        &jump_window, &stick_threshold)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* owner = require_contiguous_array(owner_obj, NPY_UINT8, 1, "grab_owner_port_u8");
  PyArrayObject* percent = require_contiguous_array(percent_obj, NPY_FLOAT32, 1, "percent_f32");
  PyArrayObject* buttons = require_contiguous_array(buttons_obj, NPY_UINT16, 1, "buttons_held_u16");
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
  const float* sx = (const float*)PyArray_DATA(stick_x);
  const float* sy = (const float*)PyArray_DATA(stick_y);
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
        const uint16_t held = b[i + 1];
        bool mash_active =
            (held & (0x0100u | 0x0200u | 0x0400u | 0x0800u | 0x0040u | 0x0020u)) != 0u;
        int8_t next_x = mx[i];
        int8_t next_y = my[i];
        if (sx[i + 1] < -(float)stick_threshold) {
          next_x = -1;
        } else if (sx[i + 1] > (float)stick_threshold) {
          next_x = 1;
        }
        if (sy[i + 1] < -(float)stick_threshold) {
          next_y = -1;
        } else if (sy[i + 1] > (float)stick_threshold) {
          next_y = 1;
        }
        if (next_x != mx[i] || next_y != my[i]) mash_active = true;
        if (mash_active) timer -= (float)mash_damage;
        if (timer > 0.0f) {
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

PyObject* msl_derive_ledge_cooldown_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  int cooldown_frames = 0;
  if (!PyArg_ParseTuple(args, "OOi", &action_obj, &hitlag_obj, &cooldown_frames)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  if (action == NULL || hitlag == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(hitlag) != n) {
    PyErr_SetString(PyExc_ValueError, "ledge cooldown inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* h = (const uint16_t*)PyArray_DATA(hitlag);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  int seed = cooldown_frames;
  if (seed < 0) seed = 0;
  if (seed > 255) seed = 255;
  if (seed > 0) seed -= 1;
  for (npy_intp t = 1; t < n; t++) {
    int cd = o[t - 1];
    if (h[t - 1] == 0u && cd > 0) cd -= 1;
    if (a[t - 1] == 0x00FDu && a[t] >= 0x001Du && a[t] <= 0x0026u) {
      cd = seed;
    }
    o[t] = (uint8_t)cd;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_match_flow_timer_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  int port0 = 0;
  int dead_timer = 0;
  int dead_up_star_initial = 0;
  int dead_up_star_phase1 = 0;
  int dead_up_star_phase2 = 0;
  int dead_up_fall_entry = 0;
  int dead_up_fall_lerp = 0;
  int dead_up_fall_hitcamera = 0;
  int dead_up_fall_phase3 = 0;
  int dead_up_fall_phase4 = 0;
  int rebirth_timer = 0;
  int rebirth_wait_timer = 0;
  int entry_start_frames = 0;
  int entry_end_frames = 0;
  if (!PyArg_ParseTuple(args, "Oiiiiiiiiiiiiii", &action_obj, &port0, &dead_timer,
                        &dead_up_star_initial, &dead_up_star_phase1, &dead_up_star_phase2,
                        &dead_up_fall_entry, &dead_up_fall_lerp, &dead_up_fall_hitcamera,
                        &dead_up_fall_phase3, &dead_up_fall_phase4, &rebirth_timer,
                        &rebirth_wait_timer, &entry_start_frames, &entry_end_frames)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  if (action == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  const int dead_up_star_total = (dead_up_star_initial > 0 ? dead_up_star_initial : 0) +
                                 (dead_up_star_phase1 > 0 ? dead_up_star_phase1 : 0) +
                                 (dead_up_star_phase2 > 0 ? dead_up_star_phase2 : 0);
  const int dead_up_fall_total = (dead_up_fall_entry > 0 ? dead_up_fall_entry : 0) +
                                 (dead_up_fall_lerp > 0 ? dead_up_fall_lerp : 0);
  const int dead_up_fall_hitcamera_total =
      (dead_up_fall_hitcamera > 0 ? dead_up_fall_hitcamera : 0) +
      (dead_up_fall_phase3 > 0 ? dead_up_fall_phase3 : 0) +
      (dead_up_fall_phase4 > 0 ? dead_up_fall_phase4 : 0);
  const int entry_total = 5 * (port0 + 1);
  uint16_t prev = 0xFFFFu;
  int run_len = 0;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t ai = a[i];
    if (i > 0 && ai == prev) {
      run_len += 1;
    } else {
      prev = ai;
      run_len = 1;
    }
    int total = -1;
    if (ai == 0u || ai == 1u || ai == 2u) {
      total = dead_timer;
    } else if (ai == 4u) {
      total = dead_up_star_total;
    } else if (ai == 6u || ai == 9u) {
      total = dead_up_fall_total;
    } else if (ai == 7u || ai == 8u || ai == 10u) {
      total = dead_up_fall_hitcamera_total;
    } else if (ai == 12u) {
      total = rebirth_timer;
    } else if (ai == 13u) {
      total = rebirth_wait_timer;
    } else if (ai == 322u) {
      total = entry_total;
    } else if (ai == 323u) {
      total = entry_start_frames > 0 ? entry_start_frames - 1 : 0;
    } else if (ai == 324u) {
      total = entry_end_frames;
    }
    if (total < 0) continue;
    int timer = total - run_len + 1;
    if (timer < 0) timer = 0;
    if (timer > 255) timer = 255;
    o[i] = (uint8_t)timer;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_passivewall_timer_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  int total = 0;
  if (!PyArg_ParseTuple(args, "OOi", &action_obj, &frame_obj, &total)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  if (action == NULL || frame == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n) {
    PyErr_SetString(PyExc_ValueError, "passivewall inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  if (total <= 0) return (PyObject*)out;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  uint16_t prev = 0xFFFFu;
  int run_len = 0;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t ai = a[i];
    if (!((ai == 202u || ai == 203u) && af[i] == 0)) {
      prev = ai;
      run_len = 0;
      continue;
    }
    if (prev == ai) {
      run_len += 1;
    } else {
      prev = ai;
      run_len = 1;
    }
    int timer = total - run_len + 1;
    if (timer < 0) timer = 0;
    if (timer > 255) timer = 255;
    o[i] = (uint8_t)timer;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_walljump_phase_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* raw_x_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOO", &action_obj, &frame_obj, &pos_x_obj, &pos_y_obj,
                        &raw_x_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 1, "pos_x_f32");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 1, "pos_y_f32");
  PyArrayObject* raw_x = require_contiguous_array(raw_x_obj, NPY_INT8, 1, "raw_main_x_i8");
  if (action == NULL || frame == NULL || pos_x == NULL || pos_y == NULL || raw_x == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(pos_x) != n || PyArray_SIZE(pos_y) != n ||
      PyArray_SIZE(raw_x) != n) {
    PyErr_SetString(PyExc_ValueError, "walljump phase inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* timer = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT8, 0);
  PyArrayObject* side = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_INT8, 0);
  if (timer == NULL || side == NULL) {
    Py_XDECREF(timer);
    Py_XDECREF(side);
    return NULL;
  }
  uint8_t* t = (uint8_t*)PyArray_DATA(timer);
  int8_t* s = (int8_t*)PyArray_DATA(side);
  for (npy_intp i = 0; i < n; i++) t[i] = 254u;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const float* x = (const float*)PyArray_DATA(pos_x);
  const float* y = (const float*)PyArray_DATA(pos_y);
  const int8_t* rx = (const int8_t*)PyArray_DATA(raw_x);
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t ai = a[i];
    if (!(ai == 27u || ai == 28u || (ai >= 29u && ai <= 34u))) continue;
    const int frame_i = af[i];
    if (frame_i < 12) continue;
    const float px = x[i];
    const float py = y[i];
    if (!isfinite(px) || !isfinite(py) || py >= -5.0f) continue;
    const int cur_x = (i + 1 < n) ? (int)rx[i + 1] : (int)rx[i];
    const int prev_x = (int)rx[i];
    if (px <= -60.0f) {
      if (!((frame_i >= 20 && cur_x <= -64) || (frame_i >= 19 && prev_x > -64 && cur_x <= -64))) {
        continue;
      }
      int hidden = frame_i - 8;
      if (hidden < 0) hidden = 0;
      if (hidden > 120) hidden = 120;
      t[i] = (uint8_t)hidden;
      s[i] = 1;
    } else if (px >= 60.0f) {
      if (!((frame_i >= 20 && cur_x >= 64) || (frame_i >= 17 && prev_x < 64 && cur_x >= 64))) {
        continue;
      }
      int hidden = frame_i - 8;
      if (hidden < 0) hidden = 0;
      if (hidden > 120) hidden = 120;
      t[i] = (uint8_t)hidden;
      s[i] = -1;
    }
  }
  return Py_BuildValue("NN", timer, side);
}

PyObject* msl_derive_entry_end_fall_lock_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* ground_obj = NULL;
  int act_entry_end = 0;
  int act_fall = 0;
  if (!PyArg_ParseTuple(args, "OOii", &action_obj, &ground_obj, &act_entry_end, &act_fall)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  if (action == NULL || ground == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(ground) != n) {
    PyErr_SetString(PyExc_ValueError, "entry_end_fall_lock inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* g = (const uint8_t*)PyArray_DATA(ground);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  const uint16_t entry = (uint16_t)((uint32_t)act_entry_end & 0xFFFFu);
  const uint16_t fall = (uint16_t)((uint32_t)act_fall & 0xFFFFu);
  uint16_t prev = 0xFFFFu;
  uint8_t lock = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur = a[i];
    if (cur == fall && g[i] == 0u) {
      lock = (prev == entry || lock != 0u) ? 1u : 0u;
    } else {
      lock = 0u;
    }
    o[i] = lock;
    prev = cur;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_jab_rapid_count_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* released_obj = NULL;
  PyObject* pressed_obj = NULL;
  int button_mask_a = 0;
  if (!PyArg_ParseTuple(args, "OOOi", &action_obj, &released_obj, &pressed_obj, &button_mask_a)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* released =
      require_contiguous_array(released_obj, NPY_UINT16, 1, "buttons_released_u16");
  PyArrayObject* pressed =
      require_contiguous_array(pressed_obj, NPY_UINT16, 1, "buttons_pressed_u16");
  if (action == NULL || released == NULL || pressed == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(released) != n || PyArray_SIZE(pressed) != n) {
    PyErr_SetString(PyExc_ValueError, "jab rapid count inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* rel = (const uint16_t*)PyArray_DATA(released);
  const uint16_t* prs = (const uint16_t*)PyArray_DATA(pressed);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  uint8_t count = 0u;
  uint16_t prev = 0xFFFFu;
  const uint16_t mask = (uint16_t)((uint32_t)button_mask_a & 0xFFFFu);
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur = a[i];
    const bool entered_attack11 = cur == 0x002Cu && prev != 0x002Cu;
    const bool jab = cur == 0x002Cu || cur == 0x002Du || cur == 0x002Eu;
    if (entered_attack11) {
      count = 0u;
    } else if (!jab) {
      count = 0u;
    }
    if (jab && !entered_attack11) {
      if (((rel[i] | prs[i]) & mask) != 0u && count < 255u) count++;
      o[i] = count;
    }
    prev = cur;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_walk_anim_source_vel_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* frame_speed_obj = NULL;
  PyObject* slow_obj = NULL;
  PyObject* middle_obj = NULL;
  PyObject* fast_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOO", &action_obj, &char_obj, &facing_obj, &frame_speed_obj,
                        &slow_obj, &middle_obj, &fast_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_INT8, 1, "facing_dir1_i8");
  PyArrayObject* frame_speed =
      require_contiguous_array(frame_speed_obj, NPY_FLOAT32, 1, "frame_speed_mul_f32");
  PyArrayObject* slow = require_contiguous_array(slow_obj, NPY_FLOAT32, 1, "walk_slow_lut");
  PyArrayObject* middle = require_contiguous_array(middle_obj, NPY_FLOAT32, 1, "walk_middle_lut");
  PyArrayObject* fast = require_contiguous_array(fast_obj, NPY_FLOAT32, 1, "walk_fast_lut");
  if (action == NULL || chr == NULL || facing == NULL || frame_speed == NULL || slow == NULL ||
      middle == NULL || fast == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(facing) != n || PyArray_SIZE(frame_speed) != n) {
    PyErr_SetString(PyExc_ValueError, "walk anim source velocity inputs must have equal lengths");
    return NULL;
  }
  if (PyArray_SIZE(slow) < 256 || PyArray_SIZE(middle) < 256 || PyArray_SIZE(fast) < 256) {
    PyErr_SetString(PyExc_ValueError, "walk divisor LUTs must have at least 256 entries");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const int8_t* f = (const int8_t*)PyArray_DATA(facing);
  const float* rate_arr = (const float*)PyArray_DATA(frame_speed);
  const float* slow_lut = (const float*)PyArray_DATA(slow);
  const float* middle_lut = (const float*)PyArray_DATA(middle);
  const float* fast_lut = (const float*)PyArray_DATA(fast);
  float* o = (float*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n; i++) {
    float denom = 0.0f;
    if (a[i] == 0x000Fu) {
      denom = slow_lut[c[i]];
    } else if (a[i] == 0x0010u) {
      denom = middle_lut[c[i]];
    } else if (a[i] == 0x0011u) {
      denom = fast_lut[c[i]];
    } else {
      continue;
    }
    if (!isfinite(denom) || denom <= 0.0f) continue;
    npy_intp rate_i = i;
    if (i + 1 < n && a[i + 1] == a[i]) rate_i = i + 1;
    const float rate = rate_arr[rate_i];
    if (!isfinite(rate) || rate <= 0.0f) continue;
    const float dir = f[i] < 0 ? -1.0f : 1.0f;
    o[i] = dir * rate * denom;
  }
  return (PyObject*)out;
}

static int msl_py_walk_action_from_speed(uint8_t char_id, float gr_vel, const float* walk_max,
                                         float mid_mul, float fast_mul) {
  const float max = walk_max[char_id];
  if (!isfinite(max) || max <= 0.0f) return 0x000F;
  const float v = fabsf(gr_vel);
  if (v >= fast_mul * max) return 0x0011;
  if (v >= mid_mul * max) return 0x0010;
  return 0x000F;
}

static float msl_py_walk_rate_from_source(uint16_t action, uint8_t char_id, float facing_dir,
                                          float source_vel, const float* slow, const float* middle,
                                          const float* fast, bool* valid) {
  float denom = 0.0f;
  if (action == 0x000Fu) {
    denom = slow[char_id];
  } else if (action == 0x0010u) {
    denom = middle[char_id];
  } else if (action == 0x0011u) {
    denom = fast[char_id];
  } else {
    *valid = false;
    return 0.0f;
  }
  if (!isfinite(denom) || denom <= 0.0f) {
    *valid = false;
    return 0.0f;
  }
  *valid = true;
  if (source_vel * facing_dir <= 0.0f) return 0.0f;
  return fabsf(source_vel) / denom;
}

static int msl_py_walk_msid(uint16_t action) {
  if (action == 0x000Fu) return 7;
  if (action == 0x0010u) return 8;
  if (action == 0x0011u) return 9;
  return -1;
}

static int msl_py_predict_walk_retarget_af(uint8_t char_id, uint16_t cur_action,
                                           uint16_t dst_action, float anim_frame, float rate,
                                           const float* cycles, npy_intp cycle_w) {
  const int cur_msid = msl_py_walk_msid(cur_action);
  const int dst_msid = msl_py_walk_msid(dst_action);
  if (cur_msid < 0 || dst_msid < 0) return INT32_MIN;
  const float cur_cycle = cycles[((npy_intp)char_id * cycle_w) + cur_msid];
  const float dst_cycle = cycles[((npy_intp)char_id * cycle_w) + dst_msid];
  if (!(cur_cycle > 0.0f) || !(dst_cycle > 0.0f)) return INT32_MIN;
  const float post_tick = anim_frame + rate;
  const int quotient = (int)(post_tick / cur_cycle);
  const float adjusted = post_tick - cur_cycle * (float)quotient;
  const int final_frame = (int)(dst_cycle * (adjusted / cur_cycle));
  int out_af = final_frame + 1;
  if (out_af >= (int)dst_cycle) out_af = 0;
  return out_af;
}

PyObject* msl_derive_walk_retarget_tick_source_vel_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* ref_af_obj = NULL;
  PyObject* ground_vel_obj = NULL;
  PyObject* hidden_vel_obj = NULL;
  PyObject* slow_obj = NULL;
  PyObject* middle_obj = NULL;
  PyObject* fast_obj = NULL;
  PyObject* walk_max_obj = NULL;
  PyObject* cycles_obj = NULL;
  double mid_mul = 0.0;
  double fast_mul = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOdd", &action_obj, &char_obj, &facing_obj, &anim_obj,
                        &ref_af_obj, &ground_vel_obj, &hidden_vel_obj, &slow_obj, &middle_obj,
                        &fast_obj, &walk_max_obj, &cycles_obj, &mid_mul, &fast_mul)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_INT8, 1, "facing_dir1_i8");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  PyArrayObject* ref_af =
      require_contiguous_array(ref_af_obj, NPY_INT16, 1, "ref_action_frame_i16");
  PyArrayObject* ground_vel =
      require_contiguous_array(ground_vel_obj, NPY_FLOAT32, 1, "speed_ground_x_self_f32");
  PyArrayObject* hidden_vel =
      require_contiguous_array(hidden_vel_obj, NPY_FLOAT32, 1, "walk_anim_source_vel_f32");
  PyArrayObject* slow = require_contiguous_array(slow_obj, NPY_FLOAT32, 1, "walk_slow_lut");
  PyArrayObject* middle = require_contiguous_array(middle_obj, NPY_FLOAT32, 1, "walk_middle_lut");
  PyArrayObject* fast = require_contiguous_array(fast_obj, NPY_FLOAT32, 1, "walk_fast_lut");
  PyArrayObject* walk_max = require_contiguous_array(walk_max_obj, NPY_FLOAT32, 1, "walk_max_lut");
  PyArrayObject* cycles = require_contiguous_array(cycles_obj, NPY_FLOAT32, 2, "end_frame_lut");
  if (action == NULL || chr == NULL || facing == NULL || anim == NULL || ref_af == NULL ||
      ground_vel == NULL || hidden_vel == NULL || slow == NULL || middle == NULL || fast == NULL ||
      walk_max == NULL || cycles == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  const npy_intp hidden_n = PyArray_SIZE(hidden_vel);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(facing) != n || PyArray_SIZE(anim) != n ||
      PyArray_SIZE(ref_af) != n || PyArray_SIZE(ground_vel) != n ||
      !(hidden_n == n || hidden_n + 1 == n)) {
    PyErr_SetString(PyExc_ValueError, "walk retarget inputs must have equal lengths");
    return NULL;
  }
  if (PyArray_SIZE(slow) < 256 || PyArray_SIZE(middle) < 256 || PyArray_SIZE(fast) < 256 ||
      PyArray_SIZE(walk_max) < 256 || PyArray_DIM(cycles, 0) < 256) {
    PyErr_SetString(PyExc_ValueError, "walk retarget LUTs must cover 256 character ids");
    return NULL;
  }
  const npy_intp cycle_w = PyArray_DIM(cycles, 1);
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const int8_t* face = (const int8_t*)PyArray_DATA(facing);
  const float* anim_p = (const float*)PyArray_DATA(anim);
  const int16_t* ref_p = (const int16_t*)PyArray_DATA(ref_af);
  const float* gv = (const float*)PyArray_DATA(ground_vel);
  const float* hv = (const float*)PyArray_DATA(hidden_vel);
  const float* slow_lut = (const float*)PyArray_DATA(slow);
  const float* middle_lut = (const float*)PyArray_DATA(middle);
  const float* fast_lut = (const float*)PyArray_DATA(fast);
  const float* max_lut = (const float*)PyArray_DATA(walk_max);
  const float* cycle_lut = (const float*)PyArray_DATA(cycles);
  float* o = (float*)PyArray_DATA(out);
  for (npy_intp i = 0; i + 1 < n; i++) {
    const uint16_t action_i = a[i];
    if (!(action_i == 0x000Fu || action_i == 0x0010u || action_i == 0x0011u)) continue;
    const uint16_t target = (uint16_t)msl_py_walk_action_from_speed(
        c[i], gv[i], max_lut, (float)mid_mul, (float)fast_mul);
    if (target == action_i || !(target == 0x000Fu || target == 0x0010u || target == 0x0011u)) {
      continue;
    }
    const float dir = face[i] < 0 ? -1.0f : 1.0f;
    bool hidden_valid = false;
    bool ground_valid = false;
    const float hidden_rate = msl_py_walk_rate_from_source(action_i, c[i], dir, hv[i], slow_lut,
                                                           middle_lut, fast_lut, &hidden_valid);
    const float ground_rate = msl_py_walk_rate_from_source(action_i, c[i], dir, gv[i], slow_lut,
                                                           middle_lut, fast_lut, &ground_valid);
    const int hidden_af = hidden_valid
                              ? msl_py_predict_walk_retarget_af(c[i], action_i, target, anim_p[i],
                                                                hidden_rate, cycle_lut, cycle_w)
                              : INT32_MIN;
    const int ground_af = ground_valid
                              ? msl_py_predict_walk_retarget_af(c[i], action_i, target, anim_p[i],
                                                                ground_rate, cycle_lut, cycle_w)
                              : INT32_MIN;
    const int ref = (int)ref_p[i + 1];
    if (ground_af == ref && hidden_af != ref) {
      o[i] = gv[i];
    } else if (hidden_af == ref && ground_af != ref) {
      o[i] = hv[i];
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_run_anim_source_vel_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* frame_speed_obj = NULL;
  PyObject* scaling_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOO", &action_obj, &char_obj, &facing_obj, &frame_speed_obj,
                        &scaling_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_INT8, 1, "facing_dir1_i8");
  PyArrayObject* frame_speed =
      require_contiguous_array(frame_speed_obj, NPY_FLOAT32, 1, "frame_speed_mul_f32");
  PyArrayObject* scaling = require_contiguous_array(scaling_obj, NPY_FLOAT32, 1, "run_scaling_lut");
  if (action == NULL || chr == NULL || facing == NULL || frame_speed == NULL || scaling == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(facing) != n || PyArray_SIZE(frame_speed) != n) {
    PyErr_SetString(PyExc_ValueError, "run anim source velocity inputs must have equal lengths");
    return NULL;
  }
  if (PyArray_SIZE(scaling) < 256) {
    PyErr_SetString(PyExc_ValueError, "run scaling LUT must have at least 256 entries");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const int8_t* f = (const int8_t*)PyArray_DATA(facing);
  const float* rate_arr = (const float*)PyArray_DATA(frame_speed);
  const float* scale_lut = (const float*)PyArray_DATA(scaling);
  float* o = (float*)PyArray_DATA(out);
  for (npy_intp i = 0; i < n; i++) {
    if (!(a[i] == 0x0015u || a[i] == 0x0016u)) continue;
    const float scaling_v = scale_lut[c[i]];
    if (!isfinite(scaling_v) || scaling_v <= 0.0f) continue;
    npy_intp rate_i = i;
    if (i + 1 < n && a[i + 1] == a[i]) rate_i = i + 1;
    const float rate = rate_arr[rate_i];
    if (!isfinite(rate) || rate <= 0.0f) continue;
    const float dir = f[i] < 0 ? -1.0f : 1.0f;
    o[i] = dir * rate * scaling_v;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_facing_dir1_sign_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* facing_obj = NULL;
  PyObject* action_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &facing_obj, &action_obj)) {
    return NULL;
  }
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  if (facing == NULL || action == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(facing);
  if (PyArray_SIZE(action) != n) {
    PyErr_SetString(PyExc_ValueError, "facing_dir1 inputs must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_INT8, 0);
  if (out == NULL) return NULL;
  const uint8_t* face = (const uint8_t*)PyArray_DATA(facing);
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  int8_t* o = (int8_t*)PyArray_DATA(out);
  uint16_t prev = 0xFFFFu;
  int8_t cur_sign = 1;
  for (npy_intp i = 0; i < n; i++) {
    const int8_t face_sign = face[i] != 0u ? 1 : -1;
    if (i == 0 || a[i] != prev) cur_sign = face_sign;
    o[i] = cur_sign;
    prev = a[i];
  }
  return (PyObject*)out;
}

PyObject* msl_derive_camera_target_world_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* scale_y_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* pos_z_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOO", &char_obj, &anim_obj, &anim_frame_obj, &scale_y_obj,
                        &facing_obj, &pos_x_obj, &pos_y_obj, &pos_z_obj)) {
    return NULL;
  }

  PyArrayObject* char_id = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  if (char_id == NULL) {
    return NULL;
  }
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_UINT32, 1, "animation_index_u32");
  if (anim == NULL) {
    return NULL;
  }
  PyArrayObject* anim_frame =
      require_contiguous_array(anim_frame_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  if (anim_frame == NULL) {
    return NULL;
  }
  PyArrayObject* scale_y =
      require_contiguous_array(scale_y_obj, NPY_FLOAT32, 1, "fighter_scale_y_f32");
  if (scale_y == NULL) {
    return NULL;
  }
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing_u8");
  if (facing == NULL) {
    return NULL;
  }
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 1, "pos_x_f32");
  if (pos_x == NULL) {
    return NULL;
  }
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 1, "pos_y_f32");
  if (pos_y == NULL) {
    return NULL;
  }
  PyArrayObject* pos_z = require_contiguous_array(pos_z_obj, NPY_FLOAT32, 1, "pos_z_f32");
  if (pos_z == NULL) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(char_id, 0);
  if (PyArray_DIM(anim, 0) != n || PyArray_DIM(anim_frame, 0) != n ||
      PyArray_DIM(scale_y, 0) != n || PyArray_DIM(facing, 0) != n || PyArray_DIM(pos_x, 0) != n ||
      PyArray_DIM(pos_y, 0) != n || PyArray_DIM(pos_z, 0) != n) {
    PyErr_SetString(PyExc_ValueError, "camera target world derivation inputs must share length");
    return NULL;
  }

  if (char_params_init() != 0 || anim_pose_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "camera target native tables failed to initialize");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out_x = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_z = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_r = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  if (out_x == NULL || out_y == NULL || out_z == NULL || out_r == NULL) {
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    Py_XDECREF(out_z);
    Py_XDECREF(out_r);
    return NULL;
  }

  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_id);
  const uint32_t* anim_p = (const uint32_t*)PyArray_DATA(anim);
  const float* anim_frame_p = (const float*)PyArray_DATA(anim_frame);
  const float* scale_y_p = (const float*)PyArray_DATA(scale_y);
  const uint8_t* facing_p = (const uint8_t*)PyArray_DATA(facing);
  const float* pos_x_p = (const float*)PyArray_DATA(pos_x);
  const float* pos_y_p = (const float*)PyArray_DATA(pos_y);
  const float* pos_z_p = (const float*)PyArray_DATA(pos_z);
  float* out_x_p = (float*)PyArray_DATA(out_x);
  float* out_y_p = (float*)PyArray_DATA(out_y);
  float* out_z_p = (float*)PyArray_DATA(out_z);
  float* out_r_p = (float*)PyArray_DATA(out_r);

  for (npy_intp i = 0; i < n; i++) {
    const uint8_t cid = char_p[i];
    const MslCharParams* ch = msl_char_params(cid);
    if (ch == NULL) {
      continue;
    }
    const uint32_t anim_u32 = anim_p[i];
    if (anim_u32 > 0xFFFFu) {
      continue;
    }
    const float frame_f = anim_frame_p[i];
    if (!isfinite(frame_f)) {
      continue;
    }
    const int frame_i = (int)floorf(frame_f);
    if (frame_i < 0 || frame_i > 0xFFFF) {
      continue;
    }
    float m[12];
    if (anim_pose_get_matrix(cid, (uint16_t)anim_u32, (uint16_t)frame_i,
                             ch->camera_zoom_target_bone_part_id, m) != 0) {
      continue;
    }

    const float ox = ch->camera_zoom_target_offset_x;
    const float oy = ch->camera_zoom_target_offset_y;
    const float oz = ch->camera_zoom_target_offset_z;
    float lx = (float)(m[0] * ox + m[1] * oy + m[2] * oz + m[3]);
    float ly = (float)(m[4] * ox + m[5] * oy + m[6] * oz + m[7]);
    float lz = (float)(m[8] * ox + m[9] * oy + m[10] * oz + m[11]);

    float scale = scale_y_p[i];
    if (!isfinite(scale) || !(scale > 0.0f)) {
      scale = 1.0f;
    }
    float model_scaling = ch->model_scaling;
    if (!isfinite(model_scaling) || !(model_scaling > 0.0f)) {
      model_scaling = 1.0f;
    }
    const float pose_scale = (float)(scale * model_scaling);
    lx = (float)(lx * pose_scale);
    ly = (float)(ly * pose_scale);
    lz = (float)(lz * pose_scale);

    const float facing_dir = facing_p[i] ? 1.0f : -1.0f;
    out_x_p[i] = (float)(pos_x_p[i] + facing_dir * lz);
    out_y_p[i] = (float)(pos_y_p[i] + ly);
    out_z_p[i] = (float)(pos_z_p[i] - facing_dir * lx);
    out_r_p[i] = (float)(ch->camera_box_radius * scale);
  }

  return Py_BuildValue("NNNN", out_x, out_y, out_z, out_r);
}

static inline void msl_py_mtx34_mul_point(const float m[12], float x, float y, float z,
                                          float* out_x, float* out_y, float* out_z) {
  *out_x = (float)(m[0] * x + m[1] * y + m[2] * z + m[3]);
  *out_y = (float)(m[4] * x + m[5] * y + m[6] * z + m[7]);
  *out_z = (float)(m[8] * x + m[9] * y + m[10] * z + m[11]);
}

static inline uint8_t msl_py_apply_specialhi_xrotn(uint8_t char_id, uint16_t action_id,
                                                   uint16_t msid, uint16_t frame, uint16_t part_id,
                                                   float model_scale, float rotate_model,
                                                   uint8_t rotate_model_valid, float* io_x,
                                                   float* io_y, float* io_z) {
  if (rotate_model_valid == 0u || !msl_specialhi_rotate_model_action(action_id) ||
      !msl_anim_part_under_xrotn(char_id, part_id) || !isfinite(rotate_model)) {
    return 0u;
  }
  float m[12];
  if (anim_pose_get_matrix(char_id, msid, frame, 2u, m) != 0) {
    return 0u;
  }

  float ax0 = 0.0f, ay0 = 0.0f, az0 = 0.0f;
  float ax1 = 0.0f, ay1 = 0.0f, az1 = 0.0f;
  msl_py_mtx34_mul_point(m, 0.0f, 0.0f, 0.0f, &ax0, &ay0, &az0);
  msl_py_mtx34_mul_point(m, 1.0f, 0.0f, 0.0f, &ax1, &ay1, &az1);
  ax0 *= model_scale;
  ay0 *= model_scale;
  az0 *= model_scale;
  ax1 *= model_scale;
  ay1 *= model_scale;
  az1 *= model_scale;

  float axis_x = ax1 - ax0;
  float axis_y = ay1 - ay0;
  float axis_z = az1 - az0;
  const float axis_len = sqrtf(axis_x * axis_x + axis_y * axis_y + axis_z * axis_z);
  if (!(axis_len > 0.0f)) {
    return 0u;
  }
  axis_x /= axis_len;
  axis_y /= axis_len;
  axis_z /= axis_len;

  const float angle = msl_specialhi_xrotn_angle_from_rotate_model(rotate_model);
  const float px = *io_x - ax0;
  const float py = *io_y - ay0;
  const float pz = *io_z - az0;
  const float c = cosf(angle);
  const float s = sinf(angle);
  const float dot = axis_x * px + axis_y * py + axis_z * pz;
  const float cross_x = axis_y * pz - axis_z * py;
  const float cross_y = axis_z * px - axis_x * pz;
  const float cross_z = axis_x * py - axis_y * px;
  *io_x = ax0 + (px * c) + (cross_x * s) + (axis_x * dot * (1.0f - c));
  *io_y = ay0 + (py * c) + (cross_y * s) + (axis_y * dot * (1.0f - c));
  *io_z = az0 + (pz * c) + (cross_z * s) + (axis_z * dot * (1.0f - c));
  return 1u;
}

PyObject* msl_derive_hitbox_prev_centers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* pos_z_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* scale_y_obj = NULL;
  PyObject* rotate_model_obj = NULL;
  PyObject* rotate_valid_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "iOOOOOOOOOOOO", &num_players, &char_obj, &action_obj, &anim_obj,
                        &action_frame_obj, &anim_frame_obj, &pos_x_obj, &pos_y_obj, &pos_z_obj,
                        &facing_obj, &scale_y_obj, &rotate_model_obj, &rotate_valid_obj)) {
    return NULL;
  }
  if (num_players != 2 && num_players != 4) {
    PyErr_SetString(PyExc_ValueError, "num_players must be 2 or 4");
    return NULL;
  }

  PyArrayObject* char_id = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id");
  PyArrayObject* action_id = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_UINT32, 2, "animation_index");
  PyArrayObject* action_frame =
      require_contiguous_array(action_frame_obj, NPY_INT16, 2, "action_frame");
  PyArrayObject* anim_frame =
      require_contiguous_array(anim_frame_obj, NPY_FLOAT32, 2, "anim_frame_f32");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "pos_x");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 2, "pos_y");
  PyArrayObject* pos_z = NULL;
  if (pos_z_obj != Py_None) {
    pos_z = require_contiguous_array(pos_z_obj, NPY_FLOAT32, 2, "pos_z");
  }
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 2, "facing");
  PyArrayObject* scale_y = require_contiguous_array(scale_y_obj, NPY_FLOAT32, 2, "fighter_scale_y");
  PyArrayObject* rotate_model = NULL;
  if (rotate_model_obj != Py_None) {
    rotate_model =
        require_contiguous_array(rotate_model_obj, NPY_FLOAT32, 2, "specialhi_rotate_model_f32");
  }
  PyArrayObject* rotate_valid = NULL;
  if (rotate_valid_obj != Py_None) {
    rotate_valid =
        require_contiguous_array(rotate_valid_obj, NPY_UINT8, 2, "specialhi_rotate_model_valid_u8");
  }
  if (char_id == NULL || action_id == NULL || anim == NULL || action_frame == NULL ||
      anim_frame == NULL || pos_x == NULL || pos_y == NULL ||
      (pos_z_obj != Py_None && pos_z == NULL) || facing == NULL || scale_y == NULL ||
      (rotate_model_obj != Py_None && rotate_model == NULL) ||
      (rotate_valid_obj != Py_None && rotate_valid == NULL)) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(char_id, 0);
  const npy_intp width = PyArray_DIM(char_id, 1);
  if (width < num_players) {
    PyErr_SetString(PyExc_ValueError, "char_id width smaller than num_players");
    return NULL;
  }
  if (require_exact_2d_shape(action_id, n, width, "action_id") != 0 ||
      require_exact_2d_shape(anim, n, width, "animation_index") != 0 ||
      require_exact_2d_shape(action_frame, n, width, "action_frame") != 0 ||
      require_exact_2d_shape(anim_frame, n, width, "anim_frame_f32") != 0 ||
      require_exact_2d_shape(pos_x, n, width, "pos_x") != 0 ||
      require_exact_2d_shape(pos_y, n, width, "pos_y") != 0 ||
      (pos_z != NULL && require_exact_2d_shape(pos_z, n, width, "pos_z") != 0) ||
      require_exact_2d_shape(facing, n, width, "facing") != 0 ||
      require_exact_2d_shape(scale_y, n, width, "fighter_scale_y") != 0 ||
      (rotate_model != NULL &&
       require_exact_2d_shape(rotate_model, n, width, "specialhi_rotate_model_f32") != 0) ||
      (rotate_valid != NULL &&
       require_exact_2d_shape(rotate_valid, n, width, "specialhi_rotate_model_valid_u8") != 0)) {
    return NULL;
  }

  if (char_params_init() != 0 || anim_pose_init() != 0 || anim_table_init() != 0 ||
      hitboxes_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "hitbox prev center native tables failed to initialize");
    return NULL;
  }

  npy_intp dims_valid[3] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_MAX_HITBOXES};
  npy_intp dims_xyz[3] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_MAX_HITBOXES};
  PyArrayObject* out_valid = (PyArrayObject*)PyArray_ZEROS(3, dims_valid, NPY_UINT8, 0);
  PyArrayObject* out_x = (PyArrayObject*)PyArray_ZEROS(3, dims_xyz, NPY_FLOAT32, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_ZEROS(3, dims_xyz, NPY_FLOAT32, 0);
  PyArrayObject* out_z = (PyArrayObject*)PyArray_ZEROS(3, dims_xyz, NPY_FLOAT32, 0);
  if (out_valid == NULL || out_x == NULL || out_y == NULL || out_z == NULL) {
    Py_XDECREF(out_valid);
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    Py_XDECREF(out_z);
    return NULL;
  }

  const uint8_t* char_p = (const uint8_t*)PyArray_DATA(char_id);
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action_id);
  const uint32_t* anim_p = (const uint32_t*)PyArray_DATA(anim);
  const int16_t* action_frame_p = (const int16_t*)PyArray_DATA(action_frame);
  const float* anim_frame_p = (const float*)PyArray_DATA(anim_frame);
  const float* pos_x_p = (const float*)PyArray_DATA(pos_x);
  const float* pos_y_p = (const float*)PyArray_DATA(pos_y);
  const float* pos_z_p = pos_z != NULL ? (const float*)PyArray_DATA(pos_z) : NULL;
  const uint8_t* facing_p = (const uint8_t*)PyArray_DATA(facing);
  const float* scale_y_p = (const float*)PyArray_DATA(scale_y);
  const float* rotate_model_p =
      rotate_model != NULL ? (const float*)PyArray_DATA(rotate_model) : NULL;
  const uint8_t* rotate_valid_p =
      rotate_valid != NULL ? (const uint8_t*)PyArray_DATA(rotate_valid) : NULL;
  uint8_t* valid_p = (uint8_t*)PyArray_DATA(out_valid);
  float* out_x_p = (float*)PyArray_DATA(out_x);
  float* out_y_p = (float*)PyArray_DATA(out_y);
  float* out_z_p = (float*)PyArray_DATA(out_z);

  for (npy_intp fi = 0; fi < n; fi++) {
    for (int p = 0; p < num_players; p++) {
      const npy_intp pi = fi * width + p;
      if (action_frame_p[pi] < 0 || anim_p[pi] > 0xFFFFu) {
        continue;
      }
      const float af = anim_frame_p[pi];
      if (!isfinite(af) || af < 0.0f) {
        continue;
      }
      const uint16_t frame = (uint16_t)floorf(af);
      const uint8_t cid = char_p[pi];
      const MslCharParams* ch = msl_char_params(cid);
      if (ch == NULL) {
        continue;
      }
      const MslHitboxEvent* events = NULL;
      uint16_t event_count = 0;
      if (hitboxes_get_events(cid, (uint16_t)anim_p[pi], &events, &event_count) != 0 ||
          events == NULL || event_count == 0) {
        continue;
      }
      const MslHitboxEvent* active[MSL_MAX_HITBOXES] = {0};
      for (uint16_t ei = 0; ei < event_count; ei++) {
        const MslHitboxEvent* ev = &events[ei];
        if (ev->frame > frame) {
          continue;
        }
        if (ev->kind == 1u) {
          if (ev->hitbox_id == 0xFFu) {
            memset(active, 0, sizeof(active));
          } else if (ev->hitbox_id < MSL_MAX_HITBOXES) {
            active[ev->hitbox_id] = NULL;
          }
        } else if (ev->hitbox_id < MSL_MAX_HITBOXES) {
          active[ev->hitbox_id] = ev;
        }
      }

      const float scale_y_val = scale_y_p[pi];
      const float model_scaling =
          (isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
      const float model_scale = (float)(scale_y_val * model_scaling);
      const float facing_dir = facing_p[pi] ? 1.0f : -1.0f;
      const float px = pos_x_p[pi];
      const float py = pos_y_p[pi];
      const float pz = pos_z_p != NULL ? pos_z_p[pi] : 0.0f;
      const uint16_t action = action_p[pi];
      const float rotate_model_val = rotate_model_p != NULL ? rotate_model_p[pi] : 0.0f;
      const uint8_t rotate_valid_val = rotate_valid_p != NULL ? rotate_valid_p[pi] : 0u;

      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        const MslHitboxEvent* ev = active[hb_id];
        if (ev == NULL) {
          continue;
        }
        float m[12];
        if (anim_pose_get_matrix(cid, (uint16_t)anim_p[pi], frame, ev->bone_part_id, m) != 0) {
          continue;
        }
        float lx = 0.0f, ly = 0.0f, lz = 0.0f;
        msl_py_mtx34_mul_point(m, ev->x, ev->y, ev->z, &lx, &ly, &lz);
        lx = (float)(lx * model_scale);
        ly = (float)(ly * model_scale);
        lz = (float)(lz * model_scale);
        (void)msl_py_apply_specialhi_xrotn(cid, action, (uint16_t)anim_p[pi], frame,
                                           ev->bone_part_id, model_scale, rotate_model_val,
                                           rotate_valid_val, &lx, &ly, &lz);
        const npy_intp oi =
            (fi * (npy_intp)MSL_MAX_PLAYERS + p) * (npy_intp)MSL_MAX_HITBOXES + hb_id;
        valid_p[oi] = 1u;
        out_x_p[oi] = (float)(facing_dir * lz + px);
        out_y_p[oi] = (float)(ly + py);
        out_z_p[oi] = (float)(-facing_dir * lx + pz);
      }
    }
  }

  return Py_BuildValue("NNNN", out_valid, out_x, out_y, out_z);
}

PyObject* msl_derive_combo_push_timer_seed_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* combo_count_obj = NULL;
  PyObject* last_attack_obj = NULL;
  PyObject* victim_obj = Py_None;
  if (!PyArg_ParseTuple(args, "OO|O", &combo_count_obj, &last_attack_obj, &victim_obj)) {
    return NULL;
  }
  PyArrayObject* counts = require_contiguous_array(combo_count_obj, NPY_UINT8, 2, "combo_count");
  PyArrayObject* attacks =
      require_contiguous_array(last_attack_obj, NPY_UINT8, 2, "last_attack_landed");
  PyArrayObject* victims = NULL;
  if (victim_obj != Py_None) {
    victims = require_contiguous_array(victim_obj, NPY_UINT8, 2, "combo_victim_port");
  }
  if (counts == NULL || attacks == NULL || (victim_obj != Py_None && victims == NULL)) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(counts, 0);
  const npy_intp count_w = PyArray_DIM(counts, 1);
  if (count_w > (npy_intp)MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "combo_count width exceeds MSL_MAX_PLAYERS");
    return NULL;
  }
  if (require_exact_2d_shape(attacks, n, count_w, "last_attack_landed") != 0 ||
      (victims != NULL && require_exact_2d_shape(victims, n, count_w, "combo_victim_port") != 0)) {
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

  npy_intp dims[2] = {n, (npy_intp)MSL_MAX_PLAYERS};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  if (out == NULL) {
    return NULL;
  }
  uint16_t* out_p = (uint16_t*)PyArray_DATA(out);
  if (common->combo_push_count_threshold == 0u || common->combo_push_timer_frames == 0u) {
    return (PyObject*)out;
  }

  const uint8_t* counts_p = (const uint8_t*)PyArray_DATA(counts);
  const uint8_t* attacks_p = (const uint8_t*)PyArray_DATA(attacks);
  const uint8_t* victims_p = victims != NULL ? (const uint8_t*)PyArray_DATA(victims) : NULL;
  uint16_t timer[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_count[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_attack[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_victim[MSL_MAX_PLAYERS];
  uint8_t repeated[MSL_MAX_PLAYERS] = {0};
  for (int i = 0; i < MSL_MAX_PLAYERS; i++) {
    prev_victim[i] = 0xFFu;
  }
  const int players = (count_w < (npy_intp)MSL_MAX_PLAYERS) ? (int)count_w : MSL_MAX_PLAYERS;
  for (npy_intp fi = 0; fi < n; fi++) {
    for (int p = 0; p < players; p++) {
      const npy_intp pi = fi * count_w + p;
      const uint8_t cur = counts_p[pi];
      const uint8_t attack = attacks_p[pi];
      const uint8_t victim = victims_p != NULL ? victims_p[pi] : 0xFFu;
      const uint8_t same_victim =
          victims_p == NULL || (victim != 0xFFu && victim == prev_victim[p]) ? 1u : 0u;
      const uint8_t same_attack = (attack != 0u && attack == prev_attack[p]) ? 1u : 0u;
      const uint8_t increment = (cur > prev_count[p]) ? 1u : 0u;
      if (cur == 0u || attack == 0u) {
        repeated[p] = 0u;
      } else if (increment) {
        if (same_attack && same_victim) {
          repeated[p] = repeated[p] == 0xFFu ? 0xFFu : (uint8_t)(repeated[p] + 1u);
        } else {
          repeated[p] = 1u;
        }
      } else if (!(same_attack && same_victim)) {
        repeated[p] = 1u;
      }

      if (increment && cur >= common->combo_push_count_threshold &&
          repeated[p] >= common->combo_push_count_threshold) {
        timer[p] = common->combo_push_timer_frames;
      } else if (timer[p] != 0u) {
        timer[p] = (uint16_t)(timer[p] - 1u);
      }
      prev_count[p] = cur;
      prev_attack[p] = attack;
      prev_victim[p] = victim;
    }
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      out_p[fi * (npy_intp)MSL_MAX_PLAYERS + p] = timer[p];
    }
  }
  return (PyObject*)out;
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

PyObject* msl_derive_illusion_ghost_pos012_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOO", &action_obj, &action_frame_obj, &pos_x_obj, &pos_y_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "post_action_id_u16");
  PyArrayObject* action_frame =
      require_contiguous_array(action_frame_obj, NPY_INT16, 2, "post_action_frame_i16");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "post_pos_x");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 2, "post_pos_y");
  if (action == NULL || action_frame == NULL || pos_x == NULL || pos_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp players = PyArray_DIM(action, 1);
  if (PyArray_DIM(action_frame, 0) != n || PyArray_DIM(action_frame, 1) != players ||
      PyArray_DIM(pos_x, 0) != n || PyArray_DIM(pos_x, 1) != players ||
      PyArray_DIM(pos_y, 0) != n || PyArray_DIM(pos_y, 1) != players) {
    PyErr_SetString(PyExc_ValueError, "illusion ghost inputs must share [frames, players]");
    return NULL;
  }
  npy_intp dims[2] = {n, players};
  PyArrayObject* out0_x = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out0_y = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out1_x = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out1_y = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out2_x = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out2_y = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  if (out0_x == NULL || out0_y == NULL || out1_x == NULL || out1_y == NULL || out2_x == NULL ||
      out2_y == NULL) {
    Py_XDECREF(out0_x);
    Py_XDECREF(out0_y);
    Py_XDECREF(out1_x);
    Py_XDECREF(out1_y);
    Py_XDECREF(out2_x);
    Py_XDECREF(out2_y);
    return NULL;
  }
  const uint16_t* action_p = (const uint16_t*)PyArray_DATA(action);
  const int16_t* frame_p = (const int16_t*)PyArray_DATA(action_frame);
  const float* px = (const float*)PyArray_DATA(pos_x);
  const float* py = (const float*)PyArray_DATA(pos_y);
  float* o0x = (float*)PyArray_DATA(out0_x);
  float* o0y = (float*)PyArray_DATA(out0_y);
  float* o1x = (float*)PyArray_DATA(out1_x);
  float* o1y = (float*)PyArray_DATA(out1_y);
  float* o2x = (float*)PyArray_DATA(out2_x);
  float* o2y = (float*)PyArray_DATA(out2_y);
  for (npy_intp p = 0; p < players; p++) {
    float ghost0_x = n > 0 ? px[p] : 0.0f;
    float ghost0_y = n > 0 ? py[p] : 0.0f;
    float ghost1_x = ghost0_x;
    float ghost1_y = ghost0_y;
    float ghost2_x = ghost0_x;
    float ghost2_y = ghost0_y;
    for (npy_intp fi = 0; fi < n; fi++) {
      const npy_intp idx = fi * players + p;
      const uint16_t cur_a = action_p[idx];
      const float cur_x = px[idx];
      const float cur_y = py[idx];
      uint8_t entry_main = 0u;
      if (cur_a == 348u || cur_a == 351u) {
        if (fi == 0) {
          entry_main = 1u;
        } else {
          const npy_intp prev = (fi - 1) * players + p;
          if (action_p[prev] != cur_a || frame_p[idx] < frame_p[prev]) {
            entry_main = 1u;
          }
        }
      }
      if (entry_main) {
        ghost0_x = cur_x;
        ghost0_y = cur_y;
        ghost1_x = cur_x;
        ghost1_y = cur_y;
        ghost2_x = cur_x;
        ghost2_y = cur_y;
      } else if (cur_a == 348u || cur_a == 349u || cur_a == 351u || cur_a == 352u) {
        ghost2_x = ghost1_x;
        ghost2_y = ghost1_y;
        ghost1_x = ghost0_x;
        ghost1_y = ghost0_y;
        ghost0_x = cur_x;
        ghost0_y = cur_y;
      }
      o0x[idx] = ghost0_x;
      o0y[idx] = ghost0_y;
      o1x[idx] = ghost1_x;
      o1y[idx] = ghost1_y;
      o2x[idx] = ghost2_x;
      o2y[idx] = ghost2_y;
    }
  }
  return Py_BuildValue("NNNNNN", out0_x, out0_y, out1_x, out1_y, out2_x, out2_y);
}

typedef struct MslPyHbPrim {
  uint8_t valid;
  uint16_t flags;
  int16_t def_frame;
  uint8_t group;
  uint8_t rehit;
  float x;
  float y;
  float z;
  float r;
  float damage;
} MslPyHbPrim;

typedef struct MslPyCapPrim {
  uint8_t valid;
  float ax;
  float ay;
  float az;
  float bx;
  float by;
  float bz;
  float r;
} MslPyCapPrim;

static inline uint8_t msl_py_is_shield_active_action(uint16_t action_id) {
  return (action_id == MSL_ACT_GUARD_ON || action_id == MSL_ACT_GUARD ||
          action_id == MSL_ACT_GUARD_REFLECT || action_id == MSL_ACT_GUARD_SET_OFF)
             ? 1u
             : 0u;
}

static inline uint8_t msl_py_is_attackair_action(uint16_t action_id) {
  return (action_id >= MSL_ACT_ATTACK_AIR_N && action_id <= MSL_ACT_ATTACK_AIR_LW) ? 1u : 0u;
}

static inline uint8_t msl_py_hitlist_victim_pointer_may_change(uint8_t stocks, uint16_t action_id) {
  if (stocks == 0u) {
    return 1u;
  }
  return (action_id == MSL_ACT_DEAD_DOWN || action_id == MSL_ACT_DEAD_LEFT ||
          action_id == MSL_ACT_DEAD_RIGHT || action_id == MSL_ACT_DEAD_UP_STAR ||
          action_id == MSL_ACT_REBIRTH || action_id == MSL_ACT_REBIRTH_WAIT)
             ? 1u
             : 0u;
}

static inline uint8_t msl_py_sphere_sphere_intersects(float ax, float ay, float az, float ar,
                                                      float bx, float by, float bz, float br) {
  const float dx = ax - bx;
  const float dy = ay - by;
  const float dz = az - bz;
  const float rr = ar + br;
  return (dx * dx + dy * dy + dz * dz) <= (rr * rr) ? 1u : 0u;
}

static inline float msl_py_point_segment_dist2(float px, float py, float pz, float ax, float ay,
                                               float az, float bx, float by, float bz) {
  const float abx = bx - ax;
  const float aby = by - ay;
  const float abz = bz - az;
  const float apx = px - ax;
  const float apy = py - ay;
  const float apz = pz - az;
  const float denom = abx * abx + aby * aby + abz * abz;
  float t = 0.0f;
  if (denom > 0.0f) {
    t = (apx * abx + apy * aby + apz * abz) / denom;
    if (t < 0.0f) {
      t = 0.0f;
    } else if (t > 1.0f) {
      t = 1.0f;
    }
  }
  const float qx = ax + t * abx;
  const float qy = ay + t * aby;
  const float qz = az + t * abz;
  const float dx = px - qx;
  const float dy = py - qy;
  const float dz = pz - qz;
  return dx * dx + dy * dy + dz * dz;
}

static inline uint8_t msl_py_sphere_capsule_intersects(float sx, float sy, float sz, float r_sphere,
                                                       float ax, float ay, float az, float bx,
                                                       float by, float bz, float r_capsule) {
  const float d2 = msl_py_point_segment_dist2(sx, sy, sz, ax, ay, az, bx, by, bz);
  const float r = r_sphere + r_capsule;
  return d2 <= (r * r) ? 1u : 0u;
}

static inline float msl_py_clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

static inline float msl_py_trigger_unit_from_input(uint16_t buttons, uint8_t l, uint8_t r) {
  if ((buttons & (uint16_t)(0x0040u | 0x0020u)) != 0u) {
    return 1.0f;
  }
  const uint8_t m = l > r ? l : r;
  return (float)m * (1.0f / 255.0f);
}

static inline int msl_py_get_env_dmg(float dmg) {
  if (dmg == 0.0f) {
    return 0;
  }
  const int i = (int)dmg;
  return i != 0 ? i : 1;
}

static inline uint16_t msl_py_calc_hitlag_frames(const MslCommonParams* common, int dmg_int) {
  float tmp_f = (float)dmg_int * common->hitlag_dmg_mul + common->hitlag_base;
  int tmp = (int)tmp_f;
  if (tmp < 0) {
    tmp = 0;
  }
  if (tmp > 0xFFFF) {
    tmp = 0xFFFF;
  }
  return (uint16_t)tmp;
}

PyObject* msl_derive_combat_hitlist_seed_fields_py(PyObject* self, PyObject* args) {
  (void)self;
  int num_players = 0;
  int is_teams = 0;
  int include_per_hitbox = 0;
  int include_replay_only_shield_admission = 0;
  int include_replay_only_body_admission = 0;
  PyObject* team_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* scale_y_obj = NULL;
  PyObject* guard_x8_obj = NULL;
  PyObject* guard_x4_obj = NULL;
  PyObject* stocks_obj = NULL;
  PyObject* shield_hp_obj = NULL;
  PyObject* hurtbox_state_obj = NULL;
  PyObject* hitlag_obj = Py_None;
  PyObject* last_hit_by_obj = Py_None;
  PyObject* instance_hit_by_obj = Py_None;
  PyObject* instance_id_obj = NULL;
  PyObject* input_buttons_obj = NULL;
  PyObject* input_l_obj = NULL;
  PyObject* input_r_obj = NULL;
  PyObject* turn_has_turned_obj = Py_None;
  PyObject* anim_frame_obj = Py_None;
  PyObject* frame_speed_obj = Py_None;
  PyObject* rotate_model_obj = Py_None;
  PyObject* rotate_valid_obj = Py_None;
  PyObject* percent_obj = Py_None;
  if (!PyArg_ParseTuple(
          args, "iiOOOOOOOOOOOOOOOOOOOOOOOOOOOOiii", &num_players, &is_teams, &team_obj, &char_obj,
          &action_obj, &action_frame_obj, &anim_obj, &facing_obj, &on_ground_obj, &pos_x_obj,
          &pos_y_obj, &scale_y_obj, &guard_x8_obj, &guard_x4_obj, &stocks_obj, &shield_hp_obj,
          &hurtbox_state_obj, &hitlag_obj, &last_hit_by_obj, &instance_hit_by_obj, &instance_id_obj,
          &input_buttons_obj, &input_l_obj, &input_r_obj, &turn_has_turned_obj, &anim_frame_obj,
          &frame_speed_obj, &rotate_model_obj, &rotate_valid_obj, &percent_obj, &include_per_hitbox,
          &include_replay_only_shield_admission, &include_replay_only_body_admission)) {
    return NULL;
  }
  if (num_players != 2 && num_players != 4) {
    PyErr_SetString(PyExc_ValueError, "num_players must be 2 or 4");
    return NULL;
  }

#define REQ_ARR(name, obj, typenum, label)                                      \
  PyArrayObject* name = require_contiguous_array((obj), (typenum), 2, (label)); \
  if ((name) == NULL) {                                                         \
    return NULL;                                                                \
  }
  REQ_ARR(team, team_obj, NPY_UINT8, "team_id");
  REQ_ARR(char_id, char_obj, NPY_UINT8, "char_id");
  REQ_ARR(action_id, action_obj, NPY_UINT16, "action_id");
  REQ_ARR(action_frame, action_frame_obj, NPY_INT16, "action_frame");
  REQ_ARR(anim, anim_obj, NPY_UINT32, "animation_index");
  REQ_ARR(facing, facing_obj, NPY_UINT8, "facing");
  REQ_ARR(on_ground, on_ground_obj, NPY_UINT8, "on_ground");
  REQ_ARR(pos_x, pos_x_obj, NPY_FLOAT32, "pos_x");
  REQ_ARR(pos_y, pos_y_obj, NPY_FLOAT32, "pos_y");
  REQ_ARR(scale_y, scale_y_obj, NPY_FLOAT32, "fighter_scale_y");
  REQ_ARR(guard_x8, guard_x8_obj, NPY_UINT16, "guard_tilt_x8");
  REQ_ARR(guard_x4, guard_x4_obj, NPY_FLOAT32, "guard_tilt_x4");
  REQ_ARR(stocks, stocks_obj, NPY_UINT8, "stocks");
  REQ_ARR(shield_hp, shield_hp_obj, NPY_FLOAT32, "shield_hp");
  REQ_ARR(hurtbox_state, hurtbox_state_obj, NPY_UINT8, "hurtbox_state");
  REQ_ARR(instance_id, instance_id_obj, NPY_UINT16, "instance_id");
  REQ_ARR(input_buttons, input_buttons_obj, NPY_UINT16, "input_buttons");
  REQ_ARR(input_l, input_l_obj, NPY_UINT8, "input_l");
  REQ_ARR(input_r, input_r_obj, NPY_UINT8, "input_r");
#undef REQ_ARR

#define OPT_ARR(name, obj, typenum, label)                         \
  PyArrayObject* name = NULL;                                      \
  if ((obj) != Py_None) {                                          \
    name = require_contiguous_array((obj), (typenum), 2, (label)); \
    if ((name) == NULL) {                                          \
      return NULL;                                                 \
    }                                                              \
  }
  OPT_ARR(hitlag, hitlag_obj, NPY_UINT16, "hitlag");
  OPT_ARR(last_hit_by, last_hit_by_obj, NPY_UINT8, "last_hit_by");
  OPT_ARR(instance_hit_by, instance_hit_by_obj, NPY_UINT16, "instance_hit_by");
  OPT_ARR(turn_has_turned, turn_has_turned_obj, NPY_UINT8, "turn_has_turned");
  OPT_ARR(anim_frame, anim_frame_obj, NPY_FLOAT32, "anim_frame_f32");
  OPT_ARR(frame_speed, frame_speed_obj, NPY_FLOAT32, "frame_speed_mul_f32");
  OPT_ARR(rotate_model, rotate_model_obj, NPY_FLOAT32, "specialhi_rotate_model_f32");
  OPT_ARR(rotate_valid, rotate_valid_obj, NPY_UINT8, "specialhi_rotate_model_valid_u8");
  OPT_ARR(percent, percent_obj, NPY_FLOAT32, "percent");
#undef OPT_ARR

  const npy_intp n = PyArray_DIM(action_id, 0);
  const npy_intp width = PyArray_DIM(action_id, 1);
  if (width < num_players) {
    PyErr_SetString(PyExc_ValueError, "action_id width smaller than num_players");
    return NULL;
  }
#define CHECK_DIMS(arr, label)                                 \
  if (require_exact_2d_shape((arr), n, width, (label)) != 0) { \
    return NULL;                                               \
  }
  CHECK_DIMS(team, "team_id");
  CHECK_DIMS(char_id, "char_id");
  CHECK_DIMS(action_frame, "action_frame");
  CHECK_DIMS(anim, "animation_index");
  CHECK_DIMS(facing, "facing");
  CHECK_DIMS(on_ground, "on_ground");
  CHECK_DIMS(pos_x, "pos_x");
  CHECK_DIMS(pos_y, "pos_y");
  CHECK_DIMS(scale_y, "fighter_scale_y");
  CHECK_DIMS(guard_x8, "guard_tilt_x8");
  CHECK_DIMS(guard_x4, "guard_tilt_x4");
  CHECK_DIMS(stocks, "stocks");
  CHECK_DIMS(shield_hp, "shield_hp");
  CHECK_DIMS(hurtbox_state, "hurtbox_state");
  CHECK_DIMS(instance_id, "instance_id");
  CHECK_DIMS(input_buttons, "input_buttons");
  CHECK_DIMS(input_l, "input_l");
  CHECK_DIMS(input_r, "input_r");
  if (hitlag != NULL) CHECK_DIMS(hitlag, "hitlag");
  if (last_hit_by != NULL) CHECK_DIMS(last_hit_by, "last_hit_by");
  if (instance_hit_by != NULL) CHECK_DIMS(instance_hit_by, "instance_hit_by");
  if (turn_has_turned != NULL) CHECK_DIMS(turn_has_turned, "turn_has_turned");
  if (anim_frame != NULL) CHECK_DIMS(anim_frame, "anim_frame_f32");
  if (frame_speed != NULL) CHECK_DIMS(frame_speed, "frame_speed_mul_f32");
  if (rotate_model != NULL) CHECK_DIMS(rotate_model, "specialhi_rotate_model_f32");
  if (rotate_valid != NULL) CHECK_DIMS(rotate_valid, "specialhi_rotate_model_valid_u8");
  if (percent != NULL) CHECK_DIMS(percent, "percent");
#undef CHECK_DIMS

  if (common_params_init() != 0 || char_params_init() != 0 || anim_pose_init() != 0 ||
      anim_table_init() != 0 || hitboxes_tables_init() != 0 || hurtcaps_tables_init() != 0 ||
      shield_tilt_table_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "combat hitlist native tables failed to initialize");
    return NULL;
  }
  const MslCommonParams* common = msl_common_params();
  if (common == NULL) {
    PyErr_SetString(PyExc_RuntimeError, "common params unavailable");
    return NULL;
  }

  npy_intp dims_dense[4] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_HITLIST_GROUPS,
                            (npy_intp)MSL_MAX_PLAYERS};
  npy_intp dims_hb_valid[3] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_MAX_HITBOXES};
  npy_intp dims_hb[4] = {n, (npy_intp)MSL_MAX_PLAYERS, (npy_intp)MSL_MAX_HITBOXES,
                         (npy_intp)MSL_MAX_PLAYERS};
  PyArrayObject* out_cd = (PyArrayObject*)PyArray_ZEROS(4, dims_dense, NPY_UINT16, 0);
  PyArrayObject* out_iid = (PyArrayObject*)PyArray_ZEROS(4, dims_dense, NPY_UINT16, 0);
  PyArrayObject* out_hb_valid = (PyArrayObject*)PyArray_ZEROS(3, dims_hb_valid, NPY_UINT8, 0);
  PyArrayObject* out_hb_cd = (PyArrayObject*)PyArray_ZEROS(4, dims_hb, NPY_UINT16, 0);
  PyArrayObject* out_hb_iid = (PyArrayObject*)PyArray_ZEROS(4, dims_hb, NPY_UINT16, 0);
  PyArrayObject* out_shield_kind = (PyArrayObject*)PyArray_ZEROS(4, dims_hb, NPY_UINT8, 0);
  if (out_cd == NULL || out_iid == NULL || out_hb_valid == NULL || out_hb_cd == NULL ||
      out_hb_iid == NULL || out_shield_kind == NULL) {
    Py_XDECREF(out_cd);
    Py_XDECREF(out_iid);
    Py_XDECREF(out_hb_valid);
    Py_XDECREF(out_hb_cd);
    Py_XDECREF(out_hb_iid);
    Py_XDECREF(out_shield_kind);
    return NULL;
  }

#define PTR(name, type, arr) const type* name = (const type*)PyArray_DATA(arr)
  PTR(team_p, uint8_t, team);
  PTR(char_p, uint8_t, char_id);
  PTR(action_p, uint16_t, action_id);
  PTR(action_frame_p, int16_t, action_frame);
  PTR(anim_p, uint32_t, anim);
  PTR(facing_p, uint8_t, facing);
  PTR(on_ground_p, uint8_t, on_ground);
  PTR(pos_x_p, float, pos_x);
  PTR(pos_y_p, float, pos_y);
  PTR(scale_y_p, float, scale_y);
  PTR(guard_x8_p, uint16_t, guard_x8);
  PTR(guard_x4_p, float, guard_x4);
  PTR(stocks_p, uint8_t, stocks);
  PTR(shield_hp_p, float, shield_hp);
  PTR(hurtbox_state_p, uint8_t, hurtbox_state);
  PTR(instance_id_p, uint16_t, instance_id);
  PTR(input_buttons_p, uint16_t, input_buttons);
  PTR(input_l_p, uint8_t, input_l);
  PTR(input_r_p, uint8_t, input_r);
#undef PTR
  const uint16_t* hitlag_p = hitlag != NULL ? (const uint16_t*)PyArray_DATA(hitlag) : NULL;
  const uint8_t* last_hit_by_p =
      last_hit_by != NULL ? (const uint8_t*)PyArray_DATA(last_hit_by) : NULL;
  const uint16_t* instance_hit_by_p =
      instance_hit_by != NULL ? (const uint16_t*)PyArray_DATA(instance_hit_by) : NULL;
  const uint8_t* turn_has_turned_p =
      turn_has_turned != NULL ? (const uint8_t*)PyArray_DATA(turn_has_turned) : NULL;
  const float* anim_frame_p = anim_frame != NULL ? (const float*)PyArray_DATA(anim_frame) : NULL;
  const float* frame_speed_p = frame_speed != NULL ? (const float*)PyArray_DATA(frame_speed) : NULL;
  const float* rotate_model_p =
      rotate_model != NULL ? (const float*)PyArray_DATA(rotate_model) : NULL;
  const uint8_t* rotate_valid_p =
      rotate_valid != NULL ? (const uint8_t*)PyArray_DATA(rotate_valid) : NULL;
  const float* percent_p = percent != NULL ? (const float*)PyArray_DATA(percent) : NULL;

  uint16_t* out_cd_p = (uint16_t*)PyArray_DATA(out_cd);
  uint16_t* out_iid_p = (uint16_t*)PyArray_DATA(out_iid);
  uint8_t* out_hb_valid_p = (uint8_t*)PyArray_DATA(out_hb_valid);
  uint16_t* out_hb_cd_p = (uint16_t*)PyArray_DATA(out_hb_cd);
  uint16_t* out_hb_iid_p = (uint16_t*)PyArray_DATA(out_hb_iid);
  uint8_t* out_shield_kind_p = (uint8_t*)PyArray_DATA(out_shield_kind);

  uint16_t hitlist_cd[MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS][MSL_MAX_PLAYERS] = {{{0}}};
  uint16_t hitlist_iid[MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS][MSL_MAX_PLAYERS] = {{{0}}};
  uint16_t hitlist_hb_cd[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES][MSL_MAX_PLAYERS] = {{{0}}};
  uint16_t hitlist_hb_iid[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES][MSL_MAX_PLAYERS] = {{{0}}};
  uint8_t hitlist_hb_authoritative[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0}};
  uint16_t sim_hitlag[MSL_MAX_PLAYERS] = {0};
  uint8_t prev_group_active[MSL_MAX_PLAYERS][MSL_HITLIST_GROUPS] = {{0}};
  uint8_t prev_hb_active[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0}};
  uint8_t prev_hb_group[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0}};

  const float denom = 1.0f - common->trigger_deadzone;

  for (npy_intp fi = 0; fi < n; fi++) {
    for (int p = 0; p < num_players; p++) {
      if (sim_hitlag[p] != 0u) {
        sim_hitlag[p] = (uint16_t)(sim_hitlag[p] - 1u);
      }
    }

    MslPyHbPrim hitboxes[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES];
    MslPyCapPrim caps[MSL_MAX_PLAYERS][MSL_MAX_HURTCAPS];
    uint16_t cap_counts[MSL_MAX_PLAYERS] = {0};
    float shield_x[MSL_MAX_PLAYERS] = {0.0f};
    float shield_y[MSL_MAX_PLAYERS] = {0.0f};
    float shield_z[MSL_MAX_PLAYERS] = {0.0f};
    float shield_r[MSL_MAX_PLAYERS] = {0.0f};
    uint8_t replay_only_hb_valid_frame[MSL_MAX_PLAYERS][MSL_MAX_HITBOXES] = {{0}};
    memset(hitboxes, 0, sizeof(hitboxes));
    memset(caps, 0, sizeof(caps));

    for (int p = 0; p < num_players; p++) {
      const npy_intp pi = fi * width + p;
      const uint8_t cid = char_p[pi];
      const MslCharParams* ch = msl_char_params(cid);
      if (ch == NULL || action_frame_p[pi] < 0 || anim_p[pi] > 0xFFFFu) {
        continue;
      }
      uint16_t frame = (uint16_t)action_frame_p[pi];
      if (anim_frame_p != NULL) {
        float af_f = anim_frame_p[pi];
        if (isfinite(af_f) && af_f >= 0.0f) {
          if (frame_speed_p != NULL && (hitlag_p == NULL || hitlag_p[pi] == 0u)) {
            af_f += frame_speed_p[pi];
          }
          if (af_f < 0.0f) {
            af_f = 0.0f;
          }
          if (af_f > 65535.0f) {
            af_f = 65535.0f;
          }
          frame = (uint16_t)floorf(af_f);
        }
      }

      const uint16_t msid = (uint16_t)anim_p[pi];
      const float model_scaling =
          (isfinite(ch->model_scaling) && ch->model_scaling > 0.0f) ? ch->model_scaling : 1.0f;
      const float model_scale = scale_y_p[pi] * model_scaling;
      const float scale_y_val = scale_y_p[pi];
      const float px = pos_x_p[pi];
      const float py = pos_y_p[pi];
      float facing_dir = facing_p[pi] ? 1.0f : -1.0f;
      if (action_p[pi] == MSL_ACT_TURN && turn_has_turned_p != NULL &&
          turn_has_turned_p[pi] != 0u) {
        facing_dir = -facing_dir;
      }
      const float rotate_model_val = rotate_model_p != NULL ? rotate_model_p[pi] : 0.0f;
      const uint8_t rotate_valid_val = rotate_valid_p != NULL ? rotate_valid_p[pi] : 0u;

      const MslHurtCap* hc = NULL;
      uint16_t hc_count = 0;
      if (hurtcaps_get(cid, &hc, &hc_count) == 0 && hc != NULL) {
        if (hc_count > MSL_MAX_HURTCAPS) {
          hc_count = MSL_MAX_HURTCAPS;
        }
        for (uint16_t ci = 0; ci < hc_count; ci++) {
          float m[12];
          if (anim_pose_get_matrix(cid, msid, frame, hc[ci].bone_part_id, m) != 0) {
            continue;
          }
          float ax = 0.0f, ay = 0.0f, az = 0.0f;
          float bx = 0.0f, by = 0.0f, bz = 0.0f;
          msl_py_mtx34_mul_point(m, hc[ci].a_offset[0], hc[ci].a_offset[1], hc[ci].a_offset[2], &ax,
                                 &ay, &az);
          msl_py_mtx34_mul_point(m, hc[ci].b_offset[0], hc[ci].b_offset[1], hc[ci].b_offset[2], &bx,
                                 &by, &bz);
          ax *= model_scale;
          ay *= model_scale;
          az *= model_scale;
          bx *= model_scale;
          by *= model_scale;
          bz *= model_scale;
          (void)msl_py_apply_specialhi_xrotn(cid, action_p[pi], msid, frame, hc[ci].bone_part_id,
                                             model_scale, rotate_model_val, rotate_valid_val, &ax,
                                             &ay, &az);
          (void)msl_py_apply_specialhi_xrotn(cid, action_p[pi], msid, frame, hc[ci].bone_part_id,
                                             model_scale, rotate_model_val, rotate_valid_val, &bx,
                                             &by, &bz);
          MslPyCapPrim* out = &caps[p][cap_counts[p]++];
          out->valid = 1u;
          out->ax = facing_dir * az + px;
          out->ay = ay + py;
          out->az = -facing_dir * ax;
          out->bx = facing_dir * bz + px;
          out->by = by + py;
          out->bz = -facing_dir * bx;
          out->r = hc[ci].scale * model_scale;
        }
      }

      const MslHitboxEvent* events = NULL;
      uint16_t event_count = 0;
      if (hitboxes_get_events(cid, msid, &events, &event_count) == 0 && events != NULL) {
        const MslHitboxEvent* active[MSL_MAX_HITBOXES] = {0};
        for (uint16_t ei = 0; ei < event_count; ei++) {
          const MslHitboxEvent* ev = &events[ei];
          if (ev->frame > frame) {
            continue;
          }
          if (ev->kind == 1u) {
            if (ev->hitbox_id == 0xFFu) {
              memset(active, 0, sizeof(active));
            } else if (ev->hitbox_id < MSL_MAX_HITBOXES) {
              active[ev->hitbox_id] = NULL;
            }
          } else if (ev->hitbox_id < MSL_MAX_HITBOXES) {
            active[ev->hitbox_id] = ev;
          }
        }
        for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
          const MslHitboxEvent* ev = active[hb_id];
          if (ev == NULL) {
            continue;
          }
          float m[12];
          if (anim_pose_get_matrix(cid, msid, frame, ev->bone_part_id, m) != 0) {
            continue;
          }
          float lx = 0.0f, ly = 0.0f, lz = 0.0f;
          msl_py_mtx34_mul_point(m, ev->x, ev->y, ev->z, &lx, &ly, &lz);
          lx *= model_scale;
          ly *= model_scale;
          lz *= model_scale;
          (void)msl_py_apply_specialhi_xrotn(cid, action_p[pi], msid, frame, ev->bone_part_id,
                                             model_scale, rotate_model_val, rotate_valid_val, &lx,
                                             &ly, &lz);
          MslPyHbPrim* hb = &hitboxes[p][hb_id];
          hb->valid = 1u;
          hb->x = facing_dir * lz + px;
          hb->y = ly + py;
          hb->z = -facing_dir * lx;
          hb->r = ev->radius;
          if ((ev->u16_6 & (uint16_t)MSL_HITBOX_FLAG_IGNORE_FIGHTER_SCALE) == 0u) {
            hb->r *= scale_y_val;
          }
          hb->damage = ev->damage;
          hb->flags = ev->u16_6;
          hb->def_frame = (int16_t)ev->frame;
          hb->group = (uint8_t)((ev->u16_7 >> 8) & 0x7u);
          hb->rehit = (uint8_t)(ev->u16_7 & 0xFFu);
        }
      }

      shield_x[p] = px;
      shield_y[p] = py;
      shield_z[p] = 0.0f;
      shield_r[p] = 0.0f;
      if (stocks_p[pi] != 0u && msl_py_is_shield_active_action(action_p[pi])) {
        const float hp = shield_hp_p[pi];
        if (hp > 0.0f && common->start_shield_health > 0.0f) {
          const float trig =
              msl_py_trigger_unit_from_input(input_buttons_p[pi], input_l_p[pi], input_r_p[pi]);
          const float light =
              denom > 0.0f ? msl_py_clamp01((trig - common->trigger_deadzone) / denom) : 0.0f;
          const float hp_ratio = msl_py_clamp01(hp / common->start_shield_health);
          const float light_scale = (light * (common->shield_size_lightshield_max -
                                              common->shield_size_lightshield_min)) +
                                    common->shield_size_lightshield_min;
          const float n1 = hp_ratio * light_scale;
          const float n2 = 1.0f - common->shield_size_min_scale;
          const float s = (n2 * n1) + common->shield_size_min_scale;
          shield_r[p] = s * ch->initial_shield_size * scale_y_val;
          MslShieldTiltTableView tv;
          if (msl_shield_tilt_table_view(cid, &tv) == 0 && tv.xyz != NULL && tv.frame_count != 0u) {
            uint16_t f = guard_x8_p[pi];
            if (f >= tv.frame_count) {
              f = (uint16_t)(tv.frame_count - 1u);
            }
            float mag = msl_py_clamp01(guard_x4_p[pi]);
            const uint8_t steady_guard_no_tilt =
                (action_p[pi] == MSL_ACT_GUARD && (mag == 0.0f || mag < 1.1754943508222875e-38f))
                    ? 1u
                    : 0u;
            const uint16_t neutral = steady_guard_no_tilt ? 0u : tv.neutral_frame;
            const float* nxyz = tv.xyz + (size_t)neutral * 3u;
            const float* fxyz = tv.xyz + (size_t)f * 3u;
            const float dx = nxyz[0] + mag * (fxyz[0] - nxyz[0]);
            const float dy = nxyz[1] + mag * (fxyz[1] - nxyz[1]);
            const float dz = nxyz[2] + mag * (fxyz[2] - nxyz[2]);
            float pose_scale = scale_y_val;
            if (steady_guard_no_tilt) {
              pose_scale *= model_scaling;
            }
            const float fd = facing_p[pi] ? 1.0f : -1.0f;
            shield_x[p] = px + dz * pose_scale * fd;
            shield_y[p] = py + dy * pose_scale;
            shield_z[p] = -dx * pose_scale * fd;
          }
        }
      }
    }

    int pending_count = 0;
    struct {
      int attacker, group, defender, rehit, iid, hl;
    } pending[16];

    for (int attacker = 0; attacker < num_players; attacker++) {
      const npy_intp ai = fi * width + attacker;
      if (stocks_p[ai] == 0u) {
        memset(hitlist_hb_cd[attacker], 0, sizeof(hitlist_hb_cd[attacker]));
        memset(hitlist_hb_iid[attacker], 0, sizeof(hitlist_hb_iid[attacker]));
        memset(prev_hb_active[attacker], 0, sizeof(prev_hb_active[attacker]));
        memset(prev_group_active[attacker], 0, sizeof(prev_group_active[attacker]));
        continue;
      }

      uint8_t group_active[MSL_HITLIST_GROUPS] = {0};
      uint8_t any_hitboxes = 0u;
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        if (hitboxes[attacker][hb_id].valid) {
          any_hitboxes = 1u;
          group_active[hitboxes[attacker][hb_id].group & 0x7u] = 1u;
        }
      }
      const uint8_t clear_dense_on_enable_edge = action_p[ai] == MSL_ACT_ATTACK_HI3 ? 1u : 0u;
      if (clear_dense_on_enable_edge) {
        for (int g = 0; g < MSL_HITLIST_GROUPS; g++) {
          if (group_active[g] && !prev_group_active[attacker][g]) {
            for (int victim = 0; victim < num_players; victim++) {
              const npy_intp vi = fi * width + victim;
              if (action_p[vi] != MSL_ACT_DAMAGE_FLY_LW) {
                continue;
              }
              if (hitlag_p != NULL && hitlag_p[vi] != 0u) {
                continue;
              }
              hitlist_cd[attacker][g][victim] = 0u;
              hitlist_iid[attacker][g][victim] = 0u;
            }
          }
        }
      }
      memcpy(prev_group_active[attacker], group_active, sizeof(group_active));
      for (int g = 0; g < MSL_HITLIST_GROUPS; g++) {
        if (!group_active[g]) {
          continue;
        }
        for (int victim = 0; victim < num_players; victim++) {
          uint16_t cd = hitlist_cd[attacker][g][victim];
          if (cd == 0u || cd == 0xFFFFu) {
            continue;
          }
          cd = (uint16_t)(cd - 1u);
          hitlist_cd[attacker][g][victim] = cd;
          if (cd == 0u) {
            hitlist_iid[attacker][g][victim] = 0u;
          }
        }
      }

      uint16_t prev_cd[MSL_MAX_HITBOXES][MSL_MAX_PLAYERS];
      uint16_t prev_iid[MSL_MAX_HITBOXES][MSL_MAX_PLAYERS];
      memcpy(prev_cd, hitlist_hb_cd[attacker], sizeof(prev_cd));
      memcpy(prev_iid, hitlist_hb_iid[attacker], sizeof(prev_iid));
      uint8_t cur_active[MSL_MAX_HITBOXES] = {0};
      uint8_t cur_group[MSL_MAX_HITBOXES] = {0};
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        if (hitboxes[attacker][hb_id].valid) {
          cur_active[hb_id] = 1u;
          cur_group[hb_id] = hitboxes[attacker][hb_id].group & 0x7u;
        }
      }
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        if (!cur_active[hb_id]) {
          memset(hitlist_hb_cd[attacker][hb_id], 0, sizeof(hitlist_hb_cd[attacker][hb_id]));
          memset(hitlist_hb_iid[attacker][hb_id], 0, sizeof(hitlist_hb_iid[attacker][hb_id]));
          hitlist_hb_authoritative[attacker][hb_id] = 0u;
          continue;
        }
        const uint8_t g = cur_group[hb_id];
        const uint8_t enable_edge =
            (!prev_hb_active[attacker][hb_id] || prev_hb_group[attacker][hb_id] != g) ? 1u : 0u;
        if (enable_edge) {
          uint8_t copied = 0u;
          for (int src = 0; src < MSL_MAX_HITBOXES; src++) {
            if (src == hb_id || !prev_hb_active[attacker][src] ||
                prev_hb_group[attacker][src] != g) {
              continue;
            }
            memcpy(hitlist_hb_cd[attacker][hb_id], prev_cd[src],
                   sizeof(hitlist_hb_cd[attacker][hb_id]));
            memcpy(hitlist_hb_iid[attacker][hb_id], prev_iid[src],
                   sizeof(hitlist_hb_iid[attacker][hb_id]));
            hitlist_hb_authoritative[attacker][hb_id] = hitlist_hb_authoritative[attacker][src];
            copied = 1u;
            break;
          }
          if (!copied) {
            memset(hitlist_hb_cd[attacker][hb_id], 0, sizeof(hitlist_hb_cd[attacker][hb_id]));
            memset(hitlist_hb_iid[attacker][hb_id], 0, sizeof(hitlist_hb_iid[attacker][hb_id]));
            hitlist_hb_authoritative[attacker][hb_id] = 0u;
          }
        }
        if (sim_hitlag[attacker] != 0u) {
          continue;
        }
        for (int victim = 0; victim < num_players; victim++) {
          uint16_t cd = hitlist_hb_cd[attacker][hb_id][victim];
          if (cd == 0u || cd == 0xFFFFu) {
            continue;
          }
          cd = (uint16_t)(cd - 1u);
          hitlist_hb_cd[attacker][hb_id][victim] = cd;
          if (cd == 0u) {
            hitlist_hb_iid[attacker][hb_id][victim] = 0u;
          }
        }
      }
      memcpy(prev_hb_active[attacker], cur_active, sizeof(cur_active));
      memcpy(prev_hb_group[attacker], cur_group, sizeof(cur_group));

      if (!any_hitboxes) {
        continue;
      }

      for (int defender = 0; defender < num_players; defender++) {
        if (defender == attacker) {
          continue;
        }
        const npy_intp di = fi * width + defender;
        if (stocks_p[di] == 0u || hurtbox_state_p[di] == 2u) {
          continue;
        }
        const uint8_t defender_no_damage = hurtbox_state_p[di] != 0u ? 1u : 0u;
        if (is_teams && team_p[ai] == team_p[di]) {
          continue;
        }
        if (sim_hitlag[attacker] != 0u || sim_hitlag[defender] != 0u) {
          continue;
        }
        const uint8_t defender_on_ground = on_ground_p[di] != 0u ? 1u : 0u;
        const uint8_t defender_hitlag_seen = hitlag_p != NULL ? (hitlag_p[di] > 0u ? 1u : 0u) : 1u;
        const uint8_t attacker_hitlag_seen = hitlag_p != NULL ? (hitlag_p[ai] > 0u ? 1u : 0u) : 1u;
        const uint8_t shield_active = shield_r[defender] > 0.0f ? 1u : 0u;
        uint8_t did_hit = 0u;

        for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
          MslPyHbPrim* hb = &hitboxes[attacker][hb_id];
          if (!hb->valid) {
            continue;
          }
          if (defender_on_ground) {
            if ((hb->flags & (uint16_t)MSL_HITBOX_FLAG_HIT_GROUNDED) == 0u) {
              continue;
            }
          } else if ((hb->flags & (uint16_t)MSL_HITBOX_FLAG_HIT_AERIAL) == 0u) {
            continue;
          }

          uint8_t shield_contact_seed_kind = 0u;
          if (include_replay_only_shield_admission && shield_active && hitlag_p != NULL &&
              fi + 1 < n && msl_py_is_attackair_action(action_p[ai]) && hb->damage > 0.0f &&
              hitlag_p[di] == 0u && hitlag_p[ai] == 0u) {
            const npy_intp ni_a = (fi + 1) * width + attacker;
            const npy_intp ni_d = (fi + 1) * width + defender;
            if (action_p[ni_d] == MSL_ACT_GUARD_SET_OFF && hitlag_p[ni_d] > 0u &&
                hitlag_p[ni_a] > 0u) {
              shield_contact_seed_kind = 2u;
            } else if (hitlag_p[ni_d] == 0u && hitlag_p[ni_a] == 0u) {
              shield_contact_seed_kind = 1u;
            }
          }
          if (shield_contact_seed_kind) {
            const npy_intp oi =
                (((fi * (npy_intp)MSL_MAX_PLAYERS + attacker) * MSL_MAX_HITBOXES + hb_id) *
                 MSL_MAX_PLAYERS) +
                defender;
            out_shield_kind_p[oi] = shield_contact_seed_kind;
          }

          const uint8_t hit_group = hb->group & 0x7u;
          uint8_t prune_guard_stale_seed_bridge = 0u;
          if (hitlag_p != NULL && last_hit_by_p != NULL && instance_hit_by_p != NULL &&
              action_p[di] == MSL_ACT_GUARD && hitlag_p[di] == 0u &&
              last_hit_by_p[di] == (uint8_t)attacker &&
              instance_hit_by_p[di] != instance_id_p[ai] && hb->def_frame == action_frame_p[ai] &&
              action_frame_p[ai] <= 8) {
            prune_guard_stale_seed_bridge = 1u;
          }

          const uint16_t cd = hitlist_cd[attacker][hit_group][defender];
          if (cd != 0u) {
            const uint8_t first_guardsetoff =
                (hitlag_p != NULL && action_p[di] == MSL_ACT_GUARD_SET_OFF && hitlag_p[di] > 0u &&
                 (fi == 0 ||
                  (hitlag_p[(fi - 1) * width + defender] == 0u &&
                   msl_py_is_shield_active_action(action_p[(fi - 1) * width + defender]))))
                    ? 1u
                    : 0u;
            const uint8_t guardsetoff_damage_onset =
                (hitlag_p != NULL && fi > 0 && action_p[di] == MSL_ACT_GUARD_SET_OFF &&
                 hitlag_p[di] > 0u && hitlag_p[(fi - 1) * width + defender] == 0u &&
                 shield_hp_p[di] < shield_hp_p[(fi - 1) * width + defender])
                    ? 1u
                    : 0u;
            if (first_guardsetoff || guardsetoff_damage_onset) {
              const uint16_t seeded = hb->rehit == 0u ? 0xFFFFu : (uint16_t)hb->rehit;
              for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
                if (hitboxes[attacker][reg].valid &&
                    (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                  hitlist_hb_cd[attacker][reg][defender] = seeded;
                  hitlist_hb_iid[attacker][reg][defender] = instance_id_p[di];
                  hitlist_hb_authoritative[attacker][reg] = 1u;
                }
              }
            }
            if (include_replay_only_shield_admission && hitlag_p != NULL && fi + 1 < n &&
                msl_py_is_attackair_action(action_p[ai]) && hb->damage > 0.0f &&
                hitlag_p[di] == 0u && hitlag_p[ai] == 0u &&
                action_p[(fi + 1) * width + defender] == MSL_ACT_GUARD_SET_OFF &&
                hitlag_p[(fi + 1) * width + defender] > 0u &&
                hitlag_p[(fi + 1) * width + attacker] > 0u) {
              for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
                if (hitboxes[attacker][reg].valid &&
                    (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                  hitlist_hb_cd[attacker][reg][defender] = 0u;
                  hitlist_hb_iid[attacker][reg][defender] = 0u;
                  replay_only_hb_valid_frame[attacker][reg] = 1u;
                }
              }
            } else if (include_replay_only_body_admission && hitlag_p != NULL &&
                       percent_p != NULL && fi + 1 < n && hb->damage > 0.0f && hitlag_p[di] == 0u &&
                       hitlag_p[ai] == 0u && hitlag_p[(fi + 1) * width + defender] > 0u &&
                       hitlag_p[(fi + 1) * width + attacker] > 0u &&
                       percent_p[(fi + 1) * width + defender] > percent_p[di] &&
                       (last_hit_by_p == NULL ||
                        last_hit_by_p[(fi + 1) * width + defender] == (uint8_t)attacker) &&
                       (instance_hit_by_p == NULL ||
                        instance_hit_by_p[(fi + 1) * width + defender] == instance_id_p[ai])) {
              for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
                if (hitboxes[attacker][reg].valid &&
                    (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                  hitlist_hb_cd[attacker][reg][defender] = 0u;
                  hitlist_hb_iid[attacker][reg][defender] = 0u;
                  replay_only_hb_valid_frame[attacker][reg] = 1u;
                }
              }
            } else {
              const uint16_t def_iid = instance_id_p[di];
              if (hitlist_iid[attacker][hit_group][defender] == def_iid) {
                if (prune_guard_stale_seed_bridge) {
                  hitlist_cd[attacker][hit_group][defender] = 0u;
                  hitlist_iid[attacker][hit_group][defender] = 0u;
                } else {
                  continue;
                }
              } else if (prune_guard_stale_seed_bridge) {
                hitlist_cd[attacker][hit_group][defender] = 0u;
                hitlist_iid[attacker][hit_group][defender] = 0u;
              } else if (msl_py_hitlist_victim_pointer_may_change(stocks_p[di], action_p[di])) {
                hitlist_cd[attacker][hit_group][defender] = 0u;
                hitlist_iid[attacker][hit_group][defender] = 0u;
              } else {
                hitlist_iid[attacker][hit_group][defender] = def_iid;
                continue;
              }
            }
          }

          const uint8_t shield_contact =
              shield_active && (shield_contact_seed_kind == 2u ||
                                (shield_contact_seed_kind != 1u &&
                                 msl_py_sphere_sphere_intersects(
                                     hb->x, hb->y, hb->z, hb->r, shield_x[defender],
                                     shield_y[defender], shield_z[defender], shield_r[defender])))
                  ? 1u
                  : 0u;
          if (shield_contact) {
            if (hb->damage > 0.0f && defender_hitlag_seen) {
              const uint16_t seeded = hb->rehit == 0u ? 0xFFFFu : (uint16_t)hb->rehit;
              for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
                if (hitboxes[attacker][reg].valid &&
                    (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                  hitlist_hb_cd[attacker][reg][defender] = seeded;
                  hitlist_hb_iid[attacker][reg][defender] = instance_id_p[di];
                  hitlist_hb_authoritative[attacker][reg] = 1u;
                }
              }
              hitlist_cd[attacker][hit_group][defender] = seeded;
              hitlist_iid[attacker][hit_group][defender] = instance_id_p[di];
              const uint16_t hl = msl_py_calc_hitlag_frames(common, msl_py_get_env_dmg(hb->damage));
              sim_hitlag[attacker] = hl;
              sim_hitlag[defender] = hl;
              did_hit = 1u;
              break;
            }
            continue;
          }

          if (cap_counts[defender] == 0u) {
            continue;
          }
          for (uint16_t ci = 0; ci < cap_counts[defender]; ci++) {
            MslPyCapPrim* cap = &caps[defender][ci];
            if (!cap->valid ||
                !msl_py_sphere_capsule_intersects(hb->x, hb->y, hb->z, hb->r, cap->ax, cap->ay,
                                                  cap->az, cap->bx, cap->by, cap->bz, cap->r)) {
              continue;
            }
            const uint8_t no_damage_contact_seen =
                (defender_no_damage && attacker_hitlag_seen) ? 1u : 0u;
            if (!defender_hitlag_seen && !no_damage_contact_seen) {
              if (!(include_replay_only_body_admission && hitlag_p != NULL && percent_p != NULL &&
                    fi + 1 < n && hitlag_p[di] == 0u && hitlag_p[ai] == 0u &&
                    hitlag_p[(fi + 1) * width + defender] > 0u &&
                    hitlag_p[(fi + 1) * width + attacker] > 0u &&
                    percent_p[(fi + 1) * width + defender] > percent_p[di] &&
                    (last_hit_by_p == NULL ||
                     last_hit_by_p[(fi + 1) * width + defender] == (uint8_t)attacker) &&
                    (instance_hit_by_p == NULL ||
                     instance_hit_by_p[(fi + 1) * width + defender] == instance_id_p[ai]))) {
                continue;
              }
              if (pending_count < (int)(sizeof(pending) / sizeof(pending[0]))) {
                pending[pending_count].attacker = attacker;
                pending[pending_count].group = hit_group;
                pending[pending_count].defender = defender;
                pending[pending_count].rehit = hb->rehit;
                pending[pending_count].iid = instance_id_p[(fi + 1) * width + defender];
                pending[pending_count].hl =
                    msl_py_calc_hitlag_frames(common, msl_py_get_env_dmg(hb->damage));
                pending_count++;
              }
              did_hit = 1u;
              break;
            }
            const uint16_t seeded = hb->rehit == 0u ? 0xFFFFu : (uint16_t)hb->rehit;
            for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
              if (hitboxes[attacker][reg].valid &&
                  (hitboxes[attacker][reg].group & 0x7u) == hit_group) {
                hitlist_hb_cd[attacker][reg][defender] = seeded;
                hitlist_hb_iid[attacker][reg][defender] = instance_id_p[di];
                hitlist_hb_authoritative[attacker][reg] = 1u;
              }
            }
            hitlist_cd[attacker][hit_group][defender] = seeded;
            hitlist_iid[attacker][hit_group][defender] = instance_id_p[di];
            const uint16_t hl = msl_py_calc_hitlag_frames(common, msl_py_get_env_dmg(hb->damage));
            if (defender_no_damage) {
              if (hitlag_p != NULL) {
                sim_hitlag[attacker] = hitlag_p[ai];
              }
            } else {
              sim_hitlag[attacker] = hl;
              sim_hitlag[defender] = hl;
            }
            did_hit = 1u;
            break;
          }
          if (did_hit) {
            break;
          }
        }
      }
    }

    for (int attacker = 0; attacker < num_players; attacker++) {
      for (int hb_id = 0; hb_id < MSL_MAX_HITBOXES; hb_id++) {
        if (!hitboxes[attacker][hb_id].valid) {
          continue;
        }
        const npy_intp oi = (fi * (npy_intp)MSL_MAX_PLAYERS + attacker) * MSL_MAX_HITBOXES + hb_id;
        out_hb_valid_p[oi] = replay_only_hb_valid_frame[attacker][hb_id]
                                 ? 1u
                                 : (hitlist_hb_authoritative[attacker][hb_id] ? 1u : 0u);
      }
    }

    memcpy(out_cd_p + fi * (npy_intp)MSL_MAX_PLAYERS * MSL_HITLIST_GROUPS * MSL_MAX_PLAYERS,
           hitlist_cd, sizeof(hitlist_cd));
    memcpy(out_iid_p + fi * (npy_intp)MSL_MAX_PLAYERS * MSL_HITLIST_GROUPS * MSL_MAX_PLAYERS,
           hitlist_iid, sizeof(hitlist_iid));
    memcpy(out_hb_cd_p + fi * (npy_intp)MSL_MAX_PLAYERS * MSL_MAX_HITBOXES * MSL_MAX_PLAYERS,
           hitlist_hb_cd, sizeof(hitlist_hb_cd));
    memcpy(out_hb_iid_p + fi * (npy_intp)MSL_MAX_PLAYERS * MSL_MAX_HITBOXES * MSL_MAX_PLAYERS,
           hitlist_hb_iid, sizeof(hitlist_hb_iid));

    for (int pi = 0; pi < pending_count; pi++) {
      const uint16_t seeded = pending[pi].rehit == 0 ? 0xFFFFu : (uint16_t)pending[pi].rehit;
      for (int reg = 0; reg < MSL_MAX_HITBOXES; reg++) {
        if (hitboxes[pending[pi].attacker][reg].valid &&
            (hitboxes[pending[pi].attacker][reg].group & 0x7u) == (uint8_t)pending[pi].group) {
          hitlist_hb_cd[pending[pi].attacker][reg][pending[pi].defender] = seeded;
          hitlist_hb_iid[pending[pi].attacker][reg][pending[pi].defender] =
              (uint16_t)pending[pi].iid;
          hitlist_hb_authoritative[pending[pi].attacker][reg] = 1u;
        }
      }
      hitlist_cd[pending[pi].attacker][pending[pi].group][pending[pi].defender] = seeded;
      hitlist_iid[pending[pi].attacker][pending[pi].group][pending[pi].defender] =
          (uint16_t)pending[pi].iid;
      sim_hitlag[pending[pi].attacker] = (uint16_t)pending[pi].hl;
      sim_hitlag[pending[pi].defender] = (uint16_t)pending[pi].hl;
    }
  }

  return Py_BuildValue("NNNNNN", out_cd, out_iid, out_hb_valid, out_hb_cd, out_hb_iid,
                       out_shield_kind);
}

static float msl_py_segment_x_at_y(float y, float x0, float y0, float x1, float y1,
                                   bool require_vertical_lower_endpoint) {
  if (require_vertical_lower_endpoint && fabsf(x1 - x0) < 1.0e-6f) {
    const float min_y = y0 < y1 ? y0 : y1;
    if (y > min_y + 1.0f) return NAN;
  }
  const float lo = (y0 < y1 ? y0 : y1) - 0.25f;
  const float hi = (y0 > y1 ? y0 : y1) + 0.25f;
  if (y < lo || y > hi) return NAN;
  if (fabsf(y1 - y0) < 1.0e-6f) return x0;
  float t = (y - y0) / (y1 - y0);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;
  return x0 + (x1 - x0) * t;
}

PyObject* msl_derive_specialhi_rotate_model_seed_lane_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* vel_x_obj = NULL;
  PyObject* vel_y_obj = NULL;
  PyObject* kind_obj = NULL;
  PyObject* x0_obj = NULL;
  PyObject* y0_obj = NULL;
  PyObject* x1_obj = NULL;
  PyObject* y1_obj = NULL;
  int stage_id = 0;
  int act_hi = 0;
  int act_air_hi = 0;
  int act_landing = 0;
  int act_fall = 0;
  int act_bound = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOiOOOOOiiiii", &action_obj, &facing_obj, &pos_x_obj, &pos_y_obj,
                        &vel_x_obj, &vel_y_obj, &stage_id, &kind_obj, &x0_obj, &y0_obj, &x1_obj,
                        &y1_obj, &act_hi, &act_air_hi, &act_landing, &act_fall, &act_bound)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 1, "facing_u8");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 1, "pos_x_f32");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 1, "pos_y_f32");
  PyArrayObject* vel_x =
      require_contiguous_array(vel_x_obj, NPY_FLOAT32, 1, "speed_air_x_self_f32");
  PyArrayObject* vel_y = require_contiguous_array(vel_y_obj, NPY_FLOAT32, 1, "speed_y_self_f32");
  PyArrayObject* kind = require_contiguous_array(kind_obj, NPY_UINT8, 1, "segment_kind");
  PyArrayObject* x0 = require_contiguous_array(x0_obj, NPY_FLOAT32, 1, "segment_x0");
  PyArrayObject* y0 = require_contiguous_array(y0_obj, NPY_FLOAT32, 1, "segment_y0");
  PyArrayObject* x1 = require_contiguous_array(x1_obj, NPY_FLOAT32, 1, "segment_x1");
  PyArrayObject* y1 = require_contiguous_array(y1_obj, NPY_FLOAT32, 1, "segment_y1");
  if (action == NULL || facing == NULL || pos_x == NULL || pos_y == NULL || vel_x == NULL ||
      vel_y == NULL || kind == NULL || x0 == NULL || y0 == NULL || x1 == NULL || y1 == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(facing) != n || PyArray_SIZE(pos_x) != n || PyArray_SIZE(pos_y) != n ||
      PyArray_SIZE(vel_x) != n || PyArray_SIZE(vel_y) != n) {
    PyErr_SetString(PyExc_ValueError, "SpecialHi rotateModel seed inputs must have equal lengths");
    return NULL;
  }
  const npy_intp seg_n = PyArray_SIZE(kind);
  if (PyArray_SIZE(x0) != seg_n || PyArray_SIZE(y0) != seg_n || PyArray_SIZE(x1) != seg_n ||
      PyArray_SIZE(y1) != seg_n) {
    PyErr_SetString(PyExc_ValueError,
                    "SpecialHi rotateModel segment arrays must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_FLOAT32, 0);
  PyArrayObject* valid = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL || valid == NULL) {
    Py_XDECREF(out);
    Py_XDECREF(valid);
    return NULL;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* f = (const uint8_t*)PyArray_DATA(facing);
  const float* px = (const float*)PyArray_DATA(pos_x);
  const float* py = (const float*)PyArray_DATA(pos_y);
  const float* vx = (const float*)PyArray_DATA(vel_x);
  const float* vy = (const float*)PyArray_DATA(vel_y);
  const uint8_t* sk = (const uint8_t*)PyArray_DATA(kind);
  const float* sx0 = (const float*)PyArray_DATA(x0);
  const float* sy0 = (const float*)PyArray_DATA(y0);
  const float* sx1 = (const float*)PyArray_DATA(x1);
  const float* sy1 = (const float*)PyArray_DATA(y1);
  float* o = (float*)PyArray_DATA(out);
  uint8_t* v = (uint8_t*)PyArray_DATA(valid);
  const uint16_t hi = (uint16_t)((uint32_t)act_hi & 0xFFFFu);
  const uint16_t air_hi = (uint16_t)((uint32_t)act_air_hi & 0xFFFFu);
  const uint16_t landing = (uint16_t)((uint32_t)act_landing & 0xFFFFu);
  const uint16_t fall = (uint16_t)((uint32_t)act_fall & 0xFFFFu);
  const uint16_t bound = (uint16_t)((uint32_t)act_bound & 0xFFFFu);
  float cur = 0.0f;
  bool cur_valid = false;
  uint16_t prev_action = 0xFFFFu;
  uint8_t prev_facing = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t ai = a[i];
    const bool special = ai == hi || ai == air_hi || ai == landing || ai == fall || ai == bound;
    if (!special) {
      cur_valid = false;
      prev_action = ai;
      prev_facing = f[i];
      continue;
    }
    bool collision_refresh = false;
    if (stage_id == 32 && (ai == hi || ai == air_hi)) {
      const float x = px[i];
      const float y = py[i];
      if (isfinite(x) && isfinite(y)) {
        for (npy_intp sidx = 0; sidx < seg_n; sidx++) {
          if (!(sk[sidx] == 2u || sk[sidx] == 3u)) continue;
          const float wall_x =
              msl_py_segment_x_at_y(y, sx0[sidx], sy0[sidx], sx1[sidx], sy1[sidx], false);
          if (!isfinite(wall_x)) continue;
          const float delta = sk[sidx] == 3u ? (wall_x - x) : (x - wall_x);
          if (delta >= 0.0f && delta <= 6.0f) {
            collision_refresh = true;
            break;
          }
        }
      }
    }
    const bool has_vel =
        isfinite(vx[i]) && isfinite(vy[i]) && (fabsf(vx[i]) > 0.0f || fabsf(vy[i]) > 0.0f);
    const bool prev_special = prev_action == hi || prev_action == air_hi ||
                              prev_action == landing || prev_action == fall || prev_action == bound;
    const bool recompute = !cur_valid || !prev_special || f[i] != prev_facing || collision_refresh;
    if (recompute && has_vel) {
      const float fx = f[i] != 0u ? 1.0f : -1.0f;
      cur = atan2f(vy[i], vx[i] * fx);
      cur_valid = true;
    }
    if (cur_valid) {
      o[i] = cur;
      v[i] = 1u;
    }
    prev_action = ai;
    prev_facing = f[i];
  }
  return Py_BuildValue("NN", out, valid);
}

PyObject* msl_derive_throw_pulse_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* anim_obj = NULL;
  PyObject* rate_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* last_attack_obj = NULL;
  PyObject* pulses_obj = NULL;
  PyObject* pulse_count_obj = NULL;
  PyObject* cmd1_obj = NULL;
  PyObject* shot_kind_obj = NULL;
  int num_players = 0;
  int act_throw_b = 0;
  int act_throw_hi = 0;
  int falco_char_id = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOiiii", &action_obj, &char_obj, &anim_obj, &rate_obj,
                        &hitstun_obj, &last_attack_obj, &pulses_obj, &pulse_count_obj, &cmd1_obj,
                        &shot_kind_obj, &num_players, &act_throw_b, &act_throw_hi,
                        &falco_char_id)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* anim = require_contiguous_array(anim_obj, NPY_FLOAT32, 2, "anim_frame_f32");
  PyArrayObject* rate = require_contiguous_array(rate_obj, NPY_FLOAT32, 2, "frame_speed_mul_f32");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 2, "hitstun_u16");
  PyArrayObject* last_attack =
      require_contiguous_array(last_attack_obj, NPY_UINT8, 2, "last_attack_landed_u8");
  PyArrayObject* pulses = require_contiguous_array(pulses_obj, NPY_INT16, 3, "pulse_lut");
  PyArrayObject* pulse_count =
      require_contiguous_array(pulse_count_obj, NPY_UINT8, 2, "pulse_count_lut");
  PyArrayObject* cmd1 = require_contiguous_array(cmd1_obj, NPY_INT16, 2, "cmd1_start_lut");
  PyArrayObject* shot_kind =
      require_contiguous_array(shot_kind_obj, NPY_UINT16, 1, "shot_itkind_lut");
  if (action == NULL || chr == NULL || anim == NULL || rate == NULL || hitstun == NULL ||
      last_attack == NULL || pulses == NULL || pulse_count == NULL || cmd1 == NULL ||
      shot_kind == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(chr, n, width, "char_id_u8") < 0 ||
      require_exact_2d_shape(anim, n, width, "anim_frame_f32") < 0 ||
      require_exact_2d_shape(rate, n, width, "frame_speed_mul_f32") < 0 ||
      require_exact_2d_shape(hitstun, n, width, "hitstun_u16") < 0 ||
      require_exact_2d_shape(last_attack, n, width, "last_attack_landed_u8") < 0) {
    return NULL;
  }
  if (PyArray_DIM(pulses, 0) < 256 || PyArray_DIM(pulse_count, 0) < 256 ||
      PyArray_DIM(cmd1, 0) < 256 || PyArray_DIM(shot_kind, 0) < 256 ||
      PyArray_DIM(pulses, 1) != PyArray_DIM(pulse_count, 1) ||
      PyArray_DIM(pulses, 1) != PyArray_DIM(cmd1, 1)) {
    PyErr_SetString(PyExc_ValueError, "throw pulse LUTs have incompatible shapes");
    return NULL;
  }
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  npy_intp dims[2] = {n, 4};
  PyArrayObject* out_consumed = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  PyArrayObject* out_crossed = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  PyArrayObject* out_pending = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  if (out_consumed == NULL || out_crossed == NULL || out_pending == NULL) {
    Py_XDECREF(out_consumed);
    Py_XDECREF(out_crossed);
    Py_XDECREF(out_pending);
    return NULL;
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const float* af = (const float*)PyArray_DATA(anim);
  const float* r = (const float*)PyArray_DATA(rate);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* lal = (const uint8_t*)PyArray_DATA(last_attack);
  const int16_t* pulse_lut = (const int16_t*)PyArray_DATA(pulses);
  const uint8_t* count_lut = (const uint8_t*)PyArray_DATA(pulse_count);
  const int16_t* cmd1_lut = (const int16_t*)PyArray_DATA(cmd1);
  const uint16_t* shot_lut = (const uint16_t*)PyArray_DATA(shot_kind);
  uint8_t* consumed = (uint8_t*)PyArray_DATA(out_consumed);
  uint8_t* crossed = (uint8_t*)PyArray_DATA(out_crossed);
  uint8_t* pending = (uint8_t*)PyArray_DATA(out_pending);
  const npy_intp action_cap = PyArray_DIM(pulses, 1);
  const npy_intp max_pulses = PyArray_DIM(pulses, 2);
  int cursor_char[4] = {-1, -1, -1, -1};
  int cursor_action[4] = {-1, -1, -1, -1};
  int cursor_next_idx[4] = {0, 0, 0, 0};
  float cursor_timer[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  bool cursor_active[4] = {false, false, false, false};
  for (npy_intp i = 0; i < n; i++) {
    for (int p = 0; p < players; p++) {
      const npy_intp idx = (i * width) + p;
      const uint16_t action_id = a[idx];
      const uint8_t char_id = c[idx];
      uint8_t count = 0u;
      const int16_t* pulse_base = NULL;
      if ((npy_intp)action_id < action_cap) {
        count = count_lut[((npy_intp)char_id * action_cap) + action_id];
        if (count > max_pulses) count = (uint8_t)max_pulses;
        pulse_base = &pulse_lut[(((npy_intp)char_id * action_cap) + action_id) * max_pulses];
      }
      if (count == 0u || pulse_base == NULL) {
        cursor_active[p] = false;
        cursor_char[p] = char_id;
        cursor_action[p] = action_id;
        continue;
      }
      const float cur_af = af[idx];
      const float cur_rate = r[idx];
      if (!isfinite(cur_af) || !isfinite(cur_rate) || cur_rate <= 0.0f) {
        cursor_active[p] = false;
        continue;
      }
      if (!cursor_active[p] || cursor_char[p] != (int)char_id ||
          cursor_action[p] != (int)action_id || (i > 0 && af[((i - 1) * width) + p] > cur_af)) {
        cursor_char[p] = char_id;
        cursor_action[p] = action_id;
        cursor_active[p] = true;
        int next_idx = 0;
        while (next_idx < count && (float)pulse_base[next_idx] <= cur_af) next_idx++;
        cursor_next_idx[p] = next_idx;
        cursor_timer[p] =
            next_idx < count ? fmaxf((float)pulse_base[next_idx] - cur_af, 0.0f) : INFINITY;
      }
      if (cursor_active[p] && cursor_next_idx[p] < count) {
        const float timer_after = cursor_timer[p] - cur_rate;
        if (timer_after <= 1.0e-4f) {
          const int pulse = pulse_base[cursor_next_idx[p]];
          if (pulse > 0 && pulse <= 255) pending[(i * 4) + p] = (uint8_t)pulse;
          cursor_next_idx[p]++;
          cursor_timer[p] = cursor_next_idx[p] < count
                                ? (float)(pulse_base[cursor_next_idx[p]] - pulse)
                                : INFINITY;
        } else {
          cursor_timer[p] = timer_after;
        }
      }
      const float prev_af = cur_af - cur_rate;
      int crossed_pulse = -1;
      for (int k = 0; k < count; k++) {
        const float pf = (float)pulse_base[k];
        if (prev_af < pf && pf <= cur_af) {
          crossed_pulse = (int)pulse_base[k];
          break;
        }
      }
      if (crossed_pulse < 0) continue;
      if (crossed_pulse > 0 && crossed_pulse <= 255) crossed[(i * 4) + p] = (uint8_t)crossed_pulse;
      const int prev_frame_i = (int)floorf(prev_af);
      bool stale_window = false;
      if ((int)action_id == act_throw_b && count >= 1u) {
        int cmd1_start = -1;
        if ((npy_intp)action_id < action_cap)
          cmd1_start = (int)cmd1_lut[((npy_intp)char_id * action_cap) + action_id];
        if (cmd1_start >= 0 && crossed_pulse == (int)pulse_base[0] && prev_frame_i == cmd1_start) {
          stale_window = true;
        }
      } else if ((int)action_id == act_throw_hi && (int)char_id == falco_char_id && count >= 2u) {
        if (crossed_pulse == (int)pulse_base[1] && prev_frame_i == (int)pulse_base[0]) {
          stale_window = true;
        }
      }
      if (!stale_window && (int)action_id == act_throw_b) {
        const int shot_itkind = (int)shot_lut[char_id];
        if (shot_itkind != 0) {
          for (int vp = 0; vp < players; vp++) {
            if (vp == p) continue;
            const npy_intp v_idx = (i * width) + vp;
            if (hs[v_idx] > 0u && (int)lal[v_idx] == shot_itkind) {
              stale_window = true;
              break;
            }
          }
        }
      }
      if (stale_window) consumed[(i * 4) + p] = 1u;
    }
  }
  return Py_BuildValue("NNN", out_consumed, out_crossed, out_pending);
}

static bool msl_py_throw_laser_grabbed_victim_action(uint16_t action_id) {
  switch (action_id) {
    case 223:
    case 224:
    case 225:
    case 226:
    case 227:
    case 228:
    case 231:
    case 232:
    case 239:
    case 240:
    case 241:
    case 242:
    case 243:
      return true;
    default:
      return false;
  }
}

PyObject* msl_derive_throw_laser_item_hitlist_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* grab_owner_obj = NULL;
  PyObject* instance_hit_by_obj = NULL;
  PyObject* instance_id_obj = NULL;
  PyObject* item_exists_obj = NULL;
  PyObject* item_state_obj = NULL;
  PyObject* item_type_obj = NULL;
  PyObject* item_owner_obj = NULL;
  PyObject* item_iid_obj = NULL;
  PyObject* mask_lut_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOi", &action_obj, &grab_owner_obj, &instance_hit_by_obj,
                        &instance_id_obj, &item_exists_obj, &item_state_obj, &item_type_obj,
                        &item_owner_obj, &item_iid_obj, &mask_lut_obj, &num_players)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* grab_owner =
      require_contiguous_array(grab_owner_obj, NPY_UINT8, 2, "grab_owner_port_u8");
  PyArrayObject* instance_hit_by =
      require_contiguous_array(instance_hit_by_obj, NPY_UINT16, 2, "instance_hit_by_u16");
  PyArrayObject* instance_id =
      require_contiguous_array(instance_id_obj, NPY_UINT16, 2, "instance_id_u16");
  PyArrayObject* item_exists =
      require_contiguous_array(item_exists_obj, NPY_UINT8, 2, "item_exists_u8");
  PyArrayObject* item_state =
      require_contiguous_array(item_state_obj, NPY_UINT8, 2, "item_state_u8");
  PyArrayObject* item_type =
      require_contiguous_array(item_type_obj, NPY_UINT16, 2, "item_type_u16");
  PyArrayObject* item_owner =
      require_contiguous_array(item_owner_obj, NPY_INT8, 2, "item_owner_i8");
  PyArrayObject* item_iid =
      require_contiguous_array(item_iid_obj, NPY_UINT16, 2, "item_instance_id_u16");
  PyArrayObject* mask_lut =
      require_contiguous_array(mask_lut_obj, NPY_UINT8, 1, "throw_laser_hitbox_mask_lut");
  if (action == NULL || grab_owner == NULL || instance_hit_by == NULL || instance_id == NULL ||
      item_exists == NULL || item_state == NULL || item_type == NULL || item_owner == NULL ||
      item_iid == NULL || mask_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(grab_owner, n, width, "grab_owner_port_u8") < 0 ||
      require_exact_2d_shape(instance_hit_by, n, width, "instance_hit_by_u16") < 0 ||
      require_exact_2d_shape(instance_id, n, width, "instance_id_u16") < 0) {
    return NULL;
  }
  if (PyArray_NDIM(item_exists) != 2 || PyArray_DIM(item_exists, 0) != n) {
    PyErr_SetString(PyExc_ValueError, "item_exists_u8 must have shape [frames, item_slots]");
    return NULL;
  }
  const npy_intp slots = PyArray_DIM(item_exists, 1);
  if (require_exact_2d_shape(item_state, n, slots, "item_state_u8") < 0 ||
      require_exact_2d_shape(item_type, n, slots, "item_type_u16") < 0 ||
      require_exact_2d_shape(item_owner, n, slots, "item_owner_i8") < 0 ||
      require_exact_2d_shape(item_iid, n, slots, "item_instance_id_u16") < 0) {
    return NULL;
  }
  if (PyArray_SIZE(mask_lut) < 65536) {
    PyErr_SetString(PyExc_ValueError, "throw_laser_hitbox_mask_lut must have 65536 entries");
    return NULL;
  }
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  npy_intp dims[2] = {n, 15};
  PyArrayObject* out_port = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_UINT8, 0);
  PyArrayObject* out_cd = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  PyArrayObject* out_mask = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  PyArrayObject* out_iid = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  if (out_port == NULL || out_cd == NULL || out_mask == NULL || out_iid == NULL) {
    Py_XDECREF(out_port);
    Py_XDECREF(out_cd);
    Py_XDECREF(out_mask);
    Py_XDECREF(out_iid);
    return NULL;
  }
  uint8_t* vp_out = (uint8_t*)PyArray_DATA(out_port);
  for (npy_intp k = 0; k < n * 15; k++) vp_out[k] = 0xFFu;
  uint8_t* cd_out = (uint8_t*)PyArray_DATA(out_cd);
  uint8_t* mask_out = (uint8_t*)PyArray_DATA(out_mask);
  uint16_t* iid_out = (uint16_t*)PyArray_DATA(out_iid);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* grab_data = (const uint8_t*)PyArray_DATA(grab_owner);
  const uint16_t* ihb_data = (const uint16_t*)PyArray_DATA(instance_hit_by);
  const uint16_t* iid_data = (const uint16_t*)PyArray_DATA(instance_id);
  const uint8_t* ie_data = (const uint8_t*)PyArray_DATA(item_exists);
  const uint8_t* is_data = (const uint8_t*)PyArray_DATA(item_state);
  const uint16_t* it_data = (const uint16_t*)PyArray_DATA(item_type);
  const int8_t* io_data = (const int8_t*)PyArray_DATA(item_owner);
  const uint16_t* ii_data = (const uint16_t*)PyArray_DATA(item_iid);
  const uint8_t* mask_lut_data = (const uint8_t*)PyArray_DATA(mask_lut);
  const int scan_slots = slots < 15 ? (int)slots : 15;
  for (npy_intp i = 0; i < n; i++) {
    for (int it = 0; it < scan_slots; it++) {
      const npy_intp item_idx = (i * slots) + it;
      if (ie_data[item_idx] == 0u || is_data[item_idx] != 1u) continue;
      const uint16_t item_kind = it_data[item_idx];
      const uint8_t hb_mask = mask_lut_data[item_kind];
      if (hb_mask == 0u) continue;
      const int owner = (int)io_data[item_idx];
      if (owner < 0 || owner >= players) continue;
      const npy_intp owner_idx = (i * width) + owner;
      const uint16_t owner_action = action_data[owner_idx];
      if (!(owner_action == 219u || owner_action == 220u || owner_action == 221u ||
            owner_action == 222u)) {
        continue;
      }
      const uint16_t laser_iid = ii_data[item_idx];
      if (laser_iid == 0u) continue;
      int candidate = -1;
      for (int victim = 0; victim < players; victim++) {
        if (victim == owner) continue;
        const npy_intp victim_idx = (i * width) + victim;
        if (grab_data[victim_idx] != (uint8_t)owner) continue;
        if (!msl_py_throw_laser_grabbed_victim_action(action_data[victim_idx])) continue;
        if (ihb_data[victim_idx] != laser_iid) continue;
        if (candidate >= 0) {
          candidate = -1;
          break;
        }
        candidate = victim;
      }
      if (candidate < 0) continue;
      const npy_intp out_idx = (i * 15) + it;
      vp_out[out_idx] = (uint8_t)candidate;
      cd_out[out_idx] = 16u;
      mask_out[out_idx] = hb_mask;
      iid_out[out_idx] = iid_data[(i * width) + candidate];
    }
  }
  return Py_BuildValue("NNNN", out_port, out_cd, out_mask, out_iid);
}

PyObject* msl_derive_item_attack_fields_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* item_exists_obj = NULL;
  PyObject* item_type_obj = NULL;
  PyObject* item_owner_obj = NULL;
  PyObject* item_spawn_id_obj = NULL;
  PyObject* fighter_attack_id_obj = NULL;
  PyObject* fighter_attack_instance_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOi", &item_exists_obj, &item_type_obj, &item_owner_obj,
                        &item_spawn_id_obj, &fighter_attack_id_obj, &fighter_attack_instance_obj,
                        &num_players)) {
    return NULL;
  }
  PyArrayObject* item_exists =
      require_contiguous_array(item_exists_obj, NPY_UINT8, 2, "item_exists_u8");
  PyArrayObject* item_type =
      require_contiguous_array(item_type_obj, NPY_UINT16, 2, "item_type_u16");
  PyArrayObject* item_owner =
      require_contiguous_array(item_owner_obj, NPY_INT8, 2, "item_owner_i8");
  PyArrayObject* item_spawn_id =
      require_contiguous_array(item_spawn_id_obj, NPY_UINT32, 2, "item_spawn_id_u32");
  PyArrayObject* fighter_attack_id =
      require_contiguous_array(fighter_attack_id_obj, NPY_UINT16, 2, "fighter_attack_id_u16");
  PyArrayObject* fighter_attack_instance = require_contiguous_array(
      fighter_attack_instance_obj, NPY_UINT16, 2, "fighter_attack_instance_u16");
  if (item_exists == NULL || item_type == NULL || item_owner == NULL || item_spawn_id == NULL ||
      fighter_attack_id == NULL || fighter_attack_instance == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(item_exists, 0);
  const npy_intp slots = PyArray_DIM(item_exists, 1);
  if (require_exact_2d_shape(item_type, n, slots, "item_type_u16") < 0 ||
      require_exact_2d_shape(item_owner, n, slots, "item_owner_i8") < 0 ||
      require_exact_2d_shape(item_spawn_id, n, slots, "item_spawn_id_u32") < 0) {
    return NULL;
  }
  if (PyArray_NDIM(fighter_attack_id) != 2 || PyArray_NDIM(fighter_attack_instance) != 2 ||
      PyArray_DIM(fighter_attack_id, 0) != n || PyArray_DIM(fighter_attack_instance, 0) != n ||
      PyArray_DIM(fighter_attack_id, 1) != PyArray_DIM(fighter_attack_instance, 1)) {
    PyErr_SetString(PyExc_ValueError, "fighter attack arrays must share shape [frames, players]");
    return NULL;
  }
  const npy_intp width = PyArray_DIM(fighter_attack_id, 1);
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  npy_intp dims[2] = {n, slots};
  PyArrayObject* out_attack_id = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  PyArrayObject* out_attack_instance = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  if (out_attack_id == NULL || out_attack_instance == NULL) {
    Py_XDECREF(out_attack_id);
    Py_XDECREF(out_attack_instance);
    return NULL;
  }
  const uint8_t* exists = (const uint8_t*)PyArray_DATA(item_exists);
  const uint16_t* type = (const uint16_t*)PyArray_DATA(item_type);
  const int8_t* owner_data = (const int8_t*)PyArray_DATA(item_owner);
  const uint32_t* spawn = (const uint32_t*)PyArray_DATA(item_spawn_id);
  const uint16_t* fighter_aid = (const uint16_t*)PyArray_DATA(fighter_attack_id);
  const uint16_t* fighter_ainst = (const uint16_t*)PyArray_DATA(fighter_attack_instance);
  uint16_t* out_aid = (uint16_t*)PyArray_DATA(out_attack_id);
  uint16_t* out_ainst = (uint16_t*)PyArray_DATA(out_attack_instance);
  uint32_t active_spawn[15] = {0};
  uint16_t active_type[15] = {0};
  uint16_t active_aid[15] = {0};
  uint16_t active_ainst[15] = {0};
  bool active_valid[15] = {false};
  for (npy_intp i = 0; i < n; i++) {
    bool seen[15] = {false};
    const int scan_slots = slots < 15 ? (int)slots : 15;
    for (int slot = 0; slot < scan_slots; slot++) {
      const npy_intp item_idx = (i * slots) + slot;
      if (exists[item_idx] == 0u) continue;
      const uint32_t key_spawn = spawn[item_idx];
      const uint16_t key_type = type[item_idx];
      int active_idx = -1;
      for (int k = 0; k < 15; k++) {
        if (active_valid[k] && active_spawn[k] == key_spawn && active_type[k] == key_type) {
          active_idx = k;
          break;
        }
      }
      if (active_idx < 0) {
        for (int k = 0; k < 15; k++) {
          if (!active_valid[k]) {
            active_idx = k;
            break;
          }
        }
        if (active_idx < 0) active_idx = 0;
        active_valid[active_idx] = true;
        active_spawn[active_idx] = key_spawn;
        active_type[active_idx] = key_type;
        const int owner = (int)owner_data[item_idx];
        if (owner >= 0 && owner < players) {
          active_aid[active_idx] = fighter_aid[(i * width) + owner];
          active_ainst[active_idx] = fighter_ainst[(i * width) + owner];
        } else {
          active_aid[active_idx] = 1u;
          active_ainst[active_idx] = 0u;
        }
      }
      seen[active_idx] = true;
      out_aid[item_idx] = active_aid[active_idx];
      out_ainst[item_idx] = active_ainst[active_idx];
    }
    for (int k = 0; k < 15; k++) {
      if (active_valid[k] && !seen[k]) active_valid[k] = false;
    }
  }
  return Py_BuildValue("NN", out_attack_id, out_attack_instance);
}

PyObject* msl_derive_item_reflect_damage_mul_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* item_exists_obj = NULL;
  PyObject* item_type_obj = NULL;
  PyObject* item_owner_obj = NULL;
  PyObject* item_instance_id_obj = NULL;
  PyObject* item_vel_x_obj = NULL;
  PyObject* item_vel_y_obj = NULL;
  PyObject* item_spawn_id_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* reflector_lut_obj = NULL;
  double powershield_mul = 1.0;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOdi", &item_exists_obj, &item_type_obj, &item_owner_obj,
                        &item_instance_id_obj, &item_vel_x_obj, &item_vel_y_obj, &item_spawn_id_obj,
                        &action_obj, &char_obj, &flags_obj, &reflector_lut_obj, &powershield_mul,
                        &num_players)) {
    return NULL;
  }
  PyArrayObject* item_exists =
      require_contiguous_array(item_exists_obj, NPY_UINT8, 2, "item_exists_u8");
  PyArrayObject* item_type =
      require_contiguous_array(item_type_obj, NPY_UINT16, 2, "item_type_u16");
  PyArrayObject* item_owner =
      require_contiguous_array(item_owner_obj, NPY_INT8, 2, "item_owner_i8");
  PyArrayObject* item_instance_id =
      require_contiguous_array(item_instance_id_obj, NPY_UINT16, 2, "item_instance_id_u16");
  PyArrayObject* item_vel_x =
      require_contiguous_array(item_vel_x_obj, NPY_FLOAT32, 2, "item_vel_x_f32");
  PyArrayObject* item_vel_y =
      require_contiguous_array(item_vel_y_obj, NPY_FLOAT32, 2, "item_vel_y_f32");
  PyArrayObject* item_spawn_id =
      require_contiguous_array(item_spawn_id_obj, NPY_UINT32, 2, "item_spawn_id_u32");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 3, "state_flags_u8");
  PyArrayObject* reflector_lut =
      require_contiguous_array(reflector_lut_obj, NPY_FLOAT32, 1, "reflector_damage_mul_lut");
  if (item_exists == NULL || item_type == NULL || item_owner == NULL || item_instance_id == NULL ||
      item_vel_x == NULL || item_vel_y == NULL || item_spawn_id == NULL || action == NULL ||
      chr == NULL || flags == NULL || reflector_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(item_exists, 0);
  const npy_intp slots = PyArray_DIM(item_exists, 1);
  if (require_exact_2d_shape(item_type, n, slots, "item_type_u16") < 0 ||
      require_exact_2d_shape(item_owner, n, slots, "item_owner_i8") < 0 ||
      require_exact_2d_shape(item_instance_id, n, slots, "item_instance_id_u16") < 0 ||
      require_exact_2d_shape(item_vel_x, n, slots, "item_vel_x_f32") < 0 ||
      require_exact_2d_shape(item_vel_y, n, slots, "item_vel_y_f32") < 0 ||
      require_exact_2d_shape(item_spawn_id, n, slots, "item_spawn_id_u32") < 0) {
    return NULL;
  }
  if (PyArray_NDIM(action) != 2 || PyArray_NDIM(chr) != 2 || PyArray_DIM(action, 0) != n ||
      PyArray_DIM(chr, 0) != n || PyArray_DIM(action, 1) != PyArray_DIM(chr, 1) ||
      PyArray_NDIM(flags) != 3 || PyArray_DIM(flags, 0) != n ||
      PyArray_DIM(flags, 1) != PyArray_DIM(action, 1) || PyArray_DIM(flags, 2) < 4 ||
      PyArray_SIZE(reflector_lut) < 256) {
    PyErr_SetString(PyExc_ValueError,
                    "item reflect inputs have incompatible fighter/state flag shapes");
    return NULL;
  }
  const npy_intp width = PyArray_DIM(action, 1);
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  npy_intp dims[2] = {n, slots};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  float* out_mul = (float*)PyArray_DATA(out);
  for (npy_intp k = 0; k < n * slots; k++) out_mul[k] = 1.0f;
  const uint8_t* exists = (const uint8_t*)PyArray_DATA(item_exists);
  const uint16_t* type = (const uint16_t*)PyArray_DATA(item_type);
  const int8_t* owner_data = (const int8_t*)PyArray_DATA(item_owner);
  const uint16_t* iid_data = (const uint16_t*)PyArray_DATA(item_instance_id);
  const float* vx_data = (const float*)PyArray_DATA(item_vel_x);
  const float* vy_data = (const float*)PyArray_DATA(item_vel_y);
  const uint32_t* spawn = (const uint32_t*)PyArray_DATA(item_spawn_id);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const uint8_t* flags_data = (const uint8_t*)PyArray_DATA(flags);
  const float* reflector_lut_data = (const float*)PyArray_DATA(reflector_lut);
  uint32_t active_spawn[15] = {0};
  uint16_t active_type[15] = {0};
  float active_mul[15] = {0};
  int active_owner[15] = {0};
  int active_iid[15] = {0};
  float active_vx[15] = {0};
  float active_vy[15] = {0};
  bool active_valid[15] = {false};
  for (npy_intp i = 0; i < n; i++) {
    bool seen[15] = {false};
    const int scan_slots = slots < 15 ? (int)slots : 15;
    for (int slot = 0; slot < scan_slots; slot++) {
      const npy_intp item_idx = (i * slots) + slot;
      if (exists[item_idx] == 0u) continue;
      const uint32_t key_spawn = spawn[item_idx];
      const uint16_t key_type = type[item_idx];
      const int owner = (int)owner_data[item_idx];
      const int iid = (int)iid_data[item_idx];
      const float vx = vx_data[item_idx];
      const float vy = vy_data[item_idx];
      int active_idx = -1;
      for (int k = 0; k < 15; k++) {
        if (active_valid[k] && active_spawn[k] == key_spawn && active_type[k] == key_type) {
          active_idx = k;
          break;
        }
      }
      if (active_idx < 0) {
        for (int k = 0; k < 15; k++) {
          if (!active_valid[k]) {
            active_idx = k;
            break;
          }
        }
        if (active_idx < 0) active_idx = 0;
        active_valid[active_idx] = true;
        active_spawn[active_idx] = key_spawn;
        active_type[active_idx] = key_type;
        active_mul[active_idx] = 1.0f;
        active_owner[active_idx] = owner;
        active_iid[active_idx] = iid;
        active_vx[active_idx] = vx;
        active_vy[active_idx] = vy;
      }
      float mul = active_mul[active_idx];
      const int prev_owner = active_owner[active_idx];
      const int prev_iid = active_iid[active_idx];
      const float prev_vx = active_vx[active_idx];
      const float prev_vy = active_vy[active_idx];
      const bool owner_changed = owner != prev_owner;
      const bool instance_transfer = iid != prev_iid;
      const float prev_speed_sq = prev_vx * prev_vx + prev_vy * prev_vy;
      const float cur_speed_sq = vx * vx + vy * vy;
      const bool reversed_vel_same_owner =
          owner == prev_owner && !instance_transfer && prev_speed_sq > 1.0e-6f &&
          cur_speed_sq > 1.0e-6f && (prev_vx * vx + prev_vy * vy) < 0.0f &&
          fabsf(cur_speed_sq - prev_speed_sq) <= 0.25f * fmaxf(prev_speed_sq, cur_speed_sq);
      if ((owner_changed || instance_transfer || reversed_vel_same_owner) && owner >= 0 &&
          owner < players) {
        const npy_intp fighter_idx = (i * width) + owner;
        const uint16_t act = action_data[fighter_idx];
        const uint8_t flags3 = flags_data[((i * width + owner) * PyArray_DIM(flags, 2)) + 3];
        if (act == 0x00B6u && (flags3 & 0x20u)) {
          mul = powershield_mul > 0.0 ? (float)powershield_mul : 1.0f;
        } else {
          const float char_mul = reflector_lut_data[char_data[fighter_idx]];
          if (char_mul > 0.0f) mul = char_mul;
        }
      }
      active_mul[active_idx] = mul;
      active_owner[active_idx] = owner;
      active_iid[active_idx] = iid;
      active_vx[active_idx] = vx;
      active_vy[active_idx] = vy;
      seen[active_idx] = true;
      out_mul[item_idx] = mul;
    }
    for (int k = 0; k < 15; k++) {
      if (active_valid[k] && !seen[k]) active_valid[k] = false;
    }
  }
  return (PyObject*)out;
}

static bool msl_py_guard_family_action(uint16_t action_id) {
  return action_id >= 178u && action_id <= 182u;
}

typedef struct MslPyShyguyKeyState {
  bool used;
  uint32_t spawn_id;
  uint32_t key_id;
  int prev_state;
  int state_age;
  int hitlag;
  int state3_moving_age;
  int state4_zero_x_prefix;
} MslPyShyguyKeyState;

typedef struct MslPyShyguyPhaseState {
  bool used;
  uint32_t spawn_id;
  uint32_t key_id;
  int state;
  uint8_t phase;
  float prev_vel_y;
  bool has_prev_vel_y;
} MslPyShyguyPhaseState;

typedef struct MslPyShyguySpawnState {
  bool used;
  uint32_t spawn_id;
  int first_seen;
  uint32_t group_base;
} MslPyShyguySpawnState;

typedef struct MslPyShyguyGroupState {
  bool used;
  uint32_t group_base;
  uint8_t speed_index;
  bool speed_valid;
} MslPyShyguyGroupState;

typedef struct MslPyShyguyPrevItem {
  uint8_t state;
  uint32_t spawn_id;
  uint16_t instance_id;
  float vel_y;
} MslPyShyguyPrevItem;

static float msl_py_shyguy_phase_value(const float* dyn, int phase) {
  phase &= 0xFF;
  if (phase == 0) {
    return 0.0f;
  }
  if (phase <= 128) {
    return dyn[phase - 1];
  }
  return dyn[256 - phase];
}

static float msl_py_shyguy_rate2_value(const float* dyn, int phase) {
  phase &= 0xFF;
  if (phase == 0) {
    return 0.0f;
  }
  if (phase == 1) {
    return msl_py_shyguy_phase_value(dyn, 1);
  }
  const int base = (2 * phase) - 2;
  return msl_py_shyguy_phase_value(dyn, base) + msl_py_shyguy_phase_value(dyn, base + 1);
}

static float msl_py_shyguy_pair_forward(const float* dyn, int pair) {
  pair &= 0x3F;
  const int base = pair * 2;
  return dyn[base] + dyn[(base + 1) & 0x7F];
}

static float msl_py_shyguy_current_vel_for_phase(const float* dyn, int phase, int state) {
  phase &= 0xFF;
  if (state == 4) {
    if (phase <= 29) {
      return 0.0f;
    }
    const int q = (phase - 18) & 0xFF;
    if (q == 12) {
      return msl_py_shyguy_phase_value(dyn, 1);
    }
    if (q == 140) {
      return -msl_py_shyguy_phase_value(dyn, 1);
    }
    if (q >= 141) {
      return msl_py_shyguy_pair_forward(dyn, q - 141);
    }
    return msl_py_shyguy_rate2_value(dyn, q - 11);
  }
  return msl_py_shyguy_phase_value(dyn, phase);
}

static int msl_py_shyguy_phase_distance(int a, int b) {
  a &= 0xFF;
  b &= 0xFF;
  const int ab = (a - b) & 0xFF;
  const int ba = (b - a) & 0xFF;
  return ab < ba ? ab : ba;
}

static int msl_py_shyguy_visible_phase(const float* dyn, float prev_vel_y, float cur_vel_y,
                                       int state, int predicted) {
  const float eps = 0.001f;
  if (state == 4 && fabsf(cur_vel_y) <= eps) {
    return -1;
  }
  int best = -1;
  int best_dist = 999;
  for (int phase = 0; phase < 256; phase++) {
    const int prev = (phase - 1) & 0xFF;
    if (fabsf(msl_py_shyguy_current_vel_for_phase(dyn, phase, state) - cur_vel_y) <= eps &&
        fabsf(msl_py_shyguy_current_vel_for_phase(dyn, prev, state) - prev_vel_y) <= eps) {
      const int dist = msl_py_shyguy_phase_distance(phase, predicted);
      if (best < 0 || dist < best_dist) {
        best = phase;
        best_dist = dist;
      }
    }
  }
  return best;
}

static MslPyShyguyKeyState* msl_py_shyguy_key_state(MslPyShyguyKeyState* states, size_t* count,
                                                    size_t cap, uint32_t spawn_id,
                                                    uint32_t key_id) {
  for (size_t i = 0; i < *count; i++) {
    if (states[i].used && states[i].spawn_id == spawn_id && states[i].key_id == key_id) {
      return &states[i];
    }
  }
  if (*count >= cap) {
    return NULL;
  }
  MslPyShyguyKeyState* st = &states[*count];
  memset(st, 0, sizeof(*st));
  st->used = true;
  st->spawn_id = spawn_id;
  st->key_id = key_id;
  st->prev_state = -1;
  (*count)++;
  return st;
}

static MslPyShyguyPhaseState* msl_py_shyguy_phase_state(MslPyShyguyPhaseState* states,
                                                        size_t* count, size_t cap,
                                                        uint32_t spawn_id, uint32_t key_id) {
  for (size_t i = 0; i < *count; i++) {
    if (states[i].used && states[i].spawn_id == spawn_id && states[i].key_id == key_id) {
      return &states[i];
    }
  }
  if (*count >= cap) {
    return NULL;
  }
  MslPyShyguyPhaseState* st = &states[*count];
  memset(st, 0, sizeof(*st));
  st->used = true;
  st->spawn_id = spawn_id;
  st->key_id = key_id;
  st->state = -1;
  (*count)++;
  return st;
}

static MslPyShyguySpawnState* msl_py_shyguy_spawn_state(MslPyShyguySpawnState* states,
                                                        size_t* count, size_t cap,
                                                        uint32_t spawn_id, int fi) {
  for (size_t i = 0; i < *count; i++) {
    if (states[i].used && states[i].spawn_id == spawn_id) {
      return &states[i];
    }
  }
  if (*count >= cap) {
    return NULL;
  }
  MslPyShyguySpawnState* st = &states[*count];
  memset(st, 0, sizeof(*st));
  st->used = true;
  st->spawn_id = spawn_id;
  st->first_seen = fi;
  st->group_base = spawn_id;
  (*count)++;
  return st;
}

static MslPyShyguyGroupState* msl_py_shyguy_group_state(MslPyShyguyGroupState* states,
                                                        size_t* count, size_t cap,
                                                        uint32_t group_base) {
  for (size_t i = 0; i < *count; i++) {
    if (states[i].used && states[i].group_base == group_base) {
      return &states[i];
    }
  }
  if (*count >= cap) {
    return NULL;
  }
  MslPyShyguyGroupState* st = &states[*count];
  memset(st, 0, sizeof(*st));
  st->used = true;
  st->group_base = group_base;
  (*count)++;
  return st;
}

static int msl_py_shyguy_pattern_from_item(float pos_x, float pos_y, const float* vpos) {
  int best = pos_x < 0.0f ? 0 : 3;
  const int end = best + 3;
  float best_err = fabsf(pos_y - vpos[best]);
  for (int i = best + 1; i < end; i++) {
    const float err = fabsf(pos_y - vpos[i]);
    if (err < best_err) {
      best = i;
      best_err = err;
    }
  }
  return best;
}

static int msl_py_shyguy_speed_index_from_item(float vel_x, int state, const float* speeds,
                                               float state4_mul) {
  float speed = fabsf(vel_x);
  if (state == 4 && state4_mul != 0.0f) {
    speed /= state4_mul;
  }
  int best = 0;
  float best_err = fabsf(speed - speeds[0]);
  for (int i = 1; i < 3; i++) {
    const float err = fabsf(speed - speeds[i]);
    if (err < best_err) {
      best = i;
      best_err = err;
    }
  }
  return best;
}

static int msl_py_shyguy_hitlag_from_damage(uint16_t damage, float damage_mul, float base) {
  const int frames = (int)(((float)damage * damage_mul) + base);
  return frames > 0 ? frames - 1 : 0;
}

PyObject* msl_derive_yoshi_shyguy_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* exists_obj = NULL;
  PyObject* type_obj = NULL;
  PyObject* owner_obj = NULL;
  PyObject* state_obj = NULL;
  PyObject* spawn_obj = NULL;
  PyObject* iid_obj = NULL;
  PyObject* vel_x_obj = NULL;
  PyObject* vel_y_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* damage_obj = NULL;
  PyObject* vpos_obj = NULL;
  PyObject* speeds_obj = NULL;
  PyObject* dyn_obj = NULL;
  int replay_stage_id = 0;
  int param_stage_id = 0;
  int item_kind = 0;
  int timer_reset = 0;
  int spawn_delay_step = 0;
  double state4_speed_mul = 0.0;
  double hitlag_damage_mul = 0.0;
  double hitlag_base = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOiiiiidOOOdd", &exists_obj, &type_obj, &owner_obj,
                        &state_obj, &spawn_obj, &iid_obj, &vel_x_obj, &vel_y_obj, &pos_x_obj,
                        &pos_y_obj, &damage_obj, &replay_stage_id, &param_stage_id, &item_kind,
                        &timer_reset, &spawn_delay_step, &state4_speed_mul, &vpos_obj, &speeds_obj,
                        &dyn_obj, &hitlag_damage_mul, &hitlag_base)) {
    return NULL;
  }
  PyArrayObject* exists = require_contiguous_array(exists_obj, NPY_UINT8, 2, "item_exists_u8");
  PyArrayObject* type = require_contiguous_array(type_obj, NPY_UINT16, 2, "item_type_u16");
  PyArrayObject* owner = require_contiguous_array(owner_obj, NPY_INT8, 2, "item_owner_i8");
  PyArrayObject* state = require_contiguous_array(state_obj, NPY_UINT8, 2, "item_state_u8");
  PyArrayObject* spawn = require_contiguous_array(spawn_obj, NPY_UINT32, 2, "item_spawn_id_u32");
  PyArrayObject* iid = require_contiguous_array(iid_obj, NPY_UINT16, 2, "item_instance_id_u16");
  PyArrayObject* vel_x = require_contiguous_array(vel_x_obj, NPY_FLOAT32, 2, "item_vel_x_f32");
  PyArrayObject* vel_y = require_contiguous_array(vel_y_obj, NPY_FLOAT32, 2, "item_vel_y_f32");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "item_pos_x_f32");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 2, "item_pos_y_f32");
  PyArrayObject* damage = require_contiguous_array(damage_obj, NPY_UINT16, 2, "item_damage_u16");
  PyArrayObject* vpos_arr = require_contiguous_array(vpos_obj, NPY_FLOAT32, 1, "shyguy_vpos_f32");
  PyArrayObject* speeds_arr =
      require_contiguous_array(speeds_obj, NPY_FLOAT32, 1, "shyguy_speeds_f32");
  PyArrayObject* dyn_arr = require_contiguous_array(dyn_obj, NPY_FLOAT32, 1, "shyguy_dyn_y_f32");
  if (exists == NULL || type == NULL || owner == NULL || state == NULL || spawn == NULL ||
      iid == NULL || vel_x == NULL || vel_y == NULL || pos_x == NULL || pos_y == NULL ||
      damage == NULL || vpos_arr == NULL || speeds_arr == NULL || dyn_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(exists, 0);
  const npy_intp slots = PyArray_DIM(exists, 1);
  if (require_exact_2d_shape(type, n, slots, "item_type_u16") != 0 ||
      require_exact_2d_shape(owner, n, slots, "item_owner_i8") != 0 ||
      require_exact_2d_shape(state, n, slots, "item_state_u8") != 0 ||
      require_exact_2d_shape(spawn, n, slots, "item_spawn_id_u32") != 0 ||
      require_exact_2d_shape(iid, n, slots, "item_instance_id_u16") != 0 ||
      require_exact_2d_shape(vel_x, n, slots, "item_vel_x_f32") != 0 ||
      require_exact_2d_shape(vel_y, n, slots, "item_vel_y_f32") != 0 ||
      require_exact_2d_shape(pos_x, n, slots, "item_pos_x_f32") != 0 ||
      require_exact_2d_shape(pos_y, n, slots, "item_pos_y_f32") != 0 ||
      require_exact_2d_shape(damage, n, slots, "item_damage_u16") != 0) {
    return NULL;
  }
  if (PyArray_SIZE(vpos_arr) < 6 || PyArray_SIZE(speeds_arr) < 3 || PyArray_SIZE(dyn_arr) < 128) {
    PyErr_SetString(PyExc_ValueError, "Yoshi Shy Guy param arrays have invalid lengths");
    return NULL;
  }

  npy_intp dims1[1] = {n};
  npy_intp dims2[2] = {n, slots};
  PyArrayObject* prev_vel_y = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_FLOAT32, 0);
  PyArrayObject* prev_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* phase = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* phase_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* timer = (PyArrayObject*)PyArray_ZEROS(1, dims1, NPY_UINT16, 0);
  PyArrayObject* pattern = (PyArrayObject*)PyArray_ZEROS(1, dims1, NPY_UINT8, 0);
  PyArrayObject* stage_valid = (PyArrayObject*)PyArray_ZEROS(1, dims1, NPY_UINT8, 0);
  PyArrayObject* speed_index = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* speed_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* delay = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT16, 0);
  PyArrayObject* delay_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* hitlag = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  PyArrayObject* hitlag_valid = (PyArrayObject*)PyArray_ZEROS(2, dims2, NPY_UINT8, 0);
  if (prev_vel_y == NULL || prev_valid == NULL || phase == NULL || phase_valid == NULL ||
      timer == NULL || pattern == NULL || stage_valid == NULL || speed_index == NULL ||
      speed_valid == NULL || delay == NULL || delay_valid == NULL || hitlag == NULL ||
      hitlag_valid == NULL) {
    Py_XDECREF(prev_vel_y);
    Py_XDECREF(prev_valid);
    Py_XDECREF(phase);
    Py_XDECREF(phase_valid);
    Py_XDECREF(timer);
    Py_XDECREF(pattern);
    Py_XDECREF(stage_valid);
    Py_XDECREF(speed_index);
    Py_XDECREF(speed_valid);
    Py_XDECREF(delay);
    Py_XDECREF(delay_valid);
    Py_XDECREF(hitlag);
    Py_XDECREF(hitlag_valid);
    return NULL;
  }

  const uint8_t* ex = (const uint8_t*)PyArray_DATA(exists);
  const uint16_t* ty = (const uint16_t*)PyArray_DATA(type);
  const int8_t* ow = (const int8_t*)PyArray_DATA(owner);
  const uint8_t* stp = (const uint8_t*)PyArray_DATA(state);
  const uint32_t* sp = (const uint32_t*)PyArray_DATA(spawn);
  const uint16_t* iidp = (const uint16_t*)PyArray_DATA(iid);
  const float* vx = (const float*)PyArray_DATA(vel_x);
  const float* vy = (const float*)PyArray_DATA(vel_y);
  const float* px = (const float*)PyArray_DATA(pos_x);
  const float* py = (const float*)PyArray_DATA(pos_y);
  const uint16_t* dmg = (const uint16_t*)PyArray_DATA(damage);
  const float* vpos = (const float*)PyArray_DATA(vpos_arr);
  const float* speeds = (const float*)PyArray_DATA(speeds_arr);
  const float* dyn = (const float*)PyArray_DATA(dyn_arr);

  float* out_prev_vy = (float*)PyArray_DATA(prev_vel_y);
  uint8_t* out_prev_valid = (uint8_t*)PyArray_DATA(prev_valid);
  uint8_t* out_phase = (uint8_t*)PyArray_DATA(phase);
  uint8_t* out_phase_valid = (uint8_t*)PyArray_DATA(phase_valid);
  uint16_t* out_timer = (uint16_t*)PyArray_DATA(timer);
  uint8_t* out_pattern = (uint8_t*)PyArray_DATA(pattern);
  uint8_t* out_stage_valid = (uint8_t*)PyArray_DATA(stage_valid);
  uint8_t* out_speed_idx = (uint8_t*)PyArray_DATA(speed_index);
  uint8_t* out_speed_valid = (uint8_t*)PyArray_DATA(speed_valid);
  uint16_t* out_delay = (uint16_t*)PyArray_DATA(delay);
  uint8_t* out_delay_valid = (uint8_t*)PyArray_DATA(delay_valid);
  uint8_t* out_hitlag = (uint8_t*)PyArray_DATA(hitlag);
  uint8_t* out_hitlag_valid = (uint8_t*)PyArray_DATA(hitlag_valid);

  const size_t cap = (size_t)(n * slots + 16);
  MslPyShyguyKeyState* key_states = (MslPyShyguyKeyState*)calloc(cap, sizeof(*key_states));
  MslPyShyguyPhaseState* phase_states = (MslPyShyguyPhaseState*)calloc(cap, sizeof(*phase_states));
  MslPyShyguySpawnState* spawn_states = (MslPyShyguySpawnState*)calloc(cap, sizeof(*spawn_states));
  MslPyShyguyGroupState* group_states = (MslPyShyguyGroupState*)calloc(cap, sizeof(*group_states));
  MslPyShyguyPrevItem* prev_items =
      (MslPyShyguyPrevItem*)calloc((size_t)(slots > 1 ? slots : 1), sizeof(*prev_items));
  int* shyguy_slots = (int*)calloc((size_t)(slots > 1 ? slots : 1), sizeof(*shyguy_slots));
  uint32_t* new_spawns = (uint32_t*)calloc((size_t)(slots > 1 ? slots : 1), sizeof(*new_spawns));
  if (key_states == NULL || phase_states == NULL || spawn_states == NULL || group_states == NULL ||
      prev_items == NULL || shyguy_slots == NULL || new_spawns == NULL) {
    free(key_states);
    free(phase_states);
    free(spawn_states);
    free(group_states);
    free(prev_items);
    free(shyguy_slots);
    free(new_spawns);
    PyErr_NoMemory();
    return NULL;
  }

  size_t key_count = 0;
  size_t phase_count = 0;
  size_t spawn_count = 0;
  size_t group_count = 0;
  int prev_item_count = 0;
  int cur_timer = timer_reset > 0 ? timer_reset - 1 : 0;
  int cur_pattern = 0;
  const bool stage_ok = replay_stage_id == param_stage_id;
  for (npy_intp fi = 0; fi < n; fi++) {
    int shyguy_count = 0;
    int new_count = 0;
    uint32_t group_base = UINT32_MAX;
    for (npy_intp slot = 0; slot < slots; slot++) {
      const npy_intp idx = fi * slots + slot;
      if (ex[idx] != 0u && ty[idx] == (uint16_t)item_kind && ow[idx] == -1) {
        shyguy_slots[shyguy_count++] = (int)slot;
        bool seen_spawn = false;
        for (size_t si = 0; si < spawn_count; si++) {
          if (spawn_states[si].used && spawn_states[si].spawn_id == sp[idx]) {
            seen_spawn = true;
            break;
          }
        }
        if (!seen_spawn) {
          new_spawns[new_count++] = sp[idx];
          if (sp[idx] < group_base) {
            group_base = sp[idx];
          }
        }
      }
    }

    if (stage_ok) {
      out_timer[fi] = (uint16_t)(cur_timer < 0 ? 0 : cur_timer);
      out_pattern[fi] = (uint8_t)cur_pattern;
      out_stage_valid[fi] = 1u;
    }

    for (int si = 0; si < shyguy_count; si++) {
      const int slot = shyguy_slots[si];
      const npy_intp idx = fi * slots + slot;
      for (int pi = 0; pi < prev_item_count; pi++) {
        bool match = false;
        if (sp[idx] != 0u) {
          match = prev_items[pi].spawn_id == sp[idx];
        } else {
          match = prev_items[pi].instance_id == iidp[idx];
        }
        if (match && (stp[idx] == 1u || stp[idx] == 4u)) {
          out_prev_vy[idx] = prev_items[pi].vel_y;
          out_prev_valid[idx] = 1u;
          break;
        }
      }
    }

    if (stage_ok && shyguy_count > 0) {
      cur_timer = timer_reset;
      const int first_slot = shyguy_slots[0];
      const npy_intp first_idx = fi * slots + first_slot;
      cur_pattern = msl_py_shyguy_pattern_from_item(px[first_idx], py[first_idx], vpos);
      out_pattern[fi] = (uint8_t)cur_pattern;
      if (new_count > 0) {
        for (int ni = 0; ni < new_count; ni++) {
          MslPyShyguySpawnState* ss =
              msl_py_shyguy_spawn_state(spawn_states, &spawn_count, cap, new_spawns[ni], (int)fi);
          if (ss == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy spawn-state capacity exhausted");
            goto fail;
          }
          ss->group_base = group_base;
        }
      }

      for (int si = 0; si < shyguy_count; si++) {
        const int slot = shyguy_slots[si];
        const npy_intp idx = fi * slots + slot;
        MslPyShyguySpawnState* ss =
            msl_py_shyguy_spawn_state(spawn_states, &spawn_count, cap, sp[idx], (int)fi);
        if (ss == NULL) {
          PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy spawn-state capacity exhausted");
          goto fail;
        }
        if ((stp[idx] == 1u || stp[idx] == 4u) && fabsf(vx[idx]) > 0.05f) {
          MslPyShyguyGroupState* gs =
              msl_py_shyguy_group_state(group_states, &group_count, cap, ss->group_base);
          if (gs == NULL) {
            PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy group-state capacity exhausted");
            goto fail;
          }
          gs->speed_index = (uint8_t)msl_py_shyguy_speed_index_from_item(vx[idx], stp[idx], speeds,
                                                                         (float)state4_speed_mul);
          gs->speed_valid = true;
        }
      }

      for (int si = 0; si < shyguy_count; si++) {
        const int slot = shyguy_slots[si];
        const npy_intp idx = fi * slots + slot;
        MslPyShyguySpawnState* ss =
            msl_py_shyguy_spawn_state(spawn_states, &spawn_count, cap, sp[idx], (int)fi);
        if (ss == NULL) {
          PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy spawn-state capacity exhausted");
          goto fail;
        }
        MslPyShyguyGroupState* gs =
            msl_py_shyguy_group_state(group_states, &group_count, cap, ss->group_base);
        if (gs == NULL) {
          PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy group-state capacity exhausted");
          goto fail;
        }
        if (gs->speed_valid) {
          out_speed_idx[idx] = gs->speed_index;
          out_speed_valid[idx] = 1u;
        }

        const uint32_t key_id = sp[idx] == 0u ? (uint32_t)iidp[idx] : sp[idx];
        MslPyShyguyKeyState* ks =
            msl_py_shyguy_key_state(key_states, &key_count, cap, sp[idx], key_id);
        if (ks == NULL) {
          PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy key-state capacity exhausted");
          goto fail;
        }
        int age = 0;
        if (ks->prev_state < 0 || ks->prev_state != (int)stp[idx]) {
          age = 0;
          if ((stp[idx] == 2u || stp[idx] == 3u) && dmg[idx] > 0u) {
            ks->hitlag = msl_py_shyguy_hitlag_from_damage(dmg[idx], (float)hitlag_damage_mul,
                                                          (float)hitlag_base);
          } else {
            ks->hitlag = 0;
          }
          ks->state3_moving_age = 0;
          ks->state4_zero_x_prefix = (stp[idx] == 4u && fabsf(vx[idx]) <= 0.001f) ? 20 : 0;
        } else {
          age = ks->state_age + 1;
        }

        const int arg0 = sp[idx] >= ss->group_base ? (int)(sp[idx] - ss->group_base) : 0;
        if (stp[idx] == 0u) {
          age = (int)fi - ss->first_seen;
          int remaining = (spawn_delay_step * arg0) - age - 1;
          if (remaining < 0) remaining = 0;
          out_delay[idx] = (uint16_t)remaining;
          out_delay_valid[idx] = 1u;
        } else if (stp[idx] == 3u) {
          int rem_hitlag = ks->hitlag;
          if (rem_hitlag < 0) rem_hitlag = 0;
          if (rem_hitlag > 255) rem_hitlag = 255;
          out_hitlag[idx] = (uint8_t)rem_hitlag;
          out_hitlag_valid[idx] = 1u;
          int remaining = 12 - ks->state3_moving_age;
          if (remaining < 0) remaining = 0;
          out_delay[idx] = (uint16_t)remaining;
          out_delay_valid[idx] = 1u;
          if (ks->hitlag > 0) {
            ks->hitlag--;
          } else {
            ks->state3_moving_age++;
          }
        } else if (stp[idx] == 2u) {
          int rem_hitlag = ks->hitlag;
          if (rem_hitlag < 0) rem_hitlag = 0;
          if (rem_hitlag > 255) rem_hitlag = 255;
          out_hitlag[idx] = (uint8_t)rem_hitlag;
          out_hitlag_valid[idx] = 1u;
          if (ks->hitlag > 0) {
            ks->hitlag--;
          }
        } else if (stp[idx] == 4u) {
          int rem_turn = ks->state4_zero_x_prefix;
          if (rem_turn < 0) rem_turn = 0;
          out_delay[idx] = (uint16_t)rem_turn;
          out_delay_valid[idx] = 1u;
          if (ks->state4_zero_x_prefix > 0) {
            ks->state4_zero_x_prefix--;
          }
          out_hitlag[idx] = 0u;
          out_hitlag_valid[idx] = 1u;
        } else if (stp[idx] == 1u) {
          out_delay[idx] = 0u;
          out_delay_valid[idx] = 1u;
          out_hitlag[idx] = 0u;
          out_hitlag_valid[idx] = 1u;
        }
        ks->prev_state = (int)stp[idx];
        ks->state_age = age;
      }
    } else if (stage_ok && cur_timer > 0) {
      cur_timer--;
    }

    for (int si = 0; si < shyguy_count; si++) {
      const int slot = shyguy_slots[si];
      const npy_intp idx = fi * slots + slot;
      if (stp[idx] != 1u && stp[idx] != 4u) {
        continue;
      }
      const uint32_t key_id = sp[idx] == 0u ? (uint32_t)iidp[idx] : sp[idx];
      MslPyShyguyPhaseState* ps =
          msl_py_shyguy_phase_state(phase_states, &phase_count, cap, sp[idx], key_id);
      if (ps == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Yoshi Shy Guy phase-state capacity exhausted");
        goto fail;
      }
      uint8_t cur_phase = 0u;
      if (ps->state == (int)stp[idx]) {
        const int predicted = ((int)ps->phase + 1) & 0xFF;
        int visible = -1;
        if (stp[idx] == 1u || fabsf(vx[idx]) >= 0.5f) {
          visible = msl_py_shyguy_visible_phase(dyn, ps->has_prev_vel_y ? ps->prev_vel_y : 0.0f,
                                                vy[idx], stp[idx], predicted);
        }
        cur_phase = (uint8_t)(visible < 0 ? predicted : visible);
      }
      out_phase[idx] = cur_phase;
      out_phase_valid[idx] = 1u;
      ps->state = (int)stp[idx];
      ps->phase = cur_phase;
      ps->prev_vel_y = vy[idx];
      ps->has_prev_vel_y = true;
    }

    prev_item_count = 0;
    for (int si = 0; si < shyguy_count && prev_item_count < slots; si++) {
      const int slot = shyguy_slots[si];
      const npy_intp idx = fi * slots + slot;
      if (stp[idx] == 1u || stp[idx] == 4u) {
        prev_items[prev_item_count].state = stp[idx];
        prev_items[prev_item_count].spawn_id = sp[idx];
        prev_items[prev_item_count].instance_id = iidp[idx];
        prev_items[prev_item_count].vel_y = vy[idx];
        prev_item_count++;
      }
    }
  }

  free(key_states);
  free(phase_states);
  free(spawn_states);
  free(group_states);
  free(prev_items);
  free(shyguy_slots);
  free(new_spawns);
  return Py_BuildValue("NNNNNNNNNNNNN", prev_vel_y, prev_valid, phase, phase_valid, timer, pattern,
                       stage_valid, speed_index, speed_valid, delay, delay_valid, hitlag,
                       hitlag_valid);

fail:
  free(key_states);
  free(phase_states);
  free(spawn_states);
  free(group_states);
  free(prev_items);
  free(shyguy_slots);
  free(new_spawns);
  Py_XDECREF(prev_vel_y);
  Py_XDECREF(prev_valid);
  Py_XDECREF(phase);
  Py_XDECREF(phase_valid);
  Py_XDECREF(timer);
  Py_XDECREF(pattern);
  Py_XDECREF(stage_valid);
  Py_XDECREF(speed_index);
  Py_XDECREF(speed_valid);
  Py_XDECREF(delay);
  Py_XDECREF(delay_valid);
  Py_XDECREF(hitlag);
  Py_XDECREF(hitlag_valid);
  return NULL;
}

PyObject* msl_derive_item_hidden_callback_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_exists_obj = NULL;
  PyObject* seed_type_obj = NULL;
  PyObject* seed_owner_obj = NULL;
  PyObject* seed_iid_obj = NULL;
  PyObject* seed_spawn_obj = NULL;
  PyObject* seed_vx_obj = NULL;
  PyObject* seed_vy_obj = NULL;
  PyObject* ref_exists_obj = NULL;
  PyObject* ref_type_obj = NULL;
  PyObject* ref_owner_obj = NULL;
  PyObject* ref_iid_obj = NULL;
  PyObject* ref_spawn_obj = NULL;
  PyObject* ref_vx_obj = NULL;
  PyObject* ref_vy_obj = NULL;
  PyObject* seed_action_obj = NULL;
  PyObject* ref_action_obj = NULL;
  PyObject* laser_lut_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOOOOOOi", &seed_exists_obj, &seed_type_obj,
                        &seed_owner_obj, &seed_iid_obj, &seed_spawn_obj, &seed_vx_obj, &seed_vy_obj,
                        &ref_exists_obj, &ref_type_obj, &ref_owner_obj, &ref_iid_obj,
                        &ref_spawn_obj, &ref_vx_obj, &ref_vy_obj, &seed_action_obj, &ref_action_obj,
                        &laser_lut_obj, &num_players)) {
    return NULL;
  }
  PyArrayObject* seed_exists =
      require_contiguous_array(seed_exists_obj, NPY_UINT8, 2, "seed_item_exists_u8");
  PyArrayObject* seed_type =
      require_contiguous_array(seed_type_obj, NPY_UINT16, 2, "seed_item_type_u16");
  PyArrayObject* seed_owner =
      require_contiguous_array(seed_owner_obj, NPY_INT8, 2, "seed_item_owner_i8");
  PyArrayObject* seed_iid =
      require_contiguous_array(seed_iid_obj, NPY_UINT16, 2, "seed_item_instance_id_u16");
  PyArrayObject* seed_spawn =
      require_contiguous_array(seed_spawn_obj, NPY_UINT32, 2, "seed_item_spawn_id_u32");
  PyArrayObject* seed_vx =
      require_contiguous_array(seed_vx_obj, NPY_FLOAT32, 2, "seed_item_vel_x_f32");
  PyArrayObject* seed_vy =
      require_contiguous_array(seed_vy_obj, NPY_FLOAT32, 2, "seed_item_vel_y_f32");
  PyArrayObject* ref_exists =
      require_contiguous_array(ref_exists_obj, NPY_UINT8, 2, "ref_item_exists_u8");
  PyArrayObject* ref_type =
      require_contiguous_array(ref_type_obj, NPY_UINT16, 2, "ref_item_type_u16");
  PyArrayObject* ref_owner =
      require_contiguous_array(ref_owner_obj, NPY_INT8, 2, "ref_item_owner_i8");
  PyArrayObject* ref_iid =
      require_contiguous_array(ref_iid_obj, NPY_UINT16, 2, "ref_item_instance_id_u16");
  PyArrayObject* ref_spawn =
      require_contiguous_array(ref_spawn_obj, NPY_UINT32, 2, "ref_item_spawn_id_u32");
  PyArrayObject* ref_vx =
      require_contiguous_array(ref_vx_obj, NPY_FLOAT32, 2, "ref_item_vel_x_f32");
  PyArrayObject* ref_vy =
      require_contiguous_array(ref_vy_obj, NPY_FLOAT32, 2, "ref_item_vel_y_f32");
  PyArrayObject* seed_action =
      require_contiguous_array(seed_action_obj, NPY_UINT16, 2, "seed_action_id_u16");
  PyArrayObject* ref_action =
      require_contiguous_array(ref_action_obj, NPY_UINT16, 2, "ref_action_id_u16");
  PyArrayObject* laser_lut =
      require_contiguous_array(laser_lut_obj, NPY_UINT8, 1, "laser_type_lut");
  if (seed_exists == NULL || seed_type == NULL || seed_owner == NULL || seed_iid == NULL ||
      seed_spawn == NULL || seed_vx == NULL || seed_vy == NULL || ref_exists == NULL ||
      ref_type == NULL || ref_owner == NULL || ref_iid == NULL || ref_spawn == NULL ||
      ref_vx == NULL || ref_vy == NULL || seed_action == NULL || ref_action == NULL ||
      laser_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_exists, 0);
  const npy_intp slots = PyArray_DIM(seed_exists, 1);
  if (require_exact_2d_shape(seed_type, n, slots, "seed_item_type_u16") < 0 ||
      require_exact_2d_shape(seed_owner, n, slots, "seed_item_owner_i8") < 0 ||
      require_exact_2d_shape(seed_iid, n, slots, "seed_item_instance_id_u16") < 0 ||
      require_exact_2d_shape(seed_spawn, n, slots, "seed_item_spawn_id_u32") < 0 ||
      require_exact_2d_shape(seed_vx, n, slots, "seed_item_vel_x_f32") < 0 ||
      require_exact_2d_shape(seed_vy, n, slots, "seed_item_vel_y_f32") < 0 ||
      require_exact_2d_shape(ref_exists, n, slots, "ref_item_exists_u8") < 0 ||
      require_exact_2d_shape(ref_type, n, slots, "ref_item_type_u16") < 0 ||
      require_exact_2d_shape(ref_owner, n, slots, "ref_item_owner_i8") < 0 ||
      require_exact_2d_shape(ref_iid, n, slots, "ref_item_instance_id_u16") < 0 ||
      require_exact_2d_shape(ref_spawn, n, slots, "ref_item_spawn_id_u32") < 0 ||
      require_exact_2d_shape(ref_vx, n, slots, "ref_item_vel_x_f32") < 0 ||
      require_exact_2d_shape(ref_vy, n, slots, "ref_item_vel_y_f32") < 0) {
    return NULL;
  }
  if (PyArray_NDIM(seed_action) != 2 || PyArray_NDIM(ref_action) != 2 ||
      PyArray_DIM(seed_action, 0) != n || PyArray_DIM(ref_action, 0) != n ||
      PyArray_DIM(seed_action, 1) != PyArray_DIM(ref_action, 1) ||
      PyArray_SIZE(laser_lut) < 65536) {
    PyErr_SetString(PyExc_ValueError, "item hidden callback action/LUT inputs are invalid");
    return NULL;
  }
  const npy_intp width = PyArray_DIM(seed_action, 1);
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  npy_intp dims[2] = {n, slots};
  PyArrayObject* reflect_port = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_UINT8, 0);
  PyArrayObject* reflect_iid = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  PyArrayObject* bounce_valid = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  PyArrayObject* bounce_vx = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* bounce_vy = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* body_victim = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_UINT8, 0);
  PyArrayObject* body_height = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  PyArrayObject* callback_flags = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  if (reflect_port == NULL || reflect_iid == NULL || bounce_valid == NULL || bounce_vx == NULL ||
      bounce_vy == NULL || body_victim == NULL || body_height == NULL || callback_flags == NULL) {
    Py_XDECREF(reflect_port);
    Py_XDECREF(reflect_iid);
    Py_XDECREF(bounce_valid);
    Py_XDECREF(bounce_vx);
    Py_XDECREF(bounce_vy);
    Py_XDECREF(body_victim);
    Py_XDECREF(body_height);
    Py_XDECREF(callback_flags);
    return NULL;
  }
  uint8_t* rp = (uint8_t*)PyArray_DATA(reflect_port);
  uint8_t* bvictim = (uint8_t*)PyArray_DATA(body_victim);
  for (npy_intp k = 0; k < n * slots; k++) {
    rp[k] = 0xFFu;
    bvictim[k] = 0xFFu;
  }
  const uint8_t* se = (const uint8_t*)PyArray_DATA(seed_exists);
  const uint16_t* st = (const uint16_t*)PyArray_DATA(seed_type);
  const int8_t* so = (const int8_t*)PyArray_DATA(seed_owner);
  const uint16_t* siid = (const uint16_t*)PyArray_DATA(seed_iid);
  const uint32_t* sspawn = (const uint32_t*)PyArray_DATA(seed_spawn);
  const float* svx = (const float*)PyArray_DATA(seed_vx);
  const float* svy = (const float*)PyArray_DATA(seed_vy);
  const uint8_t* re = (const uint8_t*)PyArray_DATA(ref_exists);
  const uint16_t* rt = (const uint16_t*)PyArray_DATA(ref_type);
  const int8_t* ro = (const int8_t*)PyArray_DATA(ref_owner);
  const uint16_t* riid = (const uint16_t*)PyArray_DATA(ref_iid);
  const uint32_t* rspawn = (const uint32_t*)PyArray_DATA(ref_spawn);
  const float* rvx = (const float*)PyArray_DATA(ref_vx);
  const float* rvy = (const float*)PyArray_DATA(ref_vy);
  const uint16_t* sa = (const uint16_t*)PyArray_DATA(seed_action);
  const uint16_t* ra = (const uint16_t*)PyArray_DATA(ref_action);
  const uint8_t* laser = (const uint8_t*)PyArray_DATA(laser_lut);
  uint16_t* riid_out = (uint16_t*)PyArray_DATA(reflect_iid);
  uint8_t* bvalid = (uint8_t*)PyArray_DATA(bounce_valid);
  float* bvx = (float*)PyArray_DATA(bounce_vx);
  float* bvy = (float*)PyArray_DATA(bounce_vy);
  for (npy_intp i = 0; i < n; i++) {
    bool guard_context = false;
    for (int p = 0; p < players; p++) {
      const npy_intp pidx = (i * width) + p;
      if (msl_py_guard_family_action(sa[pidx]) || msl_py_guard_family_action(ra[pidx])) {
        guard_context = true;
        break;
      }
    }
    if (!guard_context) continue;
    for (npy_intp it = 0; it < slots; it++) {
      const npy_intp idx = (i * slots) + it;
      if (se[idx] == 0u) continue;
      const uint16_t item_type = st[idx];
      if (laser[item_type] == 0u) continue;
      const uint16_t seed_item_iid = siid[idx];
      const int seed_item_owner = (int)so[idx];
      const bool ref_exists_now = re[idx] != 0u;
      const bool ref_same_item = ref_exists_now && rt[idx] == item_type &&
                                 riid[idx] == seed_item_iid && rspawn[idx] == sspawn[idx];
      if (ref_exists_now && rt[idx] == item_type && rspawn[idx] == sspawn[idx]) {
        const int ref_item_owner = (int)ro[idx];
        const uint16_t ref_item_iid = riid[idx];
        if (ref_item_owner >= 0 && ref_item_owner < players &&
            (ref_item_owner != seed_item_owner || ref_item_iid != seed_item_iid)) {
          rp[idx] = (uint8_t)ref_item_owner;
          riid_out[idx] = ref_item_iid;
        }
      }
      if (ref_same_item && (int)ro[idx] == seed_item_owner) {
        const float dx = rvx[idx] - svx[idx];
        const float dy = rvy[idx] - svy[idx];
        if (isfinite(svx[idx]) && isfinite(svy[idx]) && isfinite(rvx[idx]) && isfinite(rvy[idx]) &&
            (dx * dx + dy * dy) > 0.25f) {
          bvalid[idx] = 1u;
          bvx[idx] = rvx[idx];
          bvy[idx] = rvy[idx];
        }
      }
    }
  }
  return Py_BuildValue("NNNNNNNN", reflect_port, reflect_iid, bounce_valid, bounce_vx, bounce_vy,
                       body_victim, body_height, callback_flags);
}

PyObject* msl_derive_illusion_seed_position_updates_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* item_exists_obj = NULL;
  PyObject* item_type_obj = NULL;
  PyObject* item_owner_obj = NULL;
  PyObject* item_iid_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* instance_hit_by_obj = NULL;
  PyObject* ghost_x_obj = NULL;
  PyObject* ghost_y_obj = NULL;
  PyObject* illusion_lut_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOi", &item_exists_obj, &item_type_obj, &item_owner_obj,
                        &item_iid_obj, &action_obj, &hitlag_obj, &instance_hit_by_obj, &ghost_x_obj,
                        &ghost_y_obj, &illusion_lut_obj, &num_players)) {
    return NULL;
  }
  PyArrayObject* item_exists =
      require_contiguous_array(item_exists_obj, NPY_UINT8, 2, "item_exists_u8");
  PyArrayObject* item_type =
      require_contiguous_array(item_type_obj, NPY_UINT16, 2, "item_type_u16");
  PyArrayObject* item_owner =
      require_contiguous_array(item_owner_obj, NPY_INT8, 2, "item_owner_i8");
  PyArrayObject* item_iid =
      require_contiguous_array(item_iid_obj, NPY_UINT16, 2, "item_instance_id_u16");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT8, 2, "hitlag_u8");
  PyArrayObject* instance_hit_by =
      require_contiguous_array(instance_hit_by_obj, NPY_UINT16, 2, "instance_hit_by_u16");
  PyArrayObject* ghost_x = require_contiguous_array(ghost_x_obj, NPY_FLOAT32, 2, "ghost_x_f32");
  PyArrayObject* ghost_y = require_contiguous_array(ghost_y_obj, NPY_FLOAT32, 2, "ghost_y_f32");
  PyArrayObject* illusion_lut =
      require_contiguous_array(illusion_lut_obj, NPY_UINT8, 1, "illusion_item_kind_lut");
  if (item_exists == NULL || item_type == NULL || item_owner == NULL || item_iid == NULL ||
      action == NULL || hitlag == NULL || instance_hit_by == NULL || ghost_x == NULL ||
      ghost_y == NULL || illusion_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(item_exists, 0);
  const npy_intp slots = PyArray_DIM(item_exists, 1);
  if (require_exact_2d_shape(item_type, n, slots, "item_type_u16") < 0 ||
      require_exact_2d_shape(item_owner, n, slots, "item_owner_i8") < 0 ||
      require_exact_2d_shape(item_iid, n, slots, "item_instance_id_u16") < 0) {
    return NULL;
  }
  if (PyArray_NDIM(action) != 2 || PyArray_NDIM(hitlag) != 2 ||
      PyArray_NDIM(instance_hit_by) != 2 || PyArray_NDIM(ghost_x) != 2 ||
      PyArray_NDIM(ghost_y) != 2 || PyArray_DIM(action, 0) != n || PyArray_DIM(hitlag, 0) != n ||
      PyArray_DIM(instance_hit_by, 0) != n || PyArray_DIM(ghost_x, 0) != n ||
      PyArray_DIM(ghost_y, 0) != n || PyArray_DIM(action, 1) != PyArray_DIM(hitlag, 1) ||
      PyArray_DIM(action, 1) != PyArray_DIM(instance_hit_by, 1) ||
      PyArray_DIM(action, 1) != PyArray_DIM(ghost_x, 1) ||
      PyArray_DIM(action, 1) != PyArray_DIM(ghost_y, 1) || PyArray_SIZE(illusion_lut) < 65536) {
    PyErr_SetString(PyExc_ValueError, "illusion seed position inputs have incompatible shapes");
    return NULL;
  }
  const npy_intp width = PyArray_DIM(action, 1);
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  npy_intp dims[2] = {n, slots};
  PyArrayObject* mask = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  PyArrayObject* out_x = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  if (mask == NULL || out_x == NULL || out_y == NULL) {
    Py_XDECREF(mask);
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    return NULL;
  }
  const uint8_t* exists = (const uint8_t*)PyArray_DATA(item_exists);
  const uint16_t* type = (const uint16_t*)PyArray_DATA(item_type);
  const int8_t* owner_data = (const int8_t*)PyArray_DATA(item_owner);
  const uint16_t* iid_data = (const uint16_t*)PyArray_DATA(item_iid);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* hitlag_data = (const uint8_t*)PyArray_DATA(hitlag);
  const uint16_t* ihb_data = (const uint16_t*)PyArray_DATA(instance_hit_by);
  const float* gx = (const float*)PyArray_DATA(ghost_x);
  const float* gy = (const float*)PyArray_DATA(ghost_y);
  const uint8_t* illusion = (const uint8_t*)PyArray_DATA(illusion_lut);
  uint8_t* m = (uint8_t*)PyArray_DATA(mask);
  float* ox = (float*)PyArray_DATA(out_x);
  float* oy = (float*)PyArray_DATA(out_y);
  for (npy_intp fi = 1; fi < n; fi++) {
    for (npy_intp slot = 0; slot < slots; slot++) {
      const npy_intp item_idx = (fi * slots) + slot;
      if (exists[item_idx] == 0u || illusion[type[item_idx]] == 0u) continue;
      const int owner = (int)owner_data[item_idx];
      if (owner < 0 || owner >= players) continue;
      const uint16_t owner_action = action_data[(fi * width) + owner];
      const bool setphys = owner_action == 348u || owner_action == 349u || owner_action == 351u ||
                           owner_action == 352u;
      if (!setphys) continue;
      bool ongoing_guardsetoff_hitlag = false;
      int candidate_victim = -1;
      for (int p = 0; p < players; p++) {
        const npy_intp pidx = (fi * width) + p;
        if (hitlag_data[pidx] == 0u || action_data[pidx] != 181u) continue;
        if (candidate_victim >= 0) {
          candidate_victim = -2;
          break;
        }
        candidate_victim = p;
      }
      if (candidate_victim >= 0) {
        int illusion_candidates = 0;
        for (npy_intp other = 0; other < slots; other++) {
          const npy_intp other_idx = (fi * slots) + other;
          if (exists[other_idx] != 0u && illusion[type[other_idx]] != 0u) {
            illusion_candidates++;
            if (illusion_candidates > 1) break;
          }
        }
        ongoing_guardsetoff_hitlag = illusion_candidates == 1;
      }
      bool ongoing_body_hitlag = false;
      if (!ongoing_guardsetoff_hitlag) {
        for (int p = 0; p < players; p++) {
          const npy_intp pidx = (fi * width) + p;
          if (hitlag_data[pidx] != 0u && ihb_data[pidx] == iid_data[item_idx]) {
            ongoing_body_hitlag = true;
            break;
          }
        }
      }
      if (ongoing_guardsetoff_hitlag || ongoing_body_hitlag) continue;
      m[item_idx] = 1u;
      ox[item_idx] = gx[(fi * width) + owner];
      oy[item_idx] = gy[(fi * width) + owner];
    }
  }
  return Py_BuildValue("NNN", mask, out_x, out_y);
}

PyObject* msl_trim_stale_hitlist_seed_bridge_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* hitlist_cd_obj = NULL;
  PyObject* hitlist_iid_obj = NULL;
  PyObject* hitlist_hb_valid_obj = NULL;
  PyObject* hitlist_hb_cd_obj = NULL;
  PyObject* hitlist_hb_iid_obj = NULL;
  PyObject* post_instance_id_obj = NULL;
  PyObject* post_instance_hit_by_obj = NULL;
  PyObject* post_last_hit_by_obj = NULL;
  PyObject* post_hitlag_obj = NULL;
  PyObject* post_hitstun_obj = NULL;
  PyObject* post_action_obj = NULL;
  int num_players = 0;
  int act_attack_11 = 0;
  int act_attack_lw4 = 0;
  int act_damage_fly_top = 0;
  int act_landing_fall_special = 0;
  int act_guard_on = 0;
  int act_guard = 0;
  int act_guard_set_off = 0;
  int act_guard_reflect = 0;
  int act_guard_off = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOiiiiiiiiii", &hitlist_cd_obj, &hitlist_iid_obj,
                        &hitlist_hb_valid_obj, &hitlist_hb_cd_obj, &hitlist_hb_iid_obj,
                        &post_instance_id_obj, &post_instance_hit_by_obj, &post_last_hit_by_obj,
                        &post_hitlag_obj, &post_hitstun_obj, &post_action_obj, &num_players,
                        &act_attack_11, &act_attack_lw4, &act_damage_fly_top,
                        &act_landing_fall_special, &act_guard_on, &act_guard, &act_guard_set_off,
                        &act_guard_reflect, &act_guard_off)) {
    return NULL;
  }
  PyArrayObject* hitlist_cd = require_contiguous_array(hitlist_cd_obj, NPY_UINT16, 4, "hitlist_cd");
  PyArrayObject* hitlist_iid =
      require_contiguous_array(hitlist_iid_obj, NPY_UINT16, 4, "hitlist_iid");
  PyArrayObject* hitlist_hb_valid =
      require_contiguous_array(hitlist_hb_valid_obj, NPY_UINT8, 3, "hitlist_hb_valid");
  PyArrayObject* hitlist_hb_cd =
      require_contiguous_array(hitlist_hb_cd_obj, NPY_UINT16, 4, "hitlist_hb_cd");
  PyArrayObject* hitlist_hb_iid =
      require_contiguous_array(hitlist_hb_iid_obj, NPY_UINT16, 4, "hitlist_hb_iid");
  PyArrayObject* post_instance_id =
      require_contiguous_array(post_instance_id_obj, NPY_UINT16, 2, "post_instance_id");
  PyArrayObject* post_instance_hit_by =
      require_contiguous_array(post_instance_hit_by_obj, NPY_UINT16, 2, "post_instance_hit_by");
  PyArrayObject* post_last_hit_by =
      require_contiguous_array(post_last_hit_by_obj, NPY_UINT8, 2, "post_last_hit_by");
  PyArrayObject* post_hitlag =
      require_contiguous_array(post_hitlag_obj, NPY_UINT16, 2, "post_hitlag");
  PyArrayObject* post_hitstun =
      require_contiguous_array(post_hitstun_obj, NPY_UINT16, 2, "post_hitstun");
  PyArrayObject* post_action =
      require_contiguous_array(post_action_obj, NPY_UINT16, 2, "post_action");
  if (hitlist_cd == NULL || hitlist_iid == NULL || hitlist_hb_valid == NULL ||
      hitlist_hb_cd == NULL || hitlist_hb_iid == NULL || post_instance_id == NULL ||
      post_instance_hit_by == NULL || post_last_hit_by == NULL || post_hitlag == NULL ||
      post_hitstun == NULL || post_action == NULL) {
    return NULL;
  }
  if (PyArray_NDIM(hitlist_cd) != 4 || PyArray_NDIM(hitlist_iid) != 4 ||
      PyArray_NDIM(hitlist_hb_valid) != 3 || PyArray_NDIM(hitlist_hb_cd) != 4 ||
      PyArray_NDIM(hitlist_hb_iid) != 4) {
    PyErr_SetString(PyExc_ValueError, "hitlist arrays have invalid rank");
    return NULL;
  }
  const npy_intp n = PyArray_DIM(hitlist_cd, 0);
  const npy_intp width = PyArray_DIM(hitlist_cd, 1);
  const npy_intp coarse_hb = PyArray_DIM(hitlist_cd, 2);
  const npy_intp defenders = PyArray_DIM(hitlist_cd, 3);
  const npy_intp hb_count = PyArray_DIM(hitlist_hb_valid, 2);
  if (PyArray_DIM(hitlist_iid, 0) != n || PyArray_DIM(hitlist_iid, 1) != width ||
      PyArray_DIM(hitlist_iid, 2) != coarse_hb || PyArray_DIM(hitlist_iid, 3) != defenders ||
      PyArray_DIM(hitlist_hb_valid, 0) != n || PyArray_DIM(hitlist_hb_valid, 1) != width ||
      PyArray_DIM(hitlist_hb_cd, 0) != n || PyArray_DIM(hitlist_hb_cd, 1) != width ||
      PyArray_DIM(hitlist_hb_cd, 2) != hb_count || PyArray_DIM(hitlist_hb_cd, 3) != defenders ||
      PyArray_DIM(hitlist_hb_iid, 0) != n || PyArray_DIM(hitlist_hb_iid, 1) != width ||
      PyArray_DIM(hitlist_hb_iid, 2) != hb_count || PyArray_DIM(hitlist_hb_iid, 3) != defenders ||
      require_exact_2d_shape(post_instance_id, n, width, "post_instance_id") < 0 ||
      require_exact_2d_shape(post_instance_hit_by, n, width, "post_instance_hit_by") < 0 ||
      require_exact_2d_shape(post_last_hit_by, n, width, "post_last_hit_by") < 0 ||
      require_exact_2d_shape(post_hitlag, n, width, "post_hitlag") < 0 ||
      require_exact_2d_shape(post_hitstun, n, width, "post_hitstun") < 0 ||
      require_exact_2d_shape(post_action, n, width, "post_action") < 0) {
    return NULL;
  }
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > defenders) players = (int)defenders;
  if (players > 4) players = 4;
  uint16_t* cd = (uint16_t*)PyArray_DATA(hitlist_cd);
  uint16_t* iid = (uint16_t*)PyArray_DATA(hitlist_iid);
  const uint8_t* hb_valid = (const uint8_t*)PyArray_DATA(hitlist_hb_valid);
  uint16_t* hb_cd = (uint16_t*)PyArray_DATA(hitlist_hb_cd);
  uint16_t* hb_iid = (uint16_t*)PyArray_DATA(hitlist_hb_iid);
  const uint16_t* inst = (const uint16_t*)PyArray_DATA(post_instance_id);
  const uint16_t* inst_by = (const uint16_t*)PyArray_DATA(post_instance_hit_by);
  const uint8_t* last_by = (const uint8_t*)PyArray_DATA(post_last_hit_by);
  const uint16_t* hitlag = (const uint16_t*)PyArray_DATA(post_hitlag);
  const uint16_t* hitstun = (const uint16_t*)PyArray_DATA(post_hitstun);
  const uint16_t* action = (const uint16_t*)PyArray_DATA(post_action);
  for (npy_intp fi = 0; fi < n; fi++) {
    for (int attacker = 0; attacker < players; attacker++) {
      const uint16_t attacker_iid = inst[(fi * width) + attacker];
      for (int defender = 0; defender < players; defender++) {
        const npy_intp def_idx = (fi * width) + defender;
        if (inst_by[def_idx] == attacker_iid) continue;
        const uint16_t defender_action = action[def_idx];
        const uint8_t last_hit_by_owner = last_by[def_idx];
        const uint16_t owner_iid = inst_by[def_idx];
        bool owner_matches = last_hit_by_owner == (uint8_t)attacker;
        if (!owner_matches) {
          bool owner_iid_matches_live = false;
          for (int p = 0; p < players; p++) {
            if (inst[(fi * width) + p] == owner_iid) {
              owner_iid_matches_live = true;
              break;
            }
          }
          owner_matches = players == 2 && attacker != defender &&
                          defender_action > (uint16_t)act_attack_lw4 &&
                          last_hit_by_owner >= (uint8_t)players && !owner_iid_matches_live;
        }
        if (!owner_matches) continue;
        int hitlag_pre = (int)hitlag[def_idx];
        if (hitlag_pre > 0) hitlag_pre--;
        int hitstun_pre = (int)hitstun[def_idx];
        if (hitstun_pre > 0) hitstun_pre--;
        const bool guard_family = defender_action == (uint16_t)act_guard_on ||
                                  defender_action == (uint16_t)act_guard ||
                                  defender_action == (uint16_t)act_guard_set_off ||
                                  defender_action == (uint16_t)act_guard_reflect ||
                                  defender_action == (uint16_t)act_guard_off;
        const bool neutral_non_guard = hitlag_pre == 0 && hitstun_pre == 0 && !guard_family &&
                                       defender_action != (uint16_t)act_landing_fall_special;
        const uint16_t attacker_action = action[(fi * width) + attacker];
        const bool damage_state_bridge = hitlag_pre == 0 &&
                                         defender_action == (uint16_t)act_damage_fly_top &&
                                         attacker_action >= (uint16_t)act_attack_11 &&
                                         attacker_action <= (uint16_t)act_attack_lw4;
        if (!(neutral_non_guard || damage_state_bridge)) continue;
        for (npy_intp hb = 0; hb < coarse_hb; hb++) {
          const npy_intp idx = (((fi * width + attacker) * coarse_hb + hb) * defenders) + defender;
          if (cd[idx] == 0xFFFFu) {
            cd[idx] = 0u;
            iid[idx] = 0u;
          }
        }
        for (npy_intp hb = 0; hb < hb_count; hb++) {
          const npy_intp idx = (((fi * width + attacker) * hb_count + hb) * defenders) + defender;
          const npy_intp valid_idx = ((fi * width + attacker) * hb_count) + hb;
          if (hb_cd[idx] == 0xFFFFu && hb_valid[valid_idx] == 0u) {
            hb_cd[idx] = 0u;
            hb_iid[idx] = 0u;
          }
        }
      }
    }
  }
  Py_RETURN_NONE;
}

static float msl_py_stale_multiplier_from_queue(uint8_t queue_index, const uint16_t* move_ids,
                                                npy_intp stride, uint16_t move_id,
                                                const float* weights, npy_intp weight_count) {
  if (move_id == 0xFFFFu || move_id == 1u) return 1.0f;
  const int qi = queue_index < 10u ? (int)queue_index : 0;
  int pos = qi != 0 ? qi - 1 : 9;
  float mult = 1.0f;
  for (int i = 0; i < 9; i++) {
    if (move_ids[(npy_intp)pos] == move_id && i < weight_count) {
      mult -= weights[i];
    }
    pos = pos != 0 ? pos - 1 : 9;
  }
  (void)stride;
  return mult;
}

static int msl_py_env_dmg_from_float(float dmg) {
  if (dmg == 0.0f) return 0;
  const int i = (int)dmg;
  return i != 0 ? i : 1;
}

PyObject* msl_derive_attacker_shield_ground_kb_vel_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* attack_id_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* animation_index_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* ground_friction_mul_obj = NULL;
  PyObject* lightshield_obj = NULL;
  PyObject* guard_damage_obj = NULL;
  PyObject* stale_queue_obj = NULL;
  PyObject* stale_move_id_obj = NULL;
  PyObject* active_damage_lut_obj = NULL;
  PyObject* char_friction_lut_obj = NULL;
  PyObject* stale_weights_obj = NULL;
  int num_players = 0;
  int act_guard_set_off = 0;
  int act_guard_reflect = 0;
  double shield_kb_mul = 0.0;
  double shield_kb_base = 0.0;
  double shield_kb_friction_mul = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOOOOOiiiddd", &action_obj, &attack_id_obj,
                        &anim_frame_obj, &animation_index_obj, &on_ground_obj, &hitlag_obj,
                        &pos_x_obj, &char_obj, &ground_friction_mul_obj, &lightshield_obj,
                        &guard_damage_obj, &stale_queue_obj, &stale_move_id_obj,
                        &active_damage_lut_obj, &char_friction_lut_obj, &stale_weights_obj,
                        &num_players, &act_guard_set_off, &act_guard_reflect, &shield_kb_mul,
                        &shield_kb_base, &shield_kb_friction_mul)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* attack_id =
      require_contiguous_array(attack_id_obj, NPY_UINT16, 2, "attack_id_u16");
  PyArrayObject* anim_frame =
      require_contiguous_array(anim_frame_obj, NPY_FLOAT32, 2, "anim_frame_f32");
  PyArrayObject* animation_index =
      require_contiguous_array(animation_index_obj, NPY_UINT32, 2, "animation_index_u32");
  PyArrayObject* on_ground = require_contiguous_array(on_ground_obj, NPY_UINT8, 2, "on_ground_u8");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "pos_x_f32");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* ground_friction_mul =
      require_contiguous_array(ground_friction_mul_obj, NPY_FLOAT32, 2, "ground_friction_mul");
  PyArrayObject* lightshield =
      require_contiguous_array(lightshield_obj, NPY_FLOAT32, 2, "lightshield_amount");
  PyArrayObject* guard_damage =
      require_contiguous_array(guard_damage_obj, NPY_UINT8, 2, "guard_setoff_hitlag_damage_min");
  PyArrayObject* stale_queue =
      require_contiguous_array(stale_queue_obj, NPY_UINT8, 2, "stale_queue_index");
  PyArrayObject* stale_move_id =
      require_contiguous_array(stale_move_id_obj, NPY_UINT16, 3, "stale_move_id");
  PyArrayObject* active_damage_lut =
      require_contiguous_array(active_damage_lut_obj, NPY_UINT16, 3, "active_damage_lut");
  PyArrayObject* char_friction_lut =
      require_contiguous_array(char_friction_lut_obj, NPY_FLOAT32, 1, "char_friction_lut");
  PyArrayObject* stale_weights =
      require_contiguous_array(stale_weights_obj, NPY_FLOAT32, 1, "stale_weights");
  if (action == NULL || attack_id == NULL || anim_frame == NULL || animation_index == NULL ||
      on_ground == NULL || hitlag == NULL || pos_x == NULL || chr == NULL ||
      ground_friction_mul == NULL || lightshield == NULL || guard_damage == NULL ||
      stale_queue == NULL || stale_move_id == NULL || active_damage_lut == NULL ||
      char_friction_lut == NULL || stale_weights == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(attack_id, n, width, "attack_id_u16") < 0 ||
      require_exact_2d_shape(anim_frame, n, width, "anim_frame_f32") < 0 ||
      require_exact_2d_shape(animation_index, n, width, "animation_index_u32") < 0 ||
      require_exact_2d_shape(on_ground, n, width, "on_ground_u8") < 0 ||
      require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0 ||
      require_exact_2d_shape(pos_x, n, width, "pos_x_f32") < 0 ||
      require_exact_2d_shape(chr, n, width, "char_id_u8") < 0 ||
      require_exact_2d_shape(ground_friction_mul, n, width, "ground_friction_mul") < 0 ||
      require_exact_2d_shape(lightshield, n, width, "lightshield_amount") < 0 ||
      require_exact_2d_shape(guard_damage, n, width, "guard_setoff_hitlag_damage_min") < 0 ||
      require_exact_2d_shape(stale_queue, n, width, "stale_queue_index") < 0 ||
      PyArray_NDIM(stale_move_id) != 3 || PyArray_DIM(stale_move_id, 0) != n ||
      PyArray_DIM(stale_move_id, 1) != width || PyArray_DIM(stale_move_id, 2) < 10 ||
      PyArray_NDIM(active_damage_lut) != 3 || PyArray_DIM(active_damage_lut, 0) < 256 ||
      PyArray_SIZE(char_friction_lut) < 256 || PyArray_SIZE(stale_weights) < 9) {
    PyErr_SetString(PyExc_ValueError, "attacker shield ground kb inputs have incompatible shapes");
    return NULL;
  }
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  npy_intp dims[2] = {n, 4};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* attack_data = (const uint16_t*)PyArray_DATA(attack_id);
  const float* anim_data = (const float*)PyArray_DATA(anim_frame);
  const uint32_t* anim_idx_data = (const uint32_t*)PyArray_DATA(animation_index);
  const uint8_t* ground_data = (const uint8_t*)PyArray_DATA(on_ground);
  const uint16_t* hitlag_data = (const uint16_t*)PyArray_DATA(hitlag);
  const float* pos_x_data = (const float*)PyArray_DATA(pos_x);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const float* ground_mul_data = (const float*)PyArray_DATA(ground_friction_mul);
  const float* light_data = (const float*)PyArray_DATA(lightshield);
  const uint8_t* guard_damage_data = (const uint8_t*)PyArray_DATA(guard_damage);
  const uint8_t* stale_queue_data = (const uint8_t*)PyArray_DATA(stale_queue);
  const uint16_t* stale_move_data = (const uint16_t*)PyArray_DATA(stale_move_id);
  const uint16_t* active_damage = (const uint16_t*)PyArray_DATA(active_damage_lut);
  const float* char_friction = (const float*)PyArray_DATA(char_friction_lut);
  const float* weights = (const float*)PyArray_DATA(stale_weights);
  float* o = (float*)PyArray_DATA(out);
  const npy_intp active_anim_cap = PyArray_DIM(active_damage_lut, 1);
  const npy_intp active_frame_cap = PyArray_DIM(active_damage_lut, 2);
  for (int attacker = 0; attacker < players; attacker++) {
    float kb = 0.0f;
    for (npy_intp i = 0; i < n; i++) {
      const npy_intp idx = (i * width) + attacker;
      if (ground_data[idx] == 0u) {
        kb = 0.0f;
        o[(i * 4) + attacker] = kb;
        continue;
      }
      bool has_onset = false;
      float onset_kb = 0.0f;
      if (hitlag_data[idx] > 0u) {
        for (int defender = 0; defender < players; defender++) {
          if (defender == attacker) continue;
          const npy_intp def_idx = (i * width) + defender;
          if (hitlag_data[def_idx] == 0u) continue;
          const uint16_t defender_action = action_data[def_idx];
          if (defender_action != (uint16_t)act_guard_set_off &&
              defender_action != (uint16_t)act_guard_reflect) {
            continue;
          }
          const uint16_t prev_attacker_hitlag =
              i > 0 ? hitlag_data[((i - 1) * width) + attacker] : 0u;
          const uint16_t prev_defender_hitlag =
              i > 0 ? hitlag_data[((i - 1) * width) + defender] : 0u;
          const uint8_t prev_defender_dmg =
              i > 0 ? guard_damage_data[((i - 1) * width) + defender] : 0u;
          if (prev_attacker_hitlag != 0u && prev_defender_hitlag != 0u && prev_defender_dmg > 0u) {
            continue;
          }
          int int_dmg = 0;
          const uint8_t cid = char_data[idx];
          const uint32_t anim_idx = anim_idx_data[idx];
          int anim_frame_i = (int)floorf(anim_data[idx]);
          if (anim_frame_i < 0) anim_frame_i = 0;
          if ((npy_intp)anim_idx < active_anim_cap && (npy_intp)anim_frame_i < active_frame_cap) {
            int_dmg = (int)
                active_damage[(((npy_intp)cid * active_anim_cap) + anim_idx) * active_frame_cap +
                              anim_frame_i];
          }
          if (int_dmg > 0) {
            const uint16_t move_id = attack_data[idx];
            const uint16_t* queue =
                &stale_move_data[((i * width + attacker) * PyArray_DIM(stale_move_id, 2))];
            const float stale_mult = msl_py_stale_multiplier_from_queue(
                stale_queue_data[idx], queue, PyArray_DIM(stale_move_id, 2), move_id, weights,
                PyArray_SIZE(stale_weights));
            int_dmg = msl_py_env_dmg_from_float((float)int_dmg * stale_mult);
          }
          if (int_dmg <= 0) int_dmg = (int)guard_damage_data[def_idx];
          if (int_dmg <= 0) continue;
          const float eval_kb =
              light_data[def_idx] * (float)int_dmg * (float)shield_kb_mul + (float)shield_kb_base;
          onset_kb = pos_x_data[def_idx] > pos_x_data[idx] ? -eval_kb : eval_kb;
          has_onset = true;
          break;
        }
      }
      if (has_onset) kb = onset_kb;
      o[(i * 4) + attacker] = kb;
      if (hitlag_data[idx] > 1u || kb == 0.0f) continue;
      const float friction =
          ground_mul_data[idx] * char_friction[char_data[idx]] * (float)shield_kb_friction_mul;
      if (friction <= 0.0f || fabsf(friction) >= fabsf(kb)) {
        kb = 0.0f;
      } else if (kb < 0.0f) {
        kb += friction;
      } else {
        kb -= friction;
      }
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_guardsetoff_frame_speed_overrides_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* state_age_obj = NULL;
  PyObject* animation_index_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* shield_obj = NULL;
  PyObject* lightshield_obj = NULL;
  PyObject* attack_id_obj = NULL;
  PyObject* stale_queue_obj = NULL;
  PyObject* stale_move_id_obj = NULL;
  PyObject* active_damage_lut_obj = NULL;
  PyObject* end_frame_lut_obj = NULL;
  PyObject* stale_weights_obj = NULL;
  int num_players = 0;
  int act_guard_set_off = 0;
  double shield_hit_mul = 0.0;
  double shield_hit_base = 0.0;
  double shield_hit_ls_min = 0.0;
  double shield_hit_ls_max = 0.0;
  double shield_stun_mul = 0.0;
  double shield_stun_base = 0.0;
  double shield_stun_ls_min = 0.0;
  double shield_stun_ls_max = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOOOiidddddddd", &action_obj, &hitlag_obj, &state_age_obj,
                        &animation_index_obj, &char_obj, &flags_obj, &shield_obj, &lightshield_obj,
                        &attack_id_obj, &stale_queue_obj, &stale_move_id_obj,
                        &active_damage_lut_obj, &end_frame_lut_obj, &stale_weights_obj,
                        &num_players, &act_guard_set_off, &shield_hit_mul, &shield_hit_base,
                        &shield_hit_ls_min, &shield_hit_ls_max, &shield_stun_mul, &shield_stun_base,
                        &shield_stun_ls_min, &shield_stun_ls_max)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* state_age = require_contiguous_array(state_age_obj, NPY_INT16, 2, "state_age_i16");
  PyArrayObject* animation_index =
      require_contiguous_array(animation_index_obj, NPY_UINT32, 2, "animation_index_u32");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 3, "state_flags_u8");
  PyArrayObject* shield = require_contiguous_array(shield_obj, NPY_FLOAT32, 2, "shield_f32");
  PyArrayObject* lightshield =
      require_contiguous_array(lightshield_obj, NPY_FLOAT32, 2, "lightshield_amount");
  PyArrayObject* attack_id =
      require_contiguous_array(attack_id_obj, NPY_UINT16, 2, "attack_id_u16");
  PyArrayObject* stale_queue =
      require_contiguous_array(stale_queue_obj, NPY_UINT8, 2, "stale_queue_index");
  PyArrayObject* stale_move_id =
      require_contiguous_array(stale_move_id_obj, NPY_UINT16, 3, "stale_move_id");
  PyArrayObject* active_damage_lut =
      require_contiguous_array(active_damage_lut_obj, NPY_UINT16, 3, "active_damage_lut");
  PyArrayObject* end_frame_lut =
      require_contiguous_array(end_frame_lut_obj, NPY_FLOAT32, 2, "end_frame_lut");
  PyArrayObject* stale_weights =
      require_contiguous_array(stale_weights_obj, NPY_FLOAT32, 1, "stale_weights");
  if (action == NULL || hitlag == NULL || state_age == NULL || animation_index == NULL ||
      chr == NULL || flags == NULL || shield == NULL || lightshield == NULL || attack_id == NULL ||
      stale_queue == NULL || stale_move_id == NULL || active_damage_lut == NULL ||
      end_frame_lut == NULL || stale_weights == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0 ||
      require_exact_2d_shape(state_age, n, width, "state_age_i16") < 0 ||
      require_exact_2d_shape(animation_index, n, width, "animation_index_u32") < 0 ||
      require_exact_2d_shape(chr, n, width, "char_id_u8") < 0 ||
      require_exact_2d_shape(shield, n, width, "shield_f32") < 0 ||
      require_exact_2d_shape(lightshield, n, width, "lightshield_amount") < 0 ||
      PyArray_NDIM(attack_id) != 2 || PyArray_DIM(attack_id, 0) != n ||
      PyArray_NDIM(stale_queue) != 2 || PyArray_DIM(stale_queue, 0) != n ||
      PyArray_NDIM(flags) != 3 || PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) != width ||
      PyArray_DIM(flags, 2) < 4 || PyArray_NDIM(stale_move_id) != 3 ||
      PyArray_DIM(stale_move_id, 0) != n ||
      PyArray_DIM(stale_move_id, 1) != PyArray_DIM(attack_id, 1) ||
      PyArray_DIM(stale_queue, 1) != PyArray_DIM(attack_id, 1) ||
      PyArray_DIM(stale_move_id, 2) < 10 || PyArray_NDIM(active_damage_lut) != 3 ||
      PyArray_DIM(active_damage_lut, 0) < 256 || PyArray_NDIM(end_frame_lut) != 2 ||
      PyArray_DIM(end_frame_lut, 0) < 256 || PyArray_DIM(end_frame_lut, 1) <= 40 ||
      PyArray_SIZE(stale_weights) < 9) {
    PyErr_SetString(PyExc_ValueError, "GuardSetOff frame-speed inputs have incompatible shapes");
    return NULL;
  }
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  npy_intp dims[2] = {n, 4};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hitlag_data = (const uint16_t*)PyArray_DATA(hitlag);
  const int16_t* age_data = (const int16_t*)PyArray_DATA(state_age);
  const uint32_t* anim_idx_data = (const uint32_t*)PyArray_DATA(animation_index);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const uint8_t* flags_data = (const uint8_t*)PyArray_DATA(flags);
  const float* shield_data = (const float*)PyArray_DATA(shield);
  const float* light_data = (const float*)PyArray_DATA(lightshield);
  const uint16_t* attack_data = (const uint16_t*)PyArray_DATA(attack_id);
  const uint8_t* stale_queue_data = (const uint8_t*)PyArray_DATA(stale_queue);
  const uint16_t* stale_move_data = (const uint16_t*)PyArray_DATA(stale_move_id);
  const uint16_t* active_damage = (const uint16_t*)PyArray_DATA(active_damage_lut);
  const float* end_frames = (const float*)PyArray_DATA(end_frame_lut);
  const float* weights = (const float*)PyArray_DATA(stale_weights);
  float* o = (float*)PyArray_DATA(out);
  const npy_intp active_anim_cap = PyArray_DIM(active_damage_lut, 1);
  const npy_intp active_frame_cap = PyArray_DIM(active_damage_lut, 2);
  const npy_intp end_width = PyArray_DIM(end_frame_lut, 1);
  const npy_intp flags_depth = PyArray_DIM(flags, 2);
  const npy_intp hist_width = PyArray_DIM(attack_id, 1);
  if (hist_width < players) {
    PyErr_SetString(PyExc_ValueError, "GuardSetOff history arrays are narrower than num_players");
    Py_DECREF(out);
    return NULL;
  }
  for (int defender = 0; defender < players; defender++) {
    float carry_rate = 0.0f;
    for (npy_intp i = 0; i < n; i++) {
      const npy_intp def_idx = (i * width) + defender;
      if (action_data[def_idx] != (uint16_t)act_guard_set_off) {
        carry_rate = 0.0f;
        continue;
      }
      const uint16_t cur_hl = hitlag_data[def_idx];
      const uint16_t prev_action = i > 0 ? action_data[((i - 1) * width) + defender] : 0xFFFFu;
      const uint16_t prev_hl = i > 0 ? hitlag_data[((i - 1) * width) + defender] : 0u;
      const int prev_af = i > 0 ? (int)age_data[((i - 1) * width) + defender] : 0;
      const int cur_af = (int)age_data[def_idx];
      const bool segment_entry = i == 0 || prev_action != (uint16_t)act_guard_set_off ||
                                 cur_hl > prev_hl || cur_af < prev_af;
      if (segment_entry && cur_hl > 0u) {
        int best = 0;
        for (int attacker = 0; attacker < players; attacker++) {
          if (attacker == defender) continue;
          const npy_intp atk_idx = (i * width) + attacker;
          if (hitlag_data[atk_idx] == 0u) continue;
          const uint8_t cid = char_data[atk_idx];
          const uint32_t anim_idx = anim_idx_data[atk_idx];
          int frame = (int)age_data[atk_idx];
          if (frame < 0) frame = 0;
          int dmg = 0;
          if ((npy_intp)anim_idx < active_anim_cap && (npy_intp)frame < active_frame_cap) {
            dmg = (int)
                active_damage[(((npy_intp)cid * active_anim_cap) + anim_idx) * active_frame_cap +
                              frame];
          }
          if (dmg <= 0) continue;
          const npy_intp hist_idx = (i * hist_width) + attacker;
          const uint16_t move_id = attack_data[hist_idx];
          const uint16_t* queue =
              &stale_move_data[((i * hist_width + attacker) * PyArray_DIM(stale_move_id, 2))];
          const float stale_mult = msl_py_stale_multiplier_from_queue(
              stale_queue_data[hist_idx], queue, PyArray_DIM(stale_move_id, 2), move_id, weights,
              PyArray_SIZE(stale_weights));
          dmg = msl_py_env_dmg_from_float((float)dmg * stale_mult);
          if (dmg > best) best = dmg;
        }
        if (best > 0) {
          float hidden_light = light_data[def_idx];
          bool inferred_light_ok = hidden_light > 0.0f;
          if (!inferred_light_ok && i > 0 && shield_hit_mul > 0.0) {
            const float shield_drop =
                shield_data[((i - 1) * width) + defender] - shield_data[def_idx];
            const float hit_den = (float)shield_hit_mul * (float)best;
            if (shield_drop > 0.0f && hit_den > 0.0f && shield_hit_ls_max != shield_hit_ls_min) {
              const float hit_light_term =
                  1.0f - ((shield_drop - (float)shield_hit_base) / hit_den);
              if (isfinite(hit_light_term)) {
                const float inferred = (hit_light_term - (float)shield_hit_ls_min) /
                                       ((float)shield_hit_ls_max - (float)shield_hit_ls_min);
                if (inferred >= -0.001f && inferred <= 1.001f) {
                  hidden_light = fminf(fmaxf(inferred, 0.0f), 1.0f);
                  inferred_light_ok = true;
                }
              }
            }
          } else {
            hidden_light = fminf(fmaxf(hidden_light, 0.0f), 1.0f);
          }
          const bool powershield_active = (flags_data[(def_idx * flags_depth) + 3] & 0x20u) != 0u;
          if (!(powershield_active || inferred_light_ok)) {
            carry_rate = 0.0f;
            continue;
          }
          const float stun_light_term =
              hidden_light * ((float)shield_stun_ls_max - (float)shield_stun_ls_min) +
              (float)shield_stun_ls_min;
          const float stun_frames =
              (float)shield_stun_mul * ((float)best * (1.0f - stun_light_term)) +
              (float)shield_stun_base;
          const uint8_t def_cid = char_data[def_idx];
          const float end_frame = end_frames[((npy_intp)def_cid * end_width) + 40];
          if (end_frame > 0.0f && stun_frames > 0.0f) {
            carry_rate = ((float)((float)end_frame + 0.1f)) / stun_frames;
          }
        }
      }
      if (cur_hl > 0u && carry_rate > 0.0f) {
        o[(i * 4) + defender] = carry_rate;
      }
    }
  }
  return (PyObject*)out;
}

static inline uint8_t msl_py_lut_u8(const uint8_t* lut, npy_intp lut_n, uint16_t key) {
  return (npy_intp)key < lut_n ? lut[key] : 0u;
}

static int msl_py_invert_hitlag_min_damage(int hitlag_frames, float slope, float base) {
  if (hitlag_frames <= 0) return 0;
  for (int dmg = 1; dmg < 0xFF; dmg++) {
    if ((int)(((float)dmg * slope) + base) >= hitlag_frames) return dmg;
  }
  return 0xFF;
}

static int msl_py_infer_shield_damage_taken(float shield_now, float shield_next, float light,
                                            bool guard_now, float shield_hit_mul,
                                            float shield_hit_base, float shield_hit_ls_min,
                                            float shield_hit_ls_max, float hold_drain_mul,
                                            float hold_drain_base, float hold_drain_max) {
  if (!(shield_hit_mul > 0.0f)) return 0;
  float shield_drop = shield_now - shield_next;
  if (!(shield_drop > 0.0f)) return 0;
  if (light < 0.0f) light = 0.0f;
  if (light > 1.0f) light = 1.0f;
  if (guard_now) {
    const float drain_factor = light * (hold_drain_max - hold_drain_base) + hold_drain_base;
    shield_drop -= hold_drain_mul * drain_factor;
  }
  const float light_term = light * (shield_hit_ls_max - shield_hit_ls_min) + shield_hit_ls_min;
  const float denom = shield_hit_mul * (1.0f - light_term);
  if (!(denom > 0.0f)) return 0;
  const float raw = (shield_drop - shield_hit_base) / denom;
  if (!isfinite(raw)) return 0;
  int dmg = (int)nearbyintf(raw);
  if (dmg <= 0) return 0;
  if (dmg > 0xFF) dmg = 0xFF;
  const float predicted = shield_hit_mul * ((float)dmg * (1.0f - light_term)) + shield_hit_base;
  if (fabsf(predicted - shield_drop) > 0.35f) return 0;
  return dmg;
}

PyObject* msl_derive_shield_contact_seed_bridge_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* shield_contact_obj = NULL;
  PyObject* hitlist_valid_obj = NULL;
  PyObject* hitlist_cd_obj = NULL;
  PyObject* hitlist_iid_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* instance_id_obj = NULL;
  PyObject* shield_obj = NULL;
  PyObject* lightshield_obj = NULL;
  PyObject* animation_index_obj = NULL;
  PyObject* state_age_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* attack_id_obj = NULL;
  PyObject* stale_queue_obj = NULL;
  PyObject* stale_move_id_obj = NULL;
  PyObject* active_shield_hit_lut_obj = NULL;
  PyObject* stale_weights_obj = NULL;
  PyObject* guard_lut_obj = NULL;
  PyObject* attack_lut_obj = NULL;
  PyObject* same_frame_lut_obj = NULL;
  PyObject* same_frame_special_lut_obj = NULL;
  int num_players = 0;
  int act_guard_set_off = 0;
  double hitlag_dmg_mul = 0.0;
  double hitlag_base = 0.0;
  double shield_hit_mul = 0.0;
  double shield_hit_base = 0.0;
  double shield_hit_ls_min = 0.0;
  double shield_hit_ls_max = 0.0;
  double hold_drain_mul = 0.0;
  double hold_drain_base = 0.0;
  double hold_drain_max = 0.0;
  if (!PyArg_ParseTuple(
          args, "OOOOOOOOOOOOOOOOOOOOOiiddddddddd", &shield_contact_obj, &hitlist_valid_obj,
          &hitlist_cd_obj, &hitlist_iid_obj, &action_obj, &hitlag_obj, &instance_id_obj,
          &shield_obj, &lightshield_obj, &animation_index_obj, &state_age_obj, &char_obj,
          &attack_id_obj, &stale_queue_obj, &stale_move_id_obj, &active_shield_hit_lut_obj,
          &stale_weights_obj, &guard_lut_obj, &attack_lut_obj, &same_frame_lut_obj,
          &same_frame_special_lut_obj, &num_players, &act_guard_set_off, &hitlag_dmg_mul,
          &hitlag_base, &shield_hit_mul, &shield_hit_base, &shield_hit_ls_min, &shield_hit_ls_max,
          &hold_drain_mul, &hold_drain_base, &hold_drain_max)) {
    return NULL;
  }
  PyArrayObject* shield_contact =
      require_contiguous_array(shield_contact_obj, NPY_UINT8, 4, "shield_contact_hb_kind");
  PyArrayObject* hitlist_valid =
      require_contiguous_array(hitlist_valid_obj, NPY_UINT8, 3, "hitlist_hb_valid");
  PyArrayObject* hitlist_cd =
      require_contiguous_array(hitlist_cd_obj, NPY_UINT16, 4, "hitlist_hb_cd");
  PyArrayObject* hitlist_iid =
      require_contiguous_array(hitlist_iid_obj, NPY_UINT16, 4, "hitlist_hb_iid");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* instance_id =
      require_contiguous_array(instance_id_obj, NPY_UINT16, 2, "instance_id_u16");
  PyArrayObject* shield = require_contiguous_array(shield_obj, NPY_FLOAT32, 2, "shield_f32");
  PyArrayObject* lightshield =
      require_contiguous_array(lightshield_obj, NPY_FLOAT32, 2, "lightshield_amount");
  PyArrayObject* animation_index =
      require_contiguous_array(animation_index_obj, NPY_UINT32, 2, "animation_index_u32");
  PyArrayObject* state_age = require_contiguous_array(state_age_obj, NPY_INT16, 2, "state_age_i16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* attack_id =
      require_contiguous_array(attack_id_obj, NPY_UINT16, 2, "attack_id_u16");
  PyArrayObject* stale_queue =
      require_contiguous_array(stale_queue_obj, NPY_UINT8, 2, "stale_queue_index");
  PyArrayObject* stale_move_id =
      require_contiguous_array(stale_move_id_obj, NPY_UINT16, 3, "stale_move_id");
  PyArrayObject* active_shield_hit_lut =
      require_contiguous_array(active_shield_hit_lut_obj, NPY_UINT16, 3, "active_shield_hit_lut");
  PyArrayObject* stale_weights =
      require_contiguous_array(stale_weights_obj, NPY_FLOAT32, 1, "stale_weights");
  PyArrayObject* guard_lut = require_contiguous_array(guard_lut_obj, NPY_UINT8, 1, "guard_lut");
  PyArrayObject* attack_lut = require_contiguous_array(attack_lut_obj, NPY_UINT8, 1, "attack_lut");
  PyArrayObject* same_frame_lut =
      require_contiguous_array(same_frame_lut_obj, NPY_UINT8, 1, "same_frame_lut");
  PyArrayObject* same_frame_special_lut =
      require_contiguous_array(same_frame_special_lut_obj, NPY_UINT8, 1, "same_frame_special_lut");
  if (shield_contact == NULL || hitlist_valid == NULL || hitlist_cd == NULL ||
      hitlist_iid == NULL || action == NULL || hitlag == NULL || instance_id == NULL ||
      shield == NULL || lightshield == NULL || animation_index == NULL || state_age == NULL ||
      chr == NULL || attack_id == NULL || stale_queue == NULL || stale_move_id == NULL ||
      active_shield_hit_lut == NULL || stale_weights == NULL || guard_lut == NULL ||
      attack_lut == NULL || same_frame_lut == NULL || same_frame_special_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0 ||
      require_exact_2d_shape(instance_id, n, width, "instance_id_u16") < 0 ||
      require_exact_2d_shape(shield, n, width, "shield_f32") < 0 ||
      require_exact_2d_shape(lightshield, n, width, "lightshield_amount") < 0 ||
      require_exact_2d_shape(animation_index, n, width, "animation_index_u32") < 0 ||
      require_exact_2d_shape(state_age, n, width, "state_age_i16") < 0 ||
      require_exact_2d_shape(chr, n, width, "char_id_u8") < 0 ||
      PyArray_NDIM(shield_contact) != 4 || PyArray_DIM(shield_contact, 0) != n ||
      PyArray_DIM(shield_contact, 1) < width || PyArray_DIM(shield_contact, 3) < width ||
      PyArray_NDIM(hitlist_valid) != 3 || PyArray_DIM(hitlist_valid, 0) != n ||
      PyArray_DIM(hitlist_valid, 1) < width ||
      PyArray_DIM(hitlist_valid, 2) != PyArray_DIM(shield_contact, 2) ||
      PyArray_NDIM(hitlist_cd) != 4 || PyArray_DIM(hitlist_cd, 0) != n ||
      PyArray_DIM(hitlist_cd, 1) < width ||
      PyArray_DIM(hitlist_cd, 2) != PyArray_DIM(shield_contact, 2) ||
      PyArray_DIM(hitlist_cd, 3) < width || PyArray_NDIM(hitlist_iid) != 4 ||
      PyArray_DIM(hitlist_iid, 0) != n || PyArray_DIM(hitlist_iid, 1) < width ||
      PyArray_DIM(hitlist_iid, 2) != PyArray_DIM(shield_contact, 2) ||
      PyArray_DIM(hitlist_iid, 3) < width || PyArray_NDIM(attack_id) != 2 ||
      PyArray_DIM(attack_id, 0) != n || PyArray_NDIM(stale_queue) != 2 ||
      PyArray_DIM(stale_queue, 0) != n || PyArray_NDIM(stale_move_id) != 3 ||
      PyArray_DIM(stale_move_id, 0) != n ||
      PyArray_DIM(stale_move_id, 1) != PyArray_DIM(attack_id, 1) ||
      PyArray_DIM(stale_queue, 1) != PyArray_DIM(attack_id, 1) ||
      PyArray_DIM(stale_move_id, 2) < 10 || PyArray_NDIM(active_shield_hit_lut) != 3 ||
      PyArray_DIM(active_shield_hit_lut, 0) < 256 || PyArray_SIZE(stale_weights) < 9 ||
      PyArray_SIZE(guard_lut) < 65536 || PyArray_SIZE(attack_lut) < 65536 ||
      PyArray_SIZE(same_frame_lut) < 65536 || PyArray_SIZE(same_frame_special_lut) < 65536) {
    PyErr_SetString(PyExc_ValueError, "shield contact seed bridge inputs have incompatible shapes");
    return NULL;
  }
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  if (PyArray_DIM(attack_id, 1) < players) {
    PyErr_SetString(PyExc_ValueError, "shield contact history arrays are narrower than players");
    return NULL;
  }
  npy_intp out_dims[2] = {n, 4};
  PyArrayObject* out_hit_damage = (PyArrayObject*)PyArray_ZEROS(2, out_dims, NPY_UINT8, 0);
  PyArrayObject* out_shield_taken = (PyArrayObject*)PyArray_ZEROS(2, out_dims, NPY_UINT8, 0);
  if (out_hit_damage == NULL || out_shield_taken == NULL) {
    Py_XDECREF(out_hit_damage);
    Py_XDECREF(out_shield_taken);
    return NULL;
  }
  uint8_t* contact = (uint8_t*)PyArray_DATA(shield_contact);
  uint8_t* valid = (uint8_t*)PyArray_DATA(hitlist_valid);
  uint16_t* cd = (uint16_t*)PyArray_DATA(hitlist_cd);
  uint16_t* iid_out = (uint16_t*)PyArray_DATA(hitlist_iid);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hitlag_data = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* iid = (const uint16_t*)PyArray_DATA(instance_id);
  const float* shield_data = (const float*)PyArray_DATA(shield);
  const float* light_data = (const float*)PyArray_DATA(lightshield);
  const uint32_t* anim_idx_data = (const uint32_t*)PyArray_DATA(animation_index);
  const int16_t* age_data = (const int16_t*)PyArray_DATA(state_age);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* attack_data = (const uint16_t*)PyArray_DATA(attack_id);
  const uint8_t* stale_queue_data = (const uint8_t*)PyArray_DATA(stale_queue);
  const uint16_t* stale_move_data = (const uint16_t*)PyArray_DATA(stale_move_id);
  const uint16_t* active_lut = (const uint16_t*)PyArray_DATA(active_shield_hit_lut);
  const float* weights = (const float*)PyArray_DATA(stale_weights);
  const uint8_t* guard = (const uint8_t*)PyArray_DATA(guard_lut);
  const uint8_t* attack = (const uint8_t*)PyArray_DATA(attack_lut);
  const uint8_t* same_frame = (const uint8_t*)PyArray_DATA(same_frame_lut);
  const uint8_t* same_frame_special = (const uint8_t*)PyArray_DATA(same_frame_special_lut);
  uint8_t* hit_damage = (uint8_t*)PyArray_DATA(out_hit_damage);
  uint8_t* shield_taken = (uint8_t*)PyArray_DATA(out_shield_taken);
  const npy_intp hb_count = PyArray_DIM(shield_contact, 2);
  const npy_intp contact_w = PyArray_DIM(shield_contact, 1);
  const npy_intp contact_dw = PyArray_DIM(shield_contact, 3);
  const npy_intp hv_w = PyArray_DIM(hitlist_valid, 1);
  const npy_intp hcd_w = PyArray_DIM(hitlist_cd, 1);
  const npy_intp hcd_dw = PyArray_DIM(hitlist_cd, 3);
  const npy_intp hii_w = PyArray_DIM(hitlist_iid, 1);
  const npy_intp hii_dw = PyArray_DIM(hitlist_iid, 3);
  const npy_intp hist_width = PyArray_DIM(attack_id, 1);
  const npy_intp stale_depth = PyArray_DIM(stale_move_id, 2);
  const npy_intp active_anim_cap = PyArray_DIM(active_shield_hit_lut, 1);
  const npy_intp active_frame_cap = PyArray_DIM(active_shield_hit_lut, 2);
  const npy_intp guard_n = PyArray_SIZE(guard_lut);
  const npy_intp attack_n = PyArray_SIZE(attack_lut);
  const npy_intp same_frame_n = PyArray_SIZE(same_frame_lut);
  const npy_intp same_frame_special_n = PyArray_SIZE(same_frame_special_lut);

#define MSL_CONTACT_IDX(frame, attacker, hb, defender) \
  ((((frame) * contact_w + (attacker)) * hb_count + (hb)) * contact_dw + (defender))
#define MSL_VALID_IDX(frame, attacker, hb) (((frame) * hv_w + (attacker)) * hb_count + (hb))
#define MSL_CD_IDX(frame, attacker, hb, defender) \
  ((((frame) * hcd_w + (attacker)) * hb_count + (hb)) * hcd_dw + (defender))
#define MSL_IID_IDX(frame, attacker, hb, defender) \
  ((((frame) * hii_w + (attacker)) * hb_count + (hb)) * hii_dw + (defender))

  for (npy_intp i = 0; i + 1 < n; i++) {
    for (int defender = 0; defender < players; defender++) {
      const npy_intp def_idx = i * width + defender;
      const npy_intp def_next_idx = (i + 1) * width + defender;
      const bool defender_guard_now = msl_py_lut_u8(guard, guard_n, action_data[def_idx]) != 0u;
      const bool defender_guard_next =
          msl_py_lut_u8(guard, guard_n, action_data[def_next_idx]) != 0u;
      if (!(defender_guard_now || defender_guard_next)) continue;
      if (hitlag_data[def_idx] != 0u) continue;
      for (int attacker = 0; attacker < players; attacker++) {
        if (attacker == defender) continue;
        const npy_intp atk_idx = i * width + attacker;
        const npy_intp atk_next_idx = (i + 1) * width + attacker;
        const bool owner = msl_py_lut_u8(attack, attack_n, action_data[atk_idx]) != 0u ||
                           msl_py_lut_u8(same_frame, same_frame_n, action_data[atk_next_idx]) != 0u;
        if (!owner) continue;
        if (hitlag_data[atk_idx] != 0u) continue;
        if (action_data[def_next_idx] == (uint16_t)act_guard_set_off &&
            hitlag_data[def_next_idx] > 0u && hitlag_data[atk_next_idx] > 0u) {
          for (npy_intp hb = 0; hb < hb_count; hb++) {
            contact[MSL_CONTACT_IDX(i, attacker, hb, defender)] = 2u;
          }
          const int hitlag_int_dmg = msl_py_invert_hitlag_min_damage(
              (int)hitlag_data[def_next_idx], (float)hitlag_dmg_mul, (float)hitlag_base);
          int active_int_dmg = 0;
          for (int other = 0; other < players; other++) {
            if (other == defender || hitlag_data[((i + 1) * width) + other] == 0u) continue;
            const npy_intp other_idx = ((i + 1) * width) + other;
            const uint8_t cid = char_data[other_idx];
            const uint32_t anim_idx = anim_idx_data[other_idx];
            int frame = (int)age_data[other_idx];
            if (frame < 0) frame = 0;
            int int_dmg = 0;
            if ((npy_intp)anim_idx < active_anim_cap && (npy_intp)frame < active_frame_cap) {
              int_dmg = (int)
                  active_lut[(((npy_intp)cid * active_anim_cap) + anim_idx) * active_frame_cap +
                             frame];
            }
            if (int_dmg <= 0) continue;
            const npy_intp hist_idx = ((i + 1) * hist_width) + other;
            const uint16_t move_id = attack_data[hist_idx];
            const uint16_t* queue =
                &stale_move_data[(((i + 1) * hist_width) + other) * stale_depth];
            const float stale_mult =
                msl_py_stale_multiplier_from_queue(stale_queue_data[hist_idx], queue, stale_depth,
                                                   move_id, weights, PyArray_SIZE(stale_weights));
            int_dmg = msl_py_env_dmg_from_float((float)int_dmg * stale_mult);
            if (int_dmg > active_int_dmg) active_int_dmg = int_dmg;
          }
          const bool use_active_upper = msl_py_lut_u8(same_frame_special, same_frame_special_n,
                                                      action_data[atk_next_idx]) != 0u;
          if (active_int_dmg > 0 && hitlag_int_dmg > 0) {
            active_int_dmg = use_active_upper && active_int_dmg > hitlag_int_dmg ? active_int_dmg
                                                                                 : hitlag_int_dmg;
          } else if (active_int_dmg <= 0) {
            active_int_dmg = hitlag_int_dmg;
          }
          const npy_intp out_idx = i * 4 + defender;
          if (active_int_dmg > hit_damage[out_idx]) hit_damage[out_idx] = (uint8_t)active_int_dmg;
          const int taken = msl_py_infer_shield_damage_taken(
              shield_data[def_idx], shield_data[def_next_idx], light_data[def_idx],
              defender_guard_now, (float)shield_hit_mul, (float)shield_hit_base,
              (float)shield_hit_ls_min, (float)shield_hit_ls_max, (float)hold_drain_mul,
              (float)hold_drain_base, (float)hold_drain_max);
          if (taken > hit_damage[out_idx] && taken > shield_taken[out_idx]) {
            shield_taken[out_idx] = (uint8_t)taken;
          }
        } else if ((hitlag_data[def_next_idx] == 0u && hitlag_data[atk_next_idx] == 0u) ||
                   (hitlag_data[def_next_idx] > 0u && hitlag_data[atk_next_idx] > 0u &&
                    action_data[def_next_idx] != (uint16_t)act_guard_set_off)) {
          for (npy_intp hb = 0; hb < hb_count; hb++) {
            contact[MSL_CONTACT_IDX(i, attacker, hb, defender)] = 1u;
          }
        }
      }
    }
  }

  for (npy_intp i = 0; i + 1 < n; i++) {
    for (int defender = 0; defender < players; defender++) {
      for (int attacker = 0; attacker < players; attacker++) {
        if (attacker == defender) continue;
        bool has_proven = false;
        for (npy_intp hb = 0; hb < hb_count; hb++) {
          if (contact[MSL_CONTACT_IDX(i, attacker, hb, defender)] == 2u) {
            has_proven = true;
            break;
          }
        }
        if (!has_proven) continue;
        if (hitlag_data[i * width + attacker] != 0u || hitlag_data[i * width + defender] != 0u) {
          continue;
        }
        if (action_data[((i + 1) * width) + defender] != (uint16_t)act_guard_set_off) continue;
        if (hitlag_data[((i + 1) * width) + attacker] == 0u ||
            hitlag_data[((i + 1) * width) + defender] == 0u) {
          continue;
        }
        const uint16_t defender_iid = iid[((i + 1) * width) + defender];
        const uint16_t attacker_action = action_data[((i + 1) * width) + attacker];
        const uint16_t defender_action = action_data[((i + 1) * width) + defender];
        npy_intp j = i + 1;
        while (j < n && action_data[(j * width) + attacker] == attacker_action &&
               action_data[(j * width) + defender] == defender_action &&
               hitlag_data[(j * width) + attacker] > 0u &&
               hitlag_data[(j * width) + defender] > 0u &&
               iid[(j * width) + defender] == defender_iid) {
          for (npy_intp hb = 0; hb < hb_count; hb++) {
            if (contact[MSL_CONTACT_IDX(i, attacker, hb, defender)] != 2u) continue;
            valid[MSL_VALID_IDX(j, attacker, hb)] = 1u;
            cd[MSL_CD_IDX(j, attacker, hb, defender)] = 0xFFFFu;
            iid_out[MSL_IID_IDX(j, attacker, hb, defender)] = defender_iid;
          }
          j++;
        }
      }
    }
  }

#undef MSL_CONTACT_IDX
#undef MSL_VALID_IDX
#undef MSL_CD_IDX
#undef MSL_IID_IDX

  return Py_BuildValue("NN", out_hit_damage, out_shield_taken);
}

PyObject* msl_derive_rebound_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* seed_speed_obj = NULL;
  PyObject* ref_speed_obj = NULL;
  PyObject* seed_frame_speed_obj = NULL;
  PyObject* numerator_lut_obj = NULL;
  int num_players = 0;
  int act_rebound_stop = 0;
  int act_rebound = 0;
  double x0_mul = 0.0;
  double x0_base = 0.0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOiiidd", &action_obj, &hitlag_obj, &ground_obj, &char_obj,
                        &seed_speed_obj, &ref_speed_obj, &seed_frame_speed_obj, &numerator_lut_obj,
                        &num_players, &act_rebound_stop, &act_rebound, &x0_mul, &x0_base)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 2, "on_ground_u8");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* seed_speed =
      require_contiguous_array(seed_speed_obj, NPY_FLOAT32, 2, "seed_speed_ground_x");
  PyArrayObject* ref_speed =
      require_contiguous_array(ref_speed_obj, NPY_FLOAT32, 2, "ref_speed_ground_x");
  PyArrayObject* seed_frame_speed =
      require_contiguous_array(seed_frame_speed_obj, NPY_FLOAT32, 2, "seed_frame_speed");
  PyArrayObject* numerator_lut =
      require_contiguous_array(numerator_lut_obj, NPY_FLOAT32, 1, "rebound_numerator_lut");
  if (action == NULL || hitlag == NULL || ground == NULL || chr == NULL || seed_speed == NULL ||
      ref_speed == NULL || seed_frame_speed == NULL || numerator_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  const npy_intp width = PyArray_DIM(action, 1);
  if (require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0 ||
      require_exact_2d_shape(ground, n, width, "on_ground_u8") < 0 ||
      require_exact_2d_shape(chr, n, width, "char_id_u8") < 0 || PyArray_NDIM(seed_speed) != 2 ||
      PyArray_NDIM(ref_speed) != 2 || PyArray_NDIM(seed_frame_speed) != 2 ||
      PyArray_DIM(seed_speed, 0) != n - 1 || PyArray_DIM(ref_speed, 0) != n - 1 ||
      PyArray_DIM(seed_frame_speed, 0) != n - 1 || PyArray_DIM(seed_speed, 1) != width ||
      PyArray_DIM(ref_speed, 1) != width || PyArray_DIM(seed_frame_speed, 1) != width ||
      PyArray_SIZE(numerator_lut) < 256) {
    PyErr_SetString(PyExc_ValueError, "rebound seed lane inputs have incompatible shapes");
    return NULL;
  }
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  npy_intp dims[2] = {n, 4};
  PyArrayObject* out_accel = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_rate = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  if (out_accel == NULL || out_rate == NULL) {
    Py_XDECREF(out_accel);
    Py_XDECREF(out_rate);
    return NULL;
  }
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hitlag_data = (const uint16_t*)PyArray_DATA(hitlag);
  const uint8_t* ground_data = (const uint8_t*)PyArray_DATA(ground);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const float* seed_speed_data = (const float*)PyArray_DATA(seed_speed);
  const float* ref_speed_data = (const float*)PyArray_DATA(ref_speed);
  const float* frame_speed_data = (const float*)PyArray_DATA(seed_frame_speed);
  const float* numerator_data = (const float*)PyArray_DATA(numerator_lut);
  float* accel = (float*)PyArray_DATA(out_accel);
  float* rate_out = (float*)PyArray_DATA(out_rate);
  for (int p = 0; p < players; p++) {
    for (npy_intp i = 0; i < n - 1; i++) {
      const npy_intp idx = (i * width) + p;
      const npy_intp next_idx = ((i + 1) * width) + p;
      if (action_data[idx] != (uint16_t)act_rebound_stop || hitlag_data[idx] == 0u ||
          action_data[next_idx] != (uint16_t)act_rebound || hitlag_data[next_idx] != 0u ||
          ground_data[idx] == 0u || ground_data[next_idx] == 0u) {
        continue;
      }
      const float pending_xe8 = ref_speed_data[idx] - seed_speed_data[idx];
      if (!isfinite(pending_xe8) || fabsf(pending_xe8) <= 1.0e-6f || fabsf(pending_xe8) > 2.0f) {
        continue;
      }
      float pending_rate = 0.0f;
      if (i + 2 < n - 1 && action_data[((i + 2) * width) + p] == (uint16_t)act_rebound) {
        const float later_rate = frame_speed_data[((i + 2) * width) + p];
        if (isfinite(later_rate) && later_rate > 0.0f && later_rate < 20.0f) {
          pending_rate = later_rate;
        }
      }
      if (pending_rate == 0.0f && x0_mul > 0.0) {
        const float rebound_x191c = (fabsf(pending_xe8) - (float)x0_base) / (float)x0_mul;
        const float numerator = numerator_data[char_data[idx]];
        if (rebound_x191c > 0.0f && numerator > 0.0f) {
          const float rate = (numerator + 0.1f) / rebound_x191c;
          if (isfinite(rate) && rate > 0.0f && rate < 20.0f) pending_rate = rate;
        }
      }
      npy_intp j = i;
      while (j >= 0 && action_data[(j * width) + p] == (uint16_t)act_rebound_stop &&
             hitlag_data[(j * width) + p] > 0u && ground_data[(j * width) + p] != 0u) {
        accel[(j * 4) + p] = pending_xe8;
        if (pending_rate > 0.0f) rate_out[(j * 4) + p] = pending_rate;
        j--;
      }
      if (pending_rate > 0.0f) rate_out[((i + 1) * 4) + p] = pending_rate;
    }
  }
  return Py_BuildValue("NN", out_accel, out_rate);
}

PyObject* msl_derive_mpcoll_wall_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* line_id_obj = NULL;
  PyObject* kind_obj = NULL;
  PyObject* x0_obj = NULL;
  PyObject* y0_obj = NULL;
  PyObject* x1_obj = NULL;
  PyObject* y1_obj = NULL;
  int stage_id = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOiOOOOOO", &action_obj, &frame_obj, &hitlag_obj, &hitstun_obj,
                        &pos_x_obj, &pos_y_obj, &stage_id, &line_id_obj, &kind_obj, &x0_obj,
                        &y0_obj, &x1_obj, &y1_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 1, "pos_x_f32");
  PyArrayObject* pos_y = require_contiguous_array(pos_y_obj, NPY_FLOAT32, 1, "pos_y_f32");
  PyArrayObject* line_id = require_contiguous_array(line_id_obj, NPY_UINT16, 1, "segment_line_id");
  PyArrayObject* kind = require_contiguous_array(kind_obj, NPY_UINT8, 1, "segment_kind");
  PyArrayObject* x0 = require_contiguous_array(x0_obj, NPY_FLOAT32, 1, "segment_x0");
  PyArrayObject* y0 = require_contiguous_array(y0_obj, NPY_FLOAT32, 1, "segment_y0");
  PyArrayObject* x1 = require_contiguous_array(x1_obj, NPY_FLOAT32, 1, "segment_x1");
  PyArrayObject* y1 = require_contiguous_array(y1_obj, NPY_FLOAT32, 1, "segment_y1");
  if (action == NULL || frame == NULL || hitlag == NULL || hitstun == NULL || pos_x == NULL ||
      pos_y == NULL || line_id == NULL || kind == NULL || x0 == NULL || y0 == NULL || x1 == NULL ||
      y1 == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(hitlag) != n || PyArray_SIZE(hitstun) != n ||
      PyArray_SIZE(pos_x) != n || PyArray_SIZE(pos_y) != n) {
    PyErr_SetString(PyExc_ValueError, "mpcoll wall seed inputs must have equal lengths");
    return NULL;
  }
  const npy_intp seg_n = PyArray_SIZE(line_id);
  if (PyArray_SIZE(kind) != seg_n || PyArray_SIZE(x0) != seg_n || PyArray_SIZE(y0) != seg_n ||
      PyArray_SIZE(x1) != seg_n || PyArray_SIZE(y1) != seg_n) {
    PyErr_SetString(PyExc_ValueError, "mpcoll segment arrays must have equal lengths");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out_kind = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_id = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT16, 0);
  if (out_kind == NULL || out_id == NULL) {
    Py_XDECREF(out_kind);
    Py_XDECREF(out_id);
    return NULL;
  }
  uint16_t* oid = (uint16_t*)PyArray_DATA(out_id);
  for (npy_intp i = 0; i < n; i++) oid[i] = 0xFFFFu;
  if (stage_id != 32) {
    return Py_BuildValue("NN", out_kind, out_id);
  }
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const float* px = (const float*)PyArray_DATA(pos_x);
  const float* py = (const float*)PyArray_DATA(pos_y);
  const uint16_t* sid = (const uint16_t*)PyArray_DATA(line_id);
  const uint8_t* sk = (const uint8_t*)PyArray_DATA(kind);
  const float* sx0 = (const float*)PyArray_DATA(x0);
  const float* sy0 = (const float*)PyArray_DATA(y0);
  const float* sx1 = (const float*)PyArray_DATA(x1);
  const float* sy1 = (const float*)PyArray_DATA(y1);
  uint8_t* ok = (uint8_t*)PyArray_DATA(out_kind);
  for (npy_intp i = 0; i < n; i++) {
    if (a[i] != 90u || hl[i] != 0u || hs[i] == 0u || af[i] < 10) continue;
    const float x = px[i];
    const float y = py[i];
    if (!isfinite(x) || !isfinite(y)) continue;
    uint8_t best_kind = 0u;
    uint16_t best_id = 0xFFFFu;
    float best_delta = 7.0f;
    for (npy_intp sidx = 0; sidx < seg_n; sidx++) {
      if (!(sk[sidx] == 2u || sk[sidx] == 3u)) continue;
      const float wall_x =
          msl_py_segment_x_at_y(y, sx0[sidx], sy0[sidx], sx1[sidx], sy1[sidx], true);
      if (!isfinite(wall_x)) continue;
      float delta = 0.0f;
      uint8_t candidate_kind = 0u;
      if (sk[sidx] == 3u) {
        delta = wall_x - x;
        candidate_kind = 1u;
      } else {
        delta = x - wall_x;
        candidate_kind = 2u;
      }
      if (delta < 0.0f || delta > 6.0f) continue;
      if (delta < best_delta) {
        best_delta = delta;
        best_kind = candidate_kind;
        best_id = sid[sidx];
      }
    }
    if (best_kind != 0u) {
      ok[i] = best_kind;
      oid[i] = best_id;
    }
  }
  return Py_BuildValue("NN", out_kind, out_id);
}

PyObject* msl_derive_source_clear_timer_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* src_obj = NULL;
  PyObject* x9_obj = NULL;
  int init_frames = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOi", &action_obj, &char_obj, &ground_obj, &flags_obj, &src_obj,
                        &x9_obj, &init_frames)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  PyArrayObject* src = require_contiguous_array(src_obj, NPY_UINT8, 1, "last_hit_by_u8");
  PyArrayObject* x9 = require_contiguous_array(x9_obj, NPY_UINT8, 2, "x9_b1_lut");
  if (action == NULL || chr == NULL || ground == NULL || flags == NULL || src == NULL ||
      x9 == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(ground) != n || PyArray_SIZE(src) != n ||
      PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 5 || PyArray_DIM(x9, 0) < 256) {
    PyErr_SetString(PyExc_ValueError, "source clear timer inputs have incompatible shapes");
    return NULL;
  }
  const npy_intp action_cap = PyArray_DIM(x9, 1);
  npy_intp dims[1] = {n};
  PyArrayObject* out_timer = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  PyArrayObject* out_phase = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out_timer == NULL || out_phase == NULL) {
    Py_XDECREF(out_timer);
    Py_XDECREF(out_phase);
    return NULL;
  }
  if (init_frames < 0) init_frames = 0;
  if (init_frames > 255) init_frames = 255;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const uint8_t* g = (const uint8_t*)PyArray_DATA(ground);
  const uint8_t* sf = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp sf_w = PyArray_DIM(flags, 1);
  const uint8_t* source = (const uint8_t*)PyArray_DATA(src);
  const uint8_t* x9p = (const uint8_t*)PyArray_DATA(x9);
  uint8_t* timer_o = (uint8_t*)PyArray_DATA(out_timer);
  uint8_t* phase_o = (uint8_t*)PyArray_DATA(out_phase);
  int timer = -1;
  uint8_t edge_pending = 0u;
  uint8_t phase_active = 0u;
  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur_a = a[i];
    const uint8_t cur_src = source[i];
    if (i > 0 && source[i - 1] == 6u && cur_src != 6u) edge_pending = 1u;
    if (i > 0 && cur_a != a[i - 1]) {
      uint8_t x9_b1 = 0u;
      if ((npy_intp)cur_a < action_cap) x9_b1 = x9p[((npy_intp)c[i] * action_cap) + cur_a];
      if (g[i] != 0u && x9_b1 != 0u && timer < 0 && cur_src != 6u) {
        timer = init_frames;
        phase_active = edge_pending ? 1u : 0u;
      }
    }
    if (cur_src == 6u) {
      timer = -1;
      edge_pending = 0u;
      phase_active = 0u;
    }
    const bool x221f_b3 = (sf[(i * sf_w) + 4] & 0x10u) != 0u;
    if (!x221f_b3 && timer >= 0) timer--;
    if (timer >= 0) {
      timer_o[i] = (uint8_t)(timer + 1 > 255 ? 255 : timer + 1);
      phase_o[i] = phase_active ? 1u : 0u;
    } else {
      timer_o[i] = 0u;
      phase_o[i] = 0u;
      phase_active = 0u;
    }
  }
  return Py_BuildValue("NN", out_timer, out_phase);
}

PyObject* msl_derive_source_clear_grounded_damage_clear_phase_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* combo_obj = NULL;
  PyObject* timer_obj = NULL;
  PyObject* phase_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* src_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOO", &action_obj, &frame_obj, &ground_obj, &hitlag_obj,
                        &hitstun_obj, &combo_obj, &timer_obj, &phase_obj, &flags_obj, &src_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* combo = require_contiguous_array(combo_obj, NPY_UINT8, 1, "combo_count_u8");
  PyArrayObject* timer = require_contiguous_array(timer_obj, NPY_UINT8, 1, "timer_u8");
  PyArrayObject* phase = require_contiguous_array(phase_obj, NPY_UINT8, 1, "owner_phase_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  PyArrayObject* src = require_contiguous_array(src_obj, NPY_UINT8, 1, "last_hit_by_u8");
  if (action == NULL || frame == NULL || ground == NULL || hitlag == NULL || hitstun == NULL ||
      combo == NULL || timer == NULL || phase == NULL || flags == NULL || src == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(ground) != n || PyArray_SIZE(hitlag) != n ||
      PyArray_SIZE(hitstun) != n || PyArray_SIZE(combo) != n || PyArray_SIZE(timer) != n ||
      PyArray_SIZE(phase) != n || PyArray_SIZE(src) != n || PyArray_DIM(flags, 0) != n ||
      PyArray_DIM(flags, 1) < 5) {
    PyErr_SetString(PyExc_ValueError,
                    "source_clear_grounded_damage_clear_phase inputs have incompatible shapes");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint8_t* g = (const uint8_t*)PyArray_DATA(ground);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* combo_p = (const uint8_t*)PyArray_DATA(combo);
  const uint8_t* timer_p = (const uint8_t*)PyArray_DATA(timer);
  const uint8_t* phase_p = (const uint8_t*)PyArray_DATA(phase);
  const uint8_t* sf = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp sf_w = PyArray_DIM(flags, 1);
  const uint8_t* source = (const uint8_t*)PyArray_DATA(src);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  for (npy_intp i = 1; i < n; i++) {
    if (source[i] >= 6u || timer_p[i] == 0u || phase_p[i] == 0u || hl[i] != 0u || hs[i] != 0u ||
        g[i] == 0u || (sf[(i * sf_w) + 4] & 0x10u) != 0u) {
      continue;
    }
    if (a[i - 1] == 0x0010u && a[i] == 0x000Eu && af[i] == 0 && combo_p[i] == 0u &&
        timer_p[i - 1] == (uint8_t)(timer_p[i] + 1u)) {
      o[i] = 1u;
      continue;
    }
    if (a[i - 1] == 0x000Fu && a[i] == 0x0014u && af[i] == 1 && timer_p[i] == 1u &&
        timer_p[i - 1] == 2u) {
      o[i] = 1u;
    }
  }
  return (PyObject*)out;
}

static bool msl_source_clear_terminal_action_allowed(uint16_t action) {
  switch (action) {
    case 0x000Eu:  // Wait
    case 0x00B3u:  // Guard
    case 0x00B7u:  // DownBoundU
    case 0x00B8u:  // DownWaitU
    case 0x00BAu:  // DownStandU
    case 0x00BBu:  // DownAttackU
    case 0x00BCu:  // DownForwardU
    case 0x00BDu:  // DownBackU
    case 0x00BFu:  // DownBoundD
    case 0x00C0u:  // DownWaitD
    case 0x00C2u:  // DownStandD
    case 0x00C3u:  // DownAttackD
    case 0x00C4u:  // DownForwardD
    case 0x00C5u:  // DownBackD
    case 0x00C7u:  // Passive
    case 0x00C8u:  // PassiveStandF
    case 0x00C9u:  // PassiveStandB
    case 0x00E9u:  // EscapeF
    case 0x015Eu:  // Fox/Falco SpecialAirSStart
    case 0x0004u:  // DeadUpStar
    case 0x0041u:  // AttackAirN
    case 0x0045u:  // AttackAirLw
    case 0x00ECu:  // EscapeAir
    case 0x0012u:  // Turn
    case 0x0018u:  // KneeBend
    case 0x0019u:  // JumpF
    case 0x001Au:  // JumpB
    case 0x0027u:  // Squat
    case 0x0038u:  // AttackHi3
      return true;
    default:
      return false;
  }
}

static bool msl_source_clear_terminal_followup_action(uint16_t action) {
  return action == 0x0041u || action == 0x0045u || action == 0x00ECu;
}

static bool msl_source_clear_terminal_continuation_action(uint16_t action) {
  return action == 0x0012u || action == 0x0018u || action == 0x0019u || action == 0x001Au ||
         action == 0x0027u || action == 0x0038u;
}

PyObject* msl_derive_source_clear_terminal_phase_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* combo_obj = NULL;
  PyObject* last_attack_obj = NULL;
  PyObject* timer_obj = NULL;
  PyObject* owner_phase_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* src_obj = NULL;
  PyObject* cmd0_on_obj = NULL;
  PyObject* cmd0_off_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOO", &char_obj, &action_obj, &frame_obj, &hitlag_obj,
                        &hitstun_obj, &combo_obj, &last_attack_obj, &timer_obj, &owner_phase_obj,
                        &flags_obj, &src_obj, &cmd0_on_obj, &cmd0_off_obj)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 1, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* combo = require_contiguous_array(combo_obj, NPY_UINT8, 1, "combo_count_u8");
  PyArrayObject* last_attack =
      require_contiguous_array(last_attack_obj, NPY_UINT8, 1, "last_attack_landed_u8");
  PyArrayObject* timer = require_contiguous_array(timer_obj, NPY_UINT8, 1, "timer_u8");
  PyArrayObject* owner_phase =
      require_contiguous_array(owner_phase_obj, NPY_UINT8, 1, "owner_phase_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  PyArrayObject* src = require_contiguous_array(src_obj, NPY_UINT8, 1, "last_hit_by_u8");
  PyArrayObject* cmd0_on = require_contiguous_array(cmd0_on_obj, NPY_INT16, 2, "cmd0_on_lut");
  PyArrayObject* cmd0_off = require_contiguous_array(cmd0_off_obj, NPY_INT16, 2, "cmd0_off_lut");
  if (chr == NULL || action == NULL || frame == NULL || hitlag == NULL || hitstun == NULL ||
      combo == NULL || last_attack == NULL || timer == NULL || owner_phase == NULL ||
      flags == NULL || src == NULL || cmd0_on == NULL || cmd0_off == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(chr) != n || PyArray_SIZE(frame) != n || PyArray_SIZE(hitlag) != n ||
      PyArray_SIZE(hitstun) != n || PyArray_SIZE(combo) != n || PyArray_SIZE(last_attack) != n ||
      PyArray_SIZE(timer) != n || PyArray_SIZE(owner_phase) != n || PyArray_SIZE(src) != n ||
      PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 5 || PyArray_DIM(cmd0_on, 0) < 256 ||
      PyArray_DIM(cmd0_off, 0) < 256 || PyArray_DIM(cmd0_on, 1) != PyArray_DIM(cmd0_off, 1)) {
    PyErr_SetString(PyExc_ValueError,
                    "source_clear_terminal_phase inputs have incompatible shapes");
    return NULL;
  }
  const npy_intp action_cap = PyArray_DIM(cmd0_on, 1);
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;

  const uint8_t* c = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* combo_p = (const uint8_t*)PyArray_DATA(combo);
  const uint8_t* last_attack_p = (const uint8_t*)PyArray_DATA(last_attack);
  const uint8_t* timer_p = (const uint8_t*)PyArray_DATA(timer);
  const uint8_t* phase_p = (const uint8_t*)PyArray_DATA(owner_phase);
  const uint8_t* sf = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp sf_w = PyArray_DIM(flags, 1);
  const uint8_t* source = (const uint8_t*)PyArray_DATA(src);
  const int16_t* on_lut = (const int16_t*)PyArray_DATA(cmd0_on);
  const int16_t* off_lut = (const int16_t*)PyArray_DATA(cmd0_off);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);

  for (npy_intp i = 1; i < n; i++) {
    if (timer_p[i] != 1u) continue;
    const uint16_t act = a[i];
    if (!msl_source_clear_terminal_action_allowed(act)) continue;
    if (msl_source_clear_terminal_continuation_action(act) && phase_p[i] == 0u) continue;
    const int cur_af = (int)af[i];
    if (act == 0x0019u) {
      if (cur_af < 10 || cur_af > 20) continue;
    } else if (act == 0x001Au) {
      if (cur_af < 30) continue;
    } else if (act == 0x0018u) {
      if (cur_af != 1) continue;
      if ((sf[(i * sf_w) + 3] & 0x60u) == 0u) continue;
    }
    const uint8_t owner = source[i];
    if (owner >= 6u) continue;
    if (hl[i] != 0u || hs[i] != 0u) continue;
    if (combo_p[i] == 0u || last_attack_p[i] == 0u) {
      if (!(act == 0x015Eu || act == 0x0045u || act == 0x0018u || act == 0x0019u ||
            act == 0x001Au)) {
        continue;
      }
      if ((act == 0x0018u || act == 0x0019u || act == 0x001Au) && last_attack_p[i] == 0u) {
        continue;
      }
    }
    if ((sf[(i * sf_w) + 4] & 0x10u) != 0u) continue;
    if (act != 0x00B3u && !msl_source_clear_terminal_continuation_action(act) &&
        (sf[(i * sf_w) + 1] != 0u || sf[(i * sf_w) + 2] != 0u)) {
      continue;
    }
    if (timer_p[i - 1] != 2u) continue;
    if (source[i - 1] != owner) continue;
    const uint16_t prev_act = a[i - 1];
    const int prev_af = (int)af[i - 1];
    const bool same_action_progress = (act == prev_act && cur_af == prev_af + 1);
    const bool guard_hold_progress =
        (act == 0x00B3u && prev_act == 0x00B3u && cur_af == -1 && prev_af == -1);
    const bool continuation_entry_progress =
        (msl_source_clear_terminal_continuation_action(act) && cur_af == 1 && prev_af >= 0);
    if (!(same_action_progress || guard_hold_progress || continuation_entry_progress)) continue;
    if (msl_source_clear_terminal_followup_action(act) || act == 0x0038u) {
      const uint8_t cid = c[i];
      int cmd0_on = -1;
      int cmd0_off = -1;
      if ((npy_intp)act < action_cap) {
        cmd0_on = (int)on_lut[((npy_intp)cid * action_cap) + act];
        cmd0_off = (int)off_lut[((npy_intp)cid * action_cap) + act];
      }
      if (act == 0x0041u) {
        if (cmd0_on < 0 || cur_af >= cmd0_on) continue;
      } else if (act == 0x00ECu) {
        if (cmd0_on < 0 || cur_af >= cmd0_on) continue;
      } else if (act == 0x0045u) {
        if (cmd0_off < 0 || cur_af >= cmd0_off) continue;
        if (combo_p[i] != 0u) continue;
      } else if (act == 0x0038u) {
        if (cmd0_off < 0 || cur_af >= cmd0_off) continue;
      }
    }
    o[i] = 1u;
  }
  return (PyObject*)out;
}

static float msl_py_randf_after_pre_gate(uint32_t seed_in, int stream_offset_steps,
                                         int consume_count) {
  uint32_t seed = seed_in;
  const int steps = stream_offset_steps + consume_count + 1;
  for (int i = 0; i < steps; i++) {
    seed = seed * 214013u + 2531011u;
  }
  return (float)((seed >> 16) & 0xFFFFu) * (1.0f / 65536.0f);
}

static int msl_py_local_slot_from_source_port(const uint8_t* source_port0, npy_intp width,
                                              npy_intp row, int players, int source_port0_raw) {
  for (int p = 0; p < players; p++) {
    if ((int)source_port0[(row * width) + p] == source_port0_raw) return p;
  }
  return -1;
}

static int msl_py_f26_source_port_for_player(const uint8_t* last_hit_by,
                                             const uint8_t* ref_last_hit_by,
                                             const uint8_t* source_port0, npy_intp width,
                                             npy_intp row, int players, int p) {
  const int source = (int)last_hit_by[(row * width) + p];
  if (msl_py_local_slot_from_source_port(source_port0, width, row, players, source) >= 0) {
    return source;
  }
  return (int)ref_last_hit_by[(row * width) + p];
}

static int msl_py_f26_current_pre_action_marker(const uint16_t* action, const uint16_t* ref_action,
                                                const uint8_t* ground, const uint16_t* hitlag,
                                                const uint16_t* hitstun, const uint32_t* rng_seed,
                                                npy_intp width, npy_intp row, int p,
                                                int stream_offset_steps, float roll_prob) {
  const uint16_t cur = action[(row * width) + p];
  if (ground[(row * width) + p] != 0u || hitlag[(row * width) + p] != 0u ||
      hitstun[(row * width) + p] != 0u) {
    return 0;
  }
  if (!(cur == 65u || cur == 67u)) return 0;
  float rolls[4];
  for (int consume = 0; consume < 4; consume++) {
    rolls[consume] = msl_py_randf_after_pre_gate(rng_seed[row], stream_offset_steps, consume);
  }
  if (ref_action[(row * width) + p] == 91u) {
    for (int consume = 0; consume < 4; consume++) {
      if (rolls[consume] < roll_prob) return consume > 0 ? consume : 4;
    }
    return 0;
  }
  if (rolls[0] < roll_prob) {
    for (int consume = 1; consume < 4; consume++) {
      if (rolls[consume] >= roll_prob) return consume;
    }
  }
  return 0;
}

static int msl_py_f26_marker_stream_steps(int marker) {
  if (marker <= 0) return 0;
  return (marker == 4 ? 0 : marker) + 1;
}

static int msl_py_f26_prior_same_frame_stream_steps(
    const uint16_t* action, const uint16_t* ref_action, const uint8_t* ground,
    const uint16_t* hitlag, const uint16_t* hitstun, const uint8_t* last_hit_by,
    const uint8_t* ref_last_hit_by, const uint8_t* source_port0, const uint32_t* rng_seed,
    npy_intp width, npy_intp row, int players, int p, float roll_prob) {
  const int cur_source = msl_py_f26_source_port_for_player(last_hit_by, ref_last_hit_by,
                                                           source_port0, width, row, players, p);
  const int cur_attacker =
      msl_py_local_slot_from_source_port(source_port0, width, row, players, cur_source);
  if (cur_attacker < 0) return 0;
  int stream_steps = 0;
  for (int q = 0; q < players; q++) {
    if (q == p) continue;
    const int other_source = msl_py_f26_source_port_for_player(
        last_hit_by, ref_last_hit_by, source_port0, width, row, players, q);
    const int other_attacker =
        msl_py_local_slot_from_source_port(source_port0, width, row, players, other_source);
    if (other_attacker < 0 || other_attacker >= cur_attacker) continue;
    const int marker =
        msl_py_f26_current_pre_action_marker(action, ref_action, ground, hitlag, hitstun, rng_seed,
                                             width, row, q, stream_steps, roll_prob);
    stream_steps += msl_py_f26_marker_stream_steps(marker);
  }
  return stream_steps;
}

PyObject* msl_derive_fighter_8006cda4_pre_gate_count_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* frame_obj = NULL;
  PyObject* ref_action_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* last_hit_by_obj = NULL;
  PyObject* all_source_obj = NULL;
  PyObject* all_action_obj = NULL;
  PyObject* all_frame_obj = NULL;
  PyObject* all_ref_action_obj = NULL;
  PyObject* all_ground_obj = NULL;
  PyObject* all_hitlag_obj = NULL;
  PyObject* all_hitstun_obj = NULL;
  PyObject* all_last_hit_by_obj = NULL;
  PyObject* all_ref_last_hit_by_obj = NULL;
  PyObject* rng_obj = NULL;
  double roll_prob_d = 0.0;
  int victim_port = 0;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOOOOOOOdii", &action_obj, &frame_obj, &ref_action_obj,
                        &ground_obj, &hitlag_obj, &hitstun_obj, &flags_obj, &last_hit_by_obj,
                        &all_source_obj, &all_action_obj, &all_frame_obj, &all_ref_action_obj,
                        &all_ground_obj, &all_hitlag_obj, &all_hitstun_obj, &all_last_hit_by_obj,
                        &all_ref_last_hit_by_obj, &rng_obj, &roll_prob_d, &victim_port,
                        &num_players)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* frame = require_contiguous_array(frame_obj, NPY_INT16, 1, "action_frame_i16");
  PyArrayObject* ref_action =
      require_contiguous_array(ref_action_obj, NPY_UINT16, 1, "ref_action_id_u16");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 1, "on_ground_u8");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 1, "hitlag_u16");
  PyArrayObject* hitstun = require_contiguous_array(hitstun_obj, NPY_UINT16, 1, "hitstun_u16");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  PyArrayObject* last_hit_by =
      require_contiguous_array(last_hit_by_obj, NPY_UINT8, 1, "last_hit_by_u8");
  PyArrayObject* all_source =
      require_contiguous_array(all_source_obj, NPY_UINT8, 2, "all_source_port0_u8");
  PyArrayObject* all_action =
      require_contiguous_array(all_action_obj, NPY_UINT16, 2, "all_action_id_u16");
  PyArrayObject* all_frame =
      require_contiguous_array(all_frame_obj, NPY_INT16, 2, "all_action_frame_i16");
  PyArrayObject* all_ref_action =
      require_contiguous_array(all_ref_action_obj, NPY_UINT16, 2, "all_ref_action_id_u16");
  PyArrayObject* all_ground =
      require_contiguous_array(all_ground_obj, NPY_UINT8, 2, "all_on_ground_u8");
  PyArrayObject* all_hitlag =
      require_contiguous_array(all_hitlag_obj, NPY_UINT16, 2, "all_hitlag_u16");
  PyArrayObject* all_hitstun =
      require_contiguous_array(all_hitstun_obj, NPY_UINT16, 2, "all_hitstun_u16");
  PyArrayObject* all_last_hit_by =
      require_contiguous_array(all_last_hit_by_obj, NPY_UINT8, 2, "all_last_hit_by_u8");
  PyArrayObject* all_ref_last_hit_by =
      require_contiguous_array(all_ref_last_hit_by_obj, NPY_UINT8, 2, "all_ref_last_hit_by_u8");
  PyArrayObject* rng =
      require_contiguous_array(rng_obj, NPY_UINT32, 1, "frame_pre_random_seed_u32");
  if (action == NULL || frame == NULL || ref_action == NULL || ground == NULL || hitlag == NULL ||
      hitstun == NULL || flags == NULL || last_hit_by == NULL || all_source == NULL ||
      all_action == NULL || all_frame == NULL || all_ref_action == NULL || all_ground == NULL ||
      all_hitlag == NULL || all_hitstun == NULL || all_last_hit_by == NULL ||
      all_ref_last_hit_by == NULL || rng == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_SIZE(frame) != n || PyArray_SIZE(ref_action) != n || PyArray_SIZE(ground) != n ||
      PyArray_SIZE(hitlag) != n || PyArray_SIZE(hitstun) != n || PyArray_SIZE(last_hit_by) != n ||
      PyArray_SIZE(rng) != n || PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 5) {
    PyErr_SetString(PyExc_ValueError,
                    "fighter_8006cda4_pre_gate_count 1D inputs have incompatible shapes");
    return NULL;
  }
  if (num_players < 0) num_players = 0;
  if (num_players > 4) num_players = 4;
  if (victim_port < 0 || victim_port >= num_players) {
    PyErr_SetString(PyExc_ValueError, "fighter_8006cda4_pre_gate_count victim_port out of range");
    return NULL;
  }
  const npy_intp width = PyArray_DIM(all_source, 1);
  if (require_exact_2d_shape(all_action, n, width, "all_action_id_u16") < 0 ||
      require_exact_2d_shape(all_frame, n, width, "all_action_frame_i16") < 0 ||
      require_exact_2d_shape(all_ref_action, n, width, "all_ref_action_id_u16") < 0 ||
      require_exact_2d_shape(all_ground, n, width, "all_on_ground_u8") < 0 ||
      require_exact_2d_shape(all_hitlag, n, width, "all_hitlag_u16") < 0 ||
      require_exact_2d_shape(all_hitstun, n, width, "all_hitstun_u16") < 0 ||
      require_exact_2d_shape(all_last_hit_by, n, width, "all_last_hit_by_u8") < 0 ||
      require_exact_2d_shape(all_ref_last_hit_by, n, width, "all_ref_last_hit_by_u8") < 0) {
    return NULL;
  }
  if (width < num_players) {
    PyErr_SetString(PyExc_ValueError, "fighter_8006cda4_pre_gate_count all-player width too small");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) return NULL;

  const uint16_t* a = (const uint16_t*)PyArray_DATA(action);
  const int16_t* af = (const int16_t*)PyArray_DATA(frame);
  const uint16_t* ref_a = (const uint16_t*)PyArray_DATA(ref_action);
  const uint8_t* g = (const uint8_t*)PyArray_DATA(ground);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hs = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* sf = (const uint8_t*)PyArray_DATA(flags);
  const npy_intp sf_w = PyArray_DIM(flags, 1);
  const uint8_t* lhb = (const uint8_t*)PyArray_DATA(last_hit_by);
  const uint8_t* source_port = (const uint8_t*)PyArray_DATA(all_source);
  const uint16_t* aa = (const uint16_t*)PyArray_DATA(all_action);
  const int16_t* aaf = (const int16_t*)PyArray_DATA(all_frame);
  const uint16_t* ara = (const uint16_t*)PyArray_DATA(all_ref_action);
  const uint8_t* ag = (const uint8_t*)PyArray_DATA(all_ground);
  const uint16_t* ahl = (const uint16_t*)PyArray_DATA(all_hitlag);
  const uint16_t* ahs = (const uint16_t*)PyArray_DATA(all_hitstun);
  const uint8_t* alhb = (const uint8_t*)PyArray_DATA(all_last_hit_by);
  const uint8_t* arlhb = (const uint8_t*)PyArray_DATA(all_ref_last_hit_by);
  const uint32_t* seed = (const uint32_t*)PyArray_DATA(rng);
  uint8_t* o = (uint8_t*)PyArray_DATA(out);
  const float roll_prob = (float)roll_prob_d;

  for (npy_intp i = 0; i < n; i++) {
    const uint16_t cur = a[i];
    if (cur == 74u) {
      o[i] = (uint8_t)((af[i] == 0 || af[i] >= 16) ? 2 : 1);
      continue;
    }
    if (cur == 363u) {
      if (af[i] > 3) o[i] = 1u;
      continue;
    }
    if (cur == 57u) {
      o[i] = 1u;
      continue;
    }
    if (cur == 65u && g[i] == 0u && hl[i] == 0u && hs[i] == 0u) {
      if (ref_a[i] == 91u) {
        const int stream_steps = msl_py_f26_prior_same_frame_stream_steps(
            aa, ara, ag, ahl, ahs, alhb, arlhb, source_port, seed, width, i, num_players,
            victim_port, roll_prob);
        const int marker = msl_py_f26_current_pre_action_marker(
            aa, ara, ag, ahl, ahs, seed, width, i, victim_port, stream_steps, roll_prob);
        o[i] = (uint8_t)marker;
      }
      continue;
    }
    if (cur == 67u && g[i] == 0u && hl[i] == 0u && hs[i] == 0u) {
      const int stream_steps = msl_py_f26_prior_same_frame_stream_steps(
          aa, ara, ag, ahl, ahs, alhb, arlhb, source_port, seed, width, i, num_players, victim_port,
          roll_prob);
      const int marker = msl_py_f26_current_pre_action_marker(aa, ara, ag, ahl, ahs, seed, width, i,
                                                              victim_port, stream_steps, roll_prob);
      if (marker != 0) o[i] = (uint8_t)marker;
      continue;
    }
    if (cur == 239u && g[i] != 0u && hl[i] > 0u && hs[i] == 0u &&
        (sf[(i * sf_w) + 1] & 0x10u) != 0u) {
      o[i] = 2u;
      continue;
    }
    if (cur == 90u && g[i] == 0u && hl[i] == 0u && hs[i] > 0u) {
      const int attacker =
          msl_py_local_slot_from_source_port(source_port, width, i, num_players, (int)lhb[i]);
      if (attacker < 0 || attacker == victim_port) continue;
      const uint16_t attacker_action = aa[(i * width) + attacker];
      const int attacker_frame = (int)aaf[(i * width) + attacker];
      if (attacker_action == 67u && (attacker_frame == 3 || attacker_frame == 4)) {
        float rolls[4];
        for (int consume = 0; consume < 4; consume++) {
          rolls[consume] = msl_py_randf_after_pre_gate(seed[i], 0, consume);
        }
        if (ref_a[i] == 91u) {
          for (int consume = 0; consume < 4; consume++) {
            if (rolls[consume] < roll_prob) {
              o[i] = (uint8_t)(consume > 0 ? consume : 4);
              break;
            }
          }
          continue;
        }
        if (rolls[0] < roll_prob) {
          for (int consume = 1; consume < 4; consume++) {
            if (rolls[consume] >= roll_prob) {
              o[i] = (uint8_t)consume;
              break;
            }
          }
          continue;
        }
      }
      if (attacker_action == 67u && attacker_frame >= 6) {
        o[i] = 2u;
      }
    }
  }

  for (npy_intp i = 0; i < n; i++) {
    const int consume = (int)o[i];
    if (consume <= 0 || consume > 4) continue;
    if (!(a[i] == 90u && g[i] == 0u && hl[i] == 0u && hs[i] > 0u)) continue;
    const int attacker =
        msl_py_local_slot_from_source_port(source_port, width, i, num_players, (int)lhb[i]);
    if (attacker < 0 || attacker == victim_port) continue;
    if (aa[(i * width) + attacker] != 67u) continue;
    npy_intp j = i - 1;
    while (j >= 0) {
      if (a[j] != 90u) break;
      if (g[j] != 0u || hs[j] == 0u) break;
      if (msl_py_local_slot_from_source_port(source_port, width, j, num_players, (int)lhb[j]) !=
          attacker) {
        break;
      }
      if (o[j] == 0u) o[j] = (uint8_t)consume;
      j--;
    }
  }

  for (npy_intp i = 0; i < n; i++) {
    const int consume = (int)o[i];
    if (consume <= 0 || consume > 3) continue;
    if (!(a[i] == 65u && g[i] == 0u && hl[i] == 0u && hs[i] == 0u && ref_a[i] == 91u)) {
      continue;
    }
    const int source_port_raw = msl_py_f26_source_port_for_player(alhb, arlhb, source_port, width,
                                                                  i, num_players, victim_port);
    const int attacker =
        msl_py_local_slot_from_source_port(source_port, width, i, num_players, source_port_raw);
    if (attacker < 0 || attacker == victim_port) continue;
    const uint16_t attacker_action = aa[(i * width) + attacker];
    npy_intp j = i - 1;
    bool seen_damagefall_handoff = false;
    while (j >= 0) {
      const uint16_t cur = a[j];
      if (cur == 65u) {
        if (g[j] != 0u || hl[j] != 0u || hs[j] != 0u) break;
      } else if (cur == 38u) {
        if (seen_damagefall_handoff) break;
        if (g[j] != 0u || hl[j] != 0u || hs[j] != 0u) break;
        seen_damagefall_handoff = true;
      } else if (cur == 90u) {
        if (!seen_damagefall_handoff) break;
        if (g[j] != 0u || hl[j] != 0u || hs[j] <= 0u) break;
      } else {
        break;
      }
      if (msl_py_local_slot_from_source_port(source_port, width, j, num_players, source_port_raw) !=
          attacker) {
        break;
      }
      if (cur != 90u && aa[(j * width) + attacker] != attacker_action) break;
      if (o[j] == 0u) o[j] = (uint8_t)consume;
      j--;
    }
  }

  return (PyObject*)out;
}

PyObject* msl_derive_source_clear_processhit_damage_pending_phase_py(PyObject* self,
                                                                     PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* flags_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &action_obj, &flags_obj)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 1, "action_id_u16");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 2, "state_flags_u8");
  if (action == NULL || flags == NULL) return NULL;
  const npy_intp n = PyArray_SIZE(action);
  if (PyArray_DIM(flags, 0) != n || PyArray_DIM(flags, 1) < 5) {
    PyErr_SetString(PyExc_ValueError,
                    "source_clear_processhit_damage_pending_phase inputs have incompatible shapes");
    return NULL;
  }
  npy_intp dims[1] = {n};
  return PyArray_ZEROS(1, dims, NPY_UINT8, 0);
}

PyObject* msl_derive_phantom_damage_pending_seed_lanes_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* percent_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* hit_by_obj = NULL;
  PyObject* iid_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOi", &percent_obj, &hitlag_obj, &action_obj, &hit_by_obj,
                        &iid_obj, &num_players)) {
    return NULL;
  }
  PyArrayObject* percent = require_contiguous_array(percent_obj, NPY_FLOAT32, 2, "percent_f32");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT16, 2, "hitlag_u16");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hit_by =
      require_contiguous_array(hit_by_obj, NPY_UINT16, 2, "instance_hit_by_u16");
  PyArrayObject* iid = require_contiguous_array(iid_obj, NPY_UINT16, 2, "instance_id_u16");
  if (percent == NULL || hitlag == NULL || action == NULL || hit_by == NULL || iid == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(percent, 0);
  const npy_intp width = PyArray_DIM(percent, 1);
  if (require_exact_2d_shape(hitlag, n, width, "hitlag_u16") < 0 ||
      require_exact_2d_shape(action, n, width, "action_id_u16") < 0 ||
      require_exact_2d_shape(hit_by, n, width, "instance_hit_by_u16") < 0 ||
      require_exact_2d_shape(iid, n, width, "instance_id_u16") < 0) {
    return NULL;
  }
  int players = num_players;
  if (players < 0) players = 0;
  if (players > width) players = (int)width;
  if (players > 4) players = 4;
  npy_intp dims[2] = {n, 4};
  PyArrayObject* out_damage = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_timer = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  PyArrayObject* out_source = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_UINT8, 0);
  if (out_damage == NULL || out_timer == NULL || out_source == NULL) {
    Py_XDECREF(out_damage);
    Py_XDECREF(out_timer);
    Py_XDECREF(out_source);
    return NULL;
  }
  uint8_t* os = (uint8_t*)PyArray_DATA(out_source);
  for (npy_intp i = 0; i < n * 4; i++) os[i] = 0xFFu;
  const float* pct = (const float*)PyArray_DATA(percent);
  const uint16_t* hl = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* act = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hb = (const uint16_t*)PyArray_DATA(hit_by);
  const uint16_t* id = (const uint16_t*)PyArray_DATA(iid);
  float* od = (float*)PyArray_DATA(out_damage);
  uint16_t* ot = (uint16_t*)PyArray_DATA(out_timer);
  for (npy_intp i = 0; i + 1 < n; i++) {
    for (int defender = 0; defender < players; defender++) {
      const npy_intp idx = i * width + defender;
      const npy_intp next_idx = (i + 1) * width + defender;
      if (act[idx] != act[next_idx]) continue;
      const float dmg = pct[next_idx] - pct[idx];
      if (!(dmg > 0.0f) || !isfinite(dmg)) continue;
      const uint16_t source_iid = hb[idx];
      if (source_iid == 0u) continue;
      int source_slot = -1;
      for (int attacker = 0; attacker < players; attacker++) {
        if (attacker == defender) continue;
        if (id[i * width + attacker] == source_iid) {
          source_slot = attacker;
          break;
        }
      }
      if (source_slot < 0) continue;
      npy_intp start = i;
      while (start >= 0) {
        const npy_intp pidx = start * width + defender;
        if (hl[pidx] == 0u || hb[pidx] != source_iid || fabsf(pct[pidx] - pct[idx]) > 1.0e-5f) {
          break;
        }
        start--;
      }
      start++;
      if (start > i) continue;
      if (start <= 0 || hl[(start - 1) * width + defender] != 0u) continue;
      if (fabsf(pct[start * width + defender] - pct[(start - 1) * width + defender]) > 1.0e-5f) {
        continue;
      }
      for (npy_intp j = start; j <= i; j++) {
        const npy_intp timer = i - j + 1;
        if (timer > 0xFFFF) continue;
        const npy_intp oidx = j * 4 + defender;
        od[oidx] = dmg;
        ot[oidx] = (uint16_t)timer;
        os[oidx] = (uint8_t)source_slot;
      }
    }
  }
  return Py_BuildValue("NNN", out_damage, out_timer, out_source);
}

static inline bool msl_py_hidden_z_action_allows_depth(uint16_t a) {
  if (a >= 0x004Bu && a <= 0x005Bu) return false;
  if (a == 0x00F7u || a == 0x00F8u) return false;
  if (a == 0x00B5u || a == 0x00B7u || a == 0x00BFu || a == 0x00FCu || a == 0x00FDu) {
    return false;
  }
  if (a >= 0x00DBu && a <= 0x00E2u) return false;
  if (a >= 0x012Cu) return false;
  return true;
}

PyObject* msl_derive_grounded_overlap_hidden_pos_z_py(PyObject* self, PyObject* args) {
  (void)self;
  int num_players = 0;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* stocks_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_z_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* push_x_obj = NULL;
  PyObject* push_y_obj = NULL;
  double step_d = 0.0;
  double z_max_d = 0.0;
  if (!PyArg_ParseTuple(args, "iOOOOOOOOOdd", &num_players, &char_obj, &action_obj, &ground_obj,
                        &stocks_obj, &pos_x_obj, &pos_z_obj, &facing_obj, &push_x_obj, &push_y_obj,
                        &step_d, &z_max_d)) {
    return NULL;
  }
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* ground = require_contiguous_array(ground_obj, NPY_UINT8, 2, "on_ground_u8");
  PyArrayObject* stocks = require_contiguous_array(stocks_obj, NPY_UINT8, 2, "stocks_u8");
  PyArrayObject* pos_x = require_contiguous_array(pos_x_obj, NPY_FLOAT32, 2, "pos_x_f32");
  PyArrayObject* pos_z = require_contiguous_array(pos_z_obj, NPY_FLOAT32, 2, "pos_z_f32");
  PyArrayObject* facing = require_contiguous_array(facing_obj, NPY_UINT8, 2, "facing_u8");
  PyArrayObject* push_x = require_contiguous_array(push_x_obj, NPY_FLOAT32, 1, "push_x_lut");
  PyArrayObject* push_y = require_contiguous_array(push_y_obj, NPY_FLOAT32, 1, "push_y_lut");
  if (chr == NULL || action == NULL || ground == NULL || stocks == NULL || pos_x == NULL ||
      pos_z == NULL || facing == NULL || push_x == NULL || push_y == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(chr, 0);
  const npy_intp width = PyArray_DIM(chr, 1);
  if (num_players < 0 || num_players > width) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for hidden pos_z shape");
    return NULL;
  }
  if (require_exact_2d_shape(action, n, width, "action_id_u16") < 0 ||
      require_exact_2d_shape(ground, n, width, "on_ground_u8") < 0 ||
      require_exact_2d_shape(stocks, n, width, "stocks_u8") < 0 ||
      require_exact_2d_shape(pos_x, n, width, "pos_x_f32") < 0 ||
      require_exact_2d_shape(pos_z, n, width, "pos_z_f32") < 0 ||
      require_exact_2d_shape(facing, n, width, "facing_u8") < 0) {
    return NULL;
  }
  if (PyArray_SIZE(push_x) < 256 || PyArray_SIZE(push_y) < 256) {
    PyErr_SetString(PyExc_ValueError, "pushbox LUTs must have at least 256 entries");
    return NULL;
  }
  PyArrayObject* out = (PyArrayObject*)PyArray_NewCopy(pos_z, NPY_CORDER);
  if (out == NULL) return NULL;
  const float step = (float)step_d;
  const float z_max = (float)z_max_d;
  if (!(step > 0.0f && z_max > 0.0f)) {
    return (PyObject*)out;
  }
  const uint8_t* ch = (const uint8_t*)PyArray_DATA(chr);
  const uint16_t* act = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* gr = (const uint8_t*)PyArray_DATA(ground);
  const uint8_t* st = (const uint8_t*)PyArray_DATA(stocks);
  const float* x = (const float*)PyArray_DATA(pos_x);
  const uint8_t* fac = (const uint8_t*)PyArray_DATA(facing);
  const float* px = (const float*)PyArray_DATA(push_x);
  const float* py = (const float*)PyArray_DATA(push_y);
  float* out_p = (float*)PyArray_DATA(out);
  float z_step[4] = {0};
  for (npy_intp fi = 1; fi < n; fi++) {
    for (npy_intp p = 0; p < width; p++) {
      out_p[(fi * width) + p] = ((const float*)PyArray_DATA(pos_z))[(fi * width) + p];
    }
    for (int k = 0; k < 4; k++) z_step[k] = 0.0f;
    for (int p = 0; p < num_players; p++) {
      const npy_intp idx_prev_p = ((fi - 1) * width) + p;
      const npy_intp idx_cur_p = (fi * width) + p;
      const uint8_t cid = ch[idx_prev_p];
      if (st[idx_cur_p] == 0u || gr[idx_cur_p] == 0u || st[idx_prev_p] == 0u ||
          gr[idx_prev_p] == 0u || !msl_py_hidden_z_action_allows_depth(act[idx_cur_p]) ||
          !msl_py_hidden_z_action_allows_depth(act[idx_prev_p]) || !(py[cid] > 0.0f)) {
        continue;
      }
      const float p_push = py[cid];
      const float p_face = fac[idx_prev_p] != 0u ? 1.0f : -1.0f;
      const float p_center = x[idx_prev_p] + px[cid] * p_face;
      for (int q = 0; q < num_players; q++) {
        if (q == p) continue;
        const npy_intp idx_prev_q = ((fi - 1) * width) + q;
        const npy_intp idx_cur_q = (fi * width) + q;
        const uint8_t qid = ch[idx_prev_q];
        if (st[idx_cur_q] == 0u || gr[idx_cur_q] == 0u || st[idx_prev_q] == 0u ||
            gr[idx_prev_q] == 0u || !msl_py_hidden_z_action_allows_depth(act[idx_cur_q]) ||
            !msl_py_hidden_z_action_allows_depth(act[idx_prev_q]) || !(py[qid] > 0.0f)) {
          continue;
        }
        const float q_face = fac[idx_prev_q] != 0u ? 1.0f : -1.0f;
        const float q_center = x[idx_prev_q] + px[qid] * q_face;
        const float delta_x = p_center - q_center;
        if (fabsf(delta_x) >= p_push + py[qid]) continue;
        const float delta_z = out_p[idx_prev_p] - out_p[idx_prev_q];
        if (delta_z < 0.0f) {
          z_step[p] -= step;
        } else if (delta_z > 0.0f) {
          z_step[p] += step;
        } else if (delta_x < 0.0f) {
          z_step[p] -= step;
        } else if (delta_x > 0.0f) {
          z_step[p] += step;
        } else if (q < p) {
          z_step[p] -= step;
        } else {
          z_step[p] += step;
        }
      }
    }
    for (int p = 0; p < num_players; p++) {
      const npy_intp idx_prev = ((fi - 1) * width) + p;
      const npy_intp idx_cur = (fi * width) + p;
      const uint8_t cid = ch[idx_prev];
      if (st[idx_cur] == 0u || gr[idx_cur] == 0u || st[idx_prev] == 0u || gr[idx_prev] == 0u ||
          !msl_py_hidden_z_action_allows_depth(act[idx_cur]) ||
          !msl_py_hidden_z_action_allows_depth(act[idx_prev]) || !(py[cid] > 0.0f)) {
        continue;
      }
      const float z = out_p[idx_prev];
      float dz = z_step[p];
      if (dz == 0.0f && z != 0.0f) dz = z < 0.0f ? step : -step;
      if ((dz > 0.0f && z < 0.0f && z + dz >= 0.0f) || (dz < 0.0f && z > 0.0f && z + dz <= 0.0f)) {
        dz = -z;
      }
      if (z + dz > z_max) {
        dz = z_max - z;
      } else if (z + dz < -z_max) {
        dz = -z_max - z;
      }
      out_p[idx_cur] = z + dz;
    }
  }
  return (PyObject*)out;
}
