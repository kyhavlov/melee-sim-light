#include "msl_binding_data.h"
#include "msl_binding_internal.h"

#include "../src/ecb_tables.h"
#include "../src/script_events.h"

PyObject* msl_sizes(PyObject* self, PyObject* args) {
  (void)self;
  (void)args;
  return Py_BuildValue("{s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i}", "seed",
                       (int)sizeof(MslSeed), "match_config", (int)sizeof(MslMatchConfig), "input",
                       (int)sizeof(MslInput), "compare", (int)sizeof(MslCompare), "sample",
                       (int)sizeof(MslSample), "gamestate", (int)sizeof(MeleeGamestate), "terminal",
                       (int)sizeof(MslTerminal), "processed_input", (int)sizeof(MslProcessedInput),
                       "controller_input", (int)sizeof(PyMslControllerInput), "stage_state",
                       (int)sizeof(MslDebugStageState), "internals", (int)sizeof(MslDebugInternals),
                       "collision_contacts", (int)sizeof(MslDebugCollisionContacts), "colldata_ecb",
                       (int)sizeof(MslDebugCollDataEcb));
}

PyObject* msl_clear_data_dir_py(PyObject* self, PyObject* args) {
  (void)self;
  (void)args;
  msl_clear_data_dir();
  Py_RETURN_NONE;
}

PyObject* msl_set_data_dir_py(PyObject* self, PyObject* args) {
  (void)self;
  const char* path = NULL;
  if (!PyArg_ParseTuple(args, "s", &path)) {
    return NULL;
  }
  if (msl_set_data_dir(path) != 0) {
    PyErr_SetString(PyExc_ValueError, "set_data_dir: empty or oversized path");
    return NULL;
  }
  Py_RETURN_NONE;
}

PyObject* msl_data_schema_versions(PyObject* self, PyObject* args) {
  (void)self;
  (void)args;
  return Py_BuildValue(
      "{s:I,s:I,s:I,s:I,s:I,s:I,s:I}", "attack_id_move_id", attack_id_tables_format_version(),
      "ecb_bottom", ecb_bottom_tables_format_version(), "ecb_extents",
      ecb_extents_tables_format_version(), "fighter_anims", anim_pose_data_schema_version(),
      "fighter_hitboxes", hitboxes_tables_format_version(), "fighter_scripts",
      script_events_format_version(), "motion_state_owners", motion_state_owners_format_version());
}

PyObject* msl_alloc_reset(PyObject* self, PyObject* args) {
  (void)self;
  (void)args;
  msl_alloc_reset_counters();
  Py_RETURN_NONE;
}

PyObject* msl_alloc_stats(PyObject* self, PyObject* args) {
  (void)self;
  (void)args;
  const unsigned long long calls = (unsigned long long)msl_alloc_total_calls();
  const unsigned long long bytes = (unsigned long long)msl_alloc_total_bytes();
  return Py_BuildValue("{s:K,s:K}", "calls", calls, "bytes", bytes);
}

PyObject* msl_char_params_ecb_joints_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  if (!PyArg_ParseTuple(args, "I", &char_id_u)) {
    return NULL;
  }
  if (char_id_u > 255u) {
    PyErr_SetString(PyExc_ValueError, "char_id out of range");
    return NULL;
  }
  if (char_params_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "char_params_init failed");
    return NULL;
  }
  const MslCharParams* ch = msl_char_params((uint8_t)char_id_u);
  if (ch == NULL) {
    PyErr_SetString(PyExc_ValueError, "unknown char_id");
    return NULL;
  }
  PyObject* out = PyList_New((Py_ssize_t)ch->ecb_joint_count);
  if (out == NULL) {
    return NULL;
  }
  for (uint8_t i = 0; i < ch->ecb_joint_count; i++) {
    PyObject* v = PyLong_FromUnsignedLong((unsigned long)ch->ecb_joints[i]);
    if (v == NULL) {
      Py_DECREF(out);
      return NULL;
    }
    PyList_SET_ITEM(out, (Py_ssize_t)i, v);
  }
  return out;
}

PyObject* msl_char_params_part_anchors_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  if (!PyArg_ParseTuple(args, "I", &char_id_u)) {
    return NULL;
  }
  if (char_id_u > 255u) {
    PyErr_SetString(PyExc_ValueError, "char_id out of range");
    return NULL;
  }
  if (char_params_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "char_params_init failed");
    return NULL;
  }
  const MslCharParams* ch = msl_char_params((uint8_t)char_id_u);
  if (ch == NULL) {
    PyErr_SetString(PyExc_ValueError, "unknown char_id");
    return NULL;
  }
  PyObject* ecb = PyList_New((Py_ssize_t)ch->ecb_joint_count);
  if (ecb == NULL) {
    return NULL;
  }
  for (uint8_t i = 0; i < ch->ecb_joint_count; i++) {
    PyObject* v = PyLong_FromUnsignedLong((unsigned long)ch->ecb_joints[i]);
    if (v == NULL) {
      Py_DECREF(ecb);
      return NULL;
    }
    PyList_SET_ITEM(ecb, (Py_ssize_t)i, v);
  }
  PyObject* out = Py_BuildValue(
      "{s:N,s:i,s:i,s:i,s:i,s:i,s:i}", "ecb_joints", ecb, "laser_spawn_joint_part_id",
      (int)ch->laser_spawn_joint_part_id, "reflector_bone_part_id", (int)ch->reflector_bone_part_id,
      "camera_zoom_target_bone_part_id", (int)ch->camera_zoom_target_bone_part_id,
      "grab_capture_anchor_part_id", (int)ch->grab_capture_anchor_part_id,
      "throw_release_mpcoll_floor_publication_mask",
      (int)ch->throw_release_mpcoll_floor_publication_mask, "escapeair_carried_floor_wall_source",
      (int)ch->escapeair_carried_floor_wall_source);
  return out;
}

