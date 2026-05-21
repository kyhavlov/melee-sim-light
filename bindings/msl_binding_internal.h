#ifndef MSL_BINDING_INTERNAL_H
#define MSL_BINDING_INTERNAL_H

#include <Python.h>

#include <numpy/arrayobject.h>

PyArrayObject* require_contiguous_array(PyObject* obj, int typenum, int min_ndim, const char* name);
int require_exact_2d_shape(PyArrayObject* arr, npy_intp rows, npy_intp cols, const char* name);

#endif
