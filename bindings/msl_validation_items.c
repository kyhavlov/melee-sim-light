/* Native validation item/event materialization. */

#define PY_SSIZE_T_CLEAN
#define PY_ARRAY_UNIQUE_SYMBOL MSL_BINDING_ARRAY_API
#define NO_IMPORT_ARRAY

#include "msl_validation_items.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../src/api.h"

static int require_u8_rows(PyArrayObject* arr, npy_intp rows, npy_intp min_cols, const char* name) {
  if (PyArray_NDIM(arr) != 2 || PyArray_DIM(arr, 0) != rows || PyArray_DIM(arr, 1) < min_cols) {
    PyErr_Format(PyExc_ValueError, "%s must be uint8[%zd, >=%zd]", name, rows, min_cols);
    return -1;
  }
  return 0;
}

static inline bool guard_family_action(uint16_t action) { return action >= 178u && action <= 182u; }

static bool throw_laser_grabbed_victim_action(uint16_t action_id) {
  switch (action_id) {
    case 223:
    case 224:
    case 225:
    case 226:
    case 227:
    case 228:
    case 231:
    case 232:
    case 239:
    case 240:
    case 241:
    case 242:
    case 243:
      return true;
    default:
      return false;
  }
}

PyObject* msl_validation_copy_item_rows_with_illusion_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* ref_obj = NULL;
  PyObject* items_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* instance_hit_by_obj = NULL;
  PyObject* ghost_x_obj = NULL;
  PyObject* ghost_y_obj = NULL;
  PyObject* illusion_lut_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOOOOi", &seed_obj, &ref_obj, &items_obj, &action_obj,
                        &hitlag_obj, &instance_hit_by_obj, &ghost_x_obj, &ghost_y_obj,
                        &illusion_lut_obj, &num_players)) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  PyArrayObject* seed = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* ref = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  PyArrayObject* items = require_contiguous_array_readonly(items_obj, NPY_UINT8, 2, "items_u8");
  PyArrayObject* action = require_contiguous_array_readonly(action_obj, NPY_UINT16, 2, "action_id");
  PyArrayObject* hitlag = require_contiguous_array_readonly(hitlag_obj, NPY_UINT16, 2, "hitlag");
  PyArrayObject* instance_hit_by =
      require_contiguous_array_readonly(instance_hit_by_obj, NPY_UINT16, 2, "instance_hit_by");
  PyArrayObject* ghost_x =
      require_contiguous_array_readonly(ghost_x_obj, NPY_FLOAT32, 2, "ghost_x");
  PyArrayObject* ghost_y =
      require_contiguous_array_readonly(ghost_y_obj, NPY_FLOAT32, 2, "ghost_y");
  PyArrayObject* illusion_lut =
      require_contiguous_array_readonly(illusion_lut_obj, NPY_UINT8, 1, "illusion_item_kind_lut");
  if (seed == NULL || ref == NULL || items == NULL || action == NULL || hitlag == NULL ||
      instance_hit_by == NULL || ghost_x == NULL || ghost_y == NULL || illusion_lut == NULL) {
    return NULL;
  }

  const npy_intp n = PyArray_DIM(seed, 0);
  const npy_intp frames = n + 1;
  const npy_intp width = PyArray_DIM(action, 1);
  const size_t items_bytes = sizeof(((MslSeed*)0)->items);
  if (n < 1 || PyArray_DIM(ref, 0) != n || PyArray_DIM(items, 0) != frames ||
      PyArray_DIM(items, 1) != (npy_intp)items_bytes ||
      require_u8_rows(seed, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      require_u8_rows(ref, n, (npy_intp)sizeof(MslCompare), "ref_u8") != 0 ||
      PyArray_NDIM(action) != 2 || PyArray_DIM(action, 0) != frames || PyArray_NDIM(hitlag) != 2 ||
      PyArray_DIM(hitlag, 0) != frames || PyArray_DIM(hitlag, 1) != width ||
      PyArray_NDIM(instance_hit_by) != 2 || PyArray_DIM(instance_hit_by, 0) != frames ||
      PyArray_DIM(instance_hit_by, 1) != width || PyArray_NDIM(ghost_x) != 2 ||
      PyArray_DIM(ghost_x, 0) != frames || PyArray_DIM(ghost_x, 1) != width ||
      PyArray_NDIM(ghost_y) != 2 || PyArray_DIM(ghost_y, 0) != frames ||
      PyArray_DIM(ghost_y, 1) != width || PyArray_SIZE(illusion_lut) < 65536) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError,
                      "validation item illusion buffers have incompatible shapes");
    }
    return NULL;
  }
  if (num_players > width) {
    PyErr_SetString(PyExc_ValueError, "num_players exceeds replay player field width");
    return NULL;
  }
  const int players = num_players;

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed);
  uint8_t* ref_u8 = (uint8_t*)PyArray_DATA(ref);
  const uint8_t* items_u8 = (const uint8_t*)PyArray_DATA(items);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint16_t* hitlag_data = (const uint16_t*)PyArray_DATA(hitlag);
  const uint16_t* ihb_data = (const uint16_t*)PyArray_DATA(instance_hit_by);
  const float* gx = (const float*)PyArray_DATA(ghost_x);
  const float* gy = (const float*)PyArray_DATA(ghost_y);
  const uint8_t* illusion = (const uint8_t*)PyArray_DATA(illusion_lut);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref, 0);
  const size_t items_stride = (size_t)PyArray_STRIDE(items, 0);

  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed_row = (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride);
    MslCompare* ref_row = (MslCompare*)(void*)(ref_u8 + (size_t)i * ref_stride);
    memcpy(seed_row->items, items_u8 + (size_t)i * items_stride, items_bytes);
    memcpy(ref_row->items, items_u8 + (size_t)(i + 1) * items_stride, items_bytes);

    if (i == 0) {
      continue;
    }
    for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
      MslItem* item = &seed_row->items[slot];
      if (item->exists == 0u || illusion[item->type] == 0u) {
        continue;
      }
      const int owner = (int)item->owner;
      if (owner < 0 || owner >= players) {
        continue;
      }
      const uint16_t owner_action = action_data[(i * width) + owner];
      const bool setphys = owner_action == 348u || owner_action == 349u || owner_action == 351u ||
                           owner_action == 352u;
      if (!setphys) {
        continue;
      }

      bool ongoing_guardsetoff_hitlag = false;
      int candidate_victim = -1;
      for (int p = 0; p < players; p++) {
        const npy_intp pidx = (i * width) + p;
        if (hitlag_data[pidx] == 0u || action_data[pidx] != 181u) {
          continue;
        }
        if (candidate_victim >= 0) {
          candidate_victim = -2;
          break;
        }
        candidate_victim = p;
      }
      if (candidate_victim >= 0) {
        int illusion_candidates = 0;
        for (int other = 0; other < MSL_MAX_ITEMS; other++) {
          const MslItem* other_item = &seed_row->items[other];
          if (other_item->exists != 0u && illusion[other_item->type] != 0u) {
            illusion_candidates++;
            if (illusion_candidates > 1) {
              break;
            }
          }
        }
        ongoing_guardsetoff_hitlag = illusion_candidates == 1;
      }

      bool ongoing_body_hitlag = false;
      if (!ongoing_guardsetoff_hitlag) {
        for (int p = 0; p < players; p++) {
          const npy_intp pidx = (i * width) + p;
          if (hitlag_data[pidx] != 0u && ihb_data[pidx] == item->instance_id) {
            ongoing_body_hitlag = true;
            break;
          }
        }
      }
      if (ongoing_guardsetoff_hitlag || ongoing_body_hitlag) {
        continue;
      }
      item->pos_x = gx[(i * width) + owner];
      item->pos_y = gy[(i * width) + owner];
    }
  }
  Py_RETURN_NONE;
}