PyObject* msl_item_article_params_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int char_id_u = 0;
  if (!PyArg_ParseTuple(args, "I", &char_id_u)) {
    return NULL;
  }
  if (char_id_u > 255u) {
    PyErr_SetString(PyExc_ValueError, "char_id out of range");
    return NULL;
  }
  if (item_article_params_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "item_article_params_init failed");
    return NULL;
  }
  const MslItemArticleParams* p = item_article_params_get((uint8_t)char_id_u);
  if (p == NULL) {
    PyErr_SetString(PyExc_ValueError, "unknown char_id");
    return NULL;
  }
  PyObject* a_offsets =
      Py_BuildValue("[f,f,f]", (double)p->needle_hurtbox_a_offset[0],
                    (double)p->needle_hurtbox_a_offset[1], (double)p->needle_hurtbox_a_offset[2]);
  if (a_offsets == NULL) {
    return NULL;
  }
  PyObject* b_offsets =
      Py_BuildValue("[f,f,f]", (double)p->needle_hurtbox_b_offset[0],
                    (double)p->needle_hurtbox_b_offset[1], (double)p->needle_hurtbox_b_offset[2]);
  if (b_offsets == NULL) {
    Py_DECREF(a_offsets);
    return NULL;
  }
  PyObject* hb_damage = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_size = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_x = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_y = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_z = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_angle = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_kbg = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_wsk = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_bkb = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_element = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_shield_damage = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  PyObject* hb_flags = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);
  if (hb_damage == NULL || hb_size == NULL || hb_x == NULL || hb_y == NULL || hb_z == NULL ||
      hb_angle == NULL || hb_kbg == NULL || hb_wsk == NULL || hb_bkb == NULL ||
      hb_element == NULL || hb_shield_damage == NULL || hb_flags == NULL) {
    Py_XDECREF(a_offsets);
    Py_XDECREF(b_offsets);
    Py_XDECREF(hb_damage);
    Py_XDECREF(hb_size);
    Py_XDECREF(hb_x);
    Py_XDECREF(hb_y);
    Py_XDECREF(hb_z);
    Py_XDECREF(hb_angle);
    Py_XDECREF(hb_kbg);
    Py_XDECREF(hb_wsk);
    Py_XDECREF(hb_bkb);
    Py_XDECREF(hb_element);
    Py_XDECREF(hb_shield_damage);
    Py_XDECREF(hb_flags);
    return NULL;
  }
  for (int i = 0; i < MSL_ITEM_ARTICLE_MAX_HITBOXES; i++) {
    PyList_SET_ITEM(hb_damage, i, PyFloat_FromDouble((double)p->needle_hitbox_damage_by_id[i]));
    PyList_SET_ITEM(hb_size, i, PyFloat_FromDouble((double)p->needle_hitbox_size[i]));
    PyList_SET_ITEM(hb_x, i, PyFloat_FromDouble((double)p->needle_hitbox_x_offset[i]));
    PyList_SET_ITEM(hb_y, i, PyFloat_FromDouble((double)p->needle_hitbox_y_offset[i]));
    PyList_SET_ITEM(hb_z, i, PyFloat_FromDouble((double)p->needle_hitbox_z_offset[i]));
    PyList_SET_ITEM(hb_angle, i, PyLong_FromLong((long)p->needle_hitbox_angle[i]));
    PyList_SET_ITEM(hb_kbg, i, PyLong_FromLong((long)p->needle_hitbox_kbg[i]));
    PyList_SET_ITEM(hb_wsk, i, PyLong_FromLong((long)p->needle_hitbox_wsk[i]));
    PyList_SET_ITEM(hb_bkb, i, PyLong_FromLong((long)p->needle_hitbox_bkb[i]));
    PyList_SET_ITEM(hb_element, i, PyLong_FromLong((long)p->needle_hitbox_element[i]));
    PyList_SET_ITEM(hb_shield_damage, i, PyLong_FromLong((long)p->needle_hitbox_shield_damage[i]));
    PyList_SET_ITEM(hb_flags, i, PyLong_FromUnsignedLong((unsigned long)p->needle_hitbox_flags[i]));
  }
  PyObject* out = Py_BuildValue(
      "{s:i,s:i,s:i,s:i,s:i,s:f,s:f,s:f,s:f,s:f,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:f,s:i,s:"
      "i,s:"
      "N,s:"
      "N,s:f,s:f,s:i,s:N,s:N,s:N,s:N,s:N,s:N,s:N,s:N,s:N,s:N,s:N,s:N}",
      "blaster_shot_itkind", (int)p->blaster_shot_itkind, "blaster_gun_itkind",
      (int)p->blaster_gun_itkind, "laser_spawn_joint_part_id", (int)p->laser_spawn_joint_part_id,
      "laser_lifetime_frames", (int)p->laser_lifetime_frames, "side_special_illusion_itkind",
      (int)p->side_special_illusion_itkind, "laser_damage", (double)p->laser_damage, "laser_size",
      (double)p->laser_size, "illusion_item_state0_damage", (double)p->illusion_item_state0_damage,
      "illusion_item_state1_damage", (double)p->illusion_item_state1_damage,
      "shield_bounce_extra_degrees", (double)p->shield_bounce_extra_degrees, "needle_throw_itkind",
      (int)p->needle_throw_itkind, "needle_held_itkind", (int)p->needle_held_itkind,
      "needle_lifetime_frames", (int)p->needle_lifetime_frames, "needle_bounce_lifetime_frames",
      (int)p->needle_bounce_lifetime_frames, "sheik_chain_itkind", (int)p->sheik_chain_itkind,
      "sheik_chain_spawn_part_id", (int)p->sheik_chain_spawn_part_id, "sheik_chain_lifetime_frames",
      (int)p->sheik_chain_lifetime_frames, "sheik_vanish_itkind", (int)p->sheik_vanish_itkind,
      "sheik_vanish_spawn_part_id", (int)p->sheik_vanish_spawn_part_id,
      "sheik_vanish_lifetime_frames", (int)p->sheik_vanish_lifetime_frames, "needle_launch_speed",
      (double)p->needle_launch_speed, "needle_hurtbox_count", (int)p->needle_hurtbox_count,
      "needle_hurtbox_bone_id", (int)p->needle_hurtbox_bone_id, "needle_hurtbox_a_offset",
      a_offsets, "needle_hurtbox_b_offset", b_offsets, "needle_hurtbox_scale",
      (double)p->needle_hurtbox_scale, "needle_hitbox_damage", (double)p->needle_hitbox_damage,
      "needle_hitbox_count", (int)p->needle_hitbox_count, "needle_hitbox_damage_by_id", hb_damage,
      "needle_hitbox_size", hb_size, "needle_hitbox_x_offset", hb_x, "needle_hitbox_y_offset", hb_y,
      "needle_hitbox_z_offset", hb_z, "needle_hitbox_angle", hb_angle, "needle_hitbox_kbg", hb_kbg,
      "needle_hitbox_wsk", hb_wsk, "needle_hitbox_bkb", hb_bkb, "needle_hitbox_element", hb_element,
      "needle_hitbox_shield_damage", hb_shield_damage, "needle_hitbox_flags", hb_flags);
  if (out == NULL) {
    return NULL;
  }
#define MSL_SET_DICT_LONG(KEY, VALUE)                 \
  do {                                                \
    PyObject* v__ = PyLong_FromLong((long)(VALUE));   \
    if (v__ == NULL) {                                \
      Py_DECREF(out);                                 \
      return NULL;                                    \
    }                                                 \
    if (PyDict_SetItemString(out, (KEY), v__) != 0) { \
      Py_DECREF(v__);                                 \
      Py_DECREF(out);                                 \
      return NULL;                                    \
    }                                                 \
    Py_DECREF(v__);                                   \
  } while (0)
#define MSL_SET_DICT_FLOAT(KEY, VALUE)                   \
  do {                                                   \
    PyObject* v__ = PyFloat_FromDouble((double)(VALUE)); \
    if (v__ == NULL) {                                   \
      Py_DECREF(out);                                    \
      return NULL;                                       \
    }                                                    \
    if (PyDict_SetItemString(out, (KEY), v__) != 0) {    \
      Py_DECREF(v__);                                    \
      Py_DECREF(out);                                    \
      return NULL;                                       \
    }                                                    \
    Py_DECREF(v__);                                      \
  } while (0)
#define MSL_SET_DICT_HITBOX_U16_TABLE(KEY, ARR)                         \
  do {                                                                  \
    PyObject* lst__ = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);        \
    if (lst__ == NULL) {                                                \
      Py_DECREF(out);                                                   \
      return NULL;                                                      \
    }                                                                   \
    for (int hi__ = 0; hi__ < MSL_ITEM_ARTICLE_MAX_HITBOXES; hi__++) {  \
      PyList_SET_ITEM(lst__, hi__, PyLong_FromLong((long)(ARR)[hi__])); \
    }                                                                   \
    if (PyDict_SetItemString(out, (KEY), lst__) != 0) {                 \
      Py_DECREF(lst__);                                                 \
      Py_DECREF(out);                                                   \
      return NULL;                                                      \
    }                                                                   \
    Py_DECREF(lst__);                                                   \
  } while (0)
#define MSL_SET_DICT_HITBOX_FLOAT_TABLE(KEY, ARR)                            \
  do {                                                                       \
    PyObject* lst__ = PyList_New(MSL_ITEM_ARTICLE_MAX_HITBOXES);             \
    if (lst__ == NULL) {                                                     \
      Py_DECREF(out);                                                        \
      return NULL;                                                           \
    }                                                                        \
    for (int hi__ = 0; hi__ < MSL_ITEM_ARTICLE_MAX_HITBOXES; hi__++) {       \
      PyList_SET_ITEM(lst__, hi__, PyFloat_FromDouble((double)(ARR)[hi__])); \
    }                                                                        \
    if (PyDict_SetItemString(out, (KEY), lst__) != 0) {                      \
      Py_DECREF(lst__);                                                      \
      Py_DECREF(out);                                                        \
      return NULL;                                                           \
    }                                                                        \
    Py_DECREF(lst__);                                                        \
  } while (0)
  MSL_SET_DICT_HITBOX_U16_TABLE("needle_hitbox_bone_id", p->needle_hitbox_bone_id);
  MSL_SET_DICT_HITBOX_FLOAT_TABLE("needle_hitbox_jobj_x_offset", p->needle_hitbox_jobj_x_offset);
  MSL_SET_DICT_HITBOX_FLOAT_TABLE("needle_hitbox_jobj_y_offset", p->needle_hitbox_jobj_y_offset);
  MSL_SET_DICT_HITBOX_FLOAT_TABLE("needle_hitbox_jobj_z_offset", p->needle_hitbox_jobj_z_offset);
