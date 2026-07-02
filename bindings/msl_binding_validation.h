#ifndef MSL_BINDING_VALIDATION_H
#define MSL_BINDING_VALIDATION_H

#include "msl_py_common.h"

PyObject* msl_slpz_unorder_events(PyObject* self, PyObject* args);
PyObject* msl_standard_rollout_compare(PyObject* self, PyObject* args);
PyObject* msl_one_step_summary_create(PyObject* self, PyObject* args);
PyObject* msl_one_step_summary_accumulate(PyObject* self, PyObject* args);
PyObject* msl_one_step_summary_finish(PyObject* self, PyObject* args);
PyObject* msl_one_step_summary(PyObject* self, PyObject* args);
PyObject* msl_one_step_eval_samples(PyObject* self, PyObject* args);
PyObject* msl_one_step_eval_buffers(PyObject* self, PyObject* args);
PyObject* msl_standard_rollout_scan(PyObject* self, PyObject* args);
PyObject* msl_standard_rollout_scan_buffers(PyObject* self, PyObject* args);

#endif
