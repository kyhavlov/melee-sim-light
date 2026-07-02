#ifndef MSL_VALIDATION_MOVEMENT_HISTORY_H
#define MSL_VALIDATION_MOVEMENT_HISTORY_H

#include "msl_py_common.h"

PyObject* msl_derive_guard_setoff_post_hitlag_owner_py(PyObject* self, PyObject* args);
PyObject* msl_derive_run_x0_py(PyObject* self, PyObject* args);
PyObject* msl_derive_runbrake_cmd0_py(PyObject* self, PyObject* args);
PyObject* msl_derive_dash_x4_py(PyObject* self, PyObject* args);
PyObject* msl_derive_ecb_lock_timer_py(PyObject* self, PyObject* args);
PyObject* msl_derive_ecb_lock_bottom_rel_y_py(PyObject* self, PyObject* args);
PyObject* msl_derive_damage_hitlag_colldata_ecb_py(PyObject* self, PyObject* args);
PyObject* msl_derive_downwait_timer_py(PyObject* self, PyObject* args);
PyObject* msl_derive_kneebend_internals_py(PyObject* self, PyObject* args);
PyObject* msl_derive_smash_charge_seed_lanes_py(PyObject* self, PyObject* args);
PyObject* msl_derive_ledge_cooldown_py(PyObject* self, PyObject* args);
PyObject* msl_derive_cliff_ledge_floor_segment_id_py(PyObject* self, PyObject* args);
PyObject* msl_derive_cliff_option_stick_latch_x8_py(PyObject* self, PyObject* args);
PyObject* msl_derive_match_flow_timer_py(PyObject* self, PyObject* args);
PyObject* msl_derive_passivewall_timer_py(PyObject* self, PyObject* args);
PyObject* msl_derive_walljump_phase_seed_lanes_py(PyObject* self, PyObject* args);
PyObject* msl_derive_entry_end_fall_lock_py(PyObject* self, PyObject* args);
PyObject* msl_derive_jab_rapid_count_py(PyObject* self, PyObject* args);
PyObject* msl_derive_walk_anim_source_vel_py(PyObject* self, PyObject* args);
PyObject* msl_derive_walk_retarget_tick_source_vel_py(PyObject* self, PyObject* args);
PyObject* msl_derive_run_anim_source_vel_py(PyObject* self, PyObject* args);
PyObject* msl_derive_facing_dir1_sign_py(PyObject* self, PyObject* args);
PyObject* msl_derive_common_fall_blend_seed_py(PyObject* self, PyObject* args);

PyObject* msl_derive_shine_release_state_py(PyObject* self, PyObject* args);

#endif
