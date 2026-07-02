/* Native preprocessing for animation/timebase validation derivation. */

#include "msl_preprocess.h"

typedef struct MslPyU32FloatEntry {
  uint32_t key;
  float value;
} MslPyU32FloatEntry;

typedef struct MslPyU32FloatTable {
  MslPyU32FloatEntry* entries;
  Py_ssize_t count;
} MslPyU32FloatTable;

static int msl_py_u32_float_entry_cmp(const void* a, const void* b) {
  const MslPyU32FloatEntry* ea = (const MslPyU32FloatEntry*)a;
  const MslPyU32FloatEntry* eb = (const MslPyU32FloatEntry*)b;
  return (ea->key > eb->key) - (ea->key < eb->key);
}

static void msl_py_u32_float_table_free(MslPyU32FloatTable* table) {
  PyMem_Free(table->entries);
  table->entries = NULL;
  table->count = 0;
}

static int msl_py_nested_int_float_table_build(PyObject* outer, MslPyU32FloatTable* table,
                                               const char* name) {
  table->entries = NULL;
  table->count = 0;
  if (outer == Py_None) {
    return 0;
  }
  if (!PyDict_Check(outer)) {
    PyErr_Format(PyExc_TypeError, "%s must be a dict", name);
    return -1;
  }
  Py_ssize_t total = 0;
  Py_ssize_t pos = 0;
  PyObject* key = NULL;
  PyObject* inner = NULL;
  while (PyDict_Next(outer, &pos, &key, &inner)) {
    if (PyDict_Check(inner)) {
      total += PyDict_Size(inner);
    }
  }
  if (total <= 0) {
    return 0;
  }
  table->entries = (MslPyU32FloatEntry*)PyMem_Malloc(sizeof(*table->entries) * (size_t)total);
  if (table->entries == NULL) {
    PyErr_NoMemory();
    return -1;
  }

  pos = 0;
  while (PyDict_Next(outer, &pos, &key, &inner)) {
    if (!PyDict_Check(inner)) {
      continue;
    }
    const long cid_long = PyLong_AsLong(key);
    if (PyErr_Occurred()) {
      msl_py_u32_float_table_free(table);
      return -1;
    }
    if (cid_long < 0 || cid_long > 255) {
      continue;
    }
    Py_ssize_t inner_pos = 0;
    PyObject* inner_key = NULL;
    PyObject* value = NULL;
    while (PyDict_Next(inner, &inner_pos, &inner_key, &value)) {
      const long raw_inner = PyLong_AsLong(inner_key);
      if (PyErr_Occurred()) {
        msl_py_u32_float_table_free(table);
        return -1;
      }
      const double d = PyFloat_AsDouble(value);
      if (PyErr_Occurred()) {
        msl_py_u32_float_table_free(table);
        return -1;
      }
      table->entries[table->count].key =
          ((uint32_t)((uint8_t)cid_long) << 16) | ((uint32_t)raw_inner & 0xFFFFu);
      table->entries[table->count].value = (float)d;
      table->count++;
    }
  }
  qsort(table->entries, (size_t)table->count, sizeof(*table->entries), msl_py_u32_float_entry_cmp);
  return 0;
}

static bool msl_py_u32_float_table_lookup(const MslPyU32FloatTable* table, uint8_t cid,
                                          uint16_t inner_key, float* out) {
  const uint32_t key = ((uint32_t)cid << 16) | (uint32_t)inner_key;
  Py_ssize_t lo = 0;
  Py_ssize_t hi = table->count;
  while (lo < hi) {
    const Py_ssize_t mid = lo + ((hi - lo) / 2);
    const uint32_t mid_key = table->entries[mid].key;
    if (mid_key < key) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < table->count && table->entries[lo].key == key) {
    *out = table->entries[lo].value;
    return true;
  }
  return false;
}

