#define PY_SSIZE_T_CLEAN
#define PY_ARRAY_UNIQUE_SYMBOL MSL_BINDING_ARRAY_API
#define NO_IMPORT_ARRAY
#include <Python.h>

#include <stdint.h>
#include <string.h>

#include <numpy/arrayobject.h>

#include "msl_taxonomy_native.h"

#include "../src/api.h"

enum {
  MSL_TAX_FIELD_ACTION_ID = 0,
  MSL_TAX_FIELD_ACTION_FRAME = 1,
  MSL_TAX_FIELD_ON_GROUND = 2,
  MSL_TAX_FIELD_FACING = 3,
  MSL_TAX_FIELD_STOCKS = 4,
  MSL_TAX_FIELD_JUMPS_LEFT = 5,
  MSL_TAX_FIELD_IS_DEAD = 6,
  MSL_TAX_FIELD_HITLAG = 7,
  MSL_TAX_FIELD_HITSTUN = 8,
  MSL_TAX_FIELD_L_CANCEL = 9,
  MSL_TAX_FIELD_HURTBOX_STATE = 10,
  MSL_TAX_FIELD_GROUND_ID = 11,
  MSL_TAX_FIELD_ANIMATION_INDEX = 12,
  MSL_TAX_FIELD_INSTANCE_HIT_BY = 13,
  MSL_TAX_FIELD_INSTANCE_ID = 14,
  MSL_TAX_FIELD_LAST_ATTACK_LANDED = 15,
  MSL_TAX_FIELD_COMBO_COUNT = 16,
  MSL_TAX_FIELD_LAST_HIT_BY = 17,
  MSL_TAX_FIELD_STATE_FLAGS = 18,
  MSL_TAX_FIELD_ITEM_EXISTS = 19,
  MSL_TAX_FIELD_ITEM_TYPE = 20,
  MSL_TAX_FIELD_ITEM_STATE = 21,
  MSL_TAX_FIELD_ITEM_OWNER = 22,
  MSL_TAX_FIELD_ITEM_INSTANCE_ID = 23,
};

typedef struct {
  uint8_t* kind;
  int32_t* record;
  uint8_t* subject;
  uint8_t* field_code;
  int8_t* subindex;
  int32_t* seed_value;
  int32_t* ref_value;
  int32_t* out_value;
  int32_t* seed_frame;
  int32_t* ref_frame;
  uint16_t* seed_action_id;
  uint16_t* ref_action_id;
  uint16_t* out_action_id;
  uint16_t* prev_action_id;
  int16_t* seed_action_frame;
  int16_t* ref_action_frame;
  int16_t* out_action_frame;
  uint8_t* on_ground;
  uint16_t* hitlag;
  uint16_t* hitstun;
} MslTaxonomyEventArrays;

static PyArrayObject* tax_require_contiguous_u8_ro(PyObject* obj, int min_ndim, const char* name) {
  if (!PyObject_TypeCheck(obj, &PyArray_Type)) {
    PyErr_Format(PyExc_TypeError, "%s must be a NumPy array", name);
    return NULL;
  }
  PyArrayObject* arr = (PyArrayObject*)obj;
  if (!PyArray_ISCARRAY_RO(arr)) {
    PyErr_Format(PyExc_ValueError, "%s must be contiguous C-order", name);
    return NULL;
  }
  if (PyArray_TYPE(arr) != NPY_UINT8) {
    PyErr_Format(PyExc_ValueError, "%s has wrong dtype (expected uint8)", name);
    return NULL;
  }
  if (PyArray_NDIM(arr) < min_ndim) {
    PyErr_Format(PyExc_ValueError, "%s must be at least %dD", name, min_ndim);
    return NULL;
  }
  return arr;
}

