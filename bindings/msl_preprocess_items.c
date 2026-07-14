/* Native preprocessing for item/event validation derivation. */

#include "msl_preprocess.h"

#include "../src/api.h"

PyObject* msl_fill_items_fixed_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject *offsets_obj = NULL, *type_obj = NULL, *state_obj = NULL, *direction_obj = NULL;
  PyObject *vel_x_obj = NULL, *vel_y_obj = NULL, *pos_x_obj = NULL, *pos_y_obj = NULL;
  PyObject *damage_obj = NULL, *timer_obj = NULL, *spawn_obj = NULL, *misc0_obj = NULL;
  PyObject *misc1_obj = NULL, *misc2_obj = NULL, *misc3_obj = NULL, *owner_obj = NULL;
  PyObject *iid_obj = NULL, *owner_map_obj = NULL, *out_obj = NULL;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOOOOOOOOO", &offsets_obj, &type_obj, &state_obj,
                        &direction_obj, &vel_x_obj, &vel_y_obj, &pos_x_obj, &pos_y_obj, &damage_obj,
                        &timer_obj, &spawn_obj, &misc0_obj, &misc1_obj, &misc2_obj, &misc3_obj,
                        &owner_obj, &iid_obj, &owner_map_obj, &out_obj)) {
    return NULL;
  }

  PyArrayObject* offsets =
      require_contiguous_array_readonly(offsets_obj, NPY_INT32, 1, "item_offsets_i32");
  PyArrayObject* type = require_contiguous_array_readonly(type_obj, NPY_UINT16, 1, "item_type_u16");
  PyArrayObject* state =
      require_contiguous_array_readonly(state_obj, NPY_UINT8, 1, "item_state_u8");
  PyArrayObject* direction =
      require_contiguous_array_readonly(direction_obj, NPY_FLOAT32, 1, "item_direction_f32");
  PyArrayObject* vel_x =
      require_contiguous_array_readonly(vel_x_obj, NPY_FLOAT32, 1, "item_vel_x_f32");
  PyArrayObject* vel_y =
      require_contiguous_array_readonly(vel_y_obj, NPY_FLOAT32, 1, "item_vel_y_f32");
  PyArrayObject* pos_x =
      require_contiguous_array_readonly(pos_x_obj, NPY_FLOAT32, 1, "item_pos_x_f32");
  PyArrayObject* pos_y =
      require_contiguous_array_readonly(pos_y_obj, NPY_FLOAT32, 1, "item_pos_y_f32");
  PyArrayObject* damage =
      require_contiguous_array_readonly(damage_obj, NPY_UINT16, 1, "item_damage_u16");
  PyArrayObject* timer =
      require_contiguous_array_readonly(timer_obj, NPY_FLOAT32, 1, "item_timer_f32");
  PyArrayObject* spawn =
      require_contiguous_array_readonly(spawn_obj, NPY_UINT32, 1, "item_spawn_id_u32");
  PyArrayObject* misc0 =
      require_contiguous_array_readonly(misc0_obj, NPY_UINT8, 1, "item_misc0_u8");
  PyArrayObject* misc1 =
      require_contiguous_array_readonly(misc1_obj, NPY_UINT8, 1, "item_misc1_u8");
  PyArrayObject* misc2 =
      require_contiguous_array_readonly(misc2_obj, NPY_UINT8, 1, "item_misc2_u8");
  PyArrayObject* misc3 =
      require_contiguous_array_readonly(misc3_obj, NPY_UINT8, 1, "item_misc3_u8");
  PyArrayObject* owner = require_contiguous_array_readonly(owner_obj, NPY_INT8, 1, "item_owner_i8");
  PyArrayObject* iid =
      require_contiguous_array_readonly(iid_obj, NPY_UINT16, 1, "item_instance_id_u16");
  PyArrayObject* owner_map =
      require_contiguous_array_readonly(owner_map_obj, NPY_INT8, 1, "owner_map_i8");
  if (!PyArray_Check(out_obj)) {
    PyErr_SetString(PyExc_TypeError, "out_items must be a NumPy array");
    return NULL;
  }
  PyArrayObject* out = (PyArrayObject*)out_obj;
  if (offsets == NULL || type == NULL || state == NULL || direction == NULL || vel_x == NULL ||
      vel_y == NULL || pos_x == NULL || pos_y == NULL || damage == NULL || timer == NULL ||
      spawn == NULL || misc0 == NULL || misc1 == NULL || misc2 == NULL || misc3 == NULL ||
      owner == NULL || iid == NULL || owner_map == NULL) {
    return NULL;
  }
  const npy_intp n_frames = PyArray_DIM(offsets, 0) - 1;
  const npy_intp n_items = PyArray_DIM(type, 0);
  if (n_frames < 0 || PyArray_DIM(owner_map, 0) < 4 || PyArray_NDIM(out) != 2 ||
      PyArray_DIM(out, 0) != n_frames || PyArray_DIM(out, 1) != MSL_MAX_ITEMS ||
      PyArray_TYPE(out) != NPY_VOID || !PyArray_ISCARRAY(out) ||
      PyArray_ITEMSIZE(out) != (npy_intp)sizeof(MslItem) ||
      PyArray_STRIDES(out)[0] != (npy_intp)(sizeof(MslItem) * MSL_MAX_ITEMS) ||
      PyArray_STRIDES(out)[1] != (npy_intp)sizeof(MslItem)) {
    PyErr_SetString(PyExc_ValueError, "invalid fixed item output shape");
    return NULL;
  }
  PyArrayObject* arrays[] = {state, direction, vel_x, vel_y, pos_x, pos_y, damage, timer,
                             spawn, misc0,     misc1, misc2, misc3, owner, iid};
  for (size_t ai = 0; ai < sizeof(arrays) / sizeof(arrays[0]); ai++) {
    if (PyArray_DIM(arrays[ai], 0) != n_items) {
      PyErr_SetString(PyExc_ValueError, "flat item arrays must have matching lengths");
      return NULL;
    }
  }

  const int32_t* offp = (const int32_t*)PyArray_DATA(offsets);
  const uint16_t* typep = (const uint16_t*)PyArray_DATA(type);
  const uint8_t* statep = (const uint8_t*)PyArray_DATA(state);
  const float* dirp = (const float*)PyArray_DATA(direction);
  const float* vxp = (const float*)PyArray_DATA(vel_x);
  const float* vyp = (const float*)PyArray_DATA(vel_y);
  const float* pxp = (const float*)PyArray_DATA(pos_x);
  const float* pyp = (const float*)PyArray_DATA(pos_y);
  const uint16_t* dmgp = (const uint16_t*)PyArray_DATA(damage);
  const float* timerp = (const float*)PyArray_DATA(timer);
  const uint32_t* spawnp = (const uint32_t*)PyArray_DATA(spawn);
  const uint8_t* misc0p = (const uint8_t*)PyArray_DATA(misc0);
  const uint8_t* misc1p = (const uint8_t*)PyArray_DATA(misc1);
  const uint8_t* misc2p = (const uint8_t*)PyArray_DATA(misc2);
  const uint8_t* misc3p = (const uint8_t*)PyArray_DATA(misc3);
  const int8_t* ownerp = (const int8_t*)PyArray_DATA(owner);
  const uint16_t* iidp = (const uint16_t*)PyArray_DATA(iid);
  const int8_t* owner_mapp = (const int8_t*)PyArray_DATA(owner_map);
  char* out_base = PyArray_BYTES(out);
  const npy_intp out_row_stride = PyArray_STRIDES(out)[0];
  const npy_intp out_slot_stride = PyArray_STRIDES(out)[1];

  for (npy_intp fi = 0; fi < n_frames; fi++) {
    const int32_t start_i32 = offp[fi];
    const int32_t stop_i32 = offp[fi + 1];
    if (start_i32 < 0 || stop_i32 < start_i32 || stop_i32 > n_items) {
      PyErr_SetString(PyExc_ValueError, "invalid item offsets");
      return NULL;
    }
    uint32_t cand[MSL_MAX_ITEMS];
    uint16_t cand_iid[MSL_MAX_ITEMS];
    uint32_t cand_spawn[MSL_MAX_ITEMS];
    uint16_t cand_type[MSL_MAX_ITEMS];
    int cand_count = 0;
    for (int32_t idx = start_i32; idx < stop_i32; idx++) {
      const uint16_t key_iid = iidp[idx];
      const uint32_t key_spawn = spawnp[idx];
      const uint16_t key_type = typep[idx];
      int insert = cand_count;
      while (insert > 0 && item_key_less(key_iid, key_spawn, key_type, cand_iid[insert - 1],
                                         cand_spawn[insert - 1], cand_type[insert - 1])) {
        insert--;
      }
      if (insert >= MSL_MAX_ITEMS) {
        continue;
      }
      if (cand_count < MSL_MAX_ITEMS) {
        cand_count++;
      }
      for (int j = cand_count - 1; j > insert; j--) {
        cand[j] = cand[j - 1];
        cand_iid[j] = cand_iid[j - 1];
        cand_spawn[j] = cand_spawn[j - 1];
        cand_type[j] = cand_type[j - 1];
      }
      cand[insert] = (uint32_t)idx;
      cand_iid[insert] = key_iid;
      cand_spawn[insert] = key_spawn;
      cand_type[insert] = key_type;
    }
    for (int slot = 0; slot < cand_count; slot++) {
      const uint32_t idx = cand[slot];
      MslItem* item = (MslItem*)(void*)(out_base + fi * out_row_stride + slot * out_slot_stride);
      item->exists = 1u;
      item->state = statep[idx];
      item->type = typep[idx];
      const int8_t raw_owner = ownerp[idx];
      item->owner = (raw_owner >= 0 && raw_owner < 4) ? owner_mapp[(int)raw_owner] : -1;
      item->instance_id = iidp[idx];
      item->direction = dirp[idx];
      item->vel_x = vxp[idx];
      item->vel_y = vyp[idx];
      item->pos_x = pxp[idx];
      item->pos_y = pyp[idx];
      item->damage = dmgp[idx];
      item->timer = timerp[idx];
      item->spawn_id = spawnp[idx];
      item->misc0 = misc0p[idx];
      item->misc1 = misc1p[idx];
      item->misc2 = misc2p[idx];
      item->misc3 = misc3p[idx];
    }
  }
  Py_RETURN_NONE;
}

