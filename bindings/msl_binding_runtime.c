#include "msl_binding_runtime.h"

static void pymsl_release_sequence_buffers(PyMslHandle* h) {
  if (h == NULL) {
    return;
  }
  Py_CLEAR(h->rollout_action_obj);
  Py_CLEAR(h->rollout_viewpoint_obj);
  Py_CLEAR(h->rollout_gamestate_obj);
  Py_CLEAR(h->rollout_terminal_obj);
  Py_CLEAR(h->rollout_done_obj);
  Py_CLEAR(h->rollout_reset_mask_obj);
  h->rollout_horizon = 0;
  h->rollout_action_format = PYMSL_ACTION_FORMAT_NONE;
  h->rollout_action_bytes = NULL;
  h->rollout_action_frame_stride = 0u;
  h->rollout_action_batch_stride = 0u;
  h->rollout_viewpoint_bytes = NULL;
  h->rollout_viewpoint_stride = 0u;
  h->rollout_gamestate_bytes = NULL;
  h->rollout_gamestate_frame_stride = 0u;
  h->rollout_gamestate_batch_stride = 0u;
  h->rollout_terminal_bytes = NULL;
  h->rollout_terminal_frame_stride = 0u;
  h->rollout_terminal_batch_stride = 0u;
  h->rollout_done_bytes = NULL;
  h->rollout_done_frame_stride = 0u;
  h->rollout_done_batch_stride = 0u;
  h->rollout_reset_mask_bytes = NULL;
  h->rollout_reset_mask_frame_stride = 0u;
  h->rollout_reset_mask_batch_stride = 0u;
}

static void pymsl_release_bound_buffers(PyMslHandle* h) {
  if (h == NULL) {
    return;
  }
  Py_CLEAR(h->match_config_obj);
  Py_CLEAR(h->prev_input_obj);
  Py_CLEAR(h->input_obj);
  Py_CLEAR(h->compare_obj);
  Py_CLEAR(h->viewpoint_obj);
  Py_CLEAR(h->gamestate_obj);
  Py_CLEAR(h->terminal_obj);
  h->match_config_bytes = NULL;
  h->match_config_stride = 0u;
  h->prev_input_bytes = NULL;
  h->prev_input_stride = 0u;
  h->input_bytes = NULL;
  h->input_stride = 0u;
  h->compare_bytes = NULL;
  h->compare_stride = 0u;
  h->viewpoint_bytes = NULL;
  h->viewpoint_stride = 0u;
  h->gamestate_bytes = NULL;
  h->gamestate_stride = 0u;
  h->terminal_bytes = NULL;
  h->terminal_stride = 0u;
}

static void pymsl_capsule_destructor(PyObject* capsule) {
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(capsule, "msl.Handle");
  if (h == NULL) {
    return;
  }
  pymsl_release_bound_buffers(h);
  pymsl_release_sequence_buffers(h);
  if (h->batch) {
    msl_batch_destroy(h->batch);
    h->batch = NULL;
  }
  PyMem_Free(h->prev_input_storage);
  h->prev_input_storage = NULL;
  PyMem_Free(h->input_storage);
  h->input_storage = NULL;
  PyMem_Free(h);
}

PyArrayObject* require_contiguous_array(PyObject* obj, int typenum, int min_ndim,
                                        const char* name) {
  if (!PyObject_TypeCheck(obj, &PyArray_Type)) {
    PyErr_Format(PyExc_TypeError, "%s must be a NumPy array", name);
    return NULL;
  }
  PyArrayObject* arr = (PyArrayObject*)obj;
  if (!PyArray_ISCARRAY(arr)) {
    PyErr_Format(PyExc_ValueError, "%s must be contiguous C-order", name);
    return NULL;
  }
  if (PyArray_TYPE(arr) != typenum) {
    PyErr_Format(PyExc_ValueError, "%s has wrong dtype (expected typenum=%d)", name, typenum);
    return NULL;
  }
  if (PyArray_NDIM(arr) < min_ndim) {
    PyErr_Format(PyExc_ValueError, "%s must be at least %dD", name, min_ndim);
    return NULL;
  }
  return arr;
}

PyArrayObject* require_contiguous_array_readonly(PyObject* obj, int typenum, int min_ndim,
                                                 const char* name) {
  if (!PyObject_TypeCheck(obj, &PyArray_Type)) {
    PyErr_Format(PyExc_TypeError, "%s must be a NumPy array", name);
    return NULL;
  }
  PyArrayObject* arr = (PyArrayObject*)obj;
  if (!PyArray_ISCARRAY_RO(arr)) {
    PyErr_Format(PyExc_ValueError, "%s must be contiguous C-order", name);
    return NULL;
  }
  if (PyArray_TYPE(arr) != typenum) {
    PyErr_Format(PyExc_ValueError, "%s has wrong dtype (expected typenum=%d)", name, typenum);
    return NULL;
  }
  if (PyArray_NDIM(arr) < min_ndim) {
    PyErr_Format(PyExc_ValueError, "%s must be at least %dD", name, min_ndim);
    return NULL;
  }
  return arr;
}

int require_exact_2d_shape(PyArrayObject* arr, npy_intp rows, npy_intp cols, const char* name) {
  if (PyArray_NDIM(arr) != 2 || PyArray_DIM(arr, 0) != rows || PyArray_DIM(arr, 1) != cols) {
    PyErr_Format(PyExc_ValueError,
                 "%s must share exact [frames, players] shape: expected [%zd, %zd], got [%zd, %zd]",
                 name, (Py_ssize_t)rows, (Py_ssize_t)cols,
                 PyArray_NDIM(arr) >= 1 ? (Py_ssize_t)PyArray_DIM(arr, 0) : (Py_ssize_t)-1,
                 PyArray_NDIM(arr) >= 2 ? (Py_ssize_t)PyArray_DIM(arr, 1) : (Py_ssize_t)-1);
    return -1;
  }
  return 0;
}

static inline float pymsl_clamp_float(float x, float lo, float hi) {
  if (!isfinite(x)) {
    return lo;
  }
  if (x < lo) {
    return lo;
  }
  if (x > hi) {
    return hi;
  }
  return x;
}