static int msl_py_landing_air_lag_table_build(PyObject* outer, float out_lag[256][5]) {
  memset(out_lag, 0, sizeof(float) * 256u * 5u);
  if (!PyDict_Check(outer)) {
    PyErr_SetString(PyExc_TypeError, "char_landing_air_lag_frames must be a dict");
    return -1;
  }
  Py_ssize_t pos = 0;
  PyObject* key = NULL;
  PyObject* inner = NULL;
  while (PyDict_Next(outer, &pos, &key, &inner)) {
    if (!PyDict_Check(inner)) {
      continue;
    }
    const long cid_long = PyLong_AsLong(key);
    if (PyErr_Occurred()) {
      return -1;
    }
    if (cid_long < 0 || cid_long > 255) {
      continue;
    }
    static const char* names[5] = {"airn", "airf", "airb", "airhi", "airlw"};
    for (int i = 0; i < 5; i++) {
      PyObject* value = PyDict_GetItemString(inner, names[i]);
      if (value == NULL) {
        continue;
      }
      const double d = PyFloat_AsDouble(value);
      if (PyErr_Occurred()) {
        return -1;
      }
      out_lag[(uint8_t)cid_long][i] = (float)d;
    }
  }
  return 0;
}

static int msl_py_landing_air_index(uint16_t action_id) {
  switch (action_id) {
    case 0x0046:
      return 0;
    case 0x0047:
      return 1;
    case 0x0048:
      return 2;
    case 0x0049:
      return 3;
    case 0x004A:
      return 4;
    default:
      return -1;
  }
}

static bool msl_py_landing_fallspecial_lag_for_entry(npy_intp i, uint8_t cid,
                                                     const uint16_t* action,
                                                     const MslPyU32FloatTable* origin_lag,
                                                     float* out_lag) {
  enum {
    ACT_FALL_SPECIAL = 0x0023,
    ACT_FALL_SPECIAL_F = 0x0024,
    ACT_FALL_SPECIAL_B = 0x0025,
    ACT_ESCAPE_AIR = 0x00EC,
  };
  if (i <= 0) {
    return false;
  }
  uint16_t prev_action = action[i - 1];
  uint16_t origin_action = prev_action;
  if (prev_action == ACT_FALL_SPECIAL || prev_action == ACT_FALL_SPECIAL_F ||
      prev_action == ACT_FALL_SPECIAL_B) {
    npy_intp j = i - 1;
    while (j > 0) {
      const uint16_t before = action[j - 1];
      if (!(before == ACT_FALL_SPECIAL || before == ACT_FALL_SPECIAL_F ||
            before == ACT_FALL_SPECIAL_B)) {
        break;
      }
      j--;
    }
    origin_action = j > 0 ? action[j - 1] : prev_action;
  }
  if (origin_action == ACT_ESCAPE_AIR) {
    return false;
  }
  return msl_py_u32_float_table_lookup(origin_lag, cid, origin_action, out_lag);
}

