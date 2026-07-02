#ifndef MSL_PY_COMMON_H
#define MSL_PY_COMMON_H

#ifndef PY_SSIZE_T_CLEAN
#define PY_SSIZE_T_CLEAN
#endif
#ifndef PY_ARRAY_UNIQUE_SYMBOL
#define PY_ARRAY_UNIQUE_SYMBOL MSL_BINDING_ARRAY_API
#endif
#ifndef MSL_BINDING_IMPORT_ARRAY
#ifndef NO_IMPORT_ARRAY
#define NO_IMPORT_ARRAY
#endif
#endif

#include <Python.h>

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <numpy/arrayobject.h>

PyArrayObject* require_contiguous_array(PyObject* obj, int typenum, int min_ndim, const char* name);
PyArrayObject* require_contiguous_array_readonly(PyObject* obj, int typenum, int min_ndim,
                                                 const char* name);
int require_exact_2d_shape(PyArrayObject* arr, npy_intp rows, npy_intp cols, const char* name);

static inline float f32_from_double(double x) { return (float)x; }

#endif