static inline int8_t pymsl_libmelee_axis_to_i8(float x) {
  const float clamped = pymsl_clamp_float(x, 0.0f, 1.0f);
  const long raw = lrintf(clamped * 160.0f - 80.0f);
  if (raw < -128) {
    return -128;
  }
  if (raw > 127) {
    return 127;
  }
  return (int8_t)raw;
}

static inline uint8_t pymsl_libmelee_shoulder_to_u8(float x) {
  const float clamped = pymsl_clamp_float(x, 0.0f, 1.0f);
  const long raw = lrintf(clamped * 140.0f);
  if (raw < 0) {
    return 0u;
  }
  if (raw > 255) {
    return 255u;
  }
  return (uint8_t)raw;
}

static inline uint16_t pymsl_controller_buttons(const PyMslControllerPlayer* p) {
  uint16_t buttons = 0u;
  if (p->A) {
    buttons |= (uint16_t)MSL_BUTTON_A;
  }
  if (p->B) {
    buttons |= (uint16_t)MSL_BUTTON_B;
  }
  if (p->X) {
    buttons |= (uint16_t)MSL_BUTTON_X;
  }
  if (p->Y) {
    buttons |= (uint16_t)MSL_BUTTON_Y;
  }
  if (p->Z) {
    buttons |= (uint16_t)MSL_BUTTON_Z;
  }
  if (p->L) {
    buttons |= (uint16_t)MSL_BUTTON_L;
  }
  if (p->R) {
    buttons |= (uint16_t)MSL_BUTTON_R;
  }
  if (p->D_UP) {
    buttons |= (uint16_t)MSL_BUTTON_D_UP;
  }
  return buttons;
}

static void pymsl_controller_to_input(const PyMslControllerInput* src, MslInput* dst) {
  memset(dst, 0, sizeof(*dst));
  for (int p = 0; p < MSL_MAX_PLAYERS; p++) {
    const PyMslControllerPlayer* in = &src->p[p];
    dst->p[p].buttons = pymsl_controller_buttons(in);
    dst->p[p].main_x = pymsl_libmelee_axis_to_i8(in->main_stick_x);
    dst->p[p].main_y = pymsl_libmelee_axis_to_i8(in->main_stick_y);
    dst->p[p].c_x = pymsl_libmelee_axis_to_i8(in->c_stick_x);
    dst->p[p].c_y = pymsl_libmelee_axis_to_i8(in->c_stick_y);
    dst->p[p].l = pymsl_libmelee_shoulder_to_u8(in->shoulder);
    dst->p[p].r = 0u;
  }
}

PyObject* msl_init(PyObject* self, PyObject* args, PyObject* kwargs) {
  (void)self;
  static const char* kwlist[] = {
      "batch_size", "num_players", "ucf_enabled", "ucf_cardinals_1_0_enabled", NULL,
  };
  int batch_size = 0;
  int num_players = 0;
  int ucf_enabled = -1;
  int ucf_cardinals_1_0_enabled = -1;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ii|ii", (char**)kwlist, &batch_size, &num_players,
                                   &ucf_enabled, &ucf_cardinals_1_0_enabled)) {
    return NULL;
  }

  MslBatch* batch = msl_batch_create(batch_size, num_players);
  if (batch == NULL) {
    PyErr_SetString(PyExc_RuntimeError,
                    "msl_batch_create failed; see stderr for data loading details");
    return NULL;
  }
  if (ucf_enabled != -1) {
    const int err = msl_batch_set_ucf_enabled(batch, ucf_enabled != 0);
    if (err != 0) {
      msl_batch_destroy(batch);
      PyErr_Format(PyExc_RuntimeError, "msl_batch_set_ucf_enabled failed: %d", err);
      return NULL;
    }
  }
  if (ucf_cardinals_1_0_enabled != -1) {
    const int err = msl_batch_set_ucf_cardinals_1_0_enabled(batch, ucf_cardinals_1_0_enabled != 0);
    if (err != 0) {
      msl_batch_destroy(batch);
      PyErr_Format(PyExc_RuntimeError, "msl_batch_set_ucf_cardinals_1_0_enabled failed: %d", err);
      return NULL;
    }
  }

  PyMslHandle* h = (PyMslHandle*)PyMem_Calloc(1u, sizeof(PyMslHandle));
  if (h == NULL) {
    msl_batch_destroy(batch);
    PyErr_NoMemory();
    return NULL;
  }
  h->batch = batch;
  h->prev_input_storage_stride = sizeof(MslInput);
  h->prev_input_storage = (uint8_t*)PyMem_Calloc((size_t)batch_size, h->prev_input_storage_stride);
  h->input_storage_stride = sizeof(MslInput);
  h->input_storage = (uint8_t*)PyMem_Calloc((size_t)batch_size, h->input_storage_stride);
  if (h->prev_input_storage == NULL || h->input_storage == NULL) {
    msl_batch_destroy(h->batch);
    h->batch = NULL;
    PyMem_Free(h->prev_input_storage);
    PyMem_Free(h->input_storage);
    PyMem_Free(h);
    PyErr_NoMemory();
    return NULL;
  }

  PyObject* capsule = PyCapsule_New(h, "msl.Handle", pymsl_capsule_destructor);
  if (capsule == NULL) {
    // PyCapsule_New sets an exception on failure. We must clean up manually here because the capsule
    // was never created (calling the capsule destructor with NULL would be a bug).
    msl_batch_destroy(h->batch);
    h->batch = NULL;
    PyMem_Free(h->prev_input_storage);
    h->prev_input_storage = NULL;
    PyMem_Free(h->input_storage);
    h->input_storage = NULL;
    PyMem_Free(h);
    return NULL;
  }
  return capsule;
}

PyMslHandle* unpack_handle(PyObject* handle_obj) {
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(handle_obj, "msl.Handle");
  if (h == NULL || h->batch == NULL) {
    PyErr_SetString(PyExc_ValueError, "invalid msl handle");
    return NULL;
  }
  return h;
}