PyObject* msl_derive_frame_speed_mul_f32_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* state_age_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* animation_index_obj = NULL;
  PyObject* lr_press_timer_obj = NULL;
  PyObject* shield_hp_obj = NULL;
  PyObject* lightshield_obj = NULL;
  PyObject* end_frames_by_char_id = NULL;
  PyObject* char_landing_air_lag_frames = NULL;
  PyObject* char_fallspecial_origin_lag = NULL;
  int use_guard_setoff_entry_rate = 0;
  int common_lcancel_window_frames = 0;
  double shield_hit_mul_d = 0.0;
  double shield_hit_base_d = 0.0;
  double shield_hit_ls_min_d = 0.0;
  double shield_hit_ls_max_d = 0.0;
  double shield_stun_mul_d = 0.0;
  double shield_stun_base_d = 0.0;
  double shield_stun_ls_min_d = 0.0;
  double shield_stun_ls_max_d = 0.0;
  double common_lcancel_lag_div_d = 0.0;
  double common_landing_fall_special_lag_frames_d = 0.0;

  if (!PyArg_ParseTuple(
          args, "OOOOOOOOiddddddddOiddOO", &state_age_obj, &action_obj, &hitlag_obj, &char_obj,
          &animation_index_obj, &lr_press_timer_obj, &shield_hp_obj, &lightshield_obj,
          &use_guard_setoff_entry_rate, &shield_hit_mul_d, &shield_hit_base_d, &shield_hit_ls_min_d,
          &shield_hit_ls_max_d, &shield_stun_mul_d, &shield_stun_base_d, &shield_stun_ls_min_d,
          &shield_stun_ls_max_d, &end_frames_by_char_id, &common_lcancel_window_frames,
          &common_lcancel_lag_div_d, &common_landing_fall_special_lag_frames_d,
          &char_landing_air_lag_frames, &char_fallspecial_origin_lag)) {
    return NULL;
  }

  PyArrayObject* state_age =
      require_contiguous_array_readonly(state_age_obj, NPY_FLOAT32, 1, "state_age_f32");
  PyArrayObject* action = require_contiguous_array_readonly(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* hitlag = require_contiguous_array_readonly(hitlag_obj, NPY_UINT16, 1, "hitlag");
  PyArrayObject* chr = require_contiguous_array_readonly(char_obj, NPY_UINT8, 1, "char_id");
  PyArrayObject* animation_index =
      require_contiguous_array_readonly(animation_index_obj, NPY_UINT32, 1, "animation_index");
  PyArrayObject* lr_press_timer =
      require_contiguous_array_readonly(lr_press_timer_obj, NPY_UINT8, 1, "lr_press_timer");
  PyArrayObject* shield_hp = NULL;
  PyArrayObject* lightshield = NULL;
  if (use_guard_setoff_entry_rate) {
    shield_hp = require_contiguous_array_readonly(shield_hp_obj, NPY_FLOAT32, 1, "shield_hp");
    lightshield =
        require_contiguous_array_readonly(lightshield_obj, NPY_FLOAT32, 1, "lightshield_amount");
  }
  if (state_age == NULL || action == NULL || hitlag == NULL || chr == NULL ||
      animation_index == NULL || lr_press_timer == NULL ||
      (use_guard_setoff_entry_rate && (shield_hp == NULL || lightshield == NULL))) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(state_age, 0);
  if (PyArray_NDIM(state_age) != 1 || PyArray_NDIM(action) != 1 || PyArray_NDIM(hitlag) != 1 ||
      PyArray_NDIM(chr) != 1 || PyArray_NDIM(animation_index) != 1 ||
      PyArray_NDIM(lr_press_timer) != 1 || PyArray_DIM(action, 0) != n ||
      PyArray_DIM(hitlag, 0) != n || PyArray_DIM(chr, 0) != n ||
      PyArray_DIM(animation_index, 0) != n || PyArray_DIM(lr_press_timer, 0) != n ||
      (use_guard_setoff_entry_rate &&
       (PyArray_DIM(shield_hp, 0) != n || PyArray_DIM(lightshield, 0) != n))) {
    PyErr_SetString(PyExc_ValueError, "frame-speed inputs must be one-dimensional equal length");
    return NULL;
  }
  if (!PyDict_Check(end_frames_by_char_id) || !PyDict_Check(char_landing_air_lag_frames) ||
      !(char_fallspecial_origin_lag == Py_None || PyDict_Check(char_fallspecial_origin_lag))) {
    PyErr_SetString(PyExc_TypeError, "frame-speed source tables must be dictionaries");
    return NULL;
  }

  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_SimpleNew(1, dims, NPY_FLOAT32);
  if (out == NULL) {
    return NULL;
  }
  float* outp = (float*)PyArray_DATA(out);
  if (n == 0) {
    return (PyObject*)out;
  }

  MslPyU32FloatTable end_frame_table = {0};
  MslPyU32FloatTable fallspecial_origin_lag_table = {0};
  float landing_air_lag[256][5];
  if (msl_py_nested_int_float_table_build(end_frames_by_char_id, &end_frame_table,
                                          "end_frames.by_char_id") < 0 ||
      msl_py_nested_int_float_table_build(char_fallspecial_origin_lag,
                                          &fallspecial_origin_lag_table,
                                          "char_fallspecial_origin_lag") < 0 ||
      msl_py_landing_air_lag_table_build(char_landing_air_lag_frames, landing_air_lag) < 0) {
    msl_py_u32_float_table_free(&end_frame_table);
    msl_py_u32_float_table_free(&fallspecial_origin_lag_table);
    Py_DECREF(out);
    return NULL;
  }

  const float* age = (const float*)PyArray_DATA(state_age);
  const uint16_t* actionp = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hitlagp = (const uint16_t*)PyArray_DATA(hitlag);
  const uint8_t* charp = (const uint8_t*)PyArray_DATA(chr);
  const uint32_t* animp = (const uint32_t*)PyArray_DATA(animation_index);
  const uint8_t* lr = (const uint8_t*)PyArray_DATA(lr_press_timer);
  const float* hp = use_guard_setoff_entry_rate ? (const float*)PyArray_DATA(shield_hp) : NULL;
  const float* light = use_guard_setoff_entry_rate ? (const float*)PyArray_DATA(lightshield) : NULL;
  const float shield_hit_mul = (float)shield_hit_mul_d;
  const float shield_hit_base = (float)shield_hit_base_d;
  const float shield_hit_ls_min = (float)shield_hit_ls_min_d;
  const float shield_hit_ls_max = (float)shield_hit_ls_max_d;
  const float shield_stun_mul = (float)shield_stun_mul_d;
  const float shield_stun_base = (float)shield_stun_base_d;
  const float shield_stun_ls_min = (float)shield_stun_ls_min_d;
  const float shield_stun_ls_max = (float)shield_stun_ls_max_d;
  const float common_lcancel_lag_div = (float)common_lcancel_lag_div_d;
  const float common_landing_fall_special_lag_frames =
      (float)common_landing_fall_special_lag_frames_d;

  enum {
    ACT_LANDING_AIR_N = 0x0046,
    ACT_LANDING_AIR_F = 0x0047,
    ACT_LANDING_AIR_B = 0x0048,
    ACT_LANDING_AIR_HI = 0x0049,
    ACT_LANDING_AIR_LW = 0x004A,
    ACT_LANDING_FALL_SPECIAL = 0x002B,
    ACT_GUARD_SET_OFF = 0x00B5,
  };

  float last = 1.0f;
  outp[0] = last;
  for (npy_intp i = 1; i < n; i++) {
    const bool changed =
        actionp[i] != actionp[i - 1] || animp[i] != animp[i - 1] || charp[i] != charp[i - 1];
    if (!changed) {
      if (hitlagp[i] != 0u) {
        // Hitlag freezes animation advancement but does not rewrite fp->frame_speed_mul.
        // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
        outp[i] = last;
        continue;
      }
      const float delta = (float)(age[i] - age[i - 1]);
      if (isfinite(delta) && delta >= 0.0f) {
        last = delta;
      }
      outp[i] = last;
      continue;
    }

    // Motion-state entry writes the new anim rate even under hitlag; hitlag only gates the later
    // animation tick. refs/melee/src/melee/ft/fighter.c::Fighter_ChangeMotionState
    // refs/melee/src/melee/ft/fighter.c::Fighter_8006A360
    const uint8_t cid = charp[i];
    const uint32_t msid_u32 = animp[i];
    float end_frame = 0.0f;
    bool has_end_frame = false;
    if (msid_u32 != 0xFFFFFFFFu) {
      if (msl_py_u32_float_table_lookup(&end_frame_table, cid, (uint16_t)(msid_u32 & 0xFFFFu),
                                        &end_frame)) {
        has_end_frame = true;
      }
    }

    const uint16_t a = actionp[i];
    if (a == ACT_LANDING_FALL_SPECIAL && has_end_frame) {
      float lag = common_landing_fall_special_lag_frames;
      (void)msl_py_landing_fallspecial_lag_for_entry(i, cid, actionp, &fallspecial_origin_lag_table,
                                                     &lag);
      if (lag > 0.0f) {
        // ftCo_LandingFallSpecial_Enter seeds anim rate as (end_frame + 0.1f) / landing_lag.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Landing.c
        last = (float)((end_frame + 0.1f) / lag);
        outp[i] = last;
        continue;
      }
    }

    const int landing_air_idx = msl_py_landing_air_index(a);
    if (landing_air_idx >= 0) {
      float lag = landing_air_lag[cid][landing_air_idx];
      if (lag > 0.0f && has_end_frame) {
        // LandingAir uses (ftAnim_8006F484 + 0.1f) / lag; EnterWithLag applies the L-cancel
        // divisor when x67F is inside the common L-cancel window.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_LandingAir.c::{
        //   ftCo_LandingAir_EnterWithMsidLag,ftCo_LandingAir_EnterWithLag}
        if (lag > 0.0f && (int)lr[i] < common_lcancel_window_frames) {
          const float div_lag = lag / common_lcancel_lag_div;
          int int_lag = (int)div_lag;
          if (int_lag == 0) {
            int_lag = 1;
          }
          lag = (float)int_lag;
        }
        if (lag > 0.0f) {
          last = (float)((end_frame + 0.1f) / lag);
          outp[i] = last;
          continue;
        }
      }
    }

    if (a == ACT_GUARD_SET_OFF && actionp[i - 1] != ACT_GUARD_SET_OFF && has_end_frame &&
        use_guard_setoff_entry_rate) {
      // GuardSetOff entry rate uses ftCo_80092F2C. Slippi lacks x19A4/x19A0, so validation
      // backsolves shield-hit integer damage from the causal shield HP drop and lightshield term.
      // refs/melee/src/melee/ft/chara/ftCommon/ftCo_Guard.c::ftCo_80092F2C
      // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80076CBC,getEnvDmg}
      float shield_drop = (float)(hp[i - 1] - hp[i]);
      if (shield_drop > 0.0f && shield_hit_mul > 0.0f) {
        float ls = light[i];
        if (ls < 0.0f) {
          ls = 0.0f;
        }
        if (ls > 1.0f) {
          ls = 1.0f;
        }
        const float shield_hit_light_term =
            (float)(ls * (shield_hit_ls_max - shield_hit_ls_min) + shield_hit_ls_min);
        const float shield_hit_den = (float)(shield_hit_mul * (1.0f - shield_hit_light_term));
        if (shield_hit_den > 0.0f) {
          float shield_damage_taken = (float)((shield_drop - shield_hit_base) / shield_hit_den);
          if (shield_damage_taken < 0.0f) {
            shield_damage_taken = 0.0f;
          }
          float int_dmg_est = truncf(shield_damage_taken);
          if (int_dmg_est < 0.0f) {
            int_dmg_est = 0.0f;
          }
          const float shield_stun_light_term =
              (float)(ls * (shield_stun_ls_max - shield_stun_ls_min) + shield_stun_ls_min);
          const float setoff_f =
              (float)(shield_stun_mul * (int_dmg_est * (1.0f - shield_stun_light_term)) +
                      shield_stun_base);
          if (setoff_f > 0.0f) {
            last = (float)((end_frame + 0.1f) / setoff_f);
            outp[i] = last;
            continue;
          }
        }
      }
    }

    last = 1.0f;
    outp[i] = last;
  }
  msl_py_u32_float_table_free(&end_frame_table);
  msl_py_u32_float_table_free(&fallspecial_origin_lag_table);
  return (PyObject*)out;
}