#undef MSL_SET_DICT_HITBOX_FLOAT_TABLE
#undef MSL_SET_DICT_HITBOX_U16_TABLE
  MSL_SET_DICT_LONG("vanish_hitbox_count", p->vanish_hitbox_count);
  MSL_SET_DICT_FLOAT("vanish_hitbox_damage", p->vanish_hitbox_damage);
  MSL_SET_DICT_FLOAT("vanish_hitbox_size", p->vanish_hitbox_size);
  MSL_SET_DICT_FLOAT("vanish_hitbox_x_offset", p->vanish_hitbox_x_offset);
  MSL_SET_DICT_FLOAT("vanish_hitbox_y_offset", p->vanish_hitbox_y_offset);
  MSL_SET_DICT_FLOAT("vanish_hitbox_z_offset", p->vanish_hitbox_z_offset);
  MSL_SET_DICT_LONG("vanish_hitbox_angle", p->vanish_hitbox_angle);
  MSL_SET_DICT_LONG("vanish_hitbox_kbg", p->vanish_hitbox_kbg);
  MSL_SET_DICT_LONG("vanish_hitbox_wsk", p->vanish_hitbox_wsk);
  MSL_SET_DICT_LONG("vanish_hitbox_bkb", p->vanish_hitbox_bkb);
  MSL_SET_DICT_LONG("vanish_hitbox_element", p->vanish_hitbox_element);
  MSL_SET_DICT_LONG("vanish_hitbox_shield_damage", p->vanish_hitbox_shield_damage);
  MSL_SET_DICT_LONG("vanish_hitbox_flags", p->vanish_hitbox_flags);
  MSL_SET_DICT_LONG("vanish_hitbox_size_keyframe_count", p->vanish_hitbox_size_keyframe_count);
  MSL_SET_DICT_LONG("vanish_hitbox_size_keyframe_frame_0", p->vanish_hitbox_size_keyframe_frame[0]);
  MSL_SET_DICT_FLOAT("vanish_hitbox_size_keyframe_value_0",
                     p->vanish_hitbox_size_keyframe_value[0]);
  MSL_SET_DICT_LONG("vanish_hitbox_size_keyframe_frame_1", p->vanish_hitbox_size_keyframe_frame[1]);
  MSL_SET_DICT_FLOAT("vanish_hitbox_size_keyframe_value_1",
                     p->vanish_hitbox_size_keyframe_value[1]);
  MSL_SET_DICT_LONG("vanish_hitbox_remove_frame", p->vanish_hitbox_remove_frame);
#define MSL_SET_DICT_FLOAT_TABLE(KEY, ARR)                                      \
  do {                                                                          \
    PyObject* lst__ = PyList_New(MSL_ITEM_ARTICLE_NEEDLE_DROP_TABLE_LEN);       \
    if (lst__ == NULL) {                                                        \
      Py_DECREF(out);                                                           \
      return NULL;                                                              \
    }                                                                           \
    for (int ti__ = 0; ti__ < MSL_ITEM_ARTICLE_NEEDLE_DROP_TABLE_LEN; ti__++) { \
      PyList_SET_ITEM(lst__, ti__, PyFloat_FromDouble((double)(ARR)[ti__]));    \
    }                                                                           \
    if (PyDict_SetItemString(out, (KEY), lst__) != 0) {                         \
      Py_DECREF(lst__);                                                         \
      Py_DECREF(out);                                                           \
      return NULL;                                                              \
    }                                                                           \
    Py_DECREF(lst__);                                                           \
  } while (0)
  MSL_SET_DICT_FLOAT_TABLE("needle_drop_min_vel_y", p->needle_drop_min_vel_y);
  MSL_SET_DICT_FLOAT_TABLE("needle_drop_gravity", p->needle_drop_gravity);
  MSL_SET_DICT_FLOAT_TABLE("needle_bounce_min_vel_y", p->needle_bounce_min_vel_y);
  MSL_SET_DICT_FLOAT_TABLE("needle_bounce_gravity", p->needle_bounce_gravity);
  MSL_SET_DICT_FLOAT_TABLE("needle_bounce_x_vel", p->needle_bounce_x_vel);
#undef MSL_SET_DICT_FLOAT_TABLE
  MSL_SET_DICT_LONG("sheik_chain_link_count", p->sheik_chain_link_count);
  MSL_SET_DICT_FLOAT("sheik_chain_segment_length", p->sheik_chain_segment_length);
  MSL_SET_DICT_FLOAT("sheik_chain_friction_x10", p->sheik_chain_friction_x10);
  MSL_SET_DICT_FLOAT("sheik_chain_friction_x14", p->sheik_chain_friction_x14);
  MSL_SET_DICT_FLOAT("sheik_chain_gravity", p->sheik_chain_gravity);
  MSL_SET_DICT_FLOAT("sheik_chain_decay_x34", p->sheik_chain_decay_x34);
  MSL_SET_DICT_FLOAT("sheik_chain_wall_bounce_x58", p->sheik_chain_wall_bounce_x58);
  MSL_SET_DICT_FLOAT("sheik_chain_attr_x4c", p->sheik_chain_attr_x4c);
  MSL_SET_DICT_FLOAT("sheik_chain_initial_vel_x50", p->sheik_chain_initial_vel_x50);
  MSL_SET_DICT_FLOAT("sheik_chain_attr_x54", p->sheik_chain_attr_x54);
#undef MSL_SET_DICT_LONG
#undef MSL_SET_DICT_FLOAT
  return out;
}

