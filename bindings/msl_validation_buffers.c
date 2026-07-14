/* Native validation buffer materialization for replay-visible owner families. */

#include "msl_validation_buffers.h"

#include <stdint.h>
#include <string.h>

#include "../src/api.h"

static int validation_array_rows(PyArrayObject* arr, npy_intp* rows, const char* name) {
  if (PyArray_NDIM(arr) < 1) {
    PyErr_Format(PyExc_ValueError, "%s must have at least one dimension", name);
    return -1;
  }
  *rows = PyArray_DIM(arr, 0);
  return 0;
}

static int validation_require_rows(PyArrayObject* arr, npy_intp rows, const char* name) {
  if (PyArray_DIM(arr, 0) != rows) {
    PyErr_Format(PyExc_ValueError, "%s row count mismatch", name);
    return -1;
  }
  return 0;
}

static int validation_require_u8_rows(PyArrayObject* arr, npy_intp rows, npy_intp min_cols,
                                      const char* name) {
  if (PyArray_NDIM(arr) != 2 || PyArray_DIM(arr, 0) != rows || PyArray_DIM(arr, 1) < min_cols) {
    PyErr_Format(PyExc_ValueError, "%s must be uint8[%zd, >=%zd]", name, rows, min_cols);
    return -1;
  }
  return 0;
}

static PyArrayObject* validation_require_output(PyObject* obj, int typenum, int ndim,
                                                const char* name) {
  PyArrayObject* arr = require_contiguous_array(obj, typenum, ndim, name);
  if (arr == NULL) {
    return NULL;
  }
  if (!PyArray_ISWRITEABLE(arr)) {
    PyErr_Format(PyExc_ValueError, "%s must be writable", name);
    return NULL;
  }
  return arr;
}

static int validation_require_2d(PyArrayObject* arr, npy_intp rows, npy_intp cols,
                                 const char* name) {
  if (PyArray_NDIM(arr) != 2 || PyArray_DIM(arr, 0) != rows || PyArray_DIM(arr, 1) != cols) {
    PyErr_Format(PyExc_ValueError, "%s shape mismatch", name);
    return -1;
  }
  return 0;
}

static int validation_require_3d(PyArrayObject* arr, npy_intp rows, npy_intp cols, npy_intp depth,
                                 const char* name) {
  if (PyArray_NDIM(arr) != 3 || PyArray_DIM(arr, 0) != rows || PyArray_DIM(arr, 1) != cols ||
      PyArray_DIM(arr, 2) != depth) {
    PyErr_Format(PyExc_ValueError, "%s shape mismatch", name);
    return -1;
  }
  return 0;
}

PyObject* msl_validation_finalized_frame_indices_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* frame_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &frame_obj)) {
    return NULL;
  }
  PyArrayObject* frame_ids =
      require_contiguous_array_readonly(frame_obj, NPY_INT32, 1, "frame_ids_i32");
  if (frame_ids == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(frame_ids, 0);
  const int32_t* ids = (const int32_t*)PyArray_DATA(frame_ids);
  uint8_t* keep_mask = (uint8_t*)PyMem_Calloc((size_t)n, sizeof(uint8_t));
  if (keep_mask == NULL) {
    PyErr_NoMemory();
    return NULL;
  }

  npy_intp count = 0;
  int64_t min_id = 0;
  int64_t max_id = -1;
  if (n > 0) {
    min_id = ids[0];
    max_id = ids[0];
    for (npy_intp i = 1; i < n; i++) {
      if ((int64_t)ids[i] < min_id) min_id = ids[i];
      if ((int64_t)ids[i] > max_id) max_id = ids[i];
    }
  }
  const int64_t id_range = max_id - min_id + 1;
  if (id_range > 0 && id_range <= (int64_t)n * 8 + 1024 && id_range <= 10000000) {
    uint8_t* seen = (uint8_t*)PyMem_Calloc((size_t)id_range, sizeof(uint8_t));
    if (seen == NULL) {
      PyMem_Free(keep_mask);
      PyErr_NoMemory();
      return NULL;
    }
    for (npy_intp i = n - 1; i >= 0; i--) {
      const int64_t key = (int64_t)ids[i] - min_id;
      if (seen[key] == 0u) {
        seen[key] = 1u;
        keep_mask[i] = 1u;
        count++;
      }
    }
    PyMem_Free(seen);
  } else {
    for (npy_intp i = n - 1; i >= 0; i--) {
      uint8_t seen_later = 0u;
      for (npy_intp j = i + 1; j < n; j++) {
        if (ids[j] == ids[i]) {
          seen_later = 1u;
          break;
        }
      }
      if (seen_later == 0u) {
        keep_mask[i] = 1u;
        count++;
      }
    }
  }

  npy_intp dims[1] = {count};
  PyArrayObject* out = (PyArrayObject*)PyArray_SimpleNew(1, dims, NPY_INT32);
  if (out == NULL) {
    PyMem_Free(keep_mask);
    return NULL;
  }
  int32_t* keep = (int32_t*)PyArray_DATA(out);
  npy_intp out_i = 0;
  for (npy_intp i = 0; i < n; i++) {
    if (keep_mask[i] != 0u) {
      keep[out_i++] = (int32_t)i;
    }
  }
  PyMem_Free(keep_mask);
  return (PyObject*)out;
}

