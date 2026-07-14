#ifndef MSL_BINDING_DATA_H
#define MSL_BINDING_DATA_H

#include "msl_py_common.h"

PyObject* msl_sizes(PyObject* self, PyObject* args);
PyObject* msl_clear_data_dir_py(PyObject* self, PyObject* args);
PyObject* msl_set_data_dir_py(PyObject* self, PyObject* args);
PyObject* msl_data_schema_versions(PyObject* self, PyObject* args);
PyObject* msl_alloc_reset(PyObject* self, PyObject* args);
PyObject* msl_alloc_stats(PyObject* self, PyObject* args);
PyObject* msl_char_params_ecb_joints_py(PyObject* self, PyObject* args);
PyObject* msl_char_params_part_anchors_py(PyObject* self, PyObject* args);
PyObject* msl_item_article_params_py(PyObject* self, PyObject* args);
PyObject* msl_sheik_chain_debug_py(PyObject* self, PyObject* args);
PyObject* msl_stage_floor_segment_py(PyObject* self, PyObject* args);
PyObject* msl_debug_stage_moving_floor_surface_py(PyObject* self, PyObject* args);
PyObject* msl_stage_topology_flags_py(PyObject* self, PyObject* args);
PyObject* msl_stage_fighter_floor_segment_py(PyObject* self, PyObject* args);
PyObject* msl_stage_ceiling_segment_py(PyObject* self, PyObject* args);
PyObject* msl_stage_wall_segment_py(PyObject* self, PyObject* args, uint8_t left_wall);
PyObject* msl_stage_left_wall_segment_py(PyObject* self, PyObject* args);
PyObject* msl_stage_right_wall_segment_py(PyObject* self, PyObject* args);
PyObject* msl_stage_raw_line_non_kind_py(PyObject* self, PyObject* args);
PyObject* msl_stage_static_query_py(PyObject* self, PyObject* args);
PyObject* msl_stage_item_line_hit_py(PyObject* self, PyObject* args);
PyObject* msl_mpcoll_check_bounding_aabb_py(PyObject* self, PyObject* args);
PyObject* msl_mpcoll_end_publication_py(PyObject* self, PyObject* args);
PyObject* msl_stage_match_flow_roles_py(PyObject* self, PyObject* args);
#endif