// Test-only inspector for the Sheik Chain solved Verlet geometry. Returns the live solved link
// world positions and the 4 fighter-HitCapsule world positions (it_802BCB88 stride map) for a given
// batch/player's Chain article, or None if no live solved Chain is present.
PyObject* msl_sheik_chain_debug_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  int player_index = 0;
  if (!PyArg_ParseTuple(args, "O|ii", &handle_obj, &batch_index, &player_index)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (batch_index < 0 || batch_index >= h->batch->batch_size || player_index < 0 ||
      player_index >= MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "batch_index/player_index out of range");
    return NULL;
  }
  const MslItemArticleParams* ap = item_article_params_get((uint8_t)MSL_CHAR_ID_SHEIK);
  if (ap == NULL || ap->sheik_chain_itkind == 0u) {
    Py_RETURN_NONE;
  }
  int found = -1;
  for (int it = 0; it < MSL_MAX_ITEMS; it++) {
    const size_t ii = (size_t)batch_index * (size_t)MSL_MAX_ITEMS + (size_t)it;
    if (h->batch->state.item_exists[ii] != 0u &&
        h->batch->state.item_type[ii] == ap->sheik_chain_itkind &&
        (int)h->batch->state.item_owner[ii] == player_index &&
        h->batch->state.item_sheik_chain_links_valid[ii] != 0u) {
      found = it;
      break;
    }
  }
  if (found < 0) {
    Py_RETURN_NONE;
  }
  const size_t ii = (size_t)batch_index * (size_t)MSL_MAX_ITEMS + (size_t)found;
  int n = (int)ap->sheik_chain_link_count;
  if (n > MSL_SHEIK_CHAIN_MAX_LINKS) {
    n = MSL_SHEIK_CHAIN_MAX_LINKS;
  }
  const size_t base = ii * (size_t)MSL_SHEIK_CHAIN_MAX_LINKS;
  PyObject* links = PyList_New(n);
  PyObject* velocities = PyList_New(n);
  PyObject* active_links = PyList_New(n);
  for (int i = 0; i < n; i++) {
    PyList_SET_ITEM(
        links, i,
        Py_BuildValue("ddd", (double)h->batch->state.item_sheik_chain_link_pos_x[base + (size_t)i],
                      (double)h->batch->state.item_sheik_chain_link_pos_y[base + (size_t)i],
                      (double)h->batch->state.item_sheik_chain_link_pos_z[base + (size_t)i]));
    PyList_SET_ITEM(
        velocities, i,
        Py_BuildValue("ddd", (double)h->batch->state.item_sheik_chain_link_vel_x[base + (size_t)i],
                      (double)h->batch->state.item_sheik_chain_link_vel_y[base + (size_t)i],
                      (double)h->batch->state.item_sheik_chain_link_vel_z[base + (size_t)i]));
    PyList_SET_ITEM(
        active_links, i,
        PyLong_FromLong((long)h->batch->state.item_sheik_chain_link_active[base + (size_t)i]));
  }
  const size_t hist_base = ii * (size_t)MSL_SHEIK_CHAIN_HISTORY_LEN;
  PyObject* history = PyList_New(MSL_SHEIK_CHAIN_HISTORY_LEN);
  for (int i = 0; i < MSL_SHEIK_CHAIN_HISTORY_LEN; i++) {
    PyList_SET_ITEM(
        history, i,
        Py_BuildValue("dd",
                      (double)h->batch->state.item_sheik_chain_history_x[hist_base + (size_t)i],
                      (double)h->batch->state.item_sheik_chain_history_y[hist_base + (size_t)i]));
  }
  const size_t fidx = (size_t)batch_index * (size_t)MSL_MAX_PLAYERS + (size_t)player_index;
  PyObject* hbs = PyList_New(MSL_MAX_HITBOXES);
  PyObject* hb_links = PyList_New(MSL_MAX_HITBOXES);
  for (int hb = 0; hb < MSL_MAX_HITBOXES; hb++) {
    float hx = 0.0f;
    float hy = 0.0f;
    const uint8_t link_idx =
        h->batch->state
            .item_sheik_chain_hitbox_link_idx[ii * (size_t)MSL_MAX_HITBOXES + (size_t)hb];
    uint8_t ok = 0u;
    if (link_idx != 0xFFu && link_idx < (uint8_t)n) {
      hx = h->batch->state.item_sheik_chain_link_pos_x[base + (size_t)link_idx];
      hy = h->batch->state.item_sheik_chain_link_pos_y[base + (size_t)link_idx];
      ok = h->batch->state.item_sheik_chain_hitcaps_active[ii] ? 1u : 0u;
    }
    PyList_SET_ITEM(hbs, hb, Py_BuildValue("ddi", (double)hx, (double)hy, (int)ok));
    PyList_SET_ITEM(hb_links, hb, PyLong_FromLong((long)link_idx));
  }
  PyObject* input_main = Py_BuildValue(
      "{s:i,s:i,s:i,s:i,s:d,s:d,s:d,s:d,s:d,s:d}", "x", (int)h->batch->state.input_main_x[fidx],
      "y", (int)h->batch->state.input_main_y[fidx], "prev_x",
      (int)h->batch->state.prev_input_main_x[fidx], "prev_y",
      (int)h->batch->state.prev_input_main_y[fidx], "unit_x",
      (double)stick_i8_to_unit(h->batch->state.input_main_x[fidx]), "unit_y",
      (double)stick_i8_to_unit(h->batch->state.input_main_y[fidx]), "prev_unit_x",
      (double)stick_i8_to_unit(h->batch->state.prev_input_main_x[fidx]), "prev_unit_y",
      (double)stick_i8_to_unit(h->batch->state.prev_input_main_y[fidx]), "chain_prev_unit_x",
      (double)h->batch->state.item_sheik_chain_prev_stick_x[ii], "chain_prev_unit_y",
      (double)h->batch->state.item_sheik_chain_prev_stick_y[ii]);
  PyObject* item_state = Py_BuildValue(
      "{s:i,s:d,s:d,s:d,s:d,s:d,s:d,s:d,s:i,s:d,s:d,s:d,s:i,s:d,s:d,s:d}", "state",
      (int)h->batch->state.item_state[ii], "item_x", (double)h->batch->state.item_pos_x[ii],
      "item_y", (double)h->batch->state.item_pos_y[ii], "owner_x",
      (double)h->batch->state.pos_x[fidx], "owner_y", (double)h->batch->state.pos_y[fidx],
      "owner_z", (double)h->batch->state.pos_z[fidx], "facing",
      h->batch->state.facing[fidx] ? 1.0 : -1.0, "action_frame",
      (double)h->batch->state.action_frame[fidx], "target_valid",
      (int)h->batch->state.item_sheik_chain_target_valid[ii], "target_x",
      (double)h->batch->state.item_sheik_chain_target_x[ii], "target_y",
      (double)h->batch->state.item_sheik_chain_target_y[ii], "target_z",
      (double)h->batch->state.item_sheik_chain_target_z[ii], "animation_index",
      (int)h->batch->state.animation_index[fidx], "anim_frame",
      (double)h->batch->state.anim_frame_f32[fidx], "chain_pose_angle",
      (double)h->batch->state.sheik_chain_pose_angle[fidx], "chain_pose_mag",
      (double)h->batch->state.sheik_chain_pose_mag[fidx]);
  return Py_BuildValue("{s:N,s:N,s:N,s:N,s:N,s:N,s:N,s:N,s:i,s:i,s:i}", "links", links,
                       "velocities", velocities, "active_links", active_links, "history", history,
                       "hitboxes", hbs, "hitbox_link_idx", hb_links, "input", input_main, "item",
                       item_state, "hitcaps_active",
                       (int)h->batch->state.item_sheik_chain_hitcaps_active[ii], "hit_cooldown",
                       (int)h->batch->state.item_sheik_chain_hit_cooldown[ii], "sheik_chain_x0",
                       (int)h->batch->state.sheik_special_timer[fidx]);
}

PyObject* msl_stage_floor_segment_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int stage_id_u = 0;
  unsigned int segment_i_u = 0;
  if (!PyArg_ParseTuple(args, "II", &stage_id_u, &segment_i_u)) {
    return NULL;
  }
  if (stage_collision_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "stage_collision_init failed");
    return NULL;
  }
  if (!stage_collision_require_stage((uint32_t)stage_id_u)) {
    PyErr_SetString(PyExc_RuntimeError, "requested stage collision artifact is unavailable");
    return NULL;
  }
  const MslStageFloorGraph* graph = stage_collision_get_floor_graph((uint32_t)stage_id_u);
  if (graph == NULL) {
    Py_RETURN_NONE;
  }
  const int idx = stage_collision_floor_line_index((uint32_t)stage_id_u, (uint16_t)segment_i_u);
  if (idx < 0 || (size_t)idx >= graph->line_count) {
    Py_RETURN_NONE;
  }
  const MslStageFloorLine* line = &graph->lines[(size_t)idx];
  return Py_BuildValue(
      "{s:i,s:f,s:f,s:f,s:f,s:f,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i}",
      "segment_i", (int)line->segment_i, "x0", (double)line->x0, "y0", (double)line->y0, "x1",
      (double)line->x1, "y1", (double)line->y1, "ground_friction_mul",
      (double)line->ground_friction_mul, "is_ledge", (int)line->is_ledge, "is_platform",
      (int)line->is_platform, "fighter_solid", (int)line->fighter_solid, "platform_transform_kind",
      (int)line->platform_transform_kind, "platform_transform_id", (int)line->platform_transform_id,
      "hi_flags", (int)line->hi_flags, "lo_flags", (int)line->lo_flags, "joint_id",
      (int)line->joint_id, "raw_prev_id", (int)line->raw_prev_id, "raw_next_id",
      (int)line->raw_next_id, "has_prev_link", (int)line->has_prev_link, "has_next_link",
      (int)line->has_next_link, "prev", (int)line->prev, "next", (int)line->next, "line_index",
      idx);
}