PyObject* msl_validation_project_post_cache_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* prev_obj = NULL;
  PyObject* input_obj = NULL;
  PyObject* ref_obj = NULL;
  PyObject* post_team_obj = NULL;
  PyObject* post_char_obj = NULL;
  PyObject* post_action_obj = NULL;
  PyObject* post_action_frame_obj = NULL;
  PyObject* post_animation_index_obj = NULL;
  PyObject* post_facing_obj = NULL;
  PyObject* post_on_ground_obj = NULL;
  PyObject* post_ground_id_obj = NULL;
  PyObject* post_pos_x_obj = NULL;
  PyObject* post_pos_y_obj = NULL;
  PyObject* post_stocks_obj = NULL;
  PyObject* post_shield_obj = NULL;
  PyObject* post_hurtbox_obj = NULL;
  PyObject* post_instance_hit_by_obj = NULL;
  PyObject* post_instance_id_obj = NULL;
  PyObject* post_hitlag_obj = NULL;
  PyObject* post_hitstun_obj = NULL;
  PyObject* post_state_flags_obj = NULL;
  PyObject* post_last_hit_by_obj = NULL;
  PyObject* post_source_port0_obj = NULL;
  PyObject* pre_buttons_obj = NULL;
  PyObject* pre_main_x_obj = NULL;
  PyObject* pre_main_y_obj = NULL;
  PyObject* pre_l_obj = NULL;
  PyObject* pre_r_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(
          args, "OOOOOOOOOOOOOOOOOOOOOOOOOOOOOi", &seed_obj, &prev_obj, &input_obj, &ref_obj,
          &post_team_obj, &post_char_obj, &post_action_obj, &post_action_frame_obj,
          &post_animation_index_obj, &post_facing_obj, &post_on_ground_obj, &post_ground_id_obj,
          &post_pos_x_obj, &post_pos_y_obj, &post_stocks_obj, &post_shield_obj, &post_hurtbox_obj,
          &post_instance_hit_by_obj, &post_instance_id_obj, &post_hitlag_obj, &post_hitstun_obj,
          &post_state_flags_obj, &post_last_hit_by_obj, &post_source_port0_obj, &pre_buttons_obj,
          &pre_main_x_obj, &pre_main_y_obj, &pre_l_obj, &pre_r_obj, &num_players)) {
    return NULL;
  }

  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* prev_arr = require_contiguous_array(prev_obj, NPY_UINT8, 2, "prev_input_u8");
  PyArrayObject* input_arr = require_contiguous_array(input_obj, NPY_UINT8, 2, "input_u8");
  PyArrayObject* ref_arr = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  PyArrayObject* post_team = validation_require_output(post_team_obj, NPY_UINT8, 2, "post_team_id");
  PyArrayObject* post_char = validation_require_output(post_char_obj, NPY_UINT8, 2, "post_char_id");
  PyArrayObject* post_action =
      validation_require_output(post_action_obj, NPY_UINT16, 2, "post_action_id");
  PyArrayObject* post_action_frame =
      validation_require_output(post_action_frame_obj, NPY_INT16, 2, "post_action_frame");
  PyArrayObject* post_animation_index =
      validation_require_output(post_animation_index_obj, NPY_UINT32, 2, "post_animation_index");
  PyArrayObject* post_facing =
      validation_require_output(post_facing_obj, NPY_UINT8, 2, "post_facing");
  PyArrayObject* post_on_ground =
      validation_require_output(post_on_ground_obj, NPY_UINT8, 2, "post_on_ground");
  PyArrayObject* post_ground_id =
      validation_require_output(post_ground_id_obj, NPY_UINT16, 2, "post_ground_id");
  PyArrayObject* post_pos_x =
      validation_require_output(post_pos_x_obj, NPY_FLOAT32, 2, "post_pos_x");
  PyArrayObject* post_pos_y =
      validation_require_output(post_pos_y_obj, NPY_FLOAT32, 2, "post_pos_y");
  PyArrayObject* post_stocks =
      validation_require_output(post_stocks_obj, NPY_UINT8, 2, "post_stocks");
  PyArrayObject* post_shield =
      validation_require_output(post_shield_obj, NPY_FLOAT32, 2, "post_shield_hp");
  PyArrayObject* post_hurtbox =
      validation_require_output(post_hurtbox_obj, NPY_UINT8, 2, "post_hurtbox_state");
  PyArrayObject* post_instance_hit_by =
      validation_require_output(post_instance_hit_by_obj, NPY_UINT16, 2, "post_instance_hit_by");
  PyArrayObject* post_instance_id =
      validation_require_output(post_instance_id_obj, NPY_UINT16, 2, "post_instance_id");
  PyArrayObject* post_hitlag =
      validation_require_output(post_hitlag_obj, NPY_UINT16, 2, "post_hitlag");
  PyArrayObject* post_hitstun =
      validation_require_output(post_hitstun_obj, NPY_UINT16, 2, "post_hitstun");
  PyArrayObject* post_state_flags =
      validation_require_output(post_state_flags_obj, NPY_UINT8, 3, "post_state_flags");
  PyArrayObject* post_last_hit_by =
      validation_require_output(post_last_hit_by_obj, NPY_UINT8, 2, "post_last_hit_by");
  PyArrayObject* post_source_port0 =
      validation_require_output(post_source_port0_obj, NPY_UINT8, 2, "post_source_port0");
  PyArrayObject* pre_buttons =
      validation_require_output(pre_buttons_obj, NPY_UINT16, 2, "pre_buttons");
  PyArrayObject* pre_main_x = validation_require_output(pre_main_x_obj, NPY_INT8, 2, "pre_main_x");
  PyArrayObject* pre_main_y = validation_require_output(pre_main_y_obj, NPY_INT8, 2, "pre_main_y");
  PyArrayObject* pre_l = validation_require_output(pre_l_obj, NPY_UINT8, 2, "pre_l");
  PyArrayObject* pre_r = validation_require_output(pre_r_obj, NPY_UINT8, 2, "pre_r");
  if (seed_arr == NULL || prev_arr == NULL || input_arr == NULL || ref_arr == NULL ||
      post_team == NULL || post_char == NULL || post_action == NULL || post_action_frame == NULL ||
      post_animation_index == NULL || post_facing == NULL || post_on_ground == NULL ||
      post_ground_id == NULL || post_pos_x == NULL || post_pos_y == NULL || post_stocks == NULL ||
      post_shield == NULL || post_hurtbox == NULL || post_instance_hit_by == NULL ||
      post_instance_id == NULL || post_hitlag == NULL || post_hitstun == NULL ||
      post_state_flags == NULL || post_last_hit_by == NULL || post_source_port0 == NULL ||
      pre_buttons == NULL || pre_main_x == NULL || pre_main_y == NULL || pre_l == NULL ||
      pre_r == NULL) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(seed_arr, 0);
  const npy_intp rows = n + 1;
  if (n < 1 || num_players < 0 || num_players > MSL_MAX_PLAYERS ||
      validation_require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      validation_require_u8_rows(prev_arr, n, (npy_intp)sizeof(MslInput), "prev_input_u8") != 0 ||
      validation_require_u8_rows(input_arr, n, (npy_intp)sizeof(MslInput), "input_u8") != 0 ||
      validation_require_u8_rows(ref_arr, n, (npy_intp)sizeof(MslCompare), "ref_u8") != 0 ||
      validation_require_2d(post_team, rows, MSL_MAX_PLAYERS, "post_team_id") != 0 ||
      validation_require_2d(post_char, rows, MSL_MAX_PLAYERS, "post_char_id") != 0 ||
      validation_require_2d(post_action, rows, MSL_MAX_PLAYERS, "post_action_id") != 0 ||
      validation_require_2d(post_action_frame, rows, MSL_MAX_PLAYERS, "post_action_frame") != 0 ||
      validation_require_2d(post_animation_index, rows, MSL_MAX_PLAYERS, "post_animation_index") !=
          0 ||
      validation_require_2d(post_facing, rows, MSL_MAX_PLAYERS, "post_facing") != 0 ||
      validation_require_2d(post_on_ground, rows, MSL_MAX_PLAYERS, "post_on_ground") != 0 ||
      validation_require_2d(post_ground_id, rows, MSL_MAX_PLAYERS, "post_ground_id") != 0 ||
      validation_require_2d(post_pos_x, rows, MSL_MAX_PLAYERS, "post_pos_x") != 0 ||
      validation_require_2d(post_pos_y, rows, MSL_MAX_PLAYERS, "post_pos_y") != 0 ||
      validation_require_2d(post_stocks, rows, MSL_MAX_PLAYERS, "post_stocks") != 0 ||
      validation_require_2d(post_shield, rows, MSL_MAX_PLAYERS, "post_shield_hp") != 0 ||
      validation_require_2d(post_hurtbox, rows, MSL_MAX_PLAYERS, "post_hurtbox_state") != 0 ||
      validation_require_2d(post_instance_hit_by, rows, MSL_MAX_PLAYERS, "post_instance_hit_by") !=
          0 ||
      validation_require_2d(post_instance_id, rows, MSL_MAX_PLAYERS, "post_instance_id") != 0 ||
      validation_require_2d(post_hitlag, rows, MSL_MAX_PLAYERS, "post_hitlag") != 0 ||
      validation_require_2d(post_hitstun, rows, MSL_MAX_PLAYERS, "post_hitstun") != 0 ||
      validation_require_3d(post_state_flags, rows, MSL_MAX_PLAYERS, 5, "post_state_flags") != 0 ||
      validation_require_2d(post_last_hit_by, rows, MSL_MAX_PLAYERS, "post_last_hit_by") != 0 ||
      validation_require_2d(post_source_port0, rows, MSL_MAX_PLAYERS, "post_source_port0") != 0 ||
      validation_require_2d(pre_buttons, rows, MSL_MAX_PLAYERS, "pre_buttons") != 0 ||
      validation_require_2d(pre_main_x, rows, MSL_MAX_PLAYERS, "pre_main_x") != 0 ||
      validation_require_2d(pre_main_y, rows, MSL_MAX_PLAYERS, "pre_main_y") != 0 ||
      validation_require_2d(pre_l, rows, MSL_MAX_PLAYERS, "pre_l") != 0 ||
      validation_require_2d(pre_r, rows, MSL_MAX_PLAYERS, "pre_r") != 0) {
    return NULL;
  }

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  uint8_t* prev_u8 = (uint8_t*)PyArray_DATA(prev_arr);
  uint8_t* input_u8 = (uint8_t*)PyArray_DATA(input_arr);
  uint8_t* ref_u8 = (uint8_t*)PyArray_DATA(ref_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t prev_stride = (size_t)PyArray_STRIDE(prev_arr, 0);
  const size_t input_stride = (size_t)PyArray_STRIDE(input_arr, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref_arr, 0);

#define OUT2(T, ARR, I, P) ((T*)PyArray_DATA(ARR))[((size_t)(I) * MSL_MAX_PLAYERS) + (size_t)(P)]
#define OUT3(T, ARR, I, P, K) \
  ((T*)PyArray_DATA(ARR))[(((size_t)(I) * MSL_MAX_PLAYERS + (size_t)(P)) * 5u) + (size_t)(K)]

  for (npy_intp i = 0; i < n; i++) {
    const MslSeed* seed = (const MslSeed*)(const void*)(seed_u8 + (size_t)i * seed_stride);
    const MslInput* prev = (const MslInput*)(const void*)(prev_u8 + (size_t)i * prev_stride);
    for (int p = 0; p < num_players; p++) {
      OUT2(uint8_t, post_team, i, p) = seed->team_id[p];
      OUT2(uint8_t, post_char, i, p) = seed->char_id[p];
      OUT2(uint16_t, post_action, i, p) = seed->action_id[p];
      OUT2(int16_t, post_action_frame, i, p) = seed->action_frame[p];
      OUT2(uint32_t, post_animation_index, i, p) = seed->animation_index[p];
      OUT2(uint8_t, post_facing, i, p) = seed->facing[p];
      OUT2(uint8_t, post_on_ground, i, p) = seed->on_ground[p];
      OUT2(uint16_t, post_ground_id, i, p) = seed->ground_id[p];
      OUT2(float, post_pos_x, i, p) = seed->pos_x[p];
      OUT2(float, post_pos_y, i, p) = seed->pos_y[p];
      OUT2(uint8_t, post_stocks, i, p) = seed->stocks[p];
      OUT2(float, post_shield, i, p) = seed->shield_hp[p];
      OUT2(uint8_t, post_hurtbox, i, p) = seed->hurtbox_state[p];
      OUT2(uint16_t, post_instance_hit_by, i, p) = seed->instance_hit_by[p];
      OUT2(uint16_t, post_instance_id, i, p) = seed->instance_id[p];
      OUT2(uint16_t, post_hitlag, i, p) = seed->hitlag[p];
      OUT2(uint16_t, post_hitstun, i, p) = seed->hitstun[p];
      for (int k = 0; k < 5; k++) {
        OUT3(uint8_t, post_state_flags, i, p, k) = seed->state_flags[p][k];
      }
      OUT2(uint8_t, post_last_hit_by, i, p) = seed->last_hit_by[p];
      OUT2(uint8_t, post_source_port0, i, p) = seed->source_port0[p];
      OUT2(uint16_t, pre_buttons, i, p) = prev->p[p].buttons;
      OUT2(int8_t, pre_main_x, i, p) = prev->p[p].main_x;
      OUT2(int8_t, pre_main_y, i, p) = prev->p[p].main_y;
      OUT2(uint8_t, pre_l, i, p) = prev->p[p].l;
      OUT2(uint8_t, pre_r, i, p) = prev->p[p].r;
    }
  }

  const MslSeed* seed_last = (const MslSeed*)(const void*)(seed_u8 + (size_t)(n - 1) * seed_stride);
  const MslInput* input_last =
      (const MslInput*)(const void*)(input_u8 + (size_t)(n - 1) * input_stride);
  const MslCompare* ref_last =
      (const MslCompare*)(const void*)(ref_u8 + (size_t)(n - 1) * ref_stride);
  for (int p = 0; p < num_players; p++) {
    OUT2(uint8_t, post_team, n, p) = ref_last->team_id[p];
    OUT2(uint8_t, post_char, n, p) = ref_last->char_id[p];
    OUT2(uint16_t, post_action, n, p) = ref_last->action_id[p];
    OUT2(int16_t, post_action_frame, n, p) = ref_last->action_frame[p];
    OUT2(uint32_t, post_animation_index, n, p) = ref_last->animation_index[p];
    OUT2(uint8_t, post_facing, n, p) = ref_last->facing[p];
    OUT2(uint8_t, post_on_ground, n, p) = ref_last->on_ground[p];
    OUT2(uint16_t, post_ground_id, n, p) = ref_last->ground_id[p];
    OUT2(float, post_pos_x, n, p) = ref_last->pos_x[p];
    OUT2(float, post_pos_y, n, p) = ref_last->pos_y[p];
    OUT2(uint8_t, post_stocks, n, p) = ref_last->stocks[p];
    OUT2(float, post_shield, n, p) = ref_last->shield_hp[p];
    OUT2(uint8_t, post_hurtbox, n, p) = ref_last->hurtbox_state[p];
    OUT2(uint16_t, post_instance_hit_by, n, p) = ref_last->instance_hit_by[p];
    OUT2(uint16_t, post_instance_id, n, p) = ref_last->instance_id[p];
    OUT2(uint16_t, post_hitlag, n, p) = ref_last->hitlag[p];
    OUT2(uint16_t, post_hitstun, n, p) = ref_last->hitstun[p];
    for (int k = 0; k < 5; k++) {
      OUT3(uint8_t, post_state_flags, n, p, k) = ref_last->state_flags[p][k];
    }
    OUT2(uint8_t, post_last_hit_by, n, p) = ref_last->last_hit_by[p];
    OUT2(uint8_t, post_source_port0, n, p) = seed_last->source_port0[p];
    OUT2(uint16_t, pre_buttons, n, p) = input_last->p[p].buttons;
    OUT2(int8_t, pre_main_x, n, p) = input_last->p[p].main_x;
    OUT2(int8_t, pre_main_y, n, p) = input_last->p[p].main_y;
    OUT2(uint8_t, pre_l, n, p) = input_last->p[p].l;
    OUT2(uint8_t, pre_r, n, p) = input_last->p[p].r;
  }

#undef OUT2
#undef OUT3

  Py_RETURN_NONE;
}