PyObject* msl_derive_item_spawn_id_counter_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* exists_obj = NULL;
  PyObject* spawn_obj = NULL;
  if (!PyArg_ParseTuple(args, "OO", &exists_obj, &spawn_obj)) {
    return NULL;
  }
  PyArrayObject* exists = require_contiguous_array(exists_obj, NPY_UINT8, 2, "item_exists_u8_2d");
  PyArrayObject* spawn = require_contiguous_array(spawn_obj, NPY_UINT32, 2, "item_spawn_id_u32_2d");
  if (exists == NULL || spawn == NULL) {
    return NULL;
  }
  if (PyArray_NDIM(exists) != 2 || PyArray_NDIM(spawn) != 2) {
    PyErr_SetString(PyExc_ValueError,
                    "item_exists_u8_2d and item_spawn_id_u32_2d must be exactly 2D");
    return NULL;
  }
  const npy_intp n = PyArray_DIM(spawn, 0);
  const npy_intp w = PyArray_DIM(spawn, 1);
  if (require_exact_2d_shape(exists, n, w, "item_exists_u8_2d") != 0) {
    return NULL;
  }
  npy_intp dims[1] = {n};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(1, dims, NPY_UINT32, 0);
  if (out == NULL) {
    return NULL;
  }
  const uint8_t* exists_p = (const uint8_t*)PyArray_DATA(exists);
  const uint32_t* spawn_p = (const uint32_t*)PyArray_DATA(spawn);
  uint32_t* out_p = (uint32_t*)PyArray_DATA(out);
  uint32_t max_seen = 0u;
  uint8_t any_seen = 0u;
  for (npy_intp i = 0; i < n; i++) {
    uint32_t row_max = 0u;
    uint8_t row_any = 0u;
    for (npy_intp k = 0; k < w; k++) {
      const npy_intp ix = i * w + k;
      if (exists_p[ix] != 0u) {
        row_any = 1u;
        if (spawn_p[ix] > row_max) {
          row_max = spawn_p[ix];
        }
      }
    }
    if (row_any) {
      any_seen = 1u;
      if (row_max > max_seen) {
        max_seen = row_max;
      }
    }
    out_p[i] = any_seen ? (max_seen + 1u) : 0u;
  }
  return (PyObject*)out;
}