PyObject* msl_debug_stage_moving_floor_surface_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* handle_obj = NULL;
  int batch_index = 0;
  unsigned int segment_i_u = 0;
  if (!PyArg_ParseTuple(args, "OiI", &handle_obj, &batch_index, &segment_i_u)) {
    return NULL;
  }
  PyMslHandle* h = unpack_handle(handle_obj);
  if (h == NULL) {
    return NULL;
  }
  if (batch_index < 0 || batch_index >= h->batch->batch_size) {
    PyErr_SetString(PyExc_IndexError, "batch_index out of range");
    return NULL;
  }

  const uint32_t stage_id = h->batch->state.stage_id[(size_t)batch_index];
  const MslStageFloorGraph* graph = stage_collision_get_floor_graph(stage_id);
  if (graph == NULL) {
    Py_RETURN_NONE;
  }
  const int idx = stage_collision_floor_line_index(stage_id, (uint16_t)segment_i_u);
  if (idx < 0 || (size_t)idx >= graph->line_count) {
    Py_RETURN_NONE;
  }
  MslStageMovingSurfaceState surface = {0};
  if (!stage_collision_floor_line_moving_surface_state(h->batch, batch_index,
                                                       &graph->lines[(size_t)idx], &surface)) {
    Py_RETURN_NONE;
  }
  return Py_BuildValue(
      "{s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:f,s:f,s:f,s:f,s:f,s:f,s:f,s:"
      "f,s:f}",
      "valid", (int)surface.valid, "active", (int)surface.active, "visible", (int)surface.visible,
      "current_owned", (int)surface.current_owned, "source_trusted", (int)surface.source_trusted,
      "reached_hidden_this_step", (int)surface.reached_hidden_this_step, "platform_transform_kind",
      (int)surface.platform_transform_kind, "platform_transform_id",
      (int)surface.platform_transform_id, "stage_object_support_kind",
      (int)surface.stage_object_support_kind, "segment_i", (int)surface.segment_i, "joint_id",
      (int)surface.joint_id, "source_frame", (int)surface.source_frame, "source_phase",
      (int)surface.source_phase, "source_timer", (int)surface.source_timer, "source_bits",
      (int)surface.source_bits, "source_target", (double)surface.source_target, "x0",
      (double)surface.x0, "y0", (double)surface.y0, "x1", (double)surface.x1, "y1",
      (double)surface.y1, "normal_x", (double)surface.normal_x, "normal_y",
      (double)surface.normal_y, "velocity_x", (double)surface.velocity_x, "velocity_y",
      (double)surface.velocity_y);
}

PyObject* msl_stage_topology_flags_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int stage_id_u = 0;
  if (!PyArg_ParseTuple(args, "I", &stage_id_u)) {
    return NULL;
  }
  if (stage_collision_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "stage_collision_init failed");
    return NULL;
  }
  if (!stage_collision_require_stage((uint32_t)stage_id_u)) {
    PyErr_SetString(PyExc_RuntimeError, "requested stage collision artifact is unavailable");
    return NULL;
  }
  const uint32_t stage_id = (uint32_t)stage_id_u;
  return Py_BuildValue("{s:i,s:i,s:i}", "flat_between_sloped_ledges",
                       (int)stage_collision_stage_has_flat_between_sloped_ledges(stage_id),
                       "only_static_cardinal_hard_floors",
                       (int)stage_collision_stage_has_only_static_cardinal_hard_floors(stage_id),
                       "alternate_floor_endpoint_links",
                       (int)stage_collision_stage_has_alternate_floor_endpoint_links(stage_id));
}

PyObject* msl_stage_fighter_floor_segment_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int stage_id_u = 0;
  unsigned int segment_i_u = 0;
  if (!PyArg_ParseTuple(args, "II", &stage_id_u, &segment_i_u)) {
    return NULL;
  }
  if (stage_collision_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "stage_collision_init failed");
    return NULL;
  }
  if (!stage_collision_require_stage((uint32_t)stage_id_u)) {
    PyErr_SetString(PyExc_RuntimeError, "requested stage collision artifact is unavailable");
    return NULL;
  }
  const MslStageFloorGraph* graph = stage_collision_get_fighter_floor_graph((uint32_t)stage_id_u);
  if (graph == NULL) {
    Py_RETURN_NONE;
  }
  const int idx =
      stage_collision_fighter_floor_line_index((uint32_t)stage_id_u, (uint16_t)segment_i_u);
  if (idx < 0 || (size_t)idx >= graph->line_count) {
    Py_RETURN_NONE;
  }
  const MslStageFloorLine* line = &graph->lines[(size_t)idx];
  return Py_BuildValue(
      "{s:i,s:f,s:f,s:f,s:f,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i}",
      "segment_i", (int)line->segment_i, "x0", (double)line->x0, "y0", (double)line->y0, "x1",
      (double)line->x1, "y1", (double)line->y1, "is_ledge", (int)line->is_ledge, "is_platform",
      (int)line->is_platform, "fighter_solid", (int)line->fighter_solid, "platform_transform_kind",
      (int)line->platform_transform_kind, "platform_transform_id", (int)line->platform_transform_id,
      "hi_flags", (int)line->hi_flags, "lo_flags", (int)line->lo_flags, "joint_id",
      (int)line->joint_id, "raw_prev_id", (int)line->raw_prev_id, "raw_next_id",
      (int)line->raw_next_id, "has_prev_link", (int)line->has_prev_link, "has_next_link",
      (int)line->has_next_link, "prev", (int)line->prev, "next", (int)line->next, "line_index",
      idx);
}

PyObject* msl_stage_ceiling_segment_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int stage_id_u = 0;
  unsigned int segment_i_u = 0;
  if (!PyArg_ParseTuple(args, "II", &stage_id_u, &segment_i_u)) {
    return NULL;
  }
  if (stage_collision_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "stage_collision_init failed");
    return NULL;
  }
  if (!stage_collision_require_stage((uint32_t)stage_id_u)) {
    PyErr_SetString(PyExc_RuntimeError, "requested stage collision artifact is unavailable");
    return NULL;
  }
  const MslStageCeilingGraph* graph = stage_collision_get_ceiling_graph((uint32_t)stage_id_u);
  if (graph == NULL) {
    Py_RETURN_NONE;
  }
  const int idx = stage_collision_ceiling_line_index((uint32_t)stage_id_u, (uint16_t)segment_i_u);
  if (idx < 0 || (size_t)idx >= graph->line_count) {
    Py_RETURN_NONE;
  }
  const MslStageCeilingLine* line = &graph->lines[(size_t)idx];
  return Py_BuildValue(
      "{s:i,s:f,s:f,s:f,s:f,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i}", "segment_i",
      (int)line->segment_i, "x0", (double)line->x0, "y0", (double)line->y0, "x1", (double)line->x1,
      "y1", (double)line->y1, "fighter_solid", (int)line->fighter_solid, "hi_flags",
      (int)line->hi_flags, "lo_flags", (int)line->lo_flags, "joint_id", (int)line->joint_id,
      "raw_prev_id", (int)line->raw_prev_id, "raw_next_id", (int)line->raw_next_id, "has_prev_link",
      (int)line->has_prev_link, "has_next_link", (int)line->has_next_link, "prev", (int)line->prev,
      "next", (int)line->next, "line_index", idx);
}

PyObject* msl_stage_wall_segment_py(PyObject* self, PyObject* args, uint8_t left_wall) {
  (void)self;
  unsigned int stage_id_u = 0;
  unsigned int segment_i_u = 0;
  if (!PyArg_ParseTuple(args, "II", &stage_id_u, &segment_i_u)) {
    return NULL;
  }
  if (stage_collision_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "stage_collision_init failed");
    return NULL;
  }
  if (!stage_collision_require_stage((uint32_t)stage_id_u)) {
    PyErr_SetString(PyExc_RuntimeError, "requested stage collision artifact is unavailable");
    return NULL;
  }
  const MslStageWallGraph* graph = left_wall
                                       ? stage_collision_get_left_wall_graph((uint32_t)stage_id_u)
                                       : stage_collision_get_right_wall_graph((uint32_t)stage_id_u);
  if (graph == NULL) {
    Py_RETURN_NONE;
  }
  const int idx =
      left_wall
          ? stage_collision_left_wall_line_index((uint32_t)stage_id_u, (uint16_t)segment_i_u)
          : stage_collision_right_wall_line_index((uint32_t)stage_id_u, (uint16_t)segment_i_u);
  if (idx < 0 || (size_t)idx >= graph->line_count) {
    Py_RETURN_NONE;
  }
  const MslStageWallLine* line = &graph->lines[(size_t)idx];
  return Py_BuildValue(
      "{s:i,s:f,s:f,s:f,s:f,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i,s:i}", "segment_i",
      (int)line->segment_i, "x0", (double)line->x0, "y0", (double)line->y0, "x1", (double)line->x1,
      "y1", (double)line->y1, "fighter_solid", (int)line->fighter_solid, "hi_flags",
      (int)line->hi_flags, "lo_flags", (int)line->lo_flags, "joint_id", (int)line->joint_id,
      "raw_prev_id", (int)line->raw_prev_id, "raw_next_id", (int)line->raw_next_id, "has_prev_link",
      (int)line->has_prev_link, "has_next_link", (int)line->has_next_link, "prev", (int)line->prev,
      "next", (int)line->next, "line_index", idx);
}

PyObject* msl_stage_left_wall_segment_py(PyObject* self, PyObject* args) {
  return msl_stage_wall_segment_py(self, args, 1u);
}