static int32_t tax_seed_player_value(const MslSeed* seed, int p, int code, int sub) {
  switch (code) {
    case MSL_TAX_FIELD_ACTION_ID:
      return (int32_t)seed->action_id[p];
    case MSL_TAX_FIELD_ACTION_FRAME:
      return (int32_t)seed->action_frame[p];
    case MSL_TAX_FIELD_ON_GROUND:
      return (int32_t)seed->on_ground[p];
    case MSL_TAX_FIELD_FACING:
      return (int32_t)seed->facing[p];
    case MSL_TAX_FIELD_STOCKS:
      return (int32_t)seed->stocks[p];
    case MSL_TAX_FIELD_JUMPS_LEFT:
      return (int32_t)seed->jumps_left[p];
    case MSL_TAX_FIELD_IS_DEAD:
      return seed->stocks[p] == 0 ? 1 : 0;
    case MSL_TAX_FIELD_HITLAG:
      return (int32_t)seed->hitlag[p];
    case MSL_TAX_FIELD_HITSTUN:
      return (int32_t)seed->hitstun[p];
    case MSL_TAX_FIELD_L_CANCEL:
      return (int32_t)seed->l_cancel[p];
    case MSL_TAX_FIELD_HURTBOX_STATE:
      return (int32_t)seed->hurtbox_state[p];
    case MSL_TAX_FIELD_GROUND_ID:
      return (int32_t)seed->ground_id[p];
    case MSL_TAX_FIELD_ANIMATION_INDEX:
      return (int32_t)seed->animation_index[p];
    case MSL_TAX_FIELD_INSTANCE_HIT_BY:
      return (int32_t)seed->instance_hit_by[p];
    case MSL_TAX_FIELD_INSTANCE_ID:
      return (int32_t)seed->instance_id[p];
    case MSL_TAX_FIELD_LAST_ATTACK_LANDED:
      return (int32_t)seed->last_attack_landed[p];
    case MSL_TAX_FIELD_COMBO_COUNT:
      return (int32_t)seed->combo_count[p];
    case MSL_TAX_FIELD_LAST_HIT_BY:
      return (int32_t)seed->last_hit_by[p];
    case MSL_TAX_FIELD_STATE_FLAGS:
      return (sub >= 0 && sub < MSL_STATE_FLAGS_BYTES) ? (int32_t)seed->state_flags[p][sub] : 0;
    default:
      return 0;
  }
}

static int32_t tax_compare_player_value(const MslCompare* cmp, int p, int code, int sub) {
  switch (code) {
    case MSL_TAX_FIELD_ACTION_ID:
      return (int32_t)cmp->action_id[p];
    case MSL_TAX_FIELD_ACTION_FRAME:
      return (int32_t)cmp->action_frame[p];
    case MSL_TAX_FIELD_ON_GROUND:
      return (int32_t)cmp->on_ground[p];
    case MSL_TAX_FIELD_FACING:
      return (int32_t)cmp->facing[p];
    case MSL_TAX_FIELD_STOCKS:
      return (int32_t)cmp->stocks[p];
    case MSL_TAX_FIELD_JUMPS_LEFT:
      return (int32_t)cmp->jumps_left[p];
    case MSL_TAX_FIELD_IS_DEAD:
      return (int32_t)cmp->is_dead[p];
    case MSL_TAX_FIELD_HITLAG:
      return (int32_t)cmp->hitlag[p];
    case MSL_TAX_FIELD_HITSTUN:
      return (int32_t)cmp->hitstun[p];
    case MSL_TAX_FIELD_L_CANCEL:
      return (int32_t)cmp->l_cancel[p];
    case MSL_TAX_FIELD_HURTBOX_STATE:
      return (int32_t)cmp->hurtbox_state[p];
    case MSL_TAX_FIELD_GROUND_ID:
      return (int32_t)cmp->ground_id[p];
    case MSL_TAX_FIELD_ANIMATION_INDEX:
      return (int32_t)cmp->animation_index[p];
    case MSL_TAX_FIELD_INSTANCE_HIT_BY:
      return (int32_t)cmp->instance_hit_by[p];
    case MSL_TAX_FIELD_INSTANCE_ID:
      return (int32_t)cmp->instance_id[p];
    case MSL_TAX_FIELD_LAST_ATTACK_LANDED:
      return (int32_t)cmp->last_attack_landed[p];
    case MSL_TAX_FIELD_COMBO_COUNT:
      return (int32_t)cmp->combo_count[p];
    case MSL_TAX_FIELD_LAST_HIT_BY:
      return (int32_t)cmp->last_hit_by[p];
    case MSL_TAX_FIELD_STATE_FLAGS:
      return (sub >= 0 && sub < MSL_STATE_FLAGS_BYTES) ? (int32_t)cmp->state_flags[p][sub] : 0;
    default:
      return 0;
  }
}

