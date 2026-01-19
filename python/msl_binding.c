#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <numpy/arrayobject.h>

#include "../src/msl_api.h"

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

static PyArrayObject* require_contiguous_array(PyObject* obj, int typenum, int min_ndim, const char* name) {
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
  static const char* kwlist[] = {"batch_size", "num_players", NULL};
  int batch_size = 0;
  int num_players = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kwargs, "ii", (char**)kwlist, &batch_size, &num_players)) {
    return NULL;
  }

  MslBatch* batch = msl_batch_create(batch_size, num_players);
  if (batch == NULL) {
    PyErr_SetString(PyExc_MemoryError, "msl_batch_create failed");
    return NULL;
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
    pymsl_capsule_destructor(capsule);
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

static PyObject* msl_reseed_seed_v0(PyObject* self, PyObject* args) {
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
  if (PyArray_DIM(seed, 1) < (npy_intp)sizeof(MslSeedV0)) {
    PyErr_SetString(PyExc_ValueError, "seed second dim too small for MslSeedV0");
    return NULL;
  }

  const uint8_t* seed_bytes = (const uint8_t*)PyArray_DATA(seed);
  const size_t stride = (size_t)PyArray_STRIDE(seed, 0);

  const int err = msl_batch_reseed_seed_v0(h->batch, seed_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_reseed_seed_v0 failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_step_input_v0(PyObject* self, PyObject* args) {
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

  if (PyArray_DIM(prev_input, 1) < (npy_intp)sizeof(MslInputV0) ||
      PyArray_DIM(input, 1) < (npy_intp)sizeof(MslInputV0)) {
    PyErr_SetString(PyExc_ValueError, "input second dim too small for MslInputV0");
    return NULL;
  }

  const uint8_t* prev_bytes = (const uint8_t*)PyArray_DATA(prev_input);
  const uint8_t* in_bytes = (const uint8_t*)PyArray_DATA(input);
  const size_t prev_stride = (size_t)PyArray_STRIDE(prev_input, 0);
  const size_t in_stride = (size_t)PyArray_STRIDE(input, 0);

  const int err = msl_batch_step_input_v0(h->batch, prev_bytes, prev_stride, in_bytes, in_stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_step_input_v0 failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_write_compare_v0(PyObject* self, PyObject* args) {
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
  if (PyArray_DIM(out, 1) < (npy_intp)sizeof(MslCompareV0)) {
    PyErr_SetString(PyExc_ValueError, "out second dim too small for MslCompareV0");
    return NULL;
  }

  uint8_t* out_bytes = (uint8_t*)PyArray_DATA(out);
  const size_t stride = (size_t)PyArray_STRIDE(out, 0);

  const int err = msl_batch_write_compare_v0(h->batch, out_bytes, stride);
  if (err != 0) {
    PyErr_Format(PyExc_RuntimeError, "msl_batch_write_compare_v0 failed: %d", err);
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* msl_sizes(PyObject* self, PyObject* args) {
  return Py_BuildValue(
      "{s:i,s:i,s:i,s:i}",
      "seed_v0", (int)sizeof(MslSeedV0),
      "input_v0", (int)sizeof(MslInputV0),
      "compare_v0", (int)sizeof(MslCompareV0),
      "sample_v0", (int)sizeof(MslSampleV0));
}

static PyMethodDef methods[] = {
    {"init", (PyCFunction)msl_init, METH_VARARGS | METH_KEYWORDS, "init(batch_size, num_players) -> handle"},
    {"reseed_seed_v0", msl_reseed_seed_v0, METH_VARARGS, "reseed_seed_v0(handle, seed_bytes[batch, seed_stride])"},
    {"step_input_v0", msl_step_input_v0, METH_VARARGS, "step_input_v0(handle, prev_input_bytes, input_bytes)"},
    {"write_compare_v0", msl_write_compare_v0, METH_VARARGS, "write_compare_v0(handle, out_bytes)"},
    {"sizes", msl_sizes, METH_NOARGS, "sizes() -> dict of struct sizes"},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "msl_binding",
    NULL,
    -1,
    methods,
    NULL,
    NULL,
    NULL,
    NULL,
};

PyMODINIT_FUNC PyInit_msl_binding(void) {
  import_array();
  return PyModule_Create(&moduledef);
}