PyObject* msl_validation_init_static_buffers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* ref_obj = NULL;
  PyObject* frame_ids_obj = NULL;
  PyObject* rng_obj = NULL;
  unsigned long stage_id_ul = 0;
  int num_players = 0;
  int is_teams = 0;
  double damage_ratio = 1.0;
  if (!PyArg_ParseTuple(args, "OOOOkiid", &seed_obj, &ref_obj, &frame_ids_obj, &rng_obj,
                        &stage_id_ul, &num_players, &is_teams, &damage_ratio)) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }

  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* ref_arr = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  PyArrayObject* frame_ids_arr =
      require_contiguous_array_readonly(frame_ids_obj, NPY_INT32, 1, "frame_ids_i32");
  PyArrayObject* rng_arr =
      require_contiguous_array_readonly(rng_obj, NPY_UINT32, 1, "frame_pre_random_seed_u32");
  if (seed_arr == NULL || ref_arr == NULL || frame_ids_arr == NULL || rng_arr == NULL) {
    return NULL;
  }
  npy_intp n_frames = 0;
  if (validation_array_rows(frame_ids_arr, &n_frames, "frame_ids_i32") != 0 ||
      validation_require_rows(rng_arr, n_frames, "frame_pre_random_seed_u32") != 0) {
    return NULL;
  }
  if (n_frames < 2) {
    PyErr_SetString(PyExc_ValueError, "validation buffer init requires at least two frames");
    return NULL;
  }
  const npy_intp n_samples = n_frames - 1;
  if (validation_require_u8_rows(seed_arr, n_samples, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      validation_require_u8_rows(ref_arr, n_samples, (npy_intp)sizeof(MslCompare), "ref_u8") != 0) {
    return NULL;
  }

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  uint8_t* ref_u8 = (uint8_t*)PyArray_DATA(ref_arr);
  const int32_t* frame_ids = (const int32_t*)PyArray_DATA(frame_ids_arr);
  const uint32_t* rng = (const uint32_t*)PyArray_DATA(rng_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref_arr, 0);
  const uint32_t stage_id = (uint32_t)stage_id_ul;
  for (npy_intp i = 0; i < n_samples; i++) {
    MslSeed* seed = (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride);
    MslCompare* ref = (MslCompare*)(void*)(ref_u8 + (size_t)i * ref_stride);
    seed->frame_id = frame_ids[i];
    ref->frame_id = frame_ids[i + 1];
    // A validation seed is the post-frame fighter snapshot at i, but step_input executes the
    // destination frame i+1. Slippi's priority-0 frame-start GObj records the HSD stream before
    // that destination frame's fighter callbacks, so both the transition seed and its reference
    // carry rng[i+1]. This is one global scheduler boundary, not an owner-specific promotion.
    // refs/slippi-ssbm-asm/Recording/SendFrameStart.s::Macro_SendFrameStart
    seed->frame_pre_random_seed = rng[i + 1];
    ref->frame_pre_random_seed = rng[i + 1];
    seed->stage_id = stage_id;
    ref->stage_id = stage_id;
    seed->num_players = (uint8_t)num_players;
    ref->num_players = (uint8_t)num_players;
    seed->is_teams = (uint8_t)(is_teams != 0);
    ref->is_teams = (uint8_t)(is_teams != 0);
    seed->match_damage_ratio = (float)damage_ratio;
    memset(ref->is_dead, 1, sizeof(ref->is_dead));

    memset(seed->combo_victim_port, 0xFF, sizeof(seed->combo_victim_port));
    memset(seed->grab_owner_port, 0xFF, sizeof(seed->grab_owner_port));
    memset(seed->phantom_damage_source_port, 0xFF, sizeof(seed->phantom_damage_source_port));
    for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
      seed->floor_skip_segment_id_u16[p] = 0xFFFFu;
      seed->floor_skip_segment_valid_u8[p] = 0;
    }
  }
  Py_RETURN_NONE;
}