PyObject* msl_stage_right_wall_segment_py(PyObject* self, PyObject* args) {
  return msl_stage_wall_segment_py(self, args, 0u);
}

PyObject* msl_stage_raw_line_non_kind_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int stage_id_u = 0;
  unsigned int segment_i_u = 0;
  unsigned int skip_kind_u = 0;
  int forward = 0;
  if (!PyArg_ParseTuple(args, "IIIp", &stage_id_u, &segment_i_u, &skip_kind_u, &forward)) {
    return NULL;
  }
  if (stage_collision_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "stage_collision_init failed");
    return NULL;
  }
  if (!stage_collision_require_stage((uint32_t)stage_id_u)) {
    PyErr_SetString(PyExc_RuntimeError, "requested stage collision artifact is unavailable");
    return NULL;
  }
  MslStageRawLineKind kind = MSL_STAGE_RAW_LINE_UNKNOWN;
  uint16_t out_segment_i = 0xFFFFu;
  const uint8_t ok = forward ? stage_collision_raw_line_next_non_kind(
                                   (uint32_t)stage_id_u, (uint16_t)segment_i_u,
                                   (MslStageRawLineKind)skip_kind_u, &kind, &out_segment_i)
                             : stage_collision_raw_line_prev_non_kind(
                                   (uint32_t)stage_id_u, (uint16_t)segment_i_u,
                                   (MslStageRawLineKind)skip_kind_u, &kind, &out_segment_i);
  if (!ok) {
    Py_RETURN_NONE;
  }
  return Py_BuildValue("{s:i,s:i}", "kind", (int)kind, "segment_i", (int)out_segment_i);
}

PyObject* msl_stage_static_query_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int stage_id_u = 0;
  unsigned int checks_u = 0;
  double x0 = 0.0;
  double y0 = 0.0;
  double x1 = 0.0;
  double y1 = 0.0;
  unsigned int line_id_skip_u = 0xFFFFu;
  int joint_id_skip = -1;
  int joint_id_only = -1;
  if (!PyArg_ParseTuple(args, "IIddddIii", &stage_id_u, &checks_u, &x0, &y0, &x1, &y1,
                        &line_id_skip_u, &joint_id_skip, &joint_id_only)) {
    return NULL;
  }
  if (stage_collision_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "stage_collision_init failed");
    return NULL;
  }
  if (!stage_collision_require_stage((uint32_t)stage_id_u)) {
    PyErr_SetString(PyExc_RuntimeError, "requested stage collision artifact is unavailable");
    return NULL;
  }
  MslStageQueryHit hit = {0};
  if (!stage_collision_static_query((uint32_t)stage_id_u, (uint32_t)checks_u, (float)x0, (float)y0,
                                    (float)x1, (float)y1, (uint16_t)line_id_skip_u,
                                    (int16_t)joint_id_skip, (int16_t)joint_id_only, &hit)) {
    Py_RETURN_NONE;
  }
  return Py_BuildValue("{s:i,s:i,s:i,s:i,s:f,s:f,s:f,s:f,s:f}", "kind", (int)hit.kind, "segment_i",
                       (int)hit.segment_i, "joint_id", (int)hit.joint_id, "flags", (int)hit.flags,
                       "x", (double)hit.x, "y", (double)hit.y, "normal_x", (double)hit.normal_x,
                       "normal_y", (double)hit.normal_y, "dist2", (double)hit.dist2);
}

PyObject* msl_mpcoll_check_bounding_aabb_py(PyObject* self, PyObject* args) {
  (void)self;
  double prev_pos_x = 0.0;
  double prev_pos_y = 0.0;
  double cur_pos_x = 0.0;
  double cur_pos_y = 0.0;
  double prev_left = 0.0;
  double prev_right = 0.0;
  double prev_bottom = 0.0;
  double prev_top = 0.0;
  double cur_left = 0.0;
  double cur_right = 0.0;
  double cur_bottom = 0.0;
  double cur_top = 0.0;
  unsigned int flags = 0u;
  double ledge_snap_x = 0.0;
  double ledge_snap_y = 0.0;
  double ledge_snap_height = 0.0;
  if (!PyArg_ParseTuple(args, "ddddddddddddIddd", &prev_pos_x, &prev_pos_y, &cur_pos_x, &cur_pos_y,
                        &prev_left, &prev_right, &prev_bottom, &prev_top, &cur_left, &cur_right,
                        &cur_bottom, &cur_top, &flags, &ledge_snap_x, &ledge_snap_y,
                        &ledge_snap_height)) {
    return NULL;
  }
  MslMpcollBoundingAabb aabb = {0};
  mpcoll_check_bounding_aabb(
      (float)prev_pos_x, (float)prev_pos_y, (float)cur_pos_x, (float)cur_pos_y, (float)prev_left,
      (float)prev_right, (float)prev_bottom, (float)prev_top, (float)cur_left, (float)cur_right,
      (float)cur_bottom, (float)cur_top, (uint32_t)flags, (float)ledge_snap_x, (float)ledge_snap_y,
      (float)ledge_snap_height, &aabb);
  return Py_BuildValue("{s:f,s:f,s:f,s:f}", "left", (double)aabb.left, "bottom",
                       (double)aabb.bottom, "right", (double)aabb.right, "top", (double)aabb.top);
}

PyObject* msl_mpcoll_end_publication_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int floor_segment_id = 0xFFFFu;
  unsigned int ceiling_segment_id = 0xFFFFu;
  unsigned int env_flags = 0u;
  int force_floor = 0;
  int floor_arg2 = 0;
  double cur_y = 0.0;
  double last_y = 0.0;
  if (!PyArg_ParseTuple(args, "IIIppdd", &floor_segment_id, &ceiling_segment_id, &env_flags,
                        &force_floor, &floor_arg2, &cur_y, &last_y)) {
    return NULL;
  }
  MslMpcollEndEvents ev = {0};
  msl_mpcoll_end_static_events((uint16_t)floor_segment_id, (uint16_t)ceiling_segment_id,
                               (uint32_t)env_flags, force_floor ? 1u : 0u, floor_arg2 ? 1u : 0u,
                               (float)cur_y, (float)last_y, &ev);
  return Py_BuildValue("{s:i,s:i,s:i,s:i,s:i,s:f}", "floor_callback", (int)ev.floor_callback,
                       "floor_callback_arg", (int)ev.floor_callback_arg, "ceiling_callback",
                       (int)ev.ceiling_callback, "floor_segment_id", (int)ev.floor_segment_id,
                       "ceiling_segment_id", (int)ev.ceiling_segment_id, "dy", (double)ev.dy);
}

PyObject* msl_stage_match_flow_roles_py(PyObject* self, PyObject* args) {
  (void)self;
  unsigned int stage_id_u = 0;
  if (!PyArg_ParseTuple(args, "I", &stage_id_u)) {
    return NULL;
  }
  if (stage_collision_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "stage_collision_init failed");
    return NULL;
  }
  if (!stage_collision_require_stage((uint32_t)stage_id_u)) {
    PyErr_SetString(PyExc_RuntimeError, "requested stage match-flow artifact is unavailable");
    return NULL;
  }
  MslStageBounds cam = {0};
  MslStageBounds blast = {0};
  if (!stage_collision_get_cam_bounds_world((uint32_t)stage_id_u, &cam) ||
      !stage_collision_get_blast_bounds_world((uint32_t)stage_id_u, &blast)) {
    Py_RETURN_NONE;
  }
  PyObject* spawn = PyList_New((Py_ssize_t)MSL_MAX_PLAYERS);
  PyObject* respawn = PyList_New((Py_ssize_t)MSL_MAX_PLAYERS);
  if (spawn == NULL || respawn == NULL) {
    Py_XDECREF(spawn);
    Py_XDECREF(respawn);
    return NULL;
  }
  for (int i = 0; i < MSL_MAX_PLAYERS; i++) {
    MslStagePoint2 sp = {0};
    MslStagePoint2 rp = {0};
    if (!stage_collision_get_spawn_point((uint32_t)stage_id_u, i, &sp) ||
        !stage_collision_get_respawn_point((uint32_t)stage_id_u, i, &rp)) {
      Py_DECREF(spawn);
      Py_DECREF(respawn);
      Py_RETURN_NONE;
    }
    PyObject* sp_obj = Py_BuildValue("(ff)", (double)sp.x, (double)sp.y);
    PyObject* rp_obj = Py_BuildValue("(ff)", (double)rp.x, (double)rp.y);
    if (sp_obj == NULL || rp_obj == NULL) {
      Py_XDECREF(sp_obj);
      Py_XDECREF(rp_obj);
      Py_DECREF(spawn);
      Py_DECREF(respawn);
      return NULL;
    }
    PyList_SET_ITEM(spawn, i, sp_obj);
    PyList_SET_ITEM(respawn, i, rp_obj);
  }
  return Py_BuildValue("{s:(ffff),s:(ffff),s:N,s:N}", "cam_bounds", (double)cam.left,
                       (double)cam.right, (double)cam.top, (double)cam.bottom, "blast_bounds",
                       (double)blast.left, (double)blast.right, (double)blast.top,
                       (double)blast.bottom, "spawn_points", spawn, "respawn_points", respawn);
}

