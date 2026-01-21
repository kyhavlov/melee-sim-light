#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <numpy/arrayobject.h>

#include "../src/alloc.h"
#include "../src/api.h"
#include "../src/anim_pose.h"
#include "../src/ecb_tables.h"

typedef struct {
  MslBatch* batch;
} PyMslHandle;

static void pymsl_capsule_destructor(PyObject* capsule) {
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(capsule, "msl.Handle");
  if (h == NULL) {
    return;
  }
  if (h->batch) {
    msl_batch_destroy(h->batch);
    h->batch = NULL;
  }
  PyMem_Free(h);
}

static PyArrayObject* require_contiguous_array(PyObject* obj, int typenum, int min_ndim,
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

static PyObject* msl_init(PyObject* self, PyObject* args, PyObject* kwargs) {
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
    PyErr_SetString(PyExc_MemoryError, "msl_batch_create failed");
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

  PyMslHandle* h = (PyMslHandle*)PyMem_Malloc(sizeof(PyMslHandle));
  if (h == NULL) {
    msl_batch_destroy(batch);
    PyErr_NoMemory();
    return NULL;
  }
  h->batch = batch;

  PyObject* capsule = PyCapsule_New(h, "msl.Handle", pymsl_capsule_destructor);
  if (capsule == NULL) {
    // PyCapsule_New sets an exception on failure. We must clean up manually here because the capsule
    // was never created (calling the capsule destructor with NULL would be a bug).
    msl_batch_destroy(h->batch);
    h->batch = NULL;
    PyMem_Free(h);
    return NULL;
  }
  return capsule;
}

static PyMslHandle* unpack_handle(PyObject* handle_obj) {
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(handle_obj, "msl.Handle");
  if (h == NULL || h->batch == NULL) {
    PyErr_SetString(PyExc_ValueError, "invalid msl handle");
    return NULL;
  }
  return h;
}

static PyObject* msl_reseed_seed(PyObject* self, PyObject* args) {
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

static PyObject* msl_step_input(PyObject* self, PyObject* args) {
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

static PyObject* msl_write_compare(PyObject* self, PyObject* args) {
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

static PyObject* msl_debug_write_processed_input(PyObject* self, PyObject* args) {
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
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslProcessedInput)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslProcessedInput");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_debug_write_processed_input(h->batch, out_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_write_processed_input failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_debug_write_internals(PyObject* self, PyObject* args) {
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
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslDebugInternals)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslDebugInternals");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_debug_write_internals(h->batch, out_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_debug_write_internals failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_sizes(PyObject* self, PyObject* args) {
  return Py_BuildValue("{s:i,s:i,s:i,s:i,s:i,s:i}", "seed", (int)sizeof(MslSeed), "input",
                       (int)sizeof(MslInput), "compare", (int)sizeof(MslCompare), "sample",
                       (int)sizeof(MslSample), "processed_input", (int)sizeof(MslProcessedInput),
                       "internals", (int)sizeof(MslDebugInternals));
}

static PyObject* msl_alloc_reset(PyObject* self, PyObject* args) {
  msl_alloc_reset_counters();
  Py_RETURN_NONE;
}

static PyObject* msl_alloc_stats(PyObject* self, PyObject* args) {
  const unsigned long long calls = (unsigned long long)msl_alloc_total_calls();
  const unsigned long long bytes = (unsigned long long)msl_alloc_total_bytes();
  return Py_BuildValue("{s:K,s:K}", "calls", calls, "bytes", bytes);
}

static PyObject* msl_ecb_bottom_rel_y_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  unsigned long anim_u = 0;
  int action_frame = 0;
  if (!PyArg_ParseTuple(args, "Iki", &char_id_u, &anim_u, &action_frame)) {
    return NULL;
  }
  if (char_id_u > 255u) {
    PyErr_SetString(PyExc_ValueError, "char_id out of range");
    return NULL;
  }
  const float y = msl_ecb_bottom_rel_y((uint8_t)char_id_u, (uint32_t)anim_u, action_frame);
  return PyFloat_FromDouble((double)y);
}

static PyObject* msl_ecb_extents_rel_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  unsigned long anim_u = 0;
  int action_frame = 0;
  if (!PyArg_ParseTuple(args, "Iki", &char_id_u, &anim_u, &action_frame)) {
    return NULL;
  }
  if (char_id_u > 255u) {
    PyErr_SetString(PyExc_ValueError, "char_id out of range");
    return NULL;
  }
  const MslEcbExtentsRel ex =
      msl_ecb_extents_rel((uint8_t)char_id_u, (uint32_t)anim_u, action_frame);
  return Py_BuildValue("(ffff)", ex.min_x, ex.max_x, ex.min_y, ex.max_y);
}