PyObject* msl_validation_fill_static_player_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* ref_obj = NULL;
  int slot = 0;
  int team_id = 0;
  int handicap = 9;
  double attack_ratio = 1.0;
  double defense_ratio = 1.0;
  double fighter_scale_y = 1.0;
  if (!PyArg_ParseTuple(args, "OOiiiddd", &seed_obj, &ref_obj, &slot, &team_id, &handicap,
                        &attack_ratio, &defense_ratio, &fighter_scale_y)) {
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* ref_arr = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  if (seed_arr == NULL || ref_arr == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  if (slot < 0 || slot >= MSL_MAX_PLAYERS ||
      validation_require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      validation_require_u8_rows(ref_arr, n, (npy_intp)sizeof(MslCompare), "ref_u8") != 0) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "slot out of range");
    }
    return NULL;
  }
  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  uint8_t* ref_u8 = (uint8_t*)PyArray_DATA(ref_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref_arr, 0);
  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed = (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride);
    MslCompare* ref = (MslCompare*)(void*)(ref_u8 + (size_t)i * ref_stride);
    seed->team_id[slot] = (uint8_t)team_id;
    ref->team_id[slot] = (uint8_t)team_id;
    seed->handicap[slot] = (uint8_t)handicap;
    seed->attack_ratio[slot] = (float)attack_ratio;
    seed->defense_ratio[slot] = (float)defense_ratio;
    seed->fighter_scale_y[slot] = (float)fighter_scale_y;
  }
  Py_RETURN_NONE;
}