PyObject* msl_move_tables_debug_query_py(PyObject* self, PyObject* args) {
  (void)self;
  const char* kind = NULL;
  int char_id = 0;
  int action_or_msid = 0;
  double a = 0.0;
  double b = 0.0;
  if (!PyArg_ParseTuple(args, "siidd", &kind, &char_id, &action_or_msid, &a, &b)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  if (strcmp(kind, "attackair_cmd0") == 0) {
    return PyLong_FromLong((long)move_tables_attackair_cmd0_active(
        (uint8_t)char_id, (uint16_t)action_or_msid, f32_from_double(a)));
  }
  if (strcmp(kind, "attackair_allow_interrupt") == 0) {
    return PyLong_FromLong((long)move_tables_attackair_allow_interrupt(
        (uint8_t)char_id, (uint16_t)action_or_msid, f32_from_double(a)));
  }
  if (strcmp(kind, "attackair_second_create_hitbox_phase") == 0) {
    return PyLong_FromLong((long)move_tables_attackair_second_create_hitbox_phase(
        (uint8_t)char_id, (uint16_t)action_or_msid, f32_from_double(a)));
  }
  if (strcmp(kind, "grounded_attack_allow_interrupt") == 0) {
    return PyLong_FromLong((long)move_tables_grounded_attack_allow_interrupt(
        (uint8_t)char_id, (uint16_t)action_or_msid, f32_from_double(a)));
  }
  if (strcmp(kind, "grounded_smash_charge_crossed") == 0) {
    uint8_t hold = 0u;
    const uint8_t ok = move_tables_grounded_smash_charge_crossed(
        (uint8_t)char_id, (uint16_t)action_or_msid, f32_from_double(a), f32_from_double(b), &hold);
    return Py_BuildValue("(ii)", (int)ok, (int)hold);
  }
  if (strcmp(kind, "grounded_smash_charge_info") == 0) {
    uint16_t frame = 0u;
    uint8_t hold = 0u;
    const uint8_t ok = move_tables_grounded_smash_charge_info(
        (uint8_t)char_id, (uint16_t)action_or_msid, &frame, &hold);
    return Py_BuildValue("(iii)", (int)ok, ok ? (int)frame : -1, ok ? (int)hold : 0);
  }
  if (strcmp(kind, "grounded_smash_charge_damage_mul") == 0) {
    return PyFloat_FromDouble((double)move_tables_grounded_smash_charge_damage_mul(
        (uint8_t)char_id, (uint16_t)action_or_msid));
  }
  if (strcmp(kind, "escape_allow_interrupt") == 0) {
    return PyLong_FromLong((long)move_tables_escape_allow_interrupt(
        (uint8_t)char_id, (uint16_t)action_or_msid, f32_from_double(a)));
  }
  if (strcmp(kind, "escapeair_cmd0") == 0) {
    return PyLong_FromLong(
        (long)move_tables_escapeair_cmd0_active((uint8_t)char_id, f32_from_double(a)));
  }
  if (strcmp(kind, "special_cmd0") == 0) {
    return PyLong_FromLong((long)move_tables_special_cmd0_active_at_frame(
        (uint8_t)char_id, (uint16_t)action_or_msid, (int)a));
  }
  if (strcmp(kind, "special_cmd2_pulse") == 0) {
    int16_t pulse_frame = -1;
    const uint8_t ok = move_tables_special_cmd2_pulse_crossed(
        (uint8_t)char_id, (uint16_t)action_or_msid, f32_from_double(a), f32_from_double(b),
        &pulse_frame);
    return Py_BuildValue("(ii)", (int)ok, ok ? (int)pulse_frame : -1);
  }
  if (strcmp(kind, "hit_status") == 0) {
    uint8_t status = 0u;
    const uint8_t ok = move_tables_hit_status_at_frame((uint8_t)char_id, (uint16_t)action_or_msid,
                                                       (uint16_t)a, &status);
    return Py_BuildValue("(ii)", (int)ok, (int)status);
  }
  if (strcmp(kind, "hurtbox_can_hit_mask") == 0) {
    uint32_t mask = 0u;
    const uint8_t ok = move_tables_hurtbox_can_hit_mask_at_frame(
        (uint8_t)char_id, (uint16_t)action_or_msid, (uint16_t)a, (uint16_t)b, &mask);
    return Py_BuildValue("(iI)", (int)ok, (unsigned int)mask);
  }
  if (strcmp(kind, "state_flags_221c_y") == 0) {
    uint8_t flags = 0u;
    const uint8_t ok = move_tables_state_flags_221c_y_at_frame(
        (uint8_t)char_id, (uint16_t)action_or_msid, (uint16_t)a, &flags);
    return Py_BuildValue("(ii)", (int)ok, (int)flags);
  }
  if (strcmp(kind, "airborne_state_event") == 0) {
    uint8_t state = 0xFFu;
    const uint8_t ok = move_tables_airborne_state_event_at_frame(
        (uint8_t)char_id, (uint16_t)action_or_msid, (uint16_t)a, &state);
    return Py_BuildValue("(ii)", (int)ok, ok ? (int)state : -1);
  }
  if (strcmp(kind, "escapef_flip") == 0) {
    return PyLong_FromLong(
        (long)move_tables_escapef_should_flip_facing((uint8_t)char_id, (int16_t)a, (int16_t)b));
  }
  if (strcmp(kind, "jab_combo") == 0) {
    return PyLong_FromLong((long)move_tables_jab_combo_active(
        (uint8_t)char_id, (uint16_t)action_or_msid, f32_from_double(a)));
  }
  if (strcmp(kind, "jab_rapid") == 0) {
    return PyLong_FromLong((long)move_tables_jab_rapid_active(
        (uint8_t)char_id, (uint16_t)action_or_msid, f32_from_double(a)));
  }
  if (strcmp(kind, "attack100_loop_end") == 0) {
    return PyLong_FromLong((long)move_tables_attack100_loop_end_check_crossed(
        (uint8_t)char_id, (int16_t)a, (int16_t)b));
  }
  if (strcmp(kind, "dash_cmd0") == 0) {
    return PyLong_FromLong(
        (long)move_tables_dash_cmd0_active((uint8_t)char_id, f32_from_double(a)));
  }
  if (strcmp(kind, "runbrake_cmd0") == 0) {
    return PyLong_FromLong(
        (long)move_tables_runbrake_cmd0_active((uint8_t)char_id, f32_from_double(a)));
  }
  if (strcmp(kind, "turnrun_cmd1") == 0) {
    return PyLong_FromLong(
        (long)move_tables_turnrun_cmd1_active((uint8_t)char_id, f32_from_double(a)));
  }
  if (strcmp(kind, "catchpull_enter_wait") == 0) {
    return PyLong_FromLong((long)move_tables_catchpull_should_enter_wait(
        (uint8_t)char_id, (uint16_t)action_or_msid, f32_from_double(a)));
  }
  if (strcmp(kind, "catchattack_grabbed_hit") == 0) {
    return PyLong_FromLong(
        (long)move_tables_catchattack_grabbed_hit_active((uint8_t)char_id, f32_from_double(a)));
  }
  PyErr_Format(PyExc_ValueError, "unknown move_tables debug query: %s", kind);
  return NULL;
}

PyObject* msl_move_tables_throw_has_release_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  if (!PyArg_ParseTuple(args, "ii", &char_id, &throw_action_id)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  const uint8_t has = move_tables_throw_has_release((uint8_t)char_id, (uint16_t)throw_action_id);
  return PyLong_FromLong((long)has);
}

PyObject* msl_move_tables_throw_release_frame_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  if (!PyArg_ParseTuple(args, "ii", &char_id, &throw_action_id)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }

  float release_af = 0.0f;
  const uint8_t ok =
      move_tables_throw_release_frame((uint8_t)char_id, (uint16_t)throw_action_id, &release_af);
  if (!ok) {
    return Py_BuildValue("(id)", 0, 0.0);
  }
  return Py_BuildValue("(id)", 1, (double)release_af);
}

