#ifndef MSL_VALIDATION_BUFFERS_H
#define MSL_VALIDATION_BUFFERS_H

#include "msl_py_common.h"

PyObject* msl_validation_init_static_buffers_py(PyObject* self, PyObject* args);
PyObject* msl_validation_fill_static_player_py(PyObject* self, PyObject* args);
PyObject* msl_validation_fill_visible_player_py(PyObject* self, PyObject* args);
PyObject* msl_validation_project_post_cache_py(PyObject* self, PyObject* args);
PyObject* msl_validation_finalized_frame_indices_py(PyObject* self, PyObject* args);

#endif
