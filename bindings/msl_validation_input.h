#ifndef MSL_VALIDATION_INPUT_H
#define MSL_VALIDATION_INPUT_H

#include "msl_py_common.h"

PyObject* msl_validation_derive_guard_input_prefix_py(PyObject* self, PyObject* args);
PyObject* msl_validation_derive_input_history_suffix_py(PyObject* self, PyObject* args);
PyObject* msl_process_stick_i8_units_py(PyObject* self, PyObject* args);
PyObject* msl_derive_ucf_pad_buffer_state_py(PyObject* self, PyObject* args);
PyObject* msl_compute_tilt_timer_axis_py(PyObject* self, PyObject* args);
PyObject* msl_compute_tilt_timer_axis_pre_post_py(PyObject* self, PyObject* args);
PyObject* msl_compute_tilt_timer_y_pre_post_with_fall_fast_py(PyObject* self, PyObject* args);
PyObject* msl_derive_grab_mash_stick_sign_post_py(PyObject* self, PyObject* args);
PyObject* msl_derive_guard_release_lockout_and_lightshield_py(PyObject* self, PyObject* args);
PyObject* msl_compute_press_timer_u8_py(PyObject* self, PyObject* args);
PyObject* msl_compute_lr_press_timer_x67f_py(PyObject* self, PyObject* args);
PyObject* msl_derive_turn_internals_py(PyObject* self, PyObject* args);
PyObject* msl_compute_x672_trigger_timer_pre_post_py(PyObject* self, PyObject* args);
PyObject* msl_compute_fighter_stick_input_counters_py(PyObject* self, PyObject* args);
PyObject* msl_compute_fighter_trigger_input_counters_py(PyObject* self, PyObject* args);
PyObject* msl_compute_fighter_button_timers_py(PyObject* self, PyObject* args);

PyObject* msl_derive_combo_seed_fields_py(PyObject* self, PyObject* args);
PyObject* msl_derive_instance_id_x2073_py(PyObject* self, PyObject* args);
PyObject* msl_derive_instance_id_counter_py(PyObject* self, PyObject* args);

#endif