static int32_t tax_seed_item_value(const MslSeed* seed, int slot, int code) {
  const MslItem* item = &seed->items[slot];
  switch (code) {
    case MSL_TAX_FIELD_ITEM_EXISTS:
      return (int32_t)item->exists;
    case MSL_TAX_FIELD_ITEM_TYPE:
      return (int32_t)item->type;
    case MSL_TAX_FIELD_ITEM_STATE:
      return (int32_t)item->state;
    case MSL_TAX_FIELD_ITEM_OWNER:
      return (int32_t)item->owner;
    case MSL_TAX_FIELD_ITEM_INSTANCE_ID:
      return (int32_t)item->instance_id;
    default:
      return 0;
  }
}

static int32_t tax_compare_item_value(const MslCompare* cmp, int slot, int code) {
  const MslItem* item = &cmp->items[slot];
  switch (code) {
    case MSL_TAX_FIELD_ITEM_EXISTS:
      return (int32_t)item->exists;
    case MSL_TAX_FIELD_ITEM_TYPE:
      return (int32_t)item->type;
    case MSL_TAX_FIELD_ITEM_STATE:
      return (int32_t)item->state;
    case MSL_TAX_FIELD_ITEM_OWNER:
      return (int32_t)item->owner;
    case MSL_TAX_FIELD_ITEM_INSTANCE_ID:
      return (int32_t)item->instance_id;
    default:
      return 0;
  }
}

static void tax_append_player_event(MslTaxonomyEventArrays* arrays, npy_intp idx, int record, int p,
                                    int field_code, int subindex, const MslSeed* seed,
                                    const MslCompare* ref, const MslCompare* out) {
  arrays->kind[idx] = 0;
  arrays->record[idx] = (int32_t)record;
  arrays->subject[idx] = (uint8_t)p;
  arrays->field_code[idx] = (uint8_t)field_code;
  arrays->subindex[idx] = (int8_t)subindex;
  arrays->seed_value[idx] = tax_seed_player_value(seed, p, field_code, subindex);
  arrays->ref_value[idx] = tax_compare_player_value(ref, p, field_code, subindex);
  arrays->out_value[idx] = tax_compare_player_value(out, p, field_code, subindex);
  arrays->seed_frame[idx] = seed->frame_id;
  arrays->ref_frame[idx] = ref->frame_id;
  arrays->seed_action_id[idx] = seed->action_id[p];
  arrays->ref_action_id[idx] = ref->action_id[p];
  arrays->out_action_id[idx] = out->action_id[p];
  arrays->prev_action_id[idx] = seed->seed_prev_action_id[p];
  arrays->seed_action_frame[idx] = seed->action_frame[p];
  arrays->ref_action_frame[idx] = ref->action_frame[p];
  arrays->out_action_frame[idx] = out->action_frame[p];
  arrays->on_ground[idx] = seed->on_ground[p];
  arrays->hitlag[idx] = seed->hitlag[p];
  arrays->hitstun[idx] = seed->hitstun[p];
}

static void tax_append_item_event(MslTaxonomyEventArrays* arrays, npy_intp idx, int record,
                                  int slot, int field_code, const MslSeed* seed,
                                  const MslCompare* ref, const MslCompare* out) {
  arrays->kind[idx] = 1;
  arrays->record[idx] = (int32_t)record;
  arrays->subject[idx] = (uint8_t)slot;
  arrays->field_code[idx] = (uint8_t)field_code;
  arrays->subindex[idx] = -1;
  arrays->seed_value[idx] = tax_seed_item_value(seed, slot, field_code);
  arrays->ref_value[idx] = tax_compare_item_value(ref, slot, field_code);
  arrays->out_value[idx] = tax_compare_item_value(out, slot, field_code);
  arrays->seed_frame[idx] = seed->frame_id;
  arrays->ref_frame[idx] = ref->frame_id;
  arrays->seed_action_id[idx] = UINT16_MAX;
  arrays->ref_action_id[idx] = UINT16_MAX;
  arrays->out_action_id[idx] = UINT16_MAX;
  arrays->prev_action_id[idx] = UINT16_MAX;
  arrays->seed_action_frame[idx] = INT16_MIN;
  arrays->ref_action_frame[idx] = INT16_MIN;
  arrays->out_action_frame[idx] = INT16_MIN;
  arrays->on_ground[idx] = UINT8_MAX;
  arrays->hitlag[idx] = UINT16_MAX;
  arrays->hitstun[idx] = UINT16_MAX;
}

