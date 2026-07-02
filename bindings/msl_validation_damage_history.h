#ifndef MSL_VALIDATION_DAMAGE_HISTORY_H
#define MSL_VALIDATION_DAMAGE_HISTORY_H

#include "msl_py_common.h"

PyObject* msl_derive_damage_time_since_hit_x18ac_py(PyObject* self, PyObject* args);
PyObject* msl_derive_damage_hitlag_sdi_reset_post_mask_py(PyObject* self, PyObject* args);
PyObject* msl_derive_damage_entry_tilt_timer_reset_post_mask_py(PyObject* self, PyObject* args);
PyObject* msl_derive_guard_reflect_timer_plus1_py(PyObject* self, PyObject* args);
PyObject* msl_derive_guard_reflect_origin_guardon_py(PyObject* self, PyObject* args);
PyObject* msl_derive_guard_special_enable_timer_x1c_py(PyObject* self, PyObject* args);
PyObject* msl_derive_guard_setoff_hitlag_damage_min_py(PyObject* self, PyObject* args);
PyObject* msl_derive_guard_setoff_hitlag_exit_phase_py(PyObject* self, PyObject* args);
PyObject* msl_derive_damage_jump_buffer_x14_py(PyObject* self, PyObject* args);
PyObject* msl_derive_damage_meteor_cancel_x1a_py(PyObject* self, PyObject* args);
PyObject* msl_derive_damage_post_hitlag_cb_kind_py(PyObject* self, PyObject* args);
PyObject* msl_derive_guard_tilt_state_py(PyObject* self, PyObject* args);
PyObject* msl_derive_shine_release_state_py(PyObject* self, PyObject* args);
PyObject* msl_derive_magnify_damage_counter_x1910_py(PyObject* self, PyObject* args);
PyObject* msl_derive_colanim_internals_py(PyObject* self, PyObject* args);
PyObject* msl_derive_capture_mash_buttons_pressed_py(PyObject* self, PyObject* args);
PyObject* msl_derive_capture_grab_hidden_post_py(PyObject* self, PyObject* args);

#endif