PyObject* msl_validation_derive_throw_laser_item_hitlist_buffers_py(PyObject* self,
                                                                    PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* mask_lut_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOi", &seed_obj, &mask_lut_obj, &num_players)) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* mask_lut =
      require_contiguous_array_readonly(mask_lut_obj, NPY_UINT8, 1, "throw_laser_hitbox_mask_lut");
  if (seed_arr == NULL || mask_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  if (require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      PyArray_SIZE(mask_lut) < 65536) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "throw laser hitlist validation inputs are invalid");
    }
    return NULL;
  }
  const int players = num_players;

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const uint8_t* mask = (const uint8_t*)PyArray_DATA(mask_lut);

  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed = (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride);
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      seed->item_hitlist_victim_port[it] = 0xFFu;
      seed->item_hitlist_victim_cd[it] = 0u;
      seed->item_hitlist_victim_hitbox_mask[it] = 0u;
      seed->item_hitlist_victim_iid[it] = 0u;
    }
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const MslItem* item = &seed->items[it];
      if (item->exists == 0u || item->state != 1u) {
        continue;
      }
      const uint8_t hb_mask = mask[item->type];
      if (hb_mask == 0u) {
        continue;
      }
      const int owner = (int)item->owner;
      if (owner < 0 || owner >= players) {
        continue;
      }
      const uint16_t owner_action = seed->action_id[owner];
      if (!(owner_action == 219u || owner_action == 220u || owner_action == 221u ||
            owner_action == 222u)) {
        continue;
      }
      const uint16_t laser_iid = item->instance_id;
      if (laser_iid == 0u) {
        continue;
      }
      int candidate = -1;
      for (int victim = 0; victim < players; victim++) {
        if (victim == owner) {
          continue;
        }
        if (seed->grab_owner_port[victim] != (uint8_t)owner) {
          continue;
        }
        if (!throw_laser_grabbed_victim_action(seed->action_id[victim])) {
          continue;
        }
        if (seed->instance_hit_by[victim] != laser_iid) {
          continue;
        }
        if (candidate >= 0) {
          candidate = -1;
          break;
        }
        candidate = victim;
      }
      if (candidate < 0) {
        continue;
      }
      seed->item_hitlist_victim_port[it] = (uint8_t)candidate;
      seed->item_hitlist_victim_cd[it] = 16u;
      seed->item_hitlist_victim_hitbox_mask[it] = hb_mask;
      seed->item_hitlist_victim_iid[it] = seed->instance_id[candidate];
    }
  }
  Py_RETURN_NONE;
}

