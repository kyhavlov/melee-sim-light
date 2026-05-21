#pragma once

#define PY_SSIZE_T_CLEAN
#include <Python.h>

PyObject* msl_collect_mismatch_events(PyObject* self, PyObject* args);