static npy_intp tax_scan_mismatches(const uint8_t* seed_bytes, const uint8_t* ref_bytes,
                                    const uint8_t* out_bytes, npy_intp n, int num_players,
                                    MslTaxonomyEventArrays* arrays) {
  static const int player_scalar_fields[] = {
      MSL_TAX_FIELD_ACTION_ID,       MSL_TAX_FIELD_ACTION_FRAME,
      MSL_TAX_FIELD_ON_GROUND,       MSL_TAX_FIELD_FACING,
      MSL_TAX_FIELD_STOCKS,          MSL_TAX_FIELD_JUMPS_LEFT,
      MSL_TAX_FIELD_IS_DEAD,         MSL_TAX_FIELD_HITLAG,
      MSL_TAX_FIELD_HITSTUN,         MSL_TAX_FIELD_L_CANCEL,
      MSL_TAX_FIELD_HURTBOX_STATE,   MSL_TAX_FIELD_GROUND_ID,
      MSL_TAX_FIELD_ANIMATION_INDEX, MSL_TAX_FIELD_INSTANCE_HIT_BY,
      MSL_TAX_FIELD_INSTANCE_ID,     MSL_TAX_FIELD_LAST_ATTACK_LANDED,
      MSL_TAX_FIELD_COMBO_COUNT,     MSL_TAX_FIELD_LAST_HIT_BY,
  };
  static const int item_fields[] = {
      MSL_TAX_FIELD_ITEM_EXISTS, MSL_TAX_FIELD_ITEM_TYPE,        MSL_TAX_FIELD_ITEM_STATE,
      MSL_TAX_FIELD_ITEM_OWNER,  MSL_TAX_FIELD_ITEM_INSTANCE_ID,
  };
  npy_intp count = 0;
  for (npy_intp i = 0; i < n; i++) {
    const MslSeed* seed = (const MslSeed*)(const void*)(seed_bytes + (i * sizeof(MslSeed)));
    const MslCompare* ref = (const MslCompare*)(const void*)(ref_bytes + (i * sizeof(MslCompare)));
    const MslCompare* out = (const MslCompare*)(const void*)(out_bytes + (i * sizeof(MslCompare)));
    for (int p = 0; p < num_players; p++) {
      for (size_t f = 0; f < sizeof(player_scalar_fields) / sizeof(player_scalar_fields[0]); f++) {
        const int code = player_scalar_fields[f];
        if (tax_compare_player_value(out, p, code, -1) !=
            tax_compare_player_value(ref, p, code, -1)) {
          if (arrays != NULL) {
            tax_append_player_event(arrays, count, (int)i, p, code, -1, seed, ref, out);
          }
          count++;
        }
      }
      for (int sub = 0; sub < MSL_STATE_FLAGS_BYTES; sub++) {
        if (out->state_flags[p][sub] != ref->state_flags[p][sub]) {
          if (arrays != NULL) {
            tax_append_player_event(arrays, count, (int)i, p, MSL_TAX_FIELD_STATE_FLAGS, sub, seed,
                                    ref, out);
          }
          count++;
        }
      }
    }
    for (int slot = 0; slot < MSL_MAX_ITEMS; slot++) {
      for (size_t f = 0; f < sizeof(item_fields) / sizeof(item_fields[0]); f++) {
        const int code = item_fields[f];
        if (tax_compare_item_value(out, slot, code) != tax_compare_item_value(ref, slot, code)) {
          if (arrays != NULL) {
            tax_append_item_event(arrays, count, (int)i, slot, code, seed, ref, out);
          }
          count++;
        }
      }
    }
  }
  return count;
}