typedef struct MslPyU32U8Entry {
  uint32_t key;
  uint8_t value;
} MslPyU32U8Entry;

typedef struct MslPyU32U8Table {
  MslPyU32U8Entry* entries;
  Py_ssize_t count;
} MslPyU32U8Table;

static int msl_py_u32_u8_entry_cmp(const void* a, const void* b) {
  const MslPyU32U8Entry* ea = (const MslPyU32U8Entry*)a;
  const MslPyU32U8Entry* eb = (const MslPyU32U8Entry*)b;
  return (ea->key > eb->key) - (ea->key < eb->key);
}

static void msl_py_u32_u8_table_free(MslPyU32U8Table* table) {
  PyMem_Free(table->entries);
  table->entries = NULL;
  table->count = 0;
}

static int msl_py_origin_allow_table_build(PyObject* outer, MslPyU32U8Table* table) {
  table->entries = NULL;
  table->count = 0;
  if (!PyDict_Check(outer)) {
    PyErr_SetString(PyExc_TypeError, "origin_allow_by_char must be a dict");
    return -1;
  }
  Py_ssize_t total = 0;
  Py_ssize_t pos = 0;
  PyObject* key = NULL;
  PyObject* inner = NULL;
  while (PyDict_Next(outer, &pos, &key, &inner)) {
    if (PyDict_Check(inner)) {
      total += PyDict_Size(inner);
    }
  }
  if (total <= 0) {
    return 0;
  }
  table->entries = (MslPyU32U8Entry*)PyMem_Malloc(sizeof(*table->entries) * (size_t)total);
  if (table->entries == NULL) {
    PyErr_NoMemory();
    return -1;
  }
  pos = 0;
  while (PyDict_Next(outer, &pos, &key, &inner)) {
    if (!PyDict_Check(inner)) {
      continue;
    }
    const long cid_long = PyLong_AsLong(key);
    if (PyErr_Occurred()) {
      msl_py_u32_u8_table_free(table);
      return -1;
    }
    if (cid_long < 0 || cid_long > 255) {
      continue;
    }
    Py_ssize_t inner_pos = 0;
    PyObject* inner_key = NULL;
    PyObject* value = NULL;
    while (PyDict_Next(inner, &inner_pos, &inner_key, &value)) {
      const long raw_inner = PyLong_AsLong(inner_key);
      if (PyErr_Occurred()) {
        msl_py_u32_u8_table_free(table);
        return -1;
      }
      if (!PyTuple_Check(value) || PyTuple_GET_SIZE(value) < 2) {
        continue;
      }
      const long entry_allow = PyLong_AsLong(PyTuple_GET_ITEM(value, 0));
      if (PyErr_Occurred()) {
        msl_py_u32_u8_table_free(table);
        return -1;
      }
      const long direct_lfs_allow = PyLong_AsLong(PyTuple_GET_ITEM(value, 1));
      if (PyErr_Occurred()) {
        msl_py_u32_u8_table_free(table);
        return -1;
      }
      table->entries[table->count].key =
          ((uint32_t)((uint8_t)cid_long) << 16) | ((uint32_t)raw_inner & 0xFFFFu);
      table->entries[table->count].value =
          (uint8_t)((entry_allow != 0 ? 1u : 0u) | (direct_lfs_allow != 0 ? 2u : 0u));
      table->count++;
    }
  }
  qsort(table->entries, (size_t)table->count, sizeof(*table->entries), msl_py_u32_u8_entry_cmp);
  return 0;
}