PyObject* msl_derive_item_attack_fields_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* item_exists_obj = NULL;
  PyObject* item_type_obj = NULL;
  PyObject* item_owner_obj = NULL;
  PyObject* item_spawn_id_obj = NULL;
  PyObject* fighter_attack_id_obj = NULL;
  PyObject* fighter_attack_instance_obj = NULL;
  PyObject* prev_frame_spawn_kind_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOi|O", &item_exists_obj, &item_type_obj, &item_owner_obj,
                        &item_spawn_id_obj, &fighter_attack_id_obj, &fighter_attack_instance_obj,
                        &num_players, &prev_frame_spawn_kind_obj)) {
    return NULL;
  }
  PyArrayObject* item_exists =
      require_contiguous_array(item_exists_obj, NPY_UINT8, 2, "item_exists_u8");
  PyArrayObject* item_type =
      require_contiguous_array(item_type_obj, NPY_UINT16, 2, "item_type_u16");
  PyArrayObject* item_owner =
      require_contiguous_array(item_owner_obj, NPY_INT8, 2, "item_owner_i8");
  PyArrayObject* item_spawn_id =
      require_contiguous_array(item_spawn_id_obj, NPY_UINT32, 2, "item_spawn_id_u32");
  PyArrayObject* fighter_attack_id =
      require_contiguous_array(fighter_attack_id_obj, NPY_UINT16, 2, "fighter_attack_id_u16");
  PyArrayObject* fighter_attack_instance = require_contiguous_array(
      fighter_attack_instance_obj, NPY_UINT16, 2, "fighter_attack_instance_u16");
  PyArrayObject* prev_frame_spawn_kind = NULL;
  if (prev_frame_spawn_kind_obj != NULL && prev_frame_spawn_kind_obj != Py_None) {
    prev_frame_spawn_kind = require_contiguous_array(prev_frame_spawn_kind_obj, NPY_UINT16, 1,
                                                     "prev_frame_spawn_kind_u16");
  }
  if (item_exists == NULL || item_type == NULL || item_owner == NULL || item_spawn_id == NULL ||
      fighter_attack_id == NULL || fighter_attack_instance == NULL ||
      (prev_frame_spawn_kind_obj != NULL && prev_frame_spawn_kind_obj != Py_None &&
       prev_frame_spawn_kind == NULL)) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(item_exists, 0);
  const npy_intp slots = PyArray_DIM(item_exists, 1);
  if (require_exact_2d_shape(item_type, n, slots, "item_type_u16") < 0 ||
      require_exact_2d_shape(item_owner, n, slots, "item_owner_i8") < 0 ||
      require_exact_2d_shape(item_spawn_id, n, slots, "item_spawn_id_u32") < 0) {
    return NULL;
  }
  if (PyArray_NDIM(fighter_attack_id) != 2 || PyArray_NDIM(fighter_attack_instance) != 2 ||
      PyArray_DIM(fighter_attack_id, 0) != n || PyArray_DIM(fighter_attack_instance, 0) != n ||
      PyArray_DIM(fighter_attack_id, 1) != PyArray_DIM(fighter_attack_instance, 1)) {
    PyErr_SetString(PyExc_ValueError, "fighter attack arrays must share shape [frames, players]");
    return NULL;
  }
  const npy_intp width = PyArray_DIM(fighter_attack_id, 1);
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS || width < num_players) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for fighter width");
    return NULL;
  }
  const int players = num_players;
  npy_intp dims[2] = {n, slots};
  PyArrayObject* out_attack_id = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  PyArrayObject* out_attack_instance = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT16, 0);
  if (out_attack_id == NULL || out_attack_instance == NULL) {
    Py_XDECREF(out_attack_id);
    Py_XDECREF(out_attack_instance);
    return NULL;
  }
  const uint8_t* exists = (const uint8_t*)PyArray_DATA(item_exists);
  const uint16_t* type = (const uint16_t*)PyArray_DATA(item_type);
  const int8_t* owner_data = (const int8_t*)PyArray_DATA(item_owner);
  const uint32_t* spawn = (const uint32_t*)PyArray_DATA(item_spawn_id);
  const uint16_t* fighter_aid = (const uint16_t*)PyArray_DATA(fighter_attack_id);
  const uint16_t* fighter_ainst = (const uint16_t*)PyArray_DATA(fighter_attack_instance);
  const uint16_t* prev_kind =
      prev_frame_spawn_kind != NULL ? (const uint16_t*)PyArray_DATA(prev_frame_spawn_kind) : NULL;
  const npy_intp prev_kind_count =
      prev_frame_spawn_kind != NULL ? PyArray_SIZE(prev_frame_spawn_kind) : 0;
  uint16_t* out_aid = (uint16_t*)PyArray_DATA(out_attack_id);
  uint16_t* out_ainst = (uint16_t*)PyArray_DATA(out_attack_instance);
  uint32_t active_spawn[15] = {0};
  uint16_t active_type[15] = {0};
  uint16_t active_aid[15] = {0};
  uint16_t active_ainst[15] = {0};
  bool active_valid[15] = {false};
  for (npy_intp i = 0; i < n; i++) {
    bool seen[15] = {false};
    const int scan_slots = slots < 15 ? (int)slots : 15;
    for (int slot = 0; slot < scan_slots; slot++) {
      const npy_intp item_idx = (i * slots) + slot;
      if (exists[item_idx] == 0u) continue;
      const uint32_t key_spawn = spawn[item_idx];
      const uint16_t key_type = type[item_idx];
      int active_idx = -1;
      for (int k = 0; k < 15; k++) {
        if (active_valid[k] && active_spawn[k] == key_spawn && active_type[k] == key_type) {
          active_idx = k;
          break;
        }
      }
      if (active_idx < 0) {
        for (int k = 0; k < 15; k++) {
          if (!active_valid[k]) {
            active_idx = k;
            break;
          }
        }
        if (active_idx < 0) active_idx = 0;
        active_valid[active_idx] = true;
        active_spawn[active_idx] = key_spawn;
        active_type[active_idx] = key_type;
        const int owner = (int)owner_data[item_idx];
        if (owner >= 0 && owner < players) {
          npy_intp owner_frame = i;
          if (prev_kind != NULL && i > 0) {
            bool use_prev_owner_frame = false;
            for (npy_intp k = 0; k < prev_kind_count; k++) {
              if (prev_kind[k] == key_type) {
                use_prev_owner_frame = true;
                break;
              }
            }
            if (use_prev_owner_frame) {
              const npy_intp cur_owner_idx = (i * width) + owner;
              const npy_intp prev_owner_idx = ((i - 1) * width) + owner;
              // Source owner: Item_80268B18/it_8027B0C4 copies xD88/xD8C at the live item-spawn
              // callback. Slippi first serializes newly-created Fox/Falco laser shots in the
              // post-frame after that callback, by which point the fighter can already have left
              // Blaster Loop and reset x2068/x206C to FtMoveId_Default. For data-backed laser shot
              // kinds, repair only that first-visibility timing gap by using the previous
              // post-frame owner identity when the current owner identity is already default.
              // refs/melee/src/melee/it/items/itfoxlaser.c::it_8029C504
              // refs/melee/src/melee/it/it_2725.c::{it_8027B0C4,it_8027B070}
              if ((fighter_aid[cur_owner_idx] == 1u || fighter_ainst[cur_owner_idx] == 0u) &&
                  fighter_aid[prev_owner_idx] != 1u && fighter_ainst[prev_owner_idx] != 0u) {
                owner_frame = i - 1;
              }
            }
          }
          active_aid[active_idx] = fighter_aid[(owner_frame * width) + owner];
          active_ainst[active_idx] = fighter_ainst[(owner_frame * width) + owner];
        } else {
          active_aid[active_idx] = 1u;
          active_ainst[active_idx] = 0u;
        }
      }
      seen[active_idx] = true;
      out_aid[item_idx] = active_aid[active_idx];
      out_ainst[item_idx] = active_ainst[active_idx];
    }
    for (int k = 0; k < 15; k++) {
      if (active_valid[k] && !seen[k]) active_valid[k] = false;
    }
  }
  return Py_BuildValue("NN", out_attack_id, out_attack_instance);
}