static PyObject* msl_anim_pose_matrix_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  unsigned int msid_u = 0;
  unsigned int frame_u = 0;
  unsigned int part_id_u = 0;
  if (!PyArg_ParseTuple(args, "IIII", &char_id_u, &msid_u, &frame_u, &part_id_u)) {
    return NULL;
  }
  if (char_id_u > 255u) {
    PyErr_SetString(PyExc_ValueError, "char_id out of range");
    return NULL;
  }
  if (msid_u > 0xFFFFu || frame_u > 0xFFFFu || part_id_u > 0xFFFFu) {
    PyErr_SetString(PyExc_ValueError, "msid/frame/part_id out of range");
    return NULL;
  }

  npy_intp dims[1] = {(npy_intp)12};
  PyArrayObject* arr = (PyArrayObject*)PyArray_SimpleNew(1, dims, NPY_FLOAT32);
  if (arr == NULL) {
    return NULL;
  }
  float* out = (float*)PyArray_DATA(arr);
  const int err = anim_pose_get_matrix((uint8_t)char_id_u, (uint16_t)msid_u, (uint16_t)frame_u,
                                       (uint16_t)part_id_u, out);
  if (err != 0) {
    Py_DECREF(arr);
    PyErr_SetString(PyExc_ValueError, "anim_pose_get_matrix failed");
    return NULL;
  }
  return (PyObject*)arr;
}

static PyObject* msl_destroy(PyObject* self, PyObject* args) {
  PyObject* capsule = NULL;
  if (!PyArg_ParseTuple(args, "O", &capsule)) {
    return NULL;
  }
  PyMslHandle* h = (PyMslHandle*)PyCapsule_GetPointer(capsule, "msl.Handle");
  if (h == NULL) {
    return NULL;
  }
  if (h->batch) {
    msl_batch_destroy(h->batch);
    h->batch = NULL;
  }
  Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"init", (PyCFunction)msl_init, METH_VARARGS | METH_KEYWORDS,
     "init(batch_size, num_players, ucf_enabled=?, ucf_cardinals_1_0_enabled=?) -> handle"},
    {"destroy", msl_destroy, METH_VARARGS,
     "destroy(handle) -> None (free underlying C batch immediately)"},
    {"reseed_seed", msl_reseed_seed, METH_VARARGS,
     "reseed_seed(handle, seed_bytes[batch, seed_stride])"},
    {"step_input", msl_step_input, METH_VARARGS,
     "step_input(handle, prev_input_bytes, input_bytes)"},
    {"write_compare", msl_write_compare, METH_VARARGS, "write_compare(handle, out_bytes)"},
    {"debug_write_processed_input", msl_debug_write_processed_input, METH_VARARGS,
     "debug_write_processed_input(handle, out_bytes)"},
    {"debug_write_internals", msl_debug_write_internals, METH_VARARGS,
     "debug_write_internals(handle, out_bytes)"},
    {"sizes", msl_sizes, METH_NOARGS, "sizes() -> dict of struct sizes"},
    {"alloc_reset", msl_alloc_reset, METH_NOARGS,
     "Reset C allocation counters (debug/perf guardrail)."},
    {"alloc_stats", msl_alloc_stats, METH_NOARGS,
     "Get C allocation counters (debug/perf guardrail)."},
    {"ecb_bottom_rel_y", msl_ecb_bottom_rel_y_py, METH_VARARGS,
     "ecb_bottom_rel_y(char_id, animation_index, action_frame) -> float"},
    {"ecb_extents_rel", msl_ecb_extents_rel_py, METH_VARARGS,
     "ecb_extents_rel(char_id, animation_index, action_frame) -> (min_x, max_x, min_y, max_y)"},
    {"anim_pose_matrix", msl_anim_pose_matrix_py, METH_VARARGS,
     "anim_pose_matrix(char_id, msid, frame, part_id) -> np.ndarray[float32] shape=(12,)"},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT, "msl_binding", NULL, -1, methods, NULL, NULL, NULL, NULL,
};

PyMODINIT_FUNC PyInit_msl_binding(void) {
  import_array();
  return PyModule_Create(&moduledef);
}