PyObject* msl_collect_mismatch_events(PyObject* self, PyObject* args) {
  (void)self;
  PyObject* seed_obj;
  PyObject* ref_obj;
  PyObject* out_obj;
  int num_players;
  if (!PyArg_ParseTuple(args, "OOOi", &seed_obj, &ref_obj, &out_obj, &num_players)) {
    return NULL;
  }
  PyArrayObject* seed = tax_require_contiguous_u8_ro(seed_obj, 2, "seed_bytes");
  if (seed == NULL) return NULL;
  PyArrayObject* ref = tax_require_contiguous_u8_ro(ref_obj, 2, "ref_bytes");
  if (ref == NULL) return NULL;
  PyArrayObject* out = tax_require_contiguous_u8_ro(out_obj, 2, "out_bytes");
  if (out == NULL) return NULL;
  if (PyArray_NDIM(seed) != 2 || PyArray_NDIM(ref) != 2 || PyArray_NDIM(out) != 2) {
    PyErr_SetString(PyExc_ValueError, "seed_bytes/ref_bytes/out_bytes must be 2D uint8 arrays");
    return NULL;
  }
  const npy_intp n = PyArray_DIM(seed, 0);
  if (PyArray_DIM(ref, 0) != n || PyArray_DIM(out, 0) != n) {
    PyErr_SetString(PyExc_ValueError,
                    "seed_bytes/ref_bytes/out_bytes must have the same row count");
    return NULL;
  }
  if (PyArray_DIM(seed, 1) != (npy_intp)sizeof(MslSeed) ||
      PyArray_DIM(ref, 1) != (npy_intp)sizeof(MslCompare) ||
      PyArray_DIM(out, 1) != (npy_intp)sizeof(MslCompare)) {
    PyErr_Format(PyExc_ValueError, "byte strides must match native structs: seed=%zd compare=%zd",
                 (Py_ssize_t)sizeof(MslSeed), (Py_ssize_t)sizeof(MslCompare));
    return NULL;
  }
  if (num_players < 1 || num_players > MSL_MAX_PLAYERS) {
    PyErr_SetString(PyExc_ValueError, "num_players out of range");
    return NULL;
  }

  const uint8_t* seed_bytes = (const uint8_t*)PyArray_DATA(seed);
  const uint8_t* ref_bytes = (const uint8_t*)PyArray_DATA(ref);
  const uint8_t* out_bytes = (const uint8_t*)PyArray_DATA(out);
  const npy_intp count =
      tax_scan_mismatches(seed_bytes, ref_bytes, out_bytes, n, num_players, NULL);

  npy_intp dims[1] = {count};
  MslTaxonomyEventArrays arrays;
  memset(&arrays, 0, sizeof(arrays));
  PyObject* kind_obj = NULL;
  PyObject* record_obj = NULL;
  PyObject* subject_obj = NULL;
  PyObject* field_code_obj = NULL;
  PyObject* subindex_obj = NULL;
  PyObject* seed_value_obj = NULL;
  PyObject* ref_value_obj = NULL;
  PyObject* out_value_obj = NULL;
  PyObject* seed_frame_obj = NULL;
  PyObject* ref_frame_obj = NULL;
  PyObject* seed_action_id_obj = NULL;
  PyObject* ref_action_id_obj = NULL;
  PyObject* out_action_id_obj = NULL;
  PyObject* prev_action_id_obj = NULL;
  PyObject* seed_action_frame_obj = NULL;
  PyObject* ref_action_frame_obj = NULL;
  PyObject* out_action_frame_obj = NULL;
  PyObject* on_ground_obj = NULL;
  PyObject* hitlag_obj = NULL;
  PyObject* hitstun_obj = NULL;
#define MSL_TAX_ALLOC_ARRAY(NAME, TYPE)          \
  NAME##_obj = PyArray_SimpleNew(1, dims, TYPE); \
  if (NAME##_obj == NULL) goto fail;             \
  arrays.NAME = (void*)PyArray_DATA((PyArrayObject*)NAME##_obj)

  MSL_TAX_ALLOC_ARRAY(kind, NPY_UINT8);
  MSL_TAX_ALLOC_ARRAY(record, NPY_INT32);
  MSL_TAX_ALLOC_ARRAY(subject, NPY_UINT8);
  MSL_TAX_ALLOC_ARRAY(field_code, NPY_UINT8);
  MSL_TAX_ALLOC_ARRAY(subindex, NPY_INT8);
  MSL_TAX_ALLOC_ARRAY(seed_value, NPY_INT32);
  MSL_TAX_ALLOC_ARRAY(ref_value, NPY_INT32);
  MSL_TAX_ALLOC_ARRAY(out_value, NPY_INT32);
  MSL_TAX_ALLOC_ARRAY(seed_frame, NPY_INT32);
  MSL_TAX_ALLOC_ARRAY(ref_frame, NPY_INT32);
  MSL_TAX_ALLOC_ARRAY(seed_action_id, NPY_UINT16);
  MSL_TAX_ALLOC_ARRAY(ref_action_id, NPY_UINT16);
  MSL_TAX_ALLOC_ARRAY(out_action_id, NPY_UINT16);
  MSL_TAX_ALLOC_ARRAY(prev_action_id, NPY_UINT16);
  MSL_TAX_ALLOC_ARRAY(seed_action_frame, NPY_INT16);
  MSL_TAX_ALLOC_ARRAY(ref_action_frame, NPY_INT16);
  MSL_TAX_ALLOC_ARRAY(out_action_frame, NPY_INT16);
  MSL_TAX_ALLOC_ARRAY(on_ground, NPY_UINT8);
  MSL_TAX_ALLOC_ARRAY(hitlag, NPY_UINT16);
  MSL_TAX_ALLOC_ARRAY(hitstun, NPY_UINT16);
#undef MSL_TAX_ALLOC_ARRAY

  const npy_intp filled =
      tax_scan_mismatches(seed_bytes, ref_bytes, out_bytes, n, num_players, &arrays);
  if (filled != count) {
    PyErr_SetString(PyExc_RuntimeError, "taxonomy mismatch scan count changed between passes");
    goto fail;
  }

  PyObject* result = PyDict_New();
  if (result == NULL) goto fail;
#define MSL_TAX_SET(NAME)                                                    \
  if (PyDict_SetItemString(result, #NAME, NAME##_obj) < 0) goto fail_result; \
  Py_DECREF(NAME##_obj);                                                     \
  NAME##_obj = NULL

  MSL_TAX_SET(kind);
  MSL_TAX_SET(record);
  MSL_TAX_SET(subject);
  MSL_TAX_SET(field_code);
  MSL_TAX_SET(subindex);
  MSL_TAX_SET(seed_value);
  MSL_TAX_SET(ref_value);
  MSL_TAX_SET(out_value);
  MSL_TAX_SET(seed_frame);
  MSL_TAX_SET(ref_frame);
  MSL_TAX_SET(seed_action_id);
  MSL_TAX_SET(ref_action_id);
  MSL_TAX_SET(out_action_id);
  MSL_TAX_SET(prev_action_id);
  MSL_TAX_SET(seed_action_frame);
  MSL_TAX_SET(ref_action_frame);
  MSL_TAX_SET(out_action_frame);
  MSL_TAX_SET(on_ground);
  MSL_TAX_SET(hitlag);
  MSL_TAX_SET(hitstun);
#undef MSL_TAX_SET
  return result;

fail_result:
  Py_DECREF(result);
fail:
  Py_XDECREF(kind_obj);
  Py_XDECREF(record_obj);
  Py_XDECREF(subject_obj);
  Py_XDECREF(field_code_obj);
  Py_XDECREF(subindex_obj);
  Py_XDECREF(seed_value_obj);
  Py_XDECREF(ref_value_obj);
  Py_XDECREF(out_value_obj);
  Py_XDECREF(seed_frame_obj);
  Py_XDECREF(ref_frame_obj);
  Py_XDECREF(seed_action_id_obj);
  Py_XDECREF(ref_action_id_obj);
  Py_XDECREF(out_action_id_obj);
  Py_XDECREF(prev_action_id_obj);
  Py_XDECREF(seed_action_frame_obj);
  Py_XDECREF(ref_action_frame_obj);
  Py_XDECREF(out_action_frame_obj);
  Py_XDECREF(on_ground_obj);
  Py_XDECREF(hitlag_obj);
  Py_XDECREF(hitstun_obj);
  return NULL;
}
