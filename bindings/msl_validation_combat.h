#ifndef MSL_VALIDATION_COMBAT_H
#define MSL_VALIDATION_COMBAT_H

#include "msl_py_common.h"

PyObject* msl_validation_derive_staling_buffers_py(PyObject* self, PyObject* args);
PyObject* msl_validation_derive_grab_constraint_x2174_buffers_py(PyObject* self, PyObject* args);
PyObject* msl_validation_derive_dynamic_pose_buffers_py(PyObject* self, PyObject* args);

PyObject* msl_derive_staling_history_py(PyObject* self, PyObject* args);
PyObject* msl_derive_illusion_ghost_pos012_py(PyObject* self, PyObject* args);
PyObject* msl_derive_combat_hitlist_seed_fields_py(PyObject* self, PyObject* args);
PyObject* msl_derive_specialhi_rotate_model_seed_lane_py(PyObject* self, PyObject* args);
PyObject* msl_trim_stale_hitlist_seed_bridge_py(PyObject* self, PyObject* args);
PyObject* msl_derive_attacker_shield_ground_kb_vel_py(PyObject* self, PyObject* args);
PyObject* msl_derive_marth_counter_hitlag_floor_active_py(PyObject* self, PyObject* args);
PyObject* msl_derive_rebound_seed_lanes_py(PyObject* self, PyObject* args);
PyObject* msl_derive_source_clear_timer_py(PyObject* self, PyObject* args);
PyObject* msl_derive_phantom_damage_pending_seed_lanes_py(PyObject* self, PyObject* args);

#endif