PyObject* msl_derive_item_reflect_damage_mul_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* item_exists_obj = NULL;
  PyObject* item_type_obj = NULL;
  PyObject* item_owner_obj = NULL;
  PyObject* item_instance_id_obj = NULL;
  PyObject* item_vel_x_obj = NULL;
  PyObject* item_vel_y_obj = NULL;
  PyObject* item_spawn_id_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* reflector_lut_obj = NULL;
  double powershield_mul = 1.0;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOOdi", &item_exists_obj, &item_type_obj, &item_owner_obj,
                        &item_instance_id_obj, &item_vel_x_obj, &item_vel_y_obj, &item_spawn_id_obj,
                        &action_obj, &char_obj, &flags_obj, &reflector_lut_obj, &powershield_mul,
                        &num_players)) {
    return NULL;
  }
  PyArrayObject* item_exists =
      require_contiguous_array(item_exists_obj, NPY_UINT8, 2, "item_exists_u8");
  PyArrayObject* item_type =
      require_contiguous_array(item_type_obj, NPY_UINT16, 2, "item_type_u16");
  PyArrayObject* item_owner =
      require_contiguous_array(item_owner_obj, NPY_INT8, 2, "item_owner_i8");
  PyArrayObject* item_instance_id =
      require_contiguous_array(item_instance_id_obj, NPY_UINT16, 2, "item_instance_id_u16");
  PyArrayObject* item_vel_x =
      require_contiguous_array(item_vel_x_obj, NPY_FLOAT32, 2, "item_vel_x_f32");
  PyArrayObject* item_vel_y =
      require_contiguous_array(item_vel_y_obj, NPY_FLOAT32, 2, "item_vel_y_f32");
  PyArrayObject* item_spawn_id =
      require_contiguous_array(item_spawn_id_obj, NPY_UINT32, 2, "item_spawn_id_u32");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* chr = require_contiguous_array(char_obj, NPY_UINT8, 2, "char_id_u8");
  PyArrayObject* flags = require_contiguous_array(flags_obj, NPY_UINT8, 3, "state_flags_u8");
  PyArrayObject* reflector_lut =
      require_contiguous_array(reflector_lut_obj, NPY_FLOAT32, 1, "reflector_damage_mul_lut");
  if (item_exists == NULL || item_type == NULL || item_owner == NULL || item_instance_id == NULL ||
      item_vel_x == NULL || item_vel_y == NULL || item_spawn_id == NULL || action == NULL ||
      chr == NULL || flags == NULL || reflector_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(item_exists, 0);
  const npy_intp slots = PyArray_DIM(item_exists, 1);
  if (require_exact_2d_shape(item_type, n, slots, "item_type_u16") < 0 ||
      require_exact_2d_shape(item_owner, n, slots, "item_owner_i8") < 0 ||
      require_exact_2d_shape(item_instance_id, n, slots, "item_instance_id_u16") < 0 ||
      require_exact_2d_shape(item_vel_x, n, slots, "item_vel_x_f32") < 0 ||
      require_exact_2d_shape(item_vel_y, n, slots, "item_vel_y_f32") < 0 ||
      require_exact_2d_shape(item_spawn_id, n, slots, "item_spawn_id_u32") < 0) {
    return NULL;
  }
  if (PyArray_NDIM(action) != 2 || PyArray_NDIM(chr) != 2 || PyArray_DIM(action, 0) != n ||
      PyArray_DIM(chr, 0) != n || PyArray_DIM(action, 1) != PyArray_DIM(chr, 1) ||
      PyArray_NDIM(flags) != 3 || PyArray_DIM(flags, 0) != n ||
      PyArray_DIM(flags, 1) != PyArray_DIM(action, 1) || PyArray_DIM(flags, 2) < 4 ||
      PyArray_SIZE(reflector_lut) < 256) {
    PyErr_SetString(PyExc_ValueError,
                    "item reflect inputs have incompatible fighter/state flag shapes");
    return NULL;
  }
  const npy_intp width = PyArray_DIM(action, 1);
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS || width < num_players) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for fighter width");
    return NULL;
  }
  const int players = num_players;
  npy_intp dims[2] = {n, slots};
  PyArrayObject* out = (PyArrayObject*)PyArray_EMPTY(2, dims, NPY_FLOAT32, 0);
  if (out == NULL) return NULL;
  float* out_mul = (float*)PyArray_DATA(out);
  for (npy_intp k = 0; k < n * slots; k++) out_mul[k] = 1.0f;
  const uint8_t* exists = (const uint8_t*)PyArray_DATA(item_exists);
  const uint16_t* type = (const uint16_t*)PyArray_DATA(item_type);
  const int8_t* owner_data = (const int8_t*)PyArray_DATA(item_owner);
  const uint16_t* iid_data = (const uint16_t*)PyArray_DATA(item_instance_id);
  const float* vx_data = (const float*)PyArray_DATA(item_vel_x);
  const float* vy_data = (const float*)PyArray_DATA(item_vel_y);
  const uint32_t* spawn = (const uint32_t*)PyArray_DATA(item_spawn_id);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const uint8_t* flags_data = (const uint8_t*)PyArray_DATA(flags);
  const float* reflector_lut_data = (const float*)PyArray_DATA(reflector_lut);
  uint32_t active_spawn[15] = {0};
  uint16_t active_type[15] = {0};
  float active_mul[15] = {0};
  int active_owner[15] = {0};
  int active_iid[15] = {0};
  float active_vx[15] = {0};
  float active_vy[15] = {0};
  bool active_valid[15] = {false};
  for (npy_intp i = 0; i < n; i++) {
    bool seen[15] = {false};
    const int scan_slots = slots < 15 ? (int)slots : 15;
    for (int slot = 0; slot < scan_slots; slot++) {
      const npy_intp item_idx = (i * slots) + slot;
      if (exists[item_idx] == 0u) continue;
      const uint32_t key_spawn = spawn[item_idx];
      const uint16_t key_type = type[item_idx];
      const int owner = (int)owner_data[item_idx];
      const int iid = (int)iid_data[item_idx];
      const float vx = vx_data[item_idx];
      const float vy = vy_data[item_idx];
      int active_idx = -1;
      for (int k = 0; k < 15; k++) {
        if (active_valid[k] && active_spawn[k] == key_spawn && active_type[k] == key_type) {
          active_idx = k;
          break;
        }
      }
      if (active_idx < 0) {
        for (int k = 0; k < 15; k++) {
          if (!active_valid[k]) {
            active_idx = k;
            break;
          }
        }
        if (active_idx < 0) active_idx = 0;
        active_valid[active_idx] = true;
        active_spawn[active_idx] = key_spawn;
        active_type[active_idx] = key_type;
        active_mul[active_idx] = 1.0f;
        active_owner[active_idx] = owner;
        active_iid[active_idx] = iid;
        active_vx[active_idx] = vx;
        active_vy[active_idx] = vy;
      }
      float mul = active_mul[active_idx];
      const int prev_owner = active_owner[active_idx];
      const int prev_iid = active_iid[active_idx];
      const float prev_vx = active_vx[active_idx];
      const float prev_vy = active_vy[active_idx];
      const bool owner_changed = owner != prev_owner;
      const bool instance_transfer = iid != prev_iid;
      const float prev_speed_sq = prev_vx * prev_vx + prev_vy * prev_vy;
      const float cur_speed_sq = vx * vx + vy * vy;
      const bool reversed_vel_same_owner =
          owner == prev_owner && !instance_transfer && prev_speed_sq > 1.0e-6f &&
          cur_speed_sq > 1.0e-6f && (prev_vx * vx + prev_vy * vy) < 0.0f &&
          fabsf(cur_speed_sq - prev_speed_sq) <= 0.25f * fmaxf(prev_speed_sq, cur_speed_sq);
      if ((owner_changed || instance_transfer || reversed_vel_same_owner) && owner >= 0 &&
          owner < players) {
        const npy_intp fighter_idx = (i * width) + owner;
        const uint16_t act = action_data[fighter_idx];
        const uint8_t flags3 = flags_data[((i * width + owner) * PyArray_DIM(flags, 2)) + 3];
        if (act == 0x00B6u && (flags3 & 0x20u)) {
          mul = powershield_mul > 0.0 ? (float)powershield_mul : 1.0f;
        } else {
          const float char_mul = reflector_lut_data[char_data[fighter_idx]];
          if (char_mul > 0.0f) mul = char_mul;
        }
      }
      active_mul[active_idx] = mul;
      active_owner[active_idx] = owner;
      active_iid[active_idx] = iid;
      active_vx[active_idx] = vx;
      active_vy[active_idx] = vy;
      seen[active_idx] = true;
      out_mul[item_idx] = mul;
    }
    for (int k = 0; k < 15; k++) {
      if (active_valid[k] && !seen[k]) active_valid[k] = false;
    }
  }
  return (PyObject*)out;
}