PyObject* msl_reseed_seed(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* seed_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &seed_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* seed = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed");
  if (seed == NULL) {
    return NULL;
  }
  if (PyArray_DIM(seed, 1) < (npy_intp)sizeof(MslSeed)) {
    PyErr_SetString(PyExc_ValueError, "seed second dim too small for MslSeed");
    return NULL;
  }

  const uint8_t* seed_bytes = (const uint8_t*)PyArray_DATA(seed);
  const size_t stride = (size_t)PyArray_STRIDE(seed, 0);

  const int err = msl_batch_reseed_seed(h->batch, seed_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_reseed_seed failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

PyObject* msl_reseed_seed_rollout(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* seed_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &seed_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* seed = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed");
  if (seed == NULL) {
    return NULL;
  }
  if (PyArray_DIM(seed, 1) < (npy_intp)sizeof(MslSeed)) {
    PyErr_SetString(PyExc_ValueError, "seed second dim too small for MslSeed");
    return NULL;
  }

  const uint8_t* seed_bytes = (const uint8_t*)PyArray_DATA(seed);
  const size_t stride = (size_t)PyArray_STRIDE(seed, 0);

  const int err = msl_batch_reseed_seed_rollout(h->batch, seed_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_reseed_seed_rollout failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

PyObject* msl_apply_replay_frame_rng(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* seed_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &seed_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* seed = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed");
  if (seed == NULL) {
    return NULL;
  }
  if (PyArray_DIM(seed, 1) < (npy_intp)sizeof(MslSeed)) {
    PyErr_SetString(PyExc_ValueError, "seed second dim too small for MslSeed");
    return NULL;
  }

  const uint8_t* seed_bytes = (const uint8_t*)PyArray_DATA(seed);
  const size_t stride = (size_t)PyArray_STRIDE(seed, 0);

  const int err = msl_batch_apply_replay_frame_rng(h->batch, seed_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_apply_replay_frame_rng failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

PyObject* msl_init_match(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* config_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &config_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* config = require_contiguous_array(config_obj, NPY_UINT8, 2, "match_config");
  if (config == NULL) {
    return NULL;
  }
  if (PyArray_DIM(config, 1) < (npy_intp)sizeof(MslMatchConfig)) {
    PyErr_SetString(PyExc_ValueError, "match_config second dim too small for MslMatchConfig");
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);
  if (PyArray_DIM(config, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "match_config has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(config, 0), batch_size);
    return NULL;
  }

  const uint8_t* config_bytes = (const uint8_t*)PyArray_DATA(config);
  const size_t stride = (size_t)PyArray_STRIDE(config, 0);

  const int err = msl_batch_init_match(h->batch, config_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_init_match failed: %d", err);
    return NULL;
  }
  memset(h->prev_input_storage, 0,
         (size_t)msl_batch_batch_size(h->batch) * h->prev_input_storage_stride);
  memset(h->input_storage, 0, (size_t)msl_batch_batch_size(h->batch) * h->input_storage_stride);

  Py_RETURN_NONE;
}

PyObject* msl_init_match_masked(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* config_obj = NULL;
  PyObject* mask_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &handle_obj, &config_obj, &mask_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* config = require_contiguous_array(config_obj, NPY_UINT8, 2, "match_config");
  if (config == NULL) {
    return NULL;
  }
  if (PyArray_DIM(config, 1) < (npy_intp)sizeof(MslMatchConfig)) {
    PyErr_SetString(PyExc_ValueError, "match_config second dim too small for MslMatchConfig");
    return NULL;
  }
  PyArrayObject* mask = require_contiguous_array(mask_obj, NPY_UINT8, 1, "mask");
  if (mask == NULL) {
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);
  if (PyArray_DIM(config, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "match_config has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(config, 0), batch_size);
    return NULL;
  }
  if (PyArray_DIM(mask, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "mask has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(mask, 0), batch_size);
    return NULL;
  }

  const uint8_t* config_bytes = (const uint8_t*)PyArray_DATA(config);
  const size_t config_stride = (size_t)PyArray_STRIDE(config, 0);
  const uint8_t* mask_bytes = (const uint8_t*)PyArray_DATA(mask);
  const size_t mask_stride = (size_t)PyArray_STRIDE(mask, 0);

  const int err =
      msl_batch_init_match_masked(h->batch, config_bytes, config_stride, mask_bytes, mask_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_init_match_masked failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}
PyObject* msl_step_input(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* prev_input_obj = NULL;
  PyObject* input_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &handle_obj, &prev_input_obj, &input_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* prev_input = require_contiguous_array(prev_input_obj, NPY_UINT8, 2, "prev_input");
  if (prev_input == NULL) {
    return NULL;
  }
  PyArrayObject* input = require_contiguous_array(input_obj, NPY_UINT8, 2, "input");
  if (input == NULL) {
    return NULL;
  }

  if (PyArray_DIM(prev_input, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(input, 1) < (npy_intp)sizeof(MslInput)) {
    PyErr_SetString(PyExc_ValueError, "input second dim too small for MslInput");
    return NULL;
  }

  const uint8_t* prev_bytes = (const uint8_t*)PyArray_DATA(prev_input);
  const uint8_t* in_bytes = (const uint8_t*)PyArray_DATA(input);
  const size_t prev_stride = (size_t)PyArray_STRIDE(prev_input, 0);
  const size_t in_stride = (size_t)PyArray_STRIDE(input, 0);

  const int err = msl_batch_step_input(h->batch, prev_bytes, prev_stride, in_bytes, in_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_step_input failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

PyObject* msl_step_input_replay_frame_rng(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* seed_obj = NULL;
  PyObject* prev_input_obj = NULL;
  PyObject* input_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOO", &handle_obj, &seed_obj, &prev_input_obj, &input_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* seed = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed");
  if (seed == NULL) {
    return NULL;
  }
  PyArrayObject* prev_input = require_contiguous_array(prev_input_obj, NPY_UINT8, 2, "prev_input");
  if (prev_input == NULL) {
    return NULL;
  }
  PyArrayObject* input = require_contiguous_array(input_obj, NPY_UINT8, 2, "input");
  if (input == NULL) {
    return NULL;
  }

  if (PyArray_DIM(seed, 1) < (npy_intp)sizeof(MslSeed)) {
    PyErr_SetString(PyExc_ValueError, "seed second dim too small for MslSeed");
    return NULL;
  }
  if (PyArray_DIM(prev_input, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(input, 1) < (npy_intp)sizeof(MslInput)) {
    PyErr_SetString(PyExc_ValueError, "input second dim too small for MslInput");
    return NULL;
  }

  const uint8_t* seed_bytes = (const uint8_t*)PyArray_DATA(seed);
  const uint8_t* prev_bytes = (const uint8_t*)PyArray_DATA(prev_input);
  const uint8_t* in_bytes = (const uint8_t*)PyArray_DATA(input);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed, 0);
  const size_t prev_stride = (size_t)PyArray_STRIDE(prev_input, 0);
  const size_t in_stride = (size_t)PyArray_STRIDE(input, 0);

  const int err = msl_batch_step_input_replay_frame_rng(
      h->batch, seed_bytes, seed_stride, prev_bytes, prev_stride, in_bytes, in_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_step_input_replay_frame_rng failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}
PyObject* msl_write_compare(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* out_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &handle_obj, &out_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslCompare)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslCompare");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_write_compare(h->batch, out_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_write_compare failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static int require_batch_rows(PyArrayObject* arr, int batch_size, const char* name) {
  if (PyArray_DIM(arr, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "%s has too few rows: got %zd, need %d", name,
                 (Py_ssize_t)PyArray_DIM(arr, 0), batch_size);
    return -1;
  }
  return 0;
}

PyObject* msl_bind_buffers(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* match_config_obj = NULL;
  PyObject* prev_input_obj = NULL;
  PyObject* input_obj = NULL;
  PyObject* compare_obj = NULL;
  PyObject* viewpoint_obj = Py_None;
  PyObject* gamestate_obj = Py_None;
  PyObject* terminal_obj = Py_None;
  if (!PyArg_ParseTuple(args, "OOOOO|OOO", &handle_obj, &match_config_obj, &prev_input_obj,
                        &input_obj, &compare_obj, &viewpoint_obj, &gamestate_obj, &terminal_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  pymsl_release_sequence_buffers(h);
  const int batch_size = msl_batch_batch_size(h->batch);

  PyArrayObject* match_config =
      require_contiguous_array(match_config_obj, NPY_UINT8, 2, "match_config");
  if (match_config == NULL || require_batch_rows(match_config, batch_size, "match_config") != 0) {
    return NULL;
  }
  if (PyArray_DIM(match_config, 1) < (npy_intp)sizeof(MslMatchConfig)) {
    PyErr_SetString(PyExc_ValueError, "match_config second dim too small for MslMatchConfig");
    return NULL;
  }

  PyArrayObject* prev_input = require_contiguous_array(prev_input_obj, NPY_UINT8, 2, "prev_input");
  if (prev_input == NULL || require_batch_rows(prev_input, batch_size, "prev_input") != 0) {
    return NULL;
  }
  PyArrayObject* input = require_contiguous_array(input_obj, NPY_UINT8, 2, "input");
  if (input == NULL || require_batch_rows(input, batch_size, "input") != 0) {
    return NULL;
  }
  if (PyArray_DIM(prev_input, 1) < (npy_intp)sizeof(MslInput) ||
      PyArray_DIM(input, 1) < (npy_intp)sizeof(MslInput)) {
    PyErr_SetString(PyExc_ValueError, "input second dim too small for MslInput");
    return NULL;
  }

  PyArrayObject* compare = require_contiguous_array(compare_obj, NPY_UINT8, 2, "compare");
  if (compare == NULL || require_batch_rows(compare, batch_size, "compare") != 0) {
    return NULL;
  }
  if (PyArray_DIM(compare, 1) < (npy_intp)sizeof(MslCompare)) {
    PyErr_SetString(PyExc_ValueError, "compare second dim too small for MslCompare");
    return NULL;
  }

  PyArrayObject* viewpoint = NULL;
  if (viewpoint_obj != Py_None) {
    viewpoint = require_contiguous_array(viewpoint_obj, NPY_UINT8, 1, "viewpoint");
    if (viewpoint == NULL || require_batch_rows(viewpoint, batch_size, "viewpoint") != 0) {
      return NULL;
    }
  }

  PyArrayObject* gamestate = NULL;
  if (gamestate_obj != Py_None) {
    gamestate = require_contiguous_array(gamestate_obj, NPY_UINT8, 2, "gamestate");
    if (gamestate == NULL || require_batch_rows(gamestate, batch_size, "gamestate") != 0) {
      return NULL;
    }
    if (PyArray_DIM(gamestate, 1) < (npy_intp)sizeof(MeleeGamestate)) {
      PyErr_SetString(PyExc_ValueError, "gamestate second dim too small for MeleeGamestate");
      return NULL;
    }
  }

  PyArrayObject* terminal = NULL;
  if (terminal_obj != Py_None) {
    terminal = require_contiguous_array(terminal_obj, NPY_UINT8, 2, "terminal");
    if (terminal == NULL || require_batch_rows(terminal, batch_size, "terminal") != 0) {
      return NULL;
    }
    if (PyArray_DIM(terminal, 1) < (npy_intp)sizeof(MslTerminal)) {
      PyErr_SetString(PyExc_ValueError, "terminal second dim too small for MslTerminal");
      return NULL;
    }
  }

  pymsl_release_bound_buffers(h);

  Py_INCREF(match_config_obj);
  h->match_config_obj = match_config_obj;
  h->match_config_bytes = (const uint8_t*)PyArray_DATA(match_config);
  h->match_config_stride = (size_t)PyArray_STRIDE(match_config, 0);

  Py_INCREF(prev_input_obj);
  h->prev_input_obj = prev_input_obj;
  h->prev_input_bytes = (const uint8_t*)PyArray_DATA(prev_input);
  h->prev_input_stride = (size_t)PyArray_STRIDE(prev_input, 0);

  Py_INCREF(input_obj);
  h->input_obj = input_obj;
  h->input_bytes = (const uint8_t*)PyArray_DATA(input);
  h->input_stride = (size_t)PyArray_STRIDE(input, 0);

  Py_INCREF(compare_obj);
  h->compare_obj = compare_obj;
  h->compare_bytes = (uint8_t*)PyArray_DATA(compare);
  h->compare_stride = (size_t)PyArray_STRIDE(compare, 0);

  if (viewpoint_obj != Py_None) {
    Py_INCREF(viewpoint_obj);
    h->viewpoint_obj = viewpoint_obj;
    h->viewpoint_bytes = (const uint8_t*)PyArray_DATA(viewpoint);
    h->viewpoint_stride = (size_t)PyArray_STRIDE(viewpoint, 0);
  }
  if (gamestate_obj != Py_None) {
    Py_INCREF(gamestate_obj);
    h->gamestate_obj = gamestate_obj;
    h->gamestate_bytes = (uint8_t*)PyArray_DATA(gamestate);
    h->gamestate_stride = (size_t)PyArray_STRIDE(gamestate, 0);
  }
  if (terminal_obj != Py_None) {
    Py_INCREF(terminal_obj);
    h->terminal_obj = terminal_obj;
    h->terminal_bytes = (uint8_t*)PyArray_DATA(terminal);
    h->terminal_stride = (size_t)PyArray_STRIDE(terminal, 0);
  }

  Py_RETURN_NONE;
}

PyObject* msl_unbind_buffers(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  pymsl_release_bound_buffers(h);
  pymsl_release_sequence_buffers(h);
  Py_RETURN_NONE;
}

static int require_sequence_rows(PyArrayObject* arr, int length, int batch_size, const char* name) {
  if (PyArray_NDIM(arr) != 3 || PyArray_DIM(arr, 0) < (npy_intp)length ||
      PyArray_DIM(arr, 1) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError,
                 "%s must have shape [length, batch, bytes] with at least [%d, %d, ...]", name,
                 length, batch_size);
    return -1;
  }
  return 0;
}

PyObject* msl_bind_sequence_buffers(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* match_config_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* compare_obj = NULL;
  PyObject* viewpoint_obj = NULL;
  PyObject* gamestate_obj = NULL;
  PyObject* terminal_obj = NULL;
  PyObject* done_obj = NULL;
  PyObject* reset_mask_obj = NULL;
  const char* action_format = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOs", &handle_obj, &match_config_obj, &action_obj,
                        &compare_obj, &viewpoint_obj, &gamestate_obj, &terminal_obj, &done_obj,
                        &reset_mask_obj, &action_format)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);

  PyArrayObject* match_config =
      require_contiguous_array(match_config_obj, NPY_UINT8, 2, "match_config");
  if (match_config == NULL || require_batch_rows(match_config, batch_size, "match_config") != 0) {
    return NULL;
  }
  if (PyArray_DIM(match_config, 1) < (npy_intp)sizeof(MslMatchConfig)) {
    PyErr_SetString(PyExc_ValueError, "match_config second dim too small for MslMatchConfig");
    return NULL;
  }

  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT8, 3, "action");
  if (action == NULL) {
    return NULL;
  }
  const int horizon = (int)PyArray_DIM(action, 0);
  if (horizon <= 0 || require_sequence_rows(action, horizon, batch_size, "action") != 0) {
    return NULL;
  }
  int action_format_id = PYMSL_ACTION_FORMAT_NONE;
  size_t action_row_size = 0u;
  if (strcmp(action_format, "raw") == 0) {
    action_format_id = PYMSL_ACTION_FORMAT_RAW;
    action_row_size = sizeof(MslInput);
  } else if (strcmp(action_format, "controller") == 0) {
    action_format_id = PYMSL_ACTION_FORMAT_CONTROLLER;
    action_row_size = sizeof(PyMslControllerInput);
  } else {
    PyErr_SetString(PyExc_ValueError, "action_format must be 'raw' or 'controller'");
    return NULL;
  }
  if (PyArray_DIM(action, 2) < (npy_intp)action_row_size) {
    PyErr_Format(PyExc_ValueError, "action third dim too small for %s action", action_format);
    return NULL;
  }

  PyArrayObject* compare = require_contiguous_array(compare_obj, NPY_UINT8, 2, "compare");
  if (compare == NULL || require_batch_rows(compare, batch_size, "compare") != 0) {
    return NULL;
  }
  if (PyArray_DIM(compare, 1) < (npy_intp)sizeof(MslCompare)) {
    PyErr_SetString(PyExc_ValueError, "compare second dim too small for MslCompare");
    return NULL;
  }

  PyArrayObject* viewpoint = require_contiguous_array(viewpoint_obj, NPY_UINT8, 1, "viewpoint");
  if (viewpoint == NULL || require_batch_rows(viewpoint, batch_size, "viewpoint") != 0) {
    return NULL;
  }

  PyArrayObject* gamestate = require_contiguous_array(gamestate_obj, NPY_UINT8, 3, "gamestate");
  if (gamestate == NULL ||
      require_sequence_rows(gamestate, horizon + 1, batch_size, "gamestate") != 0) {
    return NULL;
  }
  if (PyArray_DIM(gamestate, 2) < (npy_intp)sizeof(MeleeGamestate)) {
    PyErr_SetString(PyExc_ValueError, "gamestate third dim too small for MeleeGamestate");
    return NULL;
  }

  PyArrayObject* terminal = require_contiguous_array(terminal_obj, NPY_UINT8, 3, "terminal");
  if (terminal == NULL || require_sequence_rows(terminal, horizon, batch_size, "terminal") != 0) {
    return NULL;
  }
  if (PyArray_DIM(terminal, 2) < (npy_intp)sizeof(MslTerminal)) {
    PyErr_SetString(PyExc_ValueError, "terminal third dim too small for MslTerminal");
    return NULL;
  }

  PyArrayObject* done = require_contiguous_array(done_obj, NPY_UINT8, 2, "done");
  if (done == NULL || require_exact_2d_shape(done, horizon, batch_size, "done") != 0) {
    return NULL;
  }

  PyArrayObject* reset_mask = require_contiguous_array(reset_mask_obj, NPY_UINT8, 2, "reset_mask");
  if (reset_mask == NULL ||
      require_exact_2d_shape(reset_mask, horizon, batch_size, "reset_mask") != 0) {
    return NULL;
  }

  pymsl_release_bound_buffers(h);
  pymsl_release_sequence_buffers(h);

  Py_INCREF(match_config_obj);
  h->match_config_obj = match_config_obj;
  h->match_config_bytes = (const uint8_t*)PyArray_DATA(match_config);
  h->match_config_stride = (size_t)PyArray_STRIDE(match_config, 0);

  Py_INCREF(compare_obj);
  h->compare_obj = compare_obj;
  h->compare_bytes = (uint8_t*)PyArray_DATA(compare);
  h->compare_stride = (size_t)PyArray_STRIDE(compare, 0);

  Py_INCREF(action_obj);
  h->rollout_action_obj = action_obj;
  h->rollout_horizon = horizon;
  h->rollout_action_format = action_format_id;
  h->rollout_action_bytes = (const uint8_t*)PyArray_DATA(action);
  h->rollout_action_frame_stride = (size_t)PyArray_STRIDE(action, 0);
  h->rollout_action_batch_stride = (size_t)PyArray_STRIDE(action, 1);

  Py_INCREF(viewpoint_obj);
  h->rollout_viewpoint_obj = viewpoint_obj;
  h->rollout_viewpoint_bytes = (const uint8_t*)PyArray_DATA(viewpoint);
  h->rollout_viewpoint_stride = (size_t)PyArray_STRIDE(viewpoint, 0);

  Py_INCREF(gamestate_obj);
  h->rollout_gamestate_obj = gamestate_obj;
  h->rollout_gamestate_bytes = (uint8_t*)PyArray_DATA(gamestate);
  h->rollout_gamestate_frame_stride = (size_t)PyArray_STRIDE(gamestate, 0);
  h->rollout_gamestate_batch_stride = (size_t)PyArray_STRIDE(gamestate, 1);

  Py_INCREF(terminal_obj);
  h->rollout_terminal_obj = terminal_obj;
  h->rollout_terminal_bytes = (uint8_t*)PyArray_DATA(terminal);
  h->rollout_terminal_frame_stride = (size_t)PyArray_STRIDE(terminal, 0);
  h->rollout_terminal_batch_stride = (size_t)PyArray_STRIDE(terminal, 1);

  Py_INCREF(done_obj);
  h->rollout_done_obj = done_obj;
  h->rollout_done_bytes = (uint8_t*)PyArray_DATA(done);
  h->rollout_done_frame_stride = (size_t)PyArray_STRIDE(done, 0);
  h->rollout_done_batch_stride = (size_t)PyArray_STRIDE(done, 1);

  Py_INCREF(reset_mask_obj);
  h->rollout_reset_mask_obj = reset_mask_obj;
  h->rollout_reset_mask_bytes = (const uint8_t*)PyArray_DATA(reset_mask);
  h->rollout_reset_mask_frame_stride = (size_t)PyArray_STRIDE(reset_mask, 0);
  h->rollout_reset_mask_batch_stride = (size_t)PyArray_STRIDE(reset_mask, 1);

  memset(h->prev_input_storage, 0, (size_t)batch_size * h->prev_input_storage_stride);
  memset(h->input_storage, 0, (size_t)batch_size * h->input_storage_stride);

  Py_RETURN_NONE;
}

PyObject* msl_unbind_sequence_buffers(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  pymsl_release_bound_buffers(h);
  pymsl_release_sequence_buffers(h);
  Py_RETURN_NONE;
}

static int msl_require_sequence_buffers(PyMslHandle* h) {
  if (h->match_config_bytes == NULL || h->rollout_action_bytes == NULL ||
      h->prev_input_storage == NULL || h->input_storage == NULL || h->rollout_done_bytes == NULL ||
      h->rollout_reset_mask_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "sequence buffers are not bound");
    return -1;
  }
  return 0;
}

static int msl_require_sequence_frame(PyMslHandle* h, int frame) {
  if (frame < 0 || frame >= h->rollout_horizon) {
    PyErr_Format(PyExc_IndexError, "sequence frame %d is outside length %d", frame,
                 h->rollout_horizon);
    return -1;
  }
  return 0;
}

static const uint8_t* pymsl_prepare_sequence_input_frame(PyMslHandle* h, int frame) {
  const int batch_size = msl_batch_batch_size(h->batch);
  const uint8_t* action_frame =
      h->rollout_action_bytes + (size_t)frame * h->rollout_action_frame_stride;
  if (h->rollout_action_format == PYMSL_ACTION_FORMAT_RAW) {
    return action_frame;
  }
  if (h->rollout_action_format == PYMSL_ACTION_FORMAT_CONTROLLER) {
    for (int bi = 0; bi < batch_size; bi++) {
      const PyMslControllerInput* src =
          (const PyMslControllerInput*)(action_frame + (size_t)bi * h->rollout_action_batch_stride);
      MslInput* dst = (MslInput*)(h->input_storage + (size_t)bi * h->input_storage_stride);
      pymsl_controller_to_input(src, dst);
    }
    return h->input_storage;
  }
  PyErr_SetString(PyExc_ValueError, "unsupported sequence action format");
  return NULL;
}

static size_t pymsl_prepared_sequence_input_stride(PyMslHandle* h) {
  return h->rollout_action_format == PYMSL_ACTION_FORMAT_RAW ? h->rollout_action_batch_stride
                                                             : h->input_storage_stride;
}

static void pymsl_copy_input_frame_to_prev(PyMslHandle* h, const uint8_t* input_frame,
                                           size_t input_stride) {
  const int batch_size = msl_batch_batch_size(h->batch);
  for (int bi = 0; bi < batch_size; bi++) {
    memcpy(h->prev_input_storage + (size_t)bi * h->prev_input_storage_stride,
           input_frame + (size_t)bi * input_stride, sizeof(MslInput));
  }
}

static void pymsl_clear_prev_input_masked(PyMslHandle* h, const uint8_t* mask_bytes,
                                          size_t mask_stride) {
  const int batch_size = msl_batch_batch_size(h->batch);
  for (int bi = 0; bi < batch_size; bi++) {
    if (*(const uint8_t*)(mask_bytes + (size_t)bi * mask_stride) != 0u) {
      memset(h->prev_input_storage + (size_t)bi * h->prev_input_storage_stride, 0,
             h->prev_input_storage_stride);
    }
  }
}

PyObject* msl_reset_prev_input(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);
  memset(h->prev_input_storage, 0, (size_t)batch_size * h->prev_input_storage_stride);
  Py_RETURN_NONE;
}

PyObject* msl_set_prev_input_from_sequence(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int frame = 0;
  if (!PyArg_ParseTuple(args, "Oi", &handle_obj, &frame)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_sequence_buffers(h) != 0 ||
      msl_require_sequence_frame(h, frame) != 0) {
    return NULL;
  }
  const uint8_t* input_frame =
      h->rollout_action_bytes + (size_t)frame * h->rollout_action_frame_stride;
  if (h->rollout_action_format == PYMSL_ACTION_FORMAT_CONTROLLER) {
    input_frame = pymsl_prepare_sequence_input_frame(h, frame);
    if (input_frame == NULL) {
      return NULL;
    }
    pymsl_copy_input_frame_to_prev(h, input_frame, h->input_storage_stride);
  } else {
    pymsl_copy_input_frame_to_prev(h, input_frame, h->rollout_action_batch_stride);
  }
  Py_RETURN_NONE;
}

static int msl_require_bound_step_buffers(PyMslHandle* h) {
  if (h->prev_input_bytes == NULL || h->input_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "input buffers are not bound");
    return -1;
  }
  return 0;
}

PyObject* msl_init_match_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (h->match_config_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "match_config buffer is not bound");
    return NULL;
  }
  int err = 0;
  PyThreadState* py_thread_state = PyEval_SaveThread();
  err = msl_batch_init_match(h->batch, h->match_config_bytes, h->match_config_stride);
  PyEval_RestoreThread(py_thread_state);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_init_match failed: %d", err);
    return NULL;
  }
  memset(h->prev_input_storage, 0,
         (size_t)msl_batch_batch_size(h->batch) * h->prev_input_storage_stride);
  memset(h->input_storage, 0, (size_t)msl_batch_batch_size(h->batch) * h->input_storage_stride);
  Py_RETURN_NONE;
}

PyObject* msl_init_match_sequence_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_sequence_buffers(h) != 0) {
    return NULL;
  }
  int err = msl_batch_init_match(h->batch, h->match_config_bytes, h->match_config_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_init_match failed: %d", err);
    return NULL;
  }
  memset(h->prev_input_storage, 0,
         (size_t)msl_batch_batch_size(h->batch) * h->prev_input_storage_stride);
  memset(h->input_storage, 0, (size_t)msl_batch_batch_size(h->batch) * h->input_storage_stride);
  if (h->rollout_gamestate_bytes != NULL) {
    err = melee_batch_write_gamestate(h->batch, h->rollout_viewpoint_bytes,
                                      h->rollout_viewpoint_stride, h->rollout_gamestate_bytes,
                                      h->rollout_gamestate_batch_stride);
    if (err != 0) {
      PyErr_Format(PyExc_RuntimeError, "melee_batch_write_gamestate failed: %d", err);
      return NULL;
    }
  }
  Py_RETURN_NONE;
}

PyObject* msl_reset_sequence_masked(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int frame = 0;
  int write_initial_observation = 1;
  if (!PyArg_ParseTuple(args, "Oi|i", &handle_obj, &frame, &write_initial_observation)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_sequence_buffers(h) != 0 ||
      msl_require_sequence_frame(h, frame) != 0) {
    return NULL;
  }
  const uint8_t* mask_frame =
      h->rollout_reset_mask_bytes + (size_t)frame * h->rollout_reset_mask_frame_stride;
  int err = msl_batch_init_match_masked(h->batch, h->match_config_bytes, h->match_config_stride,
                                        mask_frame, h->rollout_reset_mask_batch_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_init_match_masked failed: %d", err);
    return NULL;
  }
  pymsl_clear_prev_input_masked(h, mask_frame, h->rollout_reset_mask_batch_stride);
  if (write_initial_observation != 0) {
    uint8_t* obs_frame =
        h->rollout_gamestate_bytes + (size_t)frame * h->rollout_gamestate_frame_stride;
    err = melee_batch_write_gamestate(h->batch, h->rollout_viewpoint_bytes,
                                      h->rollout_viewpoint_stride, obs_frame,
                                      h->rollout_gamestate_batch_stride);
    if (err != 0) {
      PyErr_Format(PyExc_RuntimeError, "melee_batch_write_gamestate failed: %d", err);
      return NULL;
    }
  }
  Py_RETURN_NONE;
}

PyObject* msl_step_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_bound_step_buffers(h) != 0) {
    return NULL;
  }
  int err = 0;
  err = msl_batch_step_input(h->batch, h->prev_input_bytes, h->prev_input_stride, h->input_bytes,
                             h->input_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_step_input failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_write_compare_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (h->compare_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "compare buffer is not bound");
    return NULL;
  }
  int err = 0;
  PyThreadState* py_thread_state = PyEval_SaveThread();
  err = msl_batch_write_compare(h->batch, h->compare_bytes, h->compare_stride);
  PyEval_RestoreThread(py_thread_state);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_write_compare failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_step_write_compare_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_bound_step_buffers(h) != 0) {
    return NULL;
  }
  if (h->compare_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "compare buffer is not bound");
    return NULL;
  }
  int err = 0;
  err = msl_batch_step_input(h->batch, h->prev_input_bytes, h->prev_input_stride, h->input_bytes,
                             h->input_stride);
  if (err == 0) {
    err = msl_batch_write_compare(h->batch, h->compare_bytes, h->compare_stride);
  }
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch step/write_compare failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_step_sequence(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int frame = 0;
  int write_outputs = 1;
  int write_compare = 0;
  int max_frame_id = -1;
  if (!PyArg_ParseTuple(args, "Oi|iii", &handle_obj, &frame, &write_outputs, &write_compare,
                        &max_frame_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL || msl_require_sequence_buffers(h) != 0 ||
      msl_require_sequence_frame(h, frame) != 0) {
    return NULL;
  }

  const uint8_t* input_frame = pymsl_prepare_sequence_input_frame(h, frame);
  if (input_frame == NULL) {
    return NULL;
  }
  const size_t input_stride = pymsl_prepared_sequence_input_stride(h);
  int err = msl_batch_step_input(h->batch, h->prev_input_storage, h->prev_input_storage_stride,
                                 input_frame, input_stride);
  if (err == 0) {
    pymsl_copy_input_frame_to_prev(h, input_frame, input_stride);
  }
  if (err == 0 && write_outputs != 0) {
    uint8_t* obs_frame =
        h->rollout_gamestate_bytes + (size_t)(frame + 1) * h->rollout_gamestate_frame_stride;
    err = melee_batch_write_gamestate(h->batch, h->rollout_viewpoint_bytes,
                                      h->rollout_viewpoint_stride, obs_frame,
                                      h->rollout_gamestate_batch_stride);
  }
  if (err == 0 && write_outputs != 0) {
    uint8_t* terminal_frame =
        h->rollout_terminal_bytes + (size_t)frame * h->rollout_terminal_frame_stride;
    err = msl_batch_write_terminal(h->batch, terminal_frame, h->rollout_terminal_batch_stride,
                                   (int32_t)max_frame_id);
    if (err == 0) {
      uint8_t* done_frame = h->rollout_done_bytes + (size_t)frame * h->rollout_done_frame_stride;
      for (int bi = 0; bi < msl_batch_batch_size(h->batch); bi++) {
        const MslTerminal* terminal =
            (const MslTerminal*)(terminal_frame + (size_t)bi * h->rollout_terminal_batch_stride);
        *(uint8_t*)(done_frame + (size_t)bi * h->rollout_done_batch_stride) = terminal->done;
      }
    }
  }
  if (err == 0 && write_compare != 0) {
    err = msl_batch_write_compare(h->batch, h->compare_bytes, h->compare_stride);
  }
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch sequence step failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_write_gamestate_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  if (!PyArg_ParseTuple(args, "O", &handle_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (h->viewpoint_bytes == NULL || h->gamestate_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "viewpoint and gamestate buffers must be bound");
    return NULL;
  }
  int err = 0;
  PyThreadState* py_thread_state = PyEval_SaveThread();
  err = melee_batch_write_gamestate(h->batch, h->viewpoint_bytes, h->viewpoint_stride,
                                    h->gamestate_bytes, h->gamestate_stride);
  PyEval_RestoreThread(py_thread_state);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "melee_batch_write_gamestate failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_write_terminal_bound(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int max_frame_id = -1;
  if (!PyArg_ParseTuple(args, "O|i", &handle_obj, &max_frame_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (h->terminal_bytes == NULL) {
    PyErr_SetString(PyExc_ValueError, "terminal buffer is not bound");
    return NULL;
  }
  int err = 0;
  PyThreadState* py_thread_state = PyEval_SaveThread();
  err = msl_batch_write_terminal(h->batch, h->terminal_bytes, h->terminal_stride,
                                 (int32_t)max_frame_id);
  PyEval_RestoreThread(py_thread_state);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_write_terminal failed: %d", err);
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_write_gamestate(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* viewpoint_obj = NULL;
  PyObject* out_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOO", &handle_obj, &viewpoint_obj, &out_obj)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* viewpoint = require_contiguous_array(viewpoint_obj, NPY_UINT8, 1, "viewpoint");
  if (viewpoint == NULL) {
    return NULL;
  }
  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);
  if (PyArray_DIM(viewpoint, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "viewpoint has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(viewpoint, 0), batch_size);
    return NULL;
  }
  if (PyArray_DIM(out, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "out has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(out, 0), batch_size);
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MeleeGamestate)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MeleeGamestate");
    return NULL;
  }

  const uint8_t* viewpoint_bytes = (const uint8_t*)PyArray_DATA(viewpoint);
  const size_t viewpoint_stride = (size_t)PyArray_STRIDE(viewpoint, 0);
  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t out_stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = melee_batch_write_gamestate(h->batch, viewpoint_bytes, viewpoint_stride,
                                              out_bytes, out_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "melee_batch_write_gamestate failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

PyObject* msl_write_terminal(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  PyObject* out_obj = NULL;
  int max_frame_id = -1;
  if (!PyArg_ParseTuple(args, "OO|i", &handle_obj, &out_obj, &max_frame_id)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }

  PyArrayObject* out = require_contiguous_array(out_obj, NPY_UINT8, 2, "out");
  if (out == NULL) {
    return NULL;
  }
  const int batch_size = msl_batch_batch_size(h->batch);
  if (PyArray_DIM(out, 0) < (npy_intp)batch_size) {
    PyErr_Format(PyExc_ValueError, "out has too few rows: got %zd, need %d",
                 (Py_ssize_t)PyArray_DIM(out, 0), batch_size);
    return NULL;
  }
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslTerminal)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslTerminal");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t out_stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_write_terminal(h->batch, out_bytes, out_stride, (int32_t)max_frame_id);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_write_terminal failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

PyObject* msl_destroy(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* capsule = NULL;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return NULL;
  }
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(capsule, "msl.Handle");
  if (h == NULL) {
    return NULL;
  }
  pymsl_release_bound_buffers(h);
  pymsl_release_sequence_buffers(h);
  if (h->batch) {
    msl_batch_destroy(h->batch);
    h->batch = NULL;
  }
  PyMem_Free(h->prev_input_storage);
  h->prev_input_storage = NULL;
  PyMem_Free(h->input_storage);
  h->input_storage = NULL;
  Py_RETURN_NONE;
}