PyObject* msl_validation_derive_item_hidden_callback_buffers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* ref_obj = NULL;
  PyObject* laser_lut_obj = NULL;
  PyObject* shield_bounce_lut_obj = NULL;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOi", &seed_obj, &ref_obj, &laser_lut_obj, &shield_bounce_lut_obj,
                        &num_players)) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* ref_arr = require_contiguous_array(ref_obj, NPY_UINT8, 2, "ref_u8");
  PyArrayObject* laser_lut =
      require_contiguous_array(laser_lut_obj, NPY_UINT8, 1, "laser_type_lut");
  PyArrayObject* shield_bounce_lut =
      require_contiguous_array(shield_bounce_lut_obj, NPY_UINT8, 1, "shield_bounce_type_lut");
  if (seed_arr == NULL || ref_arr == NULL || laser_lut == NULL || shield_bounce_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  if (PyArray_DIM(ref_arr, 0) != n ||
      require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      require_u8_rows(ref_arr, n, (npy_intp)sizeof(MslCompare), "ref_u8") != 0 ||
      PyArray_SIZE(laser_lut) < 65536 || PyArray_SIZE(shield_bounce_lut) < 65536) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "item hidden callback validation inputs are invalid");
    }
    return NULL;
  }
  const int players = num_players;

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* ref_u8 = (const uint8_t*)PyArray_DATA(ref_arr);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t ref_stride = (size_t)PyArray_STRIDE(ref_arr, 0);
  const uint8_t* laser = (const uint8_t*)PyArray_DATA(laser_lut);
  const uint8_t* shield_bounce = (const uint8_t*)PyArray_DATA(shield_bounce_lut);

  for (npy_intp i = 0; i < n; i++) {
    MslSeed* seed = (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride);
    const MslCompare* ref = (const MslCompare*)(const void*)(ref_u8 + (size_t)i * ref_stride);
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      seed->item_reflect_transfer_port[it] = 0xFFu;
      seed->item_reflect_transfer_iid[it] = 0u;
      seed->item_shield_bounce_valid[it] = 0u;
      seed->item_shield_bounce_vel_x[it] = 0.0f;
      seed->item_shield_bounce_vel_y[it] = 0.0f;
      seed->item_hidden_body_hit_victim_port[it] = 0xFFu;
      seed->item_hidden_body_hit_hurt_height[it] = 0u;
      seed->item_hidden_callback_flags[it] = 0u;
    }
    bool guard_context = false;
    for (int p = 0; p < players; p++) {
      if (guard_family_action(seed->action_id[p]) || guard_family_action(ref->action_id[p])) {
        guard_context = true;
        break;
      }
    }
    for (int it = 0; it < MSL_MAX_ITEMS; it++) {
      const MslItem* si = &seed->items[it];
      const MslItem* ri = &ref->items[it];
      if (si->exists == 0u) {
        continue;
      }
      const uint16_t item_type = si->type;
      const bool is_laser = laser[item_type] != 0u;
      const bool is_shield_bounce_item = shield_bounce[item_type] != 0u;
      if (!is_laser && !is_shield_bounce_item) {
        continue;
      }
      const bool ref_exists_now = ri->exists != 0u;
      const bool ref_same_item = ref_exists_now && ri->type == item_type &&
                                 ri->instance_id == si->instance_id && ri->spawn_id == si->spawn_id;
      if (is_laser && guard_context && ref_exists_now && ri->type == item_type &&
          ri->spawn_id == si->spawn_id) {
        const int ref_item_owner = (int)ri->owner;
        const int seed_item_owner = (int)si->owner;
        if (ref_item_owner >= 0 && ref_item_owner < players &&
            (ref_item_owner != seed_item_owner || ri->instance_id != si->instance_id)) {
          seed->item_reflect_transfer_port[it] = (uint8_t)ref_item_owner;
          seed->item_reflect_transfer_iid[it] = ri->instance_id;
        }
      }
      if (is_shield_bounce_item && guard_context && ref_same_item && ri->owner == si->owner) {
        const float dx = ri->vel_x - si->vel_x;
        const float dy = ri->vel_y - si->vel_y;
        const float ref_speed2 = (ri->vel_x * ri->vel_x) + (ri->vel_y * ri->vel_y);
        if (isfinite(si->vel_x) && isfinite(si->vel_y) && isfinite(ri->vel_x) &&
            isfinite(ri->vel_y) && (dx * dx + dy * dy) > 1.0e-6f && ref_speed2 > 1.0e-6f) {
          // Source owner: ShieldBounced item callback state (`xC54/xC58`) is hidden but exposed by
          // same-item velocity change under Guard/GuardReflect context.
          // refs/melee/src/melee/ft/ftcoll.c::{ftColl_80077688,ftColl_8007925C}
          // refs/melee/src/melee/it/item.c::Item_80269DC8
          seed->item_shield_bounce_valid[it] = 1u;
          seed->item_shield_bounce_vel_x[it] = ri->vel_x;
          seed->item_shield_bounce_vel_y[it] = ri->vel_y;
        }
      }
      if (is_laser && !ref_exists_now && !ref_same_item && isfinite(si->direction) &&
          isfinite(si->vel_x) && si->direction * si->vel_x < 0.0f) {
        uint8_t victim = 0xFFu;
        for (int p = 0; p < players; p++) {
          if (ref->hitlag[p] == 0u || ref->hitstun[p] == 0u ||
              ref->instance_hit_by[p] != si->instance_id) {
            continue;
          }
          if (victim != 0xFFu) {
            victim = 0xFEu;
            break;
          }
          victim = (uint8_t)p;
        }
        if (victim < 0xFEu) {
          // Source owner: reflected laser OnGiveDamage latch (`xC34_damageDealt`) is hidden, but a
          // unique next-row victim with this item instance exposes the callback owner.
          // refs/melee/src/melee/it/item.c::{OnGiveDamageThink,Item_8026A294,Item_80269F14}
          seed->item_hidden_body_hit_victim_port[it] = victim;
          seed->item_hidden_body_hit_hurt_height[it] = 1u;
          seed->item_hidden_callback_flags[it] = 1u;
        }
      }
    }
  }
  Py_RETURN_NONE;
}

