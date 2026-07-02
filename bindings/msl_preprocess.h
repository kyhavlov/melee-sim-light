#ifndef MSL_PREPROCESS_H
#define MSL_PREPROCESS_H

#include "msl_py_common.h"

int parse_u16_sequence_fixed(PyObject* obj, uint16_t* out, Py_ssize_t cap, Py_ssize_t* out_count,
                             const char* name);
bool u16_in_fixed_set(uint16_t v, const uint16_t* set, Py_ssize_t count);
bool item_key_less(uint16_t iid_a, uint32_t spawn_a, uint16_t type_a, uint16_t iid_b,
                   uint32_t spawn_b, uint16_t type_b);
void msl_py_mtx34_mul_point(const float m[12], float x, float y, float z, float* out_x,
                            float* out_y, float* out_z);
uint8_t msl_py_apply_specialhi_xrotn(uint8_t char_id, uint16_t action_id, uint16_t msid,
                                     uint16_t frame, uint16_t part_id, float model_scale,
                                     float rotate_model, uint8_t rotate_model_valid, float* io_x,
                                     float* io_y, float* io_z);
void msl_py_apply_live_transn_tail(uint8_t char_id, uint16_t msid, uint16_t frame, uint16_t part_id,
                                   float model_scale, float* io_x, float* io_y, float* io_z);
float msl_py_segment_x_at_y(float y, float x0, float y0, float x1, float y1,
                            bool require_vertical_lower_endpoint);
bool msl_py_action_is_airborne_damage_family(uint16_t action_id);

PyObject* msl_fill_items_fixed_py(PyObject* self, PyObject* args);
PyObject* msl_derive_item_spawn_id_counter_py(PyObject* self, PyObject* args);
PyObject* msl_derive_throw_laser_item_hitlist_seed_lanes_py(PyObject* self, PyObject* args);
PyObject* msl_derive_item_attack_fields_py(PyObject* self, PyObject* args);
PyObject* msl_derive_item_reflect_damage_mul_py(PyObject* self, PyObject* args);
PyObject* msl_derive_item_hidden_callback_seed_lanes_py(PyObject* self, PyObject* args);
PyObject* msl_derive_illusion_seed_position_updates_py(PyObject* self, PyObject* args);
PyObject* msl_derive_frame_speed_mul_f32_py(PyObject* self, PyObject* args);
PyObject* msl_derive_landing_fallspecial_allow_interrupt_py(PyObject* self, PyObject* args);

#endif