PyObject* msl_validation_fill_visible_player_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* prev_obj = NULL;
  PyObject* input_obj = NULL;
  PyObject* ref_obj = NULL;
  int slot = 0;
  int source_port0 = 0;
  unsigned long stage_id_ul = 0;
  int dmg_x2225_b7 = 0;
  int dmg_x2224_b2 = 0;
  PyObject* buttons_obj = NULL;
  PyObject* main_x_obj = NULL;
  PyObject* main_y_obj = NULL;
  PyObject* c_x_obj = NULL;
  PyObject* c_y_obj = NULL;
  PyObject* l_obj = NULL;
  PyObject* r_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* action_frame_obj = NULL;
  PyObject* anim_frame_obj = NULL;
  PyObject* pos_x_obj = NULL;
  PyObject* pos_y_obj = NULL;
  PyObject* pos_z_obj = NULL;
  PyObject* facing_obj = NULL;
  PyObject* percent_obj = NULL;
  PyObject* shield_obj = NULL;
  PyObject* stocks_obj = NULL;
  PyObject* jumps_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
  PyObject* lcancel_obj = NULL;
  PyObject* hurtbox_obj = NULL;
  PyObject* ground_obj = NULL;
  PyObject* animation_index_obj = NULL;
  PyObject* instance_hit_by_obj = NULL;
  PyObject* instance_id_obj = NULL;
  PyObject* last_attack_obj = NULL;
  PyObject* combo_count_obj = NULL;
  PyObject* last_hit_by_obj = NULL;
  PyObject* state_flags_obj = NULL;
  PyObject* speed_air_x_obj = NULL;
  PyObject* speed_ground_x_obj = NULL;
  PyObject* speed_y_obj = NULL;
  PyObject* speed_x_attack_obj = NULL;
  PyObject* speed_y_attack_obj = NULL;
  if (!PyArg_ParseTuple(
          args, "OOOOikiiiOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO", &seed_obj, &prev_obj, &input_obj,
          &ref_obj, &slot, &stage_id_ul, &source_port0, &dmg_x2225_b7, &dmg_x2224_b2, &buttons_obj,
          &main_x_obj, &main_y_obj, &c_x_obj, &c_y_obj, &l_obj, &r_obj, &char_obj, &action_obj,
          &action_frame_obj, &anim_frame_obj, &pos_x_obj, &pos_y_obj, &pos_z_obj, &facing_obj,
          &percent_obj, &shield_obj, &stocks_obj, &jumps_obj, &on_ground_obj, &hitlag_obj,
          &hitstun_obj, &lcancel_obj, &hurtbox_obj, &ground_obj, &animation_index_obj,
          &instance_hit_by_obj, &instance_id_obj, &last_attack_obj, &combo_count_obj,
          &last_hit_by_obj, &state_flags_obj, &speed_air_x_obj, &speed_ground_x_obj, &speed_y_obj,
          &speed_x_attack_obj, &speed_y_attack_obj)) {
    return NULL;
  }

  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* prev_arr = require_contiguous_array(prev_obj, NPY_UINT8, 2, "prev_input_u8");
  PyArrayObject* input_arr = require_contiguous_array(input_obj, NPY_UINT8, 2, "input_u8");
  PyArrayObject* ref_arr = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  PyArrayObject* buttons = require_contiguous_array_readonly(buttons_obj, NPY_UINT16, 1, "buttons");
  PyArrayObject* main_x = require_contiguous_array_readonly(main_x_obj, NPY_INT8, 1, "main_x");
  PyArrayObject* main_y = require_contiguous_array_readonly(main_y_obj, NPY_INT8, 1, "main_y");
  PyArrayObject* c_x = require_contiguous_array_readonly(c_x_obj, NPY_INT8, 1, "c_x");
  PyArrayObject* c_y = require_contiguous_array_readonly(c_y_obj, NPY_INT8, 1, "c_y");
  PyArrayObject* l = require_contiguous_array_readonly(l_obj, NPY_UINT8, 1, "l");
  PyArrayObject* r = require_contiguous_array_readonly(r_obj, NPY_UINT8, 1, "r");
  PyArrayObject* ch = require_contiguous_array_readonly(char_obj, NPY_UINT8, 1, "char_id");
  PyArrayObject* action = require_contiguous_array_readonly(action_obj, NPY_UINT16, 1, "action_id");
  PyArrayObject* action_frame =
      require_contiguous_array_readonly(action_frame_obj, NPY_INT16, 1, "action_frame");
  PyArrayObject* anim_frame =
      require_contiguous_array_readonly(anim_frame_obj, NPY_FLOAT32, 1, "anim_frame_f32");
  PyArrayObject* pos_x = require_contiguous_array_readonly(pos_x_obj, NPY_FLOAT32, 1, "pos_x");
  PyArrayObject* pos_y = require_contiguous_array_readonly(pos_y_obj, NPY_FLOAT32, 1, "pos_y");
  PyArrayObject* pos_z = require_contiguous_array_readonly(pos_z_obj, NPY_FLOAT32, 1, "pos_z");
  PyArrayObject* facing = require_contiguous_array_readonly(facing_obj, NPY_UINT8, 1, "facing");
  PyArrayObject* percent =
      require_contiguous_array_readonly(percent_obj, NPY_FLOAT32, 1, "percent");
  PyArrayObject* shield = require_contiguous_array_readonly(shield_obj, NPY_FLOAT32, 1, "shield");
  PyArrayObject* stocks = require_contiguous_array_readonly(stocks_obj, NPY_UINT8, 1, "stocks");
  PyArrayObject* jumps = require_contiguous_array_readonly(jumps_obj, NPY_UINT8, 1, "jumps");
  PyArrayObject* on_ground =
      require_contiguous_array_readonly(on_ground_obj, NPY_UINT8, 1, "on_ground");
  PyArrayObject* hitlag = require_contiguous_array_readonly(hitlag_obj, NPY_UINT16, 1, "hitlag");
  PyArrayObject* hitstun = require_contiguous_array_readonly(hitstun_obj, NPY_UINT16, 1, "hitstun");
  PyArrayObject* lcancel = require_contiguous_array_readonly(lcancel_obj, NPY_UINT8, 1, "l_cancel");
  PyArrayObject* hurtbox =
      require_contiguous_array_readonly(hurtbox_obj, NPY_UINT8, 1, "hurtbox_state");
  PyArrayObject* ground = require_contiguous_array_readonly(ground_obj, NPY_UINT16, 1, "ground_id");
  PyArrayObject* animation_index =
      require_contiguous_array_readonly(animation_index_obj, NPY_UINT32, 1, "animation_index");
  PyArrayObject* instance_hit_by =
      require_contiguous_array_readonly(instance_hit_by_obj, NPY_UINT16, 1, "instance_hit_by");
  PyArrayObject* instance_id =
      require_contiguous_array_readonly(instance_id_obj, NPY_UINT16, 1, "instance_id");
  PyArrayObject* last_attack =
      require_contiguous_array_readonly(last_attack_obj, NPY_UINT8, 1, "last_attack_landed");
  PyArrayObject* combo_count =
      require_contiguous_array_readonly(combo_count_obj, NPY_UINT8, 1, "combo_count");
  PyArrayObject* last_hit_by =
      require_contiguous_array_readonly(last_hit_by_obj, NPY_UINT8, 1, "last_hit_by");
  PyArrayObject* state_flags =
      require_contiguous_array_readonly(state_flags_obj, NPY_UINT8, 2, "state_flags");
  PyArrayObject* speed_air_x =
      require_contiguous_array_readonly(speed_air_x_obj, NPY_FLOAT32, 1, "speed_air_x_self");
  PyArrayObject* speed_ground_x =
      require_contiguous_array_readonly(speed_ground_x_obj, NPY_FLOAT32, 1, "speed_ground_x_self");
  PyArrayObject* speed_y =
      require_contiguous_array_readonly(speed_y_obj, NPY_FLOAT32, 1, "speed_y_self");
  PyArrayObject* speed_x_attack =
      require_contiguous_array_readonly(speed_x_attack_obj, NPY_FLOAT32, 1, "speed_x_attack");
  PyArrayObject* speed_y_attack =
      require_contiguous_array_readonly(speed_y_attack_obj, NPY_FLOAT32, 1, "speed_y_attack");
  if (seed_arr == NULL || prev_arr == NULL || input_arr == NULL || ref_arr == NULL ||
      buttons == NULL || main_x == NULL || main_y == NULL || c_x == NULL || c_y == NULL ||
      l == NULL || r == NULL || ch == NULL || action == NULL || action_frame == NULL ||
      anim_frame == NULL || pos_x == NULL || pos_y == NULL || pos_z == NULL || facing == NULL ||
      percent == NULL || shield == NULL || stocks == NULL || jumps == NULL || on_ground == NULL ||
      hitlag == NULL || hitstun == NULL || lcancel == NULL || hurtbox == NULL || ground == NULL ||
      animation_index == NULL || instance_hit_by == NULL || instance_id == NULL ||
      last_attack == NULL || combo_count == NULL || last_hit_by == NULL || state_flags == NULL ||
      speed_air_x == NULL || speed_ground_x == NULL || speed_y == NULL || speed_x_attack == NULL ||
      speed_y_attack == NULL) {
    return NULL;
  }

  npy_intp n_frames = 0;
  if (validation_array_rows(action, &n_frames, "action_id") != 0) {
    return NULL;
  }
  if (n_frames < 2) {
    PyErr_SetString(PyExc_ValueError, "action_id must contain at least two frames");
    return NULL;
  }
