#ifndef MSL_BINDING_RUNTIME_H
#define MSL_BINDING_RUNTIME_H

#include "msl_binding_internal.h"

PyObject* msl_init(PyObject* self, PyObject* args, PyObject* kwargs);
PyObject* msl_reseed_seed(PyObject* self, PyObject* args);
PyObject* msl_reseed_seed_rollout(PyObject* self, PyObject* args);
PyObject* msl_apply_replay_frame_rng(PyObject* self, PyObject* args);
PyObject* msl_init_match(PyObject* self, PyObject* args);
PyObject* msl_init_match_masked(PyObject* self, PyObject* args);
PyObject* msl_step_input(PyObject* self, PyObject* args);
PyObject* msl_step_input_replay_frame_rng(PyObject* self, PyObject* args);
PyObject* msl_write_compare(PyObject* self, PyObject* args);
PyObject* msl_bind_buffers(PyObject* self, PyObject* args);
PyObject* msl_unbind_buffers(PyObject* self, PyObject* args);
PyObject* msl_bind_sequence_buffers(PyObject* self, PyObject* args);
PyObject* msl_unbind_sequence_buffers(PyObject* self, PyObject* args);
PyObject* msl_reset_prev_input(PyObject* self, PyObject* args);
PyObject* msl_set_prev_input_from_sequence(PyObject* self, PyObject* args);
PyObject* msl_init_match_bound(PyObject* self, PyObject* args);
PyObject* msl_init_match_sequence_bound(PyObject* self, PyObject* args);
PyObject* msl_reset_sequence_masked(PyObject* self, PyObject* args);
PyObject* msl_step_bound(PyObject* self, PyObject* args);
PyObject* msl_write_compare_bound(PyObject* self, PyObject* args);
PyObject* msl_step_write_compare_bound(PyObject* self, PyObject* args);
PyObject* msl_step_sequence(PyObject* self, PyObject* args);
PyObject* msl_write_gamestate_bound(PyObject* self, PyObject* args);
PyObject* msl_write_terminal_bound(PyObject* self, PyObject* args);
PyObject* msl_write_gamestate(PyObject* self, PyObject* args);
PyObject* msl_write_terminal(PyObject* self, PyObject* args);
PyObject* msl_destroy(PyObject* self, PyObject* args);

#endif