PyObject* msl_derive_illusion_seed_position_updates_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* item_exists_obj = NULL;
  PyObject* item_type_obj = NULL;
  PyObject* item_owner_obj = NULL;
  PyObject* item_iid_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* instance_hit_by_obj = NULL;
  PyObject* ghost_x_obj = NULL;
  PyObject* ghost_y_obj = NULL;
  PyObject* illusion_lut_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOOi", &item_exists_obj, &item_type_obj, &item_owner_obj,
                        &item_iid_obj, &action_obj, &hitlag_obj, &instance_hit_by_obj, &ghost_x_obj,
                        &ghost_y_obj, &illusion_lut_obj, &num_players)) {
    return NULL;
  }
  PyArrayObject* item_exists =
      require_contiguous_array(item_exists_obj, NPY_UINT8, 2, "item_exists_u8");
  PyArrayObject* item_type =
      require_contiguous_array(item_type_obj, NPY_UINT16, 2, "item_type_u16");
  PyArrayObject* item_owner =
      require_contiguous_array(item_owner_obj, NPY_INT8, 2, "item_owner_i8");
  PyArrayObject* item_iid =
      require_contiguous_array(item_iid_obj, NPY_UINT16, 2, "item_instance_id_u16");
  PyArrayObject* action = require_contiguous_array(action_obj, NPY_UINT16, 2, "action_id_u16");
  PyArrayObject* hitlag = require_contiguous_array(hitlag_obj, NPY_UINT8, 2, "hitlag_u8");
  PyArrayObject* instance_hit_by =
      require_contiguous_array(instance_hit_by_obj, NPY_UINT16, 2, "instance_hit_by_u16");
  PyArrayObject* ghost_x = require_contiguous_array(ghost_x_obj, NPY_FLOAT32, 2, "ghost_x_f32");
  PyArrayObject* ghost_y = require_contiguous_array(ghost_y_obj, NPY_FLOAT32, 2, "ghost_y_f32");
  PyArrayObject* illusion_lut =
      require_contiguous_array(illusion_lut_obj, NPY_UINT8, 1, "illusion_item_kind_lut");
  if (item_exists == NULL || item_type == NULL || item_owner == NULL || item_iid == NULL ||
      action == NULL || hitlag == NULL || instance_hit_by == NULL || ghost_x == NULL ||
      ghost_y == NULL || illusion_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(item_exists, 0);
  const npy_intp slots = PyArray_DIM(item_exists, 1);
  if (require_exact_2d_shape(item_type, n, slots, "item_type_u16") < 0 ||
      require_exact_2d_shape(item_owner, n, slots, "item_owner_i8") < 0 ||
      require_exact_2d_shape(item_iid, n, slots, "item_instance_id_u16") < 0) {
    return NULL;
  }
  if (PyArray_NDIM(action) != 2 || PyArray_NDIM(hitlag) != 2 ||
      PyArray_NDIM(instance_hit_by) != 2 || PyArray_NDIM(ghost_x) != 2 ||
      PyArray_NDIM(ghost_y) != 2 || PyArray_DIM(action, 0) != n || PyArray_DIM(hitlag, 0) != n ||
      PyArray_DIM(instance_hit_by, 0) != n || PyArray_DIM(ghost_x, 0) != n ||
      PyArray_DIM(ghost_y, 0) != n || PyArray_DIM(action, 1) != PyArray_DIM(hitlag, 1) ||
      PyArray_DIM(action, 1) != PyArray_DIM(instance_hit_by, 1) ||
      PyArray_DIM(action, 1) != PyArray_DIM(ghost_x, 1) ||
      PyArray_DIM(action, 1) != PyArray_DIM(ghost_y, 1) || PyArray_SIZE(illusion_lut) < 65536) {
    PyErr_SetString(PyExc_ValueError, "illusion seed position inputs have incompatible shapes");
    return NULL;
  }
  const npy_intp width = PyArray_DIM(action, 1);
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS || width < num_players) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range for fighter width");
    return NULL;
  }
  const int players = num_players;
  npy_intp dims[2] = {n, slots};
  PyArrayObject* mask = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_UINT8, 0);
  PyArrayObject* out_x = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  PyArrayObject* out_y = (PyArrayObject*)PyArray_ZEROS(2, dims, NPY_FLOAT32, 0);
  if (mask == NULL || out_x == NULL || out_y == NULL) {
    Py_XDECREF(mask);
    Py_XDECREF(out_x);
    Py_XDECREF(out_y);
    return NULL;
  }
  const uint8_t* exists = (const uint8_t*)PyArray_DATA(item_exists);
  const uint16_t* type = (const uint16_t*)PyArray_DATA(item_type);
  const int8_t* owner_data = (const int8_t*)PyArray_DATA(item_owner);
  const uint16_t* iid_data = (const uint16_t*)PyArray_DATA(item_iid);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* hitlag_data = (const uint8_t*)PyArray_DATA(hitlag);
  const uint16_t* ihb_data = (const uint16_t*)PyArray_DATA(instance_hit_by);
  const float* gx = (const float*)PyArray_DATA(ghost_x);
  const float* gy = (const float*)PyArray_DATA(ghost_y);
  const uint8_t* illusion = (const uint8_t*)PyArray_DATA(illusion_lut);
  uint8_t* m = (uint8_t*)PyArray_DATA(mask);
  float* ox = (float*)PyArray_DATA(out_x);
  float* oy = (float*)PyArray_DATA(out_y);
  for (npy_intp fi = 1; fi < n; fi++) {
    for (npy_intp slot = 0; slot < slots; slot++) {
      const npy_intp item_idx = (fi * slots) + slot;
      if (exists[item_idx] == 0u || illusion[type[item_idx]] == 0u) continue;
      const int owner = (int)owner_data[item_idx];
      if (owner < 0 || owner >= players) continue;
      const uint16_t owner_action = action_data[(fi * width) + owner];
      const bool setphys = owner_action == 348u || owner_action == 349u || owner_action == 351u ||
                           owner_action == 352u;
      if (!setphys) continue;
      bool ongoing_guardsetoff_hitlag = false;
      int candidate_victim = -1;
      for (int p = 0; p < players; p++) {
        const npy_intp pidx = (fi * width) + p;
        if (hitlag_data[pidx] == 0u || action_data[pidx] != 181u) continue;
        if (candidate_victim >= 0) {
          candidate_victim = -2;
          break;
        }
        candidate_victim = p;
      }
      if (candidate_victim >= 0) {
        int illusion_candidates = 0;
        for (npy_intp other = 0; other < slots; other++) {
          const npy_intp other_idx = (fi * slots) + other;
          if (exists[other_idx] != 0u && illusion[type[other_idx]] != 0u) {
            illusion_candidates++;
            if (illusion_candidates > 1) break;
          }
        }
        ongoing_guardsetoff_hitlag = illusion_candidates == 1;
      }
      bool ongoing_body_hitlag = false;
      if (!ongoing_guardsetoff_hitlag) {
        for (int p = 0; p < players; p++) {
          const npy_intp pidx = (fi * width) + p;
          if (hitlag_data[pidx] != 0u && ihb_data[pidx] == iid_data[item_idx]) {
            ongoing_body_hitlag = true;
            break;
          }
        }
      }
      if (ongoing_guardsetoff_hitlag || ongoing_body_hitlag) continue;
      m[item_idx] = 1u;
      ox[item_idx] = gx[(fi * width) + owner];
      oy[item_idx] = gy[(fi * width) + owner];
    }
  }
  return Py_BuildValue("NNN", mask, out_x, out_y);
}