#define CHECK_ROWS(ARR, NAME) validation_require_rows((ARR), n_frames, (NAME))
  if (CHECK_ROWS(buttons, "buttons") != 0 || CHECK_ROWS(main_x, "main_x") != 0 ||
      CHECK_ROWS(main_y, "main_y") != 0 || CHECK_ROWS(c_x, "c_x") != 0 ||
      CHECK_ROWS(c_y, "c_y") != 0 || CHECK_ROWS(l, "l") != 0 || CHECK_ROWS(r, "r") != 0 ||
      CHECK_ROWS(ch, "char_id") != 0 || CHECK_ROWS(action_frame, "action_frame") != 0 ||
      CHECK_ROWS(anim_frame, "anim_frame_f32") != 0 || CHECK_ROWS(pos_x, "pos_x") != 0 ||
      CHECK_ROWS(pos_y, "pos_y") != 0 || CHECK_ROWS(pos_z, "pos_z") != 0 ||
      CHECK_ROWS(facing, "facing") != 0 || CHECK_ROWS(percent, "percent") != 0 ||
      CHECK_ROWS(shield, "shield") != 0 || CHECK_ROWS(stocks, "stocks") != 0 ||
      CHECK_ROWS(jumps, "jumps") != 0 || CHECK_ROWS(on_ground, "on_ground") != 0 ||
      CHECK_ROWS(hitlag, "hitlag") != 0 || CHECK_ROWS(hitstun, "hitstun") != 0 ||
      CHECK_ROWS(lcancel, "l_cancel") != 0 || CHECK_ROWS(hurtbox, "hurtbox_state") != 0 ||
      CHECK_ROWS(ground, "ground_id") != 0 || CHECK_ROWS(animation_index, "animation_index") != 0 ||
      CHECK_ROWS(instance_hit_by, "instance_hit_by") != 0 ||
      CHECK_ROWS(instance_id, "instance_id") != 0 ||
      CHECK_ROWS(last_attack, "last_attack_landed") != 0 ||
      CHECK_ROWS(combo_count, "combo_count") != 0 || CHECK_ROWS(last_hit_by, "last_hit_by") != 0 ||
      CHECK_ROWS(speed_air_x, "speed_air_x_self") != 0 ||
      CHECK_ROWS(speed_ground_x, "speed_ground_x_self") != 0 ||
      CHECK_ROWS(speed_y, "speed_y_self") != 0 ||
      CHECK_ROWS(speed_x_attack, "speed_x_attack") != 0 ||
      CHECK_ROWS(speed_y_attack, "speed_y_attack") != 0) {
    return NULL;
  }
