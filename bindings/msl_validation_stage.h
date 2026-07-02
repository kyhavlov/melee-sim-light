#ifndef MSL_VALIDATION_STAGE_H
#define MSL_VALIDATION_STAGE_H

#include "msl_py_common.h"

PyObject* msl_validation_derive_yoshi_shyguy_buffers_py(PyObject* self, PyObject* args);
PyObject* msl_validation_derive_dream_whispy_wind_seed_lanes_py(PyObject* self, PyObject* args);

PyObject* msl_derive_yoshi_shyguy_seed_lanes_py(PyObject* self, PyObject* args);
PyObject* msl_derive_dream_whispy_wind_seed_lanes_py(PyObject* self, PyObject* args);
PyObject* msl_derive_fod_platform_motion_with_ground_contact_py(PyObject* self, PyObject* args);
PyObject* msl_derive_fod_floor_skip_segments_py(PyObject* self, PyObject* args);
PyObject* msl_derive_mpcoll_wall_seed_lanes_py(PyObject* self, PyObject* args);

#endif