static bool msl_py_u32_u8_table_lookup(const MslPyU32U8Table* table, uint8_t cid,
                                       uint16_t inner_key, uint8_t* out) {
  const uint32_t key = ((uint32_t)cid << 16) | (uint32_t)inner_key;
  Py_ssize_t lo = 0;
  Py_ssize_t hi = table->count;
  while (lo < hi) {
    const Py_ssize_t mid = lo + ((hi - lo) / 2);
    const uint32_t mid_key = table->entries[mid].key;
    if (mid_key < key) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  if (lo < table->count && table->entries[lo].key == key) {
    *out = table->entries[lo].value;
    return true;
  }
  return false;
}

PyObject* msl_derive_landing_fallspecial_allow_interrupt_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* origin_allow_by_char = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &action_obj, &char_obj, &origin_allow_by_char)) {
    return NULL;
  }
  PyArrayObject* action = require_contiguous_array_readonly(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* chr = require_contiguous_array_readonly(char_obj, NPY_UINT8, 1, "char_id");
  if (action == NULL || chr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(action, 0);
  if (PyArray_NDIM(action) != 1 || PyArray_NDIM(chr) != 1 || PyArray_DIM(chr, 0) != n ||
      !PyDict_Check(origin_allow_by_char)) {
    PyErr_SetString(PyExc_ValueError, "LandingFallSpecial allow inputs have incompatible shapes");
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_ZEROS(1, dims, NPY_UINT8, 0);
  if (out == NULL) {
    return NULL;
  }
  MslPyU32U8Table origin_allow_table = {0};
  if (msl_py_origin_allow_table_build(origin_allow_by_char, &origin_allow_table) < 0) {
    Py_DECREF(out);
    return NULL;
  }
  const uint16_t* actionp = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* charp = (const uint8_t*)PyArray_DATA(chr);
  uint8_t* outp = (uint8_t*)PyArray_DATA(out);
  enum {
    ACT_FALL_SPECIAL = 0x0023,
    ACT_FALL_SPECIAL_F = 0x0024,
    ACT_FALL_SPECIAL_B = 0x0025,
    ACT_LANDING_FALL_SPECIAL = 0x002B,
    ACT_ESCAPE_AIR = 0x00EC,
  };
  int fallspecial_allow = 0;
  int lfs_allow = 0;
  int prev = -1;
  for (npy_intp i = 0; i < n; i++) {
    const int cur = (int)actionp[i];
    const bool cur_fall =
        cur == ACT_FALL_SPECIAL || cur == ACT_FALL_SPECIAL_F || cur == ACT_FALL_SPECIAL_B;
    const bool prev_fall =
        prev == ACT_FALL_SPECIAL || prev == ACT_FALL_SPECIAL_F || prev == ACT_FALL_SPECIAL_B;
    if (i == 0 || cur != prev) {
      uint8_t allow_bits = 0;
      const bool found =
          msl_py_u32_u8_table_lookup(&origin_allow_table, charp[i], (uint16_t)prev, &allow_bits);
      if (cur_fall) {
        // EscapeAir_Coll enters FallSpecial with allow_interrupt=false; FallSpecial_Coll forwards
        // the carried mv.co.fallspecial.allow_interrupt into LandingFallSpecial.
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_EscapeAir.c::ftCo_80099D70
        // refs/melee/src/melee/ft/chara/ftCommon/ftCo_FallSpecial.c::ftCo_80096D28
        fallspecial_allow = prev == ACT_ESCAPE_AIR ? 0 : (found ? ((allow_bits & 1u) != 0u) : 1);
        lfs_allow = 0;
      } else if (cur == ACT_LANDING_FALL_SPECIAL) {
        if (prev_fall) {
          lfs_allow = fallspecial_allow != 0;
        } else if (found) {
          lfs_allow = (allow_bits & 2u) != 0u;
        } else {
          lfs_allow = 0;
        }
      } else {
        fallspecial_allow = 0;
        lfs_allow = 0;
      }
    }
    if (cur_fall) {
      outp[i] = (uint8_t)(fallspecial_allow != 0);
    } else if (cur == ACT_LANDING_FALL_SPECIAL) {
      outp[i] = (uint8_t)(lfs_allow != 0);
    }
    prev = cur;
  }
  msl_py_u32_u8_table_free(&origin_allow_table);
  return (PyObject*)out;
}