#undef CHECK_ROWS
  if (PyArray_NDIM(state_flags) != 2 || PyArray_DIM(state_flags, 0) != n_frames ||
      PyArray_DIM(state_flags, 1) < 5) {
    PyErr_SetString(PyExc_ValueError, "state_flags must be uint8[n_frames, >=5]");
    return NULL;
  }

  const npy_intp n_samples = n_frames - 1;
  if (slot < 0 || slot >= MSL_MAX_PLAYERS ||
      validation_require_u8_rows(seed_arr, n_samples, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      validation_require_u8_rows(prev_arr, n_samples, (npy_intp)sizeof(MslInput),
                                 "prev_input_u8") != 0 ||
      validation_require_u8_rows(input_arr, n_samples, (npy_intp)sizeof(MslInput), "input_u8") !=
          0 ||
      validation_require_u8_rows(ref_arr, n_samples, (npy_intp)sizeof(MslCompare), "ref_u8") != 0) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "slot out of range");
    }
    return NULL;
  }

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  uint8_t* prev_u8 = (uint8_t*)PyArray_DATA(prev_arr);
  uint8_t* input_u8 = (uint8_t*)PyArray_DATA(input_arr);
  uint8_t* ref_u8 = (uint8_t*)PyArray_DATA(ref_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t prev_stride = (size_t)PyArray_STRIDE(prev_arr, 0);
  const size_t input_stride = (size_t)PyArray_STRIDE(input_arr, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref_arr, 0);

  const uint16_t* buttons_v = (const uint16_t*)PyArray_DATA(buttons);
  const int8_t* main_x_v = (const int8_t*)PyArray_DATA(main_x);
  const int8_t* main_y_v = (const int8_t*)PyArray_DATA(main_y);
  const int8_t* c_x_v = (const int8_t*)PyArray_DATA(c_x);
  const int8_t* c_y_v = (const int8_t*)PyArray_DATA(c_y);
  const uint8_t* l_v = (const uint8_t*)PyArray_DATA(l);
  const uint8_t* r_v = (const uint8_t*)PyArray_DATA(r);
  const uint8_t* char_v = (const uint8_t*)PyArray_DATA(ch);
  const uint16_t* action_v = (const uint16_t*)PyArray_DATA(action);
  const int16_t* action_frame_v = (const int16_t*)PyArray_DATA(action_frame);
  const float* anim_frame_v = (const float*)PyArray_DATA(anim_frame);
  const float* pos_x_v = (const float*)PyArray_DATA(pos_x);
  const float* pos_y_v = (const float*)PyArray_DATA(pos_y);
  const float* pos_z_v = (const float*)PyArray_DATA(pos_z);
  const uint8_t* facing_v = (const uint8_t*)PyArray_DATA(facing);
  const float* percent_v = (const float*)PyArray_DATA(percent);
  const float* shield_v = (const float*)PyArray_DATA(shield);
  const uint8_t* stocks_v = (const uint8_t*)PyArray_DATA(stocks);
  const uint8_t* jumps_v = (const uint8_t*)PyArray_DATA(jumps);
  const uint8_t* on_ground_v = (const uint8_t*)PyArray_DATA(on_ground);
  const uint16_t* hitlag_v = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* hitstun_v = (const uint16_t*)PyArray_DATA(hitstun);
  const uint8_t* lcancel_v = (const uint8_t*)PyArray_DATA(lcancel);
  const uint8_t* hurtbox_v = (const uint8_t*)PyArray_DATA(hurtbox);
  const uint16_t* ground_v = (const uint16_t*)PyArray_DATA(ground);
  const uint32_t* animation_index_v = (const uint32_t*)PyArray_DATA(animation_index);
  const uint16_t* instance_hit_by_v = (const uint16_t*)PyArray_DATA(instance_hit_by);
  const uint16_t* instance_id_v = (const uint16_t*)PyArray_DATA(instance_id);
  const uint8_t* last_attack_v = (const uint8_t*)PyArray_DATA(last_attack);
  const uint8_t* combo_count_v = (const uint8_t*)PyArray_DATA(combo_count);
  const uint8_t* last_hit_by_v = (const uint8_t*)PyArray_DATA(last_hit_by);
  const uint8_t* state_flags_v = (const uint8_t*)PyArray_DATA(state_flags);
  const npy_intp state_flags_stride = PyArray_STRIDE(state_flags, 0);
  const float* speed_air_x_v = (const float*)PyArray_DATA(speed_air_x);
  const float* speed_ground_x_v = (const float*)PyArray_DATA(speed_ground_x);
  const float* speed_y_v = (const float*)PyArray_DATA(speed_y);
  const float* speed_x_attack_v = (const float*)PyArray_DATA(speed_x_attack);
  const float* speed_y_attack_v = (const float*)PyArray_DATA(speed_y_attack);
  for (npy_intp i = 0; i < n_samples; i++) {
    MslSeed* seed = (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride);
    MslInput* prev = (MslInput*)(void*)(prev_u8 + (size_t)i * prev_stride);
    MslInput* input = (MslInput*)(void*)(input_u8 + (size_t)i * input_stride);
    MslCompare* ref = (MslCompare*)(void*)(ref_u8 + (size_t)i * ref_stride);

    prev->p[slot].buttons = buttons_v[i];
    input->p[slot].buttons = buttons_v[i + 1];
    prev->p[slot].main_x = main_x_v[i];
    input->p[slot].main_x = main_x_v[i + 1];
    prev->p[slot].main_y = main_y_v[i];
    input->p[slot].main_y = main_y_v[i + 1];
    prev->p[slot].c_x = c_x_v[i];
    input->p[slot].c_x = c_x_v[i + 1];
    prev->p[slot].c_y = c_y_v[i];
    input->p[slot].c_y = c_y_v[i + 1];
    prev->p[slot].l = l_v[i];
    input->p[slot].l = l_v[i + 1];
    prev->p[slot].r = r_v[i];
    input->p[slot].r = r_v[i + 1];

    seed->char_id[slot] = char_v[i];
    ref->char_id[slot] = char_v[i + 1];
    seed->action_id[slot] = action_v[i];
    ref->action_id[slot] = action_v[i + 1];
    seed->action_frame[slot] = action_frame_v[i];
    ref->action_frame[slot] = action_frame_v[i + 1];
    seed->anim_frame_f32[slot] = anim_frame_v[i];
    seed->pos_x[slot] = pos_x_v[i];
    ref->pos_x[slot] = pos_x_v[i + 1];
    seed->pos_y[slot] = pos_y_v[i];
    ref->pos_y[slot] = pos_y_v[i + 1];
    seed->pos_z[slot] = pos_z_v[i];
    seed->facing[slot] = facing_v[i];
    ref->facing[slot] = facing_v[i + 1];
    seed->percent[slot] = percent_v[i];
    ref->percent[slot] = percent_v[i + 1];
    seed->shield_hp[slot] = shield_v[i];
    ref->shield_hp[slot] = shield_v[i + 1];
    seed->stocks[slot] = stocks_v[i];
    ref->stocks[slot] = stocks_v[i + 1];
    ref->is_dead[slot] = (stocks_v[i + 1] == 0) ? 1u : 0u;
    seed->jumps_left[slot] = jumps_v[i];
    ref->jumps_left[slot] = jumps_v[i + 1];
    seed->on_ground[slot] = on_ground_v[i];
    ref->on_ground[slot] = on_ground_v[i + 1];
    seed->hitlag[slot] = hitlag_v[i];
    ref->hitlag[slot] = hitlag_v[i + 1];
    seed->hitstun[slot] = hitstun_v[i];
    ref->hitstun[slot] = hitstun_v[i + 1];
    seed->l_cancel[slot] = lcancel_v[i];
    ref->l_cancel[slot] = lcancel_v[i + 1];
    seed->hurtbox_state[slot] = hurtbox_v[i];
    ref->hurtbox_state[slot] = hurtbox_v[i + 1];
    seed->ground_id[slot] = ground_v[i];
    ref->ground_id[slot] = ground_v[i + 1];
    seed->animation_index[slot] = animation_index_v[i];
    ref->animation_index[slot] = animation_index_v[i + 1];
    seed->instance_hit_by[slot] = instance_hit_by_v[i];
    ref->instance_hit_by[slot] = instance_hit_by_v[i + 1];
    seed->instance_id[slot] = instance_id_v[i];
    ref->instance_id[slot] = instance_id_v[i + 1];
    seed->last_attack_landed[slot] = last_attack_v[i];
    ref->last_attack_landed[slot] = last_attack_v[i + 1];
    seed->combo_count[slot] = combo_count_v[i];
    ref->combo_count[slot] = combo_count_v[i + 1];
    seed->last_hit_by[slot] = last_hit_by_v[i];
    ref->last_hit_by[slot] = last_hit_by_v[i + 1];
    for (int k = 0; k < 5; k++) {
      seed->state_flags[slot][k] =
          state_flags_v[(size_t)i * (size_t)state_flags_stride + (size_t)k];
      ref->state_flags[slot][k] =
          state_flags_v[(size_t)(i + 1) * (size_t)state_flags_stride + (size_t)k];
    }
    seed->speed_air_x_self[slot] = speed_air_x_v[i];
    ref->speed_air_x_self[slot] = speed_air_x_v[i + 1];
    seed->speed_ground_x_self[slot] = speed_ground_x_v[i];
    ref->speed_ground_x_self[slot] = speed_ground_x_v[i + 1];
    seed->speed_y_self[slot] = speed_y_v[i];
    ref->speed_y_self[slot] = speed_y_v[i + 1];
    seed->speed_x_attack[slot] = speed_x_attack_v[i];
    ref->speed_x_attack[slot] = speed_x_attack_v[i + 1];
    seed->speed_y_attack[slot] = speed_y_attack_v[i];
    ref->speed_y_attack[slot] = speed_y_attack_v[i + 1];
    seed->source_port0[slot] = (uint8_t)source_port0;
    seed->dmg_x2225_b7[slot] = (uint8_t)(dmg_x2225_b7 != 0);
    seed->dmg_x2224_b2[slot] = (uint8_t)(dmg_x2224_b2 != 0);
    seed->ground_friction_mul[slot] = 1.0f;

    // Teacher-forced floor sweeps seed CollData.prev_pos from the previous replay-visible row.
    float floor_prev_x = pos_x_v[i == 0 ? 0 : i - 1];
    float floor_prev_y = pos_y_v[i == 0 ? 0 : i - 1];
    seed->floor_sweep_prev_pos_x_f32[slot] = floor_prev_x;
    seed->floor_sweep_prev_pos_y_f32[slot] = floor_prev_y;
    seed->floor_sweep_prev_pos_valid_u8[slot] = 1;
  }
  Py_RETURN_NONE;
}
