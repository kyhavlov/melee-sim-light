#ifndef MSL_VALIDATION_HISTORY_COMMON_H
#define MSL_VALIDATION_HISTORY_COMMON_H

#include "msl_preprocess.h"

#include "../src/action_ids.h"
#include "../src/api.h"
#include "../src/ids.h"

static inline PyArrayObject* vh_require(PyObject* obj, int typenum, int ndim, const char* name) {
  return require_contiguous_array(obj, typenum, ndim, name);
}

static inline int vh_check_len(PyArrayObject* arr, npy_intp n, const char* name) {
  if (PyArray_SIZE(arr) != n) {
    PyErr_Format(PyExc_ValueError, "%s length mismatch", name);
    return -1;
  }
  return 0;
}

static inline int vh_seed_rows(PyArrayObject* seed, npy_intp n_samples) {
  if (PyArray_NDIM(seed) != 2 || PyArray_DIM(seed, 0) != n_samples ||
      PyArray_DIM(seed, 1) < (npy_intp)sizeof(MslSeed)) {
    PyErr_SetString(PyExc_ValueError, "seed_u8 must be uint8[n_samples, >=sizeof(MslSeed)]");
    return -1;
  }
  return 0;
}

static inline MslSeed* vh_seed_at(uint8_t* base, size_t stride, npy_intp i) {
  return (MslSeed*)(void*)(base + (size_t)i * stride);
}

static inline const MslSeed* vh_seed_const_at(const uint8_t* base, size_t stride, npy_intp i) {
  return (const MslSeed*)(const void*)(base + (size_t)i * stride);
}

static inline const MslCompare* vh_ref_const_at(const uint8_t* base, size_t stride, npy_intp i) {
  return (const MslCompare*)(const void*)(base + (size_t)i * stride);
}

static inline bool msl_py_capture_attach_action(uint16_t a) {
  return a == 0x00DFu || a == 0x00E0u || a == 0x00E1u || a == 0x00E2u || a == 0x00E3u ||
         a == 0x00E4u;
}

static inline bool msl_py_capture_wait_action(uint16_t a) { return a == 0x00E0u || a == 0x00E3u; }

static inline bool msl_py_capture_damage_action(uint16_t a) { return a == 0x00E1u || a == 0x00E4u; }

static inline uint8_t msl_py_damage_action_any(uint16_t a) {
  return (a >= (uint16_t)MSL_ACT_DAMAGE_HI_1 && a <= (uint16_t)MSL_ACT_DAMAGE_FLY_ROLL) ? 1u : 0u;
}

static inline uint8_t msl_py_cliff_action_any(uint16_t a) {
  return (a == (uint16_t)MSL_ACT_CLIFF_CATCH || a == (uint16_t)MSL_ACT_CLIFF_WAIT ||
          a == (uint16_t)MSL_ACT_CLIFF_CLIMB_SLOW || a == (uint16_t)MSL_ACT_CLIFF_CLIMB_QUICK ||
          a == (uint16_t)MSL_ACT_CLIFF_ATTACK_SLOW || a == (uint16_t)MSL_ACT_CLIFF_ATTACK_QUICK ||
          a == (uint16_t)MSL_ACT_CLIFF_ESCAPE_SLOW || a == (uint16_t)MSL_ACT_CLIFF_ESCAPE_QUICK ||
          a == (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW1 || a == (uint16_t)MSL_ACT_CLIFF_JUMP_SLOW2 ||
          a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK1 || a == (uint16_t)MSL_ACT_CLIFF_JUMP_QUICK2)
             ? 1u
             : 0u;
}

#endif