PyObject* msl_move_tables_throw_release_hit_idx_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  double cur_anim_frame = 0.0;
  if (!PyArg_ParseTuple(args, "iid", &char_id, &throw_action_id, &cur_anim_frame)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }

  uint8_t hit_idx = 0;
  const uint8_t released = move_tables_throw_release_hit_idx(
      (uint8_t)char_id, (uint16_t)throw_action_id, f32_from_double(cur_anim_frame), &hit_idx);
  if (!released) {
    return Py_BuildValue("(ii)", 0, -1);
  }
  return Py_BuildValue("(ii)", 1, (int)hit_idx);
}

PyObject* msl_move_tables_throw_hitbox_params_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  int hit_idx = 0;
  if (!PyArg_ParseTuple(args, "iii", &char_id, &throw_action_id, &hit_idx)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }

  MslThrowHitboxParams p = {0};
  const uint8_t ok = move_tables_throw_hitbox_params((uint8_t)char_id, (uint16_t)throw_action_id,
                                                     (uint8_t)hit_idx, &p);
  if (!ok) {
    Py_RETURN_NONE;
  }

  return Py_BuildValue("(dIIIIIII)", (double)p.damage, (unsigned int)p.angle, (unsigned int)p.kbg,
                       (unsigned int)p.wsk, (unsigned int)p.bkb, (unsigned int)p.element,
                       (unsigned int)p.sfx_kind, (unsigned int)p.sfx_severity);
}

PyObject* msl_move_tables_throw_release_after_create_hitbox_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  if (!PyArg_ParseTuple(args, "ii", &char_id, &throw_action_id)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  const uint8_t has =
      move_tables_throw_release_after_create_hitbox((uint8_t)char_id, (uint16_t)throw_action_id);
  return PyLong_FromLong((long)has);
}

PyObject* msl_move_tables_throw_cmd1_active_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  double cur_anim_frame = 0.0;
  if (!PyArg_ParseTuple(args, "iid", &char_id, &throw_action_id, &cur_anim_frame)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  const uint8_t active = move_tables_throw_cmd1_active((uint8_t)char_id, (uint16_t)throw_action_id,
                                                       f32_from_double(cur_anim_frame));
  return PyLong_FromLong((long)active);
}

PyObject* msl_move_tables_throw_should_spawn_projectile_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  double prev_anim_frame = 0.0;
  double cur_anim_frame = 0.0;
  if (!PyArg_ParseTuple(args, "iidd", &char_id, &throw_action_id, &prev_anim_frame,
                        &cur_anim_frame)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  const uint8_t should = move_tables_throw_should_spawn_projectile(
      (uint8_t)char_id, (uint16_t)throw_action_id, f32_from_double(prev_anim_frame),
      f32_from_double(cur_anim_frame));
  return PyLong_FromLong((long)should);
}

enum { MSL_BINDING_MOVE_TABLES_PULSE_MAX = 16 };

PyObject* msl_move_tables_throw_should_flip_facing_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  double prev_anim_frame = 0.0;
  double cur_anim_frame = 0.0;
  if (!PyArg_ParseTuple(args, "iidd", &char_id, &throw_action_id, &prev_anim_frame,
                        &cur_anim_frame)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  const uint8_t should = move_tables_throw_should_flip_facing(
      (uint8_t)char_id, (uint16_t)throw_action_id, f32_from_double(prev_anim_frame),
      f32_from_double(cur_anim_frame));
  return PyLong_FromLong((long)should);
}

PyObject* msl_move_tables_throw_crossed_projectile_pulse_frame_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  double prev_anim_frame = 0.0;
  double cur_anim_frame = 0.0;
  if (!PyArg_ParseTuple(args, "iidd", &char_id, &throw_action_id, &prev_anim_frame,
                        &cur_anim_frame)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  int16_t pulse_frame = 0;
  const uint8_t crossed = move_tables_throw_crossed_projectile_pulse_frame(
      (uint8_t)char_id, (uint16_t)throw_action_id, f32_from_double(prev_anim_frame),
      f32_from_double(cur_anim_frame), &pulse_frame);
  if (!crossed) {
    return Py_BuildValue("(ii)", 0, -1);
  }
  return Py_BuildValue("(ii)", 1, (int)pulse_frame);
}

PyObject* msl_move_tables_throw_projectile_first_pulse_frame_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  if (!PyArg_ParseTuple(args, "ii", &char_id, &throw_action_id)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  int16_t pulse_frame = 0;
  const uint8_t ok = move_tables_throw_projectile_first_pulse_frame(
      (uint8_t)char_id, (uint16_t)throw_action_id, &pulse_frame);
  if (!ok) {
    return Py_BuildValue("(ii)", 0, -1);
  }
  return Py_BuildValue("(ii)", 1, (int)pulse_frame);
}

PyObject* msl_move_tables_throw_projectile_last_pulse_frame_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  if (!PyArg_ParseTuple(args, "ii", &char_id, &throw_action_id)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  int16_t pulse_frame = 0;
  const uint8_t ok = move_tables_throw_projectile_last_pulse_frame(
      (uint8_t)char_id, (uint16_t)throw_action_id, &pulse_frame);
  if (!ok) {
    return Py_BuildValue("(ii)", 0, -1);
  }
  return Py_BuildValue("(ii)", 1, (int)pulse_frame);
}

PyObject* msl_move_tables_throw_projectile_pulse_ordinal_py(PyObject* self, PyObject* args) {
  (void)self;
  int char_id = 0;
  int throw_action_id = 0;
  int pulse_frame = 0;
  if (!PyArg_ParseTuple(args, "iii", &char_id, &throw_action_id, &pulse_frame)) {
    return NULL;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  uint8_t ordinal = 0;
  const uint8_t ok = move_tables_throw_projectile_pulse_ordinal(
      (uint8_t)char_id, (uint16_t)throw_action_id, (int16_t)pulse_frame, &ordinal);
  if (!ok) {
    return Py_BuildValue("(ii)", 0, -1);
  }
  return Py_BuildValue("(ii)", 1, (int)ordinal);
}

PyObject* msl_move_tables_special_pseudo_random_sfx_ranges_crossed_py(PyObject* self,
                                                                      PyObject* args) {
  (void)self;
  int char_id = 0;
  int msid = 0;
  double prev_anim_frame = 0.0;
  double cur_anim_frame = 0.0;
  int max_out = 8;
  if (!PyArg_ParseTuple(args, "iidd|i", &char_id, &msid, &prev_anim_frame, &cur_anim_frame,
                        &max_out)) {
    return NULL;
  }
  if (max_out < 0) {
    max_out = 0;
  }
  if (max_out > MSL_BINDING_MOVE_TABLES_PULSE_MAX) {
    max_out = MSL_BINDING_MOVE_TABLES_PULSE_MAX;
  }
  if (move_tables_init() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "move_tables_init failed");
    return NULL;
  }
  uint8_t ranges[MSL_BINDING_MOVE_TABLES_PULSE_MAX] = {0};
  const uint8_t n = move_tables_special_pseudo_random_sfx_ranges_crossed(
      (uint8_t)char_id, (uint16_t)msid, f32_from_double(prev_anim_frame),
      f32_from_double(cur_anim_frame), ranges, (uint8_t)max_out);
  PyObject* out = PyTuple_New((Py_ssize_t)n);
  if (out == NULL) {
    return NULL;
  }
  for (uint8_t i = 0; i < n; i++) {
    PyObject* v = PyLong_FromLong((long)ranges[i]);
    if (v == NULL) {
      Py_DECREF(out);
      return NULL;
    }
    PyTuple_SET_ITEM(out, (Py_ssize_t)i, v);
  }
  return out;
}