PyObject* msl_validation_derive_item_reflect_damage_mul_buffers_py(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj = NULL;
  PyObject* items_obj = NULL;
  PyObject* action_obj = NULL;
  PyObject* char_obj = NULL;
  PyObject* flags_obj = NULL;
  PyObject* reflector_lut_obj = NULL;
  double powershield_mul = 1.0;
  int num_players = 0;
  if (!PyArg_ParseTuple(args, "OOOOOOdi", &seed_obj, &items_obj, &action_obj, &char_obj, &flags_obj,
                        &reflector_lut_obj, &powershield_mul, &num_players)) {
    return NULL;
  }
  if (num_players < 0 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }
  PyArrayObject* seed_arr = require_contiguous_array(seed_obj, NPY_UINT8, 2, "seed_u8");
  PyArrayObject* items_arr = require_contiguous_array_readonly(items_obj, NPY_UINT8, 2, "items_u8");
  PyArrayObject* action = require_contiguous_array_readonly(action_obj, NPY_UINT16, 2, "action_id");
  PyArrayObject* chr = require_contiguous_array_readonly(char_obj, NPY_UINT8, 2, "char_id");
  PyArrayObject* flags = require_contiguous_array_readonly(flags_obj, NPY_UINT8, 3, "state_flags");
  PyArrayObject* reflector_lut =
      require_contiguous_array_readonly(reflector_lut_obj, NPY_FLOAT32, 1, "reflector_lut");
  if (seed_arr == NULL || items_arr == NULL || action == NULL || chr == NULL || flags == NULL ||
      reflector_lut == NULL) {
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed_arr, 0);
  const npy_intp frames = n + 1;
  const npy_intp width = PyArray_DIM(action, 1);
  const size_t items_bytes = sizeof(((MslSeed*)0)->items);
  if (PyArray_DIM(items_arr, 0) != frames || PyArray_DIM(items_arr, 1) != (npy_intp)items_bytes ||
      require_u8_rows(seed_arr, n, (npy_intp)sizeof(MslSeed), "seed_u8") != 0 ||
      PyArray_NDIM(action) != 2 || PyArray_DIM(action, 0) != frames || PyArray_NDIM(chr) != 2 ||
      PyArray_DIM(chr, 0) != frames || PyArray_DIM(chr, 1) != width || PyArray_NDIM(flags) != 3 ||
      PyArray_DIM(flags, 0) != frames || PyArray_DIM(flags, 1) != width ||
      PyArray_DIM(flags, 2) < 4 || PyArray_SIZE(reflector_lut) < 256) {
    if (!PyErr_Occurred()) {
      PyErr_SetString(PyExc_ValueError, "item reflect validation buffers are invalid");
    }
    return NULL;
  }
  if (num_players > width) {
    PyErr_SetString(PyExc_ValueError, "num_players exceeds replay player field width");
    return NULL;
  }
  const int players = num_players;

  uint8_t* seed_u8 = (uint8_t*)PyArray_DATA(seed_arr);
  const uint8_t* items_u8 = (const uint8_t*)PyArray_DATA(items_arr);
  const uint16_t* action_data = (const uint16_t*)PyArray_DATA(action);
  const uint8_t* char_data = (const uint8_t*)PyArray_DATA(chr);
  const uint8_t* flags_data = (const uint8_t*)PyArray_DATA(flags);
  const float* reflector = (const float*)PyArray_DATA(reflector_lut);
  const size_t seed_stride = (size_t)PyArray_STRIDE(seed_arr, 0);
  const size_t items_stride = (size_t)PyArray_STRIDE(items_arr, 0);
  const npy_intp flags_depth = PyArray_DIM(flags, 2);

  uint32_t active_spawn[MSL_MAX_ITEMS] = {0};
  uint16_t active_type[MSL_MAX_ITEMS] = {0};
  float active_mul[MSL_MAX_ITEMS] = {0.0f};
  int active_owner[MSL_MAX_ITEMS] = {0};
  int active_iid[MSL_MAX_ITEMS] = {0};
  float active_vx[MSL_MAX_ITEMS] = {0.0f};
  float active_vy[MSL_MAX_ITEMS] = {0.0f};
  bool active_valid[MSL_MAX_ITEMS] = {false};

  for (npy_intp i = 0; i < frames; i++) {
    bool seen[MSL_MAX_ITEMS] = {false};
    const MslItem* items = (const MslItem*)(const void*)(items_u8 + (size_t)i * items_stride);
    MslSeed* seed = i < n ? (MslSeed*)(void*)(seed_u8 + (size_t)i * seed_stride) : NULL;
    if (seed != NULL) {
      for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) seed->item_reflect_damage_mul[slot] = 1.0f;
    }
    for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
      const MslItem* item = &items[slot];
      if (item->exists == 0u) continue;
      int active_idx = -1;
      for (int k = 0; k < MSL_MAX_ITEMS; k++) {
        if (active_valid[k] && active_spawn[k] == item->spawn_id && active_type[k] == item->type) {
          active_idx = k;
          break;
        }
      }
      if (active_idx < 0) {
        for (int k = 0; k < MSL_MAX_ITEMS; k++) {
          if (!active_valid[k]) {
            active_idx = k;
            break;
          }
        }
        if (active_idx < 0) active_idx = 0;
        active_valid[active_idx] = true;
        active_spawn[active_idx] = item->spawn_id;
        active_type[active_idx] = item->type;
        active_mul[active_idx] = 1.0f;
        active_owner[active_idx] = (int)item->owner;
        active_iid[active_idx] = (int)item->instance_id;
        active_vx[active_idx] = item->vel_x;
        active_vy[active_idx] = item->vel_y;
      }
      float mul = active_mul[active_idx];
      const int owner = (int)item->owner;
      const int prev_owner = active_owner[active_idx];
      const int prev_iid = active_iid[active_idx];
      const float prev_vx = active_vx[active_idx];
      const float prev_vy = active_vy[active_idx];
      const bool owner_changed = owner != prev_owner;
      const bool instance_transfer = (int)item->instance_id != prev_iid;
      const float prev_speed_sq = prev_vx * prev_vx + prev_vy * prev_vy;
      const float cur_speed_sq = item->vel_x * item->vel_x + item->vel_y * item->vel_y;
      const bool reversed_vel_same_owner =
          owner == prev_owner && !instance_transfer && prev_speed_sq > 1.0e-6f &&
          cur_speed_sq > 1.0e-6f && (prev_vx * item->vel_x + prev_vy * item->vel_y) < 0.0f &&
          fabsf(cur_speed_sq - prev_speed_sq) <= 0.25f * fmaxf(prev_speed_sq, cur_speed_sq);
      if ((owner_changed || instance_transfer || reversed_vel_same_owner) && owner >= 0 &&
          owner < players) {
        const npy_intp fighter_idx = (i * width) + owner;
        const uint16_t act = action_data[fighter_idx];
        const uint8_t flags3 = flags_data[(fighter_idx * flags_depth) + 3];
        if (act == 0x00B6u && (flags3 & 0x20u)) {
          mul = powershield_mul > 0.0 ? (float)powershield_mul : 1.0f;
        } else {
          const float char_mul = reflector[char_data[fighter_idx]];
          if (char_mul > 0.0f) mul = char_mul;
        }
      }
      active_mul[active_idx] = mul;
      active_owner[active_idx] = owner;
      active_iid[active_idx] = (int)item->instance_id;
      active_vx[active_idx] = item->vel_x;
      active_vy[active_idx] = item->vel_y;
      seen[active_idx] = true;
      if (seed != NULL) seed->item_reflect_damage_mul[slot] = mul;
    }
    for (int k = 0; k < MSL_MAX_ITEMS; k++) {
      if (active_valid[k] && !seen[k]) active_valid[k] = false;
    }
  }
  Py_RETURN_NONE;
}
